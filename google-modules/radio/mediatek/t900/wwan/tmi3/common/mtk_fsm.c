// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/kref.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/pci.h>
#include <linux/sched/debug.h>
#include <linux/sched/signal.h>
#include <linux/skbuff.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>
#include <linux/timer.h>
#include <linux/wait.h>

#include "mtk_debug.h"
#include "mtk_fsm.h"
#include "mtk_port.h"
#include "mtk_port_io.h"
#include "mtk_utility.h"
#ifdef CONFIG_TX00_UT_FSM
#include "ut_fsm_fake.h"
#endif

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#include "radio-bridge.h"
#endif

#define TAG "FSM"
#define EVT_TF_PAUSE		(0)
#define EVT_TF_GATECLOSED	(1)
#define EVT_TF_WLOCK		(2)
#define EVT_TF_TRM		(3)
#define EVT_TF_GNSS		(4)
#define EVT_TF_SKIP_MDEE_HS	(5)
#define MTK_FSM_INFO_LEN	(64)

#define FSM_FLAG_MD_HS_MASK	(FSM_F_MD_HS_START | FSM_F_MD_HS2_DONE | FSM_F_MD_HS4_DONE)

#define FSM_FLAG_MDEE_MASK	(FSM_F_MDEE_INIT | FSM_F_MDEE_INIT_DONE | FSM_F_MDEE_CLEARQ_DONE |\
				FSM_F_MDEE_ALLQ_RESET | FSM_F_MDEE_MSG |\
				FSM_F_MDEE_RECV_OK | FSM_F_MDEE_PASS)

#define DEV_EVT_D2H_MDEE_MASK	(DEV_EVT_D2H_EXCEPT_INIT | DEV_EVT_D2H_EXCEPT_INIT_DONE |\
				 DEV_EVT_D2H_EXCEPT_CLEARQ_DONE | DEV_EVT_D2H_EXCEPT_ALLQ_RESET)
#define RTFT_DATA_SIZE		(3 * 1024)
#define DEV_STATE_POLLER_INTVAL	(HZ / 20)
#define BOOTUP_DELAY_TIME	(HZ * 60)
#define MD_POWER_OFF_DELAY_TIME	(HZ * 40)
#define TRM_PROCESS_TIME_MS	(60 * 1000)

#define MDEE_CHK_ID		0x45584350
#define MDEE_REC_OK_CHK_ID	0x45524543

#define REGION_BITMASK		0xF
#define BROM_EVT_SHIFT		4
#define LK_EVT_SHIFT		8

#define HOST_EVT_SHIFT		28
#define HOST_REGION_BITMASK	0xF0000000

#define FSM_DEBUGFS_BUF_LEN	32
#define MTK_DEV_CTRL_CMD_LEN	20

#define FSM_WAKEUP_SOURCE_NAME		"fsm_wakeup_source"

enum host_event {
	HOST_EVT_INIT = 0,
	HOST_ENTER_DA = 2,
	HOST_READY_FOR_FB_RESET,
};

enum brom_event {
	BROM_EVT_NORMAL = 0,
	BROM_EVT_JUMP_BL,
	BROM_EVT_TIME_OUT,
	BROM_EVT_JUMP_DA,
	BROM_EVT_START_DL,
	BROM_EVT_FW_DL,
	BROM_EVT_JUMP_LK,
};

enum lk_event {
	LK_EVT_NORMAL = 0,
	LK_EVT_CREATE_PD_PORT,
	LK_EVT_CREATE_FB_PORT,
};

enum device_stage {
	DEV_STAGE_INIT = 0,
	DEV_STAGE_BROM1,
	DEV_STAGE_BROM2,
	DEV_STAGE_LK,
	DEV_STAGE_IDLE,
	DEV_STAGE_PL,
	DEV_STAGE_MAX
};

enum runtime_feature_support_type {
	RTFT_TYPE_NOT_EXIST			= 0,
	RTFT_TYPE_NOT_SUPPORT			= 1,
	RTFT_TYPE_MUST_SUPPORT			= 2,
	RTFT_TYPE_OPTIONAL_SUPPORT		= 3,
	RTFT_TYPE_SUPPORT_BACKWARD_COMPAT	= 4,
};

enum query_runtime_feature_id {
	QUERY_RTFT_ID_MD_PORT_ENUM	= 0,
	QUERY_RTFT_ID_SAP_PORT_ENUM	= 1,
	QUERY_RTFT_ID_MD_PORT_CFG	= 2,
	QUERY_RTFT_ID_MAX
};

enum ctrl_msg_id {
	CTRL_MSG_HS1			= 0,
	CTRL_MSG_HS2			= 1,
	CTRL_MSG_HS3			= 2,
	CTRL_MSG_HS4			= 3,
	CTRL_MSG_MDEE			= 4,
	CTRL_MSG_MDEE_REC_OK		= 6,
	CTRL_MSG_MDEE_PASS		= 8,
	CTRL_MSG_UNIFIED_PORT_CFG	= 11,
	CTRL_MSG_TRIGGER_MDEE	= 12,
};

struct timesync_zone {
	__le32 tz_minutewest;
	__le32 tz_dsttime;
};

struct high_res_timeinfo {
	__le64 utc_us;
	__le64 app_ref;
};

struct mtk_timesync_info {
	__le64 utc;
	struct timesync_zone tz;
	struct high_res_timeinfo hrt;
} __packed;

struct ctrl_msg_header {
	__le32 id;
	__le32 ex_msg;
	__le32 data_len;
	u8 reserved[];
} __packed;

struct runtime_feature_entry {
	u8 feature_id;
	struct runtime_feature_info support_info;
	u8 reserved[2];
	__le32 data_len;
	u8 data[];
};

struct feature_query {
	__le32 head_pattern;
	struct runtime_feature_info ft_set[FEATURE_CNT];
	__le32 tail_pattern;
};

struct mtk_fsm_notifier_block {
	struct list_head entry;
	struct notifier_block notifier;
	char uname[FSM_NB_NAME_MAX_LEN]; /* user name */
	void *priv_data; /* user private data */
	void (*cb)(struct notifier_fsm_state *state, void *priv_data);
};

struct notifier_fsm_state_entry {
	struct list_head entry;
	struct notifier_fsm_state *state;
};

struct mtk_dev_ctrl_cmd {
	char cmd[MTK_DEV_CTRL_CMD_LEN];
	int (*func)(void *mdev);
};

static unsigned short skip_mdee_hs;

static int mtk_dev_ctrl_trm_func(void *__mdev)
{
	struct mtk_md_dev *mdev = __mdev;
	struct mtk_md_fsm *fsm;
	int ret;

	fsm = mdev->fsm;
	if (test_and_set_bit(EVT_TF_TRM, &fsm->t_flag))
		return -EBUSY;

	ret = mtk_fsm_evt_submit(mdev, FSM_EVT_TRM_NOTIFY,
				 FSM_F_DFLT, NULL, 0, EVT_MODE_BLOCKING);

	/* mtk_fsm_evt_submit only return two values:
	 * FSM_EVT_RET_DONE (1)/ FSM_EVT_RET_FAIL (-1)
	 */
	if (ret == FSM_EVT_RET_DONE)
		return 0;

	clear_bit(EVT_TF_TRM, &fsm->t_flag);
	return ret;
}

static struct mtk_dev_ctrl_cmd dev_ctrl_tbl[] = {
	{"trm", mtk_dev_ctrl_trm_func},
};

static BLOCKING_NOTIFIER_HEAD(fsm_state_notifier_chain);
static LIST_HEAD(notifier_chain);
static struct mtk_md_fsm *external_fsm;
static DEFINE_SPINLOCK(external_fsm_lock);

int mtk_fsm_state_get(void *state, void *flag)
{
	unsigned long irq_flags;

	if (!state || !flag)
		return -EINVAL;

	spin_lock_irqsave(&external_fsm_lock, irq_flags);
	if (!external_fsm) {
		spin_unlock_irqrestore(&external_fsm_lock, irq_flags);
		return -ENODEV;
	}

	*(unsigned int *)state = external_fsm->state;
	*(unsigned int *)flag = external_fsm->fsm_flag;
	spin_unlock_irqrestore(&external_fsm_lock, irq_flags);
	return 0;
}
EXPORT_SYMBOL(mtk_fsm_state_get);

static int mtk_fsm_kernel_notifier_cb(struct notifier_block *nb, unsigned long action, void *data)
{
	struct mtk_fsm_notifier_block *fsm_nb;

	if (!nb || !data)
		return -EINVAL;

	fsm_nb = container_of(nb, struct mtk_fsm_notifier_block, notifier);
	pr_info("Before call cb(%s)!\n", fsm_nb->uname);
	fsm_nb->cb(data, fsm_nb->priv_data);

	return 0;
}

/**
 * mtk_fsm_kernel_notifier_register() - register fsm state change kernel notifier cb
 * @uname: user name, length must be less than 32 bytes
 * @cb: user notifier callback
 * @priv_data: user private data
 *
 * This function is used for kernel user to register notifier block cb.
 * When the fsm state changes, it will notify the user through cb.
 * If you don't need to receive fsm state changes,
 * call mtk_fsm_kernel_notifier_unregister() to unregister.
 *
 * Return: return value is 0 on success, a negative error code on failure.
 */
int mtk_fsm_kernel_notifier_register(char *uname,
				     void (*cb)(struct notifier_fsm_state *state, void *priv_data),
				     void *priv_data)
{
	struct mtk_fsm_notifier_block *fsm_nb;
	struct notifier_block *nb;

	if (!cb || !uname || strlen(uname) >= FSM_NB_NAME_MAX_LEN)
		return -EINVAL;

	fsm_nb = kzalloc(sizeof(*fsm_nb), GFP_KERNEL);
	if (!fsm_nb)
		return -ENOMEM;

	nb = &fsm_nb->notifier;
	nb->notifier_call = mtk_fsm_kernel_notifier_cb;
	strncpy(fsm_nb->uname, uname, FSM_NB_NAME_MAX_LEN - 1);
	fsm_nb->priv_data = priv_data;
	fsm_nb->cb = cb;

	if (blocking_notifier_chain_register(&fsm_state_notifier_chain, nb))
		goto free_fsm_nb;

	list_add_tail(&fsm_nb->entry, &notifier_chain);
	pr_info("%s register fsm notifier cb success!\n", fsm_nb->uname);

	return 0;

free_fsm_nb:
	kfree(fsm_nb);
	return -EEXIST;
}
EXPORT_SYMBOL(mtk_fsm_kernel_notifier_register);

/**
 * mtk_fsm_kernel_notifier_unregister() - unregister fsm state change kernel notifier cb
 * @uname: user name, length must be less than 32 bytes
 *
 * Return: return value is 0 on success, a negative error code on failure.
 */
