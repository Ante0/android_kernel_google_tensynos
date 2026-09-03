// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of Tethering Pipeline Service
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Mark Chien <markchien@google.com>, KH Shi <kenghua@google.com>
 */
#ifdef linux
#include "tethering_pipeline_service.h"

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>

#if IS_ENABLED(CONFIG_NOA_MD_SAMSUNG_SUPPORT)
#include "sim/ncp/md/samsung/ncp_md_fw.h"
#endif /* CONFIG_NOA_MD_SAMSUNG_SUPPORT */

#include "common/compiler.h"
#include "common/core.h"
#include "common/ring_id.h"
#include "offload.h"
#include "if_ether.h"
#include "nep_tables.h"
#include "netengine_utils.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "network_pipeline_module/dma_engine.h"
#include "network_pipeline_module/buffer_pool_engine.h"
#else /* linux */
#include "net/tethering_pipeline_service.h"

#include <cerrno>
#include <cstdint>

#include "common/compiler.h"
#include "common/core.h"
#include "common/ring_id.h"
#include "linux_port/log.h"
#include "net/offload.h"
#include "net/nep_tables.h"
#include "net/netengine_utils.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "network_pipeline_module/dma_engine.h"
#include "network_pipeline_module/buffer_pool_engine.h"
#endif /* linux */

#define MAX_TETHERING_PROCESSING_COUINT (1U)

static int32_t TetheringEngineWlanPktProcessing(struct NestedRingStage *stage,
						NestedRingRequest *req)
{
	int32_t ret;
	uintptr_t end;
	nep_device_entry *entry;
	bool is_downstream = false;
	// To prevent the tethering engine from consuming excessive time,
	// processing is exited immediately after a packet lookup is completed.
	uint32_t count = MAX_TETHERING_PROCESSING_COUINT;

	NESTED_RING_FOR_EACH_ENTRY_N(stage, req, end, entry, count)
	{
		if (entry->pkt_info.action != PKT_ACTION_UNDECIDED) {
			continue;
		}

#if defined(USE_NETENGINE_HW_OFFLOAD)
		// Hardware offload is enabled. We skip software NAT lookups.
		// Leave the action as PKT_ACTION_UNDECIDED so the packet
		// flows to the next stage (SdnRxStage).
		(void)ret;
		(void)is_downstream;
		continue;
#else
		ret = NetEngineTetheringHandleWlanPacket(&entry->pkt_info, &entry->forward_info,
							 &is_downstream);
		if (ret) {
			entry->pkt_info.action = PKT_ACTION_UNDECIDED;
#ifdef linux
			net_engine_offload_util_cnt_inc(true, is_downstream);
#endif /* linux */
		} else {
#ifdef linux
			net_engine_offload_util_cnt_inc(false, is_downstream);
#endif /* linux */
		}
#endif
	}
	return 0;
}

static int32_t TetheringEngineModemPktProcessing(struct NestedRingStage *stage,
						 NestedRingRequest *req)
{
	int32_t ret;
	uintptr_t end;
	nep_device_entry *entry;
#ifdef linux
	bool is_downstream = false;
#endif /* linux */
	// To prevent the tethering engine from consuming excessive time,
	// processing is exited immediately after a packet lookup is completed.
	uint32_t count = MAX_TETHERING_PROCESSING_COUINT;

	NESTED_RING_FOR_EACH_ENTRY_N(stage, req, end, entry, count)
	{
		if (entry->pkt_info.action != PKT_ACTION_UNDECIDED) {
			continue;
		}

#if defined(USE_NETENGINE_HW_OFFLOAD)
		// Hardware offload is enabled. We skip software NAT lookups.
		// Leave the action as PKT_ACTION_UNDECIDED so the packet
		// flows to the next stage (SdnRxStage).
		(void)ret;
		continue;
#else
		ret = NetEngineTetheringHandleModemPacket(&entry->pkt_info, &entry->forward_info);
		if (ret) {
			entry->pkt_info.action = PKT_ACTION_UNDECIDED;
#ifdef linux
			net_engine_offload_util_cnt_inc(true, is_downstream);
#endif /* linux */
		} else {
#ifdef linux
			net_engine_offload_util_cnt_inc(false, is_downstream);
#endif /* linux */
		}
#endif
	}
	return 0;
}

int32_t NestedRingTetheringStageSetup(const NestedRingTetheringStageSetupParams *params,
				      bool is_wlan)
{
	NestedRingStage *stage = params->stage;
	NestedRingEngine *engine = params->engine;

	if (is_wlan) {
		NestedRingEngineInit(engine, TetheringEngineWlanPktProcessing);
	} else {
		NestedRingEngineInit(engine, TetheringEngineModemPktProcessing);
	}

	NestedRingStageInit(stage, false, *params->shadow_ring_info, *params->cached_ring_info,
			    NULL, engine, params->next_stage, params->task, params->name, NULL);
	return 0;
}

static int32_t TetheringEngineProcessing(struct NepEngine *engine __attribute__((unused)),
					 struct NepProcessingRequest *request)
{
	uint8_t interface;
	uint8_t flow;
	uint8_t category;
	int32_t ret;
	struct NepPacketContext *packet = request->packet;
	struct noa_desc *desc = (struct noa_desc *)&packet->noa_desc[0];
	bool is_downstream = false;
	nep_pkt_info pkt_info;
	nep_forward_info nw_info;

