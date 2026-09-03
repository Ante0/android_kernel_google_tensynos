/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2023, MediaTek Inc.
 */

#ifndef __MTK_PCIE_MEMLOG_H__
#define __MTK_PCIE_MEMLOG_H__

#include "mtk_memlog.h"

#define MTK_DATA_RX_MEMLOG_RG(rxq_id)  ((MTK_MEMLOG_RG_DATA_RX) + (rxq_id))
#define MTK_DATA_RX_H_MEMLOG_RG(rxq_id) ((MTK_MEMLOG_RG_DATA_RX_H_FREQ) + (rxq_id))

#ifdef CONFIG_MTK_MEMLOG_EVENT_SUPPORT

enum pcie_memlog_event_type {
	PM_DS_LOCK = EXTERN_EVT_TYPE,
	/* Add new value after this line */
	PM_RT_IDLE,
	CTRL_TX_DONE,
	CTRL_RX_DONE,
	DATA_TX_PKT_INFO,
	DATA_PIT_MSG_INFO,
	DATA_RX_PKT_INFO,
	DATA_IRQ_SRC_INFO,
	DATA_IRQ_HANDLER_INFO,
	STATS_CTRL_TX,
	STATS_CTRL_RX,
	STATS_DATA_TX,
	STATS_DATA_RX,
	STATS_PCI,
	STATS_PM,
};

struct event_pm_ds_lock {
	struct memlog_event_msg event_msg;
	u32 user_id;
	u64 ds_lock_sent;
	u32 reg_value;
} __packed;

#define MTK_DBG_PM_DS_LOCK(mdev, __user_id, __ds_lock_sent, __reg_value) \
do { \
	struct event_pm_ds_lock *event_pm_ds_lock; \
	struct mtk_md_dev *__mdev = mdev; \
	event_pm_ds_lock = mtk_memlog_req_address(__mdev, MTK_MEMLOG_RG_PM_H_FREQ, \
						  sizeof(struct event_pm_ds_lock)); \
	if (!event_pm_ds_lock) \
		break; \
	event_pm_ds_lock->user_id = __user_id; \
	event_pm_ds_lock->ds_lock_sent = __ds_lock_sent; \
	event_pm_ds_lock->reg_value = __reg_value; \
	mtk_memlog_event_msg_init(&event_pm_ds_lock->event_msg, PM_DS_LOCK); \
	mtk_memlog_req_done(__mdev, MTK_MEMLOG_RG_PM_H_FREQ); \
} while (0)

struct event_pm_rt_idle {
	struct memlog_event_msg event_msg;
	u16 delay_seconds;
} __packed;

#define MTK_DBG_PM_RT_IDLE(mdev, __delay_seconds) \
do { \
	struct event_pm_rt_idle *event_pm_rt_idle; \
	struct mtk_md_dev *__mdev = mdev; \
	event_pm_rt_idle = mtk_memlog_req_address(__mdev, MTK_MEMLOG_RG_PM_H_FREQ, \
						  sizeof(struct event_pm_rt_idle)); \
	if (!event_pm_rt_idle) \
		break; \
	event_pm_rt_idle->delay_seconds = __delay_seconds; \
	mtk_memlog_event_msg_init(&event_pm_rt_idle->event_msg, PM_RT_IDLE); \
	mtk_memlog_req_done(__mdev, MTK_MEMLOG_RG_PM_H_FREQ); \
} while (0)

struct event_ctrl_tx_done {
	struct memlog_event_msg event_msg;
	u8 cldma_id;
	u8 txq_no;
	u8 wr_idx;
	u8 free_idx;
	u8 cnt;
	u8 budget;
	u8 flag;
} __packed;

#define MTK_DBG_CTRL_TX_DONE(mdev, __cldma_id, __txq_no, __wr_idx, __free_idx, \
			     __cnt, __budget, __flag, __rg_offset) \
do {\
	struct event_ctrl_tx_done *event_ctrl_tx_done; \
	struct mtk_md_dev *__mdev = mdev; \
	u8 rg_offset = __rg_offset; \
	event_ctrl_tx_done = mtk_memlog_req_address(__mdev, \
						    MTK_MEMLOG_RG_CTRL_H_FREQ + rg_offset, \
						    sizeof(struct event_ctrl_tx_done)); \
	if (!event_ctrl_tx_done) \
		break; \
	event_ctrl_tx_done->cldma_id = __cldma_id; \
	event_ctrl_tx_done->txq_no = __txq_no; \
	event_ctrl_tx_done->wr_idx = __wr_idx; \
	event_ctrl_tx_done->free_idx = __free_idx; \
	event_ctrl_tx_done->cnt = __cnt; \
	event_ctrl_tx_done->budget = __budget; \
	event_ctrl_tx_done->flag = __flag; \
	mtk_memlog_event_msg_init(&event_ctrl_tx_done->event_msg, CTRL_TX_DONE); \
	mtk_memlog_req_done(__mdev, MTK_MEMLOG_RG_CTRL_H_FREQ + rg_offset); \
} while (0)

