/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header File for NOA WLAN Event RPC Client
 *
 * Copyright (c) 2025 Google LLC.
 */

#ifndef __WLAN_EVENT_RPC_CLIENT_H__
#define __WLAN_EVENT_RPC_CLIENT_H__

#include <linux/mutex.h>
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_os.h"
#include "pw_rpc_c/pw_status.h"

typedef int32_t (*event_callback)(const uint32_t event_id,
				  const void* event_data,
				  size_t event_data_size);

typedef struct wlan_event_rpc_client {
	struct mutex mutex;
	event_callback callback;
	PwRpcClient* rpc_client;
	uint32_t next_call_id;
} wlan_event_rpc_client;

/// @brief Get the instance of WLAN event RPC client
///
/// @return the unary instance of WLAN event RPC client.
wlan_event_rpc_client* wlan_event_rpc_client_get_instance(void);

/// @brief Initializes the WLAN event RPC client.
///
/// @param[in] client The WLAN event RPC client instance.
/// @param[in] rpc_client The RPC client instance to use.
/// @return PW_STATUS_OK if successful, otherwise an error code.
PwStatus wlan_event_rpc_client_init(PwRpcClient* rpc_client);

/// @brief Deinitializes the WLAN event RPC client.
///
/// @param[in] client The WLAN event RPC client instance.
/// @return PW_STATUS_OK if successful, otherwise an error code.
PwStatus wlan_event_rpc_client_deinit(void);

/// @brief Opens the WLAN event stream.
///
/// @param[in] client The WLAN event RPC client instance.
/// @return PW_STATUS_OK if successful, otherwise an error code.
PwStatus wlan_event_rpc_client_open(void);

/// @brief Closes the WLAN event stream.
///
/// @param[in] client The WLAN event RPC client instance.
/// @return PW_STATUS_OK if successful, otherwise an error code.
PwStatus wlan_event_rpc_client_close(void);

/// @brief Handles a WLAN event.
///
/// @param[in] client The WLAN event RPC client instance.
/// @param[in] event_id The ID of the event.
/// @param[in] event_data A pointer to the event data.
/// @param[in] event_data_size The size of the event data.
/// @return PW_STATUS_OK if successful, otherwise an error code.
PwStatus wlan_event_rpc_event_handler(const uint32_t event_id,
				      const void* event_data,
				      size_t event_data_size);

/// @brief Registers a callback function to be called when a WLAN event is received.
///
/// @param[in] client The WLAN event RPC client instance.
/// @param[in] callback The callback function to register.
void wlan_event_rpc_client_register_event_callback(event_callback callback);

#endif /* __WLAN_EVENT_RPC_CLIENT_H__ */
