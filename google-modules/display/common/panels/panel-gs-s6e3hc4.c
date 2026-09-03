// SPDX-License-Identifier: MIT
/*
 * MIPI-DSI based s6e3hc4 AMOLED LCD panel driver.
 *
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */


#include <drm/drm_vblank.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <video/mipi_display.h>

#include <trace/panel_trace.h>

#include "gs_panel/drm_panel_funcs_defaults.h"
#include "gs_panel/gs_panel_funcs_defaults.h"
#include "gs_panel/gs_panel.h"

/**
 * struct s6e3hc4_panel - panel specific runtime info
 *
 * This struct maintains s6e3hc4 panel specific runtime info, any fixed details about panel should
 * most likely go into struct gs_panel_desc. The variables with the prefix hw_ keep track of the
 * features that were actually committed to hardware, and should be modified after sending cmds to panel,
 * i.e. updating hw state.
 */
struct s6e3hc4_panel {
	/** @base: base panel struct */
	struct gs_panel base;
	/**
	 * @auto_mode_vrefresh: indicates current minimum refresh rate while in auto mode,
	 *			if 0 it means that auto mode is not enabled
	 */
	u32 auto_mode_vrefresh;
	/** @force_changeable_te: force changeable TE (instead of fixed) during early exit */
	bool force_changeable_te;
	/**
	 * @is_pixel_off: pixel-off command is sent to panel. Only sending normal-on or resetting
	 *			panel can recover to normal mode after entering pixel-off state.
	 */
	bool is_pixel_off;
};

#define to_spanel(ctx) container_of(ctx, struct s6e3hc4_panel, base)

static struct drm_dsc_config WQHD_PPS_CONFIG = {
	.line_buf_depth = 9,
	.bits_per_component = 8,
	.convert_rgb = true,
	.slice_count = 2,
	.slice_width = 720,
	.slice_height = 52,
	.simple_422 = false,
	.pic_width = 1440,
	.pic_height = 3120,
	.rc_tgt_offset_high = 3,
	.rc_tgt_offset_low = 3,
	.bits_per_pixel = 128,
	.rc_edge_factor = 6,
	.rc_quant_incr_limit1 = 11,
	.rc_quant_incr_limit0 = 11,
	.initial_xmit_delay = 512,
	.initial_dec_delay = 616,
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
	.scale_decrement_interval = 10,
	.scale_increment_interval = 1478,
	.nfl_bpg_offset = 482,
	.slice_bpg_offset = 376,
	.final_offset = 4336,
	.vbr_enable = false,
	.slice_chunk_size = 720,
	.dsc_version_minor = 1,
	.dsc_version_major = 1,
	.native_422 = false,
	.native_420 = false,
	.second_line_bpg_offset = 0,
	.nsl_bpg_offset = 0,
	.second_line_offset_adj = 0,
};

static struct drm_dsc_config FHD_PPS_CONFIG = {
	.line_buf_depth = 9,
	.bits_per_component = 8,
	.convert_rgb = true,
	.slice_count = 2,
	.slice_width = 540,
	.slice_height = 78,
	.simple_422 = false,
	.pic_width = 1080,
	.pic_height = 2340,
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
	.scale_increment_interval = 1939,
	.nfl_bpg_offset = 320,
	.slice_bpg_offset = 334,
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

#define S6E3HC4_WRCTRLD_BCTRL_BIT 0x20
#define S6E3HC4_WRCTRLD_HBM_BIT 0xC0
#define S6E3HC4_WRCTRLD_LOCAL_HBM_BIT 0x10

#define S6E3HC4_TE2_CHANGEABLE 0x04
#define S6E3HC4_TE2_FIXED 0x51
#define S6E3HC4_TE2_RISING_EDGE_OFFSET 0x10
#define S6E3HC4_TE2_FALLING_EDGE_OFFSET 0x30
#define S6E3HC4_TE2_FALLING_EDGE_OFFSET_NS 0x25

static const u8 unlock_cmd_f0[] = { 0xF0, 0x5A, 0x5A };
static const u8 lock_cmd_f0[] = { 0xF0, 0xA5, 0xA5 };
static const u8 unlock_cmd_fc[] = { 0xFC, 0x5A, 0x5A };
static const u8 lock_cmd_fc[] = { 0xFC, 0xA5, 0xA5 };
static const u8 display_off[] = { 0x28 };
static const u8 display_on[] = { 0x29 };
static const u8 sleep_in[] = { 0x10 };
static const u8 freq_update[] = { 0xF7, 0x0F };
static const u8 nop[] = { 0x00 };

static const struct gs_dsi_cmd s6e3hc4_sleepin_cmds[] = {
	/* SP back failure workaround on EVT */
	GS_DSI_DELAY_CMDLIST(10, sleep_in),
	GS_DSI_REV_CMDLIST(PANEL_REV_LT(PANEL_REV_DVT1_1), unlock_cmd_fc),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_DVT1_1), 0xB0, 0x00, 0x0D, 0xFE),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_DVT1_1), 0xFE, 0x02),
	GS_DSI_REV_CMDLIST(PANEL_REV_LT(PANEL_REV_DVT1_1), lock_cmd_fc),
	GS_DSI_DELAY_CMDLIST(100, nop),
};
static DEFINE_GS_CMDSET(s6e3hc4_sleepin);

static const struct gs_dsi_cmd s6e3hc4_lp_cmds[] = {
	GS_DSI_CMDLIST(display_off),
	GS_DSI_CMDLIST(unlock_cmd_f0),

	/* Fixed TE: sync on */
	GS_DSI_CMD(0xB9, 0x51),
	/* Set freq at 30 Hz */
	GS_DSI_CMD(0xB0, 0x00, 0x01, 0x60),
	GS_DSI_CMD(0x60, 0x00),
	/* Set 10 Hz idle */
	GS_DSI_CMD(0xB0, 0x00, 0x18, 0xBD),
	GS_DSI_CMD(0xBD, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00),
	GS_DSI_CMD(0xBD, 0x25),
	/* AOD timing */
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_DVT1_1), 0xB0, 0x00, 0x3D, 0xF6),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_DVT1_1), 0xF6, 0xAF, 0xB1),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_DVT1_1), 0xB0, 0x00, 0x41, 0xF6),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_DVT1_1), 0xF6, 0xB3),
	GS_DSI_CMDLIST(freq_update),
	/* AOD low mode setting */
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_DVT1), 0xB0, 0x01, 0x7D, 0x94),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_DVT1), 0x94, 0x1C),
	GS_DSI_CMDLIST(lock_cmd_f0),
};
static DEFINE_GS_CMDSET(s6e3hc4_lp);

static const struct gs_dsi_cmd s6e3hc4_lp_off_cmds[] = {
	GS_DSI_CMDLIST(display_off),
	GS_DSI_CMDLIST(unlock_cmd_f0),
	/* AOD low mode off */
	GS_DSI_CMD(0xB0, 0x00, 0x52, 0x94),
	GS_DSI_CMD(0x94, 0x00),
	GS_DSI_CMD(0x53, 0x20),
	GS_DSI_CMDLIST(lock_cmd_f0),
};

