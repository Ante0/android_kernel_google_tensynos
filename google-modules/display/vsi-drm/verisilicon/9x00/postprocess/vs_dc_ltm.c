/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */
#include <trace/dpu_trace.h>

#include "vs_dc_ltm.h"

#include "drm/vs_drm.h"
#include "vs_crtc.h"
#include "vs_dc_property.h"
#include "vs_dc_hw.h"
#include "vs_dc_reg_be.h"

static bool ltm_enable_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;

	config = dc_read(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, LTM, enable);
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address), config);

	return true;
}

static bool ltm_degamma_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	u32 entry_cnt = 0;
	u32 base_offset = 0x4;
	const struct drm_vs_ltm_xgamma *ltm_degamma = data;

	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, DE_GAMMA, enable);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address), config);

	if (enable) {
		for (entry_cnt = 0; entry_cnt < VS_LTM_XGAMMA_COEF_NUM; entry_cnt++)
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_DE_GAMMA_DATA_Address) +
					 (entry_cnt * base_offset),
				 VS_SET_FIELD(0, DCREG_SH_PANEL0_LTM_DE_GAMMA_DATA, VALUE,
					      ltm_degamma->coef[entry_cnt]));
	}
	return true;
}

static bool ltm_gamma_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	u32 entry_cnt = 0;
	u32 base_offset = 0x4;
	const struct drm_vs_ltm_xgamma *ltm_gamma = data;

	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, GAMMA, enable);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address), config);

	if (enable) {
		for (entry_cnt = 0; entry_cnt < VS_LTM_XGAMMA_COEF_NUM; entry_cnt++)
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_GAMMA_DATA_Address) +
					 (entry_cnt * base_offset),
				 VS_SET_FIELD(0, DCREG_SH_PANEL0_LTM_GAMMA_DATA, VALUE,
					      ltm_gamma->coef[entry_cnt]));
	}
	return true;
}

static bool ltm_luma_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_ltm_luma *ltm_luma = data;

	if (enable) {
		switch (ltm_luma->mode) {
		case VS_LTM_LUMA_GRAY:
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_RGBY_COEFF0_Address),
				 ltm_luma->coef[0]);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_RGBY_COEFF1_Address),
				 ltm_luma->coef[1]);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_RGBY_COEFF2_Address),
				 ltm_luma->coef[2]);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_GRAY_WEIGHT_Address),
				 255);
			break;

		case VS_LTM_LUMA_LIGHTNESS:
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_LIGHTNESS_WEIGHT_Address),
				 ltm_luma->coef[0]);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_GRAY_WEIGHT_Address),
				 0);
			break;

		case VS_LTM_LUMA_MIXED:
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_RGBY_COEFF0_Address),
				 ltm_luma->coef[0]);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_RGBY_COEFF1_Address),
				 ltm_luma->coef[1]);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_RGBY_COEFF2_Address),
				 ltm_luma->coef[2]);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_LIGHTNESS_WEIGHT_Address),
				 ltm_luma->coef[3]);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_GRAY_WEIGHT_Address),
				 ltm_luma->coef[4]);
			break;

		default:
			break;
		}
	}

	return true;
}

static bool ltm_freq_decomp_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	const struct drm_vs_ltm_freq_decomp *freq_decomp = data;

	if (enable) {
		config = dc_read(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, FREQ_DECOMP,
				      freq_decomp->decomp_enable);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address),
			 config);

		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF00_Address),
			 freq_decomp->coef[0]);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF01_Address),
			 freq_decomp->coef[1]);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF02_Address),
			 freq_decomp->coef[2]);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF10_Address),
			 freq_decomp->coef[3]);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF11_Address),
			 freq_decomp->coef[4]);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF12_Address),
			 freq_decomp->coef[5]);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF20_Address),
			 freq_decomp->coef[6]);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF21_Address),
			 freq_decomp->coef[7]);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF22_Address),
			 freq_decomp->coef[8]);

		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_FILTER_NORM_Address),
			 freq_decomp->norm);
	} else {
		config = dc_read(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, FREQ_DECOMP, 0x1);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address),
			 config);

		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF00_Address),
			 0x0);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF01_Address),
			 0x0);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF02_Address),
			 0x0);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF10_Address),
			 0x0);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF11_Address),
			 0x0);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF12_Address),
			 0x0);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF20_Address),
			 0x0);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF21_Address),
			 0x0);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_COEF22_Address),
			 0x0);

		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_FREQ_DECOMP_FILTER_NORM_Address),
			 0x0);
	}
	return true;
}

