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

#define iyida_WRCTRLD_DIMMING_BIT 0x08
#define iyida_WRCTRLD_BCTRL_BIT 0x20

#define MIPI_DSI_FREQ_DEFAULT 1468
#define MIPI_DSI_FREQ_ALTERNATIVE 1388

#define PROJECT "iyida"
#define IYIDA_BRIGHTNESS_20NITS 422

static const u16 WIDTH_MM = 147, HEIGHT_MM = 141;
static const u16 HDISPLAY = 2152, VDISPLAY = 2076;
static const u16 HFP = 80, HSA = 30, HBP = 38;
static const u16 VFP = 6, VSA = 4, VBP = 14;

#define iyida_DSC {\
	.enabled = true,\
	.dsc_count = 2,\
	.cfg = &pps_config,\
}

#define iyida_TE_USEC_120HZ_HS 404
#define iyida_TE_USEC_60HZ_NS  642
#define iyida_TE_USEC_AOD  1284

#define iyida_TE2_FREQ_AOD 30
#define iyida_TE2_FREQ_NORMAL 120

static const struct gs_panel_mode_array iyida_modes = GS_PANEL_MODES(
#ifndef PANEL_FACTORY_BUILD
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
			.te_usec = iyida_TE_USEC_120HZ_HS,
			.bpc = 8,
			.dsc = iyida_DSC,
		},
	},
#endif
	{
		.mode = {
			.name = "2152x2076x120@240",
			DRM_VRR_MODE_TIMING(120, 240, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = iyida_TE_USEC_120HZ_HS,
			.bpc = 8,
			.dsc = iyida_DSC,
		},
	},
	{
		.mode = {
			.name = "2152x2076x60@60",
			DRM_VRR_MODE_TIMING(60, 60, HDISPLAY, HFP, HSA, HBP,
					VDISPLAY, VFP, VSA, VBP),
			.flags = DRM_MODE_FLAG_NS,
			.width_mm = WIDTH_MM,
			.height_mm = HEIGHT_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = iyida_TE_USEC_60HZ_NS,
			.bpc = 8,
			.dsc = iyida_DSC,
		},
	},
);

static const struct gs_panel_mode_array iyida_lp_modes = {
	.num_modes = 1,
	.modes = {
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
				.te_usec = iyida_TE_USEC_AOD,
				.bpc = 8,
				.dsc = iyida_DSC,
				.is_lp_mode = true,
			},
		},
	},
};

static const struct gs_brightness_configuration iyida_brt_configs[] = {
	{
		.panel_rev = PANEL_REV_GE((u32)PANEL_REV_PROTO1),
		.default_brightness = 1031,    /* 140 nits brightness */
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 1,
					.max = 1450,
				},
				.level = {
					.min = 2,
					.max = 2988,
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
					.min = 2989,
					.max = 3498,
				},
				.percentage = {
					.min = 71,
					.max = 100,
				},
			},
		},
	},
};

static struct gs_panel_brightness_desc iyida_brightness_desc = {
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

static const struct gs_dsi_cmd iyida_lp_night_cmd[] = {
	/* 1 nit (newer) / 2 nits (proto) */
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_GE(PANEL_REV_EVT1),
		0x51, 0x00, 0x50),
	GS_DSI_FLUSH_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1),
		0x51, 0x00, 0x83),
};

static const struct gs_dsi_cmd iyida_lp_low_cmd[] = {
	/* 10 nits */
	GS_DSI_FLUSH_CMD(0x51, 0x01, 0x30),
};
static const struct gs_dsi_cmd iyida_lp_high_cmd[] = {
	/* 50 nits */
	GS_DSI_FLUSH_CMD(0x51, 0x02, 0xB2),
};

static const struct gs_dsi_cmd iyida_lp_sun_cmd[] = {
	/* 150 nits */
	GS_DSI_FLUSH_CMD(0x51, 0x04, 0x73),
};

static const struct gs_binned_lp iyida_binned_lp[] = {
	/* night threshold 4 nits */
	BINNED_LP_MODE_TIMING("night", 193, iyida_lp_night_cmd, 12, 12 + 50),
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 581, iyida_lp_low_cmd, 12, 12 + 50),
	/* high threshold 140 nits */
	BINNED_LP_MODE_TIMING("high", 1031, iyida_lp_high_cmd, 12, 12 + 50),
	BINNED_LP_MODE_TIMING("sun", 4095, iyida_lp_sun_cmd, 12, 12 + 50),
};

static const u8 iyida_cross_talk_cmd_1[] = {
	0x8D, 0x02, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xCE, 0x01, 0x03,
	0xF2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x1C, 0x01, 0x16, 0x85, 0x03, 0x27, 0x04, 0x00, 0x00, 0x00, 0xFF, 0x22,
	0x84, 0x03, 0xF1, 0x03, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x84, 0x03, 0x8C, 0x04,
	0x00, 0x00, 0x00, 0xFF, 0x17, 0x84, 0x03, 0x23, 0x04, 0x00, 0x00, 0x00, 0xFF,
	0x16, 0x85, 0x03, 0x27, 0x04, 0x00, 0x00, 0x00, 0xFF, 0x22, 0x84, 0x03, 0xF1,
	0x03, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x84, 0x03, 0x8C, 0x04, 0x00, 0x00, 0x00,
	0xFF, 0x17, 0x84, 0x03, 0x23, 0x04, 0x00, 0x00, 0x00, 0xFF, 0x16, 0x85, 0x03,
	0x27, 0x04, 0x00, 0x00, 0x00, 0xFF, 0x22, 0x84, 0x03, 0xF1, 0x03, 0x00, 0x00,
	0x00, 0xFF, 0x00, 0x84, 0x03, 0x8C, 0x04, 0x00, 0x00, 0x00, 0xFF, 0x17, 0x84,
	0x03, 0x23, 0x04, 0x00, 0x00, 0x00, 0xFF, 0x16, 0x85, 0x03, 0x27, 0x04, 0x00,
	0x00, 0x00, 0xFF, 0x22, 0x84, 0x03, 0xF1, 0x03, 0x00, 0x00, 0x00, 0xFF, 0x00,
	0x84, 0x03, 0x8C, 0x04, 0x00, 0x00, 0x00, 0xFF, 0x17, 0x84, 0x03, 0x23, 0x04,
	0x00, 0x00, 0x00, 0xFF, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00
};

