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
#include "vs_trace.h"
#include <linux/trace.h>
#include <linux/trace_events.h>

#define TRACE_INSTANCE "pixel-display"

static const char *const vs_trace_events[] = {
	"dc_write",
	/*"dc_read",*/
	/*"dc_write_relaxed",*/
	/*"dc_read_relaxed",*/
	/*"dc_write_u32_blob",*/
	/*"dc_write_relaxed_u32_blob",*/
	"disp_frame_start_timeout",
	"disp_frame_start_missing",
	"disp_frame_start",
	"disp_frame_done",
	"disp_frame_done_missing",
	"disp_frame_done_timeout",
	"disp_be_intr_enabled",
	"disp_be_intr_disabled",
	"disp_trigger_recovery",
	"disp_enable",
	"disp_disable",
	"disp_commit_begin",
	"disp_commit_done",
	"disp_commit_skip",
	"disp_commit_wait_begin",
	"disp_commit_wait_done",
	"disp_frame_irq_enable",
	"disp_frame_irqs",
	"disp_frame_irqs_overflow",
	"disp_reset_irqs",
	"disp_err_irqs",
	"disp_bus_err_irqs",
	"disp_qos_set_rd_bw",
	"disp_qos_set_wr_bw",
	"disp_qos_set_core_clk",
	"disp_qos_boost_fab_clk",
	"disp_sof",
	"disp_update_layer_feature",
	"disp_config_layer_feature",
	"disp_update_out_ctrl_feature",
	"disp_config_out_ctrl_feature",
	"disp_output_mux_sel",
	"disp_update_wb_feature",
	"disp_config_wb_feature",
	/*"disp_hw_feature_data",*/
	"disp_update_feature_en_dirty",
	"disp_config_feature_en_dirty",
	"disp_update_feature_en",
	"disp_config_feature_en",
	"disp_component_reg_switch",
	"disp_plane",
	"disp_set_property",
	"sram_allocation",
	"sram_free",
	"sram_alloc_failure",
	"sram_free_failure",
};

static const char *const dpu_trace_events[] = {
	"disp_vblank_irq_enable",
};

int vs_init_trace(struct device *dev)
{
	int i;
	int ret;
	struct trace_array *trace;

	trace = trace_array_get_by_name(TRACE_INSTANCE, NULL);
	if (!trace) {
		dev_warn(dev, "failed to get trace instance array in %s\n", TRACE_INSTANCE);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(vs_trace_events); i++) {
		ret = trace_array_set_clr_event(trace, NULL, vs_trace_events[i], true);
		if (ret)
			dev_warn(dev, "failed to enable trace event vs_drm/%s\n",
				 vs_trace_events[i]);
	}

	for (i = 0; i < ARRAY_SIZE(dpu_trace_events); i++) {
		ret = trace_array_set_clr_event(trace, NULL, dpu_trace_events[i], true);
		if (ret)
			dev_warn(dev, "failed to enable trace event dpu/%s\n", dpu_trace_events[i]);
	}

	trace_array_put(trace);

	return 0;
}
