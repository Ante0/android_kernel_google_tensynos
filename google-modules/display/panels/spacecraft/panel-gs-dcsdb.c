// SPDX-License-Identifier: MIT
#include <drm/display/drm_dsc_helper.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/thermal.h>
#include <video/mipi_display.h>

#include "gs_panel/drm_panel_funcs_defaults.h"
#include "gs_panel/gs_panel.h"
#include "gs_panel/gs_panel_funcs_defaults.h"
#include "trace/panel_trace.h"

#define DCSDB_DDIC_ID_LEN 11
#define DCSDB_DIMMING_FRAME 32
#define DCSDB_LUMINANCE_READS_PER_DBV 4
#define DCSDB_LUMINANCE_READ_LONG_LEN 18
#define DCSDB_LUMINANCE_READ_SHORT_LEN 14
#define DCSDB_DEFAULT_VDDD 1100000

#define MIPI_DSI_FREQ_MBPS_DEFAULT 756
#define MIPI_DSI_FREQ_MBPS_ALTERNATIVE 740

#define UNPREPARE_DELAY_MS 10

/* DBV corresponding to 2000 nits */
#define DCSDB_PROTO_MAX_HBM_BRIGHTNESS_LEVEL 4023
#define DCSDB_EVT_MAX_HBM_BRIGHTNESS_LEVEL 3870
/* Max supported DBV */
#define DCSDB_PROTO_MAX_BRIGHTNESS_LEVEL 4095
#define DCSDB_EVT_MAX_BRIGHTNESS_LEVEL 3992

#define PROJECT "DCSDB"

static const u8 VRR_MIN_IDLE_RR_HZ = 60;

/* Truncate 8-bit signed value to 6-bit signed value */
#define TO_6BIT_SIGNED(v) ((v) & 0x3F)

static struct drm_dsc_config dcsdb_dsc_cfg = {
	/* Used DSC v1.2 */
	.dsc_version_major = 1,
	.dsc_version_minor = 2,
	.line_buf_depth = 9,
	.bits_per_component = 8,
	.convert_rgb = true, /* confirm */
	.slice_count = 2,
	.slice_width = 540,
	.slice_height = 24,
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
		{0, 4, TO_6BIT_SIGNED(2)},
		{0, 4, TO_6BIT_SIGNED(0)},
		{1, 5, TO_6BIT_SIGNED(0)},
		{1, 6, TO_6BIT_SIGNED(-2)},
		{3, 7, TO_6BIT_SIGNED(-4)},
		{3, 7, TO_6BIT_SIGNED(-6)},
		{3, 7, TO_6BIT_SIGNED(-8)},
		{3, 8, TO_6BIT_SIGNED(-8)},
		{3, 9, TO_6BIT_SIGNED(-8)},
		{3, 10, TO_6BIT_SIGNED(-10)},
		{5, 10, TO_6BIT_SIGNED(-10)},
		{5, 11, TO_6BIT_SIGNED(-12)},
		{5, 11, TO_6BIT_SIGNED(-12)},
		{9, 12, TO_6BIT_SIGNED(-12)},
		{12, 13, TO_6BIT_SIGNED(-12)},
	},
	.rc_model_size = 8192,
	.flatness_min_qp = 3,
	.flatness_max_qp = 12,
	.initial_scale_value = 32,
	.scale_decrement_interval = 7,
	.scale_increment_interval = 588,
	.nfl_bpg_offset = 1069,
	.slice_bpg_offset = 1085,
	.final_offset = 4336,
	.vbr_enable = false,
	.slice_chunk_size = 540,
	.native_422 = false,
	.native_420 = false,
	.second_line_bpg_offset = 0,
	.nsl_bpg_offset = 0,
	.second_line_offset_adj = 0,
};

#define DCSDB_DSC {\
		.enabled = true, \
		.dsc_count = 2, \
		.cfg = &dcsdb_dsc_cfg, \
}

static const u16 WIDTH_MM = 65, HEIGHT_MM = 146;
static const u16 HDISPLAY = 1080, VDISPLAY = 2424;
static const u16 HFP = 32, HSA = 12, HBP = 16;
static const u16 VFP = 12, VSA = 4, VBP = 15;

static const struct gs_panel_mode_array dcsdb_modes = GS_PANEL_MODES(
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
			.te_usec = 8447,
			.bpc = 8,
			.dsc = DCSDB_DSC,
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
			.te_usec = 276,
			.bpc = 8,
			.dsc = DCSDB_DSC,
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
			.te_usec = 8447,
			.bpc = 8,
			.dsc = DCSDB_DSC,
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
			.te_usec = 276,
			.bpc = 8,
			.dsc = DCSDB_DSC,
		},
	},
);

static const struct gs_panel_mode_array dcsdb_lp_modes = GS_PANEL_MODES(
	{
		.mode = {
			.name = "1080x2424x30@30",
			DRM_MODE_TIMING(30, 1080, 32, 12, 16, 2424, 12, 4, 15),
			.type = DRM_MODE_TYPE_DRIVER,
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 1902,
			.bpc = 8,
			.dsc = DCSDB_DSC,
			.is_lp_mode = true,
		},
	},
);

static const struct gs_brightness_configuration dcsdb_brt_configs[] = {
	{
		.panel_rev = PANEL_REV_EVT1_1 | PANEL_REV_LATEST,
		.default_brightness = 1715, /* dbv_for_140_nits */
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 1,
					.max = 1400,
				},
				.level = {
					.min = 1,
					.max = 3611,
				},
				.percentage = {
					.min = 0,
					.max = 70,
				},
			},
			.hbm = {
				.nits = {
					.min = 1400,
					.max = 2000,
				},
				.level = {
					.min = 3612,
					.max = 3870,
				},
				.percentage = {
					.min = 70,
					.max = 100,
				},
			},
		},
	},
	{
		.panel_rev = PANEL_REV_EVT1,
		.default_brightness = 1715, /* dbv_for_140_nits */
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 1,
					.max = 1400,
				},
				.level = {
					.min = 1,
					.max = 3611,
				},
				.percentage = {
					.min = 0,
					.max = 64,
				},
			},
			.hbm = {
				.nits = {
					.min = 1400,
					.max = 2200,
				},
				.level = {
					.min = 3612,
					.max = 3939,
				},
				.percentage = {
					.min = 64,
					.max = 100,
				},
			},
		},
	},
	{
		.panel_rev = PANEL_REV_PROTO1 | PANEL_REV_PROTO1_1,
		.default_brightness = 1783, /* dbv_for_140_nits */
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 1,
					.max = 1400,
				},
				.level = {
					.min = 1,
					.max = 3754,
				},
				.percentage = {
					.min = 0,
					.max = 64,
				},
			},
			.hbm = {
				.nits = {
					.min = 1400,
					.max = 2200,
				},
				.level = {
					.min = 3755,
					.max = 4095,
				},
				.percentage = {
					.min = 64,
					.max = 100,
				},
			},
		},
	},
};

static const struct gs_dsi_cmd dcsdb_lp_cmds[] = {
	/* disable dimming */
	GS_DSI_QUEUE_CMD(0x53, 0x20),
	/* enter AOD */
	GS_DSI_FLUSH_CMD(MIPI_DCS_ENTER_IDLE_MODE),
};
static DEFINE_GS_CMDSET(dcsdb_lp);