static const u8 iyida_cross_talk_cmd_2[] = {
	0x8D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x00, 0x00, 0x96, 0x00, 0x00,
	0x96, 0x00, 0x00, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x1C, 0x22, 0x21, 0x19, 0x1A,
	0x1B, 0x12, 0x17, 0x15, 0x13, 0x10, 0x0D, 0x10, 0x12, 0x0D, 0x0D, 0x08, 0x06,
	0x05, 0x14, 0x0E, 0x0E, 0x0A, 0x0A, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x02, 0x03, 0x0A, 0x04, 0x28, 0x1C, 0x22, 0x21, 0x19,
	0x1A, 0x1B, 0x12, 0x17, 0x15, 0x13, 0x10, 0x0D, 0x10, 0x12, 0x0D, 0x0D, 0x08,
	0x06, 0x05, 0x14, 0x0E, 0x0E, 0x0A, 0x0A, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x02, 0x03, 0x0A, 0x04, 0x28, 0x1C, 0x22, 0x21,
	0x19, 0x1A, 0x1B, 0x12, 0x17, 0x15, 0x13, 0x10, 0x0D, 0x10, 0x12, 0x0D, 0x0D,
	0x08, 0x06, 0x05, 0x14, 0x0E, 0x0E, 0x0A, 0x0A, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x02, 0x03, 0x0A, 0x04, 0x28, 0x1C, 0x22,
	0x21, 0x19, 0x1A, 0x1B, 0x12, 0x17, 0x15, 0x13, 0x10, 0x0D, 0x10, 0x12, 0x0D,
	0x0D, 0x08, 0x06, 0x05, 0x14, 0x0E, 0x0E, 0x0A, 0x0A, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x02, 0x03, 0x0A, 0x04, 0x4B, 0x4B,
	0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x4B, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
};

static const u8 iyida_cross_talk_cmd_3[] = {
	0x8D, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x5B, 0x78, 0x9F, 0xBF, 0xFF, 0x03, 0x03, 0x03, 0x3C,
	0x3C, 0x3C, 0x64, 0x80, 0x80, 0x3C, 0x3C, 0x3C, 0x64, 0x80, 0x80, 0x3C, 0x3C,
	0x3C, 0x64, 0x80, 0x80, 0x02, 0x00, 0x02, 0x00, 0x00, 0x42, 0x00, 0x38, 0x02,
	0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x42, 0x00, 0x38, 0x02, 0x00, 0x02, 0x00,
	0x02, 0x00, 0x00, 0x42, 0x00, 0x38, 0x02, 0x00, 0xB4, 0xB4, 0xB4, 0xB4, 0x80,
	0x80, 0xB4, 0xB4, 0xB4, 0xB4, 0x80, 0x80, 0xB4, 0xB4, 0xB4, 0xB4, 0x80, 0x80,
	0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x68, 0x02, 0x00, 0x02, 0x00, 0x02,
	0x00, 0x02, 0x00, 0x02, 0x68, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00,
	0x02, 0x68, 0x02, 0x00, 0x50, 0x50, 0x50, 0x80, 0x80, 0x80, 0x50, 0x50, 0x50,
	0x80, 0x80, 0x80, 0x50, 0x50, 0x50, 0x80, 0x80, 0x80, 0x02, 0x00, 0x02, 0x00,
	0x00, 0x4B, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x4B, 0x02,
	0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x4B, 0x02, 0x00, 0x02, 0x00,
	0x50, 0x50, 0x50, 0x50, 0x80, 0x80, 0x50, 0x50, 0x50, 0x50, 0x80, 0x80, 0x50,
	0x50, 0x50, 0x50, 0x80, 0x80, 0x02, 0x00, 0x02, 0x68, 0x00, 0x22, 0x00, 0x34,
	0x02, 0x00, 0x02, 0x00, 0x02, 0x68, 0x00, 0x22, 0x00, 0x34, 0x02, 0x00, 0x02,
	0x00, 0x02, 0x68, 0x00, 0x22, 0x00, 0x34, 0x02, 0x00, 0xA0, 0xA0, 0xA0, 0xA0,
	0x80, 0x50, 0xA0, 0xA0, 0xA0, 0xA0, 0x80, 0x50, 0xA0, 0xA0, 0xA0, 0xA0, 0x80,
	0x50, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x40, 0x02, 0x30, 0x02, 0x00,
	0x02, 0x00, 0x02, 0x00, 0x02, 0x40, 0x02, 0x30, 0x02, 0x00
};

