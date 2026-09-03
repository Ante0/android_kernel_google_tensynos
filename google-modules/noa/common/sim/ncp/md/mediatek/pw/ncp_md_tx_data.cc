/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD (MTK) Driver
 *
 * Copyright 2024 Google LLC.
 */

#include "modem_fw_ring.h"
#include "modem_interface.h"
#include "ncp_md.h"
#include "ncp_md_irq.h"
#include "ncp_md_rx_data.h"
#include "ncp_md_tx_data.h"
#include "ncp_modem_data.h"
#include "noa_md_apc2ncp_ring.h"
#include "sys_common.h"

#ifdef linux
#else
#include "ring_event_client/ring_event_client.h"
#endif

SEC_EXRAM_DATA static noa_buffer_pool_desc g_pool_ring_buf[NOA_MD_FW_FIFO_SIZE];

/* NCP stops to delay doorbell when detects TCP slow start,
 * and restarts the doorbell delay after the specified doorbell count expires.
 */
static unsigned int doorbell_reset_count = 500;

/* the DRB speed threshold for enable polling reister at tx done interrupt bottom-half */
static unsigned int tx_poll_th = 200;

inline u32 noa_ncp_md_tx_peek_next_index(u32 cur_idx, u32 max_cnt)
{
	cur_idx++;
	return (cur_idx == max_cnt) ? 0 : cur_idx;
}

// Refer to mtk_dpmaif_ring_buf_releasable
inline unsigned int noa_ncp_md_tx_ring_releasable_cnt(
	unsigned int total_cnt,
	unsigned int rel_idx,
	unsigned int rd_idx)
{
	unsigned int pkt_cnt;

	if (rel_idx <= rd_idx)
		pkt_cnt = rd_idx - rel_idx;
	else
		pkt_cnt = total_cnt + rd_idx - rel_idx;

	return pkt_cnt;
}

// Refer to mtk_dpmaif_ring_buf_get_next_idx
inline unsigned int noa_ncp_md_tx_ring_next_idx(
		unsigned int buf_len, unsigned int buf_idx)
{
	return (++buf_idx) % buf_len;
};

/* Helper Functions */
static inline unsigned int noa_ncp_md_tx_available_desc(
	unsigned int total_cnt,
	unsigned int read_idx,
	unsigned write_idx)
{
	unsigned int available_cnt;

	if (write_idx < read_idx)
		available_cnt = read_idx - write_idx + 1;
	else
		available_cnt = total_cnt + read_idx - write_idx - 1;

	return available_cnt;
}

// Refer to MSG_DRB in mtk_dpmaif_fill_tx_info of T900
static inline void noa_ncp_md_tx_set_msg_drb(
	struct noa_tx_queue *txq,
	unsigned short cur_idx,
	struct noa_modem_tx_desc *tx_desc)
{
	struct MtkMessageDescriptorRingBuffer* msg_drb =
		(struct MtkMessageDescriptorRingBuffer *)txq->drb_base + cur_idx;

	// for VPN/Tethering
	msg_drb->descriptor_type = MSG_DRB;
	msg_drb->continue_bit = DPMAIF_DRB_MORE;
	msg_drb->packet_length = tx_desc->basic.dl;
	msg_drb->count_l_psn = 0;  // Disable msg_count feature.
	msg_drb->channel_id = tx_desc->ext.pkt_info.intf_id;
	msg_drb->network_type = tx_desc->ext.pkt_info.network_type;
	msg_drb->ipv4 = 0;  // Disable IPv4 UL checksum offload.
	msg_drb->l4_checksum = 1;  // Enable TCP checksum offload.
}

static inline void noa_ncp_md_tx_set_pd_drb(
	struct noa_tx_queue *txq,
	unsigned short cur_idx,
	struct noa_modem_tx_desc *tx_desc,
	char last_one)
{
	struct MtkPayloadDescriptorRingBuffer* pd_drb =
		(struct MtkPayloadDescriptorRingBuffer *)txq->drb_base + cur_idx;

	// for VPN/Tethering
	pd_drb->descriptor_type = PD_DRB;
	if (last_one) {
		pd_drb->continue_bit = DPMAIF_DRB_LASTONE;
	} else {
		pd_drb->continue_bit = DPMAIF_DRB_MORE;
	}
	pd_drb->data_length = tx_desc->basic.dl;
	pd_drb->address_low = tx_desc->basic.dp_low;
	pd_drb->address_high = tx_desc->basic.dp_high;
}

