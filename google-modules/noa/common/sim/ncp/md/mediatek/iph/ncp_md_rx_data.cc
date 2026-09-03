/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD (MTK) Driver
 *
 * Copyright 2024 Google LLC.
 */

#include "modem_interface.h"
#include "ncp_md.h"
#include "ncp_md_rx_data.h"
#include "ncp_modem_data.h"
#include "noa_md_apc2ncp_ring.h"
#include "sys_common.h"

#ifdef linux
#define FLUSH_DC_CACHE(...) dma_wmb();
#else
#include "pw_assert/assert.h"
#define FLUSH_DC_CACHE(...) FlushDCache(__VA_ARGS__)
#endif

void noa_ncp_md_rx_set_switch_command(struct noa_md_fw_rx *rx, uint32_t switch_cmd)
{
	if (unlikely(!rx)) {
		NCP_MD_RX_ERROR("rx is NULL");
	}

	rx->switch_cmd = switch_cmd;
	NCP_MD_RX_DEBUG("switch_cmd: %d", rx->switch_cmd);
}

static uint32_t noa_ncp_md_rx_get_switch_command(struct noa_md_fw_rx *rx)
{
	if (unlikely(!rx)) {
		NCP_MD_RX_ERROR("rx is NULL");
		return -EINVAL;
	}
	NCP_MD_RX_DEBUG("switch_cmd: %d", rx->switch_cmd);

	return rx->switch_cmd;
}

static unsigned short noa_ncp_md_bat_rx_tkid_map(
	int bat_id,
	int bat_type,
	unsigned short rx_tkid)
{
	unsigned short mapped_rx_tkid = 0;
	switch (bat_type) {
	case NORMAL_BAT:
		if (bat_id == 0) {
			mapped_rx_tkid = rx_tkid;
		} else if (bat_id == 1) {
			mapped_rx_tkid = rx_tkid - NOA_MD_RX_NOA_NORMAL_BAT1_BASE;
		} else {
			NCP_MD_RX_ERROR("Invalid bat_id for DPMAIF_BAT");
		}
		break;
	case FRAG_BAT:
		if (bat_id == 0) {
			mapped_rx_tkid = rx_tkid - NOA_MD_RX_NOA_FRAG_BAT0_BASE;
		} else if (bat_id == 1) {
			mapped_rx_tkid = rx_tkid - NOA_MD_RX_NOA_FRAG_BAT1_BASE;
		} else {
			NCP_MD_RX_ERROR("Invalid bat_id for DPMAIF_FRAG");
		}
		break;
	default:
		break;
	}

	return mapped_rx_tkid;
}

#ifdef linux
static void noa_ncp_md_rx_rxq_pit_cache_memory_flush(
		struct noa_rx_queue *rxq, unsigned short cnt)
{
	unsigned int cur_pit = rxq->pit_rd_idx;
	dma_addr_t cache_start_addr;

	/* flush pit base memory cache for read pit data */
	cache_start_addr =
		rxq->pit_dma_addr + (sizeof(*rxq->pit_base) * cur_pit);

	if ((cur_pit + cnt) <= rxq->pit_cnt) {
		dma_sync_single_for_cpu(DCB_TO_DEV(rxq->dcb), cache_start_addr,
					sizeof(*rxq->pit_base) * cnt, DMA_FROM_DEVICE);

	} else {
		dma_sync_single_for_cpu(DCB_TO_DEV(rxq->dcb), cache_start_addr,
					sizeof(*rxq->pit_base) * (rxq->pit_cnt - cur_pit),
			DMA_FROM_DEVICE);

		dma_sync_single_for_cpu(DCB_TO_DEV(rxq->dcb), rxq->pit_dma_addr,
					sizeof(*rxq->pit_base) * (cur_pit + cnt - rxq->pit_cnt),
			DMA_FROM_DEVICE);
	}
}
#endif

// Refer to mtk_dpmaif_ring_buf_get_next_idx
static inline unsigned int noa_ncp_md_rx_ring_next_idx(
	unsigned int buf_len,
	unsigned int buf_idx)
{
	buf_idx++;

	return buf_idx < buf_len ? buf_idx : 0;
};

// Refer to mtk_dpmaif_ring_buf_readable
static inline unsigned int noa_ncp_md_rx_ring_buf_readable(
	unsigned int total_cnt,
	unsigned int rd_idx,
	unsigned int wr_idx)
{
	unsigned int pkt_cnt;

	if (wr_idx >= rd_idx)
		pkt_cnt = wr_idx - rd_idx;
	else
		pkt_cnt = total_cnt + wr_idx - rd_idx;

	return pkt_cnt;
}

static void noa_ncp_md_rx_rxq_pit_invalidate_cache(const struct noa_rx_queue *rxq,
						   unsigned short cnt)
{
	unsigned int cur_pit = rxq->pit_rd_idx;
	struct dpmaif_pd_pit *cache_start_addr;

	if (unlikely(cnt == 0 || cnt > rxq->pit_cnt)) {
		PW_CHECK(1);
		return;
	}

	cache_start_addr = (struct dpmaif_pd_pit *)rxq->pit_dpa_base + cur_pit;

	if ((cur_pit + cnt) <= rxq->pit_cnt) {
		InvalidateDCache(cache_start_addr, sizeof(struct dpmaif_pd_pit) * cnt);
	} else {
		InvalidateDCache(cache_start_addr,
				 sizeof(struct dpmaif_pd_pit) * (rxq->pit_cnt - cur_pit));

		InvalidateDCache((struct dpmaif_pd_pit *)rxq->pit_dpa_base,
				 sizeof(struct dpmaif_pd_pit) * (cur_pit + cnt - rxq->pit_cnt));
	}
}

static int noa_ncp_md_rx_get_rx_info(
	void *pit,
	struct noa_rx_info *rx_info,
	u32 pit_seq_expect,
	u8 q_id)
{
	struct dpmaif_pd_pit *pd_pit = (struct dpmaif_pd_pit *)pit;
	int ret = -DATA_PIT_SEQ_CHK_FAIL;
	struct dpmaif_msg_pit *msg_pit;
	u64 dma_addr;
	u32 cnt = 0;
	uint32_t low_address = 0x0, high_address = 0x0;

	/* The longest check time is 2ms */
	do {
		rx_info->pit_pd_seq = FIELD_GET(PIT_PD_SEQ, le32_to_cpu(pd_pit->pd_footer));
		if (rx_info->pit_pd_seq == pit_seq_expect) {
			ret = 0;
			break;
		}
		InvalidateDCache(pd_pit, sizeof(struct dpmaif_pd_pit));
		// TODO: b/433062662 - Use Micro second for the PIT polling steps
		pw::this_thread::sleep_for(std::chrono::milliseconds(DPMAIF_POLL_STEP));
	} while (++cnt < DPMAIF_POLL_PIT_CNT_MAX);

	if (unlikely(ret < 0))
		goto out;

	rx_info->msg_pit = FIELD_GET(PIT_PD_PKT_TYPE, le32_to_cpu(pd_pit->pd_header));
	if (rx_info->msg_pit) {
		msg_pit = (struct dpmaif_msg_pit *)pit;
		rx_info->pit_msg_chnl_id = FIELD_GET(PIT_MSG_CHNL_ID, le32_to_cpu(msg_pit->dword1));
		rx_info->pit_msg_checksum = FIELD_GET(PIT_MSG_CHECKSUM,
						      le32_to_cpu(msg_pit->dword1));
		rx_info->pit_msg_err = FIELD_GET(PIT_MSG_ERR, le32_to_cpu(msg_pit->dword1));
		rx_info->pit_msg_dp = FIELD_GET(PIT_MSG_DP, le32_to_cpu(msg_pit->dword1));
		rx_info->pit_msg_hash = FIELD_GET(PIT_MSG_HASH, le32_to_cpu(msg_pit->dword3));
		rx_info->pit_msg_pro = FIELD_GET(PIT_MSG_PRO, le32_to_cpu(msg_pit->dword3));
		rx_info->pit_msg_ip = FIELD_GET(PIT_MSG_IP, le32_to_cpu(msg_pit->dword4));
	} else {
		rx_info->normal_bat = FIELD_GET(PIT_PD_BUF_TYPE, le32_to_cpu(pd_pit->pd_header));
		rx_info->pit_pd_cur_bid = (FIELD_GET(PIT_PD_H_BID,
				le32_to_cpu(pd_pit->pd_footer)) << 13) +
				FIELD_GET(PIT_PD_BUF_ID, le32_to_cpu(pd_pit->pd_header));
		rx_info->pit_pd_data_len = FIELD_GET(PIT_PD_DATA_LEN,
						     le32_to_cpu(pd_pit->pd_header));
		rx_info->pit_pd_hd_offset = FIELD_GET(PIT_PD_HD_OFFSET,
						      le32_to_cpu(pd_pit->pd_footer)) << 2;
		rx_info->pit_continue = FIELD_GET(PIT_PD_CONT,
						  le32_to_cpu(pd_pit->pd_header));
		dma_addr = le32_to_cpu(pd_pit->addr_high);
		rx_info->pit_pd_dma_addr = (dma_addr << 32) + le32_to_cpu(pd_pit->addr_low);
		high_address = le32_to_cpu(pd_pit->addr_high);
		low_address = le32_to_cpu(pd_pit->addr_low);
		NCP_MD_RX_DEBUG(
			"normal_bat:%u, bid:%u, len:%u, offset:%u, cont:%u, addr_high:0x%llx, dma_addr:0x%llx",
			rx_info->normal_bat, rx_info->pit_pd_cur_bid, rx_info->pit_pd_data_len,
			rx_info->pit_pd_hd_offset, rx_info->pit_continue, dma_addr, rx_info->pit_pd_dma_addr);
	}

out:
	return ret;
}