static const u8 iyida_cross_talk_cmd_4[] = {
	0x8D, 0x02, 0x00, 0x02, 0x00, 0x02, 0x40, 0x02, 0x30, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00,
	0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02,
	0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x64, 0x00, 0x9B, 0x01, 0x00, 0x01, 0x28,
	0x01, 0x50, 0x01, 0x79, 0x01, 0xD2, 0x01, 0xE4, 0x01, 0xFC, 0x28, 0x32, 0x80,
	0x80, 0x80, 0x5A, 0x5A, 0x5A, 0x5A, 0x28, 0x32, 0x80, 0x80, 0x80, 0x5A, 0x5A,
	0x5A, 0x5A, 0x28, 0x32, 0x80, 0x80, 0x80, 0x5A, 0x5A, 0x5A, 0x5A, 0xFF, 0xC8,
	0xA0, 0x80, 0x80, 0x14, 0x14, 0x14, 0x14, 0xFF, 0xC8, 0xA0, 0x80, 0x80, 0x14,
	0x14, 0x00, 0x00, 0xFF, 0xC8, 0xA0, 0x80, 0x80, 0x14, 0x14, 0x00, 0x00, 0x28,
	0x28, 0x64, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x64, 0x64, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x64, 0x64, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x00, 0x64, 0x00, 0x9B, 0x01, 0x00, 0x01, 0x28, 0x01, 0x50, 0x01, 0x79, 0x01,
	0xD2, 0x01, 0xE4, 0x01, 0xFC, 0x28, 0x32, 0x80, 0x80, 0x80, 0x5A, 0x5A, 0x5A,
	0x5A, 0x28, 0x32, 0x80, 0x80, 0x80, 0x5A, 0x5A, 0x5A, 0x5A, 0x28, 0x32, 0x80,
	0x80, 0x80, 0x5A, 0x5A, 0x5A, 0x5A, 0xFF, 0xC8, 0xA0, 0x80, 0x80, 0x14, 0x14,
	0x14, 0x14, 0xFF, 0xC8, 0xA0, 0x80, 0x80, 0x14, 0x14, 0x00, 0x00, 0xFF, 0xC8,
	0xA0, 0x80, 0x80, 0x14, 0x14, 0x00, 0x00, 0x28, 0x28, 0x64, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x64, 0x64, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x64,
	0x64, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xFF, 0xFF
};

static const u8 iyida_cross_talk_cmd_5[] = {
	0x8E, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const struct gs_dsi_cmd iyida_cross_talk_cmds[] = {
	GS_DSI_QUEUE_CMD(0xF0, 0x5A, 0x5A),
	GS_DSI_QUEUE_CMD(0xF1, 0x5A, 0x5A),
	GS_DSI_QUEUE_CMD(0xFC, 0x5A, 0x5A),
	GS_DSI_QUEUE_CMDLIST(iyida_cross_talk_cmd_1),
	GS_DSI_QUEUE_CMD(0xB0, 0x01, 0x00, 0x8D),
	GS_DSI_QUEUE_CMDLIST(iyida_cross_talk_cmd_2),
	GS_DSI_QUEUE_CMD(0xB0, 0x02, 0x00, 0x8D),
	GS_DSI_QUEUE_CMDLIST(iyida_cross_talk_cmd_3),
	GS_DSI_QUEUE_CMD(0xB0, 0x03, 0x00, 0x8D),
	GS_DSI_QUEUE_CMDLIST(iyida_cross_talk_cmd_4),
	GS_DSI_QUEUE_CMDLIST(iyida_cross_talk_cmd_5),
	GS_DSI_QUEUE_CMD(0xF7, 0x2F),
	GS_DSI_QUEUE_CMD(0xF0, 0xA5, 0xA5),
	GS_DSI_QUEUE_CMD(0xF1, 0xA5, 0xA5),
	GS_DSI_FLUSH_CMD(0xFC, 0xA5, 0xA5),
};
static DEFINE_GS_CMDSET(iyida_cross_talk);

static const struct gs_dsi_cmd iyida_init_cmds[] = {
	/* Sleep out*/
	GS_DSI_DELAY_CMD(120, MIPI_DCS_EXIT_SLEEP_MODE),

	/* Enable TE */
	GS_DSI_QUEUE_CMD(MIPI_DCS_SET_TEAR_ON, 0x00),
	/* Fixed TE2 */
	GS_DSI_QUEUE_CMDLIST(test_key_enable),
	/* 51 fix ; 41 manual */
	GS_DSI_QUEUE_CMD(0xB9, 0x51, 0x51),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x04, 0xB9),
	GS_DSI_QUEUE_CMD(0xB9, 0x80, 0x90, 0x02, 0x80, 0x90, 0x02),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x0D, 0xB9),
	GS_DSI_QUEUE_CMD(0xB9, 0x02, 0x02, 0x07, 0x02, 0x02, 0x07),
	/* Early Exit Off */
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x06, 0xBD),
	GS_DSI_QUEUE_CMD(0xBD, 0x80),
	/* Improve NBM to AOD transition */
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x03, 0xBB),
	GS_DSI_QUEUE_CMD(0xBB, 0x00, 0x0C),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x48, 0xF4),
	GS_DSI_QUEUE_CMD(0xF4, 0x73, 0x73, 0x73, 0x73),
	/* RETENTION Off */
	GS_DSI_QUEUE_CMDLIST(test_key_fc_enable),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0xA1, 0x62),
	GS_DSI_QUEUE_CMD(0x62, 0x01),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x02, 0xC4),
	GS_DSI_QUEUE_CMD(0xC4, 0x00),
	GS_DSI_QUEUE_CMDLIST(test_key_fc_disable),
	GS_DSI_QUEUE_CMDLIST(panel_update),
	GS_DSI_QUEUE_CMDLIST(test_key_disable),

	/* CASET: 2151 */
	GS_DSI_QUEUE_CMD(MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x08, 0x67),
	/* PASET: 2075 */
	GS_DSI_QUEUE_CMD(MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x08, 0x1B),

	/* Reset WRCTRLD */
	GS_DSI_FLUSH_CMD(MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20),
};
static DEFINE_GS_CMDSET(iyida_init);

/**
 * struct iyida_panel - panel specific runtime info
 *
 * This struct maintains iyida panel specific runtime info, any fixed details about panel
 * should most likely go into struct gs_panel_desc
 */
struct iyida_panel {
	/** @base: base panel struct */
	struct gs_panel base;
	/**
	 * @is_pixel_off: pixel-off command is sent to panel. Only sending normal-on or resetting
	 *		  panel can recover to normal mode after entering pixel-off state.
	 */
	bool is_pixel_off;
};
#define to_spanel(ctx) container_of(ctx, struct iyida_panel, base)

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