static bool ltm_luma_adj_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			       const void *obj_state)
{
	const struct drm_vs_1d_lut *luma_adj = data;

	if (luma_adj->entry_cnt != VS_MAX_1D_LUT_ENTRY_CNT)
		return false;

	return true;
}

static bool ltm_luma_adj_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	u32 entry_cnt = 0;
	u32 base_offset = 0x4;
	const struct drm_vs_1d_lut *luma_adj = data;

	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, GUIDE_CURVE, enable);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address), config);

	if (enable) {
		for (entry_cnt = 0; entry_cnt < luma_adj->entry_cnt - 1; entry_cnt++)
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_GUID_CURVE_LUT_Address) +
					 (entry_cnt * base_offset),
				 luma_adj->data[entry_cnt]);
	}

	return true;
}

static bool ltm_grid_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_ltm_grid *grid_size = data;

	if (enable) {
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_GRID_WIDTH_Address),
			 grid_size->width);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_GRID_HEIGHT_Address),
			 grid_size->height);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_GRID_DEPTH_Address),
			 grid_size->depth);
	}

	return true;
}

static bool ltm_af_filter_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
				const void *obj_state)
{
	return true;
}

static bool ltm_af_filter_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	u32 entry_cnt = 0;
	const struct drm_vs_ltm_af_filter *af_filter = data;

	DPU_ATRACE_BEGIN(__func__);
	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, AFFINE_GRID_TEMP_SMOOTH,
			      af_filter->enable_temporal_filter);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address), config);

	if (enable) {
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_AF_GRID_TEMP_SWGT_Address),
			 af_filter->weight);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_AF_SLOPE_BIAS_INDEX_Address),
			 0);

		for (entry_cnt = 0; entry_cnt < VS_LTM_AFFINE_LUT_NUM; entry_cnt++) {
			config = VS_SET_FIELD(0, DCREG_SH_PANEL0_LTM_AF_SLOPE_BIAS_DATA, BIAS,
					      af_filter->bias[entry_cnt]) |
				 VS_SET_FIELD(0, DCREG_SH_PANEL0_LTM_AF_SLOPE_BIAS_DATA, SLOPE,
					      af_filter->slope[entry_cnt]);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_AF_SLOPE_BIAS_DATA_Address),
				 config);
		}
	}
	DPU_ATRACE_END(__func__);

	return true;
}

static bool ltm_af_filter_config_hw_v2(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	u32 entry_cnt = 0;
	const struct drm_vs_ltm_af_filter_v2 *af_filter = data;

	DPU_ATRACE_BEGIN(__func__);
	if (enable) {
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_AF_GRID_TEMP_SWGT_Address),
			 af_filter->weight);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_AF_SLOPE_BIAS_INDEX_Address),
			 0);

		for (entry_cnt = 0; entry_cnt < VS_LTM_AFFINE_LUT_NUM; entry_cnt++) {
			config = VS_SET_FIELD(0, DCREG_SH_PANEL0_LTM_AF_SLOPE_BIAS_DATA, BIAS,
					      af_filter->bias[entry_cnt]) |
				 VS_SET_FIELD(0, DCREG_SH_PANEL0_LTM_AF_SLOPE_BIAS_DATA, SLOPE,
					      af_filter->slope[entry_cnt]);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_AF_SLOPE_BIAS_DATA_Address),
				 config);
		}
	}
	DPU_ATRACE_END(__func__);

	return true;
}

