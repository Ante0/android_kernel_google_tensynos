// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#include <asm-generic/errno-base.h>
#include <asm/io.h>
#include <linux/completion.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/types.h>

#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/power_service_client.nanopb.h"

#include "google_dpa_services.h"

#define POWEROFF_DELAY ((uint32_t)500)

struct google_dpa_power_ctx {
	struct google_dpa_rpc_context rpc_ctx;
	noa_service_power_service_Response response;
};

static void dpa_rpc_power_service_poweroff_response_cb(PwRpcCall *call, const uint8_t *payload,
						       size_t payload_size, PwStatus status)
{
	struct google_dpa_power_ctx *ctx = (struct google_dpa_power_ctx *)call->context;
	PwRpcClientDeserializeResponse(payload, payload_size,
				       noa_service_power_service_Response_fields, &ctx->response);
	ctx->rpc_ctx.status = status;
	complete(&ctx->rpc_ctx.complete);
}

int dpa_rpc_power_service_poweroff(struct device *dev, PwRpcClient *client)
{
	noa_service_power_service_Request request = { 0 };
	struct google_dpa_power_ctx ctx = { 0 };
	PwStatus status;
	uint32_t call_id;
	int err;

	init_google_dpa_rpc_context(&ctx.rpc_ctx, dev, client);

	request.delay = POWEROFF_DELAY;

	status = PowerServicePowerOff(client, &request, dpa_rpc_power_service_poweroff_response_cb,
				      NULL, &ctx, &call_id);
	if (status != kPwStatusOk) {
		dev_err(dev, "PowerOff call is not sent (err=%d)", status);
		return -EIO;
	}

	err = dpa_wait_for_rpc_completion(&ctx.rpc_ctx, call_id);
	if (err)
		return err;

	if (ctx.rpc_ctx.status != kPwStatusOk) {
		dev_err(dev, "PowerOff call sent but got response (result=%d)\n",
			ctx.response.result);
		return -EIO;
	}

	return ctx.rpc_ctx.status;
}
