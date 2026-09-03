/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */

#include "vs_dc_plane_blender.h"

#include "drm/vs_drm.h"
#include "vs_dc_property.h"
#include "vs_dc_hw.h"
#include "vs_dc_reg_be.h"
#include "vs_dc_reg_fe0.h"
#include "vs_dc_reg_fe1.h"
#include "vs_trace.h"

static bool blend_mode_check(const struct dc_hw *hw, u8 hw_id, const void *data, u32 size,
			     const void *obj_state)
{
	const struct drm_vs_blend *bld = data;
	bool alpha_enabled = false;

	if (bld->color_mode == VS_BLD_UDEF) {
		vs_dc_hw_get_plane_property(hw, hw_id, "BLEND_ALPHA", &alpha_enabled);
		if (!alpha_enabled) {
			dev_err(hw->dev, "%s invalid color mode, alpha blend not enabled\n",
				__func__);
			return false;
		}
	}
	return true;
}

static bool blend_mode_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_blend *bld = data;
	bool alpha_enabled = false;
	u32 config = 0;

	vs_dc_hw_get_plane_property(hw, hw_id, "BLEND_ALPHA", &alpha_enabled);
	trace_config_hw_layer_feature("BLEND_MODE", hw_id, "en:%d alpha_en:%d", enable,
				      alpha_enabled);

	config = dc_read(hw, VS_SH_LAYER_FIELD(hw_id, ALPHA_BLEND_CONFIG_Address));
	config = VS_SET_FIELD(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG, ALPHA_BLEND, enable);

	if (enable) {
		trace_config_hw_layer_feature("BLEND_MODE_DATA", hw_id,
					      "color_mode:%d alpha_mode:%d", bld->color_mode,
					      bld->alpha_mode);

		if (!alpha_enabled) {
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     ALPHA_BLEND_FAST, ENABLED);
			config = VS_SET_FIELD(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
					      ALPHA_BLEND_FAST_MODE, bld->color_mode);

		} else {
			switch (bld->color_mode) {
			case VS_BLD_CLR:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, ZERO);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, ZERO);
				break;
			case VS_BLD_SRC:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, ONE);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, ZERO);
				break;
			case VS_BLD_DST:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, ZERO);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, ONE);
				break;
			case VS_BLD_SRC_OVR:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, ONE);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, INVERSED);
				break;
			case VS_BLD_DST_OVR:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, INVERSED);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, ONE);
				break;
			case VS_BLD_SRC_IN:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, NORMAL);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, ZERO);
				break;
			case VS_BLD_DST_IN:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, ZERO);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, NORMAL);
				break;
			case VS_BLD_SRC_OUT:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, INVERSED);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, ZERO);
				break;
			case VS_BLD_DST_OUT:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, ZERO);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, INVERSED);
				break;
			case VS_BLD_SRC_ATOP:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, NORMAL);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, INVERSED);
				break;
			case VS_BLD_DST_ATOP:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, INVERSED);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, NORMAL);
				break;
			case VS_BLD_XOR:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, INVERSED);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, INVERSED);
				break;
			case VS_BLD_PLUS:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, ONE);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, ONE);
				break;
			case VS_BLD_BLD:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_FACTOR_MODE, NORMAL);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, NORMAL);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, INVERSED);
				break;
			case VS_BLD_UDEF:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_COLOR_BLEND_MODE, FACTOR_MODE);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_COLOR_BLEND_MODE, FACTOR_MODE);
				break;
			default:
				break;
			}

			switch (bld->alpha_mode) {
			case VS_BLD_CLR:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, ZERO);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, ZERO);
				break;
			case VS_BLD_SRC:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, ONE);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, ZERO);
				break;
			case VS_BLD_DST:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, ZERO);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, ONE);
				break;
			case VS_BLD_SRC_OVR:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, ONE);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, INVERSED);
				break;
			case VS_BLD_DST_OVR:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, INVERSED);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, ONE);
				break;
			case VS_BLD_SRC_IN:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, NORMAL);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, ZERO);
				break;
			case VS_BLD_DST_IN:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, ZERO);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, NORMAL);
				break;
			case VS_BLD_SRC_OUT:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, INVERSED);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, ZERO);
				break;
			case VS_BLD_DST_OUT:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, ZERO);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, INVERSED);
				break;
			case VS_BLD_SRC_ATOP:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, ZERO);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, ONE);
				break;
			case VS_BLD_DST_ATOP:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, ONE);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, ZERO);
				break;
			case VS_BLD_XOR:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, INVERSED);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, INVERSED);
				break;
			case VS_BLD_PLUS:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, ONE);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, ONE);
				break;
			case VS_BLD_BLD:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_FACTOR_MODE, NORMAL);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_FACTOR_MODE, DEST);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, NORMAL);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, INVERSED);
				break;
			case VS_BLD_UDEF:
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     SRC_ALPHA_BLEND_MODE, FACTOR_MODE);
				config = VS_SET_FIELD_PREDEF(config,
							     DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
							     DST_ALPHA_BLEND_MODE, FACTOR_MODE);
				break;
			default:
				break;
			}
		}
	} else {
		if (!alpha_enabled)
			config = VS_SET_FIELD_PREDEF(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
						     ALPHA_BLEND_FAST, DISABLED);
	}

	dc_write(hw, VS_SH_LAYER_FIELD(hw_id, ALPHA_BLEND_CONFIG_Address), config);

	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(blend_mode_proto, "BLEND_MODE", struct drm_vs_blend, blend_mode_check,
			  NULL, blend_mode_config_hw);