static bool ltm_af_slice_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	const struct drm_vs_ltm_af_slice *af_slice = data;

	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, SLICE_AFFINE_GRID, enable);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address), config);

	if (enable) {
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_START_POS_X_Address),
			 af_slice->start_pos[0]);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_GRID_XSCALE_Address),
			 af_slice->scale[0]);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_GRID_XSCALE_HALF_Address),
			 af_slice->scale_half[0]);

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_START_POS_Y_Address),
			 af_slice->start_pos[1]);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_GRID_YSCALE_Address),
			 af_slice->scale[1]);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_GRID_YSCALE_HALF_Address),
			 af_slice->scale_half[1]);
	}

	return true;
}

static bool ltm_af_trans_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 cnt = 0;
	u32 base_offset = 0x4;
	const struct drm_vs_ltm_af_trans *af_trans = data;

	if (enable) {
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_SLOPE_FRAC_BIT_Address),
			 af_trans->slope_bit);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_BIAS_BIT_ADJUST_Address),
			 af_trans->bias_bit);

		for (cnt = 0; cnt < VS_LTM_AFFINE_OUT_SCALE_SIZE; cnt++) {
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_MIN_OUTPUT_SCALE_LUT_Address) +
					 cnt * base_offset,
				 af_trans->scale[cnt]);
		}
	}

	return true;
}

static bool ltm_tone_adj_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			       const void *obj_state)
{
	const struct drm_vs_ltm_tone_adj *tone_adj = data;

	if (tone_adj->entry_cnt != VS_LTM_TONE_ADJ_COEF_NUM)
		return false;

	return true;
}

static bool ltm_tone_adj_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	u32 entry_cnt = 0;
	u32 base_offset = 0x4;
	const struct drm_vs_ltm_tone_adj *tone_adj = data;

	DPU_ATRACE_BEGIN(__func__);
	if (enable) {
		config = dc_read(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, USE_OUTPUT_LUMA,
				      tone_adj->luma_from);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address),
			 config);

		for (entry_cnt = 0; entry_cnt < tone_adj->entry_cnt; entry_cnt++)
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_TONE_ADJUST_LUT_Address) +
					 (entry_cnt * base_offset),
				 tone_adj->data[entry_cnt]);
	} else {
		config = dc_read(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, USE_OUTPUT_LUMA, 0x1);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address),
			 config);

		for (entry_cnt = 0; entry_cnt < tone_adj->entry_cnt; entry_cnt++)
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_TONE_ADJUST_LUT_Address) +
					 (entry_cnt * base_offset),
				 0x0);
	}
	DPU_ATRACE_END(__func__);

	return true;
}

static bool ltm_color_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	u32 entry_cnt = 0;
	u32 base_offset = 0x4;
	const struct drm_vs_ltm_color *ltm_color = data;

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_INPUT_LUMA_THRESH_Address),
		 ltm_color->luma_thresh);

	if (enable) {
		config = dc_read(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, SATURATION_CTRL,
				      ltm_color->satu_ctrl);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address),
			 config);

		for (entry_cnt = 0; entry_cnt < VS_LTM_ALPHA_GAIN_SIZE; entry_cnt++)
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_ALPHA_GAIN_LUT_Address) +
					 (entry_cnt * base_offset),
				 ltm_color->gain[entry_cnt]);
		for (entry_cnt = 0; entry_cnt < VS_LTM_ALPHA_LUMA_SIZE; entry_cnt++)
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_ALPHA_LUMA_LUT_Address) +
					 (entry_cnt * base_offset),
				 ltm_color->luma[entry_cnt]);

		for (entry_cnt = 0; entry_cnt < VS_LTM_SATU_CTRL_SIZE; entry_cnt++)
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_SATU_CTRL_LUT_Address) +
					 (entry_cnt * base_offset),
				 ltm_color->satu[entry_cnt]);
	} else {
		config = dc_read(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, SATURATION_CTRL, 0x0);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address),
			 config);
	}

	return true;
}