static const struct gs_dsi_cmd s6e3hc4_lp_low_cmds[] = {
	GS_DSI_CMDLIST(unlock_cmd_f0),
	/* AOD 10 nit */
	GS_DSI_CMD(0x53, 0x25),
	/* AOD low Mode, 10 nit */
	GS_DSI_CMD(0xB0, 0x00, 0x52, 0x94),
	GS_DSI_CMD(0x94, 0x01, 0x07, 0x98, 0x02),
	GS_DSI_CMD(0x51, 0x00, 0x01),
	GS_DSI_DELAY_CMDLIST(34, lock_cmd_f0),
	GS_DSI_CMDLIST(display_on),
};

static const struct gs_dsi_cmd s6e3hc4_lp_high_cmds[] = {
	GS_DSI_CMDLIST(unlock_cmd_f0),
	/* AOD 50 nit */
	GS_DSI_CMD(0x53, 0x24),
	/* AOD high Mode, 50 nit */
	GS_DSI_CMD(0xB0, 0x00, 0x52, 0x94),
	GS_DSI_CMD(0x94, 0x00, 0x07, 0x98, 0x02),
	GS_DSI_CMD(0x51, 0x00, 0x01),
	GS_DSI_DELAY_CMDLIST(34, lock_cmd_f0),
	GS_DSI_CMDLIST(display_on),
};

static const struct gs_binned_lp s6e3hc4_binned_lp[] = {
	BINNED_LP_MODE("off", 0, s6e3hc4_lp_off_cmds),
	BINNED_LP_MODE_TIMING("low", 80, s6e3hc4_lp_low_cmds, S6E3HC4_TE2_RISING_EDGE_OFFSET,
			      S6E3HC4_TE2_FALLING_EDGE_OFFSET),
	BINNED_LP_MODE_TIMING("high", 2047, s6e3hc4_lp_high_cmds, S6E3HC4_TE2_RISING_EDGE_OFFSET,
			      S6E3HC4_TE2_FALLING_EDGE_OFFSET)
};

static u8 s6e3hc4_get_te2_option(struct gs_panel *ctx)
{
	struct s6e3hc4_panel *spanel = to_spanel(ctx);

	if (!ctx || !ctx->current_mode)
		return S6E3HC4_TE2_CHANGEABLE;

	if (ctx->current_mode->gs_mode.is_lp_mode ||
	    (test_bit(FEAT_EARLY_EXIT, ctx->hw_status.feat) && spanel->auto_mode_vrefresh < 30))
		return S6E3HC4_TE2_FIXED;

	return S6E3HC4_TE2_CHANGEABLE;
}

static void s6e3hc4_update_te2_internal(struct gs_panel *ctx, bool lock)
{
	struct gs_panel_te2_timing timing = {
		.rising_edge = S6E3HC4_TE2_RISING_EDGE_OFFSET,
		.falling_edge = S6E3HC4_TE2_FALLING_EDGE_OFFSET,
	};
	u32 rising, falling;
	struct device *dev = ctx->dev;
	u8 option = s6e3hc4_get_te2_option(ctx);
	u8 idx;
	int ret;

	if (!ctx)
		return;

	ret = gs_panel_get_current_mode_te2(ctx, &timing);
	if (ret) {
		dev_dbg(ctx->dev, "failed to get TE2 timng\n");
		return;
	}
	rising = timing.rising_edge;
	falling = timing.falling_edge;

	if (option == S6E3HC4_TE2_CHANGEABLE && test_bit(FEAT_OP_NS, ctx->sw_status.feat))
		falling = S6E3HC4_TE2_FALLING_EDGE_OFFSET_NS;

	ctx->te2.option = (option == S6E3HC4_TE2_FIXED) ? TEX_OPT_FIXED : TEX_OPT_CHANGEABLE;

	dev_dbg(ctx->dev, "TE2 updated: option %s, idle %s, rising=0x%X falling=0x%X\n",
		(option == S6E3HC4_TE2_CHANGEABLE) ? "changeable" : "fixed",
		ctx->idle_data.panel_idle_vrefresh ? "active" : "inactive", rising, falling);

	if (lock)
		GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x42, 0xF2);
	GS_DCS_BUF_ADD_CMD(dev, 0xF2, 0x0D);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0xB9);
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, option);
	idx = option == S6E3HC4_TE2_FIXED ? 0x22 : 0x1E;
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, idx, 0xB9);
	if (option == S6E3HC4_TE2_FIXED) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, (rising >> 8) & 0xF, rising & 0xFF,
				   (falling >> 8) & 0xF, falling & 0xFF, (rising >> 8) & 0xF,
				   rising & 0xFF, (falling >> 8) & 0xF, falling & 0xFF);
	} else {
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, (rising >> 8) & 0xF, rising & 0xFF,
				   (falling >> 8) & 0xF, falling & 0xFF);
	}
	if (lock)
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
}

static void s6e3hc4_update_te2(struct gs_panel *ctx)
{
	s6e3hc4_update_te2_internal(ctx, true);
}

static inline bool is_auto_mode_allowed(struct gs_panel *ctx)
{
	/* don't want to enable auto mode/early exit during hbm mode */
	if (GS_IS_HBM_ON(ctx->hbm_mode))
		return false;

	if (ctx->idle_data.idle_delay_ms) {
		const unsigned int delta_ms = gs_panel_get_idle_time_delta(ctx);

		if (delta_ms < ctx->idle_data.idle_delay_ms)
			return false;
	}

	return ctx->idle_data.panel_idle_enabled;
}

static u32 s6e3hc4_get_min_idle_vrefresh(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	const int vrefresh = drm_mode_vrefresh(&pmode->mode);
	int min_idle_vrefresh = ctx->min_vrefresh;

	if ((min_idle_vrefresh < 0) || !is_auto_mode_allowed(ctx))
		return 0;

	if (min_idle_vrefresh <= 10)
		min_idle_vrefresh = 10;
	else if (min_idle_vrefresh <= 30)
		min_idle_vrefresh = 30;
	else if (min_idle_vrefresh <= 60)
		min_idle_vrefresh = 60;
	else
		return 0;

	if (min_idle_vrefresh >= vrefresh) {
		dev_dbg(ctx->dev, "min idle vrefresh (%d) higher than target (%d)\n",
			min_idle_vrefresh, vrefresh);
		return 0;
	}

	return min_idle_vrefresh;
}

