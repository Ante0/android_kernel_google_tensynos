// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA NEP CMD RPC Client
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kimi Luinata <kimiluinata@google.com>
 */

#include "nep_cmd_rpc_client.h"

#include <linux/completion.h>
#include <linux/mutex.h>

#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/nep_cmd_rpc_service_client.nanopb.h"

typedef struct NepCmdRpcClient {
  struct mutex mutex;
  struct completion compl;
  PwRpcClient* rpc_client;
} NepCmdRpcClient;

// The NepCmdRpcClient instance we use throughout the RPC.
static NepCmdRpcClient client;
static noa_service_nep_cmd_service_Response response = {
    .result = noa_service_nep_cmd_service_CmdResult_SUCCESS};

void NepCmdRpcClientOnCompleteCallback(struct PwRpcCallStruct* __attribute__((__unused__))call,
                                       const uint8_t* payload, size_t payload_size,
                                       PwStatus status) {
  if (status != kPwStatusOk) {
    pr_info("%s: NEP cmd failed.", __func__);
    return;
  }

  NepCmdRpcServiceCommandDeserializeResponse(payload, payload_size, &response);
  pr_info("%s: Received NEP response: %d", __func__, response.result);
  complete(&client.compl);
}

void LargeNepCmdRpcClientOnCompleteCallback(struct PwRpcCallStruct*
                                       __attribute__((__unused__))call,
                                       const uint8_t* payload, size_t payload_size,
                                       PwStatus status) {
  if (status != kPwStatusOk) {
    pr_info("%s: Large NEP cmd failed.", __func__);
    return;
  }

  NepCmdRpcServiceLargeCommandDeserializeResponse(payload, payload_size, &response);
  pr_info("%s: Received NEP response: %d", __func__, response.result);
  complete(&client.compl);
}

// Handle on error rpc command.
void NepCmdRpcClientOnErrorCallback(struct PwRpcCallStruct*
                                    __attribute__((__unused__))call, PwStatus error) {
  pr_info("%s: NEP cmd received error", __func__);
}


PwStatus NepCmdRpcClientSendCommand(uint32_t cmd, const void* msg,
                                    uint32_t msg_len, void* res_buf, uint32_t* res_code) {
  pr_info("%s: Call to NEP, cmd %d", __func__, cmd);

  if (client.rpc_client == NULL) {
    pr_info("%s: client not initialized!", __func__);
    return kPwStatusFailedPrecondition;
  }

  if (mutex_lock_interruptible(&client.mutex)) {
    return kPwStatusAborted;
  }

  PwStatus status;
  if (msg_len > 128) {
    noa_service_nep_cmd_service_LargeRequest large_request;
    large_request.cmd = cmd;
    large_request.msg.size = msg_len;
    if (msg_len > 0) memcpy(large_request.msg.bytes, msg, msg_len);

    status = NepCmdRpcServiceLargeCommand(
        client.rpc_client, &large_request, LargeNepCmdRpcClientOnCompleteCallback,
        NepCmdRpcClientOnErrorCallback, NULL, NULL);
  }
  else {
    noa_service_nep_cmd_service_Request request;
    request.cmd = cmd;
    request.msg.size = msg_len;
    if (msg_len > 0) memcpy(request.msg.bytes, msg, msg_len);

    status = NepCmdRpcServiceCommand(
        client.rpc_client, &request, NepCmdRpcClientOnCompleteCallback,
        NepCmdRpcClientOnErrorCallback, NULL, NULL);
  }

  if (status == kPwStatusOk) {
    wait_for_completion_interruptible(&client.compl);
    pr_info("%s: done, res_size %d", __func__, response.msg.size);
    if (response.msg.size > 0 && res_buf != NULL) {
      memcpy(res_buf, response.msg.bytes, response.msg.size);
    }
    if (res_code != NULL) {
      *res_code = response.result;
    }
  } else {
    pr_info("%s: NEP cmd failed.", __func__);
  }

  mutex_unlock(&client.mutex);
  return status;
}

void NepCmdRpcClientInit(PwRpcClient* rpc_client) {
  init_completion(&client.compl);
  mutex_init(&client.mutex);
  if (mutex_lock_interruptible(&client.mutex)) {
    return;
  }

  client.rpc_client = rpc_client;
  mutex_unlock(&client.mutex);

  pr_info("%s: nep cmd rpc client init done", __func__);
}

PwStatus NepCmdRpcClientDeinit(void) {
  if (mutex_lock_interruptible(&client.mutex)) {
		return kPwStatusAborted;
  }
  client.rpc_client = NULL;
  complete(&client.compl);
	mutex_unlock(&client.mutex);

  return kPwStatusOk;
}