// Refer to mtk_dpmaif_tx_rel_internal
static int noa_ncp_md_tx_rel_internal(
		struct noa_tx_queue *txq, unsigned int rel_cnt,
		unsigned int *real_rel_cnt)
{
	struct MtkPayloadDescriptorRingBuffer *cur_drb = NULL,
		*drb_base = (struct MtkPayloadDescriptorRingBuffer *)txq->drb_base;
	unsigned short cur_idx;
	unsigned int i;
	unsigned int vpn_tether_count = 0, read_cnt = 0;

	cur_idx = txq->drb_rel_rd_idx;
	NCP_MD_TX_INFO(
		"drb_rel_rd_idx:%u, rel_cnt:%u", txq->drb_rel_rd_idx, rel_cnt);
	for (i = 0 ; i < rel_cnt; i++) {
		cur_drb = drb_base + cur_idx;
		if (cur_drb->descriptor_type == PD_DRB) {
			NCP_MD_TX_INFO("handle pd_drb");
			/* The last one drb entry of one tx packet, so,
			   skb will be released. */
			if (cur_drb->continue_bit == DPMAIF_DRB_LASTONE) {
				NCP_MD_TX_INFO("handle lastone");
			}
		} else {
			NCP_MD_TX_INFO("handle msg_drb");
			// recycle the related Tx buffer in Tx buffer pool
			if (txq->tkid_queue[cur_idx] >= NOA_MD_FW_TX_POOL_TKID_OFFSET) {
				noa_ncp_md_tx_pool_replenish(g_md_fw, txq->tkid_queue[cur_idx]);
				txq->tkid_queue[cur_idx] = 0;
				vpn_tether_count++;
			}
		}
		cur_idx =
			noa_ncp_md_tx_ring_next_idx(txq->drb_cnt, cur_idx);
		txq->drb_rel_rd_idx = cur_idx;
		NCP_MD_TX_INFO("txq%u pkt(%u),w=%u,r=%u,rel=%u,cnt=%u",
			txq->id, cur_idx, txq->drb_wr_idx,
			txq->drb_rd_idx, txq->drb_rel_rd_idx,
			rel_cnt);
	}
	*real_rel_cnt = i;
	NCP_MD_TX_INFO("real_rel_cnt=[%u], vpn_tether_count=[%u]", *real_rel_cnt, vpn_tether_count);
	// Minus VPN/Tethering which only support single skb(msg+pd)
	read_cnt = *real_rel_cnt - vpn_tether_count * 2;
	if (read_cnt > 0) {
		// Update the read index to shared info
		noa_ncp_md_apc2ncp_set_ring_read_idx(txq->id, read_cnt);
	}
	return 0;
}

// Refer to mtk_dpmaif_tx_rel
static int noa_ncp_md_tx_rel(struct noa_tx_queue *txq)
{
	unsigned int real_rel_cnt = 0;
	int ret = 0, rel_cnt;

	rel_cnt = noa_ncp_md_tx_ring_releasable_cnt(
		txq->drb_cnt, txq->drb_rel_rd_idx, txq->drb_rd_idx);
	NCP_MD_TX_INFO("rel_cnt: %u", rel_cnt);
	NCP_MD_TX_INFO("txq%u drb: w=%u,r=%u,rel=%u, rel_cnt=%u",
		txq->id, txq->drb_wr_idx, txq->drb_rd_idx,
		txq->drb_rel_rd_idx, rel_cnt);
	if (likely(rel_cnt > 0)) {
		/* Release tx data buffer. */
		ret = noa_ncp_md_tx_rel_internal(
			txq, rel_cnt, &real_rel_cnt);
	}
	return ret;
}

void noa_ncp_md_tx_drb_rel_ctrl(struct noa_tx_queue *txq, unsigned int drb_speed)
{
	if (drb_speed > tx_poll_th)
		txq->drb_poll_enable = true;
	else
		txq->drb_poll_enable = false;
}

static unsigned int noa_ncp_md_tx_poll_tx_drb(struct noa_tx_queue *txq)
{
	unsigned short old_sw_rd_idx, new_hw_rd_idx;
	unsigned int drb_cnt;
	int ret;
	old_sw_rd_idx = txq->drb_rd_idx;
	ret = NOA_MD_GET_RING_INDEX(NOA_MD_DPMAIF_DRB_RIDX, txq->id);
	if (unlikely(ret < 0)) {
		NCP_MD_TX_ERROR("Failed to read txq%u drb_rd_idx, ret=%d\n", txq->id, ret);
		return 0;
	}
	new_hw_rd_idx = ret;
	if (old_sw_rd_idx <= new_hw_rd_idx)
		drb_cnt = new_hw_rd_idx - old_sw_rd_idx;
	else
		drb_cnt = txq->drb_cnt - old_sw_rd_idx + new_hw_rd_idx;
	txq->drb_rd_idx = new_hw_rd_idx;
	return drb_cnt;
}

