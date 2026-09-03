// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include "mtk_debug.h"
#include "mtk_debugfs.h"
#include "mtk_port.h"
#include "mtk_port_io.h"
#ifdef CONFIG_TX00_UT_PORT
#include "ut_port_fake.h"
#endif

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#include "pcie/link-exception.h"
#endif

#define TAG					"PORT"

#define MTK_PORT_ENUM_VER			(0)
/* this is an empirical value, negotiate with device designer */
#define MTK_PORT_ENUM_HEAD_PATTERN		(0x5a5a5a5a)
#define MTK_PORT_ENUM_TAIL_PATTERN		(0xa5a5a5a5)
#define MTK_DFLT_TRB_TIMEOUT			(5 * HZ)
#define MTK_DFLT_TRB_STATUS			(0x1)
#define MTK_TRB_HEADER_ADDED			(0xADDED)
#define PORT_ATTR_DIR_NAME_LEN			(64)
#define MTK_CHECK_RX_SEQ_MASK			(0x7fff)
#define MTK_PORT_NON_DIPC			BIT(1)
#define MTK_PORT_FLOW_CTRL			BIT(2)
#define MTK_FLOWCTRL_TIMEOUT_CNT_MAX		2
#define PORT_DUMP_NAME_MAX_LEN			(32)
#define PORT_TX_TRIGGER_ACTION_CNT		(2)
#define LB_PORT_DL_MAX_CNT			(200000)

#define MTK_PORT_SEARCH_FROM_RADIX_TREE(p, s) ({\
	struct mtk_port *_p;			\
	_p = rcu_dereference_raw(*(s));		\
	if (!_p)				\
		continue;			\
	p = _p;					\
})

#define MTK_PORT_INTERNAL_NODE_CHECK(p, s, i) ({\
	if (radix_tree_is_internal_node(p)) {	\
		s = radix_tree_iter_retry(&(i));\
		continue;			\
	}					\
})

enum misc_ctrl_msg_id {
	CTRL_MSG_SET_LB_MODE		= 0,
	CTRL_MSG_ACK_LB_MODE		= 1,
};

enum lb_mode_id {
	LB_MODE = 0x1,
	UL_MODE = 0x2,
	DL_MODE = 0x3,
};

struct ctrl_msg_header {
	__le32 id;
	__le32 ex_msg;
	__le32 data_len;
	u8 reserved[];
} __packed;

struct lb_ctrl_msg {
	__le32 mode_id;
	__le32 dl_pkt_num;
} __packed;

/* global group for stale ports */
static LIST_HEAD(stale_list_grp);
/* mutex lock for stale_list_group */
DEFINE_MUTEX(port_mngr_grp_mtx);

static DEFINE_IDA(ccci_dev_ids);

/* For debug priprietary loopback port */
static bool proprietary_test;
/* For debug wwan loopback port */
static bool wwan_loopback_test;
/* For debug relayfs loopback port */
static bool relayfs_loopback_test;
/* For port dump enable */
static bool port_dump_enable = true;

/* For different scenarios, change the type of specific ports */
static void mtk_port_type_change(struct mtk_port_mngr *port_mngr, struct mtk_port *port)
{
	if (port->info.rx_ch == CCCI_LB_IT_RX) {
		if (proprietary_test) {
			port->info.type = PORT_TYPE_PROPRIETARY;
			MTK_INFO(port_mngr->ctrl_blk->mdev,
				 "Port(%s) change to TYPE_PROPRIETARY\n", port->info.name);
		} else if (wwan_loopback_test) {
			port->info.type = PORT_TYPE_WWAN;
			MTK_INFO(port_mngr->ctrl_blk->mdev,
				 "Port(%s) change to TYPE_WWAN\n", port->info.name);
		} else if (relayfs_loopback_test) {
			port->info.type = PORT_TYPE_RELAYFS;
			MTK_INFO(port_mngr->ctrl_blk->mdev,
				 "Port(%s) change to RELAYFS\n", port->info.name);
		}
	}
#ifdef CONFIG_MTK_DEVLINK_COREDUMP_SUPPORT
	if (port->info.rx_ch == DUMP_INDEX_RX) {
		port->info.type = PORT_TYPE_INTERNAL;
		MTK_INFO(port_mngr->ctrl_blk->mdev,
			 "Port(%s) change to TYPE_INTERNAL\n", port->info.name);
	}
#endif

#ifdef CONFIG_MTK_DEVLINK_FLASH_SUPPORT
	if (port->info.rx_ch == FBOOT_INDEX_RX) {
		port->info.type = PORT_TYPE_INTERNAL;
		MTK_INFO(port_mngr->ctrl_blk->mdev,
			 "Port(%s) change to TYPE_INTERNAL\n", port->info.name);
	}
#endif
}

/* This function working always under mutex lock port_mngr_grp_mtx */
void mtk_port_release(struct kref *port_kref)
{
	struct mtk_stale_list *s_list;
	struct mtk_port *port;

	port = container_of(port_kref, struct mtk_port, kref);
	/* The port on stale list also be deleted when release this port */
	if (!test_bit(PORT_S_ON_STALE_LIST, &port->status))
		goto port_exit;

	list_del(&port->stale_entry);
	list_for_each_entry(s_list, &stale_list_grp, entry) {
		/* If this port is the last port of stale list, free the list and dev_id */
		if (!strncmp(s_list->dev_str, port->dev_str, MTK_DEV_STR_LEN) &&
		    list_empty(&s_list->ports) && s_list->dev_id >= 0) {
			pr_info("Free dev id of stale list(%s)\n", s_list->dev_str);
			ida_free(&ccci_dev_ids, s_list->dev_id);
			s_list->dev_id = -1;
			break;
		}
	}
port_exit:
	ports_ops[port->info.type]->exit(port);
	kfree(port);
}

static int mtk_port_tbl_add(struct mtk_port_mngr *port_mngr, struct mtk_port *port)
{
	int tbl_type;
	int ret;

	tbl_type = MTK_PORT_TBL_TYPE(port->info.rx_ch);
	if (tbl_type < PORT_TBL_SAP || tbl_type >= PORT_TBL_MAX) {
		MTK_WARN(port_mngr->ctrl_blk->mdev,
			 "Invalid tbl_type (%d) for port(%s), rx_ch: 0x%x\n",
			 tbl_type, port->info.name, port->info.rx_ch);
		return -EINVAL;
	}

	ret = radix_tree_insert(&port_mngr->port_tbl[tbl_type],
				port->info.rx_ch & 0xFFF, port);
	if (ret) {
		dev_err(port_mngr->ctrl_blk->mdev->dev,
			"port(%s) add to port_tbl failed, return %d\n",
			port->info.name, ret);
	} else {
		port_mngr->port_cnt++;
		if (!port_dump_enable)
			port->info.flags &= ~PORT_F_DUMP;
	}

	return ret;
}

static void mtk_port_tbl_del(struct mtk_port_mngr *port_mngr, struct mtk_port *port)
{
	radix_tree_delete(&port_mngr->port_tbl[MTK_PORT_TBL_TYPE(port->info.rx_ch)],
			  port->info.rx_ch & 0xFFF);
	port_mngr->port_cnt--;
}

static struct mtk_port *mtk_port_restore_from_stale_list(struct mtk_port_mngr *port_mngr,
							 struct mtk_stale_list *s_list)
{
	struct mtk_port *port, *next_port;
	int ret;

	mutex_lock(&port_mngr_grp_mtx);
	list_for_each_entry_safe(port, next_port, &s_list->ports, stale_entry) {
		kref_get(&port->kref);
		list_del(&port->stale_entry);
		ret = mtk_port_tbl_add(port_mngr, port);
		if (ret) {
			list_add_tail(&port->stale_entry, &s_list->ports);
			kref_put(&port->kref, mtk_port_release);
			mutex_unlock(&port_mngr_grp_mtx);
			dev_err(port_mngr->ctrl_blk->mdev->dev,
				"Failed when adding (%s) to port mngr\n",
				port->info.name);
			return ERR_PTR(ret);
		}

		port->port_mngr = port_mngr;
		clear_bit(PORT_S_ON_STALE_LIST, &port->status);
		ports_ops[port->info.type]->reset(port);
	}
	mutex_unlock(&port_mngr_grp_mtx);

	return NULL;
}

static struct mtk_port *mtk_port_alloc_and_add(struct mtk_port_mngr *port_mngr,
					       struct mtk_port_cfg *dflt_info)
{
	struct mtk_port *port;
	int ret;

	/* This memory will be free in function "mtk_port_release", if
	 * "mtk_port_release" called by mtk_port_stale_list_grp_cleanup,
	 * we can't use "devm_free" due to no dev(struct device) entity.
	 */
	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port) {
		ret = -ENOMEM;
		goto err_alloc_port;
	}
	memcpy(&port->info, dflt_info, sizeof(*dflt_info));

	mtk_port_type_change(port_mngr, port);

	ret = mtk_port_tbl_add(port_mngr, port);
	if (ret < 0) {
		dev_err(port_mngr->ctrl_blk->mdev->dev,
			"Failed to add port(%s) to port tbl\n", dflt_info->name);
		goto err_free_port;
	}

	port->port_mngr = port_mngr;
	ret = ports_ops[port->info.type]->init(port);
	if (ret < 0) {
		mtk_port_tbl_del(port_mngr, port);
		goto err_free_port;
	}

	memcpy(port->dev_str, port_mngr->ctrl_blk->mdev->dev_str, MTK_DEV_STR_LEN);
	return port;

err_free_port:
	kfree(port);
err_alloc_port:
	return ERR_PTR(ret);
}

static struct mtk_port *mtk_port_alloc_with_info(struct mtk_port_mngr *port_mngr,
						 struct mtk_port_cfg_ch_info *ch_info)
{
	struct mtk_port *port;
	int len;

	/* This memory will be free in function "mtk_port_release",
	 * if "mtk_port_release" called by mtk_port_stale_list_grp_cleanup,
	 * we can't use "devm_free" due to no dev(struct device) entity.
	 */
	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port) {
		MTK_ERR(port_mngr->ctrl_blk->mdev,
			"Failed to alloc memory for port(%s)\n", ch_info->port_name);
		return ERR_PTR(-ENOMEM);
	}
	MTK_HEX_DUMP(port_mngr->ctrl_blk->mdev,
		     MTK_DBG_PORT, MTK_MEMLOG_RG_CTRL_DUMP,
		     "Dumping port_cfg_ch_info:", ch_info, sizeof(*ch_info));
	port->info.rx_ch = le16_to_cpu(ch_info->dl_ch_id);
	port->info.tx_ch = le16_to_cpu(ch_info->ul_ch_id);
	port->info.tx_dump_cnt = MTK_DFLT_TX_DUMP_CNT;
	port->info.rx_dump_cnt = MTK_DFLT_RX_DUMP_CNT;
	port->info.flags |= PORT_F_DUMP;
	memcpy(port->dev_str, port_mngr->ctrl_blk->mdev->dev_str, MTK_DEV_STR_LEN);
	len = ch_info->port_name_len;
	if (len >= MTK_DFLT_PORT_NAME_LEN)
		len = MTK_DFLT_PORT_NAME_LEN - 1;
	strncpy(port->info.name, ch_info->port_name, len);

	if (strnstr(port->info.name, "FlowCtrl", MTK_DFLT_PORT_NAME_LEN) ||
	    strnstr(port->info.name, "MiscCtrl", MTK_DFLT_PORT_NAME_LEN))
		port->info.type = PORT_TYPE_INTERNAL;
	else
		port->info.type = PORT_TYPE_CHAR;

	return port;
}

static void mtk_port_free_or_backup(struct mtk_port_mngr *port_mngr,
				    struct mtk_port *port, struct mtk_stale_list *s_list)
{
	mutex_lock(&port_mngr_grp_mtx);

	mtk_port_tbl_del(port_mngr, port);
	if (port->info.type != PORT_TYPE_INTERNAL && s_list) {
		if (test_bit(PORT_S_OPEN, &port->status)) {
			/* backup: move using ports to stale list, for no need to
			 * re-open ports after remove and plug-in device again
			 */
			list_add_tail(&port->stale_entry, &s_list->ports);
			set_bit(PORT_S_ON_STALE_LIST, &port->status);
			MTK_INFO(port->port_mngr->ctrl_blk->mdev,
				 "Port(%s) move to stale list\n", port->info.name);
			memcpy(port->dev_str, port_mngr->ctrl_blk->mdev->dev_str, MTK_DEV_STR_LEN);
			port->port_mngr = NULL;
		}
		kref_put(&port->kref, mtk_port_release);
	} else {
		mtk_port_release(&port->kref);
	}

	mutex_unlock(&port_mngr_grp_mtx);
}

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
struct mtk_port *mtk_port_search_by_id(struct mtk_port_mngr *port_mngr, int rx_ch)
#else
static struct mtk_port *mtk_port_search_by_id(struct mtk_port_mngr *port_mngr, int rx_ch)
#endif
{
	int tbl_type = MTK_PORT_TBL_TYPE(rx_ch);

	if (tbl_type < PORT_TBL_SAP || tbl_type >= PORT_TBL_MAX)
		return NULL;

	return radix_tree_lookup(&port_mngr->port_tbl[tbl_type], MTK_CH_ID(rx_ch));
}

struct mtk_port *mtk_port_search_by_name(struct mtk_port_mngr *port_mngr, char *name)
{
	int tbl_type = PORT_TBL_SAP;
	struct radix_tree_iter iter;
	struct mtk_port *port;
	void __rcu **slot;

	do {
		radix_tree_for_each_slot(slot, &port_mngr->port_tbl[tbl_type], &iter, 0) {
			MTK_PORT_SEARCH_FROM_RADIX_TREE(port, slot);
			MTK_PORT_INTERNAL_NODE_CHECK(port, slot, iter);
			if (!strncmp(port->info.name, name, MTK_DFLT_PORT_NAME_LEN))
				return port;
		}
		tbl_type++;
	} while (tbl_type < PORT_TBL_MAX);

