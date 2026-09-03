/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>

#include "vs_crtc.h"
#include "vs_dc_dsc.h"
#include "vs_dc_dsc_rc_tables.h"
#include "vs_dc_hw.h"
#include "vs_dc_reg_be.h"
#include "vs_trace_9x00.h"

struct vs_dc_dsc_dataflow_params {
	int initial_lines;
	int ob_max_addr;
	int drb_max_addr;
};

struct dsc_hw_config {
	/* Dsc version */
	u8 dsc_version_major;
	u8 dsc_version_minor;

	/* Define if native 420 and/or native 422 are supported */
	bool native_420;
	bool native_422;

	/* Number of hard slice encoder */
	u8 nb_hs_enc;

	/* Number of soft slice context */
	u8 nb_ss_enc;

	/* Derasterization buffer */
	bool derasterization_buffer_enable;

	/**
	 * Max picture size accross all hard slcies.
	 * max_container_pixels_per_line defines the max PICTURE_WIDTH
	 * max_container_pixels_hs_line defines the max PICTURE_WIDTH within
	 *     a single Hard Slice Encoder when used in independent mode,
	 *     or the max SLICE_WIDTH when used in split mode.
	 * max_lines defines the max PICTURE_HEIGHT.
	 */
	int max_container_pixels_per_line;
	int max_container_pixels_hs_line;
	int max_lines;

	/**
	 * Max number of bits per component.
	 * for R/G/B & YCbCr reconstructed pixels */
	int max_bpc;

	/* Output data interface width */
	int output_data_width;

	/* Output buffer RAMs addr bus width*/
	int ob_addr_width;
};

/************************** Constant Definitions *****************************/
#define ENC_ICH_RST_MANUAL_OVERRIDE 0x00
#define ENC_ICH_RST_MANUAL_VALUE 0x00
#define ENC_ICH_DISABLE 0x00
#define ENC_ICH_FULL_ICH_ERR_PERSION 0x00
#define OFFSET_FRACTIONAL_BITS 11
#define MAX_CONTAINER_PIXELS_HS_LINE 4096

/************************** Encoder Address Space ****************************/
#define ENC_MAIN_CONF_ADDR 0x000

#define HS_DF_CTRL_ADDR 0x000
#define HS_GEN_STATUS_ADDR 0x001
#define HS_HSLICE_STATUS_ADDR 0x002
#define HS_OUT_STATUS_ADDR 0x003
#define HS_INT_STAT_ADDR 0x004
#define HS_INT_CLR_ADDR 0x005
#define HS_INT_MASK_ADDR 0x006
#define HS_DSC_MAIN_CONF_ADDR 0x00C
#define HS_DSC_PICT_SIZE_ADDR 0x00D
#define HS_DSC_SLICE_SIZE_ADDR 0x00E
#define HS_DSC_MISC_SIZE_ADDR 0x00F
#define HS_DSC_HRD_DELAY_ADDR 0x010
#define HS_DSC_RC_SCALE_ADDR 0x011
#define HS_DSC_RC_SCALE_INC_DEC_ADDR 0x012
#define HS_DSC_RC_OFFSETS_1_ADDR 0x013
#define HS_DSC_RC_OFFSETS_2_ADDR 0x014
#define HS_DSC_RC_OFFSETS_3_ADDR 0x015
#define HS_DSC_RC_OFFSETS_4_ADDR 0x016
#define HS_DSC_FLATNESS_QP_ADDR 0x017
#define HS_DSC_RC_MODEL_SIZE_ADDR 0x018
#define HS_DSC_RC_CONFIG_ADDR 0x019
#define HS_DSC_RC_BUF_TRESH_0_ADDR 0x01A
#define HS_DSC_RC_BUF_TRESH_1_ADDR 0x01B
#define HS_DSC_RC_BUF_TRESH_2_ADDR 0x01C
#define HS_DSC_RC_BUF_TRESH_3_ADDR 0x01D
#define HS_DSC_RC_MIN_QP_0_ADDR 0x01E
#define HS_DSC_RC_MIN_QP_1_ADDR 0x01F
#define HS_DSC_RC_MIN_QP_2_ADDR 0x020
#define HS_DSC_RC_MAX_QP_0_ADDR 0x021
#define HS_DSC_RC_MAX_QP_1_ADDR 0x022
#define HS_DSC_RC_MAX_QP_2_ADDR 0x023
#define HS_DSC_RC_RANGE_BPG_OFFSETS_0_ADDR 0x024
#define HS_DSC_RC_RANGE_BPG_OFFSETS_1_ADDR 0x025
#define HS_DSC_RC_RANGE_BPG_OFFSETS_2_ADDR 0x026

/************************** Register Field Value *****************************/

/************************** ENC_MAIN_CONF Register ***************************/
#define DSC_ENC_MAIN_CONF_SP_EN_FIELD 0
#define DSC_ENC_MAIN_CONF_MUL_MODE_EN_FIELD 1
#define DSC_ENC_MAIN_CONF_MUL_OUT_SEL_FIELD 2
#define DSC_ENC_MAIN_CONF_MUL_EOC_EN_FIELD 5
#define DSC_ENC_MAIN_CONF_DE_RASTER_EN_FIELD 6
#define DSC_ENC_MAIN_CONF_NUM_OF_SS_FIELD 7
#define DSC_ENC_MAIN_CONF_DRB_MAX_ADDR_FIELD 18

/************************** ENC_DF_CTRL Register ***************************/
#define ENC_DF_CTRL_INIT_LINES_FIELD 0
#define ENC_DF_CTRL_INIT_VIDEO_MODE_FIELD 9
#define ENC_DF_CTRL_INIT_ICH_RST_MAN_OVER_FIELD 10
#define ENC_DF_CTRL_INIT_ICH_RST_MAN_VALUE_FIELD 11
#define ENC_DF_CTRL_INIT_FULL_ICH_PRECISION_FIELD 12
#define ENC_DF_CTRL_INIT_ICH_DISABLE_FIELD 13
#define ENC_DF_CTRL_INIT_OB_MAX_ADDR_FIELD 18