int mtk_fsm_kernel_notifier_unregister(char *uname)
{
	struct mtk_fsm_notifier_block *fsm_nb, *next;

	if (!uname || strlen(uname) >= FSM_NB_NAME_MAX_LEN)
		return -EINVAL;

	list_for_each_entry_safe(fsm_nb, next, &notifier_chain, entry) {
		if (!strncmp(fsm_nb->uname, uname, FSM_NB_NAME_MAX_LEN - 1)) {
			if (blocking_notifier_chain_unregister(&fsm_state_notifier_chain,
							       &fsm_nb->notifier)) {
				pr_err("fsm_state_notifier_chain is err!\n");
				return -ENOENT;
			}

			list_del(&fsm_nb->entry);
			pr_info("%s unregister fsm notifier cb success!\n", fsm_nb->uname);
			kfree(fsm_nb);
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL(mtk_fsm_kernel_notifier_unregister);

void mtk_fsm_kernel_notifier_cleanup(void)
{
	struct mtk_fsm_notifier_block *fsm_nb, *next;

	list_for_each_entry_safe(fsm_nb, next, &notifier_chain, entry) {
		list_del(&fsm_nb->entry);
		blocking_notifier_chain_unregister(&fsm_state_notifier_chain, &fsm_nb->notifier);
		pr_info("%s unregister fsm notifier cb!\n", fsm_nb->uname);
		kfree(fsm_nb);
	}
}

static void mtk_fsm_kernel_nb_work(struct work_struct *work)
{
	struct mtk_md_fsm *fsm = container_of(work, struct mtk_md_fsm, nb_work);
	struct notifier_fsm_state_entry *state_entry, *tmp_state_entry;

	mutex_lock(&fsm->nb_list_mtx);
	list_for_each_entry_safe(state_entry, tmp_state_entry, &fsm->nb_list, entry) {
		list_del(&state_entry->entry);
		mutex_unlock(&fsm->nb_list_mtx);
		blocking_notifier_call_chain(&fsm_state_notifier_chain, 0, state_entry->state);
		devm_kfree(fsm->mdev->dev, state_entry->state);
		devm_kfree(fsm->mdev->dev, state_entry);

		mutex_lock(&fsm->nb_list_mtx);
	}
	mutex_unlock(&fsm->nb_list_mtx);
}

static int mtk_fsm_send_hs1_msg(struct fsm_hs_info *hs_info)
{
	struct mtk_md_fsm *fsm = container_of(hs_info, struct mtk_md_fsm, hs_info[hs_info->id]);
	struct ctrl_msg_header *ctrl_msg_h;
	struct feature_query *ft_query;
	struct sk_buff *skb;
	int ret, msg_size;

	msg_size = sizeof(*ctrl_msg_h) + sizeof(*ft_query);
	skb = __dev_alloc_skb(msg_size, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, msg_size);
	/* fill control message header */
	ctrl_msg_h = (struct ctrl_msg_header *)skb->data;
	ctrl_msg_h->id = cpu_to_le32(CTRL_MSG_HS1);
	ctrl_msg_h->ex_msg = 0;
	ctrl_msg_h->data_len = cpu_to_le32(sizeof(*ft_query));

	/* fill feature query structure */
	ft_query = (struct feature_query *)(skb->data + sizeof(*ctrl_msg_h));
	ft_query->head_pattern = cpu_to_le32(FEATURE_QUERY_PATTERN);
	memcpy(ft_query->ft_set, hs_info->query_ft_set, sizeof(hs_info->query_ft_set));
	ft_query->tail_pattern = cpu_to_le32(FEATURE_QUERY_PATTERN);

	/* send handshake1 message to device */
	MTK_HEX_DUMP(fsm->mdev, MTK_DBG_RDIT, MTK_MEMLOG_RG_CTRL_DUMP,
		     "Dumping hs1 msg:", skb->data, skb->len);
	ret = mtk_port_internal_write(hs_info->ctrl_port, skb);
	if (ret <= 0)
		return ret;

	return 0;
}

static int mtk_fsm_feature_set_match(enum runtime_feature_support_type *cur_ft_spt,
				     struct runtime_feature_info rtft_info_st,
				     struct runtime_feature_info rtft_info_cfg)
{
	int ret = 0;

	switch (FIELD_GET(FEATURE_TYPE, rtft_info_st.feature)) {
	case RTFT_TYPE_NOT_EXIST:
		fallthrough;
	case RTFT_TYPE_NOT_SUPPORT:
		*cur_ft_spt = RTFT_TYPE_NOT_EXIST;
		break;
	case RTFT_TYPE_MUST_SUPPORT:
		if (FIELD_GET(FEATURE_TYPE, rtft_info_cfg.feature) == RTFT_TYPE_NOT_EXIST ||
		    FIELD_GET(FEATURE_TYPE, rtft_info_cfg.feature) == RTFT_TYPE_NOT_SUPPORT)
			ret = -EPROTO;
		else
			*cur_ft_spt = RTFT_TYPE_MUST_SUPPORT;

		break;
	case RTFT_TYPE_OPTIONAL_SUPPORT:
		if (FIELD_GET(FEATURE_TYPE, rtft_info_cfg.feature) == RTFT_TYPE_NOT_EXIST ||
		    FIELD_GET(FEATURE_TYPE, rtft_info_cfg.feature) == RTFT_TYPE_NOT_SUPPORT) {
			*cur_ft_spt = RTFT_TYPE_NOT_SUPPORT;
		} else {
			if (FIELD_GET(FEATURE_VER, rtft_info_st.feature) ==
			    FIELD_GET(FEATURE_VER, rtft_info_cfg.feature))
				*cur_ft_spt = RTFT_TYPE_MUST_SUPPORT;
			else
				*cur_ft_spt = RTFT_TYPE_NOT_SUPPORT;
		}

		break;
	case RTFT_TYPE_SUPPORT_BACKWARD_COMPAT:
		if (FIELD_GET(FEATURE_VER, rtft_info_st.feature) >=
		    FIELD_GET(FEATURE_VER, rtft_info_cfg.feature))
			*cur_ft_spt = RTFT_TYPE_MUST_SUPPORT;
		else
			*cur_ft_spt = RTFT_TYPE_NOT_EXIST;

		break;
	default:
		ret = -EPROTO;
	}

	return ret;
}

static int (*query_rtft_action[FEATURE_CNT])(struct mtk_md_dev *mdev, void *rt_data, int len) = {
	[QUERY_RTFT_ID_MD_PORT_ENUM] = mtk_port_status_update,
	[QUERY_RTFT_ID_SAP_PORT_ENUM] = mtk_port_status_update,
};

static int mtk_fsm_parse_hs2_msg(struct fsm_hs_info *hs_info)
{
	struct mtk_md_fsm *fsm = container_of(hs_info, struct mtk_md_fsm, hs_info[hs_info->id]);
	char *rt_data = ((struct sk_buff *)hs_info->rt_data)->data;
	enum runtime_feature_support_type cur_ft_spt;
	struct runtime_feature_entry *rtft_entry;
	int ft_id, ret = 0, offset;
	int remain;

	offset = sizeof(struct feature_query);
	for (ft_id = 0; ft_id < FEATURE_CNT && offset < hs_info->rt_data_len; ft_id++) {
		remain = hs_info->rt_data_len - offset;
		if (remain < sizeof(*rtft_entry)) {
			MTK_ERR(fsm->mdev, "Remain length %d, not enough to rtft_entry\n", remain);
			break;
		}

		rtft_entry = (struct runtime_feature_entry *)(rt_data + offset);
		if (remain < sizeof(*rtft_entry) + le32_to_cpu(rtft_entry->data_len)) {
			MTK_ERR(fsm->mdev,
				"Remain length %d, not enough to rtft_entry + payload\n", remain);
			break;
		}

		ret = mtk_fsm_feature_set_match(&cur_ft_spt,
						rtft_entry->support_info,
						hs_info->query_ft_set[ft_id]);
		if (ret < 0)
			break;

		if (cur_ft_spt == RTFT_TYPE_MUST_SUPPORT)
			if (query_rtft_action[ft_id])
				ret = query_rtft_action[ft_id](fsm->mdev, rtft_entry->data,
							       le32_to_cpu(rtft_entry->data_len));

		if (ret < 0)
			break;

		offset += sizeof(*rtft_entry) + le32_to_cpu(rtft_entry->data_len);
	}

	if (ft_id != FEATURE_CNT) {
		MTK_ERR(fsm->mdev, "Unable to handle mistake hs2 msg, ft_id=%d\n", ft_id);
		MTK_HEX_DUMP(fsm->mdev, MTK_DBG_RDIT, MTK_MEMLOG_RG_CTRL_DUMP,
			     "Dumpping hs2 msg:", rt_data, hs_info->rt_data_len);
		ret = -EPROTO;
	}

	return ret;
}

static int mtk_fsm_rtft_sbp_id_action(struct mtk_md_dev *mdev, void *rt_data)
{
	int sbp_id = 0;

	*(int *)rt_data = sbp_id;

	return sizeof(*rt_data);
}

static int mtk_fsm_rtft_timesync_action(struct mtk_md_dev *mdev, void *rt_data)
{
	struct mtk_timesync_info *ts_info = (struct mtk_timesync_info *)rt_data;
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	ts_info->utc = cpu_to_le64(ts.tv_sec);

	return sizeof(*ts_info);
}

static int mtk_fsm_rtft_skip_mdee_hs_action(struct mtk_md_dev *mdev, void *rt_data)
{
	struct mtk_md_fsm *fsm = mdev->fsm;

	if (skip_mdee_hs) {
		set_bit(EVT_TF_SKIP_MDEE_HS, &fsm->t_flag);
		*(int *)rt_data = 1;
	} else {
		clear_bit(EVT_TF_SKIP_MDEE_HS, &fsm->t_flag);
		*(int *)rt_data = 0;
	}
	MTK_INFO(mdev, "respond MD with skip_mdee_hs = %d in hs3 runtime data.\n",
		 *(int *)rt_data);

	return sizeof(int);
}

static int (*support_rtft_action[FEATURE_CNT])(struct mtk_md_dev *mdev, void *rt_data) = {
	[SUPPORT_RTFT_ID_MD_SBP_ID] = mtk_fsm_rtft_sbp_id_action,
	[SUPPORT_RTFT_ID_MD_DSD] = NULL,
	[SUPPORT_RTFT_ID_MD_TIMESYNC] = mtk_fsm_rtft_timesync_action,
	[SUPPORT_RTFT_ID_SKIP_MDEE_HS] = mtk_fsm_rtft_skip_mdee_hs_action,
};

/**
 * mtk_fsm_supported_rtft_register() - register fsm supported runtime feature
 * @rtft_id: feature id
 * @cb: pointer to supported feature callback provided by user
 */
void mtk_fsm_supported_rtft_register(enum support_runtime_feature_id rtft_id,
				     int (*cb)(struct mtk_md_dev *mdev, void *rt_data))
{
	support_rtft_action[rtft_id] = cb;
	pr_info("%ps register rtft_id(%d) cb success!\n", __builtin_return_address(0), rtft_id);
}
EXPORT_SYMBOL(mtk_fsm_supported_rtft_register);

static int mtk_fsm_append_rtft_entries(struct mtk_md_dev *mdev, void *feature_data,
				       unsigned int *len, struct fsm_hs_info *hs_info)
{
	char *rt_data = ((struct sk_buff *)hs_info->rt_data)->data;
	struct runtime_feature_entry *rtft_entry;
	int ft_id, ret = 0, rtdata_len = 0;
	struct feature_query *ft_query;
	u8 version;

	ft_query = (struct feature_query *)rt_data;
	if (le32_to_cpu(ft_query->head_pattern) != FEATURE_QUERY_PATTERN ||
	    le32_to_cpu(ft_query->tail_pattern) != FEATURE_QUERY_PATTERN) {
		MTK_ERR(mdev,
			"Failed to match ft_query pattern: head=0x%x,tail=0x%x\n",
			le32_to_cpu(ft_query->head_pattern), le32_to_cpu(ft_query->tail_pattern));
		ret = -EPROTO;
		goto hs_err;
	}

	/* parse runtime feature query and fill runtime feature entry */
	rtft_entry = feature_data;
	for (ft_id = 0; ft_id < FEATURE_CNT && rtdata_len < RTFT_DATA_SIZE; ft_id++) {
		rtft_entry->feature_id = ft_id;
		rtft_entry->data_len = 0;

		switch (FIELD_GET(FEATURE_TYPE, ft_query->ft_set[ft_id].feature)) {
		case RTFT_TYPE_NOT_EXIST:
			fallthrough;
		case RTFT_TYPE_NOT_SUPPORT:
			fallthrough;
		case RTFT_TYPE_MUST_SUPPORT:
			rtft_entry->support_info = ft_query->ft_set[ft_id];
			break;
		case RTFT_TYPE_OPTIONAL_SUPPORT:
			if (FIELD_GET(FEATURE_VER, ft_query->ft_set[ft_id].feature) ==
			    FIELD_GET(FEATURE_VER, hs_info->supported_ft_set[ft_id].feature) &&
			    FIELD_GET(FEATURE_TYPE, hs_info->supported_ft_set[ft_id].feature) >=
			    RTFT_TYPE_MUST_SUPPORT)
				rtft_entry->support_info.feature =
					FIELD_PREP(FEATURE_TYPE, RTFT_TYPE_MUST_SUPPORT);
			else
				rtft_entry->support_info.feature =
					FIELD_PREP(FEATURE_TYPE, RTFT_TYPE_NOT_SUPPORT);

			version = FIELD_GET(FEATURE_VER, hs_info->supported_ft_set[ft_id].feature);
			rtft_entry->support_info.feature |= FIELD_PREP(FEATURE_VER, version);
			break;
		case RTFT_TYPE_SUPPORT_BACKWARD_COMPAT:
			if (FIELD_GET(FEATURE_VER, ft_query->ft_set[ft_id].feature) >=
			    FIELD_GET(FEATURE_VER, hs_info->supported_ft_set[ft_id].feature))
				rtft_entry->support_info.feature =
					FIELD_PREP(FEATURE_TYPE, RTFT_TYPE_MUST_SUPPORT);
			else
				rtft_entry->support_info.feature =
					FIELD_PREP(FEATURE_TYPE, RTFT_TYPE_NOT_SUPPORT);

			version = FIELD_GET(FEATURE_VER, hs_info->supported_ft_set[ft_id].feature);
			rtft_entry->support_info.feature |= FIELD_PREP(FEATURE_VER, version);
			break;
		}

		if (FIELD_GET(FEATURE_TYPE, rtft_entry->support_info.feature) ==
		    RTFT_TYPE_MUST_SUPPORT) {
			if (support_rtft_action[ft_id]) {
				ret = support_rtft_action[ft_id](mdev, rtft_entry->data);
				if (ret < 0) {
					MTK_ERR(mdev,
						"Failed to exe rtft act, ft_id = %d, ret = %d\n",
						ft_id, ret);
					goto hs_err;
				}

				rtft_entry->data_len = cpu_to_le32(ret);
			} else {
				rtft_entry->support_info.feature =
					FIELD_PREP(FEATURE_TYPE, RTFT_TYPE_NOT_SUPPORT);
			}
		}

		rtdata_len += sizeof(*rtft_entry) + le32_to_cpu(rtft_entry->data_len);
		rtft_entry = (struct runtime_feature_entry *)(feature_data + rtdata_len);
	}
	*len = rtdata_len;

	return 0;

hs_err:
	MTK_HEX_DUMP(mdev, MTK_DBG_RDIT, MTK_MEMLOG_RG_CTRL_DUMP,
		     "Dumping hs2 msg:", rt_data, hs_info->rt_data_len);
	*len = 0;
	return ret;
}

static int mtk_fsm_send_hs3_msg(struct fsm_hs_info *hs_info)
{
	struct mtk_md_fsm *fsm = container_of(hs_info, struct mtk_md_fsm, hs_info[hs_info->id]);
	unsigned int data_len, msg_size = 0;
	struct ctrl_msg_header *ctrl_msg_h;
	struct sk_buff *skb;
	int ret;

	skb = __dev_alloc_skb(RTFT_DATA_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	/* fill control message header */
	msg_size += sizeof(*ctrl_msg_h);
	ctrl_msg_h = (struct ctrl_msg_header *)skb->data;
	ctrl_msg_h->id = cpu_to_le32(CTRL_MSG_HS3);
	ctrl_msg_h->ex_msg = 0;
	ret = mtk_fsm_append_rtft_entries(fsm->mdev,
					  skb->data + sizeof(*ctrl_msg_h),
					  &data_len, hs_info);
	if (ret) {
		dev_kfree_skb_any(skb);
		return ret;
	}

	ctrl_msg_h->data_len = cpu_to_le32(data_len);
	msg_size += data_len;
	skb_put(skb, msg_size);
	/* send handshake3 message to device */
	MTK_HEX_DUMP(fsm->mdev, MTK_DBG_RDIT, MTK_MEMLOG_RG_CTRL_DUMP,
		     "Dumping hs3 msg:", skb->data, skb->len);
	ret = mtk_port_internal_write(hs_info->ctrl_port, skb);
	if (ret <= 0)
		return ret;

	return 0;
}

static int mtk_fsm_sap_ctrl_msg_handler(void *__fsm, struct sk_buff *skb)
{
	struct ctrl_msg_header *ctrl_msg_h;
	struct mtk_md_fsm *fsm = __fsm;
	struct fsm_hs_info *hs_info;
	int ret;

	ctrl_msg_h = (struct ctrl_msg_header *)skb->data;
	skb_pull(skb, sizeof(*ctrl_msg_h));

	hs_info = &fsm->hs_info[HS_ID_SAP];
	if (le32_to_cpu(ctrl_msg_h->id) != CTRL_MSG_HS2)
		return -EPROTO;

	hs_info->rt_data = skb;
	hs_info->rt_data_len = skb->len;
	ret = mtk_fsm_evt_submit(fsm->mdev, FSM_EVT_STARTUP,
				 hs_info->fsm_flag_hs2, hs_info, sizeof(*hs_info), 0);
	if (ret == FSM_EVT_RET_FAIL)
		dev_kfree_skb(skb);

	return 0;
}

static int mtk_fsm_md_ctrl_msg_handler(void *__fsm, struct sk_buff *skb)
{
	struct ctrl_msg_header *ctrl_msg_h;
	struct mtk_md_fsm *fsm = __fsm;
	struct fsm_hs_info *hs_info;
	bool consumed_skb = false;
	u32 ex_msg;
	int ret;

	ctrl_msg_h = (struct ctrl_msg_header *)skb->data;
	ex_msg = le32_to_cpu(ctrl_msg_h->ex_msg);
	hs_info = &fsm->hs_info[HS_ID_MD];
	switch (le32_to_cpu(ctrl_msg_h->id)) {
	case CTRL_MSG_HS2:
		skb_pull(skb, sizeof(*ctrl_msg_h));
		hs_info->rt_data = skb;
		hs_info->rt_data_len = skb->len;
		ret = mtk_fsm_evt_submit(fsm->mdev, FSM_EVT_STARTUP,
					 hs_info->fsm_flag_hs2, hs_info, sizeof(*hs_info), 0);
		if (ret != FSM_EVT_RET_FAIL)
			consumed_skb = true;
		break;
	case CTRL_MSG_HS4:
		mtk_fsm_evt_submit(fsm->mdev, FSM_EVT_STARTUP,
				   hs_info->fsm_flag_hs4, hs_info, sizeof(*hs_info), 0);
		break;
	case CTRL_MSG_MDEE:
		if (ex_msg != MDEE_CHK_ID)
			MTK_ERR(fsm->mdev, "Unable to match MDEE pkt(0x%x)\n", ex_msg);
		else
			mtk_fsm_evt_submit(fsm->mdev, FSM_EVT_MDEE,
					   FSM_F_MDEE_MSG, hs_info, sizeof(*hs_info), 0);

		break;
	case CTRL_MSG_MDEE_REC_OK:
		if (ex_msg != MDEE_REC_OK_CHK_ID)
			MTK_ERR(fsm->mdev, "Unable to match MDEE REC OK pkt(0x%x)\n", ex_msg);
		else
			mtk_fsm_evt_submit(fsm->mdev, FSM_EVT_MDEE,
					   FSM_F_MDEE_RECV_OK, NULL, 0, 0);

		break;
	case CTRL_MSG_MDEE_PASS:
		mtk_fsm_evt_submit(fsm->mdev, FSM_EVT_MDEE, FSM_F_MDEE_PASS, NULL, 0, 0);
		break;
	case CTRL_MSG_UNIFIED_PORT_CFG:
		mtk_port_tbl_update(fsm->mdev, skb->data + sizeof(*ctrl_msg_h),
				    skb->len - sizeof(*ctrl_msg_h));
		if (mtk_port_internal_write(hs_info->ctrl_port, skb) <= 0)
			MTK_ERR(fsm->mdev, "Unable to send port config ack msg\n");

		consumed_skb = true;
		break;
	default:
		MTK_ERR(fsm->mdev, "Invalid ctrl msg id\n");
	}

	if (!consumed_skb)
		dev_kfree_skb(skb);

	return 0;
}

static int mtk_fsm_gnss_ctrl_msg_handler(void *__fsm, struct sk_buff *skb)
{
	struct ctrl_msg_header *ctrl_msg_h;
	struct mtk_md_fsm *fsm = __fsm;
	int ret;

	ctrl_msg_h = (struct ctrl_msg_header *)skb->data;
	skb_pull(skb, sizeof(*ctrl_msg_h));

	if (le32_to_cpu(ctrl_msg_h->id) != CTRL_MSG_UNIFIED_PORT_CFG)
		return -EPROTO;

	ret = mtk_fsm_evt_submit(fsm->mdev, FSM_EVT_GNSS_PORT_ENUM, FSM_F_DFLT, skb, skb->len, 0);
	if (ret == FSM_EVT_RET_FAIL)
		dev_kfree_skb(skb);
	return 0;
}

static int (*ctrl_msg_handler[HS_ID_MAX])(void *__fsm, struct sk_buff *skb) = {
	[HS_ID_MD] = mtk_fsm_md_ctrl_msg_handler,
	[HS_ID_SAP] = mtk_fsm_sap_ctrl_msg_handler,
	[HS_ID_GNSS] = mtk_fsm_gnss_ctrl_msg_handler,
};

static ssize_t fsm_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct mtk_md_fsm *fsm;

	fsm = ((struct mtk_md_dev *)pci_get_drvdata(pdev))->fsm;
	if (fsm)
		return scnprintf(buf, FSM_DEBUGFS_BUF_LEN,
				"state=%d, fsm_flag=0x%x\n", fsm->state, fsm->fsm_flag);

	return scnprintf(buf, FSM_DEBUGFS_BUF_LEN, "Invalid param!\n");
}

static DEVICE_ATTR_RO(fsm_state);

static ssize_t mtk_device_ctrl_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct mtk_dev_ctrl_cmd *ctrl_cmd = NULL;
	struct mtk_md_dev *mdev;
	struct pci_dev *pdev;
	ssize_t ret;
	u32 i;

	if (unlikely(!buf || !count))
		return -EINVAL;

	pdev = container_of(dev, struct pci_dev, dev);
	mdev = (struct mtk_md_dev *)pci_get_drvdata(pdev);

	for (i = 0; i < ARRAY_SIZE(dev_ctrl_tbl); i++) {
		if (!strncmp(buf, dev_ctrl_tbl[i].cmd, strlen(dev_ctrl_tbl[i].cmd))) {
			ctrl_cmd = &dev_ctrl_tbl[i];
			break;
		}
	}

	if (!ctrl_cmd) {
		MTK_INFO(mdev, "not found %s in dev_ctrl_tbl\n", buf);
		return -EINVAL;
	}

	ret = ctrl_cmd->func(mdev);
	if (!ret)
		return count;

	return ret;
}
static DEVICE_ATTR_WO(mtk_device_ctrl);

static inline int mtk_fsm_get_trm_wake_lock(struct mtk_md_fsm *fsm)
{
	if (test_bit(EVT_TF_WLOCK, &fsm->t_flag))
		return -EBUSY;

	/* only the first trm cmd get wakelock one time */
	__pm_wakeup_event(fsm->fsm_ws, TRM_PROCESS_TIME_MS);
	set_bit(EVT_TF_WLOCK, &fsm->t_flag);
	return 0;
}

static inline void mtk_fsm_put_trm_wake_lock(struct mtk_md_fsm *fsm)
{
	if (test_bit(EVT_TF_WLOCK, &fsm->t_flag)) {
		/* since WLOCK set, FSM_EVT_TRM_NOTIFY was sent to sAP */
		__pm_relax(fsm->fsm_ws);
		clear_bit(EVT_TF_WLOCK, &fsm->t_flag);
		/* enable next trm command from user space */
		clear_bit(EVT_TF_TRM, &fsm->t_flag);
	}
}

static void mtk_fsm_host_evt_ack(struct mtk_md_dev *mdev, enum host_event id)
{
	u32 dev_state;

	dev_state = mtk_dev_get_dev_state(mdev);
	dev_state &= ~HOST_REGION_BITMASK;
	dev_state |= id << HOST_EVT_SHIFT;
	mtk_dev_ack_dev_state(mdev, dev_state);

	dev_state = mtk_dev_get_dev_state(mdev);
	if (!(dev_state & id << HOST_EVT_SHIFT))
		MTK_ERR(mdev, "Failed to ack device, state=0x%x", dev_state);
}

static void mtk_fsm_brom_evt_handler(struct mtk_md_dev *mdev, u32 dev_state)
{
	u32 brom_evt = dev_state >> BROM_EVT_SHIFT & REGION_BITMASK;

	switch (brom_evt) {
	case BROM_EVT_JUMP_BL:
		mtk_fsm_evt_submit(mdev, FSM_EVT_DOWNLOAD, FSM_F_DL_JUMPBL, NULL, 0, 0);
		break;
	case BROM_EVT_TIME_OUT:
		mtk_fsm_evt_submit(mdev, FSM_EVT_DOWNLOAD, FSM_F_DL_TIMEOUT, NULL, 0, 0);
		break;
	case BROM_EVT_JUMP_DA:
		mtk_fsm_host_evt_ack(mdev, HOST_ENTER_DA);
		mtk_fsm_evt_submit(mdev, FSM_EVT_DOWNLOAD, FSM_F_DL_DA, NULL, 0, 0);
		break;
	case BROM_EVT_START_DL:
		mtk_fsm_evt_submit(mdev, FSM_EVT_DOWNLOAD, FSM_F_DL_PORT_CREATE, NULL, 0, 0);
		break;
	default:
		MTK_ERR(mdev, "Invalid brom event, value = 0x%x\n", dev_state);
	}
}

static void mtk_fsm_lk_evt_handler(struct mtk_md_dev *mdev, u32 dev_state)
{
	u32 lk_evt = dev_state >> LK_EVT_SHIFT & REGION_BITMASK;

	switch (lk_evt) {
	case LK_EVT_NORMAL:
		MTK_INFO(mdev, "value=0x%x\n", dev_state);
		break;
	case LK_EVT_CREATE_PD_PORT:
		mtk_fsm_evt_submit(mdev, FSM_EVT_POSTDUMP, FSM_F_DFLT, NULL, 0, 0);
		break;
	case LK_EVT_CREATE_FB_PORT:
		mtk_fsm_evt_submit(mdev, FSM_EVT_DOWNLOAD, FSM_F_DL_FB, NULL, 0, 0);
		break;
	default:
		MTK_ERR(mdev, "Invalid LK event, value = 0x%x\n", dev_state);
	}
}

static void mtk_fsm_idle_evt_handler(struct mtk_md_dev *mdev,
				     u32 dev_state, struct mtk_md_fsm *fsm)
{
	int hs_id;

	fsm->hs_done_flag = fsm->cfg->get_hs_done_flags(mdev, dev_state);
	if (fsm->hs_done_flag & FSM_F_MD_HS4_DONE)
		fsm->hs_info[HS_ID_MD].fsm_flag_hs4 = FSM_F_MD_HS4_DONE;

	mtk_fsm_evt_submit(mdev, FSM_EVT_STARTUP, FSM_F_DFLT, NULL, 0, 0);

	for (hs_id = 0; hs_id < HS_ID_MAX; hs_id++)
		mtk_dev_unmask_dev_evt(mdev, fsm->hs_info[hs_id].mhccif_ch);

	mtk_dev_unmask_dev_evt(mdev, DEV_EVT_D2H_MDEE_MASK);
	mtk_dev_unmask_dev_evt(mdev, DEV_EVT_D2H_MD_REBOOT);
}

static void mtk_fsm_pl_evt_handler(struct mtk_md_dev *mdev, u32 dev_state)
{
	switch (dev_state >> BROM_EVT_SHIFT & REGION_BITMASK) {
	case BROM_EVT_FW_DL:
		mtk_fsm_evt_submit(mdev, FSM_EVT_DOWNLOAD, FSM_F_DL_PL, NULL, 0, 0);
		break;
	case BROM_EVT_JUMP_LK:
		mtk_fsm_evt_submit(mdev, FSM_EVT_DOWNLOAD, FSM_F_DL_JUMPLK, NULL, 0, 0);
		break;
	default:
		MTK_ERR(mdev, "Invalid pl event, value = 0x%x\n", dev_state);
	}
}

static void mtk_fsm_bootup_dump_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mtk_md_fsm *fsm;

	fsm = container_of(dwork, struct mtk_md_fsm, bootup_dump_work);
	mtk_fsm_evt_submit(fsm->mdev, FSM_EVT_DUMP, FSM_F_DFLT, NULL, 0, 0);
}

static void mtk_fsm_md_power_off_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mtk_md_fsm *fsm;

	fsm = container_of(dwork, struct mtk_md_fsm, md_power_off_work);

	mtk_dev_mask_dev_evt(fsm->mdev, DEV_EVT_D2H_MD_POWER_OFF);
	mtk_dev_clear_dev_evt(fsm->mdev, DEV_EVT_D2H_MD_POWER_OFF);
	mtk_fsm_evt_submit(fsm->mdev, FSM_EVT_MD_POWER_OFF_TIMEOUT, FSM_F_DFLT, NULL, 0, 0);
}

