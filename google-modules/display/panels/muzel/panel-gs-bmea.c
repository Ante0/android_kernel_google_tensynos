// SPDX-License-Identifier: MIT

#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_vblank.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <video/mipi_display.h>

#include "trace/panel_trace.h"

#include "gs_panel/drm_panel_funcs_defaults.h"
#include "gs_panel/gs_panel.h"
#include "gs_panel/gs_panel_funcs_defaults.h"

/**
 * enum bmea_panel_type - panel types supported by bmea driver
 * @PANEL_TYPE_BZEA: BMEA panel
 * @PANEL_TYPE_MTEA: MTEA panel
 * @PANEL_TYPE_MAX: placeholder, counter for number of supported panels
 */
enum bmea_panel_type {
	PANEL_TYPE_BZEA = 0,
	PANEL_TYPE_MTEA,
	PANEL_TYPE_MAX,
};

#define NUM_SUPPORTED_RESOLUTIONS 2

static struct gs_panel_desc gs_bzea;
#define GET_PANEL_TYPE(ctx) ((ctx->desc == &gs_bzea) ? PANEL_TYPE_BZEA : PANEL_TYPE_MTEA)

/**
 * struct bmea_panel - panel specific info
 *
 * This struct maintains bmea panel specific info. The variables with the prefix hw_ keep
 * track of the features that were actually committed to hardware, and should be modified
 * after sending cmds to panel, i.e., updating hw state.
 */
struct bmea_panel {
	/** @base: base panel struct */
	struct gs_panel base;
	/** @force_changeable_te: force changeable TE (instead of fixed) during early exit */
	bool force_changeable_te;
	/** @force_changeable_te2: force changeable TE2 for monitoring refresh rate */
	bool force_changeable_te2;
	/** @force_za_off: force to turn off zonal attenuation */
	bool force_za_off;
	/**
	 * @pending_skin_temp_handling: whether there is skin temperature which needs to be
	 *                              handled later
	 */
	bool pending_skin_temp_handling;
	/**
	 * @reg_dump_for_gram_collision_triggered: whether the register dump for GRAM collision
	 *                                         has been triggered
	 */
	bool reg_dump_for_gram_collision_triggered;
	/**
	 * @prev_gram_collision_count: previous count of GRAM collision for calculating the
	 *                             increased count
	 */
	u8 prev_gram_collision_count;
	/** @trace_msg: the message which records panel settings in the trace */
	char trace_msg[64];
};

#define to_spanel(ctx) container_of(ctx, struct bmea_panel, base)

static struct drm_dsc_config pps_configs[PANEL_TYPE_MAX][NUM_SUPPORTED_RESOLUTIONS] = {
	{
		/* BZEA DSCv1.2a 1080x2410 */
		{
			.line_buf_depth = 9,
			.bits_per_component = 8,
			.convert_rgb = true,
			.slice_count = 2,
			.slice_width = 540,
			.slice_height = 241,
			.simple_422 = false,
			.pic_width = 1080,
			.pic_height = 2410,
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
			.scale_increment_interval = 5983,
			.nfl_bpg_offset = 103,
			.slice_bpg_offset = 109,
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
		},
		/* BZEA DSCv1.2a 1280x2856 */
		{
			.line_buf_depth = 9,
			.bits_per_component = 8,
			.convert_rgb = true,
			.slice_count = 2,
			.slice_width = 640,
			.slice_height = 24,
			.simple_422 = false,
			.pic_width = 1280,
			.pic_height = 2856,
			.rc_tgt_offset_high = 3,
			.rc_tgt_offset_low = 3,
			.bits_per_pixel = 128,
			.rc_edge_factor = 6,
			.rc_quant_incr_limit1 = 11,
			.rc_quant_incr_limit0 = 11,
			.initial_xmit_delay = 512,
			.initial_dec_delay = 577,
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
			.scale_decrement_interval = 8,
			.scale_increment_interval = 640,
			.nfl_bpg_offset = 1069,
			.slice_bpg_offset = 913,
			.final_offset = 4336,
			.vbr_enable = false,
			.slice_chunk_size = 640,
			.dsc_version_minor = 2,
			.dsc_version_major = 1,
			.native_422 = false,
			.native_420 = false,
			.second_line_bpg_offset = 0,
			.nsl_bpg_offset = 0,
			.second_line_offset_adj = 0,
		},
	},
	{
		/* MTEA DSCv1.2a 1080x2404 */
		{
			.line_buf_depth = 9,
			.bits_per_component = 8,
			.convert_rgb = true,
			.slice_count = 2,
			.slice_width = 540,
			.slice_height = 601,
			.simple_422 = false,
			.pic_width = 1080,
			.pic_height = 2404,
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
			.scale_increment_interval = 14924,
			.nfl_bpg_offset = 41,
			.slice_bpg_offset = 44,
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
		},
		/* MTEA DSCv1.2a 1344x2992 */
		{
			.line_buf_depth = 9,
			.bits_per_component = 8,
			.convert_rgb = true,
			.slice_count = 2,
			.slice_width = 672,
			.slice_height = 34,
			.simple_422 = false,
			.pic_width = 1344,
			.pic_height = 2992,
			.rc_tgt_offset_high = 3,
			.rc_tgt_offset_low = 3,
			.bits_per_pixel = 128,
			.rc_edge_factor = 6,
			.rc_quant_incr_limit1 = 11,
			.rc_quant_incr_limit0 = 11,
			.initial_xmit_delay = 512,
			.initial_dec_delay = 592,
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
			.scale_decrement_interval = 9,
			.scale_increment_interval = 932,
			.nfl_bpg_offset = 745,
			.slice_bpg_offset = 616,
			.final_offset = 4336,
			.vbr_enable = false,
			.slice_chunk_size = 672,
			.dsc_version_minor = 2,
			.dsc_version_major = 1,
			.native_422 = false,
			.native_420 = false,
			.second_line_bpg_offset = 0,
			.nsl_bpg_offset = 0,
			.second_line_offset_adj = 0,
		},
	},
};

#define BMEA_WRCTRLD_LPM_BIT 0x04
#define BMEA_WRCTRLD_DIMMING_BIT 0x08
#define BMEA_WRCTRLD_BCTRL_BIT 0x20
#define BMEA_WRCTRLD_HBM_BIT 0xC0

#define BMEA_TE2_CHANGEABLE 0x04
#define BMEA_TE2_FIXED_120HZ 0x29
#define BMEA_TE2_FIXED_240HZ 0x41
#define BMEA_TE2_RISING_EDGE_OFFSET_PROTO 0x20
#define BMEA_TE2_FALLING_EDGE_OFFSET_PROTO 0x57
#define BMEA_TE2_RISING_EDGE_OFFSET 0x24
#define BZEA_TE2_FALLING_EDGE_OFFSET 0x59
#define MTEA_TE2_FALLING_EDGE_OFFSET 0x5B

#define BMEA_TE_USEC_120HZ 275
#define BMEA_TE_USEC_80HZ 4436
#define BMEA_TE_USEC_60HZ 8598
#define BMEA_TE_USEC_48HZ 12762
#define BMEA_TE_USEC_30HZ 25248
#define BMEA_TE_USEC_24HZ 33577
#define BMEA_TE_USEC_10HZ 91871
#define BZEA_TE_USEC_AOD 1104
#define MTEA_TE_USEC_AOD 1102

#define BZEA_TE_USEC_VRR 377
#define MTEA_TE_USEC_VRR 373

#define BMEA_OSC_DOE_CODE 0x00000100

#define MTEA_MIPI_DSI_FREQ_MBPS_DEFAULT 1368
#define MTEA_MIPI_DSI_FREQ_MBPS_ALTERNATIVE 1346
#define BZEA_MIPI_DSI_FREQ_MBPS_DEFAULT 1390
#define BZEA_MIPI_DSI_FREQ_MBPS_ALTERNATIVE 1410

#define PROJECT "BMEA"

static const u16 BZEA_FHD_HDISPLAY = 1080, BZEA_FHD_VDISPLAY = 2410;
static const u16 BZEA_FHD_HFP = 80, BZEA_FHD_HSA = 24, BZEA_FHD_HBP = 36;
static const u16 BZEA_FHD_VFP = 12, BZEA_FHD_VSA = 4, BZEA_FHD_VBP = 24;

static const u16 BZEA_WQHD_HDISPLAY = 1280, BZEA_WQHD_VDISPLAY = 2856;
static const u16 BZEA_WQHD_HFP = 80, BZEA_WQHD_HSA = 24, BZEA_WQHD_HBP = 46;
static const u16 BZEA_WQHD_VFP = 12, BZEA_WQHD_VSA = 4, BZEA_WQHD_VBP = 28;

static const u16 MTEA_FHD_HDISPLAY = 1080, MTEA_FHD_VDISPLAY = 2404;
static const u16 MTEA_FHD_HFP = 80, MTEA_FHD_HSA = 24, MTEA_FHD_HBP = 36;
static const u16 MTEA_FHD_VFP = 16, MTEA_FHD_VSA = 4, MTEA_FHD_VBP = 26;

static const u16 MTEA_WQHD_HDISPLAY = 1344, MTEA_WQHD_VDISPLAY = 2992;
static const u16 MTEA_WQHD_HFP = 88, MTEA_WQHD_HSA = 24, MTEA_WQHD_HBP = 44;
static const u16 MTEA_WQHD_VFP = 12, MTEA_WQHD_VSA = 4, MTEA_WQHD_VBP = 22;

#define MTEA_DIMENSION_MM .width_mm = 70, .height_mm = 156
#define BZEA_DIMENSION_MM .width_mm = 66, .height_mm = 147

#define BZEA_FHD_DSC { .enabled = true, .dsc_count = 2, .cfg = &pps_configs[PANEL_TYPE_BZEA][0], }
#define BZEA_WQHD_DSC { .enabled = true, .dsc_count = 2, .cfg = &pps_configs[PANEL_TYPE_BZEA][1], }

#define MTEA_FHD_DSC { .enabled = true, .dsc_count = 2, .cfg = &pps_configs[PANEL_TYPE_MTEA][0], }
#define MTEA_WQHD_DSC { .enabled = true, .dsc_count = 2, .cfg = &pps_configs[PANEL_TYPE_MTEA][1], }

