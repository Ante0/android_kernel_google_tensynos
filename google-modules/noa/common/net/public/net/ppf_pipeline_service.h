/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header of Pixel Packet Filter (PPF) Pipeline Service
 *
 * Copyright 2025 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */

#ifndef NOA_NET_PPF_PIPELINE_SERVICE_H
#define NOA_NET_PPF_PIPELINE_SERVICE_H

#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>

#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#else /* linux */
#include <cstdint>

#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#endif /* linux */

typedef struct {
	const char *name;
	NestedRingTask *task;
	NestedRingStage *stage;
	NestedShadowRing *shadow_ring_info;
	NestedCachedRing *cached_ring_info;
	NestedRingEngine *engine;
	NestedRingStage *next_stage;
} NestedRingPpfStageSetupParams;

/**
 * @brief Sets up a nested ring stage for PPF stream.
 *
 * This function configures a specific stage within the nested ring pipeline
 * designed to handle PPF traffic.
 *
 * @param[in] params A pointer to the NestedRingPpfStageSetupParams
 * structure containing all necessary configuration details.
 *
 * @return 0 on successful setup.
 *         Negative error code otherwise.
 */
int32_t NestedRingPpfStageSetup(const NestedRingPpfStageSetupParams *params);

/**
 * @brief Set up the PPF data path.
 *
 * This function establishes the PPF data path by building and
 * connecting the buffer_pool_stage and packet_movement_stage. This
 * setup enables all necessary operations for PPF. If a fallback
 * condition is detected, the function will instead utilize the fallback
 * stage, sending the descriptor out without modification. All completed
 * packets, including fallback packets, are sent to the output ring
 * through the provided sender_engine.
 *
 * @param[in] sender_engine The NepEngine instance used to send descriptors to the output ring.
 * @param[in] scheduler The NepTaskScheduler used for scheduling tasks within the PPF path.
 * @param[out] ppf_fallback_stage A pointer to a pointer to the next stage pointer for
 * PPF fallback. This allows subsequent data path connections after the fallback.
 *
 * @return 0 on success, negative error code otherwise.
 */
int32_t NepPpfPathSetup(struct NepEngine *sender_engine, struct NepTaskScheduler *scheduler,
			      struct NepStage ***ppf_fallback_stage);

/**
 * @brief Get the PPF stage singleton object.
 *
 * This function retrieves the PPF stage singleton object. It is
 * guaranteed to return a valid NepStage pointer.
 *
 * @return A valid NepStage pointer representing the PPF stage
 * singleton.
 */
struct NepStage *NepPpfStageSingletonGet(void);

#endif /* NOA_NET_PPF_PIPELINE_SERVICE_H */
