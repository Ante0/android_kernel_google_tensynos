/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */
#include "vs_dc_sharpness.h"

#include "drm/vs_drm.h"
#include "vs_dc_property.h"
#include "vs_dc_hw.h"
#include "vs_dc_reg_be.h"

// inkmode
static bool display_sharpness_inkmode_check(const struct dc_hw *hw, u8 hw_id, const void *data,
					    u32 size, const void *obj_state)
{
	const struct drm_vs_sharpness *sharpness = data;

	if (sharpness->ink_mode > 0xB)
		return false;

	return true;
}

static bool display_sharpness_inkmode_config_hw(struct dc_hw *hw, u8 hw_id, bool enable,
						const void *data)
{
	const struct drm_vs_sharpness *sharpness = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);
	u32 config = dc_read(hw, DCREG_SH_PANEL0_CONFIG_Address + offset);
	u32 mode = 0;

	enable = enable && sharpness->enable;

	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, SHARPNESS, enable);
	dc_write(hw, DCREG_SH_PANEL0_CONFIG_Address + offset, config);
	mode = VS_SET_FIELD(0, DCREG_SH_PANEL0_SHARP_INK_MODE_SELECT, VALUE, sharpness->ink_mode);
	dc_write(hw, DCREG_SH_PANEL0_SHARP_INK_MODE_SELECT_Address + offset, mode);
	return true;
}

// csc
static bool display_sharpness_csc_config_hw(struct dc_hw *hw, u8 hw_id, bool enable,
					    const void *data)
{
	const struct drm_vs_sharpness_csc *csc = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);

	if (!enable)
		return true;
	dc_write_u32_blob(hw, DCREG_SH_PANEL0_SHARP_Y2R_COEF0_Address + offset, csc->y2r_coef,
			  VS_SHARPNESS_CSC_COEF_NUM);
	dc_write_u32_blob(hw, DCREG_SH_PANEL0_SHARP_R2Y_COEF0_Address + offset, csc->r2y_coef,
			  VS_SHARPNESS_CSC_COEF_NUM);
	dc_write_u32_blob(hw, DCREG_SH_PANEL0_SHARP_Y2R_OFFSET0_Address + offset, csc->y2r_offset,
			  VS_SHARPNESS_CSC_OFFSET_NUM);
	dc_write_u32_blob(hw, DCREG_SH_PANEL0_SHARP_R2Y_OFFSET0_Address + offset, csc->r2y_offset,
			  VS_SHARPNESS_CSC_OFFSET_NUM);
	return true;
}

// luma gain
static bool display_sharpness_luma_gain_config_hw(struct dc_hw *hw, u8 hw_id, bool enable,
						  const void *data)
{
	const struct drm_vs_sharpness_luma_gain *luma = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);

	if (!enable)
		return true;
	dc_write_u32_blob(hw, DCREG_SH_PANEL0_SHARP_LUMA_GAIN_LUT0_Address + offset, luma->lut,
			  VS_SHARPNESS_LUMA_GAIN_LUT_ENTRY_NUM);
	return true;
}

// low pass filter
static bool display_sharpness_lpf_config_hw(struct dc_hw *hw, u8 hw_id, bool enable,
					    const void *data)
{
	const struct drm_vs_sharpness_lpf *lpf = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);

	if (!enable)
		return true;
