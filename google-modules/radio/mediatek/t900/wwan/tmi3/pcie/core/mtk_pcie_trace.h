/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2023, MediaTek Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mtk_pcie_trace

#if !defined(__MTK_PCIE_TRACE_EVENTS_H) || defined(TRACE_HEADER_MULTI_READ)
#define __MTK_PCIE_TRACE_EVENTS_H

#include <linux/device.h>
#include <linux/printk.h>
#include <linux/skbuff.h>
#include <linux/tracepoint.h>
#include <linux/trace_events.h>

TRACE_EVENT(mtk_irq_entry,

	TP_PROTO(int irq_id),

	TP_ARGS(irq_id),

	TP_STRUCT__entry(__field(int, irq_id)),

	TP_fast_assign(__entry->irq_id = irq_id;),

	TP_printk("%d", __entry->irq_id)
);

TRACE_EVENT(mtk_irq_exit,

	TP_PROTO(int type, int q_id),

	TP_ARGS(type, q_id),

	TP_STRUCT__entry(
		__field(int, type)
		__field(int, q_id)
	),

	TP_fast_assign(
		__entry->type = type;
		__entry->q_id = q_id;
	),

	TP_printk("%d, %d", __entry->type, __entry->q_id)
);

/* data path latency tuning */

TRACE_EVENT(mtk_tput_data_tx,

	TP_PROTO(int q_id, void *str, u32 id),

	TP_ARGS(q_id, str, id),

	TP_STRUCT__entry(
		__field(int, q_id)
		__string(str, str)
		__field(u32, id)
	),

	TP_fast_assign(
		__entry->q_id = q_id;
		__assign_str(str);
		__entry->id = id;
	),

	TP_printk("%d, %s, %u", __entry->q_id, __get_str(str), __entry->id)
);

TRACE_EVENT(mtk_tras_data_tx,

	TP_PROTO(unsigned long long base_ts, unsigned long long now, unsigned long long next_ts),

	TP_ARGS(base_ts, now, next_ts),

	TP_STRUCT__entry(
		__field(unsigned long long, base_ts)
		__field(unsigned long long, now)
		__field(unsigned long long, next_ts)
	),

	TP_fast_assign(
		__entry->base_ts = base_ts;
		__entry->now = now;
		__entry->next_ts = next_ts;

	),

	TP_printk("%llu, %llu, %llu", __entry->base_ts, __entry->now, __entry->next_ts)
);

TRACE_EVENT(mtk_tras_irq_data,

	TP_PROTO(unsigned int cfg, unsigned int frc),

	TP_ARGS(cfg, frc),

	TP_STRUCT__entry(
		__field(unsigned int, cfg)
		__field(unsigned int, frc)
	),

	TP_fast_assign(
		__entry->cfg = cfg;
		__entry->frc = frc;

	),

	TP_printk("0x%x, %u", __entry->cfg, __entry->frc)
);

TRACE_EVENT(mtk_frc,

	TP_PROTO(unsigned long long host_ts, unsigned long long host_local_ts,
		 unsigned int raw_md_frc, unsigned int curr_md_frc, unsigned int l0_count),

	TP_ARGS(host_ts, host_local_ts, raw_md_frc, curr_md_frc, l0_count),

	TP_STRUCT__entry(
		__field(unsigned long long, host_ts)
		__field(unsigned long long, host_local_ts)
		__field(unsigned int, raw_md_frc)
		__field(unsigned int, curr_md_frc)
		__field(unsigned int, l0_count)
	),

	TP_fast_assign(
		__entry->host_ts = host_ts;
		__entry->host_local_ts = host_local_ts;
		__entry->raw_md_frc = raw_md_frc;
		__entry->curr_md_frc = curr_md_frc;
		__entry->l0_count = l0_count;
	),

	TP_printk("%llu, %llu, %u, %u, %u", __entry->host_ts, __entry->host_local_ts,
		  __entry->raw_md_frc, __entry->curr_md_frc, __entry->l0_count)
);

TRACE_EVENT(mtk_tput_data_rx,

	TP_PROTO(int q_id, void *str),

	TP_ARGS(q_id, str),

	TP_STRUCT__entry(
		__field(int, q_id)
		__string(str, str)
	),

	TP_fast_assign(
		__entry->q_id = q_id;
		__assign_str(str);
	),

	TP_printk("%d, %s", __entry->q_id, __get_str(str))
);

