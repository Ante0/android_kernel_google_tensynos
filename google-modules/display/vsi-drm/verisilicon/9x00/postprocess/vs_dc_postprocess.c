/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */
#include <linux/media-bus-format.h>

#include "vs_dc_postprocess.h"

#include <drm/drm_property.h>
#include "drm/vs_drm.h"
#include "drm/vs_drm_fourcc.h"
#include "vs_dc_property.h"
#include "vs_dc_hw.h"
#include "vs_dc_reg_be.h"
#include "vs_dc_sharpness.h"
#include "vs_dc_lut.h"
#include "vs_dc_display_3d_lut.h"
#include "vs_dc_ltm.h"
#include "vs_dc_scale.h"
#include "vs_dc_display_yuv.h"
#include "vs_dc_histogram.h"
#include "vs_dc.h"
#include "vs_crtc.h"
#include "vs_trace.h"

#define VS_DP_SYNC_MAXRETRIES 30
#define VS_REDUNDANT_DP_SYNC_IRQ_THRESH_MS 100

#define FIND_LOWEST_BIT(n) (__builtin_ffs(n) - 1)
#define FIND_SECOND_LOWEST_BIT(n) (__builtin_ffs(n & (n - 1)) - 1)

static bool spliter_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			  const void *obj_state)
{
	const struct drm_vs_spliter *spliter = data;

	if (spliter) {
		if (((spliter->left_x + spliter->left_w) > spliter->src_w) ||
		    ((spliter->right_x + spliter->right_w) > spliter->src_w)) {
			pr_err("%s spliter size exceed panel origin width.\n", __func__);
			return false;
		}

		if ((spliter->left_x % 2) || (spliter->right_x % 2) || (spliter->left_w % 2) ||
		    (spliter->right_w % 2)) {
			pr_err("%s spliter size(x, w) should to be multiple of 2.\n", __func__);
			return false;
		}
	}

	return true;
}

static bool spliter_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_spliter *spliter = data;
	const struct dc_hw_display *display;
	u32 split_config = 0;
	bool dsc_enabled = false;
	bool vdc_enabled = false;

	trace_config_hw_display_feature_en("SPLITER", hw_id, enable);

	/* validate that hw_id (left) and hw_id + 1 (right) are within bounds */
	if (hw_id >= DC_DISPLAY_NUM - 1) {
		dev_err(hw->dev, "%s invalid input with hw_id %d\n", __func__, hw_id);
		return false;
	}

	display = vs_dc_hw_get_display(hw, hw_id);
	split_config = VS_SET_FIELD(split_config, DCREG_SH_PANEL0_SPLIT_CONFIG, ENABLE, enable);

	if (enable) {
		if (display->info->dsc)
			vs_dc_hw_get_display_property(hw, hw_id, "DSC", &dsc_enabled);
		if (display->info->vdc)
			vs_dc_hw_get_display_property(hw, hw_id, "VDCM", &vdc_enabled);

		if (dsc_enabled)
			split_config = VS_SET_FIELD_VALUE(
				split_config, DCREG_SH_PANEL0_SPLIT_CONFIG, SOURCE, DSC);
		else if (vdc_enabled)
			split_config = VS_SET_FIELD_VALUE(
				split_config, DCREG_SH_PANEL0_SPLIT_CONFIG, SOURCE, VDC);
		else
			split_config = VS_SET_FIELD_VALUE(
				split_config, DCREG_SH_PANEL0_SPLIT_CONFIG, SOURCE, PIPE);

		dc_write(hw, DCREG_SH_PANEL0_SPLIT_LEFT_START_Address,
			 VS_SET_FIELD(0, DCREG_SH_PANEL0_SPLIT_LEFT_START, VALUE, spliter->left_x));
		dc_write(hw, DCREG_SH_PANEL0_SPLIT_LEFT_END_Address,
			 VS_SET_FIELD(0, DCREG_SH_PANEL0_SPLIT_LEFT_END, VALUE,
				      spliter->left_x + spliter->left_w - 1));
		dc_write(hw, DCREG_SH_PANEL0_SPLIT_RIGHT_START_Address,
			 VS_SET_FIELD(0, DCREG_SH_PANEL0_SPLIT_RIGHT_START, VALUE,
				      spliter->right_x));
		dc_write(hw, DCREG_SH_PANEL0_SPLIT_RIGHT_END_Address,
			 VS_SET_FIELD(0, DCREG_SH_PANEL0_SPLIT_RIGHT_END, VALUE,
				      spliter->right_x + spliter->right_w - 1));

		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, hw->display[hw_id].output_id,
					     TIMING_HA_WIDTH_Address),
			 spliter->left_w);
		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, hw->display[hw_id + 1].output_id,
					     TIMING_HA_WIDTH_Address),
			 spliter->right_w);
		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, hw->display[hw_id].output_id,
					     TIMING_HFP_WIDTH_Address),
			 hw->display[hw_id].mode.h_sync_start - spliter->left_w);
		dc_write(hw,
			 VS_SET_OUTPUT_FIELD(DCREG_SH_OUTPUT, hw->display[hw_id + 1].output_id,
					     TIMING_HFP_WIDTH_Address),
			 hw->display[hw_id + 1].mode.h_sync_start - spliter->right_w);
	}

	dc_write(hw, DCREG_SH_PANEL0_SPLIT_CONFIG_Address, split_config);

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(spliter_proto, "SPLITER", struct drm_vs_spliter, spliter_check, NULL,
			  spliter_config_hw);

static bool panel_crop_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			     const void *obj_state)
{
	const struct drm_vs_panel_crop *panel_crop = data;
	u32 panel_width, panel_height;

	if (panel_crop) {
		panel_width = panel_crop->panel_src_width;
		panel_height = panel_crop->panel_src_height;

		if (((panel_crop->crop_rect.x + panel_crop->crop_rect.w) > panel_width) ||
		    ((panel_crop->crop_rect.y + panel_crop->crop_rect.h) > panel_height)) {
			dev_err(hw->dev, "%s panel crop size exceed panel origin width/height.\n",
				__func__);
			return false;
		}

		/* at least 4 pixel remaining after the panel crop, due to hardware limitations */
		if ((panel_crop->crop_rect.x > (panel_width - 4)) ||
		    (panel_crop->crop_rect.y > (panel_height - 4))) {
			dev_err(hw->dev,
				"%s panel crop start x/y need inside [0 : panel_width/height - 4].\n",
				__func__);
			return false;
		}

		if ((panel_crop->crop_rect.w < 4) || (panel_crop->crop_rect.h < 4)) {
			dev_err(hw->dev,
				"%s panel crop size w/h need inside [4 : panel_width/height].\n",
				__func__);
			return false;
		}
	}

	return true;
}

static bool panel_crop_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_panel_crop *panel_crop = data;

	trace_config_hw_display_feature_en("PANEL_CROP", hw_id, enable);

	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, OUT_CROP_EN_Address), enable);

	if (enable) {
		dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, OUT_CROP_START_Address),
			 VS_SET_FIELD(0, DCREG_SH_PANEL0_OUT_CROP_START, X,
				      panel_crop->crop_rect.x) |
				 VS_SET_FIELD(0, DCREG_SH_PANEL0_OUT_CROP_START, Y,
					      panel_crop->crop_rect.y));

		dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, OUT_CROP_SIZE_Address),
			 VS_SET_FIELD(0, DCREG_SH_PANEL0_OUT_CROP_SIZE, WIDTH,
				      panel_crop->crop_rect.w) |
				 VS_SET_FIELD(0, DCREG_SH_PANEL0_OUT_CROP_SIZE, HEIGHT,
					      panel_crop->crop_rect.h));

		dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, WIDTH_Address),
			 panel_crop->panel_src_width);
		dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, HEIGHT_Address),
			 panel_crop->panel_src_height);
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(panel_crop_proto, "PANEL_CROP", struct drm_vs_panel_crop,
			  panel_crop_check, NULL, panel_crop_config_hw);

