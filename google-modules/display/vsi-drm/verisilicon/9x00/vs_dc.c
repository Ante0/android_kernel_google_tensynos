// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */
#include <interconnect/google_icc_helper.h>

#include <linux/align.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/irqnr.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/trace.h>
#include <linux/trace_events.h>
#include <linux/units.h>
#include <linux/vmalloc.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/vs_drm.h>

#include <gs_drm/gs_drm_connector.h>
#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
#include <perf/core/perf_domain.h>
#endif
#include <trace/dpu_trace.h>

#include "vs_crtc.h"
#include "vs_dc.h"
#include "vs_dc_pre.h"
#include "vs_dc_post.h"
#include "vs_dc_debugfs.h"
#include "vs_dc_hw.h"
#include "vs_drm_state_record.h"
#include "vs_drv.h"
#include "vs_dc_info.h"
#include "vs_module_params.h"
#include "vs_writeback.h"
#include "vs_dc_sram.h"
#include "vs_dc_hw.h"
#include "vs_gem.h"
#include "display_compress/vs_dc_dsc.h"
#include "vs_trace.h"
#include "vs_trace_9x00.h"

#include <drm/vs_drm_fourcc.h>
#include <drm/drm_vblank.h>
#include <drm/sscd/gs_sscd_dpu.h>

#define VS_DC_AUTOSUPEND_DELAY_MS 30
#define FRAMEDONE_TIMEOUT msecs_to_jiffies(100)

struct vs_dc_sscd_state_priv {
	struct drm_state_history_data sh_data;
	struct display_sscd_section_config cfgs[];
};
#define cfgs_to_priv(ptr) container_of((void *)(ptr), struct vs_dc_sscd_state_priv, cfgs)

static unsigned int framedone_timeout;

bool is_display_cmd_sw_trigger(struct dc_hw_display *display)
{
	return (display->mode.output_mode & VS_OUTPUT_MODE_CMD) &&
	       !(display->mode.output_mode & VS_OUTPUT_MODE_CMD_AUTO);
}

void dc_component_enable_irqs(struct device *dev, struct dc_irq_info *irq_info)
{
	int i;

	for (i = 0; i < irq_info->irq_num; i++)
		enable_irq(irq_info->irqs[i]);

	irq_info->enable_count++;
	trace_disp_dc_enable_irqs(dev, irq_info);
}

void dc_component_disable_irqs(struct device *dev, struct dc_irq_info *irq_info)
{
	int i;

	for (i = 0; i < irq_info->irq_num; i++)
		disable_irq(irq_info->irqs[i]);

	irq_info->enable_count--;
	trace_disp_dc_disable_irqs(dev, irq_info);
}

static void vs_dc_do_hw_reset(struct device *dev)
{
	int i;
	bool needs_hw_reset = false;
	struct vs_dc *dc = dev_get_drvdata(dev);

	if (!dc_hw_fe_is_all_layers_idle(&dc->hw))
		needs_hw_reset = true;

	if (dc->hw.fe0_has_bus_errors) {
		dc->hw.fe0_has_bus_errors = false;
		dc_hw_do_fe0_reset(&dc->hw);
	}

	if (dc->hw.fe1_has_bus_errors) {
		dc->hw.fe1_has_bus_errors = false;
		dc_hw_do_fe1_reset(&dc->hw);
	}

	if (dc->hw.be_has_bus_errors) {
		dc->hw.be_has_bus_errors = false;
		dc_hw_do_be_reset(&dc->hw);
	}

	for (i = 0; i < DC_DISPLAY_NUM; i++) {
		if (!dc->crtc[i])
			continue;

		if (!dc->crtc[i]->needs_hw_reset)
			continue;

		needs_hw_reset = true;
		dc->crtc[i]->needs_hw_reset = false;
		dev_warn(dev, "%s CRTC-%d needs hardware reset\n", __func__, i);
	}

	if (needs_hw_reset) {
		dev_warn(dev, "%s triggering hardware reset\n", __func__);
		dc_hw_do_reset(&dc->hw);
	}
}

#if IS_ENABLED(CONFIG_PM_SLEEP)
static int vs_dc_res_disable(struct device *dev)
{
	struct vs_dc *dc = dev_get_drvdata(dev);

	if (!dc)
		return 0;

	mutex_lock(&dc->dc_lock);
	dev_dbg(dev, "%s\n", __func__);

	WARN_ON(!dc->enabled);
	if (!dc->enabled)
		goto end;

	dc_hw_save_status(&dc->hw);

	if (!dc_hw_is_be_idle(&dc->hw))
		dev_warn(dev, "DPU BE not idle\n");

	vs_dc_do_hw_reset(dev);
	dc_hw_reset_all_be_interrupts(&dc->hw);
	dc_hw_enable_clock_domain_iso(&dc->hw, true);

	vs_qos_clear_qos_configs(dc);

	dc->enabled = false;

	trace_disp_dc_disable(dc);

end:
	mutex_unlock(&dc->dc_lock);

	return 0;
}

static void _vs_dc_mark_first_frame_not_triggered(struct vs_dc *dc)
{
	int i;
	int display_num;

	if (dc->hw.info)
		display_num = dc->hw.info->display_num;
	else
		display_num = DC_DISPLAY_NUM;

	for (i = 0; i < display_num; ++i) {
		struct vs_crtc *vs_crtc = dc->crtc[i];

		if (!vs_crtc)
			continue;
		vs_crtc->first_frame_trigger = false;
	}
}

static int vs_dc_res_enable(struct device *dev)
{
	int ret = 0;
	struct vs_dc *dc = dev_get_drvdata(dev);

	mutex_lock(&dc->dc_lock);
	dev_dbg(dev, "%s\n", __func__);

	WARN_ON(dc->enabled);
	if (dc->enabled)
		goto end;

	dc_hw_force_be_csr_read(&dc->hw);
	dc_hw_enable_clock_domain_iso(&dc->hw, false);
	_vs_dc_mark_first_frame_not_triggered(dc);

	dc->enabled = true;

	trace_disp_dc_enable(dc);

end:
	mutex_unlock(&dc->dc_lock);

	return ret;
}

