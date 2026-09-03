// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */
#include <linux/types.h>
#include <drm/g2d_drm.h>

#include "g2d_registers.h"
#include "g2d_sc_hw.h"
#include "g2d_plane_hw.h"
#include "g2d_pvric_hw.h"

/* The default horizontal scale coefficient data with the filter tap of 9. */
static const u32 default_scaling_coeff_horizontal[] = {
	0x03d3ff32, 0x279af561, 0xf561279a, 0xff3203d3, 0xff420000, 0xf5c103ac, 0x2a2e24f9,
	0x03f1f517, 0x0000ff22, 0x037eff54, 0x224bf637, 0xf4e62cac, 0xff150405, 0xff670000,
	0xf6be0349, 0x2f121f95, 0x040ff4d1, 0x0000ff0b, 0x030fff79, 0x1cdbf756, 0xf4d9315d,
	0xff03040e, 0xff8c0000, 0xf7fb02d0, 0x33881a21, 0x0401f500, 0x0000feff, 0x028eff9e,
	0x176af8ab, 0xf5483592, 0xfefe03e7, 0xffaf0000, 0xf9640249, 0x377414bb, 0x03c0f5b3,
	0x0000ff02, 0x0203ffbf, 0x1217fa23, 0xf641392d, 0xff0a038c, 0xffce0000, 0xfae601bc,
	0x3aba0f81, 0x034af6f4, 0x0000ff17, 0x0176ffdb, 0x0cfefbab, 0xf7cd3c18, 0xff2802f9,
	0xffe70000, 0xfc6f0130, 0x3d450a8f, 0x029bf8cc, 0x0000ff3f, 0x00edfff0, 0x0839fd31,
	0xf9f13e3e, 0xff5b022f, 0xfff70000, 0xfdf000ac, 0x3f0105fc, 0x01b6fb3d, 0x0000ff7d,
	0x006ffffc, 0x03ddfea8, 0xfcae3f8e, 0xffa40130, 0xffff0000, 0xff590035, 0x3fe301de,
	0x009dfe45, 0x0000ffd0, 0x00000000, 0x00000000, 0x00004000, 0x00000000, 0x00000000,
};
#define H_COEF_SIZE (ARRAY_SIZE(default_scaling_coeff_horizontal))

/* The default vertical scale coefficient data with the filter tap of 5. */
static const u32 default_scaling_coeff_vertical[] = {
	0x23fffc01, 0xfc0123ff, 0xfc600000, 0x26b72142, 0x0000fba7, 0x1e84fcc2, 0xfb542966,
	0xfd240000, 0x2c061bca, 0x0000fb0c, 0x1917fd86, 0xfad12e92, 0xfde40000, 0x31061670,
	0x0000faa6, 0x13d9fe3e, 0xfa8e335b, 0xfe910000, 0x358d1156, 0x0000fa8c, 0x0ee9fede,
	0xfaa23797, 0xff230000, 0x39730c96, 0x0000fad4, 0x0a60ff5f, 0xfb253b1c, 0xff910000,
	0x3c8f084a, 0x0000fb96, 0x0656ffba, 0xfc2b3dc5, 0xffda0000, 0x3ebb0486, 0x0000fce5,
	0x02dcfff0, 0xfdc63f6e, 0xfffc0000, 0x3fdb015a, 0x0000fecf, 0x00000000, 0x00004000,
	0x00000000,
};

#define V_COEF_SIZE (ARRAY_SIZE(default_scaling_coeff_vertical))

static void plane_hw_config_load_filter(struct sc_hw *hw, u8 hw_id, const u32 *coef_v,
					const u32 *coef_h)
{
	u32 i;
	const u32 reg_offset = 0x04;
	const u32 *ch = default_scaling_coeff_horizontal;
	const u32 *cv = default_scaling_coeff_vertical;

	/* Use the user coefficients if they were both provided */
	if (coef_v && coef_h) {
		ch = coef_h;
		cv = coef_v;
	}
	for (i = 0; i < H_COEF_SIZE; i++)
		sc_write(hw,
			 vsSETFIELD_FE(SCREG_LAYER, hw_id, HSCALE_COEF_DATA_Address) +
				 (i * reg_offset),
			 ch[i]);

	for (i = 0; i < V_COEF_SIZE; i++)
		sc_write(hw,
			 vsSETFIELD_FE(SCREG_LAYER, hw_id, VSCALE_COEF_DATA_Address) +
				 (i * reg_offset),
			 cv[i]);
}

