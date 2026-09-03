/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_DRM_H__
#define __VS_DRM_H__

#include <drm/drm.h>

/* Alignment with a power of two value. */
#define VS_ALIGN(n, align) (((n) + ((align) - 1)) & ~((align) - 1))

/* FilterBlt information. */
#define VS_MAXKERNELSIZE 9
#define VS_SUBPIXELINDEXBITS 5

#define VS_SUBPIXELCOUNT (1 << VS_SUBPIXELINDEXBITS)

#define VS_SUBPIXELLOADCOUNT (VS_SUBPIXELCOUNT / 2 + 1)

#define VS_MAX_Y2R_COEF_NUM 15
#define VS_MAX_R2Y_COEF_NUM 15
#define VS_MAX_GAMUT_COEF_NUM 12

#define VS_MAX_LUT_SEG_CNT 10
#define VS_MAX_LUT_ENTRY_CNT 129
#define VS_MAX_PRIOR_3DLUT_SIZE 4913
#define VS_MAX_ROI_3DLUT_SIZE 729
#define VS_MAX_1D_LUT_ENTRY_CNT 129
#define VS_MAX_GAMMA_ENTRY_CNT 257
#define VS_MAX_GAMMA_EX_ENTRY_CNT 300
#define VS_MAX_COLOR_BAR_NUM 16

#define VS_LTM_LUMA_COEF_NUM 5
#define VS_LTM_FREQ_COEF_NUM 9
#define VS_LTM_FREQ_NORM_FRAC_BIT 18
#define VS_LTM_AFFINE_LUT_NUM 1152 /* 12x12x8 */
#define VS_LTM_AFFINE_SLICE_NUM 2
#define VS_LTM_AFFINE_OUT_SCALE_SIZE 17
#define VS_LTM_TONE_ADJ_COEF_NUM 129
#define VS_LTM_ALPHA_GAIN_SIZE 121
#define VS_LTM_ALPHA_LUMA_SIZE 129
#define VS_LTM_SATU_CTRL_SIZE 257
#define VS_LTM_XGAMMA_COEF_NUM 65
#define VS_LTM_DITHER_COEF_NUM 3
#define VS_LTM_CD_COEF_NUM 4
#define VS_LTM_CD_THRESH_NUM 4
#define VS_LTM_CD_SLOPE_NUM 2
#define VS_LTM_CD_RESULT_NUM 256
#define VS_LTM_HIST_POS_NUM 2
#define VS_LTM_HIST_SCALE_NUM 2
#define VS_LTM_HIST_RESULT_NUM 9216 /* 12x12x64 */
#define VS_HIST_RESULT_BIN_CNT 256

#define VS_SHARPNESS_CSC_COEF_NUM 9
#define VS_SHARPNESS_CSC_OFFSET_NUM 3
#define VS_SHARPNESS_LUMA_GAIN_LUT_ENTRY_NUM 9
#define VS_SHARPNESS_CA_MODE_NUM 3
#define VS_SHARPNESS_CA_PARAM_NUM 7
#define VS_SHARPNESS_LPF_COEF_NUM 4
#define VS_SHARPNESS_LPF_NOISE_LUT_NUM 9
#define VS_SHARPNESS_LPF_CURVE_LUT_NUM 13
#define VS_SHARPNESS_LPF_NORM_BPP 16

#define VS_RANDOM_DITHER_SEED_NUM 8

#define VS_SCALE_HORI_COEF_NUM 77
#define VS_SCALE_VERT_COEF_NUM 43

#define VS_MAX_ROI_CNT 2

#define VS_BLEND_ALPHA_OPAQUE 0x3ff

/* Max blur ROI width is 1440, min blur width is 100 */
#define VS_BLUR_COEF_NUM 4
#define VS_BLUR_COEF_SUM_MIN 1
#define VS_BLUR_COEF_SUM_MAX 64
#define VS_BLUR_NORM_BPP 16
#define VS_BLUR_ROI_MAX_WIDTH 1440
#define VS_BLUR_ROI_MIN_WIDTH 100

/* brightness: Max target gain is 2^10, max brightness value is 2^14 - 1 */
#define VS_MAX_TARGET_GAIN_VALUE 1024
#define VS_MAX_BRIGHTNESS_VALUE 16383

#define DEGAMMA_SIZE 260

enum drm_vs_degamma_mode {
	VS_DEGAMMA_BT709 = 0,
	VS_DEGAMMA_BT2020 = 1,
	VS_DEGAMMA_USR = 2,
};

struct drm_vs_degamma_config {
	enum drm_vs_degamma_mode mode;
	__u16 r[DEGAMMA_SIZE];
	__u16 g[DEGAMMA_SIZE];
	__u16 b[DEGAMMA_SIZE];
};

enum drm_vs_sync_dc_mode {
	VS_SINGLE_DC = 0,
	VS_MULTI_DC_PRIMARY = 1,
	VS_MULTI_DC_SECONDARY = 2,
};

enum drm_vs_dma_mode {
	/* read full image */
	VS_DMA_NORMAL = 0,
	/* read one ROI region in the image */
	VS_DMA_ONE_ROI = 1,
	/* read two ROI regions in the image */
	VS_DMA_TWO_ROI = 2,
	/* skip the ROI region in the image */
	VS_DMA_SKIP_ROI = 3,
	/* for extend layer mode
	 * read full image0 and image1, don't.
	 */
	VS_DMA_EXT_LAYER = 4,
	/* for extend layer mode
	 * read ROI region from image0 and
	 * read exrended ROI region from image1.
	 */
	VS_DMA_EXT_LAYER_EX = 5,
};