static void noa_ncp_md_rx_commit_and_complete(struct noa_rx_queue *rxq, noa_ring_producer *ring,
					      unsigned int pit_rd_idx, uint32_t pit_seq_expect,
					      bool success)
{
	if (success) {
		rxq->pit_rd_idx = pit_rd_idx;
		rxq->pit_seq_expect = pit_seq_expect;
		noa_ring_complete_processing(ring);
	} else {
		NCP_MD_RX_ERROR(
			"rxq[%u] Transaction failed or aborted! Pending writes need rollback. TODO: implement rollback",
			rxq->id);
	}
}

static int noa_ncp_md_rx_data_collect_internal(
	struct noa_rx_queue *rxq,
	int pit_cnt,
	unsigned int *pkt_cnt)
{
	unsigned int recv_pkt_cnt = 0;
	uint32_t doorbell_cnt = 0;
	uint32_t bat_idx, rx_tkid;
	uint16_t mapped_rx_tkid;
	struct dpmaif_pd_pit *pit_info;
	struct dpmaif_msg_pit *msg_pit;
	struct noa_rx_info rx_info;
	struct noa_bat_ring *bat_ring;
	struct noa_rx_record *rx_record = &rxq->rx_record;
	int ret;
	struct noa_modem_rx_vendor_msg_pd_desc raw_desc = {};
	struct noa_modem_rx_vendor_msg_pd_desc *p_desc_msg_pd = &raw_desc;
	struct noa_modem_rx_vendor_pd_desc raw_pd_desc = {};
	struct noa_modem_rx_vendor_pd_desc *p_desc_pd = &raw_pd_desc;
	// multiple noa rx rings
	noa_ring_producer *ring = &g_md_fw->rx->rx_ring[rxq->id + kNoaModemRingRxq0].ring;
	uint32_t data_dma_addr_high, data_dma_addr_low;
	unsigned int data_len, pit_hd_offset;
	unsigned int noa_ring_cnt = 0;
	unsigned int rx_cnt;
	uint32_t switch_cmd;

	/* Local variables to track the progress of ring processing.
	 * `pit_rd_process_idx` and `local_pit_seq_expect` are used to iterate through
	 * descriptors in the loop.
	 * `committed_pit_rd_idx` and `committed_pit_seq_expect` store the state at
	 * the last successfully completed packet boundary. These are used to atomically
	 * update the global state only when a full LRO packet is processed.
	 */
	unsigned int pit_rd_process_idx = rxq->pit_rd_idx;
	uint32_t local_pit_seq_expect = rxq->pit_seq_expect;
	unsigned int committed_pit_rd_idx = rxq->pit_rd_idx;
	uint32_t committed_pit_seq_expect = rxq->pit_seq_expect;

	switch_cmd = noa_ncp_md_rx_get_switch_command(g_md_fw->rx);
	NCP_MD_RX_DEBUG("switch command: %d", switch_cmd);

	NCP_MD_RX_DEBUG("PIT rxq info: pit_total=[%d], rxq->id:[%d], "
		       "rd_idx=[%d], wr_idx=[%d], rel_rd_idx=[%d]",
		       rxq->pit_cnt, rxq->id, rxq->pit_rd_idx, rxq->pit_wr_idx,
		       rxq->pit_rel_rd_idx);
#ifdef linux
	// pit cache flush for driver mode
	if (rxq->attr & DPMAIFQ_ATTR_PIT_CACHED)
		noa_ncp_md_rx_rxq_pit_cache_memory_flush(rxq, pit_cnt);

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	prefetch(rxq);
#endif
#endif

	noa_ncp_md_rx_rxq_pit_invalidate_cache(rxq, pit_cnt);

	/* Call begin_processing before loop */
	ret = noa_ring_begin_processing(ring);
	if (ret <= 0) {
		NCP_MD_RX_ERROR(
			"Fail to noa_ring_begin_processing at start, ret=[%d]", ret);
		return ret;
	}

	for (rx_cnt = 0; rx_cnt < pit_cnt; rx_cnt++) {
		NCP_MD_RX_DEBUG("rxq[%d]->pit_rd_process_idx=[%d]", rxq->id, pit_rd_process_idx);

		if (noa_ring_is_full(ring)) {
			NCP_MD_RX_DEBUG("noa_ring[%d]_is_full", kNoaModemRingRxq0 + rxq->id);
			goto out;
		}

		/* Pit sequence check. */
#ifdef linux
		pit_info = rxq->pit_base + pit_rd_process_idx;
#else
		pit_info = (struct dpmaif_pd_pit *)rxq->pit_dpa_base + pit_rd_process_idx;
#endif
		ret = noa_ncp_md_rx_get_rx_info(pit_info, &rx_info, local_pit_seq_expect, rxq->id);
		if (likely(!ret)) {
			local_pit_seq_expect++;
			if (local_pit_seq_expect >= rxq->pit_seq_max)
				local_pit_seq_expect = 0;

			rxq->pit_seq_fail_cnt = 0;
		} else {
			NCP_MD_RX_ERROR("Failed to check rxq%u pit seq, cur_seq(%u) != exp_seq(%u)",
					rxq->id, rx_info.pit_pd_seq, local_pit_seq_expect);

			rxq->pit_seq_fail_cnt++;
			if (rxq->pit_seq_fail_cnt >= DPMAIF_PIT_SEQ_CHECK_FAIL_CNT) {
				rxq->pit_seq_fail_cnt = 0;
				NCP_MD_RX_ERROR("return -DATA_FLOW_CHK_ERR");
				noa_ncp_md_rx_commit_and_complete(rxq, ring, committed_pit_rd_idx,
								  committed_pit_seq_expect, false);
				return -DATA_FLOW_CHK_ERR;
			}
			break;
		}
		// Save rx_info for debug purpose.
		rxq->rx_info = &rx_info;

		/* Handle message pit. */
		if (rx_info.msg_pit) {
			if (!rx_record->is_msg_pit_recv) {
				/* Set message pit to NOA embedded vendor desc */
				struct noa_modem_vendor_msg_pit *msg = &p_desc_msg_pd->msg;
				struct noa_md_shmem_layout *shmem =
					(struct noa_md_shmem_layout *)g_md_fw->shared_mem_info.addr;
				struct noa_rx_queue_info *rxq_info = &shmem->rxqs[rxq->id];
				msg_pit = (struct dpmaif_msg_pit *)pit_info;
				msg->dword1 = msg_pit->dword1;
				msg->dword2 = msg_pit->dword2;
				msg->dword3 = msg_pit->dword3;
				msg->dword4 = msg_pit->dword4;
				rx_record->is_msg_pit_recv = true;
				rx_record->is_previous_msg_pit = true;
				rxq_info->pkt_cnt++;

				NCP_MD_RX_DEBUG("Receive msg_pit, pit_rd_idx=[%d], pkt_cnt=[%u]",
						pit_rd_process_idx, rxq_info->pkt_cnt);

			} else {
				NCP_MD_RX_ERROR("Invalid pit, rxq[%u] two continuous message pit",
						rxq->id);
				noa_ncp_md_rx_commit_and_complete(rxq, ring, committed_pit_rd_idx,
								  committed_pit_seq_expect, false);
				return -DATA_FLOW_CHK_ERR;
			}
		} else {
			data_dma_addr_high = le32_to_cpu(pit_info->addr_high);
			data_dma_addr_low = le32_to_cpu(pit_info->addr_low);
			data_len = FIELD_GET(PIT_PD_DATA_LEN, le32_to_cpu(pit_info->pd_header));
			pit_hd_offset =
				FIELD_GET(PIT_PD_HD_OFFSET, le32_to_cpu(pit_info->pd_footer)) << 2;

			NCP_MD_RX_DEBUG(
				"data_dma_addr_high=[0x%lx], data_dma_addr_low=[0x%lx], "
				"data_len=[%u], pit_hd_offset=[%u]",
				data_dma_addr_high, data_dma_addr_low, data_len, pit_hd_offset);

			if (rx_record->is_msg_pit_recv) {
				/* Handle normal pit or frag pit. */
				bat_idx = rx_info.pit_pd_cur_bid;

				if (!rx_info.normal_bat) {
					bat_ring = &g_md_fw->rx->bat_infos[rxq->bat_ring_id]
							    .normal_bat_ring;
				} else {
					// TODO: Handle frag pit
					NCP_MD_RX_INFO("PIT frag_bat");
				}
				rx_tkid = bat_ring->rx_tkid_info.rx_tkid[bat_idx];
				mapped_rx_tkid = noa_ncp_md_bat_rx_tkid_map(
					bat_ring->id, bat_ring->type, rx_tkid);

				NCP_MD_RX_DEBUG(
					"rxq%u bat_idx=[%d], rx_tkid=[%d], mapped_rx_tkid=[%d]",
					rxq->id, bat_idx, rx_tkid, mapped_rx_tkid);

				if (test_bit(bat_idx, bat_ring->mask_tbl)) {
					NCP_MD_RX_ERROR("mask_tbl bit should be 0 at bat_idx=[%d]",
							bat_idx);
				}
				set_bit(bat_idx, bat_ring->mask_tbl);

				if (rx_record->is_previous_msg_pit) {
					struct noa_modem_vendor_pd_pit *pd = &p_desc_msg_pd->pd;
					pd->pd_header = pit_info->pd_header;
					pd->addr_low = pit_info->addr_low;
					pd->addr_high = pit_info->addr_high;
					pd->pd_footer = pit_info->pd_footer;

					/* Use rx_tkid to replace bid */
					pd->pd_header &= ~(GENMASK(15, 3));
					pd->pd_header |= (rx_tkid << 3) & GENMASK(15, 3);
					pd->pd_footer &= ~(GENMASK(10, 8));
					pd->pd_footer |= ((rx_tkid >> 13) << 8) & GENMASK(10, 8);

					/* Setup noa_desc and noa_ring_write */
					p_desc_msg_pd->basic.dp_low = data_dma_addr_low;
					p_desc_msg_pd->basic.dp_high = data_dma_addr_high;
					p_desc_msg_pd->basic.mode = NOAD_MODE_DATA;
					p_desc_msg_pd->basic.desc_type =
						NOA_DESC_MODEM_RX_MTK_MSG_PD;
					/* Driver simulation mode only */;
					p_desc_msg_pd->basic.dv =
						bat_ring->noa_data_addr[mapped_rx_tkid].noa_va;
					p_desc_msg_pd->basic.dl = data_len;
					// multiple noa RX rings
					p_desc_msg_pd->basic.src = NoaRingPathIdConvert(
						kNoaNetworkInterfaceModem,
						kNoaNetworkFlowDeviceToHost,
						kNoaModemRingRxq0 + rxq->id);
					p_desc_msg_pd->basic.ddone = 0;
					p_desc_msg_pd->basic.head_offset = 0;
					p_desc_msg_pd->basic.dst = NoaRingPathIdConvert(
						kNoaNetworkInterfaceNetengine,
						kNoaNetengineTunnel,
						kNoaNetengineRingData);
					p_desc_msg_pd->basic.reason = FWD_REASON_NETENGINE;
					p_desc_msg_pd->basic.cp = NOAD_COPY_DATA;
					p_desc_msg_pd->basic.tkid = rx_tkid;
					p_desc_msg_pd->basic.fk = NOAD_FEEDBACK_ENABLE;

					/* Change to support unified desc format */
					ret = noa_ring_write(
						ring, p_desc_msg_pd,
						sizeof(struct noa_modem_rx_vendor_msg_pd_desc));
					if (ret < 0) {
						NCP_MD_RX_ERROR("noa_fw_ring_write, ret=[%d]", ret);
					}

					rx_record->is_previous_msg_pit = false;
				} else {
					struct noa_modem_vendor_pd_pit *pd = &p_desc_pd->pd;
					pd->pd_header = pit_info->pd_header;
					pd->addr_low = pit_info->addr_low;
					pd->addr_high = pit_info->addr_high;
					pd->pd_footer = pit_info->pd_footer;

					/* Use rx_tkid to replace bid */
					pd->pd_header &= ~(GENMASK(15, 3));
					pd->pd_header |= (rx_tkid << 3) & GENMASK(15, 3);
					pd->pd_footer &= ~(GENMASK(10, 8));
					pd->pd_footer |= ((rx_tkid >> 13) << 8) & GENMASK(10, 8);

					NCP_MD_RX_DEBUG(
						"LRO payload PIT only, "
						"rxq%d, bid=[%u], mapped_rx_tkid=[%u]",
						rxq->id, bat_idx, mapped_rx_tkid);

					/* Setup noa_desc and noa_ring_write */
					p_desc_pd->basic.dp_low = data_dma_addr_low;
					p_desc_pd->basic.dp_high = data_dma_addr_high;
					p_desc_pd->basic.mode = NOAD_MODE_DATA;
					p_desc_pd->basic.desc_type = NOA_DESC_MODEM_RX_MTK_PD;
					/* Driver simulation mode only */;
					p_desc_pd->basic.dv =
						bat_ring->noa_data_addr[mapped_rx_tkid].noa_va;
					p_desc_pd->basic.dl = data_len + pit_hd_offset;
					// multiple noa RX rings
					p_desc_pd->basic.src = NoaRingPathIdConvert(
						kNoaNetworkInterfaceModem,
						kNoaNetworkFlowDeviceToHost,
						kNoaModemRingRxq0 + rxq->id);
					p_desc_pd->basic.ddone = 0;
					p_desc_pd->basic.head_offset = 0;
					p_desc_pd->basic.dst = NoaRingPathIdConvert(
						kNoaNetworkInterfaceNetengine,
						kNoaNetengineTunnel,
						kNoaNetengineRingData);
					p_desc_pd->basic.reason = FWD_REASON_NETENGINE;
					p_desc_pd->basic.cp = NOAD_COPY_DATA;
					p_desc_pd->basic.tkid = rx_tkid;
					p_desc_pd->basic.fk = NOAD_FEEDBACK_ENABLE;

					/* Change to support unified desc format */
					ret = noa_ring_write(
						ring, p_desc_pd,
						sizeof(struct noa_modem_rx_vendor_pd_desc));
					if (ret < 0) {
						NCP_MD_RX_ERROR(
							"pit_continue=1, noa_fw_ring_write, "
							"ret=[%d]",
							ret);
					}

					rx_record->is_previous_msg_pit = false;
				}

				if (!rx_info.pit_continue) {
					recv_pkt_cnt++;
					rx_record->is_msg_pit_recv = false;
				}
			} else {
				NCP_MD_RX_ERROR(
					"Invalid pit[%d], rxq[%u] no msg pit received before pd pit"
					" receive",
					pit_rd_process_idx, rxq->id);
				noa_ncp_md_rx_commit_and_complete(rxq, ring, committed_pit_rd_idx,
								  committed_pit_seq_expect, false);
				return -DATA_FLOW_CHK_ERR;
			}
		}
		doorbell_cnt++;
		noa_ring_cnt++;
		pit_rd_process_idx = noa_ncp_md_rx_ring_next_idx(rxq->pit_cnt, pit_rd_process_idx);

		if (!rx_record->is_msg_pit_recv) {
			recv_pkt_cnt++;
			committed_pit_rd_idx = pit_rd_process_idx;
			committed_pit_seq_expect = local_pit_seq_expect;
		}

		if (noa_ring_cnt >= NOA_FW_RING_BUDGET && (!rx_record->is_msg_pit_recv)) {
			NCP_MD_RX_DEBUG("NOA ring budget [%u] met for rxq[%d]. noa_ring_cnt=%u.",
				       NOA_FW_RING_BUDGET, rxq->id, noa_ring_cnt);
			/**
			 * Because the budget limit is met, we break out of this task to yield the CPU.
			 * This allows the task to be rescheduled, preventing it from consuming excessive CPU time.
			 */
			break;
		}

		if (!rx_record->is_msg_pit_recv && (switch_cmd == NOA_MD_SWITCH_CMD_EXCHANGE_STATE))
			break;
	}

out:
	if (rx_record->is_msg_pit_recv) {
		NCP_MD_RX_ERROR("Stopping mid-LRO! rxq[%u] is_msg_pit_recv=1. "
				"This may cause data corruption or loss. "
				"TODO: Implement rollback mechanism for partial LRO writes.",
				rxq->id);
	}
	noa_ncp_md_rx_commit_and_complete(rxq, ring, committed_pit_rd_idx, committed_pit_seq_expect,
					  true);

	NCP_MD_RX_INFO("doorbell_cnt=[%d]", doorbell_cnt);
	if (doorbell_cnt > 0) {
		// Actually, NCP now trigger doorbell directly
		// NCP didn't use doorbell thread
		// pit_rel_rd_idx will always equals to pit_rd_idx
		// For switching back to the DPMAIF mode
		// If adding doorbell cnt more than pit_cnt, accessing CSR will timeout
		rxq->pit_rel_rd_idx += doorbell_cnt;
		if (rxq->pit_rel_rd_idx >= rxq->pit_cnt) {
			rxq->pit_rel_rd_idx -= rxq->pit_cnt;
		}

		g_md_fw->hif->GetPowerManager().DeepSleepLock();
		if (!g_md_fw->hif->GetPowerManager().DeepSleepWaitComplete().ok()) {
			NCP_MD_RX_ERROR("Failed to wait DeepSleepLock");
			g_md_fw->hif->GetPowerManager().DeepSleepUnlock();
			return -1;
		}
		ret = NOA_MD_SEND_DOORBELL(NOA_MD_DPMAIF_PIT, rxq->id, doorbell_cnt);
		g_md_fw->hif->GetPowerManager().DeepSleepUnlock();
		if (unlikely(ret < 0)) {
			NCP_MD_RX_ERROR("PIT doorbell fail!");
		}
	}

	*pkt_cnt = recv_pkt_cnt;

	return ret;
}

