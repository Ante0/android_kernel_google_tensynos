// SPDX-License-Identifier: MIT

#include <drm/display/drm_dsc_helper.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/string.h>
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
	.slice_height = 197,
	.slice_count = 2,
	.simple_422 = false,
	.pic_width = 1080,
	.pic_height = 2364,
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
	.scale_increment_interval = 4898,
	.nfl_bpg_offset = 126,
	.slice_bpg_offset = 133,
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

static const u16 WIDTH_MM = 67, HEIGHT_MM = 147;
static const u16 HDISPLAY = 1080, VDISPLAY = 2364;
static const u16 HFP = 44, HSA = 16, HBP = 20;
static const u16 VFP = 10, VSA = 6, VBP = 10;

#define RGEB_DSC {\
	.enabled = true,\
	.dsc_count = 2,\
	.cfg = &pps_config,\
}

static const struct gs_panel_mode_array rgeb_modes = GS_PANEL_MODES(
	{
		.mode = {
			.name = "1080x2364x60@60",
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
			.te_usec = 8623,
			.bpc = 8,
			.dsc = RGEB_DSC,
		},
	},
	{
		.mode = {
			.name = "1080x2364x120@120",
			DRM_MODE_TIMING(120, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 278,
			.bpc = 8,
			.dsc = RGEB_DSC,
		},
	},
	/* VRR modes */
	{
		.mode = {
			.name = "1080x2364x120@120",
			DRM_VRR_MODE_TIMING(120, 120, HDISPLAY, HFP, HSA, HBP,
						VDISPLAY, VFP, VSA, VBP),
			.type = DRM_MODE_TYPE_PREFERRED,
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 278,
			.bpc = 8,
			.dsc = RGEB_DSC,
		},
	},
);

static const struct gs_panel_mode_array rgeb_lp_modes = GS_PANEL_MODES(
	{
		.mode = {
			.name = "1080x2364x30@30",
			DRM_MODE_TIMING(30, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = 1109,
			.bpc = 8,
			.dsc = RGEB_DSC,
			.is_lp_mode = true,
		},
	},
);

static const struct gs_brightness_configuration rgeb_brt_configs[] = {
	{
		.panel_rev = PANEL_REV_GE((u32)PANEL_REV_EVT1),
		.default_brightness = 1209, /* dbv_for_140_nits */
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 1,
					.max = 1450,
				},
				.level = {
					.min = 128,
					.max = 3499,
				},
				.percentage = {
					.min = 0,
					.max = 71,
				},
			},
			.hbm = {
				.nits = {
					.min = 1450,
					.max = 2050,
				},
				.level = {
					.min = 3500,
					.max = 4095,
				},
				.percentage = {
					.min = 71,
					.max = 100,
				},
			},
		},
	},
	{
		.panel_rev = PANEL_REV_LT(PANEL_REV_EVT1),
		.default_brightness = 1267, /* dbv_for_140_nits */
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 2,
					.max = 1250,
				},
				.level = {
					.min = 184,
					.max = 3427,
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
					.min = 3428,
					.max = 4095,
				},
				.percentage = {
					.min = 67,
					.max = 100,
				},
			},
		},
	},
};

static struct gs_panel_brightness_desc rgeb_brightness_desc = {
	.max_luminance = 10000000,
	.max_avg_luminance = 10000000,
	.min_luminance = 5,
};

#define RGEB_CROSSTALK_CHECKSUM_LEN 4
#define RGEB_CROSSTALK_CHECKSUM_REG 0x63

#define RGEB_WRCTRLD_DIMMING_BIT 0x08
#define RGEB_WRCTRLD_BCTRL_BIT 0x20

#define MIPI_DSI_FREQ_DEFAULT 865
#define MIPI_DSI_FREQ_ALTERNATIVE 756

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

static const struct gs_dsi_cmd rgeb_off_cmds[] = {
	GS_DSI_CMD(MIPI_DCS_SET_DISPLAY_OFF),
	GS_DSI_DELAY_CMD(120, MIPI_DCS_ENTER_SLEEP_MODE),
};
static DEFINE_GS_CMDSET(rgeb_off);

static const struct gs_dsi_cmd rgeb_lp_cmds[] = {
	/* Crosstalk disable */
	GS_DSI_CMDLIST(test_key_enable),
	GS_DSI_CMD(0xF1, 0x5A, 0x5A),
	GS_DSI_CMD(0xD0, 0x08),
	GS_DSI_CMD(0x91, 0x01),
	GS_DSI_CMD(0xB0, 0x00, 0x48, 0x8F),
	GS_DSI_CMD(0x8F, 0x63),
	GS_DSI_CMD(0xF1, 0xA5, 0xA5),

	/* AOD Power Setting */
	GS_DSI_CMD(0xB0, 0x00, 0x04, 0xF6),
	GS_DSI_CMD(0xF6, 0x30), /* Default */
	GS_DSI_CMDLIST(test_key_disable),

	/* AOD Mode On Setting */
	GS_DSI_CMD(MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24),
};
static DEFINE_GS_CMDSET(rgeb_lp);

static const struct gs_dsi_cmd rgeb_lp_night_cmd[] = {
	/* 2 nits */
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0x51, 0x00, 0xB8),
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1), 0x51, 0x00, 0xB0),
};

static const struct gs_dsi_cmd rgeb_lp_low_cmd[] = {
	/* 10 nits */
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0x51, 0x01, 0x7E),
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1), 0x51, 0x01, 0x6D),
};