static u32 iyida_get_min_idle_vrefresh(struct gs_panel *ctx,
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

static u32 iyida_get_te2_freq(struct gs_panel *ctx)
{
	if (ctx->current_mode && ctx->current_mode->gs_mode.is_lp_mode)
		return iyida_TE2_FREQ_AOD;

	return iyida_TE2_FREQ_NORMAL;
}

/**
 * iyida_set_panel_feat_te - configure TE type, width and frequency
 * @ctx: gs_panel struct
 * @te_freq: TE frequency
 * @return: enum gs_panel_tex_opt, fixed or changeable TE
 *
 * Description: this function should cache commands only, don't update hw_status.
 */
static enum gs_panel_tex_opt iyida_set_panel_feat_te(struct gs_panel *ctx, u32 te_freq)
{
	struct device *dev = ctx->dev;
	const unsigned long *feat = ctx->sw_status.feat;
	enum gs_panel_tex_opt te_opt = TEX_OPT_FIXED;
	/* TODO: b/491475960 - re-enable NS mode support, with original TE width */

	/* Configure TE with dynamic 240Hz / fix 120Hz / changeable TE */
	if (test_bit(FEAT_EARLY_EXIT, feat) && te_freq == 240) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x41, 0x51);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x04, 0xB9);
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x80, 0x80, 0x02, 0x81, 0xE0, 0x02, 0x06, 0x80, 0x3C);
	} else if (test_bit(FEAT_EARLY_EXIT, feat)) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x51, 0x51);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x04, 0xB9);
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x80, 0x90, 0x02, 0x80, 0x90, 0x02);
	} else {
		te_opt = TEX_OPT_CHANGEABLE;
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x44, 0x51);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x04, 0xB9);
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x80, 0x90, 0x02, 0x80, 0x90, 0x02);
	}

	/* Fixed TE2 */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x0D, 0xB9);
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x02, 0x02, 0x07, 0x02, 0x02, 0x07);

	return te_opt;
}

static void iyida_set_panel_feat_early_exit(struct gs_panel *ctx, u32 te_freq)
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

static void iyida_set_panel_feat_frequency(struct gs_panel *ctx, u32 idle_vrefresh)
{
	struct device *dev = ctx->dev;
	const unsigned long *feat = ctx->sw_status.feat;
	enum gs_pwm_mode pwm_mode = ctx->pwm_mode;
	u8 val, val1, val2, val3;

	if (ctx->op_hz == 120) {
		switch (idle_vrefresh) {
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
				"%s: unsupported init freq %uhz, fall back to HS 1hz\n",
				__func__, idle_vrefresh);
			val = 0x07;
			break;
		}
	} else {
		switch (idle_vrefresh) {
		case 60:
			val = 0x18;
			break;
		case 30:
			val = 0x19;
			break;
		case 10:
			val = 0x1B;
			break;
		case 1:
			val = 0x1C;
			break;
		default:
			dev_warn(ctx->dev,
				"%s: unsupported init freq %uhz, fall back to NS 1hz\n",
				__func__, idle_vrefresh);
			val = 0x1C;
			break;
		}
	}

	/* Low Frequency Transition */
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
	GS_DCS_BUF_ADD_CMD(dev, 0x83, test_bit(FEAT_PWM_HIGH, feat) ? (val | 0x08) : val);

	if (!test_bit(FEAT_FRAME_AUTO, feat))
		return;

	if (idle_vrefresh == 1 && pwm_mode != GS_PWM_RATE_STANDARD)
		val1 = 0x01;
	else
		val1 = 0x00;

	if (ctx->op_hz == 60) { /* 60NS */
		if (idle_vrefresh == 1)
			val2 = 0xEC;
		else if (idle_vrefresh == 10)
			val2 = 0x14;
		else if (idle_vrefresh == 30)
			val2 = 0x04;
		else {
			if (idle_vrefresh != 60)
				dev_warn(dev, "unsupported auto NS freq %u\n", idle_vrefresh);
			val2 = 0x00;
		}
	} else if (pwm_mode == GS_PWM_RATE_HIGH) { /* 120HS1 */
		if (idle_vrefresh == 1)
			val2 = 0xDC;
		else if (idle_vrefresh == 10)
			val2 = 0x2C;
		else if (idle_vrefresh == 30)
			val2 = 0x0C;
		else if (idle_vrefresh == 60)
			val2 = 0x04;
		else {
			if (idle_vrefresh != 120)
				dev_warn(dev, "unsupported auto HS1 freq %u\n", idle_vrefresh);
			val2 = 0x00;
		}
	} else { /* 120HS */
		if (idle_vrefresh == 1)
			val2 = 0xEE;
		else if (idle_vrefresh == 10)
			val2 = 0x16;
		else if (idle_vrefresh == 30)
			val2 = 0x06;
		else if (idle_vrefresh == 60)
			val2 = 0x02;
		else {
			if (idle_vrefresh != 120)
				dev_warn(dev, "unsupported auto HS freq %u\n", idle_vrefresh);
			val2 = 0x00;
		}
	}

	if (ctx->op_hz == 60) { /* 60NS */
		val3 = 0x18;
	} else if (pwm_mode == GS_PWM_RATE_HIGH) { /* 120HS1 */
		val3 = 0x08;
	} else { /* 120HS */
		val3 = 0x00;
	}

	/* Manual Mode ON */
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
	GS_DCS_BUF_ADD_CMD(dev, 0x83, val3);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x1B, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, val1, val2);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0xA0, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x80, 0x00, 0x00, 0x02, 0x00, 0x06, 0x00, 0x16);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0xB0, 0xBD);
	if (ctx->op_hz == 60) /* 60NS */
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01, 0x03, 0x01);
	else /* 120HS and 120HS1 */
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01, 0x01, 0x03, 0x01);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x85, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
	/* Frame Insertion ON */
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x03);
}

