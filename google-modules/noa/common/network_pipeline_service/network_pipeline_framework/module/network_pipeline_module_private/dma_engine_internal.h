/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for DMA Engine Internal Header
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifndef NOA_NETWORK_PIPELINE_MODULE_DMA_ENGINE_INTERNAL_H
#define NOA_NETWORK_PIPELINE_MODULE_DMA_ENGINE_INTERNAL_H

#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>

#include "network_pipeline_framework/engine.h"
#else /* linux */
#include <cstdint>

#include "network_pipeline_framework/engine.h"
#include "pw_status/status.h"
#endif /* linux */

typedef struct {
	uint8_t free_channel;
	void *driver;
	struct NepAsyncEngine engine;
} NepDmaEngine;

/**
 * @brief Process the request using DMA engine.
 *
 * This function utilizes the DMA driver to process the corresponding request.
 * The DMA engine automatically assigns the request to an idle channel and
 * executes the DMA task. This DMA Engine must be used with
 * NepDmaGenericStage to convert the packet into the corresponding DMA
 * request. After the DMA request is completed, it will be marked as
 * completed through DmaComplete.
 *
 * @param[in] engine NepEngine structure containing DMA engine context.
 * @param[in] request NepProcessingRequest structure containing the packet.
 *
 * @return 0 for success, negative value for error code.
 */
int32_t DmaEngineProcessing(struct NepEngine *engine, struct NepProcessingRequest *request);

#ifdef linux
#define BURST_SIZE (16U)
#define BURST_LENGTH (8U)
#define MASK (BURST_SIZE * BURST_LENGTH - 1U)
static uint32_t inline RoundUpDmaUnitSize(uint32_t size)
{
	return (((size) + (MASK)) & ~(MASK));
}
/**
 * @brief Callback function called after DMA task completion.
 *
 * This callback function is invoked by the DMA driver after it receives an
 * interrupt indicating the completion of a DMA task. It marks the
 * corresponding packet as completed.
 *
 * @param[in] context A pointer to the NepProcessingRequest context.
 */
void DmaComplete(void *context);
#else /* linux */
static constexpr uint32_t kBurstSize = 16;
static constexpr uint32_t kBurstLength = 1;
static constexpr uint32_t kMask = (kBurstSize * kBurstLength) - 1U;
static constexpr inline uint32_t RoundUpDmaUnitSize(uint32_t size)
{
	return (((size) + (kMask)) & ~(kMask));
}

/**
 * @brief Callback function called after DMA task completion.
 *
 * This callback function is invoked by the DMA driver after it receives an
 * interrupt indicating the completion of a DMA task. It marks the
 * corresponding packet as completed and determines whether the packet was
 * completed successfully or failed based on the provided status.
 *
 * @param[in] status The status of the DMA transfer indicating success or
 * failure.
 * @param[in] context A pointer to the NepProcessingRequest context.
 */
void DmaComplete(pw::Status status, int32_t, void *context);
#endif /* linux */

#endif /* NOA_NETWORK_PIPELINE_MODULE_DMA_ENGINE_INTERNAL_H */