static const struct gs_dsi_cmd dcsdb_lp_night_cmds[] = {
	/* 2 nit(P1.0) / 1 nit(P1.1~) */
	GS_DSI_QUEUE_CMD(0x6F, 0x04),
	GS_DSI_FLUSH_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x00, 0x03),
};

static const struct gs_dsi_cmd dcsdb_lp_low_cmds[] = {
	/* 10 nit */
	GS_DSI_QUEUE_CMD(0x6F, 0x04),
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_PROTO1, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x06, 0x71),
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_GE(PANEL_REV_PROTO1_1),
					MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x06, 0xCF),
};

static const struct gs_dsi_cmd dcsdb_lp_high_cmds[] = {
	/* 50 nit */
	GS_DSI_QUEUE_CMD(0x6F, 0x04),
	GS_DSI_FLUSH_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0B, 0xAA),
};

static const struct gs_dsi_cmd dcsdb_lp_sun_cmds[] = {
	/* 150 nit */
	GS_DSI_QUEUE_CMD(0x6F, 0x04),
	GS_DSI_FLUSH_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0F, 0xFE),
};

static const struct gs_binned_lp dcsdb_binned_lp[] = {
	/* night threshold 4 nits */
	BINNED_LP_MODE_TIMING("night", 252, dcsdb_lp_night_cmds, 0, 45),
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 909, dcsdb_lp_low_cmds, 0, 45),
	/* high threshold 140 nits */
	BINNED_LP_MODE_TIMING("high", 1715, dcsdb_lp_high_cmds, 0, 45),
	BINNED_LP_MODE_TIMING("sun", 4095, dcsdb_lp_sun_cmds, 0, 45),
};

static const struct gs_dsi_cmd dcsdb_off_cmds[] = {
	GS_DSI_FLUSH_DELAY_CMD(100, MIPI_DCS_SET_DISPLAY_OFF),
	GS_DSI_FLUSH_DELAY_CMD(120, MIPI_DCS_ENTER_SLEEP_MODE),
};
static DEFINE_GS_CMDSET(dcsdb_off);

static const struct gs_dsi_cmd dcsdb_init_cmds[] = {
	/* b/511167409: DBI param */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1), 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x08),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1), 0x6F, 0x06),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1), 0xCB, 0x71, 0x00, 0x00),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1), 0x6F, 0x06),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1), 0xCC, 0x8D, 0x00, 0x00),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1), 0x6F, 0x06),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1), 0xCE, 0xA2, 0x20, 0x00),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_DVT1), 0xF0, 0x55, 0xAA, 0x52, 0x00, 0x00),

	/* CMD3, Page0 */
	GS_DSI_QUEUE_CMD(0xFF, 0xAA, 0x55, 0xA5, 0x80),
	GS_DSI_QUEUE_CMD(0x6F, 0x16),
	GS_DSI_QUEUE_CMD(0xF4, 0x02, 0x74),
	GS_DSI_QUEUE_CMD(0x6F, 0x31),
	GS_DSI_QUEUE_CMD(0xF8, 0x01, 0x74),
	GS_DSI_QUEUE_CMD(0x6F, 0x15),
	GS_DSI_QUEUE_CMD(0xF8, 0x01, 0x8D),
	GS_DSI_QUEUE_CMD(0x6F, 0x09),
	GS_DSI_QUEUE_CMD(0xFC, 0x03),

	/* CMD3, Page1 */
	GS_DSI_QUEUE_CMD(0xFF, 0xAA, 0x55, 0xA5, 0x81),
	GS_DSI_QUEUE_CMD(0x6F, 0x0E),
	GS_DSI_QUEUE_CMD(0xF5, 0x2A),
	GS_DSI_QUEUE_CMD(0x6F, 0x0F),
	GS_DSI_QUEUE_CMD(0xF5, 0x22),

	/* CMD3, Page1 */
	GS_DSI_QUEUE_CMD(0xFF, 0xAA, 0x55, 0xA5, 0x81),
	GS_DSI_QUEUE_CMD(0x6F, 0x0B),
	GS_DSI_QUEUE_CMD(0xFD, 0x04),

	/* CMD3, Page2 */
	GS_DSI_QUEUE_CMD(0xFF, 0xAA, 0x55, 0xA5, 0x82),
	GS_DSI_QUEUE_CMD(0x6F, 0x09),
	GS_DSI_QUEUE_CMD(0xF2, 0x55),
	GS_DSI_QUEUE_CMD(0xF8, 0x0F),

	/* CMD Disable */
	GS_DSI_QUEUE_CMD(0xFF, 0xAA, 0x55, 0xA5, 0x00),
	GS_DSI_QUEUE_CMD(0x35, 0x00, 0x2D),
	GS_DSI_QUEUE_CMD(0x6F, 0x14),

	/* TE Sel */
	GS_DSI_QUEUE_CMD(0x35, 0x00, 0xE0),
	GS_DSI_QUEUE_CMD(0x6F, 0x2D),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x2E),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x2F),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x30),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x31),
	GS_DSI_QUEUE_CMD(0x44, 0x42),
	GS_DSI_QUEUE_CMD(0x6F, 0x32),
	GS_DSI_QUEUE_CMD(0x44, 0x04),
	GS_DSI_QUEUE_CMD(0x6F, 0x33),
	GS_DSI_QUEUE_CMD(0x44, 0xD4),

	GS_DSI_QUEUE_CMD(0x6F, 0x54),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x55),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x56),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x57),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x58),
	GS_DSI_QUEUE_CMD(0x44, 0x00),

	GS_DSI_QUEUE_CMD(0x6F, 0x3B),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x3C),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x3D),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x3E),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x3F),
	GS_DSI_QUEUE_CMD(0x44, 0x42),
	GS_DSI_QUEUE_CMD(0x6F, 0x40),
	GS_DSI_QUEUE_CMD(0x44, 0x01),
	GS_DSI_QUEUE_CMD(0x6F, 0x41),
	GS_DSI_QUEUE_CMD(0x44, 0x35),

	GS_DSI_QUEUE_CMD(0x6F, 0x5E),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x5F),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x60),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x61),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x62),
	GS_DSI_QUEUE_CMD(0x44, 0x00),
	GS_DSI_QUEUE_CMD(0x6F, 0x63),
	GS_DSI_QUEUE_CMD(0x44, 0x66),

	/* ACD off */
	GS_DSI_QUEUE_CMD(0x55, 0x00),
	GS_DSI_QUEUE_CMD(0x53, 0x20),
	GS_DSI_QUEUE_CMD(0x2A, 0x00, 0x00, 0x04, 0x37),
	GS_DSI_QUEUE_CMD(0x2B, 0x00, 0x00, 0x09, 0x77),

	/* Normal GMA */
	GS_DSI_QUEUE_CMD(0x26, 0x00),

	/* Normal DBV */
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0x51, 0x0E, 0xAA),
	GS_DSI_QUEUE_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1), 0x51, 0x0E, 0x1B),

	/* Select index */
	GS_DSI_QUEUE_CMD(0x6F, 0x04),
	GS_DSI_QUEUE_CMD(0x51, 0x0F, 0xFE),
	GS_DSI_QUEUE_CMD(0x81, 0x01, 0x19),

	/* VESA, slice 24 (2-dec) IC setting */
	GS_DSI_QUEUE_CMD(0x90, 0x03, 0x43),
	GS_DSI_QUEUE_CMD(0x91, 0x89, 0xA8, 0x00, 0x18, 0xC2, 0x00, 0x02, 0x0E, 0x02, 0x4C, 0x00,
			   0x07, 0x04, 0x2D, 0x04, 0x3D, 0x10, 0xF0),
	GS_DSI_QUEUE_CMD(0x6F, 0x12),
	GS_DSI_QUEUE_CMD(0x91, 0x01, 0x02, 0x01, 0x00, 0x09, 0x40, 0x09, 0xBE, 0x19, 0xFC, 0x19,
		   0xFA, 0x19, 0xF8, 0x1A, 0x38, 0x1A, 0x78, 0x1A, 0xB6, 0x2A, 0xB6, 0x2A, 0xF4,
		   0x2A, 0xF4, 0x4B, 0x34, 0x63, 0x74),

	/* AOD Saving mode Off */
	GS_DSI_QUEUE_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x03),
	GS_DSI_QUEUE_CMD(0xC7, 0x00),

	/* TE2: rising 0, width 45 */
	GS_DSI_QUEUE_CMD(0x44, 0x00, 0x00),
	GS_DSI_QUEUE_CMD(0x35, 0x00, 0x2D),

	/* Page0 */
	GS_DSI_QUEUE_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00),
	GS_DSI_QUEUE_CMD(0xBE, 0x47, 0x4F, 0x4C, 0x5F),
	GS_DSI_QUEUE_CMD(0x6F, 0x09),
	GS_DSI_QUEUE_CMD(0xBE, 0x04),
	/* Page3 */
	GS_DSI_QUEUE_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x03),
	GS_DSI_QUEUE_CMD(0xC4, 0x00, 0x00, 0x00, 0x00, 0x60, 0x10, 0x60, 0x3D, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x10, 0x60,
			 0x3D, 0x60, 0x10, 0x60, 0x3D, 0x60, 0x10, 0x60, 0x3D, 0x60, 0x10, 0x60,
			 0x3D),
	/* Page5 */
	GS_DSI_QUEUE_CMD(0xF0, 0x55, 0xAA, 0x52, 0x08, 0x05),
	GS_DSI_QUEUE_CMD(0xB0, 0x83, 0x27, 0x01),
	GS_DSI_QUEUE_CMD(0xB6, 0x07, 0x02, 0x00, 0x00, 0x00, 0x00, 0x87, 0x82, 0x00, 0x00, 0x00,
			 0x00),
	/* CMD1 */
	GS_DSI_QUEUE_CMD(0x6F, 0x01),
	GS_DSI_QUEUE_CMD(0x5A, 0x04),
	GS_DSI_QUEUE_CMD(0x6F, 0x01),
	GS_DSI_QUEUE_CMD(0x8D, 0x06),
	GS_DSI_QUEUE_CMD(0x6F, 0x26),
	GS_DSI_QUEUE_CMD(0x44, 0x09, 0x66, 0x00, 0x00, 0x52, 0x09, 0xA8),
	GS_DSI_QUEUE_CMD(0x6F, 0x34),
	GS_DSI_QUEUE_CMD(0x44, 0x09, 0x66, 0x00, 0x00, 0x52, 0x09, 0xA8),
	GS_DSI_QUEUE_CMD(0x6F, 0x63),
	GS_DSI_QUEUE_CMD(0x44, 0x66),
	GS_DSI_QUEUE_CMD(0x6F, 0x14),
	GS_DSI_FLUSH_CMD(0x35, 0x60, 0xE0),

	// b/466265306: new FFC setting to mitigate drift
	GS_DSI_QUEUE_CMD(0xC3, 0x00, 0x06, 0x20,
			   0x0C, 0xFF, 0x00, 0x06, 0x20, 0x0C,
			   0xFF, 0x00, 0x05, 0xEE, 0x16, 0x06,
			   0x3E, 0x11, 0x05, 0xEE, 0x16, 0x06,
			   0x3E, 0x11, 0x05, 0xEE, 0x16, 0x06,
			   0x3E, 0x11, 0x05, 0xEE, 0x16, 0x06,
			   0x3E, 0x11, 0x05, 0xEE, 0x16, 0x06,
			   0x3E, 0x11),
	GS_DSI_QUEUE_CMD(0xC3, 0xDD),
	GS_DSI_FLUSH_DELAY_CMD(120, MIPI_DCS_EXIT_SLEEP_MODE),
};
static DEFINE_GS_CMDSET(dcsdb_init);

