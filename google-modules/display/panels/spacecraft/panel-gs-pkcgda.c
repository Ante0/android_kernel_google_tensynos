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
 * enum pkcgda_panel_type - panel types supported by pkcgda driver
 * @PANEL_TYPE_CGYDA: CGYDA panel
 * @PANEL_TYPE_PKKDA: PKKDA panel
 * @PANEL_TYPE_MAX: placeholder, counter for number of supported panels
 */
enum pkcgda_panel_type {
	PANEL_TYPE_CGYDA = 0,
	PANEL_TYPE_PKKDA,
	PANEL_TYPE_MAX,
};

#define NUM_SUPPORTED_RESOLUTIONS 2

static struct gs_panel_desc gs_cgyda;
#define GET_PANEL_TYPE(ctx) ((ctx->desc == &gs_cgyda) ? PANEL_TYPE_CGYDA : PANEL_TYPE_PKKDA)

/**
 * struct pkcgda_panel - panel specific info
 *
 * This struct maintains pkcgda panel specific info. The variables with the prefix hw_ keep
 * track of the features that were actually committed to hardware, and should be modified
 * after sending cmds to panel, i.e., updating hw state.
 */
struct pkcgda_panel {
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
	 * @prev_gram_collision_count: previous count of GRAM collision for calculating the
	 *                             increased count
	 */
	u8 prev_gram_collision_count;
	/** @trace_msg: the message which records panel settings in the trace */
	char trace_msg[64];
};

#define to_spanel(ctx) container_of(ctx, struct pkcgda_panel, base)

