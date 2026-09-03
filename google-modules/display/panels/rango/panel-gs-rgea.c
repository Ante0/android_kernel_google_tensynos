// SPDX-License-Identifier: MIT

#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_vblank.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/thermal.h>
#include <video/mipi_display.h>

#include "trace/panel_trace.h"

#include "gs_panel/drm_panel_funcs_defaults.h"
#include "gs_panel/gs_panel.h"
#include "gs_panel/gs_panel_funcs_defaults.h"

/* DSCv1.2a 2152x2076 */
static struct drm_dsc_config pps_config = {
	.line_buf_depth = 9,
	.bits_per_component = 8,
	.convert_rgb = true,
	.slice_width = 1076,
	.slice_height = 173,
	.slice_count = 2,
	.simple_422 = false,
	.pic_width = 2152,
	.pic_height = 2076,
	.rc_tgt_offset_high = 3,
	.rc_tgt_offset_low = 3,
	.bits_per_pixel = 128,
	.rc_edge_factor = 6,
	.rc_quant_incr_limit1 = 11,
	.rc_quant_incr_limit0 = 11,
	.initial_xmit_delay = 512,
	.initial_dec_delay = 930,
	.block_pred_enable = true,
	.first_line_bpg_offset = 15,
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
		{ .range_min_qp = 5, .range_max_qp = 10, .range_bpg_offset = 54 },
		{ .range_min_qp = 5, .range_max_qp = 11, .range_bpg_offset = 52 },
		{ .range_min_qp = 5, .range_max_qp = 11, .range_bpg_offset = 52 },
		{ .range_min_qp = 9, .range_max_qp = 12, .range_bpg_offset = 52 },
		{ .range_min_qp = 12, .range_max_qp = 13, .range_bpg_offset = 52 }
	},
	.rc_model_size = 8192,
	.flatness_min_qp = 3,
	.flatness_max_qp = 12,
	.initial_scale_value = 32,
	.scale_decrement_interval = 14,
	.scale_increment_interval = 4976,
	.nfl_bpg_offset = 179,
	.slice_bpg_offset = 75,
	.final_offset = 4320,
	.vbr_enable = false,
	.slice_chunk_size = 1076,
	.dsc_version_minor = 2,
	.dsc_version_major = 1,
	.native_422 = false,
	.native_420 = false,
	.second_line_bpg_offset = 0,
	.nsl_bpg_offset = 0,
	.second_line_offset_adj = 0,
};

#define RGEA_WRCTRLD_DIMMING_BIT 0x08
#define RGEA_WRCTRLD_BCTRL_BIT 0x20

#define MIPI_DSI_FREQ_DEFAULT 1468
#define MIPI_DSI_FREQ_ALTERNATIVE 1388

#define PROJECT "RGEA"
#define RGEA_BRIGHTNESS_20NITS 452

static const u16 WIDTH_MM = 147, HEIGHT_MM = 141;
static const u16 HDISPLAY = 2152, VDISPLAY = 2076;
static const u16 HFP = 80, HSA = 30, HBP = 38;
static const u16 VFP = 6, VSA = 4, VBP = 14;

#define RGEA_DSC {\
	.enabled = true,\
	.dsc_count = 2,\
	.cfg = &pps_config,\
}

#define RGEA_TE_USEC_120HZ_HS 321
#define RGEA_TE_USEC_60HZ_HS 8635


static const struct gs_panel_mode_array rgea_modes = GS_PANEL_MODES(
#ifdef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "2152x2076x1@1",
			DRM_MODE_TIMING(1, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = RGEA_DSC,
		},
	},
	{
		.mode = {
			.name = "2152x2076x10@10",
			DRM_MODE_TIMING(10, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = RGEA_DSC,
		},
	},
	{
		.mode = {
			.name = "2152x2076x24@24",
			DRM_MODE_TIMING(24, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = RGEA_DSC,
		},
	},
	{
		.mode = {
			.name = "2152x2076x30@30",
			DRM_MODE_TIMING(30, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = RGEA_DSC,
		},
	},
	{
		.mode = {
			.name = "2152x2076x48@48",
			DRM_MODE_TIMING(48, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = RGEA_DSC,
		},
	},
	{
		.mode = {
			.name = "2152x2076x80@80",
			DRM_MODE_TIMING(80, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = RGEA_DSC,
		},
	},
#endif /* PANEL_FACTORY_BUILD */
	{
		.mode = {
			.name = "2152x2076x60@60",
			DRM_MODE_TIMING(60, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.type = DRM_MODE_TYPE_PREFERRED,
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = RGEA_TE_USEC_60HZ_HS,
			.bpc = 8,
			.dsc = RGEA_DSC,
		},
	},
	{
		.mode = {
			.name = "2152x2076x120@120",
			DRM_MODE_TIMING(120, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = RGEA_TE_USEC_120HZ_HS,
			.bpc = 8,
			.dsc = RGEA_DSC,
		},
	},
	/* VRR modes */
	{
		.mode = {
			.name = "2152x2076x120@240",
			DRM_VRR_MODE_TIMING(120, 240, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.type = DRM_MODE_TYPE_PREFERRED,
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = RGEA_TE_USEC_120HZ_HS,
			.bpc = 8,
			.dsc = RGEA_DSC,
		},
	},
	{
		.mode = {
			.name = "2152x2076x120@120",
			DRM_VRR_MODE_TIMING(120, 120, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = RGEA_TE_USEC_120HZ_HS,
			.bpc = 8,
			.dsc = RGEA_DSC,
		},
	},
);

static const struct gs_panel_mode_array rgea_lp_modes = GS_PANEL_MODES(
	{
		.mode = {
			.name = "2152x2076x30@30",
			DRM_MODE_TIMING(30, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = RGEA_DSC,
			.is_lp_mode = true,
		},
	},
);

static const struct gs_brightness_configuration rgea_brt_configs[] = {
	{
		.panel_rev = PANEL_REV_GE((u32)PANEL_REV_PROTO1),
		.default_brightness = 1103,    /* 140 nits brightness */
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 1,
					.max = 1250,
				},
				.level = {
					.min = 2,
					.max = 2988,
				},
				.percentage = {
					.min = 0,
					.max = 67,
				},
			},
			.hbm = {
				.nits = {
					.min = 1250,
					.max = 1850,
				},
				.level = {
					.min = 2989,
					.max = 3571,
				},
				.percentage = {
					.min = 67,
					.max = 100,
				},
			},
		},
	},
};

static struct gs_panel_brightness_desc rgea_brightness_desc = {
	.max_luminance = 10000000,
	.max_avg_luminance = 10000000,
	.min_luminance = 5,
};

static const u8 test_key_enable[] = { 0xF0, 0x5A, 0x5A };
static const u8 test_key_disable[] = { 0xF0, 0xA5, 0xA5 };
static const u8 test_key_fc_enable[] = { 0xFC, 0x5A, 0x5A };
static const u8 test_key_fc_disable[] = { 0xFC, 0xA5, 0xA5 };
static const u8 panel_update[] = { 0xF7, 0x2F };
static const u8 pixel_off[] = { 0x22 };

static const struct gs_dsi_cmd rgea_lp_night_cmd[] = {
	/* 2 nits */
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1_1),
		0x51, 0x00, 0x8E),
	GS_DSI_REV_CMD(PANEL_REV_RANGE(PANEL_REV_PROTO1_1, PANEL_REV_EVT1_1),
		0x51, 0x00, 0x04),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_PROTO1_1),
		0x51, 0x04, 0xD5),
};

static const struct gs_dsi_cmd rgea_lp_low_cmd[] = {
	/* 10 nits */
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1_1),
		0x51, 0x01, 0x47),
	GS_DSI_REV_CMD(PANEL_REV_RANGE(PANEL_REV_PROTO1_1, PANEL_REV_EVT1_1),
		0x51, 0x03, 0x8A),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_PROTO1_1),
		0x51, 0x04, 0xD5),
};
static const struct gs_dsi_cmd rgea_lp_high_cmd[] = {
	/* 50 nits */
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1_1),
		0x51, 0x02, 0xB2),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1_1),
		0x51, 0x07, 0xFF),
};