static int noa_ncp_md_rx_poll_rx_pit(struct noa_rx_queue *rxq)
{
	unsigned int sw_rd_idx, hw_wr_idx;
	unsigned int pit_cnt;
	int ret;

	sw_rd_idx = rxq->pit_rd_idx;
	ret = NOA_MD_GET_RING_INDEX(NOA_MD_DPMAIF_PIT_WIDX, rxq->id);
	if (unlikely(ret < 0)) {
		NCP_MD_RX_ERROR("Failed to read rxq[%u] hw pit_wr_idx, ret=%d", rxq->id, ret);
		goto out;
	}

	hw_wr_idx = ret;
	pit_cnt = noa_ncp_md_rx_ring_buf_readable(rxq->pit_cnt, sw_rd_idx, hw_wr_idx);
	rxq->pit_wr_idx = hw_wr_idx;

	return pit_cnt;

out:
	return ret;
}

static int noa_ncp_md_rx_data_collect(struct noa_rx_queue *rxq, unsigned int *pkt_cnt)
{
	unsigned int pit_cnt;
	int ret = 0;

	pit_cnt = noa_ncp_md_rx_ring_buf_readable(rxq->pit_cnt, rxq->pit_rd_idx, rxq->pit_wr_idx);
	/* only enable rxq->pit_poll_enable will polling pit */
	if (rxq->pit_poll_enable && !pit_cnt) {
		ret = noa_ncp_md_rx_poll_rx_pit(rxq);
		if (unlikely(ret < 0))
			return ret;

		pit_cnt = ret;
	}

	/* Collect rx packets. */
	if (likely(pit_cnt > 0)) {
		ret = noa_ncp_md_rx_data_collect_internal(rxq, pit_cnt, pkt_cnt);
		if (ret <= -DATA_DL_ONCE_MORE) {
			ret = -DATA_DL_ONCE_MORE;
			NCP_MD_RX_ERROR("rxq[%d], ret[%d]", rxq->id, ret);
		} else if (ret <= -DATA_ERR_STOP_MAX) {
			ret = -DATA_ERR_STOP_MAX;
			NCP_MD_RX_ERROR("rxq[%d], ret[%d]", rxq->id, ret);
		} else {
			ret = 0;
		}
	}

	return ret;
}