static void iyida_set_panel_feat_freq_mode(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	unsigned long *feat = ctx->sw_status.feat;
	u8 val1, val2, val3, val4;

	dev_info(dev, "%s: ns=%u, h_pwm=%u\n",
		__func__, test_bit(FEAT_OP_NS, feat), test_bit(FEAT_PWM_HIGH, feat));

	val3 = 0x01;
	if (test_bit(FEAT_OP_NS, feat)) { /* 60NS */
		val1 = 0xBB;
		val2 = 0x1C;
		val4 = 0xFF;
	} else if (test_bit(FEAT_PWM_HIGH, feat)) { /* 120HS1 */
		val1 = 0x99;
		val2 = 0x0F;
		if (ctx->panel_rev_id.id > PANEL_REVID_EVT1)
			val3 = 0x05;
		val4 = 0x55;
	} else { /* 120HS */
		val1 = 0x88;
		val2 = 0x07;
		val4 = 0x00;
	}

	if (ctx->panel_rev_id.id > PANEL_REVID_EVT1) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x05, 0x67);
		GS_DCS_BUF_ADD_CMD(dev, 0x67, 0xB5);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x04, 0x67);
		GS_DCS_BUF_ADD_CMD(dev, 0x67, val4);
	}

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x07, 0xF2);
	GS_DCS_BUF_ADD_CMD(dev, 0xF2, val3);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x1B, 0x67);
	GS_DCS_BUF_ADD_CMD(dev, 0x67, val1);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
}

/**
 * iyida_set_panel_feat - configure panel features
 * @ctx: gs_panel struct
 * @pmode: gs_panel_mode struct, target panel mode
 * @enforce: force to write all of registers even if no feature state changes
 *
 * Configure panel features based on the context.
 * Note: iyida_set_panel_feat_xxx() should cache commands only while
 *       iyida_set_panel_feat() aggregates and sends the commands, and update
 *       hw_status. DO NOT update them in iyida_set_panel_feat_xxx().
 */
static void iyida_set_panel_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
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
	bool irc_mode_changed, idle_vrefresh_changed, vrefresh_changed, te_freq_changed;
	char trace_msg[64];
	DECLARE_BITMAP(changed_feat, FEAT_MAX);

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
		if (bitmap_empty(changed_feat, FEAT_MAX) && !vrefresh_changed &&
		    !idle_vrefresh_changed && !te_freq_changed && !irc_mode_changed) {
			dev_dbg(dev, "no changes to panel features, skip update\n");
			return;
		}
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

	/* Trace start */
	PANEL_ATRACE_BEGIN(trace_msg);
	GS_DCS_BUF_ADD_CMD(dev, 0x9F, 0xA5, 0xA5);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);

	sw_status->te.freq_hz = te_freq;

	if (test_bit(FEAT_OP_NS, changed_feat)) {
		ctx->op_hz = (gs_is_ns_op_rate(pmode)) ? 60 : 120;
		notify_panel_op_hz_changed(ctx);
	}

	if (test_bit(FEAT_PWM_HIGH, changed_feat))
		notify_panel_pwm_mode_changed(ctx);

	if (test_bit(FEAT_EARLY_EXIT, changed_feat) || te_freq_changed) {
		hw_status->te.option = iyida_set_panel_feat_te(ctx, te_freq);
		iyida_set_panel_feat_early_exit(ctx, te_freq);
	}

	if (test_bit(FEAT_FRAME_AUTO, changed_feat) || test_bit(FEAT_PWM_HIGH, changed_feat) ||
	    test_bit(FEAT_OP_NS, changed_feat) || idle_vrefresh_changed || vrefresh_changed) {
		iyida_set_panel_feat_freq_mode(ctx);
		iyida_set_panel_feat_frequency(ctx, idle_vrefresh ? idle_vrefresh : vrefresh);
	}

	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
	PANEL_ATRACE_END(trace_msg);
	/* Trace end */

	hw_status->vrefresh = vrefresh;
	hw_status->idle_vrefresh = idle_vrefresh;
	hw_status->te.freq_hz = te_freq;
	hw_status->irc_mode = irc_mode;
	bitmap_copy(hw_status->feat, feat, FEAT_MAX);
}

static void iyida_update_refresh_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
				    const u32 idle_vrefresh)
{
	struct gs_panel_status *sw_status = &ctx->sw_status;

	dev_info(ctx->dev, "%s: mode: %s set idle_vrefresh: %u\n", __func__,
		pmode->mode.name, idle_vrefresh);

	sw_status->idle_vrefresh = idle_vrefresh;
	ctx->idle_data.panel_idle_vrefresh = idle_vrefresh;
	iyida_set_panel_feat(ctx, pmode, false);
	notify_panel_mode_changed(ctx);

	dev_info(ctx->dev, "%s: display state is notified\n", __func__);
}

static bool iyida_set_self_refresh(struct gs_panel *ctx, bool enable)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	/* send_burn_in_comp_cmds here if need */

	if (unlikely(!pmode))
		return false;

	PANEL_ATRACE_INT_PID_FMT(enable, ctx->trace_pid,
					"set_self_refresh[%s]", ctx->panel_model);

	return false;
}

static void iyida_change_frequency(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 idle_vrefresh = 0;

	if (!ctx)
		return;

	if (pmode->idle_mode == GIDLE_MODE_ON_INACTIVITY)
		idle_vrefresh = iyida_get_min_idle_vrefresh(ctx, pmode);

	if (test_bit(FEAT_FRAME_AUTO, ctx->sw_status.feat))
		idle_vrefresh = ctx->sw_status.idle_vrefresh;

	iyida_update_refresh_mode(ctx, pmode, idle_vrefresh);
	ctx->sw_status.te.freq_hz = gs_drm_mode_te_freq(&pmode->mode);

	dev_info(ctx->dev, "change to %u hz\n", vrefresh);
}

static void iyida_update_wrctrld(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	u8 val = iyida_WRCTRLD_BCTRL_BIT;

	if (ctx->dimming_on)
		val |= iyida_WRCTRLD_DIMMING_BIT;

	dev_dbg(dev, "%s(wrctrld:0x%x, hbm: %d, dimming: %d)\n", __func__, val,
		GS_IS_HBM_ON(ctx->hbm_mode), ctx->dimming_on);

	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);
}

static void iyida_update_irc(struct gs_panel *ctx)
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

