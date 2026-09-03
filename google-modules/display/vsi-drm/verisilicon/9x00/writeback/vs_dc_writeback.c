/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */
#include <drm/drm_property.h>
#include "drm/vs_drm.h"
#include "drm/vs_drm_fourcc.h"
#include "vs_dc_property.h"
#include "vs_dc_hw.h"
#include "vs_dc_reg_be.h"
#include "vs_dc_writeback.h"
#include "vs_dc.h"

static bool wb_spliter_busy;

/* writeback dither */
static bool wb_dither_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			    const void *obj_state)
{
	const struct drm_vs_wb_dither *wb_dither = data;

	if (wb_dither->index_type > VS_DTH_FRM_IDX_HW) {
		dev_err(hw->dev, "%s: crtc[%u] has invalid frame index type.\n", __func__, hw_id);
		return false;
	}

	if (wb_dither->index_type == VS_DTH_FRM_IDX_SW && wb_dither->sw_index > 3) {
		dev_err(hw->dev, "%s: crtc[%u] has invalid sw index.\n", __func__, hw_id);
		return false;
	}

	return true;
}

static bool wb_dither_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_wb_dither *dither = data;
	const u32 offset = vs_dc_get_wb_offset(hw_id);
	u32 config;

	config = dc_read(hw, DCREG_SH_WB0_CONFIG_Address + offset);
	config = VS_SET_FIELD(config, DCREG_SH_WB0_CONFIG, DITHER, enable);
	dc_write(hw, DCREG_SH_WB0_CONFIG_Address + offset, config);

	if (enable) {
		/* table */
		dc_write(hw, DCREG_SH_WB0_DTH_RTABLE_LOW_Address + offset, dither->table_low[0]);
		dc_write(hw, DCREG_SH_WB0_DTH_GTABLE_LOW_Address + offset, dither->table_low[1]);
		dc_write(hw, DCREG_SH_WB0_DTH_BTABLE_LOW_Address + offset, dither->table_low[2]);
		dc_write(hw, DCREG_SH_WB0_DTH_RTABLE_HIGH_Address + offset, dither->table_high[0]);
		dc_write(hw, DCREG_SH_WB0_DTH_GTABLE_HIGH_Address + offset, dither->table_high[2]);
		dc_write(hw, DCREG_SH_WB0_DTH_BTABLE_HIGH_Address + offset, dither->table_high[2]);

		/* index */
		config = 0;
		config = dc_read(hw, DCREG_SH_WB0_DTH_CFG_Address + offset);

		if (dither->index_type == VS_DTH_FRM_IDX_NONE) {
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_WB0_DTH_CFG,
						    FRAME_INDEX_ENABLE, DISABLED);
		} else if (dither->index_type == VS_DTH_FRM_IDX_HW) {
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_WB0_DTH_CFG,
						    FRAME_INDEX_ENABLE, ENABLED);
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_WB0_DTH_CFG, FRAME_INDEX_FROM,
						    HW);
		} else {
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_WB0_DTH_CFG,
						    FRAME_INDEX_ENABLE, ENABLED);
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_WB0_DTH_CFG, FRAME_INDEX_FROM,
						    SW);
			config = VS_SET_FIELD(config, DCREG_SH_WB0_DTH_CFG, SW_FRAME_INDEX,
					      dither->sw_index);
		}

		if (dither->frm_mode == VS_DTH_FRM_16)
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_WB0_DTH_CFG, FRAME_MODE,
						    FRAME16);
		else if (dither->frm_mode == VS_DTH_FRM_8)
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_WB0_DTH_CFG, FRAME_MODE,
						    FRAME8);
		else
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_WB0_DTH_CFG, FRAME_MODE,
						    FRAME4);

		dc_write(hw, DCREG_SH_WB0_DTH_CFG_Address + offset, config);
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(wb_dither_proto, "WB_DITHER", struct drm_vs_wb_dither, wb_dither_check,
			  NULL, wb_dither_config_hw);

