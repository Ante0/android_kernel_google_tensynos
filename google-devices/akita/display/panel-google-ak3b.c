// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPI-DSI based ak3b AMOLED LCD panel driver.
 *
 * Copyright (c) 2023 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <drm/display/drm_dsc_helper.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <video/mipi_display.h>

#include "include/trace/dpu_trace.h"
#include "panel/panel-samsung-drv.h"

static const struct drm_dsc_config pps_config = {
	.line_buf_depth = 9,
	.bits_per_component = 8,
	.convert_rgb = true,
	.slice_width = 540,
	.slice_height = 48,
	.simple_422 = false,
	.pic_width = 1080,
	.pic_height = 2400,
	.rc_tgt_offset_high = 3,
	.rc_tgt_offset_low = 3,
	.bits_per_pixel = 128,
	.rc_edge_factor = 6,
	.rc_quant_incr_limit1 = 11,
	.rc_quant_incr_limit0 = 11,
	.initial_xmit_delay = 512,
	.initial_dec_delay = 526,
	.block_pred_enable = true,
	.first_line_bpg_offset = 12,
	.initial_offset = 6144,
	.rc_buf_thresh = {
		14, 28, 42, 56,
		70, 84, 98, 105,
		112, 119, 121, 123,
		125, 126
	},
	.rc_range_params = {
		{.range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 2},
		{.range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 0},
		{.range_min_qp = 1, .range_max_qp = 5, .range_bpg_offset = 0},
		{.range_min_qp = 1, .range_max_qp = 6, .range_bpg_offset = 62},
		{.range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 60},
		{.range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 58},
		{.range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 56},
		{.range_min_qp = 3, .range_max_qp = 8, .range_bpg_offset = 56},
		{.range_min_qp = 3, .range_max_qp = 9, .range_bpg_offset = 56},
		{.range_min_qp = 3, .range_max_qp = 10, .range_bpg_offset = 54},
		{.range_min_qp = 5, .range_max_qp = 11, .range_bpg_offset = 54},
		{.range_min_qp = 5, .range_max_qp = 12, .range_bpg_offset = 52},
		{.range_min_qp = 5, .range_max_qp = 13, .range_bpg_offset = 52},
		{.range_min_qp = 7, .range_max_qp = 13, .range_bpg_offset = 52},
		{.range_min_qp = 13, .range_max_qp = 15, .range_bpg_offset = 52}
	},
	.rc_model_size = 8192,
	.flatness_min_qp = 3,
	.flatness_max_qp = 12,
	.initial_scale_value = 32,
	.scale_decrement_interval = 7,
	.scale_increment_interval = 1190,
	.nfl_bpg_offset = 523,
	.slice_bpg_offset = 543,
	.final_offset = 4336,
	.vbr_enable = false,
	.slice_chunk_size = 540,
	.dsc_version_minor = 1,
	.dsc_version_major = 1,
	.native_422 = false,
	.native_420 = false,
	.second_line_bpg_offset = 0,
	.nsl_bpg_offset = 0,
	.second_line_offset_adj = 0,
};

#define AK3B_WRCTRLD_DIMMING_BIT    0x08
#define AK3B_WRCTRLD_BCTRL_BIT      0x20
#define AK3B_WRCTRLD_HBM_BIT        0xC0
#define AK3B_WRCTRLD_LOCAL_HBM_BIT  0x10

#define LHBM_RGB_RATIO_SIZE 3

static const u8 test_key_on_f0[] = { 0xF0, 0x5A, 0x5A };
static const u8 test_key_off_f0[] = { 0xF0, 0xA5, 0xA5 };
static const u8 freq_update[] = { 0xF7, 0x0F };

#define FREQUENCY_COUNT 3
#define LHBM_BRIGHTNESS_INDEX_SIZE 4

static const u8 lhbm_brightness_write_index[FREQUENCY_COUNT][LHBM_BRIGHTNESS_INDEX_SIZE] = {
	{ 0xB0, 0x03, 0xD7, 0x66 }, /* HS120 */
	{ 0xB0, 0x03, 0xDC, 0x66 }, /* HS60 */
	{ 0xB0, 0x03, 0xE6, 0x66 }  /* NS60 */
};

static const u8 lhbm_brightness_read_index[FREQUENCY_COUNT][LHBM_BRIGHTNESS_INDEX_SIZE] = {
	{ 0xB0, 0x00, 0x22, 0xD8 }, /* HS120 */
	{ 0xB0, 0x00, 0x18, 0xD8 }, /* HS60 */
	{ 0xB0, 0x00, 0x1D, 0xD8 }  /* NS60 */
};

static const u8 lhbm_brightness_write_reg = 0x66;
static const u8 lhbm_brightness_read_reg = 0xD8;

static const struct exynos_dsi_cmd ak3b_off_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_DISPLAY_OFF),
	EXYNOS_DSI_CMD_SEQ_DELAY(120, MIPI_DCS_ENTER_SLEEP_MODE), /* sleep in */
};
static DEFINE_EXYNOS_CMD_SET(ak3b_off);

static const struct exynos_dsi_cmd ak3b_lp_cmds[] = {
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x5A, 0x5A), /* test_key_on_f0 */
	EXYNOS_DSI_CMD_SEQ(0xB9, 0x30), /* TE_SELECT 60Hz */
	EXYNOS_DSI_CMD_SEQ(0xF7, 0x0F), /* freq_update */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0xA5, 0xA5), /* test_key_off_f0 */
};
static DEFINE_EXYNOS_CMD_SET(ak3b_lp);

static const struct exynos_dsi_cmd ak3b_lp_low_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(17, 0x53, 0x25), /* AOD 10 nit */
};

static const struct exynos_dsi_cmd ak3b_lp_high_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY(17, 0x53, 0x24), /* AOD 50 nit */
};

static const struct exynos_binned_lp ak3b_binned_lp[] = {
	/* rising time = delay = 12, falling time = delay + width = 12 + 35 */
	BINNED_LP_MODE_TIMING("low", 813, ak3b_lp_low_cmds, 12, 12 + 35), /* 40 nits */
	BINNED_LP_MODE_TIMING("high", 3175, ak3b_lp_high_cmds, 12, 12 + 35)
};