static int vs_dc_pm_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);

	DPU_ATRACE_BEGIN(__func__);
	vs_dc_res_disable(dev);
	DPU_ATRACE_END(__func__);

	return 0;
}

static int vs_dc_pm_runtime_resume(struct device *dev)
{
	int ret;

	dev_dbg(dev, "%s\n", __func__);

	DPU_ATRACE_BEGIN(__func__);
	ret = vs_dc_res_enable(dev);
	DPU_ATRACE_END(__func__);

	return ret;
}

static const struct dev_pm_ops vs_dc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(vs_dc_pm_runtime_suspend, vs_dc_pm_runtime_resume, NULL)
};
#endif

static ssize_t _get_sscd_regdump(struct vs_dc *dc, char *buffer, ssize_t count,
				 enum dc_hw_reg_bank_type reg_bank)
{
	struct drm_print_iterator iter;
	struct drm_printer p;

	iter.data = buffer;
	iter.start = 0;
	iter.remain = count;

	p = drm_coredump_printer(&iter);

	dc_hw_reg_dump(&dc->hw, &p, reg_bank);

	return count - iter.remain;
}

static ssize_t get_sscd_regdump(struct vs_dc *dc, char **regdump_buf,
				enum dc_hw_reg_bank_type reg_bank)
{
	ssize_t count;

	count = _get_sscd_regdump(dc, NULL, INT_MAX, reg_bank);
	if (count <= 0) {
		*regdump_buf = NULL;
		return count;
	}

	*regdump_buf = vmalloc(count);
	if (!*regdump_buf)
		return -ENOMEM;
	_get_sscd_regdump(dc, *regdump_buf, count, reg_bank);

	return count;
}

static int vs_dc_sscd_reg_payload_destroy(struct device *dev, struct sscd_payload *payload)
{
	int i;

	if (!payload || !payload->cfgs)
		return 0;

	for (i = 0; i < payload->num_cfgs; i++)
		vfree(payload->cfgs[i].virt_addr);

	kfree(payload->cfgs);
	payload->cfgs = NULL;
	payload->num_cfgs = 0;

	return 0;
}

static int vs_dc_sscd_reg_payload_prepare(struct device *dev, struct sscd_payload *payload)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	struct {
		enum dc_hw_reg_bank_type bank;
		const char *name;
	} regs[] = {
		{ DC_HW_REG_BANK_ACTIVE, "dpu act register dump" },
		{ DC_HW_REG_BANK_SHADOW, "dpu shd register dump" },
	};
	int i, ret;

	ret = dc_be_power_get_if_active(dev);
	if (ret <= 0) {
		if (ret < 0)
			dev_err(dev, "Failed to power ON, ret %d\n", ret);
		else
			dev_info(dev, "DPU off, skipping register coredump\n");
		return ret;
	}

	payload->cfgs = kcalloc(ARRAY_SIZE(regs), sizeof(*payload->cfgs), GFP_KERNEL);
	if (!payload->cfgs) {
		dc_be_power_put(dev, false);
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		char *buf;
		ssize_t size;

		size = get_sscd_regdump(dc, &buf, regs[i].bank);
		if (size > 0) {
			display_sscd_configure_phys_hexdump_section(
				&payload->cfgs[payload->num_cfgs++], regs[i].name, buf,
				dc->hw.reg_base_phys, size);
		} else {
			vfree(buf);
			if (size < 0) {
				ret = size;
				goto err_destroy;
			}
		}
	}

	ret = dc_be_power_put(dev, false);
	if (ret < 0)
		dev_err(dev, "Failed to power OFF, ret %d\n", ret);

	if (payload->num_cfgs == 0) {
		kfree(payload->cfgs);
		payload->cfgs = NULL;
	}

	return 0;

err_destroy:
	vs_dc_sscd_reg_payload_destroy(dev, payload);
	dc_be_power_put(dev, false);
	return ret;
}

static int vs_dc_sscd_state_payload_destroy(struct device *dev, struct sscd_payload *payload)
{
	struct vs_dc_sscd_state_priv *priv;

	if (!payload || !payload->cfgs)
		return 0;

	priv = cfgs_to_priv(payload->cfgs);
	vs_drm_recorded_states_destroy(&priv->sh_data);
	kfree(priv);

	payload->cfgs = NULL;
	payload->num_cfgs = 0;

	return 0;
}

static int vs_dc_sscd_state_payload_prepare(struct device *dev, struct sscd_payload *payload)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	struct vs_dc_sscd_state_priv *priv;
	int num_recorded_drm_states, i;

	priv = kzalloc(struct_size(priv, cfgs, VS_RECORD_STATE_MAX), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	num_recorded_drm_states = vs_drm_recorded_states_prepare(&priv->sh_data, dc->drm_dev);
	if (num_recorded_drm_states <= 0) {
		kfree(priv);
		return num_recorded_drm_states;
	}

	for (i = 0; i < num_recorded_drm_states; ++i) {
		char cfg_name[13];

		scnprintf(cfg_name, sizeof(cfg_name), "drm_state-%02d", i);
		display_sscd_configure_log_buffer_section(&priv->cfgs[i], cfg_name,
							  priv->sh_data.buffers[i],
							  priv->sh_data.buffer_sizes[i]);
	}
	payload->cfgs = priv->cfgs;
	payload->num_cfgs = num_recorded_drm_states;

	return 0;
}

int vs_dc_coredump(struct vs_dc *dc, const char *reason)
{
	return trigger_coredump(dc->drm_dev, dc->hw.dev, reason);
}

static void dc_deinit(struct device *dev)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	u32 i = 0;

	for (i = 0; i < dc->hw.info->display_num; i++)
		dc_hw_enable_vblank_irqs(&dc->hw, i, false);

	vs_dpu_sram_pools_deinit();
	dc_hw_deinit(&dc->hw);
}

