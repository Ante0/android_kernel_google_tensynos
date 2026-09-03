// SPDX-License-Identifier: MIT

#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_vblank.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/string.h>
#include <linux/thermal.h>
#include <video/mipi_display.h>

#include "trace/panel_trace.h"

#include "gs_panel/drm_panel_funcs_defaults.h"
#include "gs_panel/gs_panel.h"
#include "gs_panel/gs_panel_funcs_defaults.h"

/* PPS Settings DSC 1.2a (8 bits) */
static struct drm_dsc_config pps_config = {
	.line_buf_depth = 9,
	.bits_per_component = 8,
	.convert_rgb = true,
	.slice_width = 540,
	.slice_height = 1171,
	.simple_422 = false,
	.pic_width = 1080,
	.pic_height = 2342,
	.rc_tgt_offset_high = 3,
	.rc_tgt_offset_low = 3,
	.bits_per_pixel = 128,
	.rc_edge_factor = 6,
	.rc_quant_incr_limit1 = 11,
	.rc_quant_incr_limit0 = 11,
	.initial_xmit_delay = 512,
	.initial_dec_delay = 594,
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
		{.range_min_qp = 5, .range_max_qp = 10, .range_bpg_offset = 54},
		{.range_min_qp = 5, .range_max_qp = 11, .range_bpg_offset = 52},
		{.range_min_qp = 5, .range_max_qp = 11, .range_bpg_offset = 52},
		{.range_min_qp = 9, .range_max_qp = 12, .range_bpg_offset = 52},
		{.range_min_qp = 12, .range_max_qp = 13, .range_bpg_offset = 52}
	},
	.rc_model_size = 8192,
	.flatness_min_qp = 3,
	.flatness_max_qp = 12,
	.initial_scale_value = 32,
	.scale_decrement_interval = 7,
	.scale_increment_interval = 25371,
	.nfl_bpg_offset = 27,
	.slice_bpg_offset = 23,
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

#define IYIDB_PPS_LEN 89
static const u8 iyidb_pps_tb[IYIDB_PPS_LEN] = {
	0x12, 0x00, 0x00, 0x89, 0x30, 0x80, 0x09, 0x26, 0x04, 0x38,
	0x04, 0x93, 0x02, 0x1c, 0x02, 0x1c, 0x02, 0x00, 0x02, 0x52,
	0x00, 0x20, 0x63, 0x1b, 0x00, 0x07, 0x00, 0x0f, 0x00, 0x1b,
	0x00, 0x17, 0x18, 0x00, 0x10, 0xf0, 0x03, 0x0c, 0x20, 0x00,
	0x06, 0x0b, 0x0b, 0x33, 0x0e, 0x1c, 0x2a, 0x38, 0x46, 0x54,
	0x62, 0x69, 0x70, 0x77, 0x79, 0x7b, 0x7d, 0x7e, 0x01, 0x02,
	0x01, 0x00, 0x09, 0x40, 0x09, 0xbe, 0x19, 0xfc, 0x19, 0xfa,
	0x19, 0xf8, 0x1a, 0x38, 0x1a, 0x78, 0x1a, 0xb6, 0x2a, 0xb6,
	0x2a, 0xf4, 0x2a, 0xf4, 0x4b, 0x34, 0x63, 0x74, 0x00
};

#define IYIDB_WRCTRLD_DIMMING_BIT 0x08
#define IYIDB_WRCTRLD_BCTRL_BIT 0x20

#define MIPI_DSI_FREQ_DEFAULT 865
#define MIPI_DSI_FREQ_ALTERNATIVE 756

#define IYIDB_5NITS_DBV 980

#define PROJECT "IYIDB"

static const u16 WIDTH_MM = 68, HEIGHT_MM = 148;

static const u16 HDISPLAY = 1080, VDISPLAY = 2342;
static const u16 HFP = 44, HSA = 16, HBP = 20;
static const u16 VFP = 22, VSA = 0, VBP = 36;

#define IYIDB_DSC { .enabled = true, \
	.dsc_count = 2, .cfg = &pps_config, }

static const struct gs_panel_mode_array iyidb_modes = GS_PANEL_MODES(
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1080x2342x120@120",
			DRM_VRR_MODE_TIMING(120, 120, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.type = DRM_MODE_TYPE_PREFERRED,
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 275,
			.bpc = 8,
			.dsc = IYIDB_DSC,
		},
	},
#endif
	{
		.mode = {
			.name = "1080x2342x120@240",
			DRM_VRR_MODE_TIMING(120, 240, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 275,
			.bpc = 8,
			.dsc = IYIDB_DSC,
		},
	},
	{
		.mode = {
			.name = "1080x2342x60@60",
			DRM_VRR_MODE_TIMING(60, 60, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.flags = DRM_MODE_FLAG_NS,
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 533,
			.bpc = 8,
			.dsc = IYIDB_DSC,
		},
	},
);

static const struct gs_panel_mode_array iyidb_lp_modes = GS_PANEL_MODES(
	{
		.mode = {
			.name = "1080x2342x30@30",
			DRM_MODE_TIMING(30, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.bpc = 8,
			.dsc = IYIDB_DSC,
			.is_lp_mode = true,
		},
	},
);

static const struct gs_brightness_configuration iyidb_brt_configs[] = {
	{
		.panel_rev = PANEL_REV_GE((u32)PANEL_REV_PROTO1),
		.default_brightness = 4463,	/* 140 nits brightness */
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 1,
					.max = 1450,
				},
				.level = {
					.min = 345,
					.max = 12908,
				},
				.percentage = {
					.min = 0,
					.max = 59,
				},
			},
			.hbm = {
				.nits = {
					.min = 1450,
					.max = 2450,
				},
				.level = {
					.min = 12909,
					.max = 16383,
				},
				.percentage = {
					.min = 59,
					.max = 100,
				},
			},
		},
	},
};

static struct gs_panel_brightness_desc iyidb_brightness_desc = {
	.max_luminance = 10000000,
	.max_avg_luminance = 10000000,
	.min_luminance = 5,
};

static const u8 test_key_enable[] = { 0xF0, 0x5A, 0x5A };
static const u8 test_key_disable[] = { 0xF0, 0xA5, 0xA5 };
static const u8 test_key_fc_enable[] = { 0xFC, 0x5A, 0x5A };
static const u8 test_key_fc_disable[] = { 0xFC, 0xA5, 0xA5 };
static const u8 panel_update[] = { 0xF7, 0x0F };
static const u8 manual_mode_on[] = { 0xBD, 0xB1 };
static const u8 auto_mode_on[] = { 0xBD, 0xBD };
static const u8 hlpm_global_param[] = {0xB0, 0x00, 0x02, 0x51};

static const struct gs_dsi_cmd iyidb_lp_night_cmd[] = {
	/* 1 nits */
	GS_DSI_QUEUE_CMDLIST(hlpm_global_param),
	GS_DSI_FLUSH_CMD(0x51, 0x01, 0xD8),
};

static const struct gs_dsi_cmd iyidb_lp_low_cmd[] = {
	/* 10 nits */
	GS_DSI_QUEUE_CMDLIST(hlpm_global_param),
	GS_DSI_FLUSH_CMD(0x51, 0x05, 0x41),
};

static const struct gs_dsi_cmd iyidb_lp_high_cmd[] = {
	/* 50 nits */
	GS_DSI_QUEUE_CMDLIST(hlpm_global_param),
	GS_DSI_FLUSH_CMD(0x51, 0x0A, 0xB5),
};

static const struct gs_dsi_cmd iyidb_lp_sun_cmd[] = {
	/* 150 nits */
	GS_DSI_QUEUE_CMDLIST(hlpm_global_param),
	GS_DSI_FLUSH_CMD(0x51, 0x11, 0xFB),
};

static const struct gs_binned_lp iyidb_binned_lp[] = {
	/* night threshold 4 nits */
	BINNED_LP_MODE("night", 887, iyidb_lp_night_cmd),
	/* low threshold 40 nits */
	BINNED_LP_MODE("low", 2523, iyidb_lp_low_cmd),
	/* high threshold 140 nits */
	BINNED_LP_MODE("high", 4459, iyidb_lp_high_cmd),
	BINNED_LP_MODE("sun", 16383, iyidb_lp_sun_cmd),
};

static const struct gs_dsi_cmd iyidb_init_cmds[] = {
	/* TE ON */
	GS_DSI_QUEUE_CMD(MIPI_DCS_SET_TEAR_ON, 0x00),

	/* SPI Speed 3 Divider Settings (EVT1.0 only) */
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_EVT1, test_key_fc_enable),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0x67, 0x11, 0x02),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0x66),
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_EVT1, test_key_fc_disable),

	/* Update key */
	GS_DSI_QUEUE_CMDLIST(panel_update),

	/* b/436184694: Sleep out with AVDD deviation fix (P1.0 and P1.1 only) */
	GS_DSI_FLUSH_DELAY_REV_CMD(15, PANEL_REV_LT(PANEL_REV_EVT1), MIPI_DCS_EXIT_SLEEP_MODE),
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_LT(PANEL_REV_EVT1), test_key_fc_enable),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xB0, 0x00, 0x47, 0xFE),
	GS_DSI_FLUSH_DELAY_REV_CMD(1, PANEL_REV_LT(PANEL_REV_EVT1), 0xFE, 0x40),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xB0, 0x00, 0x47, 0xFE),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xFE, 0x00),
	GS_DSI_FLUSH_DELAY_REV_CMDLIST(165, PANEL_REV_LT(PANEL_REV_EVT1), test_key_fc_disable),

	/* Sleep out (>= EVT) */
	GS_DSI_FLUSH_DELAY_REV_CMD(180, PANEL_REV_GE(PANEL_REV_EVT1), MIPI_DCS_EXIT_SLEEP_MODE),

	/* VGL -5.0V setting (EVT1.0 only) */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0xB0, 0x00, 0x02, 0x48),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0x48, 0x08, 0x10),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0xB0, 0x02, 0x32, 0x48),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0x48, 0x20),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0xB0, 0x00, 0x57, 0x48),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0x48, 0x10),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0xB0, 0x02, 0x09, 0x48),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0x48, 0x28),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0xB0, 0x01, 0xB9, 0x48),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0x48, 0xE1),

	/* Scaler settings (default) */
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMD(0xC3, 0x02),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x01, 0xC3),
	GS_DSI_QUEUE_CMD(0xC3, 0x00, 0x26, 0xD0, 0x1A),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x19, 0xC3),
	GS_DSI_QUEUE_CMD(0xC3, 0x00, 0xAA, 0xAA, 0xAB, 0xAA, 0xBD, 0x52),

	/* CASET / PASET (default) */
	GS_DSI_QUEUE_CMD(0x2A, 0x00, 0x00, 0x04, 0x37),
	GS_DSI_QUEUE_CMD(0x2B, 0x00, 0x00, 0x09, 0x25),

	/* b/424705433: SPR settings (P1.0 only) */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_PROTO1_1), 0xB0, 0x00, 0x0D, 0x86),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_PROTO1_1), 0x86, 0xB3, 0x00, 0xB3, 0x00),

	/* b/426117417: VINT2 delay (P1.0 only) */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_PROTO1_1), 0xB0, 0x00, 0x09, 0xF4),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_PROTO1_1), 0xF4, 0x00, 0x00, 0x00, 0x00),

	/* b/432614518: VGL fast charge off (P1.0 and P1.1 only) */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xB0, 0x01, 0xE4, 0x48),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0x48, 0x21),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xB0, 0x01, 0x7A, 0x48),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0x48, 0xF3),

	/* b/451930111: DBI (deburn-in) IP disable (P1.0 and P1.1 only) */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0x8F, 0x00, 0x00),

	/* AOD default DVDD change setting (EVT1.0 only) */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0xB0, 0x00, 0x51, 0xFD),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0xFD, 0x30),

	/* H-Line improvement setting (EVT1.0 only) */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0xB0, 0x00, 0x25, 0xCE),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0xCE, 0x01, 0x00),

	/* DBI edge line improvement (EVT1.0 only) */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0xB0, 0x00, 0xA4, 0x8F),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1, 0x8F, 0x00, 0x1A, 0x5E, 0xA2),

	/* VGL-VCL shift time setting */
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x54, 0xEF),
	GS_DSI_QUEUE_CMD(0xEF, 0x8A),

	/* b/475040200: first frame drop improvement (EVT1.1 only) */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1_1, 0xB0, 0x00, 0x3A, 0xA3),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1_1, 0xA3, 0x03),

	/* b/505603646, b/507236714: refine demura settings (>= DVT) */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xFC, 0x5A, 0x5A),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x59, 0x01,
			     0x76, 0x70, 0x28, 0x54, 0x0F, 0x32, 0xC7, 0x2D, 0xE3, 0x47, 0x0D,
			     0x87, 0x6C, 0xF2, 0xFF, 0x0F),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x10, 0x00, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x00, 0x00, 0x00, 0x03, 0x03, 0x03, 0x0B, 0x0B, 0x0B, 0x16,
			     0x16, 0x16, 0x1E, 0x1E, 0x1E, 0x32, 0x32, 0x32, 0xC8, 0xC8, 0xC8,
			     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE1, 0xE1, 0xE1, 0xC8, 0xC8,
			     0xC8, 0xC8, 0xC8, 0xC8, 0x00, 0x00, 0x00, 0x03, 0x03, 0x03, 0x0B,
			     0x0B, 0x0B, 0x16, 0x16, 0x16, 0x1E, 0x1E, 0x1E, 0x32, 0x32, 0x32,
			     0x78, 0x78, 0x78, 0xB9, 0xB9, 0xB9, 0xB9, 0xB9, 0xB9, 0xB0, 0xB0,
			     0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0x00, 0x00, 0x00, 0x03,
			     0x03, 0x03, 0x0B, 0x0B, 0x0B, 0x16, 0x16, 0x16, 0x1E, 0x1E, 0x1E,
			     0x32, 0x32, 0x32, 0x78, 0x78, 0x78, 0x80, 0x80, 0x80, 0x80, 0x80,
			     0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00,
			     0x00, 0x00, 0x03, 0x03, 0x03, 0x0B, 0x0B, 0x0B, 0x16, 0x16, 0x16,
			     0x1E, 0x1E, 0x1E, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50,
			     0x50, 0x46, 0x46, 0x46, 0x32, 0x32, 0x32, 0x50, 0x50, 0x50, 0x50,
			     0x50, 0x50, 0x00, 0x00, 0x00, 0x05, 0x05, 0x05, 0x14, 0x14, 0x14,
			     0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x24, 0x24,
			     0x24, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x40,
			     0x40, 0x40, 0x48, 0x48, 0x48, 0x00, 0x00, 0x00, 0x04, 0x04, 0x04,
			     0x0E, 0x0E, 0x0E, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D,
			     0x1D, 0x18, 0x18, 0x18, 0x1A, 0x1A, 0x1A, 0x1A, 0x1A, 0x1A, 0x28,
			     0x28, 0x28, 0x3E, 0x3E, 0x3E, 0x46, 0x46, 0x46, 0x00, 0x00, 0x00,
			     0x02, 0x02, 0x02, 0x08, 0x08, 0x08, 0x10, 0x10, 0x10, 0x10, 0x10,
			     0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x14,
			     0x14, 0x14, 0x28, 0x28, 0x28, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C,
			     0x00, 0x00, 0x00, 0x02, 0x02, 0x02, 0x05, 0x05, 0x05, 0x0A, 0x0A,
			     0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B, 0x0B, 0x11,
			     0x11, 0x11, 0x16, 0x16, 0x16, 0x1E, 0x1E, 0x1E, 0x26, 0x26, 0x26,
			     0x2C, 0x2C, 0x2C, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x04, 0x04,
			     0x04, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x06, 0x06, 0x06, 0x08,
			     0x08, 0x08, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
			     0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x04, 0x04, 0x04, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x05,
			     0x05, 0x05, 0x09, 0x09, 0x09, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
			     0x10, 0x10, 0x10, 0x0C, 0x0C, 0x0C, 0x00, 0x00, 0x00),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x11, 0x68, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0xFF,
			     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			     0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xA0, 0xA0,
			     0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
			     0x08, 0x08, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0,
			     0xF8, 0xF8, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xB8, 0xB8,
			     0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x08, 0x08, 0x08, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78,
			     0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xB8, 0xB8,
			     0xB8, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x50, 0x50, 0x50,
			     0x50, 0x50, 0x50, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x70, 0x70,
			     0x70, 0x70, 0x70, 0x70, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08,
			     0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x20, 0x20,
			     0x20, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x10, 0x10, 0x10, 0x18, 0x18, 0x18, 0x20, 0x20, 0x20, 0x20,
			     0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x11, 0x23, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x00),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x11, 0x24, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x00),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x11, 0x25, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x00),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x11, 0x26, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x0A),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x11, 0x27, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x0A),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x11, 0x28, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x0A),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x13, 0xF3, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x00),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x13, 0xF4, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x00),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x13, 0xF5, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x00),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x13, 0xF6, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x30),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x13, 0xF7, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x30),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xB0, 0x13, 0xF8, 0x96),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0x96, 0x30),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1),
			     0xFC, 0xA5, 0xA5),

	/* b/516652338: HBM display off flashing improvement set */
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x09, 0xF4),
	GS_DSI_QUEUE_CMD(0xF4, 0x00, 0x02, 0x02, 0x02),

	/* b/498721617: Halo flicker improvement set */
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x31, 0xA3),
	GS_DSI_QUEUE_CMD(0xA3, 0x04, 0x04, 0x04),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_init);

