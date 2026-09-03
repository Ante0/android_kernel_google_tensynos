// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <asm/io.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/types.h>

#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"
#include "ipsec_test_service/ipsec_test_service.pb.h"
#include "pw_rpc_service_client/ipsec_test_service_client.nanopb.h"

#include "google_dpa_services.h"

struct google_dpa_ipsec_ctx {
	struct google_dpa_rpc_context rpc_ctx;
};

/*
 * Callback invoked when a response is received. This is called synchronously
 * from Client::ProcessPacket.
 */
static void dpa_rpc_ipsec_service_execute_response_cb(PwRpcCall *call, const uint8_t *payload,
						      size_t payload_size, PwStatus status)
{
	struct google_dpa_ipsec_ctx *ctx = (struct google_dpa_ipsec_ctx *)call->context;

	ctx->rpc_ctx.status = status;
	complete(&ctx->rpc_ctx.complete);
}

int dpa_rpc_ipsec_service_execute(struct device *dev, PwRpcClient *client, const char *data,
				  size_t data_len, size_t offset, size_t total_len)
{
	noa_service_ipsec_test_service_SendDataCmd request = { 0 };
	struct google_dpa_ipsec_ctx ctx = { 0 };
	PwStatus status;
	uint32_t call_id;
	int err;

	init_google_dpa_rpc_context(&ctx.rpc_ctx, dev, client);

	/* Populate request */
	request.id = 0x45;
	request.offset = offset;
	request.total = total_len;
	memcpy(request.data.bytes, data, data_len);
	request.data.size = data_len;

	status = IpsecTestServiceSendData(client, &request,
					  dpa_rpc_ipsec_service_execute_response_cb, NULL, &ctx,
					  &call_id);

	if (status != kPwStatusOk) {
		/*
		 * The RPC call was not sent. This could occur due to, for example, an
		 * invalid channel ID. Handle if necessary.
		 */
		dev_err(dev, "Ipsec call is not sent (err=%d)", status);
		return -EIO;
	}

	err = dpa_wait_for_rpc_completion(&ctx.rpc_ctx, call_id);
	if (err)
		return err;

	return ctx.rpc_ctx.status;
}
