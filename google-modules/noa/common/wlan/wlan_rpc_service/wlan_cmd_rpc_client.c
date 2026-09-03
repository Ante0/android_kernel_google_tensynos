// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA WLAN Command RPC Client
 *
 * Copyright (c) 2025 Google LLC.
 */

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/list.h>
#include "pw_rpc_c/pw_rpc_os.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/wlan_rpc_service_client.nanopb.h"
#include "wlan_cmd_rpc_client.h"
#include "wlan_rpc_call_entry.h"

#define APC_NOA_DRAM_OFFSET 0x20000000

typedef enum wlan_cmd_rpc_state {
	WLAN_CMD_RPC_STATE_UNREADY = 0,
	WLAN_CMD_RPC_STATE_READY,
} wlan_cmd_rpc_state;

typedef struct wlan_cmd_rpc_client {
	struct mutex mutex;
	struct mutex rpc_call_list_lock;
	struct completion compl;
	PwRpcClient *rpc_client;
	struct list_head rpc_call_list;
	enum wlan_cmd_rpc_state state;
} wlan_cmd_rpc_client;

static wlan_cmd_rpc_client client;

static void wlan_cmd_rpc_set_state(wlan_cmd_rpc_client *client, wlan_cmd_rpc_state state)
{
	client->state = state;
}

static wlan_cmd_rpc_state wlan_cmd_rpc_get_state(wlan_cmd_rpc_client *client)
{
	return client->state;
}

static void wlan_cmd_rpc_call_list_add(wlan_rpc_call_entry *rpc_call)
{
	mutex_lock(&client.rpc_call_list_lock);
	list_add(&rpc_call->rpc_call_list, &client.rpc_call_list);
	mutex_unlock(&client.rpc_call_list_lock);
}

static void wlan_cmd_rpc_call_list_remove(wlan_rpc_call_entry *rpc_call)
{
	mutex_lock(&client.rpc_call_list_lock);
	list_del(&rpc_call->rpc_call_list);
	mutex_unlock(&client.rpc_call_list_lock);
}

void wlan_cmd_rpc_notify_callback(void *context, PwStatus UNUSED(status),
				  const void *UNUSED(response), size_t UNUSED(response_size))
{
	complete(&(((wlan_cmd_rpc_client *)context)->compl));
}

void wlan_cmd_rpc_on_complete_callback(struct PwRpcCallStruct *call, const uint8_t *payload,
				       size_t payload_size, PwStatus status)
{
	noa_service_wlan_rpc_service_Response response;
	wlan_rpc_call_entry *rpc_call = (wlan_rpc_call_entry *)call->context;

	if (mutex_lock_interruptible(&client.mutex))
		return;

	if (wlan_cmd_rpc_get_state(&client) == WLAN_CMD_RPC_STATE_UNREADY) {
		mutex_unlock(&client.mutex);
		return;
	}

	if (status != kPwStatusOk) {
		wlan_rpc_call_entry_invoke_callbacks(rpc_call, status, NULL, 0);
	} else {
		PwStatus deserialize_status =
			WlanRpcServiceCommandDeserializeResponse(payload, payload_size, &response);

		if (deserialize_status != kPwStatusOk) {
			wlan_rpc_call_entry_invoke_callbacks(rpc_call, deserialize_status, NULL, 0);
		} else {
			wlan_rpc_call_entry_invoke_callbacks(
				rpc_call,
				response.result == noa_service_wlan_rpc_service_CmdResult_SUCCESS ?
					kPwStatusOk :
					kPwStatusInternal,
				response.msg.bytes, response.msg.size);
		}
	}

	wlan_cmd_rpc_call_list_remove(rpc_call);
	wlan_rpc_call_entry_deinit(rpc_call);
	kfree(rpc_call);

	mutex_unlock(&client.mutex);
}

void wlan_cmd_rpc_on_error_callback(struct PwRpcCallStruct *call, PwStatus error)
{
	wlan_rpc_call_entry *rpc_call = (wlan_rpc_call_entry *)call->context;

	if (mutex_lock_interruptible(&client.mutex))
		return;

	if (wlan_cmd_rpc_get_state(&client) == WLAN_CMD_RPC_STATE_UNREADY) {
		mutex_unlock(&client.mutex);
		return;
	}

	wlan_rpc_call_entry_invoke_callbacks(rpc_call, error, NULL, 0);
	wlan_cmd_rpc_call_list_remove(rpc_call);
	wlan_rpc_call_entry_deinit(rpc_call);
	kfree(rpc_call);

	mutex_unlock(&client.mutex);
}

