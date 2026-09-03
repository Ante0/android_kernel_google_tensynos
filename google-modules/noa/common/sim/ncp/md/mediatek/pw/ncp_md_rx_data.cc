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

#define NCP_DEBUG 0  // 0: Disable, 1: Enable

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

	/* The longest check time is 2ms, step is 20us */
	do {
		rx_info->pit_pd_seq = FIELD_GET(PIT_PD_SEQ, le32_to_cpu(pd_pit->pd_footer));
		if (rx_info->pit_pd_seq == pit_seq_expect) {
			ret = 0;
			break;
		}

#ifdef linux
		udelay(DPMAIF_POLL_STEP);
#else
		pw::this_thread::sleep_for(std::chrono::milliseconds(DPMAIF_POLL_STEP));
#endif
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
#if NCP_DEBUG
		NCP_MD_RX_INFO("normal_bat=[%u]", rx_info->normal_bat);
		NCP_MD_RX_INFO("pit_pd_cur_bid=[%u]", rx_info->pit_pd_cur_bid);
		NCP_MD_RX_INFO("pit_pd_data_len=[%u]", rx_info->pit_pd_data_len);
		NCP_MD_RX_INFO("pit_pd_hd_offset=[%u]", rx_info->pit_pd_hd_offset);
		NCP_MD_RX_INFO("pit_continue=[%u]", rx_info->pit_continue);
		NCP_MD_RX_INFO("addr_high=[0x%llx]", dma_addr);
		NCP_MD_RX_INFO("pit_pd_dma_addr=[0x%llx]", rx_info->pit_pd_dma_addr);
#endif
	}

out:
	return ret;
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
	bool is_msg_pit_recv = false;
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
	bool is_previous_msg_pit = false;

#if NCP_DEBUG
	NCP_MD_RX_INFO("PIT rxq info: pit_total=[%d], rxq->id:[%d], "
		       "rd_idx=[%d], wr_idx=[%d], rel_rd_idx=[%d]",
		       rxq->pit_cnt, rxq->id, rxq->pit_rd_idx, rxq->pit_wr_idx,
		       rxq->pit_rel_rd_idx);