#define __WRITE_LPF(index)                                                                      \
	do {                                                                                    \
		u32 config;                                                                     \
		config = VS_SET_FIELD(0, DCREG_SH_PANEL0_SHARP_LPF##index##_COEF, VALUE0,       \
				      lpf->lpf##index##_coef[0]);                               \
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_SHARP_LPF##index##_COEF, VALUE1,  \
				      lpf->lpf##index##_coef[1]);                               \
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_SHARP_LPF##index##_COEF, VALUE2,  \
				      lpf->lpf##index##_coef[2]);                               \
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_SHARP_LPF##index##_COEF, VALUE3,  \
				      lpf->lpf##index##_coef[3]);                               \
		dc_write(hw, DCREG_SH_PANEL0_SHARP_LPF##index##_COEF_Address + offset, config); \
		dc_write(hw, DCREG_SH_PANEL0_SHARP_LPF_NORM_GAIN##index##_Address + offset,     \
			 VS_SET_FIELD(0, DCREG_SH_PANEL0_SHARP_LPF_NORM_GAIN##index, VALUE,     \
				      lpf->lpf##index##_norm));                                 \
	} while (0)

	__WRITE_LPF(0);
	__WRITE_LPF(1);
	__WRITE_LPF(2);

#undef __WRITE_LPF
	return true;
}

// low pass filter noise
static bool display_sharpness_lpf_noise_config_hw(struct dc_hw *hw, u8 hw_id, bool enable,
						  const void *data)
{
	const struct drm_vs_sharpness_lpf_noise *noise = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);

	if (!enable)
		return true;
	dc_write_u32_blob(hw, DCREG_SH_PANEL0_SHARP_NOISE_MODEL_LUT_LEV0Y0_Address + offset,
			  noise->lut0, VS_SHARPNESS_LPF_NOISE_LUT_NUM);
	dc_write_u32_blob(hw, DCREG_SH_PANEL0_SHARP_NOISE_MODEL_LUT_LEV1Y0_Address + offset,
			  noise->lut1, VS_SHARPNESS_LPF_NOISE_LUT_NUM);
	dc_write_u32_blob(hw, DCREG_SH_PANEL0_SHARP_NOISE_MODEL_LUT_LEV2Y0_Address + offset,
			  noise->lut2, VS_SHARPNESS_LPF_NOISE_LUT_NUM);
	dc_write(hw, DCREG_SH_PANEL0_SHARP_NOISE_MODEL_LUMA_STRENGTH0_Address + offset,
		 noise->luma_strength0);
	dc_write(hw, DCREG_SH_PANEL0_SHARP_NOISE_MODEL_LUMA_STRENGTH1_Address + offset,
		 noise->luma_strength1);
	dc_write(hw, DCREG_SH_PANEL0_SHARP_NOISE_MODEL_LUMA_STRENGTH2_Address + offset,
		 noise->luma_strength2);
	return true;
}

// low pass filter curve
static bool display_sharpness_lpf_curve_config_hw(struct dc_hw *hw, u8 hw_id, bool enable,
						  const void *data)
{
	const struct drm_vs_sharpness_lpf_curve *curve = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);

	if (!enable)
		return true;
	dc_write_u32_blob(hw, DCREG_SH_PANEL0_SHARP_CURVE0_LUT_SPEC0_Address + offset, curve->lut0,
			  VS_SHARPNESS_LPF_CURVE_LUT_NUM);
	dc_write_u32_blob(hw, DCREG_SH_PANEL0_SHARP_CURVE1_LUT_SPEC0_Address + offset, curve->lut1,
			  VS_SHARPNESS_LPF_CURVE_LUT_NUM);
	dc_write_u32_blob(hw, DCREG_SH_PANEL0_SHARP_CURVE2_LUT_SPEC0_Address + offset, curve->lut2,
			  VS_SHARPNESS_LPF_CURVE_LUT_NUM);
	dc_write(hw, DCREG_SH_PANEL0_SHARP_MASTER_GAIN_Address + offset, curve->master_gain);
	return true;
}

// color adaptive
static bool display_sharpness_color_adaptive_config_hw(struct dc_hw *hw, u8 hw_id, bool enable,
						       const void *data)
{
	const struct drm_vs_sharpness_color_adaptive *ca = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);
	u32 i;
	bool global_switch = false;
	// enable from argument means pass null blob to driver.
	if (enable) {
		for (i = 0; i < VS_SHARPNESS_CA_MODE_NUM; i++) {
			u32 reg_offset = i * 0x4;

			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_ENABLE0_Address + offset + reg_offset,
				 ca->mode[i].enable);
			global_switch = ca->mode[i].enable || global_switch;
		}
		dc_write(hw, DCREG_SH_PANEL0_SHARP_COLOR_ADAPTIVE_ENABLE_Address + offset,
			 global_switch);
	} else {
		for (i = 0; i < VS_SHARPNESS_CA_MODE_NUM; i++) {
			u32 reg_offset = i * 0x4;

			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_ENABLE0_Address + offset + reg_offset,
				 DCREG_SH_PANEL0_SHARP_CA_ENABLE0_ResetValue);
		}
		dc_write(hw, DCREG_SH_PANEL0_SHARP_COLOR_ADAPTIVE_ENABLE_Address + offset,
			 DCREG_SH_PANEL0_SHARP_COLOR_ADAPTIVE_ENABLE_ResetValue);
	}

