// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ring Pipline Service Cached Buffer Pool Component
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "ring_pipeline_service/cached_buffer_pool.h"

#include <linux/kernel.h>

#include "common/compiler.h"
#include "common/inttypes.h"
#include "common/noa_share/types.h"
#include "common/core.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "common/memory.h"
#include "dma_simulator.h"
#include "network_pipeline_service/task_manager.h"
#include "ring_manager_instance.h"
#else /* linux */
#include "ring_pipeline_service/cached_buffer_pool.h"

#include <cinttypes>
#include <cstdint>

#include "arch/memory.h"
#include "common/ring_id.h"
#include "common/core.h"
#include "common/compiler.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "common/noa_share/types.h"
#include "dma/templated_dma.h"
#include "device_mgmt/manager.h"
#include "network_pipeline_service/task_manager.h"
#include "ring_mgmt/ring_manager_instance.h"
#endif /* linux */

#ifdef linux
#define kMaxCacheDescriptorNum (DMA_CACHE_BUFFER_POOL_DESC_NUM)
#else /* linux */
constexpr auto kMaxCacheDescriptorNum =
	::noa::driver::dma::CacheBufferPoolProgram::kMaxCacheDescriptorNum;
using noa::driver::dma::TemplatedDma;
using noa::module::device_mgmt::DeviceManager;
#endif /* linux */

NepBufferPoolContext *NepBufferPoolContextSingletonGet(uint8_t id)
{
	SEC_FAST_DATA static NepBufferPoolContext array[NOA_RING_SERVICE_BUFFER_POOL_NUMBER];

	if (id >= NOA_RING_SERVICE_BUFFER_POOL_NUMBER) {
		return NULL;
	}
	return &array[id];
}

NestedRingEngine *NepBufferPoolCacheEngineSingletonGet(void)
{
	SEC_FAST_DATA static NestedRingEngine engine;
	return &engine;
}

#define CACHE_ENGINE_REQUEST_NUM (2U)
int32_t NepBufferPoolCacheEngineInit(NestedRingEngine *engine, NepBitmapTaskScheduler *scheduler,
				     NestedRingTask *post_complete_task)
{
	SEC_FAST_DATA static NestedRingRequestPool req_pool;
	SEC_FAST_DATA static NestedRingRequest req_arr[CACHE_ENGINE_REQUEST_NUM];

	NestedRingRequestPoolInit(&req_pool, CACHE_ENGINE_REQUEST_NUM, req_arr);
	NestedRingAsyncEngineInit(engine, NepBufferPoolCacheData, post_complete_task, NULL,
				  &req_pool, scheduler);
	return 0;
};

static bool ShouldWaitForBatch(void *context)
{
	NestedRingStage *stage = (NestedRingStage *)context;
	NestedCachedRing *cached_ring = &stage->cached_ring;
	const uint16_t cached_ring_size = stage->cached_ring.size;
	uint16_t cached_ring_tail = readw(cached_ring->tail_idx_ptr);
	uint16_t cached_ring_head = cached_ring->processed_idx;
	uint16_t count =
		noa_ring_free_items_count(cached_ring_head, cached_ring_tail, cached_ring_size);
	return count < kMaxCacheDescriptorNum;
}

int32_t NepBufferPoolCacheData(struct NestedRingStage *stage, NestedRingRequest *req)
{
#ifdef linux
	int32_t ret;
#define NepDtcmAddressMap(x) (x)
#else /* linux */
	using std::min;
	using dma_cache_buffer_pool_request = ::noa::driver::dma::CacheBufferPoolProgram::Request;
	using ::noa::driver::dma::TemplatedDma;
	using ProgramType = TemplatedDma::ProgramType;
	constexpr auto &NepDtcmAddressMap =
		::noa::driver::dma::DmaControllerBase::NepDtcmAddressMap;
#endif /* linux */
	NepBufferPoolContext *context = (NepBufferPoolContext *)stage->context;
	noa_ring_consumer *src_ring = &context->instance->ring;
	NestedShadowRing *shadow_ring = &stage->shadow_ring;
	NestedCachedRing *cached_ring = &stage->cached_ring;
	uintptr_t shadow_ring_head = shadow_ring->processed_addr;
	const uint16_t cached_ring_size = stage->cached_ring.size;
	const uint16_t cached_ring_item_len = stage->cached_ring.item_len;
	uint16_t cached_ring_head = cached_ring->processed_idx;
	uint16_t src_ring_head = noa_ring_head_read_once(src_ring);
	const uint16_t src_ring_tail_start = src_ring->basic.tail;
	uint16_t count =
		noa_ring_items_count(src_ring_head, src_ring->basic.tail, src_ring->basic.size);
	dma_cache_buffer_pool_request dma_req;

	if (!is_noa_ring_activate(src_ring)) {
		return 0;
	} else if (count < kMaxCacheDescriptorNum) {
		return -EAGAIN;
	}
	WARN_ON((src_ring->basic.tail & (cached_ring_size - 1)) != req->cached_ring_idx);

	dma_req.src = (uintptr_t)noa_ring_curr_tail_pos(&src_ring->basic);
	dma_req.dst = NepDtcmAddressMap((uintptr_t)noa_ring_buf_pos(
		cached_ring->base, cached_ring_head, cached_ring_item_len));

	src_ring->basic.tail = noa_ring_move_pos(src_ring->basic.tail, kMaxCacheDescriptorNum,
						 src_ring->basic.size);
	req->cached_ring_idx =
		noa_ring_move_pos(cached_ring_head, kMaxCacheDescriptorNum, cached_ring_size);
	req->shadow_ring_addr =
		shadow_ring_move_pos(shadow_ring, shadow_ring_head, kMaxCacheDescriptorNum);

	dma_req.context = (void *)req;
#ifdef linux
	dma_req.callback = NestedRingRequestCompleteCallback;
	ret = queue_dma_cache_buffer_pool_request(&dma_req);
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
				   TemplatedDma::ProgramToChannel(ProgramType::kCacheBufferPool),
				   ProgramType::kCacheBufferPool, dma_req);
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
	NepBufferPoolContext *context = (NepBufferPoolContext *)stage->context;
	noa_ring_consumer *src_ring = &context->instance->ring;
	return is_noa_ring_activate(src_ring) && !noa_ring_is_empty(src_ring);
}