#endif
#ifdef linux
	// pit cache flush for driver mode
	if (rxq->attr & DPMAIFQ_ATTR_PIT_CACHED)
		noa_ncp_md_rx_rxq_pit_cache_memory_flush(rxq, pit_cnt);

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	prefetch(rxq);
#endif
#endif

	/* Call begin_processing before loop */
	ret = noa_ring_begin_processing(ring);
	if (ret <= 0) {
		NCP_MD_RX_ERROR(
			"Fail to noa_ring_begin_processing at start, ret=[%d]", ret);
		return ret;
	}

	for (rx_cnt = 0; rx_cnt < pit_cnt; rx_cnt++) {
#if NCP_DEBUG
		NCP_MD_RX_INFO("rxq[%d]->pit_rd_idx=[%d]", rxq->id, rxq->pit_rd_idx);
#endif

		if (noa_ring_is_full(ring)) {
			NCP_MD_RX_ERROR("noa_ring[%d]_is_full", kNoaModemRingRxq0 + rxq->id);
			goto out;
		}

		/* Pit sequence check. */
#ifdef linux
		pit_info = rxq->pit_base + rxq->pit_rd_idx;
#else
		pit_info = (struct dpmaif_pd_pit *)rxq->pit_base + rxq->pit_rd_idx;
#endif
		ret = noa_ncp_md_rx_get_rx_info(
			pit_info,
			&rx_info,
			rxq->pit_seq_expect,
			rxq->id);
		if (likely(!ret)) {
			rxq->pit_seq_expect++;
			if (rxq->pit_seq_expect >= rxq->pit_seq_max)
				rxq->pit_seq_expect = 0;

			rxq->pit_seq_fail_cnt = 0;
		} else {
			NCP_MD_RX_ERROR("Failed to check rxq%u pit seq, cur_seq(%u) != exp_seq(%u)",
					rxq->id,
					rx_info.pit_pd_seq,
					rxq->pit_seq_expect);

			rxq->pit_seq_fail_cnt++;
			if (rxq->pit_seq_fail_cnt >= DPMAIF_PIT_SEQ_CHECK_FAIL_CNT) {
				rxq->pit_seq_fail_cnt = 0;
				NCP_MD_RX_ERROR("return -DATA_FLOW_CHK_ERR");
				ret = -DATA_FLOW_CHK_ERR;
			}
			break;
		}
		// Save rx_info for debug purpose.
		rxq->rx_info = &rx_info;

		/* Handle message pit. */
		if (rx_info.msg_pit) {
			if (!is_msg_pit_recv) {
				/* Set message pit to NOA embedded vendor desc */
				struct noa_modem_vendor_msg_pit *msg = &p_desc_msg_pd->msg;
				msg_pit = (struct dpmaif_msg_pit *)pit_info;
				msg->dword1 = msg_pit->dword1;
				msg->dword2 = msg_pit->dword2;
				msg->dword3 = msg_pit->dword3;
				msg->dword4 = msg_pit->dword4;
				is_msg_pit_recv = true;
				is_previous_msg_pit = true;
#if NCP_DEBUG
				NCP_MD_RX_INFO("Receive msg_pit, pit_rd_idx=[%d]", rxq->pit_rd_idx);
#endif
			} else {
				NCP_MD_RX_ERROR("Invalid pit, rxq[%u] two continuous message pit",
						rxq->id);
				noa_ring_complete_processing(ring);
				return -DATA_FLOW_CHK_ERR;
			}
		} else {
			data_dma_addr_high = le32_to_cpu(pit_info->addr_high);
			data_dma_addr_low = le32_to_cpu(pit_info->addr_low);
			data_len = FIELD_GET(PIT_PD_DATA_LEN, le32_to_cpu(pit_info->pd_header));
			pit_hd_offset =
				FIELD_GET(PIT_PD_HD_OFFSET, le32_to_cpu(pit_info->pd_footer)) << 2;

#if NCP_DEBUG
			NCP_MD_RX_INFO(
				"data_dma_addr_high=[0x%llx], data_dma_addr_low=[0x%llx], "
				"data_len=[%u], pit_hd_offset=[%u]",
				data_dma_addr_high, data_dma_addr_low, data_len, pit_hd_offset);
#endif
			if (is_msg_pit_recv) {
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
#if NCP_DEBUG
				NCP_MD_RX_INFO(
					"rxq%u bat_idx=[%d], rx_tkid=[%d], mapped_rx_tkid=[%d]",
					rxq->id, bat_idx, rx_tkid, mapped_rx_tkid);
#endif
				/* Use rx_tkid to replace bid */
				pit_info->pd_header &= ~(GENMASK(15, 3));
				pit_info->pd_header |= (rx_tkid << 3) & GENMASK(15, 3);
				pit_info->pd_footer &= ~(GENMASK(10, 8));
				pit_info->pd_footer |= ((rx_tkid >> 13) << 8) & GENMASK(10, 8);

				if (test_bit(bat_idx, bat_ring->mask_tbl)) {
					NCP_MD_RX_ERROR("mask_tbl bit should be 0 at bat_idx=[%d]",
							bat_idx);
				}
				set_bit(bat_idx, bat_ring->mask_tbl);

				if (is_previous_msg_pit) {
					struct noa_modem_vendor_pd_pit *pd = &p_desc_msg_pd->pd;
					pd->pd_header = pit_info->pd_header;
					pd->addr_low = pit_info->addr_low;
					pd->addr_high = pit_info->addr_high;
					pd->pd_footer = pit_info->pd_footer;

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
#if NCP_DEBUG
					NCP_MD_RX_INFO("noa_fw_ring_write, ret=[%d]", ret);
#endif
					is_previous_msg_pit = false;
				} else {
					struct noa_modem_vendor_pd_pit *pd = &p_desc_pd->pd;
					pd->pd_header = pit_info->pd_header;
					pd->addr_low = pit_info->addr_low;
					pd->addr_high = pit_info->addr_high;
					pd->pd_footer = pit_info->pd_footer;
#if NCP_DEBUG
					NCP_MD_RX_INFO(
						"LRO payload PIT only, "
						"rxq%d, bid=[%u], mapped_rx_tkid=[%u]",
						rxq->id, bat_idx, mapped_rx_tkid);
#endif
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
#if NCP_DEBUG
					NCP_MD_RX_INFO(
						"pit_continue=1, noa_fw_ring_write, ret=[%d]", ret);
#endif
					is_previous_msg_pit = false;
				}

				if (!rx_info.pit_continue) {
					recv_pkt_cnt++;
					is_msg_pit_recv = false;
				}
			} else {
				NCP_MD_RX_ERROR(
					"Invalid pit, rxq[%u] no msg pit received before pd pit"
					" receive",
					rxq->id);
				noa_ring_complete_processing(ring);
				return -DATA_FLOW_CHK_ERR;
			}
		}
		doorbell_cnt++;
		noa_ring_cnt++;
		rxq->pit_rd_idx = noa_ncp_md_rx_ring_next_idx(rxq->pit_cnt, rxq->pit_rd_idx);

		if (noa_ring_cnt >= NOA_FW_RING_BUDGET) {
#if NCP_DEBUG
			NCP_MD_RX_INFO("NOA ring budget [%u] met for rxq[%d]. noa_ring_cnt=%u.",
				       NOA_FW_RING_BUDGET, rxq->id, noa_ring_cnt);
#endif
			noa_ring_complete_processing(ring);
			noa_ring_cnt = 0;

			ret = noa_ring_begin_processing(ring);
			if (ret <= 0) {
				NCP_MD_RX_ERROR(
					"Fail to noa_ring_begin_processing after budget, ret:%d",
					ret);
				return ret;
			}
		}
	}