static const struct gs_panel_mode_array bzea_modes = GS_PANEL_MODES(
#ifndef PANEL_FACTORY_BUILD
	/* MRR modes */
	{
		.mode = {
			.name = "1280x2856x60@60",
			DRM_MODE_TIMING(60, BZEA_WQHD_HDISPLAY, BZEA_WQHD_HFP,
					    BZEA_WQHD_HSA, BZEA_WQHD_HBP,
					    BZEA_WQHD_VDISPLAY, BZEA_WQHD_VFP,
					    BZEA_WQHD_VSA, BZEA_WQHD_VBP),
					    .type = DRM_MODE_TYPE_PREFERRED,
			BZEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BMEA_TE_USEC_60HZ,
			.bpc = 8,
			.dsc = BZEA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = BZEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1280x2856x120@120",
			DRM_MODE_TIMING(120, BZEA_WQHD_HDISPLAY, BZEA_WQHD_HFP,
					     BZEA_WQHD_HSA, BZEA_WQHD_HBP,
					     BZEA_WQHD_VDISPLAY, BZEA_WQHD_VFP,
					     BZEA_WQHD_VSA, BZEA_WQHD_VBP),
			BZEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BMEA_TE_USEC_120HZ,
			.bpc = 8,
			.dsc = BZEA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = BZEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1080x2410x60@60",
			DRM_MODE_TIMING(60, BZEA_FHD_HDISPLAY, BZEA_FHD_HFP,
					    BZEA_FHD_HSA, BZEA_FHD_HBP,
					    BZEA_FHD_VDISPLAY, BZEA_FHD_VFP,
					    BZEA_FHD_VSA, BZEA_FHD_VBP),
			BZEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BMEA_TE_USEC_60HZ,
			.bpc = 8,
			.dsc = BZEA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = BZEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1080x2410x120@120",
			DRM_MODE_TIMING(120, BZEA_FHD_HDISPLAY, BZEA_FHD_HFP,
					     BZEA_FHD_HSA, BZEA_FHD_HBP,
					     BZEA_FHD_VDISPLAY, BZEA_FHD_VFP,
					     BZEA_FHD_VSA, BZEA_FHD_VBP),
			BZEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BMEA_TE_USEC_120HZ,
			.bpc = 8,
			.dsc = BZEA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = BZEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
#endif
	/* VRR modes */
	{
		.mode = {
			.name = "1280x2856x120@240",
			DRM_VRR_MODE_TIMING(120, 240, BZEA_WQHD_HDISPLAY, BZEA_WQHD_HFP,
						      BZEA_WQHD_HSA, BZEA_WQHD_HBP,
						      BZEA_WQHD_VDISPLAY, BZEA_WQHD_VFP,
						      BZEA_WQHD_VSA, BZEA_WQHD_VBP),
			/* aligned to bootloader resolution */
			.type = DRM_MODE_TYPE_PREFERRED,
			BZEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BZEA_TE_USEC_VRR,
			.bpc = 8,
			.dsc = BZEA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = BZEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1080x2410x120@240",
			DRM_VRR_MODE_TIMING(120, 240, BZEA_FHD_HDISPLAY, BZEA_FHD_HFP,
						      BZEA_FHD_HSA, BZEA_FHD_HBP,
						      BZEA_FHD_VDISPLAY, BZEA_FHD_VFP,
						      BZEA_FHD_VSA, BZEA_FHD_VBP),
			BZEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BZEA_TE_USEC_VRR,
			.bpc = 8,
			.dsc = BZEA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = BZEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1280x2856x120@120",
			DRM_VRR_MODE_TIMING(120, 120, BZEA_WQHD_HDISPLAY, BZEA_WQHD_HFP,
						      BZEA_WQHD_HSA, BZEA_WQHD_HBP,
						      BZEA_WQHD_VDISPLAY, BZEA_WQHD_VFP,
						      BZEA_WQHD_VSA, BZEA_WQHD_VBP),
			BZEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BZEA_TE_USEC_VRR,
			.bpc = 8,
			.dsc = BZEA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = BZEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1080x2410x120@120",
			DRM_VRR_MODE_TIMING(120, 120, BZEA_FHD_HDISPLAY, BZEA_FHD_HFP,
						      BZEA_FHD_HSA, BZEA_FHD_HBP,
						      BZEA_FHD_VDISPLAY, BZEA_FHD_VFP,
						      BZEA_FHD_VSA, BZEA_FHD_VBP),
			BZEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BZEA_TE_USEC_VRR,
			.bpc = 8,
			.dsc = BZEA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = BZEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
#endif /* !PANEL_FACTORY_BUILD */
); /* bzea_modes */

static const struct gs_panel_mode_array mtea_modes = GS_PANEL_MODES(
#ifndef PANEL_FACTORY_BUILD
	/* MRR modes */
	{
		.mode = {
			.name = "1344x2992x60@60",
			DRM_MODE_TIMING(60, MTEA_WQHD_HDISPLAY, MTEA_WQHD_HFP,
					    MTEA_WQHD_HSA, MTEA_WQHD_HBP,
					    MTEA_WQHD_VDISPLAY, MTEA_WQHD_VFP,
					    MTEA_WQHD_VSA, MTEA_WQHD_VBP),
			MTEA_DIMENSION_MM,
			.type = DRM_MODE_TYPE_PREFERRED,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BMEA_TE_USEC_60HZ,
			.bpc = 8,
			.dsc = MTEA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = MTEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1344x2992x120@120",
			DRM_MODE_TIMING(120, MTEA_WQHD_HDISPLAY, MTEA_WQHD_HFP,
					     MTEA_WQHD_HSA, MTEA_WQHD_HBP,
					     MTEA_WQHD_VDISPLAY, MTEA_WQHD_VFP,
					     MTEA_WQHD_VSA, MTEA_WQHD_VBP),
			MTEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BMEA_TE_USEC_120HZ,
			.bpc = 8,
			.dsc = MTEA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = MTEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1080x2404x60@60",
			DRM_MODE_TIMING(60, MTEA_FHD_HDISPLAY, MTEA_FHD_HFP,
					    MTEA_FHD_HSA, MTEA_FHD_HBP,
					    MTEA_FHD_VDISPLAY, MTEA_FHD_VFP,
					    MTEA_FHD_VSA, MTEA_FHD_VBP),
			MTEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BMEA_TE_USEC_60HZ,
			.bpc = 8,
			.dsc = MTEA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = MTEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1080x2404x120@120",
			DRM_MODE_TIMING(120, MTEA_FHD_HDISPLAY, MTEA_FHD_HFP,
					     MTEA_FHD_HSA, MTEA_FHD_HBP,
					     MTEA_FHD_VDISPLAY, MTEA_FHD_VFP,
					     MTEA_FHD_VSA, MTEA_FHD_VBP),
			MTEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BMEA_TE_USEC_120HZ,
			.bpc = 8,
			.dsc = MTEA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = MTEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
#endif
	/* VRR modes */
	{
		.mode = {
			.name = "1344x2992x120@240",
			DRM_VRR_MODE_TIMING(120, 240, MTEA_WQHD_HDISPLAY, MTEA_WQHD_HFP,
						      MTEA_WQHD_HSA, MTEA_WQHD_HBP,
						      MTEA_WQHD_VDISPLAY, MTEA_WQHD_VFP,
						      MTEA_WQHD_VSA, MTEA_WQHD_VBP),
			/* aligned to bootloader resolution */
			.type = DRM_MODE_TYPE_PREFERRED,
			MTEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = MTEA_TE_USEC_VRR,
			.bpc = 8,
			.dsc = MTEA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = MTEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1080x2404x120@240",
			DRM_VRR_MODE_TIMING(120, 240, MTEA_FHD_HDISPLAY, MTEA_FHD_HFP,
						      MTEA_FHD_HSA, MTEA_FHD_HBP,
						      MTEA_FHD_VDISPLAY, MTEA_FHD_VFP,
						      MTEA_FHD_VSA, MTEA_FHD_VBP),
			MTEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = MTEA_TE_USEC_VRR,
			.bpc = 8,
			.dsc = MTEA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = MTEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1344x2992x120@120",
			DRM_VRR_MODE_TIMING(120, 120, MTEA_WQHD_HDISPLAY, MTEA_WQHD_HFP,
						      MTEA_WQHD_HSA, MTEA_WQHD_HBP,
						      MTEA_WQHD_VDISPLAY, MTEA_WQHD_VFP,
						      MTEA_WQHD_VSA, MTEA_WQHD_VBP),
			MTEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = MTEA_TE_USEC_VRR,
			.bpc = 8,
			.dsc = MTEA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = MTEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1080x2404x120@120",
			DRM_VRR_MODE_TIMING(120, 120, MTEA_FHD_HDISPLAY, MTEA_FHD_HFP,
						      MTEA_FHD_HSA, MTEA_FHD_HBP,
						      MTEA_FHD_VDISPLAY, MTEA_FHD_VFP,
						      MTEA_FHD_VSA, MTEA_FHD_VBP),
			MTEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = MTEA_TE_USEC_VRR,
			.bpc = 8,
			.dsc = MTEA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = BMEA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = MTEA_TE2_FALLING_EDGE_OFFSET,
		},
	},
#endif /* !PANEL_FACTORY_BUILD */
); /* mtea_modes */

/* TODO: b/347362323 - Confirm AOD timing*/
static const struct gs_panel_mode_array bzea_lp_modes = GS_PANEL_MODES(
	{
		.mode = {
			.name = "1280x2856x30@30",
			DRM_MODE_TIMING(30, BZEA_WQHD_HDISPLAY, BZEA_WQHD_HFP,
					    BZEA_WQHD_HSA, BZEA_WQHD_HBP,
					    BZEA_WQHD_VDISPLAY, BZEA_WQHD_VFP,
					    BZEA_WQHD_VSA, BZEA_WQHD_VBP),
			BZEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BZEA_TE_USEC_AOD,
			.bpc = 8,
			.dsc = BZEA_WQHD_DSC,
			.is_lp_mode = true,
		},
	},
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1080x2410x30@30",
			DRM_MODE_TIMING(30, BZEA_FHD_HDISPLAY, BZEA_FHD_HFP,
					    BZEA_FHD_HSA, BZEA_FHD_HBP,
					    BZEA_FHD_VDISPLAY, BZEA_FHD_VFP,
					    BZEA_FHD_VSA, BZEA_FHD_VBP),
			BZEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = BZEA_TE_USEC_AOD,
			.bpc = 8,
			.dsc = BZEA_FHD_DSC,
			.is_lp_mode = true,
		},
	},
#endif
); /* bzea_lp_modes */

static const struct gs_panel_mode_array mtea_lp_modes = GS_PANEL_MODES(
	{
		.mode = {
			.name = "1344x2992x30@30",
			DRM_MODE_TIMING(30, MTEA_WQHD_HDISPLAY, MTEA_WQHD_HFP,
					    MTEA_WQHD_HSA, MTEA_WQHD_HBP,
					    MTEA_WQHD_VDISPLAY, MTEA_WQHD_VFP,
					    MTEA_WQHD_VSA, MTEA_WQHD_VBP),
			MTEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = MTEA_TE_USEC_AOD,
			.bpc = 8,
			.dsc = MTEA_WQHD_DSC,
			.is_lp_mode = true,
		},
	},
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1080x2404x30@30",
			DRM_MODE_TIMING(30, MTEA_FHD_HDISPLAY, MTEA_FHD_HFP,
					    MTEA_FHD_HSA, MTEA_FHD_HBP,
					    MTEA_FHD_VDISPLAY, MTEA_FHD_VFP,
					    MTEA_FHD_VSA, MTEA_FHD_VBP),
			MTEA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = MTEA_TE_USEC_AOD,
			.bpc = 8,
			.dsc = MTEA_FHD_DSC,
			.is_lp_mode = true,
		},
	},
#endif
); /* mtea_lp_modes */

static const struct gs_brightness_configuration bmea_brt_configs[] = {
	{
		.panel_rev = PANEL_REV_GE((u32)PANEL_REV_EVT1),
		.default_brightness = 4637, /* dbv_for_140_nits */
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 1,
					.max = 1450,
				},
				.level = {
					.min = 492,
					.max = 13418,
				},
				.percentage = {
					.min = 0,
					.max = 64,
				},
			},
			.hbm = {
				.nits = {
					.min = 1450,
					.max = 2250,
				},
				.level = {
					.min = 13419,
					.max = 16383,
				},
				.percentage = {
					.min = 64,
					.max = 100,
				},
			},
		},
	},
	{
		.panel_rev = PANEL_REV_PROTO1_1,
		.default_brightness = 4837, /* dbv_for_140_nits */
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 1,
					.max = 1250,
				},
				.level = {
					.min = 512,
					.max = 13084,
				},
				.percentage = {
					.min = 0,
					.max = 61,
				},
			},
			.hbm = {
				.nits = {
					.min = 1250,
					.max = 2050,
				},
				.level = {
					.min = 13085,
					.max = 16383,
				},
				.percentage = {
					.min = 61,
					.max = 100,
				},
			},
		},
	},
	{
		.panel_rev = PANEL_REV_PROTO1,
		.default_brightness = 4837, /* dbv_for_140_nits */
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 2,
					.max = 1250,
				},
				.level = {
					.min = 702,
					.max = 13084,
				},
				.percentage = {
					.min = 0,
					.max = 61,
				},
			},
			.hbm = {
				.nits = {
					.min = 1250,
					.max = 2050,
				},
				.level = {
					.min = 13085,
					.max = 16383,
				},
				.percentage = {
					.min = 61,
					.max = 100,
				},
			},
		},
	},
};

static struct gs_panel_brightness_desc bmea_brightness_desc = {
	.max_luminance = 10000000,
	.max_avg_luminance = 10000000,
	.min_luminance = 5,
};

static const u8 unlock_cmd_f0[] = { 0xF0, 0x5A, 0x5A };
static const u8 lock_cmd_f0[] = { 0xF0, 0xA5, 0xA5 };
static const u8 unlock_cmd_fc[] = { 0xFC, 0x5A, 0x5A };
static const u8 lock_cmd_fc[] = { 0xFC, 0xA5, 0xA5 };
static const u8 panel_update[] = { 0xF7, 0x2F };
static const u8 aod_off[] = { MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20 };
static const u8 aod_on_normal[] = { MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24 };
static const u8 aod_on_smooth[] = { MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x2C };

static const struct gs_dsi_cmd bmea_lp_night_cmds[] = {
	/* AOD Night Mode, 2nit */
	GS_DSI_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x02, 0xA1),
};

static const struct gs_dsi_cmd bmea_lp_low_cmds[] = {
	/* AOD Low Mode, 10nit */
	GS_DSI_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x05, 0x76),
};

static const struct gs_dsi_cmd bmea_lp_high_cmds[] = {
	/* AOD High Mode, 50nit */
	GS_DSI_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0B, 0x58),
};

static const struct gs_dsi_cmd bmea_lp_sun_cmds[] = {
	/* AOD Sun Mode, 150nit */
	GS_DSI_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x12, 0xB1),
};

static const struct gs_binned_lp bzea_binned_lp[] = {
	/* night threshold 4 nits */
	BINNED_LP_MODE_TIMING("night", 961, bmea_lp_night_cmds,
			      BMEA_TE2_RISING_EDGE_OFFSET,
			      BZEA_TE2_FALLING_EDGE_OFFSET),
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 2737, bmea_lp_low_cmds,
			      BMEA_TE2_RISING_EDGE_OFFSET,
			      BZEA_TE2_FALLING_EDGE_OFFSET),
	/* high threshold 140 nits */
	BINNED_LP_MODE_TIMING("high", 4637, bmea_lp_high_cmds,
			      BMEA_TE2_RISING_EDGE_OFFSET,
			      BZEA_TE2_FALLING_EDGE_OFFSET),
	BINNED_LP_MODE_TIMING("sun", 13084, bmea_lp_sun_cmds,
			      BMEA_TE2_RISING_EDGE_OFFSET,
			      BZEA_TE2_FALLING_EDGE_OFFSET),
};

static const struct gs_binned_lp mtea_binned_lp[] = {
	/* night threshold 4 nits */
	BINNED_LP_MODE_TIMING("night", 961, bmea_lp_night_cmds,
			      BMEA_TE2_RISING_EDGE_OFFSET,
			      MTEA_TE2_FALLING_EDGE_OFFSET),
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 2737, bmea_lp_low_cmds,
			      BMEA_TE2_RISING_EDGE_OFFSET,
			      MTEA_TE2_FALLING_EDGE_OFFSET),
	/* high threshold 140 nits */
	BINNED_LP_MODE_TIMING("high", 4637, bmea_lp_high_cmds,
			      BMEA_TE2_RISING_EDGE_OFFSET,
			      MTEA_TE2_FALLING_EDGE_OFFSET),
	BINNED_LP_MODE_TIMING("sun", 13084, bmea_lp_sun_cmds,
			      BMEA_TE2_RISING_EDGE_OFFSET,
			      MTEA_TE2_FALLING_EDGE_OFFSET),
};

#define BMEA_BURN_IN_COMP_OFFSET -4

/* Modified in bmea_get_burn_in_comp_cmdset. Set default temperature as 0x19. */
static u8 bmea_burn_in_comp_temp_cmd[] = { 0x69, 0x19 };

static const struct gs_dsi_cmd bmea_burn_in_comp_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(unlock_cmd_f0),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x03, 0x69),
	GS_DSI_QUEUE_CMDLIST(bmea_burn_in_comp_temp_cmd),
	GS_DSI_FLUSH_CMDLIST(lock_cmd_f0),
};
static DEFINE_GS_CMDSET(bmea_burn_in_comp);

static const struct gs_dsi_cmdset *bmea_get_burn_in_comp_cmdset(u32 value)
{
	bmea_burn_in_comp_temp_cmd[1] = value + BMEA_BURN_IN_COMP_OFFSET;

	return &bmea_burn_in_comp_cmdset;
}

