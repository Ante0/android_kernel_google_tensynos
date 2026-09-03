// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */
#define DEBUG

#include <drm/drm_modeset_lock.h>

#include "dptx.h"
#include "clock_mng.h"
#include "video_bridge.h"
#include "api/api.h"
#include "regmaps/ctrl_fields.h"
#include "intr.h"

static int dptx_read_edid(struct dptx *dptx);

static int handle_test_link_training(struct dptx *dptx)
{
	int retval;
	u8 lanes;
	u8 rate;
	struct video_params *vparams;
	struct dtd *mdtd;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	dptx_enable_ssc(dptx);

	/* Move to P0 */
	dptx_write_regfield(dptx, ctrl_fields->field_phy_powerdown, 0);

	retval = dptx_read_dpcd(dptx, DP_TEST_LINK_RATE, &rate);
	if (retval)
		return retval;

	retval = dptx_bw_to_phy_rate(rate);
	if (retval < 0)
		return retval;

	rate = retval;

	retval = dptx_read_dpcd(dptx, DP_TEST_LANE_COUNT, &lanes);
	if (retval)
		return retval;

	dptx_dbg(dptx, "%s: Strating link training rate=%d, lanes=%d\n",
		 __func__, rate, lanes);

	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;

	retval = dptx_video_ts_calculate(dptx, lanes, rate,
					 vparams->bpc, vparams->pix_enc,
					 mdtd->pixel_clock);
	if (retval)
		return retval;

	retval = dptx_set_link_configs(dptx, rate, lanes);
	retval = dptx_link_training(dptx);
	if (retval)
		dptx_err(dptx, "Link training failed %d\n", retval);
	else
		dptx_dbg(dptx, "Link training succeeded\n");

	return retval;
}

static int handle_test_link_video_timming(struct dptx *dptx, int stream)
{
	int retval, i;
	u8 test_h_total_lsb, test_h_total_msb, test_v_total_lsb,
	   test_v_total_msb, test_h_start_lsb, test_h_start_msb,
	   test_v_start_lsb, test_v_start_msb, test_hsync_width_lsb,
	   test_hsync_width_msb, test_vsync_width_lsb, test_vsync_width_msb,
	   test_h_width_lsb, test_h_width_msb, test_v_width_lsb,
	   test_v_width_msb;
	u32 h_total, v_total, h_start, v_start, h_width, v_width,
	    hsync_width, vsync_width, h_sync_pol, v_sync_pol, refresh_rate;
	enum video_format_type video_format;
	u8 vmode;
	u8 test_refresh_rate;
	struct video_params *vparams;
	struct dtd mdtd;

	vparams = &dptx->vparams;
	retval = 0;
	h_total = 0;
	v_total = 0;
	h_start = 0;
	v_start = 0;
	v_width = 0;
	h_width = 0;
	hsync_width = 0;
	vsync_width = 0;
	h_sync_pol = 0;
	v_sync_pol = 0;
	test_refresh_rate = 0;
	i = 0;

	/* H_TOTAL */
	retval = dptx_read_dpcd(dptx, DP_TEST_H_TOTAL_LSB, &test_h_total_lsb);
	if (retval)
		return retval;
	retval = dptx_read_dpcd(dptx, DP_TEST_H_TOTAL_MSB, &test_h_total_msb);
	if (retval)
		return retval;
	h_total |= test_h_total_lsb;
	h_total |= test_h_total_msb << 8;
	dptx_dbg(dptx, "h_total = %d\n", h_total);

	/* V_TOTAL */
	retval = dptx_read_dpcd(dptx, DP_TEST_V_TOTAL_LSB, &test_v_total_lsb);
	if (retval)
		return retval;
	retval = dptx_read_dpcd(dptx, DP_TEST_V_TOTAL_MSB, &test_v_total_msb);
	if (retval)
		return retval;
	v_total |= test_v_total_lsb;
	v_total |= test_v_total_msb << 8;
	dptx_dbg(dptx, "v_total = %d\n", v_total);

	/*  H_START */
	retval = dptx_read_dpcd(dptx, DP_TEST_H_START_LSB, &test_h_start_lsb);
	if (retval)
		return retval;
	retval = dptx_read_dpcd(dptx, DP_TEST_H_START_MSB, &test_h_start_msb);
	if (retval)
		return retval;
	h_start |= test_h_start_lsb;
	h_start |= test_h_start_msb << 8;
	dptx_dbg(dptx, "h_start = %d\n", h_start);

	/* V_START */
	retval = dptx_read_dpcd(dptx, DP_TEST_V_START_LSB, &test_v_start_lsb);
	if (retval)
		return retval;
	retval = dptx_read_dpcd(dptx, DP_TEST_V_START_MSB, &test_v_start_msb);
	if (retval)
		return retval;
	v_start |= test_v_start_lsb;
	v_start |= test_v_start_msb << 8;
	dptx_dbg(dptx, "v_start = %d\n", v_start);

	/* TEST_HSYNC */
	retval = dptx_read_dpcd(dptx, DP_TEST_H_SYNC_WIDTH_LSB,
				&test_hsync_width_lsb);
	if (retval)
		return retval;
	retval = dptx_read_dpcd(dptx, DP_TEST_H_SYNC_WIDTH_MSB,
				&test_hsync_width_msb);
	if (retval)
		return retval;
	hsync_width |= test_hsync_width_lsb;
	hsync_width |= (test_hsync_width_msb & (~(1 << 7))) << 8;
	h_sync_pol |= (test_hsync_width_msb & (1 << 7)) >> 8;
	dptx_dbg(dptx, "hsync_width = %d\n", hsync_width);
	dptx_dbg(dptx, "h_sync_pol = %d\n", h_sync_pol);

	/* TEST_VSYNC */
	retval = dptx_read_dpcd(dptx, DP_TEST_V_SYNC_WIDTH_LSB,
				&test_vsync_width_lsb);
	if (retval)
		return retval;
	retval = dptx_read_dpcd(dptx, DP_TEST_V_SYNC_WIDTH_MSB,
				&test_vsync_width_msb);
	if (retval)
		return retval;
	vsync_width |= test_vsync_width_lsb;
	vsync_width |= (test_vsync_width_msb & (~(1 << 7))) << 8;
	v_sync_pol |= (test_vsync_width_msb & (1 << 7)) >> 8;
	dptx_dbg(dptx, "vsync_width = %d\n", vsync_width);
	dptx_dbg(dptx, "v_sync_pol = %d\n", v_sync_pol);

	/* TEST_H_WIDTH */
	retval = dptx_read_dpcd(dptx, DP_TEST_H_WIDTH_LSB, &test_h_width_lsb);
	if (retval)
		return retval;
	retval = dptx_read_dpcd(dptx, DP_TEST_H_WIDTH_MSB, &test_h_width_msb);
	if (retval)
		return retval;
	h_width |= test_h_width_lsb;
	h_width |= test_h_width_msb << 8;
	dptx_dbg(dptx, "h_width = %d\n", h_width);

	/* TEST_V_WIDTH */
	retval = dptx_read_dpcd(dptx, DP_TEST_V_WIDTH_LSB, &test_v_width_lsb);
	if (retval)
		return retval;
	retval = dptx_read_dpcd(dptx, DP_TEST_V_WIDTH_MSB, &test_v_width_msb);
	if (retval)
		return retval;
	v_width |= test_v_width_lsb;
	v_width |= test_v_width_msb << 8;
	dptx_dbg(dptx, "v_width = %d\n", v_width);

	retval = dptx_read_dpcd(dptx, 0x234, &test_refresh_rate);
	if (retval)
		return retval;
	dptx_dbg(dptx, "test_refresh_rate = %d\n", test_refresh_rate);

	video_format = DMT;
	refresh_rate =  test_refresh_rate * 1000;

	if (h_total == 1056 && v_total == 628 && h_start == 216 &&
	    v_start == 27 && hsync_width == 128 && vsync_width == 4 &&
	    h_width == 800 && v_width == 600) {
		vmode = 9;
	} else if (h_total == 1088 && v_total == 517 && h_start == 224 &&
		   v_start == 31 && hsync_width == 112 && vsync_width == 8 &&
		   h_width == 848 && v_width == 480) {
		vmode = 14;
	} else if (h_total == 1344 && v_total == 806 && h_start == 296 &&
		   v_start == 35 && hsync_width == 136 && vsync_width == 6 &&
		   h_width == 1024 && v_width == 768) {
		vmode = 16;
	} else if (h_total == 1440 && v_total == 790 && h_start == 112 &&
		   v_start == 19 && hsync_width == 32 && vsync_width == 7 &&
		   h_width == 1280 && v_width == 768) {
		vmode = 22;
	} else if (h_total == 1664 && v_total == 798 && h_start == 320 &&
		   v_start == 27 && hsync_width == 128 && vsync_width == 7 &&
		   h_width == 1280 && v_width == 768) {
		vmode = 23;
	} else if (h_total == 1440 && v_total == 823 && h_start == 112 &&
		   v_start == 20 && hsync_width == 32 && vsync_width == 6 &&
		   h_width == 1280 && v_width == 800) {
		vmode = 27;
	} else if (h_total == 1800 && v_total == 1000 && h_start == 424 &&
		   v_start == 39 && hsync_width == 112 && vsync_width == 3 &&
		   h_width == 1280 && v_width == 960) {
		vmode = 32;
	} else if (h_total == 1688 && v_total == 1066 && h_start == 360 &&
		   v_start == 41 && hsync_width == 112 && vsync_width == 3 &&
		   h_width == 1280 && v_width  == 1024) {
		vmode = 35;
	} else if (h_total == 1792 && v_total == 795 && h_start == 368 &&
		   v_start == 24 && hsync_width == 112 && vsync_width == 6 &&
		   h_width == 1360 && v_width == 768) {
		vmode = 39;
	} else if (h_total == 1560 && v_total == 1080  && h_start == 112  &&
		   v_start == 27 && hsync_width == 32 && vsync_width == 4 &&
		   h_width == 1400 && v_width == 1050) {
		vmode = 41;
	} else if (h_total == 2160 && v_total == 1250 && h_start == 496 &&
		   v_start == 49 && hsync_width == 192 && vsync_width == 3 &&
		   h_width == 1600 && v_width == 1200) {
		vmode = 51;
	} else if (h_total == 2448 && v_total == 1394 && h_start == 528 &&
		   v_start == 49 && hsync_width == 200 && vsync_width == 3 &&
		   h_width == 1792 && v_width == 1344) {
		vmode = 62;
	} else if (h_total == 2600 && v_total == 1500 && h_start == 552 &&
		   v_start == 59  && hsync_width == 208 && vsync_width == 3 &&
		   h_width == 1920 && v_width == 1440) {
		vmode = 73;
	} else if (h_total == 2200 && v_total == 1125 && h_start == 192 &&
		   v_start == 41 && hsync_width == 44 && vsync_width == 5 &&
		   h_width == 1920 && v_width == 1080) {
		if (refresh_rate == 120000) {
			vmode = 63;
			video_format = VCEA;
		} else {
			vmode = 82;
		}
	} else if (h_total == 800 && v_total == 525 && h_start == 144 &&
		   v_start == 35 && hsync_width == 96 && vsync_width == 2 &&
		   h_width == 640 && v_width == 480) {
		vmode = 1;
		video_format = VCEA;
	} else if (h_total == 1650 && v_total == 750 && h_start == 260 &&
		   v_start == 25 && hsync_width == 40 && vsync_width == 5 &&
		   h_width == 1280  && v_width == 720) {
		vmode = 4;
		video_format = VCEA;
	} else if (h_total == 1680 && v_total == 831 && h_start == 328 &&
		   v_start == 28 && hsync_width == 128 && vsync_width == 6 &&
		   h_width == 1280 && v_width == 800) {
		vmode = 28;
		video_format = CVT;
	} else if (h_total == 1760 && v_total == 1235 && h_start == 112 &&
		   v_start == 32 && hsync_width == 32 && vsync_width == 4 &&
		   h_width == 1600 && v_width == 1200) {
		vmode = 40;
		video_format = CVT;
	} else if (h_total == 2208 && v_total == 1580 && h_start == 112 &&
		   v_start == 41 && hsync_width == 32 &&  vsync_width == 4 &&
		   h_width == 2048 && v_width == 1536) {
		vmode = 41;
		video_format = CVT;
	} else {
		dptx_dbg(dptx, "Unknown video mode\n");
		return -EINVAL;
	}

	if (!dptx_dtd_fill(&mdtd, vmode, refresh_rate, video_format)) {
		dptx_dbg(dptx, "%s: Invalid video mode value %d\n",
			 __func__, vmode);
		retval = -EINVAL;
		goto fail;
	}
	vparams->mdtd = mdtd;
	vparams->refresh_rate = refresh_rate;
	retval = dptx_video_ts_calculate(dptx, dptx->link.lanes,
					 dptx->link.rate, vparams->bpc,
					 vparams->pix_enc, mdtd.pixel_clock);
	if (retval)
		return retval;
	/* MMCM */
	dptx_video_reset(dptx, 1, stream);
	retval = 0; //TODO: configure_video_mmcm(dptx, mdtd.pixel_clock);
	if (retval) {
		dptx_video_reset(dptx, 0, stream);
		goto fail;
	}
	dptx_video_reset(dptx, 0, stream);

	vparams->mode = vmode;
	vparams->video_format = video_format;
	dptx_video_timing_change(dptx, stream);
fail:
	return retval;
}

