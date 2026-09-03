// SPDX-License-Identifier: GPL-2.0-only
/*
 *  CPU utilization monitor library for NEP.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#endif

#include "util/memory_pool.h"
#include "util/utilization_monitor.h"

#define RECORD_SIZE sizeof(struct utilization_record)

int utilization_monitor_init(struct utilization_monitor *util_mon, uint32_t max_record,
			     uint32_t wake_up_duration_ms)
{
	if (memory_pool_init(&util_mon->record_pool, max_record * RECORD_SIZE, RECORD_SIZE, NULL)
	    != NEP_MEMORY_POOL_ERR_NONE) {
		return UTILIZATION_MONITOR_ERR_NO_RESOURCE;
	}

	util_mon->front = NULL;
	util_mon->rear = NULL;
	util_mon->wake_up_duration_ms = wake_up_duration_ms;
	util_mon->max_record = max_record;
	util_mon->record_cnt = 0;

	return UTILIZATION_MONITOR_ERR_NONE;
}

int utilization_monitor_destroy(struct utilization_monitor *util_mon)
{
	memory_pool_destroy(&util_mon->record_pool);
	return UTILIZATION_MONITOR_ERR_NONE;
}

int utilization_monitor_add(struct utilization_monitor *util_mon, uint64_t arrive_time_ms)
{
	struct utilization_record *record;

	if (util_mon->rear != NULL && arrive_time_ms < util_mon->rear->end_time_ms) {
		return UTILIZATION_MONITOR_ERR_INVALID_PARAMETER;
	}

	if (util_mon->rear == NULL ||
	    arrive_time_ms - util_mon->rear->end_time_ms > util_mon->wake_up_duration_ms) {
		if (util_mon->record_cnt == util_mon->max_record) {
			record = util_mon->front;
			util_mon->front = record->next;
			util_mon->record_cnt--;
		} else {
			if (memory_pool_allocate(&util_mon->record_pool, (void **)&record) !=
			    NEP_MEMORY_POOL_ERR_NONE) {
				return UTILIZATION_MONITOR_ERR_NO_RESOURCE;
			}
		}
		record->start_time_ms = arrive_time_ms;
		record->end_time_ms = arrive_time_ms;
		record->packet_cnt = 1;
		record->next = NULL;
		if (util_mon->rear == NULL) {
			util_mon->front = record;
		} else {
			util_mon->rear->next = record;
		}
		util_mon->rear = record;
		util_mon->record_cnt++;
	} else {
		util_mon->rear->end_time_ms = arrive_time_ms;
		util_mon->rear->packet_cnt++;
	}

	return UTILIZATION_MONITOR_ERR_NONE;
}

int utilization_monitor_calculate(struct utilization_monitor *util_mon)
{
	uint64_t total_wake_up_time_ms = 0;
	uint64_t duration_ms;
	struct utilization_record *record = util_mon->front;

	while (record != NULL) {
		duration_ms = record->end_time_ms + util_mon->wake_up_duration_ms
				- record->start_time_ms;
		total_wake_up_time_ms += duration_ms;
		record = record->next;
	}
	if (total_wake_up_time_ms == 0) {
		return 0;
	}

	return total_wake_up_time_ms * 100 / (util_mon->rear->end_time_ms
					      + util_mon->wake_up_duration_ms
					      - util_mon->front->start_time_ms);
}

int utilization_monitor_clear(struct utilization_monitor *util_mon)
{
	struct utilization_record *record = util_mon->front;
	struct utilization_record *next_record;
	while (record != NULL) {
		next_record = record->next;
		memory_pool_free(&util_mon->record_pool, (void *)record);
		record = next_record;
	}
	util_mon->front = NULL;
	util_mon->rear = NULL;
	util_mon->record_cnt = 0;
	return UTILIZATION_MONITOR_ERR_NONE;
}