static int mtk_fsm_early_bootup_handler(struct mtk_md_fsm *fsm)
{
	struct mtk_md_dev *mdev = fsm->mdev;
	u32 dev_state, dev_stage;

	dev_state = mtk_dev_get_dev_state(mdev);
	dev_stage = dev_state & REGION_BITMASK;
	if (dev_stage >= DEV_STAGE_MAX) {
		MTK_ERR(fsm->mdev, "Invalid dev state 0x%x\n", dev_state);
		return -ENXIO;
	}

	if (dev_state == fsm->last_dev_state)
		goto exit;

	MTK_INFO(mdev, "Device stage change 0x%x->0x%x\n", fsm->last_dev_state, dev_state);
	fsm->last_dev_state = dev_state;

	cancel_delayed_work(&fsm->bootup_dump_work);
	queue_delayed_work(system_wq, &fsm->bootup_dump_work, BOOTUP_DELAY_TIME);

	switch (dev_stage) {
	case DEV_STAGE_BROM1:
		fallthrough;
	case DEV_STAGE_BROM2:
		mtk_fsm_brom_evt_handler(mdev, dev_state);
		break;
	case DEV_STAGE_LK:
		mtk_fsm_lk_evt_handler(mdev, dev_state);
		break;
	case DEV_STAGE_IDLE:
		mtk_fsm_idle_evt_handler(mdev, dev_state, fsm);
		break;
	case DEV_STAGE_PL:
		mtk_fsm_pl_evt_handler(mdev, dev_state);
		break;
	default:
		break;
	}

exit:
	return dev_stage;
}

