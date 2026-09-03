/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */

#include "vs_dc_display_yuv.h"

#include "drm/vs_drm.h"
#include "vs_dc_property.h"
#include "vs_dc_hw.h"
#include "vs_dc_reg_be.h"

static bool r2y_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
		      const void *obj_state)
{
	const struct drm_vs_r2y_config *r2y = data;

	/* Writeback is handled through the standard flow in vs_dc_post.c */
	if (vs_dc_display_is_wb(hw_id))
		return true;

	if (r2y->mode > VS_CSC_CM_F2F) {
		dev_err(hw->dev, "%s: Invalid r2y mode %#x\n", __func__, r2y->mode);
		return false;
	}
	if (r2y->gamut > VS_CSC_CG_SRGB) {
		dev_err(hw->dev, "%s: Invalid r2y gamut %#x\n", __func__, r2y->gamut);
		return false;
	}
	return true;
}

static bool display_r2y_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_r2y_config *r2y = data;
	const u32 reg_config = VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address);
	const u32 reg_coef = VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, RGB_TO_YUV_COEF0_Address);
	u32 r2y_config;

	r2y_config = dc_read(hw, reg_config);
	if (enable) {
		r2y_config = VS_SET_PANEL_FIELD_PREDEF(r2y_config, hw_id, CONFIG, R2Y, ENABLED);

		switch (r2y->mode) {
		case VS_CSC_CM_USR:
			r2y_config = VS_SET_PANEL_FIELD_PREDEF(r2y_config, hw_id, CONFIG, R2Y_MODE,
							       PROGRAMMABLE);
			break;
		case VS_CSC_CM_L2L:
			r2y_config = VS_SET_PANEL_FIELD_PREDEF(r2y_config, hw_id, CONFIG, R2Y_MODE,
							       LIMIT_RGB_2_LIMIT_YUV);
			break;
		case VS_CSC_CM_L2F:
			r2y_config = VS_SET_PANEL_FIELD_PREDEF(r2y_config, hw_id, CONFIG, R2Y_MODE,
							       LIMIT_RGB_2_FULL_YUV);
			break;
		case VS_CSC_CM_F2L:
			r2y_config = VS_SET_PANEL_FIELD_PREDEF(r2y_config, hw_id, CONFIG, R2Y_MODE,
							       FULL_RGB_2_LIMIT_YUV);
			break;
		case VS_CSC_CM_F2F:
			r2y_config = VS_SET_PANEL_FIELD_PREDEF(r2y_config, hw_id, CONFIG, R2Y_MODE,
							       FULL_RGB_2_FULL_YUV);
			break;
		default:
			break;
		}

		switch (r2y->gamut) {
		case VS_CSC_CG_601:
			r2y_config = VS_SET_PANEL_FIELD_PREDEF(r2y_config, hw_id, CONFIG, R2Y_GAMUT,
							       BT601);
			break;
		case VS_CSC_CG_709:
			r2y_config = VS_SET_PANEL_FIELD_PREDEF(r2y_config, hw_id, CONFIG, R2Y_GAMUT,
							       BT709);
			break;
		case VS_CSC_CG_2020:
			r2y_config = VS_SET_PANEL_FIELD_PREDEF(r2y_config, hw_id, CONFIG, R2Y_GAMUT,
							       BT2020);
			break;
		case VS_CSC_CG_P3:
			r2y_config =
				VS_SET_PANEL_FIELD_PREDEF(r2y_config, hw_id, CONFIG, R2Y_GAMUT, P3);
			break;
		case VS_CSC_CG_SRGB:
			r2y_config = VS_SET_PANEL_FIELD_PREDEF(r2y_config, hw_id, CONFIG, R2Y_GAMUT,
							       SRGB);
			break;
		default:
			break;
		}
		if (r2y->mode == VS_CSC_CM_USR)
			dc_write_u32_blob(hw, reg_coef, r2y->coef, VS_MAX_R2Y_COEF_NUM);
	} else {
		r2y_config = VS_SET_PANEL_FIELD_PREDEF(r2y_config, hw_id, CONFIG, R2Y, DISABLED);
	}
	dc_write(hw, reg_config, r2y_config);
	return true;
}

static bool r2y_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	/*
	 * R2Y for display output is handled through the programming logic here.
	 * WB logic is handled through the standard flow for WB properties.
	 */
	if (hw_id <= HW_DISPLAY_3)
		return display_r2y_config_hw(hw, hw_id, enable, data);

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(r2y_proto, "R2Y", struct drm_vs_r2y_config, r2y_check, NULL,
			  r2y_config_hw);

static bool uv_downsample_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
				const void *obj_state)
{
	const struct drm_vs_ds_config *ds = data;

	if (ds->h_mode > VS_DS_FILTER) {
		dev_err(hw->dev, "%s: Invalid downsample h_mode %#x\n", __func__, ds->h_mode);
		return false;
	}

	if (vs_dc_display_is_wb(hw_id)) {
		if (ds->v_mode > VS_DS_FILTER) {
			dev_err(hw->dev, "%s: Invalid downsample v_mode %#x\n", __func__,
				ds->v_mode);
			return false;
		}
	}
	return true;
}

static bool uv_downsample_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_ds_config *downsample = data;
	const u32 ds_addr[HW_DISPLAY_NUM] = { DCREG_SH_PANEL0_UV_DOWN_SAMPLE_Address,
					      DCREG_SH_PANEL1_UV_DOWN_SAMPLE_Address,
					      DCREG_SH_PANEL2_UV_DOWN_SAMPLE_Address,
					      DCREG_SH_PANEL3_UV_DOWN_SAMPLE_Address,
					      DCREG_SH_BLD_WB_UV_DOWN_SAMPLE_Address };
	u32 config = 0;

	if (enable) {
		/* For YUV422 just do horizontal down sample */
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_UV_DOWN_SAMPLE, HORI_DS_MODE,
				      downsample->h_mode);
		/* For blend writeback path, support YUV420 format,
		 * need do horizontal and vertical downsample
		 */
		if (vs_dc_display_is_wb(hw_id))
			config = VS_SET_FIELD(config, DCREG_SH_BLD_WB_UV_DOWN_SAMPLE, VERTI_DS_MODE,
					      downsample->v_mode);
		dc_write(hw, ds_addr[hw_id], config);
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(uv_downsample_proto, "DOWN_SAMPLE", struct drm_vs_ds_config,
			  uv_downsample_check, NULL, uv_downsample_config_hw);

bool vs_dc_register_display_r2y_states(struct vs_dc_property_state_group *states,
				       const struct vs_display_info *info)
{
	if (info->color_formats & (DRM_COLOR_FORMAT_YCBCR422 | DRM_COLOR_FORMAT_YCBCR420))
		__ERR_CHECK(vs_dc_property_register_state(states, &uv_downsample_proto), on_error);
	if (info->color_formats &
	    (DRM_COLOR_FORMAT_YCBCR444 | DRM_COLOR_FORMAT_YCBCR422 | DRM_COLOR_FORMAT_YCBCR420))
		__ERR_CHECK(vs_dc_property_register_state(states, &r2y_proto), on_error);
	return true;
on_error:
	return false;
}