struct event_ctrl_rx_done {
	struct memlog_event_msg event_msg;
	u8 cldma_id;
	u8 rxq_no;
	u8 free_idx;
	u8 cnt;
} __packed;

#define MTK_DBG_CTRL_RX_DONE(mdev, __cldma_id, __rxq_no, __free_idx, __cnt, __rg_offset) \
do {\
	struct event_ctrl_rx_done *event_ctrl_rx_done; \
	struct mtk_md_dev *__mdev = mdev; \
	u8 rg_offset = __rg_offset; \
	event_ctrl_rx_done = mtk_memlog_req_address(__mdev, \
						    MTK_MEMLOG_RG_CTRL_H_FREQ + rg_offset, \
						    sizeof(struct event_ctrl_rx_done)); \
	if (!event_ctrl_rx_done) \
		break; \
	event_ctrl_rx_done->cldma_id = __cldma_id; \
	event_ctrl_rx_done->rxq_no = __rxq_no; \
	event_ctrl_rx_done->free_idx = __free_idx; \
	event_ctrl_rx_done->cnt = __cnt; \
	mtk_memlog_event_msg_init(&event_ctrl_rx_done->event_msg, CTRL_RX_DONE); \
	mtk_memlog_req_done(__mdev, MTK_MEMLOG_RG_CTRL_H_FREQ + rg_offset); \
} while (0)

struct event_data_tx_pkt_info {
	struct memlog_event_msg event_msg;
	u8 txq_id;
	u8 payload_cnt;
	u16 drb_msg_idx;
	u32 id;
	u32 drb_msg_adds0;
	u32 drb_msg_adds1;
} __packed;

#define MTK_DBG_DATA_TX_PKT_INFO(mdev, __txq_id, __payload_cnt, __msg_idx, \
				 __id, __msg_adds0, __msg_adds1) \
do { \
	struct event_data_tx_pkt_info *event_data_tx_pkt_info; \
	struct mtk_md_dev *__mdev = mdev; \
	event_data_tx_pkt_info = mtk_memlog_req_address(__mdev, MTK_MEMLOG_RG_DATA_TX_H_FREQ, \
							sizeof(struct event_data_tx_pkt_info)); \
	if (!event_data_tx_pkt_info) \
		break; \
	event_data_tx_pkt_info->txq_id = __txq_id; \
	event_data_tx_pkt_info->payload_cnt = __payload_cnt; \
	event_data_tx_pkt_info->drb_msg_idx = __msg_idx; \
	event_data_tx_pkt_info->id = __id; \
	event_data_tx_pkt_info->drb_msg_adds0 = __msg_adds0; \
	event_data_tx_pkt_info->drb_msg_adds1 = __msg_adds1; \
	mtk_memlog_event_msg_init(&event_data_tx_pkt_info->event_msg, DATA_TX_PKT_INFO); \
	mtk_memlog_req_done(__mdev, MTK_MEMLOG_RG_DATA_TX_H_FREQ); \
} while (0)

struct event_data_pit_msg_info {
	struct memlog_event_msg event_msg;
	u8 rxq_id;
	u8 pit_msg_chnl_id;
	u8 pit_msg_checksum: 2;
	u8 pit_msg_dp: 1;
	u8 pit_msg_err: 1;
	u8 reseve: 4;
} __packed;

#define MTK_DBG_DATA_RX_MSG_INFO(mdev, __rxq_id, __chnl_id, __checksum, __msg_dp, \
				 __msg_err) \
do { \
	struct event_data_pit_msg_info *event_data_pit_msg_info; \
	struct mtk_md_dev *__mdev = mdev; \
	u8 rxq_id = __rxq_id; \
	event_data_pit_msg_info = mtk_memlog_req_address(__mdev, \
							 MTK_DATA_RX_H_MEMLOG_RG(rxq_id), \
							 sizeof(struct event_data_pit_msg_info)); \
	if (!event_data_pit_msg_info) \
		break; \
	event_data_pit_msg_info->rxq_id = rxq_id; \
	event_data_pit_msg_info->pit_msg_chnl_id = __chnl_id; \
	event_data_pit_msg_info->pit_msg_checksum = __checksum; \
	event_data_pit_msg_info->pit_msg_dp = __msg_dp; \
	event_data_pit_msg_info->pit_msg_err = __msg_err; \
	mtk_memlog_event_msg_init(&event_data_pit_msg_info->event_msg, DATA_PIT_MSG_INFO); \
	mtk_memlog_req_done(__mdev, MTK_DATA_RX_H_MEMLOG_RG(rxq_id)); \
} while (0)

