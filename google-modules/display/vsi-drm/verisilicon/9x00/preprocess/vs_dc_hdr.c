/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */

#include "vs_dc_hdr.h"

#include "drm/drm_print.h"
#include "drm/vs_drm.h"
#include "vs_dc_hw.h"
#include "vs_dc_info.h"
#include "vs_dc_lut.h"
#include "vs_dc_property.h"
#include "vs_dc_reg_fe0.h"
#include "vs_dc_reg_fe1.h"
#include "vs_trace.h"

#define UPDATE_LAYER_CONFIG(hw, id, field, value)                                        \
	do {                                                                             \
		const u32 __reg = VS_SH_LAYER_FIELD(hw_id, CONFIG_Address);              \
		u32 __config = dc_read(hw, __reg);                                       \
		__config = VS_SET_FIELD(__config, DCREG_SH_LAYER0_CONFIG, field, value); \
		dc_write(hw, __reg, __config);                                           \
	} while (0)

static bool hdr_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	trace_config_hw_layer_feature_en("HDR", hw_id, enable);
	UPDATE_LAYER_CONFIG(hw, hw_id, BYPASS_HDR, !enable);

	return true;
}

VS_DC_BOOL_PROPERTY_PROTO(hdr_proto, "HDR", NULL, NULL, hdr_config_hw);

static bool eotf_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
		       const void *obj_state)
{
	const struct drm_vs_xstep_lut *config = data;
	const struct vs_dc_info *info = hw->info;
	u32 eotf_in_bit, eotf_out_bit, eotf_seg_max, eotf_entry_max;

	if (config) {
		eotf_in_bit = info->pre_eotf_bits;
		eotf_out_bit = info->hdr_bits;
		eotf_seg_max = info->max_seg_num;
		eotf_entry_max = info->max_eotf_size;

		/* check the effectiveness of segment count */
		if (config->seg_cnt > eotf_seg_max) {
			DRM_DEBUG_KMS("%s: Invalid EOTF segment count.\n", __func__);
			return false;
		}

		/* check the effectiveness of entry count*/
		if (config->entry_cnt > eotf_entry_max) {
			DRM_DEBUG_KMS("%s: Invalid EOTF entry count.\n", __func__);
			return false;
		}

		/* check the effectiveness of x-step */
		if (!vs_dc_lut_check_xstep(hw, config->seg_step, config->seg_cnt, eotf_in_bit))
			return false;

		if (!vs_dc_lut_check_data(hw, config->data, config->entry_cnt, eotf_out_bit))
			return false;
	}

	return true;
}

static bool eotf_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_xstep_lut *eotf = data;
	const u32 reg_segcnt = VS_SH_LAYER_FIELD(hw_id, EOTF_SEGMENT_COUNT_Address);
	const u32 reg_segpnt = VS_SH_LAYER_FIELD(hw_id, EOTF_SEGMENT_POINT1_Address);
	const u32 reg_segstp = VS_SH_LAYER_FIELD(hw_id, EOTF_SEGMENT_STEP_Address);
	const u32 reg_data = VS_SH_LAYER_FIELD(hw_id, EOTF_DATA_Address);

	trace_config_hw_layer_feature_en("EOTF", hw_id, enable);
	UPDATE_LAYER_CONFIG(hw, hw_id, EOTF, enable);

	if (enable) {
		trace_config_hw_layer_feature("EOTF_DATA", hw_id, "seg_cnt:%d entry_cnt:%d",
					      eotf->seg_cnt, eotf->entry_cnt);
		trace_hw_layer_feature_data("EOTF_DATA", hw_id, "seg_point",
					    (const u8 *)eotf->seg_point, sizeof(eotf->seg_point));
		trace_hw_layer_feature_data("EOTF_DATA", hw_id, "seg_step",
					    (const u8 *)eotf->seg_step, sizeof(eotf->seg_step));
		trace_hw_layer_feature_data("EOTF_DATA", hw_id, "data", (const u8 *)eotf->data,
					    sizeof(eotf->data));

		/* segment count start from 0, so the configured value minus 1*/
		dc_write(hw, reg_segcnt, eotf->seg_cnt - 1);

		/* commit the segment points */
		dc_write_relaxed_u32_blob(hw, reg_segpnt, eotf->seg_point, eotf->seg_cnt - 1);

		/* commit the segment steps */
		dc_write_relaxed_u32_blob(hw, reg_segstp, eotf->seg_step, eotf->seg_cnt);

		/* commit the coef data of each entry */
		dc_write_relaxed_u32_blob(hw, reg_data, eotf->data, eotf->entry_cnt);
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(eotf_proto, "EOTF", struct drm_vs_xstep_lut, eotf_check, NULL,
			  eotf_config_hw);

static bool gamut_map_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			    const void *obj_state)
{
	const struct drm_vs_gamut_map *gamut_map = data;

	if (gamut_map->mode > VS_GAMUT_USER_DEF) {
		dev_err(hw->dev, "%s: Invalid gamut map mode %#x\n", __func__, gamut_map->mode);
		return false;
	}

	return true;
}

