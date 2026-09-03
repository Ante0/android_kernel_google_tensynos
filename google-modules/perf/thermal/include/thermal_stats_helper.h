/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thermal_stats_helper.h Helper to register and update thermal metrics
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */


#ifndef _THERMAL_STATS_HELPER_H_
#define _THERMAL_STATS_HELPER_H_

#include <linux/platform_device.h>
#include <linux/thermal.h>

#include "soc/google/google_gtc.h"
#include "soc/google/thermal_metrics.h"
#include "thermal_cpm_mbox.h"

struct thermal_stats_time_sync {
	u64 ktime_real_second_base;
	u64 ticks_base;
};

struct thermal_stats_driver_data {
	struct thermal_stats_time_sync ktime_sync;
	const char *group_name;
};

struct thermal_stats_tz_data {
	struct list_head node;
	tr_handle tr_handle;
	struct thermal_stats_time_sync *ktime_sync;
	enum hw_thermal_zone_id tz_id;
};

int thermal_stats_parse_dt(struct platform_device *pdev,
			   struct thermal_stats_driver_data *drv_data);
int thermal_stats_init(struct platform_device *pdev, struct thermal_stats_driver_data *drv_data);
void thermal_stats_cleanup(struct platform_device *pdev);
int thermal_stats_register_thermal_metrics(struct platform_device *pdev,
					   struct thermal_stats_driver_data *drv_data,
					   enum hw_thermal_zone_id tz_id);

struct thermal_stats_tz_data *thermal_stats_get_data_by_tr_handle(tr_handle tr_handle);
int thermal_stats_set_tr_thresholds(tr_handle instance, const int *thresholds, int num_thresholds);
int thermal_stats_get_tr_thresholds(tr_handle instance, int *thresholds, int *num_thresholds);
time64_t __thermal_stats_ticks_to_real_seconds(struct thermal_stats_time_sync *ktime_sync,
					       u64 global_tick);
int thermal_stats_get_tr_stats(tr_handle instance, atomic64_t *stats, struct tr_sample *max,
			       struct tr_sample *min);
int thermal_stats_reset_stats(tr_handle instance);

#endif // _THERMAL_STATS_HELPER_H_