static bool blend_alpha_config_hw(struct dc_hw *hw, u8 hw_id, bool enable, const void *data)
{
	const struct drm_vs_blend_alpha *alpha = data;
	const u32 reg_config = VS_SH_LAYER_FIELD(hw_id, ALPHA_BLEND_CONFIG_Address);
	const u32 reg_src = VS_SH_LAYER_FIELD(hw_id, SRC_ALPHA_Address);
	const u32 reg_dst = VS_SH_LAYER_FIELD(hw_id, DST_ALPHA_Address);
	const u32 reg_src_global = VS_SH_LAYER_FIELD(hw_id, SRC_GLOBAL_ALPHA_Address);
	const u32 reg_dst_global = VS_SH_LAYER_FIELD(hw_id, DST_GLOBAL_ALPHA_Address);
	u32 config = 0;

	trace_config_hw_layer_feature_en("BLEND_ALPHA", hw_id, enable);

	if (enable) {
		trace_config_hw_layer_feature(
			"BLEND_ALPHA", hw_id,
			"sam:%d sgam:%d sga:%d saa:%d dam:%d dgam:%d dga:%d daa:%d", alpha->sam,
			alpha->sgam, alpha->sga, alpha->saa, alpha->dam, alpha->dgam, alpha->dga,
			alpha->daa);

		config = dc_read(hw, reg_config);

		/* src/dst alpha mode configuration */
		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG, SRC_ALPHA_MODE,
				      alpha->sam);
		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG, DST_ALPHA_MODE,
				      alpha->dam);

		/* src/dst global alpha mode configuration */
		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
				      SRC_GLOBAL_ALPHA_MODE, alpha->sgam);
		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_ALPHA_BLEND_CONFIG,
				      DST_GLOBAL_ALPHA_MODE, alpha->dgam);

		dc_write(hw, reg_config, config);

		/* src/dst alpha value configuration */
		dc_write(hw, reg_src, alpha->saa);
		dc_write(hw, reg_dst, alpha->daa);

		/* src/dst global alpha value configuration */
		dc_write(hw, reg_src_global, alpha->sga);
		dc_write(hw, reg_dst_global, alpha->dga);

		/* TBD: src/dst color alpha registers not ready yet */
	}
	return true;
}

VS_DC_BLOB_PROPERTY_PROTO(blend_alpha_proto, "BLEND_ALPHA", struct drm_vs_blend_alpha, NULL, NULL,
			  blend_alpha_config_hw);

bool vs_dc_register_plane_blender_states(struct vs_dc_property_state_group *states,
					 const struct vs_plane_info *info)
{
	if (info->blend_config) {
		__ERR_CHECK(vs_dc_property_register_state(states, &blend_mode_proto), on_error);
		__ERR_CHECK(vs_dc_property_register_state(states, &blend_alpha_proto), on_error);
	}
	return true;
on_error:
	return false;
}
