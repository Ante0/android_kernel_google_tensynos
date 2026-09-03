// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2023, MediaTek Inc.
 */

#define pr_fmt(fmt) "DATA: " fmt

#include <linux/ip.h>
#include <net/ipv6.h>

#include "mtk_bm.h"
#include "mtk_data_plane.h"
#include "mtk_debug.h"
#include "mtk_dev.h"
#include "mtk_fsm.h"
#include "mtk_wwan.h"

#ifdef CONFIG_TX00_UT_WWAN
#include "ut_wwan_case.h"
#endif

#define TAG "DATA"

static bool data_plane_bypass;

static void mtk_data_stop(struct mtk_data_blk *data_blk, struct mtk_md_dev *data)
{
	/* Stop data port layer tx. */
	mtk_wwan_notify(data_blk, DATA_EVT_TX_STOP, 0xff);

	data_blk->hif_ops->stop(data);

	/* Unregister data port, because data port will be
	 * registered again in FSM_STATE_READY stage.
	 */
	mtk_wwan_notify(data_blk, DATA_EVT_UNREG_DEV, 0);

	data_blk->hif_ops->clear(data);
	/* To reinit flow, it needs to be set initial value. */
	data_blk->exception_dup_stop = false;
}

static void mtk_data_fsm_callback(struct mtk_fsm_param *fsm_param, void *data)
{
	struct mtk_data_blk *data_blk;

	if (!data || !fsm_param) {
		pr_warn("Invalid fsm parameter\n");
		return;
	}

	data_blk = ((struct mtk_md_dev *)data)->data_blk;

	switch (fsm_param->to) {
	case FSM_STATE_OFF:
		mtk_data_stop(data_blk, data);
		break;
	case FSM_STATE_BOOTUP:
		if (fsm_param->fsm_flag & FSM_F_MD_HS_START)
			data_blk->hif_ops->start(data);
		else if (fsm_param->fsm_flag & FSM_F_MD_REBOOT)
			mtk_data_stop(data_blk, data);
		break;
	case FSM_STATE_READY:
		mtk_wwan_notify(data_blk, DATA_EVT_REG_DEV, 0);
		break;
	case FSM_STATE_EXCEPTION:
		if ((fsm_param->fsm_flag & FSM_F_MDEE_INIT ||
		     fsm_param->fsm_flag & FSM_F_EXCEPT_INT ||
		     fsm_param->fsm_flag & FSM_F_LINK_EXCEPTION) && !data_blk->exception_dup_stop) {
			if (fsm_param->fsm_flag & FSM_F_LINK_EXCEPTION)
				data_blk->hif_ops->link_exception(data);

			data_blk->exception_dup_stop = true;
			/* Stop data port layer tx. */
			mtk_wwan_notify(data_blk, DATA_EVT_TX_STOP, 0xff);
			data_blk->hif_ops->stop(data);

			/* Dump dpmaif and drv information. */
			data_blk->hif_ops->dump(data);
			mtk_wwan_notify(data_blk, DATA_EVT_DUMP, 0);
		}
		break;
	default:
		break;
	}
}

/* mtk_data_init() - initialize data path
 * @mdev: pointer to mtk_md_dev
 * @ops: Contains transaction layer ops: send, select_txq, napi_poll, ....
 * Allocate and initialize all software resource of data transaction layer and data port layer.
 * Return: return value is 0 on success, a negative error code on failure.
 */
int mtk_data_init(struct mtk_md_dev *mdev, struct mtk_data_hif_ops *ops)
{
	struct mtk_data_blk *data_blk;
	int ret;

	BUILD_BUG_ON(sizeof(union mtk_data_pkt_info) > MAX_USER_CB_SIZE);
	if (data_plane_bypass) {
		MTK_INFO(mdev, "data plane bypass init\n");
		return 0;
	}

	data_blk = devm_kzalloc(mdev->dev, sizeof(*data_blk), GFP_KERNEL);
	if (!data_blk) {
		MTK_ERR(mdev, "Failed to allocate data_blk\n");
		return -ENOMEM;
	}

	data_blk->mdev = mdev;
	mdev->data_blk = data_blk;
	data_blk->hif_ops = ops;

	ret = data_blk->hif_ops->init(mdev);
	if (ret < 0)
		goto data_blk_free;

	ret = mtk_wwan_init(data_blk);
	if (ret < 0)
		goto hif_exit;

	ret = mtk_fsm_notifier_register(mdev, MTK_USER_DATA, mtk_data_fsm_callback, mdev,
					FSM_PRIO_1, false);
	if (ret < 0) {
		MTK_ERR(mdev, "Failed to register FSM notifier\n");
		goto wwan_exit;
	}

	return 0;

wwan_exit:
	mtk_wwan_exit(data_blk);

hif_exit:
	data_blk->hif_ops->exit(mdev);

data_blk_free:
	devm_kfree(mdev->dev, data_blk);
	mdev->data_blk = NULL;

	return ret;
}
EXPORT_SYMBOL(mtk_data_init);

/* mtk_data_exit() - deinitialize data path
 * @mdev: pointer to mtk_md_dev
 * deinitialize and release all software resource of data transction layer and data port layer.
 * Return: return value is 0 on success, a negative error code on failure.
 */
int mtk_data_exit(struct mtk_md_dev *mdev)
{
	int ret;

	if (data_plane_bypass) {
		pr_info("data plane bypass exit\n");
		return 0;
	}

	if (!mdev->data_blk) {
		MTK_ERR(mdev, "Invalid parameter, mdev=%llx\n");
		return -EINVAL;
	}

	ret = mtk_fsm_notifier_unregister(mdev, MTK_USER_DATA);
	if (ret < 0)
		MTK_ERR(mdev, "Failed to unregister fsm notifier\n");

	mtk_wwan_exit(mdev->data_blk);

	ret = ((struct mtk_data_blk *)(mdev->data_blk))->hif_ops->exit(mdev);

	devm_kfree(mdev->dev, mdev->data_blk);
	mdev->data_blk = NULL;

	return ret;
}
EXPORT_SYMBOL(mtk_data_exit);

u32 mtk_data_get_pkt_id(struct sk_buff *skb)
{
	struct iphdr *iph = (struct iphdr *)skb->data;
	int inner_offset;
	__be16 frag_off;
	u8 nexthdr;

	if (iph->version == 4) {
		return be16_to_cpu(iph->id);

	} else if (iph->version == 6) {
		nexthdr = ((struct ipv6hdr *)skb->data)->nexthdr;
		inner_offset = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr),
						&nexthdr, &frag_off);
		if (unlikely(inner_offset < 0))
			goto out;

		if (((struct ipv6hdr *)iph)->nexthdr == IPPROTO_TCP)
			return be32_to_cpu(((struct tcphdr *)((void *)iph + 40))->seq);
		else if (((struct ipv6hdr *)iph)->nexthdr == IPPROTO_UDP)
			return be16_to_cpu((__force __be16)((struct udphdr *)
							    ((void *)iph + 40))->check);
		else if (((struct ipv6hdr *)iph)->nexthdr == IPPROTO_ICMPV6)
			return be16_to_cpu((__force __be16)((struct icmp6hdr *)
							    ((void *)iph + 40))->icmp6_cksum);
	}

out:
	return INVALID_PACKET_ID;
}
EXPORT_SYMBOL(mtk_data_get_pkt_id);

module_param(data_plane_bypass, bool, 0444);
MODULE_PARM_DESC(data_plane_bypass,
		 "This is used to bypass data plane\n");