static int handle_test_link_audio_pattern(struct dptx *dptx)
{
	int retval;
	u8 test_audio_mode, test_audio_smaple_range, test_audio_ch_count,
	   audio_ch_count, orig_sample_freq, sample_freq;
	u32 audio_clock_freq;
	struct audio_params *aparams;

	aparams = &dptx->aparams;
	retval = dptx_read_dpcd(dptx, DP_TEST_AUDIO_MODE, &test_audio_mode);
	if (retval)
		return retval;

	dptx_dbg(dptx, "test_audio_mode = %d\n", test_audio_mode);

	test_audio_smaple_range = test_audio_mode &
		DP_TEST_AUDIO_SAMPLING_RATE_MASK;
	test_audio_ch_count = (test_audio_mode & DP_TEST_AUDIO_CH_COUNT_MASK)
		>> DP_TEST_AUDIO_CH_COUNT_SHIFT;

	switch (test_audio_ch_count) {
	case DP_TEST_AUDIO_CHANNEL1:
		dptx_dbg(dptx, "DP_TEST_AUDIO_CHANNEL1\n");
		audio_ch_count = 1;
		break;
	case DP_TEST_AUDIO_CHANNEL2:
		dptx_dbg(dptx, "DP_TEST_AUDIO_CHANNEL2\n");
		audio_ch_count = 2;
		break;
	case DP_TEST_AUDIO_CHANNEL3:
		dptx_dbg(dptx, "DP_TEST_AUDIO_CHANNEL3\n");
		audio_ch_count = 3;
		break;
	case DP_TEST_AUDIO_CHANNEL4:
		dptx_dbg(dptx, "DP_TEST_AUDIO_CHANNEL4\n");
		audio_ch_count = 4;
		break;
	case DP_TEST_AUDIO_CHANNEL5:
		dptx_dbg(dptx, "DP_TEST_AUDIO_CHANNEL5\n");
		audio_ch_count = 5;
		break;
	case DP_TEST_AUDIO_CHANNEL6:
		dptx_dbg(dptx, "DP_TEST_AUDIO_CHANNEL6\n");
		audio_ch_count = 6;
		break;
	case DP_TEST_AUDIO_CHANNEL7:
		dptx_dbg(dptx, "DP_TEST_AUDIO_CHANNEL7\n");
		audio_ch_count = 7;
		break;
	case DP_TEST_AUDIO_CHANNEL8:
		dptx_dbg(dptx, "DP_TEST_AUDIO_CHANNEL8\n");
		audio_ch_count = 8;
		break;
	default:
		dptx_dbg(dptx, "Invalid TEST_AUDIO_CHANNEL_COUNT\n");
		return -EINVAL;
	}
	dptx_dbg(dptx, "test_audio_ch_count = %d\n", audio_ch_count);
	aparams->num_channels = audio_ch_count;

	switch (test_audio_smaple_range) {
	case DP_TEST_AUDIO_SAMPLING_RATE_32:
		dptx_dbg(dptx, "DP_TEST_AUDIO_SAMPLING_RATE_32\n");
		orig_sample_freq = 12;
		sample_freq = 3;
		audio_clock_freq = 32000;
		break;
	case DP_TEST_AUDIO_SAMPLING_RATE_44_1:
		dptx_dbg(dptx, "DP_TEST_AUDIO_SAMPLING_RATE_44_1\n");
		orig_sample_freq = 15;
		sample_freq = 0;
		audio_clock_freq = 44100;
		break;
	case DP_TEST_AUDIO_SAMPLING_RATE_48:
		dptx_dbg(dptx, "DP_TEST_AUDIO_SAMPLING_RATE_48\n");
		orig_sample_freq = 13;
		sample_freq = 2;
		audio_clock_freq = 48000;
		break;
	case DP_TEST_AUDIO_SAMPLING_RATE_88_2:
		dptx_dbg(dptx, "DP_TEST_AUDIO_SAMPLING_RATE_88_2\n");
		orig_sample_freq = 7;
		sample_freq = 8;
		audio_clock_freq = 88200;
		break;
	case DP_TEST_AUDIO_SAMPLING_RATE_96:
		dptx_dbg(dptx, "DP_TEST_AUDIO_SAMPLING_RATE_96\n");
		orig_sample_freq = 5;
		sample_freq = 10;
		audio_clock_freq = 96000;
		break;
	case DP_TEST_AUDIO_SAMPLING_RATE_176_4:
		dptx_dbg(dptx, "DP_TEST_AUDIO_SAMPLING_RATE_176_4\n");
		orig_sample_freq = 3;
		sample_freq = 12;
		audio_clock_freq = 176400;
		break;
	case DP_TEST_AUDIO_SAMPLING_RATE_192:
		dptx_dbg(dptx, "DP_TEST_AUDIO_SAMPLING_RATE_192\n");
		orig_sample_freq = 1;
		sample_freq = 14;
		audio_clock_freq = 192000;
		break;
	default:
		dptx_dbg(dptx, "Invalid TEST_AUDIO_SAMPLING_RATE\n");
		return -EINVAL;
	}
	dptx_dbg(dptx, "sample_freq = %d\n", sample_freq);
	dptx_dbg(dptx, "orig_sample_freq = %d\n", orig_sample_freq);

	retval = 0; //TODO: configure_audio_mmcm(dptx, audio_clock_freq);
	if (retval)
		return retval;

	aparams->iec_samp_freq = sample_freq;
	aparams->iec_orig_samp_freq = orig_sample_freq;

	dptx_audio_num_ch_change(dptx);
	dptx_audio_samp_freq_config(dptx);
	dptx_audio_infoframe_sdp_en(dptx, 1);

	return retval;
}

static int handle_test_link_video_pattern(struct dptx *dptx, int stream)
{
	int retval;
	u8 misc, pattern, bpc, bpc_map, dynamic_range,
	   dynamic_range_map, color_format, color_format_map,
	   ycbcr_coeff,  ycbcr_coeff_map;
	struct video_params *vparams;
	struct dtd *mdtd;

	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;
	retval = 0;

	retval = dptx_read_dpcd(dptx, DP_TEST_PATTERN, &pattern);
	if (retval)
		return retval;
	retval = dptx_read_dpcd(dptx, DP_TEST_MISC, &misc);
	if (retval)
		return retval;

	dynamic_range = (misc & DP_TEST_DYNAMIC_RANGE_MASK)
			>> DP_TEST_DYNAMIC_RANGE_SHIFT;
	switch (dynamic_range) {
	case DP_TEST_DYNAMIC_RANGE_VESA:
		dptx_dbg(dptx, "DP_TEST_DYNAMIC_RANGE_VESA\n");
		dynamic_range_map = VESA;
		break;
	case DP_TEST_DYNAMIC_RANGE_CEA:
		dptx_dbg(dptx, "DP_TEST_DYNAMIC_RANGE_CEA\n");
		dynamic_range_map = CEA;
		break;
	default:
		dptx_dbg(dptx, "Invalid TEST_BIT_DEPTH\n");
		return -EINVAL;
	}

	ycbcr_coeff = (misc & DP_TEST_YCBCR_COEFF_MASK)
			>> DP_TEST_YCBCR_COEFF_SHIFT;

	switch (ycbcr_coeff) {
	case DP_TEST_YCBCR_COEFF_ITU601:
		dptx_dbg(dptx, "DP_TEST_YCBCR_COEFF_ITU601\n");
		ycbcr_coeff_map = ITU601;
		break;
	case DP_TEST_YCBCR_COEFF_ITU709:
		dptx_dbg(dptx, "DP_TEST_YCBCR_COEFF_ITU709:\n");
		ycbcr_coeff_map = ITU709;
		break;
	default:
		dptx_dbg(dptx, "Invalid TEST_BIT_DEPTH\n");
		return -EINVAL;
	}
	color_format = misc & DP_TEST_COLOR_FORMAT_MASK;

	switch (color_format) {
	case DP_TEST_COLOR_FORMAT_RGB:
		dptx_dbg(dptx, "DP_TEST_COLOR_FORMAT_RGB\n");
		color_format_map = RGB;
		break;
	case DP_TEST_COLOR_FORMAT_YCBCR422:
		dptx_dbg(dptx, "DP_TEST_COLOR_FORMAT_YCBCR422\n");
		color_format_map = YCBCR422;
		break;
	case DP_TEST_COLOR_FORMAT_YCBCR444:
		dptx_dbg(dptx, "DP_TEST_COLOR_FORMAT_YCBCR444\n");
		color_format_map = YCBCR444;
		break;
	default:
		dptx_dbg(dptx, "Invalid  DP_TEST_COLOR_FORMAT\n");
		return -EINVAL;
	}

	bpc = (misc & DP_TEST_BIT_DEPTH_MASK)
		>> DP_TEST_BIT_DEPTH_SHIFT;

	switch (bpc) {
	case DP_TEST_BIT_DEPTH_6:
		bpc_map = COLOR_DEPTH_6;
		dptx_dbg(dptx, "TEST_BIT_DEPTH_6\n");
		break;
	case DP_TEST_BIT_DEPTH_8:
		bpc_map = COLOR_DEPTH_8;
		dptx_dbg(dptx, "TEST_BIT_DEPTH_8\n");
		break;
	case DP_TEST_BIT_DEPTH_10:
		bpc_map = COLOR_DEPTH_10;
		dptx_dbg(dptx, "TEST_BIT_DEPTH_10\n");
		break;
	case DP_TEST_BIT_DEPTH_12:
		bpc_map = COLOR_DEPTH_12;
		dptx_dbg(dptx, "TEST_BIT_DEPTH_12\n");
		break;
	case DP_TEST_BIT_DEPTH_16:
		bpc_map = COLOR_DEPTH_16;
		dptx_dbg(dptx, "TEST_BIT_DEPTH_16\n");
		break;
	default:
		dptx_dbg(dptx, "Invalid TEST_BIT_DEPTH\n");
		return -EINVAL;
	}

	vparams->dynamic_range = dynamic_range_map;
	dptx_dbg(dptx, "Change video dynamic range to %d\n", dynamic_range_map);

	vparams->colorimetry = ycbcr_coeff_map;
	dptx_dbg(dptx, "Change video colorimetry to %d\n", ycbcr_coeff_map);

	retval = dptx_video_ts_calculate(dptx, dptx->link.lanes,
					 dptx->link.rate,
					 bpc_map, color_format_map,
					 mdtd->pixel_clock);
	if (retval)
		return retval;

	vparams->pix_enc = color_format_map;
	dptx_dbg(dptx, "Change pixel encoding to %d\n", color_format_map);

	vparams->bpc = bpc_map;
	dptx_video_bpc_change(dptx, stream);
	dptx_dbg(dptx, "Change bits per component to %d\n", bpc_map);

	dptx_video_ycc_mapping_change(dptx, stream);
	dptx_video_ts_change(dptx, stream);

	switch (pattern) {
	case DP_TEST_PATTERN_NONE:
		dptx_dbg(dptx, "TEST_PATTERN_NONE %d\n", pattern);
		break;
	case DP_TEST_PATTERN_COLOR_RAMPS:
		dptx_dbg(dptx, "TEST_PATTERN_COLOR_RAMPS %d\n", pattern);
		vparams->pattern_mode = RAMP;
		dptx_video_pattern_set(dptx, RAMP, stream);
		dptx_video_pattern_change(dptx, stream);
		dptx_dbg(dptx, "Change video pattern to RAMP\n");
		break;
	case DP_TEST_PATTERN_BW_VERITCAL_LINES:
		dptx_dbg(dptx, "TEST_PATTERN_BW_VERTICAL_LINES %d\n", pattern);
		break;
	case DP_TEST_PATTERN_COLOR_SQUARE:
		dptx_dbg(dptx, "TEST_PATTERN_COLOR_SQUARE %d\n", pattern);
		vparams->pattern_mode = COLRAMP;
		dptx_video_pattern_set(dptx, COLRAMP, stream);
		dptx_video_pattern_change(dptx, stream);
		dptx_dbg(dptx, "Change video pattern to COLRAMP\n");
		break;
	default:
		dptx_dbg(dptx, "Invalid TEST_PATTERN %d\n", pattern);
		return -EINVAL;
	}

	retval = handle_test_link_video_timming(dptx, stream);
	if (retval)
		return retval;

	return 0;
}