static bool splice_mode_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			      const void *obj_state)
{
	const struct drm_vs_splice_mode *splice_mode = data;
	const struct vs_crtc_state *vs_crtc_state = obj_state;
	struct vs_dc *dc = to_vs_dc(hw);
	struct drm_vs_splice_config splice0_config, splice1_config;

	if (splice_mode->splice0_enable) {
		splice0_config.crtc_id = FIND_LOWEST_BIT(splice_mode->splice0_crtc_mask);
		splice0_config.crtc_id_ex = FIND_SECOND_LOWEST_BIT(splice_mode->splice0_crtc_mask);

		/* get the output id information form encoder ID, if the ENCODER NONE, the output_id = hw_id */
		if (vs_crtc_state->encoder_type == DRM_MODE_ENCODER_NONE) {
			splice0_config.crtc_ofifo_id =
				dc->hw.display[splice0_config.crtc_id].info->id;
			splice0_config.crtc_ofifo_id_ex =
				dc->hw.display[splice0_config.crtc_id_ex].info->id;
		} else {
			splice0_config.crtc_ofifo_id =
				to_vs_crtc_state(dc->crtc[splice0_config.crtc_id]->base.state)
					->output_id;
			splice0_config.crtc_ofifo_id_ex =
				to_vs_crtc_state(dc->crtc[splice0_config.crtc_id_ex]->base.state)
					->output_id;
		}

		if (splice0_config.crtc_ofifo_id == splice0_config.crtc_ofifo_id_ex) {
			dev_err(hw->dev, "%s splice0 need using two different ofifo port.\n",
				__func__);
			return false;
		}

		/* OFIFO0 and OFIFO1 are the data inputs of splice0 */
		if ((splice0_config.crtc_ofifo_id != 0 && splice0_config.crtc_ofifo_id != 1) ||
		    (splice0_config.crtc_ofifo_id_ex != 0 &&
		     splice0_config.crtc_ofifo_id_ex != 1)) {
			dev_err(hw->dev, "%s splice0 has wrong ofifo port.\n", __func__);
			return false;
		}
	}

	if (splice_mode->splice1_enable) {
		splice1_config.crtc_id = FIND_LOWEST_BIT(splice_mode->splice1_crtc_mask);
		splice1_config.crtc_id_ex = FIND_SECOND_LOWEST_BIT(splice_mode->splice1_crtc_mask);

		/* get the output id information form encoder ID, if the ENCODER NONE, the output_id = hw_id */
		if (vs_crtc_state->encoder_type == DRM_MODE_ENCODER_NONE) {
			splice1_config.crtc_ofifo_id =
				dc->hw.display[splice1_config.crtc_id].info->id;
			splice1_config.crtc_ofifo_id_ex =
				dc->hw.display[splice1_config.crtc_id_ex].info->id;
		} else {
			splice1_config.crtc_ofifo_id =
				to_vs_crtc_state(dc->crtc[splice1_config.crtc_id]->base.state)
					->output_id;
			splice1_config.crtc_ofifo_id_ex =
				to_vs_crtc_state(dc->crtc[splice1_config.crtc_id_ex]->base.state)
					->output_id;
		}

		if (splice1_config.crtc_ofifo_id == splice1_config.crtc_ofifo_id_ex) {
			dev_err(hw->dev, "%s splice1 need using two different ofifo port.\n",
				__func__);
			return false;
		}

		/* OFIFO2 and OFIFO3 are the data inputs of splice1 */
		if ((splice1_config.crtc_ofifo_id != 2 && splice1_config.crtc_ofifo_id != 3) ||
		    (splice1_config.crtc_ofifo_id_ex != 2 &&
		     splice1_config.crtc_ofifo_id_ex != 3)) {
			dev_err(hw->dev, "%s splice1 has wrong ofifo port.\n", __func__);
			return false;
		}
	}

	return true;
}

static bool splice_mode_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_splice_mode *splice_mode = data;
	u32 config = 0;
	struct drm_vs_splice_config splice0_config, splice1_config;

	trace_config_hw_display_feature_en("SPLICE_MODE", hw_id, enable);

	config = dc_read_immediate(hw, DCREG_OUTIF_SPLICE_MODE_Address);

	if (!splice_mode->splice0_enable) {
		config = VS_SET_FIELD_PREDEF(config, DCREG_OUTIF_SPLICE_MODE, SPLICER0, NO_SPLICE);
	} else {
		splice0_config.crtc_id = FIND_LOWEST_BIT(splice_mode->splice0_crtc_mask);
		splice0_config.crtc_id_ex = FIND_SECOND_LOWEST_BIT(splice_mode->splice0_crtc_mask);

		switch (splice_mode->splice0_output_intf) {
		case 0:
			config = VS_SET_FIELD_PREDEF(config, DCREG_OUTIF_SPLICE_MODE, SPLICER0,
						     SPLICE_TO_OUTPUT0);
			break;
		case 1:
			config = VS_SET_FIELD_PREDEF(config, DCREG_OUTIF_SPLICE_MODE, SPLICER0,
						     SPLICE_TO_OUTPUT1);
			break;
		case 2:
		case 3:
			config = VS_SET_FIELD_PREDEF(config, DCREG_OUTIF_SPLICE_MODE, SPLICER0,
						     SPLICE_TO_OUTPUT2_3);
			break;
		default:
			break;
		}

		dc_write(hw,
			 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, splice0_config.crtc_id, WIDTH_Address),
			 splice_mode->src_panel_width0);
		dc_write(hw,
			 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, splice0_config.crtc_id_ex,
					    WIDTH_Address),
			 splice_mode->src_panel_width1);
	}

	if (!splice_mode->splice1_enable) {
		config = VS_SET_FIELD_PREDEF(config, DCREG_OUTIF_SPLICE_MODE, SPLICER1, NO_SPLICE);
	} else {
		splice1_config.crtc_id = FIND_LOWEST_BIT(splice_mode->splice1_crtc_mask);
		splice1_config.crtc_id_ex = FIND_SECOND_LOWEST_BIT(splice_mode->splice1_crtc_mask);

		switch (splice_mode->splice1_output_intf) {
		case 0:
		case 1:
			config = VS_SET_FIELD_PREDEF(config, DCREG_OUTIF_SPLICE_MODE, SPLICER1,
						     SPLICE_TO_OUTPUT0_1);
			break;
		case 2:
			config = VS_SET_FIELD_PREDEF(config, DCREG_OUTIF_SPLICE_MODE, SPLICER1,
						     SPLICE_TO_OUTPUT2);
			break;
		case 3:
			config = VS_SET_FIELD_PREDEF(config, DCREG_OUTIF_SPLICE_MODE, SPLICER1,
						     SPLICE_TO_OUTPUT3);
			break;
		default:
			break;
		}

		dc_write(hw,
			 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, splice1_config.crtc_id, WIDTH_Address),
			 splice_mode->src_panel_width0);
		dc_write(hw,
			 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, splice1_config.crtc_id_ex,
					    WIDTH_Address),
			 splice_mode->src_panel_width1);
	}

	dc_write_immediate(hw, DCREG_OUTIF_SPLICE_MODE_Address, config);

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(splice_mode_proto, "SPLICE_MODE", struct drm_vs_splice_mode,
			  splice_mode_check, NULL, splice_mode_config_hw);

