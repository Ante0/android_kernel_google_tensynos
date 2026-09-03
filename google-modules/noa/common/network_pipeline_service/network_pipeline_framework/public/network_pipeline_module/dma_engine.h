/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for DMA Engine
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifndef NOA_NETWORK_PIPELINE_MODULE_DMA_ENGINE_H
#define NOA_NETWORK_PIPELINE_MODULE_DMA_ENGINE_H

#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>

#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#else /* linux */
#include <cstdint>

#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#endif /* linux */

typedef struct {
	uintptr_t dst;
	uintptr_t src;
	uint32_t size;
} NepDmaRequest;

typedef struct {
	int32_t (*prepare_request)(NepDmaRequest *dma_req, struct NepProcessingRequest *req);
	struct NepStage stage;
} NepDmaGenericStage, NepIpHeaderFetchingStage, NepPacketMovementStage;

/**
 * brief: Get the singleton instance of the NepAsyncEngine.
 *
 * This function retrieves the singleton instance of the
 * NepAsyncEngine. This function is guaranteed to return a
 * valid pointer.
 *
 * @return A pointer to the NepAsyncEngine singleton object.
 */
struct NepAsyncEngine *NepDmaEngineSingletonGet(void);
/**
 * brief: Setup the DMA engine.
 *
 * This function is used to set up the DMA engine. It should
 * only be called once during initialization to establish the
 * DMA engine with the provided parameters.
 *
 * @param[in] engine The NepAsyncEngine dma engine to be setup.
 * @param[in] channel_num The DMA channel number to use.
 * @param[in] dma_driver The DMA hardware driver class instance, such
 * as a dma330 driver.
 * @param[in] scheduler The task scheduler to be used by the DMA engine.
 *
 * @return 0 on success. Negative value on error.
 */
int32_t NepDmaEngineSetup(struct NepAsyncEngine *engine, uint8_t channel_num, void *dma_driver,
			  struct NepTaskScheduler *scheduler);
/**
 * @brief Constructs the IP Header Fetching Stage.
 *
 * This function creates and initializes the IP Header Fetching Stage. Before
 * calling this function, the associated router, container, DMA engine, and
 * scheduler must be prepared. This function constructs the stage using
 * these pre-prepared components.
 *
 * @param[in] ip_stage Pointer to the NepIpHeaderFetchingStage structure to be
 * constructed.
 * @param[in] router Pointer to the NepStageRouter associated with this stage.
 * @param[in] container Pointer to the PktContainer used by this stage.
 * @param[in] dma_engine Pointer to the NepAsyncEngine used for DMA operations.
 * @param[in] scheduler Pointer to the NepTaskScheduler used for scheduling tasks.
 *
 * @return 0 for success, negative value for error code.
 */
int32_t NepIpHeaderFetchingStageConstructor(NepIpHeaderFetchingStage *ip_stage,
					    struct NepStageRouter *router,
					    struct PktContainer *container,
					    struct NepAsyncEngine *dma_engine,
					    struct NepTaskScheduler *scheduler);
/**
 * @brief Constructs the Packet Movement Stage.
 *
 * This function creates and initializes the Packet Movement Stage. The
 * associated router, container, DMA engine, and scheduler must be prepared
 * before calling this function. This stage is used for packet buffer
 * movement in tethering scenarios.
 *
 * @param[in] ip_stage Pointer to the NepPacketMovementStage structure to be
 * constructed.
 * @param[in] router Pointer to the NepStageRouter associated with this stage.
 * @param[in] container Pointer to the PktContainer used by this stage.
 * @param[in] dma_engine Pointer to the NepAsyncEngine used for DMA operations.
 * @param[in] scheduler Pointer to the NepTaskScheduler used for scheduling tasks.
 *
 * @return 0 for success, negative value for error code.
 */
int32_t NepPacketMovementStageConstructor(NepPacketMovementStage *movement_stage,
					  struct NepStageRouter *router,
					  struct PktContainer *container,
					  struct NepAsyncEngine *dma_engine,
					  struct NepTaskScheduler *scheduler);

#endif /* NOA_NETWORK_PIPELINE_MODULE_DMA_ENGINE_H */