static int noa_ncp_md_rx_data_collect_more(struct noa_rx_queue *rxq, unsigned int *work_done)
{
	uint64_t time_limit = jiffies + msecs_to_jiffies(2);
	unsigned int total_pkt_cnt = 0, pkt_cnt;
	int ret = 0;

	do {
		if (time_after_eq(jiffies, time_limit)) {
			ret = -DATA_DL_ONCE_MORE;
			break;
		}

		pkt_cnt = 0;
		ret = noa_ncp_md_rx_data_collect(rxq, &pkt_cnt);
		total_pkt_cnt += pkt_cnt;
		if (ret < 0)
			break;
	} while (pkt_cnt > 0 && rxq->started);

	*work_done = total_pkt_cnt;

	return ret;
}

void noa_ncp_md_rx_done_task(unsigned long data)
{
	struct noa_rx_queue *rxq = (struct noa_rx_queue *)data;
	unsigned int work_done = 0;
	int ret = 0;
	int budget = NOA_FW_RING_SIZE;
	uint32_t switch_cmd;
	int pit_widx;

	switch_cmd = noa_ncp_md_rx_get_switch_command(g_md_fw->rx);

	if (likely(rxq->started) || (switch_cmd == NOA_MD_SWITCH_CMD_EXCHANGE_STATE)) {
		if (rxq->pit_poll_enable)
			ret = noa_ncp_md_rx_data_collect_more(rxq, &work_done);
		else
			ret = noa_ncp_md_rx_data_collect(rxq, &work_done);

		if (ret == -DATA_DL_ONCE_MORE) {
			NCP_MD_RX_ERROR("rxq[%d], ret[%d]", rxq->id, ret);
			work_done = budget;
		} else {
			if (unlikely(ret == -DATA_ERR_STOP_MAX)) {
				rxq->started = false;
				NCP_MD_RX_ERROR("rxq[%d], ret[%d]", rxq->id, ret);
			}
			if (work_done >= budget)
				work_done = budget - 1;
		}
	} else {
		NCP_MD_RX_ERROR("rxq[%d],  rxq->started=[%d]",
			rxq->id, rxq->started);
	}

	if (work_done < budget) {
#ifdef linux
		__pm_wakeup_event(rxq->ws, jiffies_to_msecs(HZ));
#endif
		NOA_MD_INTERRUPT_COMPLETE(NOA_MD_DPMAIF_INTR_DL_DONE, rxq->id, 0);
		NCP_MD_RX_INFO("pit(%u): w=%u,r=%u,rel=%u",
			rxq->id,
			rxq->pit_wr_idx,
			rxq->pit_rd_idx,
			rxq->pit_rel_rd_idx);
	} else {
		NCP_MD_RX_ERROR("rxq[%d], work_done[%u] >= budget[%u]",
			rxq->id, work_done, budget);
	}

	/*
	 * In interrupt mode, if the tasklet budget was exhausted (internal loop break),
	 * or if new data arrived during processing, we must explicitly check for
	 * remaining data and reschedule ourselves. This prevents a potential deadlock
	 * where the interrupt is unmasked but no new edge triggers the handler.
	 */
	if (noa_ncp_md_rx_poll_rx_pit(rxq) > 0) {
		tasklet_schedule(&rxq->ncp_md_rx_done_task);
	}
}

void noa_ncp_md_rx_stop_rxq(struct noa_md_fw_rx *rx)
{
	NCP_MD_RX_INFO("[enter, md_fw_rx=[0x%p]", rx);
	struct noa_rx_queue *rxq;
	int i;

	if (unlikely(!rx)) {
		NCP_MD_RX_ERROR("fw_rx is NULL");
		return;
	}

	for (i = 0; i < kMaxDlQueueSize; i++) {
		rxq = &rx->dpmaif_rxqs[i];
		rxq->started = false;

		NCP_MD_RX_INFO("pit[%u]: w=[%u], r=[%u], rel=[%u]",
			rxq->id,
			rxq->pit_wr_idx,
			rxq->pit_rd_idx,
			rxq->pit_rel_rd_idx);
	}
}

// noa_ncp_md_rx_dpmaif_irq_rx_done reference to mtk_dpmaif_irq_rx_done
void noa_ncp_md_rx_dpmaif_irq_rx_done(
		struct noa_md_fw *md_fw, unsigned int q_id)
{
	NCP_MD_RX_INFO("enter, md_fw=0x%p", md_fw);

	struct noa_md_fw_rx *rx;
	struct noa_rx_queue *rxq;
	int pit_widx;

	if (!md_fw) {
		NCP_MD_RX_ERROR("md_fw is null");
		return;
	}

	rx = md_fw->rx;
	if (!rx) {
		NCP_MD_RX_ERROR("rx is null");
		return;
	}
#ifdef linux
	if (!rx->dpmaif_rxqs) {
		NCP_MD_RX_ERROR("rx->dpmaif_rxqs is null");
		return;
	}
#endif
	/* RSS: one dlq done belongs to one interrupt, and then,
	 * one interrupt will only check one dlq done status and schedule bottom half.
	 */

	rxq = &rx->dpmaif_rxqs[q_id];
	if (!rxq) {
		NCP_MD_RX_ERROR("dpmaif_rxqs is null");
		return;
	}

	rxq->started = true;
#ifdef linux
	__pm_stay_awake(rxq->ws);
#endif

	pit_widx = NOA_MD_GET_RING_INDEX(NOA_MD_DPMAIF_PIT_WIDX, rxq->id);
	if (unlikely(pit_widx < 0)) {
		NCP_MD_RX_ERROR("Failed to read rxq%u hw pit_wr_idx, ret=%d",
			q_id, pit_widx);
		return;
	}

	rxq->pit_wr_idx = pit_widx;
	tasklet_schedule(&rxq->ncp_md_rx_done_task);

	NCP_MD_RX_INFO("exit");
}

