/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Google LLC
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <drm/drm_bridge.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>

#include <drm/bridge/dw_mipi_dsi2h.h>
#include <drm/sscd/gs_sscd_dpu.h>
#include <trace/dpu_trace.h>

#include "vs_dc.h"
#include "vs_drv.h"
#include "vs_sscd.h"

static int vs_drm_get_dsi2h_count(const struct drm_device *drm_dev)
{
	struct drm_encoder *encoder;
	struct drm_bridge *bridge;
	int dsi_count = 0;

	drm_for_each_encoder(encoder, drm_dev) {
		drm_for_each_bridge_in_chain(encoder, bridge) {
			if (!is_dw_mipi_dsi2h_bridge(bridge))
				continue;

			dsi_count++;
		}
	}

	return dsi_count;
}

static void _prepare_subsystem_coredump_dpu(struct drm_device *drm_dev,
					    struct sscd_payload *dpu_reg,
					    struct sscd_payload *dpu_state)
{
	struct vs_drm_private *priv = drm_dev->dev_private;
	struct device *dc_dev = priv->dc_dev;

	DPU_ATRACE_BEGIN("sscd_prepare_dpu");

	if (priv->dpu_reg_coredump_funcs.sscd_payload_prepare)
		priv->dpu_reg_coredump_funcs.sscd_payload_prepare(dc_dev, dpu_reg);

	if (priv->dpu_state_coredump_funcs.sscd_payload_prepare)
		priv->dpu_state_coredump_funcs.sscd_payload_prepare(dc_dev, dpu_state);

	DPU_ATRACE_END("sscd_prepare_dpu");
}

static int _prepare_subsystem_coredump_dsi(struct drm_device *drm_dev, struct device **dsi_devs,
					   struct sscd_payload *dsi_payloads, int max_dsi_cnt)
{
	struct vs_drm_private *priv = drm_dev->dev_private;
	struct drm_encoder *encoder;
	struct drm_bridge *bridge;
	int dsi_count = 0;

	drm_for_each_encoder(encoder, drm_dev) {
		drm_for_each_bridge_in_chain(encoder, bridge) {
			struct device *dsi_dev;
			bool duplicate = false;
			int i;

			if (!is_dw_mipi_dsi2h_bridge(bridge) || dsi_count >= max_dsi_cnt)
				continue;

			dsi_dev = dw_mipi_dsi2h_get_device(bridge);

			for (i = 0; i < dsi_count; i++) {
				if (dsi_devs[i] == dsi_dev) {
					duplicate = true;
					break;
				}
			}
			if (duplicate)
				continue;

			dsi_devs[dsi_count] = dsi_dev;
			if (priv->dsi_coredump_funcs.sscd_payload_prepare) {
				DPU_ATRACE_BEGIN("sscd_prepare_dsi%d", dsi_count);
				priv->dsi_coredump_funcs.sscd_payload_prepare(
					dsi_dev, &dsi_payloads[dsi_count]);
				DPU_ATRACE_END("sscd_prepare_dsi%d", dsi_count);
			}
			dsi_count++;
		}
	}

	return dsi_count;
}

static void _report_subsystem_coredump_dpu(struct display_sscd_info *disp_sscd,
					   struct sscd_payload *dpu_reg,
					   struct sscd_payload *dpu_state)
{
	int i;

	for (i = 0; i < dpu_reg->num_cfgs; i++)
		display_sscd_push_section(disp_sscd, &dpu_reg->cfgs[i]);

	for (i = 0; i < dpu_state->num_cfgs; i++)
		display_sscd_push_section(disp_sscd, &dpu_state->cfgs[i]);
}

static void _report_subsystem_coredump_dsi(struct display_sscd_info *disp_sscd,
					   struct sscd_payload *dsi_payloads, int dsi_count)
{
	int i, d;

	for (d = 0; d < dsi_count; d++) {
		for (i = 0; i < dsi_payloads[d].num_cfgs; i++)
			display_sscd_push_section(disp_sscd, &dsi_payloads[d].cfgs[i]);
	}
}

static void _cleanup_subsystem_coredump_dpu(struct drm_device *drm_dev,
					    struct sscd_payload *dpu_reg,
					    struct sscd_payload *dpu_state)
{
	struct vs_drm_private *priv = drm_dev->dev_private;
	struct vs_dc *dc = dev_get_drvdata(priv->dc_dev);
	int i;