enum drm_vs_line_padding_mode {
	/*ratio 1:1*/
	VS_DMA_LINE_PADDING_1TO1 = 0,
	/*ratio 2:1*/
	VS_DMA_LINE_PADDING_2TO1 = 1,
	/*ratio 3:1*/
	VS_DMA_LINE_PADDING_3TO1 = 2,
	/*ratio 3:2*/
	VS_DMA_LINE_PADDING_3TO2 = 3,
	/*ratio 4:1*/
	VS_DMA_LINE_PADDING_4TO1 = 4,
	/*ratio 4:3*/
	VS_DMA_LINE_PADDING_4TO3 = 5,
	/*ratio 5:2*/
	VS_DMA_LINE_PADDING_5TO2 = 6,
	/*ratio 5:3*/
	VS_DMA_LINE_PADDING_5TO3 = 7,
	/*ratio 8:5*/
	VS_DMA_LINE_PADDING_8TO5 = 8,
};

enum drm_vs_sbs_mode {
	VS_SBS_LEFT = 0,
	VS_SBS_RIGHT = 1,
	VS_SBS_SPLIT = 2,
	VS_SBS_RESERVED = 3,
};

enum drm_vs_alpha_mode {
	VS_ALPHA_NORMAL = 0,
	VS_ALPHA_INVERSE = 1,
};

enum drm_vs_galpha_mode {
	VS_GALPHA_NORMAL = 0,
	VS_GALPHA_GLOBAL = 1,
	VS_GALPHA_MULTIPLE = 2,
};

enum drm_vs_ltm_luma_mode {
	VS_LTM_LUMA_GRAY = 0,
	VS_LTM_LUMA_LIGHTNESS = 1,
	VS_LTM_LUMA_MIXED = 2,
};

enum drm_vs_blend_mode {
	VS_BLD_CLR = 0,
	VS_BLD_SRC = 1,
	VS_BLD_DST = 2,
	VS_BLD_SRC_OVR = 3,
	VS_BLD_DST_OVR = 4,
	VS_BLD_SRC_IN = 5,
	VS_BLD_DST_IN = 6,
	VS_BLD_SRC_OUT = 7,
	VS_BLD_DST_OUT = 8,
	VS_BLD_SRC_ATOP = 9,
	VS_BLD_DST_ATOP = 10,
	VS_BLD_XOR = 11,
	VS_BLD_PLUS = 12,
	VS_BLD_BLD = 13,
	VS_BLD_UDEF = 14,
};

enum drm_vs_wb_point {
	VS_WB_DISP_IN = 0,
	VS_WB_DISP_CC = 1,
	VS_WB_DISP_OUT = 2,
	VS_WB_OFIFO_IN = 3,
	VS_WB_OFIFO_OUT = 4,
	VS_WB_POS_CNT = 5,
};

enum drm_vs_gamut_mode {
	VS_GAMUT_709_TO_2020 = 0,
	VS_GAMUT_2020_TO_709 = 1,
	VS_GAMUT_2020_TO_DCIP3 = 2,
	VS_GAMUT_DCIP3_TO_2020 = 3,
	VS_GAMUT_DCIP3_TO_SRGB = 4,
	VS_GAMUT_SRGB_TO_DCIP3 = 5,
	VS_GAMUT_USER_DEF = 6,
};

enum drm_vs_csc_mode {
	VS_CSC_CM_USR,
	VS_CSC_CM_L2L,
	VS_CSC_CM_L2F,
	VS_CSC_CM_F2L,
	VS_CSC_CM_F2F,
};

enum drm_vs_csc_gamut {
	VS_CSC_CG_601,
	VS_CSC_CG_709,
	VS_CSC_CG_2020,
	VS_CSC_CG_P3,
	VS_CSC_CG_SRGB,
};

enum drm_vs_calcu_mode {
	VS_CALC_LNR_COMBINE = 0,
	VS_CALC_MAX = 1,
	VS_CALC_MIXED = 2,
};

enum drm_vs_data_extend_mode {
	VS_DATA_EXT_STD = 0,
	VS_DATA_EXT_MSB = 1,
	VS_DATA_EXT_RANDOM = 2,
};

enum drm_vs_data_trunc_mode {
	VS_DATA_TRUNCATE = 0,
	VS_DATA_ROUNDING = 1,
};

enum drm_vs_dth_frm_idx {
	VS_DTH_FRM_IDX_NONE = 0,
	VS_DTH_FRM_IDX_SW = 1,
	VS_DTH_FRM_IDX_HW = 2,
};

enum drm_vs_dth_frm_mode {
	VS_DTH_FRM_4 = 4,
	VS_DTH_FRM_8 = 8,
	VS_DTH_FRM_16 = 16,
};

enum drm_vs_pattern_mode {
	VS_PURE_COLOR = 0,
	VS_COLOR_BAR_H = 1,
	VS_COLOR_BAR_V = 2,
	VS_RMAP_H = 3,
	VS_RMAP_V = 4,
	VS_BLACK_WHITE_H = 5,
	VS_BLACK_WHITE_V = 6,
	VS_BLACK_WHITE_SQR = 7,
	VS_BORDER_PATRN = 8,
	VS_CURSOR_PATRN = 9,
};

enum drm_vs_disp_tp_pos {
	VS_DISP_TP_BLD = 0,
	VS_DISP_TP_POST_PROC = 1,
	VS_DISP_TP_OFIFO = 2,
};

enum drm_vs_feature_cap_type {
	VS_FEATURE_CAP_FBC = 0,
	VS_FEATURE_CAP_MAX_BLEND_LAYER,
	VS_FEATURE_CAP_CURSOR_WIDTH,
	VS_FEATURE_CAP_CURSOR_HEIGHT,
	VS_FEATURE_CAP_LINEAR_YUV_ROTATION,
	VS_FEATURE_CAP_ANY_RESOLUTION,
	VS_FEATURE_CAP_MAX_WIDTH,
	VS_FEATURE_CAP_MAX_HEIGHT,
};