static const struct gs_dsi_cmd rgeb_lp_high_cmd[] = {
	/* 50 nits */
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0x51, 0x03, 0x1A),
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1), 0x51, 0x02, 0xF5),
};

static const struct gs_binned_lp rgeb_binned_lp[] = {
	/* night threshold 4 nits */
	BINNED_LP_MODE_TIMING("night", 240, rgeb_lp_night_cmd, 12, 12 + 50),
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 685, rgeb_lp_low_cmd, 12, 12 + 50),
	BINNED_LP_MODE_TIMING("high", 4095, rgeb_lp_high_cmd, 12, 12 + 50),
};

static const struct gs_dsi_cmd rgeb_init_cmds[] = {
	/* TE on */
	GS_DSI_CMD(MIPI_DCS_SET_TEAR_ON, 0x00),

	/* TE width setting (MTP'ed) */
	/* TE2 width setting (MTP'ed) */

	/* CASET: 1080 */
	GS_DSI_CMD(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37),

	/* PASET: 2364 */
	GS_DSI_CMD(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x3B),

	/* FFC On (865Mpbs) Setting */
	GS_DSI_CMDLIST(test_key_enable),
	GS_DSI_CMDLIST(test_key_fc_enable),
	GS_DSI_CMD(0xB0, 0x00, 0x3E, 0xC5),
	GS_DSI_CMD(0xC5, 0x59, 0x02),
	GS_DSI_CMD(0xB0, 0x00, 0x36, 0xC5),
	GS_DSI_CMD(0xC5, 0x11, 0x10, 0x50, 0x05),

	/* VDDD LDO Settings (since EVT1.0) */
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1), 0xB0, 0x00, 0x58, 0xD7),
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1), 0xD7, 0x06),
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1), 0xB0, 0x00, 0x5B, 0xD7),
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1), 0xD7, 0x06),
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1), 0xFE, 0x80),
	GS_DSI_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1), 0xFE, 0x00),
	GS_DSI_CMDLIST(test_key_fc_disable),

	/* NBM EM cycle settings (proto only) */
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xB0, 0x00, 0x01, 0xBD),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xBD, 0x81),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xB0, 0x00, 0x2E, 0xBD),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xBD, 0x00, 0x02),
	GS_DSI_REV_CMDLIST(PANEL_REV_LT(PANEL_REV_EVT1), panel_update),
	GS_DSI_CMDLIST(test_key_disable),
};
static DEFINE_GS_CMDSET(rgeb_init);

static const struct gs_dsi_cmd rgeb_crosstalk_enable_cmds[] = {
	GS_DSI_CMDLIST(test_key_enable),
	GS_DSI_CMD(0xF1, 0x5A, 0x5A),
	GS_DSI_CMD(0xD0, 0x08),
	GS_DSI_CMD(0x91, 0x02),
	GS_DSI_CMD(0xB0, 0x00, 0x48, 0x8F),
	GS_DSI_CMD(0x8F, 0x63),
	GS_DSI_CMDLIST(test_key_disable),
	GS_DSI_CMD(0xF1, 0xA5, 0xA5),
};
static DEFINE_GS_CMDSET(rgeb_crosstalk_enable);