static bool ltm_dither_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	u32 entry_cnt = 0;
	u32 base_offset = 0x4;
	const struct drm_vs_ltm_dither *ltm_dither = data;

	if (enable) {
		config = dc_read(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, DITHER,
				      ltm_dither->dither_enable);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address),
			 config);

		for (entry_cnt = 0; entry_cnt < VS_LTM_DITHER_COEF_NUM; entry_cnt++) {
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_DTH_THR00_CHANNEL_Address) +
					 (entry_cnt * base_offset),
				 ltm_dither->table_low[entry_cnt]);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_DTH_THR01_CHANNEL_Address) +
					 (entry_cnt * base_offset),
				 ltm_dither->table_low[entry_cnt] >> 16);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_DTH_THR10_CHANNEL_Address) +
					 (entry_cnt * base_offset),
				 ltm_dither->table_high[entry_cnt]);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_DTH_THR11_CHANNEL_Address) +
					 (entry_cnt * base_offset),
				 ltm_dither->table_high[entry_cnt] >> 16);
		}
	} else {
		config = dc_read(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, DITHER, 0x1);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address),
			 config);

		for (entry_cnt = 0; entry_cnt < VS_LTM_DITHER_COEF_NUM; entry_cnt++) {
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_DTH_THR00_CHANNEL_Address) +
					 (entry_cnt * base_offset),
				 0x0);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_DTH_THR01_CHANNEL_Address) +
					 (entry_cnt * base_offset),
				 0x1);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_DTH_THR10_CHANNEL_Address) +
					 (entry_cnt * base_offset),
				 0x2);
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LTM_DTH_THR11_CHANNEL_Address) +
					 (entry_cnt * base_offset),
				 0x3);
		}
	}

	return true;
}

static bool ltm_luma_ave_set_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_ltm_luma_ave_set *ltm_luma_set = data;

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_LUMA_AVE_MARGIN_X_Address),
		 ltm_luma_set->margin_x);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_LUMA_AVE_MARGIN_Y_Address),
		 ltm_luma_set->margin_y);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_LUMA_AVE_PIXEL_NORM_Address),
		 ltm_luma_set->pixel_norm);

	return true;
}

static bool ltm_hist_cd_set_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	const struct drm_vs_ltm_cd_set *ltm_cd_set = data;

	if (enable) {
		config = dc_read(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, CDETECTION,
				      ltm_cd_set->enable);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address),
			 config);

		config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
							  LTM_HIST_CONTROL_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_HIST_CONTROL, CDETEC_OVERLAP_HIST,
				      ltm_cd_set->overlap);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_CONTROL_Address),
			 config);

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_MIN_WGT_Address),
			 ltm_cd_set->min_wgt);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_FILTER_NORM_Address),
			 ltm_cd_set->filt_norm);

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_COEF0_Address),
			 ltm_cd_set->coef[0]);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_COEF1_Address),
			 ltm_cd_set->coef[1]);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_COEF2_Address),
			 ltm_cd_set->coef[2]);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_COEF3_Address),
			 ltm_cd_set->coef[3]);

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_THR0_Address),
			 ltm_cd_set->thresh[0]);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_THR1_Address),
			 ltm_cd_set->thresh[1]);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_THR2_Address),
			 ltm_cd_set->thresh[2]);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_THR3_Address),
			 ltm_cd_set->thresh[3]);

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_SLOPE0_Address),
			 ltm_cd_set->slope[0]);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_SLOPE1_Address),
			 ltm_cd_set->slope[1]);
	} else {
		config = dc_read(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, CDETECTION, 0x1);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address),
			 config);

		config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
							  LTM_HIST_CONTROL_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_HIST_CONTROL, CDETEC_OVERLAP_HIST,
				      0x0);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_CONTROL_Address),
			 config);

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_MIN_WGT_Address),
			 0x0);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_FILTER_NORM_Address),
			 0x0);

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_COEF0_Address),
			 0x0);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_COEF1_Address),
			 0x0);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_COEF2_Address),
			 0x0);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_COEF3_Address),
			 0x0);

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_THR0_Address), 0x0);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_THR1_Address), 0x0);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_THR2_Address), 0x0);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_THR3_Address), 0x0);

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_SLOPE0_Address),
			 0x0);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CD_SLOPE1_Address),
			 0x0);
	}

	return true;
}