/************************** DSC_MAIN_CONF Register ***************************/
#define DSC_MAIN_CONF_BPC_FIELD 0
#define DSC_MAIN_CONF_CONVERT_RGB_FIELD 4
#define DSC_MAIN_CONF_SIMPLE_422_FIELD 5
#define DSC_MAIN_CONF_LINEBUF_DEPTH_FIELD 6
#define DSC_MAIN_CONF_BPP_FIELD 10
#define DSC_MAIN_CONF_BLOCK_PRED_EN_FIELD 20
#define DSC_MAIN_CONF_NATIVE_420_FIELD 21
#define DSC_MAIN_CONF_NATIVE_422_FIELD 22
#define DSC_MAIN_CONF_DSC_VERSION_MINOR_FIELD 28

/************************** DSC_PICTURE_SIZE Register ***************************/
#define DSC_PICT_SIZE_PICT_WIDTH_FIELD 0
#define DSC_PICT_SIZE_PICT_HEIGHT_FIELD 16

/************************** DSC_SLICE_SIZE Register ****************************/
#define DSC_SLICE_SIZE_SLICE_WIDTH_FIELD 0
#define DSC_SLICE_SIZE_SLICE_HEIGHT_FIELD 16

/************************** DSC_MISC_SIZE Register ****************************/
#define DSC_MISC_SIZE_CHUNK_SIZE_FIELD 0

/************************** DSC_HRD_DELAYS Register ****************************/
#define DSC_HRD_DELAYS_INIT_XMIT_DELAY_FIELD 0
#define DSC_HRD_DELAYS_INIT_DEC_DELAY_FIELD 16

/************************** DSC_RC_SCALE Register ****************************/
#define DSC_RC_SCALE_INIT_SCALE_VALUE_FIELD 0

/************************** DSC_RC_SCALE_INC_DEC Register ****************************/
#define DSC_RC_SCALE_INC_DEC_SCALE_INC_INT_FIELD 0
#define DSC_RC_SCALE_INC_DEC_SCALE_DEC_INT_FIELD 16

/************************** DSC_RC_OFFSETS_1 Register ****************************/
#define DSC_RC_OFFSETS_1_FIRST_LINE_BPG_OFFSET_FIELD 0
#define DSC_RC_OFFSETS_1_SECOND_LINE_BPG_OFFSET_FIELD 5

/************************** DSC_RC_OFFSETS_2 Register ****************************/
#define DSC_RC_OFFSETS_2_NFL_BPG_OFFSET_FIELD 0
#define DSC_RC_OFFSETS_2_SLICE_BPG_OFFSET_FIELD 16

/************************** DSC_RC_OFFSETS_3 Register ****************************/
#define DSC_RC_OFFSETS_3_INITIAL_OFFSET_FIELD 0
#define DSC_RC_OFFSETS_3_FINAL_OFFSET_FIELD 16

/************************** DSC_RC_OFFSETS_4 Register ****************************/
#define DSC_RC_OFFSETS_4_NSL_BPG_OFFSET_FIELD 0
#define DSC_RC_OFFSETS_4_SECOND_LINE_OFFSET_ADJ_FIELD 16

/************************** DSC_FLATNESS_QP Register ****************************/
#define DSC_FLATNESS_QP_FLATNESS_MIN_QP_FIELD 0
#define DSC_FLATNESS_QP_FLATNESS_MAX_QP_FIELD 5
#define DSC_FLATNESS_QP_FLATNESS_DET_THRESH_FIELD 10

/************************** DSC_RC_MODEL_SIZE Register ****************************/
#define DSC_RC_MODEL_SIZE_RC_MODEL_SIZE_FIELD 0

/************************** DSC_RC_CONFIG Register ****************************/
#define DSC_RC_CONFIG_RC_EDGE_FACTOR_FIELD 0
#define DSC_RC_CONFIG_RC_QUANT_INCR_LIM0_FIELD 8
#define DSC_RC_CONFIG_RC_QUANT_INCR_LIM1_FIELD 13
#define DSC_RC_CONFIG_RC_TGT_OFFSET_HI_FIELD 20
#define DSC_RC_CONFIG_RC_TGT_OFFSET_LO_FIELD 24

/************************** DSC_RC_BUF_THRESH_0  Register ****************************/
#define DSC_RC_BUF_THRESH_0_RC_BUF_THRESH0_FIELD 0
#define DSC_RC_BUF_THRESH_0_RC_BUF_THRESH1_FIELD 8
#define DSC_RC_BUF_THRESH_0_RC_BUF_THRESH2_FIELD 16
#define DSC_RC_BUF_THRESH_0_RC_BUF_THRESH3_FIELD 24

/************************** DSC_RC_BUF_THRESH_1  Register ****************************/
#define DSC_RC_BUF_THRESH_1_RC_BUF_THRESH4_FIELD 0
#define DSC_RC_BUF_THRESH_1_RC_BUF_THRESH5_FIELD 8
#define DSC_RC_BUF_THRESH_1_RC_BUF_THRESH6_FIELD 16
#define DSC_RC_BUF_THRESH_1_RC_BUF_THRESH7_FIELD 24

/************************** DSC_RC_BUF_THRESH_2  Register ****************************/
#define DSC_RC_BUF_THRESH_2_RC_BUF_THRESH8_FIELD 0
#define DSC_RC_BUF_THRESH_2_RC_BUF_THRESH9_FIELD 8
#define DSC_RC_BUF_THRESH_2_RC_BUF_THRESH10_FIELD 16
#define DSC_RC_BUF_THRESH_2_RC_BUF_THRESH11_FIELD 24

/************************** DSC_RC_BUF_THRESH_3  Register ****************************/
#define DSC_RC_BUF_THRESH_3_RC_BUF_THRESH12_FIELD 0
#define DSC_RC_BUF_THRESH_3_RC_BUF_THRESH13_FIELD 8

