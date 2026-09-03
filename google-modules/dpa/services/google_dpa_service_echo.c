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

#include "pw_rpc/echo.pb.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/echo_service_client.nanopb.h"

#include "google_dpa_services.h"

// When the log forwarding is enabled, lots of booting logs might delay
// the echo response. Use a loose timeout to avoid false alarms.
#define DPA_HEALTH_CHECK_TIMEOUT_MSEC 3000

struct google_dpa_echo_ctx {
	struct google_dpa_rpc_context rpc_ctx;
	char *resp_msg;
};

static void dpa_rpc_echo_service_echo_sync_response_cb(PwRpcCall *call, const uint8_t *payload,
						       size_t payload_size, PwStatus status)
{
	struct google_dpa_echo_ctx *ctx = (struct google_dpa_echo_ctx *)call->context;
	pw_rpc_EchoMessage msg = { 0 };

	PwRpcClientDeserializeResponse(payload, payload_size, pw_rpc_EchoMessage_fields, &msg);
	ctx->rpc_ctx.status = status;
	if (status == kPwStatusOk)
		ctx->resp_msg = kstrdup(msg.msg, GFP_KERNEL);
	complete(&ctx->rpc_ctx.complete);
}

char *dpa_rpc_echo_service_sync(struct device *dev, PwRpcClient *client, const char *msg)
{
	pw_rpc_EchoMessage request = { 0 };
	struct google_dpa_echo_ctx ctx = { 0 };
	PwStatus status;
	uint32_t call_id;
	int err;

	init_google_dpa_rpc_context(&ctx.rpc_ctx, dev, client);

	err = strscpy(request.msg, msg, sizeof(request.msg));
	if (err < 0)
		return ERR_PTR(err);

	status = EchoServiceEcho(client, &request, dpa_rpc_echo_service_echo_sync_response_cb, NULL,
				 &ctx, &call_id);
	if (status != kPwStatusOk) {
		dev_err(dev, "Echo call is not sent (err=%d)", status);
		return ERR_PTR(-EIO);
	}

	err = dpa_wait_for_rpc_completion_timeout(&ctx.rpc_ctx, call_id,
						  DPA_HEALTH_CHECK_TIMEOUT_MSEC);
	if (err)
		return ERR_PTR(err);

	if (ctx.rpc_ctx.status != kPwStatusOk)
		return ERR_PTR(-EIO);

	if (!ctx.resp_msg)
		return ERR_PTR(-ENOMEM);

	return ctx.resp_msg;
}

static const char check_health_msg[] = "check_health";

int dpa_rpc_check_health(struct device *dev, PwRpcClient *client)
{
	char *resp;
	int ret = 0;

	if (!client->rpc_channel_id)
		return -ENODEV;

	resp = dpa_rpc_echo_service_sync(dev, client, check_health_msg);
	if (IS_ERR(resp))
		return PTR_ERR(resp);

	if (strcmp(resp, check_health_msg)) {
		dev_err(dev, "Unexpected echo reply contents. Expected %s got %s", check_health_msg,
			resp);
		ret = -EIO;
	}

	kfree(resp);
	return ret;
}