static const struct gs_dsi_cmd rgeb_crosstalk_flash_program_cmds[] = {
	/* crosstalk flash program settings */
	GS_DSI_CMDLIST(test_key_enable),
	GS_DSI_CMD(0xF1, 0xF1, 0xA2),
	GS_DSI_CMD(0xC0, 0x02),
	GS_DSI_DELAY_CMD(1, 0x74, 0x03, 0x00, 0x00),
	GS_DSI_CMD(0xC1, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	GS_DSI_CMD(1, 0xC0, 0x03),
	GS_DSI_CMD(0xC1, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00),
	GS_DSI_CMD(10, 0xC0, 0x03),
	GS_DSI_CMD(0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x03, 0xCA, 0x02, 0x03, 0x77, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x05, 0x00, 0x00, 0x00, 0x01, 0x09,
		   0x3C, 0x01, 0x0A, 0xAA, 0x03, 0x31,
		   0x04, 0x00, 0x00, 0x00, 0xFF, 0x25,
		   0xAA, 0x03, 0xBB, 0x03, 0x00, 0x00,
		   0x00, 0xFF, 0x00, 0xAA, 0x03, 0x5D,
		   0x04, 0x00, 0x00, 0x00, 0xFF, 0x15,
		   0xAA, 0x03, 0x01, 0x04, 0x00, 0x00,
		   0x00, 0xFF, 0x0A, 0xAA, 0x03, 0x31,
		   0x04, 0x00, 0x00, 0x00, 0xFF, 0x25,
		   0xAA, 0x03, 0xBB, 0x03, 0x00, 0x00,
		   0x00, 0xFF, 0x00, 0xAA, 0x03, 0x5D,
		   0x04, 0x00, 0x00, 0x00, 0xFF, 0x15,
		   0xAA, 0x03, 0x01, 0x04, 0x00, 0x00,
		   0x00, 0xFF, 0x0A, 0xAA, 0x03, 0x31,
		   0x04, 0x00, 0x00, 0x00, 0xFF, 0x25,
		   0xAA, 0x03, 0xBB, 0x03, 0x00, 0x00,
		   0x00, 0xFF, 0x00, 0xAA, 0x03, 0x5D,
		   0x04, 0x00, 0x00, 0x00, 0xFF, 0x15,
		   0xAA, 0x03, 0x01, 0x04, 0x00, 0x00,
		   0x00, 0xFF, 0x0A, 0xAA, 0x03, 0x31,
		   0x04, 0x00, 0x00, 0x00, 0xFF, 0x25,
		   0xAA, 0x03, 0xBB, 0x03, 0x00, 0x00,
		   0x00, 0xFF, 0x00, 0xAA, 0x03, 0x5D,
		   0x04, 0x00, 0x00, 0x00, 0xFF, 0x15,
		   0xAA, 0x03, 0x01, 0x04, 0x00, 0x00,
		   0x00, 0xFF, 0x8C, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x2E,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x8C, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x2E,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x8C, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x2E,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x8C, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x2E,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
		   0x00, 0x00, 0x00, 0x00),
	GS_DSI_CMD(0xC1, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	GS_DSI_DELAY_CMD(1, 0xC0, 0x03),
	GS_DSI_CMD(0xC1, 0x32, 0x06, 0x30, 0x00, 0x02, 0x00, 0x00),
	GS_DSI_DELAY_CMD(4, 0xC0, 0x03),
	GS_DSI_CMD(0x6C, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00,
		   0x00, 0x0A, 0x00, 0x00, 0x0A, 0x00,
		   0x00, 0x0A, 0x00, 0x00, 0x01, 0x01,
		   0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x3F, 0x0E, 0x05, 0x05,
		   0x09, 0x09, 0x09, 0x0E, 0x09, 0x09,
		   0x09, 0x09, 0x09, 0x0E, 0x0E, 0x0E,
		   0x09, 0x0E, 0x09, 0x05, 0x12, 0x05,
		   0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x12,
		   0x0E, 0x0E, 0x09, 0x09, 0x09, 0x09,
		   0x09, 0x05, 0x05, 0x05, 0x05, 0x00,
		   0x3F, 0x0E, 0x05, 0x05, 0x09, 0x09,
		   0x09, 0x0E, 0x09, 0x09, 0x09, 0x09,
		   0x09, 0x0E, 0x0E, 0x0E, 0x09, 0x0E,
		   0x09, 0x05, 0x12, 0x05, 0x0E, 0x0E,
		   0x0E, 0x0E, 0x0E, 0x12, 0x0E, 0x0E,
		   0x09, 0x09, 0x09, 0x09, 0x09, 0x05,
		   0x05, 0x05, 0x05, 0x00, 0x3F, 0x0E,
		   0x05, 0x05, 0x09, 0x09, 0x09, 0x0E,
		   0x09, 0x09, 0x09, 0x09, 0x09, 0x0E,
		   0x0E, 0x0E, 0x09, 0x0E, 0x09, 0x05,
		   0x12, 0x05, 0x0E, 0x0E, 0x0E, 0x0E,
		   0x0E, 0x12, 0x0E, 0x0E, 0x09, 0x09,
		   0x09, 0x09, 0x09, 0x05, 0x05, 0x05,
		   0x05, 0x00, 0x3F, 0x0E, 0x05, 0x05,
		   0x09, 0x09, 0x09, 0x0E, 0x09, 0x09,
		   0x09, 0x09, 0x09, 0x0E, 0x0E, 0x0E,
		   0x09, 0x0E, 0x09, 0x05, 0x12, 0x05,
		   0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x12,
		   0x0E, 0x0E, 0x09, 0x09, 0x09, 0x09,
		   0x09, 0x05, 0x05, 0x05, 0x05, 0x00,
		   0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
		   0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
		   0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x80, 0x80, 0x80, 0x80,
		   0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		   0x80, 0x80, 0x80, 0x80),
	GS_DSI_CMD(0xC1, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	GS_DSI_DELAY_CMD(1, 0xC0, 0x03),
	GS_DSI_CMD(0xC1, 0x32, 0x06, 0x31, 0x00, 0x02, 0x00, 0x00),
	GS_DSI_DELAY_CMD(4, 0xC0, 0x03),
	GS_DSI_CMD(0x6C, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x55, 0x76, 0xB2, 0xD1,
		   0xFF, 0x03, 0x03, 0x03, 0xFF, 0xFF,
		   0xFF, 0x50, 0x32, 0x32, 0xFF, 0xFF,
		   0xFF, 0x50, 0x32, 0x32, 0xFF, 0xFF,
		   0xFF, 0x50, 0x32, 0x32, 0x02, 0x00,
		   0x02, 0x00, 0x02, 0xBB, 0x02, 0x3E,
		   0x02, 0x00, 0x02, 0x00, 0x02, 0x00,
		   0x02, 0xBB, 0x02, 0x3E, 0x02, 0x00,
		   0x02, 0x00, 0x02, 0x00, 0x02, 0xBB,
		   0x02, 0x3E, 0x02, 0x00, 0xFF, 0xFF,
		   0xFF, 0x3C, 0x50, 0x50, 0xFF, 0xFF,
		   0xFF, 0x3C, 0x50, 0x50, 0xFF, 0xFF,
		   0xFF, 0x3C, 0x50, 0x50, 0x02, 0x00,
		   0x02, 0x00, 0x02, 0xD0, 0x00, 0x29,
		   0x02, 0x00, 0x02, 0x00, 0x02, 0x00,
		   0x02, 0xD0, 0x00, 0x29, 0x02, 0x00,
		   0x02, 0x00, 0x02, 0x00, 0x02, 0xD0,
		   0x00, 0x29, 0x02, 0x00, 0xFF, 0xFF,
		   0xFF, 0xFF, 0x78, 0x14, 0xFF, 0xFF,
		   0xFF, 0xFF, 0x78, 0x14, 0xFF, 0xFF,
		   0xFF, 0xFF, 0x78, 0x14, 0x02, 0x00,
		   0x02, 0x00, 0x02, 0x00, 0x03, 0x17,
		   0x02, 0x8B, 0x02, 0x00, 0x02, 0x00,
		   0x02, 0x00, 0x03, 0x17, 0x02, 0x8B,
		   0x02, 0x00, 0x02, 0x00, 0x02, 0x00,
		   0x03, 0x17, 0x02, 0x8B, 0xFF, 0xFF,
		   0xFF, 0xFF, 0x78, 0x00, 0xFF, 0xFF,
		   0xFF, 0xFF, 0x78, 0x00, 0xFF, 0xFF,
		   0xFF, 0xFF, 0x78, 0x00, 0x02, 0x00,
		   0x02, 0x00, 0x02, 0x00, 0x03, 0x17,
		   0x02, 0xA7, 0x02, 0x00, 0x02, 0x00,
		   0x02, 0x00, 0x03, 0x17, 0x02, 0xA7,
		   0x02, 0x00, 0x02, 0x00, 0x02, 0x00,
		   0x03, 0x17, 0x02, 0xA7, 0x5F, 0x5F,
		   0x5F, 0x80, 0x5A, 0x3C, 0x5F, 0x5F,
		   0x5F, 0x80, 0x5A, 0x3C, 0x5F, 0x5F,
		   0x5F, 0x80, 0x5A, 0x3C, 0x02, 0x00,
		   0x02, 0x00, 0x00, 0x23, 0x02, 0x4E,
		   0x02, 0x2A, 0x02, 0x00, 0x02, 0x00,
		   0x00, 0x23, 0x02, 0x4E, 0x02, 0x2A,
		   0x02, 0x00, 0x02, 0x00),
	GS_DSI_CMD(0xC1, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	GS_DSI_DELAY_CMD(1, 0xC0, 0x03),
	GS_DSI_CMD(0xC1, 0x32, 0x06, 0x32, 0x00, 0x02, 0x00, 0x00),
	GS_DSI_DELAY_CMD(4, 0xC0, 0x03),
	GS_DSI_CMD(0x6C, 0x00, 0x23, 0x02, 0x4E, 0x02, 0x2A,
		   0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		   0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		   0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		   0x02, 0x00, 0x02, 0x00, 0x02, 0x00,
		   0x02, 0x00, 0x02, 0x00, 0x02, 0x00,
		   0x02, 0x00, 0x02, 0x00, 0x02, 0x00,
		   0x02, 0x00, 0x02, 0x00, 0x02, 0x00,
		   0x02, 0x00, 0x02, 0x00, 0x02, 0x00,
		   0x00, 0x75, 0x00, 0xB1, 0x00, 0xC8,
		   0x01, 0x18, 0x01, 0x4D, 0x01, 0x92,
		   0x01, 0xD2, 0x01, 0xE4, 0x01, 0xFC,
		   0x8C, 0x78, 0x41, 0x41, 0x32, 0x28,
		   0x28, 0x28, 0x28, 0x8C, 0x78, 0x41,
		   0x41, 0x32, 0x28, 0x28, 0x28, 0x28,
		   0x8C, 0x78, 0x41, 0x41, 0x32, 0x28,
		   0x28, 0x28, 0x28, 0x87, 0x87, 0x25,
		   0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
		   0x87, 0x87, 0x25, 0x1E, 0x1E, 0x1E,
		   0x1E, 0x1E, 0x1E, 0x87, 0x87, 0x25,
		   0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
		   0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		   0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		   0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		   0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		   0x80, 0x80, 0x80, 0x00, 0x75, 0x00,
		   0xB1, 0x00, 0xC8, 0x01, 0x18, 0x01,
		   0x4D, 0x01, 0x92, 0x01, 0xD2, 0x01,
		   0xE4, 0x01, 0xFC, 0x1E, 0x1E, 0x1E,
		   0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
		   0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
		   0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
		   0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
		   0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
		   0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
		   0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
		   0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
		   0x1E, 0x1E, 0x1E, 0x80, 0x80, 0x80,
		   0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		   0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		   0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		   0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
		   0xFF, 0xFF, 0xFF, 0xFF),
	GS_DSI_CMD(0xC1, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	GS_DSI_DELAY_CMD(1, 0xC0, 0x03),
	GS_DSI_CMD(0xC1, 0x32, 0x06, 0x33, 0x00, 0x02, 0x00, 0x00),
	GS_DSI_DELAY_CMD(4, 0xC0, 0x03),
	GS_DSI_CMD(0x6C, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
		   0x00, 0x00, 0xDB, 0xBB),
	GS_DSI_CMD(0xC1, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	GS_DSI_DELAY_CMD(1, 0xC0, 0x03),
	GS_DSI_CMD(0xC1, 0x32, 0x06, 0x34, 0x00, 0x02, 0x00, 0x00),
	GS_DSI_DELAY_CMD(4, 0xC0, 0x03),
	GS_DSI_CMD(0xC0, 0x00),
	GS_DSI_CMDLIST(test_key_disable),
	GS_DSI_CMD(0xF1, 0xA5, 0xA5),
};
static DEFINE_GS_CMDSET(rgeb_crosstalk_flash_program);

/**
 * struct rgeb_panel - panel specific runtime info
 *
 * This struct maintains rgeb panel specific runtime info, any fixed details about panel
 * should most likely go into struct gs_panel_desc
 */
struct rgeb_panel {
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
	/**
	 * @is_crosstalk_latest: whether crosstalk compensation in DDIC flash is updated
	 *			 to the latest.
	 */
	bool is_crosstalk_latest;
};
#define to_spanel(ctx) container_of(ctx, struct rgeb_panel, base)

static void rgeb_change_frequency(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	u32 vrefresh = gs_is_vrr_mode(pmode) ? ctx->sw_status.idle_vrefresh :
					       drm_mode_vrefresh(&pmode->mode);

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMD(dev, 0x83, vrefresh == 60 ? 0x08 : 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);

	notify_panel_te2_freq_changed(ctx, 0);

	dev_info(dev, "%s: change to %uHz\n", __func__, vrefresh);
}

static void rgeb_mode_set(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
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

	rgeb_change_frequency(ctx, pmode);

	dev_dbg(ctx->dev, "%s: change to %dhz\n", __func__, drm_mode_vrefresh(&pmode->mode));
}

static void rgeb_refresh_ctrl(struct gs_panel *ctx)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	PANEL_ATRACE_BEGIN(__func__);

	if (gs_panel_refresh_ctrl_lite_helper(ctx, pmode, VRR_MIN_IDLE_RR_HZ))
		rgeb_change_frequency(ctx, pmode);

	PANEL_ATRACE_END(__func__);
}

static void rgeb_update_wrctrld(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	u8 val = RGEB_WRCTRLD_BCTRL_BIT;

	if (ctx->dimming_on)
		val |= RGEB_WRCTRLD_DIMMING_BIT;

	dev_dbg(dev, "%s(wrctrld:%#x, hbm: %d, dimming: %d)\n", __func__, val,
		GS_IS_HBM_ON(ctx->hbm_mode), ctx->dimming_on);

	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);
}

static int rgeb_set_brightness(struct gs_panel *ctx, u16 br)
{
	u16 brightness;
	u32 max_brightness;
	struct rgeb_panel *spanel = to_spanel(ctx);
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
			dev_dbg(dev, "%s: pixel off instead of dbv 0\n", __func__);
		}
		return 0;
	}

	if (spanel->is_pixel_off) {
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
		dev_warn(dev, "%s: capped to dbv(%d)\n", __func__, max_brightness);
	}

	/* swap endianness because panel expects MSB first */
	brightness = swab16(br);

	return gs_dcs_set_brightness(ctx, brightness);
}

static void rgeb_set_hbm_mode(struct gs_panel *ctx, enum gs_hbm_mode mode)
{
	struct device *dev = ctx->dev;

	ctx->hbm_mode = mode;

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);

	/* FGZ mode setting */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x61, 0x68);
	if (GS_IS_HBM_ON(ctx->hbm_mode) && GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode)) {
		/* FGZ Mode ON */
		if (ctx->panel_rev_id.id < PANEL_REVID_EVT1)
			GS_DCS_BUF_ADD_CMD(dev, 0x68, 0xB4, 0x2C, 0x6A, 0x80, 0x00, 0x1F, 0x08,
					   0xD3);
		else if (ctx->panel_rev_id.id < PANEL_REVID_EVT1_1)
			GS_DCS_BUF_ADD_CMD(dev, 0x68, 0xBC, 0x2C, 0x6A, 0x80, 0x00, 0x00, 0x6B,
					   0x22);
		else if (ctx->panel_rev_id.id < PANEL_REVID_EVT1_1_1)
			GS_DCS_BUF_ADD_CMD(dev, 0x68, 0xB4, 0x2C, 0x6A, 0x80, 0x00, 0x00, 0x20,
					   0xE6);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0x68, 0xBC, 0x2C, 0x6A, 0x80, 0x00, 0x00, 0x5E,
					   0x18);
	} else {
		/* FGZ Mode OFF */
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0xB0, 0x2C, 0x6A, 0x80, 0x00, 0x00, 0x00, 0x00);
	}

	/* EM cycle settings (proto only) */
	if (ctx->panel_rev_id.id < PANEL_REVID_EVT1) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, ctx->hbm_mode ? 0x80 : 0x81);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x2E, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, ctx->hbm_mode ? 0x01 : 0x02);
		GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	}

	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);

	dev_info(dev, "hbm_on=%d hbm_ircoff=%d.\n", GS_IS_HBM_ON(ctx->hbm_mode),
		 GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode));
}

