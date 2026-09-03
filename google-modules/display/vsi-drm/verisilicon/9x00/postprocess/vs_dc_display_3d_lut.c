/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */

#include "vs_dc_display_3d_lut.h"

#include <drm/drm_property.h>
#include "drm/vs_drm.h"
#include "vs_dc_hw.h"
#include "vs_dc_info.h"
#include "vs_dc_reg_be.h"
#include "vs_dc_property.h"

static bool _3d_lut_check(const struct dc_hw *hw, const struct drm_vs_color *lut, u32 size,
			  u32 lut_bit)
{
	u32 i;

	for (i = 0; i < size; i++) {
		if ((lut[i].r >> lut_bit) || (lut[i].g >> lut_bit) || (lut[i].b >> lut_bit)) {
			dev_err(hw->dev,
				"%s: The entry of 3D LUT %u(%u, %u, %u) over valid bit(%u).\n",
				__func__, i, lut[i].r, lut[i].g, lut[i].g, lut_bit);
			return false;
		}
	}
	return true;
}

static bool prior_3d_lut_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			       const void *obj_state)
{
	const struct vs_dc_info *info = hw->info;

	return _3d_lut_check(hw, data, VS_MAX_PRIOR_3DLUT_SIZE, info->cgm_lut_bits);
}

static bool prior_3d_lut_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const u32 reg_index = VS_SET_PANEL01_FIELD(DCREG_PANEL, hw_id, LUT3D_INDEX_Address);
	const u32 reg_red = VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LUT3D_RED_DATA_Address);
	const u32 reg_green = VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LUT3D_GREEN_DATA_Address);
	const u32 reg_blue = VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LUT3D_BLUE_DATA_Address);
	const struct drm_vs_color *lut = data;
	u32 i;

	UPDATE_PANEL_CONFIG(hw, hw_id, LUT3D, enable);
	if (enable) {
		/* set the index start from 0 */
		dc_write(hw, reg_index, 0);

		/* coef data of RED/GREEN/BLUE channel*/
		for (i = 0; i < VS_MAX_PRIOR_3DLUT_SIZE; i++) {
			dc_write_relaxed(hw, reg_red, lut[i].r);
			dc_write_relaxed(hw, reg_green, lut[i].g);
			dc_write_relaxed(hw, reg_blue, lut[i].b);
		}
	}
	return true;
}

VS_DC_ARRAY_PROPERTY_PROTO(prior_3d_lut_proto, "PRIOR_3DLUT", struct drm_vs_color,
			   VS_MAX_PRIOR_3DLUT_SIZE, false, prior_3d_lut_check, NULL,
			   prior_3d_lut_config_hw);

static bool _3d_lut_check_rgb16(const struct dc_hw *hw, const struct drm_vs_color_rgb16 *lut,
				u32 size, u32 lut_bit)
{
	u32 i;

	for (i = 0; i < size; i++) {
		if ((lut[i].r >> lut_bit) || (lut[i].g >> lut_bit) || (lut[i].b >> lut_bit)) {
			dev_err(hw->dev,
				"%s: The entry of 3D LUT %u(%u, %u, %u) over valid bit(%u).\n",
				__func__, i, lut[i].r, lut[i].g, lut[i].g, lut_bit);
			return false;
		}
	}
	return true;
}

static bool prior_3d_lut_check_rgb16(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			       const void *obj_state)
{
	const struct vs_dc_info *info = hw->info;

	return _3d_lut_check_rgb16(hw, data, VS_MAX_PRIOR_3DLUT_SIZE, info->cgm_lut_bits);
}

static bool prior_3d_lut_config_hw_rgb16(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const u32 reg_index = VS_SET_PANEL01_FIELD(DCREG_PANEL, hw_id, LUT3D_INDEX_Address);
	const u32 reg_red = VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LUT3D_RED_DATA_Address);
	const u32 reg_green = VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LUT3D_GREEN_DATA_Address);
	const u32 reg_blue = VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LUT3D_BLUE_DATA_Address);
	const struct drm_vs_color_rgb16 *lut = data;
	u32 i;

	UPDATE_PANEL_CONFIG(hw, hw_id, LUT3D, enable);
	if (enable) {
		/* set the index start from 0 */
		dc_write(hw, reg_index, 0);

		/* coef data of RED/GREEN/BLUE channel*/
		for (i = 0; i < VS_MAX_PRIOR_3DLUT_SIZE; i++) {
			dc_write_relaxed(hw, reg_red, lut[i].r);
			dc_write_relaxed(hw, reg_green, lut[i].g);
			dc_write_relaxed(hw, reg_blue, lut[i].b);
		}
	}
	return true;
}