static bool dp_sync_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			  const void *obj_state)
{
	const struct drm_vs_dp_sync *dp_sync = data;
	u8 i = 0;
	bool free_sync_enabled = false;

	for (i = 0; i < HW_DISPLAY_4; i++) {
		if (dp_sync->dp_sync_crtc_mask & BIT(i)) {
			vs_dc_hw_get_display_property(hw, i, "FREE_SYNC", &free_sync_enabled);

			if (free_sync_enabled) {
				dev_err(hw->dev,
					"%s DP sync and free sync can not work on one DP port.\n",
					__func__);
				return false;
			}
		}
	}
	return true;
}

static void wait_all_sync_frm_done(struct dc_hw *hw)
{
	u32 count = 0;

	do {
		count++;
		msleep(VS_REDUNDANT_DP_SYNC_IRQ_THRESH_MS);
	} while ((!hw->all_sync_frm_done) && (count < VS_DP_SYNC_MAXRETRIES));

	if (count == VS_DP_SYNC_MAXRETRIES)
		dev_err(hw->dev, "%s Wait all dp sync frm done time out.\n", __func__);
	else
		hw->all_sync_frm_done = false;
}

static bool dp_sync_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_dp_sync *dp_sync = data;
	const struct dc_hw_display *hw_display, *hw_display_ex = vs_dc_hw_get_display(hw, hw_id);
	u8 output_id, output_id_ex = hw_display_ex->output_id;
	u8 i = 0, output_mask = 0;

	trace_config_hw_display_feature_en("DP_SYNC", hw_id, enable);

	if (enable) {
		/* stop DP interface which want to sync */
		for (i = 0; i < HW_DISPLAY_4; i++) {
			if (dp_sync->dp_sync_crtc_mask & BIT(i)) {
				hw_display = vs_dc_hw_get_display(hw, i);
				output_id = hw_display->output_id;
				output_mask |= BIT(output_id);

				dc_write_immediate(hw,
						   VS_SET_OUTPUT_FIELD(DCREG_OUTPUT, output_id,
								       START_Address),
						   0);
			}
		}

		/* set DP interface which enable DP sync and which one is the master */
		dc_write_immediate(hw, DCREG_OUTPUT_SYNC_CONFIG_Address,
				   VS_SET_FIELD(0, DCREG_OUTPUT_SYNC_CONFIG, ENABLE, output_mask) |
					   VS_SET_FIELD(0, DCREG_OUTPUT_SYNC_CONFIG, SEL_MASTER,
							output_id_ex));

		/* wait interrupt "all_sync_frm_done = 1" to inform that all sync related DP is turned off*/
		wait_all_sync_frm_done(hw);

	} else {
		/* stop all synced DP interface */
		dc_write_immediate(hw, DCREG_OUTPUT_SYNC_START_Address,
				   VS_SET_FIELD_PREDEF(0, DCREG_OUTPUT_SYNC_START, VALUE, STOP));

		/* wait interrupt "all_sync_frm_done = 1" to inform that all sync related DP is turned off*/
		wait_all_sync_frm_done(hw);

		/* end this round of DP sync */
		dc_write_immediate(hw, DCREG_OUTPUT_SYNC_CONFIG_Address,
				   VS_SET_FIELD(0, DCREG_OUTPUT_SYNC_CONFIG, ENABLE, 0));
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(dp_sync_proto, "DP_SYNC", struct drm_vs_dp_sync, dp_sync_check, NULL,
			  dp_sync_config_hw);

static bool free_sync_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			    const void *obj_state)
{
	bool dp_sync_enabled = false;

	vs_dc_hw_get_display_property(hw, hw_id, "DP_SYNC", &dp_sync_enabled);
	if (dp_sync_enabled) {
		dev_err(hw->dev, "%s DP sync and free sync can not work on one DP port.\n",
			__func__);
		return false;
	}

	return true;
}

static bool free_sync_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_free_sync *free_sync = data;
	const struct dc_hw_display *hw_display = vs_dc_hw_get_display(hw, hw_id);
	const u8 output_id = hw_display->output_id;

	trace_config_hw_display_feature_en("FREE_SYNC", hw_id, enable);

	if (enable) {
		if (free_sync->type == VS_FREE_SYNC_CONFIG ||
		    free_sync->type == VS_FREE_SYNC_CONFIG_FINISH) {
			dc_write_immediate(hw,
					   VS_SET_OUTPUT_FIELD(DCREG_OUTPUT, output_id,
							       FREE_SYNC_CONFIG_Address),
					   (enable) |
						   (free_sync->mode.free_sync_max_delay << 16));
		}

		if (free_sync->type == VS_FREE_SYNC_FINISH ||
		    free_sync->type == VS_FREE_SYNC_CONFIG_FINISH) {
			dc_write_immediate(hw,
					   VS_SET_OUTPUT_FIELD(DCREG_OUTPUT, output_id,
							       FREE_SYNC_FINISH_Address),
					   free_sync->mode.free_sync_finish);
		}
	} else {
		dc_write_immediate(
			hw, VS_SET_OUTPUT_FIELD(DCREG_OUTPUT, output_id, FREE_SYNC_CONFIG_Address),
			0);
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(free_sync_proto, "FREE_SYNC", struct drm_vs_free_sync, free_sync_check,
			  NULL, free_sync_config_hw);

static bool bg_color_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			   const void *obj_state)
{
	const struct dc_hw_display *hw_display = vs_dc_hw_get_display(hw, hw_id);
	const struct vs_display_info *display_info = hw_display->info;

	if (!display_info->background) {
		dev_err(hw->dev, "%s The display is not support set background color.\n", __func__);
		return false;
	}

	return true;
}

static bool bg_color_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_color *bg_color = data;

	trace_config_hw_display_feature_en("BG_COLOR", hw_id, enable);

	if (enable) {
		if (hw_id <= HW_DISPLAY_3) {
			dc_write(hw,
				 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, BG_COLOR_ALPHA_Address),
				 bg_color->a);
			dc_write(hw,
				 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, BG_COLOR_RED_Address),
				 bg_color->r);
			dc_write(hw,
				 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, BG_COLOR_GREEN_Address),
				 bg_color->g);
			dc_write(hw,
				 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, BG_COLOR_BLUE_Address),
				 bg_color->b);
		} else {
			dc_write(hw, DCREG_SH_WB_BG_COLOR_ALPHA_Address, bg_color->a);
			dc_write(hw, DCREG_SH_WB_BG_COLOR_RED_Address, bg_color->r);
			dc_write(hw, DCREG_SH_WB_BG_COLOR_GREEN_Address, bg_color->g);
			dc_write(hw, DCREG_SH_WB_BG_COLOR_BLUE_Address, bg_color->b);
		}
	} else {
		if (hw_id <= HW_DISPLAY_3) {
			dc_write(hw,
				 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, BG_COLOR_ALPHA_Address),
				 DCREG_SH_PANEL0_BG_COLOR_ALPHA_ResetValue);
			dc_write(hw,
				 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, BG_COLOR_RED_Address),
				 DCREG_SH_PANEL0_BG_COLOR_RED_ResetValue);
			dc_write(hw,
				 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, BG_COLOR_GREEN_Address),
				 DCREG_SH_PANEL0_BG_COLOR_GREEN_ResetValue);
			dc_write(hw,
				 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, BG_COLOR_BLUE_Address),
				 DCREG_SH_PANEL0_BG_COLOR_BLUE_ResetValue);
		} else {
			dc_write(hw, DCREG_SH_WB_BG_COLOR_ALPHA_Address,
				 DCREG_SH_WB_BG_COLOR_ALPHA_ResetValue);
			dc_write(hw, DCREG_SH_WB_BG_COLOR_RED_Address,
				 DCREG_SH_WB_BG_COLOR_RED_ResetValue);
			dc_write(hw, DCREG_SH_WB_BG_COLOR_GREEN_Address,
				 DCREG_SH_WB_BG_COLOR_GREEN_ResetValue);
			dc_write(hw, DCREG_SH_WB_BG_COLOR_BLUE_Address,
				 DCREG_SH_WB_BG_COLOR_BLUE_ResetValue);
		}
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(bg_color_proto, "BG_COLOR", struct drm_vs_color, bg_color_check, NULL,
			  bg_color_config_hw);

