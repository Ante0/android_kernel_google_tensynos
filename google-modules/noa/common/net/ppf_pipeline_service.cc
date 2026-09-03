// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of Pixel Packet Filter (PPF) Pipeline Service
 *
 * Copyright 2025 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifdef linux
#include "ppf_pipeline_service.h"

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>

#include "common/compiler.h"
#include "common/core.h"
#include "common/ring_id.h"
#include "netengine_utils.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#else /* linux */
#include "net/ppf_pipeline_service.h"

#include <cerrno>
#include <cstdint>

#include "common/compiler.h"
#include "common/core.h"
#include "common/ring_id.h"
#include "linux_port/log.h"
#include "net/netengine_utils.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#endif /* linux */

#define MAX_PPF_PROCESSING_COUNT (1U)

static int32_t PpfPktProcessing(struct NestedRingStage *stage, NestedRingRequest *req)
{
	int32_t ret;
	uintptr_t end;
	nep_device_entry *entry;
	// To prevent the PPF engine from consuming excessive time,
	// processing is exited immediately after a packet lookup is completed.
	uint32_t count = MAX_PPF_PROCESSING_COUNT;

	NESTED_RING_FOR_EACH_ENTRY_N(stage, req, end, entry, count)
	{
		if (entry->pkt_info.action != PKT_ACTION_UNDECIDED) {
			continue;
		}

		ret = NetEnginePpfHandleWlanPacket(&entry->pkt_info, &entry->ppf_info);
		if (ret) {
			entry->pkt_info.action = PKT_ACTION_FILTER_PACKET;
		}
	}
	return 0;
}

int32_t NestedRingPpfStageSetup(const NestedRingPpfStageSetupParams *params)
{
	NestedRingStage *stage = params->stage;
	NestedRingEngine *engine = params->engine;

	NestedRingEngineInit(engine, PpfPktProcessing);
	NestedRingStageInit(stage, false, *params->shadow_ring_info, *params->cached_ring_info,
			    NULL, engine, params->next_stage, params->task, params->name, NULL);
	return 0;
}

static int32_t PpfEngineProcessing(struct NepEngine *engine __attribute__((unused)),
				   struct NepProcessingRequest *request)
{
	uint8_t interface;
	uint8_t flow;
	uint8_t category;
	int32_t ret = 0;
	struct NepPacketContext *packet = request->packet;
	struct noa_desc *in_desc = (struct noa_desc *)&packet->noa_desc[0];
	nep_pkt_info pkt_info;

	ParseNoaDescToNepPktInfo(in_desc, &pkt_info);
	NoaRingPathIdParse(packet->src, &interface, &flow, &category);
	if (interface == kNoaNetworkInterfaceWlan) {
		ret = NetEnginePpfHandleWlanPacket(&pkt_info, NULL);
	}

	if (ret) {
		packet->should_recycle_original_buffer = true;
	} else {
		FallbackPacketDescriptorFormat(packet);
	}
	return 0;
}

#define BIT_SIZE_OF_PPF_STAGE (4U) // 2^4 = 16 packets

INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(SEC_FAST_DATA static,
					       ppf_stage_container,
					       BIT_SIZE_OF_PPF_STAGE);

static bool IsFallbackPath(const struct NepPacketContext *packet)
{
	return !packet->should_recycle_original_buffer;
}
SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_RULE(ppf_fallback_path, IsFallbackPath, NULL, NULL);
SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_CHAIN_ROUTER(ppf_router, &ppf_fallback_path);

SEC_FAST_DATA static INIT_NEP_ENGINE(ppf_engine, NULL, PpfEngineProcessing);
SEC_FAST_DATA static INIT_NEP_STAGE(ppf_stage, &ppf_router.basic,
				    &ppf_stage_container.basic, &ppf_engine, NULL);

int32_t NepPpfPathSetup(struct NepEngine *sender_engine, struct NepTaskScheduler *scheduler,
			struct NepStage ***ppf_fallback_stage)
{
	(void)sender_engine;
	ppf_engine.scheduler = scheduler;
	NepEngineAddToScheduler(&ppf_engine, scheduler);

	ppf_stage.scheduler = scheduler;
	NepStageAddToScheduler(&ppf_stage, scheduler);

	*ppf_fallback_stage = &ppf_fallback_path.next_stage;
	return 0;
}

struct NepStage *NepPpfStageSingletonGet(void)
{
	return &ppf_stage;
}