	return NULL;
}

static int mtk_port_tbl_create(struct mtk_port_mngr *port_mngr, struct mtk_port_cfg *cfg,
			       const int port_cnt, struct mtk_stale_list *s_list)
{
	struct mtk_port_cfg *dflt_port;
	struct mtk_port *port;
	int i;

	INIT_RADIX_TREE(&port_mngr->port_tbl[PORT_TBL_SAP], GFP_KERNEL);
	INIT_RADIX_TREE(&port_mngr->port_tbl[PORT_TBL_MD], GFP_KERNEL);
	INIT_RADIX_TREE(&port_mngr->port_tbl[PORT_TBL_GNSS], GFP_KERNEL);

	mtk_port_restore_from_stale_list(port_mngr, s_list);

	/* copy ports from static port cfg table */
	for (i = 0; i < port_cnt; i++) {
		dflt_port = cfg + i;
		if (!mtk_port_search_by_id(port_mngr, dflt_port->rx_ch)) {
			port = mtk_port_alloc_and_add(port_mngr, dflt_port);
			if (IS_ERR(port))
				return PTR_ERR(port);
		}
	}

	return 0;
}

static void mtk_port_tbl_destroy(struct mtk_port_mngr *port_mngr, struct mtk_stale_list *s_list)
{
	struct mtk_port **ports;
	int tbl_type;
	int ret, idx;

	/* Queue may be shared by multiple ports, we have to free or move the ports
	 * after all the ports on the Queue are closed.
	 */
	ports = kcalloc(port_mngr->port_cnt, sizeof(struct mtk_port *), GFP_KERNEL);
	if (!ports) {
		MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to alloc ports\n");
		return;
	}

	tbl_type = PORT_TBL_SAP;
	do {
		ret = radix_tree_gang_lookup(&port_mngr->port_tbl[tbl_type],
					     (void **)ports, 0, port_mngr->port_cnt);
		for (idx = 0; idx < ret; idx++)
			ports_ops[ports[idx]->info.type]->disable(ports[idx]);
		for (idx = 0; idx < ret; idx++)
			mtk_port_free_or_backup(port_mngr, ports[idx], s_list);
	} while (++tbl_type < PORT_TBL_MAX);
	kfree(ports);
}

static struct mtk_stale_list *mtk_port_stale_list_create(struct mtk_ctrl_blk *ctrl_blk)
{
	struct mtk_stale_list *s_list;

	/* can not use devm_kzalloc here, because should pair with the free operation which
	 * may be no dev pointer.
	 */
	s_list = kzalloc(sizeof(*s_list), GFP_KERNEL);
	if (!s_list)
		return NULL;

	memcpy(s_list->dev_str, ctrl_blk->mdev->dev_str, MTK_DEV_STR_LEN);
	s_list->dev_id = -1;
	INIT_LIST_HEAD(&s_list->ports);
	rwlock_init(&s_list->port_mngr_lock);

	mutex_lock(&port_mngr_grp_mtx);
	list_add_tail(&s_list->entry, &stale_list_grp);
	mutex_unlock(&port_mngr_grp_mtx);

	return s_list;
}

static void mtk_port_stale_list_destroy(struct mtk_stale_list *s_list)
{
	mutex_lock(&port_mngr_grp_mtx);
	list_del(&s_list->entry);
	mutex_unlock(&port_mngr_grp_mtx);
	kfree(s_list);
}

static struct mtk_stale_list *mtk_port_stale_list_search(const char *dev_str)
{
	struct mtk_stale_list *tmp, *s_list = NULL;

	mutex_lock(&port_mngr_grp_mtx);
	list_for_each_entry(tmp, &stale_list_grp, entry) {
		if (!strncmp(tmp->dev_str, dev_str, MTK_DEV_STR_LEN)) {
			s_list = tmp;
			break;
		}
	}
	mutex_unlock(&port_mngr_grp_mtx);

	return s_list;
}

/**
 * mtk_port_stale_list_grp_cleanup() - free all stale lists and all ports on it.
 *
 * Context: This function will be called when driver will be removed. It will search all the stale
 * lists. For each stale list, it will free the stale ports, unregister the character device id
 * region and unregister tty driver structure.
 *
 * Return: No return value.
 */
void mtk_port_stale_list_grp_cleanup(void)
{
	struct mtk_stale_list *s_list, *next_s_list;
	struct mtk_port *port, *next_port;

	mutex_lock(&port_mngr_grp_mtx);
	list_for_each_entry_safe(s_list, next_s_list, &stale_list_grp, entry) {
		pr_err("Clean stale list of dev_str(%s)\n", s_list->dev_str);
		list_del(&s_list->entry);

		list_for_each_entry_safe(port, next_port, &s_list->ports, stale_entry) {
			list_del(&port->stale_entry);
			mtk_port_release(&port->kref);
		}

		/* can't use devm_kfree, because the port is free,
		 * can't use port to get dev pointer
		 */
		kfree(s_list);
	}
	mutex_unlock(&port_mngr_grp_mtx);
}

static struct mtk_stale_list *mtk_port_stale_list_init(struct mtk_ctrl_blk *ctrl_blk, int *dev_id)
{
	struct mtk_stale_list *s_list;

	s_list = mtk_port_stale_list_search(ctrl_blk->mdev->dev_str);
	if (!s_list) {
		MTK_INFO(ctrl_blk->mdev, "Create stale list\n");
		s_list = mtk_port_stale_list_create(ctrl_blk);
		if (unlikely(!s_list))
			return NULL;
	} else {
		MTK_INFO(ctrl_blk->mdev, "Reuse old stale list\n");
	}

	mutex_lock(&port_mngr_grp_mtx);
	if (s_list->dev_id < 0) {
		*dev_id = ida_alloc_range(&ccci_dev_ids, 0, MTK_DFLT_MAX_DEV_CNT - 1, GFP_KERNEL);
	} else {
		*dev_id = s_list->dev_id;
		s_list->dev_id = -1;
	}
	mutex_unlock(&port_mngr_grp_mtx);

	return s_list;
}

static void mtk_port_stale_list_exit(struct mtk_ctrl_blk *ctrl_blk, struct mtk_stale_list *s_list,
				     int dev_id)
{
	if (!s_list)
		return;
	mutex_lock(&port_mngr_grp_mtx);
	if (list_empty(&s_list->ports)) {
		ida_free(&ccci_dev_ids, dev_id);
		mutex_unlock(&port_mngr_grp_mtx);
		mtk_port_stale_list_destroy(s_list);
		MTK_INFO(ctrl_blk->mdev, "Destroy stale list\n");
	} else {
		s_list->dev_id = dev_id;
		mutex_unlock(&port_mngr_grp_mtx);
		MTK_INFO(ctrl_blk->mdev, "Reserve stale list\n");
	}
}

rwlock_t *mtk_port_get_port_mngr_lock(const char *dev_str)
{
	struct mtk_stale_list *s_list;

	s_list = mtk_port_stale_list_search(dev_str);
	return &s_list->port_mngr_lock;
}

void mtk_port_trb_init(struct mtk_port *port, struct trb *trb, enum mtk_trb_cmd_type cmd,
		       int (*trb_complete)(struct sk_buff *skb))
{
	kref_init(&trb->kref);
	kref_get(&port->kref);
	trb->channel_id = port->info.rx_ch;
	trb->status = MTK_DFLT_TRB_STATUS;
	trb->priv = port;
	trb->cmd = cmd;
	trb->trb_complete = trb_complete;
}

void mtk_port_pkt_record(struct mtk_port *port, struct sk_buff *skb,
			 enum mtk_skb_record_type type)
{
	struct sk_buff_head *skb_list;
	struct sk_buff *prev_skb;
	unsigned int record_cnt;
	u8 *record_type;

	if (!skb)
		return;

	if (!port) {
		dev_kfree_skb_any(skb);
		return;
	}

	if (!(port->info.flags & PORT_F_DUMP)) {
		if (type < TX_RECORD_MAX)
			mtk_port_free_tx_skb(port, skb);
		else
			mtk_port_free_rx_skb(port, skb);
		return;
	}

	if (type < TX_RECORD_MAX) {
		record_cnt = port->info.tx_dump_cnt;
		skb_list = &port->tx_skb_record_list;
	} else {
		record_cnt = port->info.rx_dump_cnt;
		skb_list = &port->rx_skb_record_list;
	}

	if (port->info.flags & PORT_F_RAW_DATA || type == RX_DROP_HEADER_ERR || type == TX_DROP) {
		if (skb_headroom(skb) > 0)
			record_type = skb_push(skb, 1);
		else
			record_type = skb->data;
	} else {
		if (type == RX_DROP_NO_HEADER)
			record_type = skb_push(skb, sizeof(struct mtk_ccci_header) -
					       offsetof(struct mtk_ccci_header, packet_len) + 1);
		else
			record_type = skb_pull(skb,
					       offsetof(struct mtk_ccci_header, packet_len) - 1);
	}
	if (!record_type) {
		if (type < TX_RECORD_MAX)
			mtk_port_free_tx_skb(port, skb);
		else
			mtk_port_free_rx_skb(port, skb);
		return;
	}
	*record_type = type;
	spin_lock(&skb_list->lock);
	__skb_queue_tail(skb_list, skb);

	while (skb_queue_len(skb_list) > record_cnt) {
		prev_skb = __skb_dequeue(skb_list);
		if (prev_skb) {
			if (type < TX_RECORD_MAX)
				mtk_port_free_tx_skb(port, prev_skb);
			else
				mtk_port_free_rx_skb(port, prev_skb);
		}
	}

	spin_unlock(&skb_list->lock);
}

void mtk_port_trb_free(struct kref *trb_kref)
{
	struct trb *trb = container_of(trb_kref, struct trb, kref);
	struct sk_buff *skb, *frag_skb, *next_skb;
	struct mtk_port *port = trb->priv;

	skb = container_of((char *)trb, struct sk_buff, cb[0]);
	if (trb->cmd == TRB_CMD_TX) {
		if (skb_has_frag_list(skb)) {
			frag_skb = skb_shinfo(skb)->frag_list;
			while (frag_skb) {
				next_skb = frag_skb->next;
				frag_skb->next = NULL;
				mtk_port_free_tx_skb(port, frag_skb);
				frag_skb = next_skb;
			}
			skb_shinfo(skb)->frag_list = NULL;
		}
		skb->data_len = 0;
		mtk_port_pkt_record(port, skb, TX_FREE);
	} else {
		mtk_mem_free_skb(port->port_mngr->ctrl_blk->bm_pool, skb);
	}
	mutex_lock(&port_mngr_grp_mtx);
	kref_put(&port->kref, mtk_port_release);
	mutex_unlock(&port_mngr_grp_mtx);
}
EXPORT_SYMBOL(mtk_port_trb_free);

static int mtk_port_open_trb_complete(struct sk_buff *skb)
{
	struct trb_open_priv *trb_open_priv = (struct trb_open_priv *)skb->data;
	struct trb *trb = (struct trb *)skb->cb;
	struct mtk_port *port = trb->priv;
	struct mtk_port_mngr *port_mngr;

	port_mngr = port->port_mngr;

	if (trb->status && trb->status != -EBUSY)
		goto out;

	port->tx_mtu = trb_open_priv->tx_mtu;
	port->rx_mtu = trb_open_priv->rx_mtu;
	port->tx_frag_size  = trb_open_priv->tx_frag_size;
	port->rx_frag_size  = trb_open_priv->rx_frag_size;
	port->max_rx_list_cnt = port->rx_buf_size / port->rx_mtu * 2;
	port->log_rg_offset  = trb_open_priv->log_rg_offset;
	/* Minus the len of the header */
	if (!(port->info.flags & PORT_F_RAW_DATA)) {
		port->tx_mtu -= MTK_CCCI_H_ELEN;
		port->rx_mtu -= MTK_CCCI_H_ELEN;
	}

out:
	wake_up_interruptible_all(&port->trb_wq);

	MTK_INFO(port->port_mngr->ctrl_blk->mdev,
		 "Open TRB:status:%d, port:%s, tx_mtu:%d, rx_mtu:%d, tx:0x%x, rx:0x%x\n",
		 trb->status, port->info.name, port->tx_mtu, port->rx_mtu,
		 port->info.tx_ch, port->info.rx_ch);
	kref_put(&trb->kref, mtk_port_trb_free);
	return 0;
}

static int mtk_port_close_trb_complete(struct sk_buff *skb)
{
	struct trb *trb = (struct trb *)skb->cb;
	struct mtk_port *port = trb->priv;

	wake_up_interruptible_all(&port->trb_wq);
	wake_up_interruptible_all(&port->rx_wq);
	MTK_INFO(port->port_mngr->ctrl_blk->mdev,
		 "Close TRB: trb->status:%d, port:%s\n",
		 trb->status, port->info.name);
	kref_put(&trb->kref, mtk_port_trb_free);

	return 0;
}

static int mtk_port_check_sta_trb_complete(struct sk_buff *skb)
{
	struct trb *trb = (struct trb *)skb->cb;
	struct mtk_port *port = trb->priv;

	wake_up_interruptible_all(&port->trb_wq);
	kref_put(&trb->kref, mtk_port_trb_free);

	return 0;
}

static int mtk_port_trb_cfg_complete(struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
	return 0;
}

static int mtk_port_tx_complete(struct sk_buff *skb)
{
	struct trb *trb = (struct trb *)skb->cb;
	struct mtk_port *port = trb->priv;

	if (trb->status < 0) {
		MTK_WARN(port->port_mngr->ctrl_blk->mdev,
			 "Failed to send data: trb->status:%d, port:%s\n",
			 trb->status, port->info.name);
		port->tx_err_cnt++;
	}

	wake_up_interruptible_all(&port->trb_wq);
	kref_put(&trb->kref, mtk_port_trb_free);

	return 0;
}