static void rgeb_set_dimming(struct gs_panel *ctx, bool dimming_on)
{
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;

	ctx->dimming_on = dimming_on;

	if (pmode->gs_mode.is_lp_mode) {
		dev_warn(dev, "in lp mode, skip to update dimming usage\n");
		return;
	}

	rgeb_update_wrctrld(ctx);
}

static bool rgeb_is_mode_seamless_atomic(const struct gs_panel *ctx,
					 const struct gs_panel_mode *old_pmode,
					 const struct gs_panel_mode *new_pmode)
{
	/* seamless mode switch is possible if only changing refresh rate */
	return drm_mode_equal_no_clocks(&old_pmode->mode, &new_pmode->mode);
}

static void rgeb_crosstalk_version_check(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct rgeb_panel *spanel = to_spanel(ctx);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	static const u8 rgeb_crosstalk_latest_checksum[] = {0xDB, 0xBB, 0xDB, 0xBB};
	u8 buf[RGEB_CROSSTALK_CHECKSUM_LEN] = { 0 };
	ssize_t read_ret = -1;

	if (spanel->is_crosstalk_latest)
		return;

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xB0, 0x02, 0x0E, 0x63);
	read_ret = mipi_dsi_dcs_read(dsi, RGEB_CROSSTALK_CHECKSUM_REG, buf,
				     RGEB_CROSSTALK_CHECKSUM_LEN);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);

	if (read_ret != RGEB_CROSSTALK_CHECKSUM_LEN) {
		dev_warn(dev, "Read %zd instead of %d from crosstalk checksum\n",
			 read_ret, RGEB_CROSSTALK_CHECKSUM_LEN);
		return;
	}

	if (!memcmp(buf, rgeb_crosstalk_latest_checksum, RGEB_CROSSTALK_CHECKSUM_LEN)) {
		spanel->is_crosstalk_latest = true;
		dev_dbg(dev, "Crosstalk is latest\n");
	}
}