TRACE_EVENT(mtk_tput_data_drb_rel,

	TP_PROTO(int q_id, int to_rel, int budget, int wr_idx, int rd_idx, int rel_idx),

	TP_ARGS(q_id, to_rel, budget, wr_idx, rd_idx, rel_idx),

	TP_STRUCT__entry(
		__field(int, q_id)
		__field(int, to_rel)
		__field(int, budget)
		__field(int, wr_idx)
		__field(int, rd_idx)
		__field(int, rel_idx)
	),

	TP_fast_assign(
		__entry->q_id = q_id;
		__entry->to_rel = to_rel;
		__entry->budget = budget;
		__entry->wr_idx = wr_idx;
		__entry->rd_idx = rd_idx;
		__entry->rel_idx = rel_idx;
	),

	TP_printk("%d, %d, %d, %d, %d, %d", __entry->q_id, __entry->to_rel,
		  __entry->budget, __entry->wr_idx, __entry->rd_idx, __entry->rel_idx)
);

TRACE_EVENT(mtk_tput_data_drb_fill,

	TP_PROTO(int srv_id, int q_id, int pkt_filled, int pkt_to_fill, int available_drb,
		 u32 id),

	TP_ARGS(srv_id, q_id, pkt_filled, pkt_to_fill, available_drb, id),

	TP_STRUCT__entry(
		__field(int, srv_id)
		__field(int, q_id)
		__field(int, pkt_filled)
		__field(int, pkt_to_fill)
		__field(int, available_drb)
		__field(u32, id)
	),

	TP_fast_assign(
		__entry->srv_id = srv_id;
		__entry->q_id = q_id;
		__entry->pkt_filled = pkt_filled;
		__entry->pkt_to_fill = pkt_to_fill;
		__entry->available_drb = available_drb;
		__entry->id = id;
	),

	TP_printk("%d, %d, %d, %d, %d, %u",
		  __entry->srv_id, __entry->q_id, __entry->pkt_filled, __entry->pkt_to_fill,
		  __entry->available_drb, __entry->id)
);

TRACE_EVENT(mtk_tput_data_napi,

	TP_PROTO(int q_id, int napi_sta, int rx_pkt),

	TP_ARGS(q_id, napi_sta, rx_pkt),

	TP_STRUCT__entry(
		__field(int, q_id)
		__field(int, napi_sta)
		__field(int, rx_pkt)
	),

	TP_fast_assign(
		__entry->q_id = q_id;
		__entry->napi_sta = napi_sta;
		__entry->rx_pkt = rx_pkt;
	),

	TP_printk("%d, %d, %d", __entry->q_id, __entry->napi_sta, __entry->rx_pkt)
);

TRACE_EVENT(mtk_tput_data_napi_per_poll,

	TP_PROTO(int q_id, int pit_cnt),

	TP_ARGS(q_id, pit_cnt),

	TP_STRUCT__entry(
		__field(int, q_id)
		__field(int, pit_cnt)
	),

	TP_fast_assign(
		__entry->q_id = q_id;
		__entry->pit_cnt = pit_cnt;
	),

	TP_printk("%d, %d", __entry->q_id, __entry->pit_cnt)
);

TRACE_EVENT(mtk_tput_data_bat,

	TP_PROTO(int bat_id, int type, int db_cnt, int rel_cnt, int wr_idx, int rd_idx),

	TP_ARGS(bat_id, type, db_cnt, rel_cnt, wr_idx, rd_idx),

	TP_STRUCT__entry(
		__field(int, bat_id)
		__field(int, type)
		__field(int, db_cnt)
		__field(int, rel_cnt)
		__field(int, wr_idx)
		__field(int, rd_idx)
	),

	TP_fast_assign(
		__entry->bat_id = bat_id;
		__entry->type = type;
		__entry->db_cnt = db_cnt;
		__entry->rel_cnt = rel_cnt;
		__entry->wr_idx = wr_idx;
		__entry->rd_idx = rd_idx;
	),

	TP_printk("%d, %d, %d, %d, %d, %d",
		  __entry->bat_id, __entry->type, __entry->db_cnt,
		  __entry->rel_cnt, __entry->wr_idx, __entry->rd_idx)
);