static bool ltm_local_hist_set_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	const struct drm_vs_ltm_hist_set *ltm_hist_set = data;

	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_CONTROL_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_HIST_CONTROL, HIST, enable);
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_HIST_CONTROL, OVERLAP_HIST,
			      ltm_hist_set->overlap);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_CONTROL_Address), config);

	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_HIST_CONFIG, ENABLE, enable);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_CONFIG_Address), config);

	if (enable) {
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_GRID_DEPTH_Address),
			 ltm_hist_set->grid_depth);

		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_START_XPOS_Address),
			 ltm_hist_set->start_pos[0]);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_START_YPOS_Address),
			 ltm_hist_set->start_pos[1]);

		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_GRID_XSCALE_Address),
			 ltm_hist_set->grid_scale[0]);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_GRID_YSCALE_Address),
			 ltm_hist_set->grid_scale[1]);
	}

	return true;
}

static bool ltm_ds_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 config = 0;
	const struct drm_vs_ltm_ds *ltm_ds = data;
	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, DSCALER, enable);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address), config);

	config = VS_SET_FIELD(0, DCREG_SH_PANEL0_LTM_DS_CROP_HOR, LEFT, ltm_ds->crop_l) |
		 VS_SET_FIELD(0, DCREG_SH_PANEL0_LTM_DS_CROP_HOR, RIGHT, ltm_ds->crop_r);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_DS_CROP_HOR_Address), config);

	config = VS_SET_FIELD(0, DCREG_SH_PANEL0_LTM_DS_CROP_VER, TOP, ltm_ds->crop_t) |
		 VS_SET_FIELD(0, DCREG_SH_PANEL0_LTM_DS_CROP_VER, BOTTOM, ltm_ds->crop_b);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_DS_CROP_VER_Address), config);

	config = VS_SET_FIELD(0, DCREG_SH_PANEL0_LTM_DS_OUTPUT, WIDTH, ltm_ds->output.w) |
		 VS_SET_FIELD(0, DCREG_SH_PANEL0_LTM_DS_OUTPUT, HEIGHT, ltm_ds->output.h);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_DS_OUTPUT_Address), config);

	if (enable) {
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_DS_HOR_NORM_Address),
			 ltm_ds->h_norm);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_DS_VER_NORM_Address),
			 ltm_ds->v_norm);
	}

	return true;
}

static bool ltm_histogram_wdma_config_check(const struct dc_hw *hw, u8 hw_id, const void *data,
					    u32 size, const void *obj_state)
{
	return true;
}