static const struct gs_dsi_cmd iyidb_fixed_te_240_gating_cmds[] = {
	/* TE settings (fixed, 240 Hz, gating) */
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB0, 0x00, 0x01, 0xBD),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xBD, 0x05),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x29, 0x29),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x29, 0x29, 0x19, 0x19, 0x19),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x32, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x84, 0x84),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x84, 0x84, 0x09, 0x09, 0x09),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x50, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x83, 0x83),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x84, 0x84, 0x34, 0x34, 0x34),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x55, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x23, 0x23),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x24, 0x24, 0x24, 0x24, 0x24),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x3C, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x04, 0x04),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x04, 0x04, 0x09, 0x09, 0x09),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x5A, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x83, 0x83),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x84, 0x84, 0x34, 0x34, 0x34),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x5F, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x23, 0x23),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x24, 0x24, 0x24, 0x24, 0x24),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x6E, 0xB9),
	GS_DSI_QUEUE_CMD(0xB9, 0x00),
	GS_DSI_QUEUE_CMDLIST(panel_update),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_fixed_te_240_gating);

static const struct gs_dsi_cmd iyidb_fixed_te_no_gating_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	/* TE settings (fixed, NBM 120 Hz, HLPM 30 Hz, no gating) */
	GS_DSI_QUEUE_CMD(0xB9, 0x19, 0x19, 0x19, 0x19, 0x19),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x32, 0xB9),
	GS_DSI_QUEUE_CMD(0xB9, 0x09, 0x09, 0x09, 0x09, 0x09),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x50, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x33, 0x33, 0x33, 0x33, 0x33),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x34, 0x34, 0x34, 0x34, 0x34),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x55, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x23, 0x23, 0x23, 0x23, 0x23),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x24, 0x24, 0x24, 0x24, 0x24),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x6E, 0xB9),
	GS_DSI_QUEUE_CMD(0xB9, 0x00),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_fixed_te_no_gating);

static const struct gs_dsi_cmd iyidb_changeable_te_cmds[] = {
	/* TE settings (changeable) */
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMD(0xB9, 0x04, 0x04, 0x04, 0x04, 0x04),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x1E, 0xB9),
	GS_DSI_QUEUE_CMD(0xB9, 0x09, 0x09, 0x09, 0x09, 0x09),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x28, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x33, 0x33, 0x33, 0x33, 0x33),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x34, 0x34, 0x34, 0x34, 0x34),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x2D, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x23, 0x23, 0x23, 0x23, 0x23),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x24, 0x24, 0x24, 0x24, 0x24),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x6E, 0xB9),
	GS_DSI_QUEUE_CMD(0xB9, 0x00),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_changeable_te);

static const struct gs_dsi_cmdset *get_iyidb_te_cmdset(struct gs_panel *ctx, u32 te_freq)
{
	if (ctx->sw_status.te.option == TEX_OPT_CHANGEABLE)
		return &iyidb_changeable_te_cmdset;

	if (te_freq == 240)
		return &iyidb_fixed_te_240_gating_cmdset;

	return &iyidb_fixed_te_no_gating_cmdset;
}

static const struct gs_dsi_cmd iyidb_fixed_te2_240_cmds[] = {
	/* TE2 settings (fixed, 240Hz) */
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x0A, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x29, 0x29),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x29, 0x29, 0x19, 0x19, 0x19),
	GS_DSI_QUEUE_CMD(0xB0, 0x01, 0x18, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x24, 0x24),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x25, 0x25, 0x25, 0x25, 0x25),
	GS_DSI_QUEUE_CMD(0xB0, 0x01, 0x1D, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x50, 0x50),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x51, 0x51, 0x51, 0x51, 0x51),
	GS_DSI_QUEUE_CMD(0xB0, 0x01, 0x22, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x24, 0x24),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x25, 0x25, 0x25, 0x25, 0x25),
	GS_DSI_QUEUE_CMD(0xB0, 0x01, 0x27, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x50, 0x50),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x51, 0x51, 0x51, 0x51, 0x51),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_fixed_te2_240);

static const struct gs_dsi_cmd iyidb_fixed_te2_120_or_30_cmds[] = {
	/* TE2 settings (fixed, NBM 120 Hz, HLPM 30 Hz) */
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x0A, 0xB9),
	GS_DSI_QUEUE_CMD(0xB9, 0x19, 0x19, 0x19, 0x19, 0x19),
	GS_DSI_QUEUE_CMD(0xB0, 0x01, 0x18, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x24, 0x24, 0x24, 0x24, 0x24),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x25, 0x25, 0x25, 0x25, 0x25),
	GS_DSI_QUEUE_CMD(0xB0, 0x01, 0x1D, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x50, 0x50, 0x50, 0x50, 0x50),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x51, 0x51, 0x51, 0x51, 0x51),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_fixed_te2_120_or_30);

static const struct gs_dsi_cmd iyidb_changeable_te2_cmds[] = {
	/* TE2 settings (changeable) */
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x0A, 0xB9),
	GS_DSI_QUEUE_CMD(0xB9, 0x04, 0x04, 0x04, 0x04, 0x04),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0xF0, 0xB9),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x24, 0x24, 0x24, 0x24, 0x24),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x25, 0x25, 0x25, 0x25, 0x25),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0xF5, 0xB9),
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB9, 0x50, 0x50, 0x50, 0x50, 0x50),
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB9, 0x51, 0x51, 0x51, 0x51, 0x51),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_changeable_te2);

static const struct gs_dsi_cmdset *get_iyidb_te2_cmdset(struct gs_panel *ctx,
		bool force_changeable)
{
	/* TE2 settings overwrite */
	if (force_changeable)
		return &iyidb_changeable_te2_cmdset;

	/* changeable TE2 */
	if (ctx->te2.option == TEX_OPT_CHANGEABLE)
		return &iyidb_changeable_te2_cmdset;

	/* fixed TE2 */
	if (ctx->te2.freq_hz == 240)
		return &iyidb_fixed_te2_240_cmdset;
	else
		return &iyidb_fixed_te2_120_or_30_cmdset;
}

static const struct gs_dsi_cmd iyidb_ffc_off_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_fc_enable),
	GS_DSI_QUEUE_CMD(0xC5, 0xBC),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x24, 0xC5),
	GS_DSI_QUEUE_CMD(0xC5, 0xBC),
	GS_DSI_FLUSH_CMDLIST(test_key_fc_disable),
};
static DEFINE_GS_CMDSET(iyidb_ffc_off);

/* placeholder, modified in get_iyidb_ffc_on_cmdset */
static u8 iyidb_ffc_on_osc1_cmd[] = {0xC5, 0x12, 0xDE};
static u8 iyidb_ffc_on_osc2_cmd[] = {0xC5, 0x09, 0x6F};

static const struct gs_dsi_cmd iyidb_ffc_on_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_fc_enable),
	/* FFC On @ OSC1 102Mhz */
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x05, 0xC5),
	GS_DSI_QUEUE_CMD(0xC5, 0x40),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x08, 0xC5),
	GS_DSI_QUEUE_CMDLIST(iyidb_ffc_on_osc1_cmd),
	GS_DSI_QUEUE_CMD(0xC5, 0xBD),
	/* FFC On @ OSC2 51Mhz */
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x29, 0xC5),
	GS_DSI_QUEUE_CMD(0xC5, 0x40),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x2C, 0xC5),
	GS_DSI_QUEUE_CMDLIST(iyidb_ffc_on_osc2_cmd),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x24, 0xC5),
	GS_DSI_QUEUE_CMD(0xC5, 0xBD),
	GS_DSI_FLUSH_CMDLIST(test_key_fc_disable),
};
static DEFINE_GS_CMDSET(iyidb_ffc_on);

static const struct gs_dsi_cmdset *get_iyidb_ffc_on_cmdset(unsigned int hs_clk_mbps)
{
	static const u8 osc1_freq_default_cmd[] = {0xC5, 0x12, 0xDE};
	static const u8 osc1_freq_alternative_cmd[] = {0xC5, 0x15, 0x96};
	static const u8 osc2_freq_default_cmd[] = {0xC5, 0x09, 0x6F};
	static const u8 osc2_freq_alternative_cmd[] = {0xC5, 0x0A, 0xCB};

	CHECK_CMDLIST_EQ_SIZE(iyidb_ffc_on_osc1_cmd, osc1_freq_default_cmd);
	CHECK_CMDLIST_EQ_SIZE(iyidb_ffc_on_osc1_cmd, osc1_freq_alternative_cmd);
	CHECK_CMDLIST_EQ_SIZE(iyidb_ffc_on_osc2_cmd, osc2_freq_default_cmd);
	CHECK_CMDLIST_EQ_SIZE(iyidb_ffc_on_osc2_cmd, osc2_freq_alternative_cmd);

	switch (hs_clk_mbps) {
	case MIPI_DSI_FREQ_DEFAULT:
		memcpy(iyidb_ffc_on_osc1_cmd, osc1_freq_default_cmd,
				sizeof(iyidb_ffc_on_osc1_cmd));
		memcpy(iyidb_ffc_on_osc2_cmd, osc2_freq_default_cmd,
				sizeof(iyidb_ffc_on_osc2_cmd));
		break;
	case MIPI_DSI_FREQ_ALTERNATIVE:
		memcpy(iyidb_ffc_on_osc1_cmd, osc1_freq_alternative_cmd,
				sizeof(iyidb_ffc_on_osc1_cmd));
		memcpy(iyidb_ffc_on_osc2_cmd, osc2_freq_alternative_cmd,
				sizeof(iyidb_ffc_on_osc2_cmd));
		break;
	default:
		memcpy(iyidb_ffc_on_osc1_cmd, osc1_freq_default_cmd,
				sizeof(iyidb_ffc_on_osc1_cmd));
		memcpy(iyidb_ffc_on_osc2_cmd, osc2_freq_default_cmd,
				sizeof(iyidb_ffc_on_osc2_cmd));
		break;
	}

	return &iyidb_ffc_on_cmdset;
}

/* placeholder, modified in get_iyidb_early_exit_[lp]_cmdset */
static u8 iyidb_early_exit_setting_cmd[] = {0xBD, 0x00};

static const struct gs_dsi_cmd iyidb_early_exit_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x01, 0xBD),
	GS_DSI_QUEUE_CMDLIST(iyidb_early_exit_setting_cmd),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_early_exit);