static void mtk_fsm_dev_state_poller_handler(struct timer_list *t)
{
	struct mtk_md_fsm *fsm = from_timer(fsm, t, dev_state_poller);
	int ret;

	ret = mtk_fsm_early_bootup_handler(fsm);
	if (ret >= 0 && ret != DEV_STAGE_IDLE)
		mod_timer(&fsm->dev_state_poller, jiffies + DEV_STATE_POLLER_INTVAL);
}

static int mtk_fsm_dev_state_interrupt_mode_handler(u32 status, void *__fsm)
{
	struct mtk_md_fsm *fsm = __fsm;
	int ret;

	mtk_dev_mask_dev_evt(fsm->mdev, status);
	mtk_dev_clear_dev_evt(fsm->mdev, status);

	ret = mtk_fsm_early_bootup_handler(fsm);
	if (ret < 0)
		return ret;

	if (ret != DEV_STAGE_IDLE)
		mtk_dev_unmask_dev_evt(fsm->mdev, DEV_EVT_D2H_BOOT_FLOW_SYNC);

	return 0;
}

static int mtk_fsm_ctrl_ch_start(struct mtk_md_fsm *fsm, struct fsm_hs_info *hs_info, int flag)
{
	if (!hs_info->ctrl_port) {
		hs_info->ctrl_port = mtk_port_internal_open(fsm->mdev, hs_info->port_name, flag);
		if (!hs_info->ctrl_port) {
			MTK_ERR(fsm->mdev, "Failed to open ctrl port(%s)\n",
				hs_info->port_name);
			return -ENODEV;
		}

		mtk_port_internal_recv_register(hs_info->ctrl_port,
						ctrl_msg_handler[hs_info->id], fsm);
	}

	return 0;
}

static void mtk_fsm_ctrl_ch_stop(struct mtk_md_fsm *fsm)
{
	struct fsm_hs_info *hs_info;
	int hs_id;

	for (hs_id = 0; hs_id < HS_ID_MAX; hs_id++) {
		hs_info = &fsm->hs_info[hs_id];
		if (hs_info->ctrl_port) {
			mtk_port_internal_close(hs_info->ctrl_port);
			hs_info->ctrl_port = NULL;
		}
	}
}

static void mtk_fsm_enq_nb_work(struct mtk_md_fsm *fsm)
{
	struct notifier_fsm_state_entry *nstate_entry;
	struct notifier_fsm_state *nstate;

	nstate_entry = devm_kzalloc(fsm->mdev->dev, sizeof(*nstate_entry), GFP_KERNEL);
	if (!nstate_entry)
		return;

	nstate = devm_kzalloc(fsm->mdev->dev, sizeof(*nstate), GFP_KERNEL);
	if (!nstate) {
		devm_kfree(fsm->mdev->dev, nstate_entry->state);
		return;
	}

	nstate->to_state = fsm->state;
	nstate->fsm_flag = fsm->fsm_flag;
	nstate_entry->state = nstate;
	mutex_lock(&fsm->nb_list_mtx);
	list_add_tail(&nstate_entry->entry, &fsm->nb_list);
	mutex_unlock(&fsm->nb_list_mtx);
	schedule_work(&fsm->nb_work);
}

#define FSM_STATE_NOTIFIER_THRESHOLD 30

static void mtk_fsm_event_notify(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	unsigned long start_jiffies, end_jiffies;
	struct mtk_fsm_notifier *nt;
	struct mtk_fsm_param param;

	param.from = fsm->state;
	param.to = FSM_STATE_INVALID;
	param.evt_id = event ? event->id : FSM_EVT_MAX;
	param.fsm_flag = event ? event->fsm_flag : FSM_F_DFLT;
	param.full_fsm_flags = fsm->fsm_flag;

	list_for_each_entry(nt, &fsm->pre_notifiers, entry) {
		fsm->notifier_record = nt->id;
		start_jiffies = jiffies;
		nt->cb(&param, nt->data);
		end_jiffies = jiffies;
		if ((end_jiffies - start_jiffies) > FSM_STATE_NOTIFIER_THRESHOLD)
			MTK_DBG(fsm->mdev, MTK_DBG_FSM, MTK_MEMLOG_RG_COMMON,
				"Fsm event pre notifier(%d) processed time :%d(ms)\n",
				nt->id, jiffies_to_msecs(end_jiffies - start_jiffies));
	}

	list_for_each_entry(nt, &fsm->post_notifiers, entry) {
		fsm->notifier_record = nt->id;
		start_jiffies = jiffies;
		nt->cb(&param, nt->data);
		end_jiffies = jiffies;
		if ((end_jiffies - start_jiffies) > FSM_STATE_NOTIFIER_THRESHOLD)
			MTK_DBG(fsm->mdev, MTK_DBG_FSM, MTK_MEMLOG_RG_COMMON,
				"Fsm event post notifier(%d) processed time :%d(ms)\n",
				nt->id, jiffies_to_msecs(end_jiffies - start_jiffies));
	}

	fsm->notifier_record = MTK_USER_MAX;
}

static void mtk_fsm_switch_state(struct mtk_md_fsm *fsm, enum mtk_fsm_state to_state,
				 struct mtk_fsm_evt *event)
{
	unsigned long start_jiffies, end_jiffies;
	char fsm_info[MTK_FSM_INFO_LEN];
	struct mtk_fsm_notifier *nt;
	struct mtk_fsm_param param;

	param.from = fsm->state;
	param.to = to_state;
	param.evt_id = event ? event->id : FSM_EVT_MAX;
	param.fsm_flag = event ? event->fsm_flag : FSM_F_DFLT;
	param.full_fsm_flags = fsm->fsm_flag;

	list_for_each_entry(nt, &fsm->pre_notifiers, entry) {
		fsm->notifier_record = nt->id;
		start_jiffies = jiffies;
		nt->cb(&param, nt->data);
		end_jiffies = jiffies;
		if ((end_jiffies - start_jiffies) > FSM_STATE_NOTIFIER_THRESHOLD)
			MTK_DBG(fsm->mdev, MTK_DBG_FSM, MTK_MEMLOG_RG_COMMON,
				"Fsm pre notifier(%d) processed time :%d(ms)\n",
				nt->id, jiffies_to_msecs(end_jiffies - start_jiffies));
	}

	fsm->notifier_record = MTK_USER_MAX;
	fsm->state = to_state;
	fsm->fsm_flag |= event ? event->fsm_flag : FSM_F_DFLT;
	param.full_fsm_flags = fsm->fsm_flag;
	MTK_INFO(fsm->mdev, "FSM transited to state=%d, fsm_flag=0x%x\n",
		 to_state, fsm->fsm_flag);
	MTK_DBG(fsm->mdev, MTK_DBG_FSM, MTK_MEMLOG_RG_COMMON,
		"FSM transited to state=%d, fsm_flag=0x%x\n", to_state, fsm->fsm_flag);

	snprintf(fsm_info, MTK_FSM_INFO_LEN,
		 "state=%d, fsm_flag=0x%x", to_state, fsm->fsm_flag);
	mtk_uevent_notify(fsm->mdev->dev, MTK_UEVENT_FSM, fsm_info);
	mtk_fsm_enq_nb_work(fsm);

	list_for_each_entry(nt, &fsm->post_notifiers, entry) {
		fsm->notifier_record = nt->id;
		start_jiffies = jiffies;
		nt->cb(&param, nt->data);
		end_jiffies = jiffies;
		if ((end_jiffies - start_jiffies) > FSM_STATE_NOTIFIER_THRESHOLD)
			MTK_DBG(fsm->mdev, MTK_DBG_FSM, MTK_MEMLOG_RG_COMMON,
				"Fsm post notifier(%d) processed time :%d(ms)\n",
				nt->id, jiffies_to_msecs(end_jiffies - start_jiffies));
	}

	fsm->notifier_record = MTK_USER_MAX;
}

static int mtk_fsm_startup_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	enum mtk_fsm_state to_state = FSM_STATE_BOOTUP;
	struct fsm_hs_info *hs_info = event->data;
	struct mtk_md_dev *mdev = fsm->mdev;
	int ret = 0;

	if ((fsm->state != FSM_STATE_ON && fsm->state != FSM_STATE_DOWNLOAD &&
	     fsm->state != FSM_STATE_BOOTUP && fsm->state != FSM_STATE_EXCEPTION) ||
	     ((fsm->fsm_flag & FSM_F_MD_REBOOT) && (event->fsm_flag & FSM_FLAG_MD_HS_MASK))) {
		ret = -EPROTO;
		goto free_rt_data;
	}

	if (fsm->state == FSM_STATE_EXCEPTION) {
		if (event->fsm_flag & FSM_FLAG_MD_HS_MASK) {
			ret = -EPROTO;
			goto free_rt_data;
		}

		/* keep in MDEE state when handle sAP HS msg after MDEE */
		to_state = FSM_STATE_EXCEPTION;
	}

	if (fsm->state != FSM_STATE_BOOTUP && fsm->state != FSM_STATE_EXCEPTION) {
		mtk_fsm_switch_state(fsm, to_state, event);
		return 0;
	}

	if (event->fsm_flag & FSM_HS_START_MASK) {
		mtk_fsm_switch_state(fsm, to_state, event);

		ret = mtk_fsm_ctrl_ch_start(fsm, hs_info, O_NONBLOCK);
		if (!ret)
			ret = mtk_fsm_send_hs1_msg(hs_info);

		if (ret)
			goto hs_err;
	} else if (event->fsm_flag & FSM_HS2_DONE_MASK) {
		ret = mtk_fsm_parse_hs2_msg(hs_info);
		if (!ret) {
			mtk_fsm_switch_state(fsm, to_state, event);
			ret = mtk_fsm_send_hs3_msg(hs_info);
			if (ret)
				hs_info->hs_status = HS3_FAIL;
		}

		dev_kfree_skb(hs_info->rt_data);
		hs_info->rt_data = NULL;
		if (ret)
			goto hs_err;

		if (event->fsm_flag & FSM_F_SAP_HS2_DONE) {
			mtk_dev_unmask_dev_evt(fsm->mdev, DEV_EVT_D2H_GNSS_ENABLE);
			mtk_dev_unmask_dev_evt(fsm->mdev, DEV_EVT_D2H_GNSS_DISABLE);
		}
	} else if (event->fsm_flag & FSM_F_MD_HS4_DONE) {
		mtk_fsm_switch_state(fsm, to_state, event);
	}

	/* if either: 1.Send hs3 fails or 2.MD HS completes first, and then MD EE completes and
	 * then sAP HS completes, the FSM should not switch to READY.
	 */
	if (fsm->state != FSM_STATE_EXCEPTION &&
	    (((fsm->fsm_flag | event->fsm_flag) & fsm->hs_done_flag) == fsm->hs_done_flag) &&
	    (!fsm->hs_info[HS_ID_MD].hs_status &&
	     !fsm->hs_info[HS_ID_SAP].hs_status)) {
		to_state = FSM_STATE_READY;
		mtk_fsm_switch_state(fsm, to_state, NULL);
		cancel_delayed_work(&fsm->bootup_dump_work);
	}

	return 0;

free_rt_data:
	if (hs_info && hs_info->rt_data) {
		dev_kfree_skb(hs_info->rt_data);
		hs_info->rt_data = NULL;
	}
hs_err:
	MTK_ERR(mdev, "Failed to hs with device %d:0x%x, ret=%d", fsm->state, fsm->fsm_flag, ret);
	return ret;
}