/************************** DSC_RC_MIN_QP_0 Register ****************************/
#define DSC_RC_MIN_QP_0_RANGE_MIN_QP0_FIELD 0
#define DSC_RC_MIN_QP_0_RANGE_MIN_QP1_FIELD 5
#define DSC_RC_MIN_QP_0_RANGE_MIN_QP2_FIELD 10
#define DSC_RC_MIN_QP_0_RANGE_MIN_QP3_FIELD 15
#define DSC_RC_MIN_QP_0_RANGE_MIN_QP4_FIELD 20

/************************** DSC_RC_MIN_QP_1 Register ****************************/
#define DSC_RC_MIN_QP_1_RANGE_MIN_QP5_FIELD 0
#define DSC_RC_MIN_QP_1_RANGE_MIN_QP6_FIELD 5
#define DSC_RC_MIN_QP_1_RANGE_MIN_QP7_FIELD 10
#define DSC_RC_MIN_QP_1_RANGE_MIN_QP8_FIELD 15
#define DSC_RC_MIN_QP_1_RANGE_MIN_QP9_FIELD 20

/************************** DSC_RC_MIN_QP_2 Register ****************************/
#define DSC_RC_MIN_QP_2_RANGE_MIN_QP10_FIELD 0
#define DSC_RC_MIN_QP_2_RANGE_MIN_QP11_FIELD 5
#define DSC_RC_MIN_QP_2_RANGE_MIN_QP12_FIELD 10
#define DSC_RC_MIN_QP_2_RANGE_MIN_QP13_FIELD 15
#define DSC_RC_MIN_QP_2_RANGE_MIN_QP14_FIELD 20

/************************** DSC_RC_MAX_QP_0 Register ****************************/
#define DSC_RC_MAX_QP_0_RANGE_MAX_QP0_FIELD 0
#define DSC_RC_MAX_QP_0_RANGE_MAX_QP1_FIELD 5
#define DSC_RC_MAX_QP_0_RANGE_MAX_QP2_FIELD 10
#define DSC_RC_MAX_QP_0_RANGE_MAX_QP3_FIELD 15
#define DSC_RC_MAX_QP_0_RANGE_MAX_QP4_FIELD 20

/************************** DSC_RC_MAX_QP_1 Register ****************************/
#define DSC_RC_MAX_QP_1_RANGE_MAX_QP5_FIELD 0
#define DSC_RC_MAX_QP_1_RANGE_MAX_QP6_FIELD 5
#define DSC_RC_MAX_QP_1_RANGE_MAX_QP7_FIELD 10
#define DSC_RC_MAX_QP_1_RANGE_MAX_QP8_FIELD 15
#define DSC_RC_MAX_QP_1_RANGE_MAX_QP9_FIELD 20

/************************** DSC_RC_MAX_QP_2 Register ****************************/
#define DSC_RC_MAX_QP_2_RANGE_MAX_QP10_FIELD 0
#define DSC_RC_MAX_QP_2_RANGE_MAX_QP11_FIELD 5
#define DSC_RC_MAX_QP_2_RANGE_MAX_QP12_FIELD 10
#define DSC_RC_MAX_QP_2_RANGE_MAX_QP13_FIELD 15
#define DSC_RC_MAX_QP_2_RANGE_MAX_QP14_FIELD 20

/************************** DSC_RC_RANGE_BPG_OFFSETS_0 Register ****************************/
#define DSC_RC_RANGE_BPG_OFFSETS_0_RANGE_BPG_OFFSET0_FIELD 0
#define DSC_RC_RANGE_BPG_OFFSETS_0_RANGE_BPG_OFFSET1_FIELD 6
#define DSC_RC_RANGE_BPG_OFFSETS_0_RANGE_BPG_OFFSET2_FIELD 12
#define DSC_RC_RANGE_BPG_OFFSETS_0_RANGE_BPG_OFFSET3_FIELD 18
#define DSC_RC_RANGE_BPG_OFFSETS_0_RANGE_BPG_OFFSET4_FIELD 24

/************************** DSC_RC_RANGE_BPG_OFFSETS_1 Register ****************************/
#define DSC_RC_RANGE_BPG_OFFSETS_1_RANGE_BPG_OFFSET5_FIELD 0
#define DSC_RC_RANGE_BPG_OFFSETS_1_RANGE_BPG_OFFSET6_FIELD 6
#define DSC_RC_RANGE_BPG_OFFSETS_1_RANGE_BPG_OFFSET7_FIELD 12
#define DSC_RC_RANGE_BPG_OFFSETS_1_RANGE_BPG_OFFSET8_FIELD 18
#define DSC_RC_RANGE_BPG_OFFSETS_1_RANGE_BPG_OFFSET9_FIELD 24

/************************** DSC_RC_RANGE_BPG_OFFSETS_2 Register ****************************/
#define DSC_RC_RANGE_BPG_OFFSETS_2_RANGE_BPG_OFFSET10_FIELD 0
#define DSC_RC_RANGE_BPG_OFFSETS_2_RANGE_BPG_OFFSET11_FIELD 6
#define DSC_RC_RANGE_BPG_OFFSETS_2_RANGE_BPG_OFFSET12_FIELD 12
#define DSC_RC_RANGE_BPG_OFFSETS_2_RANGE_BPG_OFFSET13_FIELD 18
#define DSC_RC_RANGE_BPG_OFFSETS_2_RANGE_BPG_OFFSET14_FIELD 24

/**************************** Registers Value  *******************************/
#define ENC_MAIN_CONF_VALUE(sw_config, dataflow_params)                            \
	(sw_config->split_panel_enable << DSC_ENC_MAIN_CONF_SP_EN_FIELD |          \
	 sw_config->multiplex_mode_enable << DSC_ENC_MAIN_CONF_MUL_MODE_EN_FIELD | \
	 sw_config->multiplex_out_sel << DSC_ENC_MAIN_CONF_MUL_OUT_SEL_FIELD |     \
	 sw_config->multiplex_eoc_enable << DSC_ENC_MAIN_CONF_MUL_EOC_EN_FIELD |   \
	 sw_config->de_raster_enable << DSC_ENC_MAIN_CONF_DE_RASTER_EN_FIELD |     \
	 sw_config->ss_num << DSC_ENC_MAIN_CONF_NUM_OF_SS_FIELD |                  \
	 dataflow_params->drb_max_addr << DSC_ENC_MAIN_CONF_DRB_MAX_ADDR_FIELD)