/* In HS 60Hz mode, TE period is 16.6ms but DDIC vsync period is 8.3ms. */
#define BMEA_HS_VSYNC_PERIOD_US 8333
/**
 * bmea_check_command_timing_for_te2 - control timing between a command and DDIC vsync
 * @ctx: gs_panel struct
 *
 * Control the timing of sending the command in the 2nd DDIC vsync period within two contiguous
 * TE to avoid a 120Hz frame in HS 60Hz mode. This function should be called if the command could
 * cause a 120Hz frame and mess up the timing, e.g. TE2. The below diagram illustrates the desired
 * timing of sending the command, where vsync ~= TE rising (vblank) + TE width (te_usec).
 *
 *                       send the command
 *                      /
 *   TE             .  v          TE
 *   |              .             |
 * ----------------------------------
 *    <------    16.6ms   ---- -->
 *
 * vsync          vsync         vsync
 *   |              |             |
 * ----------------------------------
 *    <-- 8.3ms  --> <-- 8.3ms -->
 *         1st            2nd
 */
static void bmea_check_command_timing_for_te2(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct drm_crtc *crtc = NULL;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	ktime_t last_te, last_vsync, now;
	s64 since_last_vsync_us, temp_us, delay_us;

	if (!pmode) {
		dev_dbg(dev, "%s: unable to get current mode\n", __func__);
		return;
	}

	/* only HS 60Hz mode and changeable TE2 need the timing control */
	if (drm_mode_vrefresh(&pmode->mode) == ctx->op_hz || ctx->te2.option == TEX_OPT_FIXED)
		return;

	if (ctx->gs_connector->base.state)
		crtc = ctx->gs_connector->base.state->crtc;
	if (!crtc) {
		dev_dbg(dev, "%s: unable to get crtc\n", __func__);
		return;
	}

	drm_crtc_vblank_count_and_time(crtc, &last_te);
	if (!last_te) {
		dev_dbg(dev, "%s: unable to get last vblank\n", __func__);
		return;
	}

	last_vsync = last_te + pmode->gs_mode.te_usec;
	now = ktime_get();
	since_last_vsync_us = ktime_us_delta(now, last_vsync);
	temp_us = since_last_vsync_us;

	/**
	 * While DPU enters/exits hibernation, we may not get the nearest vblank successfully.
	 * Divided by TE period (vsync period * 2) then we can get the remaining time (remainder).
	 */
	temp_us %= (BMEA_HS_VSYNC_PERIOD_US * 2);

	/**
	 * Do nothing if it's greater than a vsync time, i.e. sent in the 2nd vsync period.
	 * The additional 1ms is for the tolerance.
	 */
	if (temp_us > (BMEA_HS_VSYNC_PERIOD_US + 1000))
		return;

	/* Adding 1ms tolerance to make sure the command will be sent in the 2nd vsync period. */
	delay_us = BMEA_HS_VSYNC_PERIOD_US - temp_us + 1000;

	dev_dbg(dev, "%s: te %lld, vsync %lld, now %lld, since_vsync %lld, delay %lld\n", __func__,
		last_te, last_vsync, now, since_last_vsync_us, delay_us);

	PANEL_ATRACE_BEGIN(__func__);
	usleep_range(delay_us, delay_us + 100);
	PANEL_ATRACE_END(__func__);
}

/* Apply appropriate gain into DDIC for burn-in compensation */
static void bmea_send_burn_in_comp_cmds(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct bmea_panel *spanel = to_spanel(ctx);
	const struct gs_dsi_cmdset *burn_in_comp_cmdset =
					bmea_get_burn_in_comp_cmdset(ctx->skin_temperature);

	if (ctx->panel_state != GPANEL_STATE_NORMAL)
		return;

	spanel->pending_skin_temp_handling = false;

	/* SP Temperature commands */
	PANEL_ATRACE_BEGIN(__func__);
	gs_panel_send_cmdset(ctx, burn_in_comp_cmdset);
	PANEL_ATRACE_END(__func__);

	PANEL_ATRACE_INT_PID_FMT(bmea_burn_in_comp_temp_cmd[1], ctx->trace_pid,
				 "skin_temperature(offset)[%s]", ctx->panel_model);
	dev_dbg(dev, "skin_temp: apply gain into ddic at %udeg c (offset=%d)\n",
		bmea_burn_in_comp_temp_cmd[1], BMEA_BURN_IN_COMP_OFFSET);
}

static void bmea_update_te2_option(struct gs_panel *ctx, u8 val)
{
	struct device *dev = ctx->dev;

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0xB9);
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, val);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	notify_panel_te2_option_changed(ctx);
	dev_dbg(dev, "te2 option is updated to %s\n",
		(val == BMEA_TE2_CHANGEABLE) ? "changeable" :
		 ((val == BMEA_TE2_FIXED_240HZ) ? "fixed:240" : "fixed:120"));
}

static void bmea_update_te2(struct gs_panel *ctx)
{
	struct bmea_panel *spanel = to_spanel(ctx);

	if (spanel->force_changeable_te2 && ctx->te2.option == TEX_OPT_FIXED) {
		dev_dbg(ctx->dev, "force to changeable TE2\n");
		ctx->te2.option = TEX_OPT_CHANGEABLE;
		bmea_update_te2_option(ctx, BMEA_TE2_CHANGEABLE);
	}
}

static void bmea_te2_setting(struct gs_panel *ctx)
{
	struct bmea_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	u32 rising = BMEA_TE2_RISING_EDGE_OFFSET;
	u32 falling = (GET_PANEL_TYPE(ctx) == PANEL_TYPE_BZEA) ?
		      BZEA_TE2_FALLING_EDGE_OFFSET : MTEA_TE2_FALLING_EDGE_OFFSET;
	u8 option;

	if (ctx->te2.option == TEX_OPT_FIXED && !spanel->force_changeable_te2)
		option = (ctx->te2.freq_hz == 240) ? BMEA_TE2_FIXED_240HZ :
						     BMEA_TE2_FIXED_120HZ;
	else
		option = BMEA_TE2_CHANGEABLE;

	if (ctx->panel_rev_id.id < PANEL_REVID_EVT1) {
		rising = BMEA_TE2_RISING_EDGE_OFFSET_PROTO;
		falling = BMEA_TE2_FALLING_EDGE_OFFSET_PROTO;
	}

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	/* changeable or 240/120Hz fixed TE2 */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0xB9);
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, option);
	/* rising and falling edges */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x11, 0xB9);
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, (rising >> 4) & 0xFF,
				((rising & 0x0F) << 4) | (falling & 0x0F),
				(falling >> 4) & 0xFF, (rising >> 4) & 0xFF,
				((rising & 0x0F) << 4) | (falling & 0x0F),
				(falling >> 4) & 0xFF);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	notify_panel_te2_freq_changed(ctx, 0);
	notify_panel_te2_option_changed(ctx);
	dev_dbg(dev, "TE2 setting: option %s, rising 0x%X, falling 0x%X\n",
		(option == TEX_OPT_CHANGEABLE) ? "changeable" :
		 ((ctx->te2.freq_hz == 240) ? "fixed:240" : "fixed:120"),
		rising, falling);
}

static bool bmea_set_te2_freq(struct gs_panel *ctx, u32 freq_hz)
{
	struct device *dev = ctx->dev;

	if (ctx->te2.freq_hz == freq_hz)
		return false;

	if (ctx->te2.option == TEX_OPT_FIXED) {
		bool lp_mode = ctx->current_mode->gs_mode.is_lp_mode;

		if ((!lp_mode && freq_hz != 120 && freq_hz != 240) || (lp_mode && freq_hz != 30)) {
			dev_warn(dev, "unsupported fixed TE2 freq (%u) in %s mode\n", freq_hz,
				 lp_mode ? "lp" : "normal");
			return false;
		}

		ctx->te2.freq_hz = freq_hz;
		/**
		 * Fixed TE2 frequency will be limited at 30Hz automatically in AOD mode,
		 * so we don't need to send any commands.
		 */
		if (!lp_mode)
			bmea_update_te2_option(ctx, (freq_hz == 240) ? BMEA_TE2_FIXED_240HZ :
								       BMEA_TE2_FIXED_120HZ);
	} else if (ctx->te2.option == TEX_OPT_CHANGEABLE) {
		dev_dbg(dev, "set changeable TE2 freq %uhz\n", freq_hz);
		ctx->te2.freq_hz = freq_hz;
	} else {
		dev_warn(dev, "TE2 option is unsupported (%u)\n", ctx->te2.option);
		return false;
	}

	PANEL_ATRACE_INT_PID_FMT(ctx->te2.freq_hz, ctx->trace_pid,
				 "te2_freq[%s]", ctx->panel_model);

	return true;
}

static u32 bmea_get_te2_freq(struct gs_panel *ctx)
{
	return ctx->te2.freq_hz;
}

static bool bmea_set_te2_option(struct gs_panel *ctx, u32 option)
{
	struct bmea_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	u8 val;

	if (option == ctx->te2.option)
		return false;

	if (option == TEX_OPT_FIXED) {
		if (spanel->force_changeable_te2) {
			dev_dbg(dev, "force changeable TE2 is set\n");
			return false;
		}
		val = (ctx->te2.freq_hz == 240) ? BMEA_TE2_FIXED_240HZ : BMEA_TE2_FIXED_120HZ;
	} else if (option == TEX_OPT_CHANGEABLE) {
		val = BMEA_TE2_CHANGEABLE;
	} else {
		dev_warn(dev, "unsupported TE2 option (%u)\n", option);
		return false;
	}

	bmea_update_te2_option(ctx, val);
	ctx->te2.option = option;

	return true;
}

static enum gs_panel_tex_opt bmea_get_te2_option(struct gs_panel *ctx)
{
	return ctx->te2.option;
}

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

static u32 bmea_get_idle_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	const int vrefresh = drm_mode_vrefresh(&pmode->mode);

	if (!gs_is_vrr_mode(pmode))
		return (vrefresh == 60) ? GIDLE_MODE_ON_SELF_REFRESH : GIDLE_MODE_ON_INACTIVITY;

	return pmode->idle_mode;
}

static u32 bmea_get_min_idle_vrefresh(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
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

	dev_dbg(ctx->dev, "min_idle_vrefresh %d\n", min_idle_vrefresh);

	return min_idle_vrefresh;
}

static void bmea_set_panel_feat_manual_mode_fi(struct gs_panel *ctx, bool enabled)
{
	struct device *dev = ctx->dev;
	u8 val = enabled ? 0x22 : 0x00;

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x2A, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, val, val);

	if (!enabled) {
		/* Mask Setting */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x02);
	}

	dev_dbg(ctx->dev, "manual mode fi=%d\n", enabled);
}

/**
 * bmea_set_panel_feat_te - configure TE type, width and frequency
 * @ctx: gs_panel struct
 * @is_vrr: true if it's VRR mode
 * @te_freq: TE frequency
 * @return: enum gs_panel_tex_opt, fixed or changeable TE
 *
 * Description: this function should cache commands only, don't update hw_status.
 */
static enum gs_panel_tex_opt bmea_set_panel_feat_te(struct gs_panel *ctx, bool is_vrr, u32 te_freq)
{
	struct bmea_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	const unsigned long *feat = ctx->sw_status.feat;
	enum bmea_panel_type panel_type = GET_PANEL_TYPE(ctx);
	bool dvt_and_after = (ctx->panel_rev_id.id <= PANEL_REVID_EVT1_1) ? 0 : 1;
	enum gs_panel_tex_opt te_opt = TEX_OPT_FIXED;

	if ((is_vrr || test_bit(FEAT_EARLY_EXIT, feat)) && !spanel->force_changeable_te) {
		if (is_vrr && te_freq == 240) {
			static const u8 vrr_te_settings[PANEL_TYPE_MAX][2][GS_PWM_RATE_MAX][7] = {
				{ /* BZEA */
					{ /* EVT1.1 and before */
						{ 0xB9, 0xB2, 0x23, 0x02, 0x57, 0x23, 0x02 },
						{ 0xB9, 0x57, 0x23, 0x02, 0x57, 0x23, 0x02 },
					},
					{ /* DVT and after */
						{ 0xB9, 0xB0, 0xE3, 0x02, 0x55, 0x63, 0x02 },
						{ 0xB9, 0x55, 0x63, 0x02, 0x55, 0x63, 0x02 },
					},
				},
				{ /* MTEA */
					{ /* EVT1.1 and before */
						{ 0xB9, 0xB9, 0xF3, 0x02, 0x5A, 0xF3, 0x02 },
						{ 0xB9, 0x5A, 0xF3, 0x02, 0x5A, 0xF3, 0x02 },
					},
					{ /* DVT and after */
						{ 0xB9, 0xB8, 0xA3, 0x02, 0x59, 0x23, 0x02 },
						{ 0xB9, 0x59, 0x23, 0x02, 0x59, 0x23, 0x02 },
					},
				},
			};
			static const u8 vrr_mask_setting[GS_PWM_RATE_MAX] = { 0x00, 0x40 };

			/* 240Hz multi TE */
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, test_bit(FEAT_PWM_HIGH, feat) ? 0x41 : 0x31);
			/* TE width */
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x08, 0xB9);
			GS_DCS_BUF_ADD_CMDLIST(dev,
				vrr_te_settings[panel_type][dvt_and_after][ctx->pwm_mode]);
			/* VRR Masking */
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x1A, 0xB9);
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, vrr_mask_setting[ctx->pwm_mode]);
		} else { /* 120Hz */
			static const u8 fixed_te_settings[PANEL_TYPE_MAX][2][2][7] = {
				{ /* BZEA */
					{ /* MRR */
						/* EVT1.1 and before */
						{ 0xB9, 0xB2, 0x23, 0x02, 0xB2, 0x23, 0x02 },
						/* DVT and after */
						{ 0xB9, 0xB3, 0x23, 0x02, 0xB3, 0x23, 0x02 },
					},
					{ /* VRR */
						/* EVT1.1 and before */
						{ 0xB9, 0xB2, 0x23, 0x02, 0xB2, 0x23, 0x02 },
						/* DVT and after */
						{ 0xB9, 0xB0, 0xE3, 0x02, 0xB0, 0xE3, 0x02 },
					},
				},
				{ /* MTEA */
					{ /* MRR */
						/* EVT1.1 and before */
						{ 0xB9, 0xB9, 0xF3, 0x02, 0xB9, 0xF3, 0x02 },
						/* DVT and after */
						{ 0xB9, 0xBA, 0xE3, 0x02, 0xBA, 0xE3, 0x02 },
					},
					{ /* VRR */
						/* EVT1.1 and before */
						{ 0xB9, 0xB9, 0xF3, 0x02, 0xB9, 0xF3, 0x02 },
						/* DVT and after */
						{ 0xB9, 0xB8, 0xA3, 0x02, 0xB8, 0xA3, 0x02 },
					},
				},
			};

			/* Fixed TE */
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x29);
			/* TE width */
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x08, 0xB9);
			GS_DCS_BUF_ADD_CMDLIST(dev,
					fixed_te_settings[panel_type][is_vrr][dvt_and_after]);
		}
	} else {
		static const u8 changeable_te_settings[PANEL_TYPE_MAX][2][2][4] = {
			{ /* BZEA */
				{ /* MRR */
					{ 0xB9, 0xB2, 0x23, 0x02 }, /* EVT1.1 and before */
					{ 0xB9, 0xB3, 0x23, 0x02 }, /* DVT and after */
				},
				{ /* VRR */
					{ 0xB9, 0xB2, 0x23, 0x02 }, /* EVT1.1 and before */
					{ 0xB9, 0xB0, 0xE3, 0x02 }, /* DVT and after */
				},
			},
			{ /* MTEA */
				{ /* MRR */
					{ 0xB9, 0xB9, 0xF3, 0x02 }, /* EVT1.1 and before */
					{ 0xB9, 0xBA, 0xE3, 0x02 }, /* DVT and after */
				},
				{ /* VRR */
					{ 0xB9, 0xB9, 0xF3, 0x02 }, /* EVT1.1 and before */
					{ 0xB9, 0xB8, 0xA3, 0x02 }, /* DVT and after */
				},
			},
		};

		/* Changeable TE */
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x04);
		/* TE width */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x08, 0xB9);
		GS_DCS_BUF_ADD_CMDLIST(dev,
				changeable_te_settings[panel_type][is_vrr][dvt_and_after]);
		te_opt = TEX_OPT_CHANGEABLE;
	}

	/* TE sync setting */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x91, 0xB9);
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, test_bit(FEAT_PWM_HIGH, feat) ? 0x80 : 0x00);
	return te_opt;
}