static struct drm_dsc_config pps_configs[PANEL_TYPE_MAX][NUM_SUPPORTED_RESOLUTIONS] = {
	{
		/* CGYDA DSCv1.2a 1080x2410 */
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
		/* CGYDA DSCv1.2a 1280x2856 */
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
		/* PKKDA DSCv1.2a 1080x2404 */
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
		/* PKKDA DSCv1.2a 1344x2992 */
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

#define PKCGDA_WRCTRLD_LPM_BIT 0x04
#define PKCGDA_WRCTRLD_DIMMING_BIT 0x08
#define PKCGDA_WRCTRLD_BCTRL_BIT 0x20
#define PKCGDA_WRCTRLD_HBM_BIT 0xC0

#define PKCGDA_TEAR_CNT_ADDR 0xC4

#define PKCGDA_TE2_CHANGEABLE 0x04
#define PKCGDA_TE2_FIXED_120HZ 0x29
#define PKCGDA_TE2_FIXED_240HZ 0x41
#define PKCGDA_TE2_RISING_EDGE_OFFSET 0x24
#define CGYDA_TE2_FALLING_EDGE_OFFSET 0x59
#define PKKDA_TE2_FALLING_EDGE_OFFSET 0x5B

#define PKKDA_TE_USEC_480HZ 275
#define PKKDA_TE_USEC_HS 373
#define CGYDA_TE_USEC_HS 377
#define PKKDA_TE_USEC_NS 551
#define CGYDA_TE_USEC_NS 552
#define PKKDA_TE_USEC_AOD 1098
// TODO: b/445573648 update after validating the init sequence
#define CGYDA_TE_USEC_AOD 1287

#define PKKDA_MIPI_DSI_FREQ_MBPS_DEFAULT 1368
#define PKKDA_MIPI_DSI_FREQ_MBPS_ALTERNATIVE 1346
#define CGYDA_MIPI_DSI_FREQ_MBPS_DEFAULT 1390
#define CGYDA_MIPI_DSI_FREQ_MBPS_ALTERNATIVE 1410

// "PKCGDA" is longer than PROJECT_CODE_MAX
#define PROJECT "PCDA"

static const u16 CGYDA_FHD_HDISPLAY = 1080, CGYDA_FHD_VDISPLAY = 2410;
static const u16 CGYDA_FHD_HFP = 80, CGYDA_FHD_HSA = 24, CGYDA_FHD_HBP = 36;
static const u16 CGYDA_FHD_VFP = 12, CGYDA_FHD_VSA = 4, CGYDA_FHD_VBP = 24;

static const u16 CGYDA_WQHD_HDISPLAY = 1280, CGYDA_WQHD_VDISPLAY = 2856;
static const u16 CGYDA_WQHD_HFP = 80, CGYDA_WQHD_HSA = 24, CGYDA_WQHD_HBP = 46;
static const u16 CGYDA_WQHD_VFP = 12, CGYDA_WQHD_VSA = 4, CGYDA_WQHD_VBP = 28;

static const u16 PKKDA_FHD_HDISPLAY = 1080, PKKDA_FHD_VDISPLAY = 2404;
static const u16 PKKDA_FHD_HFP = 80, PKKDA_FHD_HSA = 24, PKKDA_FHD_HBP = 36;
static const u16 PKKDA_FHD_VFP = 16, PKKDA_FHD_VSA = 4, PKKDA_FHD_VBP = 26;

static const u16 PKKDA_WQHD_HDISPLAY = 1344, PKKDA_WQHD_VDISPLAY = 2992;
static const u16 PKKDA_WQHD_HFP = 88, PKKDA_WQHD_HSA = 24, PKKDA_WQHD_HBP = 44;
static const u16 PKKDA_WQHD_VFP = 12, PKKDA_WQHD_VSA = 4, PKKDA_WQHD_VBP = 22;

#define PKKDA_DIMENSION_MM .width_mm = 70, .height_mm = 156
#define CGYDA_DIMENSION_MM .width_mm = 66, .height_mm = 147

#define CGYDA_FHD_DSC { .enabled = true, .dsc_count = 2, .cfg = &pps_configs[PANEL_TYPE_CGYDA][0] }
#define CGYDA_WQHD_DSC { .enabled = true, .dsc_count = 2, .cfg = &pps_configs[PANEL_TYPE_CGYDA][1] }

#define PKKDA_FHD_DSC { .enabled = true, .dsc_count = 2, .cfg = &pps_configs[PANEL_TYPE_PKKDA][0] }
#define PKKDA_WQHD_DSC { .enabled = true, .dsc_count = 2, .cfg = &pps_configs[PANEL_TYPE_PKKDA][1] }

static const struct gs_panel_mode_array cgyda_modes = GS_PANEL_MODES(
	{
		.mode = {
			.name = "1280x2856x120@240",
			DRM_VRR_MODE_TIMING(120, 240, CGYDA_WQHD_HDISPLAY, CGYDA_WQHD_HFP,
						 CGYDA_WQHD_HSA, CGYDA_WQHD_HBP,
						 CGYDA_WQHD_VDISPLAY, CGYDA_WQHD_VFP,
						 CGYDA_WQHD_VSA, CGYDA_WQHD_VBP),
			CGYDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = CGYDA_TE_USEC_HS,
			.bpc = 8,
			.dsc = CGYDA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = CGYDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1280x2856x60@60",
			DRM_VRR_MODE_TIMING(60, 60, CGYDA_WQHD_HDISPLAY, CGYDA_WQHD_HFP,
						CGYDA_WQHD_HSA, CGYDA_WQHD_HBP,
						CGYDA_WQHD_VDISPLAY, CGYDA_WQHD_VFP,
						CGYDA_WQHD_VSA, CGYDA_WQHD_VBP),
			.flags = DRM_MODE_FLAG_NS,
			CGYDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = CGYDA_TE_USEC_NS,
			.bpc = 8,
			.dsc = CGYDA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = CGYDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1080x2410x120@240",
			DRM_VRR_MODE_TIMING(120, 240, CGYDA_FHD_HDISPLAY, CGYDA_FHD_HFP,
						 CGYDA_FHD_HSA, CGYDA_FHD_HBP,
						 CGYDA_FHD_VDISPLAY, CGYDA_FHD_VFP,
						 CGYDA_FHD_VSA, CGYDA_FHD_VBP),
			CGYDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = CGYDA_TE_USEC_HS,
			.bpc = 8,
			.dsc = CGYDA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = CGYDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1280x2856x120@120",
			DRM_VRR_MODE_TIMING(120, 120, CGYDA_WQHD_HDISPLAY, CGYDA_WQHD_HFP,
						 CGYDA_WQHD_HSA, CGYDA_WQHD_HBP,
						 CGYDA_WQHD_VDISPLAY, CGYDA_WQHD_VFP,
						 CGYDA_WQHD_VSA, CGYDA_WQHD_VBP),
			CGYDA_DIMENSION_MM,
			/* aligned to bootloader resolution */
			.type = DRM_MODE_TYPE_PREFERRED,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = CGYDA_TE_USEC_HS,
			.bpc = 8,
			.dsc = CGYDA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = CGYDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1080x2410x120@120",
			DRM_VRR_MODE_TIMING(120, 120, CGYDA_FHD_HDISPLAY, CGYDA_FHD_HFP,
						 CGYDA_FHD_HSA, CGYDA_FHD_HBP,
						 CGYDA_FHD_VDISPLAY, CGYDA_FHD_VFP,
						 CGYDA_FHD_VSA, CGYDA_FHD_VBP),
			CGYDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = CGYDA_TE_USEC_HS,
			.bpc = 8,
			.dsc = CGYDA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = CGYDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1080x2410x60@60",
			DRM_VRR_MODE_TIMING(60, 60, CGYDA_FHD_HDISPLAY, CGYDA_FHD_HFP,
						CGYDA_FHD_HSA, CGYDA_FHD_HBP,
						CGYDA_FHD_VDISPLAY, CGYDA_FHD_VFP,
						CGYDA_FHD_VSA, CGYDA_FHD_VBP),
			.flags = DRM_MODE_FLAG_NS,
			CGYDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = CGYDA_TE_USEC_NS,
			.bpc = 8,
			.dsc = CGYDA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = CGYDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
#endif
); /* cgyda_modes */

static const struct gs_panel_mode_array pkkda_modes = GS_PANEL_MODES(
	{
		.mode = {
			.name = "1344x2992x120@480",
			DRM_VRR_MODE_TIMING(120, 480, PKKDA_WQHD_HDISPLAY, PKKDA_WQHD_HFP,
						 PKKDA_WQHD_HSA, PKKDA_WQHD_HBP,
						 PKKDA_WQHD_VDISPLAY, PKKDA_WQHD_VFP,
						 PKKDA_WQHD_VSA, PKKDA_WQHD_VBP),
			PKKDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = PKKDA_TE_USEC_480HZ,
			.bpc = 8,
			.dsc = PKKDA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = PKKDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1080x2404x120@480",
			DRM_VRR_MODE_TIMING(120, 480, PKKDA_FHD_HDISPLAY, PKKDA_FHD_HFP,
						 PKKDA_FHD_HSA, PKKDA_FHD_HBP,
						 PKKDA_FHD_VDISPLAY, PKKDA_FHD_VFP,
						 PKKDA_FHD_VSA, PKKDA_FHD_VBP),
			PKKDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = PKKDA_TE_USEC_480HZ,
			.bpc = 8,
			.dsc = PKKDA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = PKKDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1344x2992x120@240",
			DRM_VRR_MODE_TIMING(120, 240, PKKDA_WQHD_HDISPLAY, PKKDA_WQHD_HFP,
						 PKKDA_WQHD_HSA, PKKDA_WQHD_HBP,
						 PKKDA_WQHD_VDISPLAY, PKKDA_WQHD_VFP,
						 PKKDA_WQHD_VSA, PKKDA_WQHD_VBP),
			PKKDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = PKKDA_TE_USEC_HS,
			.bpc = 8,
			.dsc = PKKDA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = PKKDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1344x2992x60@60",
			DRM_VRR_MODE_TIMING(60, 60, PKKDA_WQHD_HDISPLAY, PKKDA_WQHD_HFP,
						PKKDA_WQHD_HSA, PKKDA_WQHD_HBP,
						PKKDA_WQHD_VDISPLAY, PKKDA_WQHD_VFP,
						PKKDA_WQHD_VSA, PKKDA_WQHD_VBP),
			.flags = DRM_MODE_FLAG_NS,
			PKKDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = PKKDA_TE_USEC_NS,
			.bpc = 8,
			.dsc = PKKDA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = PKKDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1080x2404x120@240",
			DRM_VRR_MODE_TIMING(120, 240, PKKDA_FHD_HDISPLAY, PKKDA_FHD_HFP,
						 PKKDA_FHD_HSA, PKKDA_FHD_HBP,
						 PKKDA_FHD_VDISPLAY, PKKDA_FHD_VFP,
						 PKKDA_FHD_VSA, PKKDA_FHD_VBP),
			PKKDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = PKKDA_TE_USEC_HS,
			.bpc = 8,
			.dsc = PKKDA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = PKKDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1344x2992x120@120",
			DRM_VRR_MODE_TIMING(120, 120, PKKDA_WQHD_HDISPLAY, PKKDA_WQHD_HFP,
						 PKKDA_WQHD_HSA, PKKDA_WQHD_HBP,
						 PKKDA_WQHD_VDISPLAY, PKKDA_WQHD_VFP,
						 PKKDA_WQHD_VSA, PKKDA_WQHD_VBP),
			PKKDA_DIMENSION_MM,
			/* aligned to bootloader resolution */
			.type = DRM_MODE_TYPE_PREFERRED,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = PKKDA_TE_USEC_HS,
			.bpc = 8,
			.dsc = PKKDA_WQHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = PKKDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1080x2404x120@120",
			DRM_VRR_MODE_TIMING(120, 120, PKKDA_FHD_HDISPLAY, PKKDA_FHD_HFP,
						 PKKDA_FHD_HSA, PKKDA_FHD_HBP,
						 PKKDA_FHD_VDISPLAY, PKKDA_FHD_VFP,
						 PKKDA_FHD_VSA, PKKDA_FHD_VBP),
			PKKDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = PKKDA_TE_USEC_HS,
			.bpc = 8,
			.dsc = PKKDA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = PKKDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
	{
		.mode = {
			.name = "1080x2404x60@60",
			DRM_VRR_MODE_TIMING(60, 60, PKKDA_FHD_HDISPLAY, PKKDA_FHD_HFP,
						PKKDA_FHD_HSA, PKKDA_FHD_HBP,
						PKKDA_FHD_VDISPLAY, PKKDA_FHD_VFP,
						PKKDA_FHD_VSA, PKKDA_FHD_VBP),
			.flags = DRM_MODE_FLAG_NS,
			PKKDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = PKKDA_TE_USEC_NS,
			.bpc = 8,
			.dsc = PKKDA_FHD_DSC,
		},
		.te2_timing = {
			.rising_edge = PKCGDA_TE2_RISING_EDGE_OFFSET,
			.falling_edge = PKKDA_TE2_FALLING_EDGE_OFFSET,
		},
	},
#endif
); /* pkkda_modes */

static const struct gs_panel_mode_array cgyda_lp_modes = GS_PANEL_MODES(
	{
		.mode = {
			.name = "1280x2856x30@30",
			DRM_MODE_TIMING(30, CGYDA_WQHD_HDISPLAY, CGYDA_WQHD_HFP,
						CGYDA_WQHD_HSA, CGYDA_WQHD_HBP,
						CGYDA_WQHD_VDISPLAY, CGYDA_WQHD_VFP,
						CGYDA_WQHD_VSA, CGYDA_WQHD_VBP),
			CGYDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = CGYDA_TE_USEC_AOD,
			.bpc = 8,
			.dsc = CGYDA_WQHD_DSC,
			.is_lp_mode = true,
		},
	},
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1080x2410x30@30",
			DRM_MODE_TIMING(30, CGYDA_FHD_HDISPLAY, CGYDA_FHD_HFP,
						CGYDA_FHD_HSA, CGYDA_FHD_HBP,
						CGYDA_FHD_VDISPLAY, CGYDA_FHD_VFP,
						CGYDA_FHD_VSA, CGYDA_FHD_VBP),
			CGYDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = CGYDA_TE_USEC_AOD,
			.bpc = 8,
			.dsc = CGYDA_FHD_DSC,
			.is_lp_mode = true,
		},
	},
#endif
); /* cgyda_lp_modes */

static const struct gs_panel_mode_array pkkda_lp_modes = GS_PANEL_MODES(
	{
		.mode = {
			.name = "1344x2992x30@30",
			DRM_MODE_TIMING(30, PKKDA_WQHD_HDISPLAY, PKKDA_WQHD_HFP,
						PKKDA_WQHD_HSA, PKKDA_WQHD_HBP,
						PKKDA_WQHD_VDISPLAY, PKKDA_WQHD_VFP,
						PKKDA_WQHD_VSA, PKKDA_WQHD_VBP),
			PKKDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = PKKDA_TE_USEC_AOD,
			.bpc = 8,
			.dsc = PKKDA_WQHD_DSC,
			.is_lp_mode = true,
		},
	},
#ifndef PANEL_FACTORY_BUILD
	{
		.mode = {
			.name = "1080x2404x30@30",
			DRM_MODE_TIMING(30, PKKDA_FHD_HDISPLAY, PKKDA_FHD_HFP,
						PKKDA_FHD_HSA, PKKDA_FHD_HBP,
						PKKDA_FHD_VDISPLAY, PKKDA_FHD_VFP,
						PKKDA_FHD_VSA, PKKDA_FHD_VBP),
			PKKDA_DIMENSION_MM,
		},
		.gs_mode = {
			.mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS,
			.vblank_usec = 120,
			.te_usec = PKKDA_TE_USEC_AOD,
			.bpc = 8,
			.dsc = PKKDA_FHD_DSC,
			.is_lp_mode = true,
		},
	},
#endif
); /* pkkda_lp_modes */

static const struct gs_brightness_configuration pkcgda_brt_configs[] = {
	{
		.panel_rev = PANEL_REV_ALL,
		.default_brightness = 4460, /* 140 nits */
		.brt_capability = {
			.normal = {
				.nits = {
					.min = 0, /* 0.5 nits */
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

static struct gs_panel_brightness_desc pkcgda_brightness_desc = {
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

static const struct gs_dsi_cmd pkcgda_lp_night_cmds[] = {
	/* AOD Night Mode, 1nit */
	GS_DSI_FLUSH_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x01, 0xD8),
};

static const struct gs_dsi_cmd pkcgda_lp_low_cmds[] = {
	/* AOD Low Mode, 10nit */
	GS_DSI_FLUSH_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x05, 0x41),
};

static const struct gs_dsi_cmd pkcgda_lp_high_cmds[] = {
	/* AOD High Mode, 50nit */
	GS_DSI_FLUSH_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x0A, 0xEA),
};

static const struct gs_dsi_cmd pkcgda_lp_sun_cmds[] = {
	/* AOD Sun Mode, 150nit */
	GS_DSI_FLUSH_CMD(MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x11, 0xFB),
};

static const struct gs_binned_lp pkcgda_binned_lp[] = {
	/* night threshold 4 nits */
	BINNED_LP_MODE_TIMING("night", 884, pkcgda_lp_night_cmds,
			      PKCGDA_TE2_RISING_EDGE_OFFSET,
			      CGYDA_TE2_FALLING_EDGE_OFFSET),
	/* low threshold 40 nits */
	BINNED_LP_MODE_TIMING("low", 2520, pkcgda_lp_low_cmds,
			      PKCGDA_TE2_RISING_EDGE_OFFSET,
			      CGYDA_TE2_FALLING_EDGE_OFFSET),
	/* high threshold 140 nits */
	BINNED_LP_MODE_TIMING("high", 4460, pkcgda_lp_high_cmds,
			      PKCGDA_TE2_RISING_EDGE_OFFSET,
			      CGYDA_TE2_FALLING_EDGE_OFFSET),
	BINNED_LP_MODE_TIMING("sun", 12908, pkcgda_lp_sun_cmds,
			      PKCGDA_TE2_RISING_EDGE_OFFSET,
			      CGYDA_TE2_FALLING_EDGE_OFFSET),
};

#define PKCGDA_BURN_IN_COMP_OFFSET -4

/* Modified in pkcgda_get_burn_in_comp_cmdset. Set default temperature as 0x19. */
static u8 pkcgda_burn_in_comp_temp_cmd[] = { 0x69, 0x19 };

static const struct gs_dsi_cmd pkcgda_burn_in_comp_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(unlock_cmd_f0),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x03, 0x69),
	GS_DSI_QUEUE_CMDLIST(pkcgda_burn_in_comp_temp_cmd),
	GS_DSI_FLUSH_CMDLIST(lock_cmd_f0),
};
static DEFINE_GS_CMDSET(pkcgda_burn_in_comp);

static const struct gs_dsi_cmdset *pkcgda_get_burn_in_comp_cmdset(u32 value)
{
	pkcgda_burn_in_comp_temp_cmd[1] = value + PKCGDA_BURN_IN_COMP_OFFSET;

	return &pkcgda_burn_in_comp_cmdset;
}

/* Apply appropriate gain into DDIC for burn-in compensation */
static void pkcgda_send_burn_in_comp_cmds(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct pkcgda_panel *spanel = to_spanel(ctx);
	const struct gs_dsi_cmdset *burn_in_comp_cmdset =
					pkcgda_get_burn_in_comp_cmdset(ctx->skin_temperature);

	if (ctx->panel_state != GPANEL_STATE_NORMAL)
		return;

	spanel->pending_skin_temp_handling = false;

	/* SP Temperature commands */
	PANEL_ATRACE_BEGIN(__func__);
	gs_panel_send_cmdset(ctx, burn_in_comp_cmdset);
	PANEL_ATRACE_END(__func__);

	PANEL_ATRACE_INT_PID_FMT(pkcgda_burn_in_comp_temp_cmd[1], ctx->trace_pid,
				 "skin_temperature(offset)[%s]", ctx->panel_model);
	dev_dbg(dev, "skin_temp: apply gain into ddic at %udeg c (offset=%d)\n",
		pkcgda_burn_in_comp_temp_cmd[1], PKCGDA_BURN_IN_COMP_OFFSET);
}

static void pkcgda_update_te2_option(struct gs_panel *ctx, u8 val)
{
	struct device *dev = ctx->dev;

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0xB9);
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, val);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	notify_panel_te2_option_changed(ctx);
	dev_dbg(dev, "te2 option is updated to %s\n",
		(val == PKCGDA_TE2_CHANGEABLE) ? "changeable" :
		 ((val == PKCGDA_TE2_FIXED_240HZ) ? "fixed:240" : "fixed:120"));
}

static void pkcgda_update_te2(struct gs_panel *ctx)
{
	struct pkcgda_panel *spanel = to_spanel(ctx);

	if (spanel->force_changeable_te2 && ctx->te2.option == TEX_OPT_FIXED) {
		dev_dbg(ctx->dev, "force to changeable TE2\n");
		ctx->te2.option = TEX_OPT_CHANGEABLE;
		pkcgda_update_te2_option(ctx, PKCGDA_TE2_CHANGEABLE);
	}
}

static void pkcgda_te2_setting(struct gs_panel *ctx)
{
	struct pkcgda_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	u32 rising = PKCGDA_TE2_RISING_EDGE_OFFSET;
	u32 falling = (GET_PANEL_TYPE(ctx) == PANEL_TYPE_CGYDA) ?
		      CGYDA_TE2_FALLING_EDGE_OFFSET : PKKDA_TE2_FALLING_EDGE_OFFSET;
	u8 option;

	if (ctx->te2.option == TEX_OPT_FIXED && !spanel->force_changeable_te2)
		option = (ctx->te2.freq_hz == 240) ? PKCGDA_TE2_FIXED_240HZ :
						     PKCGDA_TE2_FIXED_120HZ;
	else
		option = PKCGDA_TE2_CHANGEABLE;

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
		(ctx->te2.option == TEX_OPT_CHANGEABLE) ? "changeable" :
		 ((ctx->te2.freq_hz == 240) ? "fixed:240" : "fixed:120"),
		rising, falling);
}

static bool pkcgda_set_te2_freq(struct gs_panel *ctx, u32 freq_hz)
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
			pkcgda_update_te2_option(ctx, (freq_hz == 240) ? PKCGDA_TE2_FIXED_240HZ :
								       PKCGDA_TE2_FIXED_120HZ);
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

static u32 pkcgda_get_te2_freq(struct gs_panel *ctx)
{
	return ctx->te2.freq_hz;
}

static bool pkcgda_set_te2_option(struct gs_panel *ctx, u32 option)
{
	struct pkcgda_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	u8 val;

	if (option == ctx->te2.option)
		return false;

	if (option == TEX_OPT_FIXED) {
		if (spanel->force_changeable_te2) {
			dev_dbg(dev, "force changeable TE2 is set\n");
			return false;
		}
		val = (ctx->te2.freq_hz == 240) ? PKCGDA_TE2_FIXED_240HZ : PKCGDA_TE2_FIXED_120HZ;
	} else if (option == TEX_OPT_CHANGEABLE) {
		val = PKCGDA_TE2_CHANGEABLE;
	} else {
		dev_warn(dev, "unsupported TE2 option (%u)\n", option);
		return false;
	}

	pkcgda_update_te2_option(ctx, val);
	ctx->te2.option = option;

	return true;
}

static enum gs_panel_tex_opt pkcgda_get_te2_option(struct gs_panel *ctx)
{
	return ctx->te2.option;
}

static void pkcgda_set_panel_feat_manual_mode_fi(struct gs_panel *ctx, bool enabled)
{
	struct device *dev = ctx->dev;
	u8 val = enabled ? 0x22 : 0x00;

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x2A, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, val, val);