TRACE_EVENT(mtk_tput_data_bat_db,

	TP_PROTO(int bat_id, int type, int wr_idx, int rd_idx, int doorbell_cnt),

	TP_ARGS(bat_id, type, wr_idx, rd_idx, doorbell_cnt),

	TP_STRUCT__entry(
		__field(int, bat_id)
		__field(int, type)
		__field(int, wr_idx)
		__field(int, rd_idx)
		__field(int, doorbell_cnt)
	),

	TP_fast_assign(
		__entry->bat_id = bat_id;
		__entry->type = type;
		__entry->wr_idx = wr_idx;
		__entry->rd_idx = rd_idx;
		__entry->doorbell_cnt = doorbell_cnt;
	),

	TP_printk("%d, %d, %d, %d, %d",
		  __entry->bat_id, __entry->type, __entry->wr_idx,
		  __entry->rd_idx, __entry->doorbell_cnt)
);

TRACE_EVENT(mtk_tput_data_preload_bat,

	TP_PROTO(int bat_id, int type, int act_cnt, int to_preload_cnt, int wr_idx, int rd_idx),

	TP_ARGS(bat_id, type, act_cnt, to_preload_cnt, wr_idx, rd_idx),

	TP_STRUCT__entry(
		__field(int, bat_id)
		__field(int, type)
		__field(int, act_cnt)
		__field(int, to_preload_cnt)
		__field(int, wr_idx)
		__field(int, rd_idx)
	),

	TP_fast_assign(
		__entry->bat_id = bat_id;
		__entry->type = type;
		__entry->act_cnt = act_cnt;
		__entry->to_preload_cnt = to_preload_cnt;
		__entry->wr_idx = wr_idx;
		__entry->rd_idx = rd_idx;
	),

	TP_printk("%d, %d, %d, %d, %d, %d",
		  __entry->bat_id, __entry->type, __entry->act_cnt, __entry->to_preload_cnt,
		  __entry->wr_idx, __entry->rd_idx)
);

TRACE_EVENT(mtk_tput_data_bat_alloc,

	TP_PROTO(int bat_id, int type, int alloc_cnt),

	TP_ARGS(bat_id, type, alloc_cnt),

	TP_STRUCT__entry(
		__field(int, bat_id)
		__field(int, type)
		__field(int, alloc_cnt)
	),

	TP_fast_assign(
		__entry->bat_id = bat_id;
		__entry->type = type;
		__entry->alloc_cnt = alloc_cnt;
	),

	TP_printk("%d, %d, %d",
		  __entry->bat_id, __entry->type, __entry->alloc_cnt)
);

TRACE_EVENT(mtk_tput_data_pit,

	TP_PROTO(int id, int pit_rel_rd_idx, int pit_wr_idx, int pit_rd_idx, int pit_rel_cnt),

	TP_ARGS(id, pit_rel_rd_idx, pit_wr_idx, pit_rd_idx, pit_rel_cnt),

	TP_STRUCT__entry(
		__field(int, id)
		__field(int, pit_rel_rd_idx)
		__field(int, pit_wr_idx)
		__field(int, pit_rd_idx)
		__field(int, pit_rel_cnt)
	),

	TP_fast_assign(
		__entry->id = id;
		__entry->pit_rel_rd_idx = pit_rel_rd_idx;
		__entry->pit_wr_idx = pit_wr_idx;
		__entry->pit_rd_idx = pit_rd_idx;
		__entry->pit_rel_cnt = pit_rel_cnt;
	),

	TP_printk("%d, %d, %d, %d, %d",
		  __entry->id, __entry->pit_rel_rd_idx, __entry->pit_wr_idx,
		  __entry->pit_rd_idx, __entry->pit_rel_cnt)
);

TRACE_EVENT(mtk_data_pit_burst_cnt,

	TP_PROTO(unsigned int id, unsigned int cnt),

	TP_ARGS(id, cnt),

	TP_STRUCT__entry(
		__field(unsigned int, id)
		__field(unsigned int, cnt)
	),

	TP_fast_assign(
		__entry->id = id;
		__entry->cnt = cnt;
	),

	TP_printk("%u, %u", __entry->id, __entry->cnt)
);