static void bmea_set_panel_feat_hbm_irc(struct gs_panel *ctx, enum irc_mode irc_mode)
{
	struct device *dev = ctx->dev;
	enum bmea_panel_type panel_type = GET_PANEL_TYPE(ctx);
	bool flat_z = (irc_mode == IRC_FLAT_Z) ? 1 : 0;
	bool dvt_and_after = (ctx->panel_rev_id.id <= PANEL_REVID_EVT1_1) ? 0 : 1;

	/*
	 * "Flat mode" is used to replace IRC on for normal mode and HDR video,
	 * and "Flat Z mode" is used to replace IRC off for sunlight
	 * environment.
	 */

	if (ctx->panel_rev_id.id <= PANEL_REVID_PROTO1_1 && panel_type == PANEL_TYPE_MTEA) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x71, 0x6D);
		GS_DCS_BUF_ADD_CMD(dev, 0x6D, flat_z ? 0xB4 : 0xB0);
	}

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x77, 0x6D);
	if (flat_z) {
		static const u8 flat_z_settings[PANEL_TYPE_MAX][2][3] = {
			{ /* BZEA */
				{ 0x6D, 0xAF, 0x8C }, /* EVT1.1 and before */
				{ 0x6D, 0xD5, 0xAA }, /* DVT and after */
			},
			{ /* MTEA */
				{ 0x6D, 0xBC, 0x96 }, /* EVT1.1 and before */
				{ 0x6D, 0xD5, 0xAA }, /* DVT and after */
			},
		};

		GS_DCS_BUF_ADD_CMDLIST(dev, flat_z_settings[panel_type][dvt_and_after]);
	}
	else /* IRC_FLAT_DEFAULT or IRC_OFF */
		GS_DCS_BUF_ADD_CMD(dev, 0x6D, 0x00, 0x00);
	if (dvt_and_after) {
		static const u8 sp_irc_settings[PANEL_TYPE_MAX][2][7] = {
			{ /* BZEA */
				{ 0x69, 0x03, 0x08, 0x08, 0x04, 0x09, 0x09 }, /* Flat mode */
				{ 0x69, 0x76, 0x76, 0x6E, 0xE2, 0xE2, 0xE2 }, /* Flat Z mode */
			},
			{ /* MTEA */
				{ 0x69, 0x08, 0x13, 0x07, 0x18, 0x1F, 0x18 }, /* Flat mode */
				{ 0x69, 0x73, 0x75, 0x6E, 0xE2, 0xE2, 0xF6 }, /* Flat Z mode */
			},
		};

		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x07, 0xC5, 0x69);
		GS_DCS_BUF_ADD_CMDLIST(dev, sp_irc_settings[panel_type][flat_z]);
	}

	dev_info(dev, "irc_mode=%d\n", irc_mode);
}

static void bmea_set_panel_feat_early_exit(struct gs_panel *ctx, u32 vrefresh, u32 te_freq)
{
	struct device *dev = ctx->dev;
	const unsigned long *feat = ctx->sw_status.feat;
	u8 val;

	if (!test_bit(FEAT_EARLY_EXIT, feat) || vrefresh == 80 || vrefresh == 48)
		val = 0x66;
	else
		val = (te_freq == 240) ? 0x64 : 0x65;

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x03, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, val);

	if (ctx->panel_rev_id.id < PANEL_REVID_EVT1) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x20, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x24, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	}

}

static void bmea_set_panel_feat_tsp_sync(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	if (ctx->panel_rev_id.id >= PANEL_REVID_EVT1)
		return;

	/* Fixed 240Hz TSP Vsync, HS */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x05, 0xF2); /* Global para */
	GS_DCS_BUF_ADD_CMD(dev, 0xF2, 0x03); /* 240Hz setting */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x20, 0xB9); /* Global para */
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x81); /* TSP Sync setting */
}

static void bmea_set_panel_feat_frequency(struct gs_panel *ctx, u32 vrefresh, u32 idle_vrefresh)
{
	struct device *dev = ctx->dev;
	const unsigned long *feat = ctx->sw_status.feat;
	u8 val;

	/*
	 * Description: this sequence possibly overrides some configs early-exit
	 * and operation set, depending on FI mode.
	 */
	if (test_bit(FEAT_FRAME_AUTO, feat)) {
		/* initial frequency 120Hz */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x08, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x0C, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00);
		/* target frequency */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x7B, 0xBD);
		if (idle_vrefresh == 10) {
			val = 0x16;
		} else if (idle_vrefresh == 24) {
			val = 0x08;
		} else if (idle_vrefresh == 30) {
			val = 0x06;
		} else if (idle_vrefresh == 60) {
			val = 0x02;
		} else {
			if (idle_vrefresh != 1)
				dev_warn(dev, "unsupported target freq %u\n", idle_vrefresh);
			/* 1Hz */
			val = 0xEE;
		}
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, val);
		/* step setting */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x7D, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x02, 0x00, 0x06, 0x00, 0x16);
		/* step setting */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x8D, 0xBD);
		if (idle_vrefresh == 10)
			/* 120->10Hz */
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x02, 0x01, 0x02, 0x00);
		else if (idle_vrefresh == 24)
			/* 120->24Hz */
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x02, 0x01, 0x01, 0x00);
		else if (idle_vrefresh == 30)
			/* 120->30Hz */
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x02, 0x01, 0x00, 0x00);
		else if (idle_vrefresh == 60)
			/* 120->60Hz */
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x02, 0x00, 0x00, 0x00);
		else
			/* 120->1Hz */
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x02, 0x01, 0x02, 0x04);
		/* auto mode */
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x05);
	} else { /* manual */
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
		if (vrefresh == 1) {
			val = 0x07;
		} else if (vrefresh == 10) {
			val = 0x03;
		} else if (vrefresh == 24) {
			val = 0x06;
		} else if (vrefresh == 30) {
			val = 0x02;
		} else if (vrefresh == 48) {
			val = 0x05;
		} else if (vrefresh == 60) {
			val = 0x01;
		} else if (vrefresh == 80) {
			val = 0x04;
		} else {
			if (vrefresh != 120)
				dev_warn(dev, "unsupported manual freq %u\n", vrefresh);
			/* 120Hz */
			val = 0x00;
		}
		GS_DCS_BUF_ADD_CMD(dev, 0x83, test_bit(FEAT_PWM_HIGH, feat) ? (val | 0x10) : val);
	}
}

/**
 * bmea_set_panel_feat_pwm - enable or disable high pwm mode
 * @ctx: gs_panel struct
 *
 * Description: the configs could possibly be overridden by frequency setting,
 * depending on FI mode.
 */
static void bmea_set_panel_feat_pwm(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	const unsigned long *feat = ctx->sw_status.feat;

	GS_DCS_BUF_ADD_CMD(dev, 0x83, test_bit(FEAT_PWM_HIGH, feat) ? 0x10 : 0x00);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x05, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, test_bit(FEAT_PWM_HIGH, feat) ? 0x28 : 0x00);
}

/**
 * bmea_set_panel_feat - configure panel features
 * @ctx: gs_panel struct
 * @pmode: gs_panel_mode struct, target panel mode
 * @idle_vrefresh: target vrefresh rate in auto mode, 0 if disabling auto mode
 * @enforce: force to write all of registers even if no feature state changes
 *
 * Configure panel features based on the context.
 * Note: bmea_set_panel_feat_xxx() should cache commands only while
 *       bmea_set_panel_feat() aggregates and sends the commands, and update
 *       hw_status. DO NOT update them in bmea_set_panel_feat_xxx().
 */
static void bmea_set_panel_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
				bool enforce)
{
	struct device *dev = ctx->dev;
	struct gs_panel_status *sw_status = &ctx->sw_status;
	struct gs_panel_status *hw_status = &ctx->hw_status;
	struct bmea_panel *spanel = to_spanel(ctx);
	unsigned long *feat = sw_status->feat;
	enum irc_mode irc_mode = sw_status->irc_mode;
	u32 idle_vrefresh = sw_status->idle_vrefresh;
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 te_freq = gs_drm_mode_te_freq(&pmode->mode);
	bool is_vrr = gs_is_vrr_mode(pmode);
	bool irc_mode_changed, idle_vrefresh_changed, vrefresh_changed, te_freq_changed;
	DECLARE_BITMAP(changed_feat, FEAT_MAX);

	/* override settings if vrr */
	if (is_vrr) {
		if (te_freq > vrefresh && idle_vrefresh > 1)
			dev_warn(dev, "te might be gated (te=%u vrefresh=%u idle_vrefresh=%u)\n",
				 te_freq, vrefresh, idle_vrefresh);

		if (!test_bit(FEAT_FRAME_AUTO, feat)) {
			vrefresh = idle_vrefresh ? idle_vrefresh : 1;
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

	snprintf(spanel->trace_msg, sizeof(spanel->trace_msg),
		 "feat: hbm=%u irc=%u h_pwm=%u fi=%u@a,%u@m ee=%u rr=%3u-%3u@%3u",
		 test_bit(FEAT_HBM, feat), irc_mode, test_bit(FEAT_PWM_HIGH, feat),
		 test_bit(FEAT_FRAME_AUTO, feat), test_bit(FEAT_FRAME_MANUAL_FI, feat),
		 test_bit(FEAT_EARLY_EXIT, feat), idle_vrefresh ? idle_vrefresh : vrefresh,
		 drm_mode_vrefresh(&pmode->mode), te_freq);
	dev_dbg(dev, "%s\n", spanel->trace_msg);
	PANEL_ATRACE_BEGIN(spanel->trace_msg);

	/* Unlock */
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);

	/* TE Settings */
	sw_status->te.freq_hz = te_freq;
	if (test_bit(FEAT_EARLY_EXIT, changed_feat) || test_bit(FEAT_PWM_HIGH, changed_feat) ||
	    te_freq_changed)
		hw_status->te.option = bmea_set_panel_feat_te(ctx, is_vrr, te_freq);

	/*
	 * High PWM mode: enable or disable
	 *
	 * Description: the configs could possibly be overridden by frequency setting,
	 * depending on FI mode.
	 */
	if (test_bit(FEAT_PWM_HIGH, changed_feat))
		bmea_set_panel_feat_pwm(ctx);

	if (test_bit(FEAT_EARLY_EXIT, changed_feat) || vrefresh_changed || te_freq_changed)
		bmea_set_panel_feat_early_exit(ctx, vrefresh, te_freq);

	/*
	 * Manual FI: enable or disable manual mode FI
	 */
	if (test_bit(FEAT_FRAME_MANUAL_FI, changed_feat))
		bmea_set_panel_feat_manual_mode_fi(ctx, test_bit(FEAT_FRAME_MANUAL_FI, feat));

	/*
	 * Frequency setting: FI, frequency, idle frequency
	 */
	if (test_bit(FEAT_FRAME_AUTO, changed_feat) || test_bit(FEAT_PWM_HIGH, changed_feat) ||
	    idle_vrefresh_changed || vrefresh_changed)
		bmea_set_panel_feat_frequency(ctx, vrefresh, idle_vrefresh);

	/*
	 * HBM IRC setting
	 */
	if (irc_mode_changed) {
		bmea_set_panel_feat_hbm_irc(ctx, irc_mode);
		hw_status->irc_mode = irc_mode;
	}

	/* TSP Sync setting */
	if (enforce)
		bmea_set_panel_feat_tsp_sync(ctx);

	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	/* Lock */
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	PANEL_ATRACE_END(spanel->trace_msg);

	hw_status->vrefresh = vrefresh;
	hw_status->idle_vrefresh = idle_vrefresh;
	hw_status->te.freq_hz = te_freq;
	bitmap_copy(hw_status->feat, feat, FEAT_MAX);
}

/**
 * bmea_update_panel_feat - configure panel features with current refresh rate
 * @ctx: gs_panel struct
 * @enforce: force to write all of registers even if no feature state changes
 *
 * Configure panel features based on the context without changing current refresh rate
 * and idle setting.
 */
static void bmea_update_panel_feat(struct gs_panel *ctx, bool enforce)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	bmea_set_panel_feat(ctx, pmode, enforce);
}