static int dptx_set_custom_pattern(struct dptx *dptx)
{
	int retval;
	u8 pattern0, pattern1, pattern2, pattern3, pattern4, pattern5,
	   pattern6, pattern7, pattern8, pattern9;

	u32 custompat0;
	u32 custompat1;
	u32 custompat2;

	retval = dptx_read_dpcd(dptx, DP_TEST_80BIT_CUSTOM_PATTERN_0, &pattern0);
	if (retval)
		return retval;

	retval = dptx_read_dpcd(dptx, DP_TEST_80BIT_CUSTOM_PATTERN_1, &pattern1);
	if (retval)
		return retval;

	retval = dptx_read_dpcd(dptx, DP_TEST_80BIT_CUSTOM_PATTERN_2, &pattern2);
	if (retval)
		return retval;

	retval = dptx_read_dpcd(dptx, DP_TEST_80BIT_CUSTOM_PATTERN_3, &pattern3);
	if (retval)
		return retval;

	retval = dptx_read_dpcd(dptx, DP_TEST_80BIT_CUSTOM_PATTERN_4, &pattern4);
	if (retval)
		return retval;

	retval = dptx_read_dpcd(dptx, DP_TEST_80BIT_CUSTOM_PATTERN_5, &pattern5);
	if (retval)
		return retval;

	retval = dptx_read_dpcd(dptx, DP_TEST_80BIT_CUSTOM_PATTERN_6, &pattern6);
	if (retval)
		return retval;

	retval = dptx_read_dpcd(dptx, DP_TEST_80BIT_CUSTOM_PATTERN_7, &pattern7);
	if (retval)
		return retval;

	retval = dptx_read_dpcd(dptx, DP_TEST_80BIT_CUSTOM_PATTERN_8, &pattern8);
	if (retval)
		return retval;

	retval = dptx_read_dpcd(dptx, DP_TEST_80BIT_CUSTOM_PATTERN_9, &pattern9);
	if (retval)
		return retval;

	/*
	 *  Calculate 30,30 and 20 bits custom patterns depending on TEST_80BIT_CUSTOM_PATTERN sequence
	 */
	custompat0 = ((((((pattern3 & (0xff >> 2)) << 8) | pattern2) << 8) | pattern1) << 8) | pattern0;
	custompat1 = ((((((((pattern7 & (0xf)) << 8) | pattern6) << 8) | pattern5) << 8) | pattern4) << 2) | ((pattern3 >> 6) & 0x3);
	custompat2 = (((pattern9 << 8) | pattern8) << 4) | ((pattern7 >> 4) & 0xf);

	dptx_write_reg(dptx, dptx->regs[DPTX], CUSTOMPAT0, custompat0);
	dptx_write_reg(dptx, dptx->regs[DPTX], CUSTOMPAT1, custompat1);
	dptx_write_reg(dptx, dptx->regs[DPTX], CUSTOMPAT2, custompat2);

	return 0;
}

static int adjust_vswing_and_preemphasis(struct dptx *dptx)
{
	int retval;
	int i;
	u8 lane_01;
	u8 lane_23;

	retval = dptx_read_dpcd(dptx, DP_ADJUST_REQUEST_LANE0_1, &lane_01);
	if (retval)
		return retval;

	retval = dptx_read_dpcd(dptx, DP_ADJUST_REQUEST_LANE2_3, &lane_23);
	if (retval)
		return retval;

	/* Need to reassert override before applying tuning parameters */
	google_dpphy_eq_tune_ovrd_enable(dptx->dp_phy,
			DPTX_PIN_TO_NUM_LANES(dptx->hw_config.pin_type), true);

	for (i = 0; i < dptx->link.lanes; i++) {
		u8 pe = 0;
		u8 vs = 0;

		switch (i) {
		case 0:
			pe = (lane_01 &  DP_ADJUST_PRE_EMPHASIS_LANE0_MASK)
				>> DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT;
			vs = (lane_01 &  DP_ADJUST_VOLTAGE_SWING_LANE0_MASK)
				>> DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT;
			break;
		case 1:
			pe = (lane_01 & DP_ADJUST_PRE_EMPHASIS_LANE1_MASK)
				>> DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT;
			vs = (lane_01 & DP_ADJUST_VOLTAGE_SWING_LANE1_MASK)
				>> DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT;
			break;
		case 2:
			pe = (lane_23 & DP_ADJUST_PRE_EMPHASIS_LANE0_MASK)
				>> DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT;
			vs = (lane_23 & DP_ADJUST_VOLTAGE_SWING_LANE0_MASK)
				>> DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT;
			break;
		case 3:
			pe = (lane_23 & DP_ADJUST_PRE_EMPHASIS_LANE1_MASK)
				>> DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT;
			vs = (lane_23 & DP_ADJUST_VOLTAGE_SWING_LANE1_MASK)
				>> DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT;
			break;
		default:
			break;
		}

		/*
		 * PHY_TX_EQ registers do not have signals tied to the ComboPHY and are effectively
		 * no-op so the usb phy driver callback is needed. Programming the registers
		 * anyways if debug status is needed.
		 */
		dptx_phy_set_pre_emphasis(dptx, i, pe);
		dptx_phy_set_vswing(dptx, i, vs);
		google_dpphy_pipe_tx_eq_set(dptx->dp_phy, i, vs, pe);
		google_dpphy_eq_tune_ovrd_apply(dptx->dp_phy, i, dptx->link.rate, vs, pe);
		google_dpphy_eq_tune_asic_read(dptx->dp_phy, i, dptx->hw_config.orient_type);
	}

	return 0;
}