static int mtk_port_stop_trb_complete(struct sk_buff *skb)
{
	struct trb *trb = (struct trb *)skb->cb;

	kref_put(&trb->kref, mtk_port_trb_free);

	return 0;
}

static int mtk_port_submit_cfg(struct mtk_port_mngr *port_mngr,
			       struct mtk_port_cfg_header *cfg_hdr, enum mtk_trb_cmd_type type)
{
	u16 port_config_len = le16_to_cpu(cfg_hdr->port_config_len);
	struct mtk_md_dev *mdev;
	struct sk_buff *skb;
	struct trb *trb;
	int ret;

	mdev = port_mngr->ctrl_blk->mdev;
	skb = __dev_alloc_skb((unsigned int)port_config_len, GFP_KERNEL);
	if (!skb) {
		MTK_ERR(mdev, "Failed to alloc skb for cfg\n");
		return -ENOMEM;
	}

	skb_put_data(skb, cfg_hdr->data, (unsigned int)port_config_len);

	trb = (struct trb *)skb->cb;
	trb->cmd = type;
	trb->trb_complete = mtk_port_trb_cfg_complete;
	ret = port_mngr->ctrl_blk->ops->submit_skb(mdev, skb, true);
	return ret;
}

/**
 * mtk_port_tbl_update() - Update port radix tree table.
 * @mdev: pointer to mtk_md_dev.
 * @data: pointer to config data from device.
 * @len: length of the data.
 *
 * This function called when host driver received a control message from device.
 *
 * Return: 0 on success and failure value on error.
 */
int mtk_port_tbl_update(struct mtk_md_dev *mdev, void *data, unsigned int len)
{
	int parsed_data_len = 0, ret = 0, ch_info_len;
	struct mtk_port_cfg_header *cfg_hdr = data;
	struct mtk_port_cfg_ch_info *ch_info;
	struct mtk_port_mngr *port_mngr;
	struct mtk_ctrl_blk *ctrl_blk;
	struct mtk_port *port;

	if (unlikely(!mdev || !cfg_hdr)) {
		pr_err("[PORT][%s][%d] Invalid input value\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (sizeof(*cfg_hdr) > len) {
		MTK_WARN(mdev, "Port cfg too short: %u\n", len);
		return -EPROTO;
	}

	if (cfg_hdr->msg_type != PORT_CFG_MSG_REQUEST || cfg_hdr->is_enable != 1) {
		MTK_WARN(mdev, "Invalid msg_type or is_enable: %d, %d\n",
			 cfg_hdr->msg_type, cfg_hdr->is_enable);
		return -EPROTO;
	}

	if (le16_to_cpu(cfg_hdr->port_config_len) > len - sizeof(*cfg_hdr)) {
		MTK_WARN(mdev, "Port cfg len %u > avail %zu, clamping\n",
			 le16_to_cpu(cfg_hdr->port_config_len),
			 len  - sizeof(*cfg_hdr));
		cfg_hdr->port_config_len = cpu_to_le16(len - sizeof(*cfg_hdr));
	}

	ctrl_blk = mdev->ctrl_blk;
	port_mngr = ctrl_blk->port_mngr;
	MTK_HEX_DUMP(mdev, MTK_DBG_PORT, MTK_MEMLOG_RG_CTRL_DUMP,
		     "Dumping port_cfg_header:", cfg_hdr,
		     sizeof(*cfg_hdr) + le16_to_cpu(cfg_hdr->port_config_len));

	switch (cfg_hdr->cfg_type) {
	case PORT_CFG_RPC:
	case PORT_CFG_FS:
	case PORT_CFG_SYSMSG:
		MTK_INFO(mdev, "Unsupported cfg_type: %d\n", cfg_hdr->cfg_type);
		cfg_hdr->is_enable = 0;
		break;
	case PORT_CFG_CH_INFO:
		ch_info_len = sizeof(*ch_info);
		while (parsed_data_len + ch_info_len <= le16_to_cpu(cfg_hdr->port_config_len)) {
			ch_info = (struct mtk_port_cfg_ch_info *)(cfg_hdr->data + parsed_data_len);
			parsed_data_len += ch_info_len;

			port = mtk_port_search_by_id(port_mngr, le16_to_cpu(ch_info->dl_ch_id));
			if (!port) {
				port = mtk_port_alloc_with_info(port_mngr, ch_info);
				if (IS_ERR(port)) {
					ret = PTR_ERR(port);
					continue;
				}

				ret = mtk_port_tbl_add(port_mngr, port);
				if (ret < 0) {
					kfree(port);
					continue;
				}

				port->port_mngr = port_mngr;
				/* port->kref will be 1 after port_init */
				ret = ports_ops[port->info.type]->init(port);
				if (ret < 0) {
					mtk_port_tbl_del(port_mngr, port);
					kfree(port);
					continue;
				}

				port->info.flags |= PORT_F_ALLOW_DROP;
			}
			if (ch_info->reserved & MTK_PORT_NON_DIPC)
				port->enable = true;
			if (ch_info->reserved & MTK_PORT_FLOW_CTRL)
				port->info.flags |= PORT_F_FLOWCTRL;
		}
		mtk_port_submit_cfg(port_mngr, cfg_hdr, TRB_CMD_SET_CH_CFG);
		cfg_hdr->msg_type = PORT_CFG_MSG_RESPONSE;
		break;
	case PORT_CFG_HIF_INFO:
		mtk_port_submit_cfg(port_mngr, cfg_hdr, TRB_CMD_SET_HIF_CFG);
		cfg_hdr->msg_type = PORT_CFG_MSG_RESPONSE;
		break;
	default:
		MTK_WARN(mdev, "Invalid cfg_type: %d\n", cfg_hdr->cfg_type);
		cfg_hdr->is_enable = 0;
		ret = -EPROTO;
		break;
	}

	return ret;
}

int mtk_port_status_check(struct mtk_port *port)
{
	/* If port is enable, it must on port_mngr's port_tbl, so the mdev must exist. */
	if (!test_bit(PORT_S_ENABLE, &port->status))
		return -ENODEV;

	if (!test_bit(PORT_S_OPEN, &port->status) || test_bit(PORT_S_FLUSH, &port->status) ||
	    !test_bit(PORT_S_WR, &port->status))
		return -EBADF;

	return 0;
}

static int mtk_port_ch_status_check(struct mtk_port *port)
{
	struct mtk_port_mngr *port_mngr = port->port_mngr;
	struct sk_buff *skb;
	struct trb *trb;
	int ret;

	skb = mtk_mem_alloc_skb(port_mngr->ctrl_blk->bm_pool, Q_MTU_3_5K, 0);
	if (!skb) {
		MTK_WARN(port_mngr->ctrl_blk->mdev,
			 "Failed to alloc skb of port(%s)\n", port->info.name);
		return -ENOMEM;
	}
	trb = (struct trb *)skb->cb;
	mtk_port_trb_init(port, trb, TRB_CMD_CHECK_STA, mtk_port_check_sta_trb_complete);
	kref_get(&trb->kref);

	ret = port_mngr->ctrl_blk->ops->submit_skb(port_mngr->ctrl_blk->mdev, skb, false);
	if (ret) {
		kref_put(&trb->kref, mtk_port_trb_free);
		kref_put(&trb->kref, mtk_port_trb_free);
		return ret;
	}

start_wait:
	ret = wait_event_interruptible_timeout(port->trb_wq, trb->status <= 0,
					       MTK_DFLT_TRB_TIMEOUT);
	if (ret == -ERESTARTSYS)
		goto start_wait;
	else if (!ret)
		ret = -ETIMEDOUT;
	else
		ret = trb->status;

	kref_put(&trb->kref, mtk_port_trb_free);
	return ret;
}

/**
 * mtk_port_send_data() - send data to device through trans layer.
 * @port: pointer to channel structure for sending data.
 * @data: data to be sent.
 *
 * This function will be called by port io.
 *
 * Return:
 *  actual sent data length if success.
 *  error value if send failed.
 */
int mtk_port_send_data(struct mtk_port *port, void *data)
{
	struct mtk_port_mngr *port_mngr;
	bool tx_timeout_abnormal = true;
	struct mtk_ctrl_blk *ctrl_blk;
	struct sk_buff *skb = data;
	u8 tx_timeout_cnt = 0;
	bool force_send;
	struct trb *trb;
	int ret, len;

	port_mngr = port->port_mngr;
	ctrl_blk = port_mngr->ctrl_blk;

	force_send = !!(port->info.flags & (PORT_F_BLOCKING | PORT_F_FORCE_SEND));
	trb = (struct trb *)skb->cb;
	mtk_port_trb_init(port, trb, TRB_CMD_TX, mtk_port_tx_complete);
	len = skb->len;
	kref_get(&trb->kref); /* kref count 1->2 */

	mutex_lock(&port->write_lock);
	/* add ccci header */
	mtk_port_add_header(skb);
	ret = mtk_port_status_check(port);
	if (!ret)
		ret = ctrl_blk->ops->submit_skb(ctrl_blk->mdev, skb, force_send);
	mutex_unlock(&port->write_lock);

	if (ret < 0) {
		kref_put(&trb->kref, mtk_port_trb_free); /* kref count 2->1 */
		kref_put(&trb->kref, mtk_port_trb_free); /* kref count 1->0 */
		mutex_lock(&port->write_lock);
		port->tx_seq--;
		mutex_unlock(&port->write_lock);
		goto out;
	}

	trace_mtk_ctrl_write(port->info.tx_ch, skb, skb->data, len);
	if (!(port->info.flags & PORT_F_BLOCKING)) {
		kref_put(&trb->kref, mtk_port_trb_free);
		ret = len;
		goto out;
	}
start_wait:

	/* wait trb done, and no timeout in tx blocking mode */
	ret = wait_event_interruptible_timeout(port->trb_wq,
					       trb->status <= 0 ||
					       test_bit(PORT_S_FLUSH, &port->status) ||
					       !test_bit(PORT_S_WR, &port->status),
					       MTK_DFLT_TRB_TIMEOUT);
	if (!ret) {
		ret = mtk_port_ch_status_check(port);
		if (ret)
			tx_timeout_abnormal = false;
		if (tx_timeout_cnt++ == PORT_TX_TRIGGER_ACTION_CNT) {
			if (tx_timeout_abnormal) {
				ret = ctrl_blk->ops->send_cmd(ctrl_blk->mdev,
							      HIF_CTRL_CMD_TX_ABORT, NULL);
				if (!ret)
					goto start_wait;
			}
			mtk_fsm_evt_submit(ctrl_blk->mdev, FSM_EVT_DUMP, FSM_F_DFLT, NULL, 0, 0);
		}
		goto start_wait;
	} else if (ret == -ERESTARTSYS) {
		ret = -EINTR;
	} else if (ret > 0) {
		if (test_bit(PORT_S_FLUSH, &port->status))
			ret = len;
		else
			ret = (!trb->status) ? len : trb->status;
	}
	trace_mtk_ctrl_write_done(port->info.tx_ch, skb, skb->data, ret);
	kref_put(&trb->kref, mtk_port_trb_free);

out:
	return ret;
}

static int mtk_port_check_rx_seq(struct mtk_port *port, struct mtk_ccci_header *ccci_h,
				 bool force_mdee)
{
	u16 seq_num, assert_bit, channel;
	struct mtk_md_dev *mdev;

	seq_num = FIELD_GET(MTK_HDR_FLD_SEQ, le32_to_cpu(ccci_h->status));
	assert_bit = FIELD_GET(MTK_HDR_FLD_AST, le32_to_cpu(ccci_h->status));
	if (assert_bit && port->rx_seq &&
	    ((seq_num - port->rx_seq) & MTK_CHECK_RX_SEQ_MASK) != 1) {
		mdev = port->port_mngr->ctrl_blk->mdev;
		channel = FIELD_GET(MTK_HDR_FLD_CHN, le32_to_cpu(ccci_h->status));
#if IS_ENABLED(CONFIG_GOOGLE_B493109074_DEBUG)
		MTK_WARN(mdev, "<ch: %04x> seq num out-of-order %d->%d, %08x, %08x, %08x, %08x\n",
			 channel, seq_num, port->rx_seq,
			 le32_to_cpu(ccci_h->packet_header), le32_to_cpu(ccci_h->packet_len),
			 le32_to_cpu(ccci_h->status), le32_to_cpu(ccci_h->ex_msg));
#else
		MTK_WARN(mdev, "<ch: %04x> seq num out-of-order %d->%d, len(%d)\n",
			 channel, seq_num, port->rx_seq, le32_to_cpu(ccci_h->packet_len));
#endif
		if (!force_mdee)
			return -EBADMSG;
		mtk_ctrl_dump(mdev);
		if (MTK_PORT_TBL_TYPE(channel) == PORT_TBL_MD)
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		{
			radio_google_port_error_handler(mdev->google,
							EXCP_REASON_CCCI_PKT_OUT_OF_ORDER);
			mtk_fsm_trigger_mdee(mdev);
		}
#else
			mtk_fsm_trigger_mdee(mdev);
#endif

		port->rx_seq = seq_num;
		return -EPROTO;
	}

	return 0;
}

static int mtk_port_rx_dispatch_frag_skb(struct mtk_port *port, struct sk_buff *skb)
{
	struct sk_buff *frag_skb, *frag_next;
	int ret;

	frag_skb = skb_shinfo(skb)->frag_list;
	skb->len -= skb->data_len;
	skb->data_len = 0;
	if (port->info.flags & PORT_F_FORCE_NO_SPILT_PACKET)
		__skb_set_hash(skb, port->rx_seq, true, false);
	skb_shinfo(skb)->frag_list = NULL;
	trace_mtk_ctrl_dispatch(port->info.rx_ch, skb, skb->data);
	ret = ports_ops[port->info.type]->recv(port, skb);
	if (ret < 0) {
		trace_mtk_ctrl_dispatch_err(port->info.rx_ch, skb, ret);
		skb_shinfo(skb)->frag_list = frag_skb;
		return ret;
	}

	while (frag_skb) {
		frag_next = frag_skb->next;
		if (!frag_skb->len) {
			frag_skb->next = NULL;
			mtk_port_free_rx_skb(port, frag_skb);
			frag_skb = frag_next;
			continue;
		}
		if (port->info.flags & PORT_F_FORCE_NO_SPILT_PACKET)
			__skb_set_hash(frag_skb, port->rx_seq, true, false);
		frag_skb->next = NULL;
		trace_mtk_ctrl_dispatch(port->info.rx_ch, frag_skb, frag_skb->data);
		ret = ports_ops[port->info.type]->recv(port, frag_skb);
		if (ret < 0) {
			trace_mtk_ctrl_dispatch_err(port->info.rx_ch, frag_skb, ret);
			frag_skb->next = frag_next;
			while (frag_skb) {
				frag_next = frag_skb->next;
				frag_skb->next = NULL;
				mtk_port_free_rx_skb(port, frag_skb);
				frag_skb = frag_next;
			}
			return -EIO;
		}
		frag_skb = frag_next;
	}

	return 0;
}

static int mtk_port_flowctrl_ch_handler(void *data, struct sk_buff *skb)
{
	struct mtk_port_mngr *port_mngr = data;
	struct mtk_port_flowctrl_blk *fc_blk;
	struct mtk_port *port;
	int rx_ch;

	fc_blk = (struct mtk_port_flowctrl_blk *)skb->data;
	rx_ch = le16_to_cpu(fc_blk->rx_ch);
	if (le16_to_cpu(fc_blk->msg) == FLOWCTRL_MSG_REACHABLE) {
		port = mtk_port_search_by_id(port_mngr, rx_ch);
		if (port) {
			port->flowctrl_acked = true;
			wake_up_interruptible_all(&port_mngr->flowctrl_wq);
		}
	}

	dev_kfree_skb(skb);

	return 0;
}

static int mtk_port_sap_miscctrl_msg_handler(void *data, struct sk_buff *skb)
{
	struct mtk_port_mngr *port_mngr = data;
	struct ctrl_msg_header *ctrl_msg_h;

	ctrl_msg_h = (struct ctrl_msg_header *)skb->data;
	skb_pull(skb, sizeof(*ctrl_msg_h));

	switch (le32_to_cpu(ctrl_msg_h->id)) {
	case CTRL_MSG_ACK_LB_MODE:
		port_mngr->lb_mode[PORT_TBL_SAP] =
			le32_to_cpu(((struct lb_ctrl_msg *)skb->data)->mode_id);
		dev_kfree_skb(skb);
		return 0;
	default:
		return -EPROTO;
	}

	return 0;
}

static int mtk_port_md_miscctrl_msg_handler(void *data, struct sk_buff *skb)
{
	struct mtk_port_mngr *port_mngr = data;
	struct ctrl_msg_header *ctrl_msg_h;

	ctrl_msg_h = (struct ctrl_msg_header *)skb->data;
	skb_pull(skb, sizeof(*ctrl_msg_h));

	switch (le32_to_cpu(ctrl_msg_h->id)) {
	case CTRL_MSG_ACK_LB_MODE:
		port_mngr->lb_mode[PORT_TBL_MD] =
			le32_to_cpu(((struct lb_ctrl_msg *)skb->data)->mode_id);
		dev_kfree_skb(skb);
		return 0;
	default:
		return -EPROTO;
	}

	return 0;
}

static int (*miscctrl_msg_handler[PORT_TBL_MAX])(void *data, struct sk_buff *skb) = {
	[PORT_TBL_SAP] = mtk_port_sap_miscctrl_msg_handler,
	[PORT_TBL_MD] = mtk_port_md_miscctrl_msg_handler,
};

/**
 * mtk_port_send_flowctrl_msg() - send flowctrl message.
 * @port: pointer to port.
 * @msg: flowctrl message, it can be set by enum mtk_port_flowctrl_msg.
 *
 * This function create flowctrl message, then send it to device to start or stop
 * DL transfer.
 *
 * Return: if flowctrl message transfer successfully, it returns 0; otherwise,
 * it returns value less than 0;
 */
int mtk_port_send_flowctrl_msg(struct mtk_port *port, int msg)
{
	struct mtk_port_flowctrl_blk *fc_blk;
	int rx_ch, ret, tbl_type, loop = 0;
	struct mtk_port_mngr *port_mngr;
	struct mtk_port *fc_port;
	struct sk_buff *skb;

	if (!(port->info.flags & PORT_F_FLOWCTRL))
		return 0;

	port_mngr = port->port_mngr;
	rx_ch = port->info.rx_ch;
	tbl_type = MTK_PORT_TBL_TYPE(rx_ch);
	if (tbl_type < PORT_TBL_SAP || tbl_type >= PORT_TBL_MAX) {
		MTK_WARN_RATELIMITED(port_mngr->ctrl_blk->mdev, "Invalid Channel_ID 0x%8x", rx_ch);
		return -EINVAL;
	}

retry:
	skb = mtk_mem_alloc_skb(port_mngr->ctrl_blk->bm_pool, Q_MTU_3_5K, 0);
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, sizeof(struct mtk_ccci_header));
	skb_put(skb, sizeof(*fc_blk));
	fc_blk = (struct mtk_port_flowctrl_blk *)skb->data;
	fc_blk->msg = cpu_to_le16(msg);
	fc_blk->rx_ch = cpu_to_le16(rx_ch);
	fc_port = port_mngr->flowctrl_port[tbl_type];

	ret = mtk_port_internal_write(fc_port, skb);
	if (ret < 0) {
		MTK_WARN_RATELIMITED(port_mngr->ctrl_blk->mdev,
				     "ignore port(%s) flowctrl message",
				     port->info.name);
		return 0;
	}

	if (msg == FLOWCTRL_MSG_REACHABLE) {
		ret = wait_event_timeout(port_mngr->flowctrl_wq,
					 port->flowctrl_acked, MTK_FLOWCTRL_TIMEOUT);
		if (ret == -ERESTARTSYS)
			return -EINTR;

		if (!ret) {
			MTK_ERR(port_mngr->ctrl_blk->mdev,
				"failed to recv port(%s) reachable message",
				port->info.name);
			mtk_fsm_evt_submit(port_mngr->ctrl_blk->mdev, FSM_EVT_DUMP,
					   FSM_F_DFLT, NULL, 0, 0);
			if (loop++ < MTK_FLOWCTRL_TIMEOUT_CNT_MAX)
				goto retry;
			else
				return -ETIMEDOUT;
		}
		port->flowctrl_acked = false;
	}

	return 0;
}

