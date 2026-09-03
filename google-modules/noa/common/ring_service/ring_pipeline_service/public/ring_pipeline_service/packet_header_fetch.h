/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header of packet header fetching component.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_RING_PIPELINE_SERVICE_PACKET_HEADER_FETCHING_H
#define NOA_RING_PIPELINE_SERVICE_PACKET_HEADER_FETCHING_H

#ifdef linux
#include <linux/types.h>

#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_framework/nested_ring_task.h"
#else /* linux */
#include <cstdint>

#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_framework/nested_ring_task.h"
#endif /* linux */

typedef struct {
	uintptr_t header_buffer_ring_base;
	uintptr_t header_buffer_ring_end_mask;
	void *dma;
} NestedRingFetchHeaderContext;

typedef struct {
	const char *name;
	NestedRingTask *task;
	NestedRingStage *stage;
	NestedShadowRing *shadow_ring_info;
	NestedCachedRing *cached_ring_info;
	NestedRingEngine *engine;
	NestedRingStage *next_stage;
	NestedRingFetchHeaderContext *fetch_header_context;
	void *dma;
	uintptr_t header_buffer_ring_base;
	uint32_t header_buffer_ring_size;
} NestedRingFetchHeaderStageSetupParams;

/**
 * @brief Fetches packet headers in a nested ring pipeline stage.
 *
 * This function processes a request to fetch packet headers for the data
 * entries in the current stage of the nested ring pipeline. It prepares DMA
 * requests to copy header information from the source packet buffers to a
 * dedicated header buffer ring. The function handles potential alignment
 * issues and ensures that only entries requiring header fetching are
 * processed.
 *
 * @param[in] stage A pointer to the `NestedRingStage` structure, representing
 *                  the current processing stage in the pipeline. This stage
 *                  contains context and configuration for header fetching.
 * @param[in] req A pointer to the `NestedRingRequest` structure, which
 *                encapsulates the set of data entries to be processed.
 *
 * @return 0 on successful initiation of the header fetch operation.
 *         -EAGAIN if DMA resources are temporarily unavailable or if the
 *                 request queue is full.
 *         Other negative error codes may be returned for invalid
 *         configurations or parameters.
 */
int32_t NestedRingFetchHeader(struct NestedRingStage *stage, NestedRingRequest *req);

/**
 * @brief Retrieves the singleton instance of the nested ring header fetch engine.
 *
 * This function provides access to the single, globally available instance of the
 * `NestedRingEngine` specifically configured for header fetching operations.
 * This engine manages the asynchronous tasks related to DMA-based header
 * transfers.
 *
 * @return A pointer to the singleton `NestedRingEngine` instance for header
 *         fetching. This function always returns a valid pointer as the engine
 *         is statically allocated.
 */
NestedRingEngine *NestedRingFetchHeaderEngineSingletonGet(void);

/**
 * @brief Initializes the nested ring header fetch engine.
 *
 * This function initializes the provided `NestedRingEngine` for header fetching
 * operations. The initialization involves setting up a pool of requests for
 * the engine, configuring the asynchronous processing function
 * (`NestedRingFetchHeader`), and associating the engine with a
 * `NepBitmapTaskScheduler` for managing asynchronous tasks. It also links a
 * post-completion task to handle tasks after DMA operations are finished.
 *
 * @param[in] engine A pointer to the `NestedRingEngine` structure to be
 *                   initialized.
 * @param[in] scheduler A pointer to the `NepBitmapTaskScheduler` that the engine
 *                      will use to schedule and manage its asynchronous header
 *                      fetching tasks.
 * @param[in] post_complete_task A pointer to the `NestedRingTask` to be
 *                               executed after the DMA operation for header
 *                               fetching is complete.
 *
 * @return 0 on successful initialization.
 */
int32_t NestedRingFetchHeaderEngineInit(NestedRingEngine *engine, NepBitmapTaskScheduler *scheduler,
					NestedRingTask *post_complete_task);

/**
 * @brief Sets up a nested ring stage for packet header fetching.
 *
 * This function configures a `NestedRingStage` specifically for the task of
 * fetching packet headers. The setup includes initializing the stage context
 * with DMA instance, header buffer ring details (base address and size), and
 * validating the alignment and size of the header buffer ring. It also links
 * the stage with its processing engine, task, and any subsequent stage in the
 * pipeline.
 *
 * @param[in] params A pointer to the `NestedRingFetchHeaderStageSetupParams`
 *                   structure, containing all necessary configuration details
 *                   for the header fetch stage.
 *
 * @return 0 on successful setup.
 *         -EINVAL if the header buffer ring size is not a power of two or if
 *                 the base address is not properly aligned.
 */
int32_t NestedRingFetchHeaderStageSetup(const NestedRingFetchHeaderStageSetupParams *params);

#endif /* NOA_RING_PIPELINE_SERVICE_PACKET_HEADER_FETCHING_H */
