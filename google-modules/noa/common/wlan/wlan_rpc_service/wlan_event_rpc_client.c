// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA WLAN Event RPC Client
 *
 * Copyright (c) 2025 Google LLC.
 */

#include <linux/slab.h>
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_os.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/wlan_rpc_service_client.nanopb.h"
#include "wlan_event_rpc_client.h"
#include "wlan_rpc_service.pb.h"

static wlan_event_rpc_client client;

static void wlan_event_rpc_on_next_callback(struct PwRpcCallStruct *call, const uint8_t *payload,
					    size_t payload_size)
{
	noa_service_wlan_rpc_service_CmdResult result =
		noa_service_wlan_rpc_service_CmdResult_INVALID;
	noa_service_wlan_rpc_service_Request request;

	if (WlanRpcServiceEventDeserializeResponse(payload, payload_size, &request) !=
	    kPwStatusOk) {
		result = noa_service_wlan_rpc_service_CmdResult_FAILURE;
	}

	if (wlan_event_rpc_event_handler(request.id, request.msg.bytes, request.msg.size) ==
	    kPwStatusOk) {
		result = noa_service_wlan_rpc_service_CmdResult_SUCCESS;
	} else {
		result = noa_service_wlan_rpc_service_CmdResult_FAILURE;
	}

	noa_service_wlan_rpc_service_Response response;

	memset((void *)&response, 0, sizeof(response));
	response.result = result;
	WlanRpcServiceEventNext(call->client, call->call_id, &response);
}

static void wlan_event_rpc_on_complete_callback(struct PwRpcCallStruct *call,
						const uint8_t *UNUSED(payload),
						size_t UNUSED(payload_size),
						PwStatus UNUSED(status))
{
	WlanRpcServiceEventComplete(call->client, call->call_id);
}

static void wlan_event_rpc_on_error_callback(struct PwRpcCallStruct *UNUSED(call), PwStatus error)
{
	pr_err("%s: error status: %d", __func__, (uint32_t)error);
	return;
}

wlan_event_rpc_client *wlan_event_rpc_client_get_instance(void)
{
	return &client;
}

PwStatus wlan_event_rpc_client_init(PwRpcClient *rpc_client)
{
	mutex_init(&client.mutex);

	client.rpc_client = rpc_client;
	client.next_call_id = 0;

	return kPwStatusOk;
}

PwStatus wlan_event_rpc_client_deinit(void)
{
	if (wlan_event_rpc_client_close() != kPwStatusOk) {
		return kPwStatusInternal;
	}

	if (mutex_lock_interruptible(&client.mutex))
		return kPwStatusAborted;

	client.rpc_client = NULL;
	mutex_unlock(&client.mutex);

	return kPwStatusOk;
}

PwStatus wlan_event_rpc_client_open(void)
{
	if (mutex_lock_interruptible(&client.mutex))
		return kPwStatusAborted;

	if (client.rpc_client == NULL) {
		mutex_unlock(&client.mutex);
		return kPwStatusFailedPrecondition;
	}

	client.next_call_id = 0;
	PwStatus event_init_status =
		WlanRpcServiceEvent(client.rpc_client, wlan_event_rpc_on_next_callback,
				    wlan_event_rpc_on_complete_callback,
				    wlan_event_rpc_on_error_callback, (void *)&client,
				    &(client.next_call_id));

	if (event_init_status == kPwStatusOk) {
		pr_info("%s: wlan event init done", __func__);
	} else {
		pr_warn("%s: wlan event init failed", __func__);
	}

	mutex_unlock(&client.mutex);

	return kPwStatusOk;
}

PwStatus wlan_event_rpc_client_close(void)
{
	if (mutex_lock_interruptible(&client.mutex))
		return kPwStatusAborted;

	if (client.rpc_client != NULL &&
	    WlanRpcServiceEventComplete(client.rpc_client, client.next_call_id) != kPwStatusOk) {
		mutex_unlock(&client.mutex);
		return kPwStatusAborted;
	}

	mutex_unlock(&client.mutex);

	return kPwStatusOk;
}

PwStatus wlan_event_rpc_event_handler(const uint32_t event_id, const void *event_data,
				      size_t event_data_size)
{
	if (mutex_lock_interruptible(&client.mutex))
		return kPwStatusAborted;

	if (client.callback != NULL && client.callback(event_id, event_data, event_data_size)) {
		mutex_unlock(&client.mutex);
		return kPwStatusAborted;
	}

	mutex_unlock(&client.mutex);

	return kPwStatusOk;
}

void wlan_event_rpc_client_register_event_callback(event_callback callback)
{
	if (mutex_lock_interruptible(&client.mutex))
		return;

	client.callback = callback;
	mutex_unlock(&client.mutex);
}
