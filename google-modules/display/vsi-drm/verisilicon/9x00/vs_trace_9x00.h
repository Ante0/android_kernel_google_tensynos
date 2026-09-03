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

#if !defined(_VS_TRACE_9x00_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _VS_TRACE_9x00_H_

#include <drm/display/drm_dsc.h>
#include <linux/pm_runtime.h>
#include <linux/tracepoint.h>
#include <linux/timekeeping.h>
#include <vs_dc.h>
#include <vs_dc_hw.h>

/**
 * dc_init_trace() - Initializes the display controller's trace events.
 * @dev: Pointer to the device structure.
 *
 * This function retrieves the trace array for the display controller and enables
 * all the trace events defined in the dc_trace_events array.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int dc_init_trace(struct device *dev);

#ifndef __9x00_VS_HW_TRACE_API_DEF__
#define __9x00_VS_HW_TRACE_API_DEF__

/*
 * Use enum to reduce some degree of function complexity explosion,
 * particularly in cases where performance is less critical
 */

enum hw_trace_component_9x00 {
	HW_TRACE_LAYER_9x00 = 0,
	HW_TRACE_OUT_CTRL_9x00,
	HW_TRACE_WRITEBACK_9x00,
};

enum dpu_component_9x00 {
	DPU_COMPONENT_BE = 0,
	DPU_COMPONENT_FE0,
	DPU_COMPONENT_FE1,
	DPU_COMPONENT_WB,
}

#endif /* __9x00_VS_HW_TRACE_API_DEF__ */

TRACE_DEFINE_ENUM(HW_TRACE_LAYER_9x00);
TRACE_DEFINE_ENUM(HW_TRACE_OUT_CTRL_9x00);
TRACE_DEFINE_ENUM(HW_TRACE_WRITEBACK_9x00);

#define show_trace_component_9x00(x) \
	__print_symbolic(x, \
		 { HW_TRACE_LAYER_9x00, "Layer" }, \
		 { HW_TRACE_OUT_CTRL_9x00, "Out_ctrl" }, \
		 { HW_TRACE_WRITEBACK_9x00, "Writeback" })

TRACE_DEFINE_ENUM(DPU_COMPONENT_BE);
TRACE_DEFINE_ENUM(DPU_COMPONENT_FE0);
TRACE_DEFINE_ENUM(DPU_COMPONENT_FE1);
TRACE_DEFINE_ENUM(DPU_COMPONENT_WB);

#define show_dpu_component(x) \
	__print_symbolic(x, \
		{ DPU_COMPONENT_BE, "BE " }, \
		{ DPU_COMPONENT_FE0, "FE0" }, \
		{ DPU_COMPONENT_FE1, "FE1" }, \
		{ DPU_COMPONENT_WB, "WB" })

/* Specific HW Programming Traces */

TRACE_EVENT(disp_set_mode,
	TP_PROTO(u32 hw_id, struct dc_hw_display_mode *mode),
	TP_ARGS(hw_id, mode),
	TP_STRUCT__entry(
			__field(u32, hw_id)
			__field(u32, bus_format)
			__field(u32, output_mode)
			__field(u16, h_active)
			__field(u16, v_active)
			__field(int, fps)
			__field(bool, en)
			__field(bool, vrr_enable)
	),
	TP_fast_assign(
			__entry->hw_id = hw_id;
			__entry->en = mode->enable;
			__entry->bus_format = mode->bus_format;
			__entry->vrr_enable = mode->vrr_enable;
			__entry->output_mode = mode->output_mode;
			__entry->h_active = mode->h_active;
			__entry->v_active = mode->v_active;
			__entry->fps = mode->fps;
	),
	TP_printk("[Out_ctrl%d] SET_MODE: en:%d bus_format:%d vrr_enable:%d output_mode:%d timings:%dx%d@%d",
		  __entry->hw_id, __entry->en, __entry->bus_format, __entry->vrr_enable,
		  __entry->output_mode, __entry->h_active, __entry->v_active, __entry->fps)
);

/* Framebuffer HW Programming Traces */

