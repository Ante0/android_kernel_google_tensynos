/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header of ring data caching component.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_RING_PIPELINE_SERVICE_RING_DATA_CACHE_H
#define NOA_RING_PIPELINE_SERVICE_RING_DATA_CACHE_H

#ifdef linux
#include <linux/types.h>

#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_framework/nested_ring_task.h"
#include "ring_manager_instance.h"
#else /* linux */
#include <cstdint>

#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_framework/nested_ring_task.h"
#include "ring_mgmt/ring_manager_instance.h"
#endif /* linux */

typedef struct {
	struct ring_manager_instance *instance;
	void (*fill_entry)(void *desc, void *entry);
	void *dma;
} NestedRingCacheContext;

typedef struct {
	const char *name;
	// Specifies if the source ring is located in DRAM. True for DRAM,
	// false for SRAM. This determines if DMA will be used.
	bool is_dram_ring;
	NestedRingTask *task;
	NestedRingStage *stage;
	NestedShadowRing *shadow_ring_info;
	NestedCachedRing *cached_ring_info;
	NestedRingEngine *engine;
	NestedRingStage *next_stage;
	NestedRingCacheContext *cache_context;
	// Identifier for the data path, used to retrieve the corresponding ring instance.
	uint8_t path_id;
	// Function pointer to populate entries in the shadow ring after data is cached.
	void (*fill_entry)(void *, void *);
	// DMA instance to use for DRAM to DTCM transfers.
	void *dma;
} NestedRingCacheStageSetupParams;

/**
 * @brief Caches descriptors from an SRAM-based source ring.
 *
 * This function copies descriptors from a source ring located in SRAM to a
 * designated cached ring using a direct memory copy (memcpy). It updates a
 * temporary tail value for the source ring to track cached descriptors and prevent
 * re-caching. The actual shared tail pointer of the source ring is updated later
 * during the data routing phase. The head index of the cached ring is updated, and
 * corresponding entries in a shadow ring are populated based on the copied
 * descriptors.
 *
 * @param[in] stage Pointer to the `NestedRingStage` structure that manages the
 *  caching operation and holds references to the source, cached, and shadow rings.
 * @param[in] req Pointer to the `NestedRingRequest` structure. This structure
 *  contains details for the current caching request, such as the starting indices
 *  and addresses for the rings involved. It is updated to reflect the new ring
 *  positions after the copy.
 *
 * @return 0 on successful completion of the descriptor caching.
 */
int32_t NestedRingCacheDescriptorFromSram(struct NestedRingStage *stage, NestedRingRequest *req);

/**
 * @brief Caches descriptors from a DRAM-based source ring using DMA.
 *
 * This function initiates an asynchronous DMA transfer to copy descriptors from a
 * source ring located in DRAM to a cached ring, which is typically in SRAM. It
 * prepares DMA request structures for the transfer. Upon completion of the DMA
 * operation, a callback function is invoked to update ring pointers and fill the
 * shadow ring entries.
 *
 * @param[in] stage Pointer to the `NestedRingStage` structure that manages the
 *  caching operation, providing access to ring configurations and context.
 * @param[in] req Pointer to the `NestedRingRequest` structure. This structure
 *  holds details for the current caching request and serves as the context for the
 *  asynchronous DMA operation. It is updated during and after the DMA transfer.
 *
 * @return 0 if the DMA request is successfully queued. Returns a negative error
 *  code if the DMA request fails to be queued (e.g., -EAGAIN) or if there are no
 *  available slot in shadow ring to cache.
 */
int32_t NestedRingCacheDescriptorFromDram(struct NestedRingStage *stage, NestedRingRequest *req);

/**
 * @brief Retrieves the singleton instance of the nested ring cache engine.
 *
 * This function provides access to a single, statically allocated instance of the
 * `NestedRingEngine`. This engine is responsible for managing asynchronous caching
 * operations, particularly for DRAM-to-SRAM transfers that utilize DMA.
 *
 * @return A pointer to the singleton `NestedRingEngine` instance.
 */
NestedRingEngine *NestedRingCacheEngineSingletonGet(void);

/**
 * @brief Initializes the nested ring cache engine.
 *
 * This function sets up the `NestedRingEngine` which is used for managing
 * asynchronous descriptor caching operations, primarily when the source ring is in
 * DRAM. It initializes a pool of `NestedRingRequest` objects and configures the
 * engine with a callback function for DMA operations (`NestedRingCacheDescriptorFromDram`),
 * a task for handling completion notifications, and a scheduler for these tasks.
 *
 * @param[in] engine Pointer to the `NestedRingEngine` structure to be initialized.
 * @param[in] scheduler Pointer to the `NepBitmapTaskScheduler` that will be used by
 *  the engine to schedule tasks upon completion of asynchronous operations.
 * @param[in] post_complete_task Pointer to the `NestedRingTask` to be executed
 *  after the DMA operation is complete.
 *
 * @return 0 on successful initialization. Returns a negative error code if
 *  initialization fails, for instance, if a required system task cannot be
 *  retrieved.
 */
int32_t NestedRingCacheEngineInit(NestedRingEngine *engine, NepBitmapTaskScheduler *scheduler,
				  NestedRingTask *post_complete_task);

/**
 * @brief Sets up a nested ring cache stage.
 *
 * This function initializes a `NestedRingStage` for caching data from a source ring.
 * It configures the stage for either asynchronous operation using DMA if the source
 * ring is in DRAM, or synchronous operation via memory copy if the source is in SRAM.
 * The function also sets up the stage's context, links it to a ring instance, and
 * connects it to the next pipeline stage if one exists.
 *
 * @param[in] params Pointer to a `NestedRingCacheStageSetupParams` structure. This
 *  structure provides all configuration data needed to set up the cache stage, such
 *  as ring details, task assignment, and whether the source ring is DRAM-based.
 *
 * @return 0 on successful setup. Returns a negative error code if setup fails, for
 *  example, due to invalid parameters or if the specified ring instance is not
 *  found.
 */
int32_t NestedRingCacheStageSetup(const NestedRingCacheStageSetupParams *params);

/**
 * @brief Sets the activation state of the netengine.
 *
 * This function controls whether the netengine is active or inactive within the
 * ring cache component. It is typically called during data path transitions
 * (e.g., between direct and offload paths) to ensure that packets are not handled
 * by the NetEngine while the engine is deactivated.
 *
 * @param[in] activate A boolean value indicating the desired state. True to
 *                     activate the netengine, false to deactivate it.
 */
void NestedRingCacheSetNetengineActivate(bool activate);

#endif /* NOA_RING_PIPELINE_SERVICE_RING_DATA_CACHE_H */