static void plane_set_fb(struct sc_hw *hw, u8 hw_id, struct sc_hw_fb *fb)
{
	/*
	 * TODO(b/355089225): offset is currently hardcoded to 0.
	 * Offset should be updated to be based on hw_id once we support 2 pipelines.
	 */
	u32 offset = 0;
	u32 config = 0;

	dev_dbg(hw->dev, "%s: setting fb on hw_id %d", __func__, hw_id);
	if (fb->enable) {
		/* address configuration */
		sc_write(hw, SCREG_LAYER0_ADDRESS_Address + offset,
			 (u32)(fb->address & 0xFFFFFFFF));
		sc_write(hw, SCREG_LAYER0_HIGH_ADDRESS_Address + offset, fb->address >> 32);
		sc_write(hw, SCREG_LAYER0_UADDRESS_Address + offset,
			 (u32)(fb->u_address & 0xFFFFFFFF));
		sc_write(hw, SCREG_LAYER0_HIGH_UADDRESS_Address + offset, fb->u_address >> 32);
		sc_write(hw, SCREG_LAYER0_VADDRESS_Address + offset,
			 (u32)(fb->v_address & 0xFFFFFFFF));
		sc_write(hw, SCREG_LAYER0_HIGH_VADDRESS_Address + offset, fb->v_address >> 32);

		/* stride/size configuration */
		sc_write(hw, SCREG_LAYER0_STRIDE_Address + offset, fb->stride);
		sc_write(hw, SCREG_LAYER0_USTRIDE_Address + offset, fb->u_stride);
		sc_write(hw, SCREG_LAYER0_VSTRIDE_Address + offset, fb->v_stride);
		sc_write(hw, SCREG_LAYER0_SIZE_Address + offset,
			 VS_SET_FIELD(0, SCREG_LAYER0_SIZE, WIDTH, fb->width) |
				 VS_SET_FIELD(0, SCREG_LAYER0_SIZE, HEIGHT, fb->height));
	}

	config = VS_SET_FIELD(config, SCREG_LAYER0_CONFIG, ENABLE, fb->enable);
	config = VS_SET_FIELD(config, SCREG_LAYER0_CONFIG, TILE_MODE, fb->tile_mode);
	config = VS_SET_FIELD(config, SCREG_LAYER0_CONFIG, FORMAT, fb->format);
	config = VS_SET_FIELD(config, SCREG_LAYER0_CONFIG, ROT_ANGLE, fb->rotation);
	config = VS_SET_FIELD(config, SCREG_LAYER0_CONFIG, UV_SWIZZLE, fb->uv_swizzle);
	config = VS_SET_FIELD(config, SCREG_LAYER0_CONFIG, SWIZZLE, fb->swizzle);

	/* Force Compression to disabled since it's not yet supported */
	config = VS_SET_FIELD(config, SCREG_LAYER0_CONFIG, COMPRESS_ENABLE,
			      SCREG_LAYER0_CONFIG_COMPRESS_ENABLE_DISABLED);

	/* Force Scale to disabled since it's not yet supported */
	config = VS_SET_FIELD(config, SCREG_LAYER0_CONFIG, SCALE,
			      SCREG_LAYER0_CONFIG_SCALE_DISABLED);

	/* Force Dither to disabled since it's not yet supported */
	config = VS_SET_FIELD(config, SCREG_LAYER0_CONFIG, DITHER,
			      SCREG_LAYER0_CONFIG_DITHER_DISABLED);

	sc_write(hw, SCREG_LAYER0_CONFIG_Address + offset, config);

	dev_dbg(hw->dev, "%s: finished fb config reg writes on hw_id %d", __func__, hw_id);
}