TRACE_EVENT(mtk_data_doorbell,

	TP_PROTO(unsigned int type, unsigned int id, unsigned int cnt),

	TP_ARGS(type, id, cnt),

	TP_STRUCT__entry(
		__field(unsigned int, type)
		__field(unsigned int, id)
		__field(unsigned int, cnt)),

	TP_fast_assign(
		__entry->type = type;
		__entry->id = id;
		__entry->cnt = cnt;),

	TP_printk("%u, %u, %u", __entry->type, __entry->id, __entry->cnt)
);

TRACE_EVENT(mtk_tput_data_lro_cnt,

	TP_PROTO(int rx_qid, unsigned int lro_pkt_cnt, int gso_segs, u32 id),

	TP_ARGS(rx_qid, lro_pkt_cnt, gso_segs, id),

	TP_STRUCT__entry(
		__field(int, rx_qid)
		__field(unsigned int, lro_pkt_cnt)
		__field(int, gso_segs)
		__field(u32, id)
	),

	TP_fast_assign(
		__entry->rx_qid = rx_qid;
		__entry->lro_pkt_cnt = lro_pkt_cnt;
		__entry->gso_segs = gso_segs;
		__entry->id = id;
	),

	TP_printk("%d, %u, %d, %u", __entry->rx_qid, __entry->lro_pkt_cnt,  __entry->gso_segs,
		  __entry->id)
);

TRACE_EVENT(mtk_data_event_stats,

	TP_PROTO(int stats_type, int qid, int cnt),

	TP_ARGS(stats_type, qid, cnt),

	TP_STRUCT__entry(
		__field(int, stats_type)
		__field(int, qid)
		__field(int, cnt)
	),

	TP_fast_assign(
		__entry->stats_type = stats_type;
		__entry->qid = qid;
		__entry->cnt = cnt;
	),

	TP_printk("%d, %d, %d", __entry->stats_type,
		  __entry->qid, __entry->cnt)
);

TRACE_EVENT(mtk_cldma_submit_tx,

	TP_PROTO(int hw_id, int txqno, int wr_idx, int req_budget),

	TP_ARGS(hw_id, txqno, wr_idx, req_budget),

	TP_STRUCT__entry(
		__field(int, hw_id)
		__field(int, txqno)
		__field(int, wr_idx)
		__field(int, req_budget)
	),

	TP_fast_assign(
		__entry->hw_id = hw_id;
		__entry->txqno = txqno;
		__entry->wr_idx = wr_idx;
		__entry->req_budget = req_budget;
	),

	TP_printk("%d, %d, %d, %d",
		  __entry->hw_id, __entry->txqno,
		  __entry->wr_idx, __entry->req_budget)
);

TRACE_EVENT(mtk_cldma_start_xfer,

	TP_PROTO(int hw_id, int txqno),

	TP_ARGS(hw_id, txqno),

	TP_STRUCT__entry(
		__field(int, hw_id)
		__field(int, txqno)
	),

	TP_fast_assign(
		__entry->hw_id = hw_id;
		__entry->txqno = txqno;
	),

	TP_printk("%d, %d",
		  __entry->hw_id, __entry->txqno)
);

TRACE_EVENT(mtk_ctrl_tx_isr,

	TP_PROTO(int hw_id, int txqno),

	TP_ARGS(hw_id, txqno),

	TP_STRUCT__entry(
		__field(int, hw_id)
		__field(int, txqno)
	),

	TP_fast_assign(
		__entry->hw_id = hw_id;
		__entry->txqno = txqno;
	),

	TP_printk("%d, %d",
		  __entry->hw_id, __entry->txqno)
);

TRACE_EVENT(mtk_ctrl_rx_isr,

	TP_PROTO(int hw_id, int rxqno),

	TP_ARGS(hw_id, rxqno),

	TP_STRUCT__entry(
		__field(int, hw_id)
		__field(int, rxqno)
	),

	TP_fast_assign(
		__entry->hw_id = hw_id;
		__entry->rxqno = rxqno;
	),

	TP_printk("%d, %d",
		  __entry->hw_id, __entry->rxqno)
);

