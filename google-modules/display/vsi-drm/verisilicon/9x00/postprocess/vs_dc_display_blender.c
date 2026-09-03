/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */

#include "vs_dc_display_blender.h"

#include "drm/vs_drm.h"
#include "vs_dc_property.h"
#include "vs_dc_hw.h"
#include "vs_dc_reg_be.h"
#include "vs_dc_lut.h"

#define UPDATE_BLEND_CONFIG(hw, hw_id, field, value)                    \
	do {                                                            \
		if ((u32)hw_id < 2) {                                   \
			UPDATE_BLEND01_CONFIG(hw, hw_id, field, value); \
		} else {                                                \
			UPDATE_BLEND23_CONFIG(hw, hw_id, field, value); \
		}                                                       \
	} while (0)

#define UPDATE_BLEND01_CONFIG(hw, hw_id, field, value)                                         \
	do {                                                                                   \
		const u32 __reg =                                                              \
			VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, BLEND_CONFIG_Address);     \
		u32 __config = dc_read(hw, __reg);                                             \
		__config = VS_SET_FIELD(__config, DCREG_SH_PANEL0_BLEND_CONFIG, field, value); \
		dc_write(hw, __reg, __config);                                                 \
	} while (0)

#define UPDATE_BLEND23_CONFIG(hw, hw_id, field, value)                                         \
	do {                                                                                   \
		const u32 __reg =                                                              \
			VS_SET_PANEL23_FIELD(DCREG_SH_PANEL, hw_id, BLEND_CONFIG_Address);     \
		u32 __config = dc_read(hw, __reg);                                             \
		__config = VS_SET_FIELD(__config, DCREG_SH_PANEL2_BLEND_CONFIG, field, value); \
		dc_write(hw, __reg, __config);                                                 \
	} while (0)

static bool blender_dither_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
				 const void *obj_state)
{
	const struct drm_vs_blender_dither *dither = data;

	if (dither->index_type == VS_DTH_FRM_IDX_NONE || dither->index_type > VS_DTH_FRM_IDX_HW) {
		dev_err(hw->dev, "%s: vs_crtc[%u] has invalid frame index type.\n", __func__,
			hw_id);
		return false;
	}
	if (dither->index_type == VS_DTH_FRM_IDX_SW && dither->sw_index > 7) {
		dev_err(hw->dev, "%s: vs_crtc[%u] has invalid sw index.\n", __func__, hw_id);
		return false;
	}
	return true;
}