/* writeback scale */
static bool wb_scale_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			   const void *obj_state)
{
	const struct dc_hw_wb *hw_wb = vs_dc_hw_get_wb(hw, hw_id);
	const struct vs_wb_info *wb_info = hw_wb->info;

	const struct drm_vs_scale_config *scale = data;

	if (scale->factor_x < wb_info->min_scale || scale->factor_x > wb_info->max_scale ||
	    scale->factor_y < wb_info->min_scale || scale->factor_y > wb_info->max_scale) {
		dev_err(hw->dev, "%s the scale factor out of range.\n", __func__);
		return false;
	}

	return true;
}

static bool wb_scale_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_scale_config *scale = data;
	u32 config = 0, i = 0;
	u32 base_offset = 0x4;
	u32 initial_offsetx = 0x8000;
	u32 initial_offsety = 0x8000;

	/* commit enable scale or disable scale*/
	config = dc_read(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, CONFIG_Address));

	config = VS_SET_FIELD(config, DCREG_SH_WB0_CONFIG, SCALE, scale->enable);

	dc_write(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, CONFIG_Address), config);

	if (scale->enable) {
		dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, 0, IMAGE_WIDTH_Address),
			 scale->src_w & 0xFFFF);
		dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, 0, IMAGE_HEIGHT_Address),
			 scale->src_h & 0xFFFF);

		dc_write(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, SPLIT_WIDTH_Address),
			 scale->src_w & 0xFFFF);
		dc_write(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, SPLIT_HEIGHT_Address),
			 scale->src_h & 0xFFFF);

		dc_write(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, SCALER_DEST_WIDTH_Address),
			 scale->dst_w & 0xFFFF);
		dc_write(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, SCALER_DEST_HEIGHT_Address),
			 scale->dst_h & 0xFFFF);

		dc_write(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, HSCALE_FACTOR_Address),
			 scale->factor_x);
		dc_write(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, VSCALE_FACTOR_Address),
			 scale->factor_y);

		for (i = 0; i < VS_SUBPIXELLOADCOUNT * 9 / 2 + 1; i++)
			dc_write(hw,
				 VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, HSCALE_COEF_DATA_Address) +
					 (i * base_offset),
				 scale->coef_h[i]);

		for (i = 0; i < VS_SUBPIXELLOADCOUNT * 5 / 2 + 1; i++)
			dc_write(hw,
				 VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, VSCALE_COEF_DATA_Address) +
					 (i * base_offset),
				 scale->coef_v[i]);

		dc_write(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, SCALE_INITIAL_OFFSET_X_Address),
			 VS_SET_FIELD(0, DCREG_SH_WB0_SCALE_INITIAL_OFFSET, X_VALUE,
				      initial_offsetx));
		dc_write(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, SCALE_INITIAL_OFFSET_Y_Address),
			 VS_SET_FIELD(0, DCREG_SH_WB0_SCALE_INITIAL_OFFSET, Y_VALUE,
				      initial_offsety));
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(wb_scale_proto, "SCALER", struct drm_vs_scale_config, wb_scale_check,
			  NULL, wb_scale_config_hw);

static bool wb_r2y_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			 const void *obj_state)
{
	const struct drm_vs_r2y_config *r2y = data;

	if (r2y->mode < VS_CSC_CM_USR || r2y->mode > VS_CSC_CM_F2F) {
		dev_err(hw->dev, "%s: Unknown r2y mode %#x\n", __func__, r2y->mode);
		return false;
	}
	if (r2y->gamut < VS_CSC_CG_601 || r2y->gamut > VS_CSC_CG_SRGB) {
		dev_err(hw->dev, "%s: Unknown r2y gamut %#x\n", __func__, r2y->gamut);
		return false;
	}
	return true;
}