#ifdef linux
void noa_ncp_md_rx_dpmaif_event_handle(
	struct noa_md_fw *md_fw,
	enum dpmaif_drv_intr_type type,
	unsigned int q_mask)
{
	NCP_MD_RX_INFO("enter, type=[%d], q_mask=[%d]", type, q_mask);
	// TODO: Check mtk_dpmaif_irq_handle & mtk_dpmaif_drv_intr_complete_t800
	// for INTR err cases
	switch (type) {
	case DPMAIF_INTR_DL_BATCNT_LEN_ERR:
		// TODO: Send DPMAIF_INTR_DL_BATCNT_LEN_ERR RPC to APC
		break;
	case DPMAIF_INTR_DL_FRGCNT_LEN_ERR:
		// TODO: Send DPMAIF_INTR_DL_FRGCNT_LEN_ERR RPC to APC
		break;
	case DPMAIF_INTR_DL_PITCNT_LEN_ERR:
		// TODO: Send DPMAIF_INTR_DL_PITCNT_LEN_ERR RPC to APC
		break;
	case DPMAIF_INTR_DL_DONE:
		NCP_MD_RX_INFO("DPMAIF_INTR_DL_DONE, q_mask=0x%x", q_mask);
		noa_ncp_md_rx_dpmaif_irq_rx_done(md_fw, q_mask);
		break;
	default:
		break;
	}
}
#else
void noa_ncp_md_rx_dpmaif_event_handle(
	struct noa_md_fw *md_fw,
	noa_dpmaif_drv_intr_type type,
	unsigned int q_id)
{
	struct noa_md_fw_rx *rx = md_fw->rx;
	struct noa_bat_info *bat_infos;
	struct noa_bat_ring *bat_ring;

	// TODO: Check mtk_dpmaif_irq_handle & mtk_dpmaif_drv_intr_complete_t800
	// for INTR err cases
	switch (type) {
	case NOA_DPMAIF_INTR_DL_BATCNT_LEN_ERR:
		// TODO: Send DPMAIF_INTR_DL_BATCNT_LEN_ERR RPC to APC
		NCP_MD_RX_ERROR("NOA_DPMAIF_INTR_DL_BATCNT_LEN_ERR, q_id=0x%x", q_id);
		// This q_id is for BAT_ID, q_id is 0 or 1.
		if (q_id < rx->bat_ring_num) {
			bat_infos = &rx->bat_infos[q_id];
			bat_ring = &bat_infos->normal_bat_ring;
			bat_ring->bat_cnt_err_intr_set = true;
			tasklet_schedule(&rx->rx_batcnt_len_err_task);
		} else {
			NCP_MD_RX_ERROR("Invalid BAT_ID=[%d]", q_id);
		}
		break;
	case NOA_DPMAIF_INTR_DL_FRGCNT_LEN_ERR:
		// TODO: Send DPMAIF_INTR_DL_FRGCNT_LEN_ERR RPC to APC
		NCP_MD_RX_ERROR("NOA_DPMAIF_INTR_DL_FRGCNT_LEN_ERR, q_id=0x%x", q_id);
		break;
	case NOA_DPMAIF_INTR_DL_PITCNT_LEN_ERR:
		// TODO: Send DPMAIF_INTR_DL_PITCNT_LEN_ERR RPC to APC
		NCP_MD_RX_ERROR("NOA_DPMAIF_INTR_DL_PITCNT_LEN_ERR, q_id=0x%x", q_id);
		break;
	case NOA_DPMAIF_INTR_DL_DONE:
		NCP_MD_RX_INFO("DPMAIF_INTR_DL_DONE, q_id=0x%x", q_id);
		// This q_id is for RXQ_ID, q_id is 0, 1 or 2.
		if (q_id < rx->rxq_cnt) {
			noa_ncp_md_rx_dpmaif_irq_rx_done(md_fw, q_id);
		} else {
			NCP_MD_RX_ERROR("Invalid RXQ_ID=[%d]", q_id);
		}
		break;
	default:
		break;
	}
}
#endif

/**
 * @brief Checks if the BAT ring is full.
 *
 * A ring is considered full if the next write index would be the same as
 * the current read index. This leaves one slot empty to distinguish
 * between a full and an empty ring.
 *
 * @param bat_ring The BAT ring to check.
 * @return True if the ring is full, false otherwise.
 */
static inline bool noa_ncp_md_rx_is_bat_ring_full(
	const struct noa_bat_ring *bat_ring)
{
	uint32_t next_wr_idx = noa_ncp_md_rx_ring_next_idx(
		bat_ring->bat_cnt,
		bat_ring->bat_wr_idx);
	return (next_wr_idx == bat_ring->bat_rd_idx);
}

/**
 * @brief Checks if a specific slot in the BAT ring is available for writing.
 *
 * @param bat_ring The BAT ring.
 * @param slot_idx The index of the slot to check.
 * @return True if the slot is marked as available in the mask table, false
 * otherwise.
 */
static inline bool noa_ncp_md_rx_is_slot_available(
	const struct noa_bat_ring *bat_ring,
	uint32_t slot_idx)
{
	return test_bit(slot_idx, bat_ring->mask_tbl);
}

/**
 * @brief Retrieves a free token ID and its associated buffer data from the pool.
 *
 * This function "pops" the next available entry from the free pool.
 *
 * @param tkid_info Pointer to the token ID information structure.
 * @param[out] tkid Pointer to store the retrieved token ID.
 * @param[out] data_addr Pointer to store the retrieved NOA data address.
 * @param[out] bat_addr Pointer to store the retrieved hardware buffer address.
 * @param bat_ring The BAT ring.
 */
static void noa_ncp_md_rx_get_free_tkid_and_data(
	struct noa_rx_tkid_info *tkid_info,
	uint16_t *tkid,
	uint64_t *data_addr,
	struct dpmaif_bat *bat_addr,
	const struct noa_bat_ring *bat_ring)
{
	uint32_t fore_idx = tkid_info->rx_tkid_free_fore;

	*tkid = tkid_info->free_pool[fore_idx].rx_tkid;
	*data_addr = tkid_info->free_pool[fore_idx].noa_data_addr;
	*bat_addr = tkid_info->free_pool[fore_idx].bat;

	/* Advance the pool's head pointer */
	tkid_info->rx_tkid_free_fore =
		noa_ncp_md_rx_ring_next_idx(
			bat_ring->bat_cnt,  // Note: Assuming bat_cnt is available here
			fore_idx);
}

/**
 * @brief Fills a specific slot in the BAT ring with new buffer information.
 *
 * This function updates the software and hardware-facing parts of the ring
 * for a single entry. It uses memcpy_toio for safe hardware access.
 *
 * @param bat_ring The BAT ring to update.
 * @param tkid The token ID to associate with the slot.
 * @param data_addr The NOA data address.
 * @param bat_addr The hardware buffer address.
 */
static void noa_ncp_md_rx_fill_bat_slot(
	struct noa_bat_ring *bat_ring,
	uint16_t tkid,
	uint64_t data_addr,
	const struct dpmaif_bat *bat_addr)
{
	uint32_t bat_idx = bat_ring->bat_wr_idx;
	uint16_t mapped_tkid;

	/* Update software mapping: bat_idx <-> tkid */
	bat_ring->rx_tkid_info.rx_tkid[bat_idx] = tkid;

	memcpy((struct dpmaif_bat *)bat_ring->bat_dpa_base + bat_idx,
		    bat_addr, sizeof(*bat_addr));
	FLUSH_DC_CACHE((dpmaif_bat *)bat_ring->bat_dpa_base + bat_idx, sizeof(dpmaif_bat));

	/* Update NOA-specific virtual address mapping */
	mapped_tkid = noa_ncp_md_bat_rx_tkid_map(
		bat_ring->id,
		bat_ring->type,
		tkid);
	bat_ring->noa_data_addr[mapped_tkid].noa_va = data_addr;

	/* Mark the slot as used */
	clear_bit(bat_idx, bat_ring->mask_tbl);

	/* Advance the ring's write pointer */
	bat_ring->bat_wr_idx = noa_ncp_md_rx_ring_next_idx(bat_ring->bat_cnt,
		bat_idx);
}

/**
 * @brief Sends a doorbell notification to the hardware if packets were reloaded.
 *
 * This function ensures that all memory writes are completed before ringing
 * the doorbell by using a write memory barrier (wmb).
 *
 * @param bat_ring The BAT ring that was updated.
 * @param count The number of reloaded packets.
 */
static void noa_ncp_md_rx_send_doorbell_if_needed(
	struct noa_bat_ring *bat_ring,
	int count)
{
	int ret;
	unsigned int ring_type;
	unsigned int widx_type;

	if (count == 0)
		return;

	g_md_fw->hif->GetPowerManager().DeepSleepLock();
	if (!g_md_fw->hif->GetPowerManager().DeepSleepWaitComplete().ok()) {
		NCP_MD_RX_ERROR("Failed to wait DeepSleepLock");
		goto end;
	}

	ring_type = (bat_ring->type == NORMAL_BAT) ? DPMAIF_BAT : DPMAIF_FRAG;
	widx_type = (bat_ring->type == NORMAL_BAT) ? NOA_MD_DPMAIF_BAT_WIDX :
						     NOA_MD_DPMAIF_FRAG_WIDX;

	ret = NOA_MD_SEND_DOORBELL(ring_type, bat_ring->id, count);
	if (unlikely(ret < 0)) {
		NCP_MD_RX_ERROR("Failed to send doorbell for BAT type %d, id %d",
				ring_type, bat_ring->id);
	}

	NCP_MD_RX_DEBUG("ring_type:%d, id:%d, wr_idx_csr:%d",
		       bat_ring->type, bat_ring->id,
		       NOA_MD_GET_RING_INDEX(widx_type, bat_ring->id));

end:
       g_md_fw->hif->GetPowerManager().DeepSleepUnlock();
}


/**
 * @brief Reloads a Buffer Address Table (BAT) ring with new buffers.
 *
 * It orchestrates the process of checking for available space and resources,
 * filling the BAT ring slots, and notifying the hardware. It is now much
 * shorter and easier to read.
 * It also includes proper locking to prevent race conditions.
 *
 * @param rx       Pointer to the main RX firmware structure.
 * @param bat_ring Pointer to the specific BAT ring to reload.
 */