static bool ltm_histogram_wdma_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_ltm_histogram_wdma_config *hist_dma_cfg = data;
	dma_addr_t addr;
	u32 config;

	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_WDMA_CONFIG_Address));
	if (hist_dma_cfg->hist_dma_addr || hist_dma_cfg->luma_dma_addr) {
		/* enable LTM WDMA */
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_WDMA_CONFIG, ENABLE, 1);
		if (hist_dma_cfg->hist_dma_addr)
			config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_WDMA_CONFIG,
					      LOCAL_HIST_WDMA_ENABLE, 1);
		if (hist_dma_cfg->luma_dma_addr)
			config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_WDMA_CONFIG,
					      DS_WDMA_ENABLE, 1);
	} else {
		/* disable LTM WDMA */
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_WDMA_CONFIG, ENABLE, 0);
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_WDMA_CONFIG,
				      LOCAL_HIST_WDMA_ENABLE, 0);
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_WDMA_CONFIG,
				      DS_WDMA_ENABLE, 0);
	}
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_WDMA_CONFIG_Address), config);

	addr = hist_dma_cfg->hist_dma_addr;
	if (addr) {
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_LOCAL_HIST_WB_ADDRESS_Address),
			 addr & 0xFFFFFFFF);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_LOCAL_HIST_WB_HIGH_ADDRESS_Address),
			 addr >> 32);

		config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
							  LTM_HIST_CONFIG_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_HIST_CONFIG, UPDATE, 1);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_CONFIG_Address),
			 config);
	} else {
		config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
							  LTM_HIST_CONFIG_Address));
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_HIST_CONFIG, UPDATE, 0);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_HIST_CONFIG_Address),
			 config);
	}
	vs_crtc_store_ltm_hist_dma_addr(hw->dev, hw_id, addr);

	addr = hist_dma_cfg->luma_dma_addr;
	if (addr) {
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_DOWN_SCALER_WB_ADDRESS_Address),
			 addr & 0xFFFFFFFF);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      LTM_DOWN_SCALER_WB_HIGH_ADDRESS_Address),
			 addr >> 32);
	}

	return true;
}

static bool ltm_histogram_config_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
				       const void *obj_state)
{
	return true;
}

static bool ltm_histogram_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	bool ret;
	const struct drm_vs_ltm_histogram_config *cfg = data;

	DPU_ATRACE_BEGIN(__func__);
	ret = ltm_enable_config_hw(hw, hw_id, enable, data) &&
	      ltm_degamma_config_hw(hw, hw_id, enable && cfg->degamma_enable, &cfg->ltm_degamma) &&
	      ltm_gamma_config_hw(hw, hw_id, enable && cfg->gamma_enable, &cfg->ltm_gamma) &&
	      ltm_luma_config_hw(hw, hw_id, enable && cfg->luma_enable, &cfg->ltm_luma) &&
	      ltm_grid_config_hw(hw, hw_id, enable && cfg->grid_enable, &cfg->grid_size) &&
	      ltm_luma_ave_set_config_hw(hw, hw_id, enable && cfg->luma_set_enable,
					 &cfg->ltm_luma_set) &&
	      ltm_hist_cd_set_config_hw(hw, hw_id, enable && cfg->cd_set_enable,
					&cfg->ltm_cd_set) &&
	      ltm_local_hist_set_config_hw(hw, hw_id, enable && cfg->hist_set_enable,
					   &cfg->ltm_hist_set) &&
	      ltm_ds_config_hw(hw, hw_id, enable && cfg->ds_enable, &cfg->ltm_ds);
	DPU_ATRACE_END(__func__);

	return ret;
}

static bool ltm_rendering_config_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
				       const void *obj_state)
{
	const struct drm_vs_ltm_rendering_config *cfg = data;

	if (cfg->luma_adj_enable &&
	    !ltm_luma_adj_check(hw, hw_id, &cfg->luma_adj, sizeof(cfg->luma_adj), obj_state))
		return false;

	return true;
}

static bool ltm_rendering_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	bool ret;
	const struct drm_vs_ltm_rendering_config *cfg = data;

	DPU_ATRACE_BEGIN(__func__);
	ret = ltm_freq_decomp_config_hw(hw, hw_id, enable && cfg->freq_decomp_enable,
					&cfg->freq_decomp) &&
	      ltm_luma_adj_config_hw(hw, hw_id, enable && cfg->luma_adj_enable, &cfg->luma_adj) &&
	      ltm_af_slice_config_hw(hw, hw_id, enable && cfg->af_slice_enable, &cfg->af_slice) &&
	      ltm_af_trans_config_hw(hw, hw_id, enable && cfg->af_trans_enable, &cfg->af_trans) &&
	      ltm_color_config_hw(hw, hw_id, enable && cfg->color_enable, &cfg->ltm_color) &&
	      ltm_dither_config_hw(hw, hw_id, enable && cfg->dither_enable, &cfg->ltm_dither);
	DPU_ATRACE_END(__func__);

	return ret;
}