static void ResetBufferPool(void *data)
{
	NepBufferPoolContext *context = (NepBufferPoolContext *)data;

	context->src_ring_consumer_idx = noa_ring_tail_read_once(&context->instance->ring);
	context->cached_ring_consumer_idx =
		context->src_ring_consumer_idx & (context->cache_stage.cached_ring.size - 1);
}

int32_t NepBufferPoolCacheStageSetup(const NepBufferPoolCacheStageSetupParams *params)
{
	NestedRingStage *stage = params->stage;
	NestedRingEngine *engine = params->engine;
	NepBufferPoolContext *context = params->context;
	struct ring_manager_instance *ring_instance =
		NoaRingManagerInfoInstanceGetById(params->path_id, kNoaRingNepInput);

	if (!ring_instance) {
		pr_err("Invalid ring instance %" PRIu16 " when setup buffer pool cache stage\n",
		       params->path_id);
		return -EINVAL;
	}
	context->instance = ring_instance;
	context->dma = params->dma;

#ifndef linux
	PW_CHECK_NOTNULL(context->dma);
#endif /* linux */
	NestedRingCacheStageInit(stage, true, *params->shadow_ring_info, *params->cached_ring_info,
				 context, engine, params->next_stage, params->task, params->name,
				 HasDataToBeCached, ResetBufferPool);
	stage->task->should_defer = ShouldWaitForBatch;
	return 0;
}

int32_t NepBufferPoolGetBuffer(NepBufferPoolContext *context, noa_buffer_pool_desc *desc)
{
	const noa_buffer_pool_desc *src_desc;
	uint16_t cached_ring_head = readw(context->cache_stage.cached_ring.head_idx_ptr);

	if (__noa_ring_is_empty(cached_ring_head, context->cached_ring_consumer_idx)) {
		return -EAGAIN;
	}

	src_desc = (const noa_buffer_pool_desc *)noa_ring_buf_pos(
		context->cache_stage.cached_ring.base, context->cached_ring_consumer_idx,
		context->cache_stage.cached_ring.item_len);
	// Since this data is located in DTCM, we do not need to perform cache invalidation.
	*desc = *src_desc;
	context->src_ring_consumer_idx = noa_ring_move_pos(context->src_ring_consumer_idx, 1,
							   context->instance->ring.basic.size);
	context->cached_ring_consumer_idx = noa_ring_move_pos(
		context->cached_ring_consumer_idx, 1, context->cache_stage.cached_ring.size);
	return 0;
}

void NepBufferPoolCompleted(NepBufferPoolContext *context, uint16_t val)
{
	// Since the DMA has already updated the source ring, we only update the cache ring here.
	writew(val, context->cache_stage.cached_ring.tail_idx_ptr);
}

uintptr_t NepBufferPoolSrcRingTailAddressGet(NepBufferPoolContext *context)
{
	return (uintptr_t)context->instance->ring.regs.read;
}

uint16_t NepBufferPoolSrcRingConsumerIdxGet(NepBufferPoolContext *context)
{
	return context->src_ring_consumer_idx;
}

uint16_t NepBufferPoolCachedRingConsumerIdxGet(NepBufferPoolContext *context)
{
	return context->cached_ring_consumer_idx;
}

void NepBufferPoolUpdateAllRingConsumerIdx(NepBufferPoolContext *context, uint16_t value)
{
	context->src_ring_consumer_idx = value;
	context->cached_ring_consumer_idx = value & (context->cache_stage.cached_ring.size - 1);
}

void NepBufferPoolUpdateCachedRingConsumerIdx(NepBufferPoolContext *context, uint16_t value)
{
	context->cached_ring_consumer_idx = value & (context->cache_stage.cached_ring.size - 1);
}
