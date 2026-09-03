// SPDX-License-Identifier: MIT //

#include <drm/display/drm_dsc_helper.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <video/mipi_display.h>

#include "trace/panel_trace.h"

#include "gs_panel/drm_panel_funcs_defaults.h"
#include "gs_panel/gs_panel.h"
#include "gs_panel/gs_panel_funcs_defaults.h"

/* PPS Setting DSC 1.2a */
static struct drm_dsc_config pps_config = {
	.line_buf_depth = 9,
	.bits_per_component = 8,
	.convert_rgb = true,
	.slice_width = 540,
	.slice_height = 101,
	.slice_count = 2,
	.simple_422 = false,
	.pic_width = 1080,
	.pic_height = 2424,
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
		{ .range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 2 },
		{ .range_min_qp = 0, .range_max_qp = 4, .range_bpg_offset = 0 },
		{ .range_min_qp = 1, .range_max_qp = 5, .range_bpg_offset = 0 },
		{ .range_min_qp = 1, .range_max_qp = 6, .range_bpg_offset = 62 },
		{ .range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 60 },
		{ .range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 58 },
		{ .range_min_qp = 3, .range_max_qp = 7, .range_bpg_offset = 56 },
		{ .range_min_qp = 3, .range_max_qp = 8, .range_bpg_offset = 56 },
		{ .range_min_qp = 3, .range_max_qp = 9, .range_bpg_offset = 56 },
		{ .range_min_qp = 3, .range_max_qp = 10, .range_bpg_offset = 54 },
		{ .range_min_qp = 5, .range_max_qp = 11, .range_bpg_offset = 54 },
		{ .range_min_qp = 5, .range_max_qp = 12, .range_bpg_offset = 52 },
		{ .range_min_qp = 5, .range_max_qp = 13, .range_bpg_offset = 52 },
		{ .range_min_qp = 7, .range_max_qp = 13, .range_bpg_offset = 52 },
		{ .range_min_qp = 13, .range_max_qp = 15, .range_bpg_offset = 52 }
	},
	.rc_model_size = 8192,
	.flatness_min_qp = 3,
	.flatness_max_qp = 12,
	.initial_scale_value = 32,
	.scale_decrement_interval = 7,
	.scale_increment_interval = 2517,
	.nfl_bpg_offset = 246,
	.slice_bpg_offset = 258,
	.final_offset = 4336,
	.vbr_enable = false,
	.slice_chunk_size = 540,
	.dsc_version_minor = 2,
	.dsc_version_major = 1,
	.native_422 = false,
	.native_420 = false,
	.second_line_bpg_offset = 0,
	.nsl_bpg_offset = 0,
	.second_line_offset_adj = 0,
};

static const u16 WIDTH_MM = 65, HEIGHT_MM = 146;
static const u16 HDISPLAY = 1080, VDISPLAY = 2424;
static const u16 HFP = 32, HSA = 12, HBP = 16;
static const u16 VFP = 8, VSA = 2, VBP = 16;

#define DCSDA_DSC {\
	.enabled = true,\
	.dsc_count = 2,\
	.cfg = &pps_config,\
}

static const struct gs_panel_mode_array dcsda_modes = GS_PANEL_MODES(
	/* MRR modes */
	{
		.mode = {
			.name = "1080x2424x60@60",
			DRM_MODE_TIMING(60, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			/* aligned to bootloader setting */
			.type = DRM_MODE_TYPE_PREFERRED,
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 8570,
			.bpc = 8,
			.dsc = DCSDA_DSC,
		},
	},
	{
		.mode = {
			.name = "1080x2424x120@120",
			DRM_MODE_TIMING(120, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 275,
			.bpc = 8,
			.dsc = DCSDA_DSC,
		},
	},
	/* VRR modes */
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1080x2424x60@60",
			DRM_VRR_MODE_TIMING(60, 60, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			/* aligned to bootloader setting */
			.type = DRM_MODE_TYPE_PREFERRED,
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 8570,
			.bpc = 8,
			.dsc = DCSDA_DSC,
		},
	},
#endif
	{
		.mode = {
			.name = "1080x2424x120@120",
			DRM_VRR_MODE_TIMING(120, 120, HDISPLAY, HFP, HSA, HBP,
							VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 275,
			.bpc = 8,
			.dsc = DCSDA_DSC,
		},
	},
);

static const struct gs_panel_mode_array dcsda_lp_modes = GS_PANEL_MODES(
	{
		.mode = {
			.name = "1080x2424x30@30",
			DRM_MODE_TIMING(30, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 1107,
			.bpc = 8,
			.dsc = DCSDA_DSC,
			.is_lp_mode = true,
		},
	},
);

static const struct gs_brightness_configuration dcsda_brt_configs[] = {
	{
		.panel_rev = PANEL_REV_ALL,
		.default_brightness = 1228, /* 140 nits dbv */
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 1,
					.max = 1400,
				},
				.level = {
					.min = 128,
					.max = 3499,
				},
				.percentage = {
					.min = 0,
					.max = 70,
				},
			},
			.hbm = {
				.nits = {
					.min = 1401,
					.max = 2000,
				},
				.level = {
					.min = 3500,
					.max = 4095,
				},
				.percentage = {
					.min = 70,
					.max = 100,
				},
			},
		},
	},
};