static const struct exynos_dsi_cmd ak3b_init_cmds[] = {
	EXYNOS_DSI_CMD_SEQ_DELAY_REV(PANEL_REV_LT(PANEL_REV_EVT1), 120, MIPI_DCS_EXIT_SLEEP_MODE),

	EXYNOS_DSI_CMD_SEQ_DELAY_REV(PANEL_REV_GE(PANEL_REV_EVT1), 10, MIPI_DCS_EXIT_SLEEP_MODE),

	/* GPO_DC Setting(HS 60Hz) */
	EXYNOS_DSI_CMD0_REV(test_key_on_f0, PANEL_REV_GE(PANEL_REV_EVT1)),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_GE(PANEL_REV_EVT1),
		0xB0, 0x00, 0x1F, 0xCB), /* Global para */
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_GE(PANEL_REV_EVT1), 0xCB, 0xFF), /* GPO_DC Setting */
	EXYNOS_DSI_CMD0_REV(freq_update, PANEL_REV_GE(PANEL_REV_EVT1)),
	/* Delay 110 ms after sending GPO_DC settings */
	EXYNOS_DSI_CMD_REV(test_key_off_f0, 110, PANEL_REV_GE(PANEL_REV_EVT1)),

	/* Frequencey settings */
	EXYNOS_DSI_CMD0_REV(test_key_on_f0, PANEL_REV_GE(PANEL_REV_PROTO1_1)),
	EXYNOS_DSI_CMD_SEQ_REV(PANEL_REV_GE(PANEL_REV_PROTO1_1), 0x60, 0x08, 0x00), /* 60 hz HS */
	EXYNOS_DSI_CMD0_REV(freq_update, PANEL_REV_GE(PANEL_REV_PROTO1_1)),
	EXYNOS_DSI_CMD0_REV(test_key_off_f0, PANEL_REV_GE(PANEL_REV_PROTO1_1)),

	/* b/241726710 - remove the parameter to avoid MIPI DSIM timeout */
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_TEAR_ON),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37),
	EXYNOS_DSI_CMD_SEQ(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x5F),
};
static DEFINE_EXYNOS_CMD_SET(ak3b_init);

static const struct exynos_dsi_cmd ak3b_lhbm_location_cmds[] = {
	/* test_key_on_f0 */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0x5A, 0x5A),
	/* test_key_on_f1 */
	EXYNOS_DSI_CMD_SEQ(0xF1, 0x5A, 0x5A),

	/* global para */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x07, 0x6D),
	/* box location */
	EXYNOS_DSI_CMD_SEQ(0x6D, 0xCC),
	/* global para */
	EXYNOS_DSI_CMD_SEQ(0xB0, 0x00, 0x08, 0x6D),
	/* center position set, x: 0x21C, y: 0x6B9, size: 0x63 */
	EXYNOS_DSI_CMD_SEQ(0x6D, 0x21, 0xC6, 0xB9, 0x63),

	/* test_key_off_f1 */
	EXYNOS_DSI_CMD_SEQ(0xF1, 0xA5, 0xA5),
	/* test_key_off_f0 */
	EXYNOS_DSI_CMD_SEQ(0xF0, 0xA5, 0xA5)
};
static DEFINE_EXYNOS_CMD_SET(ak3b_lhbm_location);

#define LHBM_GAMMA_CMD_SIZE 6

#define LHBM_RATIO_SIZE 3

/**
 * enum ak3b_lhbm_brt - local hbm brightness
 * @LHBM_R_COARSE: red coarse
 * @LHBM_GB_COARSE: green and blue coarse
 * @LHBM_R_FINE: red fine
 * @LHBM_G_FINE: green fine
 * @LHBM_B_FINE: blue fine
 * @LHBM_BRT_LEN: local hbm brightness array length
 */
enum ak3b_lhbm_brt {
	LHBM_R_COARSE = 0,
	LHBM_GB_COARSE,
	LHBM_R_FINE,
	LHBM_G_FINE,
	LHBM_B_FINE,
	LHBM_BRT_LEN
};

/**
 * enum ak3b_lhbm_brt_overdrive_group - lhbm brightness overdrive group number
 * @LHBM_OVERDRIVE_GRP_0_NIT: group number for 0 nit
 * @LHBM_OVERDRIVE_GRP_6_NIT: group number for 0-6 nits
 * @LHBM_OVERDRIVE_GRP_50_NIT: group number for 6-50 nits
 * @LHBM_OVERDRIVE_GRP_300_NIT: group number for 50-300 nits
 * @LHBM_OVERDRIVE_GRP_MAX: maximum group number
 */
enum ak3b_lhbm_brt_overdrive_group {
	LHBM_OVERDRIVE_GRP_0_NIT = 0,
	LHBM_OVERDRIVE_GRP_6_NIT,
	LHBM_OVERDRIVE_GRP_50_NIT,
	LHBM_OVERDRIVE_GRP_300_NIT,
	LHBM_OVERDRIVE_GRP_MAX
};

/* actual ratio = value / (10^9) */
u32 lhbm_rgb_ratio[LHBM_OVERDRIVE_GRP_MAX][LHBM_RATIO_SIZE] = {
	{1072323183, 1066226349, 1072461770},
	{1028055932, 1025725193, 1028108843},
	{1023112911, 1021192812, 1023156500},
	{1018876770, 1017326961, 1018911915},
};

struct ak3b_lhbm_ctl {
	/** @brt_normal: normal LHBM brightness parameters */
	u8 brt_normal[FREQUENCY_COUNT][LHBM_BRT_LEN];
	/** @brt_overdrive: overdrive LHBM brightness parameters */
	u8 brt_overdrive[FREQUENCY_COUNT][LHBM_OVERDRIVE_GRP_MAX][LHBM_BRT_LEN];
	/** @overdrived: whether LHBM is overdrived */
	bool overdrived;
	/** @hist_roi_configured: whether LHBM histogram configuration is done */
	bool hist_roi_configured;
};

/**
 * struct ak3b_panel - panel specific runtime info
 *
 * This struct maintains ak3b panel specific runtime info, any fixed details about panel
 * should most likely go into struct exynos_panel_desc
 */
struct ak3b_panel {
	/** @base: base panel struct */
	struct exynos_panel base;

	/** @local_hbm_gamma: lhbm gamma data */
	struct local_hbm_gamma {
		u8 hs120_cmd[LHBM_GAMMA_CMD_SIZE];
		u8 hs60_cmd[LHBM_GAMMA_CMD_SIZE];
		u8 ns_cmd[LHBM_GAMMA_CMD_SIZE];
		u8 aod_cmd[LHBM_GAMMA_CMD_SIZE];
	} local_hbm_gamma;

	/** @lhbm_ctl: lhbm brightness control */
	struct ak3b_lhbm_ctl lhbm_ctl;

	/** @needs_display_on: if display_on command needs to send after flip done */
	bool needs_display_on;
};

#define to_spanel(ctx) container_of(ctx, struct ak3b_panel, base)

enum frequency { HS120, HS60, NS60, AOD };
static const char* frequency_str[] = { "HS120", "HS60", "NS60", "AOD" };

static u8 get_lhbm_read_cmd(struct exynos_panel *ctx, enum frequency freq) {
	switch(freq) {
	case HS120:
		return 0x22;
	case HS60:
	case AOD:
		return 0x18;
	case NS60:
		return 0x1D;
	default:
		dev_err(ctx->dev, "LHBM Gamma read for unknown frequency %d\n", freq);
		return 0x22;
	}
}