// Refer to mtk_dpmaif_tx_done
#ifdef linux
void noa_ncp_md_tx_done_work(struct work_struct *work)
#else
void noa_ncp_md_tx_done_work(void* data)
#endif
{
	struct noa_tx_queue *txq;
	struct noa_md_fw_tx *tx = g_md_fw->tx;

	NCP_MD_TX_INFO("enter");
#ifdef linux
	struct delayed_work *dwork = to_delayed_work(work);
	txq = container_of(dwork, struct noa_tx_queue, tx_done_work);
#else
	txq = (struct noa_tx_queue *)data;
#endif

	/* Recycle drb and release hardware tx done buffer around drb. */
	noa_ncp_md_tx_rel(txq);

	struct ncp_md_irq_simulator *sim = ncp_md_irq_sim_get();
	if (txq->drb_poll_mode) {
		NCP_MD_TX_INFO("Trigger doorbell to apc in polling mode");
		sim->dpmaif_q_mask = (1 << txq->id);
		ncp_md_irq_set_apc_intr_mask(kNoaModemRingTxDrb0);
		ncp_md_irq_notify_apc();
	} else {
	        // Need to handle all tx done queues before notifying apc
	        tx->remain_queue_mask &= ~(1 << txq->id);
	        NCP_MD_TX_INFO("remain_queue_mask: %u", tx->remain_queue_mask);
	        if (!tx->remain_queue_mask) {
		        NCP_MD_TX_INFO("Trigger doorbell to apc");
			sim->dpmaif_q_mask = tx->isr_queue_mask;
		        ncp_md_irq_set_apc_intr_mask(kNoaModemRingTxDrb0);
		        ncp_md_irq_notify_apc();
	        }
	}

	// TODO: Refer to DPMAIF_INTR_UL_DONE section in
	// mtk_dpmaif_drv_intr_complete_com,
	/* try best to recycle drb */
	if (txq->drb_poll_enable && noa_ncp_md_tx_poll_tx_drb(txq) > 0) {
		NOA_MD_INTERRUPT_COMPLETE(NOA_MD_DPMAIF_INTR_UL_DONE, txq->id, DPMAIF_CLEAR_INTR);
		txq->drb_poll_mode = true;
		schedule_delayed_work(&txq->tx_done_work, msecs_to_jiffies(0));
	} else {
		txq->drb_poll_mode = false;
		NOA_MD_INTERRUPT_COMPLETE(NOA_MD_DPMAIF_INTR_UL_DONE, txq->id, DPMAIF_UNMASK_INTR);
	}

	NCP_MD_TX_INFO("exit");
}

// Refer to mtk_dpmaif_irq_tx_done
void noa_ncp_md_tx_irq_tx_done(
		struct noa_md_fw *md_fw, unsigned int q_mask)
{
	NCP_MD_TX_INFO("enter, md_fw=%p, q_mask=%d", md_fw, q_mask);

	struct noa_md_fw_tx *tx = md_fw->tx;

	if (!tx) {
		NCP_MD_TX_ERROR("tx is null");
		return;
	}

	// Pass the q_mask to apc
	tx->isr_queue_mask = q_mask;
	// Use to check if all tx done queues have been handled
	tx->remain_queue_mask = q_mask;
	for (int i = 0; i < tx->txq_cnt; i++) {
		unsigned int ulq_done = q_mask & (1 << i);
		if (ulq_done) {
			struct noa_tx_queue *txq = NULL;
			int drb_rd_idx;
			NCP_MD_TX_INFO("ulq_done for queue_id=%u", i);
			drb_rd_idx = NOA_MD_GET_RING_INDEX(NOA_MD_DPMAIF_DRB_RIDX, i);
			if (unlikely(drb_rd_idx < 0)) {
				NCP_MD_TX_ERROR(
					"Failed to read txq%u drb_rd_idx, ret=%d", i, drb_rd_idx);
				break;
			}

			txq = &tx->txqs[i];
			if (!txq) {
				NCP_MD_TX_ERROR("txq is null");
				continue;
			}
			txq->drb_rd_idx = drb_rd_idx;
			NCP_MD_TX_INFO("drb_rd_idx:%u", txq->drb_rd_idx);
			schedule_delayed_work(&txq->tx_done_work, msecs_to_jiffies(0));
		}
	}
	NCP_MD_TX_INFO("exit");
}