TRACE_EVENT(disp_config_hw_fb_mask,
	TP_PROTO(enum hw_trace_component_9x00 component, const char *prop, u32 hw_id,
		 struct dc_hw_fb *fb),
	TP_ARGS(component, prop, hw_id, fb),
	TP_STRUCT__entry(
			__field(u32, component)
			__field(u32, hw_id)
			__field(u64, address)
			__field(u32, stride)
			__field(bool, en)
			__string(prop, prop)
	),
	TP_fast_assign(
			__entry->component = component;
			__entry->hw_id = hw_id;
			__entry->address = fb->address;
			__entry->stride = fb->stride;
			__entry->en = fb->enable;
			__assign_str(prop);
	),
	TP_printk("[%s%d] %s: en:%d address:%#llx stride:%d",
		  show_trace_component_9x00(__entry->component), __entry->hw_id,
		  __get_str(prop), __entry->en, __entry->address, __entry->stride));

#define trace_config_hw_layer_fb_mask(name, hw_id, fb) \
	trace_disp_config_hw_fb_mask(HW_TRACE_LAYER_9x00, (name), (hw_id), (fb))
#define trace_config_hw_display_fb_mask(name, hw_id, fb) \
	trace_disp_config_hw_fb_mask(HW_TRACE_OUT_CTRL_9x00, (name), (hw_id), (fb))
#define trace_config_hw_wb_fb_mask(name, hw_id, fb) \
	trace_disp_config_hw_fb_mask(HW_TRACE_WRITEBACK_9x00, (name), (hw_id), (fb))

TRACE_EVENT(disp_config_hw_fb,
	TP_PROTO(enum hw_trace_component_9x00 component, const char *prop, u32 hw_id,
		 struct dc_hw_fb *fb),
	TP_ARGS(component, prop, hw_id, fb),
	TP_STRUCT__entry(
			__field(u32, component)
			__field(u32, hw_id)
			__field(u64, address)
			__field(u64, u_address)
			__field(u64, v_address)
			__field(u32, stride)
			__field(u32, u_stride)
			__field(u32, v_stride)
			__field(u16, width)
			__field(u16, height)
			__field(u8, format)
			__field(u8, tile_mode)
			__field(u8, rotation)
			__field(u8, swizzle)
			__field(u8, uv_swizzle)
			__field(u8, zpos)
			__field(u8, display_id)
			__field(bool, secure)
			__field(bool, en)
			__string(prop, prop)
	),
	TP_fast_assign(
			__entry->component = component;
			__entry->hw_id = hw_id;
			__entry->address = fb->address;
			__entry->u_address = fb->u_address;
			__entry->v_address = fb->v_address;
			__entry->stride = fb->stride;
			__entry->u_stride = fb->u_stride;
			__entry->v_stride = fb->v_stride;
			__entry->width = fb->width;
			__entry->height = fb->height;
			__entry->format = fb->format;
			__entry->rotation = fb->rotation;
			__entry->tile_mode = fb->tile_mode;
			__entry->swizzle = fb->swizzle;
			__entry->uv_swizzle = fb->uv_swizzle;
			__entry->zpos = fb->zpos;
			__entry->display_id = fb->display_id;
			__entry->secure = fb->secure;
			__entry->en = fb->enable;
			__assign_str(prop);
	),

	TP_printk(
		"[%s%d] %s: en:%d address_[x u v]:%#llx %#llx %#llx width:%d height:%d format:%d stride_[x u v]:%d %d %d tile_mode:%d rotation:%d swizzle_[x uv]:%d %d zpos:%d secure_layer:%d display_id:%d",
		show_trace_component_9x00(__entry->component), __entry->hw_id, __get_str(prop),
		__entry->en, __entry->address, __entry->u_address, __entry->v_address,
		__entry->width, __entry->height, __entry->format, __entry->stride,
		__entry->u_stride, __entry->v_stride, __entry->tile_mode, __entry->rotation,
		__entry->swizzle, __entry->uv_swizzle, __entry->zpos, __entry->secure,
		__entry->display_id));

#define trace_config_hw_layer_fb(name, hw_id, fb) \
	trace_disp_config_hw_fb(HW_TRACE_LAYER_9x00, (name), (hw_id), (fb))
#define trace_config_hw_display_fb(name, hw_id, fb) \
	trace_disp_config_hw_fb(HW_TRACE_OUT_CTRL_9x00, (name), (hw_id), (fb))
#define trace_config_hw_wb_fb(name, hw_id, fb) \
	trace_disp_config_hw_fb(HW_TRACE_WRITEBACK_9x00, (name), (hw_id), (fb))

