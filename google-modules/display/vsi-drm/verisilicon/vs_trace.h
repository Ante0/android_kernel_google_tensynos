/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM vs_drm

#if !defined(_VS_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _VS_TRACE_H_

#include <linux/tracepoint.h>
#include <drm/drm_crtc.h>
#include <vs_crtc.h>

/**
 * vs_init_trace() - Initializes the display driver's trace events.
 * @dev: Pointer to the device structure.
 *
 * This function retrieves the trace array for the display driver and enables
 * all the trace events defined in the vs_trace_events array.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int vs_init_trace(struct device *dev);

DECLARE_EVENT_CLASS(dc_readwrite,
	TP_PROTO(unsigned long reg, unsigned long value),
	TP_ARGS(reg, value),
	TP_STRUCT__entry(
		__field(unsigned long, reg)
		__field(unsigned long, value)
	),
	TP_fast_assign(
		__entry->reg = reg;
		__entry->value = value;
	),
	TP_printk("%#08lx = %#08lx", __entry->reg, __entry->value)
);

DEFINE_EVENT(dc_readwrite, dc_read,
	TP_PROTO(unsigned long reg, unsigned long value),
	TP_ARGS(reg, value)
);

DEFINE_EVENT(dc_readwrite, dc_write,
	TP_PROTO(unsigned long reg, unsigned long value),
	TP_ARGS(reg, value)
);

DEFINE_EVENT(dc_readwrite, dc_read_relaxed,
	TP_PROTO(unsigned long reg, unsigned long value),
	TP_ARGS(reg, value)
);

DEFINE_EVENT(dc_readwrite, dc_write_relaxed,
	TP_PROTO(unsigned long reg, unsigned long value),
	TP_ARGS(reg, value)
);

DEFINE_EVENT(dc_readwrite, dc_write_u32_blob,
	TP_PROTO(unsigned long reg, unsigned long value),
	TP_ARGS(reg, value)
);

DEFINE_EVENT(dc_readwrite, dc_write_relaxed_u32_blob,
	TP_PROTO(unsigned long reg, unsigned long value),
	TP_ARGS(reg, value)
);

DECLARE_EVENT_CLASS(display,
	TP_PROTO(int display_id, u32 output_id, struct vs_crtc *vs_crtc),
	TP_ARGS(display_id, output_id, vs_crtc),
	TP_STRUCT__entry(
		__field(int, display_id)
		__field(u32, output_id)
		__field(int, frames_pending)
		__field(int, te_count)
	),
	TP_fast_assign(
		__entry->display_id = display_id;
		__entry->output_id = output_id;
		__entry->frames_pending = atomic_read(&vs_crtc->frames_pending);
		__entry->te_count = atomic_read(&vs_crtc->te_count);
	),
	TP_printk("display_id: %d output_id: %u frames_pending: %d te_count: %d",
		__entry->display_id,  __entry->output_id, __entry->frames_pending,
		__entry->te_count)
);

DEFINE_EVENT(display, disp_frame_start,
	TP_PROTO(int display_id, u32 output_id, struct vs_crtc *vs_crtc),
	TP_ARGS(display_id, output_id, vs_crtc)
);
DEFINE_EVENT(display, disp_frame_done,
	TP_PROTO(int display_id, u32 output_id, struct vs_crtc *vs_crtc),
	TP_ARGS(display_id, output_id, vs_crtc)
);
DEFINE_EVENT(display, disp_commit_done,
	TP_PROTO(int display_id, u32 output_id, struct vs_crtc *vs_crtc),
	TP_ARGS(display_id, output_id, vs_crtc)
);

DECLARE_EVENT_CLASS(disp_be_intr,
	TP_PROTO(int display_id, u32 output_id, u32 value),
	TP_ARGS(display_id, output_id, value),
	TP_STRUCT__entry(
		__field(int, display_id)
		__field(u32, output_id)
		__field(u32, value)
	),
	TP_fast_assign(
		__entry->display_id = display_id;
		__entry->output_id = output_id;
		__entry->value = value;
	),
	TP_printk("display_id: %d output_id: %u enabled:%#x",
		__entry->display_id, __entry->output_id, __entry->value)
);
DEFINE_EVENT(disp_be_intr, disp_be_intr_enabled,
	TP_PROTO(int display_id, u32 output_id, u32 value),
	TP_ARGS(display_id, output_id, value)
);
DEFINE_EVENT(disp_be_intr, disp_be_intr_disabled,
	TP_PROTO(int display_id, u32 output_id, u32 value),
	TP_ARGS(display_id, output_id, value)
);

TRACE_EVENT(disp_trigger_recovery,
	TP_PROTO(int display_id, struct vs_crtc *vs_crtc),
	TP_ARGS(display_id, vs_crtc),
	TP_STRUCT__entry(
		__field(int, display_id)
		__field(int, recovering)
		__field(int, count)
		__field(int, te_count)
		__field(bool, crtc_event)
	),
	TP_fast_assign(
		__entry->display_id = display_id;
		__entry->recovering = atomic_read(&vs_crtc->recovery.recovering);
		__entry->count = vs_crtc->recovery.count;
		__entry->te_count = atomic_read(&vs_crtc->te_count);
		__entry->crtc_event = !!vs_crtc->event;
	),
	TP_printk("display_id: %d recovering: %d count:%d te_count: %d crtc_event: %s",
		__entry->display_id, __entry->recovering, __entry->count, __entry->te_count,
		__entry->crtc_event ? "yes" : "no")
);

DECLARE_EVENT_CLASS(display_crtc,
	TP_PROTO(int display_id, struct drm_crtc_state *crtc_state),
	TP_ARGS(display_id, crtc_state),
	TP_STRUCT__entry(
		__field(int, display_id)
		__field(bool, enable)
		__field(bool, active)
		__field(bool, self_refresh_active)
		__field(bool, mode_changed)
		__field(bool, connectors_changed)
		__field(bool, active_changed)
		__field(int, power_off_mode)
		__field(int, power_state)

	),
	TP_fast_assign(
		__entry->display_id = display_id;
		__entry->enable = crtc_state->enable;
		__entry->active = crtc_state->active;
		__entry->self_refresh_active = crtc_state->self_refresh_active;
		__entry->active_changed = crtc_state->active_changed;
		__entry->connectors_changed = crtc_state->connectors_changed;
		__entry->mode_changed = crtc_state->mode_changed;
		__entry->power_off_mode = to_vs_crtc_state(crtc_state)->power_off_mode;
		__entry->power_state = to_vs_crtc_state(crtc_state)->power_state;
	),
	TP_printk("id:%d enable:%d active:%d sr_active:%d power_off_mode:%d [a:%d c:%d m:%d] power_state:%d",
		__entry->display_id, __entry->enable, __entry->active,
		__entry->self_refresh_active, __entry->power_off_mode, __entry->active_changed,
		__entry->connectors_changed, __entry->mode_changed, __entry->power_state)
);

DEFINE_EVENT(display_crtc, disp_enable,
	TP_PROTO(int display_id, struct drm_crtc_state *crtc_state),
	TP_ARGS(display_id, crtc_state)
);
DEFINE_EVENT(display_crtc, disp_disable,
	TP_PROTO(int display_id, struct drm_crtc_state *crtc_state),
	TP_ARGS(display_id, crtc_state)
);

TRACE_EVENT(
	disp_commit_begin, TP_PROTO(int display_id, struct drm_crtc_state *crtc_state),
	TP_ARGS(display_id, crtc_state),
	TP_STRUCT__entry(
		__field(int, display_id)
		__field(u32, plane_mask)
		__field(bool, planes_changed)
		__field(bool, planes_updated)
		__field(bool, color_mgmt_changed)
		__field(bool, mode_changed)
		__field(bool, active_changed)
		__field(bool, connectors_changed)
		__field(bool, wb_connectors_updated)
		__field(bool, no_vblank)),
	TP_fast_assign(
		__entry->display_id = display_id; __entry->plane_mask = crtc_state->plane_mask;
		__entry->planes_changed = crtc_state->planes_changed;
		__entry->planes_updated = to_vs_crtc_state(crtc_state)->planes_updated;
		__entry->color_mgmt_changed = crtc_state->color_mgmt_changed;
		__entry->mode_changed = crtc_state->mode_changed;
		__entry->active_changed = crtc_state->active_changed;
		__entry->connectors_changed = crtc_state->connectors_changed;
		__entry->wb_connectors_updated = to_vs_crtc_state(crtc_state)->wb_connectors_updated;
		__entry->no_vblank = crtc_state->no_vblank;),
	TP_printk(
		"id:%d no_vblank:%d plane_mask:%#x [plane_chg:%d plane_upd:%d color_chg:%d mode_chg:%d active_chg:%d conn_chg:%d wb_conn_upd:%d]",
		__entry->display_id, __entry->no_vblank, __entry->plane_mask,
		__entry->planes_changed, __entry->planes_updated, __entry->color_mgmt_changed,
		__entry->mode_changed, __entry->active_changed, __entry->connectors_changed,
		__entry->wb_connectors_updated));

TRACE_EVENT(disp_commit_skip,
	TP_PROTO(int display_id, const struct drm_crtc_state *crtc_state),
	TP_ARGS(display_id, crtc_state),
	TP_STRUCT__entry(
		__field(int, display_id)
		__field(u32, plane_mask)
		__field(bool, active_changed)
		__field(bool, no_vblank)
	),
	TP_fast_assign(
		__entry->display_id = display_id;
		__entry->plane_mask = crtc_state->plane_mask;
		__entry->active_changed = crtc_state->active_changed;
		__entry->no_vblank = crtc_state->no_vblank;
	),
	TP_printk("id:%d no_vblank:%d plane_mask:%#x [active_chg:%d]",
		  __entry->display_id, __entry->no_vblank,
		  __entry->plane_mask, __entry->active_changed)
);

DECLARE_EVENT_CLASS(display_enable_irqs,
	TP_PROTO(int output_id, int enable),
	TP_ARGS(output_id, enable),
	TP_STRUCT__entry(
		__field(int, output_id)
		__field(int, enable)
	),
	TP_fast_assign(
		__entry->output_id = output_id;
		__entry->enable = enable;
	),
	TP_printk("output_id: %d %s", __entry->output_id, __entry->enable ? "enable" : "disable")
);

DEFINE_EVENT(display_enable_irqs, disp_frame_irq_enable,
	TP_PROTO(int output_id, int enable),
	TP_ARGS(output_id, enable)
);

DECLARE_EVENT_CLASS(disp_frame_irqs_class,
	TP_PROTO(u8 te_rising, u8 te_falling, u8 frame_start, u16 layer_done, u8 frame_done,
					 u8 wb_done),
	TP_ARGS(te_rising, te_falling, frame_start, layer_done, frame_done, wb_done),
	TP_STRUCT__entry(
		__field(u8, te_rising)
		__field(u8, te_falling)
		__field(u8, frame_start)
		__field(u16, layer_done)
		__field(u8, frame_done)
		__field(u8, wb_done)
	),
	TP_fast_assign(
		__entry->te_rising = te_rising;
		__entry->te_falling = te_falling;
		__entry->frame_start = frame_start;
		__entry->layer_done = layer_done;
		__entry->frame_done = frame_done;
		__entry->wb_done = wb_done;
	),
	TP_printk("te_r: %#x te_f: %#x frame_start: %#x layer_done: %#x frame_done: %#x wb_done: %#x",
		__entry->te_rising, __entry->te_falling, __entry->frame_start, __entry->layer_done,
		__entry->frame_done, __entry->wb_done)
);
DEFINE_EVENT_CONDITION(disp_frame_irqs_class, disp_frame_irqs,
	TP_PROTO(u8 te_rising, u8 te_falling, u8 frame_start, u16 layer_done, u8 frame_done,
					 u8 wb_done),
	TP_ARGS(te_rising, te_falling, frame_start, layer_done, frame_done, wb_done),
	TP_CONDITION(te_rising || te_falling || frame_start || layer_done || frame_done || wb_done)
);
DEFINE_EVENT_CONDITION(disp_frame_irqs_class, disp_frame_irqs_overflow,
	TP_PROTO(u8 te_rising, u8 te_falling, u8 frame_start, u16 layer_done, u8 frame_done,
					 u8 wb_done),
	TP_ARGS(te_rising, te_falling, frame_start, layer_done, frame_done, wb_done),
	TP_CONDITION(te_rising || te_falling || frame_start || layer_done || frame_done || wb_done)
);

TRACE_EVENT_CONDITION(disp_reset_irqs,
	TP_PROTO(u16 layer_rst_done, bool fe0_rst_done, bool fe1_rst_done, bool be_rst_done),
	TP_ARGS(layer_rst_done, fe0_rst_done, fe1_rst_done, be_rst_done),
	TP_CONDITION(layer_rst_done || fe0_rst_done || fe1_rst_done || be_rst_done),
	TP_STRUCT__entry(
		__field(u16, layer_rst_done)
		__field(bool, fe0_rst_done)
		__field(bool, fe1_rst_done)
		__field(bool, be_rst_done)
	),
	TP_fast_assign(
		__entry->layer_rst_done = layer_rst_done;
		__entry->fe0_rst_done = fe0_rst_done;
		__entry->fe1_rst_done = fe1_rst_done;
		__entry->be_rst_done = be_rst_done;
	),
	TP_printk("layer_rst_done: %#x fe0_rst_done: %d fe1_rst_done: %d be_reset_done: %d",
		__entry->layer_rst_done, __entry->fe0_rst_done, __entry->fe1_rst_done,
		__entry->be_rst_done)
);

DECLARE_EVENT_CLASS(disp_qos_set_bw,
	TP_PROTO(int display_id, u32 avg_bw_mbps, u32 peak_bw_mbps, u32 rt_bw_mbps),
	TP_ARGS(display_id, avg_bw_mbps, peak_bw_mbps, rt_bw_mbps),
	TP_STRUCT__entry(
		__field(int, display_id)
		__field(u32, avg_bw_mbps)
		__field(u32, peak_bw_mbps)
		__field(u32, rt_bw_mbps)
	),
	TP_fast_assign(
		__entry->display_id = display_id;
		__entry->avg_bw_mbps = avg_bw_mbps;
		__entry->peak_bw_mbps = peak_bw_mbps;
		__entry->rt_bw_mbps = rt_bw_mbps;
	),
	TP_printk("display_id: %d avg_bw_mbps: %u peak_bw_mbps: %u rt_bw_mbps: %u",
		  __entry->display_id, __entry->avg_bw_mbps, __entry->peak_bw_mbps,
		  __entry->rt_bw_mbps)
);
DEFINE_EVENT(disp_qos_set_bw, disp_qos_set_rd_bw,
	TP_PROTO(int display_id, u32 avg_bw_mbps, u32 peak_bw_mbps, u32 rt_bw_mbps),
	TP_ARGS(display_id, avg_bw_mbps, peak_bw_mbps, rt_bw_mbps)
);
DEFINE_EVENT(disp_qos_set_bw, disp_qos_set_wr_bw,
	TP_PROTO(int display_id, u32 avg_bw_mbps, u32 peak_bw_mbps, u32 rt_bw_mbps),
	TP_ARGS(display_id, avg_bw_mbps, peak_bw_mbps, rt_bw_mbps)
);

TRACE_EVENT(disp_qos_set_core_clk,
	TP_PROTO(int display_id, u32 core_clk_khz),
	TP_ARGS(display_id, core_clk_khz),
	TP_STRUCT__entry(
		__field(int, display_id)
		__field(u32, core_clk_khz)
	),
	TP_fast_assign(
		__entry->display_id = display_id;
		__entry->core_clk_khz = core_clk_khz;
	),
	TP_printk("display_id: %d core_clk_khz: %u", __entry->display_id, __entry->core_clk_khz)
);

TRACE_EVENT(disp_qos_boost_fab_clk,
	TP_PROTO(int display_id, u32 fab_khz, u32 core_khz),
	TP_ARGS(display_id, fab_khz, core_khz),
	TP_STRUCT__entry(
		__field(int, display_id)
		__field(u32, fab_khz)
		__field(u32, core_khz)
	),
	TP_fast_assign(
		__entry->display_id = display_id;
		__entry->fab_khz = fab_khz;
		__entry->core_khz = core_khz;
	),
	TP_printk("display_id: %d fab_khz: %u core_khz: %u",
		__entry->display_id, __entry->fab_khz, __entry->core_khz));

DECLARE_EVENT_CLASS(disp_commit_wait,
	TP_PROTO(int display_id, u32 swapped_plane_mask, u32 enabling_secure_mask,
		 int frames_pending),
	TP_ARGS(display_id, swapped_plane_mask, enabling_secure_mask, frames_pending),
	TP_STRUCT__entry(
		__field(int, display_id)
		__field(u32, swapped_plane_mask)
		__field(u32, enabling_secure_mask)
		__field(int, frames_pending)
	),
	TP_fast_assign(
		__entry->display_id = display_id;
		__entry->swapped_plane_mask = swapped_plane_mask;
		__entry->enabling_secure_mask = enabling_secure_mask;
		__entry->frames_pending = frames_pending;
	),
	TP_printk("display_id: %d swapped_plane_mask: %#x enabling_secure_mask: %#x frames_pending: %d",
		__entry->display_id, __entry->swapped_plane_mask,
		__entry->enabling_secure_mask, __entry->frames_pending)
);

DEFINE_EVENT(disp_commit_wait, disp_commit_wait_begin,
	TP_PROTO(int display_id, u32 swapped_plane_mask, u32 enabling_secure_mask,
		 int frames_pending),
	TP_ARGS(display_id, swapped_plane_mask, enabling_secure_mask, frames_pending)
);

DEFINE_EVENT(disp_commit_wait, disp_commit_wait_done,
	TP_PROTO(int display_id, u32 swapped_plane_mask, u32 enabling_secure_mask,
		 int frames_pending),
	TP_ARGS(display_id, swapped_plane_mask, enabling_secure_mask, frames_pending)
);

/* Specific HW Programming Traces */