static const struct gs_dsi_cmd rgea_lp_sun_cmd[] = {
	/* 150 nits */
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1_1),
		0x51, 0x04, 0x73),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1_1),
		0x51, 0x07, 0xFF),
};

static const struct gs_binned_lp rgea_binned_lp[] = {
	/* night threshold 4 nits */
	BINNED_LP_MODE_TIMING("night", 207, rgea_lp_night_cmd, 12, 12 + 50),
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 622, rgea_lp_low_cmd, 12, 12 + 50),
	/* high threshold 140 nits */
	BINNED_LP_MODE_TIMING("high", 1103, rgea_lp_high_cmd, 12, 12 + 50),
	BINNED_LP_MODE_TIMING("sun", 4095, rgea_lp_sun_cmd, 12, 12 + 50),
};

static const struct gs_dsi_cmd rgea_init_cmds[] = {
	/* Sleep out*/
	GS_DSI_DELAY_CMD(120, MIPI_DCS_EXIT_SLEEP_MODE),

	/* Enable TE */
	GS_DSI_CMD(MIPI_DCS_SET_TEAR_ON, 0x00),
	/* Fixed TE2 */
	GS_DSI_CMDLIST(test_key_enable),
	/* 51 fix ; 41 manual */
	GS_DSI_CMD(0xB9, 0x44, 0x51),
	GS_DSI_CMD(0xB0, 0x00, 0x04, 0xB9),
	GS_DSI_CMD(0xB9, 0x81, 0xE0, 0x02, 0x81, 0xE0, 0x02),
	GS_DSI_CMD(0xB0, 0x00, 0x0D, 0xB9),
	GS_DSI_CMD(0xB9, 0x02, 0x02, 0x07, 0x02, 0x02, 0x07),
	/* Early Exit Off */
	GS_DSI_CMD(0xB0, 0x00, 0x06, 0xBD),
	GS_DSI_CMD(0xBD, 0x80),
	/* Remove black screen */
	GS_DSI_CMD(0xB0, 0x00, 0x04, 0xBB),
	GS_DSI_CMD(0xBB, 0x0C),
	/* Improve NBM to AOD transition */
	GS_DSI_CMD(0xB0, 0x00, 0x03, 0xBB),
	GS_DSI_CMD(0xBB, 0x00, 0x0C),
	GS_DSI_CMD(0xB0, 0x00, 0x48, 0xF4),
	GS_DSI_CMD(0xF4, 0x73, 0x73, 0x73, 0x73),
	/* RETENTION Off */
	GS_DSI_CMDLIST(test_key_fc_enable),
	GS_DSI_CMD(0xB0, 0x00, 0xA1, 0x62),
	GS_DSI_CMD(0x62, 0x01),
	GS_DSI_CMD(0xB0, 0x00, 0x02, 0xC4),
	GS_DSI_CMD(0xC4, 0x00),
	GS_DSI_CMDLIST(test_key_fc_disable),
	/* 20 nits timing code */
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1), 0xB0, 0x00, 0x88, 0xAD),
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1), 0xAD, 0x23, 0x00, 0x04),
	GS_DSI_CMDLIST(panel_update),
	GS_DSI_CMDLIST(test_key_disable),

	/* CASET: 2151 */
	GS_DSI_CMD(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x08, 0x67),
	/* PASET: 2075 */
	GS_DSI_CMD(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x08, 0x1B),

	/* Reset WRCTRLD */
	GS_DSI_CMD(MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20),
};
static DEFINE_GS_CMDSET(rgea_init);

/**
 * struct rgea_panel - panel specific runtime info
 *
 * This struct maintains rgea panel specific runtime info, any fixed details about panel
 * should most likely go into struct gs_panel_desc
 */
struct rgea_panel {
	/** @base: base panel struct */
	struct gs_panel base;
	/**
	 * @is_pixel_off: pixel-off command is sent to panel. Only sending normal-on or resetting
	 *		  panel can recover to normal mode after entering pixel-off state.
	 */
	bool is_pixel_off;
	/** @is_higher_than_20nits: used for different timing setting **/
	bool is_higher_than_20nits;
};
#define to_spanel(ctx) container_of(ctx, struct rgea_panel, base)

static inline bool is_auto_mode_allowed(struct gs_panel *ctx)
{
	/* don't want to enable auto mode/early exit during dimming on */
	if (ctx->dimming_on)
		return false;

	if (ctx->idle_data.idle_delay_ms) {
		const unsigned int delta_ms = gs_panel_get_idle_time_delta(ctx);

		if (delta_ms < ctx->idle_data.idle_delay_ms)
			return false;
	}

	return ctx->idle_data.panel_idle_enabled;
}

