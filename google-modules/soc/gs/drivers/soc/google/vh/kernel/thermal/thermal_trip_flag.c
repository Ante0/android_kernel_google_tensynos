// SPDX-License-Identifier: GPL-2.0-only
/* thermal_trip_flag.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2026 Google LLC
 */

#include <linux/kernel.h>

#include <soc/google/thermal.h>

#include "thermal_trip_flag.h"

void vh_update_thermal_trip_flag(void *data, struct thermal_trip *trip)
{
	trip->flags = THERMAL_TRIP_FLAG_RW;
}
