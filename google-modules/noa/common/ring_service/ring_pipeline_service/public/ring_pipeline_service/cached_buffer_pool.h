/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for Ring Pipeline Service Cached Buffer Pool Component
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef __NOA_RING_PIPELINE_SERVICE_CACHED_BUFFER_POOL_H__
#define __NOA_RING_PIPELINE_SERVICE_CACHED_BUFFER_POOL_H__

#ifdef linux
#include <common/ring.h>

#include "common/core.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "ring_manager_instance.h"
#else /* linux */
#include <cstddef>

#include "common/core.h"
#include "common/ring.h"
#include "linux_port/types.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "ring_mgmt/ring_manager_instance.h"
#endif /* linux */

typedef struct NepBufferPoolContext {
	// The stage managing the caching logic, including shadow and cached rings.
	NestedRingStage cache_stage;
	// The source ring instance in DRAM (e.g., WLAN or Modem buffer pool).
	struct ring_manager_instance *instance;
	// Pointer to the DMA engine instance used for memory transfers.
	void *dma;
	// The current consumer index of the source ring, tracked by the buffer pool consumer.
	uint16_t src_ring_consumer_idx;
	// The current consumer index of the cached ring in DTCM.
	uint16_t cached_ring_consumer_idx;
} NepBufferPoolContext;

/**
 * @brief Retrieves the singleton instance of the buffer pool context.
 *
 * @param id The ID of the buffer pool (e.g., NOA_RING_SERVICE_BUFFER_POOL_WLAN).
 *
 * @return A pointer to the NepBufferPoolContext, or NULL if the ID is invalid.
 */
NepBufferPoolContext *NepBufferPoolContextSingletonGet(uint8_t id);

/**
 * @brief Retrieves the singleton instance of the buffer pool cache engine.
 *
 * @return A pointer to the NestedRingEngine singleton.
 */
NestedRingEngine *NepBufferPoolCacheEngineSingletonGet(void);

/**
 * @brief Initializes the buffer pool cache engine.
 *
 * Sets up the engine with the provided scheduler and completion task.
 *
 * @param engine The engine to initialize.
 * @param scheduler The bitmap task scheduler to use.
 * @param post_complete_task The task to run after a DMA batch completes.
 *
 * @return 0 on success, or a negative error code.
 */
int32_t NepBufferPoolCacheEngineInit(NestedRingEngine *engine, NepBitmapTaskScheduler *scheduler,
				     NestedRingTask *post_complete_task);

/**
 * @brief The execution function for caching buffer pool data.
 *
 * This function is called by the engine to transfer data from the source ring
 * to the cached ring (DTCM) using DMA.
 *
 * @param stage The nested ring stage.
 * @param req The request associated with the current batch.
 *
 * @return 0 on success, -EAGAIN if not enough data/space, or other error codes.
 */
int32_t NepBufferPoolCacheData(struct NestedRingStage *stage, NestedRingRequest *req);

typedef struct {
	const char *name;
	NestedRingTask *task;
	NestedRingStage *stage;
	NestedShadowRing *shadow_ring_info;
	NestedCachedRing *cached_ring_info;
	NestedRingEngine *engine;
	NestedRingStage *next_stage;
	NepBufferPoolContext *context;
	// Identifier for the data path, used to retrieve the corresponding ring instance.
	uint8_t path_id;
	// DMA instance to use for DRAM to DTCM transfers.
	void *dma;
} NepBufferPoolCacheStageSetupParams;

/**
 * @brief Sets up a buffer pool cache stage.
 *
 * Initializes the stage with the provided parameters, including ring info,
 * context, and task configurations.
 *
 * @param params Configuration parameters for the stage setup.
 *
 * @return 0 on success, or a negative error code.
 */
int32_t NepBufferPoolCacheStageSetup(const NepBufferPoolCacheStageSetupParams *params);

/**
 * @brief Retrieves a buffer descriptor from the cached pool.
 *
 * Fetches a descriptor from the cached ring if available and advances the local
 * consumer indices.
 *
 * @note This function does NOT immediately update the tail pointers visible to
 * producers. It only updates the internal consumer state.
 * To complete the transaction and release the slot:
 * 1. Call NepBufferPoolCompleted() to update the cached ring's tail pointer
 *    (in DTCM).
 * 2. Manually update the source ring's tail pointer (in DRAM) to notify the
 *    original producer.
 *
 * @param context The buffer pool context.
 * @param desc Pointer to where the retrieved descriptor will be stored.
 *
 * @return 0 on success, -EAGAIN if the cached ring is empty.
 */
int32_t NepBufferPoolGetBuffer(NepBufferPoolContext *context, noa_buffer_pool_desc *desc);

/**
 * @brief Completes the processing of a batch of buffers.
 *
 * Updates the tail pointer of the cached ring to reflect that the consumer
 * has finished processing up to the given index.
 *
 * @param context The buffer pool context.
 * @param val The new consumer index value.
 */
void NepBufferPoolCompleted(NepBufferPoolContext *context, uint16_t val);

/**
 * @brief Gets the address of the source ring's consumer (tail) register.
 *
 * @param context The buffer pool context.
 *
 * @return The physical address of the source ring's read/tail register.
 */
uintptr_t NepBufferPoolSrcRingTailAddressGet(NepBufferPoolContext *context);

/**
 * @brief Gets the current consumer index for the source ring.
 *
 * @param context The buffer pool context.
 *
 * @return The current source ring consumer index.
 */
uint16_t NepBufferPoolSrcRingConsumerIdxGet(NepBufferPoolContext *context);

/**
 * @brief Gets the current consumer index for the cached ring.
 *
 * @param context The buffer pool context.
 *
 * @return The current cached ring consumer index.
 */
uint16_t NepBufferPoolCachedRingConsumerIdxGet(NepBufferPoolContext *context);

/**
 * @brief Updates both source and cached ring consumer indices.
 *
 * Typically used for error recovery to revert indices to a previous state.
 *
 * @param context The buffer pool context.
 * @param value The value to set for both indices.
 */
void NepBufferPoolUpdateAllRingConsumerIdx(NepBufferPoolContext *context, uint16_t value);

/**
 * @brief Updates only the cached ring consumer index.
 *
 * @param context The buffer pool context.
 * @param value The value to set for the cached ring consumer index.
 */
void NepBufferPoolUpdateCachedRingConsumerIdx(NepBufferPoolContext *context, uint16_t value);
#endif /* __NOA_RING_PIPELINE_SERVICE_CACHED_BUFFER_POOL_H__ */
