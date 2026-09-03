/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thermal_sm_helper.h Helper to get address of CPM thermal shared memory sections.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_SM_HELPER_H_
#define _THERMAL_SM_HELPER_H_

#include "thermal_cpm_mbox.h"
#include "thermal_msg_helper.h"

enum thermal_sm_section {
	THERMAL_SM_CDEV_STATE = 0,
	THERMAL_SM_STATS,
	THERMAL_SM_TRIP_COUNT,
	THERMAL_SM_TJ_PRESSURE,
	THERMAL_SM_MAX_SECTION,
};

struct thermal_sm_section_data {
	u32 size;
	void __iomem *addr;
};

struct thermal_sm_tmu_state_data {
	u8 cdev;
} __packed;

struct thermal_sm_trip_counter_data {
	u64 counters[THERMAL_TMU_NR_TRIPS];
} __packed;

#define MAX_TR_THRESHOLD_COUNT 8

struct thermal_sm_stats_metrics {
	u64 max_sample_timestamp;
	u64 min_sample_timestamp;
	u64 time_in_state[MAX_TR_THRESHOLD_COUNT + 1];
	u8 bucket_count;
	u8 max_sample_temps;
	u8 min_sample_temps;
} __packed;

struct thermal_sm_stats_thresholds {
	u8 threshold_count;
	u8 thresholds[MAX_TR_THRESHOLD_COUNT];
} __packed;

struct thermal_sm_stats_data {
	struct thermal_sm_stats_metrics metrics;
	struct thermal_sm_stats_thresholds thresholds;
} __packed;

int thermal_sm_initialize_section(struct device *dev, enum thermal_sm_section section);
int thermal_sm_get_tmu_cdev_state(enum hw_dev_type cdev_id, u8 *cdev_state);
int thermal_sm_get_thermal_stats_metrics(struct thermal_sm_stats_metrics *data);
int thermal_sm_get_thermal_stats_thresholds(struct thermal_sm_stats_thresholds *data);
int thermal_sm_set_thermal_stats_thresholds(struct thermal_sm_stats_thresholds *data);
int thermal_sm_get_tmu_trip_counter(struct thermal_sm_trip_counter_data *data);
int thermal_sm_get_tj_pressure_data(u8 *data, u32 size);
#endif //_THERMAL_SM_HELPER_H_
