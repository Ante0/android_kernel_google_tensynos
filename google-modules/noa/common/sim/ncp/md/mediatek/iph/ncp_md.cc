// SPDX-License-Identifier: GPL-2.0-only
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
#include "ncp_md_shmem_sync.h"
#include "ncp_modem_data.h"
#include "noa_md_apc2ncp_ring.h"
#include "noa_md_dpa_doorbell.h"
#include "noa_md_shmem_layout.h"

#include "pw_system/work_queue.h"

#ifndef linux
struct noa_md_fw_tx tx_init;
SEC_EXRAM_DATA struct noa_md_fw_rx rx_init;
struct modem_fw_ring_ops tx_ring_ops;
struct modem_fw_ring_ops rx_ring_ops;
struct noa_md_fw md_fw_init = {};

SEC_EXRAM_DATA struct noa_rx_tkid_free_pool bat0_free_pool[BAT0_COUNT] = { {} };
SEC_EXRAM_DATA struct noa_rx_tkid_free_pool bat1_free_pool[BAT1_COUNT] = { {} };
SEC_EXRAM_DATA struct noa_rx_tkid_free_pool frag_bat0_free_pool[FRAG_BAT0_COUNT] = { {} };
SEC_EXRAM_DATA struct noa_rx_tkid_free_pool frag_bat1_free_pool[FRAG_BAT1_COUNT] = { {} };
#endif

struct noa_md_fw *g_md_fw;

static unsigned int traffic_stats_shift = 7;
#define NOA_NCP_MD_RING_TPUT_CALC(CNT, MS_SHIFT)	((CNT) >> (MS_SHIFT))

#ifndef linux
static int noa_ncp_bat_ring_init(struct noa_bat_ring_pb *bat_ring_source,
				 struct noa_bat_ring *bat_ring_dest, enum dpmaif_bat_type type,
				 int bat_ring_id, struct bat_ring *bat_ring_shmem)
{
	uint32_t low_address = 0x0, high_address = 0x0;
	bat_ring_dest->bat_base = bat_ring_source->bat_base;
	bat_ring_dest->bat_dpa_base = bat_ring_source->bat_dpa_base;
	bat_ring_dest->buf_size = bat_ring_source->buf_size;
	bat_ring_dest->bat_cnt = bat_ring_source->bat_cnt;
	bat_ring_dest->id = bat_ring_id;
	bat_ring_dest->type = type;
	bat_ring_dest->max_reload_cnt = bat_ring_source->max_reload_cnt;
	bat_ring_dest->bat_cnt_err_intr_set = false;
	bat_ring_dest->doorbell_th = MIN_BAT_BURST_CNT;
	std::atomic_store(&bat_ring_dest->to_reload_cnt, bat_ring_dest->max_reload_cnt);
	std::atomic_store(&bat_ring_dest->bat_stats, 0);
	bat_ring_dest->bat_wr_idx = bat_ring_source->bat_wr_idx;
	bat_ring_dest->bat_rd_idx = bat_ring_source->bat_rd_idx;

	bat_ring_dest->mask_tbl = (unsigned long *)bat_ring_source->mask_table_dpa_base;
	bat_ring_dest->rx_tkid_info.rx_tkid =
		(unsigned short *)bat_ring_source->tkid_table_dpa_base;
	bat_ring_dest->noa_data_addr =
		(struct noa_rx_data_addr *)bat_ring_source->buffer_table_dpa_base;

	low_address = bat_ring_dest->bat_dpa_base & 0xFFFFFFFF;
	high_address = bat_ring_dest->bat_dpa_base >> 32;
	NCP_MD_INFO("bat_dpa_base high_addr=[0x%lx], low_addr=[0x%lx]", high_address, low_address);

	NCP_MD_INFO(
		"bat[%d], type=[%d], buf_size=[%d], bat_cnt=[%d], bat_wr_idx=[%d], bat_rd_idx=[%d]",
		bat_ring_id, type, bat_ring_source->buf_size, bat_ring_dest->bat_cnt,
		bat_ring_dest->bat_wr_idx, bat_ring_dest->bat_rd_idx);
	NCP_MD_INFO(
		"bat[%d], type=[%d], mask_table_base=[0x%lx], "
		"tkid_table_base=[0x%lx], buffer_table_base=[0x%lx]",
		bat_ring_id, type, bat_ring_dest->mask_tbl, bat_ring_dest->rx_tkid_info.rx_tkid,
		bat_ring_dest->noa_data_addr);

	InvalidateDCache((struct noa_rx_tkid_free_pool *)bat_ring_shmem->ncp_free_pool_dpa_base,
			sizeof(uint64_t));
	bat_ring_dest->rx_tkid_info.free_pool =
		(struct noa_rx_tkid_free_pool *)bat_ring_shmem->ncp_free_pool_dpa_base;
	NCP_MD_INFO("bat[%d], type=[%d], rx_tkid_info.free_pool=[0x%lx]", bat_ring_id, type,
		    bat_ring_dest->rx_tkid_info.free_pool);

	spin_lock_init(&bat_ring_dest->rx_tkid_info.rx_tkid_lock);
	bat_ring_dest->rx_tkid_info.rx_tkid_free_fore = 0;
	bat_ring_dest->rx_tkid_info.rx_tkid_free_rear = 0;

	return 0;
}