static void read_lhbm_gamma(struct exynos_panel *ctx, u8 *cmd, enum frequency freq) {
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	u8 index = get_lhbm_read_cmd(ctx, freq);
	int ret;

	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0xB0, 0x00, index, 0xD8); /* global para */
	ret = mipi_dsi_dcs_read(dsi, 0xD8, cmd + 1, LHBM_GAMMA_CMD_SIZE - 1);

	if (ret != (LHBM_GAMMA_CMD_SIZE - 1)) {
		dev_err(ctx->dev, "fail to read LHBM gamma for %s\n", frequency_str[freq]);
		return;
	}

	/* fill in gamma write command 0x66 in offset 0 */
	cmd[0] = 0x66;
	dev_info(ctx->dev, "%s_gamma: %*ph\n", frequency_str[freq],
		LHBM_GAMMA_CMD_SIZE - 1, cmd + 1);
}

static void ak3b_lhbm_gamma_read(struct exynos_panel *ctx)
{
	struct ak3b_panel *spanel = to_spanel(ctx);

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);

	read_lhbm_gamma(ctx, spanel->local_hbm_gamma.hs120_cmd, HS120);
	read_lhbm_gamma(ctx, spanel->local_hbm_gamma.ns_cmd, NS60);

	if (ctx->panel_rev == PANEL_REV_PROTO1) {
		read_lhbm_gamma(ctx, spanel->local_hbm_gamma.aod_cmd, AOD);
	} else {
		read_lhbm_gamma(ctx, spanel->local_hbm_gamma.hs60_cmd, HS60);
	}

	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);
}

static void ak3b_lhbm_gamma_write(struct exynos_panel *ctx)
{
	struct ak3b_panel *spanel = to_spanel(ctx);
	const u8 hs120_cmd = spanel->local_hbm_gamma.hs120_cmd[0];
	const u8 hs60_cmd = spanel->local_hbm_gamma.hs60_cmd[0];
	const u8 ns_cmd = spanel->local_hbm_gamma.ns_cmd[0];
	const u8 aod_cmd = spanel->local_hbm_gamma.aod_cmd[0];

	if (!hs120_cmd && !hs60_cmd && !ns_cmd && !aod_cmd) {
		dev_err(ctx->dev, "%s: no lhbm gamma!\n", __func__);
		return;
	}

	dev_dbg(ctx->dev, "%s\n", __func__);
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);

	if (hs120_cmd) {
		/* HS120 */
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x03, 0xD7, 0x66); /* global para */
		EXYNOS_DCS_BUF_ADD_SET(ctx, spanel->local_hbm_gamma.hs120_cmd); /* write gamma */

		/* HS60 */
		if (ctx->panel_rev == PANEL_REV_PROTO1) {
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x03, 0xDC, 0x66); /* global para */
			EXYNOS_DCS_BUF_ADD_SET(ctx,
				spanel->local_hbm_gamma.hs120_cmd); /* write gamma */
		}
	}

	if (hs60_cmd) {
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x03, 0xDC, 0x66); /* global para */
		EXYNOS_DCS_BUF_ADD_SET(ctx, spanel->local_hbm_gamma.hs60_cmd); /* write gamma */
	}

	if (ns_cmd) {
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x03, 0xE6, 0x66); /* global para */
		EXYNOS_DCS_BUF_ADD_SET(ctx, spanel->local_hbm_gamma.ns_cmd); /* write gamma */
	}

	if (aod_cmd) {
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x03, 0xEB, 0x66); /* global para */
		EXYNOS_DCS_BUF_ADD_SET(ctx, spanel->local_hbm_gamma.aod_cmd); /* write gamma */
	}

	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);
}

static void ak3b_send_aod_without_blink_settings(struct exynos_panel *ctx)
{
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x03, 0xBB);
	EXYNOS_DCS_BUF_ADD(ctx, 0xBB, 0x01, 0x0C);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);
}

static void ak3b_get_te2_setting(struct exynos_panel_te2_timing *timing,
				    u8 *setting)
{
	u8 delay_low_byte, delay_high_byte;
	u8 width_low_byte, width_high_byte;
	u32 rising, falling;

	if (!timing || !setting)
		return;

	rising = timing->rising_edge;
	falling = timing->falling_edge;

	delay_low_byte = rising & 0xFF;
	delay_high_byte = (rising >> 8) & 0xF;
	width_low_byte = (falling - rising) & 0xFF;
	width_high_byte = ((falling - rising) >> 8) & 0xF;

	setting[0] = (delay_high_byte << 4) | width_high_byte;
	setting[1] = delay_low_byte;
	setting[2] = width_low_byte;
}

static void ak3b_update_te2(struct exynos_panel *ctx)
{
	struct exynos_panel_te2_timing timing;
	u8 hs_120hz_setting[4] = {0xCB, 0x00, 0x0C, 0x32};
	u8 hs_60hz_setting[4] = {0xCB, 0x00, 0x0C, 0x32};
	u8 lp_setting[4] = {0xCB, 0x00, 0x0C, 0x23}; // lp low/high

	if (!ctx)
		return;

	/* HS mode */
	timing = ctx->te2.mode_data[0].timing;
	ak3b_get_te2_setting(&timing, &hs_120hz_setting[1]);

	dev_dbg(ctx->dev, "TE2 updated HS 120Hz: [HEX] %*ph\n", 4, hs_120hz_setting);

	timing = ctx->te2.mode_data[1].timing;
	ak3b_get_te2_setting(&timing, &hs_60hz_setting[1]);

	dev_dbg(ctx->dev, "TE2 updated HS 60Hz: [HEX] %*ph\n", 4, hs_60hz_setting);

	/* LP mode */
	if (ctx->current_mode->exynos_mode.is_lp_mode) {
		int ret = exynos_panel_get_current_mode_te2(ctx, &timing);

		if (!ret)
			ak3b_get_te2_setting(&timing, &lp_setting[1]);
		else if (ret == -EAGAIN)
			dev_dbg(ctx->dev,
				"Panel is not ready, use default setting\n");
		else
			return;

		dev_dbg(ctx->dev, "TE2 updated LP: [HEX] %*ph\n", 4, lp_setting);
	}

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x2B, 0xF2); /* global para */
	EXYNOS_DCS_BUF_ADD(ctx, 0xF2, 0x03, 0x14); /* TE2 on */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x73, 0xCB); /* global para */
	EXYNOS_DCS_BUF_ADD_SET(ctx, hs_120hz_setting); /* HS 120Hz control */
	EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0xC9, 0xCB); /* global para */
	EXYNOS_DCS_BUF_ADD_SET(ctx, hs_60hz_setting); /* HS 60Hz control */
	if (ctx->current_mode->exynos_mode.is_lp_mode) {
		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x01, 0xC8, 0xCB); /* global para */
		EXYNOS_DCS_BUF_ADD_SET(ctx, lp_setting); /* HLPM mode */
	}
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update); /* LTPS update */
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);
}