static void s6e3hc4_update_panel_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
				      bool enforce)
{
	struct s6e3hc4_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	u32 vrefresh, idle_vrefresh = spanel->auto_mode_vrefresh;
	bool irc_mode_changed;
	u8 val;
	DECLARE_BITMAP(changed_feat, FEAT_MAX);

	if (pmode)
		vrefresh = drm_mode_vrefresh(&pmode->mode);
	else
		vrefresh = drm_mode_vrefresh(&ctx->current_mode->mode);

	if (enforce) {
		bitmap_fill(changed_feat, FEAT_MAX);
		irc_mode_changed = true;
	} else {
		bitmap_xor(changed_feat, ctx->sw_status.feat, ctx->hw_status.feat, FEAT_MAX);
		if (bitmap_empty(changed_feat, FEAT_MAX) && vrefresh == ctx->hw_status.vrefresh &&
		    idle_vrefresh == ctx->hw_status.idle_vrefresh)
			return;
		irc_mode_changed = (ctx->sw_status.irc_mode == ctx->hw_status.irc_mode);
	}

	ctx->hw_status.vrefresh = vrefresh;
	ctx->hw_status.idle_vrefresh = idle_vrefresh;
	bitmap_copy(ctx->hw_status.feat, ctx->sw_status.feat, FEAT_MAX);
	dev_dbg(dev, "ns=%d ee=%d hbm=%d irc=%d auto=%d fps=%u idle_fps=%u\n",
		test_bit(FEAT_OP_NS, ctx->sw_status.feat),
		test_bit(FEAT_EARLY_EXIT, ctx->sw_status.feat),
		test_bit(FEAT_HBM, ctx->sw_status.feat), ctx->sw_status.irc_mode,
		test_bit(FEAT_FRAME_AUTO, ctx->sw_status.feat), vrefresh, idle_vrefresh);

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);

	/* TE width setting */
	if (test_bit(FEAT_OP_NS, changed_feat)) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x04, 0xB9);
		val = test_bit(FEAT_OP_NS, ctx->sw_status.feat) ? 0x5E : 0x42;
		/* Changeable TE setting for 120Hz */
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x0C, val, 0x00, 0x1B,
			       /* Fixed TE setting */
			       0x0C, val, 0x00, 0x1B, 0x0C, val, 0x00, 0x1B);
	}
	/* TE setting */
	if (test_bit(FEAT_EARLY_EXIT, changed_feat) || test_bit(FEAT_OP_NS, changed_feat)) {
		if (test_bit(FEAT_EARLY_EXIT, ctx->sw_status.feat) &&
		    !spanel->force_changeable_te) {
			/* Fixed TE */
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x51);
		} else {
			/* Changeable TE */
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x04);
		}
	}

	/* TE2 setting */
	if (test_bit(FEAT_OP_NS, changed_feat))
		s6e3hc4_update_te2_internal(ctx, false);

	/* HBM IRC setting */
	if (irc_mode_changed) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, 0x9B, 0x92);
		if (ctx->sw_status.irc_mode == IRC_OFF) {
			GS_DCS_BUF_ADD_CMD(dev, 0x92, 0x01);
			if (ctx->panel_rev_id.s.stage >= STAGE_DVT) {
				/* IR compensation SP setting */
				GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x02, 0xF3, 0x68);
				GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x20, 0x1E, 0x0C, 0x82, 0x82, 0x78);
			}
		} else {
			GS_DCS_BUF_ADD_CMD(dev, 0x92, 0x21);
			if (ctx->panel_rev_id.id >= PANEL_REVID_DVT1) {
				/* IR compensation SP setting */
				GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x02, 0xF3, 0x68);
				GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x09, 0x09, 0x09, 0x0A, 0x0A, 0x0A);
			}
		}
	}

	/* HBM ELVSS setting */
	if (ctx->panel_rev_id.id <= PANEL_REVID_EVT1_1 && test_bit(FEAT_OP_NS, changed_feat)) {
		val = test_bit(FEAT_OP_NS, ctx->sw_status.feat) ? 0x7C : 0x76;
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, val, 0x94);
		GS_DCS_BUF_ADD_CMD(dev, 0x94, 0x02);
		if (test_bit(FEAT_OP_NS, ctx->sw_status.feat))
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x02, 0x2C, 0x94);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, 0xCC, 0x94);
		GS_DCS_BUF_ADD_CMD(dev, 0x94, 0x03, 0x02, 0x01);
	}

	/*
	 * Operating Mode: NS or HS
	 *
	 * Description: the configs could possibly be overrided by frequency setting,
	 * depending on FI mode.
	 */
	if (test_bit(FEAT_OP_NS, changed_feat)) {
		/* mode set */
		GS_DCS_BUF_ADD_CMD(dev, 0xF2, 0x01);
		val = test_bit(FEAT_OP_NS, ctx->sw_status.feat) ? 0x18 : 0x00;
		GS_DCS_BUF_ADD_CMD(dev, 0x60, val);
	}

	/*
	 * Note: the following command sequence should be sent as a whole if one of panel
	 * state defined by enum panel_state changes or at turning on panel, or unexpected
	 * behaviors will be seen, e.g. black screen, flicker.
	 */

	/*
	 * Early-exit: enable or disable
	 *
	 * Description: early-exit sequence overrides some configs HBM set.
	 */
	if (test_bit(FEAT_EARLY_EXIT, ctx->sw_status.feat)) {
		if (test_bit(FEAT_HBM, ctx->sw_status.feat))
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x21, 0x00, 0x83, 0x03, 0x01);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x21, 0x01, 0x83, 0x03, 0x03);
	} else {
		if (test_bit(FEAT_HBM, ctx->sw_status.feat))
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x21, 0x80, 0x83, 0x03, 0x01);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x21, 0x81, 0x83, 0x03, 0x03);
	}
	val = test_bit(FEAT_OP_NS, ctx->sw_status.feat) ? 0x4E : 0x1E;
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, val, 0xBD);
	if (test_bit(FEAT_HBM, ctx->sw_status.feat)) {
		if (test_bit(FEAT_OP_NS, ctx->sw_status.feat))
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04, 0x00, 0x0A);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x0B);
	} else {
		if (test_bit(FEAT_OP_NS, ctx->sw_status.feat))
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x04, 0x00, 0x08, 0x00, 0x14);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x02, 0x00, 0x06, 0x00, 0x16);
	}

	/*
	 * Frequency setting: FI, frequency, idle frequency
	 *
	 * Description: this sequence possibly overrides some configs early-exit
	 * and operation set, depending on FI mode.
	 */
	if (test_bit(FEAT_FRAME_AUTO, ctx->sw_status.feat)) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x0C, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x12, 0xBD);
		if (test_bit(FEAT_OP_NS, ctx->sw_status.feat)) {
			if (idle_vrefresh == 10) {
				val = test_bit(FEAT_HBM, ctx->sw_status.feat) ? 0x0A : 0x12;
				GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, val, 0x00, 0x02, 0x01);
			} else {
				/* 30 Hz idle */
				val = test_bit(FEAT_HBM, ctx->sw_status.feat) ? 0x02 : 0x03;
				GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, val, 0x00, 0x00, 0x00);
			}
		} else {
			if (idle_vrefresh == 10) {
				if (test_bit(FEAT_HBM, ctx->sw_status.feat))
					GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x0B, 0x00, 0x03,
						       0x01);
				else
					GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x16, 0x00, 0x06,
						       0x01);
			} else if (idle_vrefresh == 30) {
				if (test_bit(FEAT_HBM, ctx->sw_status.feat))
					GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x03, 0x00, 0x02,
						       0x01);
				else
					GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x06, 0x00, 0x04,
						       0x01);
			} else {
				/* 60 Hz idle */
				val = test_bit(FEAT_HBM, ctx->sw_status.feat) ? 0x01 : 0x02;
				GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, val, 0x00, 0x00, 0x00);
			}
		}
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x23);
	} else {
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x21);
		if (test_bit(FEAT_OP_NS, ctx->sw_status.feat)) {
			if (vrefresh == 10)
				val = 0x1B;
			else if (vrefresh == 30)
				val = 0x19;
			else
				val = 0x18;
		} else {
			if (vrefresh == 10)
				val = 0x03;
			else if (vrefresh == 30)
				val = 0x02;
			else if (vrefresh == 60)
				val = 0x01;
			else
				val = 0x00;
		}
		GS_DCS_BUF_ADD_CMD(dev, 0x60, val);
	}

	GS_DCS_BUF_ADD_CMDLIST(dev, freq_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
	;
}