static int noa_ncp_bat_info_init(struct noa_bat_info_pb *bat_info_source,
				 struct noa_bat_info *bat_info_dest, int bat_ring_id,
				 struct bat_infos *bat_info_shmem)
{
	bat_info_dest->max_mtu = DPMAIF_DFLT_MTU;
	bat_info_dest->frag_bat_enabled = bat_info_source->frag_bat_enabled;

	noa_ncp_bat_ring_init(&bat_info_source->normal_bat_ring, &bat_info_dest->normal_bat_ring,
			      NORMAL_BAT, bat_ring_id, &bat_info_shmem->normal_bat_ring);

	if (bat_info_source->frag_bat_enabled) {
		noa_ncp_bat_ring_init(&bat_info_source->frag_bat_ring,
				      &bat_info_dest->frag_bat_ring, FRAG_BAT, bat_ring_id,
				      &bat_info_shmem->frag_bat_ring);
	}

	return 0;
}

static void noa_ncp_md_init_queues(struct noa_md_fw *md_fw, struct noa_md_protobuf *buf)
{
        uint32_t low_address = 0x0, high_address = 0x0;
	for (uint32_t index = 0; index < kMaxUlQueueSize; ++index) {
                // Update for modem drb rings.
		md_fw->tx->txqs[index].drb_base = buf->txqs[index].drb_base;
		md_fw->tx->txqs[index].drb_dpa_base = buf->txqs[index].drb_dpa_base;
		md_fw->tx->txqs[index].drb_cnt = buf->txqs[index].drb_cnt;
		md_fw->tx->txqs[index].db_delay_ms = buf->txqs[index].db_delay_ms;
		md_fw->tx->txqs[index].burst_submit_cnt =
		                  buf->txqs[index].burst_submit_cnt;
		md_fw->tx->txqs[index].drb_wr_idx = buf->txqs[index].drb_wr_idx;
		md_fw->tx->txqs[index].drb_rd_idx = buf->txqs[index].drb_rd_idx;
		md_fw->tx->txqs[index].drb_rel_rd_idx =buf->txqs[index].drb_rel_rd_idx;
		md_fw->tx->txqs[index].id = index;
		low_address = md_fw->tx->txqs[index].drb_dpa_base & 0xFFFFFFFF;
		high_address = md_fw->tx->txqs[index].drb_dpa_base >> 32;
		NCP_MD_INFO(
                            "txqs[%d]high_addr=[0x%lx], low_addr=[0x%lx], drb_cnt=%d,"
                            "db_delay_ms=%d, burst_submit_cnt=%d",
                            index, high_address, low_address,
                            md_fw->tx->txqs[index].drb_cnt,
                            md_fw->tx->txqs[index].db_delay_ms,
                            md_fw->tx->txqs[index].burst_submit_cnt);
		NCP_MD_INFO(
                            "txqs[%d]drb_wr_idx=%u, drb_rd_idx=%u, drb_rel_rd_idx=%u",
                            index,
                            md_fw->tx->txqs[index].drb_wr_idx,
                            md_fw->tx->txqs[index].drb_rd_idx,
                            md_fw->tx->txqs[index].drb_rel_rd_idx);
        }
	md_fw->tx->txq_cnt = kMaxUlQueueSize;

	// Update ncp rxqs for modem
	md_fw->rx->rxq_cnt = kMaxDlQueueSize;
	for (uint32_t index = 0; index < kMaxDlQueueSize; ++index) {
		md_fw->rx->dpmaif_rxqs[index].pit_base = buf->rxqs[index].pit_base;
		md_fw->rx->dpmaif_rxqs[index].pit_dpa_base = buf->rxqs[index].pit_dpa_base;
		md_fw->rx->dpmaif_rxqs[index].pit_cnt = buf->rxqs[index].pit_cnt;
		md_fw->rx->dpmaif_rxqs[index].pit_seq_max = NOA_PIT_SEQ_MAX;
		md_fw->rx->dpmaif_rxqs[index].bat_ring_id = buf->rxqs[index].bat_ring_id;
		md_fw->rx->dpmaif_rxqs[index].id = index;
		md_fw->rx->dpmaif_rxqs[index].started = false;
		md_fw->rx->dpmaif_rxqs[index].pit_wr_idx = buf->rxqs[index].pit_wr_idx;
		md_fw->rx->dpmaif_rxqs[index].pit_rd_idx = buf->rxqs[index].pit_rd_idx;
		md_fw->rx->dpmaif_rxqs[index].pit_rel_rd_idx = buf->rxqs[index].pit_rel_rd_idx;
		md_fw->rx->dpmaif_rxqs[index].pit_seq_expect = buf->rxqs[index].pit_seq_max;
		md_fw->rx->dpmaif_rxqs[index].pit_poll_enable = false;
		std::atomic_store(&md_fw->rx->dpmaif_rxqs[index].pit_rel_cnt, 0);
		std::atomic_store(&md_fw->rx->dpmaif_rxqs[index].pit_stats, 0);
		md_fw->rx->dpmaif_rxqs[index].pit_cnt_err_intr_set = false;
		md_fw->rx->dpmaif_rxqs[index].pit_burst_rel_cnt = NOA_PIT_CNT_UPDATE_THRESHOLD;
		md_fw->rx->dpmaif_rxqs[index].pit_seq_fail_cnt = 0;
		md_fw->rx->dpmaif_rxqs[index].rx_record.is_msg_pit_recv = false;
		md_fw->rx->dpmaif_rxqs[index].rx_record.is_previous_msg_pit = false;
		md_fw->rx->dpmaif_rxqs[index].attr = kRxqAttribute[index];

		low_address = md_fw->rx->dpmaif_rxqs[index].pit_dpa_base & 0xFFFFFFFF;
		high_address = md_fw->rx->dpmaif_rxqs[index].pit_dpa_base >> 32;
		NCP_MD_INFO("pit_dpa_base high_addr=[0x%lx], low_addr=[0x%lx]",
			    high_address, low_address);
		NCP_MD_INFO("pit[%d], "
			    "pit_cnt=[%d], pit_seq_max=[%d], bat_ring_id=[%d], "
			    "pit_wr_idx=[%d], pit_rd_idx=[%d], pit_rel_rd_idx=[%d]",
			    index, md_fw->rx->dpmaif_rxqs[index].pit_cnt,
			    md_fw->rx->dpmaif_rxqs[index].pit_seq_max,
			    md_fw->rx->dpmaif_rxqs[index].bat_ring_id,
			    md_fw->rx->dpmaif_rxqs[index].pit_wr_idx,
			    md_fw->rx->dpmaif_rxqs[index].pit_rd_idx,
			    md_fw->rx->dpmaif_rxqs[index].pit_rel_rd_idx);
	}

	// Update ncp bats for modem
	md_fw->rx->bat_ring_num = kMaxBatInfoSize;
	for (uint32_t index = 0; index < kMaxBatInfoSize; ++index) {
		struct noa_bat_info_pb *bat_info_source = &buf->bat_infos[index];
		struct noa_bat_info *bat_info_dest = &md_fw->rx->bat_infos[index];
		struct noa_md_shmem_layout *shmem;
		struct bat_infos *bat_info_shmem;

		if (unlikely(!md_fw->shared_mem_info.addr)) {
			NCP_MD_ERROR("shared memory address is NULL");
			return;
		}

		shmem = (struct noa_md_shmem_layout *)md_fw->shared_mem_info.addr;
		bat_info_shmem = &shmem->switch_payload.ap_state.bat_infos[index];

		// Initialize ncp bat infos
		noa_ncp_bat_info_init(bat_info_source, bat_info_dest, index, bat_info_shmem);
	}
}
#endif

