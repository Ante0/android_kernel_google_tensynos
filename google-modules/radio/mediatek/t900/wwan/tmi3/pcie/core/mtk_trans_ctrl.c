// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#include <linux/device.h>
#include <linux/freezer.h>
#include <linux/hashtable.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/nospec.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "mtk_bm.h"
#include "mtk_cldma.h"
#include "mtk_ctrl_cfg.h"
#include "mtk_ctrl_plane.h"
#include "mtk_debug.h"
#include "mtk_dev.h"
#include "mtk_except.h"
#include "mtk_pci.h"
#include "mtk_port.h"
#if IS_ENABLED(CONFIG_MTK_WWAN_PWRCTL_SUPPORT)
#include "mtk_pwrctl.h"
#endif
#include "mtk_trans_ctrl.h"
#ifdef CONFIG_UT_PCIE_TRANS_CTRL
#include "ut_trans_ctrl.h"
#endif

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#include "pcie/link-exception.h"
#endif

#define TAG	"PCIE_CTRL"
#define RX_CH_ID_SHIFT	16
#define PORT_MTU_MASK	0xFFFF
#define QUEUE_CHL_MASK	0xFFFF

static bool mtk_queue_list_is_full(struct mtk_ctrl_trans *trans, struct queue_info *que)
{
	return trans->trans_list[que->hif_id].skb_list[que->txqno].qlen >= SKB_LIST_MAX_LEN;
}

static bool mtk_ctrl_chs_is_busy_or_empty(struct trb_srv *srv)
{
	struct mtk_ctrl_trans *trans = srv->trans;
	struct srv_que *srv_que;
	struct sk_buff *skb;
	struct trb *trb;
	int i;

	for (i = 0; i < NR_CLDMA; i++)
		list_for_each_entry(srv_que, &srv->srv_q_list[i], list)
			if (!skb_queue_empty(&trans->trans_list[i].skb_list[srv_que->qno])) {
				skb = skb_peek(&trans->trans_list[i].skb_list[srv_que->qno]);
				trb = (struct trb *)skb->cb;
				if (trb->cmd != TRB_CMD_TX ||
				    mtk_cldma_get_tx_budget(srv->trans->dev, i, srv_que->qno))
					return false;
			}

	return true;
}

static void mtk_ctrl_ch_flush(struct sk_buff_head *skb_list)
{
	struct sk_buff *skb;
	struct trb *trb;

	while (!skb_queue_empty(skb_list)) {
		skb = skb_dequeue(skb_list);
		trb = (struct trb *)skb->cb;
		trb->status = -EIO;
		trb->trb_complete(skb);
	}
}

static void mtk_ctrl_chs_flush(struct trb_srv *srv)
{
	struct srv_que *srv_que;
	int i;

	for (i = 0; i < NR_CLDMA; i++)
		list_for_each_entry(srv_que, &srv->srv_q_list[i], list)
			mtk_ctrl_ch_flush(&srv->trans->trans_list[i].skb_list[srv_que->qno]);
}

/**
 *  mtk_ch_status_check() - Checking ch status before enable or disable ch.
 * @trans: pointer to transaction structure
 * @skb: pointer to socket buffer
 *
 * This function called before enable or disable HWQ, check the HWQ status by calculate
 * count of ports which have enabled the HWQ.
 *
 * Return:
 *  0:		first user for enable or last user for disable
 *  -EBUSY:	current HWQ is occupied by other ports
 *  -EINVAL:	error command
 */
