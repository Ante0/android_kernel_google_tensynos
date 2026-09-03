// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <asm/io.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/types.h>

#include "modem_cmd_service.pb.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_service_client/modem_cmd_service_client.nanopb.h"
#include "soc/google/google_dpa_rpc.h"
#include "soc/google/google_dpa_service_modem_cmd.h"

#include "google_dpa_services.h"

struct google_dpa_modem_service_ctx {
	struct google_dpa_rpc_context rpc_ctx;
	noa_service_modem_cmd_service_CmdResult result;
};

static void dpa_rpc_modem_cmd_service_fw_init_response_cb(PwRpcCall *call,
							 const uint8_t *payload,
							 size_t payload_size,
							 PwStatus status)
{
	struct google_dpa_modem_service_ctx *ctx =
		(struct google_dpa_modem_service_ctx *)call->context;
	struct device *dev = ctx->rpc_ctx.dev;
	noa_service_modem_cmd_service_Response response = { 0 };

	dev_dbg(dev, "Execute payload %p size %zu status %d", payload, payload_size, status);

	PwRpcClientDeserializeResponse(
		payload, payload_size,
		noa_service_modem_cmd_service_Response_fields, &response);
	ctx->rpc_ctx.status = status;
	ctx->result = response.result;
	complete(&ctx->rpc_ctx.complete);
}


int dpa_rpc_modem_cmd_service_fw_init(struct device *dev, const struct noa_md_fw_init *fw_info)
{
	noa_service_modem_cmd_service_InitRequest request = { 0 };
	struct google_dpa_modem_service_ctx ctx = { 0 };
	PwRpcClient *client = google_dpa_rpc_ncp_client();
	PwStatus status;
	uint32_t call_id;
	int err;

	init_google_dpa_rpc_context(&ctx.rpc_ctx, dev, client);

	for (int index = 0; index < NOA_MD_MAX_UL_QUEUE_SIZE; index++) {
		request.txq_drb_base[index] = fw_info->txqs[index].drb_base;
		request.txq_drb_dpa_base[index] = fw_info->txqs[index].drb_dpa_base;
		request.txq_drb_count[index] = fw_info->txqs[index].drb_cnt;
		request.txq_doorbell_delay_millisecond[index] = fw_info->txqs[index].db_delay_ms;
		request.txq_burst_submit_count[index] = fw_info->txqs[index].burst_submit_cnt;
		request.drb_wr_idx[index] = fw_info->txqs[index].drb_wr_idx;
		request.drb_rd_idx[index] = fw_info->txqs[index].drb_rd_idx;
		request.drb_rel_rd_idx[index] = fw_info->txqs[index].drb_rel_rd_idx;
	}

	for (int index = 0; index < NOA_MD_MAX_DL_QUEUE_SIZE; index++) {
		request.rxq_pit_base[index] = fw_info->rxqs[index].pit_base;
		request.rxq_pit_dpa_base[index] = fw_info->rxqs[index].pit_dpa_base;
		request.rxq_pit_count[index] = fw_info->rxqs[index].pit_cnt;
		request.rxq_pit_seq_max[index] = fw_info->rxqs[index].pit_seq_max;
		request.rxq_bat_ring_id[index] = fw_info->rxqs[index].bat_ring_id;
		request.pit_wr_idx[index] = fw_info->rxqs[index].pit_wr_idx;
		request.pit_rd_idx[index] = fw_info->rxqs[index].pit_rd_idx;
		request.pit_rel_rd_idx[index] = fw_info->rxqs[index].pit_rel_rd_idx;
	}

	request.bat_buf_size = fw_info->bat_infos[0].normal_bat.buf_size;
	for (int index = 0; index < NOA_MD_MAX_BAT_SIZE; index++) {
		request.bat_base[index] = fw_info->bat_infos[index].normal_bat.bat_base;
		request.bat_dpa_base[index] = fw_info->bat_infos[index].normal_bat.bat_dpa_base;
		request.bat_count[index] = fw_info->bat_infos[index].normal_bat.bat_cnt;
		request.bat_reload_count[index] = fw_info->bat_infos[index].normal_bat.reload_cnt;
		request.bat_wr_idx[index] = fw_info->bat_infos[index].normal_bat.bat_wr_idx;
		request.bat_rd_idx[index] = fw_info->bat_infos[index].normal_bat.bat_rd_idx;
		request.bat_mask_table_dpa_base[index] =
			fw_info->bat_infos[index].normal_bat.mask_table_dpa_base;
		request.bat_tkid_table_dpa_base[index] =
			fw_info->bat_infos[index].normal_bat.tkid_table_dpa_base;
		request.bat_buffer_table_dpa_base[index] =
			fw_info->bat_infos[index].normal_bat.buffer_table_dpa_base;
	}

	request.frag_bat_buf_size = fw_info->bat_infos[0].frag_bat.buf_size;
	for (int index = 0; index < NOA_MD_MAX_FRAG_BAT_SIZE; index++) {
		request.frag_bat_base[index] = fw_info->bat_infos[index].frag_bat.bat_base;
		request.frag_bat_dpa_base[index] = fw_info->bat_infos[index].frag_bat.bat_dpa_base;
		request.frag_bat_count[index] = fw_info->bat_infos[index].frag_bat.bat_cnt;
		request.frag_bat_reload_count[index] =
			fw_info->bat_infos[index].frag_bat.reload_cnt;
		request.frag_bat_wr_idx[index] = fw_info->bat_infos[index].frag_bat.bat_wr_idx;
		request.frag_bat_rd_idx[index] = fw_info->bat_infos[index].frag_bat.bat_rd_idx;
		request.frag_bat_mask_table_dpa_base[index] =
			fw_info->bat_infos[index].frag_bat.mask_table_dpa_base;
		request.frag_bat_tkid_table_dpa_base[index] =
			fw_info->bat_infos[index].frag_bat.tkid_table_dpa_base;
		request.frag_bat_buffer_table_dpa_base[index] =
			fw_info->bat_infos[index].frag_bat.buffer_table_dpa_base;
	}

	request.tx_buffer_pool_base = fw_info->pool.va_base;
	request.tx_buffer_pool_dpa_base = fw_info->pool.pa_base;

	request.shared_memory_addr = fw_info->shared_mem_info.addr;
	request.shared_memory_size = fw_info->shared_mem_info.size;

	if (fw_info->hif_config) {
		request.has_hif_config = true;
		request.hif_config = *fw_info->hif_config;
	}

	status = ModemCmdServiceInitCommand(
					client, &request,
					dpa_rpc_modem_cmd_service_fw_init_response_cb,
					NULL, &ctx, &call_id);

	if (status != kPwStatusOk) {
		dev_err(dev, "ModemCmd call is not sent");
		return -EIO;
	}

	// Set timeout as 5 sec
	err = dpa_wait_for_rpc_completion_timeout(&ctx.rpc_ctx, call_id, 5000);
	if (err)
		return err;

	if (ctx.rpc_ctx.status != kPwStatusOk) {
		dev_err(dev, "ModemCmd execute failed with status %d", status);
		return -EIO;
	}

	dev_dbg(dev, "Received modem cmd result: %d",(int)ctx.result);

	return ctx.result == noa_service_modem_cmd_service_CmdResult_SUCCESS ? 0 : -EIO;
}
EXPORT_SYMBOL_GPL(dpa_rpc_modem_cmd_service_fw_init);