static irqreturn_t noa_ncp_md_apc2ncp_isr(int id, void *data)
{
	struct noa_md_fw *md_fw = (struct noa_md_fw *)data;
	struct noa_md_fw_tx *tx = md_fw->tx;
	struct noa_md_fw_rx *rx = md_fw->rx;

	NCP_MD_DEBUG("enter, id=[%d]", id);
	if (id >= kNoaModemRingTxDrb0 && id <= kNoaModemRingTxDrb4) {
		tx->isr_apc2ncp_bitmask.fetch_or(1 << id);
		NCP_MD_DEBUG("isr_apc2ncp_bitmask=[0x%x]", tx->isr_apc2ncp_bitmask.load());
		tasklet_schedule(&tx->apc2ncp_task);
	} else if (id >= kNoaModemRingRxRefillNormalBat0 && id < kNoaModemApcToNcpRingMax){
		rx->isr_refill_bitmask.fetch_or(1 << id);
		NCP_MD_DEBUG("isr_refill_bitmask=[0x%x]", rx->isr_refill_bitmask.load());
		tasklet_schedule(&rx->rx_tkid_free_poll_task);
	} else {
		NCP_MD_ERROR("Invalid id!");
	}
	return IRQ_HANDLED;
}

static enum noa_md_switch_status noa_ncp_md_switch_handler(
	struct noa_md_fw *md_fw, enum noa_md_switch_command command,
	struct noa_md_switch_payload *payload)
{
	struct noa_md_shmem_layout *shmem;
	int ret;