static void s6e3hc4_update_refresh_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
					const u32 idle_vrefresh)
{
	struct s6e3hc4_panel *spanel = to_spanel(ctx);
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);

	dev_dbg(ctx->dev, "%s: mode: %s set idle_vrefresh: %u\n", __func__, pmode->mode.name,
		idle_vrefresh);

	if (idle_vrefresh)
		set_bit(FEAT_FRAME_AUTO, ctx->sw_status.feat);
	else
		clear_bit(FEAT_FRAME_AUTO, ctx->sw_status.feat);

	if (vrefresh == 120 || idle_vrefresh)
		set_bit(FEAT_EARLY_EXIT, ctx->sw_status.feat);
	else
		clear_bit(FEAT_EARLY_EXIT, ctx->sw_status.feat);

	spanel->auto_mode_vrefresh = idle_vrefresh;
	/*
	 * Note: when mode is explicitly set, panel performs early exit to get out
	 * of idle at next vsync, and will not back to idle until not seeing new
	 * frame traffic for a while. If idle_vrefresh != 0, try best to guess what
	 * panel_idle_vrefresh will be soon, and s6e3hc4_update_idle_state() in
	 * new frame commit will correct it if the guess is wrong.
	 */
	ctx->idle_data.panel_idle_vrefresh = idle_vrefresh;
	s6e3hc4_update_panel_feat(ctx, pmode, false);
	notify_panel_mode_changed(ctx);
}

static void s6e3hc4_change_frequency(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 idle_vrefresh = 0;

	if (vrefresh > ctx->op_hz) {
		dev_err(ctx->dev, "invalid freq setting: op_hz=%u, vrefresh=%u\n", ctx->op_hz,
			vrefresh);
		return;
	}

	if (pmode->idle_mode == GIDLE_MODE_ON_INACTIVITY)
		idle_vrefresh = s6e3hc4_get_min_idle_vrefresh(ctx, pmode);

	s6e3hc4_update_refresh_mode(ctx, pmode, idle_vrefresh);

	dev_dbg(ctx->dev, "change to %u hz)\n", vrefresh);
}

static void s6e3hc4_panel_idle_notification(struct gs_panel *ctx, u32 display_id, u32 vrefresh,
					    u32 idle_te_vrefresh)
{
	char event_string[64];
	char *envp[] = { event_string, NULL };
	struct drm_device *dev = ctx->bridge.dev;

	if (vrefresh == idle_te_vrefresh)
		return;

	if (!dev) {
		dev_warn(ctx->dev, "%s: drm_device is null\n", __func__);
	} else {
		snprintf(event_string, sizeof(event_string), "PANEL_IDLE_ENTER=%u,%u,%u",
			 display_id, vrefresh, idle_te_vrefresh);
		kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
	}
}

static bool s6e3hc4_set_self_refresh(struct gs_panel *ctx, bool enable)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;
	struct device *dev = ctx->dev;
	struct s6e3hc4_panel *spanel = to_spanel(ctx);
	u32 idle_vrefresh;

	if (unlikely(!pmode))
		return false;

	/* self refresh is not supported in lp mode since that always makes use of early exit */
	if (pmode->gs_mode.is_lp_mode)
		return false;

	idle_vrefresh = s6e3hc4_get_min_idle_vrefresh(ctx, pmode);

	if (pmode->idle_mode != GIDLE_MODE_ON_SELF_REFRESH) {
		/*
		 * if idle mode is on inactivity, may need to update the target fps for auto mode,
		 * or switch to manual mode if idle should be disabled (idle_vrefresh=0)
		 */
		if ((pmode->idle_mode == GIDLE_MODE_ON_INACTIVITY) &&
		    (spanel->auto_mode_vrefresh != idle_vrefresh)) {
			s6e3hc4_update_refresh_mode(ctx, pmode, idle_vrefresh);
			return true;
		}
		return false;
	}

	if (!enable)
		idle_vrefresh = 0;

	/* if there's no change in idle state then skip cmds */
	if (ctx->idle_data.panel_idle_vrefresh == idle_vrefresh)
		return false;

	PANEL_ATRACE_BEGIN(__func__);
	s6e3hc4_update_refresh_mode(ctx, pmode, idle_vrefresh);

	if (idle_vrefresh) {
		const int vrefresh = drm_mode_vrefresh(&pmode->mode);

		s6e3hc4_panel_idle_notification(ctx, 0, vrefresh, 120);
	} else if (ctx->idle_data.panel_need_handle_idle_exit) {
		struct drm_crtc *crtc = NULL;

		if (ctx->gs_connector->base.state)
			crtc = ctx->gs_connector->base.state->crtc;

		/*
		 * after exit idle mode with fixed TE at non-120hz, TE may still keep at 120hz.
		 * If any layer that already be assigned to DPU that can't be handled at 120hz,
		 * panel_need_handle_idle_exit will be set then we need to wait one vblank to
		 * avoid underrun issue.
		 */
		dev_dbg(dev, "wait one vblank after exit idle\n");
		PANEL_ATRACE_BEGIN("wait_one_vblank");
		if (crtc) {
			int ret = drm_crtc_vblank_get(crtc);

			if (!ret) {
				drm_crtc_wait_one_vblank(crtc);
				drm_crtc_vblank_put(crtc);
			} else {
				usleep_range(8350, 8500);
			}
		} else {
			usleep_range(8350, 8500);
		}
		PANEL_ATRACE_END("wait_one_vblank");
	}

	PANEL_ATRACE_END(__func__);

	return true;
}