#define TRY_WRITE_DC_SHARPNESS_CA_MODE(hw, mode, id)                                               \
	do {                                                                                       \
		if (enable && mode.enable) {                                                       \
			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_GAIN##id##_Address + offset,         \
				 mode.gain);                                                       \
			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_THETA_CENTER##id##_Address + offset, \
				 mode.theta_center);                                               \
			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_THETA_RANGE##id##_Address + offset,  \
				 mode.theta_range);                                                \
			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_THETA_SLOPE##id##_Address + offset,  \
				 mode.theta_slope);                                                \
			dc_write(hw,                                                               \
				 DCREG_SH_PANEL0_SHARP_CA_RADIUS_CENTER##id##_Address + offset,    \
				 mode.radius_center);                                              \
			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_RADIUS_RANGE##id##_Address + offset, \
				 mode.radius_range);                                               \
			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_RADIUS_SLOPE##id##_Address + offset, \
				 mode.radius_slope);                                               \
		} else {                                                                           \
			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_GAIN##id##_Address + offset,         \
				 DCREG_SH_PANEL0_SHARP_CA_GAIN##id##_ResetValue);                  \
			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_THETA_CENTER##id##_Address + offset, \
				 DCREG_SH_PANEL0_SHARP_CA_THETA_CENTER##id##_ResetValue);          \
			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_THETA_RANGE##id##_Address + offset,  \
				 DCREG_SH_PANEL0_SHARP_CA_THETA_RANGE##id##_ResetValue);           \
			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_THETA_SLOPE##id##_Address + offset,  \
				 DCREG_SH_PANEL0_SHARP_CA_THETA_SLOPE##id##_ResetValue);           \
			dc_write(hw,                                                               \
				 DCREG_SH_PANEL0_SHARP_CA_RADIUS_CENTER##id##_Address + offset,    \
				 DCREG_SH_PANEL0_SHARP_CA_RADIUS_CENTER##id##_ResetValue);         \
			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_RADIUS_RANGE##id##_Address + offset, \
				 DCREG_SH_PANEL0_SHARP_CA_RADIUS_RANGE##id##_ResetValue);          \
			dc_write(hw, DCREG_SH_PANEL0_SHARP_CA_RADIUS_SLOPE##id##_Address + offset, \
				 DCREG_SH_PANEL0_SHARP_CA_RADIUS_SLOPE##id##_ResetValue);          \
		}                                                                                  \
	} while (0)

	TRY_WRITE_DC_SHARPNESS_CA_MODE(hw, ca->mode[0], 0);
	TRY_WRITE_DC_SHARPNESS_CA_MODE(hw, ca->mode[1], 1);
	TRY_WRITE_DC_SHARPNESS_CA_MODE(hw, ca->mode[2], 2);
#undef TRY_WRITE_DC_SHARPNESS_CA_MODE
	return true;
}

// color boost
static bool display_sharpness_color_boost_config_hw(struct dc_hw *hw, u8 hw_id, bool enable,
						    const void *data)
{
	const struct drm_vs_sharpness_color_boost *cb = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);

	dc_write(hw, DCREG_SH_PANEL0_SHARP_COLOR_BOOST_ENABLE_Address + offset, enable);
	if (enable) {
		dc_write(hw, DCREG_SH_PANEL0_SHARP_COLOR_BOOST_POSITIVE_GAIN_Address + offset,
			 cb->pos_gain);
		dc_write(hw, DCREG_SH_PANEL0_SHARP_COLOR_BOOST_NEGATIVE_GAIN_Address + offset,
			 cb->neg_gain);
		dc_write(hw, DCREG_SH_PANEL0_SHARP_COLOR_BOOST_LUMA_OFFSET_Address + offset,
			 cb->y_offset);
	}
	return true;
}

// soft clip
static bool display_sharpness_soft_clip_config_hw(struct dc_hw *hw, u8 hw_id, bool enable,
						  const void *data)
{
	const struct drm_vs_sharpness_soft_clip *sc = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);

	dc_write(hw, DCREG_SH_PANEL0_SHARP_SOFT_CLIP_ENABLE_Address + offset, enable);
	if (enable) {
		dc_write(hw, DCREG_SH_PANEL0_SHARP_SOFT_CLIP_POSITIVE_OFFSET_Address + offset,
			 sc->pos_offset);
		dc_write(hw, DCREG_SH_PANEL0_SHARP_SOFT_CLIP_NEGATIVE_OFFSET_Address + offset,
			 sc->neg_offset);
		dc_write(hw, DCREG_SH_PANEL0_SHARP_SOFT_CLIP_POSITIVE_WEIGHT_Address + offset,
			 sc->pos_wet);
		dc_write(hw, DCREG_SH_PANEL0_SHARP_SOFT_CLIP_NEGATIVE_WEIGHT_Address + offset,
			 sc->neg_wet);
	}
	return true;
}