static u32 rgea_get_min_idle_vrefresh(struct gs_panel *ctx,
				     const struct gs_panel_mode *pmode)
{
	const int vrefresh = drm_mode_vrefresh(&pmode->mode);
	int min_idle_vrefresh = ctx->min_vrefresh;

	if ((min_idle_vrefresh < 0) || !is_auto_mode_allowed(ctx))
		return 0;

	if (min_idle_vrefresh <= 1)
		min_idle_vrefresh = 1;
	else if (min_idle_vrefresh <= 10)
		min_idle_vrefresh = 10;
	else if (min_idle_vrefresh <= 30)
		min_idle_vrefresh = 30;
	else
		return 0;

	if (min_idle_vrefresh >= vrefresh) {
		dev_dbg(ctx->dev, "min idle vrefresh (%d) higher than target (%d)\n",
				min_idle_vrefresh, vrefresh);
		return 0;
	}

	dev_dbg(ctx->dev, "%s: min_idle_vrefresh %d\n", __func__, min_idle_vrefresh);

	return min_idle_vrefresh;
}

/**
 * rgea_set_panel_feat_te - configure TE type, width and frequency
 * @ctx: gs_panel struct
 * @is_vrr: true if it's VRR mode
 * @te_freq: TE frequency
 * @return: enum gs_panel_tex_opt, fixed or changeable TE
 *
 * Description: this function should cache commands only, don't update hw_status.
 */
static enum gs_panel_tex_opt rgea_set_panel_feat_te(struct gs_panel *ctx, bool is_vrr, u32 te_freq)
{
	struct device *dev = ctx->dev;
	const unsigned long *feat = ctx->sw_status.feat;
	enum gs_panel_tex_opt te_opt = TEX_OPT_FIXED;

	if (is_vrr || test_bit(FEAT_EARLY_EXIT, feat)) {
		if (is_vrr && te_freq == 240) {
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x41, 0x51);
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x04, 0xB9);
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x81, 0xE0, 0x02, 0x81, 0xE0, 0x02, 0x05, 0x26, 0x3D);
		} else {
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x51, 0x51);
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x04, 0xB9);
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x81, 0xE0, 0x02, 0x81, 0xE0, 0x02);
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x0D, 0xB9);
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x02, 0x02, 0x07, 0x02, 0x02, 0x07);
		}
	} else {
		te_opt = TEX_OPT_CHANGEABLE;
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x44, 0x51);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x04, 0xB9);
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x81, 0xE0, 0x02, 0x81, 0xE0, 0x02);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x0D, 0xB9);
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x02, 0x02, 0x07, 0x02, 0x02, 0x07);
	}

	return te_opt;
}

static void rgea_set_panel_feat_manual_mode_fi(struct gs_panel *ctx, u32 vrefresh, bool enabled)
{
	struct device *dev = ctx->dev;
	u8 val;

	if (vrefresh == 120)
		val = 0x00;
	else if (vrefresh == 60)
		val = 0x02;
	else if (vrefresh == 30)
		val = 0x06;
	else if (vrefresh == 10)
		val = 0x16;
	else if (vrefresh == 1)
		val = 0xEE;
	else {
		dev_warn(ctx->dev, "%s: unsupported manual mode fi freq %d\n", __func__, vrefresh);
		return;
	}

	/* Manual Mode ON */
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
	GS_DCS_BUF_ADD_CMD(dev, 0x83, 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x1B, 0xBD);
	/* Target Frequency Set */
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, val);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0xA0, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x80, 0x00, 0x00, 0x02, 0x00, 0x06, 0x00, 0x16);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0xB0, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01, 0x01, 0x03, 0x01);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x85, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
	/* Frame Insertion ON */
	if (enabled)
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x03);

	dev_info(ctx->dev, "%s: manual mode fi %s\n", __func__, enabled ? "enabled" : "disabled");
}

static void rgea_set_panel_feat_early_exit(struct gs_panel *ctx, u32 te_freq)
{
	struct device *dev = ctx->dev;
	const unsigned long *feat = ctx->sw_status.feat;
	u8 val;

	if (!test_bit(FEAT_EARLY_EXIT, feat))
		val = 0x80;
	else
		val = (te_freq == 240) ? 0x40 : 0x00;

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x06, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, val);
}

static void rgea_set_panel_feat_frequency(struct gs_panel *ctx, u32 vrefresh)
{
	struct device *dev = ctx->dev;
	const unsigned long *feat = ctx->sw_status.feat;
	u8 val;

	/* auto mode */
	if (test_bit(FEAT_FRAME_AUTO, feat)) {
		return;
	}

	/* manual mode */
	switch (vrefresh) {
	case 120:
		val = 0x00;
		break;
	case 80:
		val = 0x01;
		break;
	case 60:
		val = 0x02;
		break;
	case 30:
		val = 0x04;
		break;
	case 10:
		val = 0x06;
		break;
	case 1:
		val = 0x07;
		break;
	default:
		dev_warn(ctx->dev,
			"%s: unsupported init freq %uhz, set to default HS freq 120hz\n",
			__func__, vrefresh);
		val = 0x00;
		break;
	}

	/* Low Frequency Transition */
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
	GS_DCS_BUF_ADD_CMD(dev, 0x83, test_bit(FEAT_PWM_HIGH, feat) ? (val | 0x08) : val);
}

/**
 * rgea_set_panel_feat - configure panel features
 * @ctx: gs_panel struct
 * @pmode: gs_panel_mode struct, target panel mode
 * @enforce: force to write all of registers even if no feature state changes
 *
 * Configure panel features based on the context.
 * Note: rgea_set_panel_feat_xxx() should cache commands only while
 *       rgea_set_panel_feat() aggregates and sends the commands, and update
 *       hw_status. DO NOT update them in rgea_set_panel_feat_xxx().
 */