// Refer to mtk_dpmaif_tx_doorbell
#ifdef linux
void noa_ncp_md_tx_doorbell_work(struct work_struct *work)
#else
void noa_ncp_md_tx_doorbell_work(void* data)
#endif
{
	NCP_MD_TX_INFO("enter");

	int ret = 0;
	unsigned int to_submit_cnt;
	struct noa_tx_queue *txq;
#ifdef linux
	struct delayed_work *dwork = to_delayed_work(work);
	txq = container_of(dwork, struct noa_tx_queue, doorbell_work);
#else
	txq = (struct noa_tx_queue *)data;
#endif
	to_submit_cnt = ATOMIC_READ(&txq->to_submit_cnt);
	if (to_submit_cnt > 0) {
		NCP_MD_TX_INFO("to_submit_cnt: %u, id:%u", to_submit_cnt, txq->id);
		ret = NOA_MD_SEND_DOORBELL(NOA_MD_DPMAIF_DRB, txq->id, to_submit_cnt);
		if (unlikely(ret < 0)) {
			// TODO(b/432392753): Notify APC to execute mtk_dpmaif_common_err_handle
			NCP_MD_TX_ERROR("Failed to send txq%d doorbell", txq->id);
			return;
		}
		ATOMIC_SUB(to_submit_cnt, &txq->to_submit_cnt);
	}
	NCP_MD_TX_INFO("exit");
}

// Refer to mtk_dpmaif_book_doorbell_work
static void noa_ncp_md_tx_book_doorbell_work(struct noa_tx_queue *txq)
{
	NCP_MD_TX_INFO("enter");

	unsigned int delay_ms = 0;

	int to_submit_cnt = ATOMIC_READ(&txq->to_submit_cnt);

	if (!to_submit_cnt) {
		NCP_MD_TX_ERROR("end, to_submit_cnt is 0");
		return;
	}

	if (to_submit_cnt < txq->burst_submit_cnt && !txq->exit_tcp_ss_counter)
		delay_ms = txq->db_delay_ms;

	NCP_MD_TX_INFO("to_submit_cnt=[%u], burst_submit_cnt=[%u], delay_ms=[%u]",
		to_submit_cnt, txq->burst_submit_cnt, delay_ms);

	schedule_delayed_work(&txq->doorbell_work, msecs_to_jiffies(delay_ms));

	if (txq->exit_tcp_ss_counter)
		txq->exit_tcp_ss_counter--;

	NCP_MD_TX_INFO("exit");
}

static int noa_ncp_md_tx_handle(struct noa_md_fw *md_fw, struct noa_modem_tx_desc *tx_desc)
{
	NCP_MD_TX_INFO(
		"enter: pkt_type=[%u], intf_id=[%u], network_type=[%u],"
		"src=[%u], send_drb_cnt=[%u]",
		tx_desc->ext.pkt_info.pkt_type, tx_desc->ext.pkt_info.intf_id,
		tx_desc->ext.pkt_info.network_type, tx_desc->ext.pkt_info.src,
		tx_desc->ext.pkt_info.drb_cnt);

	// TODO: b/383414113 - Currently TX has disabled scatter and gather,
	// always 2 DRBs, need to check to enable it
	unsigned int send_drb_cnt = tx_desc->ext.pkt_info.drb_cnt;
	unsigned char vq_id = (unsigned char)tx_desc->ext.pkt_info.pkt_type;

	struct noa_md_fw_tx *tx = md_fw->tx;
	struct noa_tx_queue *txq = &tx->txqs[vq_id];
	unsigned int available_drb;
	bool in_tcp_slow_start;

	mutex_lock(&tx->read_desc_lock);
	available_drb = noa_ncp_md_tx_available_desc(
		txq->drb_cnt, txq->drb_rd_idx, txq->drb_wr_idx);

	// TODO: b/407431219 - Get correct intf_id from netengine
	if (tx_desc->basic.tkid >= NOA_MD_FW_TX_POOL_TKID_OFFSET) {
		tx_desc->ext.pkt_info.intf_id = 1;
		tx_desc->ext.pkt_info.drb_cnt = 2;
		send_drb_cnt = tx_desc->ext.pkt_info.drb_cnt;
	}

	NCP_MD_TX_INFO(
		"vq_id=[%u], available_drb=[%u], drb_cnt=[%u],"
		"drb_rd_idx=[%u], drb_wr_idx=[%u]",
		vq_id, available_drb, txq->drb_cnt, txq->drb_rd_idx, txq->drb_wr_idx);

	if (available_drb < send_drb_cnt) {
		NCP_MD_TX_ERROR("exit, no enough drb");
		mutex_unlock(&tx->read_desc_lock);
		return -ENOMEM;
	}

	in_tcp_slow_start = (bool)tx_desc->ext.pkt_info.in_tcp_slow_start;
	if (in_tcp_slow_start)
		txq->exit_tcp_ss_counter = doorbell_reset_count;

	// Get skb address and update to drb
	/* Update tx drb, a msg drb first, then payload drb. */
	noa_ncp_md_tx_set_msg_drb(
		txq,
		txq->drb_wr_idx,
		tx_desc
	);

	// Check tkid_queue is available or not before recording tkid
	if (!txq->tkid_queue) {
		NCP_MD_TX_ERROR("cannot record tkid due to tkid queue is null");
		mutex_unlock(&tx->read_desc_lock);
		return -ENOMEM;
	}

	// Record the tkid(within the range for Tx buffer pool) of the msg drb
	if (tx_desc->basic.tkid >= NOA_MD_FW_TX_POOL_TKID_OFFSET) {
		txq->tkid_queue[txq->drb_wr_idx] = tx_desc->basic.tkid;
	}

	txq->drb_wr_idx = noa_ncp_md_tx_peek_next_index(
		txq->drb_wr_idx, txq->drb_cnt);
	NCP_MD_TX_INFO("update drb_wr_idx=[%u]", txq->drb_wr_idx);

	noa_ncp_md_tx_set_pd_drb(
		txq,
		txq->drb_wr_idx,
		tx_desc,
		1  //last_one
	);

	// Record the tkid(within the range for Tx buffer pool) of the payload drb
	if (tx_desc->basic.tkid >= NOA_MD_FW_TX_POOL_TKID_OFFSET) {
		txq->tkid_queue[txq->drb_wr_idx] = tx_desc->basic.tkid;
	}

	txq->drb_wr_idx = noa_ncp_md_tx_peek_next_index(
		txq->drb_wr_idx, txq->drb_cnt);
	NCP_MD_TX_INFO("update drb_wr_idx=[%u]", txq->drb_wr_idx);

	ATOMIC_ADD(send_drb_cnt, &txq->to_submit_cnt);
	ATOMIC_ADD(send_drb_cnt, &txq->drb_stats);

	// Notify that there are new data to be processed
	/* trigger doorbell */
	noa_ncp_md_tx_book_doorbell_work(txq);
	mutex_unlock(&tx->read_desc_lock);

	NCP_MD_TX_INFO("exit");
	return 0;
}

