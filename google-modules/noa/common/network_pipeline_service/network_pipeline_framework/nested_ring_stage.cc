// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation for Nested Ring Stage
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#include "network_pipeline_framework/nested_ring_stage.h"

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/printk.h>
#include <linux/log2.h>

#include "common/inttypes.h"
#include "common/core.h"
#include "common/ring.h"
#else /* linux */
#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "linux_port/log.h"
#include "linux_port/io.h"
#include "linux_port/bitops.h"
#include "common/core.h"
#include "common/ring.h"
#include "sys_profiling/timestamp_source.h"
#endif /* linux */

static uint32_t GetTimestampTicks(void)
{
#ifdef linux
	return 0;
#else /* linux */
	return noa::profiling::DwtCyCntSource::Instance().GetGlobalTimestampTicks();
#endif /* linux */
}

void NestedCachedRingInit(NestedCachedRing *ring, char *base, uint16_t item_len, uint16_t size,
			  uint16_t *head, uint16_t *tail)
{
	ring->base = base;
	ring->size = size;
	ring->item_len = item_len;
	ring->head_idx_ptr = head;
	ring->tail_idx_ptr = tail;
	ring->processed_idx = readw(ring->tail_idx_ptr);
}

void NestedShadowRingInit(NestedShadowRing *ring, uintptr_t base, uint16_t item_len, uint16_t size,
			  uintptr_t *head, uintptr_t *tail)
{
	WARN_ON(!is_power_of_2(item_len));
	WARN_ON(!is_power_of_2(size));
	ring->base = base;
	ring->item_len_bitshift = ilog2(item_len);
	WARN_ON(!IS_ALIGNED(base, (size << ring->item_len_bitshift)));
	ring->end_mask = (size << ring->item_len_bitshift) - 1U;
	ring->head_addr_ptr = head;
	ring->tail_addr_ptr = tail;
	ring->processed_addr = noa_readptr(ring->tail_addr_ptr);
}

static void
InitStageHelper(NestedRingStage *stage, bool is_async, const NestedShadowRing shadow_ring_info,
		const NestedCachedRing cached_ring_info, void *context, NestedRingEngine *engine,
		NestedRingStage *next_stage, NestedRingTask *task, const char *name,
		uintptr_t **shadow_ring_completed_pptr, uint16_t **cached_ring_completed_pptr,
		bool (*has_remaining_data)(struct NestedRingStage *), void (*reset)(void *))
{
	stage->shadow_ring = shadow_ring_info;
	stage->shadow_ring_completed_pptr = shadow_ring_completed_pptr;
	stage->cached_ring = cached_ring_info;
	stage->cached_ring_completed_pptr = cached_ring_completed_pptr;
	stage->context = context;
	stage->engine = engine;
	stage->next_stage = next_stage;
	if (is_async) {
		NestedRingTaskSetup(task, stage, NestedRingStageAsyncProcessing);
	} else {
		NestedRingTaskSetup(task, stage, NestedRingStageProcessing);
	}
	stage->task = task;
	stage->name = name;
	stage->has_remaining_data = has_remaining_data;
	stage->reset = reset;

	// Metric Initialization
	stage->metrics.completed_counter = 0;
	stage->metrics.run_count = 0;
	stage->metrics.items_processed_count = 0;
	stage->metrics.eagain_count = 0;
	stage->metrics.total_execution_time_ticks = 0;
	stage->metrics.eagain_cpu_overhead_ticks = 0;

	INIT_LIST_HEAD(&stage->list);
}

static bool IsShadowRingHasData(NestedRingStage *stage)
{
	return readw(stage->shadow_ring.tail_addr_ptr) != readw(stage->shadow_ring.head_addr_ptr);
}

static bool IsAsyncShadowRingHasData(NestedRingStage *stage)
{
	return stage->shadow_ring.processed_addr != noa_readptr(stage->shadow_ring.head_addr_ptr);
}

void NestedRingStageInit(NestedRingStage *stage, bool is_async,
			 const NestedShadowRing shadow_ring_info,
			 const NestedCachedRing cached_ring_info, void *context,
			 NestedRingEngine *engine, NestedRingStage *next_stage,
			 NestedRingTask *task, const char *name, void (*reset)(void *))
{
	bool (*has_remaining_data)(struct NestedRingStage *);
	if (is_async) {
		has_remaining_data = IsAsyncShadowRingHasData;
	} else {
		has_remaining_data = IsShadowRingHasData;
	}
	InitStageHelper(stage, is_async, shadow_ring_info, cached_ring_info, context, engine,
			next_stage, task, name, &stage->shadow_ring.tail_addr_ptr,
			&stage->cached_ring.tail_idx_ptr, has_remaining_data, reset);
}