static void rgea_set_panel_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
				bool enforce)
{
	struct device *dev = ctx->dev;
	struct gs_panel_status *sw_status = &ctx->sw_status;
	struct gs_panel_status *hw_status = &ctx->hw_status;
	unsigned long *feat = sw_status->feat;
	enum irc_mode irc_mode = sw_status->irc_mode;
	u32 idle_vrefresh = sw_status->idle_vrefresh;
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 te_freq = gs_drm_mode_te_freq(&pmode->mode);
	bool is_vrr = gs_is_vrr_mode(pmode);
	bool irc_mode_changed, idle_vrefresh_changed, vrefresh_changed, te_freq_changed;
	char trace_msg[64];
	DECLARE_BITMAP(changed_feat, FEAT_MAX);

	/* Override settings if vrr */
	if (is_vrr) {
		if (!test_bit(FEAT_FRAME_AUTO, feat)) {
			vrefresh = idle_vrefresh ?: 1;
			idle_vrefresh = 0;
		}
	}

	/* Create bitmap of changed feature values to modify */
	if (enforce) {
		bitmap_fill(changed_feat, FEAT_MAX);
		irc_mode_changed = true;
		idle_vrefresh_changed = true;
		vrefresh_changed = true;
		te_freq_changed = true;
	} else {
		bitmap_xor(changed_feat, feat, hw_status->feat, FEAT_MAX);
		irc_mode_changed = (irc_mode != hw_status->irc_mode);
		idle_vrefresh_changed = (idle_vrefresh != hw_status->idle_vrefresh);
		vrefresh_changed = (vrefresh != hw_status->vrefresh);
		te_freq_changed = (te_freq != hw_status->te.freq_hz);
		if (bitmap_empty(changed_feat, FEAT_MAX) && !vrefresh_changed &&
		    !idle_vrefresh_changed && !te_freq_changed && !irc_mode_changed) {
			dev_dbg(dev, "no changes to panel features, skip update\n");
			return;
		}
	}

	snprintf(trace_msg, sizeof(trace_msg),
		 "feat: hbm=%u irc=%u h_pwm=%u fi=%u@a,%u@m ee=%u rr=%3u-%3u@%3u",
		 test_bit(FEAT_HBM, feat), irc_mode, test_bit(FEAT_PWM_HIGH, feat),
		 test_bit(FEAT_FRAME_AUTO, feat), test_bit(FEAT_FRAME_MANUAL_FI, feat),
		 test_bit(FEAT_EARLY_EXIT, feat), idle_vrefresh ? idle_vrefresh : vrefresh,
		 drm_mode_vrefresh(&pmode->mode), te_freq);
	dev_dbg(dev, "%s\n", trace_msg);

	/* Trace start */
	PANEL_ATRACE_BEGIN(trace_msg);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);

	sw_status->te.freq_hz = te_freq;
	if (test_bit(FEAT_EARLY_EXIT, changed_feat) || te_freq_changed)
		hw_status->te.option = rgea_set_panel_feat_te(ctx, is_vrr, te_freq);

	if (test_bit(FEAT_EARLY_EXIT, changed_feat) || vrefresh_changed || te_freq_changed)
		rgea_set_panel_feat_early_exit(ctx, te_freq);

	if (test_bit(FEAT_FRAME_MANUAL_FI, changed_feat))
		rgea_set_panel_feat_manual_mode_fi(ctx, vrefresh, test_bit(FEAT_FRAME_MANUAL_FI, feat));

	if (test_bit(FEAT_FRAME_AUTO, changed_feat) || test_bit(FEAT_PWM_HIGH, changed_feat) ||
	    idle_vrefresh_changed || vrefresh_changed)
		rgea_set_panel_feat_frequency(ctx, vrefresh);

	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
	PANEL_ATRACE_END(trace_msg);
	/* Trace end */

	hw_status->vrefresh = vrefresh;
	hw_status->idle_vrefresh = idle_vrefresh;
	hw_status->te.freq_hz = te_freq;
	bitmap_copy(hw_status->feat, feat, FEAT_MAX);
}

static void rgea_update_refresh_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
				    const u32 idle_vrefresh)
{
	struct gs_panel_status *sw_status = &ctx->sw_status;

	dev_info(ctx->dev, "%s: mode: %s set idle_vrefresh: %u\n", __func__,
		pmode->mode.name, idle_vrefresh);

	if (!gs_is_vrr_mode(pmode)) {
		u32 vrefresh = drm_mode_vrefresh(&pmode->mode);

		if (idle_vrefresh)
			set_bit(FEAT_FRAME_AUTO, sw_status->feat);
		else
			clear_bit(FEAT_FRAME_AUTO, sw_status->feat);
		if (vrefresh == 120 || idle_vrefresh)
			set_bit(FEAT_EARLY_EXIT, sw_status->feat);
		else
			clear_bit(FEAT_EARLY_EXIT, sw_status->feat);
	}

	sw_status->idle_vrefresh = idle_vrefresh;
	ctx->idle_data.panel_idle_vrefresh = idle_vrefresh;
	rgea_set_panel_feat(ctx, pmode, false);
	notify_panel_mode_changed(ctx);

	dev_info(ctx->dev, "%s: display state is notified\n", __func__);
}

static void rgea_panel_idle_notification(struct gs_panel *ctx,
		u32 display_id, u32 vrefresh, u32 idle_te_vrefresh)
{
	char event_string[64];
	char *envp[] = { event_string, NULL };
	struct drm_device *dev = ctx->bridge.dev;

	if (!dev) {
		dev_warn(ctx->dev, "%s: drm_device is null\n", __func__);
	} else {
		snprintf(event_string, sizeof(event_string),
			"PANEL_IDLE_ENTER=%u,%u,%u", display_id, vrefresh, idle_te_vrefresh);
		kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
	}
}

static void rgea_wait_one_vblank(struct gs_panel *ctx)
{
	struct drm_crtc *crtc = NULL;

	if (ctx->gs_connector->base.state)
		crtc = ctx->gs_connector->base.state->crtc;

	PANEL_ATRACE_BEGIN(__func__);
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
	PANEL_ATRACE_END(__func__);
}