static void noa_ncp_md_rx_reload_bat(struct noa_md_fw_rx *rx,
	struct noa_bat_ring *bat_ring)
{
	struct noa_rx_tkid_info *tkid_info = &bat_ring->rx_tkid_info;
	int reloaded_count = 0;

	NCP_MD_RX_DEBUG("ring_type:%d, ring_id:%d, bat_wr_idx:%d",
		bat_ring->type,
		bat_ring->id,
		bat_ring->bat_wr_idx);

	/* Protects access to shared ring and pool indices */
	spin_lock(&tkid_info->rx_tkid_lock);
	while (tkid_info->rx_tkid_free_rear !=
	       tkid_info->rx_tkid_free_fore) {
		uint32_t current_bat_idx = bat_ring->bat_wr_idx;
		uint16_t new_tkid;
		uint64_t new_data_addr;
		struct dpmaif_bat new_bat_addr;

		if (noa_ncp_md_rx_is_bat_ring_full(bat_ring)) {
			NCP_MD_RX_ERROR("Ring full (id=%d, rd=%d, wr=%d)",
					bat_ring->id, bat_ring->bat_rd_idx,
					bat_ring->bat_wr_idx);
			tasklet_schedule(&rx->rx_reload_task);
			break;
		}

		if (!noa_ncp_md_rx_is_slot_available(bat_ring, current_bat_idx)) {
			NCP_MD_RX_ERROR("Slot not available (id=%d, idx=%d)",
					bat_ring->id, current_bat_idx);
			tasklet_schedule(&rx->rx_reload_task);
			break;
		}

		/* Step 1: Get a free token and its data */
		noa_ncp_md_rx_get_free_tkid_and_data(tkid_info, &new_tkid,
			&new_data_addr, &new_bat_addr, bat_ring);

		/* Step 2: Fill the BAT slot with the new data */
		noa_ncp_md_rx_fill_bat_slot(bat_ring, new_tkid,
			      new_data_addr, &new_bat_addr);

		reloaded_count++;
	}
	NCP_MD_RX_DEBUG("bat[%d], bat_wr_idx=[%d], bat_rd_idx=[%d], "
			"rx_tkid_free_fore=[%d], rx_tkid_free_rear=[%d]",
			bat_ring->id, bat_ring->bat_wr_idx, bat_ring->bat_rd_idx,
			tkid_info->rx_tkid_free_fore, tkid_info->rx_tkid_free_rear);
	spin_unlock(&tkid_info->rx_tkid_lock);

	/* Step 3: Notify hardware if any slots were filled */
	noa_ncp_md_rx_send_doorbell_if_needed(bat_ring, reloaded_count);

	// Interrupt complete for INTR_DL_BATCNT_LEN_ERR after sending the BAT doorbell to modem
	if (bat_ring->bat_cnt_err_intr_set) {
		// TODO: We use NCP_MD_RX_ERROR for debug ROM purpose.
		// We need to change the Log Macro to NCP_MD_RX_DEBUG in the future.
		NCP_MD_RX_ERROR(
			"bat_id[%d], Interrupt complete for NOA_MD_DPMAIF_INTR_DL_BATCNT_LEN_ERR",
			bat_ring->id);
		bat_ring->bat_cnt_err_intr_set = false;
		NOA_MD_INTERRUPT_COMPLETE(NOA_MD_DPMAIF_INTR_DL_BATCNT_LEN_ERR, bat_ring->id, 0);
	}

}

void noa_ncp_md_rx_reload_bat_all(struct noa_md_fw *md_fw)
{
	struct noa_md_fw_rx *rx = md_fw->rx;
	struct noa_bat_info *bat_infos;
	struct noa_bat_ring *bat_ring;
	int bat_rd_idx;

	for (int i = 0; i < rx->bat_ring_num; i++) {
		bat_infos = &rx->bat_infos[i];
		bat_ring = &bat_infos->normal_bat_ring;
		bat_rd_idx = NOA_MD_GET_RING_INDEX(NOA_MD_DPMAIF_BAT_RIDX, bat_ring->id);
		NCP_MD_RX_INFO("batid(%d), bat_rd_idx=[%d]", i, bat_rd_idx);
		if (unlikely(bat_rd_idx < 0)) {
			NCP_MD_RX_ERROR("Failed to update normal bat_rd_idx");
		}
		bat_ring->bat_rd_idx = bat_rd_idx;
		noa_ncp_md_rx_reload_bat(rx, bat_ring);

		if (bat_infos->frag_bat_enabled) {
			bat_ring = &bat_infos->frag_bat_ring;
			bat_rd_idx = NOA_MD_GET_RING_INDEX(NOA_MD_DPMAIF_FRAG_RIDX, bat_ring->id);
			if (unlikely(bat_rd_idx < 0)) {
				NCP_MD_RX_ERROR("Failed to update graf bat_rd_idx");
			}
			bat_ring->bat_rd_idx = bat_rd_idx;
			noa_ncp_md_rx_reload_bat(rx, bat_ring);
		}
	}
}

static void noa_ncp_md_rx_reload_task(unsigned long data)
{
	struct noa_md_fw *md_fw = (struct noa_md_fw *)data;
	noa_ncp_md_rx_reload_bat_all(md_fw);
}

static int noa_ncp_md_rx_get_bat_id_by_tkid(unsigned short rx_tkid)
{
	if (rx_tkid >= NOA_MD_RX_NOA_NORMAL_BAT0_BASE &&
		rx_tkid < NOA_MD_RX_NOA_NORMAL_BAT1_BASE) {
		return DPMAIF_BAT0;
	}
	if (rx_tkid >= NOA_MD_RX_NOA_NORMAL_BAT1_BASE &&
		rx_tkid < NOA_MD_RX_NOA_MAX_BAT_BASE) {
		return DPMAIF_BAT1;
	}

	return -EINVAL;
}

static int noa_ncp_md_rx_get_bat_ring_type_by_tkid(unsigned short rx_tkid)
{
	if ((rx_tkid >= NOA_MD_RX_NOA_NORMAL_BAT0_BASE &&
		 rx_tkid < NOA_MD_RX_NOA_FRAG_BAT0_BASE) ||
		(rx_tkid >= NOA_MD_RX_NOA_NORMAL_BAT1_BASE &&
		 rx_tkid < NOA_MD_RX_NOA_FRAG_BAT1_BASE)) {
		return DPMAIF_BAT;
	}
	if ((rx_tkid >= NOA_MD_RX_NOA_FRAG_BAT0_BASE &&
		 rx_tkid < NOA_MD_RX_NOA_NORMAL_BAT1_BASE) ||
		(rx_tkid >= NOA_MD_RX_NOA_FRAG_BAT1_BASE &&
		 rx_tkid < NOA_MD_RX_NOA_MAX_BAT_BASE)) {
		return DPMAIF_FRAG;
	}

	return -EINVAL;
}

int noa_ncp_md_rx_add_tkid_to_free_pool(
	struct noa_md_fw *md_fw,
	unsigned short rx_tkid,
	u32 buf_addr_high,
	u32 buf_addr_low,
	u64 noa_data_addr)
{
	int ret = 0;
	struct noa_md_fw_rx *rx = md_fw->rx;
	struct noa_bat_info *bat_infos;
	struct noa_bat_ring *bat_ring = NULL;
	struct noa_rx_tkid_info *rx_tkid_info;
	unsigned int bat_id, bat_ring_type;
	unsigned int free_rx_tkid_cnt;

	bat_id = noa_ncp_md_rx_get_bat_id_by_tkid(rx_tkid);
	if (bat_id < 0) {
		NCP_MD_RX_ERROR("Invalid bat_id, rx_tkid:%hu", rx_tkid);
		return -EINVAL;
	}
	bat_infos = &rx->bat_infos[bat_id];
	bat_ring_type = noa_ncp_md_rx_get_bat_ring_type_by_tkid(rx_tkid);
	if (bat_ring_type < 0) {
		NCP_MD_RX_ERROR("Invalid bat_ring_type, rx_tkid:%hu", rx_tkid);
		return -EINVAL;
	}

	switch (bat_ring_type) {
	case DPMAIF_BAT:
		bat_ring = &bat_infos->normal_bat_ring;
		break;
	case DPMAIF_FRAG:
		bat_ring = &bat_infos->frag_bat_ring;
		break;
	default:
		break;
	}

	rx_tkid_info = &bat_ring->rx_tkid_info;
	spin_lock(&rx_tkid_info->rx_tkid_lock);
	rx_tkid_info->free_pool[rx_tkid_info->rx_tkid_free_rear].rx_tkid = rx_tkid;
	rx_tkid_info->free_pool[rx_tkid_info->rx_tkid_free_rear].bat.buf_addr_high = buf_addr_high;
	rx_tkid_info->free_pool[rx_tkid_info->rx_tkid_free_rear].bat.buf_addr_low = buf_addr_low;
	rx_tkid_info->free_pool[rx_tkid_info->rx_tkid_free_rear].noa_data_addr = noa_data_addr;

	rx_tkid_info->rx_tkid_free_rear =
		noa_ncp_md_rx_ring_next_idx(
			bat_ring->bat_cnt, rx_tkid_info->rx_tkid_free_rear);

	free_rx_tkid_cnt =
		noa_ncp_md_rx_ring_buf_readable(bat_ring->bat_cnt, rx_tkid_info->rx_tkid_free_fore,
						rx_tkid_info->rx_tkid_free_rear);
	spin_unlock(&rx_tkid_info->rx_tkid_lock);

	NCP_MD_RX_DEBUG("free_rx_tkid_cnt=[%d]", free_rx_tkid_cnt);

	if (free_rx_tkid_cnt >= NOA_REL_BAT_WEIGHT) {
		// Trigger NCP bat reload task
		tasklet_schedule(&rx->rx_reload_task);
	}

	return ret;
}