static bool blender_dither_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_blender_dither *dither = data;
	const u32 reg_config = VS_SH_PANEL_FIELD(hw_id, BLEND_CONFIG_Address);
	const u32 reg_start_x = VS_SH_PANEL_FIELD(hw_id, BLEND_DTH_START_X_Address);
	const u32 reg_start_y = VS_SH_PANEL_FIELD(hw_id, BLEND_DTH_START_Y_Address);
	const u32 reg_nl = VS_SH_PANEL_FIELD(hw_id, BLEND_DTH_NL_Address);
	const u32 reg_prng_xm = VS_SH_PANEL_FIELD(hw_id, BLEND_DTH_PRNG_XM_Address);
	const u32 reg_dth_config = VS_SH_PANEL_FIELD(hw_id, BLEND_DTH_CONFIG_Address);
	const u32 reg_fi_sel = VS_SH_PANEL_FIELD(hw_id, BLEND_DTH_FI_SEL_Address);
	const u32 reg_hs_x0 = VS_SH_PANEL_FIELD(hw_id, BLEND_DTH_HS_X0_Address);
	const u32 reg_hs_y0 = VS_SH_PANEL_FIELD(hw_id, BLEND_DTH_HS_Y0_Address);
	const u32 reg_ps_a1 = VS_SH_PANEL_FIELD(hw_id, BLEND_DTH_PS_A1_NUM0_Address);
	const u32 reg_ps_a2 = VS_SH_PANEL_FIELD(hw_id, BLEND_DTH_PS_A2_NUM0_Address);
	u32 config;

	if (hw_id >= HW_DISPLAY_4) {
		config = dc_read(hw, DCREG_SH_WB_BLEND_CONFIG_Address);
		dc_write(hw, DCREG_SH_WB_BLEND_CONFIG_Address,
			 VS_SET_FIELD(config, DCREG_SH_WB_BLEND_CONFIG, RANDOM_DITHER, enable));
		if (enable) {
			dc_write(hw, DCREG_SH_WB_BLEND_DTH_START_X_Address, dither->start_x);
			dc_write(hw, DCREG_SH_WB_BLEND_DTH_START_Y_Address, dither->start_y);
			dc_write(hw, DCREG_SH_WB_BLEND_DTH_NL_Address, dither->noise);
			dc_write(hw, DCREG_SH_WB_BLEND_DTH_PRNG_XM_Address, dither->mask);
			config = dc_read(hw, DCREG_SH_WB_BLEND_DTH_CONFIG_Address);
			if (dither->index_type == VS_DTH_FRM_IDX_SW) {
				config = VS_SET_FIELD_VALUE(config, DCREG_SH_WB_BLEND_DTH_CONFIG,
							    TEMP_DITHER, DISABLED);
				dc_write(hw, DCREG_SH_WB_BLEND_DTH_FI_SEL_Address,
					 VS_SET_FIELD(0, DCREG_SH_WB_BLEND_DTH_FI_SEL, VALUE,
						      dither->sw_index));
			} else {
				config = VS_SET_FIELD_VALUE(config, DCREG_SH_WB_BLEND_DTH_CONFIG,
							    TEMP_DITHER, ENABLED);
			}
			dc_write(hw, DCREG_SH_WB_BLEND_DTH_CONFIG_Address, config);
			/* seed */
			if (dither->seed.hash_seed_x_enable)
				dc_write_u32_blob(hw, DCREG_SH_WB_BLEND_DTH_HS_X0_Address,
						  dither->seed.hash_seed_x,
						  VS_RANDOM_DITHER_SEED_NUM);
			if (dither->seed.hash_seed_y_enable)
				dc_write_u32_blob(hw, DCREG_SH_WB_BLEND_DTH_HS_Y0_Address,
						  dither->seed.hash_seed_y,
						  VS_RANDOM_DITHER_SEED_NUM);
			if (dither->seed.permut_seed1_enable)
				dc_write_u32_blob(hw, DCREG_SH_WB_BLEND_DTH_PS_A1_NUM0_Address,
						  dither->seed.permut_seed1,
						  VS_RANDOM_DITHER_SEED_NUM);
			if (dither->seed.permut_seed2_enable)
				dc_write_u32_blob(hw, DCREG_SH_WB_BLEND_DTH_PS_A2_NUM0_Address,
						  dither->seed.permut_seed2,
						  VS_RANDOM_DITHER_SEED_NUM);
		}

		return true;
	}

	config = dc_read(hw, reg_config);
	switch (hw_id) {
	case HW_DISPLAY_0:
	case HW_DISPLAY_1:
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_BLEND_CONFIG, RANDOM_DITHER, enable);
		break;
	case HW_DISPLAY_2:
	case HW_DISPLAY_3:
		config = VS_SET_FIELD(config, DCREG_SH_PANEL2_BLEND_CONFIG, RANDOM_DITHER, enable);
		break;
	}
	dc_write(hw, reg_config, config);
	if (enable) {
		dc_write(hw, reg_start_x, dither->start_x);
		dc_write(hw, reg_start_y, dither->start_y);
		dc_write(hw, reg_nl, dither->noise);
		dc_write(hw, reg_prng_xm, dither->mask);

		/* index */
		config = dc_read(hw, reg_dth_config);
		if (dither->index_type == VS_DTH_FRM_IDX_SW) {
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_BLEND_DTH_CONFIG,
						    TEMP_DITHER, DISABLED);
			dc_write(hw, reg_fi_sel,
				 VS_SET_FIELD(0, DCREG_SH_PANEL0_BLEND_DTH_FI_SEL, VALUE,
					      dither->sw_index));
		} else {
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_BLEND_DTH_CONFIG,
						    TEMP_DITHER, ENABLED);
		}
		dc_write(hw, reg_dth_config, config);
		/* seed */
		if (dither->seed.hash_seed_x_enable)
			dc_write_u32_blob(hw, reg_hs_x0, dither->seed.hash_seed_x,
					  VS_RANDOM_DITHER_SEED_NUM);
		if (dither->seed.hash_seed_y_enable)
			dc_write_u32_blob(hw, reg_hs_y0, dither->seed.hash_seed_y,
					  VS_RANDOM_DITHER_SEED_NUM);
		if (dither->seed.permut_seed1_enable)
			dc_write_u32_blob(hw, reg_ps_a1, dither->seed.permut_seed1,
					  VS_RANDOM_DITHER_SEED_NUM);
		if (dither->seed.permut_seed2_enable)
			dc_write_u32_blob(hw, reg_ps_a2, dither->seed.permut_seed2,
					  VS_RANDOM_DITHER_SEED_NUM);
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(blender_dither_proto, "BLD_DITHER", struct drm_vs_blender_dither,
			  blender_dither_check, NULL, blender_dither_config_hw);