TRACE_EVENT(disp_trusty_protect_ip,
	TP_PROTO(u8 hw_id, u8 plane_sid, unsigned long *secured_layers_mask, bool secure),
	TP_ARGS(hw_id, plane_sid, secured_layers_mask, secure),
	TP_STRUCT__entry(
		__field(u8, hw_id)
		__field(u8, plane_sid)
		__field(bool, secure)
		__array(unsigned long, secured_layers_mask, BITS_TO_LONGS(HW_PLANE_NUM))
	),
	TP_fast_assign(
		__entry->hw_id = hw_id;
		__entry->plane_sid = plane_sid;
		__entry->secure = secure;
		memcpy(__entry->secured_layers_mask, secured_layers_mask,
		       sizeof(__entry->secured_layers_mask));
	),
	TP_printk(
		"[Layer%d] sid:%d secured_layers_mask:0x%*pb secure:%d",
		__entry->hw_id, __entry->plane_sid, HW_PLANE_NUM,
		__entry->secured_layers_mask, __entry->secure)
);

TRACE_EVENT(disp_underrun_config,
	TP_PROTO(u32 hw_id, u32 output_id, struct dc_hw_display_mode *mode, u32 te_width_us,
		 u32 config),
	TP_ARGS(hw_id, output_id, mode, te_width_us, config),
	TP_STRUCT__entry(
			__field(u32, hw_id)
			__field(u32, output_id)
			__field(u32, h_active)
			__field(u32, v_active)
			__field(u32, te_width_us)
			__field(int, fps)
			__field(u32, config)
	),
	TP_fast_assign(
			__entry->hw_id = hw_id;
			__entry->output_id = output_id;
			__entry->h_active = mode->h_active;
			__entry->v_active = mode->v_active;
			__entry->te_width_us = te_width_us;
			__entry->fps = mode->fps;
			__entry->config = config;
	),
	TP_printk("[Out_ctrl%d] output_id:%u timings:%dx%d@%d te_width_us:%u config:%#x",
		  __entry->hw_id, __entry->output_id,  __entry->h_active, __entry->v_active,
		  __entry->fps, __entry->te_width_us, __entry->config)
);

TRACE_EVENT(disp_urgent_cmd_config,
	TP_PROTO(u32 hw_id, u32 output_id, struct dc_hw_display_mode *mode, u32 sys_counter,
		 u32 sys_delay_counter, u32 urgent_value),
	TP_ARGS(hw_id, output_id, mode, sys_counter, sys_delay_counter, urgent_value),
	TP_STRUCT__entry(
			__field(u32, hw_id)
			__field(u32, output_id)
			__field(u32, h_active)
			__field(u32, v_active)
			__field(int, fps)
			__field(u32, sys_counter)
			__field(u32, sys_delay_counter)
			__field(u32, urgent_value)
	),
	TP_fast_assign(
			__entry->hw_id = hw_id;
			__entry->output_id = output_id;
			__entry->h_active = mode->h_active;
			__entry->v_active = mode->v_active;
			__entry->fps = mode->fps;
			__entry->sys_counter = sys_counter;
			__entry->sys_delay_counter = sys_delay_counter;
			__entry->urgent_value = urgent_value;
	),
	TP_printk("[Out_ctrl%d] output_id:%u timings:%dx%d@%d sys_counter:%#x sys_delay_counter:%#x urgent_value:%#x",
		  __entry->hw_id, __entry->output_id, __entry->h_active, __entry->v_active,
		  __entry->fps, __entry->sys_counter, __entry->sys_delay_counter,
		  __entry->urgent_value)
);