#define ENC_DF_CTRL_ADDR_VALUE(dataflow_params, sw_config)                           \
	(dataflow_params->initial_lines << ENC_DF_CTRL_INIT_LINES_FIELD |            \
	 sw_config->video_mode << ENC_DF_CTRL_INIT_VIDEO_MODE_FIELD |                \
	 ENC_ICH_RST_MANUAL_OVERRIDE << ENC_DF_CTRL_INIT_ICH_RST_MAN_OVER_FIELD |    \
	 ENC_ICH_RST_MANUAL_VALUE << ENC_DF_CTRL_INIT_ICH_RST_MAN_VALUE_FIELD |      \
	 ENC_ICH_DISABLE << ENC_DF_CTRL_INIT_ICH_DISABLE_FIELD |                     \
	 ENC_ICH_FULL_ICH_ERR_PERSION << ENC_DF_CTRL_INIT_FULL_ICH_PRECISION_FIELD | \
	 dataflow_params->ob_max_addr << ENC_DF_CTRL_INIT_OB_MAX_ADDR_FIELD)

#define DSC_MAIN_CONF_VALUE(dsc_cfg)                                         \
	((dsc_cfg->bits_per_component) << DSC_MAIN_CONF_BPC_FIELD |          \
	 (dsc_cfg->convert_rgb) << DSC_MAIN_CONF_CONVERT_RGB_FIELD |         \
	 (dsc_cfg->simple_422) << DSC_MAIN_CONF_SIMPLE_422_FIELD |           \
	 (dsc_cfg->line_buf_depth) << DSC_MAIN_CONF_LINEBUF_DEPTH_FIELD |    \
	 (dsc_cfg->bits_per_pixel) << DSC_MAIN_CONF_BPP_FIELD |              \
	 (dsc_cfg->block_pred_enable) << DSC_MAIN_CONF_BLOCK_PRED_EN_FIELD | \
	 (dsc_cfg->native_420) << DSC_MAIN_CONF_NATIVE_420_FIELD |           \
	 (dsc_cfg->native_422) << DSC_MAIN_CONF_NATIVE_422_FIELD |           \
	 (dsc_cfg->dsc_version_minor) << DSC_MAIN_CONF_DSC_VERSION_MINOR_FIELD)

#define DSC_PICT_SIZE_VALUE(dsc_cfg)                              \
	((dsc_cfg->pic_width) << DSC_PICT_SIZE_PICT_WIDTH_FIELD | \
	 (dsc_cfg->pic_height) << DSC_PICT_SIZE_PICT_HEIGHT_FIELD)

#define DSC_SLICE_SIZE_VALUE(dsc_cfg)                                 \
	((dsc_cfg->slice_width) << DSC_SLICE_SIZE_SLICE_WIDTH_FIELD | \
	 (dsc_cfg->slice_height) << DSC_SLICE_SIZE_SLICE_HEIGHT_FIELD)

#define DSC_MISC_SIZE_VALUE(dsc_cfg) ((dsc_cfg->slice_chunk_size) << DSC_MISC_SIZE_CHUNK_SIZE_FIELD)

#define DSC_HRD_DELAY_VALUE(dsc_cfg)                                             \
	((dsc_cfg->initial_xmit_delay) << DSC_HRD_DELAYS_INIT_XMIT_DELAY_FIELD | \
	 (dsc_cfg->initial_dec_delay) << DSC_HRD_DELAYS_INIT_DEC_DELAY_FIELD)

#define DSC_RC_SCALE_VALUE(dsc_cfg) \
	((dsc_cfg->initial_scale_value) << DSC_RC_SCALE_INIT_SCALE_VALUE_FIELD)

#define DSC_RC_SCALE_INC_DEC_VALUE(dsc_cfg)                                                \
	((dsc_cfg->scale_increment_interval) << DSC_RC_SCALE_INC_DEC_SCALE_INC_INT_FIELD | \
	 (dsc_cfg->scale_decrement_interval) << DSC_RC_SCALE_INC_DEC_SCALE_DEC_INT_FIELD)

#define DSC_RC_OFFSETS_1_VALUE(dsc_cfg)                                                     \
	((dsc_cfg->first_line_bpg_offset) << DSC_RC_OFFSETS_1_FIRST_LINE_BPG_OFFSET_FIELD | \
	 (dsc_cfg->second_line_bpg_offset) << DSC_RC_OFFSETS_1_SECOND_LINE_BPG_OFFSET_FIELD)

#define DSC_RC_OFFSETS_2_VALUE(dsc_cfg)                                       \
	((dsc_cfg->nfl_bpg_offset) << DSC_RC_OFFSETS_2_NFL_BPG_OFFSET_FIELD | \
	 (dsc_cfg->slice_bpg_offset) << DSC_RC_OFFSETS_2_SLICE_BPG_OFFSET_FIELD)

#define DSC_RC_OFFSETS_3_VALUE(dsc_cfg)                                       \
	((dsc_cfg->initial_offset) << DSC_RC_OFFSETS_3_INITIAL_OFFSET_FIELD | \
	 (dsc_cfg->final_offset) << DSC_RC_OFFSETS_3_FINAL_OFFSET_FIELD)

#define DSC_RC_OFFSETS_4_VALUE(dsc_cfg)                                       \
	((dsc_cfg->nsl_bpg_offset) << DSC_RC_OFFSETS_4_NSL_BPG_OFFSET_FIELD | \
	 (dsc_cfg->second_line_offset_adj) << DSC_RC_OFFSETS_4_SECOND_LINE_OFFSET_ADJ_FIELD)

#define DSC_FLATNESS_DET_THRESH(dsc_cfg) (2 << (dsc_cfg->bits_per_component - 8))

#define DSC_FLATNESS_QP_VALUE(dsc_cfg)                                         \
	((dsc_cfg->flatness_min_qp) << DSC_FLATNESS_QP_FLATNESS_MIN_QP_FIELD | \
	 (dsc_cfg->flatness_max_qp) << DSC_FLATNESS_QP_FLATNESS_MAX_QP_FIELD | \
	 DSC_FLATNESS_DET_THRESH(dsc_cfg) << DSC_FLATNESS_QP_FLATNESS_DET_THRESH_FIELD)