static int mtk_ch_status_check(struct mtk_ctrl_trans *trans, struct sk_buff *skb)
{
	struct trb *trb = (struct trb *)skb->cb;
	struct trb_close_priv *trb_close_priv;
	struct trb_open_priv *trb_open_priv;
	struct queue_info *que;
	int ret = 0;

	que = radix_tree_lookup(&trans->queue_tbl, trb->channel_id & QUEUE_CHL_MASK);

	switch (trb->cmd) {
	case TRB_CMD_ENABLE:
		trb_open_priv = (struct trb_open_priv *)skb->data;
		trb_open_priv->log_rg_offset = que->log_rg_offset;
#if IS_ENABLED(CONFIG_GOOGLE_B503643353_DEBUG)
		if (que->hif_id == 1 && que->txqno == 4)
			MTK_INFO(trans->mdev, "Usr_cnt in ENABLE: %d\n",
				 trans->usr_cnt[que->hif_id][que->txqno]);
#endif
		trans->usr_cnt[que->hif_id][que->txqno]++;
#if IS_ENABLED(CONFIG_GOOGLE_B503643353_DEBUG)
		if (trans->usr_cnt[que->hif_id][que->txqno] == 1) {
			if (que->hif_id == 1 && que->txqno == 4) {
				MTK_INFO(trans->mdev,
					 "Usr_cnt:%d hif_id: %d, txqno: %d, ch_id: %x in ENABLE\n",
					 trans->usr_cnt[que->hif_id][que->txqno],
					 que->hif_id, que->txqno, trb->channel_id);
			}
			break;
		}
		if (que->hif_id == 1 && que->txqno == 4)
			MTK_INFO(trans->mdev,
				 "Usr_cnt:%d hif_id: %d, txqno: %d, ch_id: %x at ENABLE\n",
				 trans->usr_cnt[que->hif_id][que->txqno],
				 que->hif_id, que->txqno, trb->channel_id);
#else
		if (trans->usr_cnt[que->hif_id][que->txqno] == 1)
			break;
#endif
		trb_open_priv->tx_mtu = que->tx_mtu;
		trb_open_priv->rx_mtu = que->rx_mtu;
		trb_open_priv->tx_frag_size = que->tx_frag_size;
		trb_open_priv->rx_frag_size = que->rx_frag_size;
		if (mtk_cldma_check_ch_cfg(trans->dev, que)) {
			trans->usr_cnt[que->hif_id][que->txqno]--;
			trb->status = -EINVAL;
			ret = -EINVAL;
		} else {
			trb->status = -EBUSY;
			ret = -EBUSY;
		}
		trb->trb_complete(skb);
		break;
	case TRB_CMD_DISABLE:
		trb_close_priv = (struct trb_close_priv *)skb->data;
		trb_close_priv->trb_start_time  = jiffies;
#if IS_ENABLED(CONFIG_GOOGLE_B503643353_DEBUG)
		if (que->hif_id == 1 && que->txqno == 4)
			MTK_INFO(trans->mdev, "Usr_cnt in DISABLE:%d\n",
				 trans->usr_cnt[que->hif_id][que->txqno]);
#endif
		if (trans->usr_cnt[que->hif_id][que->txqno] > 0) {
			trans->usr_cnt[que->hif_id][que->txqno]--;
#if IS_ENABLED(CONFIG_GOOGLE_B503643353_DEBUG)
			if (!trans->usr_cnt[que->hif_id][que->txqno]) {
				if (que->hif_id == 1 && que->txqno == 4) {
					MTK_INFO(trans->mdev,
						 "Usr_cnt:%d hif_id: %d, txqno: %d, ch_id: %x in DISABLE\n",
						 trans->usr_cnt[que->hif_id][que->txqno],
						 que->hif_id, que->txqno, trb->channel_id);
				}
				break;
			}
#else
			if (!trans->usr_cnt[que->hif_id][que->txqno])
				break;
#endif
		}
#if IS_ENABLED(CONFIG_GOOGLE_B503643353_DEBUG)
		if (que->hif_id == 1 && que->txqno == 4)
			MTK_INFO(trans->mdev,
				 "Usr_cnt:%d hif_id: %d, txqno: %d, ch_id: %x at DISABLE\n",
				 trans->usr_cnt[que->hif_id][que->txqno], que->hif_id,
				 que->txqno, trb->channel_id);
#endif
		trb->status = -EBUSY;
		trb->trb_complete(skb);
		ret = -EBUSY;
		break;
	default:
		MTK_ERR(trans->mdev, "Invalid trb command(%d)\n", trb->cmd);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static void mtk_ch_status_check_late(struct mtk_ctrl_trans *trans, struct sk_buff *skb)
{
	struct trb *trb = (struct trb *)skb->cb;
	struct queue_info *que;

	switch (trb->cmd) {
	case TRB_CMD_ENABLE:
		que = radix_tree_lookup(&trans->queue_tbl, trb->channel_id & QUEUE_CHL_MASK);
		if (!que) {
			MTK_ERR(trans->mdev, "Failed to lookup que, ch_id: %x, que: 0x%p\n",
				trb->channel_id, que);
			return;
		}
		if (trans->usr_cnt[que->hif_id][que->txqno]) {
			MTK_WARN(trans->mdev, "Refund usr cnt, current: %d, ch_id: %x\n",
				 trans->usr_cnt[que->hif_id][que->txqno], trb->channel_id);
			trans->usr_cnt[que->hif_id][que->txqno]--;
		}
		break;
	default:
		break;
	}
}

static void mtk_ctrl_trb_handler(struct trb_srv *srv, struct trans_list *trans_list, u32 qno)
{
	struct sk_buff_head *skb_list = &trans_list->skb_list[qno];
	struct mtk_ctrl_trans *trans = srv->trans;
	struct sk_buff *skb, *skb_next;
	struct trb *trb, *trb_next;
	unsigned long flags;
	bool kick = false;
	int loop = 0;
	int ret;

	do {
		spin_lock_irqsave(&skb_list->lock, flags);
		skb = skb_peek(skb_list);
		spin_unlock_irqrestore(&skb_list->lock, flags);
		if (!skb)
			break;
		trb = (struct trb *)skb->cb;
		kref_get(&trb->kref);

		switch (trb->cmd) {
		case TRB_CMD_ENABLE:
		case TRB_CMD_DISABLE:
			skb_unlink(skb, skb_list);
			ret = mtk_ch_status_check(trans, skb);
			if (!ret) {
				kick = true;
				if (trb->cmd == TRB_CMD_DISABLE)
					mtk_ctrl_ch_flush(skb_list);
			}
			break;
		case TRB_CMD_CHECK_STA:
			skb_unlink(skb, skb_list);
			kick = true;
			break;
		case TRB_CMD_TX:
			ret = mtk_cldma_submit_tx(trans->dev, skb);
			if (ret) {
				if (trans_list->tx_burst_cnt[qno]) {
					kick = true;
				} else if (ret == -EAGAIN) {
					kref_put(&trb->kref, mtk_port_trb_free);
					return;
				}
				break;
			}

			trans_list->tx_burst_cnt[qno]++;
			if (trans_list->tx_burst_cnt[qno] >= TX_BURST_MAX_CNT ||
			    skb_queue_is_last(skb_list, skb)) {
				kick = true;
			} else {
				skb_next = skb_peek_next(skb, skb_list);
				trb_next = (struct trb *)skb_next->cb;
				if (trb_next->cmd != TRB_CMD_TX)
					kick = true;
			}

			skb_unlink(skb, skb_list);
			break;
		default:
			skb_unlink(skb, skb_list);
		}

		if (kick) {
			ret = mtk_cldma_trb_process(trans->dev, skb);
			if (ret < 0)
				mtk_ch_status_check_late(trans, skb);
			trans_list->tx_burst_cnt[qno] = 0;
			kick = false;
		}

		kref_put(&trb->kref, mtk_port_trb_free);

		loop++;
	} while (loop < TRB_NUM_PER_ROUND);
}

static void mtk_ctrl_trb_process(struct trb_srv *srv)
{
	struct mtk_ctrl_trans *trans = srv->trans;
	struct srv_que *srv_que;
	int i;

	for (i = 0; i < NR_CLDMA; i++)
		list_for_each_entry(srv_que, &srv->srv_q_list[i], list)
			mtk_ctrl_trb_handler(srv, &trans->trans_list[i], srv_que->qno);
}

static int mtk_ctrl_trb_thread(void *args)
{
	struct trb_srv *srv = args;

	for (;;) {
		wait_event_interruptible(srv->trb_waitq,
					 !mtk_ctrl_chs_is_busy_or_empty(srv) ||
					 kthread_should_stop() || kthread_should_park());
		if (kthread_should_stop())
			break;

		if (kthread_should_park())
			kthread_parkme();

		while (!mtk_ctrl_chs_is_busy_or_empty(srv) &&
		       !kthread_should_stop() && !kthread_should_park()) {
			mtk_pm_runtime_get(srv->trans->mdev, MTK_USER_CTRL, true);
			mtk_ctrl_trb_process(srv);
			mtk_pm_runtime_put(srv->trans->mdev, MTK_USER_CTRL, true);
			cond_resched();
		}
	}
	mtk_ctrl_chs_flush(srv);
	return 0;
}

static int mtk_ctrl_trb_srv_init(struct mtk_ctrl_trans *trans)
{
	struct srv_que *srv_que;
	struct trb_srv *srv;
	int i, j;
	int ret;

	for (i = 0; i < trans->trb_srv_num; i++) {
		srv = devm_kzalloc(trans->mdev->dev, sizeof(*srv), GFP_KERNEL);
		if (!srv) {
			ret = -ENOMEM;
			goto err_free_srv;
		}

		srv->trans = trans;
		srv->srv_id = i;
		trans->trb_srv[i] = srv;

		init_waitqueue_head(&srv->trb_waitq);
		for (j = 0; j < NR_CLDMA; j++)
			INIT_LIST_HEAD(&srv->srv_q_list[j]);
	}

	for (i = 0; i < NR_CLDMA; i++)
		for (j = 0; j < HW_QUE_NUM; j++) {
			if (trans->srv_cfg[i][j] < 0 ||
			    trans->srv_cfg[i][j] >= trans->trb_srv_num)
				trans->srv_cfg[i][j] = 0;
			srv_que = devm_kzalloc(trans->mdev->dev, sizeof(*srv_que), GFP_KERNEL);
			if (!srv_que) {
				ret = -ENOMEM;
				goto err_free_srv_que;
			}
			srv_que->hif_id = i;
			srv_que->qno = j;
			list_add_tail(&srv_que->list,
				      &trans->trb_srv[trans->srv_cfg[i][j]]->srv_q_list[i]);
		}

	for (i = 0; i < trans->trb_srv_num; i++)
		trans->trb_srv[i]->trb_thread = kthread_run(mtk_ctrl_trb_thread, trans->trb_srv[i],
							    "mtk_trb_srv%d_%s", i,
							    trans->mdev->dev_str);

	return 0;
err_free_srv_que:
	for (i = 0; i < trans->trb_srv_num; i++) {
		for (j = 0; j < NR_CLDMA; j++) {
			struct srv_que *next_srv_que;

			list_for_each_entry_safe(srv_que, next_srv_que,
						 &trans->trb_srv[i]->srv_q_list[j], list) {
				list_del(&srv_que->list);
				devm_kfree(trans->mdev->dev, srv_que);
			}
		}
	}
err_free_srv:
	for (i = 0; i < trans->trb_srv_num; i++) {
		if (!trans->trb_srv[i])
			break;
		devm_kfree(trans->mdev->dev, trans->trb_srv[i]);
		trans->trb_srv[i] = NULL;
	}

	return ret;
}

static void mtk_ctrl_trb_srv_exit(struct mtk_ctrl_trans *trans)
{
	struct srv_que *srv_que, *next_srv_que;
	struct trb_srv *srv;
	int i, j;

	for (i = 0; i < trans->trb_srv_num; i++) {
		srv = trans->trb_srv[i];
		kthread_stop(srv->trb_thread);
		for (j = 0; j < NR_CLDMA; j++) {
			list_for_each_entry_safe(srv_que, next_srv_que,
						 &trans->trb_srv[i]->srv_q_list[j], list) {
				list_del(&srv_que->list);
				devm_kfree(trans->mdev->dev, srv_que);
			}
		}
		devm_kfree(trans->mdev->dev, srv);
		trans->trb_srv[i] = NULL;
	}
}

/**
 * mtk_trans_ctrl_prepare() - perform the additional part that
 * system suspend exceeds RPM suspend.
 * @mdev: pointer to mtk_md_dev
 * @param: pointer to transaction structure
 * @is_smart_suspend: true means is smart suspend
 *
 * This function called by pm when entering smart suspend. Since
 * already in RPM suspend state, entering smart suspend only needs
 * to perform the additional part that system suspend exceeds RPM
 * suspend. Do not access HW register in this function because PCIe
 * link is not ready at this time.
 *
 * Return:
 * 0:	 success.
 */
static int mtk_trans_ctrl_prepare(struct mtk_md_dev *mdev, void *param, bool is_smart_suspend)
{
	struct mtk_ctrl_trans *trans = param;
	int i;

	if (is_smart_suspend) {
		for (i = 0; i < trans->trb_srv_num; i++)
			kthread_park(trans->trb_srv[i]->trb_thread);
	}

	return 0;
}

/**
 * mtk_trans_ctrl_complete() - perform the additional part that
 * system resume exceeds RPM resume.
 * @mdev: pointer to mtk_md_dev
 * @param: pointer to transaction structure
 * @is_smart_suspend: true means is smart suspend
 *
 * This function called by pm when exiting smart suspend. Since
 * keeping RPM suspend is need after exiting smart suspend, only
 * needs to perform the additional part that system resume exceeds
 * RPM resume. Do not access HW register in this function because
 * PCIe link is not ready at this time.
 *
 * Return:
 * 0:	success.
 */
static int mtk_trans_ctrl_complete(struct mtk_md_dev *mdev, void *param, bool is_smart_suspend)
{
	struct mtk_ctrl_trans *trans = param;
	int i;

	if (is_smart_suspend) {
		for (i = 0; i < trans->trb_srv_num; i++)
			kthread_unpark(trans->trb_srv[i]->trb_thread);
	}

	return 0;
}

static int mtk_trans_ctrl_suspend(struct mtk_md_dev *mdev, void *param, bool is_runtime)
{
	struct mtk_ctrl_trans *trans = param;
	int i;

	if (!is_runtime) {
		for (i = 0; i < trans->trb_srv_num; i++)
			kthread_park(trans->trb_srv[i]->trb_thread);
	}

	mtk_cldma_suspend(trans);

	return 0;
}

static int mtk_trans_ctrl_suspend_late(struct mtk_md_dev *mdev, void *param, bool is_runtime)
{
	struct mtk_ctrl_trans *trans = param;

	mtk_cldma_suspend_late(trans);

	return 0;
}

static int mtk_trans_ctrl_resume_early(struct mtk_md_dev *mdev, void *param, bool is_runtime,
				       bool link_ready)
{
	struct mtk_ctrl_trans *trans = param;

	mtk_cldma_resume_early(trans, link_ready);

	return 0;
}

static int mtk_trans_ctrl_resume(struct mtk_md_dev *mdev, void *param, bool is_runtime,
				 bool link_ready)
{
	struct mtk_ctrl_trans *trans = param;
	int i;

	mtk_cldma_resume(trans, link_ready);

	if (!is_runtime) {
		for (i = 0; i < trans->trb_srv_num; i++)
			kthread_unpark(trans->trb_srv[i]->trb_thread);
	}

	return 0;
}

static int mtk_ctrl_pm_init(struct mtk_ctrl_trans *trans)
{
	struct mtk_pm_entity *pm_entity;
	int ret;

	pm_entity = &trans->pm_entity;
	INIT_LIST_HEAD(&pm_entity->entry);
	pm_entity->user = MTK_USER_CTRL;
	pm_entity->param = trans;
	pm_entity->prepare = mtk_trans_ctrl_prepare;
	pm_entity->complete = mtk_trans_ctrl_complete;
	pm_entity->suspend = mtk_trans_ctrl_suspend;
	pm_entity->suspend_late = mtk_trans_ctrl_suspend_late;
	pm_entity->resume_early = mtk_trans_ctrl_resume_early;
	pm_entity->resume = mtk_trans_ctrl_resume;
	ret = mtk_pm_entity_register(trans->mdev, pm_entity);
	if (ret < 0)
		MTK_ERR(trans->mdev, "Failed to register ctrl pm_entity\n");

	return ret;
}

static int mtk_ctrl_pm_exit(struct mtk_ctrl_trans *trans)
{
	int ret;

	ret = mtk_pm_entity_unregister(trans->mdev, &trans->pm_entity);
	if (ret < 0)
		MTK_ERR(trans->mdev, "Failed to unregister ctrl pm_entity\n");

	return ret;
}

static void mtk_ctrl_remove_radix_tree(struct mtk_ctrl_trans *trans)
{
	struct queue_info **queues;
	int ret, idx;

	queues = kcalloc(trans->queues_cnt, sizeof(struct queue_info *), GFP_KERNEL);
	if (!queues) {
		MTK_ERR(trans->mdev, "Failed to alloc queues to remove ctrl radix tree\n");
		return;
	}

	ret = radix_tree_gang_lookup(&trans->queue_tbl, (void **)queues,
				     0, trans->queues_cnt);
	for (idx = 0; idx < ret; idx++) {
		radix_tree_delete(&trans->queue_tbl, queues[idx]->rx_chl & QUEUE_CHL_MASK);
		kfree(queues[idx]);
	}
	kfree(queues);
}

static void mtk_ctrl_queue_info_update(struct radix_tree_root *queue_tbl, u32 port_chl_mtu)
{
	struct queue_info *queue;
	u32 rx_chl, mtu;

	if (!port_chl_mtu)
		return;

	rx_chl = port_chl_mtu >> RX_CH_ID_SHIFT;
	mtu = port_chl_mtu & PORT_MTU_MASK;
	queue = radix_tree_lookup(queue_tbl, rx_chl);
	if (!queue)
		return;

	queue->tx_mtu = mtu;
	queue->rx_mtu = mtu;
	queue->tx_frag_size = mtu;
	queue->rx_frag_size = mtu;
}

static unsigned int ctrl_port_chl_mtu;

static int mtk_pcie_hif_init(struct mtk_md_dev *mdev)
{
	struct mtk_ctrl_blk *ctrl_blk = mdev->ctrl_blk;
	struct queue_info *queue, *queue_info;
	struct mtk_ctrl_trans *trans;
	int i, j;
	int ret;

	trans = ctrl_blk->ctrl_hw_priv;
	trans->ctrl_blk = ctrl_blk;
	queue_info = trans->queue_info;

	INIT_RADIX_TREE(&trans->queue_tbl, GFP_KERNEL);
	for (i = 0; i < trans->queue_info_num; i++) {
		queue = kmemdup(queue_info + i, sizeof(*queue), GFP_KERNEL);
		if (!queue) {
			MTK_ERR(mdev, "Failed to alloc memory for queue %x\n",
				((struct queue_info *)(queue_info + i))->rx_chl);
			ret = -ENOMEM;
			goto err_free_radix_tree;
		}
		if (queue->txqno >= HW_QUE_NUM || queue->rxqno >= HW_QUE_NUM ||
		    queue->hif_id >= NR_CLDMA) {
			MTK_ERR(mdev, "Failed to get correct queue info %x\n", queue->rx_chl);
			ret = -EINVAL;
			goto err_free_radix_tree;
		}
		ret = radix_tree_insert(&trans->queue_tbl, queue->rx_chl & QUEUE_CHL_MASK, queue);
		if (ret) {
			MTK_ERR(mdev, "Insert %x fail, ret: %d", queue->rx_chl, ret);
			kfree(queue);
			goto err_free_radix_tree;
		}
		trans->queues_cnt++;
	}

	mtk_ctrl_queue_info_update(&trans->queue_tbl, ctrl_port_chl_mtu);

	for (i = 0; i < NR_CLDMA; i++) {
		for (j = 0; j < HW_QUE_NUM; j++) {
			skb_queue_head_init(&trans->trans_list[i].skb_list[j]);
			trans->trans_list[i].tx_burst_cnt[j] = 0;
		}
	}
	ret = mtk_cldma_init(trans);
	if (ret)
		goto err_free_radix_tree;

	ret = mtk_ctrl_trb_srv_init(trans);
	if (ret)
		goto err_cldma_exit;

	atomic_set(&trans->available, 1);

	ret = mtk_ctrl_pm_init(trans);
	if (ret)
		goto err_srv_exit;

	return 0;

err_srv_exit:
	atomic_set(&trans->available, 0);
	mtk_ctrl_trb_srv_exit(trans);
err_cldma_exit:
	mtk_cldma_exit(trans);
err_free_radix_tree:
	mtk_ctrl_remove_radix_tree(trans);

	return ret;
}

static int mtk_pcie_hif_exit(struct mtk_md_dev *mdev)
{
	struct mtk_ctrl_blk *ctrl_blk = mdev->ctrl_blk;
	struct mtk_ctrl_trans *trans;

	trans = ctrl_blk->ctrl_hw_priv;

	atomic_set(&trans->available, 0);
	mtk_ctrl_trb_srv_exit(trans);
	mtk_ctrl_remove_radix_tree(trans);
	mtk_cldma_exit(trans);
	mtk_ctrl_pm_exit(trans);

	return 0;
}

static int mtk_pcie_queue_info_update(struct mtk_ctrl_trans *trans, struct queue_info *que,
				      struct mtk_port_cfg_ch_info *channel_info)
{
	int hif_id, txqno, rxqno;

	if (channel_info->ul_hw_queue_id >= HW_QUE_NUM ||
	    channel_info->dl_hw_queue_id >= HW_QUE_NUM ||
	    HIF_ID(channel_info->peer_id) >= NR_CLDMA) {
		MTK_ERR(trans->mdev, "Failed to get correct CFG_CH_INFO\n");
		return -EINVAL;
	}

	hif_id = array_index_nospec(HIF_ID(channel_info->peer_id), NR_CLDMA);
	txqno = array_index_nospec(channel_info->ul_hw_queue_id, HW_QUE_NUM);
	rxqno = array_index_nospec(channel_info->dl_hw_queue_id, HW_QUE_NUM);

	que->tx_mtu = trans->tx_mtu_cfg[hif_id][txqno];
	que->rx_mtu = trans->rx_mtu_cfg[hif_id][rxqno];
	if (hif_id == CLDMA1) {
		if (que->tx_mtu > MTU_RSV_ROOM && que->rx_mtu > MTU_RSV_ROOM) {
			que->tx_mtu -= MTU_RSV_ROOM;
			que->rx_mtu -= MTU_RSV_ROOM;
		} else {
			que->tx_mtu = 0;
			que->rx_mtu = 0;
		}
	}

	que->tx_chl = le16_to_cpu(channel_info->ul_ch_id);
	que->rx_chl = le16_to_cpu(channel_info->dl_ch_id);
	que->hif_id = hif_id;
	que->txqno = txqno;
	que->rxqno = rxqno;
	que->tx_nr_gpds = TX_GPD_NUM;
	/* the number of RX GPDs should be at last two */
	que->rx_nr_gpds = RX_GPD_NUM;
	que->tx_frag_size = que->tx_mtu;
	que->rx_frag_size = que->rx_mtu;

	return 0;
}

static int mtk_pcie_que_cfg(struct mtk_ctrl_trans *trans, struct sk_buff *skb)
{
	struct mtk_port_cfg_ch_info *channel_info;
	struct trb *trb = (struct trb *)skb->cb;
	struct mtk_port_cfg_hif_info *hif_info;
	struct queue_info que_hif;
	int hif_id, txqno, rxqno;
	struct queue_info *que;
	int len = 0, ret = 0;

	switch (trb->cmd) {
	case TRB_CMD_SET_HIF_CFG:
		hif_info = (struct mtk_port_cfg_hif_info *)skb->data;
		while (len < skb->len) {
			if (unlikely(skb->len - len < sizeof(*hif_info))) {
				MTK_ERR(trans->mdev, "Failed to parse hif_info: remain len: %d\n",
					skb->len - len);
				ret = -EINVAL;
				break;
			}
			que_hif.hif_id = HIF_ID(hif_info->peer_id);
			que_hif.txqno = hif_info->ul_hw_queue_id;
			que_hif.rxqno = hif_info->dl_hw_queue_id;
			que_hif.tx_mtu = le32_to_cpu(hif_info->ul_hw_queue_mtu);
			que_hif.rx_mtu = le32_to_cpu(hif_info->dl_hw_queue_mtu);

			if (que_hif.tx_mtu > Q_MTU_63K || que_hif.rx_mtu > Q_MTU_63K ||
			    que_hif.txqno >= HW_QUE_NUM || que_hif.rxqno >= HW_QUE_NUM ||
			    que_hif.hif_id >= NR_CLDMA) {
				MTK_ERR(trans->mdev, "Failed to get correct CFG_HIF_INFO\n");
				ret = -EINVAL;
				break;
			}

			hif_id = array_index_nospec(que_hif.hif_id, NR_CLDMA);
			txqno = array_index_nospec(que_hif.txqno, HW_QUE_NUM);
			rxqno = array_index_nospec(que_hif.rxqno, HW_QUE_NUM);

			trans->tx_mtu_cfg[hif_id][txqno] = que_hif.tx_mtu;
			trans->rx_mtu_cfg[hif_id][rxqno] = que_hif.rx_mtu;

			hif_info++;
			len += sizeof(*hif_info);
		}
		trb->trb_complete(skb);
		break;
	case TRB_CMD_SET_CH_CFG:
		channel_info = (struct mtk_port_cfg_ch_info *)skb->data;
		while (len < skb->len) {
			if (unlikely(skb->len - len < sizeof(*channel_info))) {
				MTK_ERR(trans->mdev, "Failed to parse chl_info: remain len: %d\n",
					skb->len - len);
				ret = -EINVAL;
				break;
			}
			que = radix_tree_lookup(&trans->queue_tbl,
						le16_to_cpu(channel_info->dl_ch_id) &
						QUEUE_CHL_MASK);
			if (que) {
				ret = mtk_pcie_queue_info_update(trans, que, channel_info);
				if (ret)
					break;
			} else {
				que = kzalloc(sizeof(*que), GFP_KERNEL);
				if (!que) {
					MTK_ERR(trans->mdev,
						"Failed to alloc memory for que chl %x\n",
						channel_info->dl_ch_id);
					ret = -ENOMEM;
					break;
				}
				ret = mtk_pcie_queue_info_update(trans, que, channel_info);
				if (ret) {
					kfree(que);
					break;
				}
				ret = radix_tree_insert(&trans->queue_tbl,
							que->rx_chl & QUEUE_CHL_MASK, que);
				if (ret) {
					MTK_WARN(trans->mdev, "Insert %x fail, ret: %d",
						 que->rx_chl, ret);
					kfree(que);
					break;
				}
				trans->queues_cnt++;
			}
			channel_info++;
			len += sizeof(*channel_info);
		}
		trb->trb_complete(skb);
		break;
	default:
		MTK_ERR(trans->mdev, "Invalid trb command(%d)\n", trb->cmd);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int mtk_pcie_hif_submit_skb(struct mtk_md_dev *mdev, struct sk_buff *skb, bool force_send)
{
	struct mtk_ctrl_blk *ctrl_blk = mdev->ctrl_blk;
	struct mtk_ctrl_trans *trans;
	struct queue_info *que;
	struct trb *trb;
	int ret;

	trans = ctrl_blk->ctrl_hw_priv;
	trb = (struct trb *)skb->cb;

	if (trb->cmd == TRB_CMD_SET_CH_CFG || trb->cmd == TRB_CMD_SET_HIF_CFG) {
		ret = mtk_pcie_que_cfg(trans, skb);
		return ret;
	}

	if (trb->cmd == TRB_CMD_STOP || trb->cmd == TRB_CMD_RECOVER) {
		trb->trb_complete(skb);
		return 0;
	}

	que = radix_tree_lookup(&trans->queue_tbl, trb->channel_id & QUEUE_CHL_MASK);
	if (!que) {
		MTK_WARN(mdev, "lookup que fail, ch_id: %x, que: 0x%p\n",
			 trb->channel_id, que);
		return -EINVAL;
	}

	if (!atomic_read(&trans->available))
		return -EIO;

	if (mtk_queue_list_is_full(trans, que) && !force_send)
		return -EAGAIN;

	if (trb->cmd == TRB_CMD_DISABLE)
		skb_queue_head(&trans->trans_list[que->hif_id].skb_list[que->txqno], skb);
	else
		skb_queue_tail(&trans->trans_list[que->hif_id].skb_list[que->txqno], skb);

	wake_up(&trans->trb_srv[trans->srv_cfg[que->hif_id][que->txqno]]->trb_waitq);

	return 0;
}

static void mtk_pcie_hif_fsm_indication(struct mtk_md_dev *mdev, struct mtk_fsm_param *param)
{
	struct mtk_ctrl_blk *ctrl_blk = mdev->ctrl_blk;
	struct mtk_ctrl_trans *trans;

	trans = ctrl_blk->ctrl_hw_priv;
	mtk_cldma_fsm_state_listener(param, trans);
}

static int mtk_pcie_hif_dump(struct mtk_md_dev *mdev)
{
	struct mtk_ctrl_blk *ctrl_blk = mdev->ctrl_blk;
	struct mtk_ctrl_trans *trans;

	trans = ctrl_blk->ctrl_hw_priv;
	if (!trans)
		return 0;

	return mtk_cldma_dump(trans);
}

static int mtk_pcie_hif_cmd_func(struct mtk_md_dev *mdev, int cmd, void *data)
{
	struct mtk_ctrl_blk *ctrl_blk = mdev->ctrl_blk;
	struct mtk_ctrl_trans *trans;
	struct queue_info *que;
	int ret = 0;

	switch (cmd) {
	case HIF_CTRL_CMD_TRM_NOTIFY:
		mtk_dev_send_dev_evt(mdev, DEV_EVT_H2D_TRM_NOTIFY);
		/* to make sure TRM notify has been sent to device.
		 * if not, trigger link error flow.
		 */
		if (!mtk_pci_mmio_check(mdev))
			mtk_exception_report_evt(mdev, EXCEPTION_LINK_ERR);
		break;
	case HIF_CTRL_CMD_CHECK_TX_FULL:
		trans = ctrl_blk->ctrl_hw_priv;
		que = radix_tree_lookup(&trans->queue_tbl,
					((union ctrl_hif_cmd_data *)data)->rx_ch & QUEUE_CHL_MASK);
		if (!que) {
			MTK_INFO(mdev, "Failed to find que to check tx full\n");
			return -EINVAL;
		}
		return mtk_queue_list_is_full(trans, que);
	case HIF_CTRL_CMD_RPM_GET:
		return mtk_pm_runtime_get(mdev, MTK_USER_CTRL, *(bool *)data);
	case HIF_CTRL_CMD_RPM_PUT:
		return mtk_pm_runtime_put(mdev, MTK_USER_CTRL, *(bool *)data);
	case HIF_CTRL_CMD_TX_ABORT:
#ifdef CONFIG_MTK_WWAN_PWRCTL_SUPPORT
		MTK_INFO(mdev, "CTRL hif_cmd tx_abort trigger MDEE\n");
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		radio_google_cldma_error_handler(mdev->google, EXCP_REASON_CLDMA_TX_TIMEOUT);
#endif
		mtk_pwrctl_force_md_assert();
#else
		ret = -EOPNOTSUPP;
#endif
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static struct mtk_ctrl_hif_ops pcie_ctrl_ops = {
	.init = mtk_pcie_hif_init,
	.exit = mtk_pcie_hif_exit,
	.submit_skb = mtk_pcie_hif_submit_skb,
	.fsm_indication = mtk_pcie_hif_fsm_indication,
	.dump = mtk_pcie_hif_dump,
	.send_cmd = mtk_pcie_hif_cmd_func,
};

static void mtk_trans_get_ctrl_info(struct mtk_ctrl_cfg *cfg,
				    struct mtk_ctrl_trans *trans, u32 hw_ver)
{
	struct mtk_ctrl_info_desc *ctrl_info_desc;
	struct mtk_ctrl_info *ctrl_info;
	u8 i;

	for (i = 0; (ctrl_info_desc = &mtk_ctrl_info_tbl[i]) && ctrl_info_desc &&
	     ctrl_info_desc->ctrl_info; i++) {
		if (ctrl_info_desc->hw_ver != hw_ver)
			continue;

		ctrl_info = ctrl_info_desc->ctrl_info;
		cfg->fsm_cfg = ctrl_info->ctrl_cfg->fsm_cfg;
		cfg->port_layer_cfg = ctrl_info->ctrl_cfg->port_layer_cfg;
		memcpy(trans->srv_cfg, ctrl_info->srv_cfg,
		       sizeof(int) * NR_CLDMA * HW_QUE_NUM);
		trans->drv_cfg = ctrl_info->drv_cfg;
		trans->queue_info = ctrl_info->queue_info;
		trans->queue_info_num = ctrl_info->queue_info_num;
		trans->trb_srv_num = ctrl_info->trb_srv_num;
	}
}

int mtk_trans_ctrl_init(struct mtk_md_dev *mdev)
{
	struct mtk_ctrl_blk *ctrl_blk;
	struct mtk_ctrl_trans *trans;
	struct mtk_ctrl_cfg *cfg;
	int err;

	trans = devm_kzalloc(mdev->dev, sizeof(*trans), GFP_KERNEL);
	if (!trans)
		return -ENOMEM;
	trans->mdev = mdev;
	trans->queues_cnt = 0;

	cfg = devm_kzalloc(mdev->dev, sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		goto err_free_trans;

	mtk_trans_get_ctrl_info(cfg, trans, mdev->hw_ver);
	if (!cfg->fsm_cfg || !cfg->port_layer_cfg || !trans->queue_info ||
	    trans->trb_srv_num <= 0 || trans->trb_srv_num > TRB_SRV_MAX_NUM ||
	    trans->queue_info_num <= 0 || !trans->drv_cfg) {
		MTK_ERR(mdev, "Failed to get ctrl info!\n");
		goto err_free_cfg;
	}

	err = mtk_ctrl_init(mdev, &pcie_ctrl_ops, cfg);
	if (err)
		goto err_free_cfg;

	ctrl_blk = mdev->ctrl_blk;
	ctrl_blk->ctrl_hw_priv = trans;

	ctrl_blk->bm_pool = mtk_bm_pool_create(mdev, MTK_BUFF_SKB, Q_MTU_3_5K,
					       100, MTK_BM_LOW_PRIO, MTK_BUFF_RELOAD,
					       "ctrl_bm_pool_%s", mdev->dev_str);
	if (!ctrl_blk->bm_pool) {
		err = -ENOMEM;
		goto err_ctrl_exit;
	}

	ctrl_blk->bm_pool_63K = mtk_bm_pool_create(mdev, MTK_BUFF_SKB, Q_MTU_63K,
						   100, MTK_BM_LOW_PRIO, 0,
						   "ctrl_bm_pool_63K_%s", mdev->dev_str);
	if (!ctrl_blk->bm_pool_63K) {
		err = -ENOMEM;
		goto err_destroy_pool;
	}

	return 0;

err_destroy_pool:
	mtk_bm_pool_destroy(mdev, ctrl_blk->bm_pool);
err_ctrl_exit:
	mtk_ctrl_exit(mdev);
err_free_cfg:
	devm_kfree(mdev->dev, cfg);
err_free_trans:
	devm_kfree(mdev->dev, trans);
	return -ENOMEM;
}

int mtk_trans_ctrl_exit(struct mtk_md_dev *mdev)
{
	struct mtk_bm_pool *bm_pool_63K;
	struct mtk_ctrl_blk *ctrl_blk;
	struct mtk_ctrl_trans *trans;
	struct mtk_bm_pool *bm_pool;

	ctrl_blk = mdev->ctrl_blk;
	trans = ctrl_blk->ctrl_hw_priv;
	bm_pool = ctrl_blk->bm_pool;
	bm_pool_63K = ctrl_blk->bm_pool_63K;

	devm_kfree(mdev->dev, ctrl_blk->cfg);
	mtk_ctrl_exit(mdev);
	mtk_bm_pool_destroy(mdev, bm_pool);
	mtk_bm_pool_destroy(mdev, bm_pool_63K);
	devm_kfree(mdev->dev, trans);

	return 0;
}

module_param(ctrl_port_chl_mtu, uint, 0644);
MODULE_PARM_DESC(ctrl_port_chl_mtu, "This is used to config the ctrl port mtu!\n");
