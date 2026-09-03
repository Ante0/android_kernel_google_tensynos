// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <asm/io.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "google_dpa_netlink.h"
#include "google_dpa_services.h"
#include "pb_decode.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/tea_rpc_service_client.nanopb.h"

static bool handle_event_entry(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	struct google_dpa *dpa = (struct google_dpa *)*arg;
	size_t size = stream->bytes_left;
	/* TODO: build a netlink sock buffer to avoid the malloc. */
	u8 *data = kmalloc(size, GFP_KERNEL);

	if (!data)
		return false;

	if (!pb_read(stream, data, size)) {
		kfree(data);
		return false;
	}

	google_dpa_netlink_send_data(dpa, data, size);
	kfree(data);

	return true;
}

static void on_next(struct PwRpcCallStruct *call, const uint8_t *payload, size_t payload_size)
{
	struct google_dpa *dpa = (struct google_dpa *)call->context;
	noa_service_tea_rpc_service_PushEventsResponse response =
		noa_service_tea_rpc_service_PushEventsResponse_init_zero;

	response.events.funcs.decode = handle_event_entry;
	response.events.arg = dpa;

	PwStatus status =
		PwRpcClientDeserializeResponse(payload, payload_size,
					       noa_service_tea_rpc_service_PushEventsResponse_fields,
					       &response);

	if (status != kPwStatusOk)
		dev_err(dpa->dev, "Error deserializing PushEventsResponse: %d", status);
}

static void on_error(struct PwRpcCallStruct *call, PwStatus error)
{
	struct google_dpa *dpa = (struct google_dpa *)call->context;

	dev_err(dpa->dev, "TEA RPC Service PushEvents: %s: %d", __func__, error);
}

int dpa_rpc_tea_service_push_events(PwRpcClient *client, const struct google_dpa *dpa)
{
	noa_service_tea_rpc_service_PushEventsRequest request = {};
	PwStatus status = TeaRpcServicePushEvents(client, &request, on_next, NULL, on_error,
						  (void *)dpa, NULL);
	if (status != kPwStatusOk) {
		dev_err(dpa->dev, "TeaRpcService PushEvents not sent(status = %d)", status);
		return -EIO;
	}
	return 0;
}
