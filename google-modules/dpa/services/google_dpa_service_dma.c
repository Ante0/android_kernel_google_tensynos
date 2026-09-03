
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <asm/io.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/types.h>

#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/dma_service_client.nanopb.h"

#include "google_dpa_services.h"

struct google_dpa_dma_read_word_context {
	struct google_dpa_rpc_context rpc_ctx;
	u32 val;
};

static void dpa_rpc_dma_service_read_word_response_cb(PwRpcCall *call, const uint8_t *payload,
						      size_t payload_size, PwStatus status)
{
	struct google_dpa_dma_read_word_context *ctx = call->context;
	noa_service_dma_service_ReadWordResponse msg = { 0 };

	PwRpcClientDeserializeResponse(payload, payload_size,
				       noa_service_dma_service_ReadWordResponse_fields, &msg);
	ctx->rpc_ctx.status = status;
	if (status == kPwStatusOk)
		ctx->val = msg.val;

	complete(&ctx->rpc_ctx.complete);
}

int dpa_rpc_dma_service_read_word_sync(struct device *dev, PwRpcClient *client, u32 dma_handle,
				       u32 *val)
{
	noa_service_dma_service_ReadRequest request = { 0 };
	struct google_dpa_dma_read_word_context ctx = { 0 };
	PwStatus status;
	uint32_t call_id;
	int err;

	init_google_dpa_rpc_context(&ctx.rpc_ctx, dev, client);
	request.dma_addr = dma_handle;
	status = DmaServiceReadWord(client, &request, dpa_rpc_dma_service_read_word_response_cb,
				    NULL, &ctx, &call_id);

	if (status != kPwStatusOk) {
		dev_err(dev, "DmaReadWord call is not sent (err=%d)", status);
		return -EIO;
	}
	err = dpa_wait_for_rpc_completion(&ctx.rpc_ctx, call_id);
	if (err)
		return err;

	if (ctx.rpc_ctx.status)
		return -EIO;

	*val = ctx.val;

	return 0;
}

static void dpa_rpc_dma_service_copy_data_response_cb(PwRpcCall *call, const uint8_t *payload,
						      size_t payload_size, PwStatus status)
{
	struct google_dpa_rpc_context *ctx = call->context;
	noa_service_dma_service_CopyDataResponse msg = { 0 };

	PwRpcClientDeserializeResponse(payload, payload_size,
				       noa_service_dma_service_CopyDataResponse_fields, &msg);
	ctx->status = status;

	complete(&ctx->complete);
}

int dpa_rpc_dma_service_copy_data_sync(struct device *dev, PwRpcClient *client, u32 dma_dst_addr,
				       u32 dma_src_addr, u32 count)
{
	noa_service_dma_service_CopyDataRequest request = { 0 };
	struct google_dpa_rpc_context ctx = { 0 };
	PwStatus status;
	uint32_t call_id;
	int err;

	init_google_dpa_rpc_context(&ctx, dev, client);
	request.dma_src_addr = dma_src_addr;
	request.dma_dst_addr = dma_dst_addr;
	request.count = count;
	status = DmaServiceCopyData(client, &request, dpa_rpc_dma_service_copy_data_response_cb,
				    NULL, &ctx, &call_id);

	if (status != kPwStatusOk) {
		dev_err(dev, "DmaCopyData call is not sent (err=%d)", status);
		return -EIO;
	}

	err = dpa_wait_for_rpc_completion(&ctx, call_id);
	if (err)
		return err;

	if (ctx.status)
		return -EIO;

	return 0;
}

int dpa_rpc_dma_service_dma_copy_data_sync(struct device *dev, PwRpcClient *client,
					   u32 dma_dst_addr, u32 dma_src_addr, u32 count,
					   u32 dma_channel, u32 dma_burst_size,
					   u32 dma_burst_length)
{
	noa_service_dma_service_DmaCopyDataRequest request = { 0 };
	struct google_dpa_rpc_context ctx = { 0 };
	PwStatus status;
	uint32_t call_id;
	int err;

	init_google_dpa_rpc_context(&ctx, dev, client);
	request.dma_src_addr = dma_src_addr;
	request.dma_dst_addr = dma_dst_addr;
	request.dma_channel = dma_channel;
	request.dma_size = count;
	request.dma_burst_length = dma_burst_length;
	request.dma_burst_size = dma_burst_size;
	status = DmaServiceDmaCopyData(client, &request, dpa_rpc_dma_service_copy_data_response_cb,
				       NULL, &ctx, &call_id);

	if (status != kPwStatusOk) {
		dev_err(dev, "DmaCopyData call is not sent (err=%d)", status);
		return -EIO;
	}

	err = dpa_wait_for_rpc_completion(&ctx, call_id);
	if (err)
		return err;

	if (ctx.status)
		return -EIO;

	return 0;
}