static const struct gs_dsi_cmdset *get_iyidb_early_exit_cmdset(struct gs_panel *ctx,
							       u32 te_freq,
							       bool use_early_exit)
{
	/* Early exit ON / OFF (Normal mode) */
	if (!use_early_exit || ctx->sw_status.te.option == TEX_OPT_CHANGEABLE)
		iyidb_early_exit_setting_cmd[1] = 0x02; /* OFF: vsync update */
	else if (te_freq == 240)
		iyidb_early_exit_setting_cmd[1] = 0x01; /* ON: base 240hz update */
	else
		iyidb_early_exit_setting_cmd[1] = 0x00; /* ON: base 120hz update or NS mode */

	return &iyidb_early_exit_cmdset;
}

static const struct gs_dsi_cmdset *get_iyidb_early_exit_lp_cmdset(struct gs_panel *ctx,
								  bool use_early_exit)
{
	/* Early exit ON / OFF (HLPM) */
	if (use_early_exit)
		iyidb_early_exit_setting_cmd[1] = 0x00;
	else if (ctx->panel_rev_id.id < PANEL_REVID_EVT1)
		iyidb_early_exit_setting_cmd[1] = 0x20;
	else
		iyidb_early_exit_setting_cmd[1] = 0x08;

	return &iyidb_early_exit_cmdset;
}


/* placeholder, modified in get_iyidb_wrctrl_cmdset */
static u8 iyidb_wrctrl_cmd[] = {MIPI_DCS_WRITE_CONTROL_DISPLAY, IYIDB_WRCTRLD_BCTRL_BIT};

static const struct gs_dsi_cmd iyidb_wrctrl_cmds[] = {
	GS_DSI_FLUSH_CMDLIST(iyidb_wrctrl_cmd),
};
static DEFINE_GS_CMDSET(iyidb_wrctrl);

static const struct gs_dsi_cmdset *get_iyidb_wrctrl_cmdset(bool dimming_on)
{
	/* normal mode */
	iyidb_wrctrl_cmd[1] = IYIDB_WRCTRLD_BCTRL_BIT;

	if (dimming_on)
		iyidb_wrctrl_cmd[1] |= IYIDB_WRCTRLD_DIMMING_BIT;

	return &iyidb_wrctrl_cmdset;
}

/* placeholder, modified in get_iyidb_low_frequency_change_manual_cmdset */
static u8 iyidb_manual_freq_set_cmd[] = {0x16, 0x00};

static const struct gs_dsi_cmd iyidb_low_frequency_change_manual_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMDLIST(manual_mode_on),
	GS_DSI_QUEUE_CMDLIST(iyidb_manual_freq_set_cmd),
	GS_DSI_QUEUE_CMDLIST(panel_update),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_low_frequency_change_manual);

static const struct gs_dsi_cmdset *get_iyidb_low_frequency_change_manual_cmdset(
		struct gs_panel *ctx, u32 vrefresh, bool is_high_pwm, bool is_ns)
{
	/* NS mode */
	if (is_ns) {
		if (vrefresh == 60) {
			iyidb_manual_freq_set_cmd[1] = 0x20;
		} else if (vrefresh == 30) {
			iyidb_manual_freq_set_cmd[1] = 0x21;
		} else if (vrefresh == 10) {
			iyidb_manual_freq_set_cmd[1] = 0x22;
		} else if (vrefresh == 1) {
			iyidb_manual_freq_set_cmd[1] = 0x23;
		} else {
			dev_warn(ctx->dev,
				"unsupported manual NS freq %uhz, fallback to 60hz\n",
				vrefresh);
			iyidb_manual_freq_set_cmd[1] = 0x20;
		}

		return &iyidb_low_frequency_change_manual_cmdset;
	}

	/* HS mode: High PWM, Standard (proto), Hybrid (>= EVT) */
	if (vrefresh == 120) {
		iyidb_manual_freq_set_cmd[1] = 0x00;
	} else if (vrefresh == 80) {
		iyidb_manual_freq_set_cmd[1] = 0x01;
	} else if (vrefresh == 60) {
		iyidb_manual_freq_set_cmd[1] = 0x02;
	} else if (vrefresh == 48) {
		iyidb_manual_freq_set_cmd[1] = 0x03;
	} else if (vrefresh == 30) {
		iyidb_manual_freq_set_cmd[1] = 0x04;
	} else if (vrefresh == 24) {
		iyidb_manual_freq_set_cmd[1] = 0x05;
	} else if (vrefresh == 10) {
		iyidb_manual_freq_set_cmd[1] = 0x06;
	} else if (vrefresh == 1) {
		iyidb_manual_freq_set_cmd[1] = 0x07;
	} else {
		dev_warn(ctx->dev,
			"unsupported manual HS freq %uhz, fallback to 120hz\n", vrefresh);
		iyidb_manual_freq_set_cmd[1] = 0x00;
	}

	if (is_high_pwm)
		iyidb_manual_freq_set_cmd[1] |= 0x10;

	return &iyidb_low_frequency_change_manual_cmdset;
}

/* placeholder, modified in get_iyidb_low_frequency_change_auto_hs_cmdset */
static u8 iyidb_auto_target_freq_hs_cmd[] = {0xBD, 0x02};
static u8 iyidb_auto_step_setting_hs_cmd[] = {0xBD, 0x02, 0x00, 0x00, 0x00};
static u8 iyidb_auto_mode_on_cmd[] = {0xBD, 0xBD};

static const struct gs_dsi_cmd iyidb_low_frequency_change_auto_hs_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB0, 0x00, 0x82, 0xBD),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB0, 0x00, 0x84, 0xBD),
	GS_DSI_QUEUE_CMDLIST(iyidb_auto_target_freq_hs_cmd),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB0, 0x00, 0xA8, 0xBD),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB0, 0x00, 0xAA, 0xBD),
	GS_DSI_QUEUE_CMD(0xBD, 0x00, 0x02, 0x06, 0x16),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB0, 0x00, 0xC2, 0xBD),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB0, 0x00, 0xC4, 0xBD),
	GS_DSI_QUEUE_CMDLIST(iyidb_auto_step_setting_hs_cmd),
	GS_DSI_QUEUE_CMDLIST(iyidb_auto_mode_on_cmd),
	GS_DSI_QUEUE_CMDLIST(panel_update),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_low_frequency_change_auto_hs);

static const struct gs_dsi_cmdset *get_iyidb_low_frequency_change_auto_hs_cmdset(
		struct gs_panel *ctx, u32 idle_vrefresh, bool low_brightness_active)
{
	static const u8 step_120_to_60[] = {0xBD, 0x02, 0x00, 0x00, 0x00};
	static const u8 step_120_to_30[] = {0xBD, 0x02, 0x01, 0x00, 0x00};
	static const u8 step_120_to_24[] = {0xBD, 0x02, 0x01, 0x01, 0x00};
	static const u8 step_120_to_10[] = {0xBD, 0x02, 0x01, 0x02, 0x00};
	static const u8 step_120_to_10_low_nits[] = {0xBD, 0x00, 0x01, 0x04, 0x00};
	static const u8 step_120_to_1[] = {0xBD, 0x02, 0x01, 0x02, 0x04};

	CHECK_CMDLIST_EQ_SIZE(iyidb_auto_step_setting_hs_cmd, step_120_to_60);
	CHECK_CMDLIST_EQ_SIZE(iyidb_auto_step_setting_hs_cmd, step_120_to_30);
	CHECK_CMDLIST_EQ_SIZE(iyidb_auto_step_setting_hs_cmd, step_120_to_24);
	CHECK_CMDLIST_EQ_SIZE(iyidb_auto_step_setting_hs_cmd, step_120_to_10);
	CHECK_CMDLIST_EQ_SIZE(iyidb_auto_step_setting_hs_cmd, step_120_to_10_low_nits);
	CHECK_CMDLIST_EQ_SIZE(iyidb_auto_step_setting_hs_cmd, step_120_to_1);

	iyidb_auto_mode_on_cmd[1] = 0xBD;

	if (idle_vrefresh == 60) {
		iyidb_auto_target_freq_hs_cmd[1] = 0x02;
		memcpy(iyidb_auto_step_setting_hs_cmd, step_120_to_60,
				sizeof(iyidb_auto_step_setting_hs_cmd));
	} else if (idle_vrefresh == 30) {
		iyidb_auto_target_freq_hs_cmd[1] = 0x06;
		memcpy(iyidb_auto_step_setting_hs_cmd, step_120_to_30,
				sizeof(iyidb_auto_step_setting_hs_cmd));
	} else if (idle_vrefresh == 24) {
		iyidb_auto_target_freq_hs_cmd[1] = 0x08;
		memcpy(iyidb_auto_step_setting_hs_cmd, step_120_to_24,
				sizeof(iyidb_auto_step_setting_hs_cmd));
	} else if (idle_vrefresh == 10) {
		iyidb_auto_target_freq_hs_cmd[1] = 0x16;
		if (low_brightness_active) {
			memcpy(iyidb_auto_step_setting_hs_cmd,
			       step_120_to_10_low_nits,
			       sizeof(iyidb_auto_step_setting_hs_cmd));
			iyidb_auto_mode_on_cmd[1] = 0x1D;
		} else {
			memcpy(iyidb_auto_step_setting_hs_cmd, step_120_to_10,
			       sizeof(iyidb_auto_step_setting_hs_cmd));
		}
	} else if (idle_vrefresh == 1) {
		iyidb_auto_target_freq_hs_cmd[1] = 0xEE;
		memcpy(iyidb_auto_step_setting_hs_cmd, step_120_to_1,
				sizeof(iyidb_auto_step_setting_hs_cmd));
	} else {
		dev_warn(ctx->dev,
			"unsupported auto HS freq %uhz, fallback to 1hz\n",
			idle_vrefresh);
		iyidb_auto_target_freq_hs_cmd[1] = 0xEE;
		memcpy(iyidb_auto_step_setting_hs_cmd, step_120_to_1,
				sizeof(iyidb_auto_step_setting_hs_cmd));
	}

	return &iyidb_low_frequency_change_auto_hs_cmdset;
}

/* placeholder, modified in get_iyidb_low_frequency_change_auto_ns_cmdset */
static u8 iyidb_auto_target_freq_ns_cmd[] = {0xBD, 0x04};
static u8 iyidb_auto_step_setting_ns_cmd[] = {0xBD, 0x02, 0x00, 0x00, 0x00};

static const struct gs_dsi_cmd iyidb_low_frequency_change_auto_ns_cmds[] = {
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_GE(PANEL_REV_EVT1), test_key_enable),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB0, 0x00, 0x84, 0xBD),
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_GE(PANEL_REV_EVT1),
			iyidb_auto_target_freq_ns_cmd),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB0, 0x00, 0xAA, 0xBD),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB0, 0x00, 0x04, 0x14),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
			0xB0, 0x00, 0xC4, 0xBD),
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_GE(PANEL_REV_EVT1),
			iyidb_auto_step_setting_ns_cmd),
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_GE(PANEL_REV_EVT1), auto_mode_on),
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_GE(PANEL_REV_EVT1), panel_update),
	GS_DSI_FLUSH_REV_CMDLIST(PANEL_REV_GE(PANEL_REV_EVT1), test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_low_frequency_change_auto_ns);

static const struct gs_dsi_cmdset *get_iyidb_low_frequency_change_auto_ns_cmdset(
		struct gs_panel *ctx, u32 idle_vrefresh)
{
	static const u8 step_60_to_30[] = {0xBD, 0x02, 0x00, 0x00, 0x00};
	static const u8 step_60_to_10[] = {0xBD, 0x02, 0x02, 0x00, 0x00};
	static const u8 step_60_to_1[] = {0xBD, 0x02, 0x02, 0x04, 0x00};

	CHECK_CMDLIST_EQ_SIZE(iyidb_auto_step_setting_ns_cmd, step_60_to_30);
	CHECK_CMDLIST_EQ_SIZE(iyidb_auto_step_setting_ns_cmd, step_60_to_10);
	CHECK_CMDLIST_EQ_SIZE(iyidb_auto_step_setting_ns_cmd, step_60_to_1);

	if (idle_vrefresh == 30) {
		iyidb_auto_target_freq_ns_cmd[1] = 0x04;
		memcpy(iyidb_auto_step_setting_ns_cmd, step_60_to_30,
				sizeof(iyidb_auto_step_setting_ns_cmd));
	} else if (idle_vrefresh == 10) {
		iyidb_auto_target_freq_ns_cmd[1] = 0x14;
		memcpy(iyidb_auto_step_setting_ns_cmd, step_60_to_10,
				sizeof(iyidb_auto_step_setting_ns_cmd));
	} else if (idle_vrefresh == 1) {
		iyidb_auto_target_freq_ns_cmd[1] = 0xEC;
		memcpy(iyidb_auto_step_setting_ns_cmd, step_60_to_1,
				sizeof(iyidb_auto_step_setting_ns_cmd));
	} else {
		dev_warn(ctx->dev, "unsupported auto NS freq %uhz, fallback to 1hz\n",
				idle_vrefresh);
		iyidb_auto_target_freq_ns_cmd[1] = 0xEC;
		memcpy(iyidb_auto_step_setting_ns_cmd, step_60_to_1,
				sizeof(iyidb_auto_step_setting_ns_cmd));
	}

	return &iyidb_low_frequency_change_auto_ns_cmdset;
}

static const struct gs_dsi_cmdset *get_iyidb_low_frequency_change_auto_cmdset(
		struct gs_panel *ctx, u32 idle_vrefresh, bool is_ns, bool low_brightness_active)
{

	if (is_ns)
		return get_iyidb_low_frequency_change_auto_ns_cmdset(ctx, idle_vrefresh);
	else
		return get_iyidb_low_frequency_change_auto_hs_cmdset(ctx, idle_vrefresh,
								     low_brightness_active);
}

/* placeholder, modified in get_iyidb_hlpm_cmdset */
static u8 iyidb_hlpm_transition_cmd[] = {MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24};