static bool rgea_set_self_refresh(struct gs_panel *ctx, bool enable)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;
	u32 idle_vrefresh;

	if (unlikely(!pmode))
		return false;

	if (gs_is_vrr_mode(pmode))
		return false;

	dev_dbg(ctx->dev, "%s: %d\n", __func__, enable);

	/* self refresh is not supported in lp mode since that always makes use of early exit */
	if (pmode->gs_mode.is_lp_mode) {
		/* set 1Hz while self refresh is active, otherwise clear it */
		ctx->idle_data.panel_idle_vrefresh = enable ? 1 : 0;
		notify_panel_mode_changed(ctx);
		return false;
	}

	idle_vrefresh = rgea_get_min_idle_vrefresh(ctx, pmode);

	if (pmode->idle_mode != GIDLE_MODE_ON_SELF_REFRESH) {
		/*
		 * if idle mode is on inactivity, may need to update the target fps for auto mode,
		 * or switch to manual mode if idle should be disabled (idle_vrefresh=0)
		 */
		if ((pmode->idle_mode == GIDLE_MODE_ON_INACTIVITY) &&
			(ctx->sw_status.idle_vrefresh != idle_vrefresh)) {
			rgea_update_refresh_mode(ctx, pmode, idle_vrefresh);
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
	rgea_update_refresh_mode(ctx, pmode, idle_vrefresh);

	if (idle_vrefresh) {
		const int vrefresh = drm_mode_vrefresh(&pmode->mode);
		rgea_panel_idle_notification(ctx, 0, vrefresh, 120);
	} else if (ctx->idle_data.panel_need_handle_idle_exit) {
		/*
		 * after exit idle mode with fixed TE at non-120hz, TE may still keep at 120hz.
		 * If any layer that already be assigned to DPU that can't be handled at 120hz,
		 * panel_need_handle_idle_exit will be set then we need to wait one vblank to
		 * avoid underrun issue.
		 */
		dev_dbg(ctx->dev, "wait one vblank after exit idle\n");
		rgea_wait_one_vblank(ctx);
	}

	PANEL_ATRACE_END(__func__);

	return true;
}

static void rgea_change_frequency(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 idle_vrefresh = 0;

	if (!ctx)
		return;

	if (pmode->idle_mode == GIDLE_MODE_ON_INACTIVITY)
		idle_vrefresh = rgea_get_min_idle_vrefresh(ctx, pmode);

	if (gs_is_vrr_mode(pmode) && test_bit(FEAT_FRAME_AUTO, ctx->sw_status.feat))
		idle_vrefresh = ctx->sw_status.idle_vrefresh;

	rgea_update_refresh_mode(ctx, pmode, idle_vrefresh);
	ctx->sw_status.te.freq_hz = gs_drm_mode_te_freq(&pmode->mode);

	dev_info(ctx->dev, "change to %u hz\n", vrefresh);
}

static void rgea_update_wrctrld(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	u8 val = RGEA_WRCTRLD_BCTRL_BIT;

	if (ctx->dimming_on)
		val |= RGEA_WRCTRLD_DIMMING_BIT;

	dev_dbg(dev, "%s(wrctrld:0x%x, hbm: %d, dimming: %d)\n", __func__, val,
		GS_IS_HBM_ON(ctx->hbm_mode), ctx->dimming_on);

	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);
}

static void rgea_update_irc(struct gs_panel *ctx)
{
	u16 br = gs_panel_get_brightness(ctx);
	u32 max_brightness = ctx->desc->brightness_desc->brt_capability->hbm.level.max;

	if (br == max_brightness && GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode))
		GS_DCS_BUF_ADD_CMD_AND_FLUSH(ctx->dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0F, 0xFF);
	else {
		const u8 val1 = br >> 8;
		const u8 val2 = br & 0xff;

		GS_DCS_BUF_ADD_CMD_AND_FLUSH(ctx->dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, val1, val2);
	}
}

static int rgea_set_brightness(struct gs_panel *ctx, u16 br)
{
	u16 brightness;
	u32 max_brightness = ctx->desc->brightness_desc->brt_capability->hbm.level.max;
	struct rgea_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;

	if (ctx->current_mode && ctx->current_mode->gs_mode.is_lp_mode) {
		if (gs_panel_has_func(ctx, set_binned_lp))
			ctx->desc->gs_panel_func->set_binned_lp(ctx, br);
		return 0;
	}

	if (br == max_brightness && GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode)) {
		GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0F, 0xFF);
		return 0;
	}

	if (ctx->panel_rev_id.id >= PANEL_REVID_DVT1) {
		if (br > RGEA_BRIGHTNESS_20NITS && !spanel->is_higher_than_20nits) {
			spanel->is_higher_than_20nits = true;
			GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x88, 0xAD);
			GS_DCS_BUF_ADD_CMD(dev, 0xAD, 0x23, 0x00, 0x04);
			GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
			GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
		} else if (br <= RGEA_BRIGHTNESS_20NITS && spanel->is_higher_than_20nits) {
			spanel->is_higher_than_20nits = false;
			GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x88, 0xAD);
			GS_DCS_BUF_ADD_CMD(dev, 0xAD, 0x27, 0x00, 0x08);
			GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
			GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
		}
	}

	/* swap endianness because panel expects MSB first */
	brightness = swab16(br);

	return gs_dcs_set_brightness(ctx, brightness);
}

static void rgea_set_hbm_mode(struct gs_panel *ctx, enum gs_hbm_mode mode)
{
	if (ctx->hbm_mode == mode)
		return;

	ctx->hbm_mode = mode;

	rgea_update_irc(ctx);
	dev_info(ctx->dev, "hbm_on=%d hbm_ircoff=%d.\n", GS_IS_HBM_ON(ctx->hbm_mode),
		 GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode));
}

static void rgea_set_dimming(struct gs_panel *ctx, bool dimming_on)
{
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;

	ctx->dimming_on = dimming_on;

	if (pmode->gs_mode.is_lp_mode) {
		dev_warn(dev, "in lp mode, skip to update dimming usage\n");
		return;
	}

	rgea_update_wrctrld(ctx);
}

static void rgea_mode_set(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	rgea_change_frequency(ctx, pmode);
}

static bool rgea_is_mode_seamless_atomic(const struct gs_panel *ctx,
					 const struct gs_panel_mode *old_pmode,
					 const struct gs_panel_mode *new_pmode)
{
	const struct drm_display_mode *c = &old_pmode->mode;
	const struct drm_display_mode *n = &new_pmode->mode;

	/* seamless mode set can happen if active region resolution is same */
	return (c->vdisplay == n->vdisplay) && (c->hdisplay == n->hdisplay);
}