static int mtk_fsm_mdee_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	struct mtk_md_dev *mdev = fsm->mdev;
	struct ctrl_msg_header *ctrl_msg_h;
	struct fsm_hs_info *hs_info;
	struct sk_buff *ctrl_msg;
	int ret;

	if ((fsm->state != FSM_STATE_BOOTUP && fsm->state != FSM_STATE_READY &&
	     fsm->state != FSM_STATE_EXCEPTION) || (fsm->fsm_flag & FSM_F_MD_REBOOT))
		return -EPROTO;

	if (event->fsm_flag == FSM_F_MDEE_INIT) {
		mtk_fsm_switch_state(fsm, FSM_STATE_EXCEPTION, event);
		cancel_delayed_work(&fsm->bootup_dump_work);
	}

	if (test_bit(EVT_TF_SKIP_MDEE_HS, &fsm->t_flag)) {
		MTK_INFO(mdev, "skip mdee hs performed.\n");
		return 0;
	}

	switch (event->fsm_flag) {
	case FSM_F_MDEE_INIT:
		queue_delayed_work(system_wq, &fsm->bootup_dump_work, BOOTUP_DELAY_TIME);
		mtk_dev_send_dev_evt(mdev, DEV_EVT_H2D_EXCEPT_ACK);
		break;
	case FSM_F_MDEE_INIT_DONE:
		if (!(fsm->fsm_flag & FSM_F_MDEE_INIT)) {
			MTK_WARN(mdev, "invalid FSM_F_MDEE_INIT_DONE.\n");
			break;
		}

		mtk_fsm_switch_state(fsm, FSM_STATE_EXCEPTION, event);
		mtk_fsm_ctrl_ch_start(fsm, &fsm->hs_info[HS_ID_MD], O_NONBLOCK);
		break;
	case FSM_F_MDEE_CLEARQ_DONE:
		if (!(fsm->fsm_flag & FSM_F_MDEE_INIT_DONE)) {
			MTK_WARN(mdev, "invalid FSM_F_MDEE_CLEARQ_DONE.\n");
			break;
		}

		mtk_fsm_switch_state(fsm, FSM_STATE_EXCEPTION, event);
		mtk_dev_send_dev_evt(mdev, DEV_EVT_H2D_EXCEPT_CLEARQ_ACK);
		break;
	case FSM_F_MDEE_ALLQ_RESET:
		if (!(fsm->fsm_flag & FSM_F_MDEE_CLEARQ_DONE)) {
			MTK_WARN(mdev, "invalid FSM_F_MDEE_ALLQ_RESET.\n");
			break;
		}

		mtk_fsm_switch_state(fsm, FSM_STATE_EXCEPTION, event);
		break;
	case FSM_F_MDEE_MSG:
		if (!(fsm->fsm_flag & FSM_F_MDEE_ALLQ_RESET)) {
			MTK_WARN(mdev, "invalid FSM_F_MDEE_MSG.\n");
			break;
		}

		mtk_fsm_switch_state(fsm, FSM_STATE_EXCEPTION, event);
		hs_info = event->data;
		ctrl_msg = __dev_alloc_skb(sizeof(*ctrl_msg), GFP_KERNEL);
		if (!ctrl_msg)
			return -ENOMEM;

		skb_put(ctrl_msg, sizeof(*ctrl_msg_h));
		/* fill control message header */
		ctrl_msg_h = (struct ctrl_msg_header *)ctrl_msg->data;
		ctrl_msg_h->id = cpu_to_le32(CTRL_MSG_MDEE);
		ctrl_msg_h->ex_msg = cpu_to_le32(MDEE_CHK_ID);
		ctrl_msg_h->data_len = 0;

		ret = mtk_port_internal_write(hs_info->ctrl_port, ctrl_msg);
		if (ret <= 0) {
			dev_err(mdev->dev, "Unable to send MDEE msg, ret = %d\n", ret);
			return -EPROTO;
		}

		break;
	case FSM_F_MDEE_RECV_OK:
		if (!(fsm->fsm_flag & FSM_F_MDEE_MSG)) {
			MTK_WARN(mdev, "invalid FSM_F_MDEE_RECV_OK.\n");
			break;
		}

		mtk_fsm_switch_state(fsm, FSM_STATE_EXCEPTION, event);
		dev_info(mdev->dev, "MDEE hs1 done\n");
		break;
	case FSM_F_MDEE_PASS:
		if (!(fsm->fsm_flag & FSM_F_MDEE_RECV_OK)) {
			MTK_WARN(mdev, "invalid FSM_F_MDEE_PASS.\n");
			break;
		}

		mtk_fsm_switch_state(fsm, FSM_STATE_EXCEPTION, event);
		cancel_delayed_work(&fsm->bootup_dump_work);
		dev_info(mdev->dev, "MDEE hs2 done\n");
		break;
	}

	return 0;
}

static int mtk_fsm_download_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	if (fsm->state != FSM_STATE_ON && fsm->state != FSM_STATE_DOWNLOAD)
		return -EPROTO;

	mtk_fsm_switch_state(fsm, FSM_STATE_DOWNLOAD, event);

	return 0;
}

static int mtk_fsm_postdump_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	if (fsm->state != FSM_STATE_ON && fsm->state != FSM_STATE_DOWNLOAD)
		return -EPROTO;

	mtk_fsm_switch_state(fsm, FSM_STATE_POSTDUMP, event);

	return 0;
}

static void mtk_fsm_evt_release(struct kref *kref)
{
	struct mtk_fsm_evt *event = container_of(kref, struct mtk_fsm_evt, kref);

	devm_kfree(event->mdev->dev, event);
}

static void mtk_fsm_evt_put(struct mtk_fsm_evt *event)
{
	kref_put(&event->kref, mtk_fsm_evt_release);
}

static void mtk_fsm_evt_finish(struct mtk_md_fsm *fsm,
			       struct mtk_fsm_evt *event, int retval)
{
	if (event->mode & (EVT_MODE_BLOCKING | EVT_MODE_BLOCKING_WITHOUT_TIMEOUT)) {
		event->status = retval;
		wake_up(&fsm->evt_waitq);
	}

	mtk_fsm_evt_put(event);
}

static void mtk_fsm_evt_cleanup(struct mtk_md_fsm *fsm, struct list_head *evtq)
{
	struct mtk_fsm_evt *event, *tmp;

	list_for_each_entry_safe(event, tmp, evtq, entry) {
		list_del(&event->entry);
		mtk_fsm_evt_finish(fsm, event, FSM_EVT_RET_FAIL);
	}
}

static void mtk_fsm_unregister_dev_evt(struct mtk_md_fsm *fsm)
{
	struct mtk_md_dev *mdev = fsm->mdev;
	struct fsm_hs_info *hs_info;
	int hs_id;

	mtk_dev_unregister_dev_evt(mdev, DEV_EVT_D2H_MDEE_MASK);
	for (hs_id = 0; hs_id < HS_ID_MAX; hs_id++) {
		hs_info = &fsm->hs_info[hs_id];
		mtk_dev_unregister_dev_evt(mdev, hs_info->mhccif_ch);
	}
	mtk_dev_unregister_dev_evt(mdev, DEV_EVT_D2H_MD_REBOOT);
	mtk_dev_unregister_dev_evt(mdev, DEV_EVT_D2H_MD_POWER_OFF);
	mtk_dev_unregister_dev_evt(mdev, DEV_EVT_D2H_GNSS_ENABLE);
	mtk_dev_unregister_dev_evt(mdev, DEV_EVT_D2H_GNSS_DISABLE);
}

static int mtk_fsm_enter_off_state(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	struct mtk_md_dev *mdev = fsm->mdev;
	int hs_id;

	if (fsm->state == FSM_STATE_OFF || fsm->state == FSM_STATE_INVALID)
		return -EPROTO;

	switch (fsm->cfg->sync_mode) {
	case BOOT_SYNC_MODE_POLLING:
		del_timer_sync(&fsm->dev_state_poller);
		break;
	case BOOT_SYNC_MODE_INTERRUPT:
		mtk_dev_mask_dev_evt(mdev, DEV_EVT_D2H_BOOT_FLOW_SYNC);
		mtk_dev_unregister_dev_evt(mdev, DEV_EVT_D2H_BOOT_FLOW_SYNC);
		break;
	default:
		break;
	}

	for (hs_id = 0; hs_id < HS_ID_MAX; hs_id++)
		mtk_dev_mask_dev_evt(mdev, fsm->hs_info[hs_id].mhccif_ch);
	mtk_dev_mask_dev_evt(mdev, DEV_EVT_D2H_MDEE_MASK);
	mtk_dev_mask_dev_evt(mdev, DEV_EVT_D2H_MD_REBOOT);
	mtk_dev_mask_dev_evt(mdev, DEV_EVT_D2H_GNSS_ENABLE);
	mtk_dev_mask_dev_evt(mdev, DEV_EVT_D2H_GNSS_DISABLE);
	mtk_fsm_unregister_dev_evt(fsm);

	mtk_fsm_ctrl_ch_stop(fsm);
	cancel_delayed_work_sync(&fsm->bootup_dump_work);
	cancel_delayed_work_sync(&fsm->md_power_off_work);
	mtk_fsm_switch_state(fsm, FSM_STATE_OFF, event);

	mtk_fsm_put_trm_wake_lock(fsm);

	return 0;
}

static int mtk_fsm_link_exception_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	if (fsm->state == FSM_STATE_OFF || fsm->state == FSM_STATE_INVALID ||
	    (fsm->fsm_flag & FSM_F_LINK_EXCEPTION))
		return -EPROTO;

	if (event->fsm_flag == FSM_F_LINK_EXCEPTION)
		mtk_fsm_switch_state(fsm, FSM_STATE_EXCEPTION, event);
	else
		mtk_fsm_enter_off_state(fsm, event);

	return 0;
}

static int mtk_fsm_dev_rm_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	unsigned long flags;

	spin_lock_irqsave(&fsm->evtq_lock, flags);
	set_bit(EVT_TF_GATECLOSED, &fsm->t_flag);
	mtk_fsm_evt_cleanup(fsm, &fsm->evtq);
	spin_unlock_irqrestore(&fsm->evtq_lock, flags);

	return mtk_fsm_enter_off_state(fsm, event);
}

static int mtk_fsm_hs1_handler(u32 status, void *__hs_info)
{
	struct fsm_hs_info *hs_info = __hs_info;
	struct mtk_md_dev *mdev;
	struct mtk_md_fsm *fsm;

	fsm = container_of(hs_info, struct mtk_md_fsm, hs_info[hs_info->id]);
	mdev = fsm->mdev;
	mtk_fsm_evt_submit(mdev, FSM_EVT_STARTUP,
			   hs_info->fsm_flag_hs1, hs_info, sizeof(*hs_info), 0);
	mtk_dev_mask_dev_evt(mdev, hs_info->mhccif_ch);
	mtk_dev_clear_dev_evt(mdev, hs_info->mhccif_ch);

	return 0;
}

static int mtk_fsm_mdee_handler(u32 status, void *__fsm)
{
	u32 handled_mdee_mhccif_ch = 0;
	struct mtk_md_fsm *fsm = __fsm;
	struct mtk_md_dev *mdev;

	mdev = fsm->mdev;

	if (status & DEV_EVT_D2H_EXCEPT_INIT) {
		mtk_fsm_evt_submit(mdev, FSM_EVT_MDEE,
				   FSM_F_MDEE_INIT, NULL, 0, 0);
		handled_mdee_mhccif_ch |= DEV_EVT_D2H_EXCEPT_INIT;
	}

	if (status & DEV_EVT_D2H_EXCEPT_INIT_DONE) {
		mtk_fsm_evt_submit(mdev, FSM_EVT_MDEE,
				   FSM_F_MDEE_INIT_DONE, NULL, 0, 0);
		handled_mdee_mhccif_ch |= DEV_EVT_D2H_EXCEPT_INIT_DONE;
	}

	if (status & DEV_EVT_D2H_EXCEPT_CLEARQ_DONE) {
		mtk_fsm_evt_submit(mdev, FSM_EVT_MDEE,
				   FSM_F_MDEE_CLEARQ_DONE, NULL, 0, 0);
		handled_mdee_mhccif_ch |= DEV_EVT_D2H_EXCEPT_CLEARQ_DONE;
	}

	if (status & DEV_EVT_D2H_EXCEPT_ALLQ_RESET) {
		mtk_fsm_evt_submit(mdev, FSM_EVT_MDEE,
				   FSM_F_MDEE_ALLQ_RESET, NULL, 0, 0);
		handled_mdee_mhccif_ch |= DEV_EVT_D2H_EXCEPT_ALLQ_RESET;
	}

	mtk_dev_mask_dev_evt(mdev, handled_mdee_mhccif_ch);
	mtk_dev_clear_dev_evt(mdev, handled_mdee_mhccif_ch);

	return 0;
}

