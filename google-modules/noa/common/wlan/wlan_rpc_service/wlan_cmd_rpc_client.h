/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header File for NOA WLAN Command RPC Client
 *
 * Copyright (c) 2025 Google LLC.
 */

#ifndef __WLAN_CMD_RPC_CLIENT_H__
#define __WLAN_CMD_RPC_CLIENT_H__

#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"
#include "wlan_rpc_call_entry.h"

/// @brief Initializes the WLAN command RPC client.
///
/// @param[in] rpc_client The RPC client instance to use.
/// @return PW_STATUS_OK if successful, otherwise an error code.
PwStatus wlan_cmd_rpc_client_init(PwRpcClient* rpc_client);

/// @brief Deinitializes the WLAN command RPC client.
/// @return PW_STATUS_OK if successful, otherwise an error code.
PwStatus wlan_cmd_rpc_client_deinit(void);

/// @brief Sends a WLAN command over RPC.
///
/// @param[in] cmd The command ID.
/// @param[in] msg Pointer to the command message.
/// @param[in] msg_len Length of the command message.
/// @param[in] callback Callback function to be called when the command completes.
/// @param[in] context User-defined context to be passed to the callback function.
/// @return PW_STATUS_OK if the command was sent successfully, otherwise an error
/// code.
PwStatus wlan_cmd_rpc_client_send_command(const uint32_t cmd, const void* msg,
					  const uint32_t msg_len,
					  wlan_cmd_rpc_callback callback,
					  void* context);

/// @brief Notify the next pending RPC call in wlan RPC client
///
/// @param[in] context User-defined context.
/// @param[in] status Status of the RPC call.
/// @param[in] response Pointer to the response message.
/// @param[in] response_size Size of the response message.
void wlan_cmd_rpc_notify_callback(void* context, PwStatus status,
				  const void* response, size_t response_size);

/// @brief RPC on complete callback function.
///
/// @param[in] call Pointer to the RPC call structure.
/// @param[in] payload Pointer to the payload data.
/// @param[in] payload_size Size of the payload data.
/// @param[in] status Status of the RPC call.
void wlan_cmd_rpc_on_complete_callback(struct PwRpcCallStruct* call,
				       const uint8_t* payload,
				       size_t payload_size, PwStatus status);

/// @brief RPC on error callback function.
///
/// @param[in] call Pointer to the RPC call structure.
/// @param[in] error Error code.
void wlan_cmd_rpc_on_error_callback(struct PwRpcCallStruct* call,
				    PwStatus error);

#endif /* __WLAN_CMD_RPC_CLIENT_H__ */