static int dc_init(struct device *dev)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	int ret;

	dc->first_frame = true;

	dc->hw_reg_dump_options = DC_HW_REG_DUMP_IN_NONE;

	dev_info(dev, "hw_reg_dump_options:%d\n", dc->hw_reg_dump_options);

	ret = dc_hw_init(&dc->hw);
	if (ret) {
		dev_err(dev, "failed to init DC HW\n");
		return ret;
	}

	/*SRAM POOL Init*/
	ret = vs_dpu_sram_pools_init(&dc->hw);
	if (ret) {
		dev_err(dev, "failed to init SRAM POOL\n");
		return ret;
	}

	return ret;
}

static void dc_update_irq_status(struct dc_irq_info *irq_info, int irq_num, int irq_idx)
{
	ssize_t ret_masked, ret_pending;
	bool masked, pending;
	struct irq_desc *irq_desc = irq_to_desc(irq_num);

	if (irq_desc)
		irq_info->irq_depths[irq_idx] = irq_desc->depth;

	ret_masked = irq_get_irqchip_state(irq_num, IRQCHIP_STATE_MASKED, &masked);
	ret_pending = irq_get_irqchip_state(irq_num, IRQCHIP_STATE_PENDING, &pending);

	if (!ret_masked)
		assign_bit(irq_idx, irq_info->irq_masked_status, masked);
	if (!ret_pending)
		assign_bit(irq_idx, irq_info->irq_pending_status, pending);
}

static void dc_update_irq_statuses(struct device *dev, struct dc_irq_info *irq_info)
{
	int i;

	for (i = 0; i < irq_info->irq_num; ++i)
		dc_update_irq_status(irq_info, irq_info->irqs[i], i);

	trace_disp_dc_irq_status(dev, irq_info);
}

void vs_dc_update_irq_statuses(struct vs_dc *dc)
{
	int i;

	for (i = 0; i < DC_FE_NUM; i++) {
		if (dc->fe_dev[i])
			dc_update_irq_statuses(dc->fe_dev[i], &dc->fe_irq_info[i]);
	}
	dc_update_irq_statuses(dc->hw.dev, &dc->dc_irq_info);
}

void vs_dc_handle_interrupts(struct vs_dc *dc)
{
	struct device *dev = dc->hw.dev;
	struct dc_hw_interrupt_status status = { 0 };

	dc_hw_get_interrupt(&dc->hw, &status);

	dev_dbg(dev,
		"%s: te_r=%#x te_f=%#x frm_start=%#x layer_done=%x frm_done=%#x wb_frm_done=%#x\n",
		__func__, status.output_te_rising, status.output_te_falling,
		status.output_frm_start, status.layer_frm_done, status.output_frm_done,
		status.wb_frm_done);

	dev_dbg(dev, "%s: layer_rst_done=%#x fe0_rst_done=%d fe1_rst_done=%d be_reset_done=%d\n",
		__func__, status.layer_reset_done, status.reset_status[FE0_SW_RESET],
		status.reset_status[FE1_SW_RESET], status.reset_status[BE_SW_RESET]);

	if (!bitmap_empty(status.fe0_bus_errors, DC_HW_FE_BUS_ERROR_COUNT)) {
		dev_warn(dev, "%s: fe0_bus_errors: %#lx\n", __func__, status.fe0_bus_errors[0]);
		trace_disp_bus_err_irqs("fe0_bus_errors", status.fe0_bus_errors[0]);
		dc->hw.fe0_has_bus_errors = true;
	}

	if (!bitmap_empty(status.fe1_bus_errors, DC_HW_FE_BUS_ERROR_COUNT)) {
		dev_warn(dev, "%s: fe1_errors: %#lx\n", __func__, status.fe1_bus_errors[0]);
		trace_disp_bus_err_irqs("fe1_bus_errors", status.fe1_bus_errors[0]);
		dc->hw.fe1_has_bus_errors = true;
	}

	if (!bitmap_empty(status.be_bus_errors, DC_HW_BE_BUS_ERROR_COUNT)) {
		dev_warn(dev, "%s: be_bus_errors: %#lx\n", __func__, status.be_bus_errors[0]);
		trace_disp_bus_err_irqs("be_bus_errors", status.be_bus_errors[0]);
		dc->hw.be_has_bus_errors = true;
	}

	trace_disp_frame_irqs(status.output_te_rising, status.output_te_falling,
			      status.output_frm_start, status.layer_frm_done,
			      status.output_frm_done, status.wb_frm_done);
	trace_disp_frame_irqs_overflow(status.of_output_te_rising, status.of_output_te_falling,
				       status.of_output_frm_start, 0x0, status.of_output_frm_done,
				       status.of_wb_frm_done);
	trace_disp_err_irqs(status.pvric_decode_err, status.output_underrun, status.wb_datalost);
	trace_disp_reset_irqs(status.layer_reset_done, status.reset_status[FE0_SW_RESET],
			      status.reset_status[FE1_SW_RESET], status.reset_status[BE_SW_RESET]);

	dc_handle_per_display_interrupts(dc, &status);

	if (status.wb_frm_done || status.wb_datalost)
		dc_handle_wb_interrupts(dc, &status);

	/* save reset status bits */
	if (status.reset_status[FE0_SW_RESET])
		dc->hw.reset_status[FE0_SW_RESET] = status.reset_status[FE0_SW_RESET];

	if (status.reset_status[FE1_SW_RESET])
		dc->hw.reset_status[FE1_SW_RESET] = status.reset_status[FE1_SW_RESET];

	if (status.reset_status[BE_SW_RESET])
		dc->hw.reset_status[BE_SW_RESET] = status.reset_status[BE_SW_RESET];
}

static irqreturn_t dc_isr(int irq, void *data)
{
	struct device *dev = data;
	struct vs_dc *dc = dev_get_drvdata(dev);

	spin_lock(&dc->int_lock);
	vs_dc_handle_interrupts(dc);
	spin_unlock(&dc->int_lock);

	return IRQ_HANDLED;
}

unsigned int vs_dc_get_framedone_timeout(void)
{
	return framedone_timeout;
}

void vs_dc_check_interrupts(struct device *dev)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&dc->int_lock, flags);
	vs_dc_handle_interrupts(dc);
	spin_unlock_irqrestore(&dc->int_lock, flags);
}