static int iyida_set_brightness(struct gs_panel *ctx, u16 br)
{
	u16 brightness;
	u32 max_brightness;
	struct iyida_panel *spanel = to_spanel(ctx);
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
		dev_warn(dev, "%s: capped to dbv(%d)\n", __func__, max_brightness);
	}

	if (br == max_brightness && GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode)) {
		GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0F, 0xFF);
		return 0;
	}

	/* swap endianness because panel expects MSB first */
	brightness = swab16(br);

	return gs_dcs_set_brightness(ctx, brightness);
}

static void iyida_set_hbm_mode(struct gs_panel *ctx, enum gs_hbm_mode mode)
{
	if (ctx->hbm_mode == mode)
		return;

	ctx->hbm_mode = mode;
	iyida_update_irc(ctx);

	dev_info(ctx->dev, "hbm_on=%d hbm_ircoff=%d.\n", GS_IS_HBM_ON(ctx->hbm_mode),
		 GS_IS_HBM_ON_IRC_OFF(ctx->hbm_mode));
}

static void iyida_set_dimming(struct gs_panel *ctx, bool dimming_on)
{
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;

	ctx->dimming_on = dimming_on;

	if (pmode->gs_mode.is_lp_mode) {
		dev_warn(dev, "in lp mode, skip to update dimming usage\n");
		return;
	}

	iyida_update_wrctrld(ctx);
}

static void iyida_mode_set(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	iyida_change_frequency(ctx, pmode);
}

static int iyida_get_brightness(struct thermal_zone_device *tzd, int *temp)
{
	struct iyida_panel *spanel;

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

static struct thermal_zone_device_ops iyida_tzd_ops = {
	.get_temp = iyida_get_brightness,
};

static bool iyida_is_mode_valid(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	/* TODO: b/491475960 - re-enable NS mode support */
	if (gs_is_ns_op_rate(pmode))
		return false;

	return true;
}

static void iyida_debugfs_init(struct drm_panel *panel, struct dentry *root)
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

	gs_panel_debugfs_create_cmdset(csroot, &iyida_init_cmdset, "init");

	dput(csroot);
panel_out:
	dput(panel_root);
}

static int iyida_set_op_hz(struct gs_panel *ctx, unsigned int hz)
{
	dev_info(ctx->dev, "%s: %u\n", __func__, hz);
	return -EPERM;
}

static enum gs_pwm_mode iyida_get_pwm_mode(struct gs_panel *ctx)
{
	if (ctx->op_hz == 60)
		return GS_PWM_RATE_STANDARD;

	return ctx->pwm_mode;
}

static int iyida_set_pwm_mode(struct gs_panel *ctx, enum gs_pwm_mode mode)
{
	struct device *dev = ctx->dev;

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

	iyida_set_panel_feat(ctx, ctx->current_mode, false);

	PANEL_ATRACE_END("%s(%d)", __func__, mode);

	return 0;
}

static void iyida_get_panel_rev(struct gs_panel *ctx, u32 id)
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

static void iyida_set_panel_lp_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
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
	/* Early Exit */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x06, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);

	ctx->hw_status.vrefresh = 30;
	ctx->hw_status.te.freq_hz = 30;
	ctx->hw_status.te.option = TEX_OPT_FIXED;
}

static void iyida_update_refresh_ctrl_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	const u32 ctrl = ctx->refresh_ctrl;
	unsigned long *feat = ctx->sw_status.feat;
	u32 min_vrefresh = ctx->sw_status.idle_vrefresh;
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

	if (ctrl & GS_PANEL_REFRESH_CTRL_EARLY_EXIT)
		set_bit(FEAT_EARLY_EXIT, feat);
	else {
		clear_bit(FEAT_EARLY_EXIT, feat);
		clear_bit(FEAT_FRAME_AUTO, feat);
		clear_bit(FEAT_FRAME_MANUAL_FI, feat);
	}

	if (lp_mode) {
		iyida_set_panel_lp_feat(ctx, pmode);
		return;
	}

	PANEL_ATRACE_INT_PID_FMT(ctx->sw_status.idle_vrefresh, ctx->trace_pid,
				 "idle_vrefresh[%s]", ctx->panel_model);
	PANEL_ATRACE_INT_PID_FMT(test_bit(FEAT_FRAME_AUTO, feat), ctx->trace_pid,
				 "FEAT_FRAME_AUTO[%s]", ctx->panel_model);
	PANEL_ATRACE_INT_PID_FMT(test_bit(FEAT_EARLY_EXIT, feat), ctx->trace_pid,
				 "FEAT_EARLY_EXIT[%s]", ctx->panel_model);

	iyida_set_panel_feat(ctx, pmode, false);
}

static void iyida_refresh_ctrl(struct gs_panel *ctx)
{
	const u32 ctrl = ctx->refresh_ctrl;
	struct device *dev = ctx->dev;

	PANEL_ATRACE_BEGIN(__func__);

	iyida_update_refresh_ctrl_feat(ctx, ctx->current_mode);

	if (ctrl & GS_PANEL_REFRESH_CTRL_FI_FRAME_COUNT_MASK) {
		dev_dbg(dev, "%s: manually inserting frame\n", __func__);
		GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
		GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
	}

	PANEL_ATRACE_END(__func__);
}

static void iyida_set_lp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;

	PANEL_ATRACE_BEGIN(__func__);

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	if (ctx->panel_rev_id.id < PANEL_REVID_EVT1) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x93, 0xF4);
		GS_DCS_BUF_ADD_CMD(dev, 0xF4, 0x03);
	}

	GS_DCS_BUF_ADD_CMD(dev, 0x83, 0x10, 0x03);
	/* AOD Mode On */
	GS_DCS_BUF_ADD_CMD(dev, 0x53, 0x24);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_disable);

	iyida_update_refresh_ctrl_feat(ctx, pmode);

	ctx->sw_status.te.freq_hz = 30;
	ctx->sw_status.te.option = TEX_OPT_FIXED;

	PANEL_ATRACE_END(__func__);

	notify_panel_te2_freq_changed(ctx, 0);
	dev_info(dev, "enter %dhz LP mode\n", drm_mode_vrefresh(&pmode->mode));
}