static void bmea_update_refresh_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
				    const u32 idle_vrefresh)
{
	struct gs_panel_status *sw_status = &ctx->sw_status;

	/* TODO: b/308978878 - move refresh control logic to HWC */

	/*
	 * Skip idle update if going through RRS without refresh rate change. If
	 * we're switching resolution and refresh rate in the same atomic commit
	 * (MODE_RES_AND_RR_IN_PROGRESS), we shouldn't skip the update to
	 * ensure the refresh rate will be set correctly to avoid problems.
	 */
	if (ctx->mode_in_progress == MODE_RES_IN_PROGRESS) {
		dev_dbg(ctx->dev, "RRS in progress without RR change, skip mode update\n");
		notify_panel_mode_changed(ctx);
		return;
	}

	dev_dbg(ctx->dev, "mode: %s set idle_vrefresh: %u\n", pmode->mode.name, idle_vrefresh);

	if (!gs_is_vrr_mode(pmode)) {
		u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
		if (idle_vrefresh)
			set_bit(FEAT_FRAME_AUTO, sw_status->feat);
		else
			clear_bit(FEAT_FRAME_AUTO, sw_status->feat);
		if (vrefresh == 120 || vrefresh == 1 || idle_vrefresh)
			set_bit(FEAT_EARLY_EXIT, sw_status->feat);
		else
			clear_bit(FEAT_EARLY_EXIT, sw_status->feat);
	}
	sw_status->idle_vrefresh = idle_vrefresh;
	/*
	 * Note: when mode is explicitly set, panel performs early exit to get out
	 * of idle at next vsync, and will not back to idle until not seeing new
	 * frame traffic for a while. If idle_vrefresh != 0, try best to guess what
	 * panel_idle_vrefresh will be soon, and bmea_update_idle_state() in
	 * new frame commit will correct it if the guess is wrong.
	 */
	ctx->idle_data.panel_idle_vrefresh = idle_vrefresh;
	bmea_set_panel_feat(ctx, pmode, false);
	notify_panel_mode_changed(ctx);
#ifdef PANEL_FACTORY_BUILD
	/* Set and notify the frequency since changeable TE2 is used in the factory */
	bmea_set_te2_freq(ctx, drm_mode_vrefresh(&pmode->mode));
	notify_panel_te2_freq_changed(ctx, 0);
#endif

	dev_dbg(ctx->dev, "display state is notified of mode update\n");
}

static void bmea_change_frequency(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 idle_vrefresh = 0;

	if (vrefresh > ctx->op_hz) {
		/* resolution may have been changed without refresh rate change */
		if (ctx->mode_in_progress == MODE_RES_AND_RR_IN_PROGRESS)
			notify_panel_mode_changed(ctx);
		dev_err(ctx->dev, "invalid freq setting: op_hz=%u, vrefresh=%u\n", ctx->op_hz,
			vrefresh);
		return;
	}

	if (bmea_get_idle_mode(ctx, pmode) == GIDLE_MODE_ON_INACTIVITY)
		idle_vrefresh = bmea_get_min_idle_vrefresh(ctx, pmode);

	/* use current idle_vrefresh if vrr modes */
	if (gs_is_vrr_mode(pmode))
		idle_vrefresh = ctx->sw_status.idle_vrefresh;

	bmea_update_refresh_mode(ctx, pmode, idle_vrefresh);
	ctx->sw_status.te.freq_hz = gs_drm_mode_te_freq(&pmode->mode);

	dev_dbg(ctx->dev, "change to %u hz\n", vrefresh);
}

static void bmea_panel_idle_notification(struct gs_panel *ctx, u32 display_id, u32 vrefresh,
					 u32 idle_te_vrefresh)
{
	char event_string[64];
	char *envp[2] = { event_string, NULL };
	struct drm_device *dev = ctx->bridge.dev;

	if (!dev) {
		dev_warn(ctx->dev, "can't notify system of panel idle, drm_device is null\n");
	} else {
		scnprintf(event_string, sizeof(event_string), "PANEL_IDLE_ENTER=%u,%u,%u",
			  display_id, vrefresh, idle_te_vrefresh);
		kobject_uevent_env(&dev->primary->kdev->kobj, KOBJ_CHANGE, envp);
	}
}

static void bmea_wait_one_vblank(struct gs_panel *ctx)
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

static bool bmea_set_self_refresh(struct gs_panel *ctx, bool enable)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;
	struct bmea_panel *spanel = to_spanel(ctx);
	u32 idle_vrefresh;

	if (spanel->pending_skin_temp_handling && enable)
		bmea_send_burn_in_comp_cmds(ctx);

	if (unlikely(!pmode))
		return false;

	if (gs_is_vrr_mode(pmode)) {
		PANEL_ATRACE_INT_PID_FMT(enable, ctx->trace_pid,
					 "set_self_refresh[%s]", ctx->panel_model);
		return false;
	}

	dev_dbg(ctx->dev, "%s: %d\n", __func__, enable);

	/* self refresh is not supported in lp mode since that always makes use of early exit */
	if (pmode->gs_mode.is_lp_mode) {
		/* set 1Hz while self refresh is active, otherwise clear it */
		ctx->idle_data.panel_idle_vrefresh = enable ? 1 : 0;
		notify_panel_mode_changed(ctx);
		return false;
	}

	idle_vrefresh = bmea_get_min_idle_vrefresh(ctx, pmode);

	if (bmea_get_idle_mode(ctx, pmode) != GIDLE_MODE_ON_SELF_REFRESH) {
		/*
		 * if idle mode is on inactivity, may need to update the target fps for auto mode,
		 * or switch to manual mode if idle should be disabled (idle_vrefresh=0)
		 */
		if ((bmea_get_idle_mode(ctx, pmode) == GIDLE_MODE_ON_INACTIVITY) &&
		    (ctx->sw_status.idle_vrefresh != idle_vrefresh)) {
			bmea_update_refresh_mode(ctx, pmode, idle_vrefresh);
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
	bmea_update_refresh_mode(ctx, pmode, idle_vrefresh);

	if (idle_vrefresh) {
		const int vrefresh = drm_mode_vrefresh(&pmode->mode);

		bmea_panel_idle_notification(ctx, 0, vrefresh, 120);
	} else if (ctx->idle_data.panel_need_handle_idle_exit) {
		/*
		 * after exit idle mode with fixed TE at non-120hz, TE may still keep at 120hz.
		 * If any layer that already be assigned to DPU that can't be handled at 120hz,
		 * panel_need_handle_idle_exit will be set then we need to wait one vblank to
		 * avoid underrun issue.
		 */
		dev_dbg(ctx->dev, "wait one vblank after exit idle\n");
		bmea_wait_one_vblank(ctx);
	}

	PANEL_ATRACE_END(__func__);

	return true;
}

static void bmea_set_panel_lp_feat_te(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	enum bmea_panel_type panel_type = GET_PANEL_TYPE(ctx);
	bool dvt_and_after = (ctx->panel_rev_id.id <= PANEL_REVID_EVT1_1) ? 0 : 1;
	static const u8 fixed_te_settings[PANEL_TYPE_MAX][2][7] = {
		{ /* BZEA */
			{ 0xB9, 0xB2, 0x23, 0x02, 0xB2, 0x23, 0x02 }, /* EVT1.1 and before */
			{ 0xB9, 0xB3, 0x23, 0x02, 0xB3, 0x23, 0x02 }, /* DVT and after */
		},
		{ /* MTEA */
			{ 0xB9, 0xB9, 0xF3, 0x02, 0xB9, 0xF3, 0x02 }, /* EVT1.1 and before */
			{ 0xB9, 0xBA, 0xE3, 0x02, 0xBA, 0xE3, 0x02 }, /* DVT and after */
		},
	};

	/* Fixed TE */
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x29);
	/* TE width */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x08, 0xB9);
	GS_DCS_BUF_ADD_CMDLIST(dev, fixed_te_settings[panel_type][dvt_and_after]);

	ctx->hw_status.te.option = TEX_OPT_FIXED;
}

static void bmea_set_panel_lp_feat_freq(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	unsigned long *feat = ctx->sw_status.feat;
	struct gs_panel_status *sw_status = &ctx->sw_status;
	u32 idle_vrefresh = sw_status->idle_vrefresh;
	bool is_auto = (test_bit(FEAT_FRAME_AUTO, feat) || !gs_is_vrr_mode(pmode)) ? true : false;

	dev_dbg(dev, "%s: auto=%u rr=%u-%u\n", __func__, is_auto, idle_vrefresh,
		drm_mode_vrefresh(&pmode->mode));

	if (is_auto) {
		/* Default is 1 Hz */
		u8 val = 0x74;
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x7B, 0xBD);
		if (idle_vrefresh == 10)
			val = 0x08;
		else if (idle_vrefresh != 1)
			dev_warn(dev, "unsupported idle vrefresh %u for lp mode\n", idle_vrefresh);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, val);
		/* Step settings */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x7D, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x8D, 0xBD);
		if (idle_vrefresh == 10)
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x06, 0x00, 0x00, 0x00);
		else
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x06, 0x07, 0x00, 0x00);
		/* Auto mode */
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x05);
	} else {
		/* Manual mode */
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
		/* 30 Hz */
		GS_DCS_BUF_ADD_CMD(dev, 0x83, 0x18);
		/* No frame insertions */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x2A, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00);
	}

	ctx->hw_status.vrefresh = 30;
	ctx->hw_status.te.freq_hz = 30;
}

static void bmea_set_panel_lp_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;

	if (!pmode->gs_mode.is_lp_mode)
		return;

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);

	bmea_set_panel_lp_feat_te(ctx);
	bmea_set_panel_lp_feat_freq(ctx, pmode);

	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
}

static void bmea_update_refresh_ctrl_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	/**
	 * Setting changes could trigger a 120Hz frame. Check whether we need to adjust the timing
	 * of sending the commands in these conditions.
	 */
	if (gs_panel_refresh_ctrl_full_helper(ctx, pmode) && !pmode->gs_mode.is_lp_mode)
		bmea_check_command_timing_for_te2(ctx);

	if (pmode->gs_mode.is_lp_mode)
		bmea_set_panel_lp_feat(ctx, pmode);
	else
		bmea_set_panel_feat(ctx, pmode, false);
}

static void bmea_refresh_ctrl(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	const u32 ctrl = ctx->refresh_ctrl;

	PANEL_ATRACE_BEGIN(__func__);

	bmea_update_refresh_ctrl_feat(ctx, ctx->current_mode);

	if (ctrl & GS_PANEL_REFRESH_CTRL_FI_FRAME_COUNT_MASK) {
		/* TODO(b/323251635): parse frame count for inserting multiple frames */
		PANEL_ATRACE_BEGIN("insert_frame");
		dev_dbg(dev, "manually inserting frame\n");
		GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
		GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
		PANEL_ATRACE_END("insert_frame");
	}

	PANEL_ATRACE_END(__func__);
}

static void bmea_write_display_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	u8 val = BMEA_WRCTRLD_BCTRL_BIT;

	PANEL_ATRACE_BEGIN(__func__);
	if (pmode->gs_mode.is_lp_mode)
		val |= BMEA_WRCTRLD_LPM_BIT;
	else if (ctx->dimming_on)
		val |= BMEA_WRCTRLD_DIMMING_BIT;

	dev_dbg(dev, "wrctrld:0x%x, dimming: %u, aod: %u\n", val,
		ctx->dimming_on, pmode->gs_mode.is_lp_mode);

	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);
	PANEL_ATRACE_END(__func__);
}

#define BMEA_OPR_VAL_LEN 2
#define BMEA_MAX_OPR_VAL 0x7FF

/**
 * bmea_get_opr - get the panel's on-pixel ratio
 * @ctx: panel struct
 * @opr: output opr value as a percentage
 *
 * Get the on-pixel ratio from the panel's DDIC, which is a representation of
 * the current panel emmision load.
 */
static int bmea_get_opr(struct gs_panel *ctx, u8 *opr)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct device *dev = ctx->dev;
	u8 buf[BMEA_OPR_VAL_LEN] = { 0 };
	u16 val;
	int ret;

	PANEL_ATRACE_BEGIN(__func__);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_fc);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xB0, 0x01, 0x1A, 0x63);
	ret = mipi_dsi_dcs_read(dsi, 0x63, buf, BMEA_OPR_VAL_LEN);
	GS_DCS_BUF_ADD_CMDLIST(dev, lock_cmd_fc);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
	PANEL_ATRACE_END(__func__);

	if (ret != BMEA_OPR_VAL_LEN) {
		dev_warn(dev, "Failed to read OPR (%d)\n", ret);
		return ret;
	}

	val = (buf[0] << 8) | buf[1];
	*opr = DIV_ROUND_CLOSEST(val * 100, BMEA_MAX_OPR_VAL);

	return 0;
}

static void bmea_disable_acl_mode(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct gs_panel_status *hw_status = &ctx->hw_status;

	if (hw_status->acl_mode != ACL_OFF) {
		GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x55, 0x00);
		hw_status->acl_mode = ACL_OFF;
		dev_info(dev, "set acl_mode off\n");
	}
}

static u8 get_acl_mode_setting(enum gs_acl_mode acl_mode)
{
	/*
	 * BZEA ACL mode and setting:
	 *    NORMAL     - 10%   (0x01)
	 *    ENHANCED   - 15%   (0x02)
	 */

	switch (acl_mode) {
	case ACL_OFF:
		return 0x00;
	case ACL_NORMAL:
		return 0x01;
	case ACL_ENHANCED:
		return 0x02;
	}
}