static int handle_test_phy_pattern(struct dptx *dptx)
{
	u8 pattern;
	int retval;

	retval = dptx_read_dpcd(dptx, DP_TEST_PHY_PATTERN, &pattern);
	if (retval)
		return retval;

	pattern &= DP_TEST_PHY_PATTERN_SEL_MASK;

	switch (pattern) {
	case DP_TEST_PHY_PATTERN_NONE:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		dptx_info(dptx, "No test pattern selected\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_NONE);
		break;
	case DP_TEST_PHY_PATTERN_D10:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		dptx_info(dptx, "D10.2 without scrambling test phy pattern\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_1);
		break;
	case DP_TEST_PHY_PATTERN_SEMC:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		dptx_info(dptx, "Symbol error measurement count test phy pattern\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_SYM_ERM);
		break;
	case DP_TEST_PHY_PATTERN_PRBS7:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		dptx_info(dptx, "PRBS7 test phy pattern\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_PRBS7);
		break;
	case DP_TEST_PHY_PATTERN_CUSTOM:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		dptx_info(dptx, "80-bit custom pattern transmitted test phy pattern\n");

		retval = dptx_set_custom_pattern(dptx);
		if (retval)
			return retval;
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_CUSTOM80);
		break;
	case DP_TEST_PHY_PATTERN_CP2520_1:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		dptx_info(dptx, "CP2520_1 - HBR2 Compliance EYE pattern\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_CP2520_1);
		break;
	case DP_TEST_PHY_PATTERN_CP2520_2:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		dptx_info(dptx, "CP2520_2 - pattern\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_CP2520_2);
		break;
	case DP_TEST_PHY_PATTERN_CP2520_3_TPS4:
		retval = adjust_vswing_and_preemphasis(dptx);
		if (retval)
			return retval;
		dptx_info(dptx, "DP_TEST_PHY_PATTERN_CP2520_3_TPS4 - pattern\n");
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_4);
		break;
	default:
		dptx_info(dptx, "Invalid TEST_PHY_PATTERN\n");
		return -EINVAL;
	}
	return retval;
}

static int handle_automated_test_request(struct dptx *dptx)
{
	int retval;
	u8 test;

	retval = dptx_read_dpcd(dptx, DP_TEST_REQUEST, &test);
	if (retval)
		return retval;

	if (test & DP_TEST_LINK_TRAINING) {
		dptx_info(dptx, "%s: DP_TEST_LINK_TRAINING\n", __func__);

		retval = dptx_write_dpcd(dptx, DP_TEST_RESPONSE, DP_TEST_ACK);
		if (retval)
			return retval;

		retval = handle_test_link_training(dptx);
		if (retval)
			return retval;
	}

	if (test & DP_TEST_LINK_VIDEO_PATTERN) {
		dptx_info(dptx, "%s: DP_TEST_LINK_VIDEO_PATTERN\n", __func__);

		retval = dptx_write_dpcd(dptx, DP_TEST_RESPONSE, DP_TEST_ACK);
		if (retval)
			return retval;

		retval = handle_test_link_video_pattern(dptx, 0);
		if (retval)
			return retval;
	}

	if (test & DP_TEST_LINK_AUDIO_PATTERN) {
		dptx_info(dptx, "%s: DP_TEST_LINK_AUDIO_PATTERN\n", __func__);

		retval = dptx_write_dpcd(dptx, DP_TEST_RESPONSE, DP_TEST_ACK);
		if (retval)
			return retval;

		retval = handle_test_link_audio_pattern(dptx);
		if (retval)
			return retval;
	}

	if (test & DP_TEST_LINK_EDID_READ) {
		u8 *data;
		u8 checksum;
		u8 i;

		dptx_info(dptx, "%s: DP_TEST_LINK_EDID_READ\n", __func__);

		retval = dptx_read_edid(dptx);
		if (retval)
			return retval;

		if (!drm_edid_is_valid((struct edid *)dptx->edid))
			dptx_warn(dptx, "EDID data is corrupted\n");

		/* re-calculate the checksum of the last EDID block */
		data = &dptx->edid[dptx->edid_size - EDID_LENGTH];
		checksum = 0;
		for (i = 0; i < EDID_LENGTH - 1; i++)
			checksum += data[i];
		checksum = -checksum;

		retval = dptx_write_dpcd(dptx, DP_TEST_EDID_CHECKSUM, checksum);
		if (retval)
			return retval;

		retval = dptx_write_dpcd(dptx, DP_TEST_RESPONSE,
					 DP_TEST_ACK | DP_TEST_EDID_CHECKSUM_WRITE);
		if (retval)
			return retval;
	}

	if (test & DP_TEST_LINK_PHY_TEST_PATTERN) {
		dptx_info(dptx, "%s: DP_TEST_LINK_PHY_TEST_PATTERN\n", __func__);

		retval = handle_test_phy_pattern(dptx);
		if (retval)
			return retval;

		retval = dptx_write_dpcd(dptx, DP_TEST_RESPONSE, DP_TEST_ACK);
		if (retval)
			return retval;

	}
	return 0;
}

int handle_sink_request(struct dptx *dptx)
{
	int retval;
	u8 irq_vector = 0;

	retval = dptx_link_check_status(dptx, &irq_vector);
	if (retval)
		return retval;

	if (irq_vector & DP_AUTOMATED_TEST_REQUEST) {
		dptx_info(dptx, "%s: DP_AUTOMATED_TEST_REQUEST\n", __func__);
		retval = handle_automated_test_request(dptx);
		if (retval) {
			dptx_err(dptx, "Automated test request failed\n");
			dptx_write_dpcd(dptx, DP_TEST_RESPONSE, DP_TEST_NAK);
			return retval;
		}
	}

#if IS_ENABLED(CONFIG_DWC_DPTX_HDCP)
	if (irq_vector & DP_CP_IRQ) {
		dptx_info(dptx, "%s: DP_CP_IRQ\n", __func__);
		handle_cp_irq_set(dptx);
	}
#endif // CONFIG_DWC_DPTX_HDCP

	return 0;
}

static int dptx_audio_wait_for_disable_done(struct dptx *dptx)
{
	int ret = 0;

	if (!dptx->audio_enabled) {
		dptx_info(dptx, "audio is not enabled\n");
		return ret;
	}

	reinit_completion(&dptx->audio_disable_done);

	/* release dptx mutex, so that dptx_audio_close() can run */
	mutex_unlock(&dptx->mutex);

	ret = wait_for_completion_timeout(&dptx->audio_disable_done,
					  msecs_to_jiffies(AUDIO_COMPLETION_TIMEOUT_MS));
	if (ret == 0) {
		dptx_err(dptx, "timed out waiting for audio disable\n");
		ret = -ETIMEDOUT;
	} else
		dptx_info(dptx, "audio disable complete\n");

	mutex_lock(&dptx->mutex);

	return ret;
}

/**
 * hotunplug_get_crtc_active() - Gets crtc_state for dptx during unplug.
 * Specifically, this function wraps the process of grabbing modeset locks
 * and checking if the crtc_state is active.
 *
 * @dptx: dptx handle
 *
 * Return: true if a crtc is connected to dptx and active; false otherwise
 */
static bool hotunplug_get_crtc_active(struct dptx *dptx)
{
	struct drm_device *drm_dev;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_connector_state *conn_state;
	struct drm_crtc *crtc;
	int ret;
	bool crtc_active = false;

	if (!dptx->connector)
		return false;

	drm_dev = dptx->connector->dev;

	drm_modeset_acquire_init(&ctx, 0);
modeset_lock_retry:
	ret = drm_modeset_lock(&drm_dev->mode_config.connection_mutex, &ctx);
	if (ret)
		goto modeset_lock_fail;

	conn_state = dptx->connector->state;
	if (!conn_state->crtc)
		goto out;

	crtc = conn_state->crtc;
	ret = drm_modeset_lock(&crtc->mutex, &ctx);
	if (ret)
		goto modeset_lock_fail;

	if (!crtc->state)
		goto out;

	/* all locks acquired here */
	crtc_active = crtc->state->active;

modeset_lock_fail:
	if (ret == -EDEADLK) {
		ret = drm_modeset_backoff(&ctx);
		if (!ret)
			goto modeset_lock_retry;
	}
out:
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	dptx_dbg(dptx, "hotunplug crtc_exists:%d crtc_active:%d\n", (crtc != NULL), crtc_active);

	return crtc_active;
}

int handle_hotunplug_core(struct dptx *dptx, bool hpd_irq)
{
	int retval;
	struct ctrl_regfields *ctrl_fields = dptx->ctrl_fields;
	int num_lanes = DPTX_PIN_TO_NUM_LANES(dptx->hw_config.pin_type);

	if (!dptx->link.trained)
		return 0;

	dptx->hpd_irq_unplug_running = hpd_irq;

	if (!dptx->link_test_mode) {
		bool crtc_active;

		/*
		 * release dptx mutex, so that dptx_bridge_atomic_disable() can run,
		 * and so that in-flight dptx_bridge_atomic_enable can complete
		 */
		mutex_unlock(&dptx->mutex);

		crtc_active = hotunplug_get_crtc_active(dptx);

		drm_bridge_hpd_notify(&dptx->bridge, connector_status_disconnected);

		if (crtc_active || dptx->video_enabled) {
			reinit_completion(&dptx->video_disable_done);

			/* wait for dptx_bridge_atomic_disable() to run */
			if (!wait_for_completion_timeout(&dptx->video_disable_done, 3 * HZ)) {
				dptx_err(dptx, "dptx_bridge_atomic_disable() was not called\n");
				dptx_video_disable(dptx);
			}
		}

		/* re-acquire dptx mutex */
		mutex_lock(&dptx->mutex);

		/* wait for audio disable to complete */
		dptx_audio_wait_for_disable_done(dptx);
	}

	dptx->dummy_dtds_present = false;

	dptx_dbg(dptx, "Disabling Forward Error Correction\n");
	// Disable forward error correction
	dptx_write_regfield(dptx, ctrl_fields->field_enable_fec, 0);

	msleep(100);

	dptx_phy_enable_xmit(dptx, dptx->link.lanes, false);
	/* Reset TX EQ message bus */
	for (int i = 0; i < num_lanes; i++)
		google_dpphy_pipe_tx_eq_set(dptx->dp_phy, i, 0, 0);
	google_dpphy_eq_tune_ovrd_enable(dptx->dp_phy, num_lanes, false);

	dptx->link.trained = false;
	dptx->hpd_irq_unplug_running = false;

	return 0;
}

int handle_hotunplug(struct dptx *dptx)
{
	int retval;
	struct ctrl_regfields *ctrl_fields = dptx->ctrl_fields;

	handle_hotunplug_core(dptx, false);

	/* Move PHY to P3 state */
	dptx_write_regfield(dptx, ctrl_fields->field_phy_powerdown, 3);

	retval = dptx_phy_wait_busy(dptx, dptx->link.lanes);
	if (retval) {
		dptx_err(dptx, "Timed out waiting for PHY BUSY\n");
		return retval;
	}

	atomic_set(&dptx->sink_request, 0);
	dptx->link.trained = false;

	return 0;
}

static int dptx_read_edid_block(struct dptx *dptx,
				unsigned int block)
{
	int retval;
	int retry = 0;
	int i;

	u8 offset = (block & 0x1) * 128;
	u8 segment = block >> 1;

	dptx_dbg(dptx, "%s: block=%d\n",
		 __func__, block);

again:
	if (retry > 5)
		return -EINVAL;

	retval = dptx_write_bytes_to_i2c(dptx, 0x30, &segment, 1);
	if (retval == -EINVAL) {
		/* received I2C NACK, retry one more time */
		retval = dptx_write_bytes_to_i2c(dptx, 0x30, &segment, 1);
		if (retval == -EINVAL)
			dptx_warn(dptx, "I2C NACK on write to 0x30 DDC segment\n");
	}

	retval = dptx_write_bytes_to_i2c(dptx, 0x50, &offset, 1);
	if (retval == -EINVAL) {
		++retry;
		goto again;
	}

	retval = dptx_read_bytes_from_i2c(dptx, 0x50,
					  &dptx->edid[block * 128],
					  128);
	if (retval == -EINVAL) {
		++retry;
		goto again;
	}

	/* finish EDID block read with I2C address-only read (no MOT) */
	dptx_i2c_addr_only_read(dptx, 0x50);

	if (retval == -EINVAL)
		for (i = 0; i < 128; i++)
			dptx->edid_second[i] = 0x00;
	else
		for (i = 0; i < 128; i++)
			dptx->edid_second[i] = dptx->edid[128 + i];

	print_buf(&dptx->edid[block * 128], 128);
	dptx_info(dptx, "EDID block %d:\n", block);
	print_hex_dump(KERN_INFO, "dwc_dptx: ", DUMP_PREFIX_NONE, 16, 1,
			&dptx->edid[block * 128], 128, true);

	return 0;
}

static const u8 dptx_fake_edid[EDID_LENGTH] = {
	/* header */
	0x0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0,
	/* vendor: GGL, mfg week: 1, mfg year: 2025 */
	0x1c, 0xec, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x23,
	/* EDID version: 1.4 */
	0x1, 0x4,
	/* basic display parameters */
	0xa5, 0x46, 0x27, 0x78, 0x22,
	/* color characteristics */
	0xba, 0xc5, 0xa9, 0x53, 0x4e, 0xa6, 0x25, 0xe, 0x50, 0x54,
	/* established timings: 640x480@60 */
	0x20, 0x0, 0x0,
	/* standard timings: 1920x1080@60 */
	0xd1, 0xc0, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1,
	0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1,
	/* detailed timings: 1920x1080@60 */
	0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40,
	0x58, 0x2c, 0x45, 0x0, 0xba, 0x88, 0x21, 0x0, 0x0, 0x1e,
	0x0, 0x0, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x10, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	/* monitor descriptor: monitor name: GGL FAKE EDID */
	0x0, 0x0, 0x0, 0xfc, 0x0, 0x47, 0x47, 0x4c,
	0x20, 0x46, 0x41, 0x4b, 0x45, 0x20, 0x45, 0x44, 0x49, 0x44,
	/* extension flag + checksum */
	0x0, 0x8d
};

static int dptx_hfeeodb_block_count(const u8 *cta)
{
	// check CTA Extension Block's tag and revision
	if (cta[0] != CEA_EXT || cta[1] < 3)
		return 0;

	/* check DTD offset
	 * Byte 0 - 3: Tag, Revision, DTD offset and Flags
	 * Byte 4 - 6: HF-EEODB data
	 */
	if (cta[2] < 7)
		return 0;

	// check HF-EEODB data
	if ((cta[4] >> 5) != EDID_EEODB_TAG || (cta[4] & 0x1f) < 2 ||
	    cta[5] != EDID_EEODB_EXT_TAG)
		return 0;

	return cta[6];
}

static int dptx_read_edid(struct dptx *dptx)
{
	int i;
	int retval = 0;
	unsigned int ext_blocks = 0;
	int eeodb = 0;

	dptx_dbg_link(dptx, "--- START EDID READ ---\n");
	memset(dptx->edid, 0, DPTX_DEFAULT_EDID_BUFLEN);
	dptx_dbg(dptx, "Read EDID Block 0\n");
	retval = dptx_read_edid_block(dptx, 0);
	if (retval)
		goto fail;

	// Byte 126 -> 0x7E -> Number of Extended EDID Blocks
	if (dptx->edid[126] > 10) {
		/* Workaround for QD equipment */
		/* TODO investigate corruptions of EDID blocks */
		ext_blocks = 2;

		dptx_dbg(dptx, "Unexpected num_ext_blocks=%d\n", dptx->edid[126]);
	} else {
		ext_blocks = dptx->edid[126];
		dptx_dbg_link(dptx, "Extended EDID Blocks: %d\n", ext_blocks);
	}

	if (ext_blocks >= 1) {
		retval = dptx_read_edid_block(dptx, 1);
		if (retval)
			goto fail;

		eeodb = dptx_hfeeodb_block_count(&dptx->edid[1 * EDID_LENGTH]);
		if (eeodb > 0 && eeodb <= 10) {
			dptx_dbg(dptx, "eeodb = %d\n", eeodb);
			ext_blocks = eeodb;
		}
	}

	dptx->edid_size = (1 + ext_blocks) * EDID_LENGTH;

	if (dptx->edid_size > DPTX_DEFAULT_EDID_BUFLEN) {
		u8 *new_edid = kzalloc(dptx->edid_size, GFP_KERNEL);
		if (new_edid == NULL) {
			dptx_err(dptx, "EDID memory reallocation failed\n");
			retval = -ENOMEM;
			goto fail;
		}
		memcpy(new_edid, dptx->edid, 2 * EDID_LENGTH);
		kfree(dptx->edid);
		dptx->edid = new_edid;
	}

	for (i = 2; i <= ext_blocks; i++) {
		dptx_dbg(dptx, "Read EDID Block %d\n", i);
		retval = dptx_read_edid_block(dptx, i);
		if (retval)
			goto fail;
	}

	dptx_dbg_link(dptx, "--- EDID READ DONE ---\n");
	return 0;

fail:
	/* EDID read failed, use fake EDID */
	memcpy(dptx->edid, dptx_fake_edid, EDID_LENGTH);
	dptx->edid_size = EDID_LENGTH;
	dptx_err(dptx, "--- EDID READ FAILED: use fake EDID ---\n");
	dptx->stats.edid_read_failures++;
	print_hex_dump(KERN_INFO, "dwc_dptx: ", DUMP_PREFIX_NONE, 16, 1,
			&dptx->edid[0], 128, true);
	return 0;
}

/* TODO these are kernel functions. Need to make them accessible. */

static u8 drm_dp_msg_header_crc4(const u8 *data, size_t num_nibbles)
{
	u8 bitmask = 0x80;
	u8 bitshift = 7;
	u8 array_index = 0;
	int number_of_bits = num_nibbles * 4;
	u8 remainder = 0;

	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		remainder |= (data[array_index] & bitmask) >> bitshift;
		bitmask >>= 1;
		bitshift--;
		if (bitmask == 0) {
			bitmask = 0x80;
			bitshift = 7;
			array_index++;
		}
		if ((remainder & 0x10) == 0x10)
			remainder ^= 0x13;
	}

	number_of_bits = 4;
	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		if ((remainder & 0x10) != 0)
			remainder ^= 0x13;
	}

	return remainder;
}

static u8 drm_dp_msg_data_crc4(const u8 *data, u8 number_of_bytes)
{
	u8 bitmask = 0x80;
	u8 bitshift = 7;
	u8 array_index = 0;
	int number_of_bits = number_of_bytes * 8;
	u16 remainder = 0;

	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		remainder |= (data[array_index] & bitmask) >> bitshift;
		bitmask >>= 1;
		bitshift--;
		if (bitmask == 0) {
			bitmask = 0x80;
			bitshift = 7;
			array_index++;
		}
		if ((remainder & 0x100) == 0x100)
			remainder ^= 0xd5;
	}

	number_of_bits = 8;
	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		if ((remainder & 0x100) != 0)
			remainder ^= 0xd5;
	}

	return remainder & 0xff;
}

static void drm_dp_encode_sideband_msg_hdr(struct drm_dp_sideband_msg_hdr *hdr,
					   u8 *buf, int *len)
{
	int idx = 0;
	int i;
	u8 crc4;

	buf[idx++] = ((hdr->lct & 0xf) << 4) | (hdr->lcr & 0xf);
	for (i = 0; i < (hdr->lct / 2); i++) {
		buf[idx++] = (hdr->rad[i]) << 4;
		pr_err("sahakyan: i = %d, idx = %d,  buf = %x, rad = %x\n", i, idx, buf[idx], hdr->rad[i]);
	}
	buf[idx++] = (hdr->broadcast << 7) | (hdr->path_msg << 6) |
		(hdr->msg_len & 0x3f);
	buf[idx++] = (hdr->somt << 7) | (hdr->eomt << 6) | (hdr->seqno << 4);

	crc4 = drm_dp_msg_header_crc4(buf, (idx * 2) - 1);
	buf[idx - 1] |= (crc4 & 0xf);
	*len = idx;
}

static void drm_dp_crc_sideband_chunk_req(u8 *msg, u8 len)
{
	u8 crc4;

	crc4 = drm_dp_msg_data_crc4(msg, len);
	msg[len] = crc4;
}

static bool drm_dp_decode_sideband_msg_hdr(struct drm_dp_sideband_msg_hdr *hdr,
					   u8 *buf, int buflen, u8 *hdrlen)
{
	u8 crc4;
	u8 len;
	int i;
	u8 idx;

	if (buf[0] == 0)
		return false;
	len = 3;
	len += ((buf[0] & 0xf0) >> 4) / 2;
	if (len > buflen)
		return false;
	crc4 = drm_dp_msg_header_crc4(buf, (len * 2) - 1);

	if ((crc4 & 0xf) != (buf[len - 1] & 0xf)) {
		//DRM_DEBUG_KMS("crc4 mismatch 0x%x 0x%x\n", crc4, buf[len - 1]);
		return false;
	}

	hdr->lct = (buf[0] & 0xf0) >> 4;
	hdr->lcr = (buf[0] & 0xf);
	idx = 1;
	for (i = 0; i < (hdr->lct / 2); i++)
		hdr->rad[i] = buf[idx++];
	hdr->broadcast = (buf[idx] >> 7) & 0x1;
	hdr->path_msg = (buf[idx] >> 6) & 0x1;
	hdr->msg_len = buf[idx] & 0x3f;
	idx++;
	hdr->somt = (buf[idx] >> 7) & 0x1;
	hdr->eomt = (buf[idx] >> 6) & 0x1;
	hdr->seqno = (buf[idx] >> 4) & 0x1;
	idx++;
	*hdrlen = idx;
	return true;
}