static int rgea_get_brightness(struct thermal_zone_device *tzd, int *temp)
{
	struct rgea_panel *spanel;

	if (tzd == NULL)
		return -EINVAL;

	spanel = thermal_zone_device_priv(tzd);

	if (spanel && spanel->base.bl) {
		mutex_lock(&spanel->base.bl_state_lock);
		*temp = backlight_get_brightness(spanel->base.bl);
		mutex_unlock(&spanel->base.bl_state_lock);
	} else {
		return -EINVAL;
	}

	return 0;
}

static struct thermal_zone_device_ops rgea_tzd_ops = {
	.get_temp = rgea_get_brightness,
};


static void rgea_debugfs_init(struct drm_panel *panel, struct dentry *root)
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

	gs_panel_debugfs_create_cmdset(csroot, &rgea_init_cmdset, "init");

	dput(csroot);
panel_out:
	dput(panel_root);
}

static int rgea_set_pwm_mode(struct gs_panel *ctx, enum gs_pwm_mode mode)
{
	struct device *dev = ctx->dev;

	if (mode == ctx->pwm_mode || ctx->panel_rev_id.id < PANEL_REVID_DVT1)
		return -EINVAL;

	if (mode != GS_PWM_RATE_STANDARD && mode != GS_PWM_RATE_HIGH) {
		dev_warn(dev, "unsupported PWM mode (%d)\n", mode);
		return -EINVAL;
	}

	dev_info(dev, "panel PWM mode %d->%d\n", ctx->pwm_mode, mode);
	ctx->pwm_mode = mode;

	if (mode == GS_PWM_RATE_HIGH)
		set_bit(FEAT_PWM_HIGH, ctx->sw_status.feat);
	else
		clear_bit(FEAT_PWM_HIGH, ctx->sw_status.feat);

	PANEL_ATRACE_BEGIN("%s(%d)", __func__, mode);

	GS_DCS_BUF_ADD_CMD(dev, 0x9F, 0xA5, 0xA5);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x07, 0xF2);
	GS_DCS_BUF_ADD_CMD(dev, 0xF2, 0x01);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x1B, 0x67);
	GS_DCS_BUF_ADD_CMD(dev, 0x67, mode == GS_PWM_RATE_HIGH ? 0x99 : 0x88);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
	GS_DCS_BUF_ADD_CMD(dev, 0x83, mode == GS_PWM_RATE_HIGH ? 0x0F : 0x07);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_disable);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x9F, 0x5A, 0x5A);
	rgea_set_panel_feat(ctx, ctx->current_mode, false);

	PANEL_ATRACE_END("%s(%d)", __func__, mode);

	return 0;
}


static void rgea_get_panel_rev(struct gs_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	u8 build_code = (id & 0xFF00) >> 8;
	u8 main = (build_code & 0xE0) >> 3;
	u8 sub = (build_code & 0x0C) >> 2;
	u8 rev = main | sub;

	switch (rev) {
	case 0x00:
	case 0x01:
		ctx->panel_rev_id.id = PANEL_REVID_PROTO1;
		break;
	case 0x02:
		ctx->panel_rev_id.id = PANEL_REVID_PROTO1_1;
		break;
	case 0x0C:
	case 0x0D:
		ctx->panel_rev_id.id = PANEL_REVID_EVT1;
		break;
	case 0x0E:
		ctx->panel_rev_id.id = PANEL_REVID_EVT1_1;
		break;
	case 0x10:
	case 0x11:
		ctx->panel_rev_id.id = PANEL_REVID_DVT1;
		break;
	case 0x12:
		ctx->panel_rev_id.id = PANEL_REVID_DVT1_1;
		break;
	default:
		dev_warn(ctx->dev, "unknown rev from panel (0x%x), default to latest\n", rev);
		ctx->panel_rev_id.id = PANEL_REVID_LATEST;
		return;
	}

	dev_info(ctx->dev, "panel_rev: 0x%x\n", ctx->panel_rev_id.id);
}

static void rgea_set_panel_lp_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;

	if (!pmode->gs_mode.is_lp_mode)
		return;

	/* Fixed 30 Hz TE*/
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x51, 0x51);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x04, 0xB9);
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x81, 0xE0, 0x02, 0x81, 0xE0, 0x02);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x0D, 0xB9);
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x02, 0x07, 0x04, 0x02, 0x07, 0x04);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);

	ctx->hw_status.vrefresh = 30;
	ctx->hw_status.te.freq_hz = 30;
	ctx->hw_status.te.option = TEX_OPT_FIXED;
}

#ifndef PANEL_FACTORY_BUILD

static void rgea_update_refresh_ctrl_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	gs_panel_refresh_ctrl_full_helper(ctx, pmode);

	if (pmode->gs_mode.is_lp_mode)
		rgea_set_panel_lp_feat(ctx, pmode);
	else
		rgea_set_panel_feat(ctx, pmode, false);
}

static void rgea_refresh_ctrl(struct gs_panel *ctx)
{
	const u32 ctrl = ctx->refresh_ctrl;
	struct device *dev = ctx->dev;

	PANEL_ATRACE_BEGIN(__func__);

	rgea_update_refresh_ctrl_feat(ctx, ctx->current_mode);

	if (ctrl & GS_PANEL_REFRESH_CTRL_FI_FRAME_COUNT_MASK) {
		dev_dbg(dev, "%s: manually inserting frame\n", __func__);
		GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
		GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
	}

	PANEL_ATRACE_END(__func__);
}
#endif

static void rgea_set_lp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;

	PANEL_ATRACE_BEGIN(__func__);

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMD(dev, 0x83, 0x00, 0x03);
	/* AOD Mode On */
	GS_DCS_BUF_ADD_CMD(dev, 0x53, 0x24);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_disable);

#ifndef PANEL_FACTORY_BUILD
	rgea_update_refresh_ctrl_feat(ctx, pmode);
#else
	rgea_set_panel_lp_feat(ctx, pmode);
#endif

	ctx->sw_status.te.freq_hz = 30;
	ctx->sw_status.te.option = TEX_OPT_FIXED;

	PANEL_ATRACE_END(__func__);

	dev_info(dev, "enter %dhz LP mode\n", drm_mode_vrefresh(&pmode->mode));
}

static void rgea_set_nolp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;

	if (!gs_is_panel_active(ctx))
		return;

	PANEL_ATRACE_BEGIN(__func__);

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMD(dev, 0x83, 0x00, 0x00);
	/* AOD Mode Off */
	GS_DCS_BUF_ADD_CMD(dev, 0x53, ctx->dimming_on ? 0x28 : 0x20);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);