static int s6e3hc4_atomic_check(struct gs_panel *ctx, struct drm_atomic_state *state)
{
	struct drm_connector *conn = &ctx->gs_connector->base;
	struct drm_connector_state *new_conn_state =
		drm_atomic_get_new_connector_state(state, conn);
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct s6e3hc4_panel *spanel = to_spanel(ctx);

	if (unlikely(!ctx->current_mode))
		return 0;

	if (drm_mode_vrefresh(&ctx->current_mode->mode) == 120 || !new_conn_state ||
	    !new_conn_state->crtc)
		return 0;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);
	old_crtc_state = drm_atomic_get_old_crtc_state(state, new_conn_state->crtc);
	if (!old_crtc_state || !new_crtc_state || !new_crtc_state->active)
		return 0;

	if ((spanel->auto_mode_vrefresh && old_crtc_state->self_refresh_active) ||
	    !drm_atomic_crtc_effectively_active(old_crtc_state)) {
		struct drm_display_mode *mode = &new_crtc_state->adjusted_mode;

		/* set clock to max refresh rate on self refresh exit or resume due to early exit */
		mode->clock = mode->htotal * mode->vtotal * 120 / 1000;

		if (mode->clock != new_crtc_state->mode.clock) {
			new_crtc_state->mode_changed = true;
			dev_dbg(ctx->dev, "raise mode (%s) clock to 120hz on %s\n", mode->name,
				old_crtc_state->self_refresh_active ? "self refresh exit" :
								      "resume");
		}
	} else if (old_crtc_state->active_changed &&
		   (old_crtc_state->adjusted_mode.clock != old_crtc_state->mode.clock)) {
		/* clock hacked in last commit due to self refresh exit or resume, undo that */
		new_crtc_state->mode_changed = true;
		new_crtc_state->adjusted_mode.clock = new_crtc_state->mode.clock;
		dev_dbg(ctx->dev, "restore mode (%s) clock after self refresh exit or resume\n",
			new_crtc_state->mode.name);
	}

	return 0;
}

static void s6e3hc4_write_display_mode(struct gs_panel *ctx, const struct drm_display_mode *mode)
{
	struct device *dev = ctx->dev;
	u8 val = S6E3HC4_WRCTRLD_BCTRL_BIT;

	if (GS_IS_HBM_ON(ctx->hbm_mode))
		val |= S6E3HC4_WRCTRLD_HBM_BIT;

	if (!gs_is_local_hbm_disabled(ctx))
		val |= S6E3HC4_WRCTRLD_LOCAL_HBM_BIT;

	dev_dbg(dev, "%s(wrctrld:0x%x, hbm: %s, local_hbm: %s)\n", __func__, val,
		GS_IS_HBM_ON(ctx->hbm_mode) ? "on" : "off",
		!gs_is_local_hbm_disabled(ctx) ? "on" : "off");

	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);
}

#define MAX_BR_HBM_EVT1 3949
static int s6e3hc4_set_brightness(struct gs_panel *ctx, u16 br)
{
	u16 brightness;

	if (ctx->current_mode->gs_mode.is_lp_mode) {
		if (gs_panel_has_func(ctx, set_binned_lp))
			ctx->desc->gs_panel_func->set_binned_lp(ctx, br);
		return 0;
	}

	if (ctx->panel_rev_id.id <= PANEL_REVID_EVT1 && br >= MAX_BR_HBM_EVT1) {
		br = MAX_BR_HBM_EVT1;
		dev_dbg(ctx->dev, "%s: capped to dbv(%d) for EVT1 and before\n", __func__,
			MAX_BR_HBM_EVT1);
	}

	brightness = swab16(br);

	return gs_dcs_set_brightness(ctx, brightness);
}

#define TE_WIDTH_USEC_60HZ 8400
#define TE_WIDTH_USEC_120HZ 150
#define TE_WIDTH_USEC_AOD 600

/**
 * s6e3hc4_set_nolp_mode - exit AOD to normal mode
 * @ctx: panel struct
 * @pmode: targeting panel mode
 */
static void s6e3hc4_set_nolp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 delay_us = mult_frac(1000, 1020, vrefresh);

	/* clear the brightness level */
	GS_DCS_WRITE_CMD(dev, 0x51, 0x00, 0x00);

	GS_DCS_WRITE_CMDLIST(dev, display_off);
	/* AOD low mode setting off */
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x52, 0x94);
	GS_DCS_BUF_ADD_CMD(dev, 0x94, 0x00);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
	s6e3hc4_update_panel_feat(ctx, pmode, true);
	/* backlight control and dimming */
	s6e3hc4_write_display_mode(ctx, &pmode->mode);
	s6e3hc4_change_frequency(ctx, pmode);
	usleep_range(delay_us, delay_us + 10);
	GS_DCS_WRITE_CMDLIST(dev, display_on);

	dev_info(ctx->dev, "exit LP mode\n");
}

static const struct gs_dsi_cmd s6e3hc4_init_cmds[] = {
	GS_DSI_REV_CMDLIST(PANEL_REV_LT(PANEL_REV_DVT1_1), unlock_cmd_fc),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_DVT1_1), 0xB0, 0x00, 0x0D, 0xFE),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_DVT1_1), 0xFE, 0x00),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1_1), 0xB0, 0x01, 0x37, 0x90),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1_1), 0x90, 0x00),
	GS_DSI_REV_CMDLIST(PANEL_REV_LT(PANEL_REV_DVT1_1), lock_cmd_fc),
	GS_DSI_DELAY_CMDLIST(120, nop),

	/* Enable TE*/
	GS_DSI_CMD(0x35),

	GS_DSI_CMDLIST(unlock_cmd_f0),

	/* Enable SP */
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1_1), 0xB0, 0x00, 0x58, 0x69),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1_1), 0x69, 0x01),
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1), 0xB0, 0x02, 0xF3, 0x68),
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1), 0x68, 0x09, 0X09, 0X09, 0x0A, 0x0A, 0x0A),
	/* FFC: 165 Mhz, 1% tolerance */
	GS_DSI_CMD(0xB0, 0x00, 0x36, 0xC5),
	GS_DSI_CMD(0xC5, 0x11, 0x10, 0x50, 0x05, 0x4E, 0x74),
	/* NS transition flicker setting */
	GS_DSI_CMD(0xAB, 0x83),

	/* TSP sync auto mode settings*/
	GS_DSI_CMD(0xB0, 0x00, 0x3C, 0xB9),
	GS_DSI_CMD(0xB9, 0x19, 0x09),
	GS_DSI_CMD(0xB0, 0x00, 0x40, 0xB9),
	GS_DSI_CMD(0xB9, 0x30, 0x03),
	GS_DSI_CMD(0xB0, 0x00, 0x05, 0xF2),
	GS_DSI_CMD(0xF2, 0xC8, 0xC0),

	/* Set frame insertion count */
	GS_DSI_CMD(0xB0, 0x00, 0x10, 0xBD),
	GS_DSI_CMD(0xBD, 0x00),

	GS_DSI_CMDLIST(freq_update),
	GS_DSI_CMDLIST(lock_cmd_f0),

	/* CASET */
	GS_DSI_CMD(0x2A, 0x00, 0x00, 0x05, 0x9F),
	/* PASET */
	GS_DSI_CMD(0x2B, 0x00, 0x00, 0x0C, 0x2F),
};
static DEFINE_GS_CMDSET(s6e3hc4_init);