#define DSC_RC_MODEL_SIZE_VALUE(dsc_cfg) \
	((dsc_cfg->rc_model_size) << DSC_RC_MODEL_SIZE_RC_MODEL_SIZE_FIELD)

#define DSC_RC_CONFIG_VALUE(dsc_cfg)                                                 \
	((dsc_cfg->rc_edge_factor) << DSC_RC_CONFIG_RC_EDGE_FACTOR_FIELD |           \
	 (dsc_cfg->rc_quant_incr_limit0) << DSC_RC_CONFIG_RC_QUANT_INCR_LIM0_FIELD | \
	 (dsc_cfg->rc_quant_incr_limit1) << DSC_RC_CONFIG_RC_QUANT_INCR_LIM1_FIELD | \
	 (dsc_cfg->rc_tgt_offset_high) << DSC_RC_CONFIG_RC_TGT_OFFSET_HI_FIELD |     \
	 (dsc_cfg->rc_tgt_offset_low) << DSC_RC_CONFIG_RC_TGT_OFFSET_LO_FIELD)

#define DSC_RC_BUF_TRESH_0_VALUE(dsc_cfg)                                          \
	((dsc_cfg->rc_buf_thresh[0]) << DSC_RC_BUF_THRESH_0_RC_BUF_THRESH0_FIELD | \
	 (dsc_cfg->rc_buf_thresh[1]) << DSC_RC_BUF_THRESH_0_RC_BUF_THRESH1_FIELD | \
	 (dsc_cfg->rc_buf_thresh[2]) << DSC_RC_BUF_THRESH_0_RC_BUF_THRESH2_FIELD | \
	 (dsc_cfg->rc_buf_thresh[3]) << DSC_RC_BUF_THRESH_0_RC_BUF_THRESH3_FIELD)

#define DSC_RC_BUF_TRESH_1_VALUE(dsc_cfg)                                          \
	((dsc_cfg->rc_buf_thresh[4]) << DSC_RC_BUF_THRESH_1_RC_BUF_THRESH4_FIELD | \
	 (dsc_cfg->rc_buf_thresh[5]) << DSC_RC_BUF_THRESH_1_RC_BUF_THRESH5_FIELD | \
	 (dsc_cfg->rc_buf_thresh[6]) << DSC_RC_BUF_THRESH_1_RC_BUF_THRESH6_FIELD | \
	 (dsc_cfg->rc_buf_thresh[7]) << DSC_RC_BUF_THRESH_1_RC_BUF_THRESH7_FIELD)

#define DSC_RC_BUF_TRESH_2_VALUE(dsc_cfg)                                            \
	((dsc_cfg->rc_buf_thresh[8]) << DSC_RC_BUF_THRESH_2_RC_BUF_THRESH8_FIELD |   \
	 (dsc_cfg->rc_buf_thresh[9]) << DSC_RC_BUF_THRESH_2_RC_BUF_THRESH9_FIELD |   \
	 (dsc_cfg->rc_buf_thresh[10]) << DSC_RC_BUF_THRESH_2_RC_BUF_THRESH10_FIELD | \
	 (dsc_cfg->rc_buf_thresh[11]) << DSC_RC_BUF_THRESH_2_RC_BUF_THRESH11_FIELD)

#define DSC_RC_BUF_TRESH_3_VALUE(dsc_cfg)                                            \
	((dsc_cfg->rc_buf_thresh[12]) << DSC_RC_BUF_THRESH_3_RC_BUF_THRESH12_FIELD | \
	 (dsc_cfg->rc_buf_thresh[13]) << DSC_RC_BUF_THRESH_3_RC_BUF_THRESH13_FIELD)

#define DSC_RC_MIN_QP_0_VALUE(dsc_cfg)                                                       \
	((dsc_cfg->rc_range_params[0].range_min_qp) << DSC_RC_MIN_QP_0_RANGE_MIN_QP0_FIELD | \
	 (dsc_cfg->rc_range_params[1].range_min_qp) << DSC_RC_MIN_QP_0_RANGE_MIN_QP1_FIELD | \
	 (dsc_cfg->rc_range_params[2].range_min_qp) << DSC_RC_MIN_QP_0_RANGE_MIN_QP2_FIELD | \
	 (dsc_cfg->rc_range_params[3].range_min_qp) << DSC_RC_MIN_QP_0_RANGE_MIN_QP3_FIELD | \
	 (dsc_cfg->rc_range_params[4].range_min_qp) << DSC_RC_MIN_QP_0_RANGE_MIN_QP4_FIELD)

#define DSC_RC_MIN_QP_1_VALUE(dsc_cfg)                                                       \
	((dsc_cfg->rc_range_params[5].range_min_qp) << DSC_RC_MIN_QP_1_RANGE_MIN_QP5_FIELD | \
	 (dsc_cfg->rc_range_params[6].range_min_qp) << DSC_RC_MIN_QP_1_RANGE_MIN_QP6_FIELD | \
	 (dsc_cfg->rc_range_params[7].range_min_qp) << DSC_RC_MIN_QP_1_RANGE_MIN_QP7_FIELD | \
	 (dsc_cfg->rc_range_params[8].range_min_qp) << DSC_RC_MIN_QP_1_RANGE_MIN_QP8_FIELD | \
	 (dsc_cfg->rc_range_params[9].range_min_qp) << DSC_RC_MIN_QP_1_RANGE_MIN_QP9_FIELD)

#define DSC_RC_MIN_QP_2_VALUE(dsc_cfg)                                                         \
	((dsc_cfg->rc_range_params[10].range_min_qp) << DSC_RC_MIN_QP_2_RANGE_MIN_QP10_FIELD | \
	 (dsc_cfg->rc_range_params[11].range_min_qp) << DSC_RC_MIN_QP_2_RANGE_MIN_QP11_FIELD | \
	 (dsc_cfg->rc_range_params[12].range_min_qp) << DSC_RC_MIN_QP_2_RANGE_MIN_QP12_FIELD | \
	 (dsc_cfg->rc_range_params[13].range_min_qp) << DSC_RC_MIN_QP_2_RANGE_MIN_QP13_FIELD | \
	 (dsc_cfg->rc_range_params[14].range_min_qp) << DSC_RC_MIN_QP_2_RANGE_MIN_QP14_FIELD)