/* low level dither */
static bool llv_dither_degamma_check(const struct dc_hw *hw, u8 hw_id,
				     const struct drm_vs_ltm_xgamma *degamma)
{
	int i;

	if (!degamma) {
		dev_err(hw->dev, "%s: crtc[%u] degamma data is NULL.\n", __func__, hw_id);
		return false;
	}

	for (i = 0; i < VS_LTM_XGAMMA_COEF_NUM; i++) {
		if (degamma->coef[i] > (1 << VS_MAX_GAMUT_COEF_NUM)) {
			dev_err(hw->dev, "%s: crtc[%u] degamma coef[%d] out of range.\n", __func__,
				hw_id, i);
			return false;
		}
	}

	return true;
}
static bool llv_dither_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			     const void *obj_state)
{
	const struct drm_vs_llv_dither *llv = data;

	if (llv->index_type == VS_DTH_FRM_IDX_NONE || llv->index_type > VS_DTH_FRM_IDX_HW) {
		dev_err(hw->dev, "%s: crtc[%u] has invalid frame index type.\n", __func__, hw_id);
		return false;
	}
	if (llv->index_type == VS_DTH_FRM_IDX_SW && llv->sw_index > 7) {
		dev_err(hw->dev, "%s: crtc[%u] has invalid sw index.\n", __func__, hw_id);
		return false;
	}
	if (llv->degamma.enable)
		if (!llv_dither_degamma_check(hw, hw_id, &llv->degamma.data))
			return false;

	return true;
}

static void llv_dither_degamma_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	u32 entry_cnt = 0;
	u32 base_offset = 0x4;
	const struct drm_vs_ltm_xgamma *degamma = data;

	trace_config_hw_display_feature_en("LLV_DEGAMMA", hw_id, enable);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, LL_DE_GAMMA_ENABLE_Address),
		 enable);

	if (enable) {
		for (entry_cnt = 0; entry_cnt < VS_LTM_XGAMMA_COEF_NUM; entry_cnt++) {
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      LL_DE_GAMMA_DATA_Address) +
					 (entry_cnt * base_offset),
				 VS_SET_FIELD(0, DCREG_SH_PANEL0_LL_DE_GAMMA_DATA, VALUE,
					      degamma->coef[entry_cnt]));
		}
	}
}

static bool llv_dither_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_llv_dither *llv = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);
	u32 config;

	trace_config_hw_display_feature_en("LLV_DITHER", hw_id, enable);

	config = dc_read(hw, DCREG_SH_PANEL0_CONFIG_Address + offset);
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, LOW_LEVEL_DITHER, enable);
	dc_write(hw, DCREG_SH_PANEL0_CONFIG_Address + offset, config);
	if (enable) {
		dc_write(hw, DCREG_SH_PANEL0_LL_DTH_THRESH_Address + offset, llv->threshold);
		dc_write(hw, DCREG_SH_PANEL0_LL_DTH_LINEAR_THRESH_Address + offset,
			 llv->linear_threshold);
		dc_write(hw, DCREG_SH_PANEL0_LL_DTH_START_X_Address + offset, llv->start_x);
		dc_write(hw, DCREG_SH_PANEL0_LL_DTH_START_Y_Address + offset, llv->start_y);
		dc_write(hw, DCREG_SH_PANEL0_LL_DTH_PRNG_XM_Address + offset, llv->mask);

		/* index */
		config = dc_read(hw, DCREG_SH_PANEL0_LL_DTH_CONFIG_Address + offset);
		if (llv->index_type == VS_DTH_FRM_IDX_SW) {
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_LL_DTH_CONFIG,
						    TEMP_DITHER, DISABLED);
			dc_write(hw, DCREG_SH_PANEL0_LL_DTH_FI_SEL_Address + offset,
				 VS_SET_FIELD(0, DCREG_SH_PANEL0_LL_DTH_FI_SEL, VALUE,
					      llv->sw_index));
		} else {
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_LL_DTH_CONFIG,
						    TEMP_DITHER, ENABLED);
		}
		dc_write(hw, DCREG_SH_PANEL0_LL_DTH_CONFIG_Address + offset, config);
		/* seed */
		if (llv->seed.hash_seed_x_enable)
			dc_write_u32_blob(hw, DCREG_SH_PANEL0_LL_DTH_HS_X0_Address + offset,
					  llv->seed.hash_seed_x, VS_RANDOM_DITHER_SEED_NUM);
		if (llv->seed.hash_seed_y_enable)
			dc_write_u32_blob(hw, DCREG_SH_PANEL0_LL_DTH_HS_Y0_Address + offset,
					  llv->seed.hash_seed_y, VS_RANDOM_DITHER_SEED_NUM);
		if (llv->seed.permut_seed1_enable)
			dc_write_u32_blob(hw, DCREG_SH_PANEL0_LL_DTH_PS_A1_NUM0_Address + offset,
					  llv->seed.permut_seed1, VS_RANDOM_DITHER_SEED_NUM);
		if (llv->seed.permut_seed2_enable)
			dc_write_u32_blob(hw, DCREG_SH_PANEL0_LL_DTH_PS_A2_NUM0_Address + offset,
					  llv->seed.permut_seed2, VS_RANDOM_DITHER_SEED_NUM);
		/* degamma */
		llv_dither_degamma_config_hw(hw, hw_id, llv->degamma.enable, &llv->degamma.data);
	} else {
		llv_dither_degamma_config_hw(hw, hw_id, false, NULL);
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(llv_dither_proto, "LLV_DITHER", struct drm_vs_llv_dither,
			  llv_dither_check, NULL, llv_dither_config_hw);

/* gamma dither */
static bool dither_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			 const void *obj_state)
{
	const struct drm_vs_dither *dither = data;

	if (dither->index_type > VS_DTH_FRM_IDX_HW) {
		dev_err(hw->dev, "%s: crtc[%u] has invalid frame index type.\n", __func__, hw_id);
		return false;
	}

	if (dither->index_type == VS_DTH_FRM_IDX_SW && dither->sw_index > 3) {
		dev_err(hw->dev, "%s: crtc[%u] has invalid sw index.\n", __func__, hw_id);
		return false;
	}

	return true;
}

static bool gamma_dither_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_dither *dither = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);
	u32 config;

	trace_config_hw_display_feature_en("GAMMA_DITHER", hw_id, enable);

	config = dc_read(hw, DCREG_SH_PANEL0_CONFIG_Address + offset);
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, GAMMA_DITHER, enable);
	dc_write(hw, DCREG_SH_PANEL0_CONFIG_Address + offset, config);
	if (enable) {
		dc_write(hw, DCREG_SH_PANEL0_GAMMA_DTH_RTABLE_LOW_Address + offset,
			 dither->table_low[0]);
		dc_write(hw, DCREG_SH_PANEL0_GAMMA_DTH_GTABLE_LOW_Address + offset,
			 dither->table_low[1]);
		dc_write(hw, DCREG_SH_PANEL0_GAMMA_DTH_BTABLE_LOW_Address + offset,
			 dither->table_low[2]);
		dc_write(hw, DCREG_SH_PANEL0_GAMMA_DTH_RTABLE_HIGH_Address + offset,
			 dither->table_high[0]);
		dc_write(hw, DCREG_SH_PANEL0_GAMMA_DTH_GTABLE_HIGH_Address + offset,
			 dither->table_high[1]);
		dc_write(hw, DCREG_SH_PANEL0_GAMMA_DTH_BTABLE_HIGH_Address + offset,
			 dither->table_high[2]);

		/* index */
		config = dc_read(hw, DCREG_SH_PANEL0_GAMMA_DTH_CFG_Address + offset);
		if (dither->index_type == VS_DTH_FRM_IDX_NONE) {
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_GAMMA_DTH_CFG,
						    FRAME_INDEX_ENABLE, DISABLED);
		} else if (dither->index_type == VS_DTH_FRM_IDX_HW) {
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_GAMMA_DTH_CFG,
						    FRAME_INDEX_ENABLE, ENABLED);
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_GAMMA_DTH_CFG,
						    FRAME_INDEX_FROM, HW);
		} else {
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_GAMMA_DTH_CFG,
						    FRAME_INDEX_ENABLE, ENABLED);
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_GAMMA_DTH_CFG,
						    FRAME_INDEX_FROM, SW);
			config = VS_SET_FIELD(config, DCREG_SH_PANEL0_GAMMA_DTH_CFG, SW_FRAME_INDEX,
					      dither->sw_index);
		}
		dc_write(hw, DCREG_SH_PANEL0_GAMMA_DTH_CFG_Address + offset, config);
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(gamma_dither_proto, "GAMMA_DITHER", struct drm_vs_dither, dither_check,
			  NULL, gamma_dither_config_hw);

