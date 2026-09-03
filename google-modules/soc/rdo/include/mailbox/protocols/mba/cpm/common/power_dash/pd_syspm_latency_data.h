/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC
 */

#ifndef PD_SYSPM_LATENCY_DATA_H
#define PD_SYSPM_LATENCY_DATA_H

#include <linux/types.h>

/**
 * File should mirror lib/power_mon/include/lib/power_mon/power_dash/pd_latency.h
 * from the source firmware repository.
 */

union latency_payload_t {
	struct {
		u32 latency: 16;
		u32 resource_already_on: 1;
		u32 reserved: 15;
	};
	u32 data;
};

#endif /* PD_SYSPM_LATENCY_DATA_H */