PwStatus wlan_cmd_rpc_client_init(PwRpcClient *rpc_client)
{
	init_completion(&client.compl);
	mutex_init(&client.mutex);
	mutex_init(&client.rpc_call_list_lock);
	INIT_LIST_HEAD(&client.rpc_call_list);

	if (mutex_lock_interruptible(&client.mutex))
		return kPwStatusAborted;

	client.rpc_client = rpc_client;
	complete(&client.compl);
	wlan_cmd_rpc_set_state(&client, WLAN_CMD_RPC_STATE_READY);
	mutex_unlock(&client.mutex);

	pr_info("%s: wlan cmd rpc client init done", __func__);

	return kPwStatusOk;
}

PwStatus wlan_cmd_rpc_client_deinit(void)
{
	struct list_head *pos, *tmp;
	wlan_rpc_call_entry *rpc_call = NULL;

	if (mutex_lock_interruptible(&client.mutex))
		return kPwStatusAborted;

	wlan_cmd_rpc_set_state(&client, WLAN_CMD_RPC_STATE_UNREADY);
	client.rpc_client = NULL;

	list_for_each_safe (pos, tmp, &client.rpc_call_list) {
		rpc_call = list_entry(pos, struct wlan_rpc_call_entry, rpc_call_list);
		wlan_rpc_call_entry_invoke_callbacks(rpc_call, kPwStatusInternal, NULL, 0);
		wlan_cmd_rpc_call_list_remove(rpc_call);
		wlan_rpc_call_entry_deinit(rpc_call);
		kfree(rpc_call);
	}

	complete(&client.compl);
	mutex_unlock(&client.mutex);

	return kPwStatusOk;
}

PwStatus wlan_cmd_rpc_client_send_command(const uint32_t cmd, const void *msg,
					  const uint32_t msg_len, wlan_cmd_rpc_callback callback,
					  void *context)
{
	wait_for_completion_interruptible(&client.compl);

	if (mutex_lock_interruptible(&client.mutex))
		return kPwStatusAborted;

	if (client.rpc_client == NULL ||
	    wlan_cmd_rpc_get_state(&client) == WLAN_CMD_RPC_STATE_UNREADY) {
		mutex_unlock(&client.mutex);
		return kPwStatusFailedPrecondition;
	}

	wlan_rpc_call_entry *rpc_call =
		(wlan_rpc_call_entry *)kmalloc(sizeof(wlan_rpc_call_entry), GFP_KERNEL);

	if (rpc_call == NULL) {
		mutex_unlock(&client.mutex);
		return kPwStatusResourceExhausted;
	}

	wlan_rpc_call_entry_init(rpc_call);
	wlan_cmd_rpc_call_list_add(rpc_call);
	wlan_rpc_call_entry_add_callbacks(rpc_call, callback, context);

	noa_service_wlan_rpc_service_Request request;
	memset((void *)&request, 0, sizeof(noa_service_wlan_rpc_service_Request));
	request.id = cmd;

	if (msg && msg_len) {
		memcpy((void *)request.msg.bytes, msg, msg_len);
		request.msg.size = msg_len;
	}

	if (wlan_rpc_call_entry_add_callbacks(rpc_call, wlan_cmd_rpc_notify_callback,
					      (void *)&client) != kPwStatusOk) {
		wlan_cmd_rpc_call_list_remove(rpc_call);
		wlan_rpc_call_entry_deinit(rpc_call);
		kfree(rpc_call);
		complete(&client.compl);
		mutex_unlock(&client.mutex);
		return kPwStatusInternal;
	}

	PwStatus status =
		WlanRpcServiceCommand(client.rpc_client, &request,
				      wlan_cmd_rpc_on_complete_callback,
				      wlan_cmd_rpc_on_error_callback, (void *)rpc_call, NULL);

	if (status != kPwStatusOk) {
		pr_info("%s: wlan cmd rpc client send command failed", __func__);
		wlan_cmd_rpc_call_list_remove(rpc_call);
		wlan_rpc_call_entry_deinit(rpc_call);
		kfree(rpc_call);
		complete(&client.compl);
	}

	mutex_unlock(&client.mutex);

	return status;
}
