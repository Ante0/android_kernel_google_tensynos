// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of Ring Pipeline Service Feedthrough Stage
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "ring_pipeline_service/service.h"

#include <linux/printk.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#include "common/compiler.h"
#include "common/core.h"
#include "common/ring_id.h"

#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#else /* linux */
#include "ring_pipeline_service/service.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "common/compiler.h"
#include "common/core.h"
#include "common/ring_id.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/task_scheduler.h"
#endif /* linux */

SEC_FAST_DATA static INIT_NEP_STAGE_ENDING_ROUTER(ending_router);
INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(SEC_FAST_DATA static,
					       feedthrough_parking_stage_container, 4);
SEC_FAST_DATA static INIT_NEP_STAGE(feedthrough_parking_stage, &ending_router,
				    &feedthrough_parking_stage_container.basic, NULL, NULL);

int32_t RingServiceFeedthroughStageSetup(struct NepEngine *sender_engine,
					 struct NepTaskScheduler *scheduler)
{
	if (!sender_engine || !scheduler) {
		pr_err("Invalid argument to setup ring feedthrough stage\n");
		return -EINVAL;
	}
	feedthrough_parking_stage.engine = RingServiceSenderEngineGet();
	NepStageAddToScheduler(&feedthrough_parking_stage, scheduler);
	return 0;
}
struct NepStage *RingServiceFeedthroughStageSingletonGet(void)
{
	return &feedthrough_parking_stage;
}

#define BIT_SIZE_OF_FALLBACK_STAGE (4U) // 2^4 = 16 packets

INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(SEC_FAST_DATA static, rx_fallback_stage_container,
					       BIT_SIZE_OF_FALLBACK_STAGE);
SEC_FAST_DATA static INIT_NEP_STAGE(rx_fallback_stage, &ending_router,
				    &rx_fallback_stage_container.basic, NULL, NULL);

INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(SEC_FAST_DATA static, tx_fallback_stage_container,
					       BIT_SIZE_OF_FALLBACK_STAGE);
SEC_FAST_DATA static INIT_NEP_STAGE(tx_fallback_stage, &ending_router,
				    &tx_fallback_stage_container.basic, NULL, NULL);

int32_t RingServiceFallbackStageSetup(struct NepEngine *sender_engine,
				      struct NepTaskScheduler *scheduler)
{
	if (!sender_engine || !scheduler) {
		pr_err("Invalid argument to setup ring fallback stage\n");
		return -EINVAL;
	}
	tx_fallback_stage.engine = sender_engine;
	tx_fallback_stage.scheduler = scheduler;
	NepStageAddToScheduler(&tx_fallback_stage, scheduler);

	rx_fallback_stage.engine = sender_engine;
	rx_fallback_stage.scheduler = scheduler;
	NepStageAddToScheduler(&rx_fallback_stage, scheduler);
	return 0;
}

struct NepStage *RingServiceTxFallbackStageSingletonGet(void)
{
	return &tx_fallback_stage;
}

struct NepStage *RingServiceRxFallbackStageSingletonGet(void)
{
	return &rx_fallback_stage;
}
