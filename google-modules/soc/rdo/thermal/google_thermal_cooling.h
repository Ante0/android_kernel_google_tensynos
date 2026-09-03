/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_THERMAL_COOLING_H
#define _GOOGLE_THERMAL_COOLING_H

#include "google_thermal.h"

int google_thermal_cooling_init(struct thermal_data *data);
struct thermal_cooling_device *tz2poweractor(struct thermal_zone_device *tz);

#endif /* _GOOGLE_THERMAL_COOLING_H */