static bool wb_r2y_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_r2y_config *r2y = data;
	u32 r2y_config = 0;

	/* write back RGB2YUV configuration */
	r2y_config = dc_read(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, CONFIG_Address));

	if (enable) {
		r2y_config = VS_SET_FIELD_PREDEF(r2y_config, DCREG_SH_WB0_CONFIG, R2Y, ENABLED);

		switch (r2y->mode) {
		case VS_CSC_CM_USR:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config, DCREG_SH_WB0_CONFIG, R2Y_MODE,
							 PROGRAMMABLE);
			break;
		case VS_CSC_CM_L2L:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config, DCREG_SH_WB0_CONFIG, R2Y_MODE,
							 LIMIT_RGB_2_LIMIT_YUV);
			break;
		case VS_CSC_CM_L2F:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config, DCREG_SH_WB0_CONFIG, R2Y_MODE,
							 LIMIT_RGB_2_FULL_YUV);
			break;
		case VS_CSC_CM_F2L:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config, DCREG_SH_WB0_CONFIG, R2Y_MODE,
							 FULL_RGB_2_LIMIT_YUV);
			break;
		case VS_CSC_CM_F2F:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config, DCREG_SH_WB0_CONFIG, R2Y_MODE,
							 FULL_RGB_2_FULL_YUV);
			break;
		default:
			break;
		}

		switch (r2y->gamut) {
		case VS_CSC_CG_601:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config, DCREG_SH_WB0_CONFIG, R2Y_GAMUT,
							 BT601);
			break;
		case VS_CSC_CG_709:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config, DCREG_SH_WB0_CONFIG, R2Y_GAMUT,
							 BT709);
			break;
		case VS_CSC_CG_2020:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config, DCREG_SH_WB0_CONFIG, R2Y_GAMUT,
							 BT2020);
			break;
		case VS_CSC_CG_P3:
			r2y_config =
				VS_SET_FIELD_PREDEF(r2y_config, DCREG_SH_WB0_CONFIG, R2Y_GAMUT, P3);
			break;
		case VS_CSC_CG_SRGB:
			r2y_config = VS_SET_FIELD_PREDEF(r2y_config, DCREG_SH_WB0_CONFIG, R2Y_GAMUT,
							 SRGB);
			break;
		default:
			break;
		}

		if (r2y->mode == VS_CSC_CM_USR)
			dc_write_u32_blob(
				hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, RGB_TO_YUV_COEF0_Address),
				r2y->coef, VS_MAX_R2Y_COEF_NUM);
	} else {
		r2y_config = VS_SET_FIELD_PREDEF(r2y_config, DCREG_SH_WB0_CONFIG, R2Y, DISABLED);
	}
	dc_write(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, CONFIG_Address), r2y_config);
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(wb_r2y_proto, "R2Y", struct drm_vs_r2y_config, wb_r2y_check, NULL,
			  wb_r2y_config_hw);

static bool wb_spliter_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			     const void *obj_state)
{
	const struct drm_vs_wb_spliter *spliter = data;

	/* The max size of src image width is 4096. */
	if (spliter->split_rect1.w + spliter->split_rect1.x > 4096) {
		dev_err(hw->dev, "%s: The input image width > 4096 in using writeback spliter.\n",
			__func__);
		return false;
	}

	/* The	max size of spliter out width is 2560. */
	if (spliter->split_rect0.w > 2560 || spliter->split_rect1.w > 2560) {
		dev_err(hw->dev, "%s: THe spliter max out width is > 2560.\n", __func__);
		return false;
	}

	if (wb_spliter_busy) {
		dev_err(hw->dev, "%s: The writeback spliter is busy.\n", __func__);
		return false;
	}

	return true;
}

