// SPDX-License-Identifier: GPL-2.0-only
/* thermal_genl.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2021 Google LLC
 */

#include <linux/kernel.h>
#include <linux/thermal.h>
#include <soc/google/thermal.h>
#include <thermal_core.h>

#include "thermal_genl.h"

#define NUM_EVENT 2
static const int thermal_genl_enable[NUM_EVENT] = {
	THERMAL_GENL_EVENT_TZ_TRIP_UP,
	THERMAL_GENL_EVENT_TZ_TRIP_DOWN
};

#define MAX_NUM_TZ 100
static int tz_ignore_genl_array[MAX_NUM_TZ];
static int tz_irq_wakeable_array[MAX_NUM_TZ];

void register_tz_id_ignore_genl(int tz_id)
{
	if (tz_id >= MAX_NUM_TZ) {
		pr_err("tz_id: %d is over the limit!\n", tz_id);
		return;
	}

	pr_info("Disable tz_id: %d thermal genl event\n", tz_id);
	tz_ignore_genl_array[tz_id] = 1;
}
EXPORT_SYMBOL_GPL(register_tz_id_ignore_genl);

void register_tz_id_irq_wakeable(int tz_id)
{
	if (tz_id >= MAX_NUM_TZ) {
		pr_err("tz_id: %d is over the limit!\n", tz_id);
		return;
	}

	pr_info("Enable tz_id: %d irq_wakeable\n", tz_id);
	tz_irq_wakeable_array[tz_id] = 1;
}
EXPORT_SYMBOL_GPL(register_tz_id_irq_wakeable);

void vh_enable_thermal_genl_check(void *data, int event, int tz_id, int *enable_thermal_genl)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(thermal_genl_enable); i++) {
		if (thermal_genl_enable[i] == event &&
		    tz_id < MAX_NUM_TZ && !tz_ignore_genl_array[tz_id]) {
			*enable_thermal_genl = 1;
			return;
		}
	}
	*enable_thermal_genl = 0;
}

void vh_thermal_pm_notify_suspend(void *data, struct thermal_zone_device *tz, int *irq_wakeable)
{
	if (!tz || tz->id >= MAX_NUM_TZ || !irq_wakeable)
		return;

	if (tz_irq_wakeable_array[tz->id])
		*irq_wakeable = 1;
	else
		*irq_wakeable = 0;
}