	if (!enabled) {
		/* Mask Setting */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x02);

		/* Brightness Transition Setting */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x1E, 0x67);
		GS_DCS_BUF_ADD_CMD(dev, 0x67, 0x10);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x28, 0x67);
		GS_DCS_BUF_ADD_CMD(dev, 0x67, 0xF3);
	}

	dev_dbg(ctx->dev, "manual mode fi=%d\n", enabled);
}

/**
 * pkcgda_set_panel_feat_te - configure TE type, width and frequency
 * @ctx: gs_panel struct
 * @te_freq: TE frequency
 * @return: enum gs_panel_tex_opt, fixed or changeable TE
 *
 * Description: this function should cache commands only, don't update hw_status.
 */
static enum gs_panel_tex_opt pkcgda_set_panel_feat_te(struct gs_panel *ctx, u32 te_freq)
{
	struct pkcgda_panel *spanel = to_spanel(ctx);
	struct device *dev = ctx->dev;
	const unsigned long *feat = ctx->sw_status.feat;
	enum pkcgda_panel_type panel_type = GET_PANEL_TYPE(ctx);
	enum gs_panel_tex_opt te_opt = TEX_OPT_FIXED;
	const bool is_ns_mode = test_bit(FEAT_OP_NS, feat);

	if (!spanel->force_changeable_te) {
		if (te_freq == 480) {
			/* 480Hz multi TE */
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x41);
			/* TE width */
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x08, 0xB9);
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x2B, 0xA3, 0x02, 0x2B, 0xA3, 0x02);
			/* VRR Masking */
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x1A, 0xB9);
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x40);
		} else if (te_freq == 240) {
			static const u8 vrr_te_settings[PANEL_TYPE_MAX][GS_PWM_RATE_MAX][7] = {
				{ /* CGYDA */
					{ 0xB9, 0xB0, 0xE3, 0x02, 0x55, 0x63, 0x02 },
					{ 0xB9, 0x55, 0x63, 0x02, 0x55, 0x63, 0x02 },
				},
				{ /* PKKDA */
					{ 0xB9, 0xB8, 0xA3, 0x02, 0x59, 0x23, 0x02 },
					{ 0xB9, 0x59, 0x23, 0x02, 0x59, 0x23, 0x02 },
				},
			};
			static const u8 vrr_mask_setting[GS_PWM_RATE_MAX] = { 0x00, 0x40 };

			/* 240Hz multi TE */
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, test_bit(FEAT_PWM_HIGH, feat) ? 0x41 : 0x31);
			/* TE width */
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x08, 0xB9);
			GS_DCS_BUF_ADD_CMDLIST(dev,
				vrr_te_settings[panel_type][ctx->pwm_mode]);
			/* VRR Masking */
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x1A, 0xB9);
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, vrr_mask_setting[ctx->pwm_mode]);
		} else { /* 120Hz or NS */
			static const u8 fixed_te_settings[PANEL_TYPE_MAX][2][7] = {
				{ /* CGYDA */
					{ 0xB9, 0xB0, 0xE3, 0x02, 0xB0, 0xE3, 0x02 },
					{ 0xB9, 0xB3, 0x23, 0x02, 0xB3, 0x23, 0x02 },
				},
				{ /* PKKDA */
					{ 0xB9, 0xB8, 0xA3, 0x02, 0xB8, 0xA3, 0x02 },
					{ 0xB9, 0xBA, 0xE3, 0x02, 0xBA, 0xE3, 0x02 },
				},
			};

			/* Fixed TE */
			GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x29);
			/* TE width */
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x08, 0xB9);
			GS_DCS_BUF_ADD_CMDLIST(dev, fixed_te_settings[panel_type][is_ns_mode]);
		}
	} else {
		static const u8 changeable_te_settings[PANEL_TYPE_MAX][2][4] = {
			{ /* CGYDA */
				{ 0xB9, 0xB0, 0xE3, 0x02 },
				{ 0xB9, 0xB3, 0x23, 0x02 },
			},
			{ /* PKKDA */
				{ 0xB9, 0xB8, 0xA3, 0x02 },
				{ 0xB9, 0xBA, 0xE3, 0x02 },
			},
		};

		/* Changeable TE */
		GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x04);
		/* TE width */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x08, 0xB9);
		GS_DCS_BUF_ADD_CMDLIST(dev, changeable_te_settings[panel_type][is_ns_mode]);
		te_opt = TEX_OPT_CHANGEABLE;
	}

	/* TE sync setting */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x91, 0xB9);
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, test_bit(FEAT_PWM_HIGH, feat) ? 0x80 : 0x00);
	return te_opt;
}