static void rgeb_crosstalk_flash_update(struct gs_panel *ctx)
{
	gs_panel_send_cmdset(ctx, &rgeb_crosstalk_flash_program_cmdset);

	dev_dbg(ctx->dev, "Crosstalk is updated. Requires a panel reset.\n");
}

static void rgeb_debugfs_init(struct drm_panel *panel, struct dentry *root)
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

	gs_panel_debugfs_create_cmdset(csroot, &rgeb_init_cmdset, "init");

	dput(csroot);
panel_out:
	dput(panel_root);
}

static void rgeb_panel_init(struct gs_panel *ctx)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	if (gs_is_vrr_mode(pmode)) {
		ctx->sw_status.idle_vrefresh = VRR_MIN_IDLE_RR_HZ;
		rgeb_mode_set(ctx, pmode);
	}
}

static void rgeb_get_panel_rev(struct gs_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	u8 build_code = (id & 0xFF00) >> 8;
	u8 main = (build_code & 0xE0) >> 3;
	u8 sub = (build_code & 0x0C) >> 2;
	u8 var = build_code & 0x03;
	u8 rev = main | sub;

	// DVT: treat all sub-build as DVT1.0
	if (main == 0xC) {
		dev_dbg(ctx->dev, "ignore DVT sub-builds\n");
		sub = 0;
		rev = main;
	}

	gs_panel_get_panel_rev(ctx, rev);

	if (var) {
		ctx->panel_rev_id.id |= var;
		dev_info(ctx->dev, "panel_rev variant: 0x%x\n", var);
	}
}