/**
 * struct dcsdb_luminance_read_config
 *
 * This struct maintains internal status during luminance data read
 */
struct dcsdb_luminance_read_config {
	int dbv_idx;
	int freq_irc_idx;
	bool done;
};

struct temp_vddd_mapping {
	u32 temp_min;
	u32 temp_max;
	u32 vddd;
};

struct temp_vddd_mapping mappings[] = {
	{0, 39, 1075000},	// temperature <= 39°C, set vddd 1.075v
	{40, 59, 1087500},	// 40°C <= temp <= 59°C, set vddd 1.0875v
	{60, 79, 1100000},	// 60°C <= temp <= 79°C, set vddd 1.1v
	{80, 99, 1125000}	// 80°C <= temp , set vddd 1.125v
};

/**
 * struct dcsdb_panel - panel specific runtime info
 *
 * This struct maintains dcsdb panel specific runtime info, any fixed details about panel
 * should most likely go into struct gs_panel_desc.
 */
struct dcsdb_panel {
	/** @base: base panel struct */
	struct gs_panel base;
	/** @color_read_type: data type to read from flash */
	enum color_data_type color_read_type;
	/** @luminance_read_config: luminance-specific read status struct */
	struct dcsdb_luminance_read_config luminance_read_config;
	/** @hw_temp: the temperature applied into panel */
	u32 hw_temp;
	/** @hw_vddd: the vddd voltage applied into panel */
	u32 hw_vddd;
	/** @force_dynamic_vddd_off: force to turn off dynamic vddd */
	bool force_dynamic_vddd_off;
	/** @is_hbm2_enabled: indicates panel is running in HBM mode 2 */
	bool is_hbm2_enabled;
	/** @in_aod: indicates panel is in Always-On Display mode */
	bool in_aod;
};

#define to_spanel(ctx) container_of(ctx, struct dcsdb_panel, base)

static bool _is_max_hbm_level(u16 level, struct gs_panel *ctx)
{
	if (ctx->panel_rev_id.id < PANEL_REVID_EVT1)
		return level == DCSDB_PROTO_MAX_HBM_BRIGHTNESS_LEVEL;
	else
		return level == DCSDB_EVT_MAX_HBM_BRIGHTNESS_LEVEL;
}

/**
 * @return max DBV when reach hbm max with irc off
 *
 * Max DBV capped to 3992(=0xF98) starting from EVT1.1
 */
static u16 _get_max_brightness_level(struct gs_panel *ctx)
{
	if (ctx->panel_rev_id.id < PANEL_REVID_EVT1)
		return DCSDB_PROTO_MAX_BRIGHTNESS_LEVEL;
	else
		return DCSDB_EVT_MAX_BRIGHTNESS_LEVEL;
}

