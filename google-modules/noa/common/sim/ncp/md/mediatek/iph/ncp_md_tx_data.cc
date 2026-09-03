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
#include "modem_fw_mailbox.h"
#include "ring_event_client/ring_event_client.h"
#endif

#define NOA_MD_FW_BUFFER_POOL_RING_SIZE                                                            \
	(round_up((NOA_MD_FW_FIFO_SIZE + 1), NEP_BUFFER_POOL_CACHED_RING_SIZE))
__attribute__((aligned(NEP_BUFFER_POOL_CACHED_RING_ITEM_LEN))) SEC_EXRAM_DATA static noa_buffer_pool_desc
	g_pool_ring_buf[NOA_MD_FW_BUFFER_POOL_RING_SIZE];

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
	const struct noa_modem_tx_desc *tx_desc,
	unsigned char intf_id)
{
	struct MtkMessageDescriptorRingBuffer* msg_drb =
		(struct MtkMessageDescriptorRingBuffer *)txq->drb_dpa_base + cur_idx;

	// for VPN/Tethering
	msg_drb->descriptor_type = MSG_DRB;
	msg_drb->continue_bit = DPMAIF_DRB_MORE;
	msg_drb->packet_length = tx_desc->basic.dl;
	msg_drb->count_l_psn = 0;  // Disable msg_count feature.
	msg_drb->channel_id = intf_id;
	msg_drb->network_type = tx_desc->ext.pkt_info.network_type;
	msg_drb->ipv4 = 0;  // Disable IPv4 UL checksum offload.
	msg_drb->l4_checksum = 1;  // Enable TCP checksum offload.
}