static void pkcgda_set_panel_feat_hbm_irc(struct gs_panel *ctx, enum irc_mode irc_mode)
{
	struct device *dev = ctx->dev;
	enum pkcgda_panel_type panel_type = GET_PANEL_TYPE(ctx);
	bool flat_z = (irc_mode == IRC_FLAT_Z) ? 1 : 0;

	/*
	 * "Flat mode" is used to replace IRC on for normal mode and HDR video,
	 * and "Flat Z mode" is used to replace IRC off for sunlight
	 * environment.
	 */

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x77, 0x6D);
	if (flat_z) {
		if (ctx->panel_rev_id.id == PANEL_REVID_PROTO1)
			GS_DCS_BUF_ADD_CMD(dev, 0x6D, 0xD5, 0xAA);
		else if (ctx->panel_rev_id.id >= PANEL_REVID_DVT1)
			GS_DCS_BUF_ADD_CMD(dev, 0x6D, 0xA8, 0x86);
		else if (panel_type == PANEL_TYPE_CGYDA)
			GS_DCS_BUF_ADD_CMD(dev, 0x6D, 0xCE, 0xA5);
		else /* PKKDA */
			GS_DCS_BUF_ADD_CMD(dev, 0x6D, 0xBC, 0x96);
	} else { /* IRC_FLAT_DEFAULT or IRC_OFF */
		GS_DCS_BUF_ADD_CMD(dev, 0x6D, 0x00, 0x00);
	}

	/* sp_irc_settings */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x07, 0xC5, 0x69);
	if (panel_type == PANEL_TYPE_CGYDA) {
		if (!flat_z) /* Flat mode */
			GS_DCS_BUF_ADD_CMD(dev, 0x69, 0x01, 0x02, 0x06, 0x04, 0x06, 0x08);
		else { /* Flat Z mode */
			if (ctx->panel_rev_id.id >= PANEL_REVID_DVT1)
				GS_DCS_BUF_ADD_CMD(dev, 0x69, 0x64, 0x50, 0x32, 0xF0, 0xF0, 0xF0);
			else
				GS_DCS_BUF_ADD_CMD(dev, 0x69, 0x1E, 0x79, 0x18, 0x80, 0xE1, 0x82);
		}
	} else { /* PKKDA */
		if (!flat_z) /* Flat mode */
			GS_DCS_BUF_ADD_CMD(dev, 0x69, 0x02, 0x02, 0x06, 0x06, 0x06, 0x08);
		else { /* Flat Z mode */
			if (ctx->panel_rev_id.id >= PANEL_REVID_DVT1)
				GS_DCS_BUF_ADD_CMD(dev, 0x69, 0x64, 0x50, 0x32, 0xF0, 0xF0, 0xF0);
			else
				GS_DCS_BUF_ADD_CMD(dev, 0x69, 0x7F, 0x76, 0x66, 0xE3, 0xE0, 0xD0);
		}
	}

	dev_info(dev, "irc_mode=%d\n", irc_mode);
}

static void pkcgda_set_panel_feat_early_exit(struct gs_panel *ctx, u32 vrefresh, u32 te_freq)
{
	struct device *dev = ctx->dev;
	const unsigned long *feat = ctx->sw_status.feat;
	u8 val;

	if (test_bit(FEAT_EARLY_EXIT, feat) && te_freq == 480) {
		/* EM Cyc */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0xEA, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x03);
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x04, 0x75, 0x67);
		GS_DCS_BUF_ADD_CMD(dev, 0x67, 0x03);
	}

	if (!test_bit(FEAT_EARLY_EXIT, feat) || vrefresh == 80 || vrefresh == 48)
		val = 0x66;
	else
		val = (te_freq > 120) ? 0x64 : 0x65;

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x03, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, val);

	if (test_bit(FEAT_EARLY_EXIT, feat) && te_freq == 480) {
		/* Freq. Set */
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x5B, 0xBD);
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x04, 0x00, 0x0C, 0x00, 0x2C, 0x00,
					0x02, 0x00, 0x06, 0x00, 0x10, 0x01, 0xDC);
	}
}

static void pkcgda_set_panel_feat_tsp_sync(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
					   u32 te_freq)
{
	struct device *dev = ctx->dev;
	const bool use_alt_setting = gs_is_ns_op_rate(pmode) || te_freq == 480;

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x05, 0xF2);
	GS_DCS_BUF_ADD_CMD(dev, 0xF2, use_alt_setting ? 0x02 : 0x03);
}

static void pkcgda_set_panel_feat_opec_setting(struct gs_panel *ctx,
						const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	const bool is_ns_mode = gs_is_ns_op_rate(pmode);
	bool is_lp_mode = pmode->gs_mode.is_lp_mode;

	if (ctx->panel_rev_id.id < PANEL_REVID_EVT1_1)
		return;

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0xD0, 0x68);
	if (ctx->pwm_mode == GS_PWM_RATE_STANDARD || is_ns_mode || is_lp_mode)
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16);
	else /* GS_PWM_RATE_HIGH */
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10);
}

static void pkcgda_set_panel_feat_frequency(struct gs_panel *ctx, u32 vrefresh, u32 idle_vrefresh)
{
	struct device *dev = ctx->dev;
	const unsigned long *feat = ctx->sw_status.feat;
	const bool is_ns_mode = test_bit(FEAT_OP_NS, feat);
	u8 val;

	/*
	 * Description: this sequence possibly overrides some configs early-exit
	 * and operation set, depending on FI mode.
	 */
	if (test_bit(FEAT_FRAME_AUTO, feat)) {
		if (is_ns_mode) {
			/* target frequency */
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x7B, 0xBD);
			if (idle_vrefresh == 10) {
				val = 0x14;
			} else if (idle_vrefresh == 30) {
				val = 0x04;
			} else {
				if (idle_vrefresh != 1)
					dev_warn(dev, "unsupported target freq %u\n",
						      idle_vrefresh);
				/* 1Hz */
				val = 0xEC;
			}
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, val);
			/* step setting */
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x7D, 0xBD);
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x04, 0x00, 0x14, 0x00,
						0x00);
			/* step setting */
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x8D, 0xBD);
			if (idle_vrefresh == 10)
				/* 60->10Hz */
				GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x02, 0x02, 0x00, 0x00);
			else if (idle_vrefresh == 30)
				/* 60->30Hz */
				GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x02, 0x00, 0x00, 0x00);
			else
				/* 60->1Hz */
				GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x02, 0x02, 0x04, 0x00);
		} else {
			/* initial frequency */
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
					dev_warn(dev, "unsupported target freq %u\n",
						      idle_vrefresh);
				/* 1Hz */
				val = 0xEE;
			}
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, val);
			/* step setting */
			GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x7D, 0xBD);
			GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x00, 0x00, 0x02, 0x00, 0x06, 0x00,
						0x16);
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
		}
		/* auto mode */
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x05);
	} else { /* manual */
		GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
		if (is_ns_mode) {
			if (vrefresh == 1) {
				val = 0x0F;
			} else if (vrefresh == 10) {
				val = 0x0B;
			} else if (vrefresh == 30) {
				val = 0x09;
			} else {
				if (vrefresh != 60)
					dev_warn(dev, "unsupported manual freq %u\n", vrefresh);
				/* 60Hz */
				val = 0x08;
			}
		} else {
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

			val |= test_bit(FEAT_PWM_HIGH, feat) ? 0x10 : 0x00;
		}
		GS_DCS_BUF_ADD_CMD(dev, 0x83, val);
	}
}

/**
 * pkcgda_set_panel_feat_pwm - enable or disable high pwm mode
 * @ctx: gs_panel struct
 *
 * Description: the configs could possibly be overridden by frequency setting,
 * depending on FI mode.
 */
static void pkcgda_set_panel_feat_pwm(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	const unsigned long *feat = ctx->sw_status.feat;

	u8 val = test_bit(FEAT_PWM_HIGH, feat) ? 0x10 :
		 test_bit(FEAT_OP_NS, feat) ? 0x08 : 0x00;

	GS_DCS_BUF_ADD_CMD(dev, 0x83, val);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x05, 0xBD);
	val = test_bit(FEAT_PWM_HIGH, feat) ? 0x28 :
		 test_bit(FEAT_OP_NS, feat) ? 0x14 : 0x00;
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, val);
}

/**
 * pkcgda_set_panel_feat - configure panel features
 * @ctx: gs_panel struct
 * @pmode: gs_panel_mode struct, target panel mode
 * @idle_vrefresh: target vrefresh rate in auto mode, 0 if disabling auto mode
 * @enforce: force to write all of registers even if no feature state changes
 *
 * Configure panel features based on the context.
 * Note: pkcgda_set_panel_feat_xxx() should cache commands only while
 *       pkcgda_set_panel_feat() aggregates and sends the commands, and update
 *       hw_status. DO NOT update them in pkcgda_set_panel_feat_xxx().
 */