enum drm_vs_hw_cap_type {
	VS_HW_CAP_CHIP_ID = 0,
	VS_HW_CAP_CHIP_REV,
	VS_HW_CAP_CHIP_PID,
	VS_HW_CAP_CHIP_CID,
	VS_HW_CAP_PITCH_ALIGNEMENT,
	VS_HW_CAP_ADDR_ALIGNEMENT,
	VS_HW_CAP_FE0_DMA_SRAM_SIZE,
	VS_HW_CAP_FE1_DMA_SRAM_SIZE,
	VS_HW_CAP_FE0_SCL_SRAM_SIZE,
	VS_HW_CAP_FE1_SCL_SRAM_SIZE,
	VS_HW_CAP_MAX_BLEND_LAYER,
	VS_HW_CAP_MAX_EXT_LAYER,
	VS_HW_CAP_MAX_SEG_NUM,
	VS_HW_CAP_MAX_EOTF_SIZE,
	VS_HW_CAP_MAX_TONEMAP_SIZE,
	VS_HW_CAP_MAX_OETF_SIZE,
	VS_HW_CAP_MAX_DEGAMMA_SIZE,
	VS_HW_CAP_MAX_GAMMA_SIZE,
	VS_HW_CAP_CGM_LUT_SIZE,
	VS_HW_CAP_CGM_EX_LUT_SIZE,
	VS_HW_CAP_PRE_OETF_BITS,
	VS_HW_CAP_HDR_BITS,
	VS_HW_CAP_OETF_BITS,
	VS_HW_CAP_BLD_CGM_BITS,
	VS_HW_CAP_PRE_DEGAMMA_BITS,
	VS_HW_CAP_DEGAMME_BITS,
	VS_HW_CAP_CGM_LUT_BITS,
	VS_HW_CAP_GAMM_BITS,
	VS_HW_CAP_BLUR_COEF_BITS,
	VS_HW_CAP_PPC,
	VS_HW_CAP_VBLANK_MARGIN_PCT,
	VS_HW_CAP_H_BUBBLE_PCT,
	VS_HW_CAP_ROTATION_PREFETCH_LINE,
	VS_HW_CAP_ROTATION_PIPELINE_DELAY_US,
	VS_HW_CAP_ROTATION_PIPELINE_LATENCY_US,
	VS_HW_CAP_ROTATION_NOMINAL_CYCLE_NUM,
	VS_HW_CAP_ROTATION_NOMINAL_CYCLE_DENOM,
	VS_HW_CAP_AXI_BUS_BIT_WIDTH,
	VS_HW_CAP_AXI_BUS_UTIL_PCT,
	VS_HW_CAP_COUNT,
};

enum drm_vs_fe_id {
	VS_FE_0,
	VS_FE_1,
	VS_FE_NONE,
};

enum drm_vs_sharpness_ink_mode {
	VS_SHARPNESS_INK_DEFAULT = 0x0,
	VS_SHARPNESS_INK_G0 = 0x1,
	VS_SHARPNESS_INK_G1 = 0x2,
	VS_SHARPNESS_INK_G2 = 0x3,
	VS_SHARPNESS_INK_L0 = 0x4,
	VS_SHARPNESS_INK_L1 = 0x5,
	VS_SHARPNESS_INK_L2 = 0x6,
	VS_SHARPNESS_INK_ADAPT = 0x7,
	VS_SHARPNESS_INK_V0 = 0x8,
	VS_SHARPNESS_INK_V1 = 0x9,
	VS_SHARPNESS_INK_V2 = 0xA,
	VS_SHARPNESS_INK_LUMA = 0xB,
};

enum drm_vs_brightness_cal_mode {
	VS_BRIGHTNESS_CAL_MODE_WEIGHT = 0,
	VS_BRIGHTNESS_CAL_MODE_MAX = 1,
	VS_BRIGHTNESS_CAL_MODE_COUNT = 2,
};

/* Brightness Compensation ROI */
enum drm_vs_brightness_roi_type {
	VS_BRIGHTNESS_ROI0 = 0,
	VS_BRIGHTNESS_ROI1 = 1,
};

enum drm_vs_gem_query_type {
	VS_GEM_QUERY_HANDLE = 0,
};

enum drm_vs_mask_blend_type {
	VS_MASK_BLD_NORM,
	VS_MASK_BLD_INV,
	VS_MASK_BLD_COUNT,
};

enum drm_vs_rcd_bg_type {
	VS_RCD_BG_PNL,
	VS_RCD_BG_ROI,
	VS_RCD_BG_COUNT,
};

enum drm_vs_rcd_roi_type {
	VS_RCD_ROI_TOP,
	VS_RCD_ROI_BTM,
	VS_RCD_ROI_COUNT,
};

enum dc_hw_sram_unit_size {
	SRAM_UNIT_SIZE_64KB,
	SRAM_UNIT_SIZE_32KB,
};

/**
 * enum drm_vs_power_off_mode - list of CRTC POWER_OFF_MODE property values
 * VS_POWER_OFF_MODE_FULL:    indicates a FULL power ON/OFF mode.
 * VS_POWER_OFF_MODE_PSR:     indicates a PSR entry when powering OFF.
 */
enum drm_vs_power_off_mode {
	VS_POWER_OFF_MODE_FULL,
	VS_POWER_OFF_MODE_PSR,
	VS_POWER_OFF_MODE_COUNT,
};

/**
 * enum drm_vs_task_fence_ops - list of task fence operations
 * VS_TASK_FENCE_CREATE: create a new fence
 * VS_TASK_FENCE_SIGNAL: signal the fence with an error code
 */
enum drm_vs_task_fence_ops {
	VS_TASK_FENCE_CREATE = 0,
	VS_TASK_FENCE_SIGNAL = 1,
};

struct drm_vs_alpha_data_extend {
	__u32 alpha_extend_value; /* alpha3[31:24], alpha2[23:16], alpha1[15:8], alpha0[7:0] */
};

struct drm_vs_data_extend {
	enum drm_vs_data_extend_mode data_extend_mode;
	struct drm_vs_alpha_data_extend alpha_data_extend;
};

struct drm_vs_splice_config {
	__u8 crtc_id;
	__u8 crtc_id_ex;
	__u8 crtc_ofifo_id;
	__u8 crtc_ofifo_id_ex;
};