int vs_get_wb_frm_done_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_vs_wb_frm_done *args = data;
	struct vs_drm_private *priv = dev->dev_private;
	struct vs_dc *dc = NULL;
	int ret = -EBUSY;

	if (!priv->dc_dev)
		return -EINVAL;

	dc = dev_get_drvdata(priv->dc_dev);
	if (!dc)
		return -EINVAL;

	switch (args->wb_id) {
	case HW_WB_0:
		args->wb_frm_done = dc->hw.wb[HW_WB_0].wb_frm_done;
		if (args->wb_frm_done) {
			dc->hw.wb[HW_WB_0].wb_frm_done = false;
			ret = 0;
		}
		break;
	case HW_WB_1:
		args->wb_frm_done = dc->hw.wb[HW_WB_1].wb_frm_done;
		if (args->wb_frm_done) {
			dc->hw.wb[HW_WB_1].wb_frm_done = false;
			ret = 0;
		}
		break;
	case HW_BLEND_WB:
		args->wb_frm_done = dc->hw.wb[HW_BLEND_WB].wb_frm_done;
		if (args->wb_frm_done) {
			dc->hw.wb[HW_BLEND_WB].wb_frm_done = false;
			ret = 0;
		}
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

int vs_dc_sw_reset(struct drm_device *drm_dev)
{
	struct vs_drm_private *priv = drm_dev->dev_private;
	struct vs_dc *dc = NULL;
	struct device *dev;

	if (!priv->dc_dev)
		return -EINVAL;

	dev = priv->dc_dev;

	dc = dev_get_drvdata(dev);
	if (!dc)
		return -EINVAL;

	if (dc_be_power_get(dev, true) < 0)
		dev_err(dev, "sw_reset BE failed to power ON\n");
	if (dc_fe_power_get(dev, true) < 0)
		dev_err(dev, "sw reset FE failed to power ON\n");
	/* TODO (b/479235001): split sw reset between FE and BE */

	/* reset the hardware */
	dc_hw_do_reset(&dc->hw);

	/* reinitialize the hardware tracking state objects */
	dc_hw_reinit(&dc->hw);

	/* reset the DRM state. */
	drm_mode_config_reset(drm_dev);


	/* TODO (b/479235001): split sw reset between FE and BE */
	if (dc_fe_power_put(dev, true) < 0)
		dev_err(dev, "sw reset FE failed to power OFF\n");
	if (dc_be_power_put(dev, true) < 0)
		dev_err(dev, "sw_reset BE failed to power OFF\n");

	return 0;
}

int vs_sw_reset_ioctl(struct drm_device *drm_dev, void *data, struct drm_file *file_priv)
{
	if (vs_dc_sw_reset(drm_dev))
		return -EINVAL;

	return 0;
}

int vs_get_feature_cap_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_vs_query_feature_cap *args = data;
	struct vs_drm_private *priv = dev->dev_private;
	struct vs_dc *dc = NULL;

	if (!priv->dc_dev)
		return -EINVAL;

	dc = dev_get_drvdata(priv->dc_dev);
	if (!dc)
		return -EINVAL;
	switch (args->type) {
	case VS_FEATURE_CAP_FBC:
		args->cap = dc->hw.info->cap_dec;
		break;
	case VS_FEATURE_CAP_MAX_BLEND_LAYER:
		args->cap = dc->hw.info->max_blend_layer;
		break;
	case VS_FEATURE_CAP_CURSOR_WIDTH:
		args->cap = dev->mode_config.cursor_width;
		break;
	case VS_FEATURE_CAP_CURSOR_HEIGHT:
		args->cap = dev->mode_config.cursor_height;
		break;
	case VS_FEATURE_CAP_LINEAR_YUV_ROTATION:
		args->cap = dc->hw.info->linear_yuv_rotation;
		break;
	case VS_FEATURE_CAP_ANY_RESOLUTION:
		args->cap = dc->hw.info->any_resolution;
		break;
	case VS_FEATURE_CAP_MAX_WIDTH:
		args->cap = dc->hw.wb[0].info->max_width;
		break;
	case VS_FEATURE_CAP_MAX_HEIGHT:
		args->cap = dc->hw.wb[0].info->max_height;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int vs_get_hist_bins_query_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct vs_drm_private *priv = dev->dev_private;
	struct vs_crtc *vs_crtc;
	struct vs_dc *dc;
	struct dc_hw *hw;
	struct dc_hw_display *display;
	struct drm_mode_object *obj;
	struct drm_vs_hist_bins_query *query = data;
	int ret = 0;
	struct vs_gem_node *gem_node;
	unsigned long flags;

	if (!priv->dc_dev)
		return -EINVAL;

	dc = dev_get_drvdata(priv->dc_dev);
	if (!dc)
		return -EINVAL;
	hw = &dc->hw;

	/* check histogram channel */
	if (query->idx >= VS_HIST_IDX_COUNT)
		return -EINVAL;

	/*
	 * userspace provides crtc object id
	 * therefore, we need to discover matching vs_crtc->id
	 */
	obj = drm_mode_object_find(dev, file_priv, query->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		dev_err(hw->dev, "failed to find crtc object\n");
		return -ENOENT;
	}

	vs_crtc = to_vs_crtc(obj_to_crtc(obj));
	if (vs_crtc->id > DC_DISPLAY_NUM) {
		drm_mode_object_put(obj);
		dev_err(hw->dev, "invalid crtc object\n");
		return -ENOENT;
	}

	display = &hw->display[vs_crtc->id];
	drm_mode_object_put(obj);

	/* is it supported ? */
	if (!display->info || !display->info->histogram) {
		dev_err(hw->dev, "unsupported histogram\n");
		return -ENXIO;
	}

	/* validate idx */
	if (query->idx > VS_HIST_IDX_COUNT) {
		dev_err(hw->dev, "invalid histogram channel\n");
		return -EFAULT;
	}

	/* handle regular histogram channels */
	if (query->idx < VS_HIST_CHAN_IDX_COUNT) {
		struct dc_hw_hist_chan *hw_hist_chan = &display->hw_hist_chan[query->idx];
		const struct drm_vs_hist_chan *hist_config = &hw_hist_chan->drm_config;

		/* enough space ? */
		if (query->hist_bins_size != (sizeof(struct drm_vs_hist_chan_bins))) {
			dev_err(hw->dev, "invalid buffer length\n");
			return -ENOMEM;
		}

		spin_lock_irqsave(&hw->histogram_slock, flags);

		/* check if enabled or property changed */
		if (!hw_hist_chan->enable || hw_hist_chan->changed) {
			spin_unlock_irqrestore(&hw->histogram_slock, flags);
			return -ENOTCONN;
		}

		/* check if histogram_data is available */
		gem_node = hw_hist_chan->gem_node[VS_HIST_STAGE_DONE];
		if (!gem_node) {
			spin_unlock_irqrestore(&hw->histogram_slock, flags);
			return -ENODATA;
		}

		/* increase gem_node use count */
		hw_hist_chan->gem_node[VS_HIST_STAGE_USER] = gem_node;
		vs_gem_pool_node_acquire(&vs_crtc->hist_chan_gem_pool[query->idx], gem_node);

		spin_unlock_irqrestore(&hw->histogram_slock, flags);

		/* copy data to user */
		query->user_data = hist_config->user_data;
		ret = copy_to_user(u64_to_user_ptr(query->hist_bins_ptr),
				   (const void *)gem_node->vaddr, query->hist_bins_size);

		/* release gem_node to mark completion of user request handling */
		spin_lock_irqsave(&hw->histogram_slock, flags);
		/* simply decrease ref count */
		hw_hist_chan->gem_node[VS_HIST_STAGE_USER] = NULL;
		vs_gem_pool_node_release(&vs_crtc->hist_chan_gem_pool[query->idx], gem_node);
		spin_unlock_irqrestore(&hw->histogram_slock, flags);
	} else { /* handle histogram rgb */
		struct dc_hw_hist_rgb *hw_hist_rgb = &display->hw_hist_rgb;

		/* enough space ? */
		if (query->hist_bins_size != (sizeof(struct drm_vs_hist_rgb_bins))) {
			dev_err(hw->dev, "invalid buffer length\n");
			return -ENOMEM;
		}

		spin_lock_irqsave(&hw->histogram_slock, flags);

		/* check if enabled */
		if (!hw_hist_rgb->enable) {
			spin_unlock_irqrestore(&hw->histogram_slock, flags);
			return -ENOTCONN;
		}

		/* mark handling user request */
		/* check if histogram_data is available */
		gem_node = hw_hist_rgb->gem_node[VS_HIST_STAGE_DONE];
		if (!gem_node) {
			spin_unlock_irqrestore(&hw->histogram_slock, flags);
			return -ENODATA;
		}

		/* increase gem_node use count */
		hw_hist_rgb->gem_node[VS_HIST_STAGE_USER] = gem_node;
		vs_gem_pool_node_acquire(&vs_crtc->hist_rgb_gem_pool, gem_node);

		spin_unlock_irqrestore(&hw->histogram_slock, flags);

		/* copy data to user */
		ret = copy_to_user(u64_to_user_ptr(query->hist_bins_ptr),
				   (const void *)gem_node->vaddr, query->hist_bins_size);

		/* release gem_node to mark completion of user request handling */
		spin_lock_irqsave(&hw->histogram_slock, flags);
		/* simply decrease ref count */
		hw_hist_rgb->gem_node[VS_HIST_STAGE_USER] = NULL;
		vs_gem_pool_node_release(&vs_crtc->hist_rgb_gem_pool, gem_node);
		spin_unlock_irqrestore(&hw->histogram_slock, flags);
	}

	return ret;
}

int vs_get_hw_cap_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_vs_query_hw_cap *args = data;
	struct vs_drm_private *priv = dev->dev_private;
	struct vs_dc *dc = NULL;

	if (!priv->dc_dev)
		return -EINVAL;

	dc = dev_get_drvdata(priv->dc_dev);
	if (!dc)
		return -EINVAL;

	return vs_dc_get_hw_cap(dc->hw.info, args->type, &args->cap);
}