TRACE_EVENT(disp_urgent_vid_config,
	TP_PROTO(u32 hw_id, u32 output_id, u32 qos_thresh_0, u32 qos_thresh_1, u32 qos_thresh_2,
		 u32 urgent_thresh_0, u32 urgent_thresh_1, u32 urgent_thresh_2,
		 u32 urgent_low_thresh, u32 urgent_high_thresh,
		 u32 healthy_thresh),
	TP_ARGS(hw_id, output_id, qos_thresh_0, qos_thresh_1, qos_thresh_2,
		urgent_thresh_0, urgent_thresh_1, urgent_thresh_2,
		urgent_low_thresh, urgent_high_thresh,
		healthy_thresh),
	TP_STRUCT__entry(
			__field(u32, hw_id)
			__field(u32, output_id)
			__field(u32, qos_thresh_0)
			__field(u32, qos_thresh_1)
			__field(u32, qos_thresh_2)
			__field(u32, urgent_thresh_0)
			__field(u32, urgent_thresh_1)
			__field(u32, urgent_thresh_2)
			__field(u32, urgent_low_thresh)
			__field(u32, urgent_high_thresh)
			__field(u32, healthy_thresh)
	),
	TP_fast_assign(
			__entry->hw_id = hw_id;
			__entry->output_id = output_id;
			__entry->qos_thresh_0 = qos_thresh_0;
			__entry->qos_thresh_1 = qos_thresh_1;
			__entry->qos_thresh_2 = qos_thresh_2;
			__entry->urgent_thresh_0 = urgent_thresh_0;
			__entry->urgent_thresh_1 = urgent_thresh_1;
			__entry->urgent_thresh_2 = urgent_thresh_2;
			__entry->urgent_low_thresh = urgent_low_thresh;
			__entry->urgent_high_thresh = urgent_high_thresh;
			__entry->healthy_thresh = healthy_thresh;
	),
	TP_printk(
		"[Out_ctrl%d] output_id:%u qos [%#x %#x %#x] urgent [%#x %#x %#x] lo:%#x hi:%#x health:%#x",
		__entry->hw_id, __entry->output_id,
		__entry->qos_thresh_0, __entry->qos_thresh_1, __entry->qos_thresh_2,
		__entry->urgent_thresh_0, __entry->urgent_thresh_1, __entry->urgent_thresh_2,
		__entry->urgent_low_thresh, __entry->urgent_high_thresh,
		__entry->healthy_thresh)
);

TRACE_EVENT(disp_dsc,
	TP_PROTO(u32 hw_id, bool enable, bool video_mode, u8 slices_per_line, u8 ss_num,
		 const struct drm_dsc_config *dsc_cfg),
	TP_ARGS(hw_id, enable, video_mode, slices_per_line, ss_num, dsc_cfg),
	TP_STRUCT__entry(
		__field(u32, hw_id)
		__field(bool, enable)
		__field(bool, video_mode)
		__field(u8, bits_per_component)
		__field(u8, slices_per_line)
		__field(u16, pic_width)
		__field(u16, pic_height)
		__field(u16, bits_per_pixel)
		__field(u16, slice_width)
		__field(u16, slice_height)
		__field(u8, ss_num)
	),
	TP_fast_assign(
		__entry->hw_id = hw_id;
		__entry->enable = enable;
		__entry->video_mode = video_mode;
		__entry->slices_per_line = slices_per_line;
		__entry->ss_num = ss_num;
		__entry->bits_per_component = dsc_cfg ? dsc_cfg->bits_per_component : 0;
		__entry->pic_width = dsc_cfg ? dsc_cfg->pic_width : 0;
		__entry->pic_height = dsc_cfg ? dsc_cfg->pic_height : 0;
		__entry->bits_per_pixel = dsc_cfg ? dsc_cfg->bits_per_pixel : 0;
		__entry->slice_width = dsc_cfg ? dsc_cfg->slice_width : 0;
		__entry->slice_height = dsc_cfg ? dsc_cfg->slice_height : 0;
	),
	TP_printk("[Out_ctrl%d] DSC: en:%d video_mode:%d pic[%u x %u] bpp(x16):%u bpc:%u slc[%u x %u] slcline:%u ss_num:%u",
		  __entry->hw_id, __entry->enable, __entry->video_mode,
		  __entry->pic_width, __entry->pic_height, __entry->bits_per_pixel,
		  __entry->bits_per_component, __entry->slice_width, __entry->slice_height,
		  __entry->slices_per_line, __entry->ss_num)
);

DECLARE_EVENT_CLASS(disp_dc,
	TP_PROTO(struct vs_dc *dc),
	TP_ARGS(dc),
	TP_STRUCT__entry(
		__field(bool, enabled)
	),
	TP_fast_assign(
		__entry->enabled = dc->enabled;
	),
	TP_printk("DC enabled:%d", __entry->enabled)
);
DEFINE_EVENT(disp_dc, disp_dc_enable,
	TP_PROTO(struct vs_dc *dc),
	TP_ARGS(dc)
);
DEFINE_EVENT(disp_dc, disp_dc_disable,
	TP_PROTO(struct vs_dc *dc),
	TP_ARGS(dc)
);