/* panel dither */
static u32 set_wb_dither_mode_config(u8 format, u32 config)
{
	switch (format) {
	case BLD_WB_FORMAT_A2RGB101010:
	case BLD_WB_FORMAT_P010:
		config = VS_SET_FIELD_VALUE(0, DCREG_SH_BLD_WB_CONFIG, DITHER_MODE, BIT10);
		break;
	case BLD_WB_FORMAT_NV12:
	case BLD_WB_FORMAT_NV16:
	case BLD_WB_FORMAT_YV12:
	case BLD_WB_FORMAT_ARGB8888:
	default:
		config = VS_SET_FIELD_VALUE(0, DCREG_SH_BLD_WB_CONFIG, DITHER_MODE, BIT8);
		break;
	}
	return config;
}

static u32 set_dither_mode_config(u32 hw_id, u32 format, u32 config)
{
	switch (format) {
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
		if (hw_id <= HW_DISPLAY_1)
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_CONFIG, DITHER_MODE,
						    BIT6);
		else if (hw_id <= HW_DISPLAY_3)
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL2_CONFIG, DITHER_MODE,
						    BIT6);
		break;
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_YUV10_1X30:
	case MEDIA_BUS_FMT_UYVY10_1X20:
		if (HW_DISPLAY_2 == hw_id || HW_DISPLAY_3 == hw_id)
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL2_CONFIG, DITHER_MODE,
						    BIT10);
		break;
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_UYVY8_1X16:
	default:
		if (hw_id <= HW_DISPLAY_1)
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_CONFIG, DITHER_MODE,
						    BIT8);
		else if (hw_id <= HW_DISPLAY_3)
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL2_CONFIG, DITHER_MODE,
						    BIT8);
		break;
	}
	return config;
}

static bool panel_dither_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_dither *dither = data;
	u32 config_offset = vs_dc_get_display_offset(hw_id);
	u32 offset = vs_dc_get_panel_dither_table_offset(hw_id);
	u8 wb_id = hw->display[hw_id].wb.wb_id;
	u32 config;

	trace_config_hw_display_feature_en("PANEL_DITHER", hw_id, enable);

	config = dc_read(hw, DCREG_SH_PANEL0_CONFIG_Address + config_offset);
	/* set dither mode */
	if (hw_id == HW_DISPLAY_4)
		config = set_wb_dither_mode_config(hw->wb[wb_id].fb.format, config);
	else
		config = set_dither_mode_config(hw_id, hw->display[hw_id].mode.bus_format, config);
	/* set switch */
	switch (hw_id) {
	case HW_DISPLAY_0:
	case HW_DISPLAY_1:
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, DITHER, enable);
		break;
	case HW_DISPLAY_2:
	case HW_DISPLAY_3:
		config = VS_SET_FIELD(config, DCREG_SH_PANEL2_CONFIG, DITHER, enable);
		break;
	case HW_DISPLAY_4:
		config = VS_SET_FIELD(config, DCREG_SH_BLD_WB_CONFIG, DITHER, enable);
		break;
	default:
		break;
	}
	dc_write(hw, DCREG_SH_PANEL0_CONFIG_Address + config_offset, config);

	if (enable) {
		dc_write(hw, DCREG_SH_PANEL0_DTH_RTABLE_LOW_Address + offset, dither->table_low[0]);
		dc_write(hw, DCREG_SH_PANEL0_DTH_GTABLE_LOW_Address + offset, dither->table_low[1]);
		dc_write(hw, DCREG_SH_PANEL0_DTH_BTABLE_LOW_Address + offset, dither->table_low[2]);
		dc_write(hw, DCREG_SH_PANEL0_DTH_RTABLE_HIGH_Address + offset,
			 dither->table_high[0]);
		dc_write(hw, DCREG_SH_PANEL0_DTH_GTABLE_HIGH_Address + offset,
			 dither->table_high[1]);
		dc_write(hw, DCREG_SH_PANEL0_DTH_BTABLE_HIGH_Address + offset,
			 dither->table_high[2]);

		/* index */
		config = dc_read(hw, DCREG_SH_PANEL0_DTH_CFG_Address + config_offset);
		if (dither->index_type == VS_DTH_FRM_IDX_NONE) {
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_DTH_CFG,
						    FRAME_INDEX_ENABLE, DISABLED);
		} else if (dither->index_type == VS_DTH_FRM_IDX_HW) {
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_DTH_CFG,
						    FRAME_INDEX_ENABLE, ENABLED);
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_DTH_CFG,
						    FRAME_INDEX_FROM, HW);
		} else {
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_DTH_CFG,
						    FRAME_INDEX_ENABLE, ENABLED);
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_DTH_CFG,
						    FRAME_INDEX_FROM, SW);
			config = VS_SET_FIELD(config, DCREG_SH_PANEL0_DTH_CFG, SW_FRAME_INDEX,
					      dither->sw_index);
		}

		if (dither->frm_mode == VS_DTH_FRM_16)
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_DTH_CFG, FRAME_MODE,
						    FRAME16);
		else if (dither->frm_mode == VS_DTH_FRM_8)
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_DTH_CFG, FRAME_MODE,
						    FRAME8);
		else
			config = VS_SET_FIELD_VALUE(config, DCREG_SH_PANEL0_DTH_CFG, FRAME_MODE,
						    FRAME4);

		dc_write(hw, DCREG_SH_PANEL0_DTH_CFG_Address + config_offset, config);
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(panel_dither_proto, "PANEL_DITHER", struct drm_vs_dither, dither_check,
			  NULL, panel_dither_config_hw);

/* blur */
static bool blur_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
		       const void *obj_state)
{
	const struct drm_vs_blur *blur = data;
	const struct vs_dc_info *info = hw->info;
	u32 blur_coef_bit = info->blur_coef_bits;
	const struct dc_hw_display *hw_display = vs_dc_hw_get_display(hw, hw_id);
	const struct vs_display_info *display_info = hw_display->info;
	u32 i, j, sum_h = 0;

	if (!display_info->blur) {
		dev_err(hw->dev, "%s: Current crtc not support blur.\n", __func__);
		return false;
	}

	if (blur->roi.w < VS_BLUR_ROI_MIN_WIDTH || blur->roi.w > VS_BLUR_ROI_MAX_WIDTH) {
		dev_err(hw->dev, "%s: Blur ROI width out of range.\n", __func__);
		return false;
	}

	for (i = 0; i < 3; i++) {
		for (j = 0; j < blur->coef_num; j++) {
			if (blur->coef[i][j] >> blur_coef_bit) {
				dev_err(hw->dev, "%s: Blur coef value out of range.\n", __func__);
				return false;
			}

			if (j == 3)
				sum_h += blur->coef[i][j];
			else
				sum_h += 2 * blur->coef[i][j];
		}

		if (sum_h < 1 || sum_h > (1 << blur_coef_bit)) {
			dev_err(hw->dev, "%s: Sum of coef in each channel should in [1,64].\n",
				__func__);
			return false;
		}

		sum_h = 0;
	}

	return true;
}