static int mtk_port_flowctrl_ch_start(struct mtk_port_mngr *port_mngr, char *name)
{
	struct mtk_port *fc_port;

	fc_port = mtk_port_search_by_name(port_mngr, name);
	if (!fc_port) {
		MTK_INFO(port_mngr->ctrl_blk->mdev, "flow ctrl port %s not found\n", name);
		return -ENODEV;
	}

	mtk_port_internal_recv_register(fc_port, mtk_port_flowctrl_ch_handler, port_mngr);
	mtk_port_internal_open(port_mngr->ctrl_blk->mdev, name, O_NONBLOCK);

	port_mngr->flowctrl_port[MTK_PORT_TBL_TYPE(fc_port->info.rx_ch)] = fc_port;

	return 0;
}

static int mtk_port_flowctrl_ch_stop(struct mtk_port_mngr *port_mngr, char *name)
{
	struct mtk_port *fc_port;

	fc_port = mtk_port_search_by_name(port_mngr, name);
	if (!fc_port) {
		MTK_INFO(port_mngr->ctrl_blk->mdev, "flow ctrl port %s not found\n", name);
		return -ENODEV;
	}

	mtk_port_internal_close(fc_port);

	return 0;
}

static int mtk_port_miscctrl_ch_start(struct mtk_port_mngr *port_mngr, char *name)
{
	struct mtk_port *mc_port;
	int port_tbl_type;

	mc_port = mtk_port_search_by_name(port_mngr, name);
	if (!mc_port) {
		MTK_INFO(port_mngr->ctrl_blk->mdev, "misc ctrl port %s not found\n", name);
		return -ENODEV;
	}

	port_tbl_type = MTK_PORT_TBL_TYPE(mc_port->info.rx_ch);
	mtk_port_internal_recv_register(mc_port, miscctrl_msg_handler[port_tbl_type], port_mngr);
	mtk_port_internal_open(port_mngr->ctrl_blk->mdev, name, O_NONBLOCK);

	port_mngr->miscctrl_port[port_tbl_type] = mc_port;
	port_mngr->lb_mode[port_tbl_type] = 0;

	return 0;
}

static int mtk_port_miscctrl_ch_stop(struct mtk_port_mngr *port_mngr, char *name)
{
	struct mtk_port *mc_port;

	mc_port = mtk_port_search_by_name(port_mngr, name);
	if (!mc_port) {
		MTK_INFO(port_mngr->ctrl_blk->mdev, "misc ctrl port %s not found\n", name);
		return -ENODEV;
	}

	mtk_port_internal_close(mc_port);

	return 0;
}

int mtk_port_rx_pre_check(struct sk_buff *skb, void *priv)
{
	struct mtk_port_mngr *port_mngr;
	struct mtk_ccci_header *ccci_h;
	struct mtk_port *port = priv;
	u16 channel;
	int ret;

	if (!skb || !priv)
		return -EINVAL;

	port_mngr = port->port_mngr;
	/* If ccci header field has been loaded in skb data,
	 * the data should be dispatched by port manager
	 */
	if (port->info.flags & PORT_F_RAW_DATA)
		return 0;

	ccci_h = mtk_port_strip_header(skb);
	if (unlikely(!ccci_h))
		return -EPROTO;

	channel = FIELD_GET(MTK_HDR_FLD_CHN, le32_to_cpu(ccci_h->status));
	port = mtk_port_search_by_id(port_mngr, channel);
	if (unlikely(!port)) {
		MTK_WARN(port_mngr->ctrl_blk->mdev,
			 "Failed to find port by channel, rx header: %08x %08x\n",
			 ccci_h->packet_len, ccci_h->status);
		return -EAGAIN;
	}

	if (port->info.flags & PORT_F_RAW_DATA)
		return 0;

	/* The sequence number must be continuous */
	ret = mtk_port_check_rx_seq(port, ccci_h, false);
	if (unlikely(ret))
		return -EAGAIN;

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_port_rx_pre_check);

static int mtk_port_rx_dispatch(struct sk_buff *skb, void *priv, bool force_recv)
{
	enum mtk_skb_record_type type = RX_DROP_HEADER_ERR;
	struct sk_buff *cmd_skb, *frag_skb, *tmp;
	struct mtk_port_mngr *port_mngr;
	struct mtk_ccci_header *ccci_h;
	struct mtk_port *port = priv;
	int ret = -EPROTO;
	struct trb *trb;
	u16 channel;

	if (!skb || !priv)
		return -EINVAL;

	port_mngr = port->port_mngr;
	/* If ccci header field has been loaded in skb data,
	 * the data should be dispatched by port manager
	 */
	if (!(port->info.flags & PORT_F_RAW_DATA)) {
		ccci_h = mtk_port_strip_header(skb);
		if (unlikely(!ccci_h))
			goto drop_data;

		channel = FIELD_GET(MTK_HDR_FLD_CHN, le32_to_cpu(ccci_h->status));
		port = mtk_port_search_by_id(port_mngr, channel);
		if (unlikely(!port)) {
			MTK_WARN(port_mngr->ctrl_blk->mdev,
				 "Failed to find port by channel, rx header: %08x %08x\n",
				 ccci_h->packet_len, ccci_h->status);
			goto drop_data;
		}
		if (!(port->info.flags & PORT_F_RAW_DATA)) {
			/* The sequence number must be continuous */
			ret = mtk_port_check_rx_seq(port, ccci_h, true);
			if (unlikely(ret))
				goto drop_data;
		}
	}

	if ((skb->len + port->rx_data_len > port->rx_buf_size ||
	     port->rx_skb_list.qlen > port->max_rx_list_cnt) &&
	     !force_recv) {
		if (skb->len > port->rx_buf_size)
			MTK_DBG(port_mngr->ctrl_blk->mdev, MTK_DBG_CTRL_RX,
				MTK_MEMLOG_RG_CTRL_MISC, "port:%s, skb overflow:%d\n",
				port->info.name, skb->len);
		/* If the rx buffer is not enough, the data will be dropped */
		if (port->info.flags & PORT_F_ALLOW_DROP ||
		    !test_bit(PORT_S_RD, &port->status)) {
			ret = -ENOMEM;
			if (!(port->info.flags & PORT_F_RAW_DATA)) {
				MTK_DBG(port_mngr->ctrl_blk->mdev, MTK_DBG_CTRL_RX,
					MTK_MEMLOG_RG_CTRL_MISC,
					"Drop packet RX header:%08x %08x\n", ccci_h->packet_len,
					ccci_h->status);
				port->rx_seq = FIELD_GET(MTK_HDR_FLD_SEQ,
							 le32_to_cpu(ccci_h->status));
				type = RX_DROP_WITH_HEADER;
			}
			if (port->rx_mtu > port->rx_frag_size)
				goto drop_frag_skb;
			else
				goto drop_data;
		} else if (!test_bit(PORT_S_STOP, &port->status)) {
			cmd_skb = mtk_mem_alloc_skb(port_mngr->ctrl_blk->bm_pool, Q_MTU_3_5K, 0);
			if (!cmd_skb) {
				MTK_ERR(port_mngr->ctrl_blk->mdev,
					"Failed to alloc stop trb(%s)\n", port->info.name);
				return -EAGAIN;
			}
			trb = (struct trb *)cmd_skb->cb;
			mtk_port_trb_init(port, trb, TRB_CMD_STOP, mtk_port_stop_trb_complete);
			ret = port_mngr->ctrl_blk->ops->submit_skb(port_mngr->ctrl_blk->mdev,
								   cmd_skb, true);
			if (ret < 0) {
				MTK_ERR(port_mngr->ctrl_blk->mdev,
					"Failed to submit stop trb(%s)\n", port->info.name);
				kref_put(&trb->kref, mtk_port_trb_free);
			}
			set_bit(PORT_S_STOP, &port->status);
		}
		/* retry by transaction layer, do not drop data here */
		return -EAGAIN;
	}

	if (!(port->info.flags & PORT_F_RAW_DATA)) {
		MTK_DBG_CTRL_RX_DISPATCH(port_mngr->ctrl_blk->mdev,
					 ccci_h->packet_len, ccci_h->status, port->log_rg_offset);
		port->rx_seq = FIELD_GET(MTK_HDR_FLD_SEQ, le32_to_cpu(ccci_h->status));
		skb_pull(skb, sizeof(*ccci_h));
		type = RX_DROP_NO_HEADER;
	}

	/* Support scatter gather transmission */
	if (port->rx_mtu > port->rx_frag_size) {
		ret = mtk_port_rx_dispatch_frag_skb(port, skb);
		/* -EIO means partial data dispatch complete, does not goto drop flow */
		if (ret < 0 && ret != -EIO)
			goto drop_frag_skb;
	} else {
		trace_mtk_ctrl_dispatch(port->info.rx_ch, skb, skb->data);
		ret = ports_ops[port->info.type]->recv(port, skb);
		if (ret < 0) {
			trace_mtk_ctrl_dispatch_err(port->info.rx_ch, skb, ret);
			goto drop_data;
		}

		if (port->info.flags & PORT_F_FLOWCTRL) {
			mutex_lock(&port->fc_lock);
			if (test_bit(PORT_S_FLUSH, &port->status))
				mtk_port_send_flowctrl_msg(port, FLOWCTRL_MSG_UNREACHABLE);
			mutex_unlock(&port->fc_lock);
		}
	}

	return ret;

drop_frag_skb:
	frag_skb = skb_shinfo(skb)->frag_list;
	while (frag_skb) {
		tmp = frag_skb->next;
		frag_skb->next = NULL;
		mtk_port_free_rx_skb(port, frag_skb);
		frag_skb = tmp;
	}
	skb_shinfo(skb)->frag_list = NULL;
drop_data:
	mtk_port_pkt_record(port, skb, type);
	return ret;
}