	if (unlikely(!md_fw)) {
		NCP_MD_ERROR("md_fw is NULL");
		return SWITCH_STATUS_FAILED;
	}

	if (unlikely(!md_fw->shared_mem_info.addr)) {
		NCP_MD_ERROR("shared memory address is NULL");
		return SWITCH_STATUS_FAILED;
	}

	shmem = (struct noa_md_shmem_layout *)md_fw->shared_mem_info.addr;

	InvalidateDCache(
		&shmem->switch_payload,
		sizeof(struct noa_md_switch_payload));

	payload = &shmem->switch_payload;
	payload->status = SWITCH_STATUS_SUCCESS;

	NCP_MD_INFO("command: %d", payload->command);

	noa_ncp_md_rx_set_switch_command(md_fw->rx, payload->command);

	switch (payload->command) {
	case NOA_MD_SWITCH_CMD_NOTIFY_PREPARE_SWITCH:
		break;

	case NOA_MD_SWITCH_CMD_EXCHANGE_STATE:
		if (payload->target_path == NOA_DATA_PATH_OFFLOAD) {
			NCP_MD_INFO("Setup apc2ncp rings in ncp side");
			ret = noa_md_apc2ncp_ring_ncp_setup(NULL);
			if (ret) {
				noa_md_apc2ncp_ring_ncp_release();
				payload->status = SWITCH_STATUS_FAILED;
				break;
			}
		} else {
			// Stop NCP RX Done flow
			NCP_MD_INFO("Stop NCP RX Done");
			noa_ncp_md_rx_stop_rxq(md_fw->rx);

			// for tethering, handle remaining packets from md_fw_output ring to modem
			// drbs before update tx ring info to apc
			noa_ncp_md_tx_data_handling(md_fw);
			noa_ncp_md_tx_tkid_queues_shmem_update(md_fw);

			noa_ncp_md_tx_update_ring_info_for_direct_path(
				md_fw, md_fw->tx, &payload->ncp_state);

			NCP_MD_INFO("Deactive NOA_DRB Rings");
			noa_ncp_md_tx_deactivate(md_fw);

			// Flush NCP BATs
			NCP_MD_INFO("Flush NCP BATs");
			noa_ncp_md_rx_tkid_free_poll_task((unsigned long)md_fw);
			noa_ncp_md_rx_reload_bat_all(md_fw);

			NCP_MD_INFO("Deactive NOA_PIT Rings");
			noa_ncp_md_rx_deactivate(md_fw);

			NCP_MD_INFO("Update indices");
			// prepare to send indices to the APC(bat/pit/ncp_free_pool)
			noa_ncp_md_rx_update_ring_info_for_direct_path(md_fw->rx,
								       &payload->ncp_state);

			NCP_MD_INFO("Flush RX tables");
			noa_ncp_md_rx_flush_rx_table(md_fw->rx);

			NCP_MD_INFO("Release apc2ncp rings in ncp side");
			noa_md_apc2ncp_ring_ncp_release();
		}
		break;

	case NOA_MD_SWITCH_CMD_COMMIT_SWITCH:
		if (payload->target_path == NOA_DATA_PATH_OFFLOAD) {
			noa_ncp_md_tx_update_ring_info_for_offload_path(
				&payload->ap_state, md_fw->tx);

			NCP_MD_INFO("Activate NOA_DRB Rings");
			noa_ncp_md_tx_activate(md_fw);

			NCP_MD_INFO("Update RX ring info for offload path");
			noa_ncp_md_rx_update_ring_info_for_offload_path(
				&payload->ap_state, md_fw->rx);

			NCP_MD_INFO("Activate NOA_PIT Rings");
			noa_ncp_md_rx_activate(md_fw);

			NCP_MD_INFO("Enable noa irq in commit switch mode");
			md_fw->hif->SetPcieEndpointGrant(true);
			if (!md_fw->hif->EnableIrq(true).ok()) {
				payload->status = SWITCH_STATUS_FAILED;
				break;
			}
		} else {
			NCP_MD_INFO("Disable noa irq in commit switch mode");
			if (!md_fw->hif->EnableIrq(false).ok()) {
				payload->status = SWITCH_STATUS_FAILED;
				break;
			}
			md_fw->hif->SetPcieEndpointGrant(false);
		}
		break;

	case NOA_MD_SWITCH_CMD_NOTIFY_RESTART:
		break;

	case NOA_MD_SWITCH_CMD_ROLLBACK_SWITCH:
		if (payload->target_path == NOA_DATA_PATH_OFFLOAD) {
			NCP_MD_INFO("Enable noa irq in rollback switch mode");
			if (!md_fw->hif->EnableIrq(true).ok()) {
				payload->status = SWITCH_STATUS_FAILED;
				break;
			}
		} else {
			NCP_MD_INFO("Disable noa irq in rollback switch mode");
			if (!md_fw->hif->EnableIrq(false).ok()) {
				payload->status = SWITCH_STATUS_FAILED;
				break;
			}
		}
		break;

	default:
		NCP_MD_ERROR("Unknown command: %d", command);
	}