static void mtk_fsm_hs_info_init_by_hsid(struct mtk_md_fsm *fsm, int hs_id)
{
	struct fsm_hs_info *hs_info;

	if (hs_id < 0 || hs_id >= HS_ID_MAX) {
		MTK_WARN(fsm->mdev, "hs_id = %d, invalid.\n", hs_id);
		return;
	}

	hs_info = &fsm->hs_info[hs_id];
	hs_info->id = hs_id;
	hs_info->ctrl_port = NULL;
	hs_info->rt_data = NULL;
	hs_info->hs_status = HS_INIT;
	switch (hs_id) {
	case HS_ID_MD:
		snprintf(hs_info->port_name, PORT_NAME_LEN, "MDCTRL");
		hs_info->mhccif_ch = DEV_EVT_D2H_ASYNC_HS_NOTIFY_MD;
		hs_info->fsm_flag_hs1 = FSM_F_MD_HS_START;
		hs_info->fsm_flag_hs2 = FSM_F_MD_HS2_DONE;
		hs_info->query_ft_set[QUERY_RTFT_ID_MD_PORT_ENUM].feature =
			FIELD_PREP(FEATURE_TYPE, RTFT_TYPE_MUST_SUPPORT);
		hs_info->query_ft_set[QUERY_RTFT_ID_MD_PORT_ENUM].feature |=
			FIELD_PREP(FEATURE_VER, 0);
		hs_info->query_ft_set[QUERY_RTFT_ID_MD_PORT_CFG].feature =
			FIELD_PREP(FEATURE_TYPE, RTFT_TYPE_OPTIONAL_SUPPORT);
		hs_info->query_ft_set[QUERY_RTFT_ID_MD_PORT_CFG].feature |=
			FIELD_PREP(FEATURE_VER, 0);
		hs_info->supported_ft_set[SUPPORT_RTFT_ID_MD_SBP_ID].feature =
			FIELD_PREP(FEATURE_TYPE, RTFT_TYPE_MUST_SUPPORT);
		hs_info->supported_ft_set[SUPPORT_RTFT_ID_MD_SBP_ID].feature |=
			FIELD_PREP(FEATURE_VER, 0);
		hs_info->supported_ft_set[SUPPORT_RTFT_ID_MD_DSD].feature =
			FIELD_PREP(FEATURE_TYPE, RTFT_TYPE_MUST_SUPPORT);
		hs_info->supported_ft_set[SUPPORT_RTFT_ID_MD_DSD].feature |=
			FIELD_PREP(FEATURE_VER, 0);
		hs_info->supported_ft_set[SUPPORT_RTFT_ID_MD_TIMESYNC].feature =
			FIELD_PREP(FEATURE_TYPE, RTFT_TYPE_MUST_SUPPORT);
		hs_info->supported_ft_set[SUPPORT_RTFT_ID_MD_TIMESYNC].feature |=
			FIELD_PREP(FEATURE_VER, 0);
		hs_info->supported_ft_set[SUPPORT_RTFT_ID_SKIP_MDEE_HS].feature =
			FIELD_PREP(FEATURE_TYPE, RTFT_TYPE_MUST_SUPPORT);
		hs_info->supported_ft_set[SUPPORT_RTFT_ID_SKIP_MDEE_HS].feature |=
			FIELD_PREP(FEATURE_VER, 0);
		break;
	case HS_ID_SAP:
		snprintf(hs_info->port_name, PORT_NAME_LEN, "SAPCTRL");
		hs_info->mhccif_ch = DEV_EVT_D2H_ASYNC_HS_NOTIFY_SAP;
		hs_info->fsm_flag_hs1 = FSM_F_SAP_HS_START;
		hs_info->fsm_flag_hs2 = FSM_F_SAP_HS2_DONE;
		hs_info->query_ft_set[QUERY_RTFT_ID_SAP_PORT_ENUM].feature =
			FIELD_PREP(FEATURE_TYPE, RTFT_TYPE_MUST_SUPPORT);
		hs_info->query_ft_set[QUERY_RTFT_ID_SAP_PORT_ENUM].feature |=
			FIELD_PREP(FEATURE_VER, 0);
		break;
	case HS_ID_GNSS:
		snprintf(hs_info->port_name, PORT_NAME_LEN, "GNSSCTRL");
		break;
	}
}

static void mtk_fsm_hs_info_init(struct mtk_md_fsm *fsm)
{
	int hs_id;

	for (hs_id = 0; hs_id < HS_ID_MAX; hs_id++)
		mtk_fsm_hs_info_init_by_hsid(fsm, hs_id);
}

static int mtk_fsm_md_reboot_handler(u32 status, void *__fsm)
{
	struct mtk_md_fsm *fsm = __fsm;

	mtk_dev_mask_dev_evt(fsm->mdev, DEV_EVT_D2H_MD_REBOOT);
	mtk_dev_clear_dev_evt(fsm->mdev, DEV_EVT_D2H_MD_REBOOT);

	mtk_dev_mask_dev_evt(fsm->mdev, DEV_EVT_D2H_MDEE_MASK);
	mtk_dev_clear_dev_evt(fsm->mdev, DEV_EVT_D2H_MDEE_MASK);

	mtk_dev_mask_dev_evt(fsm->mdev, DEV_EVT_D2H_ASYNC_HS_NOTIFY_MD);
	mtk_dev_clear_dev_evt(fsm->mdev, DEV_EVT_D2H_ASYNC_HS_NOTIFY_MD);

	mtk_fsm_evt_submit(fsm->mdev, FSM_EVT_MD_REBOOT,
			   FSM_F_MD_REBOOT, NULL, 0, EVT_MODE_TOHEAD);

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
	radio_br_modem_exception();
#endif

	return 0;
}

static int mtk_fsm_md_power_off_handler(u32 status, void *__fsm)
{
	struct mtk_md_fsm *fsm = __fsm;

	mtk_dev_mask_dev_evt(fsm->mdev, DEV_EVT_D2H_MD_POWER_OFF);
	mtk_dev_clear_dev_evt(fsm->mdev, DEV_EVT_D2H_MD_POWER_OFF);

	mtk_fsm_evt_submit(fsm->mdev, FSM_EVT_MD_POWER_OFF,
			   FSM_F_DFLT, NULL, 0, EVT_MODE_TOHEAD);

	return 0;
}

static int mtk_fsm_gnss_disable_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	int ret = 0;

	if (fsm->state == FSM_STATE_OFF || fsm->state == FSM_STATE_INVALID)
		return -EPROTO;

	if (!test_bit(EVT_TF_GNSS, &fsm->t_flag)) {
		ret = -EPROTO;
		goto unmask_mhccif;
	}

	mtk_port_internal_close(fsm->hs_info[HS_ID_GNSS].ctrl_port);
	fsm->hs_info[HS_ID_GNSS].ctrl_port = NULL;
	mtk_fsm_event_notify(fsm, event);

	clear_bit(EVT_TF_GNSS, &fsm->t_flag);

unmask_mhccif:
	mtk_dev_unmask_dev_evt(fsm->mdev, DEV_EVT_D2H_GNSS_DISABLE);

	return ret;
}

static int mtk_fsm_gnss_disable_handler(u32 status, void *__fsm)
{
	struct mtk_md_fsm *fsm = __fsm;

	mtk_dev_mask_dev_evt(fsm->mdev, DEV_EVT_D2H_GNSS_DISABLE);
	mtk_dev_clear_dev_evt(fsm->mdev, DEV_EVT_D2H_GNSS_DISABLE);

	mtk_fsm_evt_submit(fsm->mdev, FSM_EVT_GNSS_DISABLE,
			   FSM_F_DFLT, NULL, 0, 0);

	return 0;
}

static int mtk_fsm_gnss_port_enum_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	struct sk_buff *skb = event->data;
	int ret;

	if (fsm->state == FSM_STATE_OFF || fsm->state == FSM_STATE_INVALID ||
	    !test_bit(EVT_TF_GNSS, &fsm->t_flag))
		return -EPROTO;
	mtk_port_tbl_update(fsm->mdev, skb->data, skb->len);

	ret = mtk_port_internal_write(fsm->hs_info[HS_ID_GNSS].ctrl_port, skb);
	if (ret <= 0) {
		MTK_ERR(fsm->mdev, "Failed to send gnss config ack msg\n");
		return ret;
	}

	mtk_fsm_event_notify(fsm, event);

	return 0;
}

static int mtk_fsm_gnss_enable_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	struct ctrl_msg_header *ctrl_msg_h;
	struct sk_buff *skb;
	int ret, msg_size;

	if (fsm->state == FSM_STATE_OFF || fsm->state == FSM_STATE_INVALID)
		return -EPROTO;

	if (test_bit(EVT_TF_GNSS, &fsm->t_flag)) {
		ret = -EPROTO;
		goto unmask_mhccif;
	}

	mtk_fsm_event_notify(fsm, event);

	ret = mtk_fsm_ctrl_ch_start(fsm, &fsm->hs_info[HS_ID_GNSS], O_NONBLOCK);
	if (ret)
		goto unmask_mhccif;

	msg_size = sizeof(*ctrl_msg_h);
	skb = __dev_alloc_skb(msg_size, GFP_KERNEL);
	if (!skb) {
		ret = -ENOMEM;
		goto unmask_mhccif;
	}
	skb_put(skb, msg_size);

	ctrl_msg_h = (struct ctrl_msg_header *)skb->data;
	ctrl_msg_h->id = cpu_to_le32(CTRL_MSG_HS1);
	ctrl_msg_h->ex_msg = 0;
	ctrl_msg_h->data_len = 0;

	ret = mtk_port_internal_write(fsm->hs_info[HS_ID_GNSS].ctrl_port, skb);
	if (ret < 0)
		goto unmask_mhccif;
	else
		ret = 0;

	set_bit(EVT_TF_GNSS, &fsm->t_flag);

unmask_mhccif:
	mtk_dev_unmask_dev_evt(fsm->mdev, DEV_EVT_D2H_GNSS_ENABLE);

	return ret;
}

static int mtk_fsm_gnss_enable_handler(u32 status, void *__fsm)
{
	struct mtk_md_fsm *fsm = __fsm;

	mtk_dev_mask_dev_evt(fsm->mdev, DEV_EVT_D2H_GNSS_ENABLE);
	mtk_dev_clear_dev_evt(fsm->mdev, DEV_EVT_D2H_GNSS_ENABLE);

	mtk_fsm_evt_submit(fsm->mdev, FSM_EVT_GNSS_ENABLE,
			   FSM_F_DFLT, NULL, 0, 0);

	return 0;
}

static void mtk_fsm_register_dev_evt(struct mtk_md_fsm *fsm)
{
	struct mtk_md_dev *mdev = fsm->mdev;
	struct fsm_hs_info *hs_info;
	int hs_id;

	mtk_dev_register_dev_evt(mdev, DEV_EVT_D2H_MDEE_MASK, mtk_fsm_mdee_handler, fsm);
	for (hs_id = 0; hs_id < HS_ID_MAX; hs_id++) {
		hs_info = &fsm->hs_info[hs_id];
		mtk_dev_register_dev_evt(mdev, hs_info->mhccif_ch,
					 mtk_fsm_hs1_handler, hs_info);
	}
	mtk_dev_register_dev_evt(mdev, DEV_EVT_D2H_MD_REBOOT,
				 mtk_fsm_md_reboot_handler, fsm);

	mtk_dev_register_dev_evt(mdev, DEV_EVT_D2H_MD_POWER_OFF,
				 mtk_fsm_md_power_off_handler, fsm);

	mtk_dev_register_dev_evt(mdev, DEV_EVT_D2H_GNSS_ENABLE,
				 mtk_fsm_gnss_enable_handler, fsm);
	mtk_dev_register_dev_evt(mdev, DEV_EVT_D2H_GNSS_DISABLE,
				 mtk_fsm_gnss_disable_handler, fsm);
}

static void mtk_fsm_boot_init(struct mtk_md_fsm *fsm)
{
	switch (fsm->cfg->sync_mode) {
	case BOOT_SYNC_MODE_POLLING:
		timer_setup(&fsm->dev_state_poller, mtk_fsm_dev_state_poller_handler, 0);
		mod_timer(&fsm->dev_state_poller, jiffies + DEV_STATE_POLLER_INTVAL);
		break;
	case BOOT_SYNC_MODE_INTERRUPT:
		mtk_dev_register_dev_evt(fsm->mdev, DEV_EVT_D2H_BOOT_FLOW_SYNC,
					 mtk_fsm_dev_state_interrupt_mode_handler, fsm);
		mtk_dev_unmask_dev_evt(fsm->mdev, DEV_EVT_D2H_BOOT_FLOW_SYNC);
		break;
	default:
		break;
	}
}

static int mtk_fsm_dev_add_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	if (fsm->state != FSM_STATE_OFF && fsm->state != FSM_STATE_INVALID)
		return -EPROTO;

	mtk_fsm_switch_state(fsm, FSM_STATE_ON, event);
	mtk_fsm_register_dev_evt(fsm);
	mtk_fsm_boot_init(fsm);
	queue_delayed_work(system_wq, &fsm->bootup_dump_work, BOOTUP_DELAY_TIME);

	return 0;
}

static void mtk_fsm_reset(struct mtk_md_fsm *fsm)
{
	clear_bit(EVT_TF_GATECLOSED, &fsm->t_flag);
	clear_bit(EVT_TF_WLOCK, &fsm->t_flag);
	clear_bit(EVT_TF_TRM, &fsm->t_flag);
	clear_bit(EVT_TF_GNSS, &fsm->t_flag);
	clear_bit(EVT_TF_SKIP_MDEE_HS, &fsm->t_flag);
	fsm->last_dev_state = 0;
	fsm->fsm_flag = FSM_F_DFLT;

	mtk_fsm_hs_info_init(fsm);
}

static int mtk_fsm_dev_reinit_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	struct mtk_md_dev *mdev = fsm->mdev;
	int ret;

	if (fsm->state != FSM_STATE_OFF)
		return -EPROTO;

	if (event->fsm_flag == FSM_F_FULL_REINIT) {
		ret = mtk_dev_reinit(mdev, REINIT_TYPE_EXP);
		event->fsm_flag = 0;
	} else {
		ret = mtk_dev_reinit(mdev, REINIT_TYPE_RESUME);
	}

	if (ret) {
		MTK_ERR(mdev, "HW reinit error %d\n", ret);
		return ret;
	}

	mtk_fsm_reset(fsm);
	mtk_fsm_switch_state(fsm, FSM_STATE_ON, event);
	mtk_fsm_register_dev_evt(fsm);
	mtk_fsm_boot_init(fsm);
	queue_delayed_work(system_wq, &fsm->bootup_dump_work, BOOTUP_DELAY_TIME);

	return 0;
}

static int mtk_fsm_fb_reset_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	struct mtk_md_dev *mdev = fsm->mdev;

	if (fsm->state != FSM_STATE_READY)
		return -EPROTO;

	mtk_fsm_host_evt_ack(mdev, HOST_READY_FOR_FB_RESET);
	mtk_dev_send_dev_evt(mdev, DEV_EVT_H2D_DEVICE_RESET);

	return 0;
}

static int mtk_fsm_dump_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	if (fsm->state == FSM_STATE_OFF || fsm->state == FSM_STATE_INVALID)
		return -EPROTO;

	mtk_dev_dump(fsm->mdev);

	return 0;
}