#define DSC_RC_MAX_QP_0_VALUE(dsc_cfg)                                                       \
	((dsc_cfg->rc_range_params[0].range_max_qp) << DSC_RC_MAX_QP_0_RANGE_MAX_QP0_FIELD | \
	 (dsc_cfg->rc_range_params[1].range_max_qp) << DSC_RC_MAX_QP_0_RANGE_MAX_QP1_FIELD | \
	 (dsc_cfg->rc_range_params[2].range_max_qp) << DSC_RC_MAX_QP_0_RANGE_MAX_QP2_FIELD | \
	 (dsc_cfg->rc_range_params[3].range_max_qp) << DSC_RC_MAX_QP_0_RANGE_MAX_QP3_FIELD | \
	 (dsc_cfg->rc_range_params[4].range_max_qp) << DSC_RC_MAX_QP_0_RANGE_MAX_QP4_FIELD)

#define DSC_RC_MAX_QP_1_VALUE(dsc_cfg)                                                       \
	((dsc_cfg->rc_range_params[5].range_max_qp) << DSC_RC_MAX_QP_1_RANGE_MAX_QP5_FIELD | \
	 (dsc_cfg->rc_range_params[6].range_max_qp) << DSC_RC_MAX_QP_1_RANGE_MAX_QP6_FIELD | \
	 (dsc_cfg->rc_range_params[7].range_max_qp) << DSC_RC_MAX_QP_1_RANGE_MAX_QP7_FIELD | \
	 (dsc_cfg->rc_range_params[8].range_max_qp) << DSC_RC_MAX_QP_1_RANGE_MAX_QP8_FIELD | \
	 (dsc_cfg->rc_range_params[9].range_max_qp) << DSC_RC_MAX_QP_1_RANGE_MAX_QP9_FIELD)

#define DSC_RC_MAX_QP_2_VALUE(dsc_cfg)                                                         \
	((dsc_cfg->rc_range_params[10].range_max_qp) << DSC_RC_MAX_QP_2_RANGE_MAX_QP10_FIELD | \
	 (dsc_cfg->rc_range_params[11].range_max_qp) << DSC_RC_MAX_QP_2_RANGE_MAX_QP11_FIELD | \
	 (dsc_cfg->rc_range_params[12].range_max_qp) << DSC_RC_MAX_QP_2_RANGE_MAX_QP12_FIELD | \
	 (dsc_cfg->rc_range_params[13].range_max_qp) << DSC_RC_MAX_QP_2_RANGE_MAX_QP13_FIELD | \
	 (dsc_cfg->rc_range_params[14].range_max_qp) << DSC_RC_MAX_QP_2_RANGE_MAX_QP14_FIELD)