static void iyida_set_nolp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;

	if (!gs_is_panel_active(ctx))
		return;

	PANEL_ATRACE_BEGIN(__func__);

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMD(dev, 0x83, 0x10, 0x00);
	/* AOD Mode Off */
	GS_DCS_BUF_ADD_CMD(dev, 0x53, ctx->dimming_on ? 0x28 : 0x20);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);

	iyida_update_refresh_ctrl_feat(ctx, pmode);
	iyida_set_panel_feat(ctx, pmode, true);
	iyida_change_frequency(ctx, pmode);

	PANEL_ATRACE_END(__func__);

	notify_panel_te2_freq_changed(ctx, 0);
	dev_info(dev, "exit LP mode\n");
}

static void iyida_pre_update_ffc(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	dev_dbg(dev, "disabling FFC\n");

	PANEL_ATRACE_BEGIN(__func__);

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_enable);
	/* FFC off */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x36, 0xC5);
	GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x10);
	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_disable);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_fc_disable);

	PANEL_ATRACE_END(__func__);

	ctx->ffc_en = false;
}

static void iyida_update_ffc(struct gs_panel *ctx, unsigned int hs_clk_mbps)
{
	struct device *dev = ctx->dev;
	u8 val1, val2;

	dev_dbg(dev, "hs_clk_mbps: current=%d, target=%d\n",
		ctx->dsi_hs_clk_mbps, hs_clk_mbps);

	if (hs_clk_mbps != MIPI_DSI_FREQ_DEFAULT &&
	    hs_clk_mbps != MIPI_DSI_FREQ_ALTERNATIVE) {
		dev_warn(dev, "invalid hs_clk_mbps=%d for FFC\n", hs_clk_mbps);
		return;
	}

	PANEL_ATRACE_BEGIN(__func__);

	if (ctx->dsi_hs_clk_mbps != hs_clk_mbps || !ctx->ffc_en) {
		dev_info(dev, "updating FFC for hs_clk_mbps=%d\n", hs_clk_mbps);
		ctx->dsi_hs_clk_mbps = hs_clk_mbps;

		if (hs_clk_mbps == MIPI_DSI_FREQ_DEFAULT) {
			val1 = 0x4A;
			val2 = 0x1D;
		} else {
			val1 = 0x4E;
			val2 = 0x63;
		}

		GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
		GS_DCS_BUF_ADD_CMDLIST(dev, test_key_fc_enable);
		/* Update FFC */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x3C, 0xC5);
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, val1, val2);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x44, 0xC5);
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, val1, val2);
		/* FFC ON */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x36, 0xC5);
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x11, 0x10, 0x50, 0x05);
		GS_DCS_BUF_ADD_CMDLIST(dev, test_key_disable);
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_fc_disable);

		ctx->ffc_en = true;
	}

	PANEL_ATRACE_END(__func__);
}

static void iyida_enable_errfg(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	GS_DCS_BUF_ADD_CMDLIST(dev, test_key_enable);
	GS_DCS_BUF_ADD_CMD(dev, 0xE5, 0x15);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x4C, 0xF4);
	GS_DCS_BUF_ADD_CMD(dev, 0xF4, 0x10);
	GS_DCS_BUF_ADD_CMD(dev, 0xED, 0x00, 0x00, 0x51);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, test_key_disable);
}

static int iyida_enable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	const bool needs_reset = !gs_is_panel_enabled(ctx);

	if (!pmode) {
		dev_err(dev, "no current mode set\n");
		return -EINVAL;
	}

	dev_info(dev, "iyida enable\n");

	PANEL_ATRACE_BEGIN(__func__);

	if (needs_reset) {
		/* toggle reset gpio */
		gs_panel_reset_helper(ctx);

		/* TODO: VDDD control */

		/* initial command */
		gs_panel_send_cmdset(ctx, &iyida_init_cmdset);
		iyida_enable_errfg(ctx);

		if (!ctx->force_ffc_off)
			iyida_update_ffc(ctx, MIPI_DSI_FREQ_DEFAULT);
	}

	/* frequency */
	iyida_set_panel_feat(ctx, pmode, true);
	iyida_change_frequency(ctx, pmode);

	/* DSC related configuration */
	mipi_dsi_compression_mode(to_mipi_dsi_device(dev), true);
	gs_dcs_write_dsc_config(dev, &pps_config);
	/* DSC Enable */
	GS_DCS_BUF_ADD_CMD(dev, 0x9D, 0x01);

	/* dimming and HBM */
	iyida_update_wrctrld(ctx);

	gs_panel_send_cmdset(ctx, &iyida_cross_talk_cmdset);

	if (pmode->gs_mode.is_lp_mode)
		iyida_set_lp_mode(ctx, pmode);
	else {
		const u16 min_brightness = ctx->desc->brightness_desc->min_brightness;
		u16 brightness = max(ctx->bl->props.brightness, min_brightness);

		ctx->bl->props.brightness = brightness;
		GS_DCS_BUF_ADD_CMD(dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
					(brightness >> 8) & 0xFF,
					brightness & 0xFF);
	}

	GS_DCS_WRITE_CMD(dev, MIPI_DCS_SET_DISPLAY_ON);

	ctx->dsi_hs_clk_mbps = MIPI_DSI_FREQ_DEFAULT;

	PANEL_ATRACE_END(__func__);

	return 0;
}

static int iyida_disable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	int ret;

	dev_info(dev, "%s+\n", __func__);

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
	ctx->ffc_en = false;

	GS_DCS_WRITE_DELAY_CMD(dev, 20, MIPI_DCS_SET_DISPLAY_OFF);

	if (ctx->panel_state == GPANEL_STATE_OFF)
		GS_DCS_WRITE_DELAY_CMD(dev, 100, MIPI_DCS_ENTER_SLEEP_MODE);
	return 0;
}

static ssize_t iyida_get_color_data(struct gs_panel *ctx, char *buf, size_t buf_len)
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