static const char *dptx_sideband_header_rad_string(struct drm_dp_sideband_msg_hdr *header)
{
	if (header->lct > 1)
		return "TODO";

	return "none";
}

static void dptx_print_sideband_header(struct dptx *dptx,
				       struct drm_dp_sideband_msg_hdr *header)
{
	dptx_dbg(dptx, "SIDEBAND_MSG_HEADER: "
		 "lct=%d, lcr=%d, rad=%s, bcast=%d, "
		 "path=%d, msglen=%d, somt=%d, eomt=%d, seqno=%d\n",
		 header->lct, header->lcr, dptx_sideband_header_rad_string(header),
		 header->broadcast, header->path_msg, header->msg_len,
		 header->somt, header->eomt, header->seqno);
}

static int dptx_wait_down_rep(struct dptx *dptx)
{
	int count = 0;
	u8 vector;

	while (1) {
		dptx_read_dpcd(dptx, DP_DEVICE_SERVICE_IRQ_VECTOR, &vector);

		if (vector & DP_DOWN_REP_MSG_RDY) {
			dptx_dbg(dptx, "%s: vector set\n", __func__);
			break;
		}

		count++;
		if (count > 2000) {
			dptx_dbg(dptx, "%s: Timed out\n", __func__);
			return -ETIMEDOUT;
		}

		usleep_range(950, 1000);
	}

	return 0;
}

static int dptx_clear_down_rep(struct dptx *dptx)
{
	int count = 0;
	u8 vector;

	while (1) {
		dptx_read_dpcd(dptx, DP_DEVICE_SERVICE_IRQ_VECTOR, &vector);

		if (!(vector & DP_DOWN_REP_MSG_RDY)) {
			dptx_dbg(dptx, "%s: vector clear\n", __func__);
			break;
		}

		dptx_write_dpcd(dptx, DP_DEVICE_SERVICE_IRQ_VECTOR, DP_DOWN_REP_MSG_RDY);

		count++;
		if (count > 2000) {
			dptx_dbg(dptx, "%s: Timed out\n", __func__);
			return -ETIMEDOUT;
		}

		usleep_range(950, 1000);
	}

	return 0;
}

static int dptx_sideband_get_down_rep(struct dptx *dptx, u8 request_id, u8 *msg_out)
{
	struct drm_dp_sideband_msg_hdr header;
	u8 buf[256];
	u8 header_len;
	int retval;
	int first = 1;
	u8 msg[1024];
	u8 msg_len;
	int retries = 0;

again:
	memset(msg, 0, 1024);
	msg_len = 0;

	while (1) {
		retval = dptx_wait_down_rep(dptx);
		if (retval) {
			dptx_err(dptx, "%s: Error waiting down rep (%d)\n", __func__, retval);
			return retval;
		}

		retval = dptx_read_bytes_from_dpcd(dptx, DP_SIDEBAND_MSG_DOWN_REP_BASE, buf, 256);
		if (retval) {
			dptx_err(dptx, "%s: Error reading down rep (%d)\n", __func__, retval);
			return retval;
		}
		if (!drm_dp_decode_sideband_msg_hdr(&header, buf, 256, &header_len)) {
			dptx_err(dptx, "%s: Error decoding sideband header (%d)\n", __func__, retval);
			return -EINVAL;
		}

		dptx_print_sideband_header(dptx, &header);

	/* TODO check sideband msg body crc */
		header.msg_len -= 1;
		memcpy(&msg[msg_len], &buf[header_len], header.msg_len);
		msg_len += header.msg_len;

		if (first && !header.somt) {
			dptx_err(dptx, "%s: SOMT not set\n", __func__);
			return -EINVAL;
		}
		first = 0;

		dptx_write_dpcd(dptx, DP_DEVICE_SERVICE_IRQ_VECTOR, DP_DOWN_REP_MSG_RDY);

		if (header.eomt)
			break;
	}

	print_buf(msg, msg_len);
	if ((msg[0] & 0x7f) != request_id) {
		if (retries < 3) {
			dptx_err(dptx, "%s: request_id %d does not match expected %d, retrying\n", __func__, msg[0] & 0x7f, request_id);
			retries++;
			goto again;
		} else {
			dptx_err(dptx, "%s: request_id %d does not match expected %d, giving up\n", __func__, msg[0] & 0x7f, request_id);
			return -EINVAL;
		}
	}

	retval = dptx_clear_down_rep(dptx);
	if (retval) {
		dptx_err(dptx, "%s: Error waiting down rep clear (%d)\n", __func__, retval);
		return retval;
	}

//	return 0;
	if (msg_out)
		memcpy(msg_out, msg, msg_len);

	return msg_len;
}

static int dptx_aux_msg_clear_payload_id_table(struct dptx *dptx)
{
    //dp1.4 spec 2.11.6.1
	struct drm_dp_sideband_msg_hdr header = {
		.lct = 1,
		.lcr = 6,
		.rad = { 0, },
		.broadcast = true,
		.path_msg = 1,
		.msg_len = 2,
		.somt = 1,
		.eomt = 1,
		.seqno = 0,
	};

	u8 buf[256];
	int len = 256;
	u8 *msg;

	drm_dp_encode_sideband_msg_hdr(&header, buf, &len);

	msg = &buf[len];
	msg[0] = DP_CLEAR_PAYLOAD_ID_TABLE;
	drm_dp_crc_sideband_chunk_req(msg, 1);

	len += 2;

	dptx_dbg(dptx, "%s: Sending DOWN_REQ\n", __func__);
	dptx_write_bytes_to_dpcd(dptx, DP_SIDEBAND_MSG_DOWN_REQ_BASE,
				 buf, len);

	dptx_sideband_get_down_rep(dptx, DP_CLEAR_PAYLOAD_ID_TABLE, NULL);

	return 0;
}

static bool drm_dp_sideband_parse_link_address(struct drm_dp_sideband_msg_rx *raw,
					       struct drm_dp_sideband_msg_reply_body *repmsg)
{
	int idx = 1;
	int i;

	memcpy(&repmsg->u.link_addr.guid, &raw->msg[idx], 16);
	idx += 16;
	repmsg->u.link_addr.nports = raw->msg[idx] & 0xf;
	idx++;
	if (idx > raw->curlen)
		goto fail_len;
	for (i = 0; i < repmsg->u.link_addr.nports; i++) {
		if (raw->msg[idx] & 0x80)
			repmsg->u.link_addr.ports[i].input_port = 1;

		repmsg->u.link_addr.ports[i].peer_device_type = (raw->msg[idx] >> 4) & 0x7;
		repmsg->u.link_addr.ports[i].port_number = (raw->msg[idx] & 0xf);

		idx++;
		if (idx > raw->curlen)
			goto fail_len;
		repmsg->u.link_addr.ports[i].mcs = (raw->msg[idx] >> 7) & 0x1;
		repmsg->u.link_addr.ports[i].ddps = (raw->msg[idx] >> 6) & 0x1;
		if (repmsg->u.link_addr.ports[i].input_port == 0)
			repmsg->u.link_addr.ports[i].legacy_device_plug_status = (raw->msg[idx] >> 5) & 0x1;
		idx++;
		if (idx > raw->curlen)
			goto fail_len;
		if (repmsg->u.link_addr.ports[i].input_port == 0) {
			repmsg->u.link_addr.ports[i].dpcd_revision = (raw->msg[idx]);
			idx++;
			if (idx > raw->curlen)
				goto fail_len;
			memcpy(&repmsg->u.link_addr.ports[i].peer_guid, &raw->msg[idx], 16);
			idx += 16;
			if (idx > raw->curlen)
				goto fail_len;
			repmsg->u.link_addr.ports[i].num_sdp_streams = (raw->msg[idx] >> 4) & 0xf;
			repmsg->u.link_addr.ports[i].num_sdp_stream_sinks = (raw->msg[idx] & 0xf);
			idx++;
		}
		if (idx > raw->curlen)
			goto fail_len;
	}

	return true;
fail_len:
	pr_info("Link address reply parse length fail %d %d\n", idx, raw->curlen);
	return false;
}

static int dptx_aux_msg_link_address(struct dptx *dptx, struct drm_dp_sideband_msg_rx *raw,
				     struct drm_dp_sideband_msg_reply_body *rep, int port1)
{
	struct drm_dp_sideband_msg_hdr header;
	u8 buf[256];
	int len = 256;

	u8 *msg;

	memset(&header, 0, sizeof(struct drm_dp_sideband_msg_hdr));

	header.lct = 1;
	header.lcr = 0;
	header.rad[0] = 0;
	header.broadcast = false;
	header.path_msg = 0;
	header.msg_len = 2;
	header.somt = 1;
	header.eomt = 1;
	header.seqno = 0;

	if (port1 >= 0) {
		header.lct = 2;
		header.lcr = 1;
		header.rad[0] = port1;
		pr_err("sahakyan: link port1=%d", port1);
	}

	drm_dp_encode_sideband_msg_hdr(&header, buf, &len);

	msg = &buf[len];
	msg[0] = DP_LINK_ADDRESS;

	drm_dp_crc_sideband_chunk_req(msg, 1);

	len += 2;
	print_buf(buf, len);
	dptx_dbg(dptx, "%s: Sending DOWN_REQ\n", __func__);
	dptx_write_bytes_to_dpcd(dptx, DP_SIDEBAND_MSG_DOWN_REQ_BASE,
				 buf, len);

	len = dptx_sideband_get_down_rep(dptx, DP_LINK_ADDRESS, raw->msg);
	raw->curlen = len;
	dptx_dbg(dptx, "%s: rawlen = %d\n", __func__, len);

	print_buf(raw->msg, len);
	drm_dp_sideband_parse_link_address(raw, rep);

		return 0;
}

static int dptx_aux_msg_allocate_payload(struct dptx *dptx, u8 port,
					 u8 vcpid, u16 pbn, int port1, u8 pdt)
{
	struct drm_dp_sideband_msg_hdr header;
	u8 buf[256];
	int len = 256;
	u8 *msg;
	int i;

	memset(&header, 0, sizeof(struct drm_dp_sideband_msg_hdr));

	header.lct = 1;
	header.lcr = 0;
	header.rad[0] = 0;
	header.broadcast = false;
	header.path_msg = 1;
	header.msg_len = 6;
	header.somt = 1;
	header.eomt = 1;
	header.seqno = 0;

	dptx_dbg(dptx, "%s: PBN=%d\n", __func__, pbn);
	if (port1 >= 0) {
		header.lct = 2;
		header.lcr = 1;
		header.rad[0] = port1;
		for (i = 0; i < 8; i++)
			pr_err("sahakyan: --------------------- header[%d].rad = %d", i, header.rad[i]);
	}
	drm_dp_encode_sideband_msg_hdr(&header, buf, &len);

	msg = &buf[len];
	msg[0] = DP_ALLOCATE_PAYLOAD;
	msg[1] = ((port & 0xf) << 4);
	msg[2] = vcpid & 0x7f;
	msg[3] = pbn >> 8;
	msg[4] = pbn & 0xff;
	drm_dp_crc_sideband_chunk_req(msg, 5);

	len += 6;

	dptx_dbg(dptx, "%s: Sending DOWN_REQ\n", __func__);
	dptx_write_bytes_to_dpcd(dptx, DP_SIDEBAND_MSG_DOWN_REQ_BASE,
				 buf, len);

	dptx_sideband_get_down_rep(dptx, DP_ALLOCATE_PAYLOAD, NULL);
	return 0;
}

static u32 dptx_calc_num_slots(struct dptx *dptx, int pbn)
{
	int div;
	int dp_link_bw = dptx_phy_rate_to_bw(dptx->link.rate);
	int dp_link_count = dptx->link.lanes;

	switch (dp_link_bw) {
	case DP_LINK_BW_1_62:
		div = 3 * dp_link_count;
		break;
	case DP_LINK_BW_2_7:
		div = 5 * dp_link_count;
		break;
	case DP_LINK_BW_5_4:
		div = 10 * dp_link_count;
		break;
	case DP_LINK_BW_8_1:
		div = 15 * dp_link_count;
		break;
	default:
		return 0;
	}

	return DIV_ROUND_UP(pbn, div);
}

static void dptx_audio_sfreq_based_on_edid(struct dptx *dptx, int edid_index)
{
	u8 sample_freq;
	struct audio_short_desc *audio_desc;

	audio_desc = &dptx->audio_desc;

	sample_freq = dptx->edid_second[edid_index + 2] & GENMASK(6, 0);

	if (sample_freq & BIT(0)) {
		dev_dbg(dptx->dev, "AUDIO EDID: Sink supports 32khz audio\n");
		audio_desc->max_sampling_freq = SAMPLE_FREQ_32;
	}

	if (sample_freq & BIT(1)) {
		dev_dbg(dptx->dev, "AUDIO EDID: Sink supports 44.1khz audio\n");
		audio_desc->max_sampling_freq = SAMPLE_FREQ_44_1;
	}

	if (sample_freq & BIT(2)) {
		dev_dbg(dptx->dev, "AUDIO EDID: Sink supports 48khz audio\n");
		audio_desc->max_sampling_freq = SAMPLE_FREQ_48;
	}

	if (sample_freq & BIT(3)) {
		dev_dbg(dptx->dev, "AUDIO EDID: Sink supports 88.2khz audio\n");
		audio_desc->max_sampling_freq = SAMPLE_FREQ_88_2;
	}

	if (sample_freq & BIT(4)) {
		dev_dbg(dptx->dev, "AUDIO EDID: Sink supports 96khz audio\n");
		audio_desc->max_sampling_freq = SAMPLE_FREQ_96;
	}

	if (sample_freq & BIT(5)) {
		dev_dbg(dptx->dev, "AUDIO EDID: Sink supports 176.4khz audio\n");
		audio_desc->max_sampling_freq = SAMPLE_FREQ_176_4;
	}
	if (sample_freq & BIT(6)) {
		dev_dbg(dptx->dev, "AUDIO EDID: Sink supports 192khz audio\n");
		audio_desc->max_sampling_freq = SAMPLE_FREQ_192;
	}
}

