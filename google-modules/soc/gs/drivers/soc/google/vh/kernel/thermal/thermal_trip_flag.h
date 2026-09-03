/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2026 Google LLC
 */

#ifndef __THERMAL_TRIP_FLAG_H__
#define __THERMAL_TRIP_FLAG_H__

#include <linux/thermal.h>

void vh_update_thermal_trip_flag(void *data, struct thermal_trip *trip);

#endif /* __THERMAL_TRIP_FLAG_H__ */
