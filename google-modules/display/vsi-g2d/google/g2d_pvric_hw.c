// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */
#include "g2d_pvric_hw.h"
#include "g2d_sc_hw.h"
#include "g2d_registers.h"

static void pvric_hw_enable_decoder(struct sc_hw *hw, u8 id, bool enable)
{
	u32 config;

	config = sc_read(hw, vsSETFIELD_FE(SCREG_LAYER, id, CONFIG_Address));
	sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, id, CONFIG_Address),
		 VS_SET_FIELD(config, SCREG_LAYER0_CONFIG, COMPRESS_ENABLE, enable));

	if (!enable)
		return;

	/* Invalidate PVRIC header cache to avoid corruption - see b/362350529 for details */
	config = VS_SET_FIELD(0, SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL, NOTIFY, 1);
	config = VS_SET_FIELD(config, SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL, INVALIDATE_ALL,
			      1);
	config = VS_SET_FIELD(config, SCREG_LAYER0_DEC_PVRIC_INVALIDATION_CONTROL,
			      INVALIDATE_OVERRIDE, 1);
	sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, id, DEC_PVRIC_INVALIDATION_CONTROL_Address),
		 config);
}

static void pvric_hw_enable_encoder(struct sc_hw *hw, u8 id, bool enable)
{
	u32 config;

	config = sc_read(hw, vsSETFIELD_FE(SCREG_LAYER, id, WB_CONFIG_Address));
	sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, id, WB_CONFIG_Address),
		 VS_SET_FIELD(config, SCREG_LAYER0_WB_CONFIG, COMPRESS_ENC, enable));
}

static void pvric_hw_configure_requester(struct sc_hw *hw, u8 id,
					 struct pvric_hw_config *pvric_hw_config, bool decoder)
{
	u32 i;
	u32 config;
	const u32 base_offset = 0x04;

	for (i = 0; i < pvric_hw_config->num_planes; i++) {
		config = VS_SET_FIELD(0, SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0,
				      ENABLE_LOSSY, !!pvric_hw_config->lossy);
		config = VS_SET_FIELD(config, SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0,
				      FORMAT, pvric_hw_config->format);
		config = VS_SET_FIELD(config, SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0, TILE,
				      pvric_hw_config->tile_mode);
		config = VS_SET_FIELD(config, SCREG_LAYER0_DEC_PVRIC_REQUESTER_CONTROL_REQT0,
				      SWIZZLE, pvric_hw_config->swizzle);

		/* Encoder and Decoder have separate but identical registers for RequesterControl */
		if (decoder)
			sc_write(hw,
				 vsSETFIELD_FE(SCREG_LAYER, id,
					       DEC_PVRIC_REQUESTER_CONTROL_REQT0_Address) +
					 i * base_offset,
				 config);
		else
			sc_write(hw,
				 vsSETFIELD_FE(SCREG_LAYER, id,
					       ENC_PVRIC_REQUESTER_CONTROL_REQT0_Address) +
					 i * base_offset,
				 config);
	}
}

void pvric_hw_configure_thresholds(struct sc_hw *hw, u8 id,
				   struct pvric_hw_thresholds *pvric_hw_thresholds)
{
	u32 config;

	/* ARGB10 & Alpha Thresholds */
	config = VS_SET_FIELD(0, SCREG_LAYER0_ENC_PVRIC_THRESHOLD0, THRESHOLD_ARGB10,
			      pvric_hw_thresholds->argb10);
	config = VS_SET_FIELD(config, SCREG_LAYER0_ENC_PVRIC_THRESHOLD0, THRESHOLD_ALPHA,
			      pvric_hw_thresholds->alpha);
	sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, id, ENC_PVRIC_THRESHOLD0_Address), config);

	/* YUV Thresholds */
	config = VS_SET_FIELD(0, SCREG_LAYER0_ENC_PVRIC_THRESHOLD1, THRESHOLD_YUV8,
			      pvric_hw_thresholds->yuv8);
	config = VS_SET_FIELD(config, SCREG_LAYER0_ENC_PVRIC_THRESHOLD1, YUV10_P10,
			      pvric_hw_thresholds->yuv10_p10);
	config = VS_SET_FIELD(config, SCREG_LAYER0_ENC_PVRIC_THRESHOLD1, YUV10_P16,
			      pvric_hw_thresholds->yuv10_p16);
	sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, id, ENC_PVRIC_THRESHOLD1_Address), config);

	/* Color Diff Thresholds */
	config = VS_SET_FIELD(0, SCREG_LAYER0_ENC_PVRIC_THRESHOLD2, COLOR_DIFF8,
			      pvric_hw_thresholds->color_diff8);
	config = VS_SET_FIELD(config, SCREG_LAYER0_ENC_PVRIC_THRESHOLD2, COLOR_DIFF10,
			      pvric_hw_thresholds->color_diff10);
	sc_write(hw, vsSETFIELD_FE(SCREG_LAYER, id, ENC_PVRIC_THRESHOLD2_Address), config);
}