static void dcsdb_update_acd(struct gs_panel *ctx, u16 level)
{
	struct device *dev = ctx->dev;
	struct dcsdb_panel *spanel = to_spanel(ctx);

	if (ctx->panel_rev_id.id < PANEL_REVID_EVT1)
		return;

	if (GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode) && _is_max_hbm_level(level, ctx)) {
		spanel->is_hbm2_enabled = true;
		/* set ACD Level 3 */
		GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x55, 0x04);
	} else {
		if (spanel->is_hbm2_enabled) {
			/* set ACD off */
			GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x55, 0x00);
		}
		spanel->is_hbm2_enabled = false;
	}
	dev_info(ctx->dev, "%s: is HBM2 enabled : %d\n",
				__func__, spanel->is_hbm2_enabled);
}

static void dcsdb_update_irc(struct gs_panel *ctx, const enum gs_hbm_mode hbm_mode,
							const int vrefresh)
{
	struct device *dev = ctx->dev;
	const u16 level = gs_panel_get_brightness(ctx);

	if (GS_IS_HBM_ON_IRC_OFF(hbm_mode)) {
		if (ctx->panel_rev_id.id < PANEL_REVID_EVT1) {
			/* Peak luminance ON */
			GS_DCS_BUF_ADD_CMD(dev, 0x5F, 0x05, 0x00);
			GS_DCS_BUF_ADD_CMD(dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0F, 0xFF);
		} else {
			/*
			 * sync from bigSurf : to achieve the max brightness with IRC off which
			 * need to set dbv to 0xFFF
			 */
			if (_is_max_hbm_level(level, ctx)) {
				u16 br = _get_max_brightness_level(ctx);

				GS_DCS_BUF_ADD_CMD(dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
									br >> 8, br & 0xFF);
			}

			dcsdb_update_acd(ctx, level);

			/* IRC Off */
			GS_DCS_BUF_ADD_CMD(dev, 0x5F, 0x01, 0x00);
			if (vrefresh == 120)
				GS_DCS_BUF_ADD_CMD(dev, 0x2F, 0x00);
			else
				GS_DCS_BUF_ADD_CMD(dev, 0x2F, 0x02);
		}
	} else {
		const u8 val1 = level >> 8;
		const u8 val2 = level & 0xff;

		if (ctx->panel_rev_id.id < PANEL_REVID_EVT1) {
			/* Peak luminance OFF */
			GS_DCS_BUF_ADD_CMD(dev, 0x5F, 0x00, 0x00);
		} else {
			/* IRC On */
			GS_DCS_BUF_ADD_CMD(dev, 0x5F, 0x00, 0x00);
			if (vrefresh == 120)
				GS_DCS_BUF_ADD_CMD(dev, 0x2F, 0x00);
			else
				GS_DCS_BUF_ADD_CMD(dev, 0x2F, 0x02);
		}

		/* sync from bigSurf : restore the dbv value while IRC ON */
		GS_DCS_BUF_ADD_CMD(dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, val1, val2);
	}
	/* Empty command is for flush */
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_NOP);
}

static void dcsdb_change_frequency(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	u32 vrefresh = gs_is_vrr_mode(pmode) ? ctx->sw_status.idle_vrefresh :
					       drm_mode_vrefresh(&pmode->mode);

	if (vrefresh > drm_mode_vrefresh(&pmode->mode)) {
		dev_warn(dev, "%s: out of range freq %uHz, clamping to %uHz\n",
			      __func__, vrefresh, drm_mode_vrefresh(&pmode->mode));
		vrefresh = drm_mode_vrefresh(&pmode->mode);
	}

	if (vrefresh == 120)
		GS_DCS_BUF_ADD_CMD(dev, 0x2F, 0x00);
	else
		GS_DCS_BUF_ADD_CMD(dev, 0x2F, 0x02);

	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_NOP);

	notify_panel_te2_freq_changed(ctx, 0);

	ctx->panel_settings_changed = true;

	dev_dbg(dev, "%s: change to %dhz\n", __func__, vrefresh);
}

static void dcsdb_mode_set(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;

	if (gs_is_vrr_mode(pmode)) {
		GS_DCS_BUF_ADD_CMD(dev, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
		if (drm_mode_vrefresh(&pmode->mode) > 60)
			GS_DCS_BUF_ADD_CMD(dev, 0xBE, 0x47);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0xBE, 0x4F);
	}

	dcsdb_change_frequency(ctx, pmode);

	dev_dbg(dev, "%s: change to %dhz\n", __func__, drm_mode_vrefresh(&pmode->mode));
}

static void dcsdb_refresh_ctrl(struct gs_panel *ctx)
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
			dcsdb_change_frequency(ctx, pmode);
		} else {
			dev_warn(ctx->dev,
				 "refresh_ctrl: %uHz min RR requested, only %u/%u Hz supported\n",
				 min_vrefresh, VRR_MIN_IDLE_RR_HZ, vrefresh);
		}
	}

	PANEL_ATRACE_END(__func__);
}

static void dcsdb_set_dimming(struct gs_panel *ctx, bool dimming_on)
{
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;

	if (pmode->gs_mode.is_lp_mode) {
		dev_warn(dev, "in lp mode, skip dimming update\n");
		return;
	}

	ctx->dimming_on = dimming_on;
	GS_DCS_WRITE_CMD(dev, MIPI_DCS_WRITE_CONTROL_DISPLAY,
					ctx->dimming_on ? 0x28 : 0x20);
	dev_dbg(dev, "%s dimming_on=%d\n", __func__, dimming_on);
}

static void dcsdb_set_lp_mode(struct gs_panel *ctx, const struct gs_panel_mode *mode)
{
	struct dcsdb_panel *spanel = to_spanel(ctx);

	gs_panel_set_lp_mode_helper(ctx, mode);
	spanel->in_aod = true;
}

static void dcsdb_set_nolp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	struct dcsdb_panel *spanel = to_spanel(ctx);
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (!gs_is_panel_active(ctx))
		return;

	/* exit AOD */
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_EXIT_IDLE_MODE);

	dcsdb_mode_set(ctx, pmode);

	/* Delay setting dimming on AOD exit until brightness has stabilized. */
	ctx->timestamps.idle_exit_dimming_delay_ts = ktime_add_us(
		ktime_get(), 100 + GS_VREFRESH_TO_PERIOD_USEC(vrefresh) * 2);

	spanel->in_aod = false;
	dev_info(dev, "exit LP mode\n");
}

static void dcsdb_dimming_frame_setting(struct gs_panel *ctx, u8 dimming_frame)
{
	struct device *dev = ctx->dev;

	/* Fixed time 1 frame */
	if (!dimming_frame)
		dimming_frame = 0x01;

	GS_DCS_BUF_ADD_CMD(dev, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x00);
	GS_DCS_BUF_ADD_CMD(dev, 0xB2, 0x19);
	GS_DCS_BUF_ADD_CMD(dev, 0x6F, 0x05);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xB2, dimming_frame, dimming_frame);
}

