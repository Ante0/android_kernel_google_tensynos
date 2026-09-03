// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA NEP Event RPC Client
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kimi Luinata <kimiluinata@google.com>
 */

#include "nep_event_rpc_client.h"

#include <linux/mutex.h>

#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/nep_cmd_rpc_service_client.nanopb.h"

// The nep_event_rpc_client instance we use throughout the RPC.
static nep_event_rpc_client client;

// Handles the stream of events.
static void nep_event_rpc_on_next_callback(struct PwRpcCallStruct* call,
                                    const uint8_t* payload,
                                    size_t payload_size)
{
  noa_service_nep_cmd_service_CmdResult result =
      noa_service_nep_cmd_service_CmdResult_INVALID;
  noa_service_nep_cmd_service_Request request;
  if (NepCmdRpcServiceEventDeserializeResponse(payload, payload_size,
                                               &request) != kPwStatusOk) {
    result = noa_service_nep_cmd_service_CmdResult_FAILURE;
  }

  pr_info("%s: Call to netengine driver, cmd %d", __func__,
              request.cmd);
  // Handle event request
  if (nep_event_rpc_event_handler(request.cmd, request.msg.bytes, request.msg.size) ==
      kPwStatusOk) {
    result = noa_service_nep_cmd_service_CmdResult_SUCCESS;
  } else {
    result = noa_service_nep_cmd_service_CmdResult_FAILURE;
  }
  pr_info("%s: done, with status %d", __func__, result);

  // To next event.
  noa_service_nep_cmd_service_Response response;
  response.result = result;
  NepCmdRpcServiceEventNext(call->client, call->call_id, &response);
}

static void nep_event_rpc_on_complete_callback(struct PwRpcCallStruct* call,
                                        const uint8_t* __attribute__((__unused__)) payload,
                                        size_t __attribute__((__unused__)) payload_size,
                                        PwStatus status)
{
  pr_info("%s: done, with status %d", __func__, status);
  NepCmdRpcServiceEventComplete(call->client, call->call_id);
}

static void nep_event_rpc_on_error_callback(struct PwRpcCallStruct*
                        __attribute__((__unused__)) call, PwStatus error)
{
  pr_err("%s: error status: %d", __func__, (uint32_t)error);
}

void nep_event_rpc_client_init(PwRpcClient* rpc_client)
{
  mutex_init(&client.mutex);
  if (mutex_lock_interruptible(&client.mutex)) {
		return;
  }

  client.rpc_client = rpc_client;
  client.next_call_id = 0;
  mutex_unlock(&client.mutex);

  pr_info("%s: nep event client init done", __func__);
}

PwStatus nep_event_rpc_client_deinit(void)
{
  if (nep_event_rpc_client_close() != kPwStatusOk) {
    return kPwStatusInternal;
  }
  if (mutex_lock_interruptible(&client.mutex)) {
		return kPwStatusAborted;
  }
  client.rpc_client = NULL;
  mutex_unlock(&client.mutex);
  return kPwStatusOk;
}

PwStatus nep_event_rpc_client_open(void)
{
  if (mutex_lock_interruptible(&client.mutex)){
		return kPwStatusAborted;
  }

  if (client.rpc_client == NULL) {
    mutex_unlock(&client.mutex);
    return kPwStatusFailedPrecondition;
  }

  client.next_call_id = 0;
  PwStatus event_init_status = NepCmdRpcServiceEvent(
      client.rpc_client, nep_event_rpc_on_next_callback,
      nep_event_rpc_on_complete_callback, nep_event_rpc_on_error_callback,
      (void*)&client, &(client.next_call_id));

  if (event_init_status == kPwStatusOk) {
    pr_info("%s: nep event open done", __func__);
  } else {
    pr_err("%s: nep event open failed", __func__);
  }

  mutex_unlock(&client.mutex);
  return event_init_status;
}

PwStatus nep_event_rpc_client_close(void)
{
  if (mutex_lock_interruptible(&client.mutex)){
		return kPwStatusAborted;
  }

  if (client.rpc_client != NULL &&
      NepCmdRpcServiceEventComplete(client.rpc_client, client.next_call_id) !=
          kPwStatusOk) {
    mutex_unlock(&client.mutex);;
    return kPwStatusAborted;
  }
  mutex_unlock(&client.mutex);
  pr_info("%s: nep event close done", __func__);
  return kPwStatusOk;
}

PwStatus nep_event_rpc_event_handler(const uint32_t id, const void* msg,
                                     size_t msg_len)
{
  if (mutex_lock_interruptible(&client.mutex)){
		return kPwStatusAborted;
  }
  if (client.callback != NULL && client.callback(id, msg, msg_len)) {
    mutex_unlock(&client.mutex);
    return kPwStatusAborted;
  }
  mutex_unlock(&client.mutex);
  return kPwStatusOk;
}

void nep_event_rpc_client_register_event_callback(event_callback callback)
{
  if (mutex_lock_interruptible(&client.mutex)){
		return;
  }
  client.callback = callback;
  mutex_unlock(&client.mutex);
}

// NOLINTEND(readability-identifier-naming)