void NestedRingCacheStageInit(NestedRingStage *stage, bool is_async,
			      const NestedShadowRing shadow_ring_info,
			      const NestedCachedRing cached_ring_info, void *context,
			      NestedRingEngine *engine, NestedRingStage *next_stage,
			      NestedRingTask *task, const char *name,
			      bool (*has_remaining_data)(struct NestedRingStage *),
			      void (*reset)(void *))
{
	InitStageHelper(stage, is_async, shadow_ring_info, cached_ring_info, context, engine,
			next_stage, task, name, &stage->shadow_ring.head_addr_ptr,
			&stage->cached_ring.head_idx_ptr, has_remaining_data, reset);
}

void NestedRingStageReset(NestedRingStage *stage, uint16_t src_ring_idx, uint16_t src_ring_item_len)
{
	uint16_t cached_ring_size = stage->cached_ring.size;
	uint16_t cached_ring_tail_idx = src_ring_idx % cached_ring_size;
	uintptr_t shadow_ring_addr = stage->shadow_ring.base +
				     (cached_ring_tail_idx << stage->shadow_ring.item_len_bitshift);
	writew(cached_ring_tail_idx, stage->cached_ring.tail_idx_ptr);
	writew(cached_ring_tail_idx, stage->cached_ring.head_idx_ptr);
	stage->cached_ring.processed_idx = cached_ring_tail_idx;
	stage->cached_ring.item_len = src_ring_item_len;
	noa_writeptr(shadow_ring_addr, stage->shadow_ring.head_addr_ptr);
	noa_writeptr(shadow_ring_addr, stage->shadow_ring.tail_addr_ptr);
	stage->shadow_ring.processed_addr = shadow_ring_addr;

	// Metric Initialization
	stage->metrics.completed_counter = 0;
	stage->metrics.run_count = 0;
	stage->metrics.items_processed_count = 0;
	stage->metrics.eagain_count = 0;
	stage->metrics.total_execution_time_ticks = 0;
	stage->metrics.eagain_cpu_overhead_ticks = 0;

	if (stage->reset) {
		stage->reset(stage->context);
	}
}

int32_t NestedRingStageProcessing(void *context)
{
	int32_t ret;
	NestedRingStage *stage = (NestedRingStage *)context;
	NestedRingRequest req;
	NestedShadowRing shadow_ring;
	uintptr_t start_shadow_ring_addr;
	uint32_t start_time = GetTimestampTicks();

	WARN_ON(!stage);
	WARN_ON(!stage->engine);
	WARN_ON(!stage->task);

	stage->metrics.run_count++;

	start_shadow_ring_addr = noa_readptr(*(stage->shadow_ring_completed_pptr));
	shadow_ring = stage->shadow_ring;
	req.shadow_ring_addr = start_shadow_ring_addr;
	req.cached_ring_idx = readw(*(stage->cached_ring_completed_pptr));
	ret = stage->engine->processing(stage, &req);
	if (ret) {
		if (ret == -EAGAIN) {
			goto out;
		}
		stage->engine->error_handling(stage, &req, ret);
		stage->metrics.total_execution_time_ticks += (GetTimestampTicks() - start_time);
		return 0;
	}
	writew(req.cached_ring_idx, *(stage->cached_ring_completed_pptr));
	noa_writeptr(req.shadow_ring_addr, *(stage->shadow_ring_completed_pptr));

	if (req.shadow_ring_addr != start_shadow_ring_addr && stage->next_stage) {
		NestedRingTaskQueueToScheduler(stage->next_stage->task);
	}
	ret = 0;
	stage->metrics.items_processed_count +=
		((req.shadow_ring_addr - start_shadow_ring_addr) & shadow_ring.end_mask) >>
		shadow_ring.item_len_bitshift;

out:
	if (ret == -EAGAIN || stage->has_remaining_data(stage)) {
		if (ret == -EAGAIN) {
			stage->metrics.eagain_count++;
			stage->metrics.eagain_cpu_overhead_ticks +=
				(GetTimestampTicks() - start_time);
		}
		NestedRingTaskQueueToScheduler(stage->task);
	}
	stage->metrics.completed_counter++;
	stage->metrics.total_execution_time_ticks += (GetTimestampTicks() - start_time);

	return ret;
}