static bool mtk_fsm_is_md_event(struct mtk_fsm_evt *event)
{
	switch (event->id) {
	case FSM_EVT_MDEE:
		return true;
	case FSM_EVT_STARTUP:
		if (event->fsm_flag & FSM_FLAG_MD_HS_MASK)
			return true;
		break;
	case FSM_EVT_MD_POWER_OFF:
		fallthrough;
	case FSM_EVT_MD_POWER_OFF_TIMEOUT:
		fallthrough;
	case FSM_EVT_MD_REBOOT:
		fallthrough;
	case FSM_EVT_TRM_NOTIFY:
		return true;

	default:
		break;
	}

	return false;
}

static void mtk_fsm_cleanup_md_evt(struct mtk_md_fsm *fsm)
{
	struct mtk_fsm_evt *event, *tmp_event;
	unsigned long flags;

	spin_lock_irqsave(&fsm->evtq_lock, flags);
	list_for_each_entry_safe(event, tmp_event, &fsm->evtq, entry) {
		if (mtk_fsm_is_md_event(event)) {
			list_del(&event->entry);
			mtk_fsm_evt_finish(fsm, event, FSM_EVT_RET_FAIL);
		}
	}

	spin_unlock_irqrestore(&fsm->evtq_lock, flags);
}

static int mtk_fsm_md_reboot_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	if ((fsm->state != FSM_STATE_ON && fsm->state != FSM_STATE_BOOTUP &&
	     fsm->state != FSM_STATE_EXCEPTION && fsm->state != FSM_STATE_READY) ||
	    (fsm->fsm_flag & FSM_F_EXCEPT_INT))
		return -EPROTO;

	cancel_delayed_work(&fsm->bootup_dump_work);

	mtk_fsm_switch_state(fsm, FSM_STATE_BOOTUP, event);

	mtk_dev_send_dev_evt(fsm->mdev, DEV_EVT_H2D_MD_REBOOT_ACK);
	queue_delayed_work(system_wq, &fsm->md_power_off_work, MD_POWER_OFF_DELAY_TIME);

	mtk_dev_unmask_dev_evt(fsm->mdev, DEV_EVT_D2H_MD_POWER_OFF);

	return 0;
}

static int mtk_fsm_md_power_off_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	if (fsm->state != FSM_STATE_BOOTUP || !(fsm->fsm_flag & FSM_F_MD_REBOOT))
		return -EPROTO;

	/* since MD will reboot, cleanup the MD events already in fsm event queue,
	 * to prevent fsm entering wrong state.
	 */
	mtk_fsm_cleanup_md_evt(fsm);

	if (event->id == FSM_EVT_MD_POWER_OFF) {
		cancel_delayed_work(&fsm->md_power_off_work);
	} else if (event->id == FSM_EVT_MD_POWER_OFF_TIMEOUT) {
		if (mtk_dev_dump(fsm->mdev))
			return 0;
	}

	fsm->fsm_flag &= (~(FSM_FLAG_MD_HS_MASK));
	fsm->fsm_flag &= (~(FSM_FLAG_MDEE_MASK));
	fsm->fsm_flag &= (~(FSM_F_MD_REBOOT));
	mtk_fsm_switch_state(fsm, FSM_STATE_BOOTUP, event);

	mtk_fsm_hs_info_init_by_hsid(fsm, HS_ID_MD);

	queue_delayed_work(system_wq, &fsm->bootup_dump_work, BOOTUP_DELAY_TIME);

	mtk_dev_unmask_dev_evt(fsm->mdev, DEV_EVT_D2H_ASYNC_HS_NOTIFY_MD);
	mtk_dev_clear_dev_evt(fsm->mdev, DEV_EVT_D2H_MDEE_MASK);
	mtk_dev_unmask_dev_evt(fsm->mdev, DEV_EVT_D2H_MDEE_MASK);
	mtk_dev_unmask_dev_evt(fsm->mdev, DEV_EVT_D2H_MD_REBOOT);

	mtk_fsm_put_trm_wake_lock(fsm);

	return 0;
}

static int mtk_fsm_device_ctrl_trm_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	struct mtk_md_dev *mdev = fsm->mdev;
	struct mtk_ctrl_blk *ctrl_blk;
	int ret = 0;

	ctrl_blk = mdev->ctrl_blk;

	if ((fsm->state != FSM_STATE_READY && fsm->state != FSM_STATE_EXCEPTION) ||
	    (fsm->fsm_flag & FSM_F_EXCEPT_INT))
		return -EPROTO;

	ret = mtk_fsm_get_trm_wake_lock(fsm);
	if (ret)
		return ret;

	/* HIF_CTRL_CMD_TRM_NOTIFY will check link status after send MHCCIF to device.
	 * if link err found, device_ctrl will report link err.
	 * then fsm will enter off, causing pm_relax
	 */
	ctrl_blk->ops->send_cmd(mdev, HIF_CTRL_CMD_TRM_NOTIFY, NULL);

	return 0;
}

static int mtk_fsm_exception_notify_act(struct mtk_md_fsm *fsm, struct mtk_fsm_evt *event)
{
	if (fsm->state == FSM_STATE_INVALID || fsm->state == FSM_STATE_OFF)
		return -EPROTO;

	mtk_fsm_switch_state(fsm, FSM_STATE_EXCEPTION, event);
	return 0;
}

static int (*evts_act_tbl[FSM_EVT_MAX])(struct mtk_md_fsm *__fsm, struct mtk_fsm_evt *event) = {
	[FSM_EVT_DOWNLOAD] = mtk_fsm_download_act,
	[FSM_EVT_POSTDUMP] = mtk_fsm_postdump_act,
	[FSM_EVT_STARTUP] = mtk_fsm_startup_act,
	[FSM_EVT_LINKDOWN] = mtk_fsm_link_exception_act,
	[FSM_EVT_BUS_ERR] = mtk_fsm_link_exception_act,
	[FSM_EVT_COLD_RESUME] = mtk_fsm_enter_off_state,
	[FSM_EVT_REINIT] = mtk_fsm_dev_reinit_act,
	[FSM_EVT_MDEE] = mtk_fsm_mdee_act,
	[FSM_EVT_DEV_RESET_REQ] = mtk_fsm_enter_off_state,
	[FSM_EVT_DEV_RM] = mtk_fsm_dev_rm_act,
	[FSM_EVT_DEV_ADD] = mtk_fsm_dev_add_act,
	[FSM_EVT_SOFT_OFF] = mtk_fsm_enter_off_state,
	[FSM_EVT_FB_RESET] = mtk_fsm_fb_reset_act,
	[FSM_EVT_PWROFF] = mtk_fsm_enter_off_state,
	[FSM_EVT_DUMP] = mtk_fsm_dump_act,
	[FSM_EVT_MD_REBOOT] = mtk_fsm_md_reboot_act,
	[FSM_EVT_MD_POWER_OFF] = mtk_fsm_md_power_off_act,
	[FSM_EVT_MD_POWER_OFF_TIMEOUT] = mtk_fsm_md_power_off_act,
	[FSM_EVT_TRM_NOTIFY] = mtk_fsm_device_ctrl_trm_act,
	[FSM_EVT_GNSS_ENABLE] = mtk_fsm_gnss_enable_act,
	[FSM_EVT_GNSS_PORT_ENUM] = mtk_fsm_gnss_port_enum_act,
	[FSM_EVT_GNSS_DISABLE] = mtk_fsm_gnss_disable_act,
	[FSM_EVT_EXCEPTION_NOTIFY] = mtk_fsm_exception_notify_act,
};

/**
 * mtk_fsm_trigger_mdee() - start trigger mdee
 * @mdev: mdev pointer to mtk_md_dev
 *
 * This function start trigger mdee.
 *
 * Return: return value is 0 on success, a negative error code on failure.
 */
int mtk_fsm_trigger_mdee(struct mtk_md_dev *mdev)
{
	struct ctrl_msg_header *ctrl_msg_h;
	struct fsm_hs_info *hs_info;
	struct mtk_md_fsm *fsm;
	struct sk_buff *skb;
	int msg_size, ret;

	fsm = mdev->fsm;
	if (!fsm)
		return -EINVAL;

	hs_info = &fsm->hs_info[HS_ID_MD];
	if (!hs_info->ctrl_port)
		return -ENODEV;

	if (fsm->fsm_flag & FSM_F_MDEE_INIT)
		return -EPERM;

	msg_size = sizeof(*ctrl_msg_h);
	skb = __dev_alloc_skb(msg_size, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, msg_size);
	ctrl_msg_h = (struct ctrl_msg_header *)(skb->data);
	ctrl_msg_h->id = cpu_to_le32(CTRL_MSG_TRIGGER_MDEE);
	ctrl_msg_h->ex_msg = 0;
	ret = mtk_port_internal_write(hs_info->ctrl_port, skb);
	if (ret <= 0)
		return ret;

	MTK_INFO(mdev, "Trigger MDEE success!\n");
	return 0;
}
EXPORT_SYMBOL(mtk_fsm_trigger_mdee);

/**
 * mtk_fsm_cfg_info_update() - update fsm cfg info
 * @mdev: mdev pointer to mtk_md_dev
 * @fsm_cfg: fsm cfg info
 *
 */
void mtk_fsm_cfg_info_update(struct mtk_md_dev *mdev, struct mtk_fsm_cfg *fsm_cfg)
{
	struct mtk_md_fsm *fsm = mdev->fsm;

	fsm->cfg = fsm_cfg;
}

/**
 * mtk_fsm_start() - start FSM service
 * @mdev: mdev pointer to mtk_md_dev
 *
 * This function start a fsm service to handle fsm event.
 *
 * Return: return value is 0 on success, a negative error code on failure.
 */
int mtk_fsm_start(struct mtk_md_dev *mdev)
{
	struct mtk_md_fsm *fsm = mdev->fsm;

	if (!fsm)
		return -EINVAL;

	MTK_INFO(mdev, "Start fsm by %ps!\n", __builtin_return_address(0));
	clear_bit(EVT_TF_PAUSE, &fsm->t_flag);
	if (!fsm->fsm_handler)
		return -EFAULT;

	wake_up_process(fsm->fsm_handler);

	return 0;
}
EXPORT_SYMBOL(mtk_fsm_start);

/**
 * mtk_fsm_pause() - pause fsm service
 * @mdev: pointer to mtk_md_dev.
 *
 * If the function is called in irq context, it is able to be paused, or
 * it will return as soon. It can only work in process context.
 *
 * Return:
 * * 0: the fsm handler thread is paused.
 * * <0: fail to pause fsm handler thread.
 */
int mtk_fsm_pause(struct mtk_md_dev *mdev)
{
	struct mtk_md_fsm *fsm = mdev->fsm;

	if (!fsm)
		return -EINVAL;

	MTK_INFO(mdev, "Pause fsm by %ps!\n", __builtin_return_address(0));
	if (!test_and_set_bit(EVT_TF_PAUSE, &fsm->t_flag)) {
		reinit_completion(&fsm->paused);
		wake_up_process(fsm->fsm_handler);
	}

	wait_for_completion(&fsm->paused);
	// if (!wait_for_completion_timeout(&fsm->paused, EVT_HANDLER_TIMEOUT)) {
	// 	MTK_ERR(mdev, "Pause fsm timeout!\n");
	// 	clear_bit(EVT_TF_PAUSE, &fsm->t_flag);
	// 	wake_up_process(fsm->fsm_handler);
	// 	return -ETIMEDOUT;
	// }

	return 0;
}
EXPORT_SYMBOL(mtk_fsm_pause);

static void mkt_fsm_notifier_cleanup(struct mtk_md_dev *mdev, struct list_head *ntq)
{
	struct mtk_fsm_notifier *nt, *tmp;

	list_for_each_entry_safe(nt, tmp, ntq, entry) {
		list_del(&nt->entry);
		MTK_WARN(mdev,
			 "Having to free notifier(%d) by FSM!\n", nt->id);
		devm_kfree(mdev->dev, nt);
	}
}

static void mtk_fsm_notifier_insert(struct mtk_fsm_notifier *notifier, struct list_head *head)
{
	struct mtk_fsm_notifier *nt;

	list_for_each_entry(nt, head, entry) {
		if (notifier->prio > nt->prio) {
			list_add(&notifier->entry, nt->entry.prev);
			return;
		}
	}
	list_add_tail(&notifier->entry, head);
}

/**
 * mtk_fsm_notifier_register() - register notifier callback
 * @mdev: pointer to mtk_md_dev
 * @id: user id
 * @cb: pointer to notification callback provided by user
 * @data: pointer to user data if any
 * @prio: PRIO_0, PRIO_1
 * @is_pre: 1: pre switch, 0: post switch
 *
 * Return: return value is 0 on success, a negative error code on failure.
 */
int mtk_fsm_notifier_register(struct mtk_md_dev *mdev,
			      enum mtk_user_id id,
			      void (*cb)(struct mtk_fsm_param *, void *data),
			      void *data,
			      enum mtk_fsm_prio prio,
			      bool is_pre)
{
	struct mtk_md_fsm *fsm = mdev->fsm;
	struct mtk_fsm_notifier *notifier;

	if (!fsm)
		return -EINVAL;

	if (id >= MTK_USER_MAX || !cb || prio >= FSM_PRIO_MAX)
		return -EINVAL;

	notifier = devm_kzalloc(mdev->dev, sizeof(*notifier), GFP_KERNEL);
	if (!notifier)
		return -ENOMEM;

	INIT_LIST_HEAD(&notifier->entry);
	notifier->id = id;
	notifier->cb = cb;
	notifier->data = data;
	notifier->prio = prio;

	if (is_pre)
		mtk_fsm_notifier_insert(notifier, &fsm->pre_notifiers);
	else
		mtk_fsm_notifier_insert(notifier, &fsm->post_notifiers);

	return 0;
}
EXPORT_SYMBOL(mtk_fsm_notifier_register);