TRACE_EVENT(disp_sof,
	TP_PROTO(u32 hw_id, u32 output_id, bool en),
	TP_ARGS(hw_id, output_id, en),
	TP_STRUCT__entry(
			__field(u32, hw_id)
			__field(u32, output_id)
			__field(bool, en)
	),
	TP_fast_assign(
			__entry->hw_id = hw_id;
			__entry->output_id = output_id;
			__entry->en = en;
	),
	TP_printk("[Out_ctrl%d] SOF: output_id:%d en:%d",
		  __entry->hw_id, __entry->output_id, __entry->en)
);

/*
 * Event to monitor the selection and routing of output multiplexers,
 * tracking the mapping between Out_ctrl and output_id.
 */
TRACE_EVENT(disp_output_mux_sel,
	TP_PROTO(u32 hw_id, u32 output_id, u32 mux_sel),
	TP_ARGS(hw_id, output_id, mux_sel),
	TP_STRUCT__entry(
			__field(u32, hw_id)
			__field(u32, output_id)
			__field(u32, mux_sel)
	),
	TP_fast_assign(
			__entry->hw_id = hw_id;
			__entry->output_id = output_id;
			__entry->mux_sel = mux_sel;
	),
	TP_printk("[Out_ctrl%d] output_id:%d sel:%#x",
		  __entry->hw_id, __entry->output_id, __entry->mux_sel)
);