static inline NestedRingRequest *GetRequestFromPool(NestedRingRequestPool *pool)
{
	if (((pool->head + 1) & pool->size_mask) == pool->tail) {
		return NULL;
	}
	return &pool->arr[pool->head];
}

static inline void PopRequestFromPool(NestedRingRequestPool *pool)
{
	pool->head = (pool->head + 1) & pool->size_mask;
}

static inline void CompleteRequest(NestedRingRequest *req)
{
	NestedRingStage *stage = req->stage;
	WARN_ON(req->start_cached_ring_idx != readw(*(stage->cached_ring_completed_pptr)));
	WARN_ON(req->start_shadow_ring_addr != noa_readptr(*(stage->shadow_ring_completed_pptr)));

	writew(req->cached_ring_idx, *(stage->cached_ring_completed_pptr));
	noa_writeptr(req->shadow_ring_addr, *(stage->shadow_ring_completed_pptr));
	if (stage->next_stage) {
		NestedRingTaskQueueToScheduler(stage->next_stage->task);
	}
}

int32_t NestedRingRequestCompleteTask(void *context)
{
	uint32_t start_time = GetTimestampTicks();
	NestedRingEngine *engine = (NestedRingEngine *)context;
	NestedRingRequestPool *pool;

	WARN_ON(!engine);
	WARN_ON(!engine->request_pool);

	pool = engine->request_pool;

#ifdef linux
	/*
	* In this kernel simulator, operating under SMP conditions and utilizing kernel threads
	* (kthreads) to DMA and other hardware interactions, there's an increased risk of memory
	* ordering issues. Consequently, we've introduced **memory barriers** to mitigate these
	* potential problems.
	*
	* While the necessity of these barriers might be less critical in a single-core,
	* Non-SMP (NOA) environment where data patterns are more controlled (e.g., only a
	* callback or Interrupt Service Routine (ISR) updates the 'completed' value, and
	* completed tasks only read this value), they are crucial for correctness in the SMP
	* scenario.
	*/
	smp_mb();
#endif /* linux */

	while (pool->tail != pool->head) {
		NestedRingRequest *req = &pool->arr[pool->tail];
		if (!req->completed) {
			break;
		}
		if (engine->post_complete) {
			engine->post_complete(req);
			engine->metrics.post_complete_task_count++;
		}
		CompleteRequest(req);
		engine->metrics.requests_completed_count++;
		pool->tail = (pool->tail + 1) & pool->size_mask;
	}
	engine->metrics.total_post_complete_time_ticks += (GetTimestampTicks() - start_time);
	return 0;
}

void NestedRingRequestCompleteCallback(void *request)
{
	NestedRingRequest *req = (NestedRingRequest *)request;
	NestedRingStage *stage;
	NestedRingEngine *engine;

	WARN_ON(!req);
	WARN_ON(!req->stage);
	WARN_ON(!req->stage->engine);

	stage = req->stage;
	engine = stage->engine;

	req->completed = true;
#ifdef linux
	/*
	* In this kernel simulator, operating under SMP conditions and utilizing kernel threads
	* (kthreads) to DMA and other hardware interactions, there's an increased risk of memory
	* ordering issues. Consequently, we've introduced **memory barriers** to mitigate these
	* potential problems.
	*
	* While the necessity of these barriers might be less critical in a single-core,
	* Non-SMP (NOA) environment where data patterns are more controlled (e.g., only a
	* callback or Interrupt Service Routine (ISR) updates the 'completed' value, and
	* completed tasks only read this value), they are crucial for correctness in the SMP
	* scenario.
	*/
	smp_mb();
#endif /* linux */

	NestedRingTaskQueueToScheduler(engine->post_complete_task);
	NepBitmapTaskSchedulerSignal(engine->scheduler);
}

void NestedRingRequestPoolInit(NestedRingRequestPool *pool, uint8_t size, NestedRingRequest *arr)
{
	WARN_ON(!is_power_of_2(size));
	pool->size_mask = size - 1;
	pool->arr = arr;
	pool->tail = pool->head = 0;
}