#define PORT_TBL_MASK_ALL (BIT(PORT_TBL_SAP) | BIT(PORT_TBL_MD) | BIT(PORT_TBL_GNSS))

static void mtk_ports_reset(struct mtk_port_mngr *port_mngr, unsigned int tbl_type_mask)
{
	enum mtk_port_tbl tbl_type = PORT_TBL_SAP;
	struct mtk_port **ports;
	int ret, idx;

	ports = kcalloc(port_mngr->port_cnt, sizeof(struct mtk_port *), GFP_KERNEL);
	if (!ports) {
		MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to alloc ports\n");
		return;
	}

	do {
		if (!(tbl_type_mask & BIT(tbl_type)))
			continue;

		ret = radix_tree_gang_lookup(&port_mngr->port_tbl[tbl_type],
					     (void **)ports, 0, port_mngr->port_cnt);
		for (idx = 0; idx < ret; idx++) {
			ports[idx]->enable = false;
			ports_ops[ports[idx]->info.type]->reset(ports[idx]);
		}
	} while (++tbl_type < PORT_TBL_MAX);
	kfree(ports);
}

static int mtk_port_enable_by_type(struct mtk_port_mngr *port_mngr, int tbl_type)
{
	struct mtk_port **ports;
	int ret, idx;

	if (tbl_type < 0 || tbl_type >= PORT_TBL_MAX)
		return -EINVAL;

	ports = kcalloc(port_mngr->port_cnt, sizeof(struct mtk_port *), GFP_KERNEL);
	if (!ports) {
		MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to alloc ports\n");
		return -ENOMEM;
	}

	ret = radix_tree_gang_lookup(&port_mngr->port_tbl[tbl_type],
				     (void **)ports, 0, port_mngr->port_cnt);
	for (idx = 0; idx < ret; idx++) {
		if (ports[idx]->enable)
			ports_ops[ports[idx]->info.type]->enable(ports[idx]);
	}

	kfree(ports);
	return 0;
}

static void mtk_port_disable_by_type(struct mtk_port_mngr *port_mngr, int tbl_type)
{
	struct mtk_port **ports;
	int ret, idx;

	if (tbl_type < 0 || tbl_type >= PORT_TBL_MAX)
		return;

	ports = kcalloc(port_mngr->port_cnt, sizeof(struct mtk_port *), GFP_KERNEL);
	if (!ports) {
		MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to alloc ports\n");
		return;
	}

	ret = radix_tree_gang_lookup(&port_mngr->port_tbl[tbl_type],
				     (void **)ports, 0, port_mngr->port_cnt);
	for (idx = 0; idx < ret; idx++) {
		ports[idx]->enable = false;
		ports_ops[ports[idx]->info.type]->disable(ports[idx]);
	}
	kfree(ports);
}

static void mtk_port_disable(struct mtk_port_mngr *port_mngr)
{
	int tbl_type = PORT_TBL_SAP;

	do {
		mtk_port_disable_by_type(port_mngr, tbl_type);
	} while (++tbl_type < PORT_TBL_MAX);
}

static int mtk_minidump_port_skb_recv(void *data, struct sk_buff *skb)
{
	struct mtk_port *port = data;

	if (!port || !skb)
		return -EINVAL;

	/* The minidump application want to receive Netlink message
	 * when there is path info from the device.
	 */
	mtk_netlink_send_msg(MTK_NETLINK_GROUP,
			     skb->data, skb->len);

	mtk_port_free_rx_skb(port, skb);

	return 0;
}

static ssize_t mtk_post_dump_port_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "/dev/%s", "ttyDUMP");
}

static struct kobj_attribute post_dump_port_attr = {
	.attr = {
		.name = __stringify(post_dump_port),
		.mode = 0444,
	},
	.show = mtk_post_dump_port_show,
	.store = NULL,
};

static struct attribute *port_attr[] = {
	&post_dump_port_attr.attr,
	NULL,
};

static struct attribute_group pcie_attr_group = {
	.attrs = port_attr,
};

static ssize_t mtk_brom_flag_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	char id = *buf - '0';

	switch (id) {
	case 1:
		brom_dl_flag = 1;
		break;
	case 2:
		brom_dl_flag = 0;
		break;
	default:
		break;
	}

	return count;
}

static DEVICE_ATTR_WO(mtk_brom_flag);

#define READ_BROM_CMD_SLEEP_TIME 50
static void mtk_brom_port_start_cmd(struct mtk_port *port)
{
	if (port->info.rx_ch != BROM_INDEX_RX)
		return;

	mtk_port_ch_enable(port);
	set_bit(PORT_S_ENABLE, &port->status);
	set_bit(PORT_S_OPEN, &port->status);
	set_bit(PORT_S_RD, &port->status);
	set_bit(PORT_S_WR, &port->status);
	clear_bit(PORT_S_FLUSH, &port->status);
	/* "Enter start" cmd */
	mtk_port_send_brom_cmd(port, 0xa0);
	if (mtk_port_read_brom_cmd_ack(port, 0x5f, READ_BROM_CMD_SLEEP_TIME))
		goto disable_port;
	mtk_port_send_brom_cmd(port, 0x0a);
	if (mtk_port_read_brom_cmd_ack(port, 0xf5, READ_BROM_CMD_SLEEP_TIME))
		goto disable_port;
	mtk_port_send_brom_cmd(port, 0x50);
	if (mtk_port_read_brom_cmd_ack(port, 0xaf, READ_BROM_CMD_SLEEP_TIME))
		goto disable_port;
	mtk_port_send_brom_cmd(port, 0x05);
	if (mtk_port_read_brom_cmd_ack(port, 0xfa, READ_BROM_CMD_SLEEP_TIME))
		goto disable_port;
	/* "Jump bl" cmd */
	mtk_port_send_brom_cmd(port, 0xd6);
	if (mtk_port_read_brom_cmd_ack(port, 0xd6, READ_BROM_CMD_SLEEP_TIME))
		goto disable_port;
	if (mtk_port_read_brom_cmd_ack(port, 0x00, READ_BROM_CMD_SLEEP_TIME))
		goto disable_port;
	if (mtk_port_read_brom_cmd_ack(port, 0x00, READ_BROM_CMD_SLEEP_TIME))
		goto disable_port;
	if (mtk_port_read_brom_cmd_ack(port, 0x00, READ_BROM_CMD_SLEEP_TIME))
		goto disable_port;
	if (mtk_port_read_brom_cmd_ack(port, 0x00, READ_BROM_CMD_SLEEP_TIME))
		goto disable_port;

disable_port:
	set_bit(PORT_S_FLUSH, &port->status);
	clear_bit(PORT_S_RD, &port->status);
	clear_bit(PORT_S_WR, &port->status);
	clear_bit(PORT_S_OPEN, &port->status);
	clear_bit(PORT_S_ENABLE, &port->status);
	mtk_port_ch_disable(port);
}

static int mtk_port_attr_init(struct mtk_port_mngr *port_mngr)
{
	struct mtk_md_dev *mdev = port_mngr->ctrl_blk->mdev;
	char name[PORT_ATTR_DIR_NAME_LEN];
	int ret;

	snprintf(name, PORT_ATTR_DIR_NAME_LEN, "mtk_wwan_%x_pcie", mdev->hw_ver);
	port_mngr->port_attr_kobj = kobject_create_and_add(name, kernel_kobj);

	/* Create the files associated with this kobject */
	ret = sysfs_create_group(port_mngr->port_attr_kobj, &pcie_attr_group);
	if (ret && ret != -EINVAL) {
		MTK_ERR(mdev, "Failed to create port attr, ret=%d\n", ret);
		goto err_create_file;
	}

	/* Create attribute file for bromDL */
	ret = device_create_file(mdev->dev, &dev_attr_mtk_brom_flag);
	if (ret) {
		MTK_ERR(mdev, "Unable to create brom_flag sysfs entry\n");
		sysfs_remove_group(port_mngr->port_attr_kobj, &pcie_attr_group);
		goto err_create_file;
	}

	return 0;

err_create_file:
	kobject_put(port_mngr->port_attr_kobj);
	return ret;
}

static int mtk_port_attr_exit(struct mtk_port_mngr *port_mngr)
{
	struct mtk_md_dev *mdev = port_mngr->ctrl_blk->mdev;

	device_remove_file(mdev->dev, &dev_attr_mtk_brom_flag);

	if (port_mngr->port_attr_kobj) {
		sysfs_remove_group(port_mngr->port_attr_kobj, &pcie_attr_group);
		MTK_INFO(mdev, "Remove port attr file\n");
		kobject_put(port_mngr->port_attr_kobj);
		port_mngr->port_attr_kobj = NULL;
	} else {
		MTK_INFO(mdev, "Port attr file is not exist\n");
	}

	return 0;
}

static ssize_t mtk_port_dump_show(void *data, char *buf, ssize_t max_cnt)
{
	enum mtk_port_tbl tbl_type = PORT_TBL_SAP;
	struct mtk_port_mngr *port_mngr = data;
	int port_cnt, idx, ret, cnt = 0;
	struct mtk_port **ports;

	ports = kcalloc(port_mngr->port_cnt, sizeof(struct mtk_port *), GFP_KERNEL);
	if (!ports) {
		MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to alloc ports\n");
		return 0;
	}

	do {
		port_cnt = radix_tree_gang_lookup(&port_mngr->port_tbl[tbl_type],
						  (void **)ports, 0, port_mngr->port_cnt);
		for (idx = 0; idx < port_cnt; idx++) {
			ret = scnprintf(buf + cnt, max_cnt,
					"%s,%d,%d,%d\n",
					ports[idx]->info.name,
					!!(ports[idx]->info.flags & PORT_F_DUMP),
					ports[idx]->info.tx_dump_cnt,
					ports[idx]->info.rx_dump_cnt);
			cnt += ret;
		}
	} while (++tbl_type < PORT_TBL_MAX);
	buf[cnt] = '\0';
	kfree(ports);
	return cnt;
}

static ssize_t mtk_port_dump_ctrl(void *data, const char *buf, ssize_t cnt)
{
	char port_name[PORT_DUMP_NAME_MAX_LEN] = {0};
	struct mtk_port_mngr *port_mngr = data;
	struct mtk_port *port;
	int err, val;
	char *pos;

	pos = strchr(buf, ' ');
	if (!pos) {
		pr_err("Failed to set dump status, param not enough\n");
		return -EINVAL;
	}

	strncpy(port_name, buf, pos - buf > PORT_DUMP_NAME_MAX_LEN - 1 ?
		PORT_DUMP_NAME_MAX_LEN - 1 : pos - buf);

	err = kstrtoint(pos + 1, 10, &val);
	if (err)
		return -EINVAL;

	port = mtk_port_search_by_name(port_mngr, port_name);
	if (port) {
		if (val)
			port->info.flags |= PORT_F_DUMP;
		else
			port->info.flags &= ~PORT_F_DUMP;
	}

	return cnt;
}

MTK_DBGFS(port_dump_ctrl, mtk_port_dump_show, mtk_port_dump_ctrl);

static ssize_t mtk_port_dump_set_tx_cnt(void *data, const char *buf, ssize_t cnt)
{
	char port_name[PORT_DUMP_NAME_MAX_LEN] = {0};
	struct mtk_port_mngr *port_mngr = data;
	struct mtk_port *port;
	int err, val;
	char *pos;

	pos = strchr(buf, ' ');
	if (!pos) {
		pr_err("Failed to set tx count, param not enough\n");
		return -EINVAL;
	}

	strncpy(port_name, buf, pos - buf > PORT_DUMP_NAME_MAX_LEN - 1 ?
		PORT_DUMP_NAME_MAX_LEN - 1 : pos - buf);

	err = kstrtoint(pos + 1, 10, &val);
	if (err)
		return -EINVAL;

	port = mtk_port_search_by_name(port_mngr, port_name);
	if (port) {
		spin_lock(&port->tx_skb_record_list.lock);
		port->info.tx_dump_cnt = val;
		spin_unlock(&port->tx_skb_record_list.lock);
	}

	return cnt;
}