	return SWITCH_STATUS_SUCCESS;
}

static enum noa_md_shmem_cmd_status noa_ncp_md_data_ifindex_update_handler(
	struct noa_md_fw *md_fw,
	const struct noa_md_shmem_data_payload *payload,
	struct noa_md_shmem_data_payload *resp_payload)
{
	struct noa_md_shmem_layout *shmem;

	if (unlikely(!md_fw)) {
		NCP_MD_ERROR("md_fw is NULL");
		return SHMEM_CMD_STATUS_FAILED;
	}

	if (unlikely(!payload)) {
		NCP_MD_ERROR("payload is NULL");
		return SHMEM_CMD_STATUS_FAILED;
	}

	if (unlikely(!md_fw->shared_mem_info.addr)) {
		NCP_MD_ERROR("shared memory address is NULL");
		return SHMEM_CMD_STATUS_FAILED;
	}
	NCP_MD_INFO("sub_cmd: %d, table_size: %u", payload->sub_cmd,
		    payload->ifindex_req.table_size);

	shmem = (struct noa_md_shmem_layout *)md_fw->shared_mem_info.addr;

	InvalidateDCache(
		shmem->wwan_ifindex_table,
		sizeof(uint32_t) * MAX_WWAN_IFINDEX_TABLE_SIZE);