static void rgeb_set_nolp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	if (!gs_is_panel_active(ctx))
		return;

	/* AOD Mode Off Setting */
	gs_panel_send_cmdset(ctx, &rgeb_crosstalk_enable_cmdset);
	rgeb_update_wrctrld(ctx);
	rgeb_mode_set(ctx, pmode);

	dev_info(ctx->dev, "exit LP mode\n");
}

static void rgeb_reset_helper(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	gs_panel_reset_helper(ctx);

	/* b/483580078: crosstalk enable before sleep out */
	gs_panel_send_cmdset(ctx, &rgeb_crosstalk_enable_cmdset);

	GS_DCS_WRITE_DELAY_CMD(dev, 120, MIPI_DCS_EXIT_SLEEP_MODE);

	gs_panel_send_cmdset(ctx, &rgeb_init_cmdset);

	ctx->ffc_en = true;

	rgeb_crosstalk_version_check(ctx);
}

static int rgeb_enable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct rgeb_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	const bool needs_reset = !gs_is_panel_enabled(ctx);

	if (!pmode) {
		dev_err(dev, "no current mode set\n");
		return -EINVAL;
	}

	PANEL_ATRACE_BEGIN(__func__);

	if (needs_reset) {
		rgeb_reset_helper(ctx);

		/* b/483580078: crosstalk flash program settings */
		if (!spanel->is_crosstalk_latest) {
			rgeb_crosstalk_flash_update(ctx);
			rgeb_reset_helper(ctx);
		}
	}

	/* frequency */
	rgeb_mode_set(ctx, pmode);

	/* DSC related configuration */
	mipi_dsi_compression_mode(to_mipi_dsi_device(dev), true);
	gs_dcs_write_dsc_config(dev, &pps_config);

	/* dimming and HBM */
	rgeb_update_wrctrld(ctx);

	/* display on */
	if (pmode->gs_mode.is_lp_mode)
		gs_panel_set_lp_mode_helper(ctx, pmode);

	GS_DCS_WRITE_CMD(dev, MIPI_DCS_SET_DISPLAY_ON);

	ctx->dsi_hs_clk_mbps = MIPI_DSI_FREQ_DEFAULT;

	PANEL_ATRACE_END(__func__);

	return 0;
}

static int rgeb_disable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);

	/* skip disable sequence if going through modeset */
	if (ctx->panel_state == GPANEL_STATE_MODESET)
		return 0;

	ctx->ffc_en = false;

	return gs_panel_disable(panel);
}

static int rgeb_panel_probe(struct mipi_dsi_device *dsi)
{
	struct rgeb_panel *spanel;
	struct gs_panel *ctx;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	ctx = &spanel->base;
	spanel->is_pixel_off = false;

	/* FFC is enabled in bootloader */
	ctx->ffc_en = true;

	return gs_dsi_panel_common_init(dsi, ctx);
}