static int s6e3hc4_enable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	const struct drm_display_mode *mode;
	const bool needs_reset = !gs_is_panel_enabled(ctx);
	const struct drm_dsc_config *dsc_cfg = pmode->gs_mode.dsc.cfg;
	bool is_fhd;
	int ret;

	if (!pmode) {
		dev_err(dev, "no current mode set\n");
		return -EINVAL;
	}
	mode = &pmode->mode;
	is_fhd = mode->hdisplay == 1080;

	dev_dbg(dev, "%s\n", __func__);

	if (needs_reset)
		gs_panel_reset_helper(ctx);

	/* DSC related configuration */
	GS_DCS_WRITE_CMD(ctx->dev, 0x9D, 0x01);
	ret = gs_dcs_write_dsc_config(dev, dsc_cfg);
	if (ret)
		dev_err(dev, "%s error writing dsc config (%d)\n", __func__, ret);

	if (needs_reset) {
		u32 delay = 10;

		GS_DCS_WRITE_DELAY_CMD(dev, delay, MIPI_DCS_EXIT_SLEEP_MODE);
		gs_panel_send_cmdset(ctx, &s6e3hc4_init_cmdset);
	}

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMD(dev, 0xC3, is_fhd ? 0x0D : 0x0C);
	if (ctx->panel_rev_id.id >= PANEL_REVID_EVT1_1 &&
	    ctx->panel_rev_id.id <= PANEL_REVID_DVT1_1) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x02, 0xFD, 0x95);
		GS_DCS_BUF_ADD_CMD(dev, 0x95, 0x80, 0x44, 0x08, 0x81, 0x10, 0x17, 0x74);
	}
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	s6e3hc4_update_panel_feat(ctx, pmode, true);
	s6e3hc4_write_display_mode(ctx, mode); /* dimming and HBM */
	s6e3hc4_change_frequency(ctx, pmode);

	if (pmode->gs_mode.is_lp_mode)
		gs_panel_set_lp_mode_helper(ctx, pmode);
	else if (needs_reset || (ctx->panel_state == GPANEL_STATE_BLANK))
		GS_DCS_WRITE_CMDLIST(dev, display_on);

	return 0;
}

static int s6e3hc4_disable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	int ret;

	/* skip disable sequence if going through modeset */
	if (ctx->panel_state == GPANEL_STATE_MODESET)
		return 0;

	ret = gs_panel_disable(panel);
	if (ret)
		return ret;

	/* HBM is disabled in gs_panel_disable() */
	ctx->hw_status.irc_mode = IRC_FLAT_DEFAULT;

	/* panel register state gets reset after disabling hardware */
	bitmap_clear(ctx->hw_status.feat, 0, FEAT_MAX);
	ctx->hw_status.vrefresh = 60;
	ctx->hw_status.idle_vrefresh = 0;

	GS_DCS_WRITE_DELAY_CMDLIST(dev, 20, display_off);

	if (ctx->panel_state == GPANEL_STATE_OFF)
		gs_panel_send_cmdset(ctx, &s6e3hc4_sleepin_cmdset);

	return 0;
}

/*
 * 120hz auto mode takes at least 2 frames to start lowering refresh rate in addition to
 * time to next vblank. Use just over 2 frames time to consider worst case scenario
 */
#define EARLY_EXIT_THRESHOLD_US 17000

/**
 * s6e3hc4_update_idle_state - update panel auto frame insertion state
 * @ctx: panel struct
 *
 * - update timestamp of switching to manual mode in case its been a while since the
 *   last frame update and auto mode may have started to lower refresh rate.
 * - disable auto refresh mode if there is switching delay requirement
 * - trigger early exit by command if it's changeable TE, which could result in
 *   fast 120 Hz boost and seeing 120 Hz TE earlier
 */
static bool s6e3hc4_update_idle_state(struct gs_panel *ctx)
{
	s64 delta_us;
	struct s6e3hc4_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	bool updated = false;

	ctx->idle_data.panel_idle_vrefresh = 0;
	if (!test_bit(FEAT_FRAME_AUTO, ctx->sw_status.feat))
		return false;

	delta_us = ktime_us_delta(ktime_get(), ctx->timestamps.last_commit_ts);
	if (delta_us < EARLY_EXIT_THRESHOLD_US) {
		dev_dbg(ctx->dev, "skip early exit. %lldus since last commit\n", delta_us);
		return false;
	}

	/* triggering early exit causes a switch to 120hz */
	ctx->timestamps.last_mode_set_ts = ktime_get();

	PANEL_ATRACE_BEGIN(__func__);
	/*
	 * If there is delay limitation requirement, turn off auto mode to prevent panel
	 * from lowering frequency too fast if not seeing new frame.
	 */
	if (ctx->idle_data.idle_delay_ms) {
		const struct gs_panel_mode *pmode = ctx->current_mode;
		s6e3hc4_update_refresh_mode(ctx, pmode, 0);
		updated = true;
	} else if (spanel->force_changeable_te) {
		dev_dbg(ctx->dev, "sending early exit out cmd\n");
		GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
		GS_DCS_BUF_ADD_CMDLIST(dev, freq_update);
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
	}

	PANEL_ATRACE_END(__func__);

	return updated;
}

/* TODO: move update te2 to common display driver for other panel drivers */
static void s6e3hc4_commit_done(struct gs_panel *ctx)
{
	if (ctx->current_mode->gs_mode.is_lp_mode)
		return;

	if (s6e3hc4_update_idle_state(ctx))
		s6e3hc4_update_te2(ctx);
}

static void s6e3hc4_set_hbm_mode(struct gs_panel *ctx, enum gs_hbm_mode mode)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	if (mode == ctx->hbm_mode)
		return;

	if (unlikely(!pmode))
		return;

	ctx->hbm_mode = mode;

	if (GS_IS_HBM_ON(mode)) {
		set_bit(FEAT_HBM, ctx->sw_status.feat);
		/* b/202738999 enforce IRC on */
#ifndef DPU_FACTORY_BUILD
		if (mode == GS_HBM_ON_IRC_ON)
			ctx->sw_status.irc_mode = IRC_FLAT_DEFAULT;
		else
			ctx->sw_status.irc_mode = IRC_OFF;
#endif
		s6e3hc4_update_panel_feat(ctx, NULL, false);
		s6e3hc4_write_display_mode(ctx, &pmode->mode);
	} else {
		clear_bit(FEAT_HBM, ctx->sw_status.feat);
		ctx->sw_status.irc_mode = IRC_FLAT_DEFAULT;
		s6e3hc4_write_display_mode(ctx, &pmode->mode);
		s6e3hc4_update_panel_feat(ctx, NULL, false);
	}
}

