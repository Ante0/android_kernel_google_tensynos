// SPDX-License-Identifier: GPL-2.0-only
/*
 * Header file for NOA NEP Event RPC Client
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kimi Luinata <kimiluinata@google.com>
 */

#ifndef __NEP_EVENT_RPC_CLIENT_H__
#define __NEP_EVENT_RPC_CLIENT_H__

#include <linux/types.h>
#include <linux/mutex.h>

#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_os.h"
#include "pw_rpc_c/pw_status.h"

typedef int32_t (*event_callback)(const uint32_t id, const void* msg,
                                  size_t msg_len);

typedef struct nep_event_rpc_client {
  struct mutex mutex;
  event_callback callback;
  PwRpcClient* rpc_client;
  uint32_t next_call_id;
} nep_event_rpc_client;

/// @brief Get the NEP event RPC client instance.
///
/// @return The NEP event RPC client instance.
nep_event_rpc_client* nep_event_rpc_client_get_instance(void);

/// @brief Initializes the NEP command RPC client.
///
/// @param[in] rpc_client The RPC client instance to use.
void nep_event_rpc_client_init(PwRpcClient* rpc_client);

/// @brief Deinitializes the NEP event RPC client.
/// @return PW_STATUS_OK if successful, otherwise an error code.
PwStatus nep_event_rpc_client_deinit(void);

/// @brief Opens the NEP event stream.
///
/// @return PW_STATUS_OK if successful, otherwise an error code.
PwStatus nep_event_rpc_client_open(void);

/// @brief Closes the NEP event stream.
///
/// @return PW_STATUS_OK if successful, otherwise an error code.
PwStatus nep_event_rpc_client_close(void);

/// @brief Handles NEP event.
///
/// @param id The event ID.
/// @param msg Pointer to the event msg.
/// @param msg_len Size of the event data.
/// @return PW_STATUS_OK if successful, otherwise an error code.
PwStatus nep_event_rpc_event_handler(const uint32_t id, const void* msg,
                                     size_t msg_len);

/// @brief Registers a callback function to be called when a NEP event is received.
///
/// @param[in] callback The callback function to register.
void nep_event_rpc_client_register_event_callback(event_callback callback);

#endif /* __NEP_EVENT_RPC_CLIENT_H__ */