void pvric_hw_configure_const_color(struct sc_hw *hw, u8 id,
				    struct pvric_hw_const_color *pvric_hw_const_color, bool decoder)
{
	u32 config;
	u32 codec_offset;

	/* Encoder and Decoder have separate but identical registers for ConstColor */
	if (decoder)
		codec_offset = 0x0;
	else
		codec_offset = SCREG_LAYER0_ENC_PVRIC_CONST_COLOR_CONFIG0_REQT_Address -
			       SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_Address;

	/* Const Color Non-YUV 0 */
	config = VS_SET_FIELD(0, SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG0_REQT, VALUE,
			      pvric_hw_const_color->ch0123_0);
	sc_write(hw,
		 vsSETFIELD_FE(SCREG_LAYER, id, DEC_PVRIC_CONST_COLOR_CONFIG0_REQT_Address) +
			 codec_offset,
		 config);

	/* Const Color Non-YUV 1 */
	config = VS_SET_FIELD(0, SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG1_REQT, VALUE,
			      pvric_hw_const_color->ch0123_1);
	sc_write(hw,
		 vsSETFIELD_FE(SCREG_LAYER, id, DEC_PVRIC_CONST_COLOR_CONFIG1_REQT_Address) +
			 codec_offset,
		 config);

	/* Const Color YUV 0 */
	config = VS_SET_FIELD(0, SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT, VALUE_Y,
			      pvric_hw_const_color->y_0);
	config = VS_SET_FIELD(config, SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG2_REQT, VALUE_UV,
			      pvric_hw_const_color->uv_0);
	sc_write(hw,
		 vsSETFIELD_FE(SCREG_LAYER, id, DEC_PVRIC_CONST_COLOR_CONFIG2_REQT_Address) +
			 codec_offset,
		 config);

	/* Const Color YUV 1 */
	config = VS_SET_FIELD(0, SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT, VALUE_Y,
			      pvric_hw_const_color->y_1);
	config = VS_SET_FIELD(config, SCREG_LAYER0_DEC_PVRIC_CONST_COLOR_CONFIG3_REQT, VALUE_UV,
			      pvric_hw_const_color->uv_1);
	sc_write(hw,
		 vsSETFIELD_FE(SCREG_LAYER, id, DEC_PVRIC_CONST_COLOR_CONFIG3_REQT_Address) +
			 codec_offset,
		 config);
}

void pvric_hw_update_plane(struct sc_hw *hw, u8 id, struct pvric_hw_config *pvric_conf)
{
	struct sc_hw_plane *plane = &hw->plane[id];

	if (!pvric_conf || !pvric_conf->enable) {
		plane->pvric.enable = false;
		return;
	}

	memcpy(&plane->pvric, pvric_conf, sizeof(*pvric_conf));
}

void pvric_hw_update_wb(struct sc_hw *hw, u8 id, struct pvric_hw_config *pvric_conf)
{
	struct sc_hw_wb *wb = &hw->wb[id];

	if (!pvric_conf || !pvric_conf->enable) {
		wb->pvric.enable = false;
		return;
	}

	memcpy(&wb->pvric, pvric_conf, sizeof(*pvric_conf));
}

void pvric_hw_plane_commit(struct sc_hw *hw, u8 id)
{
	struct pvric_hw_config *config = &hw->plane[id].pvric;

	pvric_hw_enable_decoder(hw, id, config->enable);
	pvric_hw_configure_requester(hw, id, config, true /* for the decoder */);
}

void pvric_hw_wb_commit(struct sc_hw *hw, u8 id)
{
	struct pvric_hw_config *config = &hw->wb[id].pvric;

	pvric_hw_enable_encoder(hw, id, config->enable);
	pvric_hw_configure_requester(hw, id, config, false /* for the encoder */);
}