DECLARE_EVENT_CLASS(disp_dc_component_en,
	TP_PROTO(enum dpu_component_9x00 dpu_component),
	TP_ARGS(dpu_component),
	TP_STRUCT__entry(
		__field(u32, dpu_component)
	),
	TP_fast_assign(
		__entry->dpu_component = dpu_component;
	),
	TP_printk("%s", show_dpu_component(__entry->dpu_component))
);
DEFINE_EVENT(disp_dc_component_en, disp_dc_component_enable,
	TP_PROTO(enum dpu_component_9x00 dpu_component),
	TP_ARGS(dpu_component)
);
DEFINE_EVENT(disp_dc_component_en, disp_dc_component_disable,
	TP_PROTO(enum dpu_component_9x00 dpu_component),
	TP_ARGS(dpu_component)
);

#define trace_disp_dc_fe_enable(fe_idx)                                    \
	do {                                                               \
		if ((fe_idx) == 0)                                         \
			trace_disp_dc_component_enable(DPU_COMPONENT_FE0); \
		else                                                       \
			trace_disp_dc_component_enable(DPU_COMPONENT_FE1); \
	} while (0)
#define trace_disp_dc_fe_disable(fe_idx)                                    \
	do {                                                                \
		if ((fe_idx) == 0)                                          \
			trace_disp_dc_component_disable(DPU_COMPONENT_FE0); \
		else                                                        \
			trace_disp_dc_component_disable(DPU_COMPONENT_FE1); \
	} while (0)
#define trace_disp_dc_be_enable() \
	trace_disp_dc_component_enable(DPU_COMPONENT_BE)
#define trace_disp_dc_be_disable() \
	trace_disp_dc_component_disable(DPU_COMPONENT_BE)
#define trace_disp_dc_wb_enable() \
	trace_disp_dc_component_enable(DPU_COMPONENT_WB)
#define trace_disp_dc_wb_disable() \
	trace_disp_dc_component_disable(DPU_COMPONENT_WB)

DECLARE_EVENT_CLASS(disp_dc_irq,
	TP_PROTO(struct device *dev, const struct dc_irq_info *irq_info),
	TP_ARGS(dev, irq_info),
	TP_STRUCT__entry(
		__field(bool, suspended)
		__field(bool, active)
		__field(int, usage_count)
		__field(int, enable_count)
		__string(name, dev_name(dev))
	),
	TP_fast_assign(
		__entry->suspended = pm_runtime_suspended(dev);
		__entry->active = pm_runtime_active(dev);
		__entry->usage_count = atomic_read(&dev->power.usage_count);
		__entry->enable_count = irq_info->enable_count;
		__assign_str(name);
	),
	TP_printk("DC IRQ %s suspended:%d active:%d usage_count:%d irq_enable_count:%d",
		  __get_str(name), __entry->suspended, __entry->active,
		  __entry->usage_count, __entry->enable_count)
);
DEFINE_EVENT(disp_dc_irq, disp_dc_enable_irqs,
	TP_PROTO(struct device *dev, const struct dc_irq_info *irq_info),
	TP_ARGS(dev, irq_info)
);
DEFINE_EVENT(disp_dc_irq, disp_dc_disable_irqs,
	TP_PROTO(struct device *dev, const struct dc_irq_info *irq_info),
	TP_ARGS(dev, irq_info)
);

TRACE_EVENT(disp_dc_irq_status,
	TP_PROTO(struct device *dev, const struct dc_irq_info *irq_info),
	TP_ARGS(dev, irq_info),
	TP_STRUCT__entry(
		__bitmask(masked_status, irq_info->irq_num)
		__bitmask(pending_status, irq_info->irq_num)
		__dynamic_array(u8, depths, irq_info->irq_num)
		__string(name, dev_name(dev))
	),
	TP_fast_assign(
		__assign_bitmask(masked_status, irq_info->irq_masked_status, irq_info->irq_num);
		__assign_bitmask(pending_status, irq_info->irq_pending_status, irq_info->irq_num);
		memcpy(__get_dynamic_array(depths), irq_info->irq_depths,
		       irq_info->irq_num * sizeof(*irq_info->irq_depths));
		__assign_str(name);
	),
	TP_printk("DC IRQ %s masked:%s pending:%s depths:[%s]",
		  __get_str(name), __get_bitmask(masked_status), __get_bitmask(pending_status),
		  __print_hex(__get_dynamic_array(depths), __get_dynamic_array_len(depths)))
);