#define RC_RANGE_BPG_OFFSETS_0_VALUE(dsc_cfg)                            \
	(((dsc_cfg->rc_range_params[0].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_0_RANGE_BPG_OFFSET0_FIELD | \
	 ((dsc_cfg->rc_range_params[1].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_0_RANGE_BPG_OFFSET1_FIELD | \
	 ((dsc_cfg->rc_range_params[2].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_0_RANGE_BPG_OFFSET2_FIELD | \
	 ((dsc_cfg->rc_range_params[3].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_0_RANGE_BPG_OFFSET3_FIELD | \
	 ((dsc_cfg->rc_range_params[4].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_0_RANGE_BPG_OFFSET4_FIELD)

#define RC_RANGE_BPG_OFFSETS_1_VALUE(dsc_cfg)                            \
	(((dsc_cfg->rc_range_params[5].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_1_RANGE_BPG_OFFSET5_FIELD | \
	 ((dsc_cfg->rc_range_params[6].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_1_RANGE_BPG_OFFSET6_FIELD | \
	 ((dsc_cfg->rc_range_params[7].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_1_RANGE_BPG_OFFSET7_FIELD | \
	 ((dsc_cfg->rc_range_params[8].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_1_RANGE_BPG_OFFSET8_FIELD | \
	 ((dsc_cfg->rc_range_params[9].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_1_RANGE_BPG_OFFSET9_FIELD)

#define RC_RANGE_BPG_OFFSETS_2_VALUE(dsc_cfg)                             \
	(((dsc_cfg->rc_range_params[10].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_2_RANGE_BPG_OFFSET10_FIELD | \
	 ((dsc_cfg->rc_range_params[11].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_2_RANGE_BPG_OFFSET11_FIELD | \
	 ((dsc_cfg->rc_range_params[12].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_2_RANGE_BPG_OFFSET12_FIELD | \
	 ((dsc_cfg->rc_range_params[13].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_2_RANGE_BPG_OFFSET13_FIELD | \
	 ((dsc_cfg->rc_range_params[14].range_bpg_offset) & 0x3f)         \
		 << DSC_RC_RANGE_BPG_OFFSETS_2_RANGE_BPG_OFFSET14_FIELD)

static const struct dsc_hw_config hw_config = {
	.dsc_version_major = 1,
	.dsc_version_minor = 2,
	.native_420 = true,
	.native_422 = true,
	.nb_hs_enc = 2,
	.nb_ss_enc = 2,
	.derasterization_buffer_enable = true,
	.max_container_pixels_per_line = 8192,
	.max_container_pixels_hs_line = 4096,
	.max_lines = 4320,
	.max_bpc = 10,
	.output_data_width = 64,
	.ob_addr_width = 11,
};

static int get_ob_max_addr_4k(u8 ss_num)
{
	if (ss_num == 1)
		return 919;
	else
		return 459;
}

static int get_ob_max_addr_8k(u8 ss_num)
{
	if (ss_num == 1)
		return 1471;
	else
		return 735;
}

static int get_deraster_buffer(const struct dsc_hw_config *dsc_hw, u8 slices_per_line, u8 ss_num,
			       u16 slice_width)
{
	const int extra_buffer = 3;
	int container_picture_hs_width;
	u8 hs_num = slices_per_line / ss_num;

	container_picture_hs_width = slice_width * ss_num;
	if (container_picture_hs_width > MAX_CONTAINER_PIXELS_HS_LINE)
		container_picture_hs_width = MAX_CONTAINER_PIXELS_HS_LINE;
	return round_down(container_picture_hs_width * (hs_num - 1) /
				(hs_num * dsc_hw->nb_hs_enc), 2) + extra_buffer;
}

static int get_initial_lines(const struct drm_dsc_config *dsc, const struct dc_hw_dsc_usage *sw)
{
	const int pipeline_latency = 28;
	/* CBR_BPP, output_rate = 8, output_ratio = 0 */
	const int output_rate = 8;
	const int output_ratio = 0;
	/* HW MAX_BPC = 10, ssm delay is 91 */
	const int ssm_delay = 91;
	const int ob_data_width = 128;
	int input_ssm_out_latency = 0;
	int obuf_latency = 0;
	int base_hs_latency = 0;
	int multi_hs_extra_budget_bits = 0;
	int multi_hs_extra_latency = 0;
	int chunk_size_bits = dsc->slice_chunk_size * 8;
	u8 hs_num = sw->slices_per_line / sw->ss_num;
	u16 bits_per_pixel = dsc->bits_per_pixel >> 4;

	input_ssm_out_latency = pipeline_latency + 3 * (ssm_delay + 2) * sw->ss_num;
	obuf_latency = DIV_ROUND_UP((9 * ob_data_width + dsc->mux_word_size), bits_per_pixel) + 1;
	base_hs_latency = dsc->initial_xmit_delay + input_ssm_out_latency + obuf_latency;

	/* Multiple Hard Slice Encoders in parallel. */
	if (sw->split_panel_enable && sw->de_raster_enable) {
		/* When receiving the pixels from a single rasterized stream,
		 * the data from HS1+ is not available until
		 * picture width * (num_of_active_hs - 1) / (num_of_active_hs) has entered the encoder.
		 */
		if ((sw->multiplex_mode_enable != 1) && (sw->ss_num != 1) && (hs_num > 1)) {
			/* When using multiple transport links with multi-context, adding a full chunk. */
			multi_hs_extra_budget_bits = chunk_size_bits + output_ratio;
		} else {
			/* When using a single transport link */
			multi_hs_extra_budget_bits =
				DIV_ROUND_UP((hs_num - 1) * chunk_size_bits, hs_num) + output_ratio;
		}
	} else if (sw->split_panel_enable && sw->multiplex_mode_enable) {
		multi_hs_extra_budget_bits = chunk_size_bits;
	} else {
		/* Independent or split panel no deraster, no multiplex */
		if (sw->ss_num != 1 && output_rate > bits_per_pixel)
			multi_hs_extra_budget_bits = chunk_size_bits;
		else
			multi_hs_extra_budget_bits = output_ratio;
	}

	/* Adding DeRaster latency which is 5 pixels */
	multi_hs_extra_latency = DIV_ROUND_UP(multi_hs_extra_budget_bits, bits_per_pixel) +
				 (sw->de_raster_enable ? 5 : 0);
	return DIV_ROUND_UP(base_hs_latency + multi_hs_extra_latency, dsc->slice_width);
}

static inline void dsc_write(struct dc_hw *hw, u32 panel_id, u32 dword_reg, u32 val)
{
	u32 addr = 0;

	if (panel_id == 0)
		addr = DCREG_PANEL0_DSC_Address;
	else if (panel_id == 1)
		addr = DCREG_PANEL1_DSC_Address;
	else if (panel_id == 2)
		addr = DCREG_PANEL2_DSC_Address;

	addr += dword_reg * 4;
	/* For debug */
	// printk("dsc_write %#x, %#0x\n", addr, val);
	dc_write(hw, addr, val);
}

static inline u32 dsc_read(struct dc_hw *hw, u32 panel_id, u32 dword_reg)
{
	u32 ret, addr = 0;

	if (panel_id == 0)
		addr = DCREG_PANEL0_DSC_Address;
	else if (panel_id == 1)
		addr = DCREG_PANEL1_DSC_Address;
	else if (panel_id == 2)
		addr = DCREG_PANEL2_DSC_Address;

	addr += dword_reg * 4;
	ret = dc_read(hw, addr);

	return ret;
}

static int dsc_enc_write_apb_registers(struct dc_hw *hw, u32 panel_id,
				       const struct drm_dsc_config *dsc_cfg,
				       const struct dc_hw_dsc_usage *sw_config,
				       const struct vs_dc_dsc_dataflow_params *dataflow_params)
{
	const u32 HS0_ADDR = 0x040;

	dsc_write(hw, panel_id, ENC_MAIN_CONF_ADDR,
		  ENC_MAIN_CONF_VALUE(sw_config, dataflow_params));

	dsc_write(hw, panel_id, HS0_ADDR | HS_DF_CTRL_ADDR,
		  ENC_DF_CTRL_ADDR_VALUE(dataflow_params, sw_config));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_MAIN_CONF_ADDR, DSC_MAIN_CONF_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_PICT_SIZE_ADDR, DSC_PICT_SIZE_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_SLICE_SIZE_ADDR, DSC_SLICE_SIZE_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_MISC_SIZE_ADDR, DSC_MISC_SIZE_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_HRD_DELAY_ADDR, DSC_HRD_DELAY_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_SCALE_ADDR, DSC_RC_SCALE_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_SCALE_INC_DEC_ADDR,
		  DSC_RC_SCALE_INC_DEC_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_OFFSETS_1_ADDR,
		  DSC_RC_OFFSETS_1_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_OFFSETS_2_ADDR,
		  DSC_RC_OFFSETS_2_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_OFFSETS_3_ADDR,
		  DSC_RC_OFFSETS_3_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_OFFSETS_4_ADDR,
		  DSC_RC_OFFSETS_4_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_FLATNESS_QP_ADDR, DSC_FLATNESS_QP_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_MODEL_SIZE_ADDR,
		  DSC_RC_MODEL_SIZE_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_CONFIG_ADDR, DSC_RC_CONFIG_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_BUF_TRESH_0_ADDR,
		  DSC_RC_BUF_TRESH_0_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_BUF_TRESH_1_ADDR,
		  DSC_RC_BUF_TRESH_1_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_BUF_TRESH_2_ADDR,
		  DSC_RC_BUF_TRESH_2_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_BUF_TRESH_3_ADDR,
		  DSC_RC_BUF_TRESH_3_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_MIN_QP_0_ADDR, DSC_RC_MIN_QP_0_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_MIN_QP_1_ADDR, DSC_RC_MIN_QP_1_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_MIN_QP_2_ADDR, DSC_RC_MIN_QP_2_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_MAX_QP_0_ADDR, DSC_RC_MAX_QP_0_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_MAX_QP_1_ADDR, DSC_RC_MAX_QP_1_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_MAX_QP_2_ADDR, DSC_RC_MAX_QP_2_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_RANGE_BPG_OFFSETS_0_ADDR,
		  RC_RANGE_BPG_OFFSETS_0_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_RANGE_BPG_OFFSETS_1_ADDR,
		  RC_RANGE_BPG_OFFSETS_1_VALUE(dsc_cfg));
	dsc_write(hw, panel_id, HS0_ADDR | HS_DSC_RC_RANGE_BPG_OFFSETS_2_ADDR,
		  RC_RANGE_BPG_OFFSETS_2_VALUE(dsc_cfg));
	return 0;
}

static int enable_dsc(struct dc_hw *hw, u8 hw_id, bool enabled)
{
	u32 config = 0;
	u32 val = 0;

	/**
	 * Enable panel DSC
	 * panel0   - Set split source to DSC.
	 * panel1&2 - Set DSC config enable.
	 */
	switch (hw_id) {
	case 0:
		if (enabled)
			val = DCREG_SH_PANEL0_SPLIT_CONFIG_SOURCE_DSC;
		else
			val = DCREG_SH_PANEL0_SPLIT_CONFIG_SOURCE_PIPE;
		config = VS_SET_FIELD(config, DCREG_SH_PANEL0_SPLIT_CONFIG, SOURCE, val);
		dc_write(hw, DCREG_SH_PANEL0_SPLIT_CONFIG_Address, config);
		break;
	case 1:
		if (enabled)
			val = DCREG_SH_PANEL1_DSC_CONFIG_COMP_ENABLE_ENABLED;
		else
			val = DCREG_SH_PANEL1_DSC_CONFIG_COMP_ENABLE_DISABLED;
		dc_write(hw, DCREG_SH_PANEL1_DSC_CONFIG_Address, val);
		break;
	case 2:
		if (enabled)
			val = DCREG_SH_PANEL2_DSC_CONFIG_COMP_ENABLE_ENABLED;
		else
			val = DCREG_SH_PANEL2_DSC_CONFIG_COMP_ENABLE_DISABLED;
		dc_write(hw, DCREG_SH_PANEL2_DSC_CONFIG_Address, val);
		break;
	default:
		dev_err(hw->dev, "Crtc %u not support DSC\n", hw_id);
		return -EINVAL;
	}
	return 0;
}

int dc_hw_config_dsc(struct dc_hw *hw, u8 hw_id, const struct dc_hw_dsc_usage *sw_cfg,
		     const struct drm_dsc_config *dsc_cfg)
{
	struct vs_dc_dsc_dataflow_params dataflow_params = { 0 };
	struct dc_hw_display *display = (struct dc_hw_display *)vs_dc_hw_get_display(hw, hw_id);

	if (!display) {
		dev_err(hw->dev, "%s: No display found with id %u\n", __func__, hw_id);
		return -ENODEV;
	}
	if (sw_cfg->enable && !dsc_cfg) {
		dev_err(hw->dev, "%s: missing dsc config for id %u\n", __func__, hw_id);
		return -EINVAL;
	}

	trace_disp_dsc(hw_id, sw_cfg->enable, sw_cfg->video_mode, sw_cfg->slices_per_line,
		       sw_cfg->ss_num, dsc_cfg);

	enable_dsc(hw, hw_id, sw_cfg->enable);
	if (sw_cfg->enable) {
		dataflow_params.initial_lines = get_initial_lines(dsc_cfg, sw_cfg);
		dataflow_params.drb_max_addr = get_deraster_buffer(
			&hw_config, sw_cfg->slices_per_line, sw_cfg->ss_num, dsc_cfg->slice_width);
		/* 4k */
		if (1)
			dataflow_params.ob_max_addr = get_ob_max_addr_4k(sw_cfg->ss_num);
		else
			dataflow_params.ob_max_addr = get_ob_max_addr_8k(sw_cfg->ss_num);
		dsc_enc_write_apb_registers(hw, hw_id, dsc_cfg, sw_cfg, &dataflow_params);
	}
	return 0;
}

int dc_hw_read_dsc_status(struct dc_hw *hw, u8 hw_id)
{
	const u32 HS0_ADDR = 0x040;

	u32 gen_stat, hslice_stat, out_stat, intr_stat;

	gen_stat = dsc_read(hw, hw_id, HS0_ADDR | HS_GEN_STATUS_ADDR);
	hslice_stat = dsc_read(hw, hw_id, HS0_ADDR | HS_HSLICE_STATUS_ADDR);
	out_stat = dsc_read(hw, hw_id, HS0_ADDR | HS_OUT_STATUS_ADDR);
	intr_stat = dsc_read(hw, hw_id, HS0_ADDR | HS_INT_STAT_ADDR);

	dev_dbg(hw->dev, "gen_stat:%#010x hslice_stat:%#010x out_stat:%#010x intr_stat:%#010x\n",
		gen_stat, hslice_stat, out_stat, intr_stat);

	trace_disp_dsc_status(gen_stat, hslice_stat, out_stat, intr_stat);

	return 0;
}