MTK_DBGFS(port_dump_tx_cnt, NULL, mtk_port_dump_set_tx_cnt);

static ssize_t mtk_port_dump_set_rx_cnt(void *data, const char *buf, ssize_t cnt)
{
	char port_name[PORT_DUMP_NAME_MAX_LEN] = {0};
	struct mtk_port_mngr *port_mngr = data;
	struct mtk_port *port;
	int err, val;
	char *pos;

	pos = strchr(buf, ' ');
	if (!pos) {
		pr_err("Failed to set rx count, param not enough\n");
		return -EINVAL;
	}

	strncpy(port_name, buf, pos - buf > PORT_DUMP_NAME_MAX_LEN - 1 ?
		PORT_DUMP_NAME_MAX_LEN - 1 : pos - buf);

	err = kstrtoint(pos + 1, 10, &val);
	if (err)
		return -EINVAL;

	port = mtk_port_search_by_name(port_mngr, port_name);
	if (port) {
		spin_lock(&port->rx_skb_record_list.lock);
		port->info.rx_dump_cnt = val;
		spin_unlock(&port->rx_skb_record_list.lock);
	}

	return cnt;
}

MTK_DBGFS(port_dump_rx_cnt, NULL, mtk_port_dump_set_rx_cnt);

static ssize_t mtk_port_check_lb_mode(void *data, char *buf, ssize_t max_cnt)
{
	struct mtk_port_mngr *port_mngr = data;
	int i, ret, cnt = 0;

	for (i = 0; i < PORT_TBL_GNSS; i++) {
		switch (port_mngr->lb_mode[i]) {
		case 0:
			fallthrough;
		case 1:
			ret = snprintf(buf + cnt, max_cnt, "%s:%s\n",
				       i == PORT_TBL_SAP ? "SAPLB" : "MDLB",
				       "LB");
			cnt += ret;
			break;
		case 2:
			ret = snprintf(buf + cnt, max_cnt, "%s:%s\n",
				       i == PORT_TBL_SAP ? "SAPLB" : "MDLB",
				       "UL");
			cnt += ret;
			break;
		case 3:
			ret = snprintf(buf + cnt, max_cnt, "%s:%s\n",
				       i == PORT_TBL_SAP ? "SAPLB" : "MDLB",
				       "DL");
			cnt += ret;
			break;
		}
	}

	return cnt;
}