static void pkcgda_set_panel_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
				bool enforce)
{
	struct device *dev = ctx->dev;
	struct gs_panel_status *sw_status = &ctx->sw_status;
	struct gs_panel_status *hw_status = &ctx->hw_status;
	struct pkcgda_panel *spanel = to_spanel(ctx);
	unsigned long *feat = sw_status->feat;
	enum irc_mode irc_mode = sw_status->irc_mode;
	u32 idle_vrefresh = sw_status->idle_vrefresh;
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 te_freq = gs_drm_mode_te_freq(&pmode->mode);
	bool irc_mode_changed, idle_vrefresh_changed, vrefresh_changed, te_freq_changed;
	DECLARE_BITMAP(changed_feat, FEAT_MAX);

	if (te_freq == 480 && GET_PANEL_TYPE(ctx) != PANEL_TYPE_PKKDA) {
		dev_warn(dev, "unsupported TE %u on panel, falling back to 120Hz\n", te_freq);
		te_freq = 120;
	}

	if (te_freq > vrefresh && idle_vrefresh > 1)
		dev_warn(dev, "te might be gated (te=%u vrefresh=%u idle_vrefresh=%u)\n",
				te_freq, vrefresh, idle_vrefresh);

	if (!test_bit(FEAT_FRAME_AUTO, feat)) {
		vrefresh = idle_vrefresh ? idle_vrefresh : 1;
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
	} else if (te_freq == 480) {
		clear_bit(FEAT_OP_NS, feat);
		set_bit(FEAT_PWM_HIGH, feat);
	} else {
		clear_bit(FEAT_OP_NS, feat);
		if (ctx->pwm_mode == GS_PWM_RATE_HIGH)
			set_bit(FEAT_PWM_HIGH, feat);
		else
			clear_bit(FEAT_PWM_HIGH, feat);
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

	/* TE settings */
	sw_status->te.freq_hz = te_freq;
	if (test_bit(FEAT_EARLY_EXIT, changed_feat) || test_bit(FEAT_PWM_HIGH, changed_feat) ||
	    test_bit(FEAT_OP_NS, changed_feat) || te_freq_changed)
		hw_status->te.option = pkcgda_set_panel_feat_te(ctx, te_freq);

	/*
	 * HBM IRC setting
	 */
	if (irc_mode_changed) {
		pkcgda_set_panel_feat_hbm_irc(ctx, irc_mode);
		hw_status->irc_mode = irc_mode;
	}

	/*
	 * High PWM or NS mode: enable or disable
	 *
	 * Description: the configs could possibly be overridden by frequency setting,
	 * depending on FI mode.
	 */
	if (test_bit(FEAT_PWM_HIGH, changed_feat) || test_bit(FEAT_OP_NS, changed_feat)) {
		pkcgda_set_panel_feat_pwm(ctx);

		if (test_bit(FEAT_OP_NS, changed_feat)) {
			ctx->op_hz = (gs_is_ns_op_rate(pmode)) ? 60 : 120;

			if (test_bit(FEAT_PWM_HIGH, changed_feat))
				notify_panel_pwm_mode_changed(ctx);
			notify_panel_op_hz_changed(ctx);
		}
	}

	/*
	 * Early-exit: enable or disable
	 */
	if (test_bit(FEAT_EARLY_EXIT, changed_feat) || vrefresh_changed || te_freq_changed)
		pkcgda_set_panel_feat_early_exit(ctx, vrefresh, te_freq);

	/*
	 * Manual FI: enable or disable manual mode FI
	 */
	if (test_bit(FEAT_FRAME_MANUAL_FI, changed_feat))
		pkcgda_set_panel_feat_manual_mode_fi(ctx, test_bit(FEAT_FRAME_MANUAL_FI, feat));

	/* TSP Sync setting */
	if (test_bit(FEAT_OP_NS, changed_feat) || te_freq_changed)
		pkcgda_set_panel_feat_tsp_sync(ctx, pmode, te_freq);

	/* Opec setting */
	if (test_bit(FEAT_PWM_HIGH, changed_feat) || test_bit(FEAT_OP_NS, changed_feat))
		pkcgda_set_panel_feat_opec_setting(ctx, pmode);

	/*
	 * Frequency setting: FI, frequency, idle frequency
	 */
	if (test_bit(FEAT_FRAME_AUTO, changed_feat) || test_bit(FEAT_PWM_HIGH, changed_feat) ||
	    idle_vrefresh_changed || vrefresh_changed)
		pkcgda_set_panel_feat_frequency(ctx, vrefresh, idle_vrefresh);

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
 * pkcgda_update_panel_feat - configure panel features with current refresh rate
 * @ctx: gs_panel struct
 * @enforce: force to write all of registers even if no feature state changes
 *
 * Configure panel features based on the context without changing current refresh rate
 * and idle setting.
 */
static void pkcgda_update_panel_feat(struct gs_panel *ctx, bool enforce)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	pkcgda_set_panel_feat(ctx, pmode, enforce);
}

static void pkcgda_update_refresh_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode,
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

	sw_status->idle_vrefresh = idle_vrefresh;
	pkcgda_set_panel_feat(ctx, pmode, false);
	notify_panel_mode_changed(ctx);

	dev_dbg(ctx->dev, "display state is notified of mode update\n");
}

static void pkcgda_change_frequency(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	u32 vrefresh = drm_mode_vrefresh(&pmode->mode);
	u32 idle_vrefresh = 0;

	/* use current idle_vrefresh */
	idle_vrefresh = ctx->sw_status.idle_vrefresh;

	pkcgda_update_refresh_mode(ctx, pmode, idle_vrefresh);
	ctx->sw_status.te.freq_hz = gs_drm_mode_te_freq(&pmode->mode);

	dev_dbg(ctx->dev, "change to %u hz\n", vrefresh);
}

static bool pkcgda_set_self_refresh(struct gs_panel *ctx, bool enable)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;
	struct pkcgda_panel *spanel = to_spanel(ctx);

	if (spanel->pending_skin_temp_handling && enable)
		pkcgda_send_burn_in_comp_cmds(ctx);

	if (unlikely(!pmode))
		return false;

	PANEL_ATRACE_INT_PID_FMT(enable, ctx->trace_pid,
					"set_self_refresh[%s]", ctx->panel_model);
	return false;
}

static void pkcgda_set_panel_lp_feat_te(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	enum pkcgda_panel_type panel_type = GET_PANEL_TYPE(ctx);
	static const u8 fixed_te_settings[PANEL_TYPE_MAX][7] = {
		{ 0xB9, 0xB3, 0x23, 0x02, 0xB3, 0x23, 0x02 },
		{ 0xB9, 0xBA, 0xE3, 0x02, 0xBA, 0xE3, 0x02 },
	};

	/* Fixed TE */
	GS_DCS_BUF_ADD_CMD(dev, 0xB9, 0x29);
	/* TE width */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x08, 0xB9);
	GS_DCS_BUF_ADD_CMDLIST(dev, fixed_te_settings[panel_type]);

	ctx->hw_status.te.option = TEX_OPT_FIXED;
}

static void pkcgda_set_panel_lp_feat_freq(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	unsigned long *feat = ctx->sw_status.feat;
	struct gs_panel_status *sw_status = &ctx->sw_status;
	u32 idle_vrefresh = sw_status->idle_vrefresh;
	bool is_auto = (test_bit(FEAT_FRAME_AUTO, feat)) ? true : false;

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

static void pkcgda_set_panel_lp_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);

	pkcgda_set_panel_lp_feat_te(ctx);
	pkcgda_set_panel_lp_feat_freq(ctx, pmode);
	pkcgda_set_panel_feat_tsp_sync(ctx, pmode, gs_drm_mode_te_freq(&pmode->mode));
	pkcgda_set_panel_feat_opec_setting(ctx, pmode);

	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
}

static void pkcgda_update_refresh_ctrl_feat(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	const u32 ctrl = ctx->refresh_ctrl;
	unsigned long *feat = ctx->sw_status.feat;
	u32 min_vrefresh = ctx->sw_status.idle_vrefresh;
	u32 vrefresh;
	bool lp_mode;
	bool idle_vrefresh_changed = false;

	if (!pmode)
		return;

	dev_dbg(ctx->dev, "refresh_ctrl=0x%X\n", ctrl);

	vrefresh = drm_mode_vrefresh(&pmode->mode);
	lp_mode = pmode->gs_mode.is_lp_mode;

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

	if (ctrl & GS_PANEL_REFRESH_CTRL_EARLY_EXIT)
		set_bit(FEAT_EARLY_EXIT, feat);
	else {
		clear_bit(FEAT_EARLY_EXIT, feat);
		clear_bit(FEAT_FRAME_AUTO, feat);
		clear_bit(FEAT_FRAME_MANUAL_FI, feat);
	}

	if (lp_mode) {
		pkcgda_set_panel_lp_feat(ctx, pmode);
		return;
	}

	PANEL_ATRACE_INT_PID_FMT(ctx->sw_status.idle_vrefresh, ctx->trace_pid,
				 "idle_vrefresh[%s]", ctx->panel_model);
	PANEL_ATRACE_INT_PID_FMT(test_bit(FEAT_FRAME_AUTO, feat), ctx->trace_pid,
				 "FEAT_FRAME_AUTO[%s]", ctx->panel_model);
	PANEL_ATRACE_INT_PID_FMT(test_bit(FEAT_EARLY_EXIT, feat), ctx->trace_pid,
				 "FEAT_EARLY_EXIT[%s]", ctx->panel_model);

	pkcgda_set_panel_feat(ctx, pmode, false);

#ifdef PANEL_FACTORY_BUILD
	if (idle_vrefresh_changed && min_vrefresh) {
		/* min_vrefresh is the same as refresh rate in factory commands. */
		pkcgda_set_te2_freq(ctx, min_vrefresh);
		notify_panel_te2_freq_changed(ctx, 0);
	}
#endif
}

static void pkcgda_refresh_ctrl(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	const u32 ctrl = ctx->refresh_ctrl;

	PANEL_ATRACE_BEGIN(__func__);

	pkcgda_update_refresh_ctrl_feat(ctx, ctx->current_mode);

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

static void pkcgda_write_display_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	u8 val = PKCGDA_WRCTRLD_BCTRL_BIT;

	PANEL_ATRACE_BEGIN(__func__);
	if (pmode->gs_mode.is_lp_mode)
		val |= PKCGDA_WRCTRLD_LPM_BIT;
	else if (ctx->dimming_on)
		val |= PKCGDA_WRCTRLD_DIMMING_BIT;

	dev_dbg(dev, "wrctrld:0x%x, dimming: %u, aod: %u\n", val,
		ctx->dimming_on, pmode->gs_mode.is_lp_mode);

	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_WRITE_CONTROL_DISPLAY, val);
	PANEL_ATRACE_END(__func__);
}

static int pkcgda_set_brightness(struct gs_panel *ctx, u16 br)
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
	ret = gs_dcs_set_brightness(ctx, brightness);
	if (!ret)
		ctx->hw_status.dbv = br;

	return ret;
}

static void pkcgda_wait_for_vsync_done(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	PANEL_ATRACE_BEGIN(__func__);
	gs_panel_wait_for_vsync_done(ctx, pmode->gs_mode.te_usec,
					 GS_VREFRESH_TO_PERIOD_USEC(ctx->hw_status.vrefresh));
	PANEL_ATRACE_END(__func__);
}

static void pkcgda_enforce_manual_and_peak(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	if (!ctx->current_mode)
		return;

	dev_dbg(dev, "enforce_manual_and_peak\n");

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	/* manual mode */
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
	/* peak refresh rate */
	GS_DCS_BUF_ADD_CMD(dev, 0x83, 0x00);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
}