struct drm_vs_splice_mode {
	bool splice0_enable;
	__u8 splice0_crtc_mask; /* crtc id mask */
	__u8 splice0_output_intf;
	bool splice1_enable;
	__u8 splice1_crtc_mask; /* crtc id mask */
	__u8 splice1_output_intf;
	__u16 src_panel_width0;
	__u16 src_panel_width1;
};

struct drm_vs_dp_sync {
	__u8 dp_sync_crtc_mask; /* crtc id mask */
};

enum drm_vs_free_sync_type {
	VS_FREE_SYNC_CONFIG,
	VS_FREE_SYNC_FINISH,
	VS_FREE_SYNC_CONFIG_FINISH,
};

struct drm_vs_free_sync_mode {
	__u16 free_sync_max_delay; /* max delay lines number */
	bool free_sync_finish;
};

struct drm_vs_free_sync {
	enum drm_vs_free_sync_type type;
	struct drm_vs_free_sync_mode mode;
};

/* Sharpness */
struct drm_vs_sharpness {
	bool enable;
	enum drm_vs_sharpness_ink_mode ink_mode;
};

struct drm_vs_sharpness_csc {
	int y2r_coef[VS_SHARPNESS_CSC_COEF_NUM];
	int r2y_coef[VS_SHARPNESS_CSC_COEF_NUM];
	int y2r_offset[VS_SHARPNESS_CSC_OFFSET_NUM];
	int r2y_offset[VS_SHARPNESS_CSC_OFFSET_NUM];
};

struct drm_vs_sharpness_luma_gain {
	int lut[VS_SHARPNESS_LUMA_GAIN_LUT_ENTRY_NUM];
};

struct drm_vs_sharpness_lpf {
	__u32 lpf0_coef[VS_SHARPNESS_LPF_COEF_NUM];
	__u32 lpf1_coef[VS_SHARPNESS_LPF_COEF_NUM];
	__u32 lpf2_coef[VS_SHARPNESS_LPF_COEF_NUM];
	__u32 lpf0_norm;
	__u32 lpf1_norm;
	__u32 lpf2_norm;
};

struct drm_vs_sharpness_lpf_noise {
	__u32 lut0[VS_SHARPNESS_LPF_NOISE_LUT_NUM];
	__u32 lut1[VS_SHARPNESS_LPF_NOISE_LUT_NUM];
	__u32 lut2[VS_SHARPNESS_LPF_NOISE_LUT_NUM];
	__u32 luma_strength0;
	__u32 luma_strength1;
	__u32 luma_strength2;
};

struct drm_vs_sharpness_lpf_curve {
	__u32 lut0[VS_SHARPNESS_LPF_CURVE_LUT_NUM];
	__u32 lut1[VS_SHARPNESS_LPF_CURVE_LUT_NUM];
	__u32 lut2[VS_SHARPNESS_LPF_CURVE_LUT_NUM];
	__u32 master_gain;
};

// TODO: Directly pass array to drm driver?
struct drm_vs_sharpness_color_adaptive_mode {
	bool enable;
	__u32 gain;
	__u32 theta_center;
	__u32 theta_range;
	__u32 theta_slope;
	__u32 radius_center;
	__u32 radius_range;
	__u32 radius_slope;
};

struct drm_vs_sharpness_color_adaptive {
	struct drm_vs_sharpness_color_adaptive_mode mode[VS_SHARPNESS_CA_MODE_NUM];
};

struct drm_vs_sharpness_color_boost {
	__u32 pos_gain;
	__u32 neg_gain;
	__u32 y_offset;
};

struct drm_vs_sharpness_soft_clip {
	__u32 pos_offset;
	__u32 neg_offset;
	__u32 pos_wet;
	__u32 neg_wet;
};

/* RGB table */
struct drm_vs_sharpness_dither {
	__u32 table_low[3];
	__u32 table_high[3];
};

enum drm_vs_ds_mode {
	VS_DS_DROP = 0,
	VS_DS_AVERAGE = 1,
	VS_DS_FILTER = 2,
};

struct drm_vs_rect {
	__u16 x;
	__u16 y;
	__u16 w;
	__u16 h;
};

struct drm_vs_color {
	__u32 a;
	__u32 r;
	__u32 g;
	__u32 b;
};

struct drm_vs_color_rgb16 {
	__u16 r;
	__u16 g;
	__u16 b;
};

struct drm_vs_spliter {
	__u32 left_x;
	__u32 left_w;
	__u32 right_x;
	__u32 right_w;
	__u32 src_w;
};

struct drm_vs_panel_crop {
	struct drm_vs_rect crop_rect;
	__u16 panel_src_width;
	__u16 panel_src_height;
};

struct drm_vs_watermark {
	__u32 watermark;
	__u8 qos_low;
	__u8 qos_high;
};

/**
 * struct drm_vs_dma - Used for configuring RoI and Layer Extension modes with
 * the DMA_CONFIG blob property.
 * @mode: The RoI mode.
 * @in_rect: The input RoI rectangle. For all modes  this is the 2nd RoI that is
 *           used. For VS_DMA_SKIP_ROI, this configures the skip region. This
 *           field is required for VS_DMA_TWO_ROI, VS_DMA_SKIP_ROI and
 *           VS_DMA_EXT_LAYER_EX.
 * @out_rect: The output rectangle. For all modes this is the 2nd out region
 *            that is used. This field is required for VS_DMA_TWO_ROI and
 *            VS_DMA_EXT_LAYER_EX.
 *
 * Note that primary RoI for all modes is configured through the
 * Plane Composition Properties (SRC_W, SRC_H, CRTC_W, CRTC_H, etc). This struct
 * is used to configure the 2nd RoI in conjunction with the DMA_CONFIG blob
 * property.
 * Note further that the VS_DMA_NORMAL and VS_DMA_ONE_ROI modes are
 * NOT supported through the DMA_CONFIG blob property, and are instead inferred
 * directly from the Plane Composition Properties. DMA_CONFIG should remain
 * unset if either of these modes are desired.
 */