static void dcsdb_set_vddd(struct gs_panel *ctx, u32 temperature)
{
	struct dcsdb_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	int ret;

	if (spanel->force_dynamic_vddd_off) {
		if (spanel->hw_vddd != DCSDB_DEFAULT_VDDD) {
			ret = regulator_set_voltage(ctx->regulator.vddd,
								DCSDB_DEFAULT_VDDD,
								DCSDB_DEFAULT_VDDD);
			if (ret)
				dev_err(dev,
					"Failed to restore VDDD voltage: %d\n", ret);
			else
				spanel->hw_vddd = DCSDB_DEFAULT_VDDD;
		}
		return;
	}

	if (temperature != spanel->hw_temp) {
		for (int i = 0; i < ARRAY_SIZE(mappings); i++) {
			if (temperature >= mappings[i].temp_min
					&& temperature <= mappings[i].temp_max) {
				if (mappings[i].vddd != spanel->hw_vddd) {
					dev_info(dev, "setting VDDD to %d for temperature %d\n",
						mappings[i].vddd, temperature);
					ret = regulator_set_voltage(ctx->regulator.vddd,
									mappings[i].vddd,
									mappings[i].vddd);
					if (ret)
						dev_err(dev,
							"Failed to set VDDD voltage: %d\n", ret);
					else
						spanel->hw_vddd = mappings[i].vddd;
				}
				break;
			}
		}
		spanel->hw_temp = temperature;
	}
}

static void dcsdb_update_temperature(struct gs_panel *ctx)
{
	struct thermal_zone_device *thermal_dev;
	struct device *dev = ctx->dev;
	int temperature;
	int ret = 0;

	dev_dbg(dev, "in display temperature work queue\n");

	thermal_dev = thermal_zone_get_zone_by_name("disp0_therm");

	if (IS_ERR(thermal_dev)) {
		dev_err(dev, "Unable to get thermal zone for tuning\n");
		return;
	}

	ret = thermal_zone_get_temp(thermal_dev, &temperature);

	if (ret) {
		dev_err(dev, "Unable to get the disp temperature\n");
		return;
	}

	temperature =  DIV_ROUND_CLOSEST(temperature, 1000);
	dev_info(dev, "display temperature : %d\n", temperature);

	dcsdb_set_vddd(ctx, temperature);
}

static void dcsdb_common_work(struct gs_panel *ctx)
{
	dcsdb_update_temperature(ctx);
}

static void dcsdb_pre_update_ffc(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	dev_dbg(ctx->dev, "disabling FFC\n");

	PANEL_ATRACE_BEGIN(__func__);

	/* FFC off */
	GS_DCS_BUF_ADD_CMD(dev, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xC3, 0x00);

	PANEL_ATRACE_END(__func__);

	ctx->ffc_en = false;
}

static void dcsdb_update_ffc(struct gs_panel *ctx, unsigned int hs_clk_mbps)
{
	struct device *dev = ctx->dev;

	dev_dbg(ctx->dev, "hs_clk_mbps: current=%d, target=%d\n",
		ctx->dsi_hs_clk_mbps, hs_clk_mbps);

	PANEL_ATRACE_BEGIN(__func__);

	if (hs_clk_mbps != MIPI_DSI_FREQ_MBPS_DEFAULT &&
	    hs_clk_mbps != MIPI_DSI_FREQ_MBPS_ALTERNATIVE) {
		dev_warn(ctx->dev, "invalid hs_clk_mbps=%d for FFC\n", hs_clk_mbps);
	} else if (ctx->dsi_hs_clk_mbps != hs_clk_mbps || !ctx->ffc_en) {
		dev_info(ctx->dev, "updating FFC for hs_clk_mbps=%d\n", hs_clk_mbps);
		ctx->dsi_hs_clk_mbps = hs_clk_mbps;

		/* Update FFC */
		GS_DCS_BUF_ADD_CMD(dev, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01);
		if (hs_clk_mbps == MIPI_DSI_FREQ_MBPS_DEFAULT)
			GS_DCS_BUF_ADD_CMD(dev, 0xC3, 0x00, 0x06, 0x20,
						0x0C, 0xFF, 0x00, 0x06, 0x20, 0x0C,
						0xFF, 0x00, 0x05, 0xEE, 0x16, 0x06,
						0x3E, 0x11, 0x05, 0xEE, 0x16, 0x06,
						0x3E, 0x11, 0x05, 0xEE, 0x16, 0x06,
						0x3E, 0x11, 0x05, 0xEE, 0x16, 0x06,
						0x3E, 0x11, 0x05, 0xEE, 0x16, 0x06,
						0x3E, 0x11);
		else /* MIPI_DSI_FREQ_MBPS_ALTERNATIVE */
			GS_DCS_BUF_ADD_CMD(dev, 0xC3, 0x00, 0x06, 0x20,
						0x0C, 0xFF, 0x00, 0x06, 0x20, 0x0C,
						0xFF, 0x00, 0x05, 0xC8, 0x15, 0x06,
						0x61, 0x11, 0x05, 0xC8, 0x15, 0x06,
						0x61, 0x11, 0x05, 0xC8, 0x15, 0x06,
						0x61, 0x11, 0x05, 0xC8, 0x15, 0x06,
						0x61, 0x11, 0x05, 0xC8, 0x15, 0x06,
						0x61, 0x11);

		/* FFC on */
		GS_DCS_BUF_ADD_CMD(dev, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x01);
		GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xC3, 0xDD);

		ctx->ffc_en = true;
	}

	PANEL_ATRACE_END(__func__);
}

static int dcsdb_enable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct dcsdb_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	const bool needs_reset = !gs_is_panel_enabled(ctx);

	if (!pmode) {
		dev_err(dev, "no current mode set\n");
		return -EINVAL;
	}

	dev_dbg(dev, "enable dcsdb panel\n");

	if (needs_reset) {
		/* toggle reset gpio */
		gs_panel_reset_helper(ctx);

		/* initial command */
		gs_panel_send_cmdset(ctx, &dcsdb_init_cmdset);

		if (ctx->force_ffc_off)
			dcsdb_pre_update_ffc(ctx);
		else {
			if (ctx->panel_rev_id.id < PANEL_REVID_EVT1)
				ctx->ffc_en = true;
			else
				dcsdb_update_ffc(ctx, MIPI_DSI_FREQ_MBPS_DEFAULT);
		}
	}

	if (spanel->in_aod && !pmode->gs_mode.is_lp_mode)
		dcsdb_set_nolp_mode(ctx, pmode);
	else
		dcsdb_mode_set(ctx, pmode);

	/* dimming frame */
	dcsdb_dimming_frame_setting(ctx, DCSDB_DIMMING_FRAME);
	ctx->timestamps.idle_exit_dimming_delay_ts = 0;

	if (pmode->gs_mode.is_lp_mode)
		dcsdb_set_lp_mode(ctx, pmode);

	GS_DCS_WRITE_CMD(dev, MIPI_DCS_SET_DISPLAY_ON);

	return 0;
}

static int dcsdb_disable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct dcsdb_panel *spanel = to_spanel(ctx);

	/* skip disable sequence if going through modeset */
	if (ctx->panel_state == GPANEL_STATE_MODESET)
		return 0;

	ctx->ffc_en = false;
	spanel->is_hbm2_enabled = false;
	spanel->in_aod = false;

	return gs_panel_disable(panel);
}