	DPU_ATRACE_BEGIN("sscd_cleanup_dpu");

	for (i = dpu_state->num_cfgs - 1; i >= 0; i--)
		display_sscd_pop_section(dc->disp_sscd);

	if (priv->dpu_state_coredump_funcs.sscd_payload_destroy)
		priv->dpu_state_coredump_funcs.sscd_payload_destroy(priv->dc_dev, dpu_state);

	for (i = dpu_reg->num_cfgs - 1; i >= 0; i--)
		display_sscd_pop_section(dc->disp_sscd);

	if (priv->dpu_reg_coredump_funcs.sscd_payload_destroy)
		priv->dpu_reg_coredump_funcs.sscd_payload_destroy(priv->dc_dev, dpu_reg);

	DPU_ATRACE_END("sscd_cleanup_dpu");
}

static void _cleanup_subsystem_coredump_dsi(struct drm_device *drm_dev, struct device **dsi_devs,
					    struct sscd_payload *dsi_payloads, int dsi_count)
{
	struct vs_drm_private *priv = drm_dev->dev_private;
	struct vs_dc *dc = dev_get_drvdata(priv->dc_dev);
	int i, d;

	for (d = dsi_count - 1; d >= 0; d--) {
		for (i = dsi_payloads[d].num_cfgs - 1; i >= 0; i--)
			display_sscd_pop_section(dc->disp_sscd);

		if (priv->dsi_coredump_funcs.sscd_payload_destroy) {
			DPU_ATRACE_BEGIN("sscd_cleanup_dsi%d", d);
			priv->dsi_coredump_funcs.sscd_payload_destroy(dsi_devs[d],
								      &dsi_payloads[d]);
			DPU_ATRACE_END("sscd_cleanup_dsi%d", d);
		}
	}
}

int trigger_coredump(struct drm_device *drm_dev, struct device *calling_dev, const char *reason)
{
	struct device *dev = drm_dev->dev;
	struct vs_drm_private *priv = drm_dev->dev_private;
	struct vs_dc *dc = dev_get_drvdata(priv->dc_dev);
	struct sscd_payload dpu_reg_payload = {};
	struct sscd_payload dpu_state_payload = {};
	struct device **dsi_devs;
	struct sscd_payload *dsi_payloads;
	int max_dsi_cnt = vs_drm_get_dsi2h_count(drm_dev);
	int dsi_count = 0, ret = 0;

	dev_info(dev, "coordinated sscd triggered; dev:%s reason:%s\n", dev_name(calling_dev),
		 reason);

	if (!dc || !dc->disp_sscd) {
		dev_err(dev, "SSCD coordinator not available\n");
		ret = -ENODEV;
		goto out;
	}

	dsi_devs = kcalloc(max_dsi_cnt, sizeof(*dsi_devs), GFP_KERNEL);
	if (!dsi_devs) {
		ret = -ENOMEM;
		goto out;
	}

	dsi_payloads = kcalloc(max_dsi_cnt, sizeof(*dsi_payloads), GFP_KERNEL);
	if (!dsi_payloads) {
		ret = -ENOMEM;
		goto free_devs;
	}

	_prepare_subsystem_coredump_dpu(drm_dev, &dpu_reg_payload, &dpu_state_payload);
	dsi_count = _prepare_subsystem_coredump_dsi(drm_dev, dsi_devs, dsi_payloads, max_dsi_cnt);

	DPU_ATRACE_BEGIN("sscd_report");
	_report_subsystem_coredump_dpu(dc->disp_sscd, &dpu_reg_payload, &dpu_state_payload);
	_report_subsystem_coredump_dsi(dc->disp_sscd, dsi_payloads, dsi_count);

	ret = display_sscd_report(dc->disp_sscd, reason);
	if (ret < 0)
		dev_warn(dev, "subsystem coredump report failed: %d\n", ret);
	DPU_ATRACE_END("sscd_report");

	_cleanup_subsystem_coredump_dsi(drm_dev, dsi_devs, dsi_payloads, dsi_count);
	_cleanup_subsystem_coredump_dpu(drm_dev, &dpu_reg_payload, &dpu_state_payload);

	kfree(dsi_payloads);
free_devs:
	kfree(dsi_devs);
out:
	return ret;
}