static bool noa_ncp_md_tx_data_handling(struct noa_md_fw *md_fw)
{
	int ret = 0;
	noa_ring_consumer *ring = &md_fw->tx->tx_ring.ring;
	struct noa_md_fw_tx *tx = md_fw->tx;

	if (!ring) {
		NCP_MD_TX_ERROR("ring is NULL");
		return -EINVAL;
	}
	NCP_MD_TX_INFO("enter, ring->name=[%s]", ring->name);
	ret = tx->ring_ops->begin_processing(ring);
	if (ret <= 0) {
		if (!ret) {
			NCP_MD_TX_ERROR("noa_ring_begin_processing, ret=[%d]", ret);
			ret = -EINVAL;
		}
		goto out;
	}

	while (true) {
		struct noa_modem_tx_desc *p_desc = NULL;
		unsigned long data_addr = 0;
		ret = tx->ring_ops->read(ring, &data_addr, sizeof(data_addr));
		if (!ret) {
			break;
		} else if (ret < 0 || !data_addr) {
			NCP_MD_TX_ERROR("noa_ring_read=[%d]", ret);
			tx->ring_ops->tail_inc(ring);
			continue;
		}
		// Get the TX descriptor from ring and handle the descriptor
		p_desc = (struct noa_modem_tx_desc *)data_addr;
#ifdef NCP_DEBUG
		NCP_MD_TX_INFO(
			"tkid=[%d(0x%x)], mode=[%d], dl=[%d], dp_high=[0x%hx], dp_low=[0x%x], "
			"dv=[0x%lx]",
			p_desc->basic.tkid, p_desc->basic.tkid, p_desc->basic.mode,
			p_desc->basic.dl, p_desc->basic.dp_high, p_desc->basic.dp_low,
			(uintptr_t)p_desc->basic.dv);
		hexdump("md_tx_pkt:", (u8 *)p_desc->basic.dv + p_desc->basic.head_offset,
			p_desc->basic.dl);
#endif
		switch (p_desc->basic.mode) {
		case NOAD_MODE_FEEDBACK:
			// Rx Refill Task for Tethering
			noa_ncp_md_rx_add_tkid_to_free_pool(
					md_fw, p_desc->basic.tkid,
					p_desc->basic.dp_high, p_desc->basic.dp_low,
					p_desc->basic.dv);
			break;
		case NOAD_MODE_DATA:
			noa_ncp_md_tx_handle(md_fw, p_desc);
			break;
		default:
			NCP_MD_TX_ERROR("Unknown Mode(%d)", p_desc->basic.mode);
			break;
		}
	}
	tx->ring_ops->complete_processing(ring);
	NCP_MD_TX_INFO("exit");
out:
	return !tx->ring_ops->is_empty(ring);
}

