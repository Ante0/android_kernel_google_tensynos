/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header of ring data routing component.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_RING_PIPELINE_SERVICE_RING_DATA_ROUTE_H
#define NOA_RING_PIPELINE_SERVICE_RING_DATA_ROUTE_H

#ifdef linux
#include <linux/types.h>

#include "common/core.h"
#include "common/ring.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_framework/nested_ring_task.h"
#include "ring_pipeline_service/cached_buffer_pool.h"
#include "ring_pipeline_service/ring_doorbell_notifier.h"
#else /* linux */
#include <cstdint>

#include "common/core.h"
#include "common/ring.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_framework/nested_ring_task.h"
#include "ring_pipeline_service/cached_buffer_pool.h"
#include "ring_pipeline_service/ring_doorbell_notifier.h"
#endif /* linux */

typedef struct {
	uint32_t feedthrough_to_wlan_host;
	uint32_t feedthrough_to_wlan_device;
	uint32_t feedthrough_to_modem_host;
	uint32_t feedthrough_to_modem_device;
	uint32_t fallback_to_wlan_host;
	uint32_t fallback_to_modem_host;
	uint32_t wlan_filter_packet;
	uint32_t modem_filter_packet;
	uint32_t forward_to_wlan;
	uint32_t forward_to_modem;
	uint32_t vpn_to_wlan_host;
	uint32_t vpn_to_wlan_device;
	uint32_t vpn_to_modem_host;
	uint32_t vpn_to_modem_device;
} RingDataRouteStats;

typedef struct NestedRingRouteContext {
	noa_ring_consumer *src_ring;
	uint32_t processed_src_ring_tail;
	noa_ring_producer *feedthrough_ring;
	noa_ring_producer *feedback_ring;
	noa_ring_producer *forward_wlan_ring;
	noa_ring_producer *forward_modem_ring;
	noa_ring_producer *vpn_ring;
	RingServiceDoorbellContext *feedthrough_path_notifier;
	RingServiceDoorbellContext *feedback_path_notifier;
	RingServiceDoorbellContext *forward_wlan_path_notifier;
	RingServiceDoorbellContext *forward_modem_path_notifier;
	RingServiceDoorbellContext *vpn_path_notifier;
	uint32_t *feedthrough_counter;
	uint32_t *fallback_counter;
	uint32_t *filter_counter;
	uint32_t *vpn_counter;
	uint32_t *forward_to_wlan_counter;
	uint32_t *forward_to_modem_counter;
	NepBufferPoolContext *wlan_buffer_pool_context;
	NepBufferPoolContext *modem_buffer_pool_context;
	int32_t (*route_data)(NestedRingStage *, struct NestedRingRouteContext *,
			      NestedRingRequest *);
	void (*format_vpn_rx_desc)(const void *src_desc, const nep_device_entry *entry,
				   void *vpn_desc);
	void *dma;
} NestedRingRouteContext;

typedef struct {
	const char *name;
	uint8_t network_type;
	bool from_device;
	bool use_vendor_ring;
	bool support_forward_wlan;
	bool support_forward_modem;
	bool support_vpn;
	NestedRingTask *task;
	NestedRingStage *stage;
	NestedShadowRing *shadow_ring_info;
	NestedCachedRing *cached_ring_info;
	NestedRingEngine *engine;
	NestedRingStage *next_stage;
	NestedRingRouteContext *route_context;
	RingDataRouteStats *stats;
	uint8_t src_path_id;
	uint8_t feedthrough_path_id;
	uint8_t feedback_path_id;
	/* Only used if support_forward_wlan is true. */
	uint8_t forward_wlan_path_id;
	/* Only used if support_forward_modem is true. */
	uint8_t forward_modem_path_id;
	/* Only used if support_vpn is true. */
	uint8_t vpn_path_id;
	RingServiceDoorbellContext *feedthrough_path_notifier;
	RingServiceDoorbellContext *feedback_path_notifier;
	/* Only used if support_forward_wlan is true. */
	RingServiceDoorbellContext *forward_wlan_path_notifier;
	/* Only used if support_forward_modem is true. */
	RingServiceDoorbellContext *forward_modem_path_notifier;
	/* Only used if support_vpn is true. */
	RingServiceDoorbellContext *vpn_path_notifier;
	void (*format_vpn_rx_desc)(const void *src_desc, const nep_device_entry *entry,
				   void *vpn_desc);
	/* DMA instance to use for DTCM to DRAM transfers. */
	void *dma;
	NepBufferPoolContext *wlan_buffer_pool_context;
	NepBufferPoolContext *modem_buffer_pool_context;
} NestedRingRouteStageSetupParams;