#ifndef PANEL_FACTORY_BUILD
	rgea_update_refresh_ctrl_feat(ctx, pmode);
#endif
	rgea_set_panel_feat(ctx, pmode, true);
	rgea_change_frequency(ctx, pmode);

	PANEL_ATRACE_END(__func__);

	dev_info(dev, "exit LP mode\n");
}

static void rgea_disable_retention(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_enable);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0xA1, 0x62);
	GS_DCS_BUF_ADD_CMD(dev, 0x62, 0x01);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x02, 0xC4);
	GS_DCS_BUF_ADD_CMD(dev, 0xC4, 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_disable);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
}

static void rgea_enable_errfg(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x54, 0xF4);
	GS_DCS_BUF_ADD_CMD(dev, 0xF4, 0xAB);
	GS_DCS_BUF_ADD_CMD(dev, 0xE5, 0x15);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x4C, 0xF4);
	GS_DCS_BUF_ADD_CMD(dev, 0xF4, 0x10);
	GS_DCS_BUF_ADD_CMD(dev, 0xED, 0x00, 0x00, 0x51);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
}

static void rgea_panel_init(struct gs_panel *ctx)
{
	rgea_disable_retention(ctx);
	rgea_enable_errfg(ctx);

	ctx->refresh_ctrl |= GS_PANEL_REFRESH_CTRL_EARLY_EXIT;
}

static int rgea_enable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	const bool needs_reset = !gs_is_panel_enabled(ctx);

	if (!pmode) {
		dev_err(dev, "no current mode set\n");
		return -EINVAL;
	}

	dev_info(dev, "rgea enable\n");

	PANEL_ATRACE_BEGIN(__func__);

	if (needs_reset) {
		/* toggle reset gpio */
		gs_panel_reset_helper(ctx);

		/* TODO: VDDD control */

		/* initial command */
		gs_panel_send_cmdset(ctx, &rgea_init_cmdset);
		rgea_enable_errfg(ctx);
	}

	/* frequency */
	rgea_set_panel_feat(ctx, pmode, true);
	rgea_change_frequency(ctx, pmode);

	/* DSC related configuration */
	mipi_dsi_compression_mode(to_mipi_dsi_device(dev), true);
	gs_dcs_write_dsc_config(dev, &pps_config);
	/* DSC Enable */
	GS_DCS_BUF_ADD_CMD(dev, 0x9D, 0x01);

	/* dimming and HBM */
	rgea_update_wrctrld(ctx);

	if (pmode->gs_mode.is_lp_mode)
		rgea_set_lp_mode(ctx, pmode);

	GS_DCS_WRITE_CMD(dev, MIPI_DCS_SET_DISPLAY_ON);

	ctx->dsi_hs_clk_mbps = MIPI_DSI_FREQ_DEFAULT;

	PANEL_ATRACE_END(__func__);

	return 0;
}

static int rgea_disable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	int ret;

	dev_info(dev, "%s\n", __func__);

	/* skip disable sequence if going through modeset */
	if (ctx->panel_state == GPANEL_STATE_MODESET)
		return 0;

	ret = gs_panel_disable(panel);
	if (ret)
		return ret;

	/* panel register state gets reset after disabling hardware */
	bitmap_clear(ctx->hw_status.feat, 0, FEAT_MAX);
	set_bit(FEAT_EARLY_EXIT, ctx->hw_status.feat);
	ctx->hw_status.vrefresh = 60;
	ctx->sw_status.te.freq_hz = 60;
	ctx->hw_status.te.freq_hz = 60;
	ctx->hw_status.idle_vrefresh = 0;
	ctx->hw_status.acl_mode = 0;
	ctx->hw_status.dbv = 0;
	ctx->hw_status.irc_mode = IRC_FLAT_DEFAULT;

	GS_DCS_WRITE_DELAY_CMD(dev, 20, MIPI_DCS_SET_DISPLAY_OFF);

	if (ctx->panel_state == GPANEL_STATE_OFF)
		GS_DCS_WRITE_DELAY_CMD(dev, 100, MIPI_DCS_ENTER_SLEEP_MODE);
	return 0;
}

static ssize_t rgea_get_color_data(struct gs_panel *ctx, char *buf, size_t buf_len)
{
	struct device *dev = ctx->dev;
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	int read_ret = -1;
	u8 total_len = ctx->desc->calibration_desc->color_cal[COLOR_DATA_TYPE_CIE].data_size;
	/* NBM and HBM */
	u8 read_len = total_len / 2;

	if (buf_len < total_len)
		return -EINVAL;

	PANEL_ATRACE_BEGIN(__func__);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	/* Flash mode, RAM access, write enable */
	GS_DCS_BUF_ADD_CMD(dev, 0xF1, 0xF1, 0xA2);
	GS_DCS_BUF_ADD_CMD(dev, 0xC0, 0x02);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x74, 0x03, 0x00, 0x00);
	usleep_range(1000, 1100);

	GS_DCS_BUF_ADD_CMD(dev, 0xC1, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xC0, 0x03);
	usleep_range(1000, 1100);

	GS_DCS_BUF_ADD_CMD(dev, 0xC1, 0x01, 0x00, 0x02, 0x00, 0x08, 0x00, 0x00);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xC0, 0x03);
	usleep_range(15000, 15500);

	/* Set NBM read address */
	GS_DCS_BUF_ADD_CMD(dev, 0xC1, 0x6B, 0x1F, 0xF0, 0x00, 0x00, 0x00, 0x64);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xC0, 0x03);
	usleep_range(110000, 110100);

	read_ret = mipi_dsi_dcs_read(dsi, 0x6E, buf, read_len);

	/* Set HBM read address */
	GS_DCS_BUF_ADD_CMD(dev, 0xC1, 0x6B, 0x1F, 0xE0, 0x00, 0x00, 0x00, 0x64);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xC0, 0x03);
	usleep_range(110000, 110100);

	read_ret += mipi_dsi_dcs_read(dsi, 0x6E, (buf+read_len), read_len);

	GS_DCS_BUF_ADD_CMD(dev, 0xC0, 0x00);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
	PANEL_ATRACE_END(__func__);
	if (read_ret != total_len) {
		dev_warn(dev, "%s: Unable to read DDIC CIE data (%d)\n", __func__, read_ret);
		return -EINVAL;
	}
	return read_ret;
}