static const struct gs_dsi_cmd pkcgda_set_lp_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(unlock_cmd_f0),
	/* HLPM Setting */
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x05, 0xBD),
	GS_DSI_QUEUE_CMD(0xBD, 0x3C, 0xC2),
	/* NS mode during lp init sequence */
	GS_DSI_QUEUE_CMD(0x83, 0x18),
	/* Enable early exit */
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x03, 0xBD),
	GS_DSI_QUEUE_CMD(0xBD, 0x65),
	GS_DSI_QUEUE_CMDLIST(panel_update),
	GS_DSI_FLUSH_CMDLIST(lock_cmd_f0),
};
static DEFINE_GS_CMDSET(pkcgda_set_lp);

static void pkcgda_set_lp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	const u16 brightness = gs_panel_get_brightness(ctx);

	dev_dbg(dev, "set_lp_mode\n");

	PANEL_ATRACE_BEGIN(__func__);
	pkcgda_wait_for_vsync_done(ctx, ctx->current_mode);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	/* Manual Mode */
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
	if (ctx->dimming_on)
		GS_DCS_BUF_ADD_CMDLIST(dev, aod_on_smooth);
	else
		GS_DCS_BUF_ADD_CMDLIST(dev, aod_on_normal);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	gs_panel_send_cmdset(ctx, &pkcgda_set_lp_cmdset);
	pkcgda_update_refresh_ctrl_feat(ctx, pmode);
	gs_panel_set_binned_lp_helper(ctx, brightness);

	ctx->sw_status.te.freq_hz = 30;
	ctx->sw_status.te.option = TEX_OPT_FIXED;

	PANEL_ATRACE_END(__func__);

	dev_info(dev, "enter %dhz LP mode\n", drm_mode_vrefresh(&pmode->mode));
}

static void pkcgda_set_nolp_mode(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	struct device *dev = ctx->dev;
	bool is_ns_mode = gs_drm_mode_te_freq(&pmode->mode) == 60;
	bool high_pwm_mode = test_bit(FEAT_PWM_HIGH, ctx->sw_status.feat);
	u8 freq_val = is_ns_mode ? 0x08 :
		high_pwm_mode ? 0x10 : 0x00;
	u8 vrr_val = is_ns_mode ? 0x14 :
		high_pwm_mode ? 0x28 : 0x00;

	dev_dbg(dev, "set_nolp_mode\n");

	PANEL_ATRACE_BEGIN(__func__);
	pkcgda_wait_for_vsync_done(ctx, ctx->current_mode);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	/* Manual Mode */
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x01);
	GS_DCS_BUF_ADD_CMDLIST(dev, aod_off);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, freq_val);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, 0x00, 0x05, 0xBD);
	GS_DCS_BUF_ADD_CMD(dev, 0xBD, vrr_val);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
	pkcgda_update_refresh_ctrl_feat(ctx, pmode);
	pkcgda_set_panel_feat(ctx, pmode, true);
	pkcgda_write_display_mode(ctx, pmode);
	pkcgda_change_frequency(ctx, pmode);

	PANEL_ATRACE_END(__func__);

	dev_info(dev, "exit LP mode\n");
}

static void pkcgda_pre_update_ffc(struct gs_panel *ctx)
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

static void pkkda_update_ffc(struct gs_panel *ctx, unsigned int hs_clk_mbps)
{
	struct device *dev = ctx->dev;

	dev_info(dev, "updating pkkda FFC for hs_clk_mbps=%d\n", hs_clk_mbps);

	PANEL_ATRACE_BEGIN(__func__);

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_fc);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x3E, 0xC5);
	if (hs_clk_mbps == PKKDA_MIPI_DSI_FREQ_MBPS_DEFAULT)
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x11, 0x10, 0x50, 0x05, 0x42, 0x33);
	else /* PKKDA_MIPI_DSI_FREQ_MBPS_ALTERNATIVE */
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x11, 0x10, 0x50, 0x05, 0x43, 0x48);
	GS_DCS_BUF_ADD_CMDLIST(dev, lock_cmd_fc);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	PANEL_ATRACE_END(__func__);
}

static void cgyda_update_ffc(struct gs_panel *ctx, unsigned int hs_clk_mbps)
{
	struct device *dev = ctx->dev;

	dev_info(dev, "updating cgyda FFC for hs_clk_mbps=%d\n", hs_clk_mbps);

	PANEL_ATRACE_BEGIN(__func__);

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_fc);

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x42, 0xC5);
	if (hs_clk_mbps == CGYDA_MIPI_DSI_FREQ_MBPS_DEFAULT)
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x41, 0x27);
	else /* CGYDA_MIPI_DSI_FREQ_MBPS_ALTERNATIVE */
		GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x10, 0x10);

	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x3E, 0xC5);
	GS_DCS_BUF_ADD_CMD(dev, 0xC5, 0x11, 0x10, 0x50, 0x05);
	GS_DCS_BUF_ADD_CMDLIST(dev, lock_cmd_fc);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	PANEL_ATRACE_END(__func__);
}

static void pkcgda_update_ffc(struct gs_panel *ctx, unsigned int hs_clk_mbps)
{
	struct device *dev = ctx->dev;

	dev_dbg(dev, "hs_clk_mbps: current=%d, target=%d\n", ctx->dsi_hs_clk_mbps,
		hs_clk_mbps);

	if (ctx->dsi_hs_clk_mbps == hs_clk_mbps && ctx->ffc_en)
		return;

	if (GET_PANEL_TYPE(ctx) == PANEL_TYPE_PKKDA) {
		if (hs_clk_mbps != PKKDA_MIPI_DSI_FREQ_MBPS_DEFAULT &&
			hs_clk_mbps != PKKDA_MIPI_DSI_FREQ_MBPS_ALTERNATIVE) {
			dev_warn(dev, "invalid hs_clk_mbps=%d for pkkda FFC\n", hs_clk_mbps);
			return;
		}
		pkkda_update_ffc(ctx, hs_clk_mbps);
	} else { /* PANEL_TYPE_CGYDA */
		if (hs_clk_mbps != CGYDA_MIPI_DSI_FREQ_MBPS_DEFAULT &&
			hs_clk_mbps != CGYDA_MIPI_DSI_FREQ_MBPS_ALTERNATIVE) {
			dev_warn(dev, "invalid hs_clk_mbps=%d for cgyda FFC\n", hs_clk_mbps);
			return;
		}
		cgyda_update_ffc(ctx, hs_clk_mbps);
	}

	ctx->dsi_hs_clk_mbps = hs_clk_mbps;
	ctx->ffc_en = true;
}

static void pkcgda_set_ssc_en(struct gs_panel *ctx, bool enabled)
{
	struct device *dev = ctx->dev;
	const bool ssc_mode_update = ctx->ssc_en != enabled;

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

static const struct gs_dsi_cmd pkcgda_init_cmds[] = {
	/* Enable TE */
	GS_DSI_QUEUE_CMD(MIPI_DCS_SET_TEAR_ON),
	/* Sleep out */
	GS_DSI_FLUSH_DELAY_CMD(120, MIPI_DCS_EXIT_SLEEP_MODE),
};
static DEFINE_GS_CMDSET(pkcgda_init);

static const struct gs_dsi_cmd pkcgda_collision_detection_cmds[] = {
	GS_DSI_QUEUE_CMDLIST(unlock_cmd_f0),
	GS_DSI_QUEUE_CMDLIST(unlock_cmd_fc),
	GS_DSI_QUEUE_CMD(0xE5, 0x1D),
	GS_DSI_QUEUE_CMD(0xB0, 0x00, 0x01, 0xF8),
	GS_DSI_QUEUE_CMD(0xF8, 0x04),
	GS_DSI_QUEUE_CMDLIST(lock_cmd_fc),
	GS_DSI_FLUSH_CMDLIST(lock_cmd_f0),
};
static DEFINE_GS_CMDSET(pkcgda_collision_detection);

static void pkcgda_set_scaler_settings(struct gs_panel *ctx, bool is_fhd)
{
	struct device *dev = ctx->dev;
	enum pkcgda_panel_type panel_type = GET_PANEL_TYPE(ctx);
	int res_index = is_fhd ? 0 : 1;

	static const u8 column_settings[PANEL_TYPE_MAX][NUM_SUPPORTED_RESOLUTIONS][5] = {
		{ /* CGYDA */
			{ MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37 },
			{ MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0xFF },
		},
		{  /* PKKDA */
			{ MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x04, 0x37 },
			{ MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x05, 0x3F },
		},
	};

	static const u8 page_settings[PANEL_TYPE_MAX][NUM_SUPPORTED_RESOLUTIONS][5] = {
		{ /* CGYDA */
			{ MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x69 },
			{ MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x0B, 0x27 },
		},
		{ /* PKKDA */
			{ MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x09, 0x63 },
			{ MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x0B, 0xAF },
		},
	};

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMD(dev, 0xC3, is_fhd ? 0x33 : 0x02);
	GS_DCS_BUF_ADD_CMDLIST(dev, column_settings[panel_type][res_index]);
	GS_DCS_BUF_ADD_CMDLIST(dev, page_settings[panel_type][res_index]);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
};

static void pkcgda_proto_set_opec_ip_wa(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	enum pkcgda_panel_type panel_type = GET_PANEL_TYPE(ctx);

	/* OPEC IP workaround P1.0 */
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_fc);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x04, 0x66);
	GS_DCS_BUF_ADD_CMD(dev, 0x66, 0x0B, 0x00, 0xD8, 0xD8, 0xD8, 0xD8, 0x40);
	if (panel_type == PANEL_TYPE_PKKDA) {
		GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x91, 0x68);
		GS_DCS_BUF_ADD_CMD(dev, 0x68, 0x03, 0xE8, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F,
					0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x07,
					0x6C, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F,
					0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x00, 0x00, 0x7F,
					0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F,
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
}