static enum frequency ak3b_get_frequency(struct exynos_panel *ctx)
{
	u32 vrefresh = drm_mode_vrefresh(&ctx->current_mode->mode);

	if (ctx->current_mode->exynos_mode.is_lp_mode)
		return AOD;
	else if (vrefresh == 120)
		return HS120;
	else if (ctx->panel_rev <= PANEL_REV_PROTO1)
		return HS60;

	return ctx->op_hz == 60 ? NS60 : HS60;
}

static void ak3b_change_frequency(struct exynos_panel *ctx,
				    const unsigned int vrefresh)
{
	const u8 hs60_setting[3] = {0x60, 0x08, 0x00};
	const u8 ns60_setting[3] = {0x60, 0x18, 0x00};
	const u8 hs120_setting[3] = {0x60, 0x00, 0x00};

	if (unlikely(!ctx))
		return;

	if (vrefresh != 60 && vrefresh != 120) {
		dev_warn(ctx->dev, "%s: invalid refresh rate %uhz\n", __func__, vrefresh);
		return;
	}

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	if (vrefresh == 120) {
		ctx->op_hz = 120;
		EXYNOS_DCS_BUF_ADD_SET(ctx, hs120_setting);
		EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x31);
	} else {
		if (ctx->op_hz == 120)
			EXYNOS_DCS_BUF_ADD_SET(ctx, hs60_setting);
		else
			EXYNOS_DCS_BUF_ADD_SET(ctx, ns60_setting);

		EXYNOS_DCS_BUF_ADD(ctx, 0xB9, 0x30);
	}
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);

	dev_dbg(ctx->dev, "%s: change to %uhz\n", __func__, vrefresh);
}

static void buf_add_frequency_select_cmd(struct exynos_panel *ctx) {
	u32 vrefresh = drm_mode_vrefresh(&ctx->current_mode->mode);
	/* NS60: 0x18, HS120: 0x00, HS60: 0x08 */
	u8 index = 0x18;
	if (vrefresh == 120) {
		index = 0x00;
		ctx->op_hz = 120;
	} else if (ctx->op_hz != 60) {
		index = 0x08;
	} else if (ctx->panel_rev <= PANEL_REV_PROTO1) {
		index = 0x08; /* NS60 is treated HS60 for Proto 1.0 */
	}

	EXYNOS_DCS_BUF_ADD(ctx, 0x60, index, 0x00);
}

static int ak3b_set_op_hz(struct exynos_panel *ctx, unsigned int hz)
{
	const unsigned int vrefresh = drm_mode_vrefresh(&ctx->current_mode->mode);

	if ((vrefresh > hz) || ((hz != 60) && (hz != 120))) {
		dev_err(ctx->dev, "invalid op_hz=%u for vrefresh=%u\n",
			hz, vrefresh);
		return -EINVAL;
	}

	ctx->op_hz = hz;

	if (ctx->panel_rev <= PANEL_REV_PROTO1) {
		dev_info(ctx->dev, "Panel revision %d always operates at op_hz = 120\n",
			ctx->panel_rev);
		return 0;
	}

	DPU_ATRACE_BEGIN(__func__);

	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	buf_add_frequency_select_cmd(ctx);
	EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);

	dev_info(ctx->dev, "set op_hz at %u\n", hz);

	DPU_ATRACE_END(__func__);

	return 0;
}

static void ak3b_update_lhbm_hist_config(struct exynos_panel *ctx)
{
	struct ak3b_panel *spanel = to_spanel(ctx);
	struct ak3b_lhbm_ctl *ctl = &spanel->lhbm_ctl;
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	const struct drm_display_mode *mode;

	/* From b/270088287#comment1 lhbm center below the center of AA: 521, radius: 99 */
	const int d = 521, r = 99;

	if (ctl->hist_roi_configured)
		return;

	if (!pmode) {
		dev_err(ctx->dev, "no current mode set\n");
		return;
	}
	mode = &pmode->mode;
	if (!exynos_drm_connector_set_lhbm_hist(&ctx->exynos_connector,
		mode->hdisplay, mode->vdisplay, d, r)) {
		ctl->hist_roi_configured = true;
		dev_dbg(ctx->dev, "configure lhbm hist: %d %d %d %d\n",
			mode->hdisplay, mode->vdisplay, d, r);
	}
}

static int ak3b_atomic_check(struct exynos_panel *ctx, struct drm_atomic_state *state)
{
	struct drm_connector *conn = &ctx->exynos_connector.base;
	struct drm_connector_state *new_conn_state =
					drm_atomic_get_new_connector_state(state, conn);
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;

	ak3b_update_lhbm_hist_config(ctx);

	if (!ctx->current_mode || drm_mode_vrefresh(&ctx->current_mode->mode) == 120 ||
	    !new_conn_state || !new_conn_state->crtc)
		return 0;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);
	old_crtc_state = drm_atomic_get_old_crtc_state(state, new_conn_state->crtc);
	if (!old_crtc_state || !new_crtc_state || !new_crtc_state->active)
		return 0;

	if (!drm_atomic_crtc_effectively_active(old_crtc_state)) {
		struct drm_display_mode *mode = &new_crtc_state->adjusted_mode;

		mode->clock = mode->htotal * mode->vtotal * 120 / 1000;
		if (mode->clock != new_crtc_state->mode.clock) {
			new_crtc_state->mode_changed = true;
			ctx->exynos_connector.needs_commit = true;
			dev_dbg(ctx->dev, "raise mode (%s) clock to 120hz on resume\n",
				mode->name);
		}
	} else if (old_crtc_state->adjusted_mode.clock != old_crtc_state->mode.clock) {
		/* clock hacked in last commit due to resume, undo that */
		new_crtc_state->mode_changed = true;
		new_crtc_state->adjusted_mode.clock = new_crtc_state->mode.clock;
		ctx->exynos_connector.needs_commit = false;
		dev_dbg(ctx->dev, "restore mode (%s) clock after resume\n",
			new_crtc_state->mode.name);
	}

	return 0;
}

static void ak3b_commit_done(struct exynos_panel *ctx)
{
	struct ak3b_panel *spanel = to_spanel(ctx);
	struct drm_crtc_commit *commit;

	if (!ctx->crtc || !ctx->crtc->state || !ctx->crtc->state->commit) {
		dev_dbg(ctx->dev, "invalid crtc or commit\n");
		return;
	}

	if (!is_panel_active(ctx) || !spanel->needs_display_on)
		return;

	commit = ctx->crtc->state->commit;

	DPU_ATRACE_BEGIN("ak3b_wait_for_flip_done");
	if (!wait_for_completion_timeout(&commit->flip_done, msecs_to_jiffies(100)))
		dev_warn(ctx->dev, "timeout when waiting for flip done\n");
	DPU_ATRACE_END("ak3b_wait_for_flip_done");

	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, MIPI_DCS_SET_DISPLAY_ON);

	spanel->needs_display_on = false;

	dev_info(ctx->dev, "%s: DISPLAY_ON\n", __func__);
}

