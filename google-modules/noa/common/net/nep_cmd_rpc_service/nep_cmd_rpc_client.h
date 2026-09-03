// SPDX-License-Identifier: GPL-2.0-only
/*
 * Header file for NOA NEP CMD RPC Client
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kimi Luinata <kimiluinata@google.com>
 */

#ifndef __NEP_CMD_RPC_CLIENT_H__
#define __NEP_CMD_RPC_CLIENT_H__

#pragma once

#include <linux/completion.h>
#include <linux/types.h>

#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_os.h"
#include "pw_rpc_c/pw_status.h"

/// @brief Sends a NEP command over RPC.
///
/// @param[in] cmd The command ID.
/// @param[in] msg Pointer to the command message.
/// @param[in] msg_len Length of the command message.
/// @return Status of the RPC call
PwStatus NepCmdRpcClientSendCommand(uint32_t cmd, const void* msg,
                                    uint32_t msg_len, void* res_buf, uint32_t* res_code);

/// @brief RPC on complete callback function.
///
/// @param[in] call Pointer to the RPC call structure.
/// @param[in] payload Pointer to the payload data.
/// @param[in] payload_size Size of the payload data.
/// @param[in] status Status of the RPC call.
void NepCmdRpcClientOnCompleteCallback(struct PwRpcCallStruct* /*call*/,
                                    const uint8_t* payload,
                                    size_t payload_size, PwStatus status);

/// @brief Nep Large CMD RPC on complete callback function.
///
/// @param[in] call Pointer to the RPC call structure.
/// @param[in] payload Pointer to the payload data.
/// @param[in] payload_size Size of the payload data.
/// @param[in] status Status of the RPC call.
void LargeNepCmdRpcClientOnCompleteCallback(struct PwRpcCallStruct* /*call*/,
                                       const uint8_t* payload,
                                       size_t payload_size, PwStatus status);

// @brief Handle on error rpc command.
//
// @param[in] call Pointer to the RPC call structure.
// @param[in] error Error of the RPC call.
void NepCmdRpcClientOnErrorCallback(struct PwRpcCallStruct* /*call*/,
                                    PwStatus error);

/// @brief Initializes the NEP command RPC client.
///
/// @param[in] rpc_client The RPC client instance to use.
void NepCmdRpcClientInit(PwRpcClient* rpc_client);

/// @brief Deinitializes the NEP command RPC client.
/// @return PW_STATUS_OK if successful, otherwise an error code.
PwStatus NepCmdRpcClientDeinit(void);

#endif /* __NEP_CMD_RPC_CLIENT_H__ */