static int noa_ncp_md_rx_handle_refill_ring_desc(
	struct noa_md_fw *md_fw,
	struct noa_modem_rx_refill_desc *p_desc,
	unsigned int qid)
{

	unsigned short rx_tkid;
	struct dpmaif_bat bat;
	u64 noa_data_addr;
	int ret = 0;

	rx_tkid = p_desc->rx_tkid;
	bat.buf_addr_high = p_desc->modem_address_high;
	bat.buf_addr_low = p_desc->modem_address_low;
	noa_data_addr = p_desc->noa_data_addr;

	ret = noa_ncp_md_rx_add_tkid_to_free_pool(
		md_fw,
		rx_tkid,
		bat.buf_addr_high,
		bat.buf_addr_low,
		noa_data_addr);
	if (ret < 0) {
		NCP_MD_RX_ERROR("Fail to add rx_tkid into free pool");
	}

	return ret;
}

void noa_ncp_md_rx_tkid_free_poll_task(unsigned long data)
{
	struct noa_md_fw *md_fw = (struct noa_md_fw *)data;
	struct noa_md_fw_rx *rx = md_fw->rx;
	struct noa_md_apc2ncp_tx_buffer_desc *desc =
		noa_md_apc2ncp_get_tx_buffer_desc();
	int desc_count;
	int ret;
	bool refill_success = false;
	uint32_t switch_cmd;

	switch_cmd = noa_ncp_md_rx_get_switch_command(md_fw->rx);

	for (int q_index = kNoaModemRingRxRefillNormalBat0; q_index < kNoaModemApcToNcpRingMax;
		q_index++) {
		unsigned int dlq_done = rx->isr_refill_bitmask.load() & (1 << q_index);
		if (dlq_done || (switch_cmd == NOA_MD_SWITCH_CMD_EXCHANGE_STATE)) {
			noa_ring_consumer *ring = &desc[q_index].ring;
			desc_count = 0;
			if (!ring) {
				NCP_MD_RX_ERROR("ring is NULL");
				return;
			}
			NCP_MD_RX_INFO("enter, ring->name=[%s], q_index=[%u]", ring->name, q_index);
			ret = noa_ring_begin_processing(ring);
			if (ret <= 0) {
				NCP_MD_RX_ERROR("noa_ring_begin_processing, ret=[%d]", ret);
				return;
			}

			while (true) {
				struct noa_modem_rx_refill_desc *p_desc = NULL;
				uint64_t data_addr = 0;

				ret = noa_ring_read(
					ring,
					&data_addr,
					sizeof(struct noa_modem_rx_refill_desc *));
				if (!ret) {
					// clear the bit when ring is empty
					rx->isr_refill_bitmask.fetch_and(~(1 << q_index));
					break;
				} else if (ret < 0 || !data_addr) {
					NCP_MD_RX_ERROR("noa_ring_read=[%d]", ret);
					noa_ring_tail_inc(ring);
					continue;
				}
				p_desc = (struct noa_modem_rx_refill_desc *)data_addr;

				NCP_MD_RX_DEBUG("ret=[%d], p_desc=[%p]", ret, p_desc);

				ret = noa_ncp_md_rx_handle_refill_ring_desc(md_fw, p_desc, q_index);
				if (ret == 0) {
					refill_success = true;
					desc_count++;
				} else {
					NCP_MD_RX_ERROR(
						"noa_ncp_md_rx_handle_refill_ring_desc failed, "
						"ret=[%d]",
						ret);
				}

				// TODO: b/442859248: Refine the break condition
				// This should use a dymanic release BAT weight to break the loop
				// then schedule the BAT reload task
				if (desc_count == NOA_REL_BAT_WEIGHT) {
					NCP_MD_RX_INFO("break free pool loop when handling "
						       "desc_count=[%d], q_index=[%d]",
						       desc_count, q_index);
					break;
				}
			}
			noa_ring_complete_processing(ring);
		}
	}

	// Trigger NCP bat reload task if any refill was successful.
	if (refill_success) {
		tasklet_schedule(&rx->rx_reload_task);
	}

	// If there are still pending refill bits, reschedule the poll task.
	if (rx->isr_refill_bitmask.load() != 0) {
		// Trigger the task of replenishing the free pool
		// and continue reading the descriptor in the replenishment ring.
		tasklet_schedule(&rx->rx_tkid_free_poll_task);
	}

	return;
}

/**
 * noa_ncp_md_rx_update_ring_info_for_offload_path - Update modem RX rings' indexes.
 * @ap_state: Pointer to the AP state to send to the NCP.
 * @rx: Pointer to the firmware rx.
 *
 * This function is called to update modem RX rings' indexes from shared memory.
 */
void noa_ncp_md_rx_update_ring_info_for_offload_path(
		struct dpath_ap_state_payload *ap_state,
		struct noa_md_fw_rx *rx)
{
	if (unlikely(!ap_state) || unlikely(!rx) ) {
		NCP_MD_RX_ERROR("ap_state or rx is NULL");
		return;
	}

	for (uint32_t index = 0; index < kMaxDlQueueSize; index++) {
		struct rx_ring_idx *rxq_source = &ap_state->rxqs[index];
		struct noa_rx_queue *rxq_dest = &rx->dpmaif_rxqs[index];
		rxq_dest->pit_wr_idx = rxq_source->pit_wr_idx;
		rxq_dest->pit_rd_idx = rxq_source->pit_rd_idx;
		rxq_dest->pit_rel_rd_idx = rxq_source->pit_rel_rd_idx;
		rxq_dest->pit_seq_expect = rxq_source->pit_seq_expect;

		NCP_MD_RX_INFO("rxqs[%d] pit_wr_idx=[%u], pit_rd_idx=[%u], pit_rel_rd_idx=[%u] "
			       "pit_seq_expect=[%u]",
			       index, rxq_dest->pit_wr_idx, rxq_dest->pit_rd_idx,
			       rxq_dest->pit_rel_rd_idx, rxq_dest->pit_seq_expect);
	}

	for (uint32_t index = 0; index < kMaxBatInfoSize; index++) {
		struct bat_ring *bat_ring_source = &ap_state->bat_infos[index].normal_bat_ring;
		struct noa_bat_ring *bat_ring_dest = &rx->bat_infos[index].normal_bat_ring;
		bat_ring_dest->bat_wr_idx = bat_ring_source->bat_wr_idx;
		bat_ring_dest->bat_rd_idx = bat_ring_source->bat_rd_idx;
		bat_ring_dest->max_reload_cnt = bat_ring_source->max_reload_cnt;
		std::atomic_store(&bat_ring_dest->to_reload_cnt, bat_ring_source->to_reload_cnt);

		NCP_MD_RX_INFO("bat[%d] bat_wr_idx=[%u], bat_rd_idx=[%u], max_reload_cnt=[%u] "
			       "to_reload_cnt=[%u]",
			       index, bat_ring_dest->bat_wr_idx, bat_ring_dest->bat_rd_idx,
			       bat_ring_dest->max_reload_cnt,
			       std::atomic_load(&bat_ring_dest->to_reload_cnt));

		if (!rx->bat_infos[index].frag_bat_enabled) {
			continue;
		}

		struct bat_ring *frag_bat_ring_source = &ap_state->bat_infos[index].frag_bat_ring;
		struct noa_bat_ring *frag_bat_ring_dest = &rx->bat_infos[index].frag_bat_ring;
		frag_bat_ring_dest->bat_wr_idx = frag_bat_ring_source->bat_wr_idx;
		frag_bat_ring_dest->bat_rd_idx = frag_bat_ring_source->bat_rd_idx;
		frag_bat_ring_dest->max_reload_cnt = frag_bat_ring_source->max_reload_cnt;
		std::atomic_store(&frag_bat_ring_dest->to_reload_cnt,
				  frag_bat_ring_source->to_reload_cnt);
	}
}

/**
 * noa_ncp_md_rx_update_ring_info_for_direct_path - Update modem RX rings' indices.
 * @rx: Pointer to the firmware rx.
 * @ncp_state: Pointer to the NCP state to send to the AP.
 *
 * This function is called to copy the NCP's final RX ring indices to shared memory.
 */
void noa_ncp_md_rx_update_ring_info_for_direct_path(
		struct noa_md_fw_rx *rx,
		struct dpath_ncp_state_payload *ncp_state)
{
	if (unlikely(!rx) || unlikely(!ncp_state) ) {
		NCP_MD_RX_ERROR("rx or ncp_state is NULL");
		return;
	}