#define BMEA_ZA_THRESHOLD_OPR 85
#define BMEA_ACL_ENHANCED_THRESHOLD_DBV 3726
/* Manage the ACL settings to DDIC that consider the dbv and opr value */
static void bmea_acl_modes_manager(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct gs_panel_status *sw_status = &ctx->sw_status;
	struct gs_panel_status *hw_status = &ctx->hw_status;

	/* Check if ACL can be enabled based on conditions */
	bool can_enable_acl = hw_status->dbv >= BMEA_ACL_ENHANCED_THRESHOLD_DBV;
	u8 opr;
	u8 target_acl_state;
	bool update_acl_settings;

	if (!can_enable_acl) {
		bmea_disable_acl_mode(ctx);
		return;
	}

	/* Check if ACL settings can be written based on conditions */
	if (!bmea_get_opr(ctx, &opr)) {
		update_acl_settings = (opr > BMEA_ZA_THRESHOLD_OPR);
	} else {
		dev_warn(ctx->dev, "Unable to update acl mode\n");
		return;
	}

	if (update_acl_settings) {
		if (sw_status->acl_mode == hw_status->acl_mode) {
			dev_dbg(dev, "skip updating acl_mode\n");
			return;
		}
		target_acl_state = get_acl_mode_setting(sw_status->acl_mode);
		GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x55, target_acl_state);
		hw_status->acl_mode = sw_status->acl_mode;
		dev_dbg(dev, "set acl : %d, opr : %u\n", target_acl_state, opr);
	} else {
		bmea_disable_acl_mode(ctx);
	}
}

/* updated za when acl mode changed */
static void bmea_set_acl_mode(struct gs_panel *ctx, enum gs_acl_mode mode)
{
	bool can_enable_acl = ctx->hw_status.dbv >= BMEA_ACL_ENHANCED_THRESHOLD_DBV;

	ctx->sw_status.acl_mode = mode;

	if (can_enable_acl) {
		if (ctx->sw_status.acl_mode != ctx->hw_status.acl_mode)
			bmea_acl_modes_manager(ctx);
	} else
		bmea_disable_acl_mode(ctx);
}

static int bmea_set_brightness(struct gs_panel *ctx, u16 br)
{
	int ret;
	u16 brightness;

	if (ctx->current_mode->gs_mode.is_lp_mode) {
		if (gs_panel_has_func(ctx, set_binned_lp))
			ctx->desc->gs_panel_func->set_binned_lp(ctx, br);
		return 0;
	}

	if (!br)
		return 0;

	brightness = swab16(br);
	bmea_check_command_timing_for_te2(ctx);
	ret = gs_dcs_set_brightness(ctx, brightness);
	if (!ret) {
		ctx->hw_status.dbv = br;
		bmea_set_acl_mode(ctx, ctx->sw_status.acl_mode);
	}

	return ret;
}

static void bmea_wait_for_vsync_done(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	PANEL_ATRACE_BEGIN(__func__);
	gs_panel_wait_for_vsync_done(ctx, pmode->gs_mode.te_usec,
				     GS_VREFRESH_TO_PERIOD_USEC(ctx->hw_status.vrefresh));
	PANEL_ATRACE_END(__func__);
}

static void bmea_enforce_manual_and_peak(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	if (!ctx->current_mode)
		return;

	dev_dbg(dev, "%s\n", __func__);

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	/* manual mode */
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
	/* peak refresh rate */
	if (ctx->current_mode->gs_mode.is_lp_mode) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0x83);
		GS_DCS_BUF_ADD_CMD(dev, 0x83, 0x00);
	} else {
		GS_DCS_BUF_ADD_CMD(dev, 0x83, 0x00);
	}
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
}

static const struct gs_dsi_cmd bmea_set_lp_cmds[] = {
	GS_DSI_CMDLIST(unlock_cmd_f0),
	/* HLPM Setting */
	GS_DSI_CMD(0xB0, 0x00, 0x05, 0xBD),
	GS_DSI_CMD(0xBD, 0x3C, 0xC2),
	/* Enable early exit */
	GS_DSI_CMD(0xB0, 0x00, 0x03, 0xBD),
	GS_DSI_CMD(0xBD, 0x65),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xB0, 0x00, 0x20, 0xBD),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xBD, 0x00, 0x00),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xB0, 0x00, 0x24, 0xBD),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xBD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
	GS_DSI_CMDLIST(lock_cmd_f0),
};
static DEFINE_GS_CMDSET(bmea_set_lp);

static void bmea_set_lp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	const u16 brightness = gs_panel_get_brightness(ctx);

	dev_dbg(dev, "%s\n", __func__);

	PANEL_ATRACE_BEGIN(__func__);
	bmea_wait_for_vsync_done(ctx, ctx->current_mode);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	/* Manual Mode */
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
	if (ctx->dimming_on)
		GS_DCS_BUF_ADD_CMDLIST(dev, aod_on_smooth);
	else
		GS_DCS_BUF_ADD_CMDLIST(dev, aod_on_normal);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	gs_panel_send_cmdset(ctx, &bmea_set_lp_cmdset);
	bmea_update_refresh_ctrl_feat(ctx, pmode);
	gs_panel_set_binned_lp_helper(ctx, brightness);

	ctx->sw_status.te.freq_hz = 30;
	ctx->sw_status.te.option = TEX_OPT_FIXED;

	PANEL_ATRACE_END(__func__);

	dev_info(dev, "enter %dhz LP mode\n", drm_mode_vrefresh(&pmode->mode));
}

static void bmea_set_nolp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;

	dev_dbg(dev, "%s\n", __func__);

	PANEL_ATRACE_BEGIN(__func__);
	bmea_wait_for_vsync_done(ctx, ctx->current_mode);
	usleep_range(5000, 5000 + 100);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	/* Manual Mode */
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
	GS_DCS_BUF_ADD_CMDLIST(dev, aod_off);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x05, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, test_bit(FEAT_PWM_HIGH, ctx->sw_status.feat) ? 0x28 : 0x00);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
	bmea_update_refresh_ctrl_feat(ctx, pmode);
	bmea_set_panel_feat(ctx, pmode, true);
	bmea_write_display_mode(ctx, pmode);
	bmea_change_frequency(ctx, pmode);

	PANEL_ATRACE_END(__func__);

	dev_info(dev, "exit LP mode\n");
}

static void bmea_pre_update_ffc(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	dev_dbg(dev, "disabling FFC\n");

	PANEL_ATRACE_BEGIN(__func__);

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_fc);
	/* FFC off */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x3E, 0xC5);
	GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x10);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_fc);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	PANEL_ATRACE_END(__func__);

	ctx->ffc_en = false;
}

static void mtea_update_ffc(struct gs_panel *ctx, unsigned int hs_clk_mbps)
{
	struct device *dev = ctx->dev;

	dev_info(dev, "updating mtea FFC for hs_clk_mbps=%d\n", hs_clk_mbps);

	PANEL_ATRACE_BEGIN(__func__);

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_fc);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x3E, 0xC5);
	/* Apply the parameters to adapt outputs 1372.8 & 1348 Mbps */
	if (hs_clk_mbps == MTEA_MIPI_DSI_FREQ_MBPS_DEFAULT)
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x11, 0x10, 0x50, 0x05, 0x41, 0xF8);
	else /* MTEA_MIPI_DSI_FREQ_MBPS_ALTERNATIVE */
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x11, 0x10, 0x50, 0x05, 0x43, 0x2E);
	GS_DCS_BUF_ADD_CMDLIST(dev, lock_cmd_fc);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	PANEL_ATRACE_END(__func__);
}

static bool bzea_is_panel_alt_osc(struct gs_panel *ctx)
{
	if (ctx->panel_rev_id.id >= PANEL_REVID_EVT1)
		return true;

	if (ctx->panel_id == PANEL_ID_INVALID_VALUE) {
		dev_err(ctx->dev, "failed to get panel id\n");
		return false;
	}

	return ctx->panel_id & BMEA_OSC_DOE_CODE;
}

static void bzea_update_ffc(struct gs_panel *ctx, unsigned int hs_clk_mbps)
{
	struct device *dev = ctx->dev;

	dev_info(dev, "updating bzea FFC for hs_clk_mbps=%d\n", hs_clk_mbps);

	PANEL_ATRACE_BEGIN(__func__);

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_fc);
	/* OSC is changed in DVT1 */
	if (ctx->panel_rev_id.id >= PANEL_REVID_DVT1) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x42, 0xC5);
		/* Apply the parameters to adapt outputs 1397.6 & 1418.4 Mbps */
		if (hs_clk_mbps == BZEA_MIPI_DSI_FREQ_MBPS_DEFAULT)
			GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x40, 0xCC);
		else /* BZEA_MIPI_DSI_FREQ_MBPS_ALTERNATIVE */
			GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x3F, 0xD9);
	} else {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x44, 0xC5);
		if (hs_clk_mbps == BZEA_MIPI_DSI_FREQ_MBPS_DEFAULT)
			GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x3A, 0xEF);
		else /* BZEA_MIPI_DSI_FREQ_MBPS_ALTERNATIVE */
			GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x3A, 0x19);
	}
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x3E, 0xC5);
	GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x11, 0x10, 0x50, 0x05);
	GS_DCS_BUF_ADD_CMDLIST(dev, lock_cmd_fc);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	PANEL_ATRACE_END(__func__);
}

static void bmea_update_ffc(struct gs_panel *ctx, unsigned int hs_clk_mbps)
{
	struct device *dev = ctx->dev;

	dev_dbg(dev, "hs_clk_mbps: current=%d, target=%d\n", ctx->dsi_hs_clk_mbps,
		hs_clk_mbps);

	/* disable FFC for bzea proto1/proto1.1 */
	if (GET_PANEL_TYPE(ctx) == PANEL_TYPE_BZEA && !bzea_is_panel_alt_osc(ctx))
		return;

	if (ctx->dsi_hs_clk_mbps == hs_clk_mbps && ctx->ffc_en)
		return;

	if (GET_PANEL_TYPE(ctx) == PANEL_TYPE_MTEA) {
		if (hs_clk_mbps != MTEA_MIPI_DSI_FREQ_MBPS_DEFAULT &&
		    hs_clk_mbps != MTEA_MIPI_DSI_FREQ_MBPS_ALTERNATIVE) {
			dev_warn(dev, "invalid hs_clk_mbps=%d for mtea FFC\n", hs_clk_mbps);
			return;
		} else {
			mtea_update_ffc(ctx, hs_clk_mbps);
		}
	} else { /* PANEL_TYPE_BZEA */
		if (hs_clk_mbps != BZEA_MIPI_DSI_FREQ_MBPS_DEFAULT &&
		    hs_clk_mbps != BZEA_MIPI_DSI_FREQ_MBPS_ALTERNATIVE) {
			dev_warn(dev, "invalid hs_clk_mbps=%d for bzea FFC\n", hs_clk_mbps);
			return;
		} else {
			bzea_update_ffc(ctx, hs_clk_mbps);
		}
	}

	ctx->dsi_hs_clk_mbps = hs_clk_mbps;
	ctx->ffc_en = true;
}

static void bmea_set_ssc_en(struct gs_panel *ctx, bool enabled)
{
	struct device *dev = ctx->dev;
	const bool ssc_mode_update = ctx->ssc_en != enabled;

	if (GET_PANEL_TYPE(ctx) != PANEL_TYPE_BZEA)
		return;

	if (!ssc_mode_update) {
		dev_dbg(ctx->dev, "ssc_mode skip update\n");
		return;
	}

	ctx->ssc_en = enabled;

	PANEL_ATRACE_BEGIN("%s(%d)", __func__, enabled);

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_fc);
	/* SSC setting */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x6E, 0xC5);
	if (enabled)
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x07, 0x7F, 0x00, 0x00);
	else
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x04);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_fc);

	PANEL_ATRACE_END(__func__);

	dev_info(dev, "ssc_mode=%d\n", enabled);
}

static const struct gs_dsi_cmd bmea_init_cmds[] = {
	/* Enable TE */
	GS_DSI_CMD(MIPI_DCS_SET_TEAR_ON),
	/* Sleep out */
	GS_DSI_DELAY_CMD(120, MIPI_DCS_EXIT_SLEEP_MODE),

	GS_DSI_CMDLIST(unlock_cmd_f0),
	GS_DSI_CMDLIST(unlock_cmd_fc),
	/* Enable GRAM collision detection */
	GS_DSI_CMD(0xE5, 0x1D),
	GS_DSI_CMD(0xB0, 0x00, 0x01, 0xF8),
	GS_DSI_CMD(0xF8, 0x04),
	/* RETENTION Off */
	GS_DSI_CMD(0xB0, 0x00, 0x9F, 0x62),
	GS_DSI_CMD(0x62, 0xFF, 0xFF, 0xFF),
	GS_DSI_CMD(0xB0, 0x00, 0x02, 0xC4),
	GS_DSI_CMD(0xC4, 0x00),
	/* Porch CLK_DC Off */
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xB0, 0x00, 0x9E, 0xB7),
	GS_DSI_REV_CMD(PANEL_REV_LT(PANEL_REV_EVT1), 0xB7, 0x00, 0x00),
	GS_DSI_CMDLIST(panel_update),
	GS_DSI_CMDLIST(lock_cmd_fc),
	GS_DSI_CMDLIST(lock_cmd_f0),
};
static DEFINE_GS_CMDSET(bmea_init);

static const struct gs_dsi_cmd bmea_sap_cmds[] = {
	GS_DSI_CMDLIST(unlock_cmd_f0),
	GS_DSI_CMD(0xB0, 0x00, 0x06, 0xF6),
	GS_DSI_CMD(0xF6, 0x38),
	GS_DSI_CMDLIST(lock_cmd_f0),
};
static DEFINE_GS_CMDSET(bmea_sap);

static const struct gs_dsi_cmd bmea_collision_detection_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(unlock_cmd_f0),
	GS_DSI_QUEUE_CMDLIST(unlock_cmd_fc),
	GS_DSI_QUEUE_CMD(0xE5, 0x1D),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x01, 0xF8),
	GS_DSI_QUEUE_CMD(0xF8, 0x04),
	GS_DSI_QUEUE_CMDLIST(lock_cmd_fc),
	GS_DSI_FLUSH_CMDLIST(lock_cmd_f0),
};
static DEFINE_GS_CMDSET(bmea_collision_detection);