struct event_data_rx_pkt_info {
	struct memlog_event_msg event_msg;
	u8 rxq_id;
	u16 headlen;
	u32 data_len;
	u16 lro_pkt_cnt;
	u16  gso_segs;
	u16 ip_protocol;
	u32 id;
} __packed;

#define MTK_DBG_DATA_RX_PKT_INFO(mdev, __rxq_id, __headlen, __data_len, __lro_pkt_cnt, \
				 __gso_segs, __ip_protocol, __id) \
do { \
	struct event_data_rx_pkt_info *event_data_rx_pkt_info; \
	struct mtk_md_dev *__mdev = mdev; \
	u8 rxq_id = __rxq_id; \
	event_data_rx_pkt_info = mtk_memlog_req_address(__mdev, \
							MTK_DATA_RX_H_MEMLOG_RG(rxq_id), \
							sizeof(struct event_data_rx_pkt_info)); \
	if (!event_data_rx_pkt_info) \
		break; \
	event_data_rx_pkt_info->rxq_id = rxq_id; \
	event_data_rx_pkt_info->headlen = __headlen; \
	event_data_rx_pkt_info->data_len = __data_len; \
	event_data_rx_pkt_info->lro_pkt_cnt = __lro_pkt_cnt; \
	event_data_rx_pkt_info->gso_segs = __gso_segs; \
	event_data_rx_pkt_info->ip_protocol = __ip_protocol; \
	event_data_rx_pkt_info->id = __id; \
	mtk_memlog_event_msg_init(&event_data_rx_pkt_info->event_msg, DATA_RX_PKT_INFO); \
	mtk_memlog_req_done(__mdev, MTK_DATA_RX_H_MEMLOG_RG(rxq_id)); \
} while (0)

struct event_data_irq_src_info {
	struct memlog_event_msg event_msg;
	u8 irq_src_id;
	u32 l2statusreg;
	u32 l2statusfiltered;
	u32 l2maskreg;
} __packed;

#define MTK_DBG_DATA_IRQ_SRC_INFO(mdev, __irq_src_id, __l2statusreg, __l2statusfiltered, \
				  __l2maskreg) \
do { \
	struct event_data_irq_src_info *event_data_irq_src_info; \
	struct mtk_md_dev *__mdev = mdev; \
	u8 irq_src_id = __irq_src_id; \
	event_data_irq_src_info = mtk_memlog_req_address(__mdev, \
							 MTK_DATA_IRQ_MEMLOG_RG(irq_src_id), \
							 sizeof(struct event_data_irq_src_info)); \
	if (!event_data_irq_src_info) \
		break; \
	event_data_irq_src_info->irq_src_id = irq_src_id; \
	event_data_irq_src_info->l2statusreg = __l2statusreg; \
	event_data_irq_src_info->l2statusfiltered = __l2statusfiltered; \
	event_data_irq_src_info->l2maskreg = __l2maskreg; \
	mtk_memlog_event_msg_init(&event_data_irq_src_info->event_msg, DATA_IRQ_SRC_INFO); \
	mtk_memlog_req_done(__mdev, MTK_DATA_IRQ_MEMLOG_RG(irq_src_id)); \
} while (0)

struct event_data_irq_handler_info {
	struct memlog_event_msg event_msg;
	u8 irq_src_id;
	u8 irq_event_handler;
	/* Queue mask or Queue index */
	u32 q_mask_id;
} __packed;

#define MTK_DBG_DATA_IRQ_HANDLER_INFO(mdev, __irq_src_id, __irq_event, __q_mask_id) \
do { \
	struct event_data_irq_handler_info *event_data_irq_handler_info; \
	struct mtk_md_dev *__mdev = mdev; \
	u8 irq_src_id = __irq_src_id; \
	u8 irq_event = __irq_event; \
	u32 q_mask_id = __q_mask_id; \
	event_data_irq_handler_info = mtk_memlog_req_address(__mdev, \
							     MTK_DATA_IRQ_MEMLOG_RG(irq_src_id), \
							     sizeof( \
							     struct event_data_irq_handler_info \
							     )); \
	if (!event_data_irq_handler_info) \
		break; \
	event_data_irq_handler_info->irq_src_id = irq_src_id; \
	event_data_irq_handler_info->irq_event_handler = irq_event; \
	event_data_irq_handler_info->q_mask_id = q_mask_id; \
	mtk_memlog_event_msg_init(&event_data_irq_handler_info->event_msg, \
				  DATA_IRQ_HANDLER_INFO); \
	mtk_memlog_req_done(__mdev, MTK_DATA_IRQ_MEMLOG_RG(irq_src_id)); \
} while (0)

