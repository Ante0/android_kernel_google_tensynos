// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/jiffies.h>

#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"

#include "google_dpa_services.h"

#define DPA_RPC_TIMEOUT_MSEC 1000

/* Wait for the completion event and abandon the RPC if we time out waiting */
int dpa_wait_for_rpc_completion_timeout(struct google_dpa_rpc_context *ctx, uint32_t call_id,
					unsigned int msec)
{
	long remaining_time;
	PwStatus status;

	remaining_time = wait_for_completion_timeout(&ctx->complete, msecs_to_jiffies(msec));
	if (remaining_time != 0)
		/* RPC successfully completed before timeout */
		return 0;

	status = PwRpcClientDropCall(ctx->client, call_id);
	if (status == kPwStatusInvalidArgument) {
		dev_warn(ctx->dev,
			 "RPC timed out and client is no longer valid. This may happen if the remote crashed.");
		return -ETIMEDOUT;
	} else if (status == kPwStatusNotFound) {
		if (!try_wait_for_completion(&ctx->complete)) {
			dev_err(ctx->dev,
				"RPC timed out and PwRPC doesn't know about the call id. This is very unexpected.");
			return -EIO;
		}
		/* RPC arrived right after we timed out, this is not an error */
		return 0;
	}

	dev_warn(ctx->dev, "RPC request timed out");
	return -ETIMEDOUT;
}

/* Wait for the completion event and abandon the RPC if we time out waiting */
int dpa_wait_for_rpc_completion(struct google_dpa_rpc_context *ctx, uint32_t call_id)
{
	return dpa_wait_for_rpc_completion_timeout(ctx, call_id, DPA_RPC_TIMEOUT_MSEC);
}