struct drm_vs_dma {
	enum drm_vs_dma_mode mode;
	struct drm_vs_rect in_rect;
	struct drm_vs_rect out_rect;
};

struct drm_vs_line_padding {
	enum drm_vs_line_padding_mode mode;
	struct drm_vs_color color;
};

struct drm_vs_sbs {
	enum drm_vs_sbs_mode mode;
	/* available under the VS_SBS_SPLIT mode, side-by-side left width. */
	__u16 left_w;
	/* available under the VS_SBS_SPLIT mode, side-by-side right start X. */
	__u16 right_x;
	/* available under the VS_SBS_SPLIT mode, side-by-side right width. */
	__u16 right_w;
};

struct drm_vs_y2r_config {
	__s32 coef[VS_MAX_Y2R_COEF_NUM];
};

struct drm_vs_r2y_config {
	enum drm_vs_csc_mode mode;
	enum drm_vs_csc_gamut gamut;
	__s32 coef[VS_MAX_R2Y_COEF_NUM];
	/* For debug, the output bus format.
	 *     Usually the output bus format info from encoder.
	 *     In our driver, the default output bus format is MEDIA_BUS_FMT_RGB888_1X24
	 *     For the convernience of debugging, adding an output bus format setting here for debugging the
	 *     writeback data.
	 */
	__u32 output_bus_format;
};

/**
 * struct drm_vs_preprocess_scale_config - Used for providing scaling coefficients
 to the preprocessor
 * @coef_h: Horizontal scaling coefficient matrix.
 * @coef_v: Vertical scaling coefficient matrix.
 */
struct drm_vs_preprocess_scale_config {
	__u32 coef_h[VS_SCALE_HORI_COEF_NUM];
	__u32 coef_v[VS_SCALE_VERT_COEF_NUM];
};

struct drm_vs_scale_config {
	bool enable;
	__u16 src_w;
	__u16 src_h;
	__u16 dst_w;
	__u16 dst_h;
	__u32 factor_x;
	__u32 factor_y;
	__u32 coef_h[VS_SCALE_HORI_COEF_NUM];
	__u32 coef_v[VS_SCALE_VERT_COEF_NUM];
};

struct drm_vs_ds_config {
	enum drm_vs_ds_mode h_mode;
	enum drm_vs_ds_mode v_mode;
};

struct drm_vs_roi_lut_config {
	struct drm_vs_rect rect;
	struct drm_vs_color data[VS_MAX_ROI_3DLUT_SIZE];
};

struct drm_vs_gamut_map {
	bool enable;
	enum drm_vs_gamut_mode mode;
	__s32 coef[VS_MAX_GAMUT_COEF_NUM];
};

/*need to refine*/
struct drm_vs_data_block {
	__u32 size; /* total size of data block buffer */
	__u64 logical;
};

struct drm_vs_xstep_lut {
	bool enable;
	__u32 seg_cnt;
	__u32 seg_point[VS_MAX_LUT_SEG_CNT - 1];
	__u32 seg_step[VS_MAX_LUT_SEG_CNT];
	__u32 entry_cnt;
	__u32 data[VS_MAX_LUT_ENTRY_CNT];
};

struct drm_vs_gamma_lut {
	__u32 seg_cnt;
	__u32 seg_point[VS_MAX_LUT_SEG_CNT - 1];
	__u32 seg_step[VS_MAX_LUT_SEG_CNT];
	__u32 entry_cnt;
	struct drm_vs_color data[VS_MAX_GAMMA_ENTRY_CNT];
};

struct drm_vs_lut {
	__u32 entry_cnt;
	__u32 seg_point[VS_MAX_LUT_ENTRY_CNT - 1];
	__u32 data[VS_MAX_LUT_ENTRY_CNT];
};

struct drm_vs_blend_alpha {
	/* src alpha pre process */
	enum drm_vs_alpha_mode sam;
	enum drm_vs_galpha_mode sgam;
	__u32 sga;
	__u32 saa;

	/* dst alpha pre process */
	enum drm_vs_alpha_mode dam;
	enum drm_vs_galpha_mode dgam;
	__u32 dga;
	__u32 daa;
};

struct drm_vs_1d_lut {
	__u32 entry_cnt;
	__u32 data[VS_MAX_1D_LUT_ENTRY_CNT];
};

struct drm_vs_ltm_xgamma {
	__u32 coef[VS_LTM_XGAMMA_COEF_NUM];
};

struct drm_vs_ltm_luma {
	enum drm_vs_ltm_luma_mode mode;
	__u16 coef[VS_LTM_LUMA_COEF_NUM];
};

struct drm_vs_ltm_freq_decomp {
	bool decomp_enable;
	__u16 coef[VS_LTM_FREQ_COEF_NUM];
	__u32 norm;
};

struct drm_vs_ltm_grid {
	__u16 width;
	__u16 height;
	__u16 depth;
};

struct drm_vs_ltm_af_filter {
	bool enable_temporal_filter;
	__u16 weight;
	__u16 slope[VS_LTM_AFFINE_LUT_NUM];
	__u16 bias[VS_LTM_AFFINE_LUT_NUM];
};

struct drm_vs_ltm_af_filter_v2 {
	__u16 weight;
	__u16 slope[VS_LTM_AFFINE_LUT_NUM];
	__u16 bias[VS_LTM_AFFINE_LUT_NUM];
};

struct drm_vs_ltm_af_slice {
	__u16 start_pos[VS_LTM_AFFINE_SLICE_NUM];
	__u16 scale[VS_LTM_AFFINE_SLICE_NUM];
	__u16 scale_half[VS_LTM_AFFINE_SLICE_NUM];
};

struct drm_vs_ltm_af_trans {
	__u8 slope_bit;
	__u8 bias_bit;
	__u16 scale[VS_LTM_AFFINE_OUT_SCALE_SIZE];
};

struct drm_vs_ltm_tone_adj {
	bool luma_from;
	__u32 entry_cnt;
	__u32 data[VS_MAX_1D_LUT_ENTRY_CNT];
};