static bool gamut_map_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_gamut_map *gamut_map = data;
	const u32 reg_coef_offset = 0x04;
	const u32 reg_coef0 = VS_SH_LAYER_FIELD(hw_id, GAMUT_MATRIX_COEF0_Address);
	const u32 reg_offset0 = VS_SH_LAYER_FIELD(hw_id, GAMUT_MATRIX_COEF_OFFSET0_Address);
	const u32 reg_offset1 = VS_SH_LAYER_FIELD(hw_id, GAMUT_MATRIX_COEF_OFFSET1_Address);
	const u32 reg_offset2 = VS_SH_LAYER_FIELD(hw_id, GAMUT_MATRIX_COEF_OFFSET2_Address);
	const u32 coef_size = 9;
	u32 i = 0;

	trace_config_hw_layer_feature_en("GAMUT_MAP", hw_id, enable);
	UPDATE_LAYER_CONFIG(hw, hw_id, GAMUT_MAPPING, enable);

	if (enable) {
		trace_config_hw_layer_feature("GAMUT_MAP_DATA", hw_id, "mode:%d", gamut_map->mode);
		trace_hw_layer_feature_data("GAMUT_MAP_DATA", hw_id, "coef",
					    (const u8 *)gamut_map->coef,
					    coef_size * sizeof(gamut_map->coef[0]));
		/* Gamut Matrix coef0 ~ coef8 config */
		for (i = 0; i < coef_size; i++) {
			dc_write(hw, reg_coef0 + i * reg_coef_offset,
				 VS_SET_FIELD(0, DCREG_SH_LAYER0_GAMUT_MATRIX_COEF0, VALUE,
					      gamut_map->coef[i]));
		}

		/* Gamut Matrix coef offset0 ~ offset2 config */
		dc_write(hw, reg_offset0,
			 VS_SET_FIELD(0, DCREG_SH_LAYER0_GAMUT_MATRIX_COEF_OFFSET0, VALUE,
				      gamut_map->coef[9]));
		dc_write(hw, reg_offset1,
			 VS_SET_FIELD(0, DCREG_SH_LAYER0_GAMUT_MATRIX_COEF_OFFSET1, VALUE,
				      gamut_map->coef[10]));
		dc_write(hw, reg_offset2,
			 VS_SET_FIELD(0, DCREG_SH_LAYER0_GAMUT_MATRIX_COEF_OFFSET2, VALUE,
				      gamut_map->coef[11]));
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(gamut_map_proto, "GAMUT_MAP", struct drm_vs_gamut_map, gamut_map_check,
			  NULL, gamut_map_config_hw);

static bool oetf_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_xstep_lut *oetf = data;
	const u32 reg_segcnt = VS_SH_LAYER_FIELD(hw_id, OETF_SEGMENT_COUNT_Address);
	const u32 reg_segpnt = VS_SH_LAYER_FIELD(hw_id, OETF_SEGMENT_POINT1_Address);
	const u32 reg_segstp = VS_SH_LAYER_FIELD(hw_id, OETF_SEGMENT_STEP_Address);
	const u32 reg_data = VS_SH_LAYER_FIELD(hw_id, OETF_DATA_Address);

	trace_config_hw_layer_feature_en("OETF", hw_id, enable);
	UPDATE_LAYER_CONFIG(hw, hw_id, OETF, enable);

	if (enable) {
		trace_config_hw_layer_feature("OETF_DATA", hw_id, "seg_cnt:%d entry_cnt:%d",
					      oetf->seg_cnt, oetf->entry_cnt);
		trace_hw_layer_feature_data("OETF_DATA", hw_id, "seg_point",
					    (const u8 *)oetf->seg_point, sizeof(oetf->seg_point));
		trace_hw_layer_feature_data("OETF_DATA", hw_id, "seg_step",
					    (const u8 *)oetf->seg_step, sizeof(oetf->seg_step));
		trace_hw_layer_feature_data("OETF_DATA", hw_id, "data", (const u8 *)oetf->data,
					    sizeof(oetf->data));
		/* segment count start from 0, so the configured value minus 1*/
		dc_write(hw, reg_segcnt, oetf->seg_cnt - 1);

		/* commit the segment points */
		dc_write_relaxed_u32_blob(hw, reg_segpnt, oetf->seg_point, oetf->seg_cnt - 1);

		/* commit the segment steps */
		dc_write_relaxed_u32_blob(hw, reg_segstp, oetf->seg_step, oetf->seg_cnt);

		/* commit the coef data of each entry */
		dc_write_relaxed_u32_blob(hw, reg_data, oetf->data, oetf->entry_cnt);
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(oetf_proto, "OETF", struct drm_vs_xstep_lut, NULL, NULL, oetf_config_hw);

static bool tone_map_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			   const void *obj_state)
{
	const struct vs_dc_info *info = hw->info;
	const u32 tm_in_bit = info->hdr_bits;
	const u32 tm_out_bit = info->hdr_bits;
	const u32 tm_entry = info->max_tonemap_size;
	const struct drm_vs_tone_map *tm = data;
	u32 i = 0;

	if (!tm)
		return true;

	if (tm->pseudo_y.y_mode > VS_CALC_MIXED) {
		dev_err(hw->dev, "%s: Invalid tone map mode.\n", __func__);
		return false;
	}

	if (tm->lut.entry_cnt != tm_entry) {
		dev_err(hw->dev, "%s: Invalid tone map entry cnt, accept %u, need %u.\n", __func__,
			tm->lut.entry_cnt, tm_entry);
		return false;
	}

	/* check the effectiveness of control point,
	 * the last point allows greater than 1<<tm_in_bit
	 */
	for (i = 0; i < tm_entry - 2; i++) {
		if (tm->lut.seg_point[i] >> tm_in_bit) {
			dev_err(hw->dev, "%s: The control point %u of tone map over valid bit.\n",
				__func__, tm->lut.seg_point[i]);
			return false;
		}
	}
	for (i = 0; i < tm_entry; i++) {
		if (tm->lut.data[i] >> tm_out_bit) {
			dev_err(hw->dev, "%s: The entry %u of tone map over valid bit.\n", __func__,
				tm->lut.data[i]);
			return false;
		}
	}
	return true;
}

static bool tone_map_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_tone_map *tm = data;
	const u32 reg_ymode = VS_SH_LAYER_FIELD(hw_id, TM_PSEUDO_YMODE_Address);
	const u32 reg_coef0 = VS_SH_LAYER_FIELD(hw_id, TM_COEF0_Address);
	const u32 reg_coef1 = VS_SH_LAYER_FIELD(hw_id, TM_COEF1_Address);
	const u32 reg_coef2 = VS_SH_LAYER_FIELD(hw_id, TM_COEF2_Address);
	const u32 reg_weight = VS_SH_LAYER_FIELD(hw_id, TM_WEIGHT_Address);
	const u32 reg_seg_pnt = VS_SH_LAYER_FIELD(hw_id, TM_SEGMENT_POINT1_Address);
	const u32 reg_seg_data = VS_SH_LAYER_FIELD(hw_id, TM_DATA_Address);
	u8 y_mode = tm->pseudo_y.y_mode;

	trace_config_hw_layer_feature_en("TONE_MAP", hw_id, enable);
	UPDATE_LAYER_CONFIG(hw, hw_id, TONE_MAPPING, enable);

	if (enable) {
		trace_config_hw_layer_feature(
			"OETF_DATA", hw_id, "y_mode:%d y_coef:[%d %d %d] y_weight:%d entry_cnt:%d",
			tm->pseudo_y.y_mode, tm->pseudo_y.coef0, tm->pseudo_y.coef1,
			tm->pseudo_y.coef2, tm->pseudo_y.weight, tm->lut.entry_cnt);
		trace_hw_layer_feature_data("OETF_DATA", hw_id, "lut seg_point",
					    (const u8 *)tm->lut.seg_point,
					    sizeof(tm->lut.seg_point));
		trace_hw_layer_feature_data("OETF_DATA", hw_id, "lut data",
					    (const u8 *)tm->lut.data, sizeof(tm->lut.data));
		dc_write(hw, reg_ymode, y_mode);

		if (y_mode == VS_CALC_LNR_COMBINE || y_mode == VS_CALC_MIXED) {
			dc_write(hw, reg_coef0, tm->pseudo_y.coef0);
			dc_write(hw, reg_coef1, tm->pseudo_y.coef1);
			dc_write(hw, reg_coef2, tm->pseudo_y.coef2);

			if (y_mode == VS_CALC_MIXED)
				dc_write(hw, reg_weight, tm->pseudo_y.weight);
		}

		/* Segment points configuration (1-32) */
		dc_write_u32_blob(hw, reg_seg_pnt, tm->lut.seg_point, tm->lut.entry_cnt - 1);

		/* Data configuration (0-32) */
		dc_write_u32_blob(hw, reg_seg_data, tm->lut.data, tm->lut.entry_cnt);
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(tone_map_proto, "TONE_MAP", struct drm_vs_tone_map, tone_map_check, NULL,
			  tone_map_config_hw);

static bool demultiply_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	trace_config_hw_layer_feature("DE_MULTIPLY", hw_id, "%d", enable);
	UPDATE_LAYER_CONFIG(hw, hw_id, DE_MULTIPLY, enable);

	return true;
}

VS_DC_BOOL_PROPERTY_PROTO(demultiply_proto, "DE_MULTIPLY", NULL, NULL, demultiply_config_hw);

bool vs_dc_register_hdr_states(struct vs_dc_property_state_group *states,
			       const struct vs_plane_info *info)
{
	__ERR_CHECK(vs_dc_property_register_state(states, &hdr_proto), on_error);
	__ERR_CHECK(vs_dc_property_register_state(states, &eotf_proto), on_error);
	__ERR_CHECK(vs_dc_property_register_state(states, &gamut_map_proto), on_error);
	if (info->tone_map)
		__ERR_CHECK(vs_dc_property_register_state(states, &tone_map_proto), on_error);
	if (info->demultiply)
		__ERR_CHECK(vs_dc_property_register_state(states, &demultiply_proto), on_error);
	__ERR_CHECK(vs_dc_property_register_state(states, &oetf_proto), on_error);
	return true;
on_error:
	return false;
}
