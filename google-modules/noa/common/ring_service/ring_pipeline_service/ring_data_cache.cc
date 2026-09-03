// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of ring data caching component.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "ring_pipeline_service/ring_data_cache.h"

#include <linux/printk.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#include "common/compiler.h"
#include "common/ring.h"
#include "common/inttypes.h"
#include "dma_simulator.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_service/task_manager.h"
#include "ring_manager_instance.h"
#else /* linux */
#include "ring_pipeline_service/ring_data_cache.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "common/compiler.h"
#include "common/ring.h"
#include "dma/templated_dma.h"
#include "device_mgmt/manager.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_service/task_manager.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "pw_assert/assert.h"
#endif /* linux */

// Enabled by default for Gem5 integration tests.
static bool g_ring_cache_netengine_activated = true;

void NestedRingCacheSetNetengineActivate(bool activate)
{
	g_ring_cache_netengine_activated = activate;
}

static inline void FillShadowEntryHelper(NestedRingStage *stage, uint16_t cached_ring_start,
					 uintptr_t shadow_ring_start, uintptr_t shadow_ring_end)
{
	NestedRingCacheContext *context = (NestedRingCacheContext *)stage->context;
	void *desc;
	void *entry;

	if (!context->fill_entry) {
		return;
	}

	NESTED_RING_FOR_EACH_DESC_AND_ENTRY(stage, &cached_ring_start, &shadow_ring_start,
					    shadow_ring_end, desc, entry)
	{
		context->fill_entry(desc, entry);
		if (!g_ring_cache_netengine_activated) {
			nep_pkt_info *pkt_info = (nep_pkt_info *)entry;
			if (pkt_info->action == PKT_ACTION_UNDECIDED) {
				pkt_info->action = PKT_ACTION_FALLBACK;
			}
		}
	}
}

static void FillShadowEntry(NestedRingRequest *req)
{
	FillShadowEntryHelper(req->stage, req->start_cached_ring_idx, req->start_shadow_ring_addr,
			      req->shadow_ring_addr);
}

int32_t NestedRingCacheDescriptorFromSram(struct NestedRingStage *stage, NestedRingRequest *req)
{
#ifndef linux
	using std::min;
#endif /* linux */
	NestedRingCacheContext *context = (NestedRingCacheContext *)stage->context;
	noa_ring_consumer *src_ring = &context->instance->ring;
	NestedShadowRing *shadow_ring = &stage->shadow_ring;
	NestedCachedRing *cached_ring = &stage->cached_ring;
	uintptr_t shadow_ring_head = noa_readptr(shadow_ring->head_addr_ptr);
	const uint16_t cached_ring_size = stage->cached_ring.size;
	const uint16_t cached_ring_item_len = stage->cached_ring.item_len;
	uint16_t cached_ring_tail = readw(cached_ring->tail_idx_ptr);
	uint16_t cached_ring_head = readw(cached_ring->head_idx_ptr);
	uint16_t cached_ring_space_to_end = noa_ring_free_items_to_end_count(
		cached_ring_head, cached_ring_tail, cached_ring_size);
	uint16_t src_ring_head = noa_ring_head_read_once(src_ring);
	uint16_t items =
		noa_ring_items_count(src_ring_head, src_ring->basic.tail, src_ring->basic.size);
	uint16_t count = min(cached_ring_space_to_end, items);

	if (!count || !is_noa_ring_activate(src_ring)) {
		return 0;
	}
	WARN_ON((src_ring->basic.tail & (cached_ring_size - 1)) != req->cached_ring_idx);
	// Because the data is already located in SRAM, we can move it directly without relying on DMA.
#ifndef linux
	InvalidateDCache(noa_ring_curr_tail_pos(&src_ring->basic), count * cached_ring_item_len);
#endif /* linux */
	memcpy(noa_ring_buf_pos(cached_ring->base, cached_ring_head, cached_ring_item_len),
	       noa_ring_curr_tail_pos(&src_ring->basic), count * cached_ring_item_len);

	src_ring->basic.tail = noa_ring_move_pos(src_ring->basic.tail, count, src_ring->basic.size);
	req->cached_ring_idx = noa_ring_move_pos(cached_ring_head, count, cached_ring_size);
	req->shadow_ring_addr = shadow_ring_move_pos(shadow_ring, shadow_ring_head, count);
	count = min((u32)items - count,
		    noa_ring_free_items_count(req->cached_ring_idx, cached_ring_tail,
					      cached_ring_size));
	if (!count) {
		goto completed;
	}
#ifndef linux
	InvalidateDCache(noa_ring_curr_tail_pos(&src_ring->basic), count * cached_ring_item_len);
#endif /* linux */
	memcpy(noa_ring_buf_pos(cached_ring->base, req->cached_ring_idx, cached_ring_item_len),
	       noa_ring_curr_tail_pos(&src_ring->basic), count * cached_ring_item_len);
	src_ring->basic.tail = noa_ring_move_pos(src_ring->basic.tail, count, src_ring->basic.size);
	req->cached_ring_idx = noa_ring_move_pos(req->cached_ring_idx, count, cached_ring_size);
	req->shadow_ring_addr = shadow_ring_move_pos(shadow_ring, req->shadow_ring_addr, count);

completed:
	FillShadowEntryHelper(stage, cached_ring_head, shadow_ring_head, req->shadow_ring_addr);
	return 0;
}