static void dptx_audio_bps_based_on_edid(struct dptx *dptx, int edid_index)
{
	u8 bpsample;
	struct audio_short_desc *audio_desc;

	audio_desc = &dptx->audio_desc;
	bpsample = dptx->edid_second[edid_index + 3] & GENMASK(2, 0);

	if (bpsample & BIT(0)) {
		dev_dbg(dptx->dev, "AUDIO EDID: Sink supports 16 bit audio\n");
		audio_desc->max_bit_per_sample = 16;
	}

	if (bpsample & BIT(1)) {
		dev_dbg(dptx->dev, "AUDIO EDID: Sink supports 20 bit audio\n");
		audio_desc->max_bit_per_sample = 20;
	}

	if (bpsample & BIT(2)) {
		dev_dbg(dptx->dev, "AUDIO EDID: Sink supports 24 bit audio\n");
		audio_desc->max_bit_per_sample = 24;
	}
}

static void dptx_fill_audio_short_desc(struct dptx *dptx, int edid_index)
{
	struct audio_short_desc *audio_desc;
//	u8 audio_block[4];
	u8 audio_data_size;
//	u8 num_of_channels, bpsample, sampleFreq;

	audio_desc = &dptx->audio_desc;

	audio_data_size = (dptx->edid_second[edid_index] & EDID_SIZE_MASK) >> EDID_SIZE_SHIFT;

	audio_desc->max_num_of_channels = (dptx->edid_second[edid_index + 1] & GENMASK(2, 0)) + 1;
	dev_dbg(dptx->dev, "AUDIO EDID: Sink supports up to %d channels\n", audio_desc->max_num_of_channels);

	/* Detect audio sampling frequency supported by the sink*/
	dptx_audio_sfreq_based_on_edid(dptx, edid_index);

	/* Detect bit per sample supported by the sink */
	dptx_audio_bps_based_on_edid(dptx, edid_index);
}

static void dptx_parse_established_timing(struct dptx *dptx)
{
	u8 byte1, byte2, byte3;

	byte1 = dptx->edid[35];
	byte2 = dptx->edid[36];
	byte3 = dptx->edid[37];

#ifndef PARSE_EST_TIMINGS_FROM_BYTE3

	// Parsing BYTE 1
	if (byte1 & ET1_800x600_60hz) {
		dev_dbg(dptx->dev, "Sink supports ET1_800x600_60hz\n");
		dptx->selected_est_timing = DMT_800x600_60hz;
		return;
	}

	if (byte1 & ET1_800x600_56hz)
		dev_dbg(dptx->dev, "Sink supports ET1_800x600_56hz, but we dont\n");

	if (byte1 & ET1_640x480_75hz)
		dev_dbg(dptx->dev, "Sink supports ET1_640x480_75hz, but we dont\n");

	if (byte1 & ET1_640x480_72hz)
		dev_dbg(dptx->dev, "Sink supports ET1_640x480_72hz, but we dont\n");

	if (byte1 & ET1_640x480_67hz)
		dev_dbg(dptx->dev, "Sink supports ET1_640x480_67hz, but we dont\n");

	if (byte1 & ET1_640x480_60hz) {
		dev_dbg(dptx->dev, "Sink supports ET1_640x480_60hz\n");
		dptx->selected_est_timing = DMT_640x480_60hz;
		return;
	}

	if (byte1 & ET1_720x400_88hz)
		dev_dbg(dptx->dev, "Sink supports ET1_720x400_88hz, but we dont\n");

	if (byte1 & ET1_720x400_70hz)
		dev_dbg(dptx->dev, "Sink supports ET1_720x400_70hz, but we dont\n");

	// Parsing BYTE 2
	if (byte2 & ET2_1280x1024_75hz)
		dev_dbg(dptx->dev, "Sink supports ET2_1280x1024_75hz, but we dont\n");

	if (byte2 & ET2_1024x768_75hz)
		dev_dbg(dptx->dev, "Sink supports ET2_1024x768_75hz, but we dont\n");

	if (byte2 & ET2_1024x768_70hz)
		dev_dbg(dptx->dev, "Sink supports ET2_1024x768_70hz, but we dont\n");

	if (byte2 & ET2_1024x768_60hz) {
		dev_dbg(dptx->dev, "Sink supports ET2_1024x768_60hz\n");
		dptx->selected_est_timing = DMT_1024x768_60hz;
		return;
	}

	if (byte2 & ET2_1024x768_87hz)
		dev_dbg(dptx->dev, "Sink supports ET2_1024x768_87hz, but we dont\n");

	if (byte2 & ET2_832x624_75hz)
		dev_dbg(dptx->dev, "Sink supports ET2_832x624_75hz, but we dont\n");

	if (byte2 & ET2_800x600_75hz)
		dev_dbg(dptx->dev, "Sink supports ET2_800x600_75hz, but we dont\n");

	if (byte2 & ET2_800x600_72hz)
		dev_dbg(dptx->dev, "Sink supports ET2_800x600_72hz, but we dont\n");

	// Parsing BYTE 3
	if (byte3 & ET3_1152x870_75hz)
		dev_dbg(dptx->dev, "Sink supports ET3_1152x870_75hz, but we dont\n");

#else

	// Parsing BYTE 3
	if (byte3 & ET3_1152x870_75hz)
		dev_dbg(dptx->dev, "Sink supports ET3_1152x870_75hz, but we dont\n");

	// Parsing BYTE 2
	if (byte2 & ET2_800x600_72hz)
		dev_dbg(dptx->dev, "Sink supports ET2_800x600_72hz, but we dont\n");

	if (byte2 & ET2_800x600_75hz)
		dev_dbg(dptx->dev, "Sink supports ET2_800x600_75hz, but we dont\n");

	if (byte2 & ET2_832x624_75hz)
		dev_dbg(dptx->dev, "Sink supports ET2_832x624_75hz, but we dont\n");

	if (byte2 & ET2_1024x768_87hz)
		dev_dbg(dptx->dev, "Sink supports ET2_1024x768_87hz, but we dont\n");

	if (byte2 & ET2_1024x768_60hz) {
		dev_dbg(dptx->dev, "Sink supports ET2_1024x768_60hz\n");
		dptx->selected_est_timing = DMT_1024x768_60hz;
		return;
	}

	if (byte2 & ET2_1024x768_70hz)
		dev_dbg(dptx->dev, "Sink supports ET2_1024x768_70hz, but we dont\n");

	if (byte2 & ET2_1024x768_75hz)
		dev_dbg(dptx->dev, "Sink supports ET2_1024x768_75hz, but we dont\n");

	if (byte2 & ET2_1280x1024_75hz)
		dev_dbg(dptx->dev, "Sink supports ET2_1280x1024_75hz, but we dont\n");

	// Parsing BYTE 1
	if (byte1 & ET1_720x400_70hz)
		dev_dbg(dptx->dev, "Sink supports ET1_720x400_70hz, but we dont\n");

	if (byte1 & ET1_720x400_88hz)
		dev_dbg(dptx->dev, "Sink supports ET1_720x400_88hz, but we dont\n");

	if (byte1 & ET1_640x480_60hz) {
		dev_dbg(dptx->dev, "Sink supports ET1_640x480_60hz\n");
		dptx->selected_est_timing = DMT_640x480_60hz;
		return;
	}

	if (byte1 & ET1_640x480_67hz)
		dev_dbg(dptx->dev, "Sink supports ET1_640x480_67hz, but we dont\n");

	if (byte1 & ET1_640x480_72hz)
		dev_dbg(dptx->dev, "Sink supports ET1_640x480_72hz, but we dont\n");

	if (byte1 & ET1_640x480_75hz)
		dev_dbg(dptx->dev, "Sink supports ET1_640x480_75hz, but we dont\n");

	if (byte1 & ET1_800x600_56hz)
		dev_dbg(dptx->dev, "Sink supports ET1_800x600_56hz, but we dont\n");

	if (byte1 & ET1_800x600_60hz) {
		dev_dbg(dptx->dev, "Sink supports ET1_800x600_60hz\n");
		dptx->selected_est_timing = DMT_800x600_60hz;
		return;
	}
#endif
}

static void dptx_check_detailed_timing_descriptors(struct dptx *dptx)
{
	dptx_dbg(dptx, "--- Prefered Timing Mode ---\n");
	dptx_dbg(dptx, "dptx->edid[54] = 0x%X, dptx->edid[55] = 0x%X,\n", dptx->edid[54], dptx->edid[55]);
	dptx_dbg(dptx, "dptx->edid[72] = 0x%X, dptx->edid[73] = 0x%X,\n", dptx->edid[72], dptx->edid[73]);
	if ((dptx->edid[54] == 0 && dptx->edid[55] == 0) && (dptx->edid[72] == 0 && dptx->edid[73] == 0)) {
		dev_err(dptx->dev, "%s FOUND EDID DUMMY BLOCKS\n", __func__);
		dev_err(dptx->dev, "%s: Going to parse established timings\n", __func__);
		dptx->dummy_dtds_present = true;
		dptx_parse_established_timing(dptx);
	}
}

static void dptx_parse_edid_audio_data_block(struct dptx *dptx)
{
	u8 byte;
	u8 tag, size;
	u8 edid_block1[128];
	int i, index;
	u8 offset;

	if (!dptx->edid_second)
		return;

	for (i = 0; i < 128; i++)
		edid_block1[i] = dptx->edid_second[i];

	// check CTA Extension Block's tag
	if (edid_block1[0] != CEA_EXT) {
		dptx_dbg(dptx, "Not a CEA extension block\n");
		return;
	}

	// check DTD offset
	offset = edid_block1[2];
	if (offset < 4 || offset > 127)
		return;

	// find the AUDIO_TAG before the DTD
	index = 4;
	while (index < offset) {
		byte = edid_block1[index];
		tag = (byte & EDID_TAG_MASK) >> EDID_TAG_SHIFT;
		size = (byte & EDID_SIZE_MASK) >> EDID_SIZE_SHIFT;

		if (tag == AUDIO_TAG) {
			dptx_fill_audio_short_desc(dptx, index);
			return;
		}

		index += size + 1;
	}
}

static int dptx_config_audio_based_on_edid(struct dptx *dptx)
{
	int retval;
//	u8 test_audio_mode;
	u8 audio_ch_count, orig_sample_freq, sample_freq, desc_audio_ch_count;
	u32 audio_clock_freq;
	struct audio_short_desc *audio_desc;
	enum audio_sample_freq audio_smaple_range;
	struct audio_params *aparams;

	audio_desc = &dptx->audio_desc;
	aparams = &dptx->aparams;

	audio_smaple_range = audio_desc->max_sampling_freq;
	desc_audio_ch_count = audio_desc->max_num_of_channels;

	/* Configure channel count */
	switch (desc_audio_ch_count) {
	case 1:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_CHANNEL1\n");
		audio_ch_count = 1;
		break;
	case 2:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_CHANNEL2\n");
		audio_ch_count = 2;
		break;
	case 3:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_CHANNEL3\n");
		audio_ch_count = 3;
		break;
	case 4:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_CHANNEL4\n");
		audio_ch_count = 4;
		break;
	case 5:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_CHANNEL5\n");
		audio_ch_count = 5;
		break;
	case 6:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_CHANNEL6\n");
		audio_ch_count = 6;
		break;
	case 7:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_CHANNEL7\n");
		audio_ch_count = 7;
		break;
	case 8:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_CHANNEL8\n");
		audio_ch_count = 8;
		break;
	default:
		dptx_dbg(dptx, "Invalid SHORT_AUDIO_DESC AUDIO_CHANNEL_COUNT\n");
		return -EINVAL;
	}
	dptx_dbg(dptx, "audio_ch_count = %d\n", audio_ch_count);
	aparams->num_channels = audio_ch_count;

	/* Configure sampling frequency */
	switch (audio_smaple_range) {
	case SAMPLE_FREQ_32:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_SAMPLING_RATE_32\n");
		orig_sample_freq = 12;
		sample_freq = 3;
		audio_clock_freq = 32000;
		break;
	case SAMPLE_FREQ_44_1:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_SAMPLING_RATE_44_1\n");
		orig_sample_freq = 15;
		sample_freq = 0;
		audio_clock_freq = 44100;
		break;
	case SAMPLE_FREQ_48:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_SAMPLING_RATE_48\n");
		orig_sample_freq = 13;
		sample_freq = 2;
		audio_clock_freq = 48000;
		break;
	case SAMPLE_FREQ_88_2:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_SAMPLING_RATE_88_2\n");
		orig_sample_freq = 7;
		sample_freq = 8;
		audio_clock_freq = 88200;
		break;
	case SAMPLE_FREQ_96:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_SAMPLING_RATE_96\n");
		orig_sample_freq = 5;
		sample_freq = 10;
		audio_clock_freq = 96000;
		break;
	case SAMPLE_FREQ_176_4:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_SAMPLING_RATE_176_4\n");
		orig_sample_freq = 3;
		sample_freq = 12;
		audio_clock_freq = 176400;
		break;
	case SAMPLE_FREQ_192:
		dptx_dbg(dptx, "SHORT AUDIO DESC AUDIO_SAMPLING_RATE_192\n");
		orig_sample_freq = 1;
		sample_freq = 14;
		audio_clock_freq = 192000;
		break;
	default:
		dptx_dbg(dptx, "Invalid SHORT AUDIO DESC AUDIO_SAMPLING_RATE\n");
		return -EINVAL;
	}
	dptx_dbg(dptx, "sample_freq = %d\n", sample_freq);
	dptx_dbg(dptx, "orig_sample_freq = %d\n", orig_sample_freq);

	/* Configure audio data width */
	aparams->data_width = audio_desc->max_bit_per_sample;
	dptx_dbg(dptx, "SHORT AUDIO DATA WIDTH = %d\n", aparams->data_width);
	dptx_audio_data_width_change(dptx);

	retval = 0; //TODO: configure_audio_mmcm(dptx, audio_clock_freq);
	if (retval)
		return retval;

	aparams->iec_samp_freq = sample_freq;
	aparams->iec_orig_samp_freq = orig_sample_freq;

	dptx_audio_num_ch_change(dptx);
	dptx_audio_samp_freq_config(dptx);
	dptx_audio_infoframe_sdp_en(dptx, 1);

	return retval;
}