static void pkcgda_set_mfd_settings(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);

	if (ctx->panel_rev_id.id == PANEL_REVID_PROTO1)
		pkcgda_proto_set_opec_ip_wa(ctx);

	/* MFD function */
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0x83);
	GS_DCS_BUF_ADD_CMD(dev, 0x83, 0x08, 0x00, 0x0B,
		GET_PANEL_TYPE(ctx) == PANEL_TYPE_CGYDA ? 0x4C : 0xD4);
	GS_DCS_BUF_ADD_CMDLIST(dev, panel_update);

	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);
}

static int pkcgda_enable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	const struct gs_panel_mode *pmode = ctx->current_mode;
	const struct drm_display_mode *mode;
	const bool needs_init = !gs_is_panel_enabled(ctx);
	enum pkcgda_panel_type panel_type = GET_PANEL_TYPE(ctx);
	bool is_fhd;

	if (!pmode) {
		dev_err(dev, "no current mode set\n");
		return -EINVAL;
	}
	mode = &pmode->mode;
	is_fhd = (panel_type == PANEL_TYPE_CGYDA) ? mode->hdisplay == CGYDA_FHD_HDISPLAY :
						   mode->hdisplay == PKKDA_FHD_HDISPLAY;

	dev_info(dev, "enabling using %s\n", is_fhd ? "fhd" : "wqhd");
	gs_panel_first_enable_helper(ctx);

	PANEL_ATRACE_BEGIN(__func__);


	/* wait TE falling for RRS since DSC and framestart must in the same VSYNC */
	if (ctx->mode_in_progress == MODE_RES_IN_PROGRESS ||
		ctx->mode_in_progress == MODE_RES_AND_RR_IN_PROGRESS)
		pkcgda_wait_for_vsync_done(ctx, pmode);

	/* DSC related configuration */
	gs_dcs_write_dsc_config(dev, &pps_configs[panel_type][is_fhd ? 0 : 1]);

	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0x9D, 0x01);

	if (needs_init) {
		gs_panel_send_cmdset(ctx, &pkcgda_init_cmdset);
		gs_panel_send_cmdset(ctx, &pkcgda_collision_detection_cmdset);
		pkcgda_set_mfd_settings(ctx);
		if (!ctx->force_ffc_off)
			pkcgda_update_ffc(ctx, GET_PANEL_TYPE(ctx) == PANEL_TYPE_PKKDA ?
					  PKKDA_MIPI_DSI_FREQ_MBPS_DEFAULT :
					  CGYDA_MIPI_DSI_FREQ_MBPS_DEFAULT);
		pkcgda_te2_setting(ctx);
	}

	pkcgda_set_scaler_settings(ctx, is_fhd);

	if (pmode->gs_mode.is_lp_mode) {
		pkcgda_set_lp_mode(ctx, pmode);
		GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, MIPI_DCS_SET_DISPLAY_ON);
	} else {
		pkcgda_update_refresh_ctrl_feat(ctx, pmode);
		pkcgda_update_panel_feat(ctx, true);
		pkcgda_write_display_mode(ctx, pmode); /* dimming */
		pkcgda_change_frequency(ctx, pmode);

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

static int pkcgda_disable(struct drm_panel *panel)
{
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct device *dev = ctx->dev;
	struct pkcgda_panel *spanel = to_spanel(ctx);
	int ret;

	dev_info(dev, "disable\n");

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
	ctx->hw_status.dbv = 0;
	ctx->hw_status.irc_mode = IRC_FLAT_DEFAULT;
	ctx->ffc_en = false;
	spanel->prev_gram_collision_count = 0;

	/* set manual and peak before turning off display */
	pkcgda_enforce_manual_and_peak(ctx);

	GS_DCS_WRITE_DELAY_CMD(dev, 20, MIPI_DCS_SET_DISPLAY_OFF);

	if (ctx->panel_state == GPANEL_STATE_OFF)
		GS_DCS_WRITE_DELAY_CMD(dev, 100, MIPI_DCS_ENTER_SLEEP_MODE);

	return 0;
}

static void pkcgda_commit_done(struct gs_panel *ctx)
{
	struct pkcgda_panel *spanel = to_spanel(ctx);

	if (ctx->current_mode->gs_mode.is_lp_mode)
		return;

	/* skip idle update if going through RRS */
	if (ctx->mode_in_progress == MODE_RES_IN_PROGRESS ||
		ctx->mode_in_progress == MODE_RES_AND_RR_IN_PROGRESS) {
		dev_dbg(ctx->dev, "RRS in progress, skip commit done\n");
		return;
	}

	if (spanel->pending_skin_temp_handling)
		pkcgda_send_burn_in_comp_cmds(ctx);
}

static void pkcgda_set_hbm_mode(struct gs_panel *ctx, enum gs_hbm_mode mode)
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

	pkcgda_update_panel_feat(ctx, false);
}

static void pkcgda_set_dimming(struct gs_panel *ctx, bool dimming_on)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	ctx->dimming_on = dimming_on;
	if (pmode->gs_mode.is_lp_mode) {
		dev_dbg(ctx->dev, "in lp mode, postpone dimming update to normal mode entry");
		return;
	}
	pkcgda_write_display_mode(ctx, pmode);
}

static void pkcgda_mode_set(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	pkcgda_change_frequency(ctx, pmode);
}

static int pkcgda_set_op_hz(struct gs_panel *ctx, unsigned int hz)
{
	return -EPERM;
}

static enum gs_pwm_mode pkcgda_get_pwm_mode(struct gs_panel *ctx)
{
	if (ctx->op_hz == 60)
		return GS_PWM_RATE_STANDARD;

	if (gs_drm_mode_te_freq(&ctx->current_mode->mode) == 480)
		return GS_PWM_RATE_HIGH;

	return ctx->pwm_mode;
}

static int pkcgda_set_pwm_mode(struct gs_panel *ctx, enum gs_pwm_mode mode)
{
	struct device *dev = ctx->dev;

	if (ctx->op_hz == 60) {
		dev_warn(dev, "can't set PWM when NS mode is active\n");
		return -EINVAL;
	}

	if (ctx->current_mode->gs_mode.is_lp_mode) {
		dev_warn(dev, "can't set PWM during LP mode\n");
		return -EINVAL;
	}

	if (gs_drm_mode_te_freq(&ctx->current_mode->mode) == 480) {
		dev_warn(dev, "can't set PWM when 480Hz TE is active\n");
		return -EINVAL;
	}

	if (mode == ctx->pwm_mode)
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

	pkcgda_update_panel_feat(ctx, false);

	PANEL_ATRACE_END("%s(%d)", __func__, mode);

	return 0;
}

static void pkcgda_get_panel_rev(struct gs_panel *ctx, u32 id)
{
	/* extract command 0xDB */
	u8 build_code = (id & 0xFF00) >> 8;
	u8 rev = ((build_code & 0xE0) >> 3) | ((build_code & 0x0C) >> 2);

	gs_panel_get_panel_rev(ctx, rev);
}