static struct gs_panel_brightness_desc dcsda_brightness_desc = {
	.max_luminance = 10000000,
	.max_avg_luminance = 10000000,
	.min_luminance = 5,
};

#define DCSDA_WRCTRLD_DIMMING_BIT 0x08
#define DCSDA_WRCTRLD_BCTRL_BIT 0x20

#define DCSDA_P1_1_DOE2_EXTINFO 0x6484051A

#define MIPI_DSI_FREQ_DEFAULT 756
#define MIPI_DSI_FREQ_ALTERNATIVE 740

#define PROJECT "DCSDA"

static const u8 VRR_MIN_IDLE_RR_HZ = 60;

static const u8 test_key_enable[] = { 0xF0, 0x5A, 0x5A };
static const u8 test_key_disable[] = { 0xF0, 0xA5, 0xA5 };
static const u8 test_key_fc_enable[] = { 0xFC, 0x5A, 0x5A };
static const u8 test_key_fc_disable[] = { 0xFC, 0xA5, 0xA5 };
static const u8 test_key_f1_enable[] = { 0xF1, 0xF1, 0xA2 };
static const u8 test_key_f1_disable[] = { 0xF1, 0xA5, 0xA5 };
static const u8 panel_update[] = { 0xF7, 0x2F };
static const u8 pixel_off[] = { 0x22 };
static const u8 flash_execute[] = { 0xC0, 0x03 };

static const u8 vrr_60_te_settings[] = { 0xB9, 0x00, 0x09, 0x4F, 0x00, 0x00, 0x10,
					 0x00, 0x09, 0x90, 0x00, 0x09, 0x90 };
static const u8 vrr_120_te_settings[] = { 0xB9, 0x00, 0x09, 0x4F, 0x00, 0x00, 0x10,
					  0x00, 0x09, 0x4F, 0x00, 0x00, 0x10 };

static const struct gs_dsi_cmd dcsda_off_cmds[] = {
	GS_DSI_FLUSH_CMD(MIPI_DCS_SET_DISPLAY_OFF),
	GS_DSI_FLUSH_DELAY_CMD(120, MIPI_DCS_ENTER_SLEEP_MODE),
};
static DEFINE_GS_CMDSET(dcsda_off);

static const struct gs_dsi_cmd dcsda_lp_cmds[] = {
	/* AOD Power Setting */
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x04, 0xF6),
	GS_DSI_QUEUE_CMD(0xF6, 0x30), /* Default */
	GS_DSI_QUEUE_CMDLIST(test_key_disable),

	/* AOD Mode On Setting */
	GS_DSI_FLUSH_CMD(MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24),
};
static DEFINE_GS_CMDSET(dcsda_lp);

static const struct gs_dsi_cmd dcsda_lp_night_cmd[] = {
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_PROTO1, 0x51, 0x00, 0xA9),
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_RANGE(PANEL_REV_PROTO1_1, PANEL_REV_EVT1_1),
		0x51, 0x00, 0x7B),
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1_1), 0x51, 0x00, 0x80),
};

static const struct gs_dsi_cmd dcsda_lp_low_cmd[] = {
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1_1), 0x51, 0x01, 0x5E),
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1_1), 0x51, 0x01, 0x6D),
};

static const struct gs_dsi_cmd dcsda_lp_high_cmd[] = {
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1_1), 0x51, 0x02, 0xD6),
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1_1), 0x51, 0x02, 0xF5),
};

static const struct gs_dsi_cmd dcsda_lp_sun_cmd[] = {
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1_1), 0x51, 0x04, 0xAC),
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1_1), 0x51, 0x04, 0xE0),
};

static const struct gs_binned_lp dcsda_binned_lp[] = {
	/* night threshold 4 nits */
	BINNED_LP_MODE_TIMING("night", 243, dcsda_lp_night_cmd, 12, 12 + 50),
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 695, dcsda_lp_low_cmd, 12, 12 + 50),
	/* high threshold 140 nits */
	BINNED_LP_MODE_TIMING("high", 1228, dcsda_lp_high_cmd, 12, 12 + 50),
	BINNED_LP_MODE_TIMING("sun", 4095, dcsda_lp_sun_cmd, 12, 12 + 50),
};

static const struct gs_dsi_cmd dcsda_init_cmds[] = {
	/* TE on */
	GS_DSI_QUEUE_CMD(MIPI_DCS_SET_TEAR_ON, 0x00),

	/* sleep out */
	GS_DSI_DELAY_CMD(120, MIPI_DCS_EXIT_SLEEP_MODE),

	/* TE width setting (MTP'ed) */
	/* TE2 width setting (MTP'ed) */

	/* CASET: 1080 */
	GS_DSI_QUEUE_CMD(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37),

	/* PASET: 2424 */
	GS_DSI_FLUSH_CMD(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x77),
};
static DEFINE_GS_CMDSET(dcsda_init);

/**
 * struct dcsda_panel - panel specific runtime info
 *
 * This struct maintains dcsda panel specific runtime info, any fixed details about panel
 * should most likely go into struct gs_panel_desc
 */