static void ak3b_update_wrctrld(struct exynos_panel *ctx)
{
	u8 val = AK3B_WRCTRLD_BCTRL_BIT;
	enum frequency freq = ak3b_get_frequency(ctx);

	if (IS_HBM_ON(ctx->hbm_mode))
		val |= AK3B_WRCTRLD_HBM_BIT;

	if (ctx->hbm.local_hbm.enabled)
		val |= AK3B_WRCTRLD_LOCAL_HBM_BIT;

	if (ctx->dimming_on)
		val |= AK3B_WRCTRLD_DIMMING_BIT;

	dev_dbg(ctx->dev,
		"%s(wrctrld:0x%x, hbm: %s, dimming: %s, local_hbm: %s)\n",
		__func__, val, IS_HBM_ON(ctx->hbm_mode) ? "on" : "off",
		ctx->dimming_on ? "on" : "off",
		ctx->hbm.local_hbm.enabled ? "on" : "off");

	/* pulse settings */
	if (ctx->panel_rev >= PANEL_REV_PROTO1_1) {
		EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);

		if (ctx->panel_rev >= PANEL_REV_EVT1) {
			EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x0B, 0xF2);

			if (ctx->hbm.local_hbm.enabled && freq == HS60)
				EXYNOS_DCS_BUF_ADD(ctx, 0xF2, 0x0C, 0x09, 0xB4);
			else
				EXYNOS_DCS_BUF_ADD(ctx, 0xF2, 0x0C, 0x00, 0x24);
		}

		EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x01, 0xBD);
		if (ctx->hbm.local_hbm.enabled) {
			EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x00, 0x01, 0x01, 0x01);
			if (ctx->panel_rev >= PANEL_REV_EVT1) {
				EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x2F, 0xBD);
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, (freq == HS60 ? 0x00 : 0x02));
			}
		} else {
			if (ctx->panel_rev >= PANEL_REV_EVT1) {
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x81, 0x01, 0x03, 0x03);
				EXYNOS_DCS_BUF_ADD(ctx, 0xB0, 0x00, 0x2F, 0xBD);
				/* HS 120Hz, HS 60Hz, NS 60Hz */
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x02);
			} else {
				EXYNOS_DCS_BUF_ADD(ctx, 0xBD, 0x01, 0x03, 0x03, 0x03);
			}

			buf_add_frequency_select_cmd(ctx);
		}

		EXYNOS_DCS_BUF_ADD_SET(ctx, freq_update);
		EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_off_f0);
	}

	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);
}

#define MAX_BR_HBM 4095
static int ak3b_set_brightness(struct exynos_panel *ctx, u16 br)
{
	u16 brightness;

	if (ctx->current_mode->exynos_mode.is_lp_mode) {
		const struct exynos_panel_funcs *funcs;

		funcs = ctx->desc->exynos_panel_func;
		if (funcs && funcs->set_binned_lp)
			funcs->set_binned_lp(ctx, br);
		return 0;
	}

	if (br > MAX_BR_HBM) {
		br = MAX_BR_HBM;
		dev_dbg(ctx->dev, "%s: capped to dbv(%d)\n",
			__func__, MAX_BR_HBM);
	}

	brightness = (br & 0xff) << 8 | br >> 8;

	return exynos_dcs_set_brightness(ctx, brightness);
}

static void ak3b_set_nolp_mode(struct exynos_panel *ctx,
				  const struct exynos_panel_mode *pmode)
{
	const struct exynos_panel_mode *current_mode = ctx->current_mode;
	unsigned int aod_vrefresh = current_mode ? drm_mode_vrefresh(&current_mode->mode) : 30;
	unsigned int new_vrefresh = drm_mode_vrefresh(&pmode->mode);
	unsigned int aod_te_usec = current_mode ? current_mode->exynos_mode.te_usec : 460;

	if (!is_panel_active(ctx))
		return;

	/* HS60 settings */
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0x60, 0x08, 0x00);
	EXYNOS_DCS_BUF_ADD(ctx, 0xF7, 0x0F);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);

	/* AOD off setting */
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD(ctx, 0x53, 0x20);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);
	ak3b_change_frequency(ctx, new_vrefresh);

	DPU_ATRACE_BEGIN("ak3b_wait_one_vblank");
	exynos_panel_wait_for_vsync_done(ctx, aod_te_usec,
			EXYNOS_VREFRESH_TO_PERIOD_USEC(aod_vrefresh));

	/* Additional sleep time to account for TE variability */
	usleep_range(1000, 1010);
	DPU_ATRACE_END("ak3b_wait_one_vblank");

	dev_info(ctx->dev, "exit LP mode\n");
}

static int ak3b_enable(struct drm_panel *panel)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	const struct exynos_panel_mode *pmode = ctx->current_mode;
	const struct drm_display_mode *mode;
	struct drm_dsc_picture_parameter_set pps_payload;
	struct ak3b_panel *spanel = to_spanel(ctx);

	if (!pmode) {
		dev_err(ctx->dev, "no current mode set\n");
		return -EINVAL;
	}
	mode = &pmode->mode;

	dev_dbg(ctx->dev, "%s+\n", __func__);

	exynos_panel_reset(ctx);

	exynos_panel_send_cmd_set(ctx, &ak3b_init_cmd_set);
	ak3b_send_aod_without_blink_settings(ctx);

	ak3b_change_frequency(ctx, drm_mode_vrefresh(mode));

	ak3b_lhbm_gamma_write(ctx);
	exynos_panel_send_cmd_set(ctx, &ak3b_lhbm_location_cmd_set);

	/* DSC related configuration */
	drm_dsc_pps_payload_pack(&pps_payload, &pps_config);
	exynos_dcs_compression_mode(ctx, 0x1); /* DSC_DEC_ON */
	EXYNOS_PPS_WRITE_BUF(ctx, &pps_payload); /* PPS_SETTING */
	EXYNOS_DCS_BUF_ADD(ctx, 0xC2, 0x14); /* PPS_MIC_OFF */
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(ctx, 0x9D, 0x01); /* PPS_DSC_EN */

	ak3b_update_wrctrld(ctx); /* dimming and HBM */

	if (pmode->exynos_mode.is_lp_mode)
		exynos_panel_set_lp_mode(ctx, pmode);

	spanel->lhbm_ctl.hist_roi_configured = false;
	spanel->needs_display_on = true;

	dev_dbg(ctx->dev, "%s-\n", __func__);

	return 0;
}

static int ak3b_disable(struct drm_panel *panel)
{
	struct exynos_panel *exynos_panel = container_of(panel, struct exynos_panel, panel);
	struct ak3b_panel *spanel = to_spanel(exynos_panel);

	spanel->needs_display_on = false;
	return exynos_panel_disable(panel);
}