/**
 * mtk_fsm_notifier_unregister() - unregister notifier callback
 * @mdev: pointer to mtk_md_dev
 * @id: user id
 *
 * Return: return value is 0 on success, a negative error code on failure.
 */
int mtk_fsm_notifier_unregister(struct mtk_md_dev *mdev, enum mtk_user_id id)
{
	struct mtk_md_fsm *fsm = mdev->fsm;
	struct mtk_fsm_notifier *nt, *tmp;

	if (!fsm)
		return -EINVAL;

	list_for_each_entry_safe(nt, tmp, &fsm->pre_notifiers, entry) {
		if (nt->id == id) {
			list_del(&nt->entry);
			devm_kfree(mdev->dev, nt);
			break;
		}
	}
	list_for_each_entry_safe(nt, tmp, &fsm->post_notifiers, entry) {
		if (nt->id == id) {
			list_del(&nt->entry);
			devm_kfree(mdev->dev, nt);
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL(mtk_fsm_notifier_unregister);

static void mtk_fsm_evt_timeout(struct timer_list *t)
{
	struct mtk_md_fsm *fsm = from_timer(fsm, t, evt_timer);

	MTK_WARN(fsm->mdev, "Handling fsm event timeout! Current notifier is %d\n",
		 fsm->notifier_record);

	MTK_WARN(fsm->mdev, "Show fsm_handler task:\n");
	sched_show_task(fsm->fsm_handler);
}

#define BLOCKING_EVT_TIMEOUT				(2 * EVT_HANDLER_TIMEOUT)

/**
 * mtk_fsm_evt_submit() - submit event
 * @mdev: pointer to mtk_md_dev
 * @id: event id
 * @flag: state flag
 * @data: user data
 * @len: data length
 * @mode: EVT_MODE_BLOCKING(1<<0) means that submit blocking until
 *        event is handled or timeout; EVT_MODE_TOHEAD(1<<1) means
 *        the event will be handled in high priority;
 *        EVT_MODE_BLOCKING_WITHOUT_TIMEOUT(1<<2) means that submit
 *        blocking until event is handled. If both EVT_MODE_BLOCKING
 *        and EVT_MODE_BLOCKING_WITHOUT_TIMEOUT set, only EVT_MODE_BLOCKING
 *        works.
 *        Do not use EVT_MODE_BLOCKING or EVT_MODE_BLOCKING_WITHOUT_TIMEOUT mode
 *        in runtime suspend or runtime resume flow.
 *
 * Return: 0 will be returned, if the event is appended (non-blocking)
 *         or submit event timeout(EVT_MODE_BLOCKING), -1 will be returned if
 *         failed to handle event, 1 will be returned if succeed to handle event.
 */
int mtk_fsm_evt_submit(struct mtk_md_dev *mdev,
		       enum mtk_fsm_evt_id id,
		       enum mtk_fsm_flag flag,
		       void *data, unsigned int len,
		       unsigned char mode)
{
	struct mtk_md_fsm *fsm = mdev->fsm;
	struct mtk_fsm_evt *event;
	unsigned long flags;
	int ret = 0;

	if (!fsm || id >= FSM_EVT_MAX) {
		MTK_ERR(mdev, "Invalid param!\n");
		return FSM_EVT_RET_FAIL;
	}

	if (test_bit(EVT_TF_GATECLOSED, &fsm->t_flag)) {
		MTK_ERR(mdev, "Failed to submit evt, fsm has been removed!\n");
		return FSM_EVT_RET_FAIL;
	}

	event = devm_kzalloc(mdev->dev, sizeof(*event),
			     (in_irq() || in_softirq() || irqs_disabled()) ?
			     GFP_ATOMIC : GFP_KERNEL);
	if (!event) {
		MTK_ERR(mdev, "Failed to alloc event!\n");
		return FSM_EVT_RET_FAIL;
	}

	kref_init(&event->kref);
	event->mdev = mdev;
	event->id = id;
	event->fsm_flag = flag;
	event->status = FSM_EVT_RET_ONGOING;
	event->data = data;
	event->len = len;
	event->mode = mode;
	MTK_INFO(mdev, "Event%d(with mode 0x%x, flag 0x%x) is appended by %ps\n",
		 event->id, event->mode, event->fsm_flag,  __builtin_return_address(0));

	spin_lock_irqsave(&fsm->evtq_lock, flags);
	if (test_bit(EVT_TF_GATECLOSED, &fsm->t_flag)) {
		spin_unlock_irqrestore(&fsm->evtq_lock, flags);
		mtk_fsm_evt_put(event);
		MTK_ERR(mdev, "Failed to add event, fsm dev has been removed!\n");
		return FSM_EVT_RET_FAIL;
	}

	kref_get(&event->kref);
	if (mode & EVT_MODE_TOHEAD)
		list_add(&event->entry, &fsm->evtq);
	else
		list_add_tail(&event->entry, &fsm->evtq);
	spin_unlock_irqrestore(&fsm->evtq_lock, flags);

	wake_up_process(fsm->fsm_handler);
	if (mode & EVT_MODE_BLOCKING) {
		ret = wait_event_timeout(fsm->evt_waitq,
					 (event->status != 0), BLOCKING_EVT_TIMEOUT);
		if (!ret && event->status != FSM_EVT_RET_DONE)
			MTK_ERR(mdev, "Handling fsm blocking event timeout!\n");

		ret = event->status;
	} else if (mode & EVT_MODE_BLOCKING_WITHOUT_TIMEOUT) {
		wait_event(fsm->evt_waitq, (event->status != 0));
		ret = event->status;
	}

	mtk_fsm_evt_put(event);

	return ret;
}
EXPORT_SYMBOL(mtk_fsm_evt_submit);

static int mtk_fsm_rpm_get(struct mtk_md_fsm *fsm, bool sync)
{
	struct mtk_md_dev *mdev = fsm->mdev;
	struct mtk_ctrl_blk *ctrl_blk;

	ctrl_blk = mdev->ctrl_blk;
	return ctrl_blk->ops->send_cmd(mdev, HIF_CTRL_CMD_RPM_GET, &sync);
}

static int mtk_fsm_rpm_put(struct mtk_md_fsm *fsm, bool sync)
{
	struct mtk_md_dev *mdev = fsm->mdev;
	struct mtk_ctrl_blk *ctrl_blk;

	ctrl_blk = mdev->ctrl_blk;
	return ctrl_blk->ops->send_cmd(mdev, HIF_CTRL_CMD_RPM_PUT, &sync);
}

static int mtk_fsm_evt_handler(void *__fsm)
{
	struct mtk_md_fsm *fsm = __fsm;
	struct mtk_fsm_evt *event;
	unsigned long flags;
	int ret;

wake_up:
	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop() &&
	       !test_bit(EVT_TF_PAUSE, &fsm->t_flag) && !list_empty(&fsm->evtq)) {
		set_current_state(TASK_RUNNING);
		spin_lock_irqsave(&fsm->evtq_lock, flags);
		event = list_first_entry(&fsm->evtq, struct mtk_fsm_evt, entry);
		list_del(&event->entry);
		spin_unlock_irqrestore(&fsm->evtq_lock, flags);

		MTK_INFO(fsm->mdev, "Event%d(0x%x) is under handling\n",
			 event->id, event->fsm_flag);
		mod_timer(&fsm->evt_timer, jiffies + EVT_HANDLER_TIMEOUT);

		if (event->id < FSM_EVT_MAX) {
			mtk_fsm_rpm_get(fsm, true);
			ret = evts_act_tbl[event->id](fsm, event);
			mtk_fsm_rpm_put(fsm, true);
			if (ret) {
				MTK_ERR(fsm->mdev,
					"Failed to handle evt, fsm state = %d, ret = %d\n",
					fsm->state, ret);
				mtk_fsm_evt_finish(fsm, event, FSM_EVT_RET_FAIL);
			} else {
				mtk_fsm_evt_finish(fsm, event, FSM_EVT_RET_DONE);
			}
		} else {
			mtk_fsm_evt_finish(fsm, event, FSM_EVT_RET_DONE);
		}

		del_timer_sync(&fsm->evt_timer);
	}

	if (kthread_should_stop()) {
		set_current_state(TASK_RUNNING);
		return 0;
	}

	if (test_bit(EVT_TF_PAUSE, &fsm->t_flag))
		complete_all(&fsm->paused);

	schedule();

	if (fatal_signal_pending(current)) {
		/* event handler thread is killed by fatal signal,
		 * all the waiters will be waken up.
		 */
		complete_all(&fsm->paused);
		mtk_fsm_evt_cleanup(fsm, &fsm->evtq);
		return -ERESTARTSYS;
	}

	goto wake_up;
}

/**
 * mtk_fsm_init() - allocate FSM control block and initialize it
 * @mdev: pointer to mtk_md_dev
 *
 * This function creates a mtk_md_fsm structure dynamically and hook
 * it up to mtk_md_dev. When you are finished with this structure,
 * call mtk_fsm_exit() and the structure will be dynamically freed.
 *
 * Return: return value is 0 on success, a negative error code on failure.
 */
int mtk_fsm_init(struct mtk_md_dev *mdev)
{
	struct mtk_md_fsm *fsm;
	unsigned long flags;
	int ret;

	fsm = devm_kzalloc(mdev->dev, sizeof(*fsm), GFP_KERNEL);
	if (!fsm)
		return -ENOMEM;

	fsm->fsm_handler = kthread_create(mtk_fsm_evt_handler, fsm, "fsm_evt_thread%d_%s",
					  mdev->hw_ver, mdev->dev_str);
	if (IS_ERR(fsm->fsm_handler)) {
		ret = PTR_ERR(fsm->fsm_handler);
		goto err_create_fsm_handler;
	}

	fsm->mdev = mdev;
	init_completion(&fsm->paused);
	timer_setup(&fsm->evt_timer, mtk_fsm_evt_timeout, 0);
	INIT_DELAYED_WORK(&fsm->bootup_dump_work, mtk_fsm_bootup_dump_work);
	INIT_DELAYED_WORK(&fsm->md_power_off_work, mtk_fsm_md_power_off_work);
	INIT_WORK(&fsm->nb_work, mtk_fsm_kernel_nb_work);

	fsm->state = FSM_STATE_INVALID;
	fsm->fsm_flag = FSM_F_DFLT;
	fsm->notifier_record = MTK_USER_MAX;

	INIT_LIST_HEAD(&fsm->evtq);
	spin_lock_init(&fsm->evtq_lock);
	mutex_init(&fsm->nb_list_mtx);
	init_waitqueue_head(&fsm->evt_waitq);

	INIT_LIST_HEAD(&fsm->pre_notifiers);
	INIT_LIST_HEAD(&fsm->post_notifiers);
	INIT_LIST_HEAD(&fsm->nb_list);

	mtk_fsm_hs_info_init(fsm);

	fsm->fsm_ws = wakeup_source_register(NULL, FSM_WAKEUP_SOURCE_NAME);
	if (!fsm->fsm_ws)
		MTK_WARN(mdev, "Unable to register fsm wakeup source\n");

	mdev->fsm = fsm;
	if (device_create_file(mdev->dev, &dev_attr_fsm_state))
		MTK_WARN(mdev, "Unable to create fsm_state sysfs entry\n");
	if (device_create_file(mdev->dev, &dev_attr_mtk_device_ctrl))
		MTK_WARN(mdev, "Unable to create mtk_device_ctrl sysfs entry\n");

	spin_lock_irqsave(&external_fsm_lock, flags);
	external_fsm = fsm;
	spin_unlock_irqrestore(&external_fsm_lock, flags);

	return 0;

err_create_fsm_handler:
	devm_kfree(mdev->dev, fsm);
	return ret;
}
EXPORT_SYMBOL(mtk_fsm_init);

/**
 * mtk_fsm_exit() - free FSM control block
 * @mdev: pointer to mtk_md_dev
 *
 * Return: return value is 0 on success, a negative error code on failure.
 */
int mtk_fsm_exit(struct mtk_md_dev *mdev)
{
	struct mtk_md_fsm *fsm = mdev->fsm;
	unsigned long flags;

	if (!fsm)
		return -EINVAL;

	if (fsm->fsm_handler) {
		kthread_stop(fsm->fsm_handler);
		fsm->fsm_handler = NULL;
	}

	complete_all(&fsm->paused);

	spin_lock_irqsave(&fsm->evtq_lock, flags);
	if (WARN_ON(!list_empty(&fsm->evtq)))
		mtk_fsm_evt_cleanup(fsm, &fsm->evtq);
	spin_unlock_irqrestore(&fsm->evtq_lock, flags);

	mkt_fsm_notifier_cleanup(mdev, &fsm->pre_notifiers);
	mkt_fsm_notifier_cleanup(mdev, &fsm->post_notifiers);
	flush_work(&fsm->nb_work);
	wakeup_source_unregister(fsm->fsm_ws);

	device_remove_file(mdev->dev, &dev_attr_fsm_state);
	device_remove_file(mdev->dev, &dev_attr_mtk_device_ctrl);

	devm_kfree(mdev->dev, fsm);

	spin_lock_irqsave(&external_fsm_lock, flags);
	external_fsm = NULL;
	spin_unlock_irqrestore(&external_fsm_lock, flags);

	return 0;
}
EXPORT_SYMBOL(mtk_fsm_exit);

module_param(skip_mdee_hs, ushort, 0644);
MODULE_PARM_DESC(skip_mdee_hs, "This value is used to skip modem exception handshake flow\n");