static int dptx_dtd_fill_based_on_est_timings(struct dptx *dptx, struct dtd *mdtd)
{
	struct video_params *vparams = &dptx->vparams;

	switch (dptx->selected_est_timing) {
	case DMT_640x480_60hz:
		dev_err(dptx->dev, "Set Video mode to DMT 640x480\n");
		vparams->video_format = DMT;
		dptx_dtd_fill(mdtd, 4, vparams->refresh_rate,
			      vparams->video_format);
		return 0;
	case DMT_800x600_60hz:
		dev_err(dptx->dev, "Set Video mode to DMT 800x600\n");
		vparams->video_format = DMT;
		dptx_dtd_fill(mdtd, 9, vparams->refresh_rate,
			      vparams->video_format);
		return 0;
	case DMT_1024x768_60hz:
		dev_err(dptx->dev, "Set Video mode to DMT 1024x768\n");
		vparams->video_format = DMT;
		dptx_dtd_fill(mdtd, 16, vparams->refresh_rate,
			      vparams->video_format);
		return 0;
	case NONE:
	default:
		dev_err(dptx->dev, "%s: Not Found selected timing in Established timings\n", __func__);
		return -EINVAL;
	}
}

int handle_hotplug(struct dptx *dptx)
{
	u8 rev;
	u8 byte;
	int alpm_availability, retval;
	struct video_params *vparams;
	struct edp_alpm *alpm;
	struct drm_dp_sideband_msg_rx raw;
	struct drm_dp_sideband_msg_reply_body rep;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	memset(&raw, 0, sizeof(raw));
	memset(&rep, 0, sizeof(rep));

	vparams = &dptx->vparams;

	dptx_soft_reset(dptx, DPTX_SRST_CTRL_AUX);

	dptx_core_init_phy(dptx);

//----------------------------------------------
//	New Sequence
//----------------------------------------------

	// Disable Audio
	dptx_write_regfield(dptx, ctrl_fields->field_en_audio_stream_sdp_vertical_ctrl, 0x0);
	dptx_write_regfield(dptx, ctrl_fields->field_en_audio_stream_sdp_horizontal_ctrl, 0x0);
	dptx_write_regfield(dptx, ctrl_fields->field_en_audio_timestamp_sdp_vertical_ctrl, 0x0);
	dptx_write_regfield(dptx, ctrl_fields->field_en_audio_timestamp_sdp_horizontal_ctrl, 0x0);

	// Reset Audio Sampler
	dptx_write_regfield(dptx, ctrl_fields->field_audio_sampler_reset, 0x1);
	if (dptx->mst) {
		dptx_write_regfield(dptx, ctrl_fields->field_audio_sampler_reset_stream1, 0x1);
		dptx_write_regfield(dptx, ctrl_fields->field_audio_sampler_reset_stream2, 0x1);
		dptx_write_regfield(dptx, ctrl_fields->field_audio_sampler_reset_stream3, 0x1);
	}
	usleep_range(10, 20);
	dptx_write_regfield(dptx, ctrl_fields->field_audio_sampler_reset, 0x0);
	if (dptx->mst) {
		dptx_write_regfield(dptx, ctrl_fields->field_audio_sampler_reset_stream1, 0x0);
		dptx_write_regfield(dptx, ctrl_fields->field_audio_sampler_reset_stream2, 0x0);
		dptx_write_regfield(dptx, ctrl_fields->field_audio_sampler_reset_stream3, 0x0);
	}

	// Power on the sink
	retval = dptx_write_dpcd(dptx, DP_SET_POWER, DP_SET_POWER_D0);
	if (retval)
		dptx_warn(dptx, "DPCD Sink Power On failed\n");

	// Read Sink DPCD registers - Receiver Capability
	retval = dptx_read_dpcd(dptx, DP_DPCD_REV, &rev);
	if (retval)
		return retval;
	dptx_dbg(dptx, "DP Revision %x.%x\n", (rev & 0xf0) >> 4, rev & 0xf);

	memset(dptx->rx_caps, 0, DPTX_RECEIVER_CAP_SIZE);
	retval = dptx_read_bytes_from_dpcd(dptx, DP_DPCD_REV,
					   dptx->rx_caps,
					   DPTX_RECEIVER_CAP_SIZE);
	if (retval) {
		dptx_err(dptx, "DPCD Sink Capabilities: Not possible to retrieve.\n");
		return retval;
	}

	// Read Sink DPCD registers - Extended Receiver Capability
	if (dptx->rx_caps[DP_TRAINING_AUX_RD_INTERVAL] &
	    DP_EXTENDED_RECEIVER_CAPABILITY_FIELD_PRESENT) {
		retval = dptx_read_bytes_from_dpcd(dptx, 0x2200,
						   dptx->rx_caps,
						   DPTX_RECEIVER_CAP_SIZE);
		if (retval) {
			dptx_err(dptx, "DPCD Extended Sink Capabilities: Not possible to retrieve.\n");
			return retval;
		}
	}

	dptx_info(dptx, "RECEIVER CAPS:\n");
	print_hex_dump(KERN_INFO, "dwc_dptx: ", DUMP_PREFIX_NONE, 16, 1,
			dptx->rx_caps, DPTX_RECEIVER_CAP_SIZE, true);

	// Read sink count
	retval = dptx_read_dpcd(dptx, DP_SINK_COUNT, &byte);
	if (retval) {
		dptx_err(dptx, "Cannot read DP_SINK_COUNT\n");
		return retval;
	}

	dptx->sink_count = DP_GET_SINK_COUNT(byte);

	// Read DFP count
	if (drm_dp_is_branch(dptx->rx_caps)) {
		dptx->branch_dev = true;
		dptx->dfp_count = dptx->rx_caps[DP_DOWN_STREAM_PORT_COUNT] & DP_PORT_COUNT_MASK;
		retval = dptx_read_bytes_from_dpcd(dptx, DP_DOWNSTREAM_PORT_0, dptx->dfp_info,
						   DP_MAX_DOWNSTREAM_PORTS);
		if (retval)
			dptx_warn(dptx, "DP Branch Device: failed to read Downstream Port info\n");

		dptx_info(dptx, "DP Branch Device: DFP count = %d, sink count = %d\n",
			  dptx->dfp_count, dptx->sink_count);

		/* Check the sink count */
		if (dptx->sink_count > dptx->dfp_count + 1) {
			dptx_err(dptx, "DP Branch Device: invalid sink count\n");
			dptx->stats.sink_count_invalid_failures++;
			return -EINVAL;
		}

		/* Force MST capable branch device to SST mode */
		retval = dptx_read_dpcd(dptx, DP_MSTM_CAP, &byte);
		if (retval == 0) {
			dptx_info(dptx, "DP Branch Device: MST_CAP = %d\n", byte);
			if (byte & DP_MST_CAP) {
				/* MST_EN = 0, UP_REQ_EN = 0, UPSTREAM_IS_SRC = 1 */
				dptx_write_dpcd(dptx, DP_MSTM_CTRL, DP_UPSTREAM_IS_SRC);
				/* Use HPD IRQs for upstream notifications */
				dptx_write_dpcd(dptx, DP_BRANCH_DEVICE_CTRL,
						DP_BRANCH_DEVICE_IRQ_HPD);
			}
		} else {
			dptx_warn(dptx, "DP Branch Device: cannot read DP_MSTM_CAP\n");
		}
	} else {
		dptx->branch_dev = false;
		dptx->dfp_count = 0;
		dptx_info(dptx, "DP Sink: sink count = %d\n", dptx->sink_count);

		/* Check the sink count */
		if (dptx->sink_count != 1) {
			dptx_err(dptx, "DP Sink: invalid sink count\n");
			dptx->stats.sink_count_invalid_failures++;
			return -EINVAL;
		}
	}

	// Defer link training?
	if (dptx->sink_count == 0) {
		dptx_dbg_link(dptx, "--- DEFER LINK TRAINING ---\n");
		dptx_dbg_link(dptx, "DP Branch Device, sink count = 0\n");
		return 0;
	}

	return handle_hotplug_core(dptx);
}

static void dptx_check_audio_capability(struct dptx *dptx)
{
	struct cea_sad *sads;
	int num_of_sad;
	int i;

	dptx->sink_has_pcm_audio = false;

	num_of_sad = drm_edid_to_sad((struct edid *)dptx->edid, &sads);
	if (num_of_sad <= 0) {
		dptx_info(dptx, "sink doesn't support audio");
		return;
	}

	/* enum hdmi_audio_coding_type & enum hdmi_audio_sample_frequency are defined in hdmi.h */
	for (i = 0; i < num_of_sad; i++) {
		dptx_info(dptx,
			"EDID: SAD %d: fmt: 0x%02x, ch: 0x%02x, freq: 0x%02x, byte2: 0x%02x\n",
			i + 1, sads[i].format, sads[i].channels, sads[i].freq, sads[i].byte2);

		/* check sink support for PCM, 48 kHz, 16-bit, 2 channel audio */
		if (sads[i].format == HDMI_AUDIO_CODING_TYPE_PCM &&
			sads[i].channels >= 1 &&
			sads[i].freq & BIT(2) &&
			sads[i].byte2 & BIT(0)) {
			dptx->sink_has_pcm_audio = true;
			dptx_info(dptx, "sink supports PCM, 48 kHz, 16-bit, 2 channel audio");
			break;
		}
	}

	kfree(sads);
}

/* Increment stats counters based off DSC and FEC support */
static void dptx_stat_fec_dsc(struct dptx *dptx, bool dptx_fec, bool dptx_dsc)
{
	if (dptx_fec && dptx_dsc)
		dptx->stats.fec_dsc_supported++;
	else
		dptx->stats.fec_dsc_not_supported++;

}

static unsigned long dp_max_rate = DPTX_DEFAULT_LINK_RATE;
module_param(dp_max_rate, ulong, 0664);
MODULE_PARM_DESC(dp_max_rate, "maximum DP link rate");

static unsigned long dp_max_lanes = DPTX_DEFAULT_LINK_LANES;
module_param(dp_max_lanes, ulong, 0664);
MODULE_PARM_DESC(dp_max_lanes, "maximum DP link lanes");

static bool dp_fec;
module_param(dp_fec, bool, 0664);
MODULE_PARM_DESC(dp_fec, "Enable/disable DP FEC");

static bool dp_ssc = true;
module_param(dp_ssc, bool, 0664);
MODULE_PARM_DESC(dp_ssc, "Enable/disable DP SSC");

static bool dp_link_test_mode;
module_param(dp_link_test_mode, bool, 0664);
MODULE_PARM_DESC(dp_link_test_mode, "Enable/disable DP link test mode");

static bool dp_link_test_force_cr;
module_param(dp_link_test_force_cr, bool, 0664);
MODULE_PARM_DESC(dp_link_test_force_cr, "Force DP link training CR to pass");

static bool dp_link_test_force_cheq;
module_param(dp_link_test_force_cheq, bool, 0664);
MODULE_PARM_DESC(dp_link_test_force_cheq, "Force DP link training CHEQ to pass");