static bool wb_spliter_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_wb_spliter *spliter = data;

	vs_dpu_link_node_config(hw, VS_DPU_LINK_WB_SPILTER, hw_id, hw->wb[hw_id].fb.display_id,
				enable);

	if (enable) {
		dc_write(hw, DCREG_SH_WB0_SPLITER_COORD_X_Address, spliter->split_rect0.x);
		dc_write(hw, DCREG_SH_WB0_SPLIT_WIDTH_Address, spliter->split_rect0.w);
		dc_write(hw, DCREG_SH_WB1_SPLITER_COORD_X_Address, spliter->split_rect1.x);
		dc_write(hw, DCREG_SH_WB1_SPLIT_WIDTH_Address, spliter->split_rect1.w);

		wb_split_id = hw_id;
		wb_spliter_busy = true;
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(wb_spliter_proto, "SPLITER", struct drm_vs_wb_spliter, wb_spliter_check,
			  NULL, wb_spliter_config_hw);

static const struct drm_prop_enum_list wb_rotation_props[] = {
	{ __builtin_ffs(DRM_MODE_ROTATE_0) - 1, "rotate-0" },
	{ __builtin_ffs(DRM_MODE_ROTATE_90) - 1, "rotate-90" },
	{ __builtin_ffs(DRM_MODE_ROTATE_180) - 1, "rotate-180" },
	{ __builtin_ffs(DRM_MODE_ROTATE_270) - 1, "rotate-270" },
	{ __builtin_ffs(DRM_MODE_REFLECT_X) - 1, "reflect-x" },
	{ __builtin_ffs(DRM_MODE_REFLECT_Y) - 1, "reflect-y" },
};

/* writeback rotation */
static bool wb_rotation_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			      const void *obj_state)
{
	const u8 rotation = to_vs_rotation(*((u32 *)data));

	if (rotation > DCREG_SH_WB0_CONFIG_ROT_ANGLE_FLIPY_ROT90) {
		dev_err(hw->dev, "%s the rotation mode out of range.\n", __func__);
		return false;
	}

	return true;
}

static bool wb_rotation_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const u8 rotation = to_vs_rotation(*((u32 *)data));
	const u32 offset = vs_dc_get_wb_offset(hw_id);
	u32 config;
	struct dc_hw_fb *fb = &hw->wb[hw_id].fb;
	u16 width = fb->width, height = fb->height;

	config = dc_read(hw, DCREG_SH_WB0_CONFIG_Address + offset);
	config = VS_SET_FIELD(config, DCREG_SH_WB0_CONFIG, ROT_ANGLE, rotation);
	dc_write(hw, DCREG_SH_WB0_CONFIG_Address + offset, config);

	/* when wb rotation 90/270/FLIPX_90/FLIPY_90, image width and height need to be exchanged */
	if (rotation == ROT_90 || rotation == ROT_270 || rotation == FLIPX_90 ||
	    rotation == FLIPY_90) {
		dc_write(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, IMAGE_WIDTH_Address),
			 height & 0xFFFF);
		dc_write(hw, VS_SET_WB_FIELD(DCREG_SH_WB, hw_id, IMAGE_HEIGHT_Address),
			 width & 0xFFFF);
	}

	return true;
}

/* wb rotation @supproted_bits will be initialized actually in vs_dc_register_writeback_states() */
VS_DC_BITMASK_PROPERTY_PROTO(wb_rotation_proto, "WB_ROTATION", wb_rotation_props, 0,
			     wb_rotation_check, NULL, wb_rotation_config_hw);

bool vs_dc_register_writeback_states(struct vs_dc_property_state_group *states,
				     const struct vs_wb_info *wb_info)
{
	if (wb_info->spliter)
		__ERR_CHECK(vs_dc_property_register_state(states, &wb_spliter_proto), on_error);
	if (wb_info->dither)
		__ERR_CHECK(vs_dc_property_register_state(states, &wb_dither_proto), on_error);
	if ((wb_info->min_scale != (1 << 16) || wb_info->max_scale != (1 << 16)) &&
	    (wb_info->min_scale != 0 || wb_info->max_scale != 0))
		__ERR_CHECK(vs_dc_property_register_state(states, &wb_scale_proto), on_error);
	if (wb_info->csc)
		__ERR_CHECK(vs_dc_property_register_state(states, &wb_r2y_proto), on_error);
	if (wb_info->rotation != DRM_MODE_ROTATE_0) {
		wb_rotation_proto.sub_proto.enum_list.supported_bits = wb_info->rotation;
		__ERR_CHECK(vs_dc_property_register_state(states, &wb_rotation_proto), on_error);
	}

	return true;
on_error:
	return false;
}