TRACE_EVENT_CONDITION(disp_err_irqs,
	TP_PROTO(u16 pvric_err, u8 underrun, u8 data_lost),
	TP_ARGS(pvric_err, underrun, data_lost),
	TP_CONDITION(pvric_err || underrun || data_lost),
	TP_STRUCT__entry(
		__field(u16, pvric_err)
		__field(u8, underrun)
		__field(u8, data_lost)
	),
	TP_fast_assign(
		__entry->pvric_err = pvric_err;
		__entry->underrun = underrun;
		__entry->data_lost = data_lost;
	),
	TP_printk("pvric_err: %#x underrun: %#x data_lost: %#x",
		__entry->pvric_err, __entry->underrun, __entry->data_lost)
);

TRACE_EVENT_CONDITION(disp_bus_err_irqs,
	TP_PROTO(const char *name, long data),
	TP_ARGS(name, data),
	TP_CONDITION(data),
	TP_STRUCT__entry(
		__string(name, name)
		__field(long, data)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->data = data;
	),
	TP_printk("%s: %#lx", __get_str(name), __entry->data)
);

TRACE_EVENT(disp_dsc_status,
	TP_PROTO(u32 general_status, u32 hslice_status, u32 out_status, u32 intr_status),
	TP_ARGS(general_status, hslice_status, out_status, intr_status),
	TP_STRUCT__entry(
		__field(u32, general_status)
		__field(u16, slice_line_count_encoded)
		__field(u16, slice_count_encoded)
		__field(u16, slice_line_count_out)
		__field(u16, slice_count_out)
		__field(u16, intr_status)
	),
	TP_fast_assign(
		__entry->general_status = general_status;
		__entry->slice_line_count_encoded = hslice_status & 0xFFFF;
		__entry->slice_count_encoded = (hslice_status >> 16) & 0xFFFF;
		__entry->slice_line_count_out = out_status & 0xFFFF;
		__entry->slice_count_out = (out_status >> 16) & 0xFFFF;
		__entry->intr_status = intr_status & 0xFFFF;
	),
	TP_printk("DSC slices:[%uenc > %uout] lines:[%uenc > %uout] status:[%s] intr:[%s]",
		 __entry->slice_count_encoded, __entry->slice_count_out,
		 __entry->slice_line_count_encoded, __entry->slice_line_count_out,
		 __print_flags(__entry->general_status, "|",
			       { (1UL << 0), "ce" },
			       { (1UL << 1), "fstart" },
			       { (1UL << 2), "fdone" },
			       { (1UL << 3), "obempty0" },
			       { (1UL << 4), "obempty1" },
			       { (1UL << 5), "obfull0" },
			       { (1UL << 6), "obfull1" }),
		 __print_flags(__entry->intr_status, "|",
			       { (1UL << 0), "uflow0" },
			       { (1UL << 1), "uflow1" },
			       { (1UL << 2), "rcmodoflow0" },
			       { (1UL << 3), "rcmodoflow1" })
		 )
);

DECLARE_EVENT_CLASS(disp_dc_power,
	TP_PROTO(struct device *dev, bool sync, int ret),
	TP_ARGS(dev, sync, ret),
	TP_STRUCT__entry(
		__field(bool, enabled)
		__field(bool, suspended)
		__field(bool, active)
		__field(int, usage_count)
		__field(bool, sync)
		__field(ktime_t, timestamp)
		__field(int, ret)
	),
	TP_fast_assign(
		__entry->enabled = pm_runtime_enabled(dev);
		__entry->suspended = pm_runtime_suspended(dev);
		__entry->active = pm_runtime_active(dev);
		__entry->usage_count = atomic_read(&dev->power.usage_count);
		__entry->sync = sync;
		__entry->timestamp = ktime_get_real();
		__entry->ret = ret;
	),
	TP_printk("DC PD device enabled:%d suspended:%d active:%d usage_count:%d sync:%d ts_utc:%lld ret:%d",
		  __entry->enabled, __entry->suspended, __entry->active, __entry->usage_count,
		  __entry->sync, __entry->timestamp, __entry->ret)
);
DEFINE_EVENT(disp_dc_power, disp_dc_power_get,
	TP_PROTO(struct device *dev, bool sync, int ret),
	TP_ARGS(dev, sync, ret)
);
DEFINE_EVENT(disp_dc_power, disp_dc_power_put,
	TP_PROTO(struct device *dev, bool sync, int ret),
	TP_ARGS(dev, sync, ret)
);