static void plane_set_scale(struct sc_hw *hw, u8 hw_id, struct sc_hw_scale *scale)
{
	u32 config;

	config = sc_read(hw, vsSETFIELD_FE(SCREG_LAYER, hw_id, CONFIG_Address));
	sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, hw_id, CONFIG_Address),
		 VS_SET_FIELD(config, SCREG_LAYER0_CONFIG, SCALE, scale->enable));

	if (!scale->enable)
		return;

	/*
	 * While G2D avoids using dirty bits where possible, there are a lot
	 * of scaling coefficients and it's best to only write them on init or
	 * after they're cleared on suspend/resume
	 */
	if (scale->coefficients_dirty) {
		plane_hw_config_load_filter(hw, hw_id, NULL, NULL);
		scale->coefficients_dirty = false;
	}

	sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, hw_id, SCALE_INITIAL_OFFSET_X_Address),
		 scale->offset_x);
	sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, hw_id, SCALE_INITIAL_OFFSET_Y_Address),
		 scale->offset_y);
	sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, hw_id, HSCALE_FACTOR_Address), scale->factor_x);
	sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, hw_id, VSCALE_FACTOR_Address), scale->factor_y);

	dev_dbg(hw->dev,
		"Scaling written to HW: offset_y: %d, offset_y: %d, factor_x: %d, factor_y: %d",
		scale->offset_x, scale->offset_y, scale->factor_x, scale->factor_y);
}

static void plane_set_roi(struct sc_hw *hw, u8 hw_id, struct sc_hw_roi *roi_hw)
{
	u32 config = 0;

	// TODO(b/355089225): offset is currently hardcoded to 0.
	// Update this when we support 2 pipelines.
	u32 offset = 0;

	config = sc_read(hw, SCREG_LAYER0_CONFIG_Address + offset);
	config = VS_SET_FIELD(config, SCREG_LAYER0_CONFIG, DMA_MODE, roi_hw->mode);
	sc_write(hw, SCREG_LAYER0_CONFIG_Address + offset, config);

	dev_dbg(hw->dev, "%s: DMA MODE: %d", __func__, roi_hw->mode);
	if (roi_hw->mode == G2D_DMA_ONE_ROI) {
		/* config in ROI 0 region. */
		config = 0;
		config = VS_SET_FIELD(config, SCREG_LAYER0_IN_ROI_ORIGIN, X, roi_hw->in_rect.x) |
			 VS_SET_FIELD(config, SCREG_LAYER0_IN_ROI_ORIGIN, Y, roi_hw->in_rect.y);
		sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, hw_id, IN_ROI_ORIGIN_Address), config);

		config = 0;
		config = VS_SET_FIELD(config, SCREG_LAYER0_IN_ROI_SIZE, WIDTH, roi_hw->in_rect.w) |
			 VS_SET_FIELD(config, SCREG_LAYER0_IN_ROI_SIZE, HEIGHT, roi_hw->in_rect.h);
		sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, hw_id, IN_ROI_SIZE_Address), config);

		dev_dbg(hw->dev, "%s: ROI written as %dx%d @ +%d+%d", __func__, roi_hw->in_rect.w,
			roi_hw->in_rect.h, roi_hw->in_rect.x, roi_hw->in_rect.y);
	}
}