struct dcsda_panel {
	/** @base: base panel struct */
	struct gs_panel base;
	/**
	 * @is_pixel_off: pixel-off command is sent to panel. Only sending normal-on or resetting
	 *		  panel can recover to normal mode after entering pixel-off state.
	 */
	bool is_pixel_off;
	/** @color_read_type: data type to read from flash */
	enum color_data_type color_read_type;
	/** @luminance_read_option: luminance-specific read option to select dbv/freq */
	u32 luminance_read_option;
};
#define to_spanel(ctx) container_of(ctx, struct dcsda_panel, base)

static void dcsda_change_frequency(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	u32 vrefresh = gs_is_vrr_mode(pmode) ? ctx->sw_status.idle_vrefresh :
					       drm_mode_vrefresh(&pmode->mode);

	if (vrefresh > drm_mode_vrefresh(&pmode->mode)) {
		dev_warn(dev, "%s: out of range freq %uHz, clamping to %uHz\n",
			      __func__, vrefresh, drm_mode_vrefresh(&pmode->mode));
		vrefresh = drm_mode_vrefresh(&pmode->mode);
	}

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMD(ctx->dev, 0x83, vrefresh == 60 ? 0x08 : 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);

	notify_panel_te2_freq_changed(ctx, 0);

	ctx->panel_settings_changed = true;

	dev_info(dev, "dcsda_change_frequency: change to %uHz\n", vrefresh);
}

static void dcsda_mode_set(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	if (gs_is_vrr_mode(pmode)) {
		struct device *dev = ctx->dev;

		GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x08, 0xB9);
		if (drm_mode_vrefresh(&pmode->mode) > 60)
			GS_DCS_BUF_ADD_CMDLIST(dev, vrr_120_te_settings);
		else
			GS_DCS_BUF_ADD_CMDLIST(dev, vrr_60_te_settings);
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
	}

	dcsda_change_frequency(ctx, pmode);

	dev_dbg(ctx->dev, "%s: change to %dhz\n", __func__, drm_mode_vrefresh(&pmode->mode));
}

static void dcsda_refresh_ctrl(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	const u32 ctrl = ctx->refresh_ctrl;

	if (!pmode)
		return;

	dev_dbg(ctx->dev, "refresh_ctrl=%#X\n", ctrl);

	if (!gs_is_vrr_mode(pmode)) {
		dev_warn(dev, "refresh_ctrl: mode control not supported for %s\n",
			       pmode->mode.name);
		return;
	}

	PANEL_ATRACE_BEGIN(__func__);

	if (ctrl & GS_PANEL_REFRESH_CTRL_FI_FRAME_COUNT_MASK ||
	    ctrl & GS_PANEL_REFRESH_CTRL_FI_AUTO)
		dev_warn(dev, "refresh_ctrl: FI functionality not supported\n");

	if (ctrl & GS_PANEL_REFRESH_CTRL_MIN_REFRESH_RATE_MASK) {
		u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
		u32 min_vrefresh = GS_PANEL_REFRESH_CTRL_MIN_REFRESH_RATE(ctrl);

		if (min_vrefresh == vrefresh || min_vrefresh == VRR_MIN_IDLE_RR_HZ) {
			ctx->sw_status.idle_vrefresh = min_vrefresh;
			PANEL_ATRACE_INT_PID_FMT(ctx->sw_status.idle_vrefresh, ctx->trace_pid,
						 "idle_vrefresh[%s]", ctx->panel_model);
			dcsda_change_frequency(ctx, pmode);
		} else {
			dev_warn(ctx->dev,
				 "refresh_ctrl: %uHz min RR requested, only %u/%u Hz supported\n",
				 min_vrefresh, VRR_MIN_IDLE_RR_HZ, vrefresh);
		}
	}

	PANEL_ATRACE_END(__func__);
}

static void dcsda_update_wrctrld(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	u8 val = DCSDA_WRCTRLD_BCTRL_BIT;

	if (ctx->dimming_on)
		val |= DCSDA_WRCTRLD_DIMMING_BIT;

	dev_dbg(dev, "dcsda_update_wrctrld(wrctrld:0x%x, hbm: %d, dimming: %d)\n", val,
		GS_IS_HBM_ON(ctx->hbm_mode), ctx->dimming_on);

	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);
}

static const struct { int in; int out; } DCSDA_PRE_EVT1_1_DBV_MAPPING[] = {
	// in is >= evt1.1 dbv, out is the corresponding < evt1.1 dbv that maps to same nits.
	// NBM Range
	{  128,  123 },
	{  129,  124 },
	{  131,  125 },
	{  132,  126 },
	{  136,  130 },
	{  137,  131 },
	{  180,  173 },
	{  181,  174 },
	{  182,  174 },
	{  183,  175 },
	{  184,  176 },
	{  185,  177 },
	{  188,  180 },
	{  189,  181 },
	{  192,  184 },
	{  193,  185 },
	{  251,  241 },
	{  252,  241 },
	{  253,  242 },
	{  311,  298 },
	{  312,  299 },
	{  369,  354 },
	{  370,  355 },
	{  371,  356 },
	{  422,  405 },
	{  445,  427 },
	{  503,  482 },
	{  532,  510 },
	{  599,  574 },
	{  639,  612 },
	{  714,  684 },
	{  766,  734 },
	{  853,  818 },
	{  917,  879 },
	{ 1019,  977 },
	{ 1100, 1054 },
	{ 1220, 1169 },
	{ 1323, 1268 },
	{ 1466, 1405 },
	{ 1596, 1530 },
	{ 1770, 1696 },
	{ 1937, 1857 },
	{ 2154, 2065 },
	{ 2374, 2275 },
	{ 2655, 2545 },
	{ 2957, 2834 },
	{ 3345, 3206 },
	// HBM Range
	{ 3499, 3354 },
	{ 4095, 3910 },
};
#define DCSDA_PRE_EVT1_1_DBV_MAPPING_SIZE (ARRAY_SIZE(DCSDA_PRE_EVT1_1_DBV_MAPPING))

