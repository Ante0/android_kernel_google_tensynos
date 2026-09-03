// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "video_bridge.h"

void video_chess_board_config(struct dptx *dptx, int stream)
{
	struct video_params *vparams;
	struct dtd *mdtd;
	u32 cb_config = 0;

	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;

	if (vparams->pix_enc == YCBCR420)
		cb_config = set(cb_config, VPG_CB_WIDTH_MASK, (mdtd->h_active / 20));
	else
		cb_config = set(cb_config, VPG_CB_WIDTH_MASK, (mdtd->h_active / 10));

	cb_config = set(cb_config, VPG_CB_HEIGHT_MASK, (mdtd->v_active / 10));
	vg_write(dptx, VPG_CB_LENGTH_CONFIG + STREAM_OFFSET(stream), cb_config);

	// Set pattern color
	if (vparams->pix_enc == RGB) {
		// PURPLE
		vg_write(dptx, VPG_CB_COLORA_L + STREAM_OFFSET(stream), 0x2B008200);
		vg_write(dptx, VPG_CB_COLORA_H + STREAM_OFFSET(stream), 0x5B00);
		// WHITE
		vg_write(dptx, VPG_CB_COLORB_L + STREAM_OFFSET(stream), 0xEB00EB00);
		vg_write(dptx, VPG_CB_COLORB_H + STREAM_OFFSET(stream), 0xEB00);
	} else {
		// PURPLE
		vg_write(dptx, VPG_CB_COLORA_L + STREAM_OFFSET(stream), 0x9F008F00);
		vg_write(dptx, VPG_CB_COLORA_H + STREAM_OFFSET(stream), 0x4A00);
		// WHITE
		vg_write(dptx, VPG_CB_COLORB_L + STREAM_OFFSET(stream), 0x80008000);
		vg_write(dptx, VPG_CB_COLORB_H + STREAM_OFFSET(stream), 0xEB00);
	}

	dptx_dbg(dptx, "VIDEO GENERATOR - Set Chess Board Pattern\n");
}

int video_generator_config(struct dptx *dptx, int stream)
{
	uint8_t colorimetry, dtd_code, color_res, resolution;
	u32 vpg_conf0 = 0, vpg_conf1 = 0, vpg_hahb = 0, vpg_hdhw = 0,
	    vpg_vavb = 0, vpg_vdvw = 0;
	enum pixel_enc_type pix_encoding = RGB;
	enum pattern_mode pattern;
	struct video_params *vparams;
	struct dtd *mdtd;

	dptx_dbg(dptx, "VIDEO GENERATOR - Configuring for Stream %d\n", stream);
	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;
	color_res = vparams->bpc;
	pix_encoding = vparams->pix_enc;
	dtd_code = vparams->mode;
	pattern = vparams->pattern_mode;

	if (pix_encoding == YCBCR444)
		colorimetry = 2;
	else if (pix_encoding == YCBCR422)
		colorimetry = 1;
	else if (pix_encoding == YCBCR420)
		colorimetry = 3;
	else
		colorimetry = 0;

	if (color_res == COLOR_DEPTH_8) {
		resolution = 4;
	} else if (color_res == COLOR_DEPTH_10) {
		resolution = 5;
	} else if (color_res == COLOR_DEPTH_12) {
		resolution = 6;
	} else if (color_res == COLOR_DEPTH_16) {
		resolution = 7;
	} else {
		dptx_err(dptx, "%s: Invalid color depth: %d", __func__, color_res);
		return FALSE;
	}

	vpg_conf0 = set(vpg_conf0, VPG_CONF0_DE_POL_MASK, 1);
	vpg_conf0 = set(vpg_conf0, VPG_CONF0_HS_POL_MASK, mdtd->h_sync_polarity);
	vpg_conf0 = set(vpg_conf0, VPG_CONF0_VS_POL_MASK, mdtd->v_sync_polarity);
	vpg_conf0 = set(vpg_conf0, VPG_CONF0_INTERLACED_MASK, mdtd->interlaced);
	if (mdtd->interlaced && (dtd_code != 39))
		vpg_conf0 = set(vpg_conf0, VPG_CONF0_VBLANK_OSC_MASK, 1);
	vpg_conf0 = set(vpg_conf0, VPG_CONF0_COLORIMETRY_MASK, colorimetry);
	vpg_conf0 = set(vpg_conf0, VPG_CONF0_PREPETITION_MASK, mdtd->pixel_repetition_input);
	vpg_conf0 = set(vpg_conf0, VPG_CONF0_COLORDEPTH_MASK, resolution);
	vpg_conf0 = set(vpg_conf0, VPG_CONF0_COLOR_RANGE_MASK, 1);

	vpg_conf1 = set(vpg_conf1, VPG_CONF1_PATT_MODE_MASK, pattern);

	vpg_hahb = set(vpg_hahb, VPG_HAHB_HACTIVE_MASK,
		       mdtd->h_active * (mdtd->pixel_repetition_input + 1));
	vpg_hahb = set(vpg_hahb, VPG_HAHB_HBLANK_MASK,
		       mdtd->h_blanking * (mdtd->pixel_repetition_input + 1));
	vpg_hdhw = set(vpg_hdhw, VPG_HDHW_HFRONT_MASK,
		       mdtd->h_sync_offset * (mdtd->pixel_repetition_input + 1));
	vpg_hdhw = set(vpg_hdhw, VPG_HDHW_HWIDTH_MASK,
		       mdtd->h_sync_pulse_width * (mdtd->pixel_repetition_input + 1));

	if (pix_encoding == YCBCR420) {
		vpg_hahb /= 2;
		vpg_hdhw /= 2;
	}

	vpg_vavb = set(vpg_vavb, VPG_VAVB_VACTIVE_MASK, mdtd->v_active);

	vg_write(dptx, VPG_CONF0 + STREAM_OFFSET(stream), vpg_conf0);
	vg_write(dptx, VPG_CONF1 + STREAM_OFFSET(stream), vpg_conf1);
	vg_write(dptx, VPG_HAHB_CONFIG + STREAM_OFFSET(stream), vpg_hahb);
	vg_write(dptx, VPG_HDHW_CONFIG + STREAM_OFFSET(stream), vpg_hdhw);
	vg_write(dptx, VPG_VAVB_CONFIG + STREAM_OFFSET(stream), vpg_vavb);
	vg_write(dptx, VPG_VDVW_CONFIG + STREAM_OFFSET(stream), vpg_vdvw);

	if (pattern == CHESS_BOARD)
		video_chess_board_config(dptx, stream);

	vpg_conf0 = set(vpg_conf0, VPG_CONF0_ENABLE_MASK, 1);
	vg_write(dptx, VPG_CONF0 + STREAM_OFFSET(stream), vpg_conf0);

	dptx_dbg(dptx, "VIDEO GENERATOR - Successful Configuration\n");

	return TRUE;
}

static void video_generator_disable(struct dptx *dptx)
{
	uint8_t i;
	uint32_t value;

	for (i = 0; i < DPTX_MAX_STREAM_NUMBER; i++) {
		value = vg_read(dptx, VPG_CONF0 + STREAM_OFFSET(i));
		value = set(value, VPG_CONF0_ENABLE_MASK, 0);
		vg_write(dptx, VPG_CONF0 + STREAM_OFFSET(i), value);
	}
	dptx_dbg(dptx, "VIDEO GENERATOR - Disabled\n");
}
