/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header of VPN Pipeline Service
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Mike Yu <yumike@google.com>
 */

#ifndef NOA_NET_VPN_PIPELINE_SERVICE_H
#define NOA_NET_VPN_PIPELINE_SERVICE_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>

#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_framework/nested_ring_task.h"
#include "network_pipeline_framework/task_scheduler.h"

typedef struct {
	const char *name;
	bool is_wlan;
	bool is_tx_path;
	NestedRingTask *task;
	NestedRingStage *stage;
	NestedShadowRing *shadow_ring_info;
	NestedCachedRing *cached_ring_info;
	NestedRingEngine *engine;
	NestedRingStage *next_stage;
} NestedRingVpnStageSetupParams;

/**
 * @brief Sets up a nested ring stage for VPN traffic.
 *
 * This function configures a specific stage within the nested ring pipeline
 * designed to handle VPN traffic.
 *
 * @param[in] params A pointer to the NestedRingVpnStageSetupParams
 * structure containing all necessary configuration details.
 *
 * @return 0 on successful setup.
 *         Negative error code otherwise.
 */
int32_t NestedRingVpnStageSetup(const NestedRingVpnStageSetupParams *params);

// Since the it is in the SW driver simulator mode, it doesn't actually use
// the IPSec Engine. Therefore, this setup function is empty.
static inline int32_t NestedRingVpnEngineSetup(NepBitmapTaskScheduler *scheduler)
{
	(void)scheduler;
	return 0;
}

/**
 * @brief Set up the VPN RX Path.
 *
 * This function sets up the VPN RX Stage. This stage identifies and processes
 * received VPN packets. After processing a VPN packet, it proceeds to the
 * vpn_rx_completed stage and sends the packet to the NOA ring through the
 * sender_engine. If a packet is not a VPN packet, it is forwarded to the stage
 * connected via vpn_fallback_stage for further processing.
 *
 * @param[in] sender_engine The NepEngine instance used to send descriptors to
 * the output ring.
 * @param[in] scheduler The task scheduler used for managing the VPN RX operations.
 * @param[out] vpn_fallback_stage A pointer to a pointer to the next stage
 * pointer for VPN fallback. This allows subsequent data path connections
 * after the fallback.
 *
 * @return 0 on success, negative value for error code.
 */
int32_t NepVpnRxPathSetup(struct NepEngine *sender_engine, struct NepTaskScheduler *scheduler,
			  struct NepStage ***vpn_fallback_stage);
/**
 * @brief Get the VPN RX stage singleton object.
 *
 * This function retrieves the singleton object of the VPN RX stage. It is
 * guaranteed to return a valid NepStage pointer.
 *
 * @return A valid NepStage pointer representing the VPN RX stage singleton.
 */
struct NepStage *NepVpnRxStageSingletonGet(void);
/**
 * @brief Set up the VPN TX Path.
 *
 * This function sets up the VPN TX Stage. This stage identifies and processes
 * received VPN packets. After processing a VPN packet, it proceeds to the
 * vpn_rx_completed stage and sends the packet to the NOA ring through the
 * sender_engine. If a packet is not a VPN packet, it is forwarded to the stage
 * connected via vpn_fallback_stage for further processing.
 *
 * @param[in] sender_engine The NepEngine instance used to send descriptors to
 * the output ring.
 * @param[in] scheduler The task scheduler used for managing the VPN TX operations.
 * @param[out] vpn_fallback_stage A pointer to a pointer to the next stage
 * pointer for VPN fallback. This allows subsequent data path connections
 * after the fallback.
 *
 * @return 0 on success, negative value for error code.
 */
int32_t NepVpnTxPathSetup(struct NepEngine *sender_engine, struct NepTaskScheduler *scheduler,
			  struct NepStage ***vpn_fallback_stage);
/**
 * @brief Get the VPN TX stage singleton object.
 *
 * This function retrieves the singleton object of the VPN TX stage. It is
 * guaranteed to return a valid NepStage pointer.
 *
 * @return A valid NepStage pointer representing the VPN TX stage singleton.
 */
struct NepStage *NepVpnTxStageSingletonGet(void);
#endif /* NOA_NET_VPN_PIPELINE_SERVICE_H */