static void ak3b_set_hbm_mode(struct exynos_panel *exynos_panel,
				enum exynos_hbm_mode mode)
{
	const bool hbm_update =
		(IS_HBM_ON(exynos_panel->hbm_mode) != IS_HBM_ON(mode));
	const bool irc_update =
		(IS_HBM_ON_IRC_OFF(exynos_panel->hbm_mode) != IS_HBM_ON_IRC_OFF(mode));

	exynos_panel->hbm_mode = mode;

	if (hbm_update)
		ak3b_update_wrctrld(exynos_panel);

	if (irc_update) {
		EXYNOS_DCS_BUF_ADD(exynos_panel, 0xF0, 0x5A, 0x5A); /* test_key_on */
		EXYNOS_DCS_BUF_ADD(exynos_panel, 0xB0, 0x00, 0x01, 0x6A); /* global para */
		EXYNOS_DCS_BUF_ADD(exynos_panel, 0x6A, IS_HBM_ON_IRC_OFF(mode) ? 0x01 : 0x21);
		EXYNOS_DCS_BUF_ADD_AND_FLUSH(exynos_panel, 0xF0, 0xA5, 0xA5); /* test_key_off */
	}
	dev_info(exynos_panel->dev, "hbm_on=%d hbm_ircoff=%d\n", IS_HBM_ON(exynos_panel->hbm_mode),
		 IS_HBM_ON_IRC_OFF(exynos_panel->hbm_mode));
}

static void ak3b_set_local_hbm_brightness(struct exynos_panel *ctx, bool is_first_stage)
{
	struct ak3b_panel *spanel = to_spanel(ctx);
	struct ak3b_lhbm_ctl *ctl = &spanel->lhbm_ctl;
	const u8 *brt;
	enum ak3b_lhbm_brt_overdrive_group group = LHBM_OVERDRIVE_GRP_MAX;
	enum frequency freq = ak3b_get_frequency(ctx);
	/* command uses one byte besides brightness */
	static u8 cmd[LHBM_BRT_LEN + 1];
	int i;

	if (!is_local_hbm_post_enabling_supported(ctx))
		return;

	DPU_ATRACE_BEGIN(__func__);

	dev_info(ctx->dev, "set LHBM brightness at %s stage\n", is_first_stage ? "1st" : "2nd");
	if (is_first_stage) {
		u32 gray = exynos_drm_connector_get_lhbm_gray_level(&ctx->exynos_connector);
		u32 dbv = exynos_panel_get_brightness(ctx);
		u32 normal_dbv_max = ctx->desc->brt_capability->normal.level.max;
		u32 normal_nit_max = ctx->desc->brt_capability->normal.nits.max;
		u32 luma = 0;

		if (gray < 15) {
			group = LHBM_OVERDRIVE_GRP_0_NIT;
		} else {
			if (dbv <= normal_dbv_max)
				luma = panel_cmn_calc_gamma_2_2_luminance(dbv, normal_dbv_max,
				normal_nit_max);
			else
				luma = panel_cmn_calc_linear_luminance(dbv, 645, -1256);
			luma = panel_cmn_calc_gamma_2_2_luminance(gray, 255, luma);

			if (luma < 6)
				group = LHBM_OVERDRIVE_GRP_6_NIT;
			else if (luma < 50)
				group = LHBM_OVERDRIVE_GRP_50_NIT;
			else if (luma < 300)
				group = LHBM_OVERDRIVE_GRP_300_NIT;
			else
				group = LHBM_OVERDRIVE_GRP_MAX;
		}
		dev_info(ctx->dev, "check LHBM overdrive condition | gray=%u dbv=%u luma=%u\n",
			gray, dbv, luma);
	}

	if (group < LHBM_OVERDRIVE_GRP_MAX) {
		brt = ctl->brt_overdrive[freq][group];
		ctl->overdrived = true;
	} else {
		brt = ctl->brt_normal[freq];
		ctl->overdrived = false;
	}
	cmd[0] = lhbm_brightness_write_reg;
	for (i = 0; i < LHBM_BRT_LEN; i++)
		cmd[i+1] = brt[i];
	dev_info(ctx->dev, "set %s brightness: [%d] %*ph\n",
		ctl->overdrived ? "overdrive" : "normal",
		ctl->overdrived ? group : -1, LHBM_BRT_LEN, brt);
	EXYNOS_DCS_BUF_ADD_SET(ctx, test_key_on_f0);
	EXYNOS_DCS_BUF_ADD_SET(ctx, lhbm_brightness_write_index[freq]);
	EXYNOS_DCS_BUF_ADD_SET(ctx, cmd);
	EXYNOS_DCS_BUF_ADD_SET_AND_FLUSH(ctx, test_key_off_f0);

	DPU_ATRACE_END(__func__);
}

static void ak3b_set_local_hbm_mode_post(struct exynos_panel *ctx)
{
	const struct ak3b_panel *spanel = to_spanel(ctx);

	if (spanel->lhbm_ctl.overdrived)
		ak3b_set_local_hbm_brightness(ctx, false);

	/* b/291544944 Extra 1 frame delay for NS mode */
	if (ak3b_get_frequency(ctx) == NS60) {
		usleep_range(16700, 16710);
		dev_info(ctx->dev, "wait 1 frame for NS mode\n");
	}
}

static void ak3b_set_dimming_on(struct exynos_panel *exynos_panel,
				 bool dimming_on)
{
	const struct exynos_panel_mode *pmode = exynos_panel->current_mode;

	exynos_panel->dimming_on = dimming_on;
	if (pmode->exynos_mode.is_lp_mode) {
		dev_info(exynos_panel->dev, "in lp mode, skip to update");
		return;
	}

	ak3b_update_wrctrld(exynos_panel);
}

static void ak3b_set_local_hbm_mode(struct exynos_panel *exynos_panel,
				 bool local_hbm_en)
{
	ak3b_update_wrctrld(exynos_panel);

	if (local_hbm_en)
		ak3b_set_local_hbm_brightness(exynos_panel, true);
}

static void ak3b_mode_set(struct exynos_panel *ctx,
			     const struct exynos_panel_mode *pmode)
{
	ak3b_change_frequency(ctx, drm_mode_vrefresh(&pmode->mode));
}

static bool ak3b_is_mode_seamless(const struct exynos_panel *ctx,
				     const struct exynos_panel_mode *pmode)
{
	/* seamless mode switch is possible if only changing refresh rate */
	return drm_mode_equal_no_clocks(&ctx->current_mode->mode, &pmode->mode);
}