#else
#define MTK_DBG_PM_DS_LOCK(mdev, user_id, ds_lock_sent, reg_value) \
	MTK_DBG(mdev, MTK_DBG_PM, MTK_MEMLOG_RG_PM_H_FREQ, \
		"user=%d, ds_lock_sent=%llu, ds_state=0x%08x\n", \
		user_id, ds_lock_sent, reg_value)

#define MTK_DBG_PM_RT_IDLE(mdev, seconds) \
	MTK_DBG(mdev, MTK_DBG_PM, MTK_MEMLOG_RG_PM_H_FREQ, \
		"Schedule Runtime Suspend after %d seconds\n", \
		seconds)

#define MTK_DBG_CTRL_TX_DONE(mdev, id, qno, wr, free, count, budget, flag, rg_offset) \
	MTK_DBG(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_H_FREQ + (rg_offset), \
		"cldma_id:0x%x txq_no:0x%x wr_idx:0x%x free_idx:0x%x " \
		"cnt:0x%x budget:0x%x flag:0x%x\n", \
		id, qno, wr, free, count, budget, flag)

#define MTK_DBG_CTRL_RX_DONE(mdev, id, qno, free, count, rg_offset) \
	MTK_DBG(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_H_FREQ + (rg_offset), \
		"cldma_id:0x%x rxq_no:0x%x free_idx:0x%x cnt:0x%x\n", \
		id, qno, free, count)

#define MTK_DBG_DATA_TX_PKT_INFO(mdev, txq_id, payload_cnt, msg_idx, \
				 id, msg_adds0, msg_adds1) \
	MTK_DBG(mdev, MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_TX_H_FREQ, \
		"txq%u, drb msg(%u) payload_cnt(%u): 0x%x, 0x%x, 0x%x\n", \
		txq_id, msg_idx, payload_cnt, id, msg_adds0, msg_adds1)

#define MTK_DBG_DATA_RX_MSG_INFO(mdev, rxq_id, chnl_id, checksum, msg_dp, msg_err) \
do { \
	u8 __rxq_id = rxq_id; \
	MTK_DBG(mdev, MTK_DBG_DPMF, MTK_DATA_RX_H_MEMLOG_RG(__rxq_id), \
		"rxq%u msg pit: ch=%u, cs=%u, dp=%u, err=%u\n", \
		__rxq_id, chnl_id, checksum, msg_dp, msg_err); \
} while (0)

#define MTK_DBG_DATA_RX_PKT_INFO(mdev, rxq_id, headlen, data_len, lro_pkt_cnt, \
				 gso_segs, ip_protocol, id) \
	MTK_DBG(mdev, MTK_DBG_DPMF, MTK_DATA_RX_H_MEMLOG_RG(rxq_id), \
		"full lro skb:%u+%u, lro_pkt_cnt=%u/%u. proto=%hu, ipid=0x%x\n", \
		headlen, data_len, lro_pkt_cnt, gso_segs, ip_protocol, id)

#define MTK_DBG_DATA_IRQ_SRC_INFO(mdev, __irq_src_id, __l2statusreg, \
				  __l2statusfiltered, __l2maskreg) \
do { \
	u8 irq_src_id = __irq_src_id; \
	MTK_DBG(mdev, MTK_DBG_DATA_IRQ, MTK_DATA_IRQ_MEMLOG_RG(irq_src_id), \
		"src:0x%x l2statusreg:0x%08x l2statusfiltered:0x%08x l2maskreg:0x%08x\n", \
		irq_src_id, __l2statusreg, __l2statusfiltered, __l2maskreg); \
} while (0)

#define MTK_DBG_DATA_IRQ_HANDLER_INFO(mdev, __irq_src_id, __irq_event, __q_mask_id) \
do { \
	u8 irq_src_id = __irq_src_id; \
	MTK_DBG(mdev, MTK_DBG_DATA_IRQ, MTK_DATA_IRQ_MEMLOG_RG(irq_src_id), \
		"irq_src_id:0x%x irq_event_handler:0x%x q_mask_id:0x%x\n", \
		irq_src_id, __irq_event, __q_mask_id); \
} while (0)

#endif

#endif
