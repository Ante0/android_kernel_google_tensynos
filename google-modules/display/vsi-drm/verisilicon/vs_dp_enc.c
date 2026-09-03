// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC
 */

#include <linux/component.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/media-bus-format.h>

#include <drm/display/drm_hdcp_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_simple_kms_helper.h>
#include "vs_crtc.h"


#define MAX_MUX_ID 6
#define bridge_to_vs_dp(_bridge) container_of_const(_bridge, struct vs_dp, bridge)

struct vs_dp_priv {
	u8 encoder_type;
	u32 output_mode;
};

struct vs_dp {
	struct device *dev;
	struct drm_encoder encoder;
	u16 mux_id;
	const struct vs_dp_priv *priv;
	struct drm_bridge *out_bridge;
	struct drm_connector *connector;
};

static const struct vs_dp_priv dp_priv = { .encoder_type = DRM_MODE_ENCODER_DPI };

static inline struct vs_dp *to_vs_dp(struct drm_encoder *enc)
{
	return container_of(enc, struct vs_dp, encoder);
}

static int vs_dp_parse_dt(struct device *dev)
{
	struct vs_dp *dp = dev_get_drvdata(dev);
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

	dp->mux_id = mux_id;
	dev_dbg(dev, "mux-id: %u\n", dp->mux_id);
	return ret;
}

static void vs_dp_atomic_enable(struct drm_encoder *encoder, struct drm_atomic_state *state)
{
}

static void vs_dp_atomic_disable(struct drm_encoder *encoder, struct drm_atomic_state *state)
{
}

static int vs_dp_atomic_check(struct drm_encoder *encoder, struct drm_crtc_state *crtc_state,
			      struct drm_connector_state *conn_state)
{
	struct vs_crtc_state *vs_crtc_state = to_vs_crtc_state(crtc_state);
	struct drm_connector *connector = conn_state->connector;
	struct vs_dp *dp = to_vs_dp(encoder);
	int ret = 0;
	struct drm_bridge *first_bridge = drm_bridge_chain_get_first_bridge(encoder);
	struct drm_bridge_state *bridge_state = ERR_PTR(-EINVAL);

	if (crtc_state->active) {
		vs_crtc_state->encoder_type = encoder->encoder_type;
		vs_crtc_state->output_id = dp->mux_id;
		vs_crtc_state->output_mode = dp->priv->output_mode;
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

	return ret;
}

static const struct drm_encoder_helper_funcs vs_dp_helper_funcs = {
	.atomic_check = vs_dp_atomic_check,
	.atomic_enable = vs_dp_atomic_enable,
	.atomic_disable = vs_dp_atomic_disable,
};

static int vs_dp_encoder_init(struct drm_device *drm, struct vs_dp *dp)
{
	int ret;

	if (!dp->out_bridge) {
		dev_err(dp->dev, "Unable to get DisplayPort bridge. Skipping this encoder\n");
		return 0;
	}

	ret = drm_simple_encoder_init(drm, &dp->encoder, dp->priv->encoder_type);
	if (ret) {
		dev_err(dp->dev, "Failed to encoder init to drm\n");
		return ret;
	}

	drm_encoder_helper_add(&dp->encoder, &vs_dp_helper_funcs);
	dp->encoder.possible_crtcs = drm_of_find_possible_crtcs(drm, dp->dev->of_node);

	ret = drm_bridge_attach(&dp->encoder, dp->out_bridge, NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		goto err_cleanup_encoder;

	dp->connector = drm_bridge_connector_init(drm, &dp->encoder);
	if (IS_ERR(dp->connector)) {
		dev_err(dp->dev, "Unable to create bridge connector\n");
		ret = PTR_ERR(dp->connector);
		goto err_cleanup_encoder;
	}

	drm_connector_attach_content_protection_property(dp->connector,
		DRM_MODE_HDCP_CONTENT_TYPE0);

	ret = drm_connector_attach_encoder(dp->connector, &dp->encoder);
	if (ret)
		goto err_cleanup_encoder;

	return 0;

err_cleanup_encoder:
	drm_encoder_cleanup(&dp->encoder);
	return ret;
}

static int vs_dp_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;
	struct vs_dp *dp = dev_get_drvdata(dev);

	return vs_dp_encoder_init(drm, dp);
}

static void vs_dp_unbind(struct device *dev, struct device *master, void *data)
{
	struct vs_dp *dp = dev_get_drvdata(dev);

	drm_encoder_cleanup(&dp->encoder);
}

static const struct component_ops vs_dp_component_ops = {
	.bind = vs_dp_bind,
	.unbind = vs_dp_unbind,
};

static const struct of_device_id vs_dp_dt_match[] = {
	{
		.compatible = "verisilicon,dp-encoder",
		.data = &dp_priv,
	},
	{},
};
MODULE_DEVICE_TABLE(of, vs_dp_dt_match);

static int vs_dp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vs_dp *dp;
	int ret;

	dp = devm_kzalloc(dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	dp->priv = of_device_get_match_data(dev);
	dp->dev = dev;
	dev_set_drvdata(dev, dp);

	ret = vs_dp_parse_dt(dev);
	if (ret)
		return ret;

	dp->out_bridge = devm_drm_of_get_bridge(dp->dev, dp->dev->of_node, 1, 0);

	if (IS_ERR(dp->out_bridge)) {
		ret = PTR_ERR(dp->out_bridge);
		dp->out_bridge = NULL;
		if (ret != -ENODEV)
			return ret;
	}

	return component_add(dev, &vs_dp_component_ops);
}

static void vs_dp_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	component_del(dev, &vs_dp_component_ops);
	dev_set_drvdata(dev, NULL);
}

struct platform_driver vs_dp_driver = {
	.probe = vs_dp_probe,
	.remove = vs_dp_remove,
	.driver = {
		.name = "vs_dp",
		.of_match_table = of_match_ptr(vs_dp_dt_match),
	},
};

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("DisplayPort Encoder Driver");
MODULE_LICENSE("GPL");