static void ak3b_calculate_lhbm_brightness(struct exynos_panel *ctx,
	const u8 *p_norm, const u32 *rgb_ratio, u8 *out_norm)
{
	const u8 rgb_offset[3][2] = {
		{LHBM_R_COARSE, LHBM_R_FINE},
		{LHBM_GB_COARSE, LHBM_G_FINE},
		{LHBM_GB_COARSE, LHBM_B_FINE}
	};
	u8 new_norm[LHBM_BRT_LEN] = {0};
	u64 tmp;
	int i;
	u16 mask, shift;

	if (!rgb_ratio)
		return;

	for (i = 0; i < LHBM_RGB_RATIO_SIZE ; i++) {
		if (i % 2) {
			mask = 0xf0;
			shift = 4;
		} else {
			mask = 0x0f;
			shift = 0;
		}
		tmp = ((p_norm[rgb_offset[i][0]] & mask) >> shift) << 8
			| p_norm[rgb_offset[i][1]];
		dev_dbg(ctx->dev, "%s: lhbm_gamma[%d] = %llu\n", __func__, i, tmp);
		/* Round off and revert to original gamma value */
		tmp = (tmp * rgb_ratio[i] + 500000000)/1000000000;
		dev_dbg(ctx->dev, "%s: new lhbm_gamma[%d] = %llu\n", __func__, i, tmp);
		new_norm[rgb_offset[i][0]] |= ((tmp & 0xff00) >> 8) << shift;
		new_norm[rgb_offset[i][1]] |= tmp & 0xff;
	}
	memcpy(out_norm, new_norm, LHBM_BRT_LEN);
	dev_info(ctx->dev, "p_norm(%*ph), new_norm(%*ph), rgb_ratio(%u %u %u)\n",
		LHBM_BRT_LEN, p_norm,
		LHBM_BRT_LEN, out_norm,
		rgb_ratio[0], rgb_ratio[1], rgb_ratio[2]);
}

static void ak3b_lhbm_brightness_init(struct exynos_panel *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct ak3b_panel *spanel = to_spanel(ctx);
	struct ak3b_lhbm_ctl *ctl = &spanel->lhbm_ctl;
	int group, freq, ret;

	for (freq = 0; freq < FREQUENCY_COUNT; freq++) {
		EXYNOS_DCS_WRITE_TABLE(ctx, test_key_on_f0);
		EXYNOS_DCS_WRITE_TABLE(ctx, lhbm_brightness_read_index[freq]);
		ret = mipi_dsi_dcs_read(dsi, lhbm_brightness_read_reg,
			ctl->brt_normal[freq], LHBM_BRT_LEN);
		EXYNOS_DCS_WRITE_TABLE(ctx, test_key_off_f0);

		if (ret != LHBM_BRT_LEN) {
			dev_err(ctx->dev, "failed to read lhbm para for %s ret=%d\n",
				frequency_str[freq], ret);
			continue;
		}

		dev_info(ctx->dev, "lhbm normal brightness for %s: %*ph\n",
			frequency_str[freq], LHBM_BRT_LEN, ctl->brt_normal[freq]);

		for (group = 0; group < LHBM_OVERDRIVE_GRP_MAX; group++) {
			ak3b_calculate_lhbm_brightness(ctx, ctl->brt_normal[freq],
				lhbm_rgb_ratio[group], ctl->brt_overdrive[freq][group]);
		}
	}

	print_hex_dump_debug("ak3b-od-brightness: ", DUMP_PREFIX_NONE,
		16, 1,
		ctl->brt_overdrive, sizeof(ctl->brt_overdrive), false);
}

static void ak3b_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
	struct exynos_panel *ctx = container_of(panel, struct exynos_panel, panel);
	struct dentry *panel_root, *csroot;

	if (!ctx)
		return;

	panel_root = debugfs_lookup("panel", root);
	if (!panel_root)
		return;

	csroot = debugfs_lookup("cmdsets", panel_root);
	if (!csroot) {
		goto panel_out;
	}

	exynos_panel_debugfs_create_cmdset(ctx, csroot, &ak3b_init_cmd_set, "init");
	dput(csroot);
panel_out:
	dput(panel_root);
}

static void ak3b_panel_init(struct exynos_panel *ctx)
{
	/* LHBM overdrive init */
	ak3b_lhbm_brightness_init(ctx);
	exynos_panel_send_cmd_set(ctx, &ak3b_lhbm_location_cmd_set);
	ak3b_lhbm_gamma_read(ctx);
	ak3b_lhbm_gamma_write(ctx);
	ak3b_send_aod_without_blink_settings(ctx);
}

static int ak3b_panel_probe(struct mipi_dsi_device *dsi)
{
	struct ak3b_panel *spanel;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	spanel->base.op_hz = 120;

	return exynos_panel_common_init(dsi, &spanel->base);
}

static void ak3b_set_ssc_en(struct exynos_panel *exynos_panel,
				 bool enabled)
{
	const bool ssc_mode_update = exynos_panel->ssc_en != enabled;

	if (!ssc_mode_update)
		return;

	exynos_panel->ssc_en = enabled;
	EXYNOS_DCS_BUF_ADD(exynos_panel, 0xF0, 0x5A, 0x5A); /* test_key_on */
	EXYNOS_DCS_BUF_ADD(exynos_panel, 0xFC, 0x5A, 0x5A); /* test_key_on */
	EXYNOS_DCS_BUF_ADD(exynos_panel, 0xB0, 0x00, 0x6E, 0xC5); /* global para */
	if (enabled)
		EXYNOS_DCS_BUF_ADD(exynos_panel, 0xC5, 0x07, 0x7F);
	else
		EXYNOS_DCS_BUF_ADD(exynos_panel, 0xC5, 0x04, 0x00);
	EXYNOS_DCS_BUF_ADD(exynos_panel, 0xFC, 0xA5, 0xA5); /* test_key_off */
	EXYNOS_DCS_BUF_ADD_AND_FLUSH(exynos_panel, 0xF0, 0xA5, 0xA5); /* test_key_off */
	dev_info(exynos_panel->dev, "ssc_mode=%d \n", exynos_panel->ssc_en);
}

static const struct exynos_display_underrun_param underrun_param = {
	.te_idle_us = 1000,
	.te_var = 1,
};

static const u32 ak3b_bl_range[] = {
	95, 205, 315, 400, 3175
};

#define AK3_DSC {\
	.enabled = true,\
	.dsc_count = 2,\
	.slice_count = 2,\
	.slice_height = 48,\
	.cfg = &pps_config\
}