static const struct gs_dsi_cmd s6e3hc4_lhbm_extra_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(unlock_cmd_f0),

	/* global para */
	GS_DSI_QUEUE_CMD(0xB0, 0x02, 0x1E, 0x92),
	/* area set */
	GS_DSI_QUEUE_CMD(0x92, 0x20, 0x88, 0x71, 0x39, 0x8A, 0x01),
	/* global para */
	GS_DSI_QUEUE_CMD(0xB0, 0x02, 0x24, 0x92),
	/* center position set, x: 0x2D0, y: 0x939 */
	GS_DSI_QUEUE_CMD(0x92, 0x2D, 0x09, 0x39),
	/* global para */
	GS_DSI_QUEUE_CMD(0xB0, 0x02, 0x27, 0x92),
	/* circle size set, radius: 6 mm */
	GS_DSI_QUEUE_CMD(0x92, 0x78),

	GS_DSI_FLUSH_CMDLIST(lock_cmd_f0)
};
static DEFINE_GS_CMDSET(s6e3hc4_lhbm_extra);

static void s6e3hc4_set_local_hbm_mode(struct gs_panel *ctx, bool local_hbm_en)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	if (local_hbm_en)
		gs_panel_send_cmdset(ctx, &s6e3hc4_lhbm_extra_cmdset);
	s6e3hc4_write_display_mode(ctx, &pmode->mode);
}

static void s6e3hc4_mode_set(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	s6e3hc4_change_frequency(ctx, pmode);
}

static bool s6e3hc4_is_mode_seamless(const struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	const struct drm_display_mode *c = &ctx->current_mode->mode;
	const struct drm_display_mode *n = &pmode->mode;

	/* seamless mode set can happen if active region resolution is same */
	return (c->vdisplay == n->vdisplay) && (c->hdisplay == n->hdisplay) &&
	       (c->flags == n->flags);
}

static int s6e3hc4_set_op_hz(struct gs_panel *ctx, unsigned int hz)
{
	u32 vrefresh = drm_mode_vrefresh(&ctx->current_mode->mode);

	if (!gs_is_panel_active(ctx))
		return -EPERM;

	if (vrefresh > hz) {
		dev_err(ctx->dev, "invalid op_hz=%d for vrefresh=%d\n", hz, vrefresh);
		return -EINVAL;
	}

	ctx->op_hz = hz;
	if (hz == 60)
		set_bit(FEAT_OP_NS, ctx->sw_status.feat);
	else
		clear_bit(FEAT_OP_NS, ctx->sw_status.feat);
	s6e3hc4_update_panel_feat(ctx, NULL, false);
	dev_info(ctx->dev, "set op_hz at %d\n", hz);
	return 0;
}

static int s6e3hc4_read_serial(struct gs_panel *ctx)
{
	if (ctx->panel_rev_id.id < PANEL_REVID_DVT1)
		return gs_panel_read_serial(ctx);
	return gs_panel_read_slsi_ddic_id(ctx);
}

static void s6e3hc4_get_panel_rev(struct gs_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	u8 build_code = (id & 0xFF00) >> 8;
	u8 rev = ((build_code & 0xE0) >> 3) | ((build_code & 0x0C) >> 2);

	gs_panel_get_panel_rev(ctx, rev);
}

static const struct gs_display_underrun_param underrun_param = {
	.te_idle_us = 350,
	.te_var = 1,
};

static const u32 s6e3hc4_bl_range[] = { 94, 180, 270, 360, 2047 };

static const struct gs_panel_mode_array s6e3hc4_modes = {
#ifndef RESTRICT_TO_SINGLE_MODE
	.num_modes = 4,
#else
	.num_modes = 1,
#endif
	.modes = {
#ifndef RESTRICT_TO_SINGLE_MODE
		{
			/* 1440x3120 @ 60Hz */
			.mode = {
				.name = "1440x3120x60",
				DRM_MODE_TIMING(60, 1440, 80, 24, 36, 3120, 12, 4, 14),
				.flags = 0,
				.width_mm = 71,
				.height_mm = 155,
			},
			.gs_mode = {
				.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
				.vblank_usec = 120,
				.bpc = 8,
				.dsc = {
					.enabled = true,
					.dsc_count = 2,
					.cfg = &WQHD_PPS_CONFIG,
				},
				.underrun_param = &underrun_param,
			},
			.te2_timing = {
				.rising_edge = S6E3HC4_TE2_RISING_EDGE_OFFSET,
				.falling_edge = S6E3HC4_TE2_FALLING_EDGE_OFFSET,
			},
			.idle_mode = GIDLE_MODE_ON_SELF_REFRESH,
		},
		{
			/* 1440x3120 @ 120Hz */
			.mode = {
				.name = "1440x3120x120",
				DRM_MODE_TIMING(120, 1440, 80, 24, 36, 3120, 12, 4, 14),
				.flags = 0,
				.width_mm = 71,
				.height_mm = 155,
			},
			.gs_mode = {
				.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
				.vblank_usec = 120,
				.te_usec = TE_WIDTH_USEC_120HZ,
				.bpc = 8,
				.dsc = {
					.enabled = true,
					.dsc_count = 2,
					.cfg = &WQHD_PPS_CONFIG,
				},
				.underrun_param = &underrun_param,
			},
			.te2_timing = {
				.rising_edge = S6E3HC4_TE2_RISING_EDGE_OFFSET,
				.falling_edge = S6E3HC4_TE2_FALLING_EDGE_OFFSET,
			},
			.idle_mode = GIDLE_MODE_ON_INACTIVITY,
		},
#endif /* !RESTRICT_TO_SINGLE_MODE */
		{
			/* 1080x2340 @ 60Hz */
			.mode = {
				.name = "1080x2340x60",
				DRM_MODE_TIMING(60, 1080, 80, 24, 36, 2340, 12, 4, 14),
				.flags = 0,
				.type = DRM_MODE_TYPE_PREFERRED,
				.width_mm = 71,
				.height_mm = 155,
			},
			.gs_mode = {
				.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
				.vblank_usec = 120,
				.bpc = 8,
				.dsc = {
					.enabled = true,
					.dsc_count = 2,
					.cfg = &FHD_PPS_CONFIG,
				},
				.underrun_param = &underrun_param,
			},
			.te2_timing = {
				.rising_edge = S6E3HC4_TE2_RISING_EDGE_OFFSET,
				.falling_edge = S6E3HC4_TE2_FALLING_EDGE_OFFSET,
			},
			.idle_mode = GIDLE_MODE_ON_SELF_REFRESH,
		},
#ifndef RESTRICT_TO_SINGLE_MODE
		{
			/* 1080x2340 @ 120Hz */
			.mode = {
				.name = "1080x2340x120",
				DRM_MODE_TIMING(120, 1080, 80, 24, 36, 2340, 12, 4, 14),
				.flags = 0,
				.width_mm = 71,
				.height_mm = 155,
			},
			.gs_mode = {
				.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
				.vblank_usec = 120,
				.te_usec = TE_WIDTH_USEC_120HZ,
				.bpc = 8,
				.dsc = {
					.enabled = true,
					.dsc_count = 2,
					.cfg = &FHD_PPS_CONFIG,
				},
				.underrun_param = &underrun_param,
			},
			.te2_timing = {
				.rising_edge = S6E3HC4_TE2_RISING_EDGE_OFFSET,
				.falling_edge = S6E3HC4_TE2_FALLING_EDGE_OFFSET,
			},
			.idle_mode = GIDLE_MODE_ON_INACTIVITY,
		},
#endif /* !RESTRICT_TO_SINGLE_MODE */
	}
};