int vs_get_ltm_hist_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	int ret;
	struct drm_vs_ltm_histogram_data *args = data;
	struct vs_drm_private *priv = dev->dev_private;
	struct vs_dc *dc = NULL;
	struct drm_mode_object *obj;
	struct vs_crtc *vs_crtc;
	const struct vs_display_info *display_info;

	if (!args->hist_ptr)
		return -ENOMEM;

	if (!priv->dc_dev)
		return -EINVAL;

	dc = dev_get_drvdata(priv->dc_dev);
	if (!dc)
		return -EINVAL;

	obj = drm_mode_object_find(dev, file_priv, args->crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!obj) {
		dev_err(dc->hw.dev, "failed to find crtc object for %u\n", args->crtc_id);
		return -ENOENT;
	}

	vs_crtc = to_vs_crtc(obj_to_crtc(obj));
	drm_mode_object_put(obj);

	if (!vs_crtc || vs_crtc->id >= DC_DISPLAY_NUM) {
		dev_err(dc->hw.dev, "invalid crtc object %p id %u\n", vs_crtc,
			(vs_crtc ? vs_crtc->id : 0));
		return -ENOENT;
	}

	display_info = &dc->hw.info->displays[vs_crtc->id];
	if (!display_info->ltm && !display_info->gtm)
		return -ENODEV;

	ret = dc_be_power_get_if_active(priv->dc_dev);
	if (ret > 0) {
		ret = vs_crtc_get_ltm_hist(file_priv, vs_crtc, &dc->hw, args);
		dc_be_power_put(priv->dc_dev, true);
	}

	return ret;
}

static ssize_t early_wakeup_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t early_wakeup_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t len)
{
	bool wakeup;
	int ret;

	if (!dev || !buf || !len) {
		pr_err("invalid parameter\n");
		return -EINVAL;
	}

	if (kstrtobool(buf, &wakeup) < 0)
		return -EINVAL;

	if (wakeup) {
		DPU_ATRACE_BEGIN(__func__);
		ret = pm_request_resume(dev);
		if (!ret) {
			pm_runtime_mark_last_busy(dev);
			pm_request_autosuspend(dev);
		}
		sysfs_notify(&dev->kobj, NULL, "early_wakeup");
		DPU_ATRACE_END(__func__);
	}

	return len;
}
static DEVICE_ATTR_RW(early_wakeup);