	memcpy(md_fw->wwan_ifindex_table, shmem->wwan_ifindex_table,
		sizeof(uint32_t) * MAX_WWAN_IFINDEX_TABLE_SIZE);

	/**
	 * Please consider printing the interface table only when debugging,
	 * e.g. when NCP_DEBUG is 1
	 */
	{
		/* Buffer size: (2 digits + 1 comma) * IFINDEX_TABLE_SIZ + 1 null terminator */
		char log_buf[MAX_WWAN_IFINDEX_TABLE_SIZE * 3 + 1];
		int offset = 0;
		int remaining_len = sizeof(log_buf);
		int written;

		for (int i = 0; i < MAX_WWAN_IFINDEX_TABLE_SIZE; i++) {
			/* Ensure space for "XX,\0" (4 bytes) before snprintf */
			if (remaining_len < 4) {
				break;
			}

			written = snprintf(log_buf + offset, remaining_len, "%02d,",
				md_fw->wwan_ifindex_table[i]);

			if (written > 0) {
				offset += written;
				remaining_len -= written;
			} else {
				/* snprintf failed */
				NCP_MD_ERROR(
					"snprintf failed at index %d "
					"(ret: %d, offset: %d, rem: %d)",
					i, written, offset, remaining_len);
				break;
			}
		}

		/* Trim the trailing comma */
		if (offset > 0) {
			log_buf[offset - 1] = '\0';
		} else {
			log_buf[0] = '\0';
		}

		NCP_MD_INFO("wwan_ifindex_table:%s", log_buf);
	}

	resp_payload->ifindex_resp.status = 1;

	return SHMEM_CMD_STATUS_SUCCESS;
}

static enum noa_md_shmem_cmd_status noa_ncp_md_data_cldma_offload_config_handler(
	struct noa_md_fw *md_fw,
	const struct noa_md_shmem_data_payload *payload_req,
	struct noa_md_shmem_data_payload *payload_resp)
{
	const struct noa_md_shmem_data_cldma_offload_config_req *config_req;
	struct noa_md_shmem_data_cldma_offload_config_resp *config_resp;

	if (unlikely(!md_fw || !payload_req || !payload_resp)) {
		NCP_MD_ERROR("Invalid parameters for CLDMA offload config");
		return SHMEM_CMD_STATUS_FAILED;
	}

	config_req = &payload_req->cldma_config_req;
	config_resp = &payload_resp->cldma_config_resp;

	NCP_MD_INFO("CLDMA Offload Config: HIF%u Q%u, GPD:0x%llx, BD:0x%llx, GPDs:%u, BDs:%u",
		    config_req->hif_id, config_req->qno, config_req->gpd_dpa, config_req->bd_dpa,
		    config_req->nr_gpds, config_req->nr_bds);