static const struct gs_dsi_cmd iyidb_hlpm_transition_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMDLIST(manual_mode_on),
	GS_DSI_QUEUE_CMDLIST(iyidb_hlpm_transition_cmd),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_hlpm_transition);

static const struct gs_dsi_cmdset *get_iyidb_hlpm_transition_cmdset(bool en)
{
	/* always use normal transition in HLPM mode */
	if (en)
		iyidb_hlpm_transition_cmd[1] = 0x24;
	else
		iyidb_hlpm_transition_cmd[1] = 0x20;

	return &iyidb_hlpm_transition_cmdset;
}

static const struct gs_dsi_cmd iyidb_hlpm_manual_cmds[] = {
	/* target freq 30 hz */
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMDLIST(manual_mode_on),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x01, 0x16),
	GS_DSI_QUEUE_CMD(0x16, 0x00),
	GS_DSI_QUEUE_CMDLIST(panel_update),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_hlpm_manual);

/* placeholder, modified in get_iyidb_hlpm_auto_cmdset */
static u8 iyidb_hlpm_auto_target_freq[] = {0xBD, 0x74};
static u8 iyidb_hlpm_auto_step_settings[] = {0xBD, 0x06, 0x07};

static const struct gs_dsi_cmd iyidb_hlpm_auto_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x85, 0xBD),
	GS_DSI_QUEUE_CMDLIST(iyidb_hlpm_auto_target_freq),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0xB7, 0xBD),
	GS_DSI_QUEUE_CMD(0xBD, 0x00, 0x08),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0xD1, 0xBD),
	GS_DSI_QUEUE_CMDLIST(iyidb_hlpm_auto_step_settings),
	GS_DSI_QUEUE_CMDLIST(auto_mode_on),
	GS_DSI_QUEUE_CMDLIST(panel_update),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_hlpm_auto);

static const struct gs_dsi_cmdset *get_iyidb_hlpm_auto_cmdset(struct gs_panel *ctx)
{
	u32 idle_vrefresh = ctx->sw_status.idle_vrefresh;

	if (idle_vrefresh == 10) {
		iyidb_hlpm_auto_target_freq[1] = 0x08;
		iyidb_hlpm_auto_step_settings[2] = 0x00;
	} else {
		if (idle_vrefresh != 1)
			dev_warn(ctx->dev,
				 "unsupported auto LP freq %uhz, fallback to 1hz\n",
				 idle_vrefresh);
		iyidb_hlpm_auto_target_freq[1] = 0x74;
		iyidb_hlpm_auto_step_settings[2] = 0x07;
	}

	return &iyidb_hlpm_auto_cmdset;
}

/* placeholder, modified in get_iyidb_low_frequency_change_auto_cmdset */
static u8 iyidb_fgz_cmd[] = {0x83, 0x10};
static u8 iyidb_fgz_gain_cmd[] = {0xB1, 0xFF};

static const struct gs_dsi_cmd iyidb_irc_settings_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	/* P1.1 fgz gain settings */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_RANGE(PANEL_REV_PROTO1_1, PANEL_REV_EVT1),
			0xB0, 0x2D, 0x14, 0x95),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_RANGE(PANEL_REV_PROTO1_1, PANEL_REV_EVT1),
			0x95, 0x0C, 0xFF),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_RANGE(PANEL_REV_PROTO1_1, PANEL_REV_EVT1),
			0xB0, 0x2D, 0x2C, 0x95),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_RANGE(PANEL_REV_PROTO1_1, PANEL_REV_EVT1),
			0x95, 0x0C, 0xC8),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_RANGE(PANEL_REV_PROTO1_1, PANEL_REV_EVT1),
			0xB0, 0x2D, 0x44, 0x95),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_RANGE(PANEL_REV_PROTO1_1, PANEL_REV_EVT1),
			0x95, 0x0C, 0xDA),
	/* FGZ ON or OFF */
	GS_DSI_QUEUE_CMDLIST(iyidb_fgz_cmd),
	/* P1.0 fgz gain settings */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_PROTO1_1), 0xB0, 0x02, 0xAB, 0xB1),
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_LT(PANEL_REV_PROTO1_1), iyidb_fgz_gain_cmd),
	/* EVT1.1 fgz gain settings */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1_1, 0xB0, 0x00, 0x96, 0x98),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1_1, 0x98, 0xA9, 0xC3),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1_1, 0xB0, 0x00, 0xA8, 0x98),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1_1, 0x98, 0xA9, 0xC3),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1_1, 0xB0, 0x00, 0xBA, 0x98),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_EVT1_1, 0x98, 0xA9, 0xC3),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_irc_settings);

static const struct gs_dsi_cmdset *get_iyidb_irc_settings_cmdset(bool en)
{
	if (en) {
		iyidb_fgz_cmd[1] = 0x10;
		iyidb_fgz_gain_cmd[1] = 0xFF;
	} else {
		iyidb_fgz_cmd[1] = 0x00;
		iyidb_fgz_gain_cmd[1] = 0x6B;
	}

	return &iyidb_irc_settings_cmdset;
}

static const struct gs_dsi_cmd iyidb_panel_update_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMDLIST(panel_update),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_panel_update);

/* placeholder, will be modified in get_iyidb_freq_mode_cmdset */
static u8 iyidb_freq_mode_cmd[] = {0x16, 0x00};
static u8 iyidb_pwm_manual_freq_set_2_cmd[] = {0x43, 0x01};
static u8 iyidb_pwm_manual_freq_set_3_cmd[] = {0x96, 0x00, 0x01, 0x01, 0x01, 0x01};
static u8 iyidb_pwm_elvss_set_cmd[] = {0xB5, 0xEC};
static u8 iyidb_pwm_vint2_set_cmd[] = {0xE1, 0x64};

static const struct gs_dsi_cmd iyidb_freq_mode_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMDLIST(iyidb_freq_mode_cmd),

	/* Proto 1.x specific commands */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
			0xB0, 0x00, 0x09, 0x43),
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_LT(PANEL_REV_EVT1),
			iyidb_pwm_manual_freq_set_2_cmd),
	/* b/421315037: em power adjustment in high pwm (P1.0 POR only) */
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_PROTO1, iyidb_pwm_manual_freq_set_3_cmd),
	/* b/421315037: em power adjustment in high pwm (> P1.0 POR) */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_RANGE(PANEL_REV_PROTO1_0_1, PANEL_REV_EVT1),
			0xB0, 0x00, 0x4F, 0xB5),
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_RANGE(PANEL_REV_PROTO1_0_1, PANEL_REV_EVT1),
			iyidb_pwm_elvss_set_cmd),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_RANGE(PANEL_REV_PROTO1_0_1, PANEL_REV_EVT1),
			0xB0, 0x00, 0xC1, 0xE1),
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_RANGE(PANEL_REV_PROTO1_0_1, PANEL_REV_EVT1),
			iyidb_pwm_vint2_set_cmd),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_RANGE(PANEL_REV_PROTO1_0_1, PANEL_REV_EVT1),
			0xB0, 0x01, 0x4B, 0xE1),
	GS_DSI_QUEUE_REV_CMDLIST(PANEL_REV_RANGE(PANEL_REV_PROTO1_0_1, PANEL_REV_EVT1),
			iyidb_pwm_vint2_set_cmd),

	GS_DSI_QUEUE_CMDLIST(panel_update),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_freq_mode);

static const struct gs_dsi_cmdset *get_iyidb_freq_mode_cmdset(struct gs_panel *ctx,
		bool is_high_pwm, bool is_ns)
{
	static const u8 manual_freq_set_3_default[] = {0x96, 0x00, 0x01, 0x01, 0x01, 0x01};
	static const u8 manual_freq_set_3_high_pwm[] = {0x96, 0x00, 0x01, 0x00, 0x01, 0x01};

	CHECK_CMDLIST_EQ_SIZE(iyidb_pwm_manual_freq_set_3_cmd, manual_freq_set_3_default);
	CHECK_CMDLIST_EQ_SIZE(iyidb_pwm_manual_freq_set_3_cmd, manual_freq_set_3_high_pwm);

	if (is_high_pwm && is_ns)
		dev_warn(ctx->dev, "both pwm and ns are enabled. overwrite to ns only.\n");

	if (is_ns) {
		iyidb_freq_mode_cmd[1] = 0x20;
	} else if (is_high_pwm) {
		iyidb_freq_mode_cmd[1] = 0x10;
		iyidb_pwm_manual_freq_set_2_cmd[1] = 0x03;
		memcpy(iyidb_pwm_manual_freq_set_3_cmd, manual_freq_set_3_high_pwm,
				sizeof(iyidb_pwm_manual_freq_set_3_cmd));
		iyidb_pwm_elvss_set_cmd[1] = 0xE6;
		iyidb_pwm_vint2_set_cmd[1] = 0x82;
	} else {
		iyidb_freq_mode_cmd[1] = 0x00;
		iyidb_pwm_manual_freq_set_2_cmd[1] = 0x01;
		memcpy(iyidb_pwm_manual_freq_set_3_cmd, manual_freq_set_3_default,
				sizeof(iyidb_pwm_manual_freq_set_3_cmd));
		iyidb_pwm_elvss_set_cmd[1] = 0xEC;
		iyidb_pwm_vint2_set_cmd[1] = 0x64;
	}

	return &iyidb_freq_mode_cmdset;
}

/* placeholder, will be modified in get_iyidb_pwm[_em_power]_cmdset */
static u8 iyidb_dbi_temperature_cmd[] = {0x8F, 0x20, 0x07};

static const struct gs_dsi_cmd iyidb_burn_in_comp_cmds[] = {
	/* deburn-in temperature settings */
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x21, 0x8F),
	GS_DSI_QUEUE_CMDLIST(iyidb_dbi_temperature_cmd),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_burn_in_comp);

static const struct gs_dsi_cmdset *iyidb_get_burn_in_comp_cmdset(u32 deg_c)
{
	/**
	 * compensation value (two-byte in little endian):
	 *
	 * 0 degC has offset 1024, and within 1 degC there are 32 steps
	 * formula: panel_temperature(deg C) * 32 + 1024
	 */
	deg_c = deg_c * 32 + 1024;
	iyidb_dbi_temperature_cmd[1] = deg_c & 0xFF;
	iyidb_dbi_temperature_cmd[2] = (deg_c >> 8) & 0xFF;

	return &iyidb_burn_in_comp_cmdset;
}

/* placeholder, modified in get_iyidb_low_frequency_change_manual_max_freq_cmdset */
static u8 iyidb_manual_max_freq_set_cmd[] = {0x16, 0x00};

static const struct gs_dsi_cmd iyidb_low_frequency_change_manual_max_freq_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMDLIST(manual_mode_on),
	GS_DSI_QUEUE_CMDLIST(iyidb_manual_max_freq_set_cmd),
	GS_DSI_QUEUE_CMDLIST(panel_update),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_low_frequency_change_manual_max_freq);

static const struct gs_dsi_cmdset *get_iyidb_low_frequency_change_manual_max_freq_cmdset(
		struct gs_panel *ctx, bool is_high_pwm, bool is_ns)
{
	if (is_ns)
		iyidb_manual_max_freq_set_cmd[1] = 0x20;  // NS mode 60 Hz
	else if (is_high_pwm)
		iyidb_manual_max_freq_set_cmd[1] = 0x10;  // High PWM mode 120 Hz
	else
		iyidb_manual_max_freq_set_cmd[1] = 0x00;  // HS mode 120 Hz

	return &iyidb_low_frequency_change_manual_max_freq_cmdset;
}

/**
 * get_iyidb_pre_sleep_in_cmdset - get cmdset to raise to maximum vsync
 *
 * b/450136794 requires to raise vsync to maximum before sending sleep
 * in. This makes sure the time for sleep in to complete (i.e. one DDIC
 * internal vsync period) is miminum, so that it will be less likely
 * interrupted by other commands (e.g. DSTB enter).
 */
static const struct gs_dsi_cmdset *get_iyidb_pre_sleep_in_cmdset(struct gs_panel *ctx)
{
	unsigned long *feat = ctx->sw_status.feat;
	bool is_pwm_high = test_bit(FEAT_PWM_HIGH, feat);
	bool is_ns = test_bit(FEAT_OP_NS, feat);

	/* LP mode: raise to 30hz */
	if (ctx->current_mode && ctx->current_mode->gs_mode.is_lp_mode)
		return &iyidb_hlpm_manual_cmdset;

	/* HS or NS mode: raise to manual 120 or 60hz */
	return get_iyidb_low_frequency_change_manual_max_freq_cmdset(ctx,
			is_pwm_high, is_ns);
}

static const struct gs_dsi_cmd iyidb_sleep_in_cmds[] = {
	GS_DSI_FLUSH_DELAY_CMD(20, MIPI_DCS_SET_DISPLAY_OFF),
	GS_DSI_QUEUE_CMD(MIPI_DCS_ENTER_SLEEP_MODE),
	GS_DSI_FLUSH_DELAY_CMD(100, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x00, 0x00),
};
static DEFINE_GS_CMDSET(iyidb_sleep_in);

static const struct gs_dsi_cmd iyidb_deep_standby_on_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMD(0xD9, 0x01),
	GS_DSI_FLUSH_DELAY_CMDLIST(100, test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_deep_standby_on);

static const struct gs_dsi_cmd iyidb_errfg_settings_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMD(0xE5, 0x15),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x01, 0xED),
	GS_DSI_QUEUE_CMD(0xED, 0x01, 0x41),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x04, 0xED),
	GS_DSI_QUEUE_CMD(0xED, 0x07),
	GS_DSI_QUEUE_CMD(0xB0, 0x01, 0x29, 0xF4),
	GS_DSI_QUEUE_CMD(0xF4, 0x60),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_errfg_settings);

/**
 * struct iyidb_panel - panel specific runtime info
 *
 * This struct maintains iyidb panel specific runtime info, any fixed details about panel
 * should most likely go into struct gs_panel_desc
 */
