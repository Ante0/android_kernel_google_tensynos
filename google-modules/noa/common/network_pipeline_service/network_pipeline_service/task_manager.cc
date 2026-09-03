// SPDX-License-Identifier: GPL-2.0-only
/*
 * All NEP task instances.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#include "network_pipeline_service/task_manager.h"

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/printk.h>

#include "common/compiler.h"
#include "common/inttypes.h"
#include "network_pipeline_service/task_manager_internal.h"
#else /* linux */
#include <cerrno>
#include <cstdint>
#include <cinttypes>
#include <atomic>

#include "common/compiler.h"
#include "linux_port/log.h"
#include "network_pipeline_service_private/task_manager_internal.h"
#endif /* linux */

#ifdef linux
unsigned long g_group_bitmap[kMaxNepTaskGroupNum];
#else /* linux */
SEC_FAST_DATA static std::atomic<uint16_t> g_group_bitmap[kMaxNepTaskGroupNum];
#endif /* linux */

SEC_FAST_DATA static NestedRingTask *g_groups[kMaxNepTaskGroupNum];
SEC_FAST_DATA static NestedRingTask g_system_tasks[kMaxNepTaskForSystem];
SEC_FAST_DATA static NestedRingTask g_cache_data_tasks[kMaxNepTaskForCacheData];
SEC_FAST_DATA static NestedRingTask g_cache_buffer_pool_tasks[kMaxNepTaskForCacheBufferPool];
SEC_FAST_DATA static NestedRingTask g_fetch_header_tasks[kMaxNepFetchHeaderTask];
SEC_FAST_DATA static NestedRingTask g_analyze_data_tasks[kMaxNepAnalyzeDataTask];
SEC_FAST_DATA static NestedRingTask g_route_data_tasks[kMaxNepTaskForRouteData];

#ifdef linux
unsigned long *NepTaskGroupBitmapGet(void)
#else /* linux */
std::atomic<uint16_t> *NepTaskGroupBitmapGet(void)
#endif /* linux */
{
	return &g_group_bitmap[0];
}

NestedRingTask **NepTaskGroupsGet(void)
{
	return &g_groups[0];
}

NestedRingTask *NepTaskGroupGet(uint8_t group)
{
	switch (group) {
	case kNepSystemTaskGroup:
		return &g_system_tasks[0];
	case kNepCacheDataTaskGroup:
		return &g_cache_data_tasks[0];
	case kNepCacheBufferPoolTaskGroup:
		return &g_cache_buffer_pool_tasks[0];
	case kNepFetchHeaderTaskGroup:
		return &g_fetch_header_tasks[0];
	case kNepAnalyzeDataTaskGroup:
		return &g_analyze_data_tasks[0];
	case kNepRouteDataTaskGroup:
		return &g_route_data_tasks[0];
	default:
		break;
	}
	return NULL;
}

static uint8_t GetGroupMaxTaskNum(uint8_t group)
{
	switch (group) {
	case kNepSystemTaskGroup:
		return kMaxNepTaskForSystem;
	case kNepCacheDataTaskGroup:
		return kMaxNepTaskForCacheData;
	case kNepCacheBufferPoolTaskGroup:
		return kMaxNepTaskForCacheBufferPool;
	case kNepFetchHeaderTaskGroup:
		return kMaxNepFetchHeaderTask;
	case kNepAnalyzeDataTaskGroup:
		return kMaxNepAnalyzeDataTask;
	case kNepRouteDataTaskGroup:
		return kMaxNepTaskForRouteData;
	default:
		break;
	}
	return 0;
}

void NepTaskInitAll(void)
{
	uint8_t group;
	for (group = 0; group < kMaxNepTaskGroupNum; group++) {
		NestedRingTask *tasks = NepTaskGroupGet(group);
		uint8_t max_task_num = GetGroupMaxTaskNum(group);
		uint8_t id;
		g_group_bitmap[group] = 0;
		for (id = 0; id < max_task_num; id++) {
			NestedRingTask *task = &tasks[id];
			task->id = id;
			task->id_bit = (1 << id);
			task->group_bitmap = &g_group_bitmap[group];
			task->context = NULL;
			task->run = NULL;
		}
		g_groups[group] = tasks;
	}
}

NestedRingTask *NepTaskGet(uint8_t group, uint8_t id)
{
	NestedRingTask *tasks = NepTaskGroupGet(group);
	uint8_t max_task_num = GetGroupMaxTaskNum(group);
	if (!tasks || id >= max_task_num) {
		pr_err("Invalid nep task with group: %" PRIu16 ", id: %" PRIu16 "\n", group, id);
		return NULL;
	}
	return &tasks[id];
}