static int dc_init_sscd(struct vs_dc *dc, struct device *dev)
{
	int ret = display_sscd_device_initialize(dev, &dc->disp_sscd, "dpu",
						 SSCD_GET_DRIVER_VERSION(), NULL);

	if (ret)
		dev_err(dev, "Error registering sscd device(%d)\n", ret);

	return ret;
}

static int dc_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm_dev = data;
	struct vs_drm_private *priv = drm_dev->dev_private;
	struct vs_dc *dc = dev_get_drvdata(dev);
	const struct vs_dc_info *dc_info;
	int ret;

	if (!drm_dev || !dc) {
		dev_err(dev, "devices are not created.\n");
		return -ENODEV;
	}

	ret = dc_be_power_get(dev, true);
	if (ret < 0)
		dev_err(dev, "dc_bind BE failed to power ON\n");
	ret = dc_fe_power_get(dev, true);
	if (ret < 0)
		dev_err(dev, "dc_bind FE failed to power ON\n");

	dc_component_enable_irqs(dev, &dc->dc_irq_info);

	ret = dc_init(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize DC hardware.\n");
		goto err_init_dc;
	}

	ret = vs_drm_iommu_attach_device(drm_dev, dev);
	if (ret < 0) {
		dev_err(dev, "Failed to attached iommu device.\n");
		goto err_clean_dc;
	}

	dc_info = dc->hw.info;

	drm_dev->mode_config.min_width = 0xffff;
	drm_dev->mode_config.min_height = 0xffff;
	drm_dev->mode_config.max_width = 0x0;
	drm_dev->mode_config.max_height = 0x0;

	priv->dc_dev = dev;
	dc->drm_dev = drm_dev;

	vs_drm_update_alignment(drm_dev, dc_info->pitch_alignment, dc_info->addr_alignment);

	ret = dc_fe_power_put(dev, true);
	if (ret < 0)
		dev_err(dev, "dc_bind FE failed to power OFF\n");
	ret = dc_be_power_put(dev, true);
	if (ret < 0)
		dev_err(dev, "dc_bind BE failed to power OFF\n");

	device_create_file(dev, &dev_attr_early_wakeup);

	if (is_coredump_enabled()) {
		ret = dc_init_sscd(dc, dev);
		if (ret < 0)
			goto err_clean_dc;

		/* connect sscd register dump callbacks */
		priv->dpu_reg_coredump_funcs.sscd_payload_prepare = vs_dc_sscd_reg_payload_prepare;
		priv->dpu_reg_coredump_funcs.sscd_payload_destroy = vs_dc_sscd_reg_payload_destroy;

		/* connect sscd drm state history callbacks */
		priv->dpu_state_coredump_funcs.sscd_payload_prepare =
			vs_dc_sscd_state_payload_prepare;
		priv->dpu_state_coredump_funcs.sscd_payload_destroy =
			vs_dc_sscd_state_payload_destroy;
	}

	return 0;

err_clean_dc:
	dc_deinit(dev);
err_init_dc:
	if (dc_be_power_put(dev, true) < 0)
		dev_err(dev, "%s: failed to power OFF during error cleanup\n", __func__);
	return ret;
}

static void dc_unbind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm_dev = data;
	struct vs_dc *dc = dev_get_drvdata(dev);
	int ret;

	dc_component_disable_irqs(dev, &dc->dc_irq_info);

	if (dc->disp_sscd) {
		display_sscd_device_free(dc->disp_sscd);
		dc->disp_sscd = NULL;
	}

	device_remove_file(dev, &dev_attr_early_wakeup);

	ret = dc_be_power_get(dev, true);
	if (ret >= 0) {
		dc_deinit(dev);
		ret = dc_be_power_put(dev, false);
		if (ret < 0)
			dev_err(dev, "dc_unbind BE failed to power OFF\n");
	} else {
		dev_err(dev, "dc_unbind BE failed to power ON for deinit; skipping deinit\n");
	}

	vs_drm_iommu_detach_device(drm_dev, dev);
}

const struct component_ops dc_component_ops = {
	.bind = dc_bind,
	.unbind = dc_unbind,
};

static const struct of_device_id dc_driver_dt_match[] = {
	{
		.compatible = "verisilicon,dc9x00",
	},
	{},
};
MODULE_DEVICE_TABLE(of, dc_driver_dt_match);

static void detach_power_domain(struct device *dev, struct vs_dc *dc, int end_idx)
{
	int i;

	for (i = end_idx - 1; i >= 0; i--) {
		device_link_del(dc->pds[i].devlink);
		if (dc->pds[i].dev && !IS_ERR(dc->pds[i].dev))
			dev_pm_domain_detach(dc->pds[i].dev, true);
	}
}

static int attach_power_domain(struct device *dev, struct vs_dc *dc)
{
	int i;
	int ret = 0;

	dc->num_pds = of_property_count_strings(dev->of_node, "power-domain-names");
	if (dc->num_pds == -EINVAL) {
		// It's possible to have no power-domain.
		dc->num_pds = 0;
		return 0;
	}
	if (dc->num_pds <= 0) {
		dev_err(dev, "failed to read power-domain-names property\n");
		return -EINVAL;
	}
	dc->pds = devm_kmalloc_array(dev, dc->num_pds, sizeof(*dc->pds), GFP_KERNEL);
	if (!dc->pds)
		return -ENOMEM;

	for (i = 0; i < dc->num_pds; i++) {
		dc->pds[i].dev = dev_pm_domain_attach_by_id(dev, i);
		if (IS_ERR_OR_NULL(dc->pds[i].dev)) {
			dev_err(dev, "failed to attach power domain at index %d\n", i);
			ret = (dc->pds[i].dev) ? PTR_ERR(dc->pds[i].dev) : -EINVAL;
			goto clean_up;
		}

		dc->pds[i].devlink = device_link_add(dev, dc->pds[i].dev,
						     DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);
		if (!dc->pds[i].devlink) {
			dev_err(dev, "failed to create a devlink to the pd at index %d\n", i);
			dev_pm_domain_detach(dc->pds[i].dev, true);
			ret = -EINVAL;
			goto clean_up;
		}
	}

	return ret;

clean_up:
	detach_power_domain(dev, dc, i);
	return ret;
}

