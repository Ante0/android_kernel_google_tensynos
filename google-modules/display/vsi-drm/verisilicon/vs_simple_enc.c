// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */
#include <linux/component.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/media-bus-format.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>

#include "vs_crtc.h"
#include "vs_dc.h"
#include "vs_simple_enc.h"
#include "vs_drv.h"

#define MAX_MUX_ID 6

static const struct simple_encoder_priv hdmi_priv = { .encoder_type = DRM_MODE_ENCODER_TMDS };

static const struct simple_encoder_priv dp_priv = { .encoder_type = DRM_MODE_ENCODER_DPMST };

static const struct simple_encoder_priv dsi_video_priv = { .encoder_type = DRM_MODE_ENCODER_DSI };

static const struct simple_encoder_priv dsi_command_priv = {
	.encoder_type = DRM_MODE_ENCODER_DSI,
	.output_mode = VS_OUTPUT_MODE_CMD | VS_OUTPUT_MODE_CMD_DE_SYNC
};

static const struct simple_encoder_priv dpi_priv = { .encoder_type = DRM_MODE_ENCODER_DPI };

static const struct drm_encoder_funcs encoder_funcs = { .destroy = drm_encoder_cleanup };

static inline struct simple_encoder *to_simple_encoder(struct drm_encoder *enc)
{
	return container_of(enc, struct simple_encoder, encoder);
}

static int encoder_parse_dt(struct device *dev)
{
	struct simple_encoder *simple = dev_get_drvdata(dev);
	u32 mux_id;
	int ret = 0;

	ret = of_property_read_u32(dev->of_node, "verisilicon,mux-id", &mux_id);
	if (ret) {
		dev_err(dev, "mux-id must be defined in device tree\n");
		return ret;
	} else if (mux_id > MAX_MUX_ID) {
		dev_err(dev, "invalid mux-id (%d)\n", mux_id);
		return -EINVAL;
	}
	simple->mux_id = mux_id;
	dev_dbg(dev, "mux-id: %u\n", simple->mux_id);

	return ret;
}

static int encoder_atomic_check(struct drm_encoder *encoder, struct drm_crtc_state *crtc_state,
				struct drm_connector_state *conn_state)
{
	struct vs_crtc_state *vs_crtc_state = to_vs_crtc_state(crtc_state);
	struct drm_connector *connector = conn_state->connector;
	struct simple_encoder *simple_enc = to_simple_encoder(encoder);
	struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	u32 max_with = simple_enc->priv->max_w;
	u32 max_height = simple_enc->priv->max_h;
	int ret = 0;
	struct drm_bridge *first_bridge = drm_bridge_chain_get_first_bridge(encoder);
	struct drm_bridge_state *bridge_state = ERR_PTR(-EINVAL);

	if (crtc_state->active) {
		vs_crtc_state->encoder_type = encoder->encoder_type;
		vs_crtc_state->output_id = simple_enc->mux_id;
		vs_crtc_state->output_mode = simple_enc->priv->output_mode;
	} else {
		vs_crtc_state->encoder_type = DRM_MODE_ENCODER_NONE;
	}

	if (first_bridge && first_bridge->funcs->atomic_duplicate_state)
		bridge_state = drm_atomic_get_bridge_state(crtc_state->state, first_bridge);

	if (IS_ERR(bridge_state)) {
		if (connector->display_info.num_bus_formats)
			vs_crtc_state->output_fmt = connector->display_info.bus_formats[0];
		else
			vs_crtc_state->output_fmt = MEDIA_BUS_FMT_FIXED;
	} else {
		vs_crtc_state->output_fmt = bridge_state->input_bus_cfg.format;
	}

	switch (vs_crtc_state->output_fmt) {
	case MEDIA_BUS_FMT_FIXED:
	case MEDIA_BUS_FMT_RGB565_1X16:
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_RGB121212_1X36:
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
	case MEDIA_BUS_FMT_UYVY10_1X20:
	case MEDIA_BUS_FMT_YUV10_1X30:
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	/* If MEDIA_BUS_FMT_FIXED, set it to default value */
	if (vs_crtc_state->output_fmt == MEDIA_BUS_FMT_FIXED)
		vs_crtc_state->output_fmt = MEDIA_BUS_FMT_RGB888_1X24;

	if (max_with || max_height) {
		if (mode->hdisplay > max_with || mode->vdisplay > max_height) {
			pr_err("%s the encoder_type %d is not support the display size,max_w:%d, max_h:%d.\n",
			       __func__, encoder->encoder_type, max_with, max_height);
			ret = -EINVAL;
		}
	}
	return ret;
}

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.atomic_check = encoder_atomic_check,
};

static int encoder_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm_dev = data;
	struct vs_drm_private *priv = drm_dev->dev_private;
	struct simple_encoder *simple = dev_get_drvdata(dev);
	struct drm_encoder *encoder;
	struct drm_bridge *bridge = NULL;
	struct drm_panel *panel = NULL;
	int ret;

	encoder = &simple->encoder;

	/* Encoder. */
	ret = drm_encoder_init(drm_dev, encoder, &encoder_funcs, simple->priv->encoder_type, NULL);
	if (ret)
		return ret;

	drm_encoder_helper_add(encoder, &encoder_helper_funcs);
	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm_dev, dev->of_node);
	simple->dev = priv->dc_dev;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1, &panel, &bridge);
	if (ret)
		goto err;

	if (panel)
		bridge = devm_drm_panel_bridge_add(dev, panel);

	ret = drm_bridge_attach(encoder, bridge, NULL, 0);
	if (ret)
		goto err;

	return 0;
err:
	drm_encoder_cleanup(encoder);

	return ret;
}

static void encoder_unbind(struct device *dev, struct device *master, void *data)
{
	struct simple_encoder *simple = dev_get_drvdata(dev);

	drm_encoder_cleanup(&simple->encoder);
}

static const struct component_ops encoder_component_ops = {
	.bind = encoder_bind,
	.unbind = encoder_unbind,
};

static const struct of_device_id simple_encoder_dt_match[] = {
	{ .compatible = "verisilicon,hdmi-encoder", .data = &hdmi_priv },
	{ .compatible = "verisilicon,dp-encoder", .data = &dp_priv },
	{ .compatible = "verisilicon,dsi-encoder", .data = &dsi_video_priv },
	{ .compatible = "verisilicon,dsi-cmd-encoder", .data = &dsi_command_priv },
	{ .compatible = "verisilicon,dpi-encoder", .data = &dpi_priv },
	{},

};
MODULE_DEVICE_TABLE(of, simple_encoder_dt_match);

static int encoder_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct simple_encoder *simple;
	int ret;

	simple = devm_kzalloc(dev, sizeof(*simple), GFP_KERNEL);
	if (!simple)
		return -ENOMEM;

	simple->priv = of_device_get_match_data(dev);

	simple->dev = dev;

	dev_set_drvdata(dev, simple);

	ret = encoder_parse_dt(dev);
	if (ret)
		return ret;

	return component_add(dev, &encoder_component_ops);
}

static void encoder_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	component_del(dev, &encoder_component_ops);

	dev_set_drvdata(dev, NULL);
}

struct platform_driver simple_encoder_driver = {
	.probe = encoder_probe,
	.remove = encoder_remove,
	.driver = {
		.name = "vs-simple-encoder",
		.of_match_table = of_match_ptr(simple_encoder_dt_match),
	},
};

MODULE_DESCRIPTION("Simple Encoder Driver");
MODULE_LICENSE("GPL v2");
