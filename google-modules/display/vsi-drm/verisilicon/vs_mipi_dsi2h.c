// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <drm/bridge/dw_mipi_dsi2h.h>
#include <drm/drm_encoder.h>
#include <drm/drm_managed.h>
#include <drm/drm_file.h>
#include <drm/drm_of.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>

#include <linux/component.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/media-bus-format.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <gs_drm/gs_drm_connector.h>

#include "vs_dc_info.h"
#include "vs_crtc.h"
#include "vs_drv.h"

#define DTS_HS_EN_ELEM_SIZE 6

#define MAX_DSI_CNT 2
#ifndef DISPLAY_INDEX_PRIMARY
#define DISPLAY_INDEX_PRIMARY 0
#endif
#ifndef DISPLAY_INDEX_SECONDARY
#define DISPLAY_INDEX_SECONDARY 1
#endif
#define BRIDGE_PORT 1
#define BRIDGE_ENDPOINT 0

struct vs_mipi_dsi2h {
	struct device *dev;
	struct drm_encoder encoder;
	/* initialization data for dw_mipi_dsi */
	struct dw_mipi_dsi2h_plat_data pdata;

	void *dw_mipi_dsi_handle;
	/* attached dsi device */
	struct mipi_dsi_device *dsi_device;
	struct dentry *debugfs_root;
	enum dc_hw_cmd_mode debug_dsi_mode;

	/* track the first frame after handoff */
	bool first_frame;
};

static struct vs_mipi_dsi2h *dsi_drvdata[MAX_DSI_CNT];

static int read_connector_panel_index(struct device_node *np, int *out_value)
{
	struct device_node *remote;

	/* read panel index in remote dsi connector node from dsi2h node */
	if (!of_graph_is_present(np))
		return -EINVAL;

	remote = of_graph_get_remote_node(np, BRIDGE_PORT, BRIDGE_ENDPOINT);
	if (!remote)
		return -EINVAL;

	if (of_property_read_u32(remote, "google,device-index", out_value))
		return -EINVAL;

	return 0;
}

static inline struct vs_mipi_dsi2h *encoder_to_dsi2h(struct drm_encoder *encoder)
{
	return container_of(encoder, struct vs_mipi_dsi2h, encoder);
}