/* Handle packets from NOA */
static void noa_ncp_md_tx_task(unsigned long data)
{
	struct noa_md_fw *md_fw = (struct noa_md_fw *)data;
	struct noa_md_fw_tx *tx = md_fw->tx;
	int ret;

	NCP_MD_TX_INFO("enter");
	ret = noa_ncp_md_tx_data_handling(md_fw);
	if (ret) {
		NCP_MD_TX_INFO("trigger tasklet_schedule for remaining data");
		tasklet_schedule(&tx->md_tx_task);
	}
	NCP_MD_TX_INFO("exit");
}

static void noa_ncp_md_tx_buffer_pool_deinit(struct noa_md_fw *md_fw)
{
	const u8 buffer_pool_ring_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost, kNoaModemNepBufferPool);
#ifdef linux
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, false, buffer_pool_ring_id,
				      kNoaRingNepInput);
#else
	::noa::service::ring_event_client::RingEventClient::Instance().RingDeactivate(
		buffer_pool_ring_id, kNoaRingNepInput);
#endif
	noa_ring_deactivate(&md_fw->tx_buffer_pool.ring);
	noa_ring_info_clean(&md_fw->tx_buffer_pool.ring);
}

static ssize_t refill_buffer(
		void *buf, size_t buf_len, const void *data, size_t data_len)
{
	*((noa_buffer_pool_desc *)buf) = *(const noa_buffer_pool_desc *)data;
	return sizeof(noa_buffer_pool_desc);
}

const static struct noa_ring_ops pool_ring_ops = {
	.write_payload = refill_buffer,
};

int noa_ncp_md_tx_pool_replenish(struct noa_md_fw *md_fw, u16 pktid)
{
	int ret = -1;
	int idx = pktid - NOA_MD_FW_TX_POOL_TKID_OFFSET;
	struct noa_tx_buffer_pool *pool = &md_fw->tx_buffer_pool;
	noa_ring_producer *ring = &md_fw->tx_buffer_pool.ring;
	noa_buffer_pool_desc item;

	if (idx < 0 || idx >= NOA_MD_FW_FIFO_SIZE) {
		NCP_MD_TX_ERROR("Replenish invalid tkid %u to nep buffer pool", pktid);
		return -EINVAL;
	}

	ret = noa_ring_begin_processing(ring);
	if (ret <= 0) {
		NCP_MD_TX_ERROR("Ring %s is not ready, ret %d", ring->name, ret);
		return ret;
	}

	item.tkid = pktid;
	item.dp_low = pool->pa_base + NOA_MD_MAX_TX_PKT_SIZE * idx;
	item.dp_high = 0;
	item.dv = (unsigned long)((uintptr_t)pool->va_base + NOA_MD_MAX_TX_PKT_SIZE * idx);

	ret = noa_ring_write(ring, &item, sizeof(item));
	if (ret < 0) {
		NCP_MD_TX_ERROR(
			"Failed to replenish tkid %u md pool, head %u, tail %u, ret %d",
			pktid, ring->basic.head, noa_ring_tail_read_once(ring), ret);
	}

	noa_ring_complete_processing(ring);
	return ret;
}

static void noa_ncp_md_tx_buffer_pool_init(struct noa_md_fw *md_fw)
{
	int ret;
	int i;
	u32 pool_size = NOA_MD_FW_FIFO_SIZE;
	const u8 buffer_pool_ring_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost, kNoaModemNepBufferPool);
	struct noa_tx_buffer_pool *pool = &md_fw->tx_buffer_pool;
	struct noa_ring_regs ring_regs = { 0 };
	struct noa_ring_info ring_info = {
		.head = pool_size - 1,
		.tail = 0,
		.base = (char *)&g_pool_ring_buf[0],
		.size = pool_size,
		.item_len = sizeof(noa_buffer_pool_desc),
		.dpa_base = ring_info.base,
	};

	for (i = 0; i < ring_info.head; i++) {
		noa_buffer_pool_desc *item = (noa_buffer_pool_desc *)noa_ring_buf_pos(
			ring_info.base, i, ring_info.item_len);
		item->tkid = i + NOA_MD_FW_TX_POOL_TKID_OFFSET;
		item->dp_low = pool->pa_base + NOA_MD_MAX_TX_PKT_SIZE * i;
		item->dp_high = 0;
		item->dv = (unsigned long)((uintptr_t)pool->va_base + NOA_MD_MAX_TX_PKT_SIZE * i);
	}

	ret = NoaRingSharedRegsGet(&ring_regs, kNoaNetworkInterfaceModem,
				   kNoaNetworkFlowDeviceToHost, kNoaModemNepBufferPool,
				   kNoaRingNepInput);
	if (ret) {
		NCP_MD_TX_ERROR("Failed to get buffer pool regs, ret %d", ret);
		goto error;
	}

	ret = noa_ring_regs_wrapper_init(
		&pool->ring, NOA_RING_TYPE_PRODUCER, &pool_ring_ops, &ring_regs, NULL,
		"md buf", 0);
	if (ret) {
		NCP_MD_TX_ERROR("Failed to init md buf ring, ret %d", ret);
		goto error;
	}
	noa_ring_info_setup(&pool->ring, &ring_info);
	noa_ring_activate(&pool->ring);