struct iyidb_panel {
	/** @base: base panel struct */
	struct gs_panel base;
	/** @force_changeable_te: force changeable TE (instead of fixed) during early exit */
	bool force_changeable_te;
	/** @force_changeable_te2: force changeable TE2 for monitoring refresh rate */
	bool force_changeable_te2;
	/**
	 * @pending_skin_temp_handling: whether there is skin temperature which needs to be
	 *                              handled later
	 */
	bool pending_skin_temp_handling;
	/**
	 * @override_reset_ddi_high_delay_ms: override reset timing (DDI wakeup) in DSTB exit
	 */
	u32 override_reset_ddi_high_delay_ms;
	/**
	 * @override_reset_mipi_high_delay_ms: override reset timing (MIPI wakeup) in DSTB exit
	 */
	u32 override_reset_mipi_high_delay_ms;
	/**
	 * @crosstalk_threshold_dbv: enable panel crosstalk feature when brightness goes above
	 *                           the threshold
	 */
	u16 crosstalk_threshold_dbv;
	/**
	 * @crosstalk_enabled: whether panel crosstalk feature is currently enabled
	 */
	bool crosstalk_enabled;
	/** @low_brightness_active: whether the applied dbv is less than 5 nits */
	bool low_brightness_active;
};
#define to_spanel(ctx) container_of(ctx, struct iyidb_panel, base)

static const struct gs_dsi_cmd iyidb_crosstalk_enable_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMD(0xA4, 0x00, 0x01),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_crosstalk_enable);

static const struct gs_dsi_cmd iyidb_crosstalk_disable_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	GS_DSI_QUEUE_CMD(0xA4, 0x01, 0x00),
	GS_DSI_FLUSH_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(iyidb_crosstalk_disable);

static const struct gs_dsi_cmdset *get_iyidb_crosstalk_cmdset(struct gs_panel *ctx)
{
	struct iyidb_panel *spanel = to_spanel(ctx);

	if (spanel->crosstalk_enabled)
		return &iyidb_crosstalk_enable_cmdset;
	else
		return &iyidb_crosstalk_disable_cmdset;
}

/**
 * iyidb_set_panel_feat_te - configure TE type, width and frequency
 * @ctx: gs_panel struct
 * @te_freq: TE frequency
 * @return: enum gs_panel_tex_opt: fixed or changeable TE
 *
 * Description: this function should cache commands only, don't update hw_status.
 */
static void iyidb_set_panel_feat_te(struct gs_panel *ctx, u32 te_freq)
{
	gs_panel_send_cmdset(ctx, get_iyidb_te_cmdset(ctx, te_freq));
}

/**
 * iyidb_te2_setting() - perform full te2 setting updates
 *
 * Typically TE2 will be described as "option" and "freq". For iyidb panel,
 * - option: CHANGEABLE / FIXED-120(30 for LP) / FIXED-240
 * - freq: constant when option is FIXED, variable when CHANGEABLE
 *
 * This callback function performs
 * 1. Send op codes
 * 2. Notify te2 frequency changes to update sysfs
 * 3. Notify te2 option changes to update sysfs
 *
 */
static void iyidb_te2_setting(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct iyidb_panel *spanel = to_spanel(ctx);

	gs_panel_send_cmdset(ctx, get_iyidb_te2_cmdset(ctx, spanel->force_changeable_te2));

	notify_panel_te2_freq_changed(ctx, 0);
	notify_panel_te2_option_changed(ctx);

	dev_dbg(dev, "TE2 setting: option(%s), freq(%d)\n",
		(ctx->te2.option == TEX_OPT_CHANGEABLE) ? "changeable" : "fixed", ctx->te2.freq_hz);
}

/**
 * iyidb_update_te2() - force te2 to changeable
 */
static void iyidb_update_te2(struct gs_panel *ctx)
{
	struct iyidb_panel *spanel = to_spanel(ctx);

	if (spanel->force_changeable_te2 && ctx->te2.option == TEX_OPT_FIXED) {
		dev_dbg(ctx->dev, "force to changeable TE2\n");
		ctx->te2.option = TEX_OPT_CHANGEABLE;
		iyidb_te2_setting(ctx);
	}
}

static bool iyidb_set_te2_freq(struct gs_panel *ctx, u32 freq_hz)
{
	struct device *dev = ctx->dev;

	if (ctx->te2.freq_hz == freq_hz)
		return false;

	if (ctx->te2.option == TEX_OPT_FIXED) {
		bool lp_mode = ctx->current_mode->gs_mode.is_lp_mode;
		u32 prev_freq_hz = ctx->te2.freq_hz;

		if ((!lp_mode && freq_hz != 120 && freq_hz != 240) || (lp_mode && freq_hz != 30)) {
			dev_warn(dev, "unsupported fixed TE2 freq (%u) in %s mode\n", freq_hz,
				 lp_mode ? "lp" : "normal");
			return false;
		}

		ctx->te2.freq_hz = freq_hz;

		/**
		 * iyidb shares same op code for fixed 120/30 hz TE2
		 * send command only if needed (i.e. switch between 240hz and non-240hz)
		 */
		if (prev_freq_hz == 240 || freq_hz == 240)
			iyidb_te2_setting(ctx);

	} else if (ctx->te2.option == TEX_OPT_CHANGEABLE) {
		dev_dbg(dev, "set changeable TE2 freq %uhz\n", freq_hz);

		/* In CHANGEABLE option, no op code is needed on freq change */
		ctx->te2.freq_hz = freq_hz;
		notify_panel_te2_freq_changed(ctx, 0);
	} else {
		dev_warn(dev, "TE2 option is unsupported (%u)\n", ctx->te2.option);
		return false;
	}

	PANEL_ATRACE_INT_PID_FMT(ctx->te2.freq_hz, ctx->trace_pid,
				 "te2_freq[%s]", ctx->panel_model);

	return true;
}

static u32 iyidb_get_te2_freq(struct gs_panel *ctx)
{
	return ctx->te2.freq_hz;
}

static bool iyidb_set_te2_option(struct gs_panel *ctx, u32 option)
{
	struct iyidb_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;

	if (option == ctx->te2.option)
		return false;

	if (option != TEX_OPT_FIXED && option != TEX_OPT_CHANGEABLE) {
		dev_warn(dev, "unsupported TE2 option (%u)\n", option);
		return false;
	}

	if (spanel->force_changeable_te2 && option == TEX_OPT_FIXED) {
		dev_dbg(dev, "force changeable TE2 is set\n");
		return false;
	}

	ctx->te2.option = option;
	iyidb_te2_setting(ctx);

	return true;
}

static enum gs_panel_tex_opt iyidb_get_te2_option(struct gs_panel *ctx)
{
	return ctx->te2.option;
}

// TODO(hungyeh): OP manual unsupported yet
static void iyidb_set_panel_feat_manual_mode_fi(struct gs_panel *ctx, u32 vrefresh, bool enabled)
{
	dev_info(ctx->dev, "manual mode fi unsupported. ignoring request (vrefresh=%d, en=%d)\n",
			(int)vrefresh, (int)enabled);
}

static void iyidb_set_panel_feat_freq_mode(struct gs_panel *ctx)
{
	const unsigned long *feat = ctx->sw_status.feat;
	bool is_pwm_high = test_bit(FEAT_PWM_HIGH, feat);
	bool is_ns = test_bit(FEAT_OP_NS, feat);

	gs_panel_send_cmdset(ctx, get_iyidb_freq_mode_cmdset(ctx, is_pwm_high, is_ns));
}

static void iyidb_set_panel_feat_early_exit(struct gs_panel *ctx, u32 te_freq)
{
	const unsigned long *feat = ctx->sw_status.feat;

	gs_panel_send_cmdset(ctx, get_iyidb_early_exit_cmdset(ctx, te_freq,
							      test_bit(FEAT_EARLY_EXIT, feat)));
}

static void iyidb_set_panel_feat_frequency(struct gs_panel *ctx, u32 vrefresh, u32 idle_vrefresh)
{
	struct iyidb_panel *spanel = to_spanel(ctx);
	const unsigned long *feat = ctx->sw_status.feat;
	bool is_high_pwm = test_bit(FEAT_PWM_HIGH, feat);
	bool is_ns = test_bit(FEAT_OP_NS, feat);
	bool low_brightness_active = spanel->low_brightness_active;

	/* Low Frequency Change */
	if (test_bit(FEAT_FRAME_AUTO, feat)) {
		/* Auto mode */
		gs_panel_send_cmdset(ctx,
				get_iyidb_low_frequency_change_auto_cmdset(ctx,
						idle_vrefresh, is_ns, low_brightness_active));

		dev_dbg(ctx->dev, "panel_freq: auto(idle_vrefresh=%u)\n", idle_vrefresh);
	} else {
		/* Manual mode */
		gs_panel_send_cmdset(ctx,
				get_iyidb_low_frequency_change_manual_cmdset(ctx,
					vrefresh, is_high_pwm, is_ns));

		if (!test_bit(FEAT_FRAME_MANUAL_FI, feat))
			dev_dbg(ctx->dev, "panel_freq: unspecified, fallback to manual\n");
		dev_dbg(ctx->dev, "panel_freq: manual(vrefresh=%u)\n", vrefresh);
	}
}

static void iyidb_set_panel_feat_hbm_irc(struct gs_panel *ctx, enum irc_mode irc_mode)
{
	bool fgz_on = (irc_mode == IRC_FLAT_Z);

	gs_panel_send_cmdset(ctx, get_iyidb_irc_settings_cmdset(fgz_on));
	dev_info(ctx->dev, "irc_mode=%d\n", irc_mode);
}

/**
 * iyidb_set_panel_feat - configure panel features
 * @ctx: gs_panel struct
 * @pmode: gs_panel_mode struct, target panel mode
 * @enforce: force to write all of registers even if no feature state changes
 *
 * Configure panel features based on the context.
 * Note: iyidb_set_panel_feat_xxx() should cache commands only while
 *       iyidb_set_panel_feat() aggregates and sends the commands, and update
 *       hw_status. DO NOT update them in iyidb_set_panel_feat_xxx().
 */
static void iyidb_set_panel_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
				bool enforce)
{
	struct device *dev = ctx->dev;
	struct iyidb_panel *spanel = to_spanel(ctx);
	struct gs_panel_status *sw_status = &ctx->sw_status;
	struct gs_panel_status *hw_status = &ctx->hw_status;
	unsigned long *feat = sw_status->feat;
	enum irc_mode irc_mode = sw_status->irc_mode;
	u32 idle_vrefresh = sw_status->idle_vrefresh;
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 te_freq = gs_drm_mode_te_freq(&pmode->mode);
	bool irc_mode_changed, idle_vrefresh_changed, vrefresh_changed, te_freq_changed;
	char trace_msg[64];
	DECLARE_BITMAP(changed_feat, FEAT_MAX);

	/* Vrr settings override */
	if (!test_bit(FEAT_FRAME_AUTO, feat)) {
		vrefresh = idle_vrefresh ?: 1;
		idle_vrefresh = 0;
	}

	if (test_bit(FEAT_FRAME_AUTO, feat) && (idle_vrefresh == 48 || idle_vrefresh == 80)) {
		dev_warn(dev, "%uhz auto mode is not supported, force manual mode\n",
			 idle_vrefresh);
		clear_bit(FEAT_EARLY_EXIT, feat);
		clear_bit(FEAT_FRAME_AUTO, feat);
		set_bit(FEAT_FRAME_MANUAL_FI, feat);
		vrefresh = idle_vrefresh;
		idle_vrefresh = 0;
	}

	/* Force NS mode if using 60Hz TE */
	if (gs_is_ns_op_rate(pmode)) {
		set_bit(FEAT_OP_NS, feat);
		clear_bit(FEAT_PWM_HIGH, feat);
	} else {
		clear_bit(FEAT_OP_NS, feat);
		if (ctx->pwm_mode == GS_PWM_RATE_HIGH)
			set_bit(FEAT_PWM_HIGH, feat);
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
	}

	/* If no changes, skip update */
	if (!enforce && bitmap_empty(changed_feat, FEAT_MAX) &&
	    !irc_mode_changed && !idle_vrefresh_changed &&
	    !vrefresh_changed && !te_freq_changed) {
		dev_dbg(dev, "%s: no changes, skip update\n", __func__);
		return;
	}

	ctx->panel_settings_changed = true;
	snprintf(trace_msg, sizeof(trace_msg),
		 "feat: hbm=%u irc=%u ns=%u h_pwm=%u fi=%u@a,%u@m ee=%u rr=%3u-%3u@%3u",
		 test_bit(FEAT_HBM, feat), irc_mode, test_bit(FEAT_OP_NS, feat),
		 test_bit(FEAT_PWM_HIGH, feat), test_bit(FEAT_FRAME_AUTO, feat),
		 test_bit(FEAT_FRAME_MANUAL_FI, feat), test_bit(FEAT_EARLY_EXIT, feat),
		 idle_vrefresh ? idle_vrefresh : vrefresh,
		 drm_mode_vrefresh(&pmode->mode), te_freq);
	dev_dbg(dev, "%s\n", trace_msg);

	PANEL_ATRACE_BEGIN(trace_msg);

	/*
	 * TE Settings
	 */
	sw_status->te.option = spanel->force_changeable_te ? TEX_OPT_CHANGEABLE : TEX_OPT_FIXED;
	sw_status->te.freq_hz = te_freq;
	if (test_bit(FEAT_EARLY_EXIT, changed_feat) || test_bit(FEAT_PWM_HIGH, changed_feat) ||
	    test_bit(FEAT_OP_NS, changed_feat) || te_freq_changed) {
		iyidb_set_panel_feat_te(ctx, te_freq);
		hw_status->te.option = sw_status->te.option;
		hw_status->te.freq_hz = te_freq;
	}

	/*
	 * HBM IRC settings
	 */
	if (irc_mode_changed) {
		iyidb_set_panel_feat_hbm_irc(ctx, irc_mode);
		hw_status->irc_mode = irc_mode;
	}

	/*
	 * Frequency mode (High PWM, NS, Standard/Hybrid mode)
	 */
	if (test_bit(FEAT_PWM_HIGH, changed_feat) || test_bit(FEAT_OP_NS, changed_feat)) {
		iyidb_set_panel_feat_freq_mode(ctx);

		if (test_bit(FEAT_OP_NS, changed_feat)) {
			ctx->op_hz = (gs_is_ns_op_rate(pmode)) ? 60 : 120;

			if (test_bit(FEAT_PWM_HIGH, changed_feat))
				notify_panel_pwm_mode_changed(ctx);
			notify_panel_op_hz_changed(ctx);
		}
	}

	/*
	 * Early exit settings
	 */
	if (test_bit(FEAT_EARLY_EXIT, changed_feat) || vrefresh_changed || te_freq_changed)
		iyidb_set_panel_feat_early_exit(ctx, te_freq);

	/*
	 * Manual frame insertion
	 */
	if (test_bit(FEAT_FRAME_MANUAL_FI, changed_feat))
		iyidb_set_panel_feat_manual_mode_fi(ctx, vrefresh,
						    test_bit(FEAT_FRAME_MANUAL_FI, feat));

	/*
	 * Frequency Settings: low frequency change
	 */
	if (test_bit(FEAT_FRAME_AUTO, changed_feat) || test_bit(FEAT_PWM_HIGH, changed_feat) ||
	    idle_vrefresh_changed || vrefresh_changed)
		iyidb_set_panel_feat_frequency(ctx, vrefresh, idle_vrefresh);

	PANEL_ATRACE_END(trace_msg);

	hw_status->vrefresh = vrefresh;
	hw_status->idle_vrefresh = idle_vrefresh;
	hw_status->te.freq_hz = te_freq;
	bitmap_copy(hw_status->feat, feat, FEAT_MAX);
}