static ssize_t pkcgda_get_color_data(struct gs_panel *ctx, char *buf, size_t buf_len)
{
	struct device *dev = ctx->dev;
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	int read_ret = -1;
	u8 read_len = ctx->desc->calibration_desc->color_cal[COLOR_DATA_TYPE_CIE].data_size;
	u32 read_addr = 0x1FC000;
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

static void pkcgda_handle_skin_temperature(struct gs_panel *ctx)
{
	if (ctx->idle_data.self_refresh_active) {
		pkcgda_send_burn_in_comp_cmds(ctx);
	} else {
		struct pkcgda_panel *spanel = to_spanel(ctx);

		spanel->pending_skin_temp_handling = true;
	}
}

/**
 * define TRIGGER_DUMP_FOR_COLLISION_TH - threshold of triggering dumps for GRAM collision
 *
 * If the collision symptom lasts (or the accumulated time) over 200ms, i.e. 24 TEAR_CNT at 120Hz
 * (200/8.3=~24), set the flag, which will trigger dumps for debugging.
 */
#define TRIGGER_DUMP_FOR_COLLISION_TH 24
#define DDIC_MAX_COLLISION_CNT 255
static void pkcgda_detect_gram_collision(struct gs_panel *ctx)
{
	struct device *dev = ctx->dev;
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	u8 collision_cnt = 0;
	int ret;

	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_f0);
	GS_DCS_BUF_ADD_CMDLIST(dev, unlock_cmd_fc);
	GS_DCS_BUF_ADD_CMD(dev, 0xB0, 0x00, 0x01, 0xC4);
	GS_DCS_BUF_ADD_CMD_AND_FLUSH(dev, 0xC4, 0x00);
	ret = mipi_dsi_dcs_read(dsi, PKCGDA_TEAR_CNT_ADDR, &collision_cnt, 1);
	if (ret != 1) {
		dev_warn(dev, "Error reading TEAR_CNT (%d)\n", ret);
	} else {
		struct pkcgda_panel *spanel = to_spanel(ctx);
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

static int pkcgda_detect_fault(struct gs_panel *ctx,
			       const struct gs_panel_detect_fault_desc *fault_desc,
			       bool irq_triggered)
{
	struct device *dev = ctx->dev;
	int ret;

	PANEL_ATRACE_BEGIN("pkcgda_detect_fault");

	if (irq_triggered)
		pkcgda_detect_gram_collision(ctx);
	else
		GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, unlock_cmd_f0);

	ret = gs_panel_detect_fault_helper(ctx, fault_desc, irq_triggered);

	if (irq_triggered)
		GS_DCS_BUF_ADD_CMDLIST(dev, lock_cmd_fc);
	GS_DCS_BUF_ADD_CMDLIST_AND_FLUSH(dev, lock_cmd_f0);

	PANEL_ATRACE_END("pkcgda_detect_fault");

	return ret;
}

static bool pkcgda_is_mode_valid(struct gs_panel *ctx, const struct gs_panel_mode *pmode)
{
	/* TODO: b/491475960 - re-enable NS mode support */
	if (gs_is_ns_op_rate(pmode))
		return false;

	return true;
}

static const u32 pkcgda_bl_range[] = { 12908 };

static void pkcgda_debugfs_init(struct drm_panel *panel, struct dentry *root)
{
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct gs_panel *ctx = container_of(panel, struct gs_panel, base);
	struct dentry *panel_root, *csroot;
	struct pkcgda_panel *spanel;

	if (!ctx)
		return;

	panel_root = debugfs_lookup("panel", root);
	if (!panel_root)
		return;

	csroot = debugfs_lookup("cmdsets", panel_root);
	if (!csroot)
		goto panel_out;

	spanel = to_spanel(ctx);

	gs_panel_debugfs_create_cmdset(csroot, &pkcgda_init_cmdset, "init");
	gs_panel_debugfs_create_cmdset(csroot, &pkcgda_set_lp_cmdset, "set_lp");
	debugfs_create_bool("force_changeable_te", 0644, panel_root, &spanel->force_changeable_te);
	debugfs_create_bool("force_changeable_te2", 0644, panel_root,
				&spanel->force_changeable_te2);
	debugfs_create_bool("force_za_off", 0644, panel_root, &spanel->force_za_off);
	dput(csroot);
panel_out:
	dput(panel_root);
#endif
}

static void pkcgda_panel_init(struct gs_panel *ctx)
{
	const struct gs_panel_mode *pmode = ctx->current_mode;

	gs_panel_send_cmdset(ctx, &pkcgda_collision_detection_cmdset);
	pkcgda_set_mfd_settings(ctx);

	ctx->refresh_ctrl |= GS_PANEL_REFRESH_CTRL_EARLY_EXIT;
	pkcgda_update_refresh_ctrl_feat(ctx, pmode);
	ctx->hw_status.irc_mode = IRC_FLAT_DEFAULT;
#ifndef PANEL_FACTORY_BUILD
	/* default fixed TE2 240Hz */
	ctx->te2.option = TEX_OPT_FIXED;
	ctx->te2.freq_hz = 240;
#endif
	/* FFC is disabled in bootloader */
	ctx->ffc_en = false;

	/* re-init panel to decouple bootloader settings */
	if (pmode) {
		dev_info(ctx->dev, "set mode: %s\n", pmode->mode.name);
		ctx->sw_status.idle_vrefresh = 0;
		pkcgda_set_panel_feat(ctx, pmode, true);
		pkcgda_change_frequency(ctx, pmode);
		pkcgda_update_ffc(ctx, GET_PANEL_TYPE(ctx) == PANEL_TYPE_PKKDA ?
				  PKKDA_MIPI_DSI_FREQ_MBPS_DEFAULT :
				  CGYDA_MIPI_DSI_FREQ_MBPS_DEFAULT);
		pkcgda_te2_setting(ctx);
	}
}

static int pkcgda_panel_probe(struct mipi_dsi_device *dsi)
{
	struct pkcgda_panel *spanel;
	struct gs_panel *ctx;

	spanel = devm_kzalloc(&dsi->dev, sizeof(*spanel), GFP_KERNEL);
	if (!spanel)
		return -ENOMEM;

	ctx = &spanel->base;

	ctx->pwm_mode = GS_PWM_RATE_STANDARD;
	ctx->hw_status.vrefresh = 60;
	ctx->hw_status.te.freq_hz = 60;
	ctx->hw_status.acl_mode = ACL_OFF;
	ctx->hw_status.dbv = 0;
	clear_bit(FEAT_ZA, ctx->hw_status.feat);
	spanel->prev_gram_collision_count = 0;

	return gs_dsi_panel_common_init(dsi, ctx);
}

static int pkcgda_panel_config(struct gs_panel *ctx)
{
	gs_panel_model_init(ctx, PROJECT, 0);

	return gs_panel_update_brightness_desc(&pkcgda_brightness_desc, pkcgda_brt_configs,
						   ARRAY_SIZE(pkcgda_brt_configs),
						   ctx->panel_rev_bitmask);
}

static const struct drm_panel_funcs pkcgda_drm_funcs = {
	.disable = pkcgda_disable,
	.unprepare = gs_panel_unprepare,
	.prepare = gs_panel_prepare_with_reset,
	.enable = pkcgda_enable,
	.get_modes = gs_panel_get_modes,
	.debugfs_init = pkcgda_debugfs_init,
};

static const struct gs_panel_funcs pkcgda_gs_funcs = {
	.set_brightness = pkcgda_set_brightness,
	.set_lp_mode = pkcgda_set_lp_mode,
	.set_nolp_mode = pkcgda_set_nolp_mode,
	.set_binned_lp = gs_panel_set_binned_lp_helper,
	.set_hbm_mode = pkcgda_set_hbm_mode,
	.set_dimming = pkcgda_set_dimming,
	.is_mode_seamless_atomic = gs_panel_is_mode_seamless_atomic_helper,
	.mode_set = pkcgda_mode_set,
	.panel_init = pkcgda_panel_init,
	.panel_config = pkcgda_panel_config,
	.get_panel_rev = pkcgda_get_panel_rev,
	.get_te2_edges = gs_panel_get_te2_edges_helper,
	.set_te2_edges = gs_panel_set_te2_edges_helper,
	.update_te2 = pkcgda_update_te2,
	.commit_done = pkcgda_commit_done,
	.set_self_refresh = pkcgda_set_self_refresh,
	.refresh_ctrl = pkcgda_refresh_ctrl,
	.read_serial = gs_panel_read_slsi_ddic_id,
	.handle_skin_temperature = pkcgda_handle_skin_temperature,
	.pre_update_ffc = pkcgda_pre_update_ffc,
	.set_ssc_en = pkcgda_set_ssc_en,
	.update_ffc = pkcgda_update_ffc,
	.set_te2_freq = pkcgda_set_te2_freq,
	.get_te2_freq = pkcgda_get_te2_freq,
	.set_te2_option = pkcgda_set_te2_option,
	.get_te2_option = pkcgda_get_te2_option,
	.get_color_data = pkcgda_get_color_data,
	.get_pwm_mode = pkcgda_get_pwm_mode,
	.set_pwm_mode = pkcgda_set_pwm_mode,
	.set_op_hz = pkcgda_set_op_hz,
	.detect_fault = pkcgda_detect_fault,
	.is_mode_valid = pkcgda_is_mode_valid,
	.trace_panel_settings_full = gs_panel_trace_settings_full_helper,
};

static struct gs_panel_reg_ctrl_desc pkcgda_reg_ctrl_desc = {
	.reg_ctrl_enable = {
		{PANEL_REG_ID_VCI, 1},
		{PANEL_REG_ID_VDDD, 10},
	},
	.reg_ctrl_disable = {
		{PANEL_REG_ID_VDDD, 10},
		{PANEL_REG_ID_VCI, 1},
	},
};

static struct gs_panel_calibration_desc pkcgda_calibration_desc = {
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

static const struct gs_panel_detect_fault_desc pkcgda_fault_desc = {
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

/**
 * While the proximity is active, we will set the min vrefresh to 30Hz with auto
 * frame insertion. Thus when the display is idle, we will have the refresh rate
 * change from 120Hz to 30Hz. According to the measurement, the pattern is: 3x120Hz
 * frame > 1x60Hz frame > 30Hz. With additional tolerance due to scheduler in the
 * kernel, the delay of notification is estimated to be ~50ms.
*/
#define NOTIFY_TE2_FREQ_CHANGED_WORK_DELAY_MS 50

#define DEFINE_PKCGDA_PANEL_DESC(NAME, MODES, LP_MODES, BINNED_LP, MIPI_DSI_FREQ)		\
static struct gs_panel_desc NAME = {								\
	.data_lane_cnt = 4,									\
	.dbv_extra_frame = true,								\
	.brightness_desc = &pkcgda_brightness_desc,						\
	.calibration_desc = &pkcgda_calibration_desc,						\
	.reg_ctrl_desc = &pkcgda_reg_ctrl_desc,							\
	/* supported HDR format bitmask : 1(DOLBY_VISION), 2(HDR10), 3(HLG) */			\
	.hdr_formats = BIT(2) | BIT(3),								\
	.bl_range = pkcgda_bl_range,								\
	.bl_num_ranges = ARRAY_SIZE(pkcgda_bl_range),						\
	.modes = &MODES,									\
	.lp_modes = &LP_MODES,									\
	.lp_cmdset = &pkcgda_set_lp_cmdset,							\
	.binned_lp = BINNED_LP,									\
	.num_binned_lp = ARRAY_SIZE(BINNED_LP),							\
	.rr_switch_duration = 1,								\
	.has_off_binned_lp_entry = false,							\
	.is_idle_supported = true,								\
	.panel_func = &pkcgda_drm_funcs,							\
	.gs_panel_func = &pkcgda_gs_funcs,							\
	.default_dsi_hs_clk_mbps = MIPI_DSI_FREQ,						\
	.reset_timing_ms = { -1, -1, 10, 10 },							\
	.notify_te2_freq_changed_work_delay_ms = NOTIFY_TE2_FREQ_CHANGED_WORK_DELAY_MS, 	\
	.fault_desc = &pkcgda_fault_desc,							\
}

DEFINE_PKCGDA_PANEL_DESC(gs_cgyda, cgyda_modes, cgyda_lp_modes, pkcgda_binned_lp,
			   CGYDA_MIPI_DSI_FREQ_MBPS_DEFAULT);
DEFINE_PKCGDA_PANEL_DESC(gs_pkkda, pkkda_modes, pkkda_lp_modes, pkcgda_binned_lp,
			   PKKDA_MIPI_DSI_FREQ_MBPS_DEFAULT);

static const struct of_device_id gs_panel_of_match[] = {
	{ .compatible = "google,gs-cgyda", .data = &gs_cgyda },
	{ .compatible = "google,gs-pkkda", .data = &gs_pkkda },
	{ }
};
MODULE_DEVICE_TABLE(of, gs_panel_of_match);

static struct mipi_dsi_driver gs_panel_driver = {
	.probe = pkcgda_panel_probe,
	.remove = gs_dsi_panel_common_remove,
	.driver = {
		.name = "panel-gs-pkcgda",
		.of_match_table = gs_panel_of_match,
	},
};
module_mipi_dsi_driver(gs_panel_driver);

MODULE_AUTHOR("Safayat Ullah <safayat@google.com>");
MODULE_DESCRIPTION("MIPI-DSI based Google PKCGDA panel driver");
MODULE_LICENSE("Dual MIT/GPL");
