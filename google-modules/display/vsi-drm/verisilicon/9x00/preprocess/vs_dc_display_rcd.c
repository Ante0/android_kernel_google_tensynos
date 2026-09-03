// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */

#include "vs_dc_display_rcd.h"

#include "drm/vs_drm.h"
#include "vs_dc_hw.h"
#include "vs_dc_property.h"
#include "vs_dc_reg_be.h"
#include "vs_trace.h"

#include <drm/drm_print.h>

void rcd_fb_config_hw(struct dc_hw *hw, struct dc_hw_rcd_mask *rcd_mask)
{
	u8 display_id = rcd_mask->display_id;

	if (rcd_mask->enable) {
		trace_config_hw_display_feature("RCD FB", display_id,
						"en:%d addr:%#llx stride:%d mask_type:%d",
						rcd_mask->enable, rcd_mask->address,
						rcd_mask->stride, rcd_mask->type);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id,
					      RCD_MASK_DATA_ADDRESS_Address),
			 (u32)(rcd_mask->address & 0xFFFFFFFF));

		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id,
					      RCD_MASK_DATA_HIGH_ADDRESS_Address),
			 rcd_mask->address >> 32);

		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id,
					      RCD_MASK_DATA_STRIDE_Address),
			 rcd_mask->stride);
	}
}

void rcd_enable(struct dc_hw *hw, struct dc_hw_rcd_mask *rcd_mask)
{
	u8 display_id = rcd_mask->display_id;
	const u32 reg_config = VS_SH_PANEL_FIELD(display_id, CONFIG_Address);
	u32 config = 0;

	trace_config_hw_display_feature_en("RCD ENABLE", display_id, rcd_mask->enable);
	if (rcd_mask->enable) {
		config = dc_read(hw, reg_config);
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, RCD, rcd_mask->enable) |
			 VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, RCD_INVERSE, rcd_mask->type);
		dc_write(hw, reg_config, config);
	} else {
		config = dc_read(hw, reg_config);
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_CONFIG, RCD, 0x0);
		dc_write(hw, reg_config, config);
	}
}

static void rcd_roi_enable_roi_top(struct dc_hw *hw, u8 display_id,
				   const struct drm_vs_rcd_roi *rcd_roi)
{
	u32 roi_values = 0x0;

	trace_config_hw_display_feature("RCD_ROI_TOP", display_id,
					"top_roi en:%d [x %d y %d w %d h %d]", rcd_roi->top_enable,
					rcd_roi->top_roi.x, rcd_roi->top_roi.y, rcd_roi->top_roi.w,
					rcd_roi->top_roi.h);

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id, RCD_ROI0_VALID_Address),
		 rcd_roi->top_enable & DCREG_SH_PANEL0_RCD_ROI0_VALID_WriteMask);

	/* commit RCD ROI x/y/w/h */
	roi_values = VS_SET_FIELD(0, DCREG_SH_PANEL0_RCD_ROI0, ORIGIN_X, rcd_roi->top_roi.x);
	roi_values |= VS_SET_FIELD(0, DCREG_SH_PANEL0_RCD_ROI0, ORIGIN_Y, rcd_roi->top_roi.y);

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id, RCD_ROI0_ORIGIN_Address),
		 roi_values);

	roi_values = VS_SET_FIELD(0, DCREG_SH_PANEL0_RCD_ROI0, SIZE_WIDTH, (rcd_roi->top_roi.w));
	roi_values |= VS_SET_FIELD(0, DCREG_SH_PANEL0_RCD_ROI0, SIZE_HEIGHT, (rcd_roi->top_roi.h));

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id, RCD_ROI0_SIZE_Address),
		 roi_values);
}

static void rcd_roi_disable_roi_top(struct dc_hw *hw, u8 display_id)
{
	trace_config_hw_display_feature_en("RCD_ROI_TOP", display_id, false);

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id, RCD_ROI0_VALID_Address),
		 DCREG_SH_PANEL0_RCD_ROI0_VALID_ResetValue);

	/* commit RCD ROI x/y/w/h */
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id, RCD_ROI0_ORIGIN_Address),
		 DCREG_SH_PANEL0_RCD_ROI0_ORIGIN_ResetValue);

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id, RCD_ROI0_SIZE_Address),
		 DCREG_SH_PANEL0_RCD_ROI0_SIZE_ResetValue);
}

static void rcd_roi_enable_roi_btm(struct dc_hw *hw, u8 display_id,
				   const struct drm_vs_rcd_roi *rcd_roi)
{
	u32 roi_values = 0x0;

	trace_config_hw_display_feature("RCD_ROI_BTM", display_id,
					"btm_roi en:%d [x %d y %d w %d h %d]", rcd_roi->btm_enable,
					rcd_roi->btm_roi.x, rcd_roi->btm_roi.y, rcd_roi->btm_roi.w,
					rcd_roi->btm_roi.h);

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id, RCD_ROI1_VALID_Address),
		 VS_SET_FIELD(0x0, DCREG_SH_PANEL0_RCD_ROI1_VALID, VALID, rcd_roi->btm_enable) &
			 DCREG_SH_PANEL0_RCD_ROI1_VALID_WriteMask);

	/* commit RCD ROI x/y/w/h */
	roi_values = VS_SET_FIELD(0, DCREG_SH_PANEL0_RCD_ROI0, ORIGIN_X, rcd_roi->btm_roi.x);
	roi_values |= VS_SET_FIELD(0, DCREG_SH_PANEL0_RCD_ROI0, ORIGIN_Y, rcd_roi->btm_roi.y);

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id, RCD_ROI0_ORIGIN_Address),
		 roi_values);

	roi_values = VS_SET_FIELD(0, DCREG_SH_PANEL0_RCD_ROI0, SIZE_WIDTH, (rcd_roi->btm_roi.w));
	roi_values |= VS_SET_FIELD(0, DCREG_SH_PANEL0_RCD_ROI0, SIZE_HEIGHT, (rcd_roi->btm_roi.h));

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id, RCD_ROI0_SIZE_Address),
		 roi_values);
}

static void rcd_roi_disable_roi_btm(struct dc_hw *hw, u8 display_id)
{
	trace_config_hw_display_feature_en("RCD_ROI_BTM", display_id, false);

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id, RCD_ROI1_VALID_Address),
		 DCREG_SH_PANEL0_RCD_ROI1_VALID_ResetValue);

	/* commit RCD ROI x/y/w/h */
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id, RCD_ROI1_ORIGIN_Address),
		 DCREG_SH_PANEL0_RCD_ROI1_ORIGIN_ResetValue);

	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, display_id, RCD_ROI1_SIZE_Address),
		 DCREG_SH_PANEL0_RCD_ROI1_SIZE_ResetValue);
}

void rcd_roi_config_hw(struct dc_hw *hw, struct dc_hw_rcd_mask *rcd_mask)
{
	const struct drm_vs_rcd_roi *rcd_roi = &rcd_mask->roi_data;
	u8 display_id = rcd_mask->display_id;

	if (rcd_mask->enable && rcd_roi->top_enable)
		rcd_roi_enable_roi_top(hw, display_id, rcd_roi);
	else
		rcd_roi_disable_roi_top(hw, display_id);

	if (rcd_mask->enable && rcd_roi->btm_enable)
		rcd_roi_enable_roi_btm(hw, display_id, rcd_roi);
	else
		rcd_roi_disable_roi_btm(hw, display_id);
}
