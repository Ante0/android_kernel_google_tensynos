/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header of Tethering Pipeline Service
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Mark Chien <markchien@google.com>, KH Shi <kenghua@google.com>
 */

#ifndef NOA_NET_TETHERING_PIPELINE_SERVICE_H
#define NOA_NET_TETHERING_PIPELINE_SERVICE_H

#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>

#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#else /* linux */
#include <cstdint>

#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#endif /* linux */

typedef struct {
	const char *name;
	NestedRingTask *task;
	NestedRingStage *stage;
	NestedShadowRing *shadow_ring_info;
	NestedCachedRing *cached_ring_info;
	NestedRingEngine *engine;
	NestedRingStage *next_stage;
} NestedRingTetheringStageSetupParams;

/**
 * @brief Sets up a nested ring stage for tethering traffic.
 *
 * This function configures a specific stage within the nested ring pipeline
 * designed to handle tethering traffic.
 *
 * @param[in] params A pointer to the NestedRingTetheringStageSetupParams
 * structure containing all necessary configuration details.
 * @param[in] is_wlan A boolean flag indicating whether this stage is intended
 * for processing WLAN traffic (true) or Modem traffic (false).
 *
 * @return 0 on successful setup.
 *         Negative error code otherwise.
 */
int32_t NestedRingTetheringStageSetup(const NestedRingTetheringStageSetupParams *params,
				      bool is_wlan);

/**
 * @brief Set up the Tethering data path.
 *
 * This function establishes the Tethering data path by building and
 * connecting the buffer_pool_stage and packet_movement_stage. This
 * setup enables all necessary operations for tethering. If a fallback
 * condition is detected, the function will instead utilize the fallback
 * stage, sending the descriptor out without modification. All completed
 * packets, including fallback packets, are sent to the output ring
 * through the provided sender_engine.
 *
 * @param[in] sender_engine The NepEngine instance used to send descriptors to the output ring.
 * @param[in] scheduler The NepTaskScheduler used for scheduling tasks within the tethering path.
 * @param[out] tethering_fallback_stage A pointer to a pointer to the next stage pointer for
 * tethering fallback. This allows subsequent data path connections after the fallback.
 *
 * @return 0 on success, negative error code otherwise.
 */
int32_t NepTetheringPathSetup(struct NepEngine *sender_engine, struct NepTaskScheduler *scheduler,
			      struct NepStage ***tethering_fallback_stage);

/**
 * @brief Get the Tethering stage singleton object.
 *
 * This function retrieves the Tethering stage singleton object. It is
 * guaranteed to return a valid NepStage pointer.
 *
 * @return A valid NepStage pointer representing the Tethering stage
 * singleton.
 */
struct NepStage *NepTetheringStageSingletonGet(void);

#endif /* NOA_NET_TETHERING_PIPELINE_SERVICE_H */