static void plane_set_y2r(struct sc_hw *hw, u8 hw_id, struct sc_hw_y2r *y2r_conf)
{
	u32 csc_mode = 0, csc_gamut = 0;
	u32 i = 0, config = 0;
	u32 idx_offset = 0x04;

	if (!y2r_conf->enable) {
		config = sc_read(hw, vsSETFIELD_FE(SCREG_LAYER, hw_id, CONFIG_Address));
		sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, hw_id, CONFIG_Address),
			 VS_SET_FIELD_PREDEF(config, SCREG_LAYER0_CONFIG, Y2R, DISABLED));
		return;
	}

	switch (y2r_conf->mode) {
	case CSC_MODE_USER_DEF:
		dev_err(hw->dev, "%s: CSC_MODE_USER_DEF configured, but it is not supported yet!",
			__func__);
		csc_mode = VS_SET_FIELD_PREDEF(0, SCREG_LAYER0_Y2R_CONFIG, MODE, PROGRAMMABLE);
		break;
	case CSC_MODE_L2L:
		csc_mode = VS_SET_FIELD_PREDEF(0, SCREG_LAYER0_Y2R_CONFIG, MODE,
					       LIMIT_YUV_2_LIMIT_RGB);
		break;
	case CSC_MODE_L2F:
		csc_mode =
			VS_SET_FIELD_PREDEF(0, SCREG_LAYER0_Y2R_CONFIG, MODE, LIMIT_YUV_2_FULL_RGB);
		break;
	case CSC_MODE_F2L:
		csc_mode =
			VS_SET_FIELD_PREDEF(0, SCREG_LAYER0_Y2R_CONFIG, MODE, FULL_YUV_2_LIMIT_RGB);
		break;
	case CSC_MODE_F2F:
		csc_mode =
			VS_SET_FIELD_PREDEF(0, SCREG_LAYER0_Y2R_CONFIG, MODE, FULL_YUV_2_FULL_RGB);
		break;
	default:
		break;
	}

	if (y2r_conf->mode != CSC_MODE_USER_DEF) {
		switch (y2r_conf->gamut) {
		case CSC_GAMUT_601:
			csc_gamut = VS_SET_FIELD_PREDEF(0, SCREG_LAYER0_Y2R_CONFIG, GAMUT, BT601);
			break;
		case CSC_GAMUT_709:
			csc_gamut = VS_SET_FIELD_PREDEF(0, SCREG_LAYER0_Y2R_CONFIG, GAMUT, BT709);
			break;
		case CSC_GAMUT_2020:
			csc_gamut = VS_SET_FIELD_PREDEF(0, SCREG_LAYER0_Y2R_CONFIG, GAMUT, BT2020);
			break;
		case CSC_GAMUT_P3:
			csc_gamut = VS_SET_FIELD_PREDEF(0, SCREG_LAYER0_Y2R_CONFIG, GAMUT, P3);
			break;
		case CSC_GAMUT_SRGB:
			csc_gamut = VS_SET_FIELD_PREDEF(0, SCREG_LAYER0_Y2R_CONFIG, GAMUT, SRGB);
			break;
		default:
			break;
		}
	} else {
		for (i = 0; i < VS_MAX_Y2R_COEF_NUM; i++) {
			sc_write(hw,
				 vsSETFIELD_FE(SCREG_LAYER, hw_id, YUV_TO_RGB_COEF0_Address) +
					 i * idx_offset,
				 y2r_conf->coef[i]);
		}
	}

	sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, hw_id, Y2R_CONFIG_Address), csc_mode | csc_gamut);

	config = sc_read(hw, vsSETFIELD_FE(SCREG_LAYER, hw_id, CONFIG_Address));
	config = VS_SET_FIELD_PREDEF(config, SCREG_LAYER0_CONFIG, Y2R, ENABLED);
	sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, hw_id, CONFIG_Address), config);
}

void plane_commit(struct sc_hw *hw, u8 layer_id)
{
	struct sc_hw_plane *plane;
	u32 i;

	dev_dbg(hw->dev, "%s: layer_id %d", __func__, layer_id);
	for (i = 0; i < NUM_PIPELINES; i++) {
		plane = &hw->plane[i];

		/*
		 * TODO(b/355089225): display_id may be irrelevant since there's a 1:1 mapping
		 * between pipelines and layers. Clean this up when implementing 2 pipeline support.
		 */
		if (plane->fb.display_id != layer_id)
			continue;

		plane_set_fb(hw, i, &plane->fb);
		plane_set_roi(hw, i, &plane->roi);
		plane_set_y2r(hw, i, &plane->y2r);
		plane_set_scale(hw, i, &plane->scale);
		pvric_hw_plane_commit(hw, layer_id);
	}
}