struct drm_vs_ltm_color {
	bool satu_ctrl;
	__u16 luma_thresh;
	__u16 gain[VS_LTM_ALPHA_GAIN_SIZE];
	__u16 luma[VS_LTM_ALPHA_LUMA_SIZE];
	__u16 satu[VS_LTM_SATU_CTRL_SIZE];
};

struct drm_vs_ltm_dither {
	bool dither_enable;
	__u32 table_low[VS_LTM_DITHER_COEF_NUM];
	__u32 table_high[VS_LTM_DITHER_COEF_NUM];
};

struct drm_vs_ltm_luma_ave {
	__u16 margin_x;
	__u16 margin_y;
	__u16 pixel_norm;
	__u16 ave;
};

struct drm_vs_ltm_luma_ave_set {
	__u16 margin_x;
	__u16 margin_y;
	__u16 pixel_norm;
};

struct drm_vs_ltm_luma_ave_get {
	__u16 ave;
};

struct drm_vs_ltm_cd_set {
	bool enable;
	bool overlap;
	__u32 min_wgt;
	__u32 filt_norm;
	__u32 coef[VS_LTM_CD_COEF_NUM];
	__u32 thresh[VS_LTM_CD_THRESH_NUM];
	__u32 slope[VS_LTM_CD_SLOPE_NUM];
};

struct drm_vs_ltm_cd_get {
	__u32 result[VS_LTM_CD_RESULT_NUM];
};

struct drm_vs_ltm_hist_set {
	bool overlap;
	__u32 grid_depth;
	__u32 start_pos[VS_LTM_HIST_POS_NUM];
	__u32 grid_scale[VS_LTM_HIST_SCALE_NUM];
};

struct drm_vs_ltm_hist_get {
	__u16 result[VS_LTM_HIST_RESULT_NUM];
};

struct drm_vs_ltm_ds {
	__u32 h_norm;
	__u32 v_norm;
	__u32 crop_l;
	__u32 crop_r;
	__u32 crop_t;
	__u32 crop_b;
	struct drm_vs_rect output;
};

struct drm_vs_ltm_histogram_config {
	bool degamma_enable;
	bool gamma_enable;
	bool luma_enable;
	bool grid_enable;
	bool luma_set_enable;
	bool cd_set_enable;
	bool hist_set_enable;
	bool ds_enable;
	struct drm_vs_ltm_xgamma ltm_degamma;
	struct drm_vs_ltm_xgamma ltm_gamma;
	struct drm_vs_ltm_luma ltm_luma;
	struct drm_vs_ltm_grid grid_size;
	struct drm_vs_ltm_luma_ave_set ltm_luma_set;
	struct drm_vs_ltm_cd_set ltm_cd_set;
	struct drm_vs_ltm_hist_set ltm_hist_set;
	struct drm_vs_ltm_ds ltm_ds;
};

struct drm_vs_ltm_histogram_wdma_config {
	// DMA buffer for struct drm_vs_ltm_hist_get
	__u64 hist_dma_addr;
	// DMA buffer for downscaled luma image
	__u64 luma_dma_addr;
};

// TODO: b/409716914 remove this struct after user space switches to v2
struct drm_vs_ltm_rendering_config {
	bool freq_decomp_enable;
	bool luma_adj_enable;
	bool af_slice_enable;
	bool af_trans_enable;
	bool color_enable;
	bool dither_enable;
	struct drm_vs_ltm_freq_decomp freq_decomp;
	struct drm_vs_1d_lut luma_adj;
	struct drm_vs_ltm_af_slice af_slice;
	struct drm_vs_ltm_af_trans af_trans;
	struct drm_vs_ltm_color ltm_color;
	struct drm_vs_ltm_dither ltm_dither;
};

struct drm_vs_ltm_rendering_config_v2 {
	bool freq_decomp_enable;
	bool luma_adj_enable;
	bool af_slice_enable;
	bool af_trans_enable;
	bool color_enable;
	bool dither_enable;
	bool af_temporal_filter_enable;
	struct drm_vs_ltm_freq_decomp freq_decomp;
	struct drm_vs_1d_lut luma_adj;
	struct drm_vs_ltm_af_slice af_slice;
	struct drm_vs_ltm_af_trans af_trans;
	struct drm_vs_ltm_color ltm_color;
	struct drm_vs_ltm_dither ltm_dither;
};

enum drm_vs_ltm_hist_read_mode {
	VS_LTM_HIST_READ_NONE = 0,
	VS_LTM_HIST_READ_CSR = 1,
	VS_LTM_HIST_READ_WDMA = 2,
};

/*
 * struct drm_vs_ltm_histogram_data is used to get the histogram data from
 * the LTM block
 *
 * timeout_ms:
 *   The timeout in ms for waiting the histogram data.
 * crtc_id:
 *   The crtc id for the CRTC that generates the histogram data.
 * read_luma_ave:
 *   The flag to indicate whether to read the luma average data.
 * luma_ave:
 *   The luma average data.
 * read_cd:
 *   The flag to indicate whether to read the content detection data.
 * cd:
 *   The content detection data.
 * read_mode:
 *   The read mode for the histogram data. see enum drm_vs_ltm_hist_read_mode
 * hist_ptr:
 *   The user space pointer of struct drm_vs_ltm_hist_get if read_mode is CSR.
 * hist_dma_addr:
 *   The DMA buffer address that has been just filled if read mode is DMA.
 */
struct drm_vs_ltm_histogram_data {
	__u16 timeout_ms;
	__u8 crtc_id;
	bool read_luma_ave;
	struct drm_vs_ltm_luma_ave_get luma_ave;

	bool read_cd;
	struct drm_vs_ltm_cd_get cd;

	enum drm_vs_ltm_hist_read_mode read_mode;
	__u64 hist_ptr;
	__u64 hist_dma_addr;
};