static void iyidb_update_refresh_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
					const u32 idle_vrefresh)
{
	struct gs_panel_status *sw_status = &ctx->sw_status;

	dev_info(ctx->dev, "%s: mode: %s set idle_vrefresh: %u\n", __func__,
		pmode->mode.name, idle_vrefresh);

	sw_status->idle_vrefresh = idle_vrefresh;
	ctx->idle_data.panel_idle_vrefresh = idle_vrefresh;
	iyidb_set_panel_feat(ctx, pmode, false);
	notify_panel_mode_changed(ctx);

	dev_info(ctx->dev, "%s: display state is notified\n", __func__);
}

static void iyidb_change_frequency(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (!ctx)
		return;

	iyidb_update_refresh_mode(ctx, pmode, ctx->sw_status.idle_vrefresh);
	ctx->sw_status.te.freq_hz = gs_drm_mode_te_freq(&pmode->mode);

	dev_info(ctx->dev, "change to %u hz\n", vrefresh);
}

static void iyidb_update_wrctrld(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	gs_panel_send_cmdset(ctx, get_iyidb_wrctrl_cmdset(ctx->dimming_on));

	dev_dbg(dev, "wrctrld: dimming: %d\n", (int)ctx->dimming_on);
}

static int iyidb_set_brightness(struct gs_panel *ctx, u16 br)
{
	int ret;
	u16 brightness;
	u32 max_brightness;
	struct device *dev = ctx->dev;
	struct iyidb_panel *spanel = to_spanel(ctx);

	if (ctx->current_mode && ctx->current_mode->gs_mode.is_lp_mode) {
		if (gs_panel_has_func(ctx, set_binned_lp))
			ctx->desc->gs_panel_func->set_binned_lp(ctx, br);
		return 0;
	}

	if (!ctx->desc->brightness_desc->brt_capability) {
		dev_err(dev, "no available brightness capability\n");
		return -EINVAL;
	}

	max_brightness = ctx->desc->brightness_desc->brt_capability->hbm.level.max;

	if (br > max_brightness) {
		br = max_brightness;
		dev_warn(dev, "%s: capped to dbv(%d)\n", __func__, max_brightness);
	}

	/* swap endianness because panel expects MSB first */
	brightness = swab16(br);
	ret = gs_dcs_set_brightness(ctx, brightness);

	if (!ret)
		ctx->hw_status.dbv = br;

	/* b/490962578: enable crosstalk when brightness reaches a threshold */
	if (br >= spanel->crosstalk_threshold_dbv) {
		if (!spanel->crosstalk_enabled) {
			spanel->crosstalk_enabled = true;
			gs_panel_send_cmdset(ctx, get_iyidb_crosstalk_cmdset(ctx));
		}
	} else {
		if (spanel->crosstalk_enabled) {
			spanel->crosstalk_enabled = false;
			gs_panel_send_cmdset(ctx, get_iyidb_crosstalk_cmdset(ctx));
		}
	}

	/* b/493765671: apply different FI table in low nits auto mode */
	if (br <= IYIDB_5NITS_DBV) {
		if (!spanel->low_brightness_active) {
			spanel->low_brightness_active = true;
			iyidb_set_panel_feat_frequency(ctx,
						       ctx->sw_status.vrefresh,
						       ctx->sw_status.idle_vrefresh);
		}
	} else {
		if (spanel->low_brightness_active) {
			spanel->low_brightness_active = false;
			iyidb_set_panel_feat_frequency(ctx,
						       ctx->sw_status.vrefresh,
						       ctx->sw_status.idle_vrefresh);
		}
	}

	return ret;
}

static void iyidb_set_hbm_mode(struct gs_panel *ctx, enum gs_hbm_mode mode)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	if (ctx->hbm_mode == mode)
		return;

	if (unlikely(!pmode))
		return;

	ctx->hbm_mode = mode;

	if (GS_IS_HBM_ON(mode)) {
		set_bit(FEAT_HBM, ctx->sw_status.feat);
		if (mode == GS_HBM_ON_IRC_ON)
			ctx->sw_status.irc_mode = IRC_FLAT_DEFAULT;
		else
			ctx->sw_status.irc_mode = IRC_FLAT_Z;
	} else {
		clear_bit(FEAT_HBM, ctx->sw_status.feat);
		ctx->sw_status.irc_mode = IRC_FLAT_DEFAULT;
	}

	dev_info(ctx->dev, "hbm_on=%d hbm_ircoff=%d.\n", GS_IS_HBM_ON(ctx->hbm_mode),
		 GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode));

	iyidb_set_panel_feat(ctx, pmode, false);
}

static void iyidb_set_dimming(struct gs_panel *ctx, bool dimming_on)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	ctx->dimming_on = dimming_on;

	if (pmode->gs_mode.is_lp_mode) {
		dev_dbg(ctx->dev, "in lp mode, postpone dimming update to normal mode entry");
		return;
	}

	iyidb_update_wrctrld(ctx);
}

static void iyidb_mode_set(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	iyidb_change_frequency(ctx, pmode);
}

static bool iyidb_is_mode_seamless_atomic(const struct gs_panel *ctx,
					 const struct gs_panel_mode *old_pmode,
					 const struct gs_panel_mode *new_pmode)
{
	const struct drm_display_mode *c = &old_pmode->mode;
	const struct drm_display_mode *n = &new_pmode->mode;

	/* seamless mode set can happen if active region resolution is same */
	return (c->vdisplay == n->vdisplay) && (c->hdisplay == n->hdisplay);
}

static bool iyidb_is_mode_valid(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	/* TODO: b/491475960 - re-enable NS mode support */
	if (gs_is_ns_op_rate(pmode))
		return false;

	return true;
}

static void iyidb_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct dentry *panel_root, *csroot;
	struct iyidb_panel *spanel;

	if (!ctx)
		return;

	panel_root = debugfs_lookup("panel", root);
	if (!panel_root)
		return;

	csroot = debugfs_lookup("cmdsets", panel_root);
	if (!csroot)
		goto panel_out;

	gs_panel_debugfs_create_cmdset(csroot, &iyidb_init_cmdset, "init");

	spanel = to_spanel(ctx);
	debugfs_create_bool("force_changeable_te", 0644, panel_root, &spanel->force_changeable_te);
	debugfs_create_bool("force_changeable_te2", 0644, panel_root,
				&spanel->force_changeable_te2);
	debugfs_create_u32("override_reset_ddi_high_delay_ms", 0644, panel_root,
			   &spanel->override_reset_ddi_high_delay_ms);
	debugfs_create_u32("override_reset_mipi_high_delay_ms", 0644, panel_root,
			   &spanel->override_reset_mipi_high_delay_ms);
	debugfs_create_u16("crosstalk_threshold_dbv", 0644, panel_root,
			   &spanel->crosstalk_threshold_dbv);

	dput(csroot);
panel_out:
	dput(panel_root);
}

static void iyidb_set_panel_lp_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	u32 idle_vrefresh = ctx->sw_status.idle_vrefresh;
	bool is_auto = (test_bit(FEAT_FRAME_AUTO, ctx->sw_status.feat)) ? true : false;

	if (!pmode->gs_mode.is_lp_mode)
		return;

	dev_dbg(ctx->dev, "%s: auto=%u rr=%u-%u\n", __func__, is_auto, idle_vrefresh,
		drm_mode_vrefresh(&pmode->mode));

	/* TE settings (disregard force_changeable_te) */
	gs_panel_send_cmdset(ctx, &iyidb_fixed_te_no_gating_cmdset);
	ctx->hw_status.te.freq_hz = 30;
	ctx->hw_status.te.option = TEX_OPT_FIXED;

	/* Low frequency settings */
	if (is_auto) {
		/* auto mode */
		gs_panel_send_cmdset(ctx, get_iyidb_hlpm_auto_cmdset(ctx));
	} else {
		/* manual mode (target 30hz) */
		gs_panel_send_cmdset(ctx, &iyidb_hlpm_manual_cmdset);
	}
	ctx->hw_status.vrefresh = 30;

}

static void iyidb_update_refresh_ctrl_feat(struct gs_panel *ctx,
		const struct gs_panel_mode *pmode, bool enforce)
{
	const u32 ctrl = ctx->refresh_ctrl;
	unsigned long *feat = ctx->sw_status.feat;
	u32 min_vrefresh = ctx->sw_status.idle_vrefresh;
	bool idle_vrefresh_changed = false;
	u32 vrefresh;
	bool lp_mode;

	if (!pmode)
		return;

	dev_dbg(ctx->dev, "refresh_ctrl=0x%X\n", ctrl);

	vrefresh = drm_mode_vrefresh(&pmode->mode);
	lp_mode =  pmode->gs_mode.is_lp_mode;

	if (ctrl & GS_PANEL_REFRESH_CTRL_MIN_REFRESH_RATE_MASK) {
		min_vrefresh = GS_PANEL_REFRESH_CTRL_MIN_REFRESH_RATE(ctrl);

		if (min_vrefresh > vrefresh) {
			dev_warn(ctx->dev, "%s: min RR %uHz requested, but valid range is 1-%uHz\n",
				 __func__, min_vrefresh, vrefresh);
			min_vrefresh = vrefresh;
		}
		ctx->sw_status.idle_vrefresh = min_vrefresh;
		idle_vrefresh_changed = true;
	}

	if (ctrl & GS_PANEL_REFRESH_CTRL_FI_AUTO) {
		if (min_vrefresh == vrefresh) {
			clear_bit(FEAT_FRAME_AUTO, feat);
			clear_bit(FEAT_FRAME_MANUAL_FI, feat);
		} else {
			set_bit(FEAT_FRAME_AUTO, feat);
			clear_bit(FEAT_FRAME_MANUAL_FI, feat);
		}
	} else {
		clear_bit(FEAT_FRAME_AUTO, feat);
		clear_bit(FEAT_FRAME_MANUAL_FI, feat);
	}

	if (ctrl & GS_PANEL_REFRESH_CTRL_EARLY_EXIT) {
		set_bit(FEAT_EARLY_EXIT, feat);
	} else {
		clear_bit(FEAT_EARLY_EXIT, feat);
		clear_bit(FEAT_FRAME_AUTO, feat);
		clear_bit(FEAT_FRAME_MANUAL_FI, feat);
	}

	PANEL_ATRACE_INT_PID_FMT(ctx->sw_status.idle_vrefresh, ctx->trace_pid,
				 "idle_vrefresh[%s]", ctx->panel_model);
	PANEL_ATRACE_INT_PID_FMT(test_bit(FEAT_FRAME_AUTO, feat), ctx->trace_pid,
				 "FEAT_FRAME_AUTO[%s]", ctx->panel_model);
	PANEL_ATRACE_INT_PID_FMT(test_bit(FEAT_EARLY_EXIT, feat), ctx->trace_pid,
				 "FEAT_EARLY_EXIT[%s]", ctx->panel_model);

	if (lp_mode) {
		iyidb_set_panel_lp_feat(ctx, pmode);
		return;
	}

	iyidb_set_panel_feat(ctx, pmode, enforce);

#ifdef PANEL_FACTORY_BUILD
	if (idle_vrefresh_changed && min_vrefresh) {
		/* min_vrefresh is the same as refresh rate in factory commands. */
		iyidb_set_te2_freq(ctx, min_vrefresh);
	}
#endif
}

static void iyidb_refresh_ctrl(struct gs_panel *ctx)
{
	const u32 ctrl = ctx->refresh_ctrl;
	struct device *dev = ctx->dev;

	PANEL_ATRACE_BEGIN(__func__);

	iyidb_update_refresh_ctrl_feat(ctx, ctx->current_mode, false);

	if (ctrl & GS_PANEL_REFRESH_CTRL_FI_FRAME_COUNT_MASK) {
		PANEL_ATRACE_BEGIN("insert_frame");
		dev_dbg(dev, "%s: manually inserting frame\n", __func__);
		gs_panel_send_cmdset(ctx, &iyidb_panel_update_cmdset);
		PANEL_ATRACE_END("insert_frame");
	}

	PANEL_ATRACE_END(__func__);
}