static const struct { int in; int out; } DCSDB_PROTO_DBV_MAPPING[] = {
	// in is EVT dbv, out is the corresponding proto dbv that maps to same nits.
	// NBM Range
	{1, 1},
	{2, 2},
	{3, 3},
	{4, 4},
	{5, 5},
	{6, 6},
	{7, 7},
	{8, 8},
	{9, 9},
	{10, 10},
	{12, 12},
	{13, 14},
	{15, 16},
	{17, 18},
	{19, 20},
	{22, 23},
	{24, 25},
	{28, 29},
	{30, 31},
	{35, 36},
	{38, 40},
	{44, 46},
	{48, 50},
	{55, 57},
	{60, 62},
	{68, 71},
	{75, 78},
	{84, 87},
	{93, 97},
	{104, 108},
	{115, 120},
	{129, 134},
	{143, 149},
	{145, 151},
	{176, 183},
	{181, 188},
	{216, 225},
	{226, 235},
	{266, 277},
	{281, 292},
	{327, 340},
	{349, 363},
	{403, 419},
	{433, 450},
	{436, 453},
	{530, 551},
	{544, 566},
	{650, 676},
	{677, 704},
	{713, 741},
	{832, 865},
	{886, 921},
	{1024, 1065},
	{1100, 1144},
	{1262, 1312},
	{1365, 1419},
	{1558, 1620},
	{1587, 1650},
	{1914, 1990},
	{1981, 2059},
	{2049, 2130},
	{2134, 2219},
	{2204, 2291},
	{2289, 2380},
	{2361, 2455},
	{2448, 2545},
	{2523, 2623},
	{2613, 2716},
	{2655, 2760},
	{2785, 2895},
	{2837, 2949},
	{2972, 3090},
	{3039, 3159},
	{3186, 3312},
	{3195, 3322},
	{3437, 3573},
	{3501, 3640},
	{3611, 3754},
	// HBM Range
	{3612, 3755},
	{3870, 4023},
};
#define DCSDB_PROTO_DBV_MAPPING_SIZE (ARRAY_SIZE(DCSDB_PROTO_DBV_MAPPING))

static u16 dcsdb_brightness_mapping_proto(struct gs_panel *ctx, u16 br)
{
	int i;
	u16 x0, x1, y0, y1;
	u16 delta;

	if (br <= DCSDB_PROTO_DBV_MAPPING[0].in)
		return DCSDB_PROTO_DBV_MAPPING[0].out;
	if (br >= DCSDB_PROTO_DBV_MAPPING[DCSDB_PROTO_DBV_MAPPING_SIZE - 1].in)
		return DCSDB_PROTO_DBV_MAPPING[DCSDB_PROTO_DBV_MAPPING_SIZE - 1].out;

	for (i = 0; i < DCSDB_PROTO_DBV_MAPPING_SIZE - 1; i++) {
		if (br < DCSDB_PROTO_DBV_MAPPING[i+1].in)
			break;
	}
	x0 = DCSDB_PROTO_DBV_MAPPING[i].in;
	x1 = DCSDB_PROTO_DBV_MAPPING[i+1].in;
	y0 = DCSDB_PROTO_DBV_MAPPING[i].out;
	y1 = DCSDB_PROTO_DBV_MAPPING[i+1].out;

	delta = mult_frac((br - x0), (y1 - y0), (x1 - x0));

	dev_dbg(ctx->dev, "map dbv evt->proto panel:%u->%u\n", br, y0 + delta);
	return y0 + delta;
}

static int dcsdb_set_brightness(struct gs_panel *ctx, u16 br)
{
	struct device *dev = ctx->dev;

	if (ctx->current_mode->gs_mode.is_lp_mode) {
		if (gs_panel_has_func(ctx, set_binned_lp))
			ctx->desc->gs_panel_func->set_binned_lp(ctx, br);
		return 0;
	}

	if (ctx->timestamps.idle_exit_dimming_delay_ts &&
		(ktime_sub(ctx->timestamps.idle_exit_dimming_delay_ts, ktime_get()) <= 0)) {
		GS_DCS_WRITE_CMD(dev, MIPI_DCS_WRITE_CONTROL_DISPLAY,
						ctx->dimming_on ? 0x28 : 0x20);
		ctx->timestamps.idle_exit_dimming_delay_ts = 0;
	}

	/* Brightness curve mapping due to curve in HLOS is for EVT */
	if (ctx->panel_rev_id.id < PANEL_REVID_EVT1) {
		br = dcsdb_brightness_mapping_proto(ctx, br);
	}

	if (GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode) && _is_max_hbm_level(br, ctx)) {
		u16 level = _get_max_brightness_level(ctx);

		GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
									level >> 8, level & 0xff);
		dev_dbg(dev, "apply max DBV(%d) when reach hbm max with irc off\n", level);

		dcsdb_update_acd(ctx, br);
	} else {
		dcsdb_update_acd(ctx, br);
		GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
						br >> 8, br & 0xff);
	}

	return 0;
}

static void dcsdb_set_hbm_mode(struct gs_panel *ctx, enum gs_hbm_mode hbm_mode)
{
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (ctx->hbm_mode == hbm_mode)
		return;

	dcsdb_update_irc(ctx, hbm_mode, vrefresh);

	ctx->hbm_mode = hbm_mode;
	dev_info(dev, "hbm_on=%d hbm_ircoff=%d\n", GS_IS_HBM_ON(ctx->hbm_mode),
			 GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode));
}

static void dcsdb_get_panel_rev(struct gs_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	const u8 build_code = (id & 0xFF00) >> 8;
	const u8 main = (build_code & 0xE0) >> 3;
	const u8 sub = (build_code & 0x0C) >> 2;
	u8 rev = main | sub;

	if (rev == 0x4)
		rev = 0;
	else if (rev == 0x5)
		rev = 1;

	gs_panel_get_panel_rev(ctx, rev);
}

static int dcsdb_read_serial(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	char buf[DCSDB_DDIC_ID_LEN] = {0};
	int ret;

	GS_DCS_WRITE_CMD(dev, 0xFF, 0xAA, 0x55, 0xA5, 0x81);
	ret = mipi_dsi_dcs_read(dsi, 0xF2, buf, DCSDB_DDIC_ID_LEN);
	if (ret != DCSDB_DDIC_ID_LEN) {
		dev_warn(dev, "Unable to read DDIC id (%d)\n", ret);
		goto done;
	} else {
		ret = 0;
	}

	bin2hex(ctx->panel_serial_number, buf, DCSDB_DDIC_ID_LEN);
done:
	GS_DCS_WRITE_CMD(dev, 0xFF, 0xAA, 0x55, 0xA5, 0x00);
	return ret;
}