struct drm_vs_gtm_luma_ave_config {
	bool degamma_enable;
	bool gamma_enable;
	bool luma_enable;
	bool luma_set_enable;
	bool ds_enable;
	struct drm_vs_ltm_xgamma ltm_degamma;
	struct drm_vs_ltm_xgamma ltm_gamma;
	struct drm_vs_ltm_luma ltm_luma;
	struct drm_vs_ltm_luma_ave_set ltm_luma_set;
	struct drm_vs_ltm_ds ltm_ds;
};

struct drm_vs_gtm_rendering_config {
	bool color_enable;
	bool dither_enable;
	struct drm_vs_ltm_color ltm_color;
	struct drm_vs_ltm_dither ltm_dither;
};

struct drm_vs_blend {
	enum drm_vs_blend_mode color_mode;
	enum drm_vs_blend_mode alpha_mode;
};

struct drm_vs_tone_map_y {
	enum drm_vs_calcu_mode y_mode;
	__u16 coef0;
	__u16 coef1;
	__u16 coef2;
	__u16 weight;
};

struct drm_vs_tone_map {
	bool enable;
	struct drm_vs_tone_map_y pseudo_y;
	struct drm_vs_lut lut;
};

struct drm_vs_data_trunc {
	enum drm_vs_data_trunc_mode gamma_data_trunc;
	enum drm_vs_data_trunc_mode panel_data_trunc;
	enum drm_vs_data_trunc_mode blend_data_trunc;
};

struct drm_vs_lut_config_ex {
	bool enable[3];
	struct drm_vs_rect rect[2];
	struct drm_vs_data_block data[3];
};

struct drm_vs_dither {
	enum drm_vs_dth_frm_idx index_type;
	__u8 sw_index;
	__u32 table_low[3];
	__u32 table_high[3];
	enum drm_vs_dth_frm_mode frm_mode;
};

struct drm_vs_random_dither_seed {
	bool hash_seed_x_enable;
	bool hash_seed_y_enable;
	bool permut_seed1_enable;
	bool permut_seed2_enable;
	__u32 hash_seed_x[VS_RANDOM_DITHER_SEED_NUM];
	__u32 hash_seed_y[VS_RANDOM_DITHER_SEED_NUM];
	__u32 permut_seed1[VS_RANDOM_DITHER_SEED_NUM];
	__u32 permut_seed2[VS_RANDOM_DITHER_SEED_NUM];
};

struct drm_vs_blender_dither {
	enum drm_vs_dth_frm_idx index_type;
	__u8 sw_index;
	__u8 noise;
	__u16 start_x;
	__u16 start_y;
	__u16 mask;
	struct drm_vs_random_dither_seed seed;
};

struct drm_vs_llv_degamma {
	bool enable;
	struct drm_vs_ltm_xgamma data;
};

struct drm_vs_llv_dither {
	enum drm_vs_dth_frm_idx index_type;
	__u8 sw_index;
	__u16 start_x;
	__u16 start_y;
	__u16 mask;
	__u16 threshold;
	__u16 linear_threshold;
	struct drm_vs_random_dither_seed seed;
	struct drm_vs_llv_degamma degamma;
};

struct drm_vs_wb_dither {
	__u32 table_low[3];
	__u32 table_high[3];
	enum drm_vs_dth_frm_idx index_type;
	__u8 sw_index;
	enum drm_vs_dth_frm_mode frm_mode;
};

struct drm_vs_wb_spliter {
	struct drm_vs_rect split_rect0;
	struct drm_vs_rect split_rect1;
};

struct drm_vs_ccm {
	__s32 coef[9];
	__s32 offset[3];
};

struct drm_vs_blur {
	struct drm_vs_rect roi;
	__u8 coef[3][VS_BLUR_COEF_NUM];
	__u32 norm[3];
	__u8 coef_num;
};

struct drm_vs_rcd_enable {
	bool enable;
	enum drm_vs_mask_blend_type type;
};

struct drm_vs_rcd_bg {
	__u32 pnl_color;
	bool roi_enable;
	__u32 roi_color;
	struct drm_vs_rect roi_rect;
	bool bg_dirty[2];
};

struct drm_vs_rcd_roi {
	bool top_enable;
	bool btm_enable;
	struct drm_vs_rect top_roi;
	struct drm_vs_rect btm_roi;
};

struct drm_vs_wb_frm_done {
	__u8 wb_id;
	bool wb_frm_done;
};

/*
 * histogram collects the image characteristics for SW to perform various related features.
 * There are 4 independent programmable histograms: VS_HIST_CHAN_IDX_0 ~ VS_HIST_CHAN_IDX_3
 * VS_HIST_RGB_IDX is for rgbHistogram
*/
enum drm_vs_hist_idx {
	VS_HIST_CHAN_IDX_0 = 0,
	VS_HIST_CHAN_IDX_1 = 1,
	VS_HIST_CHAN_IDX_2 = 2,
	VS_HIST_CHAN_IDX_3 = 3,
	VS_HIST_CHAN_IDX_COUNT = 4,
	VS_HIST_RGB_IDX = 4,
	VS_HIST_IDX_COUNT,
};

/*
 * Histogram channel programmable positions.
 * RGB_Histogram position is fixed at POS_POST
 */
enum drm_vs_hist_pos {
	VS_HIST_POS_SCALE, /* post-processing pipeline (before scaler) */
	VS_HIST_POS_RCD, /* after dither */
	VS_HIST_POS_COUNT
};

/*
 * Histogram channel bin accounting method
 */
enum drm_vs_hist_bin_mode {
	VS_HIST_BIN_MODE_MAX, /* max(max(R, G), B) */
	VS_HIST_BIN_MODE_WEIGHTS, /* (R*Weights[0] + G*Weights[1] + B*Weights[2] + 512) /1024 */
	VS_HIST_BIN_MODE_COUNT,
};

/*
 * Histogram channel weights sum must be 1024 (sum(weights[0-3]))
 */
#define VS_HIST_BIN_MODE_WEIGHTS_SUM 1024

