// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#include <asm/io.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/types.h>

#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/shell_cmd_service_client.nanopb.h"
#include "shell_cmd_service/shell_cmd_service.pb.h"

#include "google_dpa_services.h"

struct google_dpa_shell_cmd_ctx {
	struct google_dpa_rpc_context rpc_ctx;
	int result;
};

static void dpa_rpc_shell_cmd_sync_response_cb(PwRpcCall *call, const uint8_t *payload,
					       size_t payload_size, PwStatus status)
{
	struct google_dpa_shell_cmd_ctx *ctx = (struct google_dpa_shell_cmd_ctx *)call->context;
	noa_service_shell_cmd_service_Response response = { 0 };

	PwRpcClientDeserializeResponse(payload, payload_size,
				       noa_service_shell_cmd_service_Response_fields, &response);
	ctx->rpc_ctx.status = status;
	/* response.result is only set even when the RPC status isn't OK...*/
	if (status != kPwStatusOk)
		dev_info(ctx->rpc_ctx.dev, "Shell cmd RPC failed with CmdResult: %d",
			 response.result);
	complete(&ctx->rpc_ctx.complete);
}

int dpa_rpc_shell_cmd_sync(struct device *dev, PwRpcClient *client, const char *cmd)
{
	noa_service_shell_cmd_service_Request request = { 0 };
	struct google_dpa_shell_cmd_ctx ctx = { 0 };
	PwStatus status;
	uint32_t call_id;
	int err;

	init_google_dpa_rpc_context(&ctx.rpc_ctx, dev, client);

	err = strscpy(request.cmd_string, cmd, sizeof(request.cmd_string));
	if (err < 0)
		return err;
	dev_info(dev, "cmd: %s", request.cmd_string);

	status = ShellCmdServiceExecute(client, &request, dpa_rpc_shell_cmd_sync_response_cb, NULL,
					&ctx, &call_id);
	if (status != kPwStatusOk) {
		dev_err(dev, "shell_cmd call is not sent (err=%d)", status);
		return -EIO;
	}

	err = dpa_wait_for_rpc_completion(&ctx.rpc_ctx, call_id);
	if (err)
		return err;

	if (ctx.rpc_ctx.status != kPwStatusOk)
		return -EIO;

	return 0;
}
