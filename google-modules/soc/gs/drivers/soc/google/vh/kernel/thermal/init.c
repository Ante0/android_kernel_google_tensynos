// SPDX-License-Identifier: GPL-2.0-only
/* init.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2021 Google LLC
 */

#include <linux/module.h>
#include <trace/hooks/thermal.h>

#include "thermal_genl.h"
#include "thermal_trip_flag.h"

static int vh_thermal_init(void)
{
	int ret = 0;
	ret = register_trace_android_vh_enable_thermal_genl_check(
						vh_enable_thermal_genl_check, NULL);
	ret = register_trace_android_vh_update_thermal_trip_flag(
						vh_update_thermal_trip_flag, NULL);
	ret = register_trace_android_vh_thermal_pm_notify_suspend(
						vh_thermal_pm_notify_suspend, NULL);

	return ret;
}

module_init(vh_thermal_init);
MODULE_LICENSE("GPL v2");