DECLARE_EVENT_CLASS(disp_dc_component_power,
	TP_PROTO(enum dpu_component_9x00 dpu_component, struct device *dev, bool sync, int ret),
	TP_ARGS(dpu_component, dev, sync, ret),
	TP_STRUCT__entry(
		__field(u32, dpu_component)
		__field(bool, enabled)
		__field(bool, suspended)
		__field(bool, active)
		__field(int, usage_count)
		__field(bool, sync)
		__field(ktime_t, timestamp)
		__field(int, ret)
	),
	TP_fast_assign(
		__entry->dpu_component = dpu_component;
		__entry->enabled = pm_runtime_enabled(dev);
		__entry->suspended = pm_runtime_suspended(dev);
		__entry->active = pm_runtime_active(dev);
		__entry->usage_count = atomic_read(&dev->power.usage_count);
		__entry->sync = sync;
		__entry->timestamp = ktime_get_real();
		__entry->ret = ret;
	),
	TP_printk("%s: enabled:%d suspended:%d active:%d usage_count:%d sync:%d ts_utc:%lld ret:%d",
		  show_dpu_component(__entry->dpu_component), __entry->enabled,
		  __entry->suspended, __entry->active, __entry->usage_count,
		  __entry->sync, __entry->timestamp, __entry->ret)
);
DEFINE_EVENT(disp_dc_component_power, disp_dc_component_power_get,
	TP_PROTO(enum dpu_component_9x00 dpu_component, struct device *dev, bool sync, int ret),
	TP_ARGS(dpu_component, dev, sync, ret)
);
DEFINE_EVENT(disp_dc_component_power, disp_dc_component_power_put,
	TP_PROTO(enum dpu_component_9x00 dpu_component, struct device *dev, bool sync, int ret),
	TP_ARGS(dpu_component, dev, sync, ret)
);
#define trace_disp_dc_fe_power_get(fe_idx, dev, sync, ret)                       \
	do {                                                                     \
		if ((fe_idx) == 0)                                               \
			trace_disp_dc_component_power_get(DPU_COMPONENT_FE0,     \
							  (dev), (sync), (ret)); \
		else                                                             \
			trace_disp_dc_component_power_get(DPU_COMPONENT_FE1,     \
							  (dev), (sync), (ret)); \
	} while (0)
#define trace_disp_dc_fe_power_put(fe_idx, dev, sync, ret)                       \
	do {                                                                     \
		if ((fe_idx) == 0)                                               \
			trace_disp_dc_component_power_put(DPU_COMPONENT_FE0,     \
							  (dev), (sync), (ret)); \
		else                                                             \
			trace_disp_dc_component_power_put(DPU_COMPONENT_FE1,     \
							  (dev), (sync), (ret)); \
	} while (0)
#define trace_disp_dc_be_power_get(dev, sync, ret) \
	trace_disp_dc_component_power_get(DPU_COMPONENT_BE, (dev), (sync), (ret))
#define trace_disp_dc_be_power_put(dev, sync, ret) \
	trace_disp_dc_component_power_put(DPU_COMPONENT_BE, (dev), (sync), (ret))
#define trace_disp_dc_wb_power_get(dev, sync, ret) \
	trace_disp_dc_component_power_get(DPU_COMPONENT_WB, (dev), (sync), (ret))
#define trace_disp_dc_wb_power_put(dev, sync, ret) \
	trace_disp_dc_component_power_put(DPU_COMPONENT_WB, (dev), (sync), (ret))

TRACE_EVENT(disp_be_idle_timeout,
	TP_PROTO(u32 config),
	TP_ARGS(config),
	TP_STRUCT__entry(
		__field(u32, config)
	),
	TP_fast_assign(
		__entry->config = config;
	),
	TP_printk("Backend idle check timed out! Status: 0x%x", __entry->config)
);
#endif /* _VS_TRACE_9x00_H_ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE vs_trace_9x00

/* This part must be outside protection */
#include <trace/define_trace.h>