static bool blur_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_blur *blur = data;
	u32 config = 0, reg_offset = 0x04;
	u32 i = 0, j = 0;

	trace_config_hw_display_feature_en("BLUR", hw_id, enable);

	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, BLUR, enable);
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address), config);

	if (enable) {
		/* set blur ROI x/y/w/h */
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, BLUR_ROI_ORIGIN_Address),
			 (0xFFFF & blur->roi.x) | (blur->roi.y << 16));

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, BLUR_ROI_SIZE_Address),
			 (0xFFFF & blur->roi.w) | (blur->roi.h << 16));

		/* set R/G/B channel normalize factor. */
		for (i = 0; i < 3; i++) {
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      BLUR_HOR_FILTER_NORM_WEIGHT_RED_Address) +
					 i * reg_offset,
				 blur->norm[i]);
		}

		/* set R/G/B blur coef */
		for (i = 0; i < 3; i++) {
			for (j = 0; j < VS_BLUR_COEF_NUM; j++) {
				dc_write(hw,
					 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
							      BLUR_HORI_RCOEF0_Address) +
						 (4 * i + j) * reg_offset,
					 blur->coef[i][j]);
			}
		}
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(blur_proto, "BLUR", struct drm_vs_blur, blur_check, NULL, blur_config_hw);

/* brightness */
static bool brightness_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			     const void *obj_state)
{
	const struct drm_vs_brightness *brightness = data;

	if (brightness->mode > VS_BRIGHTNESS_CAL_MODE_MAX) {
		dev_err(hw->dev, "%s: crtc[%u] brightness cal mode is invalid %#x.\n", __func__,
			hw_id, brightness->mode);
		return false;
	}

	if (brightness->target > VS_MAX_TARGET_GAIN_VALUE) {
		dev_err(hw->dev, "%s: crtc[%u] brightness target gain is invalid %#x.\n", __func__,
			hw_id, brightness->target);
		return false;
	}

	if (brightness->threshold >= VS_MAX_BRIGHTNESS_VALUE) {
		dev_err(hw->dev, "%s: crtc[%u] brightness protection threshold is invalid %#x.\n",
			__func__, hw_id, brightness->threshold);
		return false;
	}

	if (brightness->mode == VS_BRIGHTNESS_CAL_MODE_WEIGHT) {
		if ((brightness->luma_coef[0] + brightness->luma_coef[1] +
		     brightness->luma_coef[2]) != 1024) {
			dev_err(hw->dev, "%s: crtc[%u] brightness R/G/B weight coef is invalid.\n",
				__func__, hw_id);
			return false;
		}
	}

	return true;
}

static bool brightness_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_brightness *brightness = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);
	u32 config;

	trace_config_hw_display_feature_en("BRIGHTNESS", hw_id, enable);

	config = dc_read(hw, DCREG_SH_PANEL0_CONFIG_Address + offset);
	config = VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, BRIGHTNESS, enable);
	dc_write(hw, DCREG_SH_PANEL0_CONFIG_Address + offset, config);
	if (enable) {
		switch (brightness->mode) {
		case VS_BRIGHTNESS_CAL_MODE_WEIGHT:
			config = VS_SET_FIELD(0, DCREG_SH_PANEL0_BRIGHT_LUMA_MODE, VALUE,
					      DCREG_SH_PANEL0_BRIGHT_LUMA_MODE_VALUE_WEIGHT_RGB);
			break;
		case VS_BRIGHTNESS_CAL_MODE_MAX:
			config = VS_SET_FIELD(0, DCREG_SH_PANEL0_BRIGHT_LUMA_MODE, VALUE,
					      DCREG_SH_PANEL0_BRIGHT_LUMA_MODE_VALUE_MAX_RGB);
			break;
		default:
			break;
		}
		dc_write(hw, DCREG_SH_PANEL0_BRIGHT_LUMA_MODE_Address + offset, config);
		/* commit the target value */
		dc_write(hw, DCREG_SH_PANEL0_BRIGHT_TARGET_Address + offset,
			 VS_SET_FIELD(0, DCREG_SH_PANEL0_BRIGHT_TARGET, VALUE, brightness->target));

		/* commit the protection threshold */
		dc_write(hw, DCREG_SH_PANEL0_BRIGHT_PROTECT_THRESHOLD_Address + offset,
			 VS_SET_FIELD(0, DCREG_SH_PANEL0_BRIGHT_PROTECT_THRESHOLD, VALUE,
				      brightness->threshold));

		/*  commit the R/G/B weight coef*/
		if (brightness->mode == VS_BRIGHTNESS_CAL_MODE_WEIGHT) {
			dc_write(hw, DCREG_SH_PANEL0_BRIGHT_LUMA_RCOEF_Address + offset,
				 VS_SET_FIELD(0, DCREG_SH_PANEL0_BRIGHT_LUMA_RCOEF, VALUE,
					      brightness->luma_coef[0]));
			dc_write(hw, DCREG_SH_PANEL0_BRIGHT_LUMA_GCOEF_Address + offset,
				 VS_SET_FIELD(0, DCREG_SH_PANEL0_BRIGHT_LUMA_GCOEF, VALUE,
					      brightness->luma_coef[1]));
			dc_write(hw, DCREG_SH_PANEL0_BRIGHT_LUMA_BCOEF_Address + offset,
				 VS_SET_FIELD(0, DCREG_SH_PANEL0_BRIGHT_LUMA_BCOEF, VALUE,
					      brightness->luma_coef[2]));
		}
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(brightness_proto, "BRIGHTNESS", struct drm_vs_brightness,
			  brightness_check, NULL, brightness_config_hw);

static bool brightness_roi_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
				 const void *obj_state)
{
	const struct drm_vs_brightness_roi *brightness_roi = data;

	if (!brightness_roi->roi0_enable && !brightness_roi->roi1_enable) {
		dev_err(hw->dev, "%s: crtc[%u] brightness at least have a enabled ROI.\n", __func__,
			hw_id);
		return false;
	}

	return true;
}

static bool brightness_roi_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_brightness_roi *brightness_roi = data;
	const u32 offset = vs_dc_get_display_offset(hw_id);

	trace_config_hw_display_feature_en("BRIGHTNESS_ROI", hw_id, enable);

	/* ROI0 */
	if (brightness_roi->roi0_enable) {
		dc_write(hw, DCREG_SH_PANEL0_BRIGHT_ROI0_ORIGIN_Address + offset,
			 (brightness_roi->roi0.y
			  << __vsFIELDSTART(DCREG_SH_PANEL0_BRIGHT_ROI0_ORIGIN_Y)) |
				 brightness_roi->roi0.x);
		dc_write(hw, DCREG_SH_PANEL0_BRIGHT_ROI0_SIZE_Address + offset,
			 (brightness_roi->roi0.h
			  << __vsFIELDSTART(DCREG_SH_PANEL0_BRIGHT_ROI0_SIZE_HEIGHT)) |
				 brightness_roi->roi0.w);
	}
	dc_write(hw, DCREG_SH_PANEL0_BRIGHT_ROI0_VALID_Address + offset,
		 brightness_roi->roi0_enable & BIT(0));

	/* ROI1 */
	if (brightness_roi->roi1_enable) {
		dc_write(hw, DCREG_SH_PANEL0_BRIGHT_ROI1_ORIGIN_Address + offset,
			 (brightness_roi->roi1.y
			  << __vsFIELDSTART(DCREG_SH_PANEL0_BRIGHT_ROI1_ORIGIN_Y)) |
				 brightness_roi->roi1.x);
		dc_write(hw, DCREG_SH_PANEL0_BRIGHT_ROI1_SIZE_Address + offset,
			 (brightness_roi->roi1.h
			  << __vsFIELDSTART(DCREG_SH_PANEL0_BRIGHT_ROI1_SIZE_HEIGHT)) |
				 brightness_roi->roi1.w);
	}
	dc_write(hw, DCREG_SH_PANEL0_BRIGHT_ROI1_VALID_Address + offset,
		 brightness_roi->roi1_enable & BIT(0));
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(brightness_roi_proto, "BRIGHTNESS_ROI", struct drm_vs_brightness_roi,
			  brightness_roi_check, NULL, brightness_roi_config_hw);