TRACE_EVENT(mtk_ctrl_tx_done,

	TP_PROTO(int hw_id, int txqno, void *skb_ptr,
		 void *skb_data, int polling_idx),

	TP_ARGS(hw_id, txqno, skb_ptr, skb_data, polling_idx),

	TP_STRUCT__entry(
		__field(int, hw_id)
		__field(int, txqno)
		__field(void *, skb_ptr)
		__field(void *, skb_data)
		__field(int, polling_idx)
	),

	TP_fast_assign(
		__entry->hw_id = hw_id;
		__entry->txqno = txqno;
		__entry->skb_ptr = skb_ptr;
		__entry->skb_data = skb_data;
		__entry->polling_idx = polling_idx;
	),

	TP_printk("%d, %d, %p, %p, %d",
		  __entry->hw_id, __entry->txqno,
		  __entry->skb_ptr, __entry->skb_data,
		  __entry->polling_idx)
);

TRACE_EVENT(mtk_ctrl_rx_done,

	TP_PROTO(int hw_id, int rxqno, void *skb_ptr,
		 void *skb_data, int polling_idx),

	TP_ARGS(hw_id, rxqno, skb_ptr, skb_data, polling_idx),

	TP_STRUCT__entry(
		__field(int, hw_id)
		__field(int, rxqno)
		__field(void *, skb_ptr)
		__field(void *, skb_data)
		__field(int, polling_idx)
	),

	TP_fast_assign(
		__entry->hw_id = hw_id;
		__entry->rxqno = rxqno;
		__entry->skb_ptr = skb_ptr;
		__entry->skb_data = skb_data;
		__entry->polling_idx = polling_idx;
	),

	TP_printk("%d, %d, %p, %p, %d",
		  __entry->hw_id, __entry->rxqno,
		  __entry->skb_ptr, __entry->skb_data,
		  __entry->polling_idx)
);

TRACE_EVENT(mtk_pm_ds_lock_start,

	TP_PROTO(int user),

	TP_ARGS(user),

	TP_STRUCT__entry(
		__field(int, user)
	),

	TP_fast_assign(
		__entry->user = user;
	),

	TP_printk("user=%d", __entry->user)
);

TRACE_EVENT(mtk_pm_ds_lock_end,

	TP_PROTO(int user),

	TP_ARGS(user),

	TP_STRUCT__entry(
		__field(int, user)
	),

	TP_fast_assign(
		__entry->user = user;
	),

	TP_printk("user=%d", __entry->user)
);

TRACE_EVENT(mtk_pm_ds_status,

	TP_PROTO(int user, u32 status),

	TP_ARGS(user, status),

	TP_STRUCT__entry(
		__field(int, user)
		__field(u32, status)
	),

	TP_fast_assign(
		__entry->user = user;
		__entry->status = status;
	),

	TP_printk("user=%d, status=0x%x", __entry->user, __entry->status)
);

TRACE_EVENT(mtk_pm_raise_wakeup_irq_to_md,

	TP_PROTO(u64 ds_lock_sent),

	TP_ARGS(ds_lock_sent),

	TP_STRUCT__entry(
		__field(u64, ds_lock_sent)
	),

	TP_fast_assign(
		__entry->ds_lock_sent = ds_lock_sent;
	),

	TP_printk("%llu", __entry->ds_lock_sent)
);

TRACE_EVENT(mtk_pm_receive_wakeup_irq_from_md,

	TP_PROTO(int irq_type),

	TP_ARGS(irq_type),

	TP_STRUCT__entry(
		__field(int, irq_type)
	),

	TP_fast_assign(
		__entry->irq_type = irq_type
	),

	TP_printk("irq_type=%d", __entry->irq_type)
);

TRACE_EVENT(mtk_pm_completion_sync_end,

	TP_PROTO(int user, int remain_jiffies),

	TP_ARGS(user, remain_jiffies),

	TP_STRUCT__entry(
		__field(int, user)
		__field(int, remain_jiffies)
	),

	TP_fast_assign(
		__entry->user = user;
		__entry->remain_jiffies = remain_jiffies;
	),

	TP_printk("user=%d, ret=%d", __entry->user, __entry->remain_jiffies)
);
#endif /* __MTK_TRACE_EVENTS_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE mtk_pcie_trace

#include <trace/define_trace.h>