	PW_LOG_INFO("JASON CLDMA Offload Config: HIF%u Q%u, GPD:0x%llx, BD:0x%llx, GPDs:%u, BDs:%u",
		    config_req->hif_id, config_req->qno, config_req->gpd_dpa, config_req->bd_dpa,
		    config_req->nr_gpds, config_req->nr_bds);

	/* TODO: Implement NCP-side hardware programming for CLDMA offload */
	config_resp->status = 1;

	return SHMEM_CMD_STATUS_SUCCESS;
}

static enum noa_md_shmem_cmd_status noa_ncp_md_debug_cmd_enable_handler(
	struct noa_md_fw *md_fw,
	const struct noa_md_shmem_debug_payload *payload,
	struct noa_md_shmem_debug_payload *resp_payload)
{
	if (unlikely(!payload || !resp_payload))
		return SHMEM_CMD_STATUS_FAILED;

	NCP_MD_INFO("sub_cmd: %d, enable: %u", payload->sub_cmd,
		    payload->enable_req.enable);

	// TODO: Implement the logic for debug command enable
	resp_payload->enable_resp.status = 1;

	return SHMEM_CMD_STATUS_SUCCESS;
}

static int noa_ncp_md_calc_tput(atomic_t *stats, unsigned int time_shift)
{
	int tmp_stats = ATOMIC_READ(stats);
	ATOMIC_SUB(tmp_stats, stats);
	return NOA_NCP_MD_RING_TPUT_CALC(tmp_stats, time_shift);
}

static void noa_ncp_md_ring_rel_ctrl_timer_func(struct timer_list *t)
{
#ifdef linux
	struct noa_md_fw *md_fw = from_timer(md_fw, t, ring_rel_ctrl_timer);
#else
	struct noa_md_fw *md_fw = container_of(t, struct noa_md_fw, ring_rel_ctrl_timer);
#endif
	struct noa_md_fw_tx *tx = md_fw->tx;
	struct noa_tx_queue *txq;

	unsigned int tmp_tput;
	int i;
	for (i = 0; i < tx->txq_cnt; i++) {
		txq = &tx->txqs[i];
		tmp_tput = noa_ncp_md_calc_tput(&txq->drb_stats, traffic_stats_shift);
		noa_ncp_md_tx_drb_rel_ctrl(txq, tmp_tput);
	}
	mod_timer(&md_fw->ring_rel_ctrl_timer,
		  jiffies + msecs_to_jiffies(BIT(traffic_stats_shift)));
}