/* General-Purpose HW Programming Traces */

DECLARE_EVENT_CLASS(disp_hw_layer_feature,
	TP_PROTO(const char *prop, u32 hw_id, const char *fmt, va_list *va),
	TP_ARGS(prop, hw_id, fmt, va),
	TP_STRUCT__entry(
			__field(u32, hw_id)
			__string(prop, prop)
			__vstring(vstr, fmt, va)
	),
	TP_fast_assign(
			__entry->hw_id = hw_id;
			__assign_str(prop);
			__assign_vstr(vstr, fmt, va);
	),
	TP_printk("[Layer%d] %s: %s", __entry->hw_id, __get_str(prop),
		  __get_str(vstr))
);

DEFINE_EVENT(disp_hw_layer_feature, disp_update_layer_feature,
	TP_PROTO(const char *prop, u32 hw_id, const char *fmt, va_list *va),
	TP_ARGS(prop, hw_id, fmt, va)
);
DEFINE_EVENT(disp_hw_layer_feature, disp_config_layer_feature,
	TP_PROTO(const char *prop, u32 hw_id, const char *fmt, va_list *va),
	TP_ARGS(prop, hw_id, fmt, va)
);

DECLARE_EVENT_CLASS(disp_hw_out_ctrl_feature,
	TP_PROTO(const char *prop, u32 hw_id, const char *fmt, va_list *va),
	TP_ARGS(prop, hw_id, fmt, va),
	TP_STRUCT__entry(
			__field(u32, hw_id)
			__string(prop, prop)
			__vstring(vstr, fmt, va)
	),
	TP_fast_assign(
			__entry->hw_id = hw_id;
			__assign_str(prop);
			__assign_vstr(vstr, fmt, va);
	),
	TP_printk("[Out_ctrl%d] %s: %s", __entry->hw_id, __get_str(prop),
		  __get_str(vstr))
);