#ifdef linux
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true, buffer_pool_ring_id,
				      kNoaRingNepInput);
#else
	::noa::service::ring_event_client::RingEventClient::Instance().RingActivate(
		buffer_pool_ring_id, kNoaRingNepInput);
#endif
	return;

error:
	noa_ncp_md_tx_buffer_pool_deinit(md_fw);
}

static int noa_ncp_md_tx_apc2ncp_handle(
			struct noa_md_fw *md_fw,
			struct MtkMessageDescriptorRingBuffer *tx_desc,
			unsigned char vq_id
			)
{
	NCP_MD_TX_INFO(
		"enter: descriptor_type=[%u], continue_bit=[%u]",
		tx_desc->descriptor_type, tx_desc->continue_bit);
	struct noa_md_fw_tx *tx = md_fw->tx;
	struct noa_tx_queue *txq = &tx->txqs[vq_id];
	bool in_tcp_slow_start = false;
	unsigned int available_drb;
	struct MtkMessageDescriptorRingBuffer *msg_drb = NULL;
	struct MtkPayloadDescriptorRingBuffer *pd_drb = NULL;

	available_drb = noa_ncp_md_tx_available_desc(
		txq->drb_cnt, txq->drb_rd_idx, txq->drb_wr_idx);

	NCP_MD_TX_INFO(
		"vq_id=[%u], available_drb=[%u], drb_cnt=[%u],"
		"drb_rd_idx=[%u], drb_wr_idx=[%u]",
		vq_id, available_drb, txq->drb_cnt, txq->drb_rd_idx, txq->drb_wr_idx);

	if (!available_drb) {
		NCP_MD_TX_ERROR("exit, no enough drb");
		return -ENOMEM;
	}

	if (tx_desc->descriptor_type == MSG_DRB) {
		msg_drb = (struct MtkMessageDescriptorRingBuffer *)txq->drb_base + txq->drb_wr_idx;
		in_tcp_slow_start = tx_desc->noa_tcp_in_slow_start;
		if (in_tcp_slow_start)
			txq->exit_tcp_ss_counter = doorbell_reset_count;

		// We need to to reset the value before copying to the modem drb
		tx_desc->noa_tcp_in_slow_start = 0;
		memcpy(msg_drb, tx_desc, sizeof(struct MtkMessageDescriptorRingBuffer));
		txq->drb_wr_idx = noa_ncp_md_tx_peek_next_index(
						txq->drb_wr_idx, txq->drb_cnt);
		txq->send_drb_cnt++;
		NCP_MD_TX_INFO(
			"[MSG_DRB]update drb_wr_idx=[%u], send_drb_cnt=[%u]",
			txq->drb_wr_idx, txq->send_drb_cnt);
	} else { //PD_DRB
		pd_drb = (struct MtkPayloadDescriptorRingBuffer *)txq->drb_base + txq->drb_wr_idx;
		memcpy(pd_drb, (struct MtkPayloadDescriptorRingBuffer *)tx_desc,
				sizeof(struct MtkPayloadDescriptorRingBuffer));
		NCP_MD_TX_INFO(
			"data_addr=[0x%x], data_len=[%u]",
			pd_drb->address_low, pd_drb->data_length);
		txq->drb_wr_idx = noa_ncp_md_tx_peek_next_index(
				txq->drb_wr_idx, txq->drb_cnt);
		txq->send_drb_cnt++;
		NCP_MD_TX_INFO(
			"[PD_DRB]update drb_wr_idx=[%u], send_drb_cnt=[%u]",
			txq->drb_wr_idx, txq->send_drb_cnt);
		if (pd_drb->continue_bit == DPMAIF_DRB_LASTONE) {
			ATOMIC_ADD(txq->send_drb_cnt, &txq->to_submit_cnt);
			ATOMIC_ADD(txq->send_drb_cnt, &txq->drb_stats);
			txq->send_drb_cnt = 0;
			/* trigger doorbell */
			noa_ncp_md_tx_book_doorbell_work(txq);
		}
	}
	NCP_MD_TX_INFO("exit");
	return 0;
}