static u16 dcsda_brightness_mapping_pre_evt1_1(struct gs_panel *ctx, u16 br)
{
	int i;
	u16 x0, x1, y0, y1;
	u32 numerator;
	u16 delta;

	if (br <= DCSDA_PRE_EVT1_1_DBV_MAPPING[0].in)
		return DCSDA_PRE_EVT1_1_DBV_MAPPING[0].out;
	if (br >= DCSDA_PRE_EVT1_1_DBV_MAPPING[DCSDA_PRE_EVT1_1_DBV_MAPPING_SIZE - 1].in)
		return DCSDA_PRE_EVT1_1_DBV_MAPPING[DCSDA_PRE_EVT1_1_DBV_MAPPING_SIZE - 1].out;

	for (i = 0; i < DCSDA_PRE_EVT1_1_DBV_MAPPING_SIZE - 1; i++) {
		if (br < DCSDA_PRE_EVT1_1_DBV_MAPPING[i+1].in)
			break;
	}
	x0 = DCSDA_PRE_EVT1_1_DBV_MAPPING[i].in;
	x1 = DCSDA_PRE_EVT1_1_DBV_MAPPING[i+1].in;
	y0 = DCSDA_PRE_EVT1_1_DBV_MAPPING[i].out;
	y1 = DCSDA_PRE_EVT1_1_DBV_MAPPING[i+1].out;

	numerator = (u32)(br - x0) * (y1 - y0);
	delta = (u16)(numerator / (x1 - x0));

	dev_dbg(ctx->dev, "map dbv for < evt1_1 panel:%u->%u\n", br, y0 + delta);
	return y0 + delta;
}

static int dcsda_set_brightness(struct gs_panel *ctx, u16 br)
{
	u16 brightness;
	u32 max_brightness;
	struct dcsda_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;

	if (ctx->current_mode && ctx->current_mode->gs_mode.is_lp_mode) {
		/* don't stay at pixel-off state in AOD, or black screen is possibly seen */
		if (spanel->is_pixel_off) {
			GS_DCS_WRITE_CMD(dev, MIPI_DCS_ENTER_NORMAL_MODE);
			spanel->is_pixel_off = false;
		}
		if (gs_panel_has_func(ctx, set_binned_lp))
			ctx->desc->gs_panel_func->set_binned_lp(ctx, br);
		return 0;
	}

	/* Use pixel off command instead of setting DBV 0 */
	if (!br) {
		if (!spanel->is_pixel_off) {
			GS_DCS_WRITE_CMDLIST(dev, pixel_off);
			spanel->is_pixel_off = true;
			dev_dbg(dev, "dcsda_set_brightness: pixel off instead of dbv 0\n");
		}
		return 0;
	} else if (br && spanel->is_pixel_off) {
		GS_DCS_WRITE_CMD(dev, MIPI_DCS_ENTER_NORMAL_MODE);
		spanel->is_pixel_off = false;
	}

	if (!ctx->desc->brightness_desc->brt_capability) {
		dev_err(dev, "no available brightness capability\n");
		return -EINVAL;
	}

	max_brightness = ctx->desc->brightness_desc->brt_capability->hbm.level.max;

	if (br > max_brightness) {
		br = max_brightness;
		dev_warn(dev, "dcsda_set_brightness: capped to dbv(%d)\n", max_brightness);
	}

	/* brightness curve mapping due to curve in HLOS is for >= EVT1.1 */
	if (ctx->panel_rev_id.id == PANEL_REVID_PROTO1 ||
			ctx->panel_rev_id.id == PANEL_REVID_PROTO1_1 ||
			ctx->panel_rev_id.id == PANEL_REVID_EVT1)
		br = dcsda_brightness_mapping_pre_evt1_1(ctx, br);

	/* swap endianness because panel expects MSB first */
	brightness = swab16(br);

	return gs_dcs_set_brightness(ctx, brightness);
}