static const struct gs_panel_mode_array s6e3hc4_lp_modes = {
	.num_modes = 2,
	.modes = {
		{
			.mode = {
				/* 1440x3120 @ 30Hz */
				.name = "1440x3120x30",
				DRM_MODE_TIMING(30, 1440, 80, 24, 36, 3120, 12, 4, 14),
				.flags = 0,
				.width_mm = 71,
				.height_mm = 155,
			},
			.gs_mode = {
				.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
				.vblank_usec = 120,
				.te_usec = TE_WIDTH_USEC_AOD,
				.bpc = 8,
				.dsc = {
					.enabled = true,
					.dsc_count = 2,
					.cfg = &WQHD_PPS_CONFIG,
				},
				.underrun_param = &underrun_param,
				.is_lp_mode = true,
			},
		},
		{
			.mode = {
				/* 1080x2340 @ 30Hz */
				.name = "1080x2340x30",
				DRM_MODE_TIMING(30, 1080, 80, 24, 36, 2340, 12, 4, 14),
				.flags = 0,
				.width_mm = 71,
				.height_mm = 155,
			},
			.gs_mode = {
				.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
				.vblank_usec = 120,
				.te_usec = TE_WIDTH_USEC_AOD,
				.bpc = 8,
				.dsc = {
					.enabled = true,
					.dsc_count = 2,
					.cfg = &FHD_PPS_CONFIG,
				},
				.underrun_param = &underrun_param,
				.is_lp_mode = true,
			},
		},
	},
};

static void s6e3hc4_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct dentry *panel_root, *csroot;
	struct s6e3hc4_panel *spanel;

	if (!ctx)
		return;

	panel_root = debugfs_lookup("panel", root);
	if (!panel_root)
		return;

	csroot = debugfs_lookup("cmdsets", panel_root);
	if (!csroot) {
		dput(panel_root);
		return;
	}

	spanel = to_spanel(ctx);

	gs_panel_debugfs_create_cmdset(csroot, &s6e3hc4_init_cmdset, "init");
	debugfs_create_bool("force_changeable_te", 0644, panel_root, &spanel->force_changeable_te);

	dput(csroot);
	dput(panel_root);
}

static int s6e3hc4_panel_probe(struct mipi_dsi_device *dsi)
{
	struct s6e3hc4_panel *spanel;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	spanel->base.op_hz = 120;
	spanel->base.hw_status.vrefresh = 60;
	return gs_dsi_panel_common_init(dsi, &spanel->base);
}

static const struct drm_panel_funcs s6e3hc4_drm_funcs = {
	.disable = s6e3hc4_disable,
	.unprepare = gs_panel_unprepare,
	.prepare = gs_panel_prepare,
	.enable = s6e3hc4_enable,
	.get_modes = gs_panel_get_modes,
	.debugfs_init = s6e3hc4_debugfs_init,
};

static const struct gs_panel_funcs s6e3hc4_gs_funcs = {
	.set_brightness = s6e3hc4_set_brightness,
	.set_lp_mode = gs_panel_set_lp_mode_helper,
	.set_nolp_mode = s6e3hc4_set_nolp_mode,
	.set_binned_lp = gs_panel_set_binned_lp_helper,
	.set_hbm_mode = s6e3hc4_set_hbm_mode,
	.set_local_hbm_mode = s6e3hc4_set_local_hbm_mode,
	.is_mode_seamless = s6e3hc4_is_mode_seamless,
	.mode_set = s6e3hc4_mode_set,
	.get_panel_rev = s6e3hc4_get_panel_rev,
	.get_te2_edges = gs_panel_get_te2_edges_helper,
	.set_te2_edges = gs_panel_set_te2_edges_helper,
	.update_te2 = s6e3hc4_update_te2,
	.commit_done = s6e3hc4_commit_done,
	.atomic_check = s6e3hc4_atomic_check,
	.set_self_refresh = s6e3hc4_set_self_refresh,
	.set_op_hz = s6e3hc4_set_op_hz,
	.read_serial = s6e3hc4_read_serial,
};

const struct brightness_capability s6e3hc4_brightness_capability = {
	.normal = {
		.nits = {
			.min = 2,
			.max = 600,
		},
		.level = {
			.min = 4,
			.max = 2047,
		},
		.percentage = {
			.min = 0,
			.max = 60,
		},
	},
	.hbm = {
		.nits = {
			.min = 600,
			.max = 1000,
		},
		.level = {
			.min = 2900,
			.max = 4095,
		},
		.percentage = {
			.min = 60,
			.max = 100,
		},
	},
};

const struct gs_panel_brightness_desc gs_brightness_desc = {
	.max_luminance = 10000000,
	.max_avg_luminance = 1200000,
	.min_luminance = 5,
	.max_brightness = 4095,
	.default_brightness = 1023,
	.brt_capability = &s6e3hc4_brightness_capability,
};

const struct gs_panel_desc samsung_s6e3hc4 = {
	.data_lane_cnt = 4,
	.dbv_extra_frame = true,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.brightness_desc = &gs_brightness_desc,
	.bl_range = s6e3hc4_bl_range,
	.bl_num_ranges = ARRAY_SIZE(s6e3hc4_bl_range),
	.modes = &s6e3hc4_modes,
	.lp_modes = &s6e3hc4_lp_modes,
	.lp_cmdset = &s6e3hc4_lp_cmdset,
	.binned_lp = s6e3hc4_binned_lp,
	.num_binned_lp = ARRAY_SIZE(s6e3hc4_binned_lp),
	.has_off_binned_lp_entry = true,
	.is_idle_supported = true,
	.rr_switch_duration = 1,
	.panel_func = &s6e3hc4_drm_funcs,
	.gs_panel_func = &s6e3hc4_gs_funcs,
};

static const struct of_device_id gs_panel_of_match[] = {
	{ .compatible = "gs,s6e3hc4", .data = &samsung_s6e3hc4 },
	{ }
};
MODULE_DEVICE_TABLE(of, gs_panel_of_match);

static struct mipi_dsi_driver gs_panel_driver = {
	.probe = s6e3hc4_panel_probe,
	.remove = gs_dsi_panel_common_remove,
	.driver = {
		.name = "panel-gs-s6e3hc4",
		.of_match_table = gs_panel_of_match,
	},
};
module_mipi_dsi_driver(gs_panel_driver);

MODULE_AUTHOR("Taylor Nelms <tknelms@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Samsung s6e3hc4 panel driver");
MODULE_LICENSE("Dual MIT/GPL");
