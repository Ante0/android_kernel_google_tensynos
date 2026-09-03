/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */

#ifndef __THERMAL_GENL_H__
#define __THERMAL_GENL_H__

#include <linux/thermal.h>

void vh_enable_thermal_genl_check(void *data, int event, int tz_id, int *enable_thermal_genl);
void vh_thermal_pm_notify_suspend(void *data, struct thermal_zone_device *tz, int *irq_wakeable);

#endif /* __THERMAL_GENL_H__ */