out:
	noa_ring_complete_processing(ring);

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

		ret = NOA_MD_SEND_DOORBELL(NOA_MD_DPMAIF_PIT, rxq->id, doorbell_cnt);
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
		} else if (ret <= -DATA_ERR_STOP_MAX) {
			ret = -DATA_ERR_STOP_MAX;
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

static void noa_ncp_md_rx_done_task(unsigned long data)
{
	struct noa_rx_queue *rxq = (struct noa_rx_queue *)data;
	unsigned int work_done = 0;
	int ret = 0;
	int budget = NOA_FW_RING_SIZE;

	if (likely(rxq->started)) {
		if (rxq->pit_poll_enable)
			ret = noa_ncp_md_rx_data_collect_more(rxq, &work_done);
		else
			ret = noa_ncp_md_rx_data_collect(rxq, &work_done);

		if (ret == -DATA_DL_ONCE_MORE) {
			work_done = budget;
		} else {
			if (unlikely(ret == -DATA_ERR_STOP_MAX))
				rxq->started = false;
			if (work_done > budget)
				work_done = budget - 1;
		}
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
	unsigned int q_mask)
{
	NCP_MD_RX_INFO("enter, type=[%d], q_mask=[%d]", type, q_mask);
	// TODO: Check mtk_dpmaif_irq_handle & mtk_dpmaif_drv_intr_complete_t800
	// for INTR err cases
	switch (type) {
	case NOA_DPMAIF_INTR_DL_BATCNT_LEN_ERR:
		// TODO: Send DPMAIF_INTR_DL_BATCNT_LEN_ERR RPC to APC
		break;
	case NOA_DPMAIF_INTR_DL_FRGCNT_LEN_ERR:
		// TODO: Send DPMAIF_INTR_DL_FRGCNT_LEN_ERR RPC to APC
		break;
	case NOA_DPMAIF_INTR_DL_PITCNT_LEN_ERR:
		// TODO: Send DPMAIF_INTR_DL_PITCNT_LEN_ERR RPC to APC
		break;
	case NOA_DPMAIF_INTR_DL_DONE:
		NCP_MD_RX_INFO("DPMAIF_INTR_DL_DONE, q_mask=0x%x", q_mask);
		noa_ncp_md_rx_dpmaif_irq_rx_done(md_fw, q_mask);
		break;
	default:
		break;
	}
}
#endif

static void noa_ncp_md_rx_reload_bat(struct noa_md_fw_rx *rx, struct noa_bat_ring *bat_ring)
{
	int doorbell_cnt;
	unsigned int next_ncp_bat_idx, bat_idx;
	int ret;
	struct noa_rx_tkid_info *rx_tkid_info = &bat_ring->rx_tkid_info;
	unsigned short rx_tkid;
	uint16_t mapped_rx_tkid;

	// ncp bat refill process
	// Get the rx_tkid and address from the rx_tkid free pool
	// Assign to the ncp bat
	// Send th doorbell to modem
	doorbell_cnt = 0;

#if NCP_DEBUG
	NCP_MD_RX_INFO("ring_type=[%d], ring_id[%d], bat_ring->bat_wr_idx(bat_idx)=[%d]",
		bat_ring->type,
		bat_ring->id,
		bat_ring->bat_wr_idx);
#endif

	while (rx_tkid_info->rx_tkid_free_rear != rx_tkid_info->rx_tkid_free_fore) {
		bat_idx = bat_ring->bat_wr_idx;
		next_ncp_bat_idx = noa_ncp_md_rx_ring_next_idx(bat_ring->bat_cnt, bat_idx);

		if (next_ncp_bat_idx == bat_ring->bat_rd_idx) {
			NCP_MD_RX_ERROR("ring_id=[%d], blocked, next bat idx == bat rd idx (%d)",
				bat_ring->id,
				next_ncp_bat_idx);
			tasklet_schedule(&rx->rx_reload_task);
			break;
		}
#ifdef linux
		if (!test_and_clear_bit(bat_idx, bat_ring->mask_tbl)) {
			NCP_MD_RX_ERROR("ring_id=[%d], mask tbl check fail, bat idx(%d)",
				bat_ring->id,
				bat_idx);
			tasklet_schedule(&rx->rx_reload_task);
			break;
		}
#else
		if(!test_bit(bat_idx, bat_ring->mask_tbl)) {
			NCP_MD_RX_ERROR("ring_id=[%d], mask tbl check fail, bat idx(%d)",
				bat_ring->id,
				bat_idx);
			tasklet_schedule(&rx->rx_reload_task);
			break;
		}
		clear_bit(bat_idx, bat_ring->mask_tbl);
#endif

		/* Get the rx_tkid and address from the NCP rx_tkid free pool */
		// maintain bat idx <-> rx tkid
		rx_tkid = rx_tkid_info->free_pool[rx_tkid_info->rx_tkid_free_fore].rx_tkid;
		rx_tkid_info->rx_tkid[bat_idx] = rx_tkid;

		// Then assign address to the NCP bat
		memcpy((struct dpmaif_bat *)bat_ring->bat_base + bat_idx,
			&rx_tkid_info->free_pool[rx_tkid_info->rx_tkid_free_fore].bat,
			sizeof(struct dpmaif_bat));

		if (bat_ring->type == NORMAL_BAT) {
			// Driver mode: assign noa_va
			mapped_rx_tkid =
				noa_ncp_md_bat_rx_tkid_map(bat_ring->id, bat_ring->type, rx_tkid);
			bat_ring->noa_data_addr[mapped_rx_tkid].noa_va =
				rx_tkid_info->free_pool[rx_tkid_info->rx_tkid_free_fore]
					.noa_data_addr;
		} else { /* FRAG_BAT */
			mapped_rx_tkid =
				noa_ncp_md_bat_rx_tkid_map(bat_ring->id, bat_ring->type, rx_tkid);
			bat_ring->noa_data_addr[mapped_rx_tkid].noa_va =
				rx_tkid_info->free_pool[rx_tkid_info->rx_tkid_free_fore]
					.noa_data_addr;
		}

		rx_tkid_info->rx_tkid_free_fore =
			noa_ncp_md_rx_ring_next_idx(
				bat_ring->bat_cnt, rx_tkid_info->rx_tkid_free_fore);

		// next rx_tkid_free_fore
		bat_ring->bat_wr_idx = next_ncp_bat_idx;
		doorbell_cnt++;
	}

	// Send doorbell to modem
	if (doorbell_cnt > 0) {

#ifdef linux
		/* Make sure all bat information written done before notifying HW. */
		dma_wmb();
#endif

		if (bat_ring->type == NORMAL_BAT) {
			ret = NOA_MD_SEND_DOORBELL(DPMAIF_BAT, bat_ring->id, doorbell_cnt);
			if (unlikely(ret < 0)) {
				NCP_MD_RX_ERROR("Fail to send doorbell, ncp BAT(%d)", bat_ring->id);
			}
#if NCP_DEBUG
			NCP_MD_RX_INFO("ring_type=[%d], ring_id[%d], bat_wr_idx(csr)=[%d]",
				       bat_ring->type, bat_ring->id,
				       NOA_MD_GET_RING_INDEX(NOA_MD_DPMAIF_BAT_WIDX, bat_ring->id));
#endif
		} else {
			ret = NOA_MD_SEND_DOORBELL(DPMAIF_FRAG, bat_ring->id, doorbell_cnt);
			if (unlikely(ret < 0)) {
				NCP_MD_RX_ERROR("Fail to send doorbell, ncp FRAG BAT(%d)",
						bat_ring->id);
			}
#if NCP_DEBUG
			NCP_MD_RX_INFO("ring_type=[%d], ring_id[%d], bat_wr_idx(csr)=[%d]",
				       bat_ring->type, bat_ring->id,
				       NOA_MD_GET_RING_INDEX(NOA_MD_DPMAIF_FRAG_WIDX,
							     bat_ring->id));
#endif
		}
	}
}

static void noa_ncp_md_rx_reload_task(unsigned long data)
{
	struct noa_md_fw *md_fw = (struct noa_md_fw *)data;
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
	spin_unlock(&rx_tkid_info->rx_tkid_lock);

	free_rx_tkid_cnt =
		noa_ncp_md_rx_ring_buf_readable(bat_ring->bat_cnt, rx_tkid_info->rx_tkid_free_fore,
						rx_tkid_info->rx_tkid_free_rear);
#if NCP_DEBUG
	NCP_MD_RX_INFO("free_rx_tkid_cnt=[%d]", free_rx_tkid_cnt);
#endif

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

static void noa_ncp_md_rx_tkid_free_poll_task(unsigned long data)
{
	struct noa_md_fw *md_fw = (struct noa_md_fw *)data;
	struct noa_md_fw_rx *rx = md_fw->rx;
	uint32_t refill_qid = rx->isr_refill_index;
	struct noa_md_apc2ncp_tx_buffer_desc *desc =
		noa_md_apc2ncp_get_tx_buffer_desc();
	noa_ring_consumer *ring = &desc[refill_qid].ring;
	int ret = 0;

	if (!ring) {
		NCP_MD_RX_ERROR("ring is NULL");
		return;
	}
	NCP_MD_RX_INFO("enter, ring->name=[%s], refill_qid=[%u]", ring->name, refill_qid);

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
			break;
		} else if (ret < 0 || !data_addr) {
			NCP_MD_RX_ERROR("noa_ring_read=[%d]", ret);
			noa_ring_tail_inc(ring);
			continue;
		}
		p_desc = (struct noa_modem_rx_refill_desc *)data_addr;

#if NCP_DEBUG
		NCP_MD_RX_INFO("ret=[%d], p_desc=[%p]", ret, p_desc);
#endif
		noa_ncp_md_rx_handle_refill_ring_desc(md_fw, p_desc, refill_qid);
	}
	noa_ring_complete_processing(ring);

	// Trigger NCP bat reload task
	tasklet_schedule(&rx->rx_reload_task);

	return;
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
		tasklet_init(&rx->dpmaif_rxqs[i].ncp_md_rx_done_task, noa_ncp_md_rx_done_task,
			     (unsigned long)rxqs);
	}

	/* init tasklet */
	tasklet_init(&rx->rx_reload_task, noa_ncp_md_rx_reload_task, (unsigned long)md_fw);
	tasklet_init(&rx->rx_tkid_free_poll_task, noa_ncp_md_rx_tkid_free_poll_task,
		     (unsigned long)md_fw);
	return ret;
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
	tasklet_kill(&rx->rx_tkid_free_poll_task);

	NCP_MD_RX_INFO("exit");
}