	if (desc->reason == FWD_REASON_FEEDTHROUGH) {
		ret = -ENOENT;
		goto out;
	}

	ParseNoaDescToNepPktInfo(desc, &pkt_info);
	NoaRingPathIdParse(packet->src, &interface, &flow, &category);
	if (interface == kNoaNetworkInterfaceWlan) {
		ret = NetEngineTetheringHandleWlanPacket(&pkt_info, &nw_info, &is_downstream);
	} else if (interface == kNoaNetworkInterfaceModem) {
		ret = NetEngineTetheringHandleModemPacket(&pkt_info, &nw_info);
	} else {
		pr_err("Invalid interface type %" PRIu16 "\n", interface);
		return -EINVAL;
	}
out:
	// TODO(b/386895928) - Add nep stats metric.
	if (ret) {
		FallbackPacketDescriptorFormat(packet);
#ifdef linux
		net_engine_offload_util_cnt_inc(true, is_downstream);
#endif /* linux */
	} else {
		NetEngineForwardDescriptorFormat(desc, &pkt_info, &nw_info);
		packet->dst = desc->dst;
#ifdef linux
		net_engine_offload_util_cnt_inc(false, is_downstream);
#endif /* linux */
	}
	return 0;
}

#define BIT_SIZE_OF_TETHERING_STAGE (4U) // 2^4 = 16 packets

SEC_FAST_DATA static INIT_NEP_STAGE_ENDING_ROUTER(ending_router);
INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(SEC_FAST_DATA static,
					       tethering_parking_stage_container,
					       BIT_SIZE_OF_TETHERING_STAGE);
SEC_FAST_DATA static INIT_NEP_STAGE(tethering_parking_stage, &ending_router,
				    &tethering_parking_stage_container.basic, NULL, NULL);

SEC_FAST_DATA static INIT_NEP_STAGE_DIRECT_ROUTER(packet_movement_stage_router,
						  &tethering_parking_stage);
INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(SEC_FAST_DATA static,
					       packet_movement_stage_container,
					       BIT_SIZE_OF_TETHERING_STAGE);
SEC_FAST_DATA static NepPacketMovementStage packet_movement_stage;

SEC_FAST_DATA static INIT_NEP_STAGE_DIRECT_ROUTER(tethering_buffer_pool_stage_router,
						  &packet_movement_stage.stage);
INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(SEC_FAST_DATA static,
					       tethering_buffer_pool_stage_container,
					       BIT_SIZE_OF_TETHERING_STAGE);
SEC_FAST_DATA static INIT_NEP_STAGE(tethering_buffer_pool_stage,
				    &tethering_buffer_pool_stage_router.basic,
				    &tethering_buffer_pool_stage_container.basic, NULL, NULL);

static bool IsFallback(const struct NepPacketContext *packet __attribute__((unused)))
{
	return true;
}
SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_RULE(fallback_path, IsFallback, NULL, NULL);

static bool IsForwardPath(const struct NepPacketContext *packet)
{
	return ((const struct noa_desc *)&packet->noa_desc[0])->reason != FWD_REASON_FALLBACK;
}
SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_RULE(tethering_forwarding_path, IsForwardPath,
						  &tethering_buffer_pool_stage, &fallback_path);

INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(SEC_FAST_DATA static, tethering_stage_container,
					       BIT_SIZE_OF_TETHERING_STAGE);
SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_CHAIN_ROUTER(tethering_router,
							  &tethering_forwarding_path);

SEC_FAST_DATA static INIT_NEP_ENGINE(tethering_engine, NULL, TetheringEngineProcessing);
SEC_FAST_DATA static INIT_NEP_STAGE(tethering_stage, &tethering_router.basic,
				    &tethering_stage_container.basic, &tethering_engine, NULL);

int32_t NepTetheringPathSetup(struct NepEngine *sender_engine, struct NepTaskScheduler *scheduler,
			      struct NepStage ***tethering_fallback_stage)
{
	int32_t ret;
	tethering_parking_stage.engine = sender_engine;
	tethering_parking_stage.scheduler = scheduler;
	NepStageAddToScheduler(&tethering_parking_stage, scheduler);

	ret = NepPacketMovementStageConstructor(&packet_movement_stage,
						&packet_movement_stage_router.basic,
						&packet_movement_stage_container.basic,
						NepDmaEngineSingletonGet(), scheduler);
	if (ret) {
		pr_err("Failed to setup packet movement stage\n");
		return ret;
	}
	tethering_buffer_pool_stage.engine = &NepBufferPoolEngineSingletonGet()->engine;
	tethering_buffer_pool_stage.scheduler = scheduler;
	NepStageAddToScheduler(&tethering_buffer_pool_stage, scheduler);

	tethering_engine.scheduler = scheduler;
	NepEngineAddToScheduler(&tethering_engine, scheduler);

	tethering_stage.scheduler = scheduler;
	NepStageAddToScheduler(&tethering_stage, scheduler);
	*tethering_fallback_stage = &fallback_path.next_stage;

	return 0;
}

struct NepStage *NepTetheringStageSingletonGet(void)
{
	return &tethering_stage;
}