static int rgeb_panel_config(struct gs_panel *ctx)
{
	/* Revision specific brightness description */
	return gs_panel_update_brightness_desc(&rgeb_brightness_desc, rgeb_brt_configs,
					       ARRAY_SIZE(rgeb_brt_configs),
					       ctx->panel_rev_bitmask);
}

static void rgeb_pre_update_ffc(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

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

static void rgeb_update_ffc(struct gs_panel *ctx, unsigned int hs_clk_mbps)
{
	struct device *dev = ctx->dev;

	dev_dbg(ctx->dev, "hs_clk_mbps: current=%u, target=%u\n",
		ctx->dsi_hs_clk_mbps, hs_clk_mbps);

	PANEL_ATRACE_BEGIN(__func__);

	if (hs_clk_mbps != MIPI_DSI_FREQ_DEFAULT && hs_clk_mbps != MIPI_DSI_FREQ_ALTERNATIVE) {
		dev_warn(ctx->dev, "invalid hs_clk_mbps=%u for FFC\n", hs_clk_mbps);
	} else if (ctx->dsi_hs_clk_mbps != hs_clk_mbps || !ctx->ffc_en) {
		dev_info(ctx->dev, "updating FFC for hs_clk_mbps=%u\n", hs_clk_mbps);
		ctx->dsi_hs_clk_mbps = hs_clk_mbps;

		/* Update FFC */
		GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
		GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_enable);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x3E, 0xC5);
		if (hs_clk_mbps == MIPI_DSI_FREQ_DEFAULT) {
			GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x59, 0x02);
		} else { /* MIPI_DSI_FREQ_ALTERNATIVE */
			GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x65, 0xD7);
		}
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x36, 0xC5);
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x11, 0x10, 0x50, 0x05);
		GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_disable);
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);

		ctx->ffc_en = true;
	}

	PANEL_ATRACE_END(__func__);
}

/* TODO: b/338409981#comment12 - The settings may change in later version */
static void rgeb_set_ssc_en(struct gs_panel *ctx, bool enabled)
{
	struct device *dev = ctx->dev;
	const bool ssc_mode_update = ctx->ssc_en != enabled;

	if (!ssc_mode_update) {
		dev_dbg(ctx->dev, "ssc_mode skip update\n");
		return;
	}

	ctx->ssc_en = enabled;
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_enable);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x6E, 0xC5); /* global para */
	if (enabled)
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x07, 0x7F, 0x00, 0x00);
	else
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x04, 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_disable);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
	dev_info(dev, "ssc_mode=%d\n", ctx->ssc_en);
}

static void rgeb_prepare_color_data_read(struct device *dev)
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

static ssize_t rgeb_read_flash_address(struct device *dev, u32 addr, size_t read_len, char *buf,
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
		dev_warn(dev, "%s: Read %zd instead of %zu from flash addr %u\n", __func__,
			 read_ret, read_len, addr);
	return read_ret;
}

static ssize_t rgeb_read_cie_data(struct gs_panel *ctx, char *buf, size_t buf_len)
{
	struct device *dev = ctx->dev;
	int read_ret = -1;
	u8 read_len = ctx->desc->calibration_desc->color_cal[COLOR_DATA_TYPE_CIE].data_size;

	if (buf_len < read_len)
		return -EINVAL;

	PANEL_ATRACE_BEGIN(__func__);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_f1_enable);
	rgeb_prepare_color_data_read(dev);
	read_ret = rgeb_read_flash_address(dev, 0x055000, read_len, buf, buf_len);

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

static const u32 lum_red_addr[] = {
	0x06B000, 0x06B202, 0x06B404, 0x06B808, 0x06BC0C, 0x06C010, 0x06CE1E, 0x06D222,
	0x06D424, 0x06D626, 0x06D828, 0x06DC2C, 0x06E030, 0x06E434, 0x06F242, 0x06F646,
};
static const u32 lum_green_addr[] = {
	0x06FE4E, 0x070050, 0x070252, 0x070656, 0x070A5A, 0x070E5E, 0x071C6C, 0x072070,
	0x072272, 0x072474, 0x072676, 0x072A7A, 0x072E7E, 0x073282, 0x074090, 0x074494,
};
static const u32 lum_blue_addr[] = {
	0x074C9C, 0x074E9E, 0x0750A0, 0x0754A4, 0x0758A8, 0x075CAC, 0x076ABA, 0x076EBE,
	0x0770C0, 0x0772C2, 0x0774C4, 0x0778C8, 0x077CCC, 0x0780D0, 0x078EDE, 0x0792E2,
};

static ssize_t rgeb_read_luminance_data(struct gs_panel *ctx, char *buf, size_t buf_len)
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
	rgeb_prepare_color_data_read(dev);
	read_ret =
		rgeb_read_flash_address(dev, lum_red_addr[option], single_read_len, buf, buf_len);
	if (read_ret != single_read_len)
		goto out;

	read_ret = rgeb_read_flash_address(dev, lum_green_addr[option], single_read_len,
					   buf + single_read_len, buf_len - single_read_len);
	if (read_ret != single_read_len)
		goto out;

	read_ret = rgeb_read_flash_address(dev, lum_blue_addr[option], single_read_len,
					   buf + (2 * single_read_len),
					   buf_len - (2 * single_read_len));
	if (read_ret != single_read_len)
		goto out;

	read_ret = total_len;