int handle_hotplug_core(struct dptx *dptx)
{
	u8 byte;
	int retval;
	int alpm_availability;
	struct edp_alpm *alpm;
	u8 sink_max_lanes;
	struct ctrl_regfields *ctrl_fields = dptx->ctrl_fields;

	dptx->dptx_max_res_store.hdisplay = 0;
	dptx->dptx_max_res_store.vdisplay = 0;

	// Read I2C speed DPCD register
	retval = dptx_read_dpcd(dptx, DP_I2C_SPEED_CAP, &byte);
	if (retval == -ETIMEDOUT) {
		dptx_warn(dptx, "I2C speed: AUX read timeout\n");
		return retval;
	} else if (retval) {
		dptx_warn(dptx, "I2C speed: AUX read retval=%d\n", retval);
	} else {
		dptx_info(dptx, "I2C speed = 0x%02x\n", byte);
	}

	// Read EDID of the sink using I2C over AUX, and parse the EDID data
	retval = dptx_read_edid(dptx);
	if (retval)
		goto done;

	if (!drm_edid_is_valid((struct edid *)dptx->edid)) {
		dptx->stats.edid_invalid_failures++;
		dptx_warn(dptx, "EDID data is corrupted\n");
	}

	dptx_check_detailed_timing_descriptors(dptx);
	dptx_parse_edid_audio_data_block(dptx);
	dptx_check_audio_capability(dptx);

	// Read FEC capability
	retval = dptx_read_dpcd(dptx, DP_FEC_CAPABILITY, &dptx->fec_caps);
	if (retval) {
		dptx_warn(dptx, "DPCD FEC Capabilities: Not possible to retrieve.\n");
		dptx->fec_caps = 0;
	}

	// Read DSC capability
	retval = dptx_read_dpcd(dptx, DP_DSC_SUPPORT, &dptx->dsc_caps);
	if (retval) {
		dptx_warn(dptx, "DPCD DSC Capabilities: Not possible to retrieve.\n");
		dptx->dsc_caps = 0;
	}

	/* Stats check if FEC/DSC is supported */
	dptx_stat_fec_dsc(dptx, !!(dptx->fec_caps & DP_FEC_CAPABLE), !!(dptx->dsc_caps &
		DP_DSC_DECOMPRESSION_IS_SUPPORTED));

	// Initialize ALPM variables
	alpm = &dptx->alpm;
	alpm_availability = alpm_is_available(dptx);
	if (alpm_availability)
		alpm->status = DISABLED;
	else
		alpm->status = NOT_AVAILABLE;

	/* Initialize max_rate and max_lanes */
	if (dp_max_rate > DPTX_PHYIF_CTRL_RATE_HBR3)
		dp_max_rate = DPTX_DEFAULT_LINK_RATE;
	dptx->max_rate = dp_max_rate;

	if (dp_max_lanes != 4 && dp_max_lanes != 2 && dp_max_lanes != 1)
		dp_max_lanes = DPTX_DEFAULT_LINK_LANES;
	dptx->max_lanes = dp_max_lanes;

	/* Initialize fec_en */
	dptx->fec_en = dp_fec;

	/* Initialize ssc_en */
	dptx->ssc_en = dp_ssc;

	/* Initialize link_test_mode */
	dptx->link_test_mode = dp_link_test_mode;
	dptx->link_test_force_cr = dp_link_test_force_cr;
	dptx->link_test_force_cheq = dp_link_test_force_cheq;

	/*
	 * When the host is limited in software to HBR and the sink supports
	 * only max 2 lanes, it leads to best-case link of HBR + 2 lanes.
	 * Such link can only support 1920x1200/1920x1080 resolutions, which
	 * is not an optimal experience.
	 *
	 * For max 2-lane devices, such as USB 3.0 hubs, let's bump the link
	 * speed to HBR2, so we can get HBR2 + 2 lanes link for 3440x1440 and
	 * 2560x1600/2560x1440 support.
	 */
	sink_max_lanes = drm_dp_max_lane_count(dptx->rx_caps);
	if (sink_max_lanes < 4 && dptx->max_rate == DPTX_PHYIF_CTRL_RATE_HBR) {
		dptx->max_rate = DPTX_PHYIF_CTRL_RATE_HBR2;
		dptx->max_lanes = 2;
	}

	// Program Sink DPCD Link Configuration registers
	retval = dptx_set_link_configs(dptx, dptx->max_rate, dptx->max_lanes);
	if (retval)
		goto done;

	// Initiate link training
	if (dptx->link.fec) {
		dptx_write_regfield(dptx, ctrl_fields->field_enhance_framing_en, 1);
		dptx_write_regfield(dptx, ctrl_fields->field_enhance_framing_with_fec_en, 1);

		// Set FEC_READY on the sink side
		retval = dptx_write_dpcd(dptx, DP_FEC_CONFIGURATION, DP_FEC_READY);
		if (retval) {
			dptx_err(dptx, "DP_FEC_CONFIGURATION write failed\n");
			dptx->link.fec = false;
			dptx->fec_caps = 0;
		}
	}

	if (!dptx->link.fec) {
		dptx_write_regfield(dptx, ctrl_fields->field_enhance_framing_with_fec_en, 0);
		dptx_write_regfield(dptx, ctrl_fields->field_enhance_framing_en, dptx->link.ef);
	}

	dptx_write_regfield(dptx, ctrl_fields->field_default_fast_link_train_en, 0);

	if (dptx->rx_caps[MAX_DOWNSPREAD] & NO_AUX_TRANSACTION_LINK_TRAINING)
		dptx_info(dptx, "Sink supports fast link training\n");

	retval = dptx_link_training(dptx);

	if (retval)
		goto done;

	msleep(20);

	if (dptx->link.fec) {
		// Enable FEC
		dptx_info(dptx, "Enabling FEC\n");
		dptx_write_regfield(dptx, ctrl_fields->field_enable_fec, 1);

		if (!dptx_read_dpcd(dptx, DP_FEC_STATUS, &byte))
			dptx_info(dptx, "FEC Status = %x\n", byte);
		else
			dptx_err(dptx, "DP_FEC_STATUS read failed\n");

		if (!dptx_read_dpcd(dptx, DP_FEC_ERROR_COUNT_LSB, &byte))
			dptx_info(dptx, "FEC Error Count = %x\n", byte);
		else
			dptx_err(dptx, "DP_FEC_ERROR_COUNT_LSB read failed\n");
	}

done:
	dptx->link.bpc = COLOR_DEPTH_INVALID;
	dptx_update_link_status(dptx, retval ? LINK_TRAINING_FAILURE : LINK_TRAINING_SUCCESS);

	if (!dptx->link_test_mode)
		drm_bridge_hpd_notify(&dptx->bridge, retval ?
				      connector_status_disconnected : connector_status_connected);

	return retval;
}

irqreturn_t dptx_threaded_irq(int irq, void *dev)
{
	int retval;
	struct dptx *dptx = dev;
	u32 hpdsts;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	dptx_dbg(dptx, "\n\n\n%s: >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n",
		 __func__);
	dptx_dbg(dptx, "%s:\n", __func__);

	mutex_lock(&dptx->mutex);

	hpdsts = dptx_read_reg(dptx, dptx->regs[DPTX], HPD_STATUS);
	dptx_dbg(dptx, "%s: HPDSTS = 0x%08x\n", __func__, hpdsts);

	/*
	 * TODO this should be set after all AUX transactions that are
	 * queued are aborted. Currently we don't queue AUX and AUX is
	 * only started from this function.
	 */
	atomic_set(&dptx->aux.abort, 0);

	if (atomic_read(&dptx->c_connect)) {
		atomic_set(&dptx->c_connect, 0);

		if (dptx_read_regfield(dptx, ctrl_fields->field_hpd_status))
			handle_hotplug(dptx);
		else
			handle_hotunplug(dptx);
	}

	if (atomic_read(&dptx->sink_request)) {
		atomic_set(&dptx->sink_request, 0);
		retval = handle_sink_request(dptx);
		if (retval)
			dptx_err(dptx, "Unable to handle sink request %d\n",
				 retval);

		dptx_connection_result_update(dptx, !retval);
	}

	dptx_dbg(dptx, "%s: DONE\n", __func__);
	dptx_dbg(dptx, "%s: =======================================\n\n",
		 __func__);

	mutex_unlock(&dptx->mutex);

	return IRQ_HANDLED;
}

static void handle_hpd_irq(struct dptx *dptx)
{
	dptx_dbg(dptx, "%s: HPD_IRQ\n", __func__);
	atomic_set(&dptx->sink_request, 1);
	dptx_notify(dptx);
}

irqreturn_t dptx_irq(int irq, void *dev)
{
	irqreturn_t retval = IRQ_HANDLED;
	struct dptx *dptx = dev;
	struct ctrl_regfields *ctrl_fields;
	u32 ists;

	ctrl_fields = dptx->ctrl_fields;

	ists = dptx_read_reg(dptx, dptx->regs[DPTX], GENERAL_INTERRUPT);
	dptx_dbg_irq(dptx, "%s: >>>> ISTS=0x%08x\n", __func__, ists);

	if (!(ists & DPTX_ISTS_ALL_INTR)) {
		retval = IRQ_NONE;
		dptx_dbg(dptx, "%s: IRQ_NONE\n", __func__);
		goto done;
	}

	if (dptx_read_regfield(dptx, ctrl_fields->field_hdcp_event)) {
		dptx_dbg(dptx, "%s: DPTX_ISTS_HDCP\n", __func__);
	}

	if (dptx_read_regfield(dptx, ctrl_fields->field_sdp_event_stream0)) {
		dptx_dbg(dptx, "%s: DPTX_ISTS_SDP\n", __func__);
		/* TODO Handle and clear */
	}

	if (dptx_read_regfield(dptx, ctrl_fields->field_audio_fifo_overflow_stream0)) {
		if (dptx_read_regfield(dptx, ctrl_fields->field_audio_fifo_overflow_en_stream0)) {
			dptx_dbg(dptx, "%s: DPTX_ISTS_AUDIO_FIFO_OVERFLOW\n",
				 __func__);
			dptx_write_regfield(dptx, ctrl_fields->field_audio_fifo_overflow_stream0, 1);
		}
	}

	if (dptx_read_regfield(dptx, ctrl_fields->field_video_fifo_overflow_stream0)) {
		if (dptx_read_regfield(dptx, ctrl_fields->field_video_fifo_overflow_en_stream0)) {
			dptx_dbg(dptx, "%s: DPTX_ISTS_VIDEO_FIFO_OVERFLOW\n",
				 __func__);
			dptx_write_regfield(dptx, ctrl_fields->field_video_fifo_overflow_stream0, 1);
		}
	}

	if (dptx_read_regfield(dptx, ctrl_fields->field_hpd_event)) {
		u32 hpdsts;

		dptx_dbg(dptx, "%s: HPD_EVENT\n", __func__);
		hpdsts = dptx_read_reg(dptx, dptx->regs[DPTX], HPD_STATUS);

		dptx_dbg(dptx, "%s: HPDSTS = 0x%08x\n", __func__, hpdsts);

		if (dptx_read_regfield(dptx, ctrl_fields->field_hpd_irq)) {
			dptx_dbg(dptx, "%s: DPTX_HPDSTS_IRQ\n", __func__);
			dptx_write_regfield(dptx, ctrl_fields->field_hpd_irq, 1);
			handle_hpd_irq(dptx);
			retval = IRQ_WAKE_THREAD;
		}

		if (dptx_read_regfield(dptx, ctrl_fields->field_hpd_hot_plug)) {
			dptx_dbg(dptx, "%s: HPD_STATUS - Hot Plug Detected\n", __func__);

			//dptx_write_regfield(dptx, ctrl_fields->field_phy_powerdown, 0);
			//dptx_write_regfield(dptx, ctrl_fields->field_hpd_hot_plug, 1);
			hpdsts = dptx_read_regfield(dptx, ctrl_fields->field_hpd_hot_plug);
			dptx_dbg(dptx, "%s: Read Regfield Hot Plug = 0x%04x\n", __func__, hpdsts);
			hpdsts |= BIT(1);
			dptx_write_reg(dptx, dptx->regs[DPTX], HPD_STATUS, hpdsts);

			//dptx_write_regfield(dptx, ctrl_fields->field_hpd_hot_plug, 1);

			//hpdsts = dptx_read_reg(dptx, dptx->regs[DPTX], HPD_STATUS);
			//dptx_dbg(dptx, "%s: After Reset HPDSTS = 0x%08x\n", __func__, hpdsts);
			hpdsts = dptx_read_regfield(dptx, ctrl_fields->field_hpd_hot_plug);
			dptx_dbg(dptx, "%s: Read Regfield Hot Plug - After Clean = 0x%04x\n", __func__, hpdsts);

			atomic_set(&dptx->aux.abort, 1);
			atomic_set(&dptx->c_connect, 1);
			dptx_notify(dptx);
			retval = IRQ_WAKE_THREAD;
		}

		if (dptx_read_regfield(dptx, ctrl_fields->field_hpd_hot_unplug)) {
			dptx_dbg(dptx, "%s: DPTX_HPDSTS_HOT_UNPLUG\n",
				 __func__);
			dptx_write_regfield(dptx, ctrl_fields->field_hpd_hot_unplug, 1);
			atomic_set(&dptx->aux.abort, 1);
			atomic_set(&dptx->c_connect, 1);
			dptx_notify(dptx);
			retval = IRQ_WAKE_THREAD;
		}
	}

done:
	dptx_dbg_irq(dptx, "%s: <<<<\n", __func__);
	return retval;
}