VS_DC_ARRAY_PROPERTY_PROTO(prior_3d_lut_proto_rgb16, "PRIOR_3DLUT_RGB16", struct drm_vs_color_rgb16,
			   VS_MAX_PRIOR_3DLUT_SIZE, false, prior_3d_lut_check_rgb16, NULL,
			   prior_3d_lut_config_hw_rgb16);

static bool roi_3d_lut_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			     const void *obj_state)
{
	const struct vs_dc_info *info = hw->info;
	const struct drm_vs_roi_lut_config *roi = data;

	return _3d_lut_check(hw, roi->data, VS_MAX_ROI_3DLUT_SIZE, info->cgm_lut_bits);
}

static bool roi_3d_lut_config_hw(struct dc_hw *hw, u8 hw_id, const void *data, u32 reg_index,
				 u32 reg_red, u32 reg_green, u32 reg_blue, u32 reg_roi_org,
				 u32 reg_roi_size)
{
	const struct drm_vs_roi_lut_config *roi = data;
	u32 i;
	u32 config = 0;

	/* set the index start from 0 */
	dc_write(hw, reg_index, 0);

	/* coef data of RED/GREEN/BLUE channel*/
	for (i = 0; i < VS_MAX_ROI_3DLUT_SIZE; i++) {
		dc_write(hw, reg_red, roi->data[i].r);
		dc_write(hw, reg_green, roi->data[i].g);
		dc_write(hw, reg_blue, roi->data[i].b);
	}

	/* commit the 3d_lut ROI rect */
	config = VS_SET_FIELD(0, DCREG_SH_PANEL0_CC_ROI0_ORIGIN, X, roi->rect.x) |
		 VS_SET_FIELD(0, DCREG_SH_PANEL0_CC_ROI0_ORIGIN, Y, roi->rect.y);
	dc_write(hw, reg_roi_org, config);

	config = VS_SET_FIELD(0, DCREG_SH_PANEL0_CC_ROI0_SIZE, WIDTH, roi->rect.w) |
		 VS_SET_FIELD(0, DCREG_SH_PANEL0_CC_ROI0_SIZE, HEIGHT, roi->rect.h);
	dc_write(hw, reg_roi_size, config);
	return true;
}

static bool roi0_3d_lut_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	UPDATE_PANEL_CONFIG(hw, hw_id, LUT3D_EX, enable);
	UPDATE_PANEL_CONFIG_EX(hw, hw_id, CC_ROI0, enable);

	if (enable)
		return roi_3d_lut_config_hw(
			hw, hw_id, data,
			VS_SET_PANEL01_FIELD(DCREG_PANEL, hw_id, LUT3D_EX_INDEX_Address),
			VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LUT3D_EX_RED_DATA_Address),
			VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LUT3D_EX_GREEN_DATA_Address),
			VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LUT3D_EX_BLUE_DATA_Address),
			VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, CC_ROI0_ORIGIN_Address),
			VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, CC_ROI0_SIZE_Address));
	else
		return true;
}

VS_DC_BLOB_PROPERTY_PROTO(roi0_3d_lut_proto, "ROI0_3DLUT", struct drm_vs_roi_lut_config,
			  roi_3d_lut_check, NULL, roi0_3d_lut_config_hw);

bool vs_dc_register_display_3d_lut_states(struct vs_dc_property_state_group *states,
					  const struct vs_display_info *info)
{
	/* TODO: b/441508921 remove the 32bit rgba 3d lut after user space has switched rgb16 */
	if (info->cgm_lut)
		__ERR_CHECK(vs_dc_property_register_state(states, &prior_3d_lut_proto), on_error);
	if (info->cgm_lut)
		__ERR_CHECK(vs_dc_property_register_state(states, &prior_3d_lut_proto_rgb16),
			    on_error);
	if (info->lut_roi0)
		__ERR_CHECK(vs_dc_property_register_state(states, &roi0_3d_lut_proto), on_error);

	return true;
on_error:
	return false;
}