static bool ltm_af_temproal_filter_config_hw(struct dc_hw *hw, u8 hw_id, bool enable)
{
	u32 config;

	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, AFFINE_GRID_TEMP_SMOOTH, enable);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address), config);

	return true;
}

static bool ltm_rendering_config_check_v2(const struct dc_hw *hw, u8 hw_id, const void *data,
					  u32 size, const void *obj_state)
{
	const struct drm_vs_ltm_rendering_config_v2 *cfg = data;

	if (cfg->luma_adj_enable &&
	    !ltm_luma_adj_check(hw, hw_id, &cfg->luma_adj, sizeof(cfg->luma_adj), obj_state))
		return false;

	return true;
}

static bool ltm_rendering_config_hw_v2(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	bool ret;
	const struct drm_vs_ltm_rendering_config_v2 *cfg = data;
	bool af_temporal_filter_enable = cfg->af_temporal_filter_enable;
	u32 config;
	u32 af;

	DPU_ATRACE_BEGIN(__func__);
	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LTM_CONFIG_Address));
	af = VS_GET_FIELD(config, DCREG_SH_PANEL0_LTM_CONFIG, SLICE_AFFINE_GRID);
	if (!af && enable && cfg->af_slice_enable && af_temporal_filter_enable) {
		/* always disable temproal filter for the first frame after af rendering
		 * is enabled
		 */
		dev_dbg(hw->dev, "override af_temporal_filter_enable to false\n");
		af_temporal_filter_enable = false;
	}

	ret = ltm_freq_decomp_config_hw(hw, hw_id, enable && cfg->freq_decomp_enable,
					&cfg->freq_decomp) &&
	      ltm_luma_adj_config_hw(hw, hw_id, enable && cfg->luma_adj_enable, &cfg->luma_adj) &&
	      ltm_af_slice_config_hw(hw, hw_id, enable && cfg->af_slice_enable, &cfg->af_slice) &&
	      ltm_af_trans_config_hw(hw, hw_id, enable && cfg->af_trans_enable, &cfg->af_trans) &&
	      ltm_color_config_hw(hw, hw_id, enable && cfg->color_enable, &cfg->ltm_color) &&
	      ltm_dither_config_hw(hw, hw_id, enable && cfg->dither_enable, &cfg->ltm_dither) &&
	      ltm_af_temproal_filter_config_hw(hw, hw_id, enable && af_temporal_filter_enable);
	DPU_ATRACE_END(__func__);

	return ret;
}

static bool gtm_luma_ave_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
				const void *obj_state)
{
	return true;
}

static bool gtm_luma_ave_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	bool ret;
	const struct drm_vs_gtm_luma_ave_config *cfg = data;

	DPU_ATRACE_BEGIN(__func__);
	ret = ltm_enable_config_hw(hw, hw_id, enable, data) &&
	      ltm_degamma_config_hw(hw, hw_id, enable && cfg->degamma_enable, &cfg->ltm_degamma) &&
	      ltm_gamma_config_hw(hw, hw_id, enable && cfg->gamma_enable, &cfg->ltm_gamma) &&
	      ltm_luma_config_hw(hw, hw_id, enable && cfg->luma_enable, &cfg->ltm_luma) &&
	      ltm_luma_ave_set_config_hw(hw, hw_id, enable && cfg->luma_set_enable,
					 &cfg->ltm_luma_set) &&
	      ltm_ds_config_hw(hw, hw_id, enable && cfg->ds_enable, &cfg->ltm_ds);
	DPU_ATRACE_END(__func__);

	return ret;
}

static bool gtm_rendering_config_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
				       const void *obj_state)
{
	return true;
}