static int noa_ncp_md_apc2ncp_handling(struct noa_md_fw *md_fw)
{
	struct noa_md_fw_tx *tx = md_fw->tx;
	uint32_t q_index = tx->isr_drb_index;
	struct noa_md_apc2ncp_tx_buffer_desc *desc = noa_md_apc2ncp_get_tx_buffer_desc();
	noa_ring_consumer *ring = &desc[q_index].ring;
	struct noa_tx_queue *txq = &tx->txqs[q_index];
	int ret = 0;

	if (!ring) {
		NCP_MD_TX_ERROR("ring is NULL");
		return -EINVAL;
	}
	NCP_MD_TX_INFO("enter, ring->name=[%s], q_index=[%u]", ring->name, q_index);
	if (!is_noa_ring_activate(ring))
		return -EINVAL;

	mutex_lock(&tx->read_desc_lock);
	// Init value before reading descriptors
	txq->send_drb_cnt = 0;
	while (true) {
		struct MtkMessageDescriptorRingBuffer *p_desc = NULL;
		unsigned long data_addr = 0;
		ret = noa_ring_read(ring, &data_addr, sizeof(data_addr));
		if (!ret) {
			break;
		} else if (ret < 0 || !data_addr) {
			NCP_MD_TX_ERROR("noa_ring_read=[%d]", ret);
			noa_ring_tail_inc(ring);
			continue;
		}
		// Get the TX descriptor from ring and handle the descriptor
		p_desc = (struct MtkMessageDescriptorRingBuffer *)data_addr;
		noa_ncp_md_tx_apc2ncp_handle(md_fw, p_desc, q_index);
	}
	mutex_unlock(&tx->read_desc_lock);
	return 0;
}

static void noa_ncp_md_tx_apc2ncp_task(unsigned long data)
{
	struct noa_md_fw *md_fw = (struct noa_md_fw *)data;
	int ret = 0;

	NCP_MD_TX_INFO("enter");
	ret = noa_ncp_md_apc2ncp_handling(md_fw);
	if (ret < 0) {
		NCP_MD_TX_ERROR("Failed to read descriptors, ret=%d", ret);
	}
	NCP_MD_TX_INFO("exit");
}

int noa_ncp_md_tx_init(struct noa_md_fw *md_fw)
{
	struct noa_md_fw_tx *tx = md_fw->tx;
	int ret = 0;
	int i = 0;

	NCP_MD_TX_INFO("enter");

	ret = tx->ring_ops->init(md_fw);
	if (ret) {
		NCP_MD_TX_ERROR("Failed to init md tx ring, ret=[%d]", ret);
		return ret;
	}

	/* register noa interrupt */
	ret = modem_fw_register_nep_interrupt(md_fw);
	NCP_MD_TX_INFO("modem_fw_register_nep_interrupt=[%d]", ret);

	/* init tasklet */
	tasklet_init(&tx->md_tx_task, noa_ncp_md_tx_task, (unsigned long)md_fw);

	NCP_MD_TX_INFO("txq_cnt:%u", tx->txq_cnt);
	for (i = 0; i < tx->txq_cnt; i++) {
		struct noa_tx_queue *txq = &tx->txqs[i];
		NOA_MD_INIT_DELAYED_WORK(
			&txq->doorbell_work, noa_ncp_md_tx_doorbell_work, txq);
		NOA_MD_INIT_DELAYED_WORK(
			&txq->tx_done_work, noa_ncp_md_tx_done_work, txq);
	}

	/* init tasklet for apc2ncp drb isr */
	tasklet_init(&tx->apc2ncp_task, noa_ncp_md_tx_apc2ncp_task, (unsigned long)md_fw);
	/* init lock for tx handle */
	mutex_init(&tx->read_desc_lock);

	// Init Modem NCP Tx buffer pool
	noa_ncp_md_tx_buffer_pool_init(md_fw);

	NCP_MD_TX_INFO("exit, ret=[%d]", ret);
	return ret;
}

void noa_ncp_md_tx_exit(struct noa_md_fw *md_fw)
{
	struct noa_md_fw_tx *tx = md_fw->tx;
	int i;

	NCP_MD_TX_INFO("enter");

	tasklet_kill(&tx->md_tx_task);
	for (i = 0; i < tx->txq_cnt; i++) {
		struct noa_tx_queue *txq = &tx->txqs[i];
		cancel_delayed_work_sync(&txq->doorbell_work);
		cancel_delayed_work_sync(&txq->tx_done_work);
	}

	tasklet_kill(&tx->apc2ncp_task);

	// Unregister interrupt
	modem_fw_free_nep_interrupt(md_fw);

	// Release TX ring
	tx->ring_ops->exit(md_fw, kNoaModemRingTxData);

	NCP_MD_TX_INFO("exit");
}
