/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */
#include "vs_dc_scale.h"

#include "drm/vs_drm.h"
#include "vs_dc_property.h"
#include "vs_dc_hw.h"
#include "vs_dc_reg_be.h"
#include "vs_dc_reg_fe0.h"

static bool scale_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_scale_config *scale = data;
	u32 config = 0, reg_offset = 0x04, i = 0;

	config = dc_read(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, SCALE_CONTROL_Address));
	dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, SCALE_CONTROL_Address),
		 VS_SET_FIELD(config, DCREG_SH_PANEL0_SCALE_CONTROL, ENABLE, scale->enable));

	if (scale->enable) {
		/*put here temporarily,refine soon*/
		dc_write(hw, DCREG_SH_LAYER0_OUT_ROI_SIZE_Address,
			 VS_SET_FIELD(0, DCREG_SH_LAYER0_OUT_ROI_SIZE, WIDTH, scale->src_w) |
				 VS_SET_FIELD(0, DCREG_SH_LAYER0_OUT_ROI_SIZE, HEIGHT,
					      scale->src_h));

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, HSCALE_FACTOR_Address),
			 scale->factor_x);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, VSCALE_FACTOR_Address),
			 scale->factor_y);

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, SCALER_SRC_WIDTH_Address),
			 scale->src_w);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, SCALER_SRC_HEIGHT_Address),
			 scale->src_h);

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, SRC_WIDTH_Address),
			 scale->src_w);
		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, SRC_HEIGHT_Address),
			 scale->src_h);

		dc_write(hw, VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, SCALER_DEST_WIDTH_Address),
			 scale->dst_w);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id, SCALER_DEST_HEIGHT_Address),
			 scale->dst_h);

		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      SCALE_INITIAL_OFFSET_X_Address),
			 0x8000);
		dc_write(hw,
			 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
					      SCALE_INITIAL_OFFSET_Y_Address),
			 0x8000);

		for (i = 0; i < sizeof(scale->coef_h) / sizeof(u32); i++)
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      HSCALE_COEF_DATA_Address) +
					 (i * reg_offset),
				 scale->coef_h[i]);

		for (i = 0; i < sizeof(scale->coef_v) / sizeof(u32); i++)
			dc_write(hw,
				 VS_SET_PANEL01_FIELD(DCREG_SH_PANEL, hw_id,
						      VSCALE_COEF_DATA_Address) +
					 (i * reg_offset),
				 scale->coef_v[i]);
	}

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(scale_proto, "SCALER", struct drm_vs_scale_config, NULL, NULL,
			  scale_config_hw);

bool vs_dc_register_scale_states(struct vs_dc_property_state_group *states,
				 const struct vs_display_info *display_info)
{
	__ERR_CHECK(vs_dc_property_register_state(states, &scale_proto), on_error);
	return true;
on_error:
	return false;
}