static void dcsda_set_hbm_mode(struct gs_panel *ctx, enum gs_hbm_mode mode)
{
	struct device *dev = ctx->dev;

	ctx->hbm_mode = mode;

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	/* FGZ mode setting */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x61, 0x68);
	if (GS_IS_HBM_ON(ctx->hbm_mode) && GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode)) {
		/* FGZ Mode ON */
		if (ctx->panel_rev_id.id == PANEL_REVID_PROTO1)
			GS_DCS_BUF_ADD_CMD(dev, 0x68, 0xB0, 0x2C, 0x6A,
						0x80, 0x00, 0x00, 0x99, 0x7A);
		else if (ctx->panel_rev_id.id == PANEL_REVID_PROTO1_1) {
			if (ctx->panel_id == DCSDA_P1_1_DOE2_EXTINFO)
				GS_DCS_BUF_ADD_CMD(dev, 0x68, 0xB0, 0x2C, 0x6A,
						0x80, 0x00, 0x00, 0x94, 0x76);
			else
				GS_DCS_BUF_ADD_CMD(dev, 0x68, 0xB0, 0x2C, 0x6A,
						0x80, 0x00, 0x00, 0xA8, 0x86);
		} else if (ctx->panel_rev_id.id == PANEL_REVID_EVT1) {
			GS_DCS_BUF_ADD_CMD(dev, 0x68, 0xB0, 0x2C, 0x6A,
						0x80, 0x00, 0x00, 0x94, 0x76);
		} else {
			GS_DCS_BUF_ADD_CMD(dev, 0x68, 0xB0, 0x2C, 0x6A,
						0x80, 0x00, 0x00, 0xA5, 0x84);
		}
	} else {
		/* FGZ Mode OFF */
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0xB0, 0x2C, 0x6A, 0x80, 0x00, 0x00, 0x00, 0x00);
	}

	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);

	dev_info(dev, "hbm_on=%d hbm_ircoff=%d.\n", GS_IS_HBM_ON(ctx->hbm_mode),
		 GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode));
}

static void dcsda_set_dimming(struct gs_panel *ctx, bool dimming_on)
{
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;

	ctx->dimming_on = dimming_on;

	if (pmode->gs_mode.is_lp_mode) {
		dev_warn(dev, "in lp mode, skip to update dimming usage\n");
		return;
	}

	dcsda_update_wrctrld(ctx);
}

static void dcsda_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct dentry *panel_root, *csroot;

	if (!ctx)
		return;

	panel_root = debugfs_lookup("panel", root);
	if (!panel_root)
		return;

	csroot = debugfs_lookup("cmdsets", panel_root);
	if (!csroot)
		goto panel_out;

	gs_panel_debugfs_create_cmdset(csroot, &dcsda_init_cmdset, "init");

	dput(csroot);
panel_out:
	dput(panel_root);
}

static void dcsda_get_panel_rev(struct gs_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	u8 build_code = (id & 0xFF00) >> 8;
	u8 main = (build_code & 0xE0) >> 3;
	u8 sub = (build_code & 0x0C) >> 2;
	u8 rev = main | sub;

	gs_panel_get_panel_rev(ctx, rev);
}

static void dcsda_set_nolp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	if (!gs_is_panel_active(ctx))
		return;

	/* AOD Mode Off Setting */
	dcsda_update_wrctrld(ctx);
	dcsda_mode_set(ctx, pmode);

	dev_info(ctx->dev, "exit LP mode\n");
}

static void dcsda_pre_update_ffc(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	if (ctx->force_ffc_off)
		return;

	dev_dbg(ctx->dev, "disabling FFC\n");

	PANEL_ATRACE_BEGIN(__func__);

	/* FFC off */
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_enable);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x36, 0xC5);
	GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x10);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_disable);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);

	PANEL_ATRACE_END(__func__);

	ctx->ffc_en = false;
}

static void dcsda_update_ffc(struct gs_panel *ctx, unsigned int hs_clk_mbps)
{
	struct device *dev = ctx->dev;

	if (ctx->force_ffc_off)
		return;

	dev_dbg(ctx->dev, "hs_clk_mbps: current=%u, target=%u\n",
		ctx->dsi_hs_clk_mbps, hs_clk_mbps);

	PANEL_ATRACE_BEGIN(__func__);

	if (hs_clk_mbps != MIPI_DSI_FREQ_DEFAULT && hs_clk_mbps != MIPI_DSI_FREQ_ALTERNATIVE) {
		dev_warn(ctx->dev, "invalid hs_clk_mbps=%u for FFC\n", hs_clk_mbps);
	} else if (ctx->dsi_hs_clk_mbps != hs_clk_mbps || !ctx->ffc_en) {
		dev_info(ctx->dev, "updating FFC for hs_clk_mbps=%u\n", hs_clk_mbps);
		ctx->dsi_hs_clk_mbps = hs_clk_mbps;

		GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
		GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_enable);

		/* Update FFC */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x3E, 0xC5);
		if (hs_clk_mbps == MIPI_DSI_FREQ_DEFAULT) {
			GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x56, 0x59);
		} else { /* MIPI_DSI_FREQ_ALTERNATIVE */
			GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x58, 0x37);
		}

		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x36, 0xC5);
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x11, 0x10, 0x50, 0x05);
		GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_disable);
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);

		ctx->ffc_en = true;
	}

	PANEL_ATRACE_END(__func__);
}

static void dcsda_set_ssc_en(struct gs_panel *ctx, bool enabled)
{
	struct device *dev = ctx->dev;
	const bool ssc_mode_update = ctx->ssc_en != enabled;

	if (!ssc_mode_update) {
		dev_dbg(ctx->dev, "ssc_mode skip update\n");
		return;
	}
	ctx->ssc_en = enabled;
	PANEL_ATRACE_BEGIN("%s(%d)", __func__, enabled);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_enable);
	/* SSC setting */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x6E, 0xC5);
	if (enabled)
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x07, 0x7F, 0x00, 0x00);
	else
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x04);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_fc_disable);
	PANEL_ATRACE_END(__func__);
	dev_info(dev, "ssc_mode=%d\n", enabled);
}