static int rgea_panel_probe(struct mipi_dsi_device *dsi)
{
	struct rgea_panel *spanel;
	struct gs_panel *ctx;
	int ret;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	spanel->base.pwm_mode = GS_PWM_RATE_STANDARD;
	spanel->is_pixel_off = false;
	spanel->is_higher_than_20nits = true;

	ctx = &spanel->base;
	ctx->thermal = devm_kzalloc(&dsi->dev, sizeof(*ctx->thermal), GFP_KERNEL);
	if (!ctx->thermal) {
		devm_kfree(&dsi->dev, spanel);
		return -ENOMEM;
	}

	ret = gs_dsi_panel_common_init(dsi, ctx);
	if (ret)
		return ret;

	ctx->thermal->tz =
		thermal_tripless_zone_device_register("inner_brightness",
			spanel, &rgea_tzd_ops, NULL);
	if (IS_ERR(ctx->thermal->tz)) {
		dev_warn(ctx->dev,
			"failed to register inner display tz: %ld",
			PTR_ERR(ctx->thermal->tz));
		return 0;
	}

	ret = thermal_zone_device_enable(ctx->thermal->tz);
	if (ret) {
		dev_warn(ctx->dev,
			"failed to enable inner display tz ret=%d",
			ret);
		thermal_zone_device_unregister(ctx->thermal->tz);
	}

	return 0;
}

static int rgea_panel_config(struct gs_panel *ctx)
{
	return gs_panel_update_brightness_desc(&rgea_brightness_desc, rgea_brt_configs,
					       ARRAY_SIZE(rgea_brt_configs),
					       ctx->panel_rev_bitmask);
}

static void rgea_panel_remove(struct mipi_dsi_device *dsi)
{
	const struct gs_panel *ctx = mipi_dsi_get_drvdata(dsi);

	if (ctx->thermal && ctx->thermal->tz)
		thermal_zone_device_unregister(ctx->thermal->tz);
	gs_dsi_panel_common_remove(dsi);
}

static const struct drm_panel_funcs rgea_drm_funcs = {
	.disable = rgea_disable,
	.unprepare = gs_panel_unprepare,
	.prepare = gs_panel_prepare,
	.enable = rgea_enable,
	.get_modes = gs_panel_get_modes,
	.debugfs_init = rgea_debugfs_init,
};

static const struct gs_panel_funcs rgea_gs_funcs = {
	.set_brightness = rgea_set_brightness,
	.set_lp_mode = rgea_set_lp_mode,
	.set_nolp_mode = rgea_set_nolp_mode,
	.set_binned_lp = gs_panel_set_binned_lp_helper,
	.set_dimming = rgea_set_dimming,
	.set_hbm_mode = rgea_set_hbm_mode,
	.set_self_refresh = rgea_set_self_refresh,
#ifndef PANEL_FACTORY_BUILD
	.refresh_ctrl = rgea_refresh_ctrl,
#endif
	.is_mode_seamless_atomic = rgea_is_mode_seamless_atomic,
	.mode_set = rgea_mode_set,
	.panel_config = rgea_panel_config,
	.panel_init = rgea_panel_init,
	.get_panel_rev = rgea_get_panel_rev,
	.read_serial = gs_panel_read_slsi_ddic_id,
	.get_color_data = rgea_get_color_data,
	.set_pwm_mode = rgea_set_pwm_mode,
	.detect_fault = gs_panel_detect_fault_helper,
};

const struct gs_panel_reg_ctrl_desc rgea_reg_ctrl_desc = {
	.reg_ctrl_enable = {
		{ PANEL_REG_ID_VDDI, 0 },
		{ PANEL_REG_ID_VCI, 10 },
	},
	.reg_ctrl_post_enable = {
		{ PANEL_REG_ID_VDDD, 5 },
	},
	.reg_ctrl_pre_disable = {
		{ PANEL_REG_ID_VDDD, 0 },
	},
	.reg_ctrl_disable = {
		{ PANEL_REG_ID_VCI, 0 },
		{ PANEL_REG_ID_VDDI, 0 },
	},
};

static struct gs_panel_calibration_desc rgea_calibration_desc = {
	.color_cal = {
		{
			.en = true,
			.data_size = 48,
			.min_option = 0,
			.max_option = 0,
		},
		{
			.en = false,
		},
	},
};

static const struct gs_panel_detect_fault_desc rgea_fault_desc = {
	.errfg_reg = 0xEE,
	.errfg_len = 2,
	.dsi_err_reg = 0xE9,
	.vlin1_mask = BIT(6),
	.dsi_err_mask = BIT(0),
	.detect_interval_ms = 5000,
	.panel_errors_mask = BIT(GS_PANEL_ERR_DSI_READ_FAILURE),
};

const struct gs_panel_desc google_rgea = {
	.data_lane_cnt = 4,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.brightness_desc = &rgea_brightness_desc,
	.calibration_desc = &rgea_calibration_desc,
	.modes = &rgea_modes,
	.lp_modes = &rgea_lp_modes,
	.binned_lp = rgea_binned_lp,
	.num_binned_lp = ARRAY_SIZE(rgea_binned_lp),
	.reg_ctrl_desc = &rgea_reg_ctrl_desc,
	.panel_func = &rgea_drm_funcs,
	.gs_panel_func = &rgea_gs_funcs,
	.default_dsi_hs_clk_mbps = MIPI_DSI_FREQ_DEFAULT,
	.reset_timing_ms = { 1, 1, 10 },
	.fault_desc = &rgea_fault_desc,
};

static const struct of_device_id gs_panel_of_match[] = {
	{ .compatible = "google,gs-rgea", .data = &google_rgea },
	{ }
};
MODULE_DEVICE_TABLE(of, gs_panel_of_match);

static struct mipi_dsi_driver gs_panel_driver = {
	.probe = rgea_panel_probe,
	.remove = rgea_panel_remove,
	.driver = {
		.name = "panel-gs-rgea",
		.of_match_table = gs_panel_of_match,
	},
};
module_mipi_dsi_driver(gs_panel_driver);

MODULE_AUTHOR("Derick Hong <derickhong@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google rgea panel driver");
MODULE_LICENSE("Dual MIT/GPL");