static void bmea_set_scaler_settings(struct gs_panel *ctx, bool is_fhd)
{
	struct device *dev = ctx->dev;
	enum bmea_panel_type panel_type = GET_PANEL_TYPE(ctx);
	int res_index = is_fhd ? 0 : 1;

	static const u8 column_settings[PANEL_TYPE_MAX][NUM_SUPPORTED_RESOLUTIONS][5] = {
		{
			{ MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37 },
			{ MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0xFF },
		},
		{
			{ MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37 },
			{ MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x05, 0x3F },
		},
	};

	static const u8 page_settings[PANEL_TYPE_MAX][NUM_SUPPORTED_RESOLUTIONS][5] = {
		{
			{ MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x69 },
			{ MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x0B, 0x27 },
		},
		{
			{ MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x63 },
			{ MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x0B, 0xAF },
		},
	};

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMD(dev, 0xC3, is_fhd ? 0x33 : 0x02);
	GS_DCS_BUF_ADD_CMDLIST(dev, column_settings[panel_type][res_index]);
	GS_DCS_BUF_ADD_CMDLIST(dev, page_settings[panel_type][res_index]);
	if (panel_type == PANEL_TYPE_BZEA && ctx->panel_rev_id.id == PANEL_REVID_PROTO1) {
		/* Scaler setting - x1.405 */
		GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_fc);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0xC3);
		GS_DCS_BUF_ADD_CMD(dev, 0xC3, 0x98, 0x38, 0x6A);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x19, 0xC3);
		GS_DCS_BUF_ADD_CMD(dev, 0xC3, 0xD8, 0x00, 0x00, 0xD8, 0x05, 0xBD);
		GS_DCS_BUF_ADD_CMDLIST(dev, lock_cmd_fc);
	}
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
};

static void bmea_set_opec_settings(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	enum bmea_panel_type panel_type = GET_PANEL_TYPE(ctx);

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_fc);
	/* OPEC IP workaround */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x04, 0x66);
	GS_DCS_BUF_ADD_CMD(dev, 0x66, 0x0B, 0x00, 0xD8, 0xD8, 0xD8, 0xD8, 0x40);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x91, 0x68);
	if ((panel_type == PANEL_TYPE_BZEA && (ctx->panel_rev_id.id == PANEL_REVID_EVT1 ||
	    ctx->panel_rev_id.id == PANEL_REVID_EVT1_1)) || panel_type == PANEL_TYPE_MTEA) {
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x03, 0xE8, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F,
					0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x07, 0x6C, 0x7F, 0xFF, 0x7F,
					0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x00,
					0x00, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F,
					0xFF, 0x7F, 0xFF);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, 0x02, 0x68);
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x24);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, 0x09, 0x68);
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x2F);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, 0x10, 0x68);
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x1C);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, 0x17, 0x68);
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x27);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, 0x1E, 0x68);
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x28);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, 0x25, 0x68);
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x1C);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, 0x2C, 0x68);
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x22);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, 0x33, 0x68);
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x22);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x01, 0x3A, 0x68);
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x1B);
	}
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x63, 0xAB);
	GS_DCS_BUF_ADD_CMD(dev, 0xAB, 0x85, 0xA0);
	GS_DCS_BUF_ADD_CMDLIST(dev, lock_cmd_fc);
	/* MFD function */
	if (ctx->panel_rev_id.id >= PANEL_REVID_DVT1) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0x83);
		GS_DCS_BUF_ADD_CMD(dev, 0x83, 0x08, 0x00, 0x0B,
			GET_PANEL_TYPE(ctx) == PANEL_TYPE_BZEA ? 0x4C : 0xD4);
		GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	}

	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
}

static void bmea_disable_retention(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_fc);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x9F, 0x62);
	GS_DCS_BUF_ADD_CMD(dev, 0x62, 0xFF, 0xFF, 0xFF);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x02, 0xC4);
	GS_DCS_BUF_ADD_CMD(dev, 0xC4, 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, lock_cmd_fc);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
}

static int bmea_enable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	const struct drm_display_mode *mode;
	const bool needs_init = !gs_is_panel_enabled(ctx);
	enum bmea_panel_type panel_type = GET_PANEL_TYPE(ctx);
	bool is_fhd;

	if (!pmode) {
		dev_err(dev, "no current mode set\n");
		return -EINVAL;
	}
	mode = &pmode->mode;
	is_fhd = (panel_type == PANEL_TYPE_BZEA) ? mode->hdisplay == BZEA_FHD_HDISPLAY :
						   mode->hdisplay == MTEA_FHD_HDISPLAY;

	dev_info(dev, "enabling using %s\n", is_fhd ? "fhd" : "wqhd");
	gs_panel_first_enable_helper(ctx);

	PANEL_ATRACE_BEGIN(__func__);


	/* wait TE falling for RRS since DSC and framestart must in the same VSYNC */
	if (ctx->mode_in_progress == MODE_RES_IN_PROGRESS ||
	    ctx->mode_in_progress == MODE_RES_AND_RR_IN_PROGRESS)
		bmea_wait_for_vsync_done(ctx, pmode);

	/* DSC related configuration */
	gs_dcs_write_dsc_config(dev, &pps_configs[panel_type][is_fhd ? 0 : 1]);

	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x9D, 0x01);

	if (needs_init) {
		gs_panel_send_cmdset(ctx, &bmea_init_cmdset);
		if (ctx->panel_rev_id.id == PANEL_REVID_EVT1)
			gs_panel_send_cmdset(ctx, &bmea_sap_cmdset);
		bmea_set_opec_settings(ctx);
		bmea_update_ffc(ctx,
			GET_PANEL_TYPE(ctx) == PANEL_TYPE_MTEA ? MTEA_MIPI_DSI_FREQ_MBPS_DEFAULT :
								 BZEA_MIPI_DSI_FREQ_MBPS_DEFAULT);
		bmea_te2_setting(ctx);
	}

	bmea_set_scaler_settings(ctx, is_fhd);

	if (pmode->gs_mode.is_lp_mode) {
		bmea_set_lp_mode(ctx, pmode);
		GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_SET_DISPLAY_ON);
	} else {
		bmea_update_refresh_ctrl_feat(ctx, pmode);
		bmea_update_panel_feat(ctx, true);
		bmea_write_display_mode(ctx, pmode); /* dimming */
		bmea_change_frequency(ctx, pmode);

		if (needs_init || (ctx->panel_state == GPANEL_STATE_BLANK)) {
			const u16 min_brightness = ctx->desc->brightness_desc->min_brightness;
			u16 brightness = max(ctx->bl->props.brightness, min_brightness);

			ctx->bl->props.brightness = brightness;
			GS_DCS_BUF_ADD_CMD(dev, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
						(brightness >> 8) & 0xFF,
						brightness & 0xFF);
			GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_SET_DISPLAY_ON);
		}
	}

	PANEL_ATRACE_END(__func__);

	return 0;
}

static int bmea_disable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	struct bmea_panel *spanel = to_spanel(ctx);
	int ret;

	dev_info(dev, "%s\n", __func__);

	/* skip disable sequence if going through modeset */
	if (ctx->panel_state == GPANEL_STATE_MODESET) {
		dev_dbg(dev, "Modeset in progress (%d), skip panel disable\n", ctx->panel_state);
		return 0;
	}

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
	spanel->prev_gram_collision_count = 0;

	/* set manual and peak before turning off display */
	bmea_enforce_manual_and_peak(ctx);

	GS_DCS_WRITE_DELAY_CMD(dev, 20, MIPI_DCS_SET_DISPLAY_OFF);

	if (ctx->panel_state == GPANEL_STATE_OFF)
		GS_DCS_WRITE_DELAY_CMD(dev, 100, MIPI_DCS_ENTER_SLEEP_MODE);

	return 0;
}

/*
 * 120hz auto mode takes at least 2 frames to start lowering refresh rate in addition to
 * time to next vblank. Use just over 2 frames time to consider worst case scenario
 */
#define EARLY_EXIT_THRESHOLD_US 17000

/**
 * bmea_update_idle_state - update panel auto frame insertion state
 * @ctx: panel struct
 *
 * - update timestamp of switching to manual mode in case its been a while since the
 *   last frame update and auto mode may have started to lower refresh rate.
 * - trigger early exit by command if it's changeable TE and no switching delay, which
 *   could result in fast 120 Hz boost and seeing 120 Hz TE earlier, otherwise disable
 *   auto refresh mode to avoid lowering frequency too fast.
 */
static void bmea_update_idle_state(struct gs_panel *ctx)
{
	s64 delta_us;
	struct bmea_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;

	if (gs_is_vrr_mode(ctx->current_mode))
		return;

	ctx->idle_data.panel_idle_vrefresh = 0;
	if (!test_bit(FEAT_FRAME_AUTO, ctx->sw_status.feat))
		return;

	delta_us = ktime_us_delta(ktime_get(), ctx->timestamps.last_commit_ts);
	if (delta_us < EARLY_EXIT_THRESHOLD_US) {
		dev_dbg(dev, "skip early exit. %lldus since last commit\n", delta_us);
		return;
	}

	/* triggering early exit causes a switch to 120hz */
	ctx->timestamps.last_mode_set_ts = ktime_get();

	PANEL_ATRACE_BEGIN(__func__);

	if (!ctx->idle_data.idle_delay_ms && spanel->force_changeable_te) {
		dev_dbg(dev, "sending early exit out cmd\n");
		GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
		GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
	} else {
		/* turn off auto mode to prevent panel from lowering frequency too fast */
		bmea_update_refresh_mode(ctx, ctx->current_mode, 0);
	}

	PANEL_ATRACE_END(__func__);
}

/**
 * define TRIGGER_DUMP_FOR_COLLISION_TH - threshold of triggering dumps for GRAM collision
 *
 * If the collision symptom lasts (or the accumulated time) over 200ms, i.e. 24 TEAR_CNT at 120Hz
 * (200/8.3=~24), set the flag, which will trigger dumps for debugging.
 */
#define TRIGGER_DUMP_FOR_COLLISION_TH 24
#define DDIC_MAX_COLLISION_CNT 255
#define TEAR_CNT_ADDR 0xC4
static void bmea_detect_gram_collision(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	u8 collision_cnt = 0;
	int ret;

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_fc);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0xC4);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xC4, 0x00);
	ret = mipi_dsi_dcs_read(dsi, TEAR_CNT_ADDR, &collision_cnt, 1);
	if (ret != 1) {
		dev_warn(dev, "Error reading TEAR_CNT (%pe)\n", ERR_PTR(ret));
	} else {
		struct bmea_panel *spanel = to_spanel(ctx);
		int cnt_diff = collision_cnt - spanel->prev_gram_collision_count;

		if (collision_cnt && cnt_diff) {
			if (cnt_diff > 0) {
				ctx->gram_collision_count += cnt_diff;
				set_bit(GS_PANEL_ERR_GRAM_COLLISION, ctx->panel_errors);

				dev_dbg(dev, "set bit for GRAM COLLISION\n");
				dev_err(dev,
					"GRAM collision found (underrun) read:%u inc:%d total:%u\n",
					collision_cnt, cnt_diff, ctx->gram_collision_count);
				dev_err(dev, "%s\n", spanel->trace_msg);
				PANEL_ATRACE_INSTANT("GRAM collision read:%u inc:%d total:%u",
						     collision_cnt, cnt_diff,
						     ctx->gram_collision_count);

				if (collision_cnt > TRIGGER_DUMP_FOR_COLLISION_TH)
					ctx->trigger_dumps_for_gram_collision = true;

				if (collision_cnt == DDIC_MAX_COLLISION_CNT) {
					/* reset TEAR_CNT */
					GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0xC4);
					GS_DCS_BUF_ADD_CMD(dev, 0xC4, 0x01);

					collision_cnt = 0;
					dev_dbg(dev, "TEAR_CNT reset for GRAM COLLISION\n");
				}
				spanel->prev_gram_collision_count = collision_cnt;
			} else {
				dev_warn(dev, "unexpected GRAM collision count %u (prev %u)\n",
					 collision_cnt, spanel->prev_gram_collision_count);
			}
		}
	}
}

static int bmea_detect_fault(struct gs_panel *ctx,
			     const struct gs_panel_detect_fault_desc *fault_desc,
			     bool irq_triggered)
{
	struct device *dev = ctx->dev;
	int ret;

	PANEL_ATRACE_BEGIN("bmea_detect_fault");

	if (irq_triggered)
		bmea_detect_gram_collision(ctx);
	else
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, unlock_cmd_f0);

	ret = gs_panel_detect_fault_helper(ctx, fault_desc, irq_triggered);

	if (irq_triggered)
		GS_DCS_BUF_ADD_CMDLIST(dev, lock_cmd_fc);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	PANEL_ATRACE_END("bmea_detect_fault");

	return ret;
}

static void bmea_commit_done(struct gs_panel *ctx)
{
	struct bmea_panel *spanel = to_spanel(ctx);

	if (ctx->current_mode->gs_mode.is_lp_mode)
		return;

	/* skip idle update if going through RRS */
	if (ctx->mode_in_progress == MODE_RES_IN_PROGRESS ||
	    ctx->mode_in_progress == MODE_RES_AND_RR_IN_PROGRESS) {
		dev_dbg(ctx->dev, "RRS in progress, skip commit done\n");
		return;
	}

	bmea_update_idle_state(ctx);

	/* TODO (b/370108151):  call bmea_acl_modes_manager(ctx) */

	if (spanel->pending_skin_temp_handling)
		bmea_send_burn_in_comp_cmds(ctx);
}

static void bmea_set_hbm_mode(struct gs_panel *ctx, enum gs_hbm_mode mode)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;
	struct gs_panel_status *sw_status = &ctx->sw_status;

	if (mode == ctx->hbm_mode)
		return;

	if (unlikely(!pmode))
		return;

	ctx->hbm_mode = mode;

	if (GS_IS_HBM_ON(mode)) {
		set_bit(FEAT_HBM, sw_status->feat);
		/* enforce IRC on for factory builds */
#ifndef PANEL_FACTORY_BUILD
		if (mode == GS_HBM_ON_IRC_ON)
			sw_status->irc_mode = IRC_FLAT_DEFAULT;
		else
			sw_status->irc_mode = IRC_FLAT_Z;
#endif
	} else {
		clear_bit(FEAT_HBM, sw_status->feat);
		sw_status->irc_mode = IRC_FLAT_DEFAULT;
	}

	bmea_update_panel_feat(ctx, false);
}