static const struct exynos_panel_mode ak3b_modes[] = {
	{
		.mode = {
			.name = "1080x2400x60",
			.clock = 168498,
			.hdisplay = 1080,
			.hsync_start = 1080 + 32, // add hfp
			.hsync_end = 1080 + 32 + 12, // add hsa
			.htotal = 1080 + 32 + 12 + 26, // add hbp
			.vdisplay = 2400,
			.vsync_start = 2400 + 12, // add vfp
			.vsync_end = 2400 + 12 + 4, // add vsa
			.vtotal = 2400 + 12 + 4 + 26, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 149,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 8500,
			.bpc = 8,
			.dsc = AK3_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = 12,
			.falling_edge = 12 + 50,
		},
	},
	{
		.mode = {
			.name = "1080x2400x120",
			.clock = 336996,
			.hdisplay = 1080,
			.hsync_start = 1080 + 32, // add hfp
			.hsync_end = 1080 + 32 + 12, // add hsa
			.htotal = 1080 + 32 + 12 + 26, // add hbp
			.vdisplay = 2400,
			.vsync_start = 2400 + 12, // add vfp
			.vsync_end = 2400 + 12 + 4, // add vsa
			.vtotal = 2400 + 12 + 4 + 26, // add vbp
			.flags = 0,
			.width_mm = 70,
			.height_mm = 149,
		},
		.exynos_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 221,
			.bpc = 8,
			.dsc = AK3_DSC,
			.underrun_param = &underrun_param,
		},
		.te2_timing = {
			.rising_edge = 12,
			.falling_edge = 12 + 50,
		},
	},
};

static const struct exynos_panel_mode ak3b_lp_mode = {
	.mode = {
		.name = "1080x2400x30",
		.clock = 84249,
		.hdisplay = 1080,
		.hsync_start = 1080 + 32, // add hfp
		.hsync_end = 1080 + 32 + 12, // add hsa
		.htotal = 1080 + 32 + 12 + 26, // add hbp
		.vdisplay = 2400,
		.vsync_start = 2400 + 12, // add vfp
		.vsync_end = 2400 + 12 + 4, // add vsa
		.vtotal = 2400 + 12 + 4 + 26, // add vbp
		.flags = 0,
		.type = DRM_MODE_TYPE_DRIVER,
		.width_mm = 70,
		.height_mm = 149,
	},
	.exynos_mode = {
		.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
		.vblank_usec = 120,
		.te_usec = 460,
		.bpc = 8,
		.dsc = AK3_DSC,
		.underrun_param = &underrun_param,
		.is_lp_mode = true,
	}
};

static const struct drm_panel_funcs ak3b_drm_funcs = {
	.disable = ak3b_disable,
	.unprepare = exynos_panel_unprepare,
	.prepare = exynos_panel_prepare,
	.enable = ak3b_enable,
	.get_modes = exynos_panel_get_modes,
	.debugfs_init = ak3b_debugfs_init,
};

static const struct exynos_panel_funcs ak3b_exynos_funcs = {
	.set_brightness = ak3b_set_brightness,
	.set_lp_mode = exynos_panel_set_lp_mode,
	.set_nolp_mode = ak3b_set_nolp_mode,
	.set_binned_lp = exynos_panel_set_binned_lp,
	.set_hbm_mode = ak3b_set_hbm_mode,
	.set_dimming_on = ak3b_set_dimming_on,
	.set_local_hbm_mode = ak3b_set_local_hbm_mode,
	.set_local_hbm_mode_post = ak3b_set_local_hbm_mode_post,
	.is_mode_seamless = ak3b_is_mode_seamless,
	.mode_set = ak3b_mode_set,
	.panel_init = ak3b_panel_init,
	.get_panel_rev = exynos_panel_get_revision_by_module_ids,
	.get_te2_edges = exynos_panel_get_te2_edges,
	.configure_te2_edges = exynos_panel_configure_te2_edges,
	.update_te2 = ak3b_update_te2,
	.set_op_hz = ak3b_set_op_hz,
	.read_id = exynos_panel_read_ddic_id,
	.atomic_check = ak3b_atomic_check,
	.commit_done = ak3b_commit_done,
	.set_ssc_en = ak3b_set_ssc_en,
};

static const struct panel_module_id_info ak3b_module_ids[] = {
	{ 0x0080100E, PANEL_REV_PROTO1 },
	{ 0x0080110E, PANEL_REV_PROTO1 },
	{ 0x0080140E, PANEL_REV_PROTO1_1 },
	{ 0x0080201E, PANEL_REV_EVT1 },
	{ 0x0080241E, PANEL_REV_EVT1_1 },
	{ 0x0080401E, PANEL_REV_DVT1 },
	{ 0x0080801E, PANEL_REV_PVT },   /* PVT/MP */
};

const struct brightness_capability ak3b_brightness_capability = {
	.normal = {
		.nits = {
			.min = 2,
			.max = 800,
		},
		.level = {
			.min = 209,
			.max = 3175,
		},
		.percentage = {
			.min = 0,
			.max = 60,
		},
	},
	.hbm = {
		.nits = {
			.min = 800,
			.max = 1400,
		},
		.level = {
			.min = 3176,
			.max = 4095,
		},
		.percentage = {
			.min = 60,
			.max = 100,
		},
	},
};

const struct exynos_panel_desc google_ak3b = {
	.data_lane_cnt = 4,
	.max_brightness = 4095,
	.min_brightness = 209,
	.dft_brightness = 1023,
	.brt_capability = &ak3b_brightness_capability,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.max_luminance = 14000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
	.keep_atc_on_for_op = true,
	.bl_range = ak3b_bl_range,
	.bl_num_ranges = ARRAY_SIZE(ak3b_bl_range),
	.modes = ak3b_modes,
	.num_modes = ARRAY_SIZE(ak3b_modes),
	.off_cmd_set = &ak3b_off_cmd_set,
	.lp_mode = &ak3b_lp_mode,
	.lp_cmd_set = &ak3b_lp_cmd_set,
	.binned_lp = ak3b_binned_lp,
	.num_binned_lp = ARRAY_SIZE(ak3b_binned_lp),
	.no_lhbm_rr_constraints = true,
	.panel_func = &ak3b_drm_funcs,
	.exynos_panel_func = &ak3b_exynos_funcs,
	.lhbm_effective_delay_frames = 1,
	.lhbm_post_cmd_delay_frames = 1,
	.reset_timing_ms = {1, 1, 20},
	.reg_ctrl_enable = {
		{PANEL_REG_ID_VDDI, 0},
		{PANEL_REG_ID_VCI, 0},
		{PANEL_REG_ID_VDDD, 10},
	},
	.reg_ctrl_disable = {
		{PANEL_REG_ID_VDDD, 0},
		{PANEL_REG_ID_VCI, 0},
		{PANEL_REG_ID_VDDI, 0},
	},
	.module_ids = ak3b_module_ids,
	.num_module_ids = ARRAY_SIZE(ak3b_module_ids),
};

static const struct of_device_id exynos_panel_of_match[] = {
	{ .compatible = "google,ak3b", .data = &google_ak3b },
	{ }
};
MODULE_DEVICE_TABLE(of, exynos_panel_of_match);

static struct mipi_dsi_driver exynos_panel_driver = {
	.probe = ak3b_panel_probe,
	.remove = exynos_panel_remove,
	.driver = {
		.name = "panel-google-ak3b",
		.of_match_table = exynos_panel_of_match,
	},
};
module_mipi_dsi_driver(exynos_panel_driver);

MODULE_AUTHOR("Safayat Ullah <safayat@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google ak3b panel driver");
MODULE_LICENSE("GPL");
