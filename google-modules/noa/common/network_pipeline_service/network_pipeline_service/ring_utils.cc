// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of Network Pipeline Service With Nested Ring.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifdef linux
#include "network_pipeline_service/ring_utils.h"

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/list.h>

#include "common/inttypes.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "ring_mgmt/ring_manager_instance.h"
#else /* linux */
#include "network_pipeline_service_private/ring_utils.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "linux_port/list.h"
#include "linux_port/log.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "sys_profiling/timestamp_source.h"
#endif /* linux */

void NepRingStageRegister(struct ring_manager_instance *instance, NestedRingStage *stage)
{
	WARN_ON(!list_empty(&stage->list));
	list_add_tail(&stage->list, &instance->stage_list);
}

static uint32_t TickToUs(uint64_t ticks)
{
#ifdef linux
	return 0;
#else /* linux */
	noa::profiling::DwtCyCntSource &source = noa::profiling::DwtCyCntSource::Instance();
	return noa::profiling::TicksToUs(&source, ticks);
#endif /* linux */
}

static void ResetAllStage(struct ring_manager_instance *instance)
{
	uint16_t tail_idx;
	uint16_t item_len;
	NestedRingStage *stage;

	tail_idx = noa_ring_tail_read_once(&instance->ring);
	item_len = instance->ring.basic.item_len;
	list_for_each_entry (stage, &instance->stage_list, list) {
		NestedRingStageReset(stage, tail_idx, item_len);
	}
}

void NepRingRegisterResetFunction(struct ring_manager_instance *instance)
{
	instance->reset = ResetAllStage;
}

static void PrintStage(NestedRingStage *stage)
{
	uint8_t i = 0;
	NestedRingRequestPool *pool = stage->engine->request_pool;

	pr_info("- Stage %s scheduled %" PRIu16 "\n", stage->name,
		NestedRingTaskIsScheduled(stage->task));
	pr_info("\t - run: %" PRIu32 " again: %" PRIu32 " completed: %" PRIu32
		" items processed: %" PRIu32 "\n",
		stage->metrics.run_count, stage->metrics.eagain_count,
		stage->metrics.completed_counter, stage->metrics.items_processed_count);
	pr_info("\t - total execution time: %" PRIu32 "us, eagain cpu overhead time: %" PRIu32
		"us\n",
		TickToUs(stage->metrics.total_execution_time_ticks),
		TickToUs(stage->metrics.eagain_cpu_overhead_ticks));
	pr_info("\t - post complete: %" PRIu32 " requests completed: %" PRIu32
		" total post complete time: %" PRIu32 "us \n",
		stage->engine->metrics.post_complete_task_count,
		stage->engine->metrics.requests_completed_count,
		TickToUs(stage->engine->metrics.total_post_complete_time_ticks));
	pr_info("\t- cached tail %" PRIu16 " head %" PRIu16 " process %" PRIu16 " size %" PRIu32
		"\n",
		readw(stage->cached_ring.tail_idx_ptr), readw(stage->cached_ring.head_idx_ptr),
		stage->cached_ring.processed_idx, stage->cached_ring.size);
	pr_info("\t- shadow tail %" PRIxPTR " head %" PRIxPTR " process %" PRIxPTR "\n",
		noa_readptr(stage->shadow_ring.tail_addr_ptr),
		noa_readptr(stage->shadow_ring.head_addr_ptr), stage->shadow_ring.processed_addr);
	if (pool) {
		pr_info("\t- Engine pool tail %" PRIu16 " head %" PRIu16 "\n", pool->tail,
			pool->head);
		for (i = 0; i <= pool->size_mask; i++) {
			NestedRingRequest *req = &pool->arr[i];
			pr_info("\t\t - req %" PRIu16 ": completed %" PRIu16 " cache %" PRIu16
				" shadow %" PRIxPTR "\n",
				i, req->completed, req->cached_ring_idx, req->shadow_ring_addr);
		}
	}
}

void NepRingStageStatusPrint(struct ring_manager_instance *instance)
{
	NestedRingStage *stage;
	pr_info("Ring: %s, tail %" PRIu32 " head %" PRIu32 " size %" PRIu32 " item_len %" PRIu32
		"\n",
		instance->ring.name, noa_ring_tail_read_once(&instance->ring),
		noa_ring_head_read_once(&instance->ring), instance->ring.basic.size,
		instance->ring.basic.item_len);
	list_for_each_entry (stage, &instance->stage_list, list) {
		PrintStage(stage);
	}
}