static inline void noa_ncp_md_tx_set_pd_drb(
	struct noa_tx_queue *txq,
	unsigned short cur_idx,
	const struct noa_modem_tx_desc *tx_desc,
	char last_one)
{
	struct MtkPayloadDescriptorRingBuffer* pd_drb =
		(struct MtkPayloadDescriptorRingBuffer *)txq->drb_dpa_base + cur_idx;

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

static u16 noa_ncp_md_tx_get_tkid_from_address(dma_addr_t dma_base, uint32_t address)
{
	u16 tkid = (address - dma_base) / NOA_MD_MAX_TX_PKT_SIZE + NOA_MD_FW_TX_POOL_TKID_OFFSET;
	bool is_tethering = (NOA_MD_FW_TX_POOL_TKID_OFFSET <= tkid &&
			     tkid < NOA_MD_FW_TX_POOL_TKID_OFFSET + NOA_MD_FW_FIFO_SIZE);
	if (!is_tethering) {
		return 0;
	}
	NCP_MD_TX_DEBUG("address=0x%x, dma_base=0x%x, tkid=%x,"
			" min_tkid=%x, max_tkid=%x, is_tethering=%d",
			address, dma_base, tkid, NOA_MD_FW_TX_POOL_TKID_OFFSET,
			NOA_MD_FW_TX_POOL_TKID_OFFSET + NOA_MD_FW_FIFO_SIZE - 1, is_tethering);
	return tkid;
}

// Refer to mtk_dpmaif_tx_rel_internal
static int noa_ncp_md_tx_rel_internal(
		struct noa_tx_queue *txq, unsigned int rel_cnt,
		unsigned int *real_rel_cnt)
{
	struct MtkPayloadDescriptorRingBuffer *cur_drb = NULL,
		*drb_base = (struct MtkPayloadDescriptorRingBuffer *)txq->drb_dpa_base;
	unsigned short cur_idx;
	unsigned int i;
	unsigned int vpn_tether_count = 0, read_cnt = 0;
	u16 tkid = 0;

	cur_idx = txq->drb_rel_rd_idx;
	NCP_MD_TX_DEBUG(
		"drb_rel_rd_idx:%u, rel_cnt:%u", txq->drb_rel_rd_idx, rel_cnt);
	for (i = 0 ; i < rel_cnt; i++) {
		cur_drb = drb_base + cur_idx;
		if (cur_drb->descriptor_type == PD_DRB) {
			NCP_MD_TX_DEBUG("handle pd_drb");
			/* The last one drb entry of one tx packet, so,
			   skb will be released. */
			if (cur_drb->continue_bit == DPMAIF_DRB_LASTONE) {
				NCP_MD_TX_DEBUG("handle lastone");
				// recycle the related Tx buffer in Tx buffer pool
				mutex_lock(&g_md_fw->tx->read_desc_lock);
				if (txq->tkid_queue.head == txq->tkid_queue.tail) {
					NCP_MD_TX_DEBUG("txq%d: skip tkid check due to tkid_queue"
							" is empty, head=%d, tail=%d",
							txq->id, txq->tkid_queue.head,
							txq->tkid_queue.tail);
				} else {
					tkid = noa_ncp_md_tx_get_tkid_from_address(
						g_md_fw->tx_buffer_pool.pa_base,
						cur_drb->address_low);
					NCP_MD_TX_DEBUG(
						"txq%d: tkid_for_drb[%d]=%x, tkid_queue[%d]=%x",
						txq->id, cur_idx, tkid, txq->tkid_queue.head,
						txq->tkid_queue.items[txq->tkid_queue.head]);
					if (tkid != 0 &&
					    tkid == txq->tkid_queue.items[txq->tkid_queue.head]) {
						noa_ncp_md_tx_pool_replenish(g_md_fw, tkid);
						txq->tkid_queue.items[txq->tkid_queue.head] = 0;
						txq->tkid_queue.head =
							noa_ncp_md_tx_peek_next_index(
								txq->tkid_queue.head,
								NOA_MD_FW_FIFO_SIZE);
						vpn_tether_count++;
					}
				}
				mutex_unlock(&g_md_fw->tx->read_desc_lock);
			}
		} else {
			NCP_MD_TX_DEBUG("handle msg_drb");
		}
		cur_idx =
			noa_ncp_md_tx_ring_next_idx(txq->drb_cnt, cur_idx);
		txq->drb_rel_rd_idx = cur_idx;
	}
	// Only print the last one release info
	NCP_MD_TX_DEBUG("txq%u pkt(%u),w=%u,r=%u,rel=%u,cnt=%u",
			txq->id, cur_idx, txq->drb_wr_idx,
			txq->drb_rd_idx, txq->drb_rel_rd_idx,
			rel_cnt);

	*real_rel_cnt = i;
	NCP_MD_TX_DEBUG("real_rel_cnt=[%u], vpn_tether_count=[%u]", *real_rel_cnt, vpn_tether_count);
	// Minus VPN/Tethering which only support single skb(msg+pd)
	unsigned int vpn_tether_count_twice = vpn_tether_count << 1;
	if (*real_rel_cnt < vpn_tether_count_twice)
		PW_CRASH("[%s][%d][%s] Error CNT", __func__, __LINE__, __FILE__);

	read_cnt = *real_rel_cnt - vpn_tether_count_twice;
	if (read_cnt > 0) {
		// Update the read index to shared info
		noa_ncp_md_apc2ncp_set_ring_read_idx(txq->id, read_cnt);
		// Trigger doorbell to apc
		ncp_md_irq_dpa_notify_apc(txq->id);
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
	NCP_MD_TX_DEBUG("txq%u drb: w=%u,r=%u,rel=%u, rel_cnt=%u",
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

#ifdef linux
	struct delayed_work *dwork = to_delayed_work(work);
	txq = container_of(dwork, struct noa_tx_queue, tx_done_work);
#else
	txq = (struct noa_tx_queue *)data;
#endif

	/* Recycle drb and release hardware tx done buffer around drb. */
	noa_ncp_md_tx_rel(txq);

	NCP_MD_TX_DEBUG("drb_poll_mode: %u", txq->drb_poll_mode);

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
	NCP_MD_TX_DEBUG("exit");
}

// Refer to mtk_dpmaif_irq_tx_done
void noa_ncp_md_tx_irq_tx_done(
		struct noa_md_fw *md_fw, unsigned int q_mask)
{
	NCP_MD_TX_DEBUG("enter, md_fw=%p, q_mask=%d", md_fw, q_mask);

	struct noa_md_fw_tx *tx = md_fw->tx;

	if (!tx) {
		NCP_MD_TX_ERROR("tx is null");
		return;
	}

	for (int i = 0; i < tx->txq_cnt; i++) {
		unsigned int ulq_done = q_mask & (1 << i);
		if (ulq_done) {
			struct noa_tx_queue *txq = NULL;
			int drb_rd_idx;
			NCP_MD_TX_DEBUG("ulq_done for queue_id=%u", i);
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
			NCP_MD_TX_DEBUG("drb_rd_idx:%u", txq->drb_rd_idx);
			schedule_delayed_work(&txq->tx_done_work, msecs_to_jiffies(0));
		}
	}
	NCP_MD_TX_DEBUG("exit");
}

union noa_dpmaif_drb_entry {
    struct { uint32_t dword1, dword2, dword3, dword4; } raw;
};

static void noa_ncp_md_tx_dump_drb_info(struct noa_tx_queue *txq)
{
    unsigned int drb_idx;
    const unsigned int dump_cnt = 10;

    if (!txq || !txq->drb_dpa_base) {
        NCP_MD_TX_ERROR("Cannot dump DRB: txq or drb_dpa_base is null.\n");
        return;
    }

    if (txq->drb_wr_idx < dump_cnt) {
        drb_idx = txq->drb_cnt + txq->drb_wr_idx - dump_cnt;
    } else {
        drb_idx = txq->drb_wr_idx - dump_cnt;
    }

    for (int index = 0; index < dump_cnt ; index++) {
        const union noa_dpmaif_drb_entry* drb_info;

        drb_info = reinterpret_cast<union noa_dpmaif_drb_entry*>(txq->drb_dpa_base)
            + drb_idx;
        NCP_MD_TX_ERROR("drb(%u)[0x%p]: 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
            drb_idx, drb_info,
            drb_info->raw.dword1, drb_info->raw.dword2,
            drb_info->raw.dword3, drb_info->raw.dword4);
        drb_idx = noa_ncp_md_tx_peek_next_index(drb_idx, txq->drb_cnt);
    }
}

// Refer to mtk_dpmaif_tx_doorbell
#ifdef linux
void noa_ncp_md_tx_doorbell_work(struct work_struct *work)
#else
void noa_ncp_md_tx_doorbell_work(void* data)
#endif
{
	NCP_MD_TX_DEBUG("enter");

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
		NCP_MD_TX_DEBUG("to_submit_cnt: %u, id:%u", to_submit_cnt, txq->id);
		g_md_fw->hif->GetPowerManager().DeepSleepLock();
		if (!g_md_fw->hif->GetPowerManager().DeepSleepWaitComplete().ok()) {
			NCP_MD_TX_ERROR("Failed to wait DeepSleepLock");
			g_md_fw->hif->GetPowerManager().DeepSleepUnlock();
			return;
		}
		ret = NOA_MD_SEND_DOORBELL(NOA_MD_DPMAIF_DRB, txq->id, to_submit_cnt);
		g_md_fw->hif->GetPowerManager().DeepSleepUnlock();
		if (unlikely(ret < 0)) {
			// TODO(b/432392753): Notify APC to execute mtk_dpmaif_common_err_handle
			NCP_MD_TX_ERROR(
				"Failed to send txq%d doorbell, w=%u,r=%u,rel=%u, to_submit=%u",
				txq->id, txq->drb_wr_idx, txq->drb_rd_idx,
				txq->drb_rel_rd_idx, to_submit_cnt);
			noa_ncp_md_tx_dump_drb_info(txq);
			// Sleep 3 sec for dumping logs before triggering the dpa crash
			pw::this_thread::sleep_for(std::chrono::milliseconds(3000));
			PW_CRASH("Doorbell timeout!");
			return;
		}
		ATOMIC_SUB(to_submit_cnt, &txq->to_submit_cnt);
	}
	NCP_MD_TX_DEBUG("exit");
}

// Refer to mtk_dpmaif_book_doorbell_work
static void noa_ncp_md_tx_book_doorbell_work(struct noa_tx_queue *txq)
{
	NCP_MD_TX_DEBUG("enter");

	unsigned int delay_ms = 0;

	int to_submit_cnt = ATOMIC_READ(&txq->to_submit_cnt);

	if (!to_submit_cnt) {
		NCP_MD_TX_ERROR("end, to_submit_cnt is 0");
		return;
	}

	if (to_submit_cnt < txq->burst_submit_cnt && !txq->exit_tcp_ss_counter)
		delay_ms = txq->db_delay_ms;

	NCP_MD_TX_DEBUG("to_submit_cnt=[%u], burst_submit_cnt=[%u], delay_ms=[%u]",
		to_submit_cnt, txq->burst_submit_cnt, delay_ms);

	schedule_delayed_work(&txq->doorbell_work, msecs_to_jiffies(delay_ms));

	if (txq->exit_tcp_ss_counter)
		txq->exit_tcp_ss_counter--;

	NCP_MD_TX_DEBUG("exit");
}

static int noa_ncp_md_tx_handle(struct noa_md_fw *md_fw, const struct noa_modem_tx_desc *tx_desc)
{
	NCP_MD_TX_DEBUG(
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
	unsigned char intf_id = tx_desc->ext.pkt_info.intf_id;
	int next_tkid_queue_tail = 0;

	mutex_lock(&tx->read_desc_lock);
	available_drb = noa_ncp_md_tx_available_desc(
		txq->drb_cnt, txq->drb_rd_idx, txq->drb_wr_idx);

	if (tx_desc->basic.tkid >= NOA_MD_FW_TX_POOL_TKID_OFFSET &&
		tx_desc->ext.pkt_info.ifindex > 0) {
		bool found_intf_id = false;
		for (int i = 0; i < MAX_WWAN_IFINDEX_TABLE_SIZE; i++) {
			if (g_md_fw->wwan_ifindex_table[i] == tx_desc->ext.pkt_info.ifindex) {
				intf_id = i;
				found_intf_id = true;
				break;
			}
		}
		if (!found_intf_id) {
			NCP_MD_TX_ERROR("ifindex=[%d] is not found in wwan_ifindex_table, tkid=%d",
					tx_desc->ext.pkt_info.ifindex, tx_desc->basic.tkid);
			mutex_unlock(&tx->read_desc_lock);
			return -EINVAL;
		}
	}

	NCP_MD_TX_DEBUG(
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
		tx_desc,
		intf_id
	);

	// TODO: b/438312277 - Flush cache only once before ringing doorbell
	FlushDCache((struct MtkMessageDescriptorRingBuffer *)txq->drb_dpa_base + txq->drb_wr_idx,
		    sizeof(struct MtkMessageDescriptorRingBuffer));

	txq->drb_wr_idx = noa_ncp_md_tx_peek_next_index(
		txq->drb_wr_idx, txq->drb_cnt);
	NCP_MD_TX_DEBUG("update drb_wr_idx=[%u]", txq->drb_wr_idx);

	noa_ncp_md_tx_set_pd_drb(
		txq,
		txq->drb_wr_idx,
		tx_desc,
		1  //last_one
	);

	// TODO: b/438312277 - Flush cache only once before ringing doorbell
	FlushDCache((struct MtkPayloadDescriptorRingBuffer *)txq->drb_dpa_base + txq->drb_wr_idx,
		    sizeof(struct MtkPayloadDescriptorRingBuffer));

	// Record the tkid(within the range for Tx buffer pool) of the payload drb
	if (tx_desc->basic.tkid >= NOA_MD_FW_TX_POOL_TKID_OFFSET) {
		next_tkid_queue_tail =
			noa_ncp_md_tx_peek_next_index(txq->tkid_queue.tail, NOA_MD_FW_FIFO_SIZE);
		if (next_tkid_queue_tail == txq->tkid_queue.head) {
			NCP_MD_TX_ERROR("txq%d: exit, tkid_queue is full, head=%d, tail=%d",
					txq->id, txq->tkid_queue.head, txq->tkid_queue.tail);
			mutex_unlock(&tx->read_desc_lock);
			return -ENOMEM;
		}
		txq->tkid_queue.items[txq->tkid_queue.tail] = tx_desc->basic.tkid;
		NCP_MD_TX_DEBUG("add tkid %x for drb%d[%d] to tail=%d", tx_desc->basic.tkid,
				txq->id, txq->drb_wr_idx, txq->tkid_queue.tail);
		txq->tkid_queue.tail = next_tkid_queue_tail;
	}

	txq->drb_wr_idx = noa_ncp_md_tx_peek_next_index(
		txq->drb_wr_idx, txq->drb_cnt);
	NCP_MD_TX_DEBUG("update drb_wr_idx=[%u]", txq->drb_wr_idx);

	ATOMIC_ADD(send_drb_cnt, &txq->to_submit_cnt);
	ATOMIC_ADD(send_drb_cnt, &txq->drb_stats);

	// Notify that there are new data to be processed
	/* trigger doorbell */
	noa_ncp_md_tx_book_doorbell_work(txq);
	mutex_unlock(&tx->read_desc_lock);

	NCP_MD_TX_DEBUG("exit");
	return 0;
}

bool noa_ncp_md_tx_data_handling(struct noa_md_fw *md_fw)
{
	int ret = 0;
	int tx_err = 0;
	noa_ring_consumer *ring = &md_fw->tx->tx_ring.ring;
	struct noa_md_fw_tx *tx = md_fw->tx;

	if (!ring) {
		NCP_MD_TX_ERROR("ring is NULL");
		return -EINVAL;
	}
	NCP_MD_TX_DEBUG("enter, ring->name=[%s]", ring->name);
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
		NCP_MD_TX_DEBUG(
			"tkid=[%d(0x%x)], mode=[%d], dl=[%d], dp_high=[0x%hx], dp_low=[0x%x], "
			"dv=[0x%lx]",
			p_desc->basic.tkid, p_desc->basic.tkid, p_desc->basic.mode,
			p_desc->basic.dl, p_desc->basic.dp_high, p_desc->basic.dp_low,
			(uintptr_t)p_desc->basic.dv);
#if NCP_DEBUG
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
			tx_err = noa_ncp_md_tx_handle(md_fw, p_desc);
			if (tx_err < 0) {
				NCP_MD_TX_ERROR("tx_err=[%d], recycle tkid %d back to nep", tx_err,
						p_desc->basic.tkid);
				noa_ncp_md_tx_pool_replenish(g_md_fw, p_desc->basic.tkid);
			}
			break;
		default:
			NCP_MD_TX_ERROR("Unknown Mode(%d)", p_desc->basic.mode);
			break;
		}
	}
	tx->ring_ops->complete_processing(ring);
	NCP_MD_TX_DEBUG("exit");
out:
	return !tx->ring_ops->is_empty(ring);
}

/* Handle packets from NOA */
static void noa_ncp_md_tx_task(unsigned long data)
{
	struct noa_md_fw *md_fw = (struct noa_md_fw *)data;
	struct noa_md_fw_tx *tx = md_fw->tx;
	int ret;

	NCP_MD_TX_DEBUG("enter");
	ret = noa_ncp_md_tx_data_handling(md_fw);
	if (ret) {
		NCP_MD_TX_INFO("trigger tasklet_schedule for remaining data");
		tasklet_schedule(&tx->md_tx_task);
	}
	NCP_MD_TX_DEBUG("exit");
}

void noa_ncp_md_tx_tkid_queues_shmem_update(struct noa_md_fw *md_fw)
{
	struct noa_md_shmem_layout *shmem;
	if (unlikely(!md_fw)) {
		NCP_MD_ERROR("md_fw is NULL");
		return;
	}

	if (unlikely(!md_fw->shared_mem_info.addr)) {
		NCP_MD_ERROR("shared memory address is NULL");
		return;
	}
	shmem = (struct noa_md_shmem_layout *)md_fw->shared_mem_info.addr;

	mutex_lock(&md_fw->tx->read_desc_lock);
	for (int i = 0; i < NOA_MD_MAX_UL_QUEUE_SIZE; i++) {
		memcpy(&shmem->tx_tkid_queues[i], &md_fw->tx->txqs[i].tkid_queue,
		       sizeof(struct noa_md_tx_tkid_queue_fifo));
	}
	mutex_unlock(&md_fw->tx->read_desc_lock);

	FlushDCache(shmem->tx_tkid_queues,
		    sizeof(struct noa_md_tx_tkid_queue_fifo) * NOA_MD_MAX_UL_QUEUE_SIZE);
	NCP_MD_DEBUG("shmem: h=%d, t=%d, sram: h=%d, t=%d", shmem->tx_tkid_queues[0].head,
		     shmem->tx_tkid_queues[0].tail, md_fw->tx->txqs[0].tkid_queue.head,
		     md_fw->tx->txqs[0].tkid_queue.tail);
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

static void trigger_doorbell(struct noa_ring_wrapper *ring)
{
	(void)ring;
#ifndef linux
	modem_fw_notify_nep_mailbox();
#endif /* linux */
}

const static struct noa_ring_ops pool_ring_ops = {
	.write_payload = refill_buffer,
	.complete_hook = trigger_doorbell,
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
		.head = pool_size,
		.tail = 0,
		.base = (char *)&g_pool_ring_buf[0],
		.size = NOA_MD_FW_BUFFER_POOL_RING_SIZE,
		.item_len = sizeof(noa_buffer_pool_desc),
		.dpa_base = ring_info.base,
	};

	// We do not populate the slot at the head with buffer info, as the head pointer 
	// indicates the next available write location.
	for (i = 0; i < ring_info.head; i++) {
		noa_buffer_pool_desc *item = (noa_buffer_pool_desc *)noa_ring_buf_pos(
			ring_info.base, i, ring_info.item_len);
		item->tkid = i + NOA_MD_FW_TX_POOL_TKID_OFFSET;
		item->dp_low = pool->pa_base + NOA_MD_MAX_TX_PKT_SIZE * i;
		item->dp_high = 0;
		item->dv = (unsigned long)((uintptr_t)pool->va_base + NOA_MD_MAX_TX_PKT_SIZE * i);
	}
	FlushDCache(ring_info.base, ring_info.item_len * ring_info.size);

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
	NCP_MD_TX_DEBUG(
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

	NCP_MD_TX_DEBUG(
		"vq_id=[%u], available_drb=[%u], drb_cnt=[%u],"
		"drb_rd_idx=[%u], drb_wr_idx=[%u]",
		vq_id, available_drb, txq->drb_cnt, txq->drb_rd_idx, txq->drb_wr_idx);

	if (!available_drb) {
		NCP_MD_TX_ERROR("exit, no enough drb");
		return -ENOMEM;
	}

	if (tx_desc->descriptor_type == MSG_DRB) {
		struct noa_md_shmem_layout *shmem =
				(struct noa_md_shmem_layout *)md_fw->shared_mem_info.addr;
		struct noa_tx_queue_info *txq_info = &shmem->txqs[vq_id];
		msg_drb = (struct MtkMessageDescriptorRingBuffer *)txq->drb_dpa_base + txq->drb_wr_idx;
		in_tcp_slow_start = tx_desc->noa_tcp_in_slow_start;
		if (in_tcp_slow_start)
			txq->exit_tcp_ss_counter = doorbell_reset_count;

		// We need to to reset the value before copying to the modem drb
		tx_desc->noa_tcp_in_slow_start = 0;
		txq_info->pkt_cnt++;
		memcpy(msg_drb, tx_desc, sizeof(struct MtkMessageDescriptorRingBuffer));
		// TODO: b/440299286 - Flush cache only once before ringing doorbell
		FlushDCache((struct MtkMessageDescriptorRingBuffer *)txq->drb_dpa_base + txq->drb_wr_idx,
		    sizeof(struct MtkMessageDescriptorRingBuffer));
		txq->drb_wr_idx = noa_ncp_md_tx_peek_next_index(
						txq->drb_wr_idx, txq->drb_cnt);
		txq->send_drb_cnt++;
		NCP_MD_TX_DEBUG(
			"[MSG_DRB]update drb_wr_idx=[%u], send_drb_cnt=[%u], pkt_cnt=[%d]",
			txq->drb_wr_idx, txq->send_drb_cnt, txq_info->pkt_cnt);
	} else { //PD_DRB
		pd_drb = (struct MtkPayloadDescriptorRingBuffer *)txq->drb_dpa_base + txq->drb_wr_idx;
		memcpy(pd_drb, (struct MtkPayloadDescriptorRingBuffer *)tx_desc,
				sizeof(struct MtkPayloadDescriptorRingBuffer));
		// TODO: b/440299286 - Flush cache only once before ringing doorbell
		FlushDCache((struct MtkPayloadDescriptorRingBuffer *)txq->drb_dpa_base + txq->drb_wr_idx,
		    sizeof(struct MtkPayloadDescriptorRingBuffer));
		txq->drb_wr_idx = noa_ncp_md_tx_peek_next_index(
				txq->drb_wr_idx, txq->drb_cnt);
		txq->send_drb_cnt++;
		NCP_MD_TX_DEBUG(
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
	NCP_MD_TX_DEBUG("exit");
	return 0;
}

static int noa_ncp_md_apc2ncp_handling(struct noa_md_fw *md_fw)
{
	struct noa_md_fw_tx *tx = md_fw->tx;
	struct noa_md_apc2ncp_tx_buffer_desc *desc = noa_md_apc2ncp_get_tx_buffer_desc();
	int polling_count = 0;
	int desc_count = 0;
	int ret = 0;

	if (!tx || !desc) {
		NCP_MD_TX_ERROR("tx or desc is NULL");
		return -EINVAL;
	}

	for (int q_index = 0; q_index < tx->txq_cnt; q_index++) {
		unsigned int ulq_done = tx->isr_apc2ncp_bitmask.load() & (1 << q_index);
		if (ulq_done) {
			tx->isr_apc2ncp_bitmask.fetch_and(~(1 << q_index));
			noa_ring_consumer *ring = &desc[q_index].ring;
			if (!ring) {
				NCP_MD_TX_ERROR("ring is NULL");
				return -EINVAL;
			}
			NCP_MD_TX_DEBUG("enter, ring->name=[%s], q_index=[%u]", ring->name, q_index);
			if (!is_noa_ring_activate(ring))
				return -EINVAL;

			if (ring->basic.tail != noa_ring_head_read_once(ring)) {
				mutex_lock(&tx->read_desc_lock);
				// Init value before reading descriptors
				struct noa_tx_queue *txq = &tx->txqs[q_index];
				desc_count = 0;
				while (true) {
					struct MtkMessageDescriptorRingBuffer *p_desc = NULL;
					unsigned long data_addr = 0;

					// Stop reading desc due to dynamic switch to direct path
					if (tx->stop_read_desc_flag.load()) {
						// One packet is presented by one message drb
						// and one payload drb w/o S&G.
						// TODO: b/441654601 - Check the last one pd_drb
						if (desc_count % 2 == 0) {
							NCP_MD_TX_INFO("Stop reading desc for "
								       "q_index=[%u]", q_index);
							break;
						}
					}

					NCP_MD_TX_DEBUG("[q%u]Tail: %u, Basic Tail: %u, Head: %d\n",
							q_index, noa_ring_tail_read_once(ring), ring->basic.tail, noa_ring_head_read_once(ring));

					ret = noa_ring_read(ring, &data_addr, sizeof(data_addr));
					if (!ret) {
					        // TODO: b/441654601 - Check if it is the last one pd_drb
						if (desc_count % 2 != 0) {
							NCP_MD_TX_INFO("Continue to read the next desc");
							continue;
						} else {
							break;
						}
					} else if (ret < 0 || !data_addr) {
						NCP_MD_TX_ERROR("noa_ring_read=[%d]", ret);
						noa_ring_tail_inc(ring);
						continue;
					}

					// Polling data until data is available
					while (((uint32_t *)data_addr)[0] == 0 &&
							((uint32_t *)data_addr)[1] == 0 &&
							((uint32_t *)data_addr)[2] == 0 &&
							((uint32_t *)data_addr)[3] == 0) {
						// Invalidate cache with correct length
						InvalidateDCache();
						pw::this_thread::sleep_for(std::chrono::milliseconds(DPMAIF_POLL_STEP));
						polling_count++;
						continue;
					}
					NCP_MD_TX_DEBUG("polling_count=[%d]", polling_count);
					NCP_MD_TX_DEBUG(
						"[q%d]data_addr=[0x%x]: 0x%08x, 0x%08x, 0x%08x, 0x%08x",
						q_index, (uintptr_t)data_addr,
						((uint32_t *)data_addr)[0], ((uint32_t *)data_addr)[1],
						((uint32_t *)data_addr)[2], ((uint32_t *)data_addr)[3]);
					polling_count = 0;

					// Get the TX descriptor from ring and handle the descriptor
					p_desc = (struct MtkMessageDescriptorRingBuffer *)data_addr;
					noa_ncp_md_tx_apc2ncp_handle(md_fw, p_desc, q_index);

					// Update desc_count and check if it is greater than the threshold
					desc_count++;
					if (desc_count >= NOA_MD_FW_READ_DESC_MAX) {
						NCP_MD_TX_DEBUG("Stop reading, desc_count=%u for queue=%u",
							desc_count, q_index);
						break;
					}
				}
				mutex_unlock(&tx->read_desc_lock);
			}
		}
	}
	return 0;
}

static void noa_ncp_md_tx_apc2ncp_task(unsigned long data)
{
	struct noa_md_fw *md_fw = (struct noa_md_fw *)data;
	int ret = 0;

	NCP_MD_TX_DEBUG("enter");
	ret = noa_ncp_md_apc2ncp_handling(md_fw);
	if (ret < 0) {
		NCP_MD_TX_ERROR("Failed to read descriptors, ret=%d", ret);
	}
	NCP_MD_TX_DEBUG("exit");
}

/// @brief Updates modem rings' indices for offload path.
/// @details This function is called to update modem rings' indices from shared memory.
///
/// @param ap_state Pointer to the AP state to send to the NCP.
/// @param tx Pointer to the firmware tx.
void noa_ncp_md_tx_update_ring_info_for_offload_path(
		struct dpath_ap_state_payload *ap_state,
		struct noa_md_fw_tx *tx) {

	if (unlikely(!ap_state) || unlikely(!tx) ) {
		NCP_MD_ERROR("ap_state or tx is NULL");
		return;
	}

	// Allow reading descriptors from apc2ncp tx rings
	tx->stop_read_desc_flag.store(false);

	for (uint32_t index = 0; index < kMaxUlQueueSize; index++) {
		const struct tx_ring_idx *txq_source = &ap_state->txqs[index];
		struct noa_tx_queue *txq_dest = &tx->txqs[index];
		txq_dest->drb_wr_idx = txq_source->drb_wr_idx;
		txq_dest->drb_rd_idx = txq_source->drb_rd_idx;
		txq_dest->drb_rel_rd_idx = txq_source->drb_rel_rd_idx;

		NCP_MD_TX_INFO("txqs[%d] drb_wr_idx=%u, drb_rd_idx=%u, drb_rel_rd_idx=%u",
				index,
				txq_dest->drb_wr_idx,
				txq_dest->drb_rd_idx,
				txq_dest->drb_rel_rd_idx);

		// Set tx apc2ncp rings' indices to rings' shared info
		noa_ncp_md_apc2ncp_set_ring_index_by_type(
				index, NOA_MD_RING_WRITE_INDEX, txq_dest->drb_wr_idx);
		noa_ncp_md_apc2ncp_set_ring_index_by_type(
				index, NOA_MD_RING_READ_INDEX, txq_dest->drb_rd_idx);
		// drb_temp_rd_idx equals to drb_wr_idx
		noa_ncp_md_apc2ncp_set_ring_index_by_type(
				index, NOA_MD_RING_TEMP_READ_INDEX, txq_dest->drb_wr_idx);
	}
}


/// @brief Updates modem rings' indices for direct path.
///
/// This function is called to update modem rings' indices to shared memory.
///
/// @param md_fw Pointer to the modem firmware.
/// @param tx Pointer to the firmware tx.
/// @param ncp_state Pointer to the NCP state to send to the AP.
void noa_ncp_md_tx_update_ring_info_for_direct_path(
		struct noa_md_fw *md_fw,
		struct noa_md_fw_tx *tx,
		struct dpath_ncp_state_payload *ncp_state) {

	if (unlikely(!md_fw)  || unlikely(!tx) || unlikely(!ncp_state) ) {
		NCP_MD_TX_ERROR("md_fw or tx or ncp_state is NULL");
		return;
	}

	// Stop reading descriptors from apc2ncp tx rings
	tx->stop_read_desc_flag.store(true);

	NCP_MD_TX_INFO("Make sure all rings stop to read");
	noa_ncp_md_apc2ncp_handling(md_fw);

	for (uint32_t index = 0; index < kMaxUlQueueSize; index++) {
		struct noa_tx_queue *txq_source = &tx->txqs[index];
		struct tx_ring_idx *txq_dest = &ncp_state->txqs[index];
		txq_dest->drb_wr_idx = txq_source->drb_wr_idx;
		txq_dest->drb_rd_idx = txq_source->drb_rd_idx;
		txq_dest->drb_rel_rd_idx = txq_source->drb_rel_rd_idx;

		NCP_MD_TX_INFO("txqs[%d] drb_wr_idx=%u, drb_rd_idx=%u, drb_rel_rd_idx=%u",
			index,
			txq_dest->drb_wr_idx,
			txq_dest->drb_rd_idx,
			txq_dest->drb_rel_rd_idx);

		// Update tx apc2ncp rings' indices to shared memory
		struct tx_apc2ncp_ring_idx *apc2ncp_txq = &ncp_state->apc2ncp_txqs[index];

		apc2ncp_txq->drb_wr_idx =
			noa_ncp_md_apc2ncp_get_ring_index_by_type(index, NOA_MD_RING_WRITE_INDEX);
		apc2ncp_txq->drb_rd_idx =
			noa_ncp_md_apc2ncp_get_ring_index_by_type(index, NOA_MD_RING_READ_INDEX);
		apc2ncp_txq->drb_temp_rd_idx =
			noa_ncp_md_apc2ncp_get_ring_index_by_type(
						index, NOA_MD_RING_TEMP_READ_INDEX);

		NCP_MD_TX_INFO("apc2ncp_txqs[%d] drb_wr_idx=%u, drb_rd_idx=%u, drb_temp_rd_idx=%u",
			index,
			apc2ncp_txq->drb_wr_idx,
			apc2ncp_txq->drb_rd_idx,
			apc2ncp_txq->drb_temp_rd_idx);
	}
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

	// Init value for stop_read_desc_flag
	tx->stop_read_desc_flag.store(false);

	NCP_MD_TX_INFO("exit, ret=[%d]", ret);
	return ret;
}

void noa_ncp_md_tx_activate(struct noa_md_fw *md_fw)
{
	struct noa_md_fw_tx *tx = md_fw->tx;
	int ret;

	NCP_MD_TX_INFO("enter");

	ret = tx->ring_ops->activate(&tx->tx_ring.ring, kNoaModemRingTxData);
	if (ret) {
		NCP_MD_TX_ERROR("Failed to activate tx ring, ret=[%d]", ret);
	}

	NCP_MD_TX_INFO("exit");
}

void noa_ncp_md_tx_deactivate(struct noa_md_fw *md_fw)
{
	struct noa_md_fw_tx *tx = md_fw->tx;
	int ret;

	NCP_MD_TX_INFO("enter");

	ret = tx->ring_ops->deactivate(&tx->tx_ring.ring, kNoaModemRingTxData);
	if (ret) {
		NCP_MD_TX_ERROR("Failed to deactivate tx ring, ret=[%d]", ret);
	}

	NCP_MD_TX_INFO("exit");
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