static void bmea_set_dimming(struct gs_panel *ctx, bool dimming_on)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	ctx->dimming_on = dimming_on;
	if (pmode->gs_mode.is_lp_mode) {
		dev_dbg(ctx->dev, "in lp mode, postpone dimming update to normal mode entry");
		return;
	}
	bmea_write_display_mode(ctx, pmode);
}

static void bmea_mode_set(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	bmea_change_frequency(ctx, pmode);
}

static bool bmea_is_mode_seamless_atomic(const struct gs_panel *ctx,
					 const struct gs_panel_mode *old_pmode,
					 const struct gs_panel_mode *new_pmode)
{
	const struct drm_display_mode *c = &old_pmode->mode;
	const struct drm_display_mode *n = &new_pmode->mode;

	/* seamless mode set can happen if active region resolution is same */
	return (c->vdisplay == n->vdisplay) && (c->hdisplay == n->hdisplay);
}

static int bmea_set_pwm_mode(struct gs_panel *ctx, enum gs_pwm_mode mode)
{
	struct device *dev = ctx->dev;

	if (mode == ctx->pwm_mode || ctx->panel_rev_id.id < PANEL_REVID_EVT1)
		return -EINVAL;

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

	bmea_update_panel_feat(ctx, false);

	PANEL_ATRACE_END("%s(%d)", __func__, mode);

	return 0;
}

static void bmea_get_panel_rev(struct gs_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	u8 build_code = (id & 0xFF00) >> 8;
	u8 rev = ((build_code & 0xE0) >> 3) | ((build_code & 0x0C) >> 2);

	gs_panel_get_panel_rev(ctx, rev);
}

static void bmea_handle_skin_temperature(struct gs_panel *ctx)
{
	if (ctx->idle_data.self_refresh_active) {
		bmea_send_burn_in_comp_cmds(ctx);
	} else {
		struct bmea_panel *spanel = to_spanel(ctx);

		spanel->pending_skin_temp_handling = true;
	}
}

static ssize_t bmea_get_color_data(struct gs_panel *ctx, char *buf, size_t buf_len)
{
	struct device *dev = ctx->dev;
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	int read_ret = -1;
	u8 read_len = ctx->desc->calibration_desc->color_cal[COLOR_DATA_TYPE_CIE].data_size;
	u32 read_addr = (ctx->panel_rev_id.id >= PANEL_REVID_EVT1) ? 0x1FC000 : 0x1FF000;
	u8 addr1, addr2, addr3;

	if (buf_len < read_len)
		return -EINVAL;

	PANEL_ATRACE_BEGIN(__func__);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
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

	/* Set read address */
	addr1 = (read_addr >> 16) & 0xFF;
	addr2 = (read_addr >> 8) & 0xFF;
	addr3 = read_addr & 0xFF;
	GS_DCS_BUF_ADD_CMD(dev, 0xC1, 0x6B, addr1, addr2, addr3, 0x0C, 0x00, read_len);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xC0, 0x03);
	usleep_range(4000, 4100);

	read_ret = mipi_dsi_dcs_read(dsi, 0x6E, buf, read_len);
	usleep_range(1000, 1100);

	GS_DCS_BUF_ADD_CMD(dev, 0xC1, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xC0, 0x03);
	usleep_range(1000, 1100);

	GS_DCS_BUF_ADD_CMD(dev, 0xC1, 0x01, 0x30, 0x02, 0x00, 0x08, 0x00, 0x00);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xC0, 0x03);
	usleep_range(15000, 15500);

	GS_DCS_BUF_ADD_CMD(dev, 0xC0, 0x0);
	GS_DCS_BUF_ADD_CMD(dev, 0xF1, 0xA5, 0xA5);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	PANEL_ATRACE_END(__func__);
	if (read_ret != read_len) {
		dev_warn(dev, "Unable to read DDIC CIE data (%d)\n", read_ret);
		return -EINVAL;
	}
	return read_ret;
}

static const u32 bmea_bl_range[] = { 13084 };

static void bmea_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct dentry *panel_root, *csroot;
	struct bmea_panel *spanel;

	if (!ctx)
		return;

	panel_root = debugfs_lookup("panel", root);
	if (!panel_root)
		return;

	csroot = debugfs_lookup("cmdsets", panel_root);
	if (!csroot)
		goto panel_out;

	spanel = to_spanel(ctx);

	gs_panel_debugfs_create_cmdset(csroot, &bmea_init_cmdset, "init");
	gs_panel_debugfs_create_cmdset(csroot, &bmea_set_lp_cmdset, "set_lp");
	debugfs_create_bool("force_changeable_te", 0644, panel_root, &spanel->force_changeable_te);
	debugfs_create_bool("force_changeable_te2", 0644, panel_root,
			    &spanel->force_changeable_te2);
	debugfs_create_bool("force_za_off", 0644, panel_root, &spanel->force_za_off);
	debugfs_create_u32("hw_acl_mode", 0644, panel_root, &ctx->hw_status.acl_mode);
	dput(csroot);
panel_out:
	dput(panel_root);
#endif
}

static void bmea_panel_init(struct gs_panel *ctx)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	if (ctx->panel_rev_id.id == PANEL_REVID_EVT1)
		gs_panel_send_cmdset(ctx, &bmea_sap_cmdset);
	bmea_set_opec_settings(ctx);
	bmea_disable_retention(ctx);
	gs_panel_send_cmdset(ctx, &bmea_collision_detection_cmdset);

	ctx->refresh_ctrl |= GS_PANEL_REFRESH_CTRL_EARLY_EXIT;
	bmea_update_refresh_ctrl_feat(ctx, pmode);
	ctx->hw_status.irc_mode = IRC_FLAT_DEFAULT;
#ifndef PANEL_FACTORY_BUILD
	/* default fixed TE2 240Hz */
	ctx->te2.option = TEX_OPT_FIXED;
	ctx->te2.freq_hz = 240;
#endif
	/* Update FFC parameters */
	bmea_update_ffc(ctx,
			GET_PANEL_TYPE(ctx) == PANEL_TYPE_MTEA ? MTEA_MIPI_DSI_FREQ_MBPS_DEFAULT :
								 BZEA_MIPI_DSI_FREQ_MBPS_DEFAULT);

	/* disable FFC for bzea proto1/proto1.1 */
	if (GET_PANEL_TYPE(ctx) == PANEL_TYPE_BZEA && !bzea_is_panel_alt_osc(ctx))
		bmea_pre_update_ffc(ctx);

	/* re-init panel to decouple bootloader settings */
	if (pmode) {
		dev_info(ctx->dev, "set mode: %s\n", pmode->mode.name);
		ctx->sw_status.idle_vrefresh = 0;
		bmea_set_panel_feat(ctx, pmode, true);
		bmea_change_frequency(ctx, pmode);
		bmea_te2_setting(ctx);
	}
}

static int bmea_panel_probe(struct mipi_dsi_device *dsi)
{
	struct bmea_panel *spanel;
	struct gs_panel *ctx;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	ctx = &spanel->base;

	ctx->op_hz = 120;
	ctx->pwm_mode = GS_PWM_RATE_STANDARD;
	ctx->hw_status.vrefresh = 60;
	ctx->hw_status.te.freq_hz = 60;
	ctx->hw_status.acl_mode = ACL_OFF;
	ctx->hw_status.dbv = 0;
	clear_bit(FEAT_ZA, ctx->hw_status.feat);
	spanel->reg_dump_for_gram_collision_triggered = false;
	spanel->prev_gram_collision_count = 0;

	return gs_dsi_panel_common_init(dsi, ctx);
}

static int bmea_panel_config(struct gs_panel *ctx)
{
	gs_panel_model_init(ctx, PROJECT, 0);

	return gs_panel_update_brightness_desc(&bmea_brightness_desc, bmea_brt_configs,
					       ARRAY_SIZE(bmea_brt_configs),
					       ctx->panel_rev_bitmask);
}

static const struct drm_panel_funcs bmea_drm_funcs = {
	.disable = bmea_disable,
	.unprepare = gs_panel_unprepare,
	.prepare = gs_panel_prepare_with_reset,
	.enable = bmea_enable,
	.get_modes = gs_panel_get_modes,
	.debugfs_init = bmea_debugfs_init,
};

static const struct gs_panel_funcs bmea_gs_funcs = {
	.set_brightness = bmea_set_brightness,
	.set_lp_mode = bmea_set_lp_mode,
	.set_nolp_mode = bmea_set_nolp_mode,
	.set_binned_lp = gs_panel_set_binned_lp_helper,
	.set_hbm_mode = bmea_set_hbm_mode,
	.set_dimming = bmea_set_dimming,
	.is_mode_seamless_atomic = bmea_is_mode_seamless_atomic,
	.mode_set = bmea_mode_set,
	.panel_init = bmea_panel_init,
	.panel_config = bmea_panel_config,
	.get_panel_rev = bmea_get_panel_rev,
	.get_te2_edges = gs_panel_get_te2_edges_helper,
	.set_te2_edges = gs_panel_set_te2_edges_helper,
	.update_te2 = bmea_update_te2,
	.commit_done = bmea_commit_done,
	.set_self_refresh = bmea_set_self_refresh,
	.refresh_ctrl = bmea_refresh_ctrl,
	.read_serial = gs_panel_read_slsi_ddic_id,
	.set_acl_mode = bmea_set_acl_mode,
	.handle_skin_temperature = bmea_handle_skin_temperature,
	.pre_update_ffc = bmea_pre_update_ffc,
	.set_ssc_en = bmea_set_ssc_en,
	.update_ffc = bmea_update_ffc,
	.set_te2_freq = bmea_set_te2_freq,
	.get_te2_freq = bmea_get_te2_freq,
	.set_te2_option = bmea_set_te2_option,
	.get_te2_option = bmea_get_te2_option,
	.get_color_data = bmea_get_color_data,
	.set_pwm_mode = bmea_set_pwm_mode,
	.detect_fault = bmea_detect_fault,
};

static struct gs_panel_reg_ctrl_desc bmea_reg_ctrl_desc = {
	.reg_ctrl_enable = {
		{PANEL_REG_ID_VCI, 1},
		{PANEL_REG_ID_VDDD, 10},
	},
	.reg_ctrl_disable = {
		{PANEL_REG_ID_VDDD, 10},
		{PANEL_REG_ID_VCI, 1},
	},
};

static struct gs_panel_calibration_desc bmea_calibration_desc = {
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

static const struct gs_panel_detect_fault_desc bmea_fault_desc = {
	.errfg_reg = 0xEE,
	.errfg_len = 2,
	.dsi_err_reg = 0xE9,
	.vgh_mask = BIT(8),
	.vlin1_mask = BIT(6),
	.dsi_err_mask = BIT(0),
	/**
	 * The err_fg pin is supported and may keep high in some worse cases. Adjust the value
	 * if needed.
	 */
	.detect_interval_ms = 5000,
	.panel_errors_mask = BIT(GS_PANEL_ERR_DSI_READ_FAILURE) | BIT(GS_PANEL_ERR_GRAM_COLLISION),
};

#define DEFINE_BMEA_PANEL_DESC(NAME, MODES, LP_MODES, BINNED_LP, MIPI_DSI_FREQ)			\
static struct gs_panel_desc NAME = {								\
	.data_lane_cnt = 4,									\
	.dbv_extra_frame = true,								\
	.brightness_desc = &bmea_brightness_desc,						\
	.calibration_desc = &bmea_calibration_desc,						\
	.reg_ctrl_desc = &bmea_reg_ctrl_desc,							\
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */			\
	.hdr_formats = BIT(2) | BIT(3),								\
	.bl_range = bmea_bl_range,								\
	.bl_num_ranges = ARRAY_SIZE(bmea_bl_range),						\
	.modes = &MODES,									\
	.lp_modes = &LP_MODES,									\
	.binned_lp = BINNED_LP,									\
	.num_binned_lp = ARRAY_SIZE(BINNED_LP),							\
	.rr_switch_duration = 1,								\
	.has_off_binned_lp_entry = false,							\
	.is_idle_supported = true,								\
	.panel_func = &bmea_drm_funcs,								\
	.gs_panel_func = &bmea_gs_funcs,							\
	.default_dsi_hs_clk_mbps = MIPI_DSI_FREQ,						\
	.reset_timing_ms = { -1, -1, 10, 10 },							\
	/**											\
	 * While the proximity is active, we will set the min vrefresh to 30Hz with auto	\
	 * frame insertion. Thus when the display is idle, we will have the refresh rate	\
	 * change from 120Hz to 30Hz. According to the measurement, the pattern is: 3x120Hz 	\
	 * frame > 1x60Hz frame > 30Hz. With additional tolerance due to scheduler in the	\
	 * kernel, the delay of notification is estimated to be ~50ms.				\
	 */											\
	.notify_te2_freq_changed_work_delay_ms = 50,						\
	.fault_desc = &bmea_fault_desc,								\
}

DEFINE_BMEA_PANEL_DESC(gs_bzea, bzea_modes, bzea_lp_modes, bzea_binned_lp,
		       BZEA_MIPI_DSI_FREQ_MBPS_DEFAULT);
DEFINE_BMEA_PANEL_DESC(gs_mtea, mtea_modes, mtea_lp_modes, mtea_binned_lp,
		       MTEA_MIPI_DSI_FREQ_MBPS_DEFAULT);

static const struct of_device_id gs_panel_of_match[] = {
	{ .compatible = "google,gs-bzea", .data = &gs_bzea },
	{ .compatible = "google,gs-mtea", .data = &gs_mtea },
	{ }
};
MODULE_DEVICE_TABLE(of, gs_panel_of_match);

static struct mipi_dsi_driver gs_panel_driver = {
	.probe = bmea_panel_probe,
	.remove = gs_dsi_panel_common_remove,
	.driver = {
		.name = "panel-gs-bmea",
		.of_match_table = gs_panel_of_match,
	},
};
module_mipi_dsi_driver(gs_panel_driver);

MODULE_AUTHOR("Jeremy DeHaan <jdehaan@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google BMEA panel driver");
MODULE_LICENSE("Dual MIT/GPL");