int32_t NestedRingCacheDescriptorFromDram(struct NestedRingStage *stage, NestedRingRequest *req)
{
#ifdef linux
	int32_t ret;
#define NepDtcmAddressMap(x) (x)
#define kMaxCacheDescriptorNum (256U)
#else /* linux */
	using std::min;
	using dma_cache_desc_request = ::noa::driver::dma::CacheDescriptorProgram::Request;
	using ::noa::driver::dma::TemplatedDma;
	using ProgramType = TemplatedDma::ProgramType;
	constexpr auto &kMaxCacheDescriptorNum =
		::noa::driver::dma::CacheDescriptorProgram::kMaxCacheDescriptorNum;
	constexpr auto &NepDtcmAddressMap =
		::noa::driver::dma::DmaControllerBase::NepDtcmAddressMap;
#endif /* linux */
	NestedRingCacheContext *context = (NestedRingCacheContext *)stage->context;
	noa_ring_consumer *src_ring = &context->instance->ring;
	NestedShadowRing *shadow_ring = &stage->shadow_ring;
	NestedCachedRing *cached_ring = &stage->cached_ring;
	uintptr_t shadow_ring_head = shadow_ring->processed_addr;
	const uint16_t cached_ring_size = stage->cached_ring.size;
	const uint16_t cached_ring_item_len = stage->cached_ring.item_len;
	uint16_t cached_ring_tail = readw(cached_ring->tail_idx_ptr);
	uint16_t cached_ring_head = cached_ring->processed_idx;
	uint16_t cached_ring_space_to_end = noa_ring_free_items_to_end_count(
		cached_ring_head, cached_ring_tail, cached_ring_size);
	uint16_t src_ring_head = noa_ring_head_read_once(src_ring);
	const uint16_t src_ring_tail_start = src_ring->basic.tail;
	uint16_t items =
		noa_ring_items_count(src_ring_head, src_ring->basic.tail, src_ring->basic.size);
	uint16_t count = min(cached_ring_space_to_end, items);
	dma_cache_desc_request dma_req;

	count = min(count, (uint16_t)kMaxCacheDescriptorNum);

	if (!count || !is_noa_ring_activate(src_ring)) {
		return 0;
	}
	WARN_ON((src_ring->basic.tail & (cached_ring_size - 1)) != req->cached_ring_idx);

	dma_req.desc_len = cached_ring_item_len;
	dma_req.unit[0].src = (uintptr_t)noa_ring_curr_tail_pos(&src_ring->basic);
	dma_req.unit[0].dst = NepDtcmAddressMap((uintptr_t)noa_ring_buf_pos(
		cached_ring->base, cached_ring_head, cached_ring_item_len));
	dma_req.unit[0].desc_num = count;

	src_ring->basic.tail = noa_ring_move_pos(src_ring->basic.tail, count, src_ring->basic.size);
	req->cached_ring_idx = noa_ring_move_pos(cached_ring_head, count, cached_ring_size);
	req->shadow_ring_addr = shadow_ring_move_pos(shadow_ring, shadow_ring_head, count);
	// Due to a DMA narrow transfer issue, the number of cached descriptors has been significantly
	// reduced. Because of this, the first transfer segment is not guaranteed to wrap the tail to
	// the start. We still need to use the free_items_to_end_count to calculate the available space.
	count = min((u32)items - count,
		    noa_ring_free_items_to_end_count(req->cached_ring_idx, cached_ring_tail,
						     cached_ring_size));
	count = min(count, (uint16_t)kMaxCacheDescriptorNum);
	if (!count) {
		dma_req.num = 1;
		goto completed;
	}

	dma_req.unit[1].src = (uintptr_t)noa_ring_curr_tail_pos(&src_ring->basic);
	dma_req.unit[1].dst = NepDtcmAddressMap((uintptr_t)noa_ring_buf_pos(
		cached_ring->base, req->cached_ring_idx, cached_ring_item_len));
	dma_req.unit[1].desc_num = count;
	dma_req.num = 2;
	src_ring->basic.tail = noa_ring_move_pos(src_ring->basic.tail, count, src_ring->basic.size);
	req->cached_ring_idx = noa_ring_move_pos(req->cached_ring_idx, count, cached_ring_size);
	req->shadow_ring_addr = shadow_ring_move_pos(shadow_ring, req->shadow_ring_addr, count);

completed:
	dma_req.context = (void *)req;
#ifdef linux
	dma_req.callback = NestedRingRequestCompleteCallback;
	ret = queue_dma_cache_desc_request(&dma_req);
	if (ret) {
		src_ring->basic.tail = src_ring_tail_start;
		return -EAGAIN;
	}
#else /* linux */
	dma_req.callback = [](pw::Status, void *context) {
		NestedRingRequestCompleteCallback(context);
	};
	auto ret = reinterpret_cast<TemplatedDma *>(context->dma)
			   ->InitiateTransfer(
				   TemplatedDma::ProgramToChannel(ProgramType::kCacheDescriptor),
				   ProgramType::kCacheDescriptor, dma_req);
	if (!ret.ok()) {
		src_ring->basic.tail = src_ring_tail_start;
		PW_LOG_ERROR("Failed to init DMA cache descriptor for ring %s with ret %s",
			     src_ring->name, ret.str());
		return -EAGAIN;
	}
#endif /* linux */
	return 0;
}