DEFINE_EVENT(disp_hw_out_ctrl_feature, disp_update_out_ctrl_feature,
	TP_PROTO(const char *prop, u32 hw_id, const char *fmt, va_list *va),
	TP_ARGS(prop, hw_id, fmt, va)
);
DEFINE_EVENT(disp_hw_out_ctrl_feature, disp_config_out_ctrl_feature,
	TP_PROTO(const char *prop, u32 hw_id, const char *fmt, va_list *va),
	TP_ARGS(prop, hw_id, fmt, va)
);

DECLARE_EVENT_CLASS(disp_hw_wb_feature,
	TP_PROTO(const char *prop, u32 hw_id, const char *fmt, va_list *va),
	TP_ARGS(prop, hw_id, fmt, va),
	TP_STRUCT__entry(
			__field(u32, hw_id)
			__string(prop, prop)
			__vstring(vstr, fmt, va)
	),
	TP_fast_assign(
			__entry->hw_id = hw_id;
			__assign_str(prop);
			__assign_vstr(vstr, fmt, va);
	),
	TP_printk("[Writeback%d] %s: %s", __entry->hw_id, __get_str(prop),
		  __get_str(vstr))
);

DEFINE_EVENT(disp_hw_wb_feature, disp_update_wb_feature,
	TP_PROTO(const char *prop, u32 hw_id, const char *fmt, va_list *va),
	TP_ARGS(prop, hw_id, fmt, va)
);
DEFINE_EVENT(disp_hw_wb_feature, disp_config_wb_feature,
	TP_PROTO(const char *prop, u32 hw_id, const char *fmt, va_list *va),
	TP_ARGS(prop, hw_id, fmt, va)
);