out:
	GS_DCS_BUF_ADD_CMD(dev, 0xC0, 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_f1_disable);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
	PANEL_ATRACE_END(__func__);
	return read_ret;
}

static ssize_t rgeb_get_color_data(struct gs_panel *ctx, char *buf, size_t buf_len)
{
	struct rgeb_panel *spanel = to_spanel(ctx);

	if (spanel->color_read_type >= COLOR_DATA_TYPE_MAX ||
	    !ctx->desc->calibration_desc->color_cal[spanel->color_read_type].en)
		return -EOPNOTSUPP;

	if (spanel->color_read_type == COLOR_DATA_TYPE_CIE)
		return rgeb_read_cie_data(ctx, buf, buf_len);
	else
		return rgeb_read_luminance_data(ctx, buf, buf_len);
}

static int rgeb_set_color_data_config(struct gs_panel *ctx, enum color_data_type read_type,
				      int option)
{
	struct rgeb_panel *spanel = to_spanel(ctx);

	spanel->color_read_type = read_type;
	if (read_type == COLOR_DATA_TYPE_LUMINANCE)
		spanel->luminance_read_option = option;
	return 0;
}

static const struct drm_panel_funcs rgeb_drm_funcs = {
	.disable = rgeb_disable,
	.unprepare = gs_panel_unprepare,
	.prepare = gs_panel_prepare,
	.enable = rgeb_enable,
	.get_modes = gs_panel_get_modes,
	.debugfs_init = rgeb_debugfs_init,
};

static const struct gs_panel_funcs rgeb_gs_funcs = {
	.set_brightness = rgeb_set_brightness,
	.set_lp_mode = gs_panel_set_lp_mode_helper,
	.set_nolp_mode = rgeb_set_nolp_mode,
	.set_binned_lp = gs_panel_set_binned_lp_helper,
	.set_dimming = rgeb_set_dimming,
	.set_hbm_mode = rgeb_set_hbm_mode,
	.is_mode_seamless_atomic = rgeb_is_mode_seamless_atomic,
	.mode_set = rgeb_mode_set,
	.panel_init = rgeb_panel_init,
	.panel_config = rgeb_panel_config,
	.get_panel_rev = rgeb_get_panel_rev,
	.refresh_ctrl = rgeb_refresh_ctrl,
	.read_serial = gs_panel_read_slsi_ddic_id,
	.pre_update_ffc = rgeb_pre_update_ffc,
	.update_ffc = rgeb_update_ffc,
	.set_ssc_en = rgeb_set_ssc_en,
	.get_color_data = rgeb_get_color_data,
	.set_color_data_config = rgeb_set_color_data_config,
	.detect_fault = gs_panel_detect_fault_helper,
};

const struct gs_panel_reg_ctrl_desc rgeb_reg_ctrl_desc = {
	.reg_ctrl_enable = {
		{ PANEL_REG_ID_VDDI, 0 },
		{ PANEL_REG_ID_VCI, 10 },
		{ PANEL_REG_ID_VDDD, 10 },
	},
	.reg_ctrl_post_enable = {
	},
	.reg_ctrl_pre_disable = {
		{ PANEL_REG_ID_VDDD, 0 },
	},
	.reg_ctrl_disable = {
		{ PANEL_REG_ID_VCI, 0 },
		{ PANEL_REG_ID_VDDI, 0 },
	},
};

static struct gs_panel_calibration_desc rgeb_calibration_desc = {
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

static const struct gs_panel_detect_fault_desc rgeb_fault_desc = {
	.dsi_err_reg = 0xE9,
	.dsi_err_mask = 0,
	.detect_interval_ms = 5000,
	.panel_errors_mask = BIT(GS_PANEL_ERR_DSI_READ_FAILURE),
};

const struct gs_panel_desc google_rgeb = {
	.data_lane_cnt = 4,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.brightness_desc = &rgeb_brightness_desc,
	.calibration_desc = &rgeb_calibration_desc,
	.modes = &rgeb_modes,
	.off_cmdset = &rgeb_off_cmdset,
	.lp_modes = &rgeb_lp_modes,
	.lp_cmdset = &rgeb_lp_cmdset,
	.binned_lp = rgeb_binned_lp,
	.num_binned_lp = ARRAY_SIZE(rgeb_binned_lp),
	.reg_ctrl_desc = &rgeb_reg_ctrl_desc,
	.panel_func = &rgeb_drm_funcs,
	.gs_panel_func = &rgeb_gs_funcs,
	.default_dsi_hs_clk_mbps = MIPI_DSI_FREQ_DEFAULT,
	.reset_timing_ms = { -1, 1, 5 },
	.fault_desc = &rgeb_fault_desc,
};

static const struct of_device_id gs_panel_of_match[] = {
	{ .compatible = "google,gs-rgeb", .data = &google_rgeb },
	{ }
};
MODULE_DEVICE_TABLE(of, gs_panel_of_match);

static struct mipi_dsi_driver gs_panel_driver = {
	.probe = rgeb_panel_probe,
	.remove = gs_dsi_panel_common_remove,
	.driver = {
		.name = "panel-gs-rgeb",
		.of_match_table = gs_panel_of_match,
	},
};
module_mipi_dsi_driver(gs_panel_driver);

MODULE_AUTHOR("Hung-Yeh Lee <hungyeh@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google rgeb panel driver");
MODULE_LICENSE("Dual MIT/GPL");