static void FallbackAllPackets(NestedRingStage *stage, NestedRingRequest *req, int32_t ret)
{
	uintptr_t end;
	nep_pkt_info *pkt_info;

	NESTED_RING_FOR_EACH_ENTRY(stage, req, end, pkt_info)
	{
		pkt_info->action = PKT_ACTION_FALLBACK;
	}
	pr_warn("Failed to process stage %s, mark packet as fallback, ret %" PRId32 "\n",
		stage->name, ret);
}

static void InitEngineHelper(NestedRingEngine *engine,
			     int32_t (*processing)(NestedRingStage *, NestedRingRequest *),
			     NestedRingTask *post_complete_task,
			     void (*post_complete)(NestedRingRequest *request),
			     NestedRingRequestPool *request_pool, NepBitmapTaskScheduler *scheduler)
{
	engine->processing = processing;
	engine->error_handling = FallbackAllPackets;
	engine->post_complete_task = post_complete_task;
	engine->post_complete = post_complete;
	engine->request_pool = request_pool;
	engine->scheduler = scheduler;
	engine->metrics.post_complete_task_count = 0;
	engine->metrics.requests_completed_count = 0;
	engine->metrics.total_post_complete_time_ticks = 0;
}

void NestedRingEngineInit(NestedRingEngine *engine,
			  int32_t (*processing)(NestedRingStage *, NestedRingRequest *))
{
	InitEngineHelper(engine, processing, NULL, NULL, NULL, NULL);
}

void NestedRingAsyncEngineInit(NestedRingEngine *engine,
			       int32_t (*processing)(NestedRingStage *, NestedRingRequest *),
			       NestedRingTask *post_complete_task,
			       void (*post_complete)(NestedRingRequest *request),
			       NestedRingRequestPool *request_pool,
			       NepBitmapTaskScheduler *scheduler)
{
	NestedRingTaskSetup(post_complete_task, engine, NestedRingRequestCompleteTask);
	InitEngineHelper(engine, processing, post_complete_task, post_complete, request_pool,
			 scheduler);
}

int32_t NestedRingStageAsyncProcessing(void *context)
{
	int32_t ret;
	NestedRingStage *stage = (NestedRingStage *)context;
	NestedRingRequest *req;
	NestedRingEngine *engine;
	NestedRingRequestPool *pool;
	NestedShadowRing shadow_ring;
	uintptr_t start_shadow_ring_addr;
	uint32_t start_time = GetTimestampTicks();

	WARN_ON(!stage);
	WARN_ON(!stage->engine);
	WARN_ON(!stage->engine->request_pool);

	stage->metrics.run_count++;
	engine = stage->engine;
	pool = engine->request_pool;
	shadow_ring = stage->shadow_ring;

	req = GetRequestFromPool(pool);
	if (!req) {
		ret = -EAGAIN;
		goto out;
	}

	req->start_shadow_ring_addr = req->shadow_ring_addr = stage->shadow_ring.processed_addr;
	req->start_cached_ring_idx = req->cached_ring_idx = stage->cached_ring.processed_idx;
	req->stage = stage;
	req->completed = false;
	start_shadow_ring_addr = req->start_shadow_ring_addr;
	ret = engine->processing(stage, req);
	if (ret) {
		if (ret == -EAGAIN) {
			goto out;
		}
		engine->error_handling(stage, req, ret);
		ret = 0;
		goto out;
	}

	if (req->shadow_ring_addr == req->start_shadow_ring_addr) {
		ret = 0;
		goto out;
	}
	PopRequestFromPool(pool);
	stage->cached_ring.processed_idx = req->cached_ring_idx;
	stage->shadow_ring.processed_addr = req->shadow_ring_addr;
	ret = 0;
	stage->metrics.items_processed_count +=
		((req->shadow_ring_addr - start_shadow_ring_addr) & shadow_ring.end_mask) >>
		shadow_ring.item_len_bitshift;

out:
	if (ret == -EAGAIN || stage->has_remaining_data(stage)) {
		if (ret == -EAGAIN) {
			stage->metrics.eagain_count++;
			stage->metrics.eagain_cpu_overhead_ticks +=
				(GetTimestampTicks() - start_time);
		}
		NestedRingTaskQueueToScheduler(stage->task);
	}
	stage->metrics.completed_counter++;
	stage->metrics.total_execution_time_ticks += (GetTimestampTicks() - start_time);
	return ret;
}