static void dcsda_enable_errfg(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMD(dev, 0xE5, 0x15);
	GS_DCS_BUF_ADD_CMD(dev, 0xED, 0x01, 0x41);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x17, 0xED);
	GS_DCS_BUF_ADD_CMD(dev, 0xED, 0x3C);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x5F, 0xF4);
	GS_DCS_BUF_ADD_CMD(dev, 0xF4, 0x30);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
}

static int dcsda_enable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	const bool needs_init = !gs_is_panel_enabled(ctx);

	if (!pmode) {
		dev_err(dev, "no current mode set\n");
		return -EINVAL;
	}

	gs_panel_first_enable_helper(ctx);

	PANEL_ATRACE_BEGIN(__func__);

	if (needs_init) {
		/* initial command */
		gs_panel_send_cmdset(ctx, &dcsda_init_cmdset);

		if (!ctx->force_ffc_off)
			dcsda_update_ffc(ctx, MIPI_DSI_FREQ_DEFAULT);

		dcsda_enable_errfg(ctx);
	}

	/* frequency */
	dcsda_mode_set(ctx, pmode);

	/* DSC related configuration */
	GS_DCS_WRITE_CMD(dev, MIPI_DSI_COMPRESSION_MODE, 0x01);
	gs_dcs_write_dsc_config(dev, &pps_config);
	/* DSC Enable */
	GS_DCS_BUF_ADD_CMD(dev, 0x9D, 0x01);

	/* dimming and HBM */
	dcsda_update_wrctrld(ctx);

	/* display on */
	if (pmode->gs_mode.is_lp_mode)
		gs_panel_set_lp_mode_helper(ctx, pmode);

	GS_DCS_WRITE_CMD(dev, MIPI_DCS_SET_DISPLAY_ON);

	ctx->dsi_hs_clk_mbps = MIPI_DSI_FREQ_DEFAULT;

	PANEL_ATRACE_END(__func__);

	return 0;
}

static int dcsda_disable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);

	/* skip disable sequence if going through modeset */
	if (ctx->panel_state == GPANEL_STATE_MODESET)
		return 0;

	ctx->ffc_en = false;

	return gs_panel_disable(panel);
}

static void dcsda_panel_init(struct gs_panel *ctx)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	if (gs_is_vrr_mode(pmode)) {
		ctx->sw_status.idle_vrefresh = VRR_MIN_IDLE_RR_HZ;
		dcsda_mode_set(ctx, pmode);
	}

	dcsda_update_ffc(ctx, MIPI_DSI_FREQ_DEFAULT);
	dcsda_enable_errfg(ctx);
}

static int dcsda_panel_probe(struct mipi_dsi_device *dsi)
{
	struct dcsda_panel *spanel;
	struct gs_panel *ctx;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	ctx = &spanel->base;
	spanel->is_pixel_off = false;
	/* FFC is disabled in bootloader */
	ctx->ffc_en = false;

	/* b/521139235: disable FFC */
	ctx->force_ffc_off = true;

	return gs_dsi_panel_common_init(dsi, ctx);
}

static int dcsda_panel_config(struct gs_panel *ctx)
{
	gs_panel_model_init(ctx, PROJECT, 0);

	return gs_panel_update_brightness_desc(&dcsda_brightness_desc,
						dcsda_brt_configs,
						ARRAY_SIZE(dcsda_brt_configs),
						ctx->panel_rev_bitmask);
}

static void dcsda_prepare_color_data_read(struct device *dev)
{
	/* Flash mode, RAM access, write enable */
	GS_DCS_BUF_ADD_CMD(dev, 0xC0, 0x02);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x74, 0x03, 0x00, 0x00);
	usleep_range(1000, 1100);

	GS_DCS_BUF_ADD_CMD(dev, 0xC1, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, flash_execute);
	usleep_range(1000, 1100);

	GS_DCS_BUF_ADD_CMD(dev, 0xC1, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, flash_execute);
}

static ssize_t dcsda_read_flash_address(struct device *dev, u32 addr, size_t read_len, char *buf,
				       size_t buf_len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	ssize_t read_ret = -1;
	u8 addr1, addr2, addr3;
	u8 read_len1, read_len2;

	if (buf_len < read_len)
		return -EINVAL;

	usleep_range(10000, 10500);
	addr1 = (addr >> 16) & 0xFF;
	addr2 = (addr >> 8) & 0xFF;
	addr3 = addr & 0xFF;
	read_len1 = (read_len >> 8) & 0xFF;
	read_len2 = read_len & 0xFF;
	GS_DCS_BUF_ADD_CMD(dev, 0xC1, 0x6B, addr1, addr2, addr3, read_len1, 0x00, read_len2);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, flash_execute);
	usleep_range(4000, 4100);

	read_ret = mipi_dsi_dcs_read(dsi, 0x6E, buf, read_len);
	if (read_ret != read_len)
		dev_warn(dev, "dcsda_read_flash_address: Read %zd instead of %zu from flash addr %u\n",
			 read_ret, read_len, addr);
	return read_ret;
}