	for (uint32_t index = 0; index < kMaxDlQueueSize; index++) {
		struct noa_rx_queue *rxq_source = &rx->dpmaif_rxqs[index];
		struct rx_ring_idx *rxq_dest = &ncp_state->rxqs[index];
		rxq_dest->pit_wr_idx = rxq_source->pit_wr_idx;
		rxq_dest->pit_rd_idx = rxq_source->pit_rd_idx;
		rxq_dest->pit_rel_rd_idx = rxq_source->pit_rel_rd_idx;
		rxq_dest->pit_seq_expect = rxq_source->pit_seq_expect;

		NCP_MD_RX_INFO("rxqs[%d] pit_wr_idx=[%u], pit_rd_idx=[%u], pit_rel_rd_idx=[%u], "
			       "pit_seq_expect=[%u]",
			       index, rxq_dest->pit_wr_idx, rxq_dest->pit_rd_idx,
			       rxq_dest->pit_rel_rd_idx, rxq_dest->pit_seq_expect);
	}

	for (uint32_t index = 0; index < kMaxBatInfoSize; index++) {
		struct noa_bat_ring *bat_ring_source = &rx->bat_infos[index].normal_bat_ring;
		struct noa_rx_tkid_info *tkid_info = &bat_ring_source->rx_tkid_info;
		struct bat_ring *bat_ring_dest = &ncp_state->bat_infos[index].normal_bat_ring;
		bat_ring_dest->bat_wr_idx = bat_ring_source->bat_wr_idx;
		bat_ring_dest->bat_rd_idx = bat_ring_source->bat_rd_idx;
		bat_ring_dest->max_reload_cnt = bat_ring_source->max_reload_cnt;
		bat_ring_dest->to_reload_cnt = std::atomic_load(&bat_ring_source->to_reload_cnt);
		bat_ring_dest->ncp_free_pool_fore = tkid_info->rx_tkid_free_fore;
		bat_ring_dest->ncp_free_pool_rear = tkid_info->rx_tkid_free_rear;

		NCP_MD_RX_INFO(
			"bat[%d] bat_wr_idx=[%u], bat_rd_idx=[%u], max_reload_cnt=[%u], "
			"to_reload_cnt=[%u], ncp_free_pool_fore=[%u], ncp_free_pool_rear=[%u]",
			index, bat_ring_dest->bat_wr_idx, bat_ring_dest->bat_rd_idx,
			bat_ring_dest->max_reload_cnt, bat_ring_dest->to_reload_cnt,
			bat_ring_dest->ncp_free_pool_fore, bat_ring_dest->ncp_free_pool_rear);

		if (!rx->bat_infos[index].frag_bat_enabled) {
			continue;
		}

		struct noa_bat_ring *frag_bat_ring_source = &rx->bat_infos[index].frag_bat_ring;
		struct noa_rx_tkid_info *frag_tkid_info = &frag_bat_ring_source->rx_tkid_info;
		struct bat_ring *frag_bat_ring_dest = &ncp_state->bat_infos[index].frag_bat_ring;
		frag_bat_ring_dest->bat_wr_idx = frag_bat_ring_source->bat_wr_idx;
		frag_bat_ring_dest->bat_rd_idx = frag_bat_ring_source->bat_rd_idx;
		frag_bat_ring_dest->max_reload_cnt = frag_bat_ring_source->max_reload_cnt;
		frag_bat_ring_dest->to_reload_cnt =
			std::atomic_load(&frag_bat_ring_source->to_reload_cnt);
		frag_bat_ring_dest->ncp_free_pool_fore = frag_tkid_info->rx_tkid_free_fore;
		frag_bat_ring_dest->ncp_free_pool_rear = frag_tkid_info->rx_tkid_free_rear;
	}
}

static void noa_ncp_md_rx_flush_bat_ring_dcache(struct noa_bat_ring *bat_ring,
						 uint32_t index, bool is_frag) {
	const char *ring_type_str = is_frag ? "frag_bat" : "bat";

	FlushDCache(bat_ring->mask_tbl,
		    BITS_TO_LONGS(bat_ring->bat_cnt) * sizeof(*bat_ring->mask_tbl));
	NCP_MD_RX_INFO("FlushDCache for %s[%d] mask table", ring_type_str, index);

	FlushDCache(bat_ring->rx_tkid_info.rx_tkid,
		    bat_ring->bat_cnt * sizeof(*bat_ring->rx_tkid_info.rx_tkid));
	NCP_MD_RX_INFO("FlushDCache for %s[%d] rx_tkid table", ring_type_str, index);

	FlushDCache(bat_ring->rx_tkid_info.free_pool,
		    bat_ring->bat_cnt * sizeof(*bat_ring->rx_tkid_info.free_pool));
	NCP_MD_RX_INFO("FlushDCache for %s[%d] rx_tkid free pool", ring_type_str, index);

	bat_ring->rx_tkid_info.rx_tkid_free_fore = 0;
	bat_ring->rx_tkid_info.rx_tkid_free_rear = 0;
	NCP_MD_RX_INFO("Reset for %s[%d] rx_tkid free pool fore/rear index", ring_type_str, index);
}

void noa_ncp_md_rx_flush_rx_table(struct noa_md_fw_rx *rx)
{
	for (uint32_t index = 0; index < kMaxBatInfoSize; index++) {
		struct noa_bat_ring *bat_ring = &rx->bat_infos[index].normal_bat_ring;
		noa_ncp_md_rx_flush_bat_ring_dcache(bat_ring, index, false);

		if (!rx->bat_infos[index].frag_bat_enabled) {
			continue;
		}

		struct noa_bat_ring *frag_bat_ring = &rx->bat_infos[index].frag_bat_ring;
		noa_ncp_md_rx_flush_bat_ring_dcache(frag_bat_ring, index, true);
	}
}

int noa_ncp_md_rx_init(struct noa_md_fw *md_fw)
{
	struct noa_md_fw_rx *rx = md_fw->rx;
	struct noa_rx_queue *rxqs;
	int ret = 0;
	int i;

	NCP_MD_RX_INFO("enter");

	ret = rx->ring_ops->init(md_fw);
	if (ret) {
		NCP_MD_RX_ERROR("Failed to init md rx ring, ret=[%d]", ret);
		return ret;
	}

	NCP_MD_RX_INFO("exit, ret=[%d]", ret);
	for (i = 0; i < rx->rxq_cnt; i++) {
		rxqs = &rx->dpmaif_rxqs[i];
		tasklet_init(&rxqs->ncp_md_rx_done_task, noa_ncp_md_rx_done_task,
			     (unsigned long)rxqs);
	}

	/* init tasklet */
	tasklet_init(&rx->rx_reload_task, noa_ncp_md_rx_reload_task, (unsigned long)md_fw);
	/**
	 * The purpose to add a new task for rx_batcnt_len_err_task
	 * Step1: tasklet_schedule(&rx->rx_reload_task);
	 * Step2: Receive NOA_DPMAIF_INTR_DL_BATCNT_LEN_ERR
	 * Step3: NOA_DPMAIF_INTR_DL_BATCNT_LEN_ERR schedules tasklet_schedule(&rx->rx_reload_task);
	 * In this case, rx_reload_task in step3 will not be scheduled
	 * if step1's rx_reload_task is running.
	 * So it may have a race condition that there is no interrupt complete for bat_len_err.
	 */
	tasklet_init(&rx->rx_batcnt_len_err_task, noa_ncp_md_rx_reload_task, (unsigned long)md_fw);
	tasklet_init(&rx->rx_tkid_free_poll_task, noa_ncp_md_rx_tkid_free_poll_task,
		     (unsigned long)md_fw);
	return ret;
}

void noa_ncp_md_rx_activate(struct noa_md_fw *md_fw)
{
	struct noa_md_fw_rx *rx = md_fw->rx;
	int ret;
	int i;

	NCP_MD_RX_INFO("enter");

	for (i = 0; i < kNoaModemRingRxDataEnd; i++) {
		ret = rx->ring_ops->activate(&rx->rx_ring[i].ring, i);
		if (ret) {
			NCP_MD_RX_ERROR("Failed to activate rx ring %d, ret=[%d]", i, ret);
		}
	}

	NCP_MD_RX_INFO("exit");
}

void noa_ncp_md_rx_deactivate(struct noa_md_fw *md_fw)
{
	struct noa_md_fw_rx *rx = md_fw->rx;
	int ret;
	int i;

	NCP_MD_RX_INFO("enter");

	for (i = 0; i < kNoaModemRingRxDataEnd; i++) {
		ret = rx->ring_ops->deactivate(&rx->rx_ring[i].ring, i);
		if (ret) {
			NCP_MD_RX_ERROR("Failed to deactivate rx ring %d, ret=[%d]", i, ret);
		}
	}

	NCP_MD_RX_INFO("exit");
}

void noa_ncp_md_rx_exit(struct noa_md_fw *md_fw)
{
	struct noa_md_fw_rx *rx = md_fw->rx;
	int ring_type;

	NCP_MD_RX_INFO("enter");

	for (ring_type = 0; ring_type < rx->rxq_cnt; ring_type++) {
		tasklet_kill(&rx->dpmaif_rxqs[ring_type].ncp_md_rx_done_task);

		// Release RX ring
		rx->ring_ops->exit(md_fw, ring_type);
	}
	tasklet_kill(&rx->rx_reload_task);
	tasklet_kill(&rx->rx_batcnt_len_err_task);
	tasklet_kill(&rx->rx_tkid_free_poll_task);

	NCP_MD_RX_INFO("exit");
}