static int dw_mipi_dsi2h_of_parse_pdata(struct device *dev, struct dw_mipi_dsi2h_plat_data *pdata)
{
	static_assert(DTS_HS_EN_ELEM_SIZE == 6);
	struct device_node *np = dev->of_node;
	u32 hs_en[DTS_HS_EN_ELEM_SIZE];

	if (!pdata)
		return -ENOMEM;

	if (of_property_read_u32(np, "verisilicon,mux-id", &pdata->mux_id)) {
		dev_err(dev, "mux_id not set\n");
		return -EINVAL;
	}
	if (of_property_read_u32(np, "max_data_lanes", &pdata->max_data_lanes)) {
		dev_err(dev, "max_data_lanes not set\n");
		return -EINVAL;
	}
	if (of_property_read_u32(np, "is_cphy", &pdata->is_cphy)) {
		/* default dphy */
		pdata->is_cphy = 0;
	}
	if (of_property_read_u32(np, "clk_type", &pdata->clk_type)) {
		dev_err(dev, "clk_type not set\n");
		return -EINVAL;
	}
	if (of_property_read_u32(np, "ppi_width", &pdata->ppi_width)) {
		dev_err(dev, "ppi_width not set\n");
		return -EINVAL;
	}
	if (of_property_read_u32(np, "high_speed", &pdata->high_speed)) {
		dev_err(dev, "high_speed not set\n");
		return -EINVAL;
	}
	if (of_property_read_u32(np, "bta", &pdata->bta)) {
		dev_err(dev, "bta not set\n");
		return -EINVAL;
	}
	if (of_property_read_u32(np, "eotp", &pdata->eotp)) {
		dev_err(dev, "eotp not set\n");
		return -EINVAL;
	}
	if (of_property_read_u32(np, "tear_effect", &pdata->tear_effect)) {
		dev_err(dev, "tear_effect not set\n");
		return -EINVAL;
	}
	if (of_property_read_u32(np, "scrambling", &pdata->scrambling)) {
		dev_err(dev, "scrambling not set\n");
		return -EINVAL;
	}
	if (of_property_read_u32(np, "vid_mode_type", &pdata->vid_mode_type)) {
		dev_err(dev, "vid_mode_type not set\n");
		return -EINVAL;
	}
	if (of_property_read_u32(np, "lptx_clk", &pdata->lptx_clk)) {
		dev_err(dev, "lptx_clk not set\n");
		return -EINVAL;
	}

	if (of_property_count_u32_elems(np, "hs_en") != DTS_HS_EN_ELEM_SIZE ||
	    of_property_read_u32_array(np, "hs_en", hs_en, DTS_HS_EN_ELEM_SIZE)) {
		dev_err(dev, "hs_en not set\n");
		return -EINVAL;
	}

	pdata->vfp_hs_en = hs_en[0];
	pdata->vbp_hs_en = hs_en[1];
	pdata->vsa_hs_en = hs_en[2];
	pdata->hfp_hs_en = hs_en[3];
	pdata->hbp_hs_en = hs_en[4];
	pdata->hsa_hs_en = hs_en[5];

	if (of_property_read_u32(np, "datarate", &pdata->datarate)) {
		dev_err(dev, "datarate not set\n");
		return -EINVAL;
	}
	if (of_property_read_u32(np, "lp2hs_time", &pdata->lp2hs_time)) {
		dev_err(dev, "lp2hs_time not set\n");
		return -EINVAL;
	}
	if (of_property_read_u32(np, "hs2lp_time", &pdata->hs2lp_time)) {
		dev_err(dev, "hs2lp_time not set\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "sys_clk", &pdata->sys_clk)) {
		dev_err(dev, "sys_clk not set\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "ipi_clk", &pdata->ipi_clk)) {
		dev_err(dev, "ipi_clk not set\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "phy_clk", &pdata->phy_clk)) {
		dev_err(dev, "phy_clk not set\n");
		return -EINVAL;
	}

	if (read_connector_panel_index(np, &pdata->index)) {
		dev_dbg(dev, "connector panel index not set\n");
		pdata->index = DISPLAY_INDEX_PRIMARY;
	}

	pdata->in_emulation = of_property_read_bool(np, "in_emulation");
	pdata->auto_calc_off = of_property_read_bool(np, "auto_calc_off");
	pdata->hs_link_enable = of_property_read_bool(np, "hs_link_enable");
	pdata->dynamic_hs_clk_en = of_property_read_bool(np, "dynamic_hs_clk_en");

	return 0;
}

static int vs_mipi_dsi_encoder_check_skip_first_frame(struct vs_mipi_dsi2h *dsi2h,
						      struct drm_crtc_state *crtc_state)
{
	struct drm_crtc_state *old_crtc_state =
		drm_atomic_get_old_crtc_state(crtc_state->state, crtc_state->crtc);
	struct vs_crtc_state *new_vs_crtc_state = to_vs_crtc_state(crtc_state);
	bool contents_changed = new_vs_crtc_state->planes_updated || crtc_state->color_mgmt_changed;

	/* skip first frame only if DPU is powered on */
	if (!old_crtc_state->self_refresh_active)
		return 0;

	if (!contents_changed) {
		/*
		 * There will be no vblank even if crtc has a event. Fake one
		 * vblank regardless of crtc event.
		 */
		new_vs_crtc_state->force_skip_update = true;
		crtc_state->no_vblank = true;
	}

	return 0;
}

static int vs_mipi_dsi_encoder_atomic_check(struct drm_encoder *encoder,
					    struct drm_crtc_state *crtc_state,
					    struct drm_connector_state *conn_state)
{
	struct vs_mipi_dsi2h *dsi2h = encoder_to_dsi2h(encoder);
	struct device *dev = dsi2h->dev;
	struct vs_crtc_state *vs_crtc_state;
	struct gs_drm_connector_state *gs_conn_state;
	bool is_cmd_mode, sw_trigger;

	if (!drm_atomic_crtc_needs_modeset(crtc_state) || !conn_state->connector)
		return 0;

	if (!is_gs_drm_connector(conn_state->connector))
		return 0;

	vs_crtc_state = to_vs_crtc_state(crtc_state);
	gs_conn_state = to_gs_connector_state(conn_state);
	const struct gs_display_mode *gs_mode = &gs_conn_state->gs_mode;

	is_cmd_mode = !(gs_mode->mode_flags & MIPI_DSI_MODE_VIDEO);
	sw_trigger = gs_mode->sw_trigger;
	vs_crtc_state->output_mode = is_cmd_mode ? VS_OUTPUT_MODE_CMD : 0;
	if (is_cmd_mode) {
		if (dsi2h->pdata.in_emulation || sw_trigger)
			vs_crtc_state->output_mode |= VS_OUTPUT_MODE_CMD_DE_SYNC;
		else if (dsi2h->debug_dsi_mode & DC_HW_CMD_MODE_AUTO) {
			vs_crtc_state->output_mode |= VS_OUTPUT_MODE_CMD_AUTO;
			dev_dbg(dsi2h->dev, "%s: Setting auto mode from debug entry\n", __func__);
		}
	}

	vs_crtc_state->encoder_type = DRM_MODE_ENCODER_DSI;
	vs_crtc_state->output_id = dsi2h->pdata.mux_id;
	vs_crtc_state->te_usec = gs_mode->te_usec;

	switch (gs_mode->bpc) {
	case 6:
		vs_crtc_state->output_fmt = MEDIA_BUS_FMT_RGB666_1X18;
		break;
	case 8:
		vs_crtc_state->output_fmt = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	case 10:
		vs_crtc_state->output_fmt = MEDIA_BUS_FMT_RGB101010_1X30;
		break;
	default:
		dev_err(dev, "Unsupported mode bpc %d, fall back to RGB888_1X24 output format\n",
			gs_mode->bpc);
		vs_crtc_state->output_fmt = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	}

	/*
	 * The first frame (modeset) happens after bootloader handoff. Skip this update if
	 * no content changes so that splash image can be kept.
	 */
	if (dsi2h->first_frame) {
		vs_mipi_dsi_encoder_check_skip_first_frame(dsi2h, crtc_state);
		dsi2h->first_frame = false;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int vs_mipi_dsi_encoder_debugfs_init(struct drm_encoder *encoder)
{
	struct vs_mipi_dsi2h *dsi2h = encoder_to_dsi2h(encoder);

	dsi2h->debugfs_root =
		debugfs_create_dir(dev_name(dsi2h->dev), encoder->dev->primary->debugfs_root);
	if (IS_ERR(dsi2h->debugfs_root)) {
		dev_err(dsi2h->dev, "failed to create debugfs root\n");
		return -ENODEV;
	}

	debugfs_create_u32("debug_dsi_mode", 0644, dsi2h->debugfs_root, &dsi2h->debug_dsi_mode);

	return 0;
}
#else
static int vs_mipi_dsi_encoder_debugfs_init(struct drm_encoder *encoder)
{
	return 0;
}
#endif /* CONFIG_DEBUG_FS */

static int vs_mipi_dsi_encoder_late_register(struct drm_encoder *encoder)
{
	return vs_mipi_dsi_encoder_debugfs_init(encoder);
}

static void vs_mipi_dsi_encoder_early_unregister(struct drm_encoder *encoder)
{
	struct vs_mipi_dsi2h *dsi2h = encoder_to_dsi2h(encoder);

	debugfs_remove_recursive(dsi2h->debugfs_root);
}

static const struct drm_encoder_funcs vs_mipi_dsi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
	.late_register = vs_mipi_dsi_encoder_late_register,
	.early_unregister = vs_mipi_dsi_encoder_early_unregister,
};

static const struct drm_encoder_helper_funcs vs_mipi_dsi_encoder_helper_funcs = {
	.atomic_check = vs_mipi_dsi_encoder_atomic_check,
};

static int vs_mipi_dsi_drm_create_encoder(struct vs_mipi_dsi2h *dsi, struct drm_device *drm_dev)
{
	struct drm_encoder *encoder = &dsi->encoder;
	int ret;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev, dsi->dev->of_node);
	ret = drm_encoder_init(drm_dev, encoder, &vs_mipi_dsi_encoder_funcs, DRM_MODE_ENCODER_DSI,
			       NULL);
	if (ret) {
		DRM_DEV_ERROR(dsi->dev, "Failed to initialize encoder with drm\n");
		return ret;
	}
	drm_encoder_helper_add(encoder, &vs_mipi_dsi_encoder_helper_funcs);

	dsi->first_frame = true;

	return 0;
}

static bool is_primary_display(struct vs_mipi_dsi2h *dsi)
{
	if (!dsi->dsi_device)
		return false;

	return (dsi->pdata.index == DISPLAY_INDEX_PRIMARY);
}

static void vs_mipi_dsi_bridge_cleanup(struct drm_device *dev, void *data)
{
	struct drm_bridge *bridge = data;

	bridge->base.state = NULL;
}

static int vs_mipi_dsi_register_bridge_cleanup(struct drm_device *drm_dev,
					       struct drm_encoder *encoder)
{
	struct drm_bridge *bridge;

	list_for_each_entry(bridge, &encoder->bridge_chain, chain_node) {
		if (is_dw_mipi_dsi2h_bridge(bridge)) {
			return drmm_add_action_or_reset(drm_dev, vs_mipi_dsi_bridge_cleanup,
							bridge);
		}
	}
	return -ENODEV;
}

static int vs_mipi_dsi_bind(struct device *dev, struct device *master, void *data)
{
	int ret, i;
	struct vs_mipi_dsi2h *dsi = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct vs_drm_private *priv = drm_dev->dev_private;
	struct dw_mipi_dsi2h_plat_data *pdata = &dsi->pdata;
	static bool primary_attached;

	ret = vs_mipi_dsi_drm_create_encoder(dsi, drm_dev);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to create drm encoder\n");
		return ret;
	}

	pdata->encoder_initialized = true;

	if (primary_attached || is_primary_display(dsi)) {
		ret = dw_mipi_dsi2h_bind(dsi->dw_mipi_dsi_handle, &dsi->encoder,
					 &priv->dsi_coredump_funcs);
		primary_attached = true;
		if (ret) {
			DRM_DEV_ERROR(dev, "Failed to bind: %d\n", ret);
			return ret;
		}
		vs_mipi_dsi_register_bridge_cleanup(drm_dev, &dsi->encoder);

		for (i = 0; i < MAX_DSI_CNT; i++) {
			if (dsi_drvdata[i] && (dsi_drvdata[i]->pdata.index != pdata->index) &&
				dsi_drvdata[i]->pdata.encoder_initialized) {
				ret = dw_mipi_dsi2h_bind(dsi_drvdata[i]->dw_mipi_dsi_handle,
							 &dsi_drvdata[i]->encoder,
							 &priv->dsi_coredump_funcs);
				if (ret) {
					DRM_DEV_ERROR(dev, "Failed to bind: %d\n", ret);
					return ret;
				}
				vs_mipi_dsi_register_bridge_cleanup(drm_dev,
								    &dsi_drvdata[i]->encoder);
			}
		}
	}

	return 0;
}

static void vs_mipi_dsi_unbind(struct device *dev, struct device *master, void *data)
{
	struct vs_mipi_dsi2h *dsi = dev_get_drvdata(dev);

	dw_mipi_dsi2h_unbind(dsi->dw_mipi_dsi_handle);
}

static const struct component_ops vs_mipi_dsi2h_component_ops = {
	.bind = vs_mipi_dsi_bind,
	.unbind = vs_mipi_dsi_unbind,
};

static int vs_mipi_dsi2h_host_attach(void *priv_data, struct mipi_dsi_device *device)
{
	struct vs_mipi_dsi2h *dsi2h = priv_data;

	dsi2h->dsi_device = device;
	return component_add(dsi2h->dev, &vs_mipi_dsi2h_component_ops);
}

static int vs_mipi_dsi2h_host_detach(void *priv_data, struct mipi_dsi_device *device)
{
	struct vs_mipi_dsi2h *dsi = priv_data;

	dsi->dsi_device = NULL;
	component_del(dsi->dev, &vs_mipi_dsi2h_component_ops);

	return 0;
}

static const struct dw_mipi_dsi2h_host_ops vs_mipi_dsi2h_host_ops = {
	.attach = vs_mipi_dsi2h_host_attach,
	.detach = vs_mipi_dsi2h_host_detach,
};

static int vs_mipi_dsi2h_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct vs_mipi_dsi2h *dsi;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->dev = dev;
	ret = dw_mipi_dsi2h_of_parse_pdata(dev, &dsi->pdata);
	if (ret)
		return -ENODATA;

	dsi_drvdata[dsi->pdata.mux_id] = dsi;

	dsi->pdata.irq = platform_get_irq(pdev, 0);
	dsi->pdata.host_ops = &vs_mipi_dsi2h_host_ops;
	dsi->pdata.priv_data = dsi;
	platform_set_drvdata(pdev, dsi);

	dsi->dw_mipi_dsi_handle = dw_mipi_dsi2h_probe(pdev, &dsi->pdata);
	if (IS_ERR(dsi->dw_mipi_dsi_handle)) {
		ret = PTR_ERR(dsi->dw_mipi_dsi_handle);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "Failed to probe vs_mipi_dsi2h: %d\n", ret);
		return ret;
	}

	return 0;
}