static ssize_t dcsda_read_cie_data(struct gs_panel *ctx, char *buf, size_t buf_len)
{
	struct device *dev = ctx->dev;
	int read_ret = -1;
	u8 read_len = ctx->desc->calibration_desc->color_cal[COLOR_DATA_TYPE_CIE].data_size;

	if (buf_len < read_len)
		return -EINVAL;

	PANEL_ATRACE_BEGIN(__func__);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_f1_enable);
	dcsda_prepare_color_data_read(dev);
	read_ret = dcsda_read_flash_address(dev, 0x055000, read_len, buf, buf_len);

	GS_DCS_BUF_ADD_CMD(dev, 0xC0, 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_f1_disable);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);

	PANEL_ATRACE_END(__func__);
	if (read_ret != read_len) {
		dev_warn(dev, "%s: Unable to read DDIC CIE data (%d)\n", __func__, read_ret);
		return -EINVAL;
	}
	return read_ret;
}

enum COLOR_INDEX { COLOR_RED = 0, COLOR_GREEN, COLOR_BLUE, MAX_COLOR };

#define COLOR_OPTION_COUNT 16

struct dcsda_lum_address {
	const u32 red_address[COLOR_OPTION_COUNT];
	const u32 green_address[COLOR_OPTION_COUNT];
	const u32 blue_address[COLOR_OPTION_COUNT];
};

static const struct dcsda_lum_address dcsda_before_evt1p1_lum_address = {
	.red_address = {
		0x06B202, 0x06B404, 0x06B606, 0x06BA0A, 0x06BE0E, 0x06C212, 0x06CE1E, 0x06D222,
		0x06D626, 0x06D828, 0x06DA2A, 0x06DE2E, 0x06E232, 0x06E636, 0x06F242, 0x06F646,
	},
	.green_address = {
		0x070050, 0x070252, 0x070454, 0x070858, 0x070C5C, 0x071060, 0x071C6C, 0x072070,
		0x072474, 0x072676, 0x072878, 0x072C7C, 0x073080, 0x073484, 0x074090, 0x074494,
	},
	.blue_address = {
		0x074E9E, 0x0750A0, 0x0752A2, 0x0756A6, 0x075AAA, 0x075EAE, 0x076ABA, 0x076EBE,
		0x0772C2, 0x0774C4, 0x0776C6, 0x077ACA, 0x077ECE, 0x0782D2, 0x078EDE, 0x0792E2,
	},
};

static const struct dcsda_lum_address dcsda_por_lum_address = {
	.red_address = {
		0x06B202, 0x06B404, 0x06B606, 0x06BA0A, 0x06BE0E, 0x06C616, 0x06CE1E, 0x06D222,
		0x06D626, 0x06D828, 0x06DA2A, 0x06DE2E, 0x06E232, 0x06EA3A, 0x06F242, 0x06F646,
	},
	.green_address = {
		0x070454, 0x070656, 0x070858, 0x070C5C, 0x071060, 0x071868, 0x072070, 0x072474,
		0x072878, 0x072A7A, 0x072C7C, 0x073080, 0x073484, 0x073C8C, 0x074494, 0x074898,
	},
	.blue_address = {
		0x0756A6, 0x0758A8, 0x075AAA, 0x075EAE, 0x0762B2, 0x076ABA, 0x0772C2, 0x0776C6,
		0x077ACA, 0x077CCC, 0x077ECE, 0x0782D2, 0x0786D6, 0x078EDE, 0x0796E6, 0x079AEA,
	},
};

static u32 dcsda_get_lum_read_addr(struct gs_panel *ctx, u32 option, enum COLOR_INDEX color_index)
{
	struct dcsda_lum_address addr = (ctx->panel_rev_bitmask < PANEL_REV_EVT1_1) ?
					dcsda_before_evt1p1_lum_address :
					dcsda_por_lum_address;

	if (color_index == COLOR_RED)
		return addr.red_address[option];
	else if (color_index == COLOR_GREEN)
		return addr.green_address[option];
	else
		return addr.blue_address[option];
}

static ssize_t dcsda_read_luminance_data(struct gs_panel *ctx, char *buf, size_t buf_len)
{
	struct device *dev = ctx->dev;
	u32 option = to_spanel(ctx)->luminance_read_option;
	u32 total_len = ctx->desc->calibration_desc->color_cal[COLOR_DATA_TYPE_LUMINANCE].data_size;
	const size_t single_read_len = 256;
	ssize_t read_ret = -1;

	if (buf_len < total_len)
		return -EINVAL;

	PANEL_ATRACE_BEGIN(__func__);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_f1_enable);
	dcsda_prepare_color_data_read(dev);

	for (int color = 0; color < MAX_COLOR; color++) {
		read_ret = dcsda_read_flash_address(dev,
						    dcsda_get_lum_read_addr(ctx, option, color),
						    single_read_len, buf + color * single_read_len,
						    buf_len - (color * single_read_len));

		if (read_ret != single_read_len)
			goto out;
	}

	read_ret = total_len;
out:
	GS_DCS_BUF_ADD_CMD(dev, 0xC0, 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_f1_disable);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
	PANEL_ATRACE_END(__func__);
	return read_ret;
}

static ssize_t dcsda_get_color_data(struct gs_panel *ctx, char *buf, size_t buf_len)
{
	struct dcsda_panel *spanel = to_spanel(ctx);

	if (spanel->color_read_type >= COLOR_DATA_TYPE_MAX ||
	    !ctx->desc->calibration_desc->color_cal[spanel->color_read_type].en)
		return -EOPNOTSUPP;

	if (spanel->color_read_type == COLOR_DATA_TYPE_CIE)
		return dcsda_read_cie_data(ctx, buf, buf_len);
	else
		return dcsda_read_luminance_data(ctx, buf, buf_len);
}