static bool gtm_rendering_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	bool ret;
	const struct drm_vs_gtm_rendering_config *cfg = data;

	DPU_ATRACE_BEGIN(__func__);
	ret = ltm_color_config_hw(hw, hw_id, enable && cfg->color_enable, &cfg->ltm_color) &&
	      ltm_dither_config_hw(hw, hw_id, enable && cfg->dither_enable, &cfg->ltm_dither);
	DPU_ATRACE_END(__func__);

	return ret;
}

VS_DC_BLOB_PROPERTY_PROTO(ltm_histogram_config_proto, "LTM_HISTOGRAM_CONFIG",
			  struct drm_vs_ltm_histogram_config, ltm_histogram_config_check, NULL,
			  ltm_histogram_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(ltm_histogram_wdma_config_proto, "LTM_HISTOGRAM_WDMA_CONFIG",
			  struct drm_vs_ltm_histogram_wdma_config, ltm_histogram_wdma_config_check,
			  NULL, ltm_histogram_wdma_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(ltm_rendering_config_proto, "LTM_RENDERING_CONFIG",
			  struct drm_vs_ltm_rendering_config, ltm_rendering_config_check, NULL,
			  ltm_rendering_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(ltm_rendering_config_proto_v2, "LTM_RENDERING_CONFIG_V2",
			  struct drm_vs_ltm_rendering_config_v2, ltm_rendering_config_check_v2,
			  NULL, ltm_rendering_config_hw_v2);

VS_DC_BLOB_PROPERTY_PROTO(ltm_af_config_proto, "LTM_AF_FILTER", struct drm_vs_ltm_af_filter,
			  ltm_af_filter_check, NULL, ltm_af_filter_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(ltm_af_config_proto_v2, "LTM_AF_FILTER_V2",
			  struct drm_vs_ltm_af_filter_v2, ltm_af_filter_check, NULL,
			  ltm_af_filter_config_hw_v2);

VS_DC_BLOB_PROPERTY_PROTO(ltm_tone_adj_config_proto, "LTM_TONE_ADJ", struct drm_vs_ltm_tone_adj,
			  ltm_tone_adj_check, NULL, ltm_tone_adj_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(gtm_luma_ave_proto, "GTM_LUMA_AVE_CONFIG",
			  struct drm_vs_gtm_luma_ave_config, gtm_luma_ave_check, NULL,
			  gtm_luma_ave_config_hw);

VS_DC_BLOB_PROPERTY_PROTO(gtm_rendering_config_proto, "GTM_RENDERING_CONFIG",
			  struct drm_vs_gtm_rendering_config, gtm_rendering_config_check, NULL,
			  gtm_rendering_config_hw);

bool vs_dc_register_ltm_states(struct vs_dc_property_state_group *states,
			       const struct vs_display_info *display_info)
{
	if (display_info->ltm) {
		__ERR_CHECK(vs_dc_property_register_state(states, &ltm_histogram_config_proto),
			    on_error);
		__ERR_CHECK(vs_dc_property_register_state(states, &ltm_histogram_wdma_config_proto),
			    on_error);
		__ERR_CHECK(vs_dc_property_register_state(states, &ltm_rendering_config_proto),
			    on_error);
		__ERR_CHECK(vs_dc_property_register_state(states, &ltm_rendering_config_proto_v2),
			    on_error);
		__ERR_CHECK(vs_dc_property_register_state(states, &ltm_af_config_proto), on_error);
		__ERR_CHECK(vs_dc_property_register_state(states, &ltm_af_config_proto_v2),
			    on_error);
		__ERR_CHECK(vs_dc_property_register_state(states, &ltm_tone_adj_config_proto),
			    on_error);
	}

	if (display_info->gtm) {
		__ERR_CHECK(vs_dc_property_register_state(states, &gtm_luma_ave_proto), on_error);
		__ERR_CHECK(vs_dc_property_register_state(states, &gtm_rendering_config_proto),
			    on_error);
		__ERR_CHECK(vs_dc_property_register_state(states, &ltm_tone_adj_config_proto),
			    on_error);
	}

	return true;
on_error:
	return false;
}