static bool ccm_lnr_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_ccm *ccm_lnr = data;
	u32 reg_coef = 0;
	u32 reg_offset = 0;
	u32 config = 0;

	trace_config_hw_display_feature_en("CCM_LNR", hw_id, enable);

	if (hw->rev == DC_REV_0) {
		reg_coef = VS_SH_PANEL_FIELD(hw_id, GAMUT_MATRIX_COEF0_Address);
		reg_offset = VS_SH_PANEL_FIELD(hw_id, GAMUT_MATRIX_COEF_OFFSET0_Address);
	}

	/* enable CCM */
	config = dc_read(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address));
	config = (hw_id > HW_DISPLAY_1) ?
			 (VS_SET_FIELD(config, DCREG_SH_PANEL2_CONFIG, GAMUT_MATRIX, enable)) :
			 (VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, GAMUT_MATRIX, enable));
	dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address), config);

	if (enable) {
		/* C0 - C8 configuration */
		dc_write_u32_blob(hw, reg_coef, ccm_lnr->coef, 9);
		/* D0 - D2 configuration */
		dc_write_u32_blob(hw, reg_offset, ccm_lnr->offset, 3);
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(ccm_lnr_proto, "CCM_LNR", struct drm_vs_ccm, NULL, NULL,
			  ccm_lnr_config_hw);

static bool ccm_non_lnr_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_ccm *non_lnr = data;
	const u32 reg_coef = VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, MATRIX_COEF0_Address);
	const u32 reg_offset =
		VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, MATRIX_COEF_OFFSET0_Address);
	const u32 coef_offset = 0x04;

	trace_config_hw_display_feature_en("CCM_NON_LNR", hw_id, enable);

	UPDATE_PANEL_CONFIG(hw, hw_id, MATRIX, enable);
	if (enable) {
		/* C0 - C8 configuration */
		dc_write(hw, reg_coef, (0xFFFF & non_lnr->coef[0]) | (non_lnr->coef[1] << 16));
		dc_write(hw, reg_coef + coef_offset * 1,
			 (0xFFFF & non_lnr->coef[2]) | (non_lnr->coef[3] << 16));
		dc_write(hw, reg_coef + coef_offset * 2,
			 (0xFFFF & non_lnr->coef[4]) | (non_lnr->coef[5] << 16));
		dc_write(hw, reg_coef + coef_offset * 3,
			 (0xFFFF & non_lnr->coef[6]) | (non_lnr->coef[7] << 16));
		dc_write(hw, reg_coef + coef_offset * 4, non_lnr->coef[8]);
		/* D0 - D2 configuration */
		dc_write_u32_blob(hw, reg_offset, non_lnr->offset, 3);
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(ccm_non_lnr_proto, "CCM_NON_LNR", struct drm_vs_ccm, NULL, NULL,
			  ccm_non_lnr_config_hw);

static bool degamma_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			  const void *obj_state)
{
	const struct vs_dc_info *info = hw->info;
	const u32 in_bit = info->pre_degamma_bits;
	const u32 out_bit = info->degamma_bits;
	const u32 max_seg = info->max_seg_num;
	const u32 max_entry = info->max_degamma_size;
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

static bool _9400_degamma_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_xstep_lut *degamma = data;
	const u32 reg_seg_cnt =
		VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, DE_GAMMA_SEGMENT_COUNT_Address);
	const u32 reg_seg_pnt =
		VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, DE_GAMMA_SEGMENT_POINT1_Address);
	const u32 reg_seg_step =
		VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, DE_GAMMA_SEGMENT_STEP_Address);
	const u32 reg_seg_data = VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, DE_GAMMA_DATA_Address);

	trace_config_hw_display_feature_en("9400_DEGAMMA", hw_id, enable);

	UPDATE_PANEL_CONFIG(hw, hw_id, DE_GAMMA, enable);

	/* C0 - C8 configuration */
	if (enable) {
		/* segment count start from 0, so the configured value minus 1*/
		dc_write(hw, reg_seg_cnt, degamma->seg_cnt - 1);

		/* commit the segment points */
		dc_write_u32_blob(hw, reg_seg_pnt, degamma->seg_point, degamma->seg_cnt - 1);

		/* commit the segment steps */
		dc_write_u32_blob(hw, reg_seg_step, degamma->seg_step, degamma->seg_cnt);

		/* commit the coef data */
		dc_write_u32_blob(hw, reg_seg_data, degamma->data, degamma->entry_cnt);
	}
	return true;
}

static bool _degamma_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	if (hw->rev == DC_REV_0)
		return _9400_degamma_config_hw(hw, hw_id, enable, data);

	return false;
}

VS_DC_BLOB_PROPERTY_PROTO(degamma_proto, "DEGAMMA", struct drm_vs_xstep_lut, degamma_check, NULL,
			  _degamma_config_hw);

static bool data_trunc_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			     const void *obj_state)
{
	const struct drm_vs_data_trunc *data_trunc = data;

	if (data_trunc->gamma_data_trunc > VS_DATA_ROUNDING) {
		dev_err(hw->dev, "%s: crtc[%u] gamma data trunc mode is invalid %#x.\n", __func__,
			hw_id, data_trunc->gamma_data_trunc);
		return false;
	}

	if (data_trunc->panel_data_trunc > VS_DATA_ROUNDING) {
		dev_err(hw->dev, "%s: crtc[%u] panel data trunc mode is invalid %#x.\n", __func__,
			hw_id, data_trunc->panel_data_trunc);
		return false;
	}

	if (data_trunc->blend_data_trunc > VS_DATA_ROUNDING) {
		dev_err(hw->dev, "%s: crtc[%u] blend data trunc mode is invalid %#x.\n", __func__,
			hw_id, data_trunc->blend_data_trunc);
		return false;
	}

	return true;
}