static ssize_t mtk_port_set_lb_mode(void *data, const char *buf, ssize_t cnt)
{
	char port_name[MTK_DFLT_PORT_NAME_LEN] = {0};
	struct mtk_port_mngr *port_mngr = data;
	struct ctrl_msg_header *ctrl_msg_h;
	struct lb_ctrl_msg *lb_ctrl_msg;
	struct mtk_port *miscctrl_port;
	struct mtk_port *lb_port;
	char mode_id[3] = {0};
	struct sk_buff *skb;
	int err, val, ret;
	int msg_size;
	char *pos;

	pos = strchr(buf, ' ');
	if (!pos) {
		pr_err("Failed to set lb mode, param not enough\n");
		return -EINVAL;
	}

	strncpy(port_name, buf, pos - buf > MTK_DFLT_PORT_NAME_LEN - 1 ?
		MTK_DFLT_PORT_NAME_LEN - 1 : pos - buf);

	lb_port = mtk_port_search_by_name(port_mngr, port_name);
	if (!lb_port || (lb_port->info.rx_ch != CCCI_LB_IT_RX &&
			 lb_port->info.rx_ch != CCCI_SAP_LBIT_RX) ||
	    !test_bit(PORT_S_ENABLE, &lb_port->status))
		return cnt;

	miscctrl_port = port_mngr->miscctrl_port[MTK_PORT_TBL_TYPE(lb_port->info.rx_ch)];
	if (!miscctrl_port)
		return cnt;

	strncpy(mode_id, pos + 1, 2);
	if (!strncmp(mode_id, "LB", 2))
		val = LB_MODE;
	else if (!strncmp(mode_id, "UL", 2))
		val = UL_MODE;
	else if (!strncmp(mode_id, "DL", 2))
		val = DL_MODE;
	else
		return -EINVAL;

	msg_size = sizeof(*ctrl_msg_h) + sizeof(*lb_ctrl_msg);
	skb = __dev_alloc_skb(msg_size, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	skb_put(skb, msg_size);

	ctrl_msg_h = (struct ctrl_msg_header *)skb->data;
	ctrl_msg_h->id = cpu_to_le32(CTRL_MSG_SET_LB_MODE);
	ctrl_msg_h->ex_msg = 0;
	ctrl_msg_h->data_len = cpu_to_le32(sizeof(*lb_ctrl_msg));

	lb_ctrl_msg = (struct lb_ctrl_msg *)(skb->data + sizeof(*ctrl_msg_h));
	lb_ctrl_msg->mode_id = cpu_to_le32(val);

	if (val == DL_MODE) {
		pos = strchr(pos + 1, ' ');
		if (!pos) {
			pr_err("Failed to set lb mode, param not enough\n");
			goto err_free_skb;
		}
		err = kstrtoint(pos + 1, 10, &val);
		if (err || val > LB_PORT_DL_MAX_CNT)
			goto err_free_skb;
		lb_ctrl_msg->dl_pkt_num = cpu_to_le32(val);
	}
	ret = mtk_port_internal_write(miscctrl_port, skb);
	if (ret <= 0)
		return ret;

	return cnt;

err_free_skb:
	dev_kfree_skb_any(skb);
	return -EINVAL;
}

MTK_DBGFS(port_lb_mode, mtk_port_check_lb_mode, mtk_port_set_lb_mode);

static inline void mtk_port_dbgfs_init(struct mtk_port_mngr *port_mngr)
{
#define PORT_DBGFS_NAME_LEN	32
	char name[PORT_DBGFS_NAME_LEN] = {0};

	snprintf(name, PORT_DBGFS_NAME_LEN, "port");
	port_mngr->dentry =
		mtk_dbgfs_create_dir(mtk_get_dev_dentry(port_mngr->ctrl_blk->mdev), name);
	if (!port_mngr->dentry)
		return;

	mtk_dbgfs_create_file(port_mngr->dentry, &mtk_dbgfs_port_dump_ctrl, port_mngr);
	mtk_dbgfs_create_file(port_mngr->dentry, &mtk_dbgfs_port_dump_tx_cnt, port_mngr);
	mtk_dbgfs_create_file(port_mngr->dentry, &mtk_dbgfs_port_dump_rx_cnt, port_mngr);
	mtk_dbgfs_create_file(port_mngr->dentry, &mtk_dbgfs_port_lb_mode, port_mngr);
}

static inline void mtk_port_dbgfs_exit(struct mtk_port_mngr *port_mngr)
{
	mtk_dbgfs_remove(port_mngr->dentry);
}

/**
 * mtk_port_add_header() - Add mtk_ccci_header to TX packet.
 * @skb: pointer to socket buffer
 *
 * This function is called by trb service. And it will help to
 * add mtk_ccci_header data to the head of skb->data.
 *
 * Return:
 *  0:		success to add ccci header.
 *  -EINVAL:	input parameter or members in input structure is illegal.
 */
int mtk_port_add_header(struct sk_buff *skb)
{
	struct mtk_ccci_header *ccci_h;
	struct mtk_port *port;
	struct trb *trb;

	trb = (struct trb *)skb->cb;
	if (trb->status == MTK_TRB_HEADER_ADDED)
		return 0;

	port = trb->priv;
	if (!port)
		return -EINVAL;

	if (port->info.flags & PORT_F_RAW_DATA)
		return 0;

	/* Port layer have reserved data length of ccci_head at the skb head */
	ccci_h = skb_push(skb, sizeof(*ccci_h));

	ccci_h->packet_header = cpu_to_le32(0);
	ccci_h->packet_len = cpu_to_le32(skb->len);
	ccci_h->ex_msg = cpu_to_le32(0);
	ccci_h->status = cpu_to_le32(FIELD_PREP(MTK_HDR_FLD_CHN, port->info.tx_ch) |
				     FIELD_PREP(MTK_HDR_FLD_SEQ, port->tx_seq++) |
				     FIELD_PREP(MTK_HDR_FLD_AST, 1));

	MTK_DBG_CTRL_ADD_HEADER(port->port_mngr->ctrl_blk->mdev,
				ccci_h->packet_len, ccci_h->status, port->log_rg_offset);

	trb->status = MTK_TRB_HEADER_ADDED;

	return 0;
}

/**
 * mtk_port_strip_header() - remove mtk_ccci_header from RX packet.
 * @skb: pointer to socket buffer.
 *
 * This function will help to remove mtk_ccci_header data from the head of skb->data.
 * But it will not check if the data of skb head is mtk_ccci_header actually.
 *
 * Return:
 *  ccci_h:	pointer to mtk_ccci_header stripped from socket buffer.
 *  NULL:	data length is invalid.
 */
struct mtk_ccci_header *mtk_port_strip_header(struct sk_buff *skb)
{
	struct mtk_ccci_header *ccci_h;

	if (skb->len < sizeof(*ccci_h)) {
		pr_err_ratelimited("[PORT][strip_header] Invalid input value, length:%d\n",
				   skb->len);
		return NULL;
	}

	ccci_h = (struct mtk_ccci_header *)skb->data;

	return ccci_h;
}

/**
 * mtk_port_status_update() - Update ports enumeration information.
 * @mdev: pointer to mtk_md_dev.
 * @data: pointer to mtk_port_enum_msg, which brings enumeration information.
 * @len: length of the data.
 *
 * This function called when host driver is doing handshake.
 * Structure mtk_port_enum_msg brings ports' enumeration information
 * from modem, and this function handles it and set "enable" of mtk_port
 * to "true" or "false".
 *
 * This function can sleep or can be called from interrupt context.
 *
 * Return:
 *  0:		success to update ports' status
 *  -EINVAL:	input parameter or members in input structure is illegal
 */
int mtk_port_status_update(struct mtk_md_dev *mdev, void *data, int len)
{
	struct mtk_port_enum_msg *msg = data;
	int port_id, msg_len, port_info_len;
	struct mtk_port_info *port_info;
	struct mtk_port_mngr *port_mngr;
	struct mtk_ctrl_blk *ctrl_blk;
	struct mtk_port *port;
	u16 ch_id;

	if (unlikely(!mdev || !msg)) {
		pr_err("[PORT][status_update] Invalid input value, mdev:%pK, msg:%pK\n",
		       mdev, msg);
		return -EINVAL;
	}

	ctrl_blk = mdev->ctrl_blk;
	port_mngr = ctrl_blk->port_mngr;
	msg_len = sizeof(*msg);
	port_info_len = sizeof(struct mtk_port_info);
	if (msg_len > len)
		return -EPROTO;

	if (le16_to_cpu(msg->version) != MTK_PORT_ENUM_VER ||
	    le32_to_cpu(msg->head_pattern) != MTK_PORT_ENUM_HEAD_PATTERN ||
	    le32_to_cpu(msg->tail_pattern) != MTK_PORT_ENUM_TAIL_PATTERN)
		return -EPROTO;

	for (port_id = 0; port_id < le16_to_cpu(msg->port_cnt) &&
	     msg_len + port_info_len * (port_id + 1) <= len; port_id++) {
		port_info = (struct mtk_port_info *)(msg->data + port_info_len * port_id);
		ch_id = FIELD_GET(MTK_INFO_FLD_CHID, le16_to_cpu(port_info->channel));
		port = mtk_port_search_by_id(port_mngr, ch_id);
		if (!port) {
			MTK_ERR(mdev, "Failed to find the port 0x%x\n", ch_id);
			continue;
		}
		port->enable = FIELD_GET(MTK_INFO_FLD_EN, le16_to_cpu(port_info->channel));
	}

	return 0;
}

/**
 * mtk_port_ch_enable() - Function for enable channel.
 * @port: pointer to channel structure for sending data.
 *
 * This function will be called when enable/create port.
 *
 * Return:
 *  trb->status if success.
 *  error value if fail.
 */
int mtk_port_ch_enable(struct mtk_port *port)
{
	struct mtk_port_mngr *port_mngr = port->port_mngr;
	struct trb_open_priv *trb_open_priv;
	struct sk_buff *skb;
	struct trb *trb;
	int ret;

	skb = mtk_mem_alloc_skb(port_mngr->ctrl_blk->bm_pool, Q_MTU_3_5K, 0);
	if (!skb) {
		MTK_WARN(port->port_mngr->ctrl_blk->mdev,
			 "Failed to alloc skb of port(%s)\n", port->info.name);
		return -ENOMEM;
	}
	/* add rx dispatch */
	trb_open_priv = (struct trb_open_priv *)skb->data;
	trb_open_priv->rx_done = mtk_port_rx_dispatch;

	skb_put(skb, sizeof(struct trb_open_priv));
	trb = (struct trb *)skb->cb;
	mtk_port_trb_init(port, trb, TRB_CMD_ENABLE, mtk_port_open_trb_complete);
	kref_get(&trb->kref);

	ret = port_mngr->ctrl_blk->ops->submit_skb(port_mngr->ctrl_blk->mdev, skb, true);
	if (ret) {
		MTK_ERR(port_mngr->ctrl_blk->mdev,
			"Failed to submit trb for port(%s), ret=%d\n", port->info.name, ret);
		kref_put(&trb->kref, mtk_port_trb_free);
		kref_put(&trb->kref, mtk_port_trb_free);
		return ret;
	}

start_wait:
	/* wait trb done */
	ret = wait_event_interruptible_timeout(port->trb_wq, trb->status <= 0,
					       MTK_DFLT_TRB_TIMEOUT);
	if (ret == -ERESTARTSYS)
		goto start_wait;
	else if (!ret)
		ret = -ETIMEDOUT;
	else
		ret = trb->status;

	kref_put(&trb->kref, mtk_port_trb_free);

	return ret;
}

/**
 * mtk_port_ch_disable() - Function for disable virtual queue.
 * @port: pointer to channel structure for sending data.
 *
 * This function will be called when disable/destroy port.
 *
 * Return:
 *  trb->status if success.
 *  error value if fail.
 */
int mtk_port_ch_disable(struct mtk_port *port)
{
	struct mtk_port_mngr *port_mngr = port->port_mngr;
	struct trb_close_priv *trb_close_priv;
	struct sk_buff *skb;
	struct trb *trb;
	int ret;

	skb = mtk_mem_alloc_skb(port_mngr->ctrl_blk->bm_pool, Q_MTU_3_5K, 0);
	if (!skb) {
		MTK_WARN(port_mngr->ctrl_blk->mdev,
			 "Failed to alloc skb of port(%s)\n", port->info.name);
		return -ENOMEM;
	}
	trb = (struct trb *)skb->cb;
	trb_close_priv = (struct trb_close_priv *)skb->data;
	mtk_port_trb_init(port, trb, TRB_CMD_DISABLE, mtk_port_close_trb_complete);
	kref_get(&trb->kref);

	trb_close_priv->txq_free_start_time = 0;
	trb_close_priv->txq_free_end_time = 0;
	trb_close_priv->rxq_free_start_time = 0;
	trb_close_priv->rxq_free_end_time = 0;
	trb_close_priv->disable_start_time  = jiffies;
	mutex_lock(&port->write_lock);
	ret = port_mngr->ctrl_blk->ops->submit_skb(port_mngr->ctrl_blk->mdev, skb, true);
	mutex_unlock(&port->write_lock);
	if (ret) {
		MTK_WARN(port_mngr->ctrl_blk->mdev,
			 "Failed to submit trb for port(%s), ret=%d\n", port->info.name, ret);
		kref_put(&trb->kref, mtk_port_trb_free);
		kref_put(&trb->kref, mtk_port_trb_free);
		return ret;
	}
	trb_close_priv->disable_end_time = jiffies;

start_wait:
	ret = wait_event_interruptible_timeout(port->trb_wq, trb->status <= 0,
					       MTK_DFLT_TRB_TIMEOUT);
	if (ret == -ERESTARTSYS) {
		goto start_wait;
	} else if (!ret) {
		ret = -ETIMEDOUT;
		WARN_ON(true);
	} else {
		ret = trb->status;
	}

	MTK_DBG(port_mngr->ctrl_blk->mdev, MTK_DBG_PORT, MTK_MEMLOG_RG_CTRL_DUMP,
		"port:%s, dis_t1:%lu, dis_t2:%lu, trb_t:%lu, txq_t1:%lu, txq_t2:%lu, rxq_t1:%lu, rxq_t2:%lu\n",
		port->info.name,
		trb_close_priv->disable_start_time,
		trb_close_priv->disable_end_time,
		trb_close_priv->trb_start_time,
		trb_close_priv->txq_free_start_time,
		trb_close_priv->txq_free_end_time,
		trb_close_priv->rxq_free_start_time,
		trb_close_priv->rxq_free_end_time);

	kref_put(&trb->kref, mtk_port_trb_free);
	return ret;
}

static void mtk_port_mngr_mdee_port_disable(struct mtk_port_mngr *port_mngr)
{
	struct mtk_port **ports;
	int ret, idx;

	ports = kcalloc(port_mngr->port_cnt, sizeof(struct mtk_port *), GFP_KERNEL);
	if (!ports) {
		MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to alloc ports\n");
		return;
	}

	ret = radix_tree_gang_lookup(&port_mngr->port_tbl[PORT_TBL_MD],
				     (void **)ports, 0, port_mngr->port_cnt);
	for (idx = 0; idx < ret; idx++) {
		if (ports[idx]->info.flags & PORT_F_MDEE_SPT) {
			ports[idx]->enable = false;
			clear_bit(PORT_S_WR, &ports[idx]->status);
			mtk_port_ch_disable(ports[idx]);
		} else if (test_bit(PORT_S_ENABLE, &ports[idx]->status)) {
			mtk_port_ch_disable(ports[idx]);
		}
	}

	kfree(ports);
}

static void mtk_port_mngr_mdee_port_enable(struct mtk_port_mngr *port_mngr)
{
	struct mtk_port **ports;
	int ret, idx;

	ports = kcalloc(port_mngr->port_cnt, sizeof(struct mtk_port *), GFP_KERNEL);
	if (!ports) {
		MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to alloc ports\n");
		return;
	}

	ret = radix_tree_gang_lookup(&port_mngr->port_tbl[PORT_TBL_MD],
				     (void **)ports, 0, port_mngr->port_cnt);
	for (idx = 0; idx < ret; idx++) {
		if (ports[idx]->info.flags & PORT_F_MDEE_SPT &&
		    test_bit(PORT_S_ENABLE, &ports[idx]->status)) {
			ports[idx]->tx_seq = 0;
			ports[idx]->rx_seq = 0;

			ports[idx]->enable = true;
			mtk_port_ch_enable(ports[idx]);
			set_bit(PORT_S_WR, &ports[idx]->status);
		} else if (test_bit(PORT_S_ENABLE, &ports[idx]->status)) {
			mtk_port_ch_enable(ports[idx]);
		}
	}

	kfree(ports);
}

static void mtk_port_mngr_mdee_disable_port_write(struct mtk_port_mngr *port_mngr)
{
	struct mtk_port **ports;
	int ret, idx;

	ports = kcalloc(port_mngr->port_cnt, sizeof(struct mtk_port *), GFP_KERNEL);
	if (!ports) {
		MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to alloc ports\n");
		return;
	}

	ret = radix_tree_gang_lookup(&port_mngr->port_tbl[PORT_TBL_MD],
				     (void **)ports, 0, port_mngr->port_cnt);
	for (idx = 0; idx < ret; idx++) {
		if (ports[idx]->info.flags & PORT_F_MDEE_SPT)
			continue;
		if (!ports[idx]->enable || !test_bit(PORT_S_ENABLE, &ports[idx]->status))
			continue;
		clear_bit(PORT_S_WR, &ports[idx]->status);
	}

	kfree(ports);
}

static void mtk_port_mngr_mdee_port_handler(struct mtk_port_mngr *port_mngr)
{
	struct mtk_port **ports;
	int ret, idx;

	ports = kcalloc(port_mngr->port_cnt, sizeof(struct mtk_port *), GFP_KERNEL);
	if (!ports) {
		MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to alloc ports\n");
		return;
	}

	ret = radix_tree_gang_lookup(&port_mngr->port_tbl[PORT_TBL_MD],
				     (void **)ports, 0, port_mngr->port_cnt);
	for (idx = 0; idx < ret; idx++) {
		if (ports[idx]->info.flags & PORT_F_MDEE_SPT) {
			ports[idx]->enable = true;
			if (!test_bit(PORT_S_ENABLE, &ports[idx]->status))
				ports_ops[ports[idx]->info.type]->enable(ports[idx]);
			if (ports[idx]->info.flags & PORT_F_MDEE_DL_ONLY)
				clear_bit(PORT_S_WR, &ports[idx]->status);
		}
	}

	kfree(ports);
}

static void mtk_port_mngr_fsm_flag_sap_hs_start_act(struct mtk_port_mngr *port_mngr)
{
	struct mtk_port *port;

	port = mtk_port_search_by_id(port_mngr, FW_INDEX_RX);
	if (port)
		ports_ops[port->info.type]->disable(port);

	port = mtk_port_search_by_id(port_mngr, CCCI_SAP_CONTROL_RX);
	if (!port) {
		MTK_ERR(port_mngr->ctrl_blk->mdev,
			"Failed to find sAP ctrl port\n");
		return;
	}
	ports_ops[port->info.type]->enable(port);

	port = mtk_port_search_by_name(port_mngr, "SAPFlowCtrl");
	if (port) {
		ports_ops[port->info.type]->enable(port);
		mtk_port_flowctrl_ch_start(port_mngr, "SAPFlowCtrl");
	}
}

static void mtk_port_mngr_fsm_flag_sap_hs2_done_act(struct mtk_port_mngr *port_mngr)
{
	struct mtk_port *port;

	/* Enable timesync port by default */
	port = mtk_port_search_by_id(port_mngr, CCCI_SAP_TIMESYNC_RX);
	if (port)
		port->enable = true;
	/* Enable minidump port by default */
	port = mtk_port_search_by_id(port_mngr, CCCI_SAP_MINIDUMP_NOTIFY_RX);
	if (port)
		port->enable = true;

	mtk_port_enable_by_type(port_mngr, PORT_TBL_SAP);

	/* The internal port "minidump_notify" opened by port mngr as user */
	port = mtk_port_internal_open(port_mngr->ctrl_blk->mdev,
				      "minidump_notify", 0);
	if (port)
		mtk_port_internal_recv_register(port, &mtk_minidump_port_skb_recv, port);

	port = mtk_port_search_by_name(port_mngr, "SAPMiscCtrl");
	if (port) {
		ports_ops[port->info.type]->enable(port);
		mtk_port_miscctrl_ch_start(port_mngr, "SAPMiscCtrl");
	}
}

void mtk_ports_dump(struct mtk_md_dev *mdev)
{
	struct mtk_ctrl_blk *ctrl_blk = mdev->ctrl_blk;
	enum mtk_port_tbl tbl_type = PORT_TBL_SAP;
	struct mtk_port **ports;
	int ret, idx;

	ports = kcalloc(ctrl_blk->port_mngr->port_cnt, sizeof(struct mtk_port *), GFP_KERNEL);
	if (!ports) {
		MTK_ERR(ctrl_blk->port_mngr->ctrl_blk->mdev, "Failed to alloc ports\n");
		return;
	}

	do {
		ret = radix_tree_gang_lookup(&ctrl_blk->port_mngr->port_tbl[tbl_type],
					     (void **)ports, 0, ctrl_blk->port_mngr->port_cnt);
		for (idx = 0; idx < ret; idx++)
			ports_ops[ports[idx]->info.type]->dump(ports[idx]);
	} while (++tbl_type < PORT_TBL_MAX);
	kfree(ports);
}

#define SAP_PORT_RAW_DATA_CFG_SHIFT 20
static void mtk_port_raw_data_flags_update(struct mtk_port *port)
{
	u32 dev_state, cfg;

	dev_state = mtk_dev_get_dev_state(port->port_mngr->ctrl_blk->mdev);
	cfg = dev_state >> SAP_PORT_RAW_DATA_CFG_SHIFT & 0x01;

	if (cfg)
		port->info.flags &= ~PORT_F_RAW_DATA;
	else
		port->info.flags |= PORT_F_RAW_DATA;
}

/**
 * mtk_port_mngr_fsm_state_handler() - Handle fsm event after state has been changed.
 * Handle events that need to be processed before ctrl trans.
 * @fsm_param: pointer to mtk_fsm_param, which including fsm state and event.
 * @arg: fsm will pass mtk_port_mngr structure back by using this parameter.
 *
 * This function will be registered to fsm by control block. If registered successful,
 * after fsm state has been changed, the fsm will call this function.
 *
 * This function can sleep or can be called from interrupt context.
 *
 * Return: No return value.
 */
void mtk_port_mngr_fsm_state_handler(struct mtk_fsm_param *fsm_param, void *arg)
{
	struct mtk_port_mngr *port_mngr;
	struct mtk_port *port;
	int evt_id;
	int flag;

	if (!fsm_param || !arg) {
		pr_err("[PORT][fsm_handler] Invalid input value, fsm_param:%pK, arg:%pK\n",
		       fsm_param, arg);
		return;
	}

	port_mngr = arg;
	evt_id = fsm_param->evt_id;
	flag = fsm_param->fsm_flag;

	switch (fsm_param->evt_id) {
	case FSM_EVT_GNSS_PORT_ENUM:
		mtk_port_enable_by_type(port_mngr, PORT_TBL_GNSS);
		return;
	case FSM_EVT_GNSS_DISABLE:
		mtk_port_disable_by_type(port_mngr, PORT_TBL_GNSS);
		return;
	default:
		break;
	}

	switch (fsm_param->to) {
	case FSM_STATE_ON:
		if (evt_id == FSM_EVT_REINIT)
			mtk_ports_reset(port_mngr, PORT_TBL_MASK_ALL);
		break;
	case FSM_STATE_BOOTUP:
		if (flag == FSM_F_DFLT) {
			port = mtk_port_search_by_id(port_mngr, BROM_INDEX_RX);
			if (port)
				ports_ops[port->info.type]->disable(port);
		} else if (flag & FSM_F_MD_REBOOT) {
			mtk_port_disable_by_type(port_mngr, PORT_TBL_MD);
			mtk_ports_reset(port_mngr, BIT(PORT_TBL_MD));
		}
		break;
	case FSM_STATE_READY:
		port = mtk_port_search_by_id(port_mngr, FBOOT_INDEX_RX);
		if (port)
			ports_ops[port->info.type]->disable(port);

		port = mtk_port_search_by_id(port_mngr, DUMP_INDEX_RX);
		if (port)
			ports_ops[port->info.type]->disable(port);

		mtk_port_enable_by_type(port_mngr, PORT_TBL_MD);
		break;
	case FSM_STATE_OFF:
		mtk_port_flowctrl_ch_stop(port_mngr, "MDFlowCtrl");
		mtk_port_flowctrl_ch_stop(port_mngr, "SAPFlowCtrl");
		mtk_port_miscctrl_ch_stop(port_mngr, "MDMiscCtrl");
		mtk_port_miscctrl_ch_stop(port_mngr, "SAPMiscCtrl");
		mtk_port_disable(port_mngr);
		break;
	case FSM_STATE_EXCEPTION:
		if (flag & FSM_F_MDEE_INIT) {
			mtk_ports_dump(port_mngr->ctrl_blk->mdev);
		} else if (flag & FSM_F_MDEE_CLEARQ_DONE) {
			mtk_port_mngr_mdee_port_disable(port_mngr);
		} else if (flag & FSM_F_EXCEPT_INT || flag & FSM_F_LINK_EXCEPTION) {
			mtk_port_flowctrl_ch_stop(port_mngr, "MDFlowCtrl");
			mtk_port_flowctrl_ch_stop(port_mngr, "SAPFlowCtrl");
			mtk_port_miscctrl_ch_stop(port_mngr, "MDMiscCtrl");
			mtk_port_miscctrl_ch_stop(port_mngr, "SAPMiscCtrl");
			mtk_port_disable(port_mngr);
		}
		break;
	default:
		break;
	}
}

static void mtk_port_mngr_fsm_state_download_handler(struct mtk_port_mngr *port_mngr, int flag)
{
	struct mtk_port *port;

	if (flag & FSM_F_DL_PORT_CREATE) {
		/* If in reinit flow, other ports may be enabled */
		mtk_port_disable(port_mngr);
		port = mtk_port_search_by_id(port_mngr, BROM_INDEX_RX);
		if (!port) {
			MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to find brom dl port\n");
			return;
		}

		if (brom_dl_flag) {
			/* Change to blocking mode to ensure accuracy */
			port->info.flags |= PORT_F_BLOCKING;
			ports_ops[port->info.type]->enable(port);
		} else {
			port->info.flags &= ~PORT_F_BLOCKING;
			mtk_brom_port_start_cmd(port);
		}
	} else if (flag & FSM_F_DL_DA) {
		port = mtk_port_search_by_id(port_mngr, BROM_INDEX_RX);
		if (!port) {
			MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to find brom dl port\n");
			return;
		}

		/* Change to non-blocking mode to ensure speed */
		port->info.flags &= ~PORT_F_BLOCKING;
	} else if (flag & FSM_F_DL_JUMPBL || flag & FSM_F_DL_TIMEOUT) {
		port = mtk_port_search_by_id(port_mngr, BROM_INDEX_RX);
		if (!port) {
			MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to find brom dl port\n");
			return;
		}

		ports_ops[port->info.type]->disable(port);
	} else if (flag & FSM_F_DL_FB) {
		port = mtk_port_search_by_id(port_mngr, FBOOT_INDEX_RX);
		if (!port) {
			MTK_ERR(port_mngr->ctrl_blk->mdev,
				"Failed to find fastboot download port\n");
			return;
		}

		mtk_port_raw_data_flags_update(port);
		ports_ops[port->info.type]->enable(port);
	} else if (flag & FSM_F_DL_PL) {
		/* BROM DL port may be still enabled due to jump to bl missing.
		 * Disable BROM DL port firstly if it's enabled.
		 */
		port = mtk_port_search_by_id(port_mngr, BROM_INDEX_RX);
		if (!port) {
			MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to find brom dl port\n");
			return;
		}

		if (test_bit(PORT_S_ENABLE, &port->status))
			ports_ops[port->info.type]->disable(port);

		port = mtk_port_search_by_id(port_mngr, FW_INDEX_RX);
		if (!port) {
			MTK_ERR(port_mngr->ctrl_blk->mdev,
				"Failed to find firmware download port\n");
			return;
		}

		mtk_port_raw_data_flags_update(port);
		ports_ops[port->info.type]->enable(port);
	} else if (flag & FSM_F_DL_JUMPLK) {
		port = mtk_port_search_by_id(port_mngr, FW_INDEX_RX);
		if (!port) {
			MTK_ERR(port_mngr->ctrl_blk->mdev,
				"Failed to find firmware download port\n");
			return;
		}

		ports_ops[port->info.type]->disable(port);
	} else {
		MTK_WARN(port_mngr->ctrl_blk->mdev, "Not support flag %d\n", flag);
	}
}

/**
 * mtk_port_mngr_fsm_state_handler_late() - Handle fsm event after state has been changed.
 * Handle events that need to be processed after ctrl trans.
 * @fsm_param: pointer to mtk_fsm_param, which including fsm state and event.
 * @arg: fsm will pass mtk_port_mngr structure back by using this parameter.
 *
 * This function will be registered to fsm by control block. If registered successful,
 * after fsm state has been changed, the fsm will call this function.
 *
 * This function can sleep or can be called from interrupt context.
 *
 * Return: No return value.
 */
void mtk_port_mngr_fsm_state_handler_late(struct mtk_fsm_param *fsm_param, void *arg)
{
	struct mtk_port_mngr *port_mngr;
	struct mtk_port *port;
	int evt_id, flag;

	if (!fsm_param || !arg) {
		pr_err("[PORT][fsm_handler] Invalid input value, fsm_param:%pK, arg:%pK\n",
		       fsm_param, arg);
		return;
	}

	port_mngr = arg;
	evt_id = fsm_param->evt_id;
	flag = fsm_param->fsm_flag;

	switch (fsm_param->evt_id) {
	case FSM_EVT_GNSS_ENABLE:
		port = mtk_port_search_by_id(port_mngr, CCCI_GNSS_CONTROL_RX);
		if (!port) {
			MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to find GNSS ctrl port\n");
			return;
		}
		mtk_ports_reset(port_mngr, BIT(PORT_TBL_GNSS));
		ports_ops[port->info.type]->enable(port);
		return;
	default:
		break;
	}

	switch (fsm_param->to) {
	case FSM_STATE_DOWNLOAD:
		mtk_port_mngr_fsm_state_download_handler(port_mngr, flag);
		break;
	case FSM_STATE_POSTDUMP:
		mtk_port_disable(port_mngr);
		port = mtk_port_search_by_id(port_mngr, DUMP_INDEX_RX);
		if (!port) {
			MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to find dump port\n");
			break;
		}

		mtk_port_raw_data_flags_update(port);
		ports_ops[port->info.type]->enable(port);
		break;
	case FSM_STATE_BOOTUP:
		if (flag & FSM_F_MD_HS_START) {
			port = mtk_port_search_by_id(port_mngr, CCCI_CONTROL_RX);
			if (!port) {
				MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to find MD ctrl port\n");
				break;
			}

			ports_ops[port->info.type]->enable(port);
		} else if (flag & FSM_F_SAP_HS_START) {
			mtk_port_mngr_fsm_flag_sap_hs_start_act(port_mngr);
		} else if (flag & FSM_F_MD_HS2_DONE) {
			port = mtk_port_search_by_id(port_mngr, CCCI_MD_RFS_RX);
			if (!port) {
				MTK_ERR(port_mngr->ctrl_blk->mdev, "Failed to find MD RFS port\n");
				break;
			}
			if (port->enable)
				ports_ops[port->info.type]->enable(port);

			port = mtk_port_search_by_name(port_mngr, "MDFlowCtrl");
			if (port) {
				ports_ops[port->info.type]->enable(port);
				mtk_port_flowctrl_ch_start(port_mngr, "MDFlowCtrl");
			}

			port = mtk_port_search_by_name(port_mngr, "MDMiscCtrl");
			if (port) {
				ports_ops[port->info.type]->enable(port);
				mtk_port_miscctrl_ch_start(port_mngr, "MDMiscCtrl");
			}
		} else if (flag & FSM_F_SAP_HS2_DONE) {
			mtk_port_mngr_fsm_flag_sap_hs2_done_act(port_mngr);
		}
		break;
	case FSM_STATE_EXCEPTION:
		if (flag & FSM_F_MDEE_INIT) {
			mtk_port_flowctrl_ch_stop(port_mngr, "MDFlowCtrl");
			mtk_port_flowctrl_ch_stop(port_mngr, "SAPFlowCtrl");
			mtk_port_miscctrl_ch_stop(port_mngr, "MDMiscCtrl");
			mtk_port_mngr_mdee_disable_port_write(port_mngr);
		} else if (flag & FSM_F_MDEE_INIT_DONE) {
			mtk_port_mngr_mdee_port_handler(port_mngr);
		} else if (flag & FSM_F_MDEE_ALLQ_RESET) {
			mtk_port_mngr_mdee_port_enable(port_mngr);
		} else if (flag & FSM_F_SAP_HS_START) {
			mtk_port_mngr_fsm_flag_sap_hs_start_act(port_mngr);
		} else if (flag & FSM_F_SAP_HS2_DONE) {
			mtk_port_mngr_fsm_flag_sap_hs2_done_act(port_mngr);
		}
		break;
	default:
		break;
	}
}

/**
 * mtk_port_mngr_init() - Initialize mtk_port_mngr and mtk_stale_list.
 * @ctrl_blk: pointer to mtk_ctrl_blk.
 * @port_cfg: pointer to port config table.
 * @port_cnt: port count.
 *
 * This function called after trans layer complete initialization.
 * Structure mtk_port_mngr is main body responsible for port management;
 * and this function alloc memory for it.
 * If port manager can't find stale list in stale list group by
 * using dev_str, it will also alloc memory for structure mtk_stale_list.
 * And then it will initialize port table and register fsm callback.
 *
 * Return:
 *  0:			-success to initialize mtk_port_mngr
 *  -ENOMEM:	-alloc memory for structure failed
 */
int mtk_port_mngr_init(struct mtk_ctrl_blk *ctrl_blk, struct mtk_port_cfg *port_cfg, int port_cnt)
{
	struct mtk_port_mngr *port_mngr;
	struct mtk_stale_list *s_list;
	int ret = -ENOMEM;
	int dev_id;

	/* 1.Init mtk_stale_list or re-use old one */
	s_list = mtk_port_stale_list_init(ctrl_blk, &dev_id);
	if (!s_list) {
		MTK_ERR(ctrl_blk->mdev, "Failed to init mtk_stale_list\n");
		goto err_out;
	}

	port_mngr = devm_kzalloc(ctrl_blk->mdev->dev, sizeof(*port_mngr), GFP_KERNEL);
	if (unlikely(!port_mngr)) {
		MTK_ERR(ctrl_blk->mdev, "Failed to alloc memory for port_mngr\n");
		goto err_exit_stale_list;
	}

	/* 2.Init port manager basic fields */
	port_mngr->ctrl_blk = ctrl_blk;
	port_mngr->dev_id = dev_id;

	/* 3.Put default ports and stale ports to port table */
	ret = mtk_port_tbl_create(port_mngr, port_cfg, port_cnt, s_list);
	if (unlikely(ret)) {
		MTK_ERR(ctrl_blk->mdev, "Failed to create port_tbl\n");
		goto err_free_port_mngr;
	}

	/* 4.Init port attribute file */
	ret = mtk_port_attr_init(port_mngr);
	if (unlikely(ret)) {
		MTK_ERR(ctrl_blk->mdev, "Failed to init port attribute file\n");
		goto err_destroy_port_tbl;
	}

	mtk_port_dbgfs_init(port_mngr);

	init_waitqueue_head(&port_mngr->flowctrl_wq);
	ctrl_blk->port_mngr = port_mngr;
	MTK_INFO(ctrl_blk->mdev, "Initialize port_mngr successfully\n");

	return ret;

err_destroy_port_tbl:
	mtk_port_tbl_destroy(port_mngr, s_list);
err_free_port_mngr:
	devm_kfree(ctrl_blk->mdev->dev, port_mngr);
err_exit_stale_list:
	mtk_port_stale_list_exit(ctrl_blk, s_list, dev_id);
err_out:
	return ret;
}

/**
 * mtk_port_mngr_exit() - Free the structure mtk_port_mngr.
 * @ctrl_blk: pointer to mtk_ctrl_blk.
 *
 * This function called before trans layer start to exit.
 * It will destroy port table and stale list, free port manager entity.
 * If there are ports that are opened, move these ports to stale list
 * and free the rest ports; if there are ports that are all closed,
 * then also free stale list.
 *
 * Return: No return value.
 */
void mtk_port_mngr_exit(struct mtk_ctrl_blk *ctrl_blk)
{
	struct mtk_port_mngr *port_mngr = ctrl_blk->port_mngr;
	struct mtk_stale_list *s_list;
	rwlock_t *port_mngr_lock;
	int dev_id;

	port_mngr_lock = mtk_port_get_port_mngr_lock(port_mngr->ctrl_blk->mdev->dev_str);
	s_list = mtk_port_stale_list_search(port_mngr->ctrl_blk->mdev->dev_str);
	dev_id = port_mngr->dev_id;

	mtk_port_dbgfs_exit(port_mngr);
	/* 1.exit port attribute file */
	mtk_port_attr_exit(port_mngr);
	/* 2.free or backup ports, then destroy port table */
	mtk_port_tbl_destroy(port_mngr, s_list);

	write_lock(port_mngr_lock);
	/* 4.free port_mngr structure */
	devm_kfree(ctrl_blk->mdev->dev, port_mngr);
	ctrl_blk->port_mngr = NULL;
	write_unlock(port_mngr_lock);
	/* 3.destroy stale list or backup register info to it */
	mtk_port_stale_list_exit(ctrl_blk, s_list, dev_id);
	MTK_INFO(ctrl_blk->mdev, "Exit port_mngr successfully\n");
}

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
EXPORT_SYMBOL_GPL(mtk_port_search_by_id);
EXPORT_SYMBOL_GPL(mtk_port_strip_header);
#endif

module_param(proprietary_test, bool, 0644);
MODULE_PARM_DESC(proprietary_test, "This value is used to test kernel API\n");
module_param(wwan_loopback_test, bool, 0644);
MODULE_PARM_DESC(wwan_loopback_test, "This value is used to test wwan port loopback\n");
module_param(relayfs_loopback_test, bool, 0644);
MODULE_PARM_DESC(relayfs_loopback_test, "This value is used to test wwan port loopback\n");
module_param(port_dump_enable, bool, 0644);
MODULE_PARM_DESC(port_dump_enable, "This value is used to control port dump\n");