static int dcsda_set_color_data_config(struct gs_panel *ctx, enum color_data_type read_type,
				      int option)
{
	struct dcsda_panel *spanel = to_spanel(ctx);

	spanel->color_read_type = read_type;
	if (read_type == COLOR_DATA_TYPE_LUMINANCE)
		spanel->luminance_read_option = option;
	return 0;
}

static const struct drm_panel_funcs dcsda_drm_funcs = {
	.disable = dcsda_disable,
	.unprepare = gs_panel_unprepare,
	.prepare = gs_panel_prepare_with_reset,
	.enable = dcsda_enable,
	.get_modes = gs_panel_get_modes,
	.debugfs_init = dcsda_debugfs_init,
};

static const struct gs_panel_funcs dcsda_gs_funcs = {
	.set_brightness = dcsda_set_brightness,
	.set_lp_mode = gs_panel_set_lp_mode_helper,
	.set_nolp_mode = dcsda_set_nolp_mode,
	.set_binned_lp = gs_panel_set_binned_lp_helper,
	.set_dimming = dcsda_set_dimming,
	.set_hbm_mode = dcsda_set_hbm_mode,
	.is_mode_seamless_atomic = gs_panel_is_mode_seamless_atomic_helper,
	.mode_set = dcsda_mode_set,
	.panel_init = dcsda_panel_init,
	.panel_config = dcsda_panel_config,
	.get_panel_rev = dcsda_get_panel_rev,
	.refresh_ctrl = dcsda_refresh_ctrl,
	.read_serial = gs_panel_read_slsi_ddic_id,
	.pre_update_ffc = dcsda_pre_update_ffc,
	.set_ssc_en = dcsda_set_ssc_en,
	.update_ffc = dcsda_update_ffc,
	.get_color_data = dcsda_get_color_data,
	.set_color_data_config = dcsda_set_color_data_config,
	.detect_fault = gs_panel_detect_fault_helper,
	.trace_panel_settings_lite = gs_panel_trace_settings_lite_helper,
};

const struct gs_panel_reg_ctrl_desc dcsda_reg_ctrl_desc = {
	.reg_ctrl_enable = {
		{ PANEL_REG_ID_VDDI, 0 },
		{ PANEL_REG_ID_VCI, 1 },
		{ PANEL_REG_ID_VDDD, 10 },
	},
	.reg_ctrl_pre_disable = {
		{ PANEL_REG_ID_VDDD, 0 },
	},
	.reg_ctrl_disable = {
		{ PANEL_REG_ID_VCI, 0 },
		{ PANEL_REG_ID_VDDI, 0 },
	},
};

static struct gs_panel_calibration_desc dcsda_calibration_desc = {
	.color_cal = {
		{
			.en = true,
			.data_size = 48,
			.min_option = 0,
			.max_option = 0,
		},
		{
			.en = true,
			.data_size = 768,
			.min_option = 0,
			.max_option = 15,
		},
	},
};

static const struct gs_panel_detect_fault_desc dcsda_fault_desc = {
	.errfg_reg = 0xEE,
	.errfg_len = 2,
	.dsi_err_reg = 0xE9,
	.vgh_mask = BIT(8),
	.vlin1_mask = BIT(6),
	.dsi_err_mask = BIT(0),
	.detect_interval_ms = 5000,
	.panel_errors_mask = 0UL,
};

const struct gs_panel_desc google_dcsda = {
	.data_lane_cnt = 4,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.brightness_desc = &dcsda_brightness_desc,
	.calibration_desc = &dcsda_calibration_desc,
	.modes = &dcsda_modes,
	.off_cmdset = &dcsda_off_cmdset,
	.lp_modes = &dcsda_lp_modes,
	.lp_cmdset = &dcsda_lp_cmdset,
	.binned_lp = dcsda_binned_lp,
	.num_binned_lp = ARRAY_SIZE(dcsda_binned_lp),
	.reg_ctrl_desc = &dcsda_reg_ctrl_desc,
	.panel_func = &dcsda_drm_funcs,
	.gs_panel_func = &dcsda_gs_funcs,
	.default_dsi_hs_clk_mbps = MIPI_DSI_FREQ_DEFAULT,
	.reset_timing_ms = { -1, -1, 10 },
	.fault_desc = &dcsda_fault_desc,
};

static const struct of_device_id gs_panel_of_match[] = {
	{ .compatible = "google,gs-dcsda", .data = &google_dcsda },
	{ }
};
MODULE_DEVICE_TABLE(of, gs_panel_of_match);

static struct mipi_dsi_driver gs_panel_driver = {
	.probe = dcsda_panel_probe,
	.remove = gs_dsi_panel_common_remove,
	.driver = {
		.name = "panel-gs-dcsda",
		.of_match_table = gs_panel_of_match,
	},
};
module_mipi_dsi_driver(gs_panel_driver);

MODULE_AUTHOR("Attis Chen <attis@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google dcsda panel driver");
MODULE_LICENSE("Dual MIT/GPL");