static bool data_trunc_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_data_trunc *data_trunc = data;
	u32 config = 0;

	trace_config_hw_display_feature_en("DATA_TRUNC", hw_id, enable);

	if (enable) {
		if (hw_id <= HW_DISPLAY_1) {
			config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
								  GAMMA_DTH_CFG_Address));
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, GAMMA_DTH_CFG_Address),
				 VS_SET_FIELD(config, DCREG_SH_PANEL0_GAMMA_DTH_CFG, ROUNDING_MODE,
					      data_trunc->gamma_data_trunc));
		}

		config = dc_read(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, DTH_CFG_Address));
		dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, DTH_CFG_Address),
			 VS_SET_FIELD(config, DCREG_SH_PANEL0_DTH_CFG, ROUNDING_MODE,
				      data_trunc->panel_data_trunc));

		if (hw_id == HW_DISPLAY_4) {
			config = dc_read(hw, DCREG_SH_WB_BLEND_DTH_CONFIG_Address);
			dc_write(hw, DCREG_SH_WB_BLEND_DTH_CONFIG_Address,
				 VS_SET_FIELD(config, DCREG_SH_WB_BLEND_DTH_CONFIG, ROUNDING_MODE,
					      data_trunc->blend_data_trunc));
		} else {
			config = dc_read(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id,
								BLEND_DTH_CONFIG_Address));
			dc_write(hw,
				 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id,
						    BLEND_DTH_CONFIG_Address),
				 VS_SET_FIELD(config, DCREG_SH_PANEL0_BLEND_DTH_CONFIG,
					      ROUNDING_MODE, data_trunc->blend_data_trunc));
		}
	} else {
		if (hw_id <= HW_DISPLAY_1) {
			config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
								  GAMMA_DTH_CFG_Address));
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, GAMMA_DTH_CFG_Address),
				 VS_SET_FIELD(config, DCREG_SH_PANEL0_GAMMA_DTH_CFG, ROUNDING_MODE,
					      0x0));
		}

		config = dc_read(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, DTH_CFG_Address));
		dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, DTH_CFG_Address),
			 VS_SET_FIELD(config, DCREG_SH_PANEL0_DTH_CFG, ROUNDING_MODE, 0x0));

		if (hw_id == HW_DISPLAY_4) {
			config = dc_read(hw, DCREG_SH_WB_BLEND_DTH_CONFIG_Address);
			dc_write(hw, DCREG_SH_WB_BLEND_DTH_CONFIG_Address,
				 VS_SET_FIELD(config, DCREG_SH_WB_BLEND_DTH_CONFIG, ROUNDING_MODE,
					      0x0));
		} else {
			config = dc_read(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id,
								BLEND_DTH_CONFIG_Address));
			dc_write(hw,
				 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id,
						    BLEND_DTH_CONFIG_Address),
				 VS_SET_FIELD(config, DCREG_SH_PANEL0_BLEND_DTH_CONFIG,
					      ROUNDING_MODE, 0x0));
		}
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(data_trunc_proto, "DATA_TRUNC", struct drm_vs_data_trunc,
			  data_trunc_check, NULL, data_trunc_config_hw);

static bool display_data_extend_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
				      const void *obj_state)
{
	const struct drm_vs_data_extend *data_extend = data;

	if (data_extend->data_extend_mode == VS_DATA_EXT_RANDOM) {
		dev_err(hw->dev, "%s: display data extend doesn't support RANDOM extend mode.\n",
			__func__);
		return false;
	}
	return true;
}

static bool display_data_extend_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_data_extend *data_extend = data;
	u32 config = 0;

	trace_config_hw_display_feature_en("DATA_EXTEND", hw_id, enable);

	if (enable) {
		if (hw_id <= HW_DISPLAY_1) {
			config = dc_read(hw,
					 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address));
			dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address),
				 VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, EXTEND_BITS_MODE,
					      data_extend->data_extend_mode));
		} else if (hw_id <= HW_DISPLAY_3) {
			config = dc_read(hw,
					 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address));
			dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address),
				 VS_SET_FIELD(config, DCREG_SH_PANEL2_CONFIG, R2Y_EXTEND_BITS,
					      data_extend->data_extend_mode));
		} else {
			config = dc_read(hw, DCREG_SH_BLD_WB_R2Y_CONFIG_Address);
			dc_write(hw, DCREG_SH_BLD_WB_R2Y_CONFIG_Address,
				 VS_SET_FIELD(config, DCREG_SH_BLD_WB_R2Y_CONFIG, R2Y_EXTEND_BITS,
					      data_extend->data_extend_mode));
		}
	} else {
		if (hw_id <= HW_DISPLAY_1) {
			config = dc_read(hw,
					 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address));
			dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address),
				 VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, EXTEND_BITS_MODE,
					      0x0));
		} else if (hw_id <= HW_DISPLAY_3) {
			config = dc_read(hw,
					 VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address));
			dc_write(hw, VS_SET_PANEL_FIELD(DCREG_SH_PANEL, hw_id, CONFIG_Address),
				 VS_SET_FIELD(config, DCREG_SH_PANEL2_CONFIG, R2Y_EXTEND_BITS,
					      0x0));
		} else {
			config = dc_read(hw, DCREG_SH_BLD_WB_R2Y_CONFIG_Address);
			dc_write(hw, DCREG_SH_BLD_WB_R2Y_CONFIG_Address,
				 VS_SET_FIELD(config, DCREG_SH_BLD_WB_R2Y_CONFIG, R2Y_EXTEND_BITS,
					      0x0));
		}
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(data_extend_proto, "DATA_EXTEND", struct drm_vs_data_extend,
			  display_data_extend_check, NULL, display_data_extend_config_hw);

bool vs_dc_register_postprocess_states(struct vs_dc_property_state_group *states,
				       const struct vs_display_info *display_info)
{
	__ERR_CHECK(vs_dc_register_display_3d_lut_states(states, display_info), on_error);
	if (display_info->background)
		__ERR_CHECK(vs_dc_property_register_state(states, &bg_color_proto), on_error);
	if (display_info->gamma_dth)
		__ERR_CHECK(vs_dc_property_register_state(states, &gamma_dither_proto), on_error);
	if (display_info->panel_dth)
		__ERR_CHECK(vs_dc_property_register_state(states, &panel_dither_proto), on_error);
	if (display_info->llv_dth)
		__ERR_CHECK(vs_dc_property_register_state(states, &llv_dither_proto), on_error);
	if (display_info->sharpness)
		__ERR_CHECK(vs_dc_register_sharpness_states(states, display_info), on_error);

	if (display_info->brightness) {
		__ERR_CHECK(vs_dc_property_register_state(states, &brightness_proto), on_error);
		__ERR_CHECK(vs_dc_property_register_state(states, &brightness_roi_proto), on_error);
	}
	if (display_info->blur)
		__ERR_CHECK(vs_dc_property_register_state(states, &blur_proto), on_error);
	if (display_info->ltm || display_info->gtm)
		__ERR_CHECK(vs_dc_register_ltm_states(states, display_info), on_error);
	if (display_info->ccm_linear)
		__ERR_CHECK(vs_dc_property_register_state(states, &ccm_lnr_proto), on_error);
	if (display_info->ccm_non_linear)
		__ERR_CHECK(vs_dc_property_register_state(states, &ccm_non_lnr_proto), on_error);
	if (display_info->degamma)
		__ERR_CHECK(vs_dc_property_register_state(states, &degamma_proto), on_error);
	if (display_info->min_scale != (1 << 16) || display_info->max_scale != (1 << 16))
		__ERR_CHECK(vs_dc_register_scale_states(states, display_info), on_error);
	__ERR_CHECK(vs_dc_register_display_r2y_states(states, display_info), on_error);
	if (display_info->spliter)
		__ERR_CHECK(vs_dc_property_register_state(states, &spliter_proto), on_error);
	if (display_info->free_sync)
		__ERR_CHECK(vs_dc_property_register_state(states, &free_sync_proto), on_error);
	if (display_info->panel_crop)
		__ERR_CHECK(vs_dc_property_register_state(states, &panel_crop_proto), on_error);
	if (display_info->dp_sync)
		__ERR_CHECK(vs_dc_property_register_state(states, &dp_sync_proto), on_error);
	if (display_info->ofifo_splice)
		__ERR_CHECK(vs_dc_property_register_state(states, &splice_mode_proto), on_error);
	if (display_info->panel_dth || display_info->gamma_dth || display_info->bld_dth)
		__ERR_CHECK(vs_dc_property_register_state(states, &data_trunc_proto), on_error);
	if (display_info->data_mode)
		__ERR_CHECK(vs_dc_property_register_state(states, &data_extend_proto), on_error);
	return true;
on_error:
	return false;
}