int dc_component_parse_irqs(struct device *dev, struct dc_irq_info *irq_info,
			    irq_handler_t handler)
{
	int ret = 0;
	int i;

	/* count irqs */
	irq_info->irq_num = platform_irq_count(to_platform_device(dev));
	if (irq_info->irq_num <= 0) {
		ret = irq_info->irq_num;
		irq_info->irq_num = 0;
		goto out;
	}

	/* allocate space for them */
	irq_info->irqs =
		devm_kmalloc_array(dev, irq_info->irq_num, sizeof(*irq_info->irqs), GFP_KERNEL);
	if (!irq_info->irqs) {
		ret = -ENOMEM;
		goto out;
	}
	irq_info->irq_depths = devm_kmalloc_array(
		dev, irq_info->irq_num, sizeof(*irq_info->irq_depths), GFP_KERNEL | __GFP_ZERO);
	if (!irq_info->irq_depths) {
		ret = -ENOMEM;
		goto out;
	}

	/* request irqs and assign handler */
	for (i = 0; i < irq_info->irq_num; ++i) {
		int irq = platform_get_irq(to_platform_device(dev), i);

		ret = devm_request_irq(dev, irq, handler, IRQF_NO_AUTOEN, dev_name(dev), dev);
		if (ret < 0) {
			dev_err(dev, "Failed to install irq %u (idx:%d)\n", irq, i);
			goto out;
		}
		irq_info->irqs[i] = irq;
	}

out:
	return ret;
}

static int dc_parse_dt(struct device *dev, struct vs_dc *dc)
{
	if (dc->path) {
		of_property_read_u32(dev->of_node, "min-rd-avg-bw",
				     &dc->min_qos_config.rd_avg_bw_mbps);
		of_property_read_u32(dev->of_node, "min-rd-peak-bw",
				     &dc->min_qos_config.rd_peak_bw_mbps);
		of_property_read_u32(dev->of_node, "min-rd-rt-bw",
				     &dc->min_qos_config.rd_rt_bw_mbps);
		of_property_read_u32(dev->of_node, "min-wr-avg-bw",
				     &dc->min_qos_config.wr_avg_bw_mbps);
		of_property_read_u32(dev->of_node, "min-wr-peak-bw",
				     &dc->min_qos_config.wr_peak_bw_mbps);
		of_property_read_u32(dev->of_node, "min-wr-rt-bw",
				     &dc->min_qos_config.wr_rt_bw_mbps);
	}

	of_property_read_u32(dev->of_node, "min-core-clk",
			     &dc->min_qos_config.core_clk);

	framedone_timeout = FRAMEDONE_TIMEOUT;
	if (of_property_present(dev->of_node, "in_emulation")) {
		framedone_timeout *= 100;
		dev_info(dev, "%s: in_emulation. framedone_timeout=%u\n", __func__,
			 framedone_timeout);
	}

	if (of_property_read_u32(dev->of_node, "boost-fab-freq", &dc->boost_fab_freq))
		dc->boost_fab_freq = 0;
	if (of_property_read_u32(dev->of_node, "boost-core-freq", &dc->boost_core_freq))
		dc->boost_core_freq = 0;

	return 0;
}

static int dc_get_trusty_device(struct device *dev, struct vs_dc *dc)
{
	struct device_node *np;

	np = of_parse_phandle(dev->of_node, "tzprot-device", 0);
	if (!np) {
		dev_warn(dev, "tzprot-device phandle not found in dts\n");
		dc->tzprot_pdev = NULL;
		return 0;
	}
	dc->tzprot_pdev = of_find_device_by_node(np);
	of_node_put(np);

	if (!dc->tzprot_pdev) {
		dev_err(dev, "tzprot-device phandle doesn't refer to a device\n");
		return -EINVAL;
	}

	return 0;
}

static int dc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_link *link = NULL;
	struct vs_dc *dc;
	int ret;
	struct resource *resource = NULL;
#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
	const char *fab_str;
#endif

	dc = devm_kzalloc(dev, sizeof(*dc), GFP_KERNEL);
	if (!dc)
		return -ENOMEM;

	spin_lock_init(&dc->int_lock);
	mutex_init(&dc->dc_lock);
	mutex_init(&dc->dc_qos_lock);

	ret = attach_power_domain(dev, dc);
	if (ret < 0)
		return ret;

	dc->hw.reg_base = devm_platform_get_and_ioremap_resource(pdev, 0, &resource);
	if (IS_ERR(dc->hw.reg_base)) {
		dev_err(dev, "failed to ioremap dpu registers\n");
		ret = PTR_ERR(dc->hw.reg_base);
		goto detach_pd;
	}
	dc->hw.reg_size = resource != NULL ? resource_size(resource) : 0;
	dc->hw.reg_dump_offset = 0;
	dc->hw.reg_dump_size = dc->hw.reg_size;
	dc->hw.dev = dev;
	dc->hw.gpcsr_reg_base = devm_platform_ioremap_resource_byname(pdev, "dpu-gpcsr");
	if (IS_ERR(dc->hw.gpcsr_reg_base))
		dev_warn(dev, "failed to map dpu gpcsr resource, continue without it\n");

	mutex_init(&dc->hw.secure_lock);
	spin_lock_init(&dc->hw.histogram_slock);
	spin_lock_init(&dc->hw.be_irq_slock);
	spin_lock_init(&dc->hw.output_mux_slock);

	ret = dc_component_parse_irqs(dev, &dc->dc_irq_info, dc_isr);
	if (ret)
		goto detach_pd;