int noa_ncp_md_init(void *data)
{
	int ret = 0;
	struct noa_md_fw *md_fw;
	NCP_MD_INFO("enter");
#ifdef linux
	md_fw = (struct noa_md_fw *)data;
#else
	struct noa_md_protobuf *buf = (struct noa_md_protobuf *)data;
	md_fw = &md_fw_init;
	if (unlikely(!buf->hif))
		PW_CRASH("[%s][%d][%s] Invalid hif", __func__, __LINE__, __FILE__);
	md_fw->hif = buf->hif;
	md_fw->tx = &tx_init;
	md_fw->rx = &rx_init;
	md_fw->tx->ring_ops = &tx_ring_ops;
	md_fw->rx->ring_ops = &rx_ring_ops;

	md_fw->tx_buffer_pool.va_base = (void *)buf->tx_buffer_pool.va_base;
	md_fw->tx_buffer_pool.pa_base = buf->tx_buffer_pool.pa_base;
	md_fw->shared_mem_info.addr = buf->shared_mem_info.addr;
	md_fw->shared_mem_info.size = buf->shared_mem_info.size;

	noa_ncp_md_init_queues(md_fw, buf);
#endif
	md_fw->tx->ring_ops = modem_fw_ring_get_tx_ops();
	ret = noa_ncp_md_tx_init(md_fw);
	if (ret) {
		noa_ncp_md_tx_exit(md_fw);
	}

	md_fw->rx->ring_ops = modem_fw_ring_get_rx_ops();
	ret = noa_ncp_md_rx_init(md_fw);
	if (ret) {
		noa_ncp_md_rx_exit(md_fw);
	}

// TODO(b/442743782): Remove it after supporting dynamic switch for script.
#ifdef NOA_MODEM_DYNAMIC_SWITCH_DISABLED
       ret = noa_md_apc2ncp_ring_ncp_setup(NULL);
       if (ret) {
               noa_md_apc2ncp_ring_ncp_release();
       }
#endif

	auto& shmem_sync = noa::driver::modem::ncp::NcpShmemSync::Instance();
	const auto status = shmem_sync.Init({.md_fw = md_fw});
	if (!status.ok()) {
		NCP_MD_ERROR("NcpShmemSync Init failed: %d", (int)status.code());
		return -EFAULT;
	}

	shmem_sync.RegisterDataPathInterruptHandler(
		noa_ncp_md_apc2ncp_isr);
	shmem_sync.RegisterSwitchCommandHandler(
		noa_ncp_md_switch_handler);
	shmem_sync.RegisterDataSubCommandHandler(
		NOA_MD_SHMEM_DATA_CMD_IFINDEX_TABLE_UPDATE,
		noa_ncp_md_data_ifindex_update_handler);
	shmem_sync.RegisterDataSubCommandHandler(
		NOA_MD_SHMEM_DATA_CMD_CLDMA_OFFLOAD_CONFIG,
		noa_ncp_md_data_cldma_offload_config_handler);
	shmem_sync.RegisterDebugSubCommandHandler(
		NOA_MD_SHMEM_DEBUG_CMD_ENABLE,
		noa_ncp_md_debug_cmd_enable_handler);

	timer_setup(&md_fw->ring_rel_ctrl_timer, noa_ncp_md_ring_rel_ctrl_timer_func, 0);
	mod_timer(&md_fw->ring_rel_ctrl_timer,
		  jiffies + msecs_to_jiffies(BIT(traffic_stats_shift)));

	// Init atomic params for apc2ncp isr bitmask
	md_fw->tx->isr_apc2ncp_bitmask = 0;
	md_fw->rx->isr_refill_bitmask = 0;

	g_md_fw = md_fw;

	ret = noa_dpmaif_register_event_handler(noa_dpmaif_event_handler);
	if (ret < 0) {
		NCP_MD_ERROR("Failed to register event handler. ret=[%d]", ret);
		return ret;
	}

	NCP_MD_INFO("ret=[%d]", ret);
	return ret;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_ncp_md_init);
#endif

int noa_ncp_md_exit(void)
{
	NCP_MD_INFO("enter");
	if (!g_md_fw) {
		NCP_MD_ERROR("g_md_fw is NULL");
	}

	noa_ncp_md_tx_exit(g_md_fw);
	NCP_MD_INFO("noa_ncp_md_tx_exit");

	noa_ncp_md_rx_exit(g_md_fw);
	NCP_MD_INFO("noa_ncp_md_rx_exit");

// TODO(b/442743782): Remove it after supporting dynamic switch for script.
#ifdef NOA_MODEM_DYNAMIC_SWITCH_DISABLED
	noa_md_apc2ncp_ring_ncp_release();
	NCP_MD_INFO("noa_md_apc2ncp_ring_ncp_release");
#endif

	for (int ring_type = 0; ring_type < kNoaModemApcToNcpRingMax; ring_type++) {
		ncp_md_irq_unregister(ring_type, g_md_fw, true);
	}
	ncp_md_irq_exit(true);
	NCP_MD_INFO("ncp_md_irq_exit for NCP");

	// Explicitly deinitialize the shmem sync singleton to release its resources.
	noa::driver::modem::ncp::NcpShmemSync::Instance().Exit();

	g_md_fw = NULL;

	NCP_MD_INFO("exit");
	return 0;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_ncp_md_exit);
#endif