/*
 * struct drm_vs_hist_chan_bins
 */
struct drm_vs_hist_chan_bins {
	__u32 result[VS_HIST_RESULT_BIN_CNT];
};

/*
 * struct drm_vs_hist_rgb_bins
 * RGB histogram is a collection of histogram data per color
 */
struct drm_vs_hist_rgb_bins {
	struct drm_vs_hist_chan_bins rgb[3];
};

/**
 * struct drm_vs_hist_chan - histogram channel configuration
 *
 * @roi: histogram roi
 * @blocked_roi: histogram blocked roi
 * @pos: histogram channel position (SCALE, RCD).
 * @bin_mode: histogram bin_mode (MAX or WEIGHTS).
 * @weights: weights per color. R: offset 0, G: offset 1, B: offset 2.
 *           sum of all weights must be equal to 1024.
 * @user_data: histogram user_data for tracking (user can track configurations)
 * @flags: histogram optional configuration flags
 *
 * It is used to set a PROPERTY of a crtc.
 */
struct drm_vs_hist_chan {
	struct drm_vs_rect roi;
	struct drm_vs_rect blocked_roi;
	enum drm_vs_hist_pos pos;
	enum drm_vs_hist_bin_mode bin_mode;
	__u16 weights[3];
	__u64 user_data;
	__u32 flags;
};

/**
 * struct drm_vs_hist_bins_query - histogram bins query
 *
 * **input**
 * @crtc_id: crtc id
 * @idx: histogram idx (histogram_channels, histogram_rgb)
 * @hist_size: hist_bins_ptr buffer size, in bytes
 *             Expected: sizeof(struct drm_vs_hist_chan_bins) for histogram channels
 *                       sizeof(struct drm_vs_hist_rgb_bins) for histogram_rgb
 *
 * **output**
 * @sequence: vblank sequence id
 * @tv_sec: vblank tv_sec
 * @tv_usec: vblank tv_usec
 * @user_data: histogram channel user_data for tracking (provided via property).
 *             0 for histogram_rgb.
 * @hist_bins_ptr: histogram bins buffer data
 *
 * It is used to query histogram channel bins via IOCTL.
 */
struct drm_vs_hist_bins_query {
	/* input part */
	__u32 crtc_id;
	enum drm_vs_hist_idx idx;
	__u16 hist_bins_size;

	/* output part */
	__u32 sequence;
	__u32 tv_sec;
	__u32 tv_usec;
	__u64 user_data;
	__u64 hist_bins_ptr;
};

struct drm_vs_pvric_offset {
	__u32 format;
	__u32 handles[3];
	__u32 header_size[3];

	__u64 offsets[3];
};

struct drm_vs_brightness {
	enum drm_vs_brightness_cal_mode mode;
	__u16 target;
	__u16 threshold;
	__u16 luma_coef[3];
};

struct drm_vs_brightness_roi {
	bool roi0_enable;
	bool roi1_enable;
	struct drm_vs_rect roi0;
	struct drm_vs_rect roi1;
};

struct drm_vs_decompress {
	bool lossy;
	__u64 physical;
	__u32 format;
	__u32 tile_type;
	__u64 clear_color;
};

struct drm_vs_gem_query_info {
	enum drm_vs_gem_query_type type;
	__u32 handle;
	__u64 data;
};

struct drm_vs_query_feature_cap {
	enum drm_vs_feature_cap_type type;
	__u32 cap;
};

struct drm_vs_query_hw_cap {
	enum drm_vs_hw_cap_type type;
	__u64 cap;
};

struct drm_vs_plane_hw_caps {
	__u32 hw_id;
	__u32 fe_id;
	__u32 min_width;
	__u32 min_height;
	__u32 max_width;
	__u32 max_height;
	__s32 min_scale;
	__s32 max_scale;
	__u32 axi_id;
};

struct drm_vs_crtc_hw_caps {
	__u32 hw_id;
	__u32 max_width;
	__u32 max_height;
	__s32 min_scale;
	__s32 max_scale;
};

struct drm_vs_task_fence_arg {
	enum drm_vs_task_fence_ops op;
	int fd;
	int err;
};

#define DRM_VS_GET_FBC_OFFSET 0x00
#define DRM_VS_SW_RESET 0x01
#define DRM_VS_GEM_QUERY 0x04
#define DRM_VS_GET_FEATURE_CAP 0x05
#define DRM_VS_GET_HW_CAP 0x07
#define DRM_VS_GET_LTM_HIST 0x08
#define DRM_VS_GET_WB_FRM_DONE 0x09
#define DRM_VS_GET_HIST_BINS 0x010
#define DRM_VS_TASK_FENCE 0x11

#define DRM_IOCTL_VS_GET_FBC_OFFSET \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VS_GET_FBC_OFFSET, struct drm_vs_pvric_offset)
#define DRM_IOCTL_VS_SW_RESET DRM_IO(DRM_COMMAND_BASE + DRM_VS_SW_RESET)
#define DRM_IOCTL_VS_GEM_QUERY \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VS_GEM_QUERY, struct drm_vs_gem_query_info)
#define DRM_IOCTL_VS_GET_FEATURE_CAP \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VS_GET_FEATURE_CAP, struct drm_vs_query_feature_cap)
#define DRM_IOCTL_VS_GET_HW_CAP \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VS_GET_HW_CAP, struct drm_vs_query_hw_cap)
#define DRM_IOCTL_VS_GET_HIST_BINS \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VS_GET_HIST_BINS, struct drm_vs_hist_bins_query)
#define DRM_IOCTL_VS_GET_LTM_HIST \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VS_GET_LTM_HIST, struct drm_vs_ltm_histogram_data)
#define DRM_IOCTL_VS_GET_WB_FRM_DONE \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VS_GET_WB_FRM_DONE, struct drm_vs_wb_frm_done)
#define DRM_IOCTL_VS_TASK_FENCE \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VS_TASK_FENCE, struct drm_vs_task_fence_arg)

#endif /* __VS_DRM_H__ */
