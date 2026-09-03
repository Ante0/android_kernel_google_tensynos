/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for
 * - client calls to ring service RPC events..
 * - ring shared info utility functions for clints
 *
 * This header provides the interface for clients to call RPC
 * events to interact with the ring service.
 * In addition, it provides ring shared info get function
 * and ring buffer pool shared info get function for clients.
 *
 * Copyright (c) 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 *         Wilson Chen <wilsonwh@google.com>
 */

#ifndef GOOGLE_DPA_RING_SERVICE_PROXY_H
#define GOOGLE_DPA_RING_SERVICE_PROXY_H

#include <linux/types.h>
#include <soc/google/google_dpa.h>
#include <soc/google/google_dpa_ring_service_proxy_defs.h>
#include "pw_rpc_c/pw_status.h"

/**
 * noa_ring_service_rpc_event_activate() - Sends an RPC event to activate a specific ring.
 * @path_id: The identifier of the ring path to be activated.
 * @direction: The data direction of the ring (e.g., transmit or receive).
 *
 * This function allows a client on the AP to request the activation of a specific
 * communication ring within the ring service on the NEP.
 *
 * Return: A PwStatus code indicating the result of the RPC call. kPwStatusOk on
 * success, or an error code on failure.
 */
PwStatus google_dpa_ring_service_rpc_event_activate(u8 path_id, u8 direction);

/**
 * noa_ring_service_rpc_event_deactivate() - Sends an RPC event to deactivate a specific ring.
 * @path_id: The identifier of the ring path to be deactivated.
 * @direction: The data direction of the ring.
 *
 * This function allows a client on the AP to request the deactivation of a
 * previously activated ring within the ring service on the NEP.
 *
 * Return: A PwStatus code indicating the result of the RPC call. kPwStatusOk on
 * success, or an error code on failure.
 */
PwStatus google_dpa_ring_service_rpc_event_deactivate(u8 path_id, u8 direction);

/**
 * noa_ring_service_rpc_event_buffer_pool_activate() - Activates a buffer pool.
 * @port: The port ID associated with the buffer pool to activate.
 *
 * Triggers an RPC event to notify the ring service on the NEP to activate a
 * specific buffer pool.
 *
 * Return: A PwStatus code indicating the result of the RPC call. kPwStatusOk on
 * success, or an error code on failure.
 */
PwStatus google_dpa_ring_service_rpc_event_buffer_pool_activate(u8 port);

/**
 * noa_ring_service_rpc_event_buffer_pool_deactivate() - Deactivates a buffer pool.
 * @port: The port ID associated with the buffer pool to deactivate.
 *
 * Triggers an RPC event to notify the ring service on the NEP to deactivate a
 * specific buffer pool.
 *
 * Return: A PwStatus code indicating the result of the RPC call. kPwStatusOk on
 * success, or an error code on failure.
 */
PwStatus google_dpa_ring_service_rpc_event_buffer_pool_deactivate(u8 port);

/**
 * google_dpa_ring_service_rpc_event_netengine_activate() - Sends an RPC event to activate netengine.
 *
 * This function allows a client on the AP to request the activation of the netengine
 * within the ring service on the NEP.
 *
 * Return: A PwStatus code indicating the result of the RPC call. kPwStatusOk on
 * success, or an error code on failure.
 */
PwStatus google_dpa_ring_service_rpc_event_netengine_activate(void);

/**
 * google_dpa_ring_service_rpc_event_netengine_deactivate() - Sends an RPC event to deactivate netengine.
 *
 * This function allows a client on the AP to request the deactivation of the netengine
 * within the ring service on the NEP.
 *
 * Return: A PwStatus code indicating the result of the RPC call. kPwStatusOk on
 * success, or an error code on failure.
 */
PwStatus google_dpa_ring_service_rpc_event_netengine_deactivate(void);

/**
 * @brief Get ring shared info
 *
 * [in] dpa - google_dpa to get ring shared info
 * [in] ring_id - id for intended ring
 * [out] google_dpa_ring * - structure of the shared info
 *                           aligned with noa_ring in noa_common
 */
struct google_dpa_ring *google_dpa_ring_shared_info_get(struct google_dpa *dpa, uint8_t ring_id);

#endif /* GOOGLE_DPA_RING_SERVICE_PROXY_H */
