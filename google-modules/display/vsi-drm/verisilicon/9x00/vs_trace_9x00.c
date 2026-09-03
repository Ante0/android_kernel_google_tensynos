// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#define CREATE_TRACE_POINTS
#include "vs_trace_9x00.h"
#include <linux/trace.h>
#include <linux/trace_events.h>

#define TRACE_INSTANCE "pixel-display"

static const char * const dc_trace_events[] = {
	"disp_set_mode",
	"disp_config_hw_fb_mask",
	"disp_config_hw_fb",
	"disp_trusty_protect_ip",
	"disp_underrun_config",
	"disp_urgent_cmd_config",
	"disp_urgent_vid_config",
	"disp_dsc",
	"disp_dc_enable",
	"disp_dc_disable",
	"disp_dc_enable_irqs",
	"disp_dc_disable_irqs",
	"disp_dc_irq_status",
	"disp_dsc_status",
	"disp_dc_power_get",
	"disp_dc_power_put",

};

static const char *const dpu_trace_events[] = {
	"disp_dpu_underrun",
	"disp_dpu_line_underrun",
};

int dc_init_trace(struct device *dev)
{
	int i;
	int ret;
	struct trace_array *trace;

	trace = trace_array_get_by_name(TRACE_INSTANCE, NULL);
	if (!trace) {
		dev_warn(dev, "failed to get trace instance array in %s\n", TRACE_INSTANCE);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(dc_trace_events); i++) {
		ret = trace_array_set_clr_event(trace, NULL, dc_trace_events[i], true);
		if (ret)
			dev_warn(dev, "failed to enable trace event vs_drm/%s\n",
				 dc_trace_events[i]);
	}

	for (i = 0; i < ARRAY_SIZE(dpu_trace_events); i++) {
		ret = trace_array_set_clr_event(trace, NULL, dpu_trace_events[i], true);
		if (ret)
			dev_warn(dev, "failed to enable trace event dpu/%s\n", dpu_trace_events[i]);
	}

	trace_array_put(trace);

	return 0;
}