#ifndef __VS_HW_TRACE_API_DEF__
#define __VS_HW_TRACE_API_DEF__

/* General-Purpose HW Programming Traces (use these signatures) */

static inline void _trace_update_hw_layer_feature(const char *prop, u32 hw_id,
						  const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	trace_disp_update_layer_feature(prop, hw_id, fmt, &args);
	va_end(args);
}

/**
 * trace_update_hw_layer_feature() - logs updating layer feature
 * @prop: name of property
 * @hw_id: id of layer being updated
 * @...: format string and args for string printing
 */
#define trace_update_hw_layer_feature(prop, hw_id, fmt, ...) \
	do { \
		if (trace_disp_update_layer_feature_enabled()) \
			_trace_update_hw_layer_feature(prop, hw_id, fmt, ##__VA_ARGS__); \
	} while (0)


static inline void _trace_config_hw_layer_feature(const char *prop, u32 hw_id,
						  const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	trace_disp_config_layer_feature(prop, hw_id, fmt, &args);
	va_end(args);
}

/**
 * trace_config_hw_layer_feature() - logs configuring layer feature
 * @prop: name of property
 * @hw_id: id of layer being updated
 * @...: format string and args for string printing
 */
#define trace_config_hw_layer_feature(prop, hw_id, fmt, ...) \
	do { \
		if (trace_disp_config_layer_feature_enabled()) \
			_trace_config_hw_layer_feature(prop, hw_id, fmt, ##__VA_ARGS__); \
	} while (0)


static inline void _trace_update_hw_display_feature(const char *prop, u32 hw_id,
						    const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	trace_disp_update_out_ctrl_feature(prop, hw_id, fmt, &args);
	va_end(args);
}

/**
 * trace_update_hw_display_feature() - logs updating display feature
 * @prop: name of property
 * @hw_id: id of hw component being updated
 * @...: format string and args for string printing
 */
#define trace_update_hw_display_feature(prop, hw_id, fmt, ...) \
	do { \
		if (trace_disp_update_out_ctrl_feature_enabled()) \
			_trace_update_hw_display_feature(prop, hw_id, fmt, ##__VA_ARGS__); \
	} while (0)


static inline void _trace_config_hw_display_feature(const char *prop, u32 hw_id,
						    const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	trace_disp_config_out_ctrl_feature(prop, hw_id, fmt, &args);
	va_end(args);
}

/**
 * trace_config_hw_display_feature() - logs configuring display feature
 * @prop: name of property
 * @hw_id: id of hw component being updated
 * @...: format string and args for string printing
 */
#define trace_config_hw_display_feature(prop, hw_id, fmt, ...) \
	do { \
		if (trace_disp_config_out_ctrl_feature_enabled()) \
			_trace_config_hw_display_feature(prop, hw_id, fmt, ##__VA_ARGS__); \
	} while (0)


static inline void _trace_update_hw_wb_feature(const char *prop, u32 hw_id,
					       const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	trace_disp_update_wb_feature(prop, hw_id, fmt, &args);
	va_end(args);
}

/**
 * trace_update_hw_wb_feature() - logs updating writeback feature
 * @prop: name of property
 * @hw_id: id of hw component being updated
 * @...: format string and args for string printing
 */
#define trace_update_hw_wb_feature(prop, hw_id, fmt, ...) \
	do { \
		if (trace_disp_update_wb_feature_enabled()) \
			_trace_update_hw_wb_feature(prop, hw_id, fmt, ##__VA_ARGS__); \
	} while (0)


static inline void _trace_config_hw_wb_feature(const char *prop, u32 hw_id,
					       const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	trace_disp_config_wb_feature(prop, hw_id, fmt, &args);
	va_end(args);
}

/**
 * trace_config_hw_wb_feature() - logs configuring writeback feature
 * @prop: name of property
 * @hw_id: id of hw component being updated
 * @...: format string and args for string printing
 */
#define trace_config_hw_wb_feature(prop, hw_id, fmt, ...) \
	do { \
		if (trace_disp_config_wb_feature_enabled()) \
			_trace_config_hw_wb_feature(prop, hw_id, fmt, ##__VA_ARGS__); \
	} while (0)

/*
 * Use enum to reduce some degree of function complexity explosion,
 * particularly in cases where performance is less critical
 */

enum hw_trace_component {
	HW_TRACE_LAYER = 0,
	HW_TRACE_OUT_CTRL,
	HW_TRACE_WRITEBACK,
};

#endif /* __VS_HW_TRACE_API_DEF__ */

TRACE_DEFINE_ENUM(HW_TRACE_LAYER);
TRACE_DEFINE_ENUM(HW_TRACE_OUT_CTRL);
TRACE_DEFINE_ENUM(HW_TRACE_WRITEBACK);

#define show_trace_component(x) \
	__print_symbolic(x, \
		 { HW_TRACE_LAYER, "Layer" }, \
		 { HW_TRACE_OUT_CTRL, "Out_ctrl" }, \
		 { HW_TRACE_WRITEBACK, "Writeback" })

/* Larger-Data HW Programming Traces */

/**
 * trace_disp_hw_feature_data() - trace hw feature with associated array
 * @component: component to modify (ex. "Layer" or "Writeback")
 * @prop: name of feature
 * @hw_id: id of layer/component being updated
 * @data_label: what to label the data array
 * @data: array to print out
 * @data_len: size of array to print out
 *
 * This trace should be disabled by default, as it may incur performance
 * penalties when copying out large amounts of data frequently,
 * but may be used for debugging/development.
 */
TRACE_EVENT_CONDITION(disp_hw_feature_data,
	TP_PROTO(enum hw_trace_component component, const char *prop, u32 hw_id,
		 const char *data_label, const u8 *data, size_t data_len),
	TP_ARGS(component, prop, hw_id, data_label, data, data_len),
	TP_CONDITION(data_len > 0),
	TP_STRUCT__entry(
			__field(u32, component)
			__field(u32, hw_id)
			__string(prop, prop)
			__string(data_label, data_label)
			__dynamic_array(u8, data, data_len)
	),
	TP_fast_assign(
			__entry->component = component;
			__assign_str(prop);
			__entry->hw_id = hw_id;
			__assign_str(data_label);
			memcpy(__get_dynamic_array(data), data, data_len);
	),
	TP_printk("[%s%d] %s: %s=[%s]", show_trace_component(__entry->component),
		  __entry->hw_id, __get_str(prop), __get_str(data_label),
		  __print_hex_str(__get_dynamic_array(data), __get_dynamic_array_len(data)))
);

#define trace_hw_layer_feature_data(name, hw_id, data_label, data, data_len) \
	trace_disp_hw_feature_data(HW_TRACE_LAYER, (name), (hw_id), (data_label), \
				   (data), (data_len))
#define trace_hw_display_feature_data(name, hw_id, data_label, data, data_len) \
	trace_disp_hw_feature_data(HW_TRACE_OUT_CTRL, (name), (hw_id), (data_label), \
				   (data), (data_len))
#define trace_hw_wb_feature_data(name, hw_id, data_label, data, data_len) \
	trace_disp_hw_feature_data(HW_TRACE_WRITEBACK, (name), (hw_id), (data_label), \
				   (data), (data_len))

/* Enable-Dirty HW Programming Traces */

/**
 * trace_disp_hw_feature_en_dirty() - trace hw feature with bool output signature
 * @component: component to modify (ex. "Layer" or "Writeback")
 * @prop: name of feature
 * @hw_id: id of layer/component being updated
 * @en: whether feature is enabled
 * @dirty: whether feature is "dirty"
 */
DECLARE_EVENT_CLASS(disp_hw_feature_en_dirty,
	TP_PROTO(enum hw_trace_component component, const char *prop, u32 hw_id,
		 bool en, bool dirty),
	TP_ARGS(component, prop, hw_id, en, dirty),
	TP_STRUCT__entry(
			__field(u32, component)
			__field(u32, hw_id)
			__field(bool, en)
			__field(bool, dirty)
			__string(prop, prop)
	),
	TP_fast_assign(
			__entry->component = component;
			__entry->hw_id = hw_id;
			__assign_str(prop);
			__entry->en = en;
			__entry->dirty = dirty;
	),
	TP_printk("[%s%d] %s: en:%d dirty:%d", show_trace_component(__entry->component),
		  __entry->hw_id, __get_str(prop), __entry->en, __entry->dirty)
);

DEFINE_EVENT(disp_hw_feature_en_dirty, disp_update_feature_en_dirty,
	TP_PROTO(enum hw_trace_component component, const char *prop, u32 hw_id,
		 bool en, bool dirty),
	TP_ARGS(component, prop, hw_id, en, dirty)
);
DEFINE_EVENT(disp_hw_feature_en_dirty, disp_config_feature_en_dirty,
	TP_PROTO(enum hw_trace_component component, const char *prop, u32 hw_id,
		 bool en, bool dirty),
	TP_ARGS(component, prop, hw_id, en, dirty)
);

#define trace_update_hw_layer_feature_en_dirty(name, hw_id, en, dirty) \
	trace_disp_update_feature_en_dirty(HW_TRACE_LAYER, (name), (hw_id), (en), (dirty))
#define trace_config_hw_layer_feature_en_dirty(name, hw_id, en, dirty) \
	trace_disp_config_feature_en_dirty(HW_TRACE_LAYER, (name), (hw_id), (en), (dirty))
#define trace_update_hw_display_feature_en_dirty(name, hw_id, en, dirty) \
	trace_disp_update_feature_en_dirty(HW_TRACE_OUT_CTRL, (name), (hw_id), (en), (dirty))
#define trace_config_hw_display_feature_en_dirty(name, hw_id, en, dirty) \
	trace_disp_config_feature_en_dirty(HW_TRACE_OUT_CTRL, (name), (hw_id), (en), (dirty))
#define trace_update_hw_wb_feature_en_dirty(name, hw_id, en, dirty) \
	trace_disp_update_feature_en_dirty(HW_TRACE_WRITEBACK, (name), (hw_id), (en), (dirty))
#define trace_config_hw_wb_feature_en_dirty(name, hw_id, en, dirty) \
	trace_disp_config_feature_en_dirty(HW_TRACE_WRITEBACK, (name), (hw_id), (en), (dirty))

/**
 * trace_disp_hw_feature_en() - trace hw feature with bool output signature
 * @component: component to modify (ex. "Layer" or "Writeback")
 * @prop: name of feature
 * @hw_id: id of layer/component being updated
 * @en: whether feature is enabled
 */
DECLARE_EVENT_CLASS(disp_hw_feature_en,
	TP_PROTO(enum hw_trace_component component, const char *prop, u32 hw_id,
		 bool en),
	TP_ARGS(component, prop, hw_id, en),
	TP_STRUCT__entry(
			__field(u32, component)
			__field(u32, hw_id)
			__field(bool, en)
			__string(prop, prop)
	),
	TP_fast_assign(
			__entry->component = component;
			__entry->hw_id = hw_id;
			__assign_str(prop);
			__entry->en = en;
	),
	TP_printk("[%s%d] %s: en:%d", show_trace_component(__entry->component),
		  __entry->hw_id, __get_str(prop), __entry->en)
);

DEFINE_EVENT(disp_hw_feature_en, disp_update_feature_en,
	TP_PROTO(enum hw_trace_component component, const char *prop, u32 hw_id, bool en),
	TP_ARGS(component, prop, hw_id, en)
);
DEFINE_EVENT(disp_hw_feature_en, disp_config_feature_en,
	TP_PROTO(enum hw_trace_component component, const char *prop, u32 hw_id, bool en),
	TP_ARGS(component, prop, hw_id, en)
);

#define trace_update_hw_layer_feature_en(name, hw_id, en) \
	trace_disp_update_feature_en(HW_TRACE_LAYER, (name), (hw_id), (en))
#define trace_config_hw_layer_feature_en(name, hw_id, en) \
	trace_disp_config_feature_en(HW_TRACE_LAYER, (name), (hw_id), (en))
#define trace_update_hw_display_feature_en(name, hw_id, en) \
	trace_disp_update_feature_en(HW_TRACE_OUT_CTRL, (name), (hw_id), (en))
#define trace_config_hw_display_feature_en(name, hw_id, en) \
	trace_disp_config_feature_en(HW_TRACE_OUT_CTRL, (name), (hw_id), (en))
#define trace_update_hw_wb_feature_en(name, hw_id, en) \
	trace_disp_update_feature_en(HW_TRACE_WRITEBACK, (name), (hw_id), (en))
#define trace_config_hw_wb_feature_en(name, hw_id, en) \
	trace_disp_config_feature_en(HW_TRACE_WRITEBACK, (name), (hw_id), (en))

/* Shadow register switch traces */

/**
 * trace_disp_hw_reg_switch() - trace hw feature shadow register switching
 * @name: name of the hardware block (ex. "Layer", "LTM, "Panel")
 * @hw_id: id of component being updated
 * @en: whether register switching is enabled
 */
DECLARE_EVENT_CLASS(disp_hw_reg_switch,
	TP_PROTO(const char *name, u32 hw_id, bool en),
	TP_ARGS(name, hw_id, en),
	TP_STRUCT__entry(
			__string(name, name)
			__field(u32, hw_id)
			__field(bool, en)
	),
	TP_fast_assign(
			__assign_str(name);
			__entry->hw_id = hw_id;
			__entry->en = en;
	),
	TP_printk("[%s%d] en:%d", __get_str(name), __entry->hw_id, __entry->en)
);

DEFINE_EVENT(disp_hw_reg_switch, disp_reg_switch,
	TP_PROTO(const char *name, u32 hw_id, bool en),
	TP_ARGS(name, hw_id, en)
);

#define trace_hw_layer_reg_switch(hw_id, en) \
	trace_disp_reg_switch("Layer", (hw_id), (en))
#define trace_hw_display_reg_switch(hw_id, en) \
	trace_disp_reg_switch("Out_ctrl", (hw_id), (en))
#define trace_hw_panel_reg_switch(hw_id, en) \
	trace_disp_reg_switch("Panel", (hw_id), (en))
#define trace_hw_ltm_reg_switch(hw_id, en) \
	trace_disp_reg_switch("LTM", (hw_id), (en))
#define trace_hw_wb_reg_switch(hw_id, en) \
	trace_disp_reg_switch("Writeback", (hw_id), (en))

/**
 * trace_disp_plane() - mark a plane as being enabled or disabled
 * @plane_id: id of plane being enabled/disabled
 * @enable: true for enable plane, false for disable plane
 */
TRACE_EVENT(disp_plane,
	TP_PROTO(int plane_id, bool enable),
	TP_ARGS(plane_id, enable),
	TP_STRUCT__entry(
		__field(int, plane_id)
		__field(bool, enable)
	),
	TP_fast_assign(
		__entry->plane_id = plane_id;
		__entry->enable = enable;
	),
	TP_printk("id:%d enable:%d",
		__entry->plane_id, __entry->enable)
);

/**
 * trace_disp_set_property() - mark a property as being set
 * @name: name of crtc or plane setting property on
 * @prop: name of property being set
 * @val: value set for the property (or blob id)
 */
TRACE_EVENT(disp_set_property,
	TP_PROTO(const char *name, const char *prop, u64 val),
	TP_ARGS(name, prop, val),
	TP_STRUCT__entry(
			__string(name, name)
			__string(prop, prop)
			__field(u64, val)
	),
	TP_fast_assign(
			__assign_str(name);
			__assign_str(prop);
			__entry->val = val;
	),
	TP_printk("[%s] %s val:%llu", __get_str(name), __get_str(prop), __entry->val)
);

/**
 * trace_sram_allocation() - log that an sram allocation was requested
 * @pool: enum vs_dpu_sram_pool_type - the pool that the request is serviced from
 * @plane: plane number the SRAM is being requested for
 * @realloc: set to 1 if this is a really instead of a new allocation
 * @old_size: size of the previous allocation if realloc==1
 * @requested_size: size of the new requested allocation
 * @free: remaining free in the pool
 */
TRACE_EVENT(sram_allocation,
	TP_PROTO(u8 pool, u8 plane, u8 realloc, u32 old_size, u32 requested_size, u32 free),
	TP_ARGS(pool, plane, realloc, old_size, requested_size, free),
	TP_STRUCT__entry(
			__field(u8, pool)
			__field(u8, plane)
			__field(u8, realloc)
			__field(u32, old_size_kb)
			__field(u32, requested_size_kb)
			__field(u32, free_kb)
	),
	TP_fast_assign(
			__entry->pool = pool;
			__entry->plane = plane;
			__entry->realloc = realloc;
			__entry->old_size_kb = old_size >> 10;
			__entry->requested_size_kb = requested_size >> 10;
			__entry->free_kb = free >> 10;
	),
	TP_printk(
		"pool:%d plane_id:%d realloc:%d old_size:%uK requested_size:%uK remain_free:%uK",
		__entry->pool, __entry->plane, __entry->realloc, __entry->old_size_kb,
		__entry->requested_size_kb, __entry->free_kb)
);

/**
 * trace_sram_free() - log that an sram free was requested
 * @pool: enum vs_dpu_sram_pool_type - the pool that the request is serviced from
 * @plane_id: plane number the SRAM is being freed for
 * @node_size: size of the node that is being freed
 * @free: remaining free memory in the pool
 */
TRACE_EVENT(sram_free,
	TP_PROTO(u8 pool, u8 plane_id, u32 node_size, u32 free),
	TP_ARGS(pool, plane_id, node_size, free),
	TP_STRUCT__entry(
			__field(u8, pool)
			__field(u8, plane_id)
			__field(u32, node_size_kb)
			__field(u32, free_kb)
	),
	TP_fast_assign(
			__entry->pool = pool;
			__entry->plane_id = plane_id;
			__entry->node_size_kb = node_size >> 10;
			__entry->free_kb = free >> 10;
	),
	TP_printk("pool:%d plane_id:%d freed_node_size:%uK remain_free:%uK", __entry->pool,
		  __entry->plane_id, __entry->node_size_kb, __entry->free_kb)
);

/**
 * sram_failure - event class for logging sram alloc/free failures
 * @pool: enum vs_dpu_sram_pool_type - the pool that the request is serviced from
 * @errno: error status
 * @free: remaining free memory in the pool
 * @plane_id: plane number the SRAM is being freed for
 */
DECLARE_EVENT_CLASS(sram_failure,
	TP_PROTO(u8 pool, int errno, u32 free, u8 plane_id),
	TP_ARGS(pool, errno, free, plane_id),
	TP_STRUCT__entry(
			__field(u8, pool)
			__field(int, errno)
			__field(u32, free_kb)
			__field(u8, plane_id)
	),
	TP_fast_assign(
			__entry->pool = pool;
			__entry->errno = errno;
			__entry->free_kb = free >> 10;
			__entry->plane_id = plane_id;
	),
	 TP_printk("pool:%d plane_id:%d remain_free:%uK err:%d",
			__entry->pool, __entry->plane_id, __entry->free_kb, __entry->errno)
);

DEFINE_EVENT_CONDITION(sram_failure, sram_alloc_failure,
		       TP_PROTO(u8 pool, int errno, u32 free, u8 plane_id),
		       TP_ARGS(pool, errno, free, plane_id),
		       TP_CONDITION(errno != 0)
);

DEFINE_EVENT_CONDITION(sram_failure, sram_free_failure,
		       TP_PROTO(u8 pool, int errno, u32 free, u8 plane_id),
		       TP_ARGS(pool, errno, free, plane_id),
		       TP_CONDITION(errno != 0)
);

#endif /* _VS_TRACE_H_ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE vs_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
