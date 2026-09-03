/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  CPU utilization monitor library for NEP.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifndef NEP_UTILIZATION_MONITOR_H_
#define NEP_UTILIZATION_MONITOR_H_

#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>
#endif

#include "memory_pool.h"

enum {
	UTILIZATION_MONITOR_ERR_NONE = 0,
	UTILIZATION_MONITOR_ERR_INVALID_PARAMETER,
	UTILIZATION_MONITOR_ERR_NO_RESOURCE,
	UTILIZATION_MONITOR_ERR_MAX
};

/* Utilization record */
struct utilization_record {
	uint64_t start_time_ms;
	uint64_t end_time_ms;
	uint64_t packet_cnt;
	struct utilization_record *next;
};

/* CPU utilization monitor */
struct utilization_monitor {
	struct memory_pool record_pool;
	struct utilization_record *front;
	struct utilization_record *rear;
	uint32_t wake_up_duration_ms;
	uint32_t max_record;
	uint32_t record_cnt;
};

int utilization_monitor_init(struct utilization_monitor *util_mon, uint32_t max_record,
			     uint32_t wake_up_duration_ms);
int utilization_monitor_destroy(struct utilization_monitor *util_mon);
int utilization_monitor_add(struct utilization_monitor *util_mon, uint64_t arrive_time_ms);
int utilization_monitor_calculate(struct utilization_monitor *util_mon);
int utilization_monitor_clear(struct utilization_monitor *util_mon);

#endif  // NEP_UTILIZATION_MONITOR_H_
