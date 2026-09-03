// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Google LLC
 */

#include <linux/moduleparam.h>

#include "vs_module_params.h"

static u32 recovery_sources = BIT(SSCD_SRC_MANUAL) |
			      BIT(SSCD_SRC_FRAME_UPDATE_TIMEOUT);
module_param(recovery_sources, uint, 0644);
MODULE_PARM_DESC(recovery_sources,
		 "Bitmask of which sources are allowed to trigger panel recovery");

static u32 coredump_sources = BIT(SSCD_SRC_MANUAL) |
			      BIT(SSCD_SRC_FRAME_UPDATE_TIMEOUT) |
			      BIT(SSCD_SRC_GRAM_COLLISION);
module_param(coredump_sources, uint, 0644);
MODULE_PARM_DESC(coredump_sources,
		 "Bitmask of which coredump sources are allowed to trigger subsystem coredump");

static bool disable_crtc_recovery;
module_param(disable_crtc_recovery, bool, 0644);
MODULE_PARM_DESC(disable_crtc_recovery, "Disable crtc recovery on flip done timeout");

static bool disable_coredump;
module_param(disable_coredump, bool, 0644);
MODULE_PARM_DESC(disable_coredump, "Whether to disable subsystem coredump for this module");

static bool disable_urgent;
module_param(disable_urgent, bool, 0644);
MODULE_PARM_DESC(disable_urgent, "Disable QoS urgent level feature");

bool recovery_source_enabled(enum coredump_source source)
{
	return (recovery_sources & BIT(source)) != 0;
}

bool coredump_source_enabled(enum coredump_source source)
{
	return (coredump_sources & BIT(source)) != 0;
}

bool is_crtc_recovery_enabled(void)
{
	return !disable_crtc_recovery;
}

bool is_coredump_enabled(void)
{
	return !disable_coredump;
}

bool is_urgent_enabled(void)
{
	return !disable_urgent;
}