static bool eotf_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
		       const void *obj_state)
{
	const struct vs_dc_info *info = hw->info;
	const u32 in_bit = info->bld_cgm_bits;
	const u32 out_bit = info->bld_cgm_bits;
	const u32 max_seg = info->max_seg_num;
	const u32 max_entry = info->max_eotf_size;
	const struct drm_vs_xstep_lut *lut = data;

	if (!lut)
		return true;
	if (lut->seg_cnt > max_seg) {
		dev_err(hw->dev, "%s: Invalid entry count.\n", __func__);
		return false;
	}
	if (lut->entry_cnt > max_entry) {
		dev_err(hw->dev, "%s: Invalid entry count.\n", __func__);
		return false;
	}

	if (!vs_dc_lut_check_xstep(hw, lut->seg_step, lut->seg_cnt, in_bit))
		return false;
	if (!vs_dc_lut_check_data(hw, lut->data, lut->entry_cnt, out_bit))
		return false;
	return true;
}

static bool eotf_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_xstep_lut *lut = data;
	const u32 reg_seg_cnt =
		VS_SET_PANEL23_FIELD(DCREG_SH_PANEL, hw_id, BLEND_EOTF_SEGMENT_COUNT_Address);
	const u32 reg_seg_pnt =
		VS_SET_PANEL23_FIELD(DCREG_SH_PANEL, hw_id, BLEND_EOTF_SEGMENT_POINT1_Address);
	const u32 reg_seg_step =
		VS_SET_PANEL23_FIELD(DCREG_SH_PANEL, hw_id, BLEND_EOTF_SEGMENT_STEP_Address);
	const u32 reg_seg_data =
		VS_SET_PANEL23_FIELD(DCREG_SH_PANEL, hw_id, BLEND_EOTF_DATA_Address);

	UPDATE_BLEND23_CONFIG(hw, hw_id, EOTF, enable);
	if (enable) {
		dc_write(hw, reg_seg_cnt, lut->seg_cnt - 1);
		dc_write_u32_blob(hw, reg_seg_pnt, lut->seg_point, lut->seg_cnt - 1);
		dc_write_u32_blob(hw, reg_seg_step, lut->seg_step, lut->seg_cnt);
		dc_write_u32_blob(hw, reg_seg_data, lut->data, lut->entry_cnt);
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(eotf_proto, "BLD_EOTF", struct drm_vs_xstep_lut, eotf_check, NULL,
			  eotf_config_hw);

static bool bld_gamut_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			    const void *obj_state)
{
	const struct drm_vs_gamut_map *gamut = data;

	if (gamut->mode > VS_GAMUT_USER_DEF) {
		dev_info(hw->dev, "%s: Invalid gamut map mode %#x\n", __func__, gamut->mode);
		return false;
	}
	return true;
}