static bool HasDataToBeCached(NestedRingStage *stage)
{
	NestedRingCacheContext *context = (NestedRingCacheContext *)stage->context;
	noa_ring_consumer *src_ring = &context->instance->ring;
	return is_noa_ring_activate(src_ring) && !noa_ring_is_empty(src_ring);
}

NestedRingEngine *NestedRingCacheEngineSingletonGet(void)
{
	SEC_FAST_DATA static NestedRingEngine engine;
	return &engine;
}

#ifdef linux
#define CACHE_ENGINE_REQUEST_NUM (8U)
#else /* linux */
#define CACHE_ENGINE_REQUEST_NUM (2U)
#endif /* linux */

int32_t NestedRingCacheEngineInit(NestedRingEngine *engine, NepBitmapTaskScheduler *scheduler,
				  NestedRingTask *post_complete_task)
{
	SEC_FAST_DATA static NestedRingRequestPool req_pool;
	SEC_FAST_DATA static NestedRingRequest req_arr[CACHE_ENGINE_REQUEST_NUM];

	NestedRingRequestPoolInit(&req_pool, CACHE_ENGINE_REQUEST_NUM, req_arr);
	NestedRingAsyncEngineInit(engine, NestedRingCacheDescriptorFromDram, post_complete_task,
				  FillShadowEntry, &req_pool, scheduler);
	return 0;
};

int32_t NestedRingCacheStageSetup(const NestedRingCacheStageSetupParams *params)
{
	bool is_async;
	NestedRingStage *stage = params->stage;
	NestedRingEngine *engine = params->engine;
	NestedRingCacheContext *context = params->cache_context;
	struct ring_manager_instance *ring_instance =
		NoaRingManagerInfoInstanceGetById(params->path_id, kNoaRingNepInput);

	if (!ring_instance) {
		pr_err("Invalid ring instance %" PRIu16 " when setup cache data stage\n",
		       params->path_id);
		return -EINVAL;
	}
	context->instance = ring_instance;
	context->fill_entry = params->fill_entry;
	context->dma = params->dma;

	if (!params->is_dram_ring) {
		is_async = false;
		NestedRingEngineInit(engine, NestedRingCacheDescriptorFromSram);
	} else {
		is_async = true;
#ifndef linux
		PW_CHECK_NOTNULL(context->dma);
#endif /* linux */
	}
	NestedRingCacheStageInit(stage, is_async, *params->shadow_ring_info,
				 *params->cached_ring_info, context, engine, params->next_stage,
				 params->task, params->name, HasDataToBeCached, NULL);
	return 0;
}