// dither
static bool display_sharpness_dither_config_hw(struct dc_hw *hw, u8 hw_id, bool enable,
					       const void *data)
{
	const struct drm_vs_sharpness_dither *dither = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);
	u32 config = 0;

	dc_write(hw, DCREG_SH_PANEL0_SHARP_DITHER_ENABLE_Address + offset, enable);
	if (enable) {
		config = VS_SET_FIELD(0, DCREG_SH_PANEL0_SHARP_DITHER_TABLE, R_VALUE0,
				      dither->table_low[0]);
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_SHARP_DITHER_TABLE, R_VALUE1,
				      dither->table_low[0] >> 16);
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_SHARP_DITHER_TABLE, R_VALUE2,
				      dither->table_high[0]);
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_SHARP_DITHER_TABLE, R_VALUE3,
				      dither->table_high[0] >> 16);
		dc_write(hw, DCREG_SH_PANEL0_SHARP_DITHER_TABLE_R_Address + offset, config);

		config = VS_SET_FIELD(0, DCREG_SH_PANEL0_SHARP_DITHER_TABLE, G_VALUE0,
				      dither->table_low[1]);
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_SHARP_DITHER_TABLE, G_VALUE1,
				      dither->table_low[1] >> 16);
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_SHARP_DITHER_TABLE, G_VALUE2,
				      dither->table_high[1]);
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_SHARP_DITHER_TABLE, G_VALUE3,
				      dither->table_high[1] >> 16);
		dc_write(hw, DCREG_SH_PANEL0_SHARP_DITHER_TABLE_G_Address + offset, config);

		config = VS_SET_FIELD(0, DCREG_SH_PANEL0_SHARP_DITHER_TABLE, B_VALUE0,
				      dither->table_low[2]);
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_SHARP_DITHER_TABLE, B_VALUE1,
				      dither->table_low[2] >> 16);
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_SHARP_DITHER_TABLE, B_VALUE2,
				      dither->table_high[2]);
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_SHARP_DITHER_TABLE, B_VALUE3,
				      dither->table_high[2] >> 16);
		dc_write(hw, DCREG_SH_PANEL0_SHARP_DITHER_TABLE_B_Address + offset, config);
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(sharpness_proto, "SHARPNESS", struct drm_vs_sharpness,
			  display_sharpness_inkmode_check, NULL,
			  display_sharpness_inkmode_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(sharpness_csc_proto, "SHARPNESS_CSC", struct drm_vs_sharpness_csc, NULL,
			  NULL, display_sharpness_csc_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(sharpness_luma_gain_proto, "SHARPNESS_LUMA_GAIN",
			  struct drm_vs_sharpness_luma_gain, NULL, NULL,
			  display_sharpness_luma_gain_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(sharpness_lpf_proto, "SHARPNESS_LPF", struct drm_vs_sharpness_lpf, NULL,
			  NULL, display_sharpness_lpf_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(sharpness_lpf_noise_proto, "SHARPNESS_LPF_NOISE",
			  struct drm_vs_sharpness_lpf_noise, NULL, NULL,
			  display_sharpness_lpf_noise_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(sharpness_lpf_curve_proto, "SHARPNESS_LPF_CURVE",
			  struct drm_vs_sharpness_lpf_curve, NULL, NULL,
			  display_sharpness_lpf_curve_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(sharpness_color_adaptive_proto, "SHARPNESS_COLOR_ADAPTIVE",
			  struct drm_vs_sharpness_color_adaptive, NULL, NULL,
			  display_sharpness_color_adaptive_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(sharpness_color_boost_proto, "SHARPNESS_COLOR_BOOST",
			  struct drm_vs_sharpness_color_boost, NULL, NULL,
			  display_sharpness_color_boost_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(sharpness_soft_clip_proto, "SHARPNESS_SOFT_CLIP",
			  struct drm_vs_sharpness_soft_clip, NULL, NULL,
			  display_sharpness_soft_clip_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(sharpness_dither_proto, "SHARPNESS_DITHER",
			  struct drm_vs_sharpness_dither, NULL, NULL,
			  display_sharpness_dither_config_hw);

bool vs_dc_register_sharpness_states(struct vs_dc_property_state_group *states,
				     const struct vs_display_info *display_info)
{
	__ERR_CHECK(vs_dc_property_register_state(states, &sharpness_proto), on_error);
	__ERR_CHECK(vs_dc_property_register_state(states, &sharpness_csc_proto), on_error);
	__ERR_CHECK(vs_dc_property_register_state(states, &sharpness_luma_gain_proto), on_error);
	__ERR_CHECK(vs_dc_property_register_state(states, &sharpness_lpf_proto), on_error);
	__ERR_CHECK(vs_dc_property_register_state(states, &sharpness_lpf_noise_proto), on_error);
	__ERR_CHECK(vs_dc_property_register_state(states, &sharpness_lpf_curve_proto), on_error);
	__ERR_CHECK(vs_dc_property_register_state(states, &sharpness_color_adaptive_proto),
		    on_error);
	__ERR_CHECK(vs_dc_property_register_state(states, &sharpness_color_boost_proto), on_error);
	__ERR_CHECK(vs_dc_property_register_state(states, &sharpness_soft_clip_proto), on_error);
	__ERR_CHECK(vs_dc_property_register_state(states, &sharpness_dither_proto), on_error);
	return true;
on_error:
	return false;
}