/**
 * @brief Routes data within a nested ring pipeline stage.
 *
 * This function processes a request to route data through a specific stage in the
 * nested ring pipeline. It determines the data's path based on its type and the
 * stage's configuration, directing it to an appropriate destination ring such as
 * feedthrough, feedback, or a specific forward path. The function also includes
 * error handling mechanisms.
 *
 * @param[in] stage A pointer to the `NestedRingStage` structure representing the
 *                  current processing stage in the pipeline. This stage contains
 *                  context and configuration for routing data.
 * @param[in] req A pointer to the `NestedRingRequest` structure that encapsulates
 *                the data to be routed and its associated metadata.
 *
 * @return 0 on successful routing.
 *         -EAGAIN if resources are temporarily unavailable.
 *         Other negative error codes for invalid configurations or data.
 */
int32_t NestedRingRouteData(struct NestedRingStage *stage, NestedRingRequest *req);

/**
 * @brief Retrieves the singleton instance of the nested ring routing engine.
 *
 * This function provides access to the single, globally available instance of the
 * `NestedRingEngine`. This engine is responsible for managing and executing data
 * routing tasks within the system.
 *
 * @return A pointer to the singleton `NestedRingEngine` instance. This function
 *         always returns a valid pointer as the engine is statically allocated.
 */
NestedRingEngine *NestedRingRouteEngineSingletonGet(void);

/**
 * @brief Initializes the nested ring routing engine.
 *
 * This function initializes the provided `NestedRingEngine`. The initialization
 * process involves setting up a pool of requests that the engine will use,
 * configuring necessary contexts for these requests, and associating the engine
 * with a `NepBitmapTaskScheduler` for handling asynchronous routing operations.
 * It also links a completion task to the engine.
 *
 * @param[in] engine A pointer to the `NestedRingEngine` structure to be
 *                   initialized.
 * @param[in] scheduler A pointer to the `NepBitmapTaskScheduler` that the engine
 *                      will use to schedule and manage its asynchronous data
 *                      routing tasks.
 * @param[in] post_complete_task Pointer to the `NestedRingTask` to be executed
 *                               after the DMA operation is complete.
 *
 * @return 0 on successful initialization.
 *         -EINVAL if the provided task for completion cannot be retrieved or if
 *                 other critical setup steps fail.
 */
int32_t NestedRingRouteEngineInit(NestedRingEngine *engine, NepBitmapTaskScheduler *scheduler,
				  NestedRingTask *post_complete_task);

/**
 * @brief Sets up a nested ring routing stage.
 *
 * This function configures a `NestedRingStage` for routing data according to the
 * specified parameters. This setup includes identifying the source ring,
 * destination rings (like feedthrough, feedback, and optional WLAN/Modem forward
 * paths), and associated doorbell notifiers. It also links the stage to its
 * processing engine, task, and DMA resources if applicable.
 *
 * @param[in] params A pointer to the `NestedRingRouteStageSetupParams` structure
 *                   containing all necessary configuration details for the
 *                   routing stage.
 *
 * @return 0 on successful setup.
 *         -EINVAL if any of the specified ring IDs are invalid or if essential
 *                 components like the DMA controller cannot be obtained.
 */
int32_t NestedRingRouteStageSetup(const NestedRingRouteStageSetupParams *params);

/**
 * @brief Retrieves the statistics of the nested ring routing engine.
 *
 * This function provides access to the statistics of the `NestedRingEngine`.
 *
 * @return A pointer to the `RingDataRouteStats` instance. This function
 *         always returns a valid pointer as the engine is statically allocated.
 */
RingDataRouteStats *RingRouteGetStats(void);

/**
 * @brief Enables or disables packet forwarding capabilities.
 *
 * This function toggles the packet forwarding functionality within the
 * ring routing component. When forwarding is disabled, packets that would
 * normally be routed to external interfaces or other domains may be
 * dropped or processed locally, depending on the stage configuration. This
 * is often used in conjunction with data path changes or when the netengine
 * is deactivated.
 *
 * @param[in] enable A boolean value indicating whether to enable packet
 *                   forwarding. True enables forwarding, false disables it.
 */
void NestedRingRouteSetForwardingEnabled(bool enable);

#endif /* NOA_RING_PIPELINE_SERVICE_RING_DATA_ROUTE_H */