static void iyidb_set_lp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	const u16 brightness = gs_panel_get_brightness(ctx);
	bool use_early_exit = test_bit(FEAT_EARLY_EXIT, ctx->sw_status.feat);

	PANEL_ATRACE_BEGIN(__func__);

	ctx->sw_status.te.freq_hz = 30;
	ctx->sw_status.te.option = TEX_OPT_FIXED;

	/* AOD mode luminance settings */
	gs_panel_set_binned_lp_helper(ctx, brightness);

	/* HLPM ON */
	gs_panel_send_cmdset(ctx, get_iyidb_hlpm_transition_cmdset(true));

	/* TE / TE2 / low frequency change settings */
	iyidb_set_panel_lp_feat(ctx, pmode);

	/* Early exit settings */
	gs_panel_send_cmdset(ctx, get_iyidb_early_exit_lp_cmdset(ctx, use_early_exit));

	PANEL_ATRACE_END(__func__);

	dev_info(ctx->dev, "enter %dhz LP mode\n", drm_mode_vrefresh(&pmode->mode));
}

static void iyidb_set_nolp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	if (!gs_is_panel_active(ctx))
		return;

	PANEL_ATRACE_BEGIN(__func__);

	/* HLPM OFF */
	gs_panel_send_cmdset(ctx, get_iyidb_hlpm_transition_cmdset(false));

	/* Enforce panel register update after exiting HLPM mode */
	iyidb_update_refresh_ctrl_feat(ctx, pmode, true);
	iyidb_update_wrctrld(ctx);
	iyidb_change_frequency(ctx, pmode);

	PANEL_ATRACE_END(__func__);

	dev_info(ctx->dev, "exit LP mode\n");
}

static void iyidb_pre_update_ffc(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	dev_dbg(dev, "disabling FFC\n");

	PANEL_ATRACE_BEGIN(__func__);

	gs_panel_send_cmdset(ctx, &iyidb_ffc_off_cmdset);

	PANEL_ATRACE_END(__func__);

	ctx->ffc_en = false;
}

static void iyidb_update_ffc(struct gs_panel *ctx, unsigned int hs_clk_mbps)
{
	struct device *dev = ctx->dev;

	if (ctx->force_ffc_off)
		return;

	dev_dbg(dev, "hs_clk_mbps: current=%d, target=%d\n",
		ctx->dsi_hs_clk_mbps, hs_clk_mbps);

	if (hs_clk_mbps != MIPI_DSI_FREQ_DEFAULT &&
		hs_clk_mbps != MIPI_DSI_FREQ_ALTERNATIVE) {
		dev_warn(dev, "invalid hs_clk_mbps=%d for FFC\n", hs_clk_mbps);
		return;
	}

	PANEL_ATRACE_BEGIN(__func__);

	if (ctx->dsi_hs_clk_mbps != hs_clk_mbps || !ctx->ffc_en) {
		dev_info(ctx->dev, "updating FFC for hs_clk_mbps=%d\n", hs_clk_mbps);

		gs_panel_send_cmdset(ctx, get_iyidb_ffc_on_cmdset(hs_clk_mbps));

		ctx->dsi_hs_clk_mbps = hs_clk_mbps;
		ctx->ffc_en = true;
	}

	PANEL_ATRACE_END(__func__);
}

static int iyidb_set_op_hz(struct gs_panel *ctx, unsigned int hz)
{
	return -EPERM;
}

static enum gs_pwm_mode iyidb_get_pwm_mode(struct gs_panel *ctx)
{
	if (ctx->op_hz == 60)
		return GS_PWM_RATE_STANDARD;

	return ctx->pwm_mode;
}

static int iyidb_set_pwm_mode(struct gs_panel *ctx, enum gs_pwm_mode mode)
{
	struct device *dev = ctx->dev;

	if (ctx->op_hz == 60) {
		dev_warn(dev, "can't set PWM when NS mode is active\n");
		return -EINVAL;
	}

	if (mode == ctx->pwm_mode)
		return 0;

	if (mode != GS_PWM_RATE_STANDARD && mode != GS_PWM_RATE_HIGH) {
		dev_warn(dev, "unsupported PWM mode (%d)\n", mode);
		return -EINVAL;
	}

	dev_info(dev, "panel PWM mode %d->%d\n", ctx->pwm_mode, mode);
	ctx->pwm_mode = mode;

	PANEL_ATRACE_BEGIN("%s(%d)", __func__, mode);

	if (mode == GS_PWM_RATE_HIGH)
		set_bit(FEAT_PWM_HIGH, ctx->sw_status.feat);
	else
		clear_bit(FEAT_PWM_HIGH, ctx->sw_status.feat);

	iyidb_set_panel_feat(ctx, ctx->current_mode, false);

	PANEL_ATRACE_END("%s(%d)", __func__, mode);

	return 0;
}

#define IYIDB_DBI_MIN_DEGC 0
#define IYIDB_DBI_MAX_DEGC 50
static void iyidb_send_burn_in_comp_cmds(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct iyidb_panel *spanel = to_spanel(ctx);
	u32 comp_temperature = ctx->skin_temperature;

	if (ctx->panel_state != GPANEL_STATE_NORMAL)
		return;

	spanel->pending_skin_temp_handling = false;

	/* clamp temperature if out of supported range */
	if (ctx->skin_temperature < IYIDB_DBI_MIN_DEGC) {
		comp_temperature = IYIDB_DBI_MIN_DEGC;
		dev_warn(dev, "DBI: temp %u out of compensation range. clamping to %u\n",
				ctx->skin_temperature, comp_temperature);
	} else if (ctx->skin_temperature > IYIDB_DBI_MAX_DEGC) {
		comp_temperature = IYIDB_DBI_MAX_DEGC;
		dev_warn(dev, "DBI: temp %u out of compensation range. clamping to %u\n",
				ctx->skin_temperature, comp_temperature);
	}

	/* SP Temperature commands */
	PANEL_ATRACE_BEGIN(__func__);
	gs_panel_send_cmdset(ctx, iyidb_get_burn_in_comp_cmdset(comp_temperature));
	PANEL_ATRACE_END(__func__);

	PANEL_ATRACE_INT_PID_FMT(ctx->skin_temperature, ctx->trace_pid,
				 "skin_temperature[%s]", ctx->panel_model);
	PANEL_ATRACE_INT_PID_FMT(comp_temperature, ctx->trace_pid,
				 "comp_temperature[%s]", ctx->panel_model);
	dev_dbg(dev, "DBI: apply gain into ddic at %u deg c\n", comp_temperature);
}

static bool iyidb_set_self_refresh(struct gs_panel *ctx, bool enable)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;
	struct iyidb_panel *spanel = to_spanel(ctx);

	if (spanel->pending_skin_temp_handling && enable)
		iyidb_send_burn_in_comp_cmds(ctx);

	if (unlikely(!pmode))
		return false;

	PANEL_ATRACE_INT_PID_FMT(enable, ctx->trace_pid,
					"set_self_refresh[%s]", ctx->panel_model);
	return false;
}

static void iyidb_commit_done(struct gs_panel *ctx)
{
	struct iyidb_panel *spanel = to_spanel(ctx);

	if (ctx->current_mode->gs_mode.is_lp_mode)
		return;

	if (spanel->pending_skin_temp_handling)
		iyidb_send_burn_in_comp_cmds(ctx);
}

static void iyidb_handle_skin_temperature(struct gs_panel *ctx)
{
	if (ctx->idle_data.self_refresh_active) {
		iyidb_send_burn_in_comp_cmds(ctx);
	} else {
		struct iyidb_panel *spanel = to_spanel(ctx);

		spanel->pending_skin_temp_handling = true;
	}
}

static int iyidb_detect_fault(struct gs_panel *ctx,
			      const struct gs_panel_detect_fault_desc *fault_desc,
			      bool irq_triggered)
{
	int ret;

	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(ctx->dev, test_key_enable);
	ret = gs_panel_detect_fault_helper(ctx, fault_desc, irq_triggered);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(ctx->dev, test_key_disable);

	return ret;
}

/* reset sequence for panel power on */
static void iyidb_run_reset_gpio_sequence(struct gs_panel *ctx)
{
	int delay;
	int tp_reset_dir;
	struct device *dev = ctx->dev;
	static const int reset_timing_ms[PANEL_RESET_TIMING_COUNT] = {
		[PANEL_RESET_TIMING_LOW] = 1,
		[PANEL_RESET_TIMING_INIT] = 60,
	};

	if (IS_ERR_OR_NULL(ctx->gpio.gpiod[DISP_TP_RESET_GPIO])) {
		dev_err(dev, "no touch reset pin, panel will not work\n");
		return;
	}

	/* set touch reset output if not initialized */
	tp_reset_dir = gpiod_get_direction(ctx->gpio.gpiod[DISP_TP_RESET_GPIO]);
	if (tp_reset_dir < 0) {
		dev_err(dev, "cannot get touch reset direction, error=%d\n", tp_reset_dir);
		return;
	}

	if (tp_reset_dir != 0)
		gpiod_direction_output(ctx->gpio.gpiod[DISP_TP_RESET_GPIO], 0);

	/* toggle reset sequence */
	PANEL_ATRACE_BEGIN("gs_panel_reset_helper");

	delay = reset_timing_ms[PANEL_RESET_TIMING_LOW];
	if (gs_panel_gpio_set(ctx, DISP_TP_RESET_GPIO, 0) < 0)
		goto out;
	if (gs_panel_gpio_set(ctx, DISP_RESET_GPIO, 0) < 0)
		goto out;
	dev_dbg(dev, "reset=L, delay: %dms\n", delay);
	delay *= USEC_PER_MSEC;
	usleep_range(delay, delay + 10);

	delay = reset_timing_ms[PANEL_RESET_TIMING_INIT];
	if (gs_panel_gpio_set(ctx, DISP_TP_RESET_GPIO, 1) < 0)
		goto out;
	if (gs_panel_gpio_set(ctx, DISP_RESET_GPIO, 1) < 0)
		goto out;
	dev_dbg(dev, "reset=H, delay: %dms\n", delay);
	delay *= USEC_PER_MSEC;
	usleep_range(delay, delay + 10);

out:
	PANEL_ATRACE_END("gs_panel_reset_helper");
}

/* reset sequence for exiting deep standby */
static void iyidb_panel_reset(struct gs_panel *ctx)
{
	int delay;
	struct device *dev = ctx->dev;
	struct iyidb_panel *spanel = to_spanel(ctx);
	enum panel_deep_standby_reset_timing {
		PANEL_DEEP_STANDBY_RESET_TIMING_DDI_LOW = 0,
		PANEL_DEEP_STANDBY_RESET_TIMING_DDI_HIGH,
		PANEL_DEEP_STANDBY_RESET_TIMING_MIPI_LOW,
		PANEL_DEEP_STANDBY_RESET_TIMING_MIPI_HIGH,
		PANEL_DEEP_STANDBY_RESET_TIMING_COUNT,
	};
	static const int reset_timing_ms[PANEL_DEEP_STANDBY_RESET_TIMING_COUNT] = {
		[PANEL_DEEP_STANDBY_RESET_TIMING_DDI_LOW] = 1,
		[PANEL_DEEP_STANDBY_RESET_TIMING_DDI_HIGH] = 16,
		[PANEL_DEEP_STANDBY_RESET_TIMING_MIPI_LOW] = 1,
		[PANEL_DEEP_STANDBY_RESET_TIMING_MIPI_HIGH] = 16,
	};

	/* toggle reset sequence */
	PANEL_ATRACE_BEGIN("iyidb_panel_reset");

	delay = reset_timing_ms[PANEL_DEEP_STANDBY_RESET_TIMING_DDI_LOW];
	if (gs_panel_gpio_set(ctx, DISP_RESET_GPIO, 0) < 0)
		goto out;
	dev_dbg(dev, "reset=L, delay: %dms\n", delay);
	delay *= USEC_PER_MSEC;
	usleep_range(delay, delay + 10);


	if (spanel->override_reset_ddi_high_delay_ms)
		delay = spanel->override_reset_ddi_high_delay_ms;
	else
		delay = reset_timing_ms[PANEL_DEEP_STANDBY_RESET_TIMING_DDI_HIGH];
	if (gs_panel_gpio_set(ctx, DISP_RESET_GPIO, 1) < 0)
		goto out;
	dev_dbg(dev, "reset=H, delay: %dms\n", delay);
	delay *= USEC_PER_MSEC;
	usleep_range(delay, delay + 10);

	delay = reset_timing_ms[PANEL_DEEP_STANDBY_RESET_TIMING_MIPI_LOW];
	if (gs_panel_gpio_set(ctx, DISP_RESET_GPIO, 0) < 0)
		goto out;
	dev_dbg(dev, "reset=L, delay: %dms\n", delay);
	delay *= USEC_PER_MSEC;
	usleep_range(delay, delay + 10);

	if (spanel->override_reset_mipi_high_delay_ms)
		delay = spanel->override_reset_mipi_high_delay_ms;
	else
		delay = reset_timing_ms[PANEL_DEEP_STANDBY_RESET_TIMING_MIPI_HIGH];
	if (gs_panel_gpio_set(ctx, DISP_RESET_GPIO, 1) < 0)
		goto out;
	dev_dbg(dev, "reset=H, delay: %dms\n", delay);
	delay *= USEC_PER_MSEC;
	usleep_range(delay, delay + 10);

out:
	PANEL_ATRACE_END("iyidb_panel_reset");
}

static void iyidb_post_reset(struct gs_panel *ctx)
{
	PANEL_ATRACE_BEGIN("post_reset");

	/* DSC related configuration */
	mipi_dsi_compression_mode(to_mipi_dsi_device(ctx->dev), true);
	gs_dcs_write_dsc_config(ctx->dev, &pps_config);

	/* initial command */
	gs_panel_send_cmdset(ctx, &iyidb_init_cmdset);

	/* enable error flag */
	gs_panel_send_cmdset(ctx, &iyidb_errfg_settings_cmdset);

	/* update FFC */
	if (!ctx->force_ffc_off)
		iyidb_update_ffc(ctx, MIPI_DSI_FREQ_DEFAULT);

	/* te2 settings and notify te2 change */
	iyidb_te2_setting(ctx);

	PANEL_ATRACE_END("post_reset");
}