static ssize_t dcsdb_read_cie_data(struct gs_panel *ctx, char *buf, size_t buf_len)
{
	struct device *dev = ctx->dev;
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	const u8 white_cie_read_len = 12;
	const u8 color_cie_read_len = 36;
	int read_ret = -1;
	size_t buf_idx = 0;

	if (white_cie_read_len + color_cie_read_len > buf_len)
		return -EINVAL;

	PANEL_ATRACE_BEGIN(__func__);
	/* White cx,cy,z read */
	GS_DCS_WRITE_CMD(dev, 0xFF, 0xAA, 0x55, 0xA5, 0x81);
	read_ret = mipi_dsi_dcs_read(dsi, 0xAC, buf, white_cie_read_len);
	if (read_ret != white_cie_read_len)
		goto err;
	buf_idx += read_ret;

	/* RGB cx,cy,z read */
	GS_DCS_WRITE_CMD(dev, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x03);
	read_ret = mipi_dsi_dcs_read(dsi, 0xE0, buf + buf_idx, color_cie_read_len);
	if (read_ret != color_cie_read_len)
		goto err;
	buf_idx += read_ret;

	PANEL_ATRACE_END(__func__);
	dev_dbg(dev, "%s: read color CIE (%zuB)\n", __func__, buf_idx);
	return buf_idx;
err:
	PANEL_ATRACE_END(__func__);
	dev_warn(dev, "%s: Unable to read DDIC CIE data (%d)\n", __func__, read_ret);
	return -EINVAL;
}

static int dcsdb_set_next_luminance_read(struct gs_panel *ctx, bool reset)
{
	struct dcsdb_luminance_read_config *read_config = &to_spanel(ctx)->luminance_read_config;

	if (reset) {
		read_config->freq_irc_idx = 0;
		read_config->done = false;
	} else {
		read_config->freq_irc_idx++;
		if (read_config->freq_irc_idx == DCSDB_LUMINANCE_READS_PER_DBV) {
			read_config->freq_irc_idx = 0;
			read_config->done = true;
		}
	}

	dev_dbg(ctx->dev, "%s: next luminance read: DBV %d freq idx %d", __func__,
		read_config->dbv_idx, read_config->freq_irc_idx);
	return 0;
}

static ssize_t dcsdb_read_reg_with_retries(struct device *dev, u8 cmd, char *buf, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	const u8 max_retries = 20;
	const char zeroes[DCSDB_LUMINANCE_READ_LONG_LEN] = { 0 };
	ssize_t read_ret = -1;
	u8 tries = 0;

	while (tries < max_retries) {
		read_ret = mipi_dsi_dcs_read(dsi, cmd, buf, len);
		if (read_ret != (ssize_t)len) {
			dev_warn(dev, "Unable to read DDIC id (%zd) on read %d for RGB 0x%02X\n",
				 read_ret, tries, cmd);
			return -1;
		}
		usleep_range(2000, 2200);

		/* Workaround for flake */
		if (memcmp(buf, zeroes, len) != 0)
			break;
		tries++;
	}

	if (tries == max_retries) {
		dev_warn(dev, "Unable to read DDIC after %d tries for RGB 0x%02X\n", tries, cmd);
		return -1;
	}

	if (tries > 0)
		dev_info(dev, "Had to retry flaky DDIC read %d times for RGB 0x%02X\n", tries, cmd);
	return read_ret;
}

static ssize_t dcsdb_read_luminance_data_once(struct gs_panel *ctx, char *buf, size_t buf_start)
{
	struct device *dev = ctx->dev;
	struct dcsdb_luminance_read_config *read_config = &to_spanel(ctx)->luminance_read_config;

	const u8 rgb_channel_select_base = 0xB0, rgb_channel_select_max = 0xB8;
	const u8 gamma_prefix_table[] = { 0, 1, 2, 4 };
	const u8 gamma_suffix_table[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xB };

	u8 dbv_gamma_select;
	size_t buf_idx = buf_start;

	if (read_config->freq_irc_idx > DCSDB_LUMINANCE_READS_PER_DBV ||
	    read_config->dbv_idx >
		    ctx->desc->calibration_desc->color_cal[COLOR_DATA_TYPE_LUMINANCE].max_option) {
		dev_err(dev, "luminance read config invalid, attempted to read %d idx %d\n",
			read_config->dbv_idx, read_config->freq_irc_idx);
		goto err;
	}

	PANEL_ATRACE_BEGIN(__func__);
	dbv_gamma_select = (gamma_prefix_table[read_config->freq_irc_idx] << 4) |
			   (gamma_suffix_table[read_config->dbv_idx] & 0x0F);
	GS_DCS_BUF_ADD_CMD(dev, 0xF0, 0x55, 0xAA, 0x52, 0x08, 0x02);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xBF, dbv_gamma_select);

	for (u8 rgb_select = rgb_channel_select_base; rgb_select <= rgb_channel_select_max;
	     rgb_select++) {
		/* Shorter reads on 0xB2, 0xB5, 0xB8 */
		size_t read_len = rgb_select % 3 == 1 ? DCSDB_LUMINANCE_READ_SHORT_LEN :
							DCSDB_LUMINANCE_READ_LONG_LEN;

		if (dcsdb_read_reg_with_retries(dev, rgb_select, buf + buf_idx, read_len) !=
		    read_len)
			goto err;
		buf_idx += read_len;
	}

	PANEL_ATRACE_END(__func__);
	dev_info(dev, "%s: DCSDB luminance read for %zuB from 0x%02X\n", __func__,
		 buf_idx - buf_start, dbv_gamma_select);
	return buf_idx;
err:
	PANEL_ATRACE_END(__func__);
	return -EINVAL;
}

static ssize_t dcsdb_read_luminance_data(struct gs_panel *ctx, char *buf, size_t buf_len)
{
	struct dcsdb_luminance_read_config *read_config = &to_spanel(ctx)->luminance_read_config;
	ssize_t buf_filled = 0;

	if (read_config->done)
		dcsdb_set_next_luminance_read(ctx, true);

	while (!read_config->done && buf_filled < buf_len) {
		buf_filled = dcsdb_read_luminance_data_once(ctx, buf, buf_filled);
		dcsdb_set_next_luminance_read(ctx, false);
		if (buf_filled < 0) /* error */
			read_config->done = true;
	}
	return buf_filled;
}

static ssize_t dcsdb_get_color_data(struct gs_panel *ctx, char *buf, size_t buf_len)
{
	struct dcsdb_panel *spanel = to_spanel(ctx);

	if (spanel->color_read_type >= COLOR_DATA_TYPE_MAX ||
	    !ctx->desc->calibration_desc->color_cal[spanel->color_read_type].en)
		return -EOPNOTSUPP;

	if (buf_len < ctx->desc->calibration_desc->color_cal[spanel->color_read_type].data_size)
		return -EINVAL;

	if (spanel->color_read_type == COLOR_DATA_TYPE_CIE)
		return dcsdb_read_cie_data(ctx, buf, buf_len);
	else if (spanel->color_read_type == COLOR_DATA_TYPE_LUMINANCE)
		return dcsdb_read_luminance_data(ctx, buf, buf_len);
	else
		return -EINVAL;
}

static int dcsdb_set_color_data_config(struct gs_panel *ctx, enum color_data_type read_type,
				      int option)
{
	struct dcsdb_panel *spanel = to_spanel(ctx);

	spanel->color_read_type = read_type;
	if (read_type == COLOR_DATA_TYPE_LUMINANCE) {
		spanel->luminance_read_config.dbv_idx = option;
		dcsdb_set_next_luminance_read(ctx, true);
	}

	return 0;
}