static bool bld_gamut_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_gamut_map *gamut = data;
	const u32 reg_coef =
		VS_SET_PANEL23_FIELD(DCREG_SH_PANEL, hw_id, BLEND_GAMUT_MATRIX_COEF0_Address);
	const u32 reg_offset = VS_SET_PANEL23_FIELD(DCREG_SH_PANEL, hw_id,
						    BLEND_GAMUT_MATRIX_COEF_OFFSET0_Address);

	UPDATE_BLEND23_CONFIG(hw, hw_id, GAMUT_MATRIX, enable);

	if (enable) {
		dc_write_u32_blob(hw, reg_coef, gamut->coef, 9);
		dc_write_u32_blob(hw, reg_offset, &gamut->coef[9], 3);
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(gamut_proto, "BLD_GAMUT_MAP", struct drm_vs_gamut_map, bld_gamut_check,
			  NULL, bld_gamut_config_hw);

static bool oetf_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
		       const void *obj_state)
{
	const struct vs_dc_info *info = hw->info;
	const u32 in_bit = info->bld_cgm_bits;
	const u32 out_bit = info->bld_cgm_bits;
	const u32 max_seg = info->max_seg_num;
	const u32 max_entry = info->max_oetf_size;
	const struct drm_vs_xstep_lut *lut = data;

	if (!lut)
		return true;

	if (lut->seg_cnt > max_seg) {
		dev_err(hw->dev, "%s: Invalid entry count.\n", __func__);
		return false;
	}
	if (lut->entry_cnt > max_entry) {
		dev_err(hw->dev, "%s: Invalid entry count.\n", __func__);
		return false;
	}

	if (!vs_dc_lut_check_xstep(hw, lut->seg_step, lut->seg_cnt, in_bit))
		return false;
	if (!vs_dc_lut_check_data(hw, lut->data, lut->entry_cnt, out_bit))
		return false;

	return true;
}

static bool oetf_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	const struct drm_vs_xstep_lut *lut = data;
	const u32 reg_seg_cnt = (hw_id < HW_DISPLAY_4) ?
					VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id,
							   BLEND_OETF_SEGMENT_COUNT_Address) :
					DCREG_SH_WB_BLEND_OETF_SEGMENT_COUNT_Address;
	const u32 reg_seg_pnt = (hw_id < HW_DISPLAY_4) ?
					VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id,
							   BLEND_OETF_SEGMENT_POINT1_Address) :
					DCREG_SH_WB_BLEND_OETF_SEGMENT_POINT1_Address;
	const u32 reg_seg_step =
		(hw_id < HW_DISPLAY_4) ?
			VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, BLEND_OETF_SEGMENT_STEP_Address) :
			DCREG_SH_WB_BLEND_OETF_SEGMENT_STEP_Address;
	const u32 reg_seg_data =
		(hw_id < HW_DISPLAY_4) ?
			VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, BLEND_OETF_DATA_Address) :
			DCREG_SH_WB_BLEND_OETF_DATA_Address;

	if (hw_id > HW_DISPLAY_3) {
		config = dc_read(hw, DCREG_SH_WB_BLEND_CONFIG_Address);
		config = VS_SET_FIELD(config, DCREG_SH_WB_BLEND_CONFIG, OETF, enable);
		dc_write(hw, DCREG_SH_WB_BLEND_CONFIG_Address, config);
	} else {
		UPDATE_BLEND_CONFIG(hw, hw_id, OETF, enable);
	}
	if (enable) {
		dc_write(hw, reg_seg_cnt, lut->seg_cnt - 1);
		dc_write_u32_blob(hw, reg_seg_pnt, lut->seg_point, lut->seg_cnt - 1);
		dc_write_u32_blob(hw, reg_seg_step, lut->seg_step, lut->seg_cnt);
		dc_write_u32_blob(hw, reg_seg_data, lut->data, lut->entry_cnt);
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(oetf_proto, "BLD_OETF", struct drm_vs_xstep_lut, oetf_check, NULL,
			  oetf_config_hw);

bool vs_dc_register_display_blender_states(struct vs_dc_property_state_group *states,
					   const struct vs_display_info *display_info)
{
	if (display_info->bld_dth) {
		/* SW disabled the blend random dither by default */
		__ERR_CHECK(vs_dc_property_register_state(states, &blender_dither_proto), on_error);
		states->items[states->num - 1].enable = false;
		states->items[states->num - 1].dirty = true;
	}
	if (display_info->bld_cgm) {
		__ERR_CHECK(vs_dc_property_register_state(states, &eotf_proto), on_error);
		__ERR_CHECK(vs_dc_property_register_state(states, &gamut_proto), on_error);
	}
	if (display_info->bld_oetf)
		__ERR_CHECK(vs_dc_property_register_state(states, &oetf_proto), on_error);
	return true;
on_error:
	return false;
}
