// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#include <asm/io.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/types.h>

#include "google_dpa_services.h"
#include "pw_log/proto/log.pb.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/pw_log_service_client.nanopb.h"

static void listen_on_complete(PwRpcCall *call, const uint8_t *payload, size_t payload_size,
			       PwStatus status)
{
	struct google_dpa_log_info *cur_log_info = (struct google_dpa_log_info *)call->context;
	struct device *dev = cur_log_info->dev;

	dev_dbg(dev, "Listen complete\n");
}

static void listen_on_error(PwRpcCall *call, PwStatus error)
{
	struct google_dpa_log_info *cur_log_info = (struct google_dpa_log_info *)call->context;
	struct device *dev = cur_log_info->dev;

	dev_err(dev, "Listen error %d\n", error);
}

int dpa_rpc_pwlog_service_listen(PwRpcClient *client, struct google_dpa_log_info *info)
{
	struct device *dev = info->dev;
	pw_log_LogRequest request = {
		.dummy_field = '\0',
	};
	PwStatus status = PwLogServiceListen(client, &request, info->pwlog_listen_on_next,
					     listen_on_complete, listen_on_error, (void *)info,
					     &info->call_id);
	if (status != kPwStatusOk) {
		dev_err(dev, "Listen call is not sent (status = %d)\n", status);
		return -EIO;
	}
	return 0;
}

int dpa_rpc_pwlog_service_listen_cancel(PwRpcClient *client, struct google_dpa_log_info *info)
{
	PwStatus status = PwLogServiceListenCancel(client, info->call_id);
	if (status != kPwStatusOk) {
		return -EINVAL;
	}
	return 0;
}