#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
	dc->core_devfreq = gs_perf_domain_find_devfreq("dpu", GS_RECOMMENDED_DEVFREQ);
	if (IS_ERR(dc->core_devfreq)) {
		ret = PTR_ERR(dc->core_devfreq);
		if (ret == -ENODEV)
			dev_err(dev, "failed to get core_devfreq source, ret=%d\n", ret);
		goto detach_pd;
	}
#else /* CONFIG_GS_PERF_DOMAIN */
	if (of_property_present(dev->of_node, "devfreq")) {
		dc->core_devfreq = devfreq_get_devfreq_by_phandle(dev, "devfreq", 0);
	} else {
		dev_warn(dev, "core_devfreq not presented in dts, skip it\n");
		dc->core_devfreq = NULL;
	}
	if (IS_ERR(dc->core_devfreq)) {
		ret = PTR_ERR(dc->core_devfreq);
		dev_err(dev, "failed to get core_devfreq source, ret=%d\n", ret);
		if (ret == -ENODEV)
			ret = -EPROBE_DEFER;
		goto detach_pd;
	}
#endif /* CONFIG_GS_PERF_DOMAIN */

	if (dc->core_devfreq) {
		link = device_link_add(dev, dc->core_devfreq->dev.parent,
				       DL_FLAG_AUTOREMOVE_CONSUMER | DL_FLAG_PM_RUNTIME);
		if (!link) {
			dev_err(dev, "failed to add devlink to the devfreq dev\n");
			ret = -EINVAL;
			goto detach_pd;
		}
	}

	dc->path = google_devm_of_icc_get(dev, "sswrp-dpu");
	if (IS_ERR(dc->path)) {
		ret = PTR_ERR(dc->path);
		dev_err(dev, "failed to get icc path: %d\n", ret);
		goto detach_pd;
	}

	/* optional configuration, it's for boosting fabric and core frequency */
#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
	ret = of_property_read_string(dev->of_node, "fab-bus-name", &fab_str);
	if (ret == 0) {
		dc->boost_fab_devfreq =
			gs_perf_domain_find_devfreq(fab_str, GS_RECOMMENDED_DEVFREQ);
		if (IS_ERR(dc->boost_fab_devfreq)) {
			ret = PTR_ERR(dc->boost_fab_devfreq);
			if (ret == -ENODEV)
				dev_err(dev, "failed to get fab_devfreq source, ret=%d\n", ret);
			goto detach_pd;
		}
	}

	dc->boost_core_devfreq = gs_perf_domain_find_devfreq("dpu", GS_RECOMMENDED_DEVFREQ);
	if (IS_ERR(dc->boost_core_devfreq)) {
		ret = PTR_ERR(dc->boost_core_devfreq);
		if (ret == -ENODEV)
			dev_err(dev, "failed to get core_devfreq source, ret=%d\n", ret);
		goto detach_pd;
	}
#else
	if (of_property_present(dev->of_node, "fabrtfreq")) {
		dc->boost_fab_devfreq = devfreq_get_devfreq_by_phandle(dev, "fabrtfreq", 0);
	} else {
		dev_dbg(dev, "fab_devfreq not presented in dts, skip it\n");
		dc->boost_fab_devfreq = NULL;
	}
	if (IS_ERR(dc->boost_fab_devfreq)) {
		ret = PTR_ERR(dc->boost_fab_devfreq);
		dev_err(dev, "failed to get fab_devfreq source for boosting, ret=%d\n", ret);
		if (ret == -ENODEV)
			ret = -EPROBE_DEFER;
		goto detach_pd;
	}

	if (of_property_present(dev->of_node, "devfreq")) {
		dc->boost_core_devfreq = devfreq_get_devfreq_by_phandle(dev, "devfreq", 0);
	} else {
		dev_dbg(dev, "core_devfreq not presented in dts, skip it\n");
		dc->boost_core_devfreq = NULL;
	}
	if (IS_ERR(dc->boost_core_devfreq)) {
		ret = PTR_ERR(dc->boost_core_devfreq);
		dev_err(dev, "failed to get core_devfreq source for boosting, ret=%d\n", ret);
		if (ret == -ENODEV)
			ret = -EPROBE_DEFER;
		goto detach_pd;
	}
#endif

	ret = dc_parse_dt(dev, dc);
	if (ret) {
		dev_err(dev, "failed to parse device-tree\n");
		goto detach_pd;
	}

	ret = dc_get_trusty_device(dev, dc);
	if (ret) {
		dev_err(dev, "error getting the trust zone device driver\n");
		goto detach_pd;
	}

	ret = dc_init_debugfs(dc);
	if (ret) {
		dev_err(dev, "failed to init debugfs\n");
		goto detach_pd;
	}
	if (is_coredump_enabled())
		dc->hw.reg_base_phys = resource->start;

	ret = dc_init_trace(dev);
	if (ret)
		dev_err(dev, "failed to enable traces\n");

	dev_set_drvdata(dev, dc);

	pm_runtime_set_autosuspend_delay(dev, VS_DC_AUTOSUPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		dev_err(dev, "runtime pm enabled but failed to add disable action: %d\n", ret);

	ret = component_add(dev, &dc_component_ops);
	if (ret) {
		dev_err(dev, "failed to add components: %d\n", ret);
		goto detach_pd;
	}

	ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to populate platform child devices: %d\n", ret);
		goto remove_components;
	}

	return ret;

remove_components:
	component_del(dev, &dc_component_ops);
detach_pd:
	detach_power_domain(dev, dc, dc->num_pds);
	return ret;
}

static void dc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vs_dc *dc = platform_get_drvdata(pdev);

	dc_deinit_debugfs(dc);
	component_del(dev, &dc_component_ops);
	detach_power_domain(dev, dc, dc->num_pds);

	dev_set_drvdata(dev, NULL);
}

struct platform_driver dc_platform_driver = {
	.probe = dc_probe,
	.remove = dc_remove,
	.driver = {
		.name = "vs-dc",
		.of_match_table = of_match_ptr(dc_driver_dt_match),
#if IS_ENABLED(CONFIG_PM_SLEEP)
		.pm = &vs_dc_pm_ops,
#endif
	},
};

MODULE_DESCRIPTION("VeriSilicon DC Driver");
MODULE_LICENSE("GPL v2");