static void vs_mipi_dsi2h_remove(struct platform_device *pdev)
{
	struct vs_mipi_dsi2h *dsi = platform_get_drvdata(pdev);

	dw_mipi_dsi2h_remove(dsi->dw_mipi_dsi_handle);
}

static const struct of_device_id vs_mipi_dsi2h_dt_match[] = {
	{ .compatible = "verisilicon,dc9x00-mipi-dsi2h" },
	{},
};
MODULE_DEVICE_TABLE(of, vs_mipi_dsi2h_dt_match);

static int vs_mipi_dsi2h_suspend(struct device *dev)
{
	struct vs_mipi_dsi2h *dsi = dev_get_drvdata(dev);

	if (!dsi->dw_mipi_dsi_handle) {
		dev_warn(dsi->dev, "%s: bridge not ready\n", __func__);
		return 0;
	}

	return dw_mipi_dsi2h_suspend(dsi->dw_mipi_dsi_handle);
}

static int vs_mipi_dsi2h_resume(struct device *dev)
{
	struct vs_mipi_dsi2h *dsi = dev_get_drvdata(dev);

	if (!dsi->dw_mipi_dsi_handle) {
		dev_warn(dsi->dev, "%s: bridge not ready\n", __func__);
		return 0;
	}

	return dw_mipi_dsi2h_resume(dsi->dw_mipi_dsi_handle);
}

DEFINE_RUNTIME_DEV_PM_OPS(vs_mipi_dsi2h_pm_ops, vs_mipi_dsi2h_suspend, vs_mipi_dsi2h_resume, NULL);

struct platform_driver vs_mipi_dsi2h_driver = {
	.probe = vs_mipi_dsi2h_probe,
	.remove = vs_mipi_dsi2h_remove,
	.driver = {
	    .name = "dw-mipi-dsi2h-vs",
	    .of_match_table = of_match_ptr(vs_mipi_dsi2h_dt_match),
	    .pm = &vs_mipi_dsi2h_pm_ops,
	},
};