static void dcsdb_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct dentry *panel_root, *csroot;
	struct dcsdb_panel *spanel;

	if (!ctx)
		return;

	panel_root = debugfs_lookup("panel", root);
	if (!panel_root)
		return;

	csroot = debugfs_lookup("cmdsets", panel_root);
	if (!csroot)
		goto panel_out;

	spanel = to_spanel(ctx);
	gs_panel_debugfs_create_cmdset(csroot, &dcsdb_init_cmdset, "init");
	debugfs_create_bool("force_dynamic_vddd_off", 0644, panel_root,
						 &spanel->force_dynamic_vddd_off);
	dput(csroot);
panel_out:
	dput(panel_root);
}

static void dcsdb_panel_init(struct gs_panel *ctx)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;
	struct device *dev = ctx->dev;

	dcsdb_dimming_frame_setting(ctx, DCSDB_DIMMING_FRAME);

	if (gs_is_vrr_mode(pmode)) {
		ctx->sw_status.idle_vrefresh = VRR_MIN_IDLE_RR_HZ;
		dcsdb_mode_set(ctx, pmode);
	}

	dcsdb_update_ffc(ctx, MIPI_DSI_FREQ_MBPS_DEFAULT);

	/* TODO(b/445666582): move to bootloader if possible */
	/* TE2: rising 0, width 45 */
	GS_DCS_BUF_ADD_CMD(dev, 0x44, 0x00, 0x00);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x35, 0x00, 0x2D);
}

static int dcsdb_panel_probe(struct mipi_dsi_device *dsi)
{
	struct dcsdb_panel *spanel;
	struct gs_panel *ctx;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	ctx = &spanel->base;
	/* FFC is disabled in bootloader */
	ctx->ffc_en = false;
	spanel->is_hbm2_enabled = false;

	return gs_dsi_panel_common_init(dsi, ctx);
}

static int dcsdb_panel_unprepare(struct drm_panel *panel)
{
	usleep_range(UNPREPARE_DELAY_MS * 1000, UNPREPARE_DELAY_MS * 1000 + 10);
	gs_panel_unprepare(panel);
	return 0;
}

static const struct drm_panel_funcs dcsdb_drm_funcs = {
	.disable = dcsdb_disable,
	.unprepare = dcsdb_panel_unprepare,
	.prepare = gs_panel_prepare,
	.enable = dcsdb_enable,
	.get_modes = gs_panel_get_modes,
	.debugfs_init = dcsdb_debugfs_init,
};

static int dcsdb_panel_config(struct gs_panel *ctx);

static const struct gs_panel_funcs dcsdb_gs_funcs = {
	.set_brightness = dcsdb_set_brightness,
	.set_lp_mode = dcsdb_set_lp_mode,
	.set_nolp_mode = dcsdb_set_nolp_mode,
	.set_binned_lp = gs_panel_set_binned_lp_helper,
	.set_hbm_mode = dcsdb_set_hbm_mode,
	.set_dimming = dcsdb_set_dimming,
	.is_mode_seamless_atomic = gs_panel_is_mode_seamless_atomic_helper,
	.mode_set = dcsdb_mode_set,
	.panel_init = dcsdb_panel_init,
	.panel_config = dcsdb_panel_config,
	.get_panel_rev = dcsdb_get_panel_rev,
	.refresh_ctrl = dcsdb_refresh_ctrl,
	.read_serial = dcsdb_read_serial,
	.pre_update_ffc = dcsdb_pre_update_ffc,
	.update_ffc = dcsdb_update_ffc,
	.get_color_data = dcsdb_get_color_data,
	.set_color_data_config = dcsdb_set_color_data_config,
	.run_common_work = dcsdb_common_work,
	.detect_fault = gs_panel_detect_fault_helper,
	.trace_panel_settings_lite = gs_panel_trace_settings_lite_helper,
};

static struct gs_panel_brightness_desc dcsdb_brightness_desc = {
	.max_luminance = 10000000,
	.max_avg_luminance = 10000000,
	.min_luminance = 5,
};

static struct gs_panel_reg_ctrl_desc dcsdb_reg_ctrl_desc = {
	.reg_ctrl_enable = {
		{PANEL_REG_ID_VDDI, 0},
		{PANEL_REG_ID_VCI, 0},
		{PANEL_REG_ID_VDDD, 11},
	},
	.reg_ctrl_disable = {
		{PANEL_REG_ID_VDDD, 10},
		{PANEL_REG_ID_VCI, 0},
		{PANEL_REG_ID_VDDI, 0},
	},
};

static struct gs_panel_calibration_desc dcsdb_calibration_desc = {
	.color_cal = {
		{
			.en = true,
			.data_size = 48,
			.min_option = 0,
			.max_option = 0,
		},
		{
			.en = true,
			.data_size = 600,
			.min_option = 0,
			.max_option = 10,
		},
	},
};

static const struct gs_panel_detect_fault_desc dcsdb_fault_desc = {
	.errfg_reg = 0x5D,
	.errfg_len = 1,
	.dsi_err_reg = 0xAB,
	.csum_mask = BIT(1),
	.te_mask = BIT(2),
	.dsi_err_mask = BIT(3),
	.detect_interval_ms = 5000,
	.panel_errors_mask = BIT(GS_PANEL_ERR_DSI_READ_FAILURE),
};

static struct gs_panel_desc gs_dcsdb = {
	.data_lane_cnt = 4,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.brightness_desc = &dcsdb_brightness_desc,
	.calibration_desc = &dcsdb_calibration_desc,
	.modes = &dcsdb_modes,
	.off_cmdset = &dcsdb_off_cmdset,
	.lp_modes = &dcsdb_lp_modes,
	.lp_cmdset = &dcsdb_lp_cmdset,
	.binned_lp = dcsdb_binned_lp,
	.num_binned_lp = ARRAY_SIZE(dcsdb_binned_lp),
	.reg_ctrl_desc = &dcsdb_reg_ctrl_desc,
	.panel_func = &dcsdb_drm_funcs,
	.gs_panel_func = &dcsdb_gs_funcs,
	.reset_timing_ms = { -1, -1, 20 },
	.refresh_on_lp = true,
	.common_work_delay_ms = 60000,
	.fault_desc = &dcsdb_fault_desc,
};

static int dcsdb_panel_config(struct gs_panel *ctx)
{
	gs_panel_model_init(ctx, PROJECT, 0);
	return gs_panel_update_brightness_desc(&dcsdb_brightness_desc, dcsdb_brt_configs,
					       ARRAY_SIZE(dcsdb_brt_configs),
					       ctx->panel_rev_bitmask);
}

static const struct of_device_id gs_panel_of_match[] = {
	{ .compatible = "google,gs-dcsdb", .data = &gs_dcsdb },
	{ }
};
MODULE_DEVICE_TABLE(of, gs_panel_of_match);

static struct mipi_dsi_driver gs_panel_driver = {
	.probe = dcsdb_panel_probe,
	.remove = gs_dsi_panel_common_remove,
	.driver = {
		.name = "panel-gs-dcsdb",
		.of_match_table = gs_panel_of_match,
	},
};
module_mipi_dsi_driver(gs_panel_driver);

MODULE_AUTHOR("Cindy Chung <chungch@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google dcsdb panel driver");
MODULE_LICENSE("Dual MIT/GPL");