static void iyida_panel_init(struct gs_panel *ctx)
{
	iyida_update_ffc(ctx, MIPI_DSI_FREQ_DEFAULT);
	iyida_enable_errfg(ctx);
	ctx->refresh_ctrl |= GS_PANEL_REFRESH_CTRL_EARLY_EXIT;
}

static int iyida_panel_probe(struct mipi_dsi_device *dsi)
{
	struct iyida_panel *spanel;
	struct gs_panel *ctx;
	int ret;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	spanel->base.op_hz = 120;
	spanel->base.pwm_mode = GS_PWM_RATE_STANDARD;
	spanel->is_pixel_off = false;

	ctx = &spanel->base;
	ctx->thermal = devm_kzalloc(&dsi->dev, sizeof(*ctx->thermal), GFP_KERNEL);
	if (!ctx->thermal) {
		devm_kfree(&dsi->dev, spanel);
		return -ENOMEM;
	}

	/* FFC is disabled in bootloader */
	ctx->ffc_en = false;

	ret = gs_dsi_panel_common_init(dsi, ctx);
	if (ret)
		return ret;

	ctx->thermal->tz =
		thermal_tripless_zone_device_register("inner_brightness",
			spanel, &iyida_tzd_ops, NULL);
	if (IS_ERR(ctx->thermal->tz)) {
		dev_warn(ctx->dev,
			"failed to register inner display tz: %ld",
			PTR_ERR(ctx->thermal->tz));
		ctx->thermal->tz = NULL;
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

static int iyida_panel_config(struct gs_panel *ctx)
{
	return gs_panel_update_brightness_desc(&iyida_brightness_desc, iyida_brt_configs,
					       ARRAY_SIZE(iyida_brt_configs),
					       ctx->panel_rev_bitmask);
}

static void iyida_panel_remove(struct mipi_dsi_device *dsi)
{
	const struct gs_panel *ctx = mipi_dsi_get_drvdata(dsi);

	if (ctx->thermal && ctx->thermal->tz)
		thermal_zone_device_unregister(ctx->thermal->tz);
	gs_dsi_panel_common_remove(dsi);
}

static const struct drm_panel_funcs iyida_drm_funcs = {
	.disable = iyida_disable,
	.unprepare = gs_panel_unprepare,
	.prepare = gs_panel_prepare,
	.enable = iyida_enable,
	.get_modes = gs_panel_get_modes,
	.debugfs_init = iyida_debugfs_init,
};

static const struct gs_panel_funcs iyida_gs_funcs = {
	.set_brightness = iyida_set_brightness,
	.set_lp_mode = iyida_set_lp_mode,
	.set_nolp_mode = iyida_set_nolp_mode,
	.set_binned_lp = gs_panel_set_binned_lp_helper,
	.set_dimming = iyida_set_dimming,
	.set_hbm_mode = iyida_set_hbm_mode,
	.set_self_refresh = iyida_set_self_refresh,
	.refresh_ctrl = iyida_refresh_ctrl,
	.is_mode_seamless_atomic = gs_panel_is_mode_seamless_atomic_helper,
	.mode_set = iyida_mode_set,
	.panel_config = iyida_panel_config,
	.get_panel_rev = iyida_get_panel_rev,
	.read_serial = gs_panel_read_slsi_ddic_id,
	.get_color_data = iyida_get_color_data,
	.set_pwm_mode = iyida_set_pwm_mode,
	.get_pwm_mode = iyida_get_pwm_mode,
	.set_op_hz = iyida_set_op_hz,
	.pre_update_ffc = iyida_pre_update_ffc,
	.update_ffc = iyida_update_ffc,
	.panel_init = iyida_panel_init,
	.is_mode_valid = iyida_is_mode_valid,
	.detect_fault = gs_panel_detect_fault_helper,
	.trace_panel_settings_full = gs_panel_trace_settings_full_helper,
	.get_te2_freq = iyida_get_te2_freq,
};

const struct gs_panel_reg_ctrl_desc iyida_reg_ctrl_desc = {
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

static struct gs_panel_calibration_desc iyida_calibration_desc = {
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

static const struct gs_panel_detect_fault_desc iyida_fault_desc = {
	.errfg_reg = 0xEE,
	.errfg_len = 2,
	.dsi_err_reg = 0xE9,
	.vlin1_mask = BIT(6),
	.dsi_err_mask = BIT(0),
	.detect_interval_ms = 5000,
	.panel_errors_mask = BIT(GS_PANEL_ERR_DSI_READ_FAILURE),
};

const struct gs_panel_desc google_iyida = {
	.data_lane_cnt = 4,
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */
	.hdr_formats = BIT(2) | BIT(3),
	.brightness_desc = &iyida_brightness_desc,
	.calibration_desc = &iyida_calibration_desc,
	.modes = &iyida_modes,
	.lp_modes = &iyida_lp_modes,
	.binned_lp = iyida_binned_lp,
	.num_binned_lp = ARRAY_SIZE(iyida_binned_lp),
	.reg_ctrl_desc = &iyida_reg_ctrl_desc,
	.panel_func = &iyida_drm_funcs,
	.gs_panel_func = &iyida_gs_funcs,
	.default_dsi_hs_clk_mbps = MIPI_DSI_FREQ_DEFAULT,
	.reset_timing_ms = { -1, -1, 10 },
	.fault_desc = &iyida_fault_desc,
};

static const struct of_device_id gs_panel_of_match[] = {
	{ .compatible = "google,gs-iyida", .data = &google_iyida },
	{ }
};
MODULE_DEVICE_TABLE(of, gs_panel_of_match);

static struct mipi_dsi_driver gs_panel_driver = {
	.probe = iyida_panel_probe,
	.remove = iyida_panel_remove,
	.driver = {
		.name = "panel-gs-iyida",
		.of_match_table = gs_panel_of_match,
	},
};
module_mipi_dsi_driver(gs_panel_driver);

MODULE_AUTHOR("Derick Hong <derickhong@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google iyida panel driver");
MODULE_LICENSE("Dual MIT/GPL");