static int iyidb_enable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	bool needs_reset = !gs_is_panel_enabled(ctx);
	const bool leaving_dstb = (ctx->panel_state == GPANEL_STATE_BLANK);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	u8 pps_buf[IYIDB_PPS_LEN] = { 0 };
	int ret, val = -1;
	u32 retry_cnt = 0;

	if (!pmode) {
		dev_err(dev, "no current mode set\n");
		return -EINVAL;
	}

	dev_info(dev, "iyidb enable\n");

	PANEL_ATRACE_BEGIN(__func__);

re_try:
	/* toggle reset gpio if panel was not powered on */
	if (needs_reset)
		gs_panel_reset_helper(ctx);

	/*
	 * callback after panel reset has been toggled,
	 * i.e. panel state was OFF | UNINITIALIZED | BLANK
	 */
	if (needs_reset || leaving_dstb)
		iyidb_post_reset(ctx);

	/* frequency */
	iyidb_set_panel_feat(ctx, pmode, true);
	iyidb_change_frequency(ctx, pmode);

	/* dimming and HBM */
	iyidb_update_wrctrld(ctx);

	if (pmode->gs_mode.is_lp_mode)
		iyidb_set_lp_mode(ctx, pmode);

	PANEL_ATRACE_BEGIN("PPS table readback check");
	ret = mipi_dsi_dcs_read(dsi, MIPI_DCS_READ_PPS_START, pps_buf, IYIDB_PPS_LEN);
	if (likely(ret == IYIDB_PPS_LEN))
		val = memcmp(iyidb_pps_tb, pps_buf, IYIDB_PPS_LEN);
	else if (ret < 0)
		dev_err(dev, "Error reading pps (%pe)\n", ERR_PTR(ret));
	else
		dev_err(dev, "Error reading pps (short read %d)\n", ret);

	if (unlikely(ret != IYIDB_PPS_LEN || val != 0)) {
		PANEL_ATRACE_INSTANT("PPS readback check fail");
		dev_err(dev, "PPS readback check fail, re-enable panel: %d\n", retry_cnt);
		PANEL_ATRACE_END("PPS table readback check");

		if (retry_cnt >= 5) {
			dev_err(dev, "re-enable panel exceed\n");
		} else {
			retry_cnt++;
			needs_reset = true;
			goto re_try;
		}
	} else {
		PANEL_ATRACE_END("PPS table readback check");
	}

	GS_DCS_WRITE_CMD(dev, MIPI_DCS_SET_DISPLAY_ON);

	ctx->dsi_hs_clk_mbps = MIPI_DSI_FREQ_DEFAULT;

	PANEL_ATRACE_END(__func__);

	return 0;
}

static int iyidb_disable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	int ret;

	/* skip disable sequence if going through modeset */
	if (ctx->panel_state == GPANEL_STATE_MODESET)
		return 0;

	/* b/450136794: raise to maximum vsync before sleep in */
	gs_panel_send_cmdset(ctx, get_iyidb_pre_sleep_in_cmdset(ctx));

	/* clear sw states and send off_cmdset */
	ret = gs_panel_disable(panel);
	if (ret)
		return ret;

	/* panel register state gets reset after disabling hardware */
	bitmap_clear(ctx->hw_status.feat, 0, FEAT_MAX);
	ctx->hw_status.vrefresh = 120;
	ctx->sw_status.te.freq_hz = 120;
	ctx->hw_status.te.freq_hz = 120;
	ctx->hw_status.idle_vrefresh = 0;
	ctx->hw_status.acl_mode = 0;
	ctx->hw_status.dbv = 0;
	ctx->hw_status.irc_mode = IRC_FLAT_DEFAULT;
	ctx->ffc_en = false;

	/**
	 * send cmdset based on next state:
	 *
	 * GPANEL_STATE_OFF: display_off -> sleep_in
	 * GPANEL_STATE_BLANK: display_off -> sleep_in -> deep_standby
	 */
	if (ctx->panel_state == GPANEL_STATE_BLANK)
		gs_panel_send_cmdset(ctx, &iyidb_deep_standby_on_cmdset);

	return 0;
}

static ssize_t iyidb_get_color_data(struct gs_panel *ctx, char *buf, size_t buf_len)
{
	struct device *dev = ctx->dev;
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	int read_ret = -1;
	u8 total_len = ctx->desc->calibration_desc->color_cal[COLOR_DATA_TYPE_CIE].data_size;

	if (buf_len < total_len)
		return -EINVAL;

	PANEL_ATRACE_BEGIN(__func__);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_enable);

	/* Read address settings */
	GS_DCS_BUF_ADD_CMD(dev, 0x67, 0x1B, 0x02, 0x00, 0x00, 0x01, 0x00,
				0x03, 0x00, 0x24, 0x00, 0x00);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x66);

	/* Read status check */
	read_ret = mipi_dsi_dcs_read(dsi, 0x6A, buf, 1);
	if (read_ret != 1) {
		dev_warn(dev, "unable to set read color address\n");
		read_ret = -EINVAL;
		goto out;
	}

	/* Flash read */
	read_ret = mipi_dsi_dcs_read(dsi, 0x67, buf, total_len);
	if (read_ret != total_len) {
		dev_warn(dev, "%s: Unable to read DDIC CIE data (%d)\n", __func__, read_ret);
		read_ret = -EINVAL;
		goto out;
	}

out:
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_fc_disable);
	PANEL_ATRACE_END(__func__);
	return read_ret;
}

static int iyidb_panel_probe(struct mipi_dsi_device *dsi)
{
	struct iyidb_panel *spanel;
	struct gs_panel *ctx;
	int ret;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	ctx = &spanel->base;

	ret = gs_dsi_panel_common_init(dsi, ctx);
	if (ret)
		return ret;

	/* use deep standby mode on suspend */
	ctx->force_power_on = true;

	/* b/521149890: disable FFC */
	ctx->force_ffc_off = true;

	return 0;
}

static int iyidb_panel_config(struct gs_panel *ctx)
{
	struct iyidb_panel *spanel = to_spanel(ctx);
	u16 nbm_max_dbv;
	int ret;

	// TODO(hungyeh): b/458181770 config vendor info
	ret = gs_panel_update_brightness_desc(&iyidb_brightness_desc, iyidb_brt_configs,
						   ARRAY_SIZE(iyidb_brt_configs),
						   ctx->panel_rev_bitmask);
	if (ret)
		return ret;

	/* b/490962578: use NBM_MAX as crosstalk threshold */
	nbm_max_dbv = iyidb_brightness_desc.brt_capability->normal.level.max;
	spanel->crosstalk_threshold_dbv = nbm_max_dbv;

	return 0;
}

static void iyidb_panel_init(struct gs_panel *ctx)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	ctx->refresh_ctrl |= GS_PANEL_REFRESH_CTRL_EARLY_EXIT;
	iyidb_update_refresh_ctrl_feat(ctx, pmode, false);
	ctx->hw_status.irc_mode = IRC_FLAT_DEFAULT;

#ifndef PANEL_FACTORY_BUILD
	/* default fixed TE2 240Hz */
	ctx->te2.option = TEX_OPT_FIXED;
	ctx->te2.freq_hz = 240;
#endif

	/* FFC is disabled in bootloader */
	ctx->ffc_en = false;

	ctx->pwm_mode = GS_PWM_RATE_STANDARD;

	ctx->op_hz = 120;

	/* re-init panel to decouple bootloader settings */
	if (pmode) {
		dev_info(ctx->dev, "set mode: %s\n", pmode->mode.name);
		ctx->sw_status.idle_vrefresh = 0;
		iyidb_set_panel_feat(ctx, pmode, true);
		iyidb_change_frequency(ctx, pmode);
		iyidb_update_ffc(ctx, MIPI_DSI_FREQ_DEFAULT);
		iyidb_te2_setting(ctx);
		gs_panel_send_cmdset(ctx, &iyidb_errfg_settings_cmdset);
	}
}

static void iyidb_get_panel_rev(struct gs_panel *ctx, u32 id)
{
	gs_panel_get_panel_rev_full(ctx, id);
}

static void iyidb_panel_remove(struct mipi_dsi_device *dsi)
{
	const struct gs_panel *ctx = mipi_dsi_get_drvdata(dsi);

	if (ctx->thermal && ctx->thermal->tz)
		thermal_zone_device_unregister(ctx->thermal->tz);
	gs_dsi_panel_common_remove(dsi);
}

static void iyidb_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct gs_panel *ctx = mipi_dsi_get_drvdata(dsi);

	if (gs_is_panel_active(ctx)) {
		dev_warn(ctx->dev, "panel still active when shutdown\n");
		drm_panel_disable(&ctx->base);
	}

	if (gs_is_panel_enabled(ctx)) {
		dev_dbg(ctx->dev, "powering off the panel\n");
		drm_panel_unprepare(&ctx->base);
	}

	gs_panel_gpio_set_optional(ctx, DISP_TP_RESET_GPIO, 0);
}

static const struct drm_panel_funcs iyidb_drm_funcs = {
	.disable = iyidb_disable,
	.unprepare = gs_panel_unprepare,
	.prepare = gs_panel_prepare,
	.enable = iyidb_enable,
	.get_modes = gs_panel_get_modes,
	.debugfs_init = iyidb_debugfs_init,
};

static const struct gs_panel_funcs iyidb_gs_funcs = {
	.set_brightness = iyidb_set_brightness,
	.set_lp_mode = iyidb_set_lp_mode,
	.set_nolp_mode = iyidb_set_nolp_mode,
	.set_binned_lp = gs_panel_set_binned_lp_helper,
	.set_hbm_mode = iyidb_set_hbm_mode,
	.set_dimming = iyidb_set_dimming,
	.is_mode_seamless_atomic = iyidb_is_mode_seamless_atomic,
	.mode_set = iyidb_mode_set,
	.panel_init = iyidb_panel_init,
	.panel_config = iyidb_panel_config,
	.get_panel_rev = iyidb_get_panel_rev,
	.refresh_ctrl = iyidb_refresh_ctrl,
	.read_serial = gs_panel_read_slsi_ddic_id,
	.pre_update_ffc = iyidb_pre_update_ffc,
	.update_ffc = iyidb_update_ffc,
	.set_pwm_mode = iyidb_set_pwm_mode,
	.get_pwm_mode = iyidb_get_pwm_mode,
	.get_color_data = iyidb_get_color_data,
	.panel_reset = iyidb_panel_reset,
	.run_reset_gpio_sequence = iyidb_run_reset_gpio_sequence,
	.get_te2_edges = gs_panel_get_te2_edges_helper,
	.set_te2_edges = gs_panel_set_te2_edges_helper,
	.update_te2 = iyidb_update_te2,
	.set_te2_freq = iyidb_set_te2_freq,
	.get_te2_freq = iyidb_get_te2_freq,
	.set_te2_option = iyidb_set_te2_option,
	.get_te2_option = iyidb_get_te2_option,
	.set_op_hz = iyidb_set_op_hz,
	.is_mode_valid = iyidb_is_mode_valid,
	.set_self_refresh = iyidb_set_self_refresh,
	.commit_done = iyidb_commit_done,
	.handle_skin_temperature = iyidb_handle_skin_temperature,
	.detect_fault = iyidb_detect_fault,
	.trace_panel_settings_full = gs_panel_trace_settings_full_helper,
};

const struct gs_panel_reg_ctrl_desc iyidb_reg_ctrl_desc = {
	.reg_ctrl_enable = {
		{ PANEL_REG_ID_VDDI, 0 },
		{ PANEL_REG_ID_VCI, 2 },
		{ PANEL_REG_ID_VDDD, 10 },
	},
	.reg_ctrl_pre_disable = {
		{ PANEL_REG_ID_VDDD, 4 },
	},
	.reg_ctrl_disable = {
		{ PANEL_REG_ID_VCI, 0 },
		{ PANEL_REG_ID_VDDI, 0 },
	},
};

static struct gs_panel_calibration_desc iyidb_calibration_desc = {
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

static const struct gs_panel_detect_fault_desc iyidb_fault_desc = {
	.errfg_reg = 0xEE,
	.errfg_len = 2,
	.dsi_err_reg = 0xE9,
	.vgh_mask = BIT(8),
	.vlin1_mask = BIT(6),
	.dsi_err_mask = BIT(0),
	.detect_interval_ms = 5000,
	.panel_errors_mask = BIT(GS_PANEL_ERR_DSI_READ_FAILURE),
};

const struct gs_panel_desc google_iyidb = {
	.data_lane_cnt = 4,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.brightness_desc = &iyidb_brightness_desc,
	.calibration_desc = &iyidb_calibration_desc,
	.modes = &iyidb_modes,
	.off_cmdset = &iyidb_sleep_in_cmdset,
	.lp_modes = &iyidb_lp_modes,
	.binned_lp = iyidb_binned_lp,
	.num_binned_lp = ARRAY_SIZE(iyidb_binned_lp),
	.reg_ctrl_desc = &iyidb_reg_ctrl_desc,
	.panel_func = &iyidb_drm_funcs,
	.gs_panel_func = &iyidb_gs_funcs,
	.default_dsi_hs_clk_mbps = MIPI_DSI_FREQ_DEFAULT,
	.fault_desc = &iyidb_fault_desc,
};

static const struct of_device_id gs_panel_of_match[] = {
	{ .compatible = "google,gs-iyidb", .data = &google_iyidb },
	{ }
};
MODULE_DEVICE_TABLE(of, gs_panel_of_match);

static struct mipi_dsi_driver gs_panel_driver = {
	.probe = iyidb_panel_probe,
	.remove = iyidb_panel_remove,
	.shutdown = iyidb_panel_shutdown,
	.driver = {
		.name = "panel-gs-iyidb",
		.of_match_table = gs_panel_of_match,
	},
};
module_mipi_dsi_driver(gs_panel_driver);

MODULE_AUTHOR("Hung-Yeh Lee <hungyeh@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google iyidb panel driver");
MODULE_LICENSE("Dual MIT/GPL");

