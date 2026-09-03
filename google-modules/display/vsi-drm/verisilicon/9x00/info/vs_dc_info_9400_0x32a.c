/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */

#include "drm/vs_drm_fourcc.h"
#include <drm/vs_drm.h>
#include <linux/regmap.h>
#include "vs_dc_info.h"
#include "vs_dc_reg_be.h"
#include "vs_dc_reg_fe0.h"
#include "vs_dc_reg_fe1.h"
#include "vs_simple_enc.h"

#define FRAC_16_16(mult, div) (((mult) << 16) / (div))

#define regmap_range_single(addr) regmap_reg_range(addr, addr)
#define regmap_range_sized(addr, size) regmap_reg_range((addr), (addr) + (size) - 1)

static const struct regmap_range dump_reg_allowed[] = {
	regmap_range_sized(0x0, 0x1e0), /*DpuFe0HostInterface*/
	regmap_range_sized(0x8000, 0x40), /*DpuFe0Secure0*/
	regmap_range_sized(0x18000, 0xa70), /*DpuFe0Layer0*/
	regmap_range_sized(0x1c000, 0xa70), /*DpuFe0Layer1*/
	regmap_range_sized(0x20000, 0xa70), /*DpuFe0Layer2*/
	regmap_range_sized(0x24000, 0xa70), /*DpuFe0Layer3*/
	regmap_range_sized(0x28000, 0xa70), /*DpuFe0Layer4*/
	regmap_range_sized(0x2c000, 0xa70), /*DpuFe0Layer5*/
	regmap_range_sized(0x38000, 0x180), /*DpuFe0Crc4K*/
	regmap_range_sized(0x39000, 0x140), /*DpuFe0TestPattern4K*/
	regmap_range_sized(0x40000, 0x140), /*DpuFe1HostInterface*/
	regmap_range_sized(0x58000, 0xa70), /*DpuFe1Layer8*/
	regmap_range_sized(0x5C000, 0xa70), /*DpuFe1Layer9*/
	regmap_range_sized(0x60000, 0xa70), /*DpuFe1Layer10*/
	regmap_range_sized(0x64000, 0xa70), /*DpuFe1Layer11*/
	regmap_range_sized(0x68000, 0xa70), /*DpuFe1Layer12*/
	regmap_range_sized(0x6C000, 0xa70), /*DpuFe1Layer13*/
	regmap_range_sized(0x78000, 0x180), /*DpuFe1Crc4K*/
	regmap_range_sized(0x79000, 0x140), /*DpuFe1TestPattern4K*/
	regmap_range_sized(0x80000, 0x260), /*DpuBeHostInterface*/
	regmap_range_sized(0x84000, 0x1c0), /*DpuBeSecure0*/
	regmap_range_sized(0x88000, 0x8e0), /*DpuBeBlender*/
	regmap_range_sized(0x8c000, 0x1fc0), /*DpuBePostProcessorPanel0*/
	regmap_range_sized(0x8f254, 0x18a0), /*DpuBePostProcessorPanel0_1*/
	regmap_range_sized(0x9c000, 0x1240), /*DpuBePostProcessorPanel1*/
	regmap_range_sized(0x9f264, 0x1850), /*DpuBePostProcessorPanel1_1*/
	regmap_range_sized(0xac000, 0x5f0), /*DpuBePostProcessorPanel2*/
	regmap_range_sized(0xb0000, 0x5f0), /*DpuBePostProcessorPanel3*/
	regmap_range_sized(0xb4000, 0xb0), /*DpuBeBldWb*/
	regmap_range_sized(0xb5000, 0x60), /*DpuBePostProWb*/
	regmap_range_sized(0xb6000, 0x2a0), /*DpuOutputInterface*/
	regmap_range_sized(0xb7000, 0xa0), /*DpuBePanel0Crc4K*/
	regmap_range_sized(0xb8000, 0xa0), /*DpuBePanel1Crc4K*/
	regmap_range_sized(0xb9000, 0xa0), /*DpuBePanel2Crc4K*/
	regmap_range_sized(0xba000, 0xa0), /*DpuBePanel3Crc4K*/
	regmap_range_sized(0xbb000, 0x80), /*DpuBeWbWDMACrc4K*/
	regmap_range_sized(0xbc000, 0x2b0), /*DpuBeTestPattern4K*/
	regmap_range_sized(0xbd000, 0x10d0), /*DpuBePanel0Hist8K*/
	regmap_range_sized(0xbf000, 0x10), /*DpuBePanel0LTMHist4K*/
	regmap_range_sized(0xc7000, 0xc10), /*DpuBePanel0RGBHist4K*/
	regmap_range_sized(0xc8000, 0x10d0), /*DpuBePanel1Hist8K*/
	regmap_range_sized(0xd2000, 0x200), /*DpuBePanel0Scaler4K*/
	regmap_range_sized(0xd4000, 0x10), /*DpuBeMux*/
	regmap_range_sized(0xd7000, 0x10), /*DpuBeBlenderLayer0OutputPath*/
	regmap_range_sized(0xd8000, 0x10), /*DpuBeBlenderLayer1OutputPath*/
	regmap_range_sized(0xd9000, 0x10), /*DpuBeBlenderLayer2OutputPath*/
	regmap_range_sized(0xda000, 0x10), /*DpuBeBlenderLayer3OutputPath*/
	regmap_range_sized(0xdb000, 0x10), /*DpuBeBlenderLayer4OutputPath*/
	regmap_range_sized(0xdc000, 0x10), /*DpuBeBlenderLayer5OutputPath*/
	regmap_range_sized(0xdf000, 0x10), /*DpuBeBlenderLayer8OutputPath*/
	regmap_range_sized(0xe0000, 0x10), /*DpuBeBlenderLayer9OutputPath*/
	regmap_range_sized(0xe1000, 0x10), /*DpuBeBlenderLayer10OutputPath*/
	regmap_range_sized(0xe2000, 0x10), /*DpuBeBlenderLayer11OutputPath*/
	regmap_range_sized(0xe3000, 0x10), /*DpuBeBlenderLayer12OutputPath*/
	regmap_range_sized(0xe4000, 0x10), /*DpuBeBlenderLayer13OutputPath*/
	regmap_range_sized(0xe7000, 0x10), /*DpuBeBlenderLayer0BlendStack*/
	regmap_range_sized(0xe8000, 0x10), /*DpuBeBlenderLayer1BlendStack*/
	regmap_range_sized(0xe9000, 0x10), /*DpuBeBlenderLayer2BlendStack*/
	regmap_range_sized(0xea000, 0x10), /*DpuBeBlenderLayer3BlendStack*/
	regmap_range_sized(0xeb000, 0x10), /*DpuBeBlenderLayer4BlendStack*/
	regmap_range_sized(0xec000, 0x10), /*DpuBeBlenderLayer5BlendStack*/
	regmap_range_sized(0xef000, 0x10), /*DpuBeBlenderLayer8BlendStack*/
	regmap_range_sized(0xf0000, 0x10), /*DpuBeBlenderLayer9BlendStack*/
	regmap_range_sized(0xf1000, 0x10), /*DpuBeBlenderLayer10BlendStack*/
	regmap_range_sized(0xf2000, 0x10), /*DpuBeBlenderLayer11BlendStack*/
	regmap_range_sized(0xf3000, 0x10), /*DpuBeBlenderLayer12BlendStack*/
	regmap_range_sized(0xf4000, 0x10), /*DpuBeBlenderLayer13BlendStack*/
	regmap_range_sized(0xf9000, 0x220), /*DpuPanel0Dsc*/
	regmap_range_sized(0xfb000, 0x220), /*DpuPanel1Dsc*/
	regmap_range_sized(0xff000, 0x220), /*DpuPanel2Dsc*/
};

#define dump_reg_disallowed_normal_contents \
	regmap_range_single(DCREG_BE_INTR_TEST_Address),\
	regmap_range_single(DCREG_BE_INTR_TEST1_Address),\
	regmap_range_single(DCREG_BE_INTR_TEST2_Address),\
	regmap_range_single(DCREG_BE_INTR_TEST3_Address),\
	regmap_range_single(DCREG_BE_TZ_INTR_TEST_Address),\
	regmap_range_single(DCREG_BE_TZ_INTR_TEST1_Address),\
	regmap_range_single(DCREG_BE_GSA_INTR_TEST_Address),\
	regmap_range_single(DCREG_BE_GSA_INTR_TEST1_Address),\
	regmap_range_single(DCREG_BE_AOC_INTR_TEST_Address),\
	regmap_range_single(DCREG_BE_AOC_INTR_TEST1_Address),\
	regmap_range_single(DCREG_OUTIF_INTR_TEST_Address),\
	regmap_range_single(DCREG_OUTIF_INTR_TEST1_Address),\
	regmap_range_single(DCREG_OUTIF_INTR_TEST2_Address),\
	regmap_range_single(DCREG_OUTIF_INTR_TEST3_Address),\
	regmap_range_single(DCREG_PDMA_INTR_TEST_Address),\
	regmap_range_single(DCREG_PDMA_INTR_TEST1_Address),\
	regmap_range_single(DCREG_PDMA_INTR_TEST2_Address),\
	regmap_range_single(DCREG_PDMA_INTR_TEST3_Address),\
	regmap_range_single(DCREG_PDMA_INTR_TEST4_Address),\
	regmap_range_single(DCREG_FE0_INTR_TEST_Address),\
	regmap_range_single(DCREG_FE0_TZ_INTR_TEST_Address),\
	regmap_range_single(DCREG_FE0_GSA_INTR_TEST_Address),\
	regmap_range_single(DCREG_FE1_INTR_TEST_Address),\
	regmap_range_single(DCREG_FE1_TZ_INTR_TEST_Address),\
	regmap_range_single(DCREG_FE1_GSA_INTR_TEST_Address)

#define dump_reg_disallowed_secure_contents \
	regmap_range_sized(0x8000, 0x40), /*DpuFe0Secure0*/\
	regmap_range_sized(0x84000, 0x1000), /*DpuBeSecure0*/\
	regmap_range_sized(0xb4000, 0x2000), /*DpuBeBldWb-DpuBePostProWb*/\
	regmap_range_sized(0xb7000, 0x9000), /*DpuBePanel0Crc4K-DpuBePanel0LTMHist4K*/\
	regmap_range_sized(0xc7000, 0x3000) /*DpuBePanel0RGBHist4K-DpuBePanel1Hist8K*/

static const struct regmap_range dump_reg_disallowed[] = {
	dump_reg_disallowed_normal_contents,
};

static const struct regmap_range dump_reg_disallowed_secure[] = {
	dump_reg_disallowed_normal_contents,
	dump_reg_disallowed_secure_contents,
};

static const struct regmap_access_table dump_reg_access_table = {
	.yes_ranges = dump_reg_allowed,
	.n_yes_ranges = ARRAY_SIZE(dump_reg_allowed),
	.no_ranges = dump_reg_disallowed,
	.n_no_ranges = ARRAY_SIZE(dump_reg_disallowed),
};

static const struct regmap_access_table dump_reg_access_table_secure = {
	.yes_ranges = dump_reg_allowed,
	.n_yes_ranges = ARRAY_SIZE(dump_reg_allowed),
	.no_ranges = dump_reg_disallowed_secure,
	.n_no_ranges = ARRAY_SIZE(dump_reg_disallowed_secure),
};

/*
 * RGB to YUV2020 in limited range conversion parameters(10 bit pipeline)
 * RGB2YUV[0] - [8] : C0 - C8;
 * RGB2YUV[9] - [11]: Pre Coefficient D0 - D2;
 * RGB2YUV[12] - [14]: Post Coefficient D0 - D2;
 */

static const u32 plane_format0[] = {
	DRM_FORMAT_XRGB4444,	DRM_FORMAT_XBGR4444,	  DRM_FORMAT_RGBX4444,
	DRM_FORMAT_BGRX4444,	DRM_FORMAT_ARGB4444,	  DRM_FORMAT_ABGR4444,
	DRM_FORMAT_RGBA4444,	DRM_FORMAT_BGRA4444,	  DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR1555,	DRM_FORMAT_RGBX5551,	  DRM_FORMAT_BGRX5551,
	DRM_FORMAT_ARGB1555,	DRM_FORMAT_ABGR1555,	  DRM_FORMAT_RGBA5551,
	DRM_FORMAT_BGRA5551,	DRM_FORMAT_RGB565,	      DRM_FORMAT_BGR565,
	DRM_FORMAT_XRGB8888,	DRM_FORMAT_XBGR8888,	  DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,	DRM_FORMAT_ARGB8888,	  DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,	DRM_FORMAT_BGRA8888,	  DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010, DRM_FORMAT_RGBA1010102,	  DRM_FORMAT_BGRA1010102,
	DRM_FORMAT_XRGB2101010, DRM_FORMAT_XBGR2101010,	  DRM_FORMAT_RGBX1010102,
	DRM_FORMAT_BGRX1010102, DRM_FORMAT_ARGB16161616F, DRM_FORMAT_ABGR16161616F,
	DRM_FORMAT_YVU420,	    DRM_FORMAT_YUV420,	      DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,	    DRM_FORMAT_NV16,	      DRM_FORMAT_NV61,
	DRM_FORMAT_P010,	    DRM_FORMAT_P210,	      DRM_FORMAT_YUV420_10BIT,
};

static const u32 plane_format1[] = {
	DRM_FORMAT_XRGB4444,	  DRM_FORMAT_XBGR4444,	    DRM_FORMAT_RGBX4444,
	DRM_FORMAT_BGRX4444,	  DRM_FORMAT_ARGB4444,	    DRM_FORMAT_ABGR4444,
	DRM_FORMAT_RGBA4444,	  DRM_FORMAT_BGRA4444,	    DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR1555,	  DRM_FORMAT_RGBX5551,	    DRM_FORMAT_BGRX5551,
	DRM_FORMAT_ARGB1555,	  DRM_FORMAT_ABGR1555,	    DRM_FORMAT_RGBA5551,
	DRM_FORMAT_BGRA5551,	  DRM_FORMAT_RGB565,	    DRM_FORMAT_BGR565,
	DRM_FORMAT_XRGB8888,	  DRM_FORMAT_XBGR8888,	    DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRX8888,	  DRM_FORMAT_ARGB8888,	    DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,	  DRM_FORMAT_BGRA8888,	    DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010,	  DRM_FORMAT_RGBA1010102,   DRM_FORMAT_BGRA1010102,
	DRM_FORMAT_ARGB16161616F, DRM_FORMAT_ABGR16161616F,
};

static const u32 wb_format0[] = {
	DRM_FORMAT_XRGB8888,
	/*
	 * Use DRM_FORMAT_XRGB16161616F to represent
	 * the writeback of DP output, as HW will write-back
	 * at 64bits/pixel in this case.
	 */
	DRM_FORMAT_XRGB16161616F,
};

static const u32 wb_format2[] = {
	DRM_FORMAT_ARGB8888, DRM_FORMAT_ARGB2101010, DRM_FORMAT_YVU420,	      DRM_FORMAT_YUV420,
	DRM_FORMAT_NV12,     DRM_FORMAT_NV21,	     DRM_FORMAT_NV16,	      DRM_FORMAT_NV61,
	DRM_FORMAT_P010,     DRM_FORMAT_P210,	     DRM_FORMAT_YUV420_10BIT,
};

#if IS_ENABLED(CONFIG_VERISILICON_PLANE_RCD)
static const u32 rcd_format[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_R8,
};
#endif

static const u64 format_modifier[] = {
	DRM_FORMAT_MOD_LINEAR,
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_LINEAR),
	fourcc_mod_vs_custom_code(DRM_FORMAT_MOD_VS_LINEAR),
	fourcc_mod_vs_pvric_code(DRM_FORMAT_MOD_VS_DEC_TILE_8X8, DRM_FORMAT_MOD_VS_DEC_LOSSLESS),
	fourcc_mod_vs_pvric_code(DRM_FORMAT_MOD_VS_DEC_TILE_8X8, DRM_FORMAT_MOD_VS_DEC_LOSSY),
	fourcc_mod_vs_pvric_code(DRM_FORMAT_MOD_VS_DEC_TILE_16X4, DRM_FORMAT_MOD_VS_DEC_LOSSLESS),
	fourcc_mod_vs_pvric_code(DRM_FORMAT_MOD_VS_DEC_TILE_16X4, DRM_FORMAT_MOD_VS_DEC_LOSSY),
	fourcc_mod_vs_pvric_code(DRM_FORMAT_MOD_VS_DEC_TILE_32X2, DRM_FORMAT_MOD_VS_DEC_LOSSLESS),
	fourcc_mod_vs_pvric_code(DRM_FORMAT_MOD_VS_DEC_TILE_32X2, DRM_FORMAT_MOD_VS_DEC_LOSSY),
	DRM_FORMAT_MOD_PVR_FBCDC_8x8_V14,
	DRM_FORMAT_MOD_PVR_FBCDC_16x4_V14,
	fourcc_mod_vs_code(DRM_FORMAT_MOD_VS_TYPE_SHALLOW, 0),
	DRM_FORMAT_MOD_INVALID,
};

static const u64 wb_modifier[] = {
	DRM_FORMAT_MOD_LINEAR,
	fourcc_mod_vs_norm_code(DRM_FORMAT_MOD_VS_LINEAR),
	fourcc_mod_vs_custom_code(DRM_FORMAT_MOD_VS_LINEAR),
	DRM_FORMAT_MOD_INVALID,
};

static const struct vs_dc_urgent_cmd_config urgent_cmd_config = {
	.h_margin_pct = 90,
	.v_margin_pct = 100,
	.delay_counter_usec = 200,
	.urgent_value = 3,
	.enable = true,
};

static const struct vs_dc_urgent_vid_config urgent_vid_config = {
	.qos_thresh_0 = 1946,
	.qos_thresh_1 = 1638,
	.qos_thresh_2 = 1229,
	.urgent_thresh_0 = 1946,
	.urgent_thresh_1 = 1638,
	.urgent_thresh_2 = 1229,
	.urgent_low_thresh = 1638,
	.urgent_high_thresh = 1946,
	.healthy_thresh = 2048,
	.enable = true,
};

#define DMA_SRAM_MAX_SIZE_KB (256)
#define SCL_SRAM_MAX_SIZE_KB (144)

static const struct vs_plane_info plane_fe0_info[] = {
	/* DC_REV_0 */
	{
		.name = "Layer0",
		.id = HW_PLANE_0,
		.sid = HW_PLANE_0_SID,
		.fe_id = VS_FE_0,
		.type = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(plane_format0),
		.formats = plane_format0,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_180 |
			    DRM_MODE_ROTATE_270 | DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
		.blend_mode = BIT(DRM_MODE_BLEND_PREMULTI) | BIT(DRM_MODE_BLEND_COVERAGE) |
			      BIT(DRM_MODE_BLEND_PIXEL_NONE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT601) | BIT(DRM_COLOR_YCBCR_BT709) |
				  BIT(DRM_COLOR_YCBCR_BT2020),
		.color_range = BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) | BIT(DRM_COLOR_YCBCR_FULL_RANGE),
		.min_scale = FRAC_16_16(1, 8), /* src << 16 / dst, mean up scale max 8 times */
		.max_scale = FRAC_16_16(4, 1), /* src << 16 / dst, meas down scale max 4 times */
		.zpos = 0,
		.max_uv_phase = 16,
		.axi_id = 0,
		.outstanding_number = 127,
		.color_mgmt = 1,
		.program_csc = 1,
		.roi = 1,
		.roi_two = 1,
		.roi_skip = 0,
		.layer_ext = 1,
		.layer_ext_ex = 1,
		.alpha_ext = 1,
		.demultiply = 1,
		.hdr = 1,
		.tone_map = 1,
		.blend_config = 1,
		.test_pattern = 1,
		.compressed = 1,
		.data_extend = 1,
		.dma_sram_max_size_kb = DMA_SRAM_MAX_SIZE_KB,
		.scl_sram_max_size_kb = SCL_SRAM_MAX_SIZE_KB,
	},
	{
		.name = "Layer1",
		.id = HW_PLANE_1,
		.sid = HW_PLANE_1_SID,
		.fe_id = VS_FE_0,
		.type = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(plane_format0),
		.formats = plane_format0,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_180 |
			    DRM_MODE_ROTATE_270 | DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
		.blend_mode = BIT(DRM_MODE_BLEND_PREMULTI) | BIT(DRM_MODE_BLEND_COVERAGE) |
			      BIT(DRM_MODE_BLEND_PIXEL_NONE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT601) | BIT(DRM_COLOR_YCBCR_BT709) |
				  BIT(DRM_COLOR_YCBCR_BT2020),
		.color_range = BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) | BIT(DRM_COLOR_YCBCR_FULL_RANGE),
		.min_scale = FRAC_16_16(1, 8), /* src << 16 / dst, mean up scale max 8 times */
		.max_scale = FRAC_16_16(4, 1), /* src << 16 / dst, meas down scale max 4 times */
		.zpos = 1,
		.max_uv_phase = 16,
		.axi_id = 1,
		.outstanding_number = 127,
		.color_mgmt = 1,
		.program_csc = 1,
		.roi = 1,
		.roi_two = 1,
		.roi_skip = 0,
		.layer_ext = 1,
		.layer_ext_ex = 1,
		.alpha_ext = 1,
		.demultiply = 1,
		.hdr = 1,
		.tone_map = 1,
		.blend_config = 1,
		.test_pattern = 1,
		.compressed = 1,
		.data_extend = 1,
		.dma_sram_max_size_kb = DMA_SRAM_MAX_SIZE_KB,
		.scl_sram_max_size_kb = SCL_SRAM_MAX_SIZE_KB,
	},
	{
		.name = "Layer2",
		.id = HW_PLANE_2,
		.sid = HW_PLANE_2_SID,
		.fe_id = VS_FE_0,
		.type = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(plane_format1),
		.formats = plane_format1,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0,
		.blend_mode = BIT(DRM_MODE_BLEND_PREMULTI) | BIT(DRM_MODE_BLEND_COVERAGE) |
			      BIT(DRM_MODE_BLEND_PIXEL_NONE),
		.min_scale = VS_PLANE_NO_SCALING,
		.max_scale = VS_PLANE_NO_SCALING,
		.zpos = 2,
		.axi_id = 0,
		.outstanding_number = 64,
		.color_mgmt = 1,
		.roi = 1,
		.roi_two = 1,
		.roi_skip = 0,
		.layer_ext = 1,
		.layer_ext_ex = 1,
		.alpha_ext = 1,
		.demultiply = 1,
		.hdr = 1,
		.tone_map = 1,
		.blend_config = 1,
		.test_pattern = 1,
		.compressed = 1,
		.data_extend = 1,
		.dma_sram_max_size_kb = DMA_SRAM_MAX_SIZE_KB,
	},
	{
		.name = "Layer3",
		.id = HW_PLANE_3,
		.sid = HW_PLANE_3_SID,
		.fe_id = VS_FE_0,
		.type = DRM_PLANE_TYPE_PRIMARY,
		.crtc_id = 0x0,
		.num_formats = ARRAY_SIZE(plane_format1),
		.formats = plane_format1,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0,
		.blend_mode = BIT(DRM_MODE_BLEND_PREMULTI) | BIT(DRM_MODE_BLEND_COVERAGE) |
			      BIT(DRM_MODE_BLEND_PIXEL_NONE),
		.min_scale = VS_PLANE_NO_SCALING,
		.max_scale = VS_PLANE_NO_SCALING,
		.zpos = 3,
		.axi_id = 1,
		.outstanding_number = 64,
		.color_mgmt = 1,
		.roi = 1,
		.roi_two = 1,
		.roi_skip = 0,
		.layer_ext = 1,
		.layer_ext_ex = 1,
		.alpha_ext = 1,
		.demultiply = 1,
		.hdr = 1,
		.tone_map = 1,
		.blend_config = 1,
		.test_pattern = 1,
		.compressed = 1,
		.data_extend = 1,
		.dma_sram_max_size_kb = DMA_SRAM_MAX_SIZE_KB,
	},
	{
		.name = "Layer4",
		.id = HW_PLANE_4,
		.sid = HW_PLANE_4_SID,
		.fe_id = VS_FE_0,
		.type = DRM_PLANE_TYPE_PRIMARY,
		.crtc_id = 0x1,
		.num_formats = ARRAY_SIZE(plane_format1),
		.formats = plane_format1,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0,
		.blend_mode = BIT(DRM_MODE_BLEND_PREMULTI) | BIT(DRM_MODE_BLEND_COVERAGE) |
			      BIT(DRM_MODE_BLEND_PIXEL_NONE),
		.min_scale = VS_PLANE_NO_SCALING,
		.max_scale = VS_PLANE_NO_SCALING,
		.zpos = 4,
		.axi_id = 0,
		.outstanding_number = 64,
		.color_mgmt = 1,
		.roi = 1,
		.roi_two = 1,
		.roi_skip = 0,
		.layer_ext = 1,
		.layer_ext_ex = 1,
		.alpha_ext = 1,
		.demultiply = 1,
		.hdr = 1,
		.tone_map = 1,
		.blend_config = 1,
		.test_pattern = 1,
		.compressed = 1,
		.data_extend = 1,
		.dma_sram_max_size_kb = DMA_SRAM_MAX_SIZE_KB,
	},
	{
		.name = "Layer5",
		.id = HW_PLANE_5,
		.sid = HW_PLANE_5_SID,
		.fe_id = VS_FE_0,
		.type = DRM_PLANE_TYPE_PRIMARY,
		.crtc_id = 0x2,
		.num_formats = ARRAY_SIZE(plane_format1),
		.formats = plane_format1,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0,
		.blend_mode = BIT(DRM_MODE_BLEND_PREMULTI) | BIT(DRM_MODE_BLEND_COVERAGE) |
			      BIT(DRM_MODE_BLEND_PIXEL_NONE),
		.min_scale = VS_PLANE_NO_SCALING,
		.max_scale = VS_PLANE_NO_SCALING,
		.zpos = 5,
		.axi_id = 1,
		.outstanding_number = 64,
		.color_mgmt = 1,
		.roi = 1,
		.roi_two = 1,
		.roi_skip = 0,
		.layer_ext = 1,
		.layer_ext_ex = 1,
		.alpha_ext = 1,
		.demultiply = 1,
		.hdr = 1,
		.tone_map = 1,
		.blend_config = 1,
		.test_pattern = 1,
		.compressed = 1,
		.data_extend = 1,
		.dma_sram_max_size_kb = DMA_SRAM_MAX_SIZE_KB,
	},
};

static const struct vs_plane_info plane_fe1_info[] = {
	{
		.name = "Layer8",
		.id = HW_PLANE_8,
		.sid = HW_PLANE_8_SID,
		.fe_id = VS_FE_1,
		.type = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(plane_format0),
		.formats = plane_format0,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_180 |
			    DRM_MODE_ROTATE_270 | DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
		.blend_mode = BIT(DRM_MODE_BLEND_PREMULTI) | BIT(DRM_MODE_BLEND_COVERAGE) |
			      BIT(DRM_MODE_BLEND_PIXEL_NONE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT601) | BIT(DRM_COLOR_YCBCR_BT709) |
				  BIT(DRM_COLOR_YCBCR_BT2020),
		.color_range = BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) | BIT(DRM_COLOR_YCBCR_FULL_RANGE),
		.min_scale = FRAC_16_16(1, 8), /* src << 16 / dst, mean up scale max 8 times */
		.max_scale = FRAC_16_16(4, 1), /* src << 16 / dst, meas down scale max 4 times */
		.zpos = 6,
		.max_uv_phase = 16,
		.axi_id = 1,
		.outstanding_number = 127,
		.color_mgmt = 1,
		.program_csc = 1,
		.roi = 1,
		.roi_two = 1,
		.roi_skip = 0,
		.layer_ext = 1,
		.layer_ext_ex = 1,
		.alpha_ext = 1,
		.demultiply = 1,
		.hdr = 1,
		.tone_map = 1,
		.blend_config = 1,
		.test_pattern = 1,
		.compressed = 1,
		.data_extend = 1,
		.dma_sram_max_size_kb = DMA_SRAM_MAX_SIZE_KB,
		.scl_sram_max_size_kb = SCL_SRAM_MAX_SIZE_KB,
	},
	{
		.name = "Layer9",
		.id = HW_PLANE_9,
		.sid = HW_PLANE_9_SID,
		.fe_id = VS_FE_1,
		.type = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(plane_format0),
		.formats = plane_format0,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0 | DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_180 |
			    DRM_MODE_ROTATE_270 | DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y,
		.blend_mode = BIT(DRM_MODE_BLEND_PREMULTI) | BIT(DRM_MODE_BLEND_COVERAGE) |
			      BIT(DRM_MODE_BLEND_PIXEL_NONE),
		.color_encoding = BIT(DRM_COLOR_YCBCR_BT601) | BIT(DRM_COLOR_YCBCR_BT709) |
				  BIT(DRM_COLOR_YCBCR_BT2020),
		.color_range = BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) | BIT(DRM_COLOR_YCBCR_FULL_RANGE),
		.min_scale = FRAC_16_16(1, 8), /* src << 16 / dst, mean up scale max 8 times */
		.max_scale = FRAC_16_16(4, 1), /* src << 16 / dst, meas down scale max 4 times */
		.zpos = 7,
		.max_uv_phase = 16,
		.axi_id = 0,
		.outstanding_number = 127,
		.color_mgmt = 1,
		.program_csc = 1,
		.roi = 1,
		.roi_two = 1,
		.roi_skip = 0,
		.layer_ext = 1,
		.layer_ext_ex = 1,
		.alpha_ext = 1,
		.demultiply = 1,
		.hdr = 1,
		.tone_map = 1,
		.blend_config = 1,
		.test_pattern = 1,
		.compressed = 1,
		.data_extend = 1,
		.dma_sram_max_size_kb = DMA_SRAM_MAX_SIZE_KB,
		.scl_sram_max_size_kb = SCL_SRAM_MAX_SIZE_KB,
	},
	{
		.name = "Layer10",
		.id = HW_PLANE_10,
		.sid = HW_PLANE_10_SID,
		.fe_id = VS_FE_1,
		.type = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(plane_format1),
		.formats = plane_format1,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0,
		.blend_mode = BIT(DRM_MODE_BLEND_PREMULTI) | BIT(DRM_MODE_BLEND_COVERAGE) |
			      BIT(DRM_MODE_BLEND_PIXEL_NONE),
		.min_scale = VS_PLANE_NO_SCALING,
		.max_scale = VS_PLANE_NO_SCALING,
		.zpos = 8,
		.axi_id = 1,
		.outstanding_number = 64,
		.color_mgmt = 1,
		.roi = 1,
		.roi_two = 1,
		.roi_skip = 0,
		.layer_ext = 1,
		.layer_ext_ex = 1,
		.alpha_ext = 1,
		.demultiply = 1,
		.hdr = 1,
		.tone_map = 1,
		.blend_config = 1,
		.test_pattern = 1,
		.compressed = 1,
		.data_extend = 1,
		.dma_sram_max_size_kb = DMA_SRAM_MAX_SIZE_KB,
	},
	{
		.name = "Layer11",
		.id = HW_PLANE_11,
		.sid = HW_PLANE_11_SID,
		.fe_id = VS_FE_1,
		.type = DRM_PLANE_TYPE_PRIMARY,
		.crtc_id = 0x3,
		.num_formats = ARRAY_SIZE(plane_format1),
		.formats = plane_format1,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0,
		.blend_mode = BIT(DRM_MODE_BLEND_PREMULTI) | BIT(DRM_MODE_BLEND_COVERAGE) |
			      BIT(DRM_MODE_BLEND_PIXEL_NONE),
		.min_scale = VS_PLANE_NO_SCALING,
		.max_scale = VS_PLANE_NO_SCALING,
		.zpos = 9,
		.axi_id = 0,
		.outstanding_number = 64,
		.color_mgmt = 1,
		.roi = 1,
		.roi_two = 1,
		.roi_skip = 0,
		.layer_ext = 1,
		.layer_ext_ex = 1,
		.alpha_ext = 1,
		.demultiply = 1,
		.hdr = 1,
		.tone_map = 1,
		.blend_config = 1,
		.test_pattern = 1,
		.compressed = 1,
		.data_extend = 1,
		.dma_sram_max_size_kb = DMA_SRAM_MAX_SIZE_KB,
	},
	{
		.name = "Layer12",
		.id = HW_PLANE_12,
		.sid = HW_PLANE_12_SID,
		.fe_id = VS_FE_1,
		.type = DRM_PLANE_TYPE_PRIMARY,
		.crtc_id = 0x4,
		.num_formats = ARRAY_SIZE(plane_format1),
		.formats = plane_format1,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0,
		.blend_mode = BIT(DRM_MODE_BLEND_PREMULTI) | BIT(DRM_MODE_BLEND_COVERAGE) |
			      BIT(DRM_MODE_BLEND_PIXEL_NONE),
		.min_scale = VS_PLANE_NO_SCALING,
		.max_scale = VS_PLANE_NO_SCALING,
		.zpos = 10,
		.axi_id = 1,
		.outstanding_number = 64,
		.color_mgmt = 1,
		.roi = 1,
		.roi_two = 1,
		.roi_skip = 0,
		.layer_ext = 1,
		.layer_ext_ex = 1,
		.alpha_ext = 1,
		.demultiply = 1,
		.hdr = 1,
		.tone_map = 1,
		.blend_config = 1,
		.test_pattern = 1,
		.compressed = 1,
		.data_extend = 1,
		.dma_sram_max_size_kb = DMA_SRAM_MAX_SIZE_KB,
	},
	{
		.name = "Layer13",
		.id = HW_PLANE_13,
		.sid = HW_PLANE_13_SID,
		.fe_id = VS_FE_1,
		.type = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(plane_format1),
		.formats = plane_format1,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0,
		.blend_mode = BIT(DRM_MODE_BLEND_PREMULTI) | BIT(DRM_MODE_BLEND_COVERAGE) |
			      BIT(DRM_MODE_BLEND_PIXEL_NONE),
		.min_scale = VS_PLANE_NO_SCALING,
		.max_scale = VS_PLANE_NO_SCALING,
		.zpos = 11,
		.axi_id = 0,
		.outstanding_number = 64,
		.color_mgmt = 1,
		.roi = 1,
		.roi_two = 1,
		.roi_skip = 0,
		.layer_ext = 1,
		.layer_ext_ex = 1,
		.alpha_ext = 1,
		.demultiply = 1,
		.hdr = 1,
		.tone_map = 1,
		.blend_config = 1,
		.test_pattern = 1,
		.compressed = 1,
		.data_extend = 1,
		.dma_sram_max_size_kb = DMA_SRAM_MAX_SIZE_KB,
	},
#if IS_ENABLED(CONFIG_VERISILICON_PLANE_RCD)
	{
		.name = "RCD_0",
		.id = HW_PLANE_14,
		.sid = HW_PLANE_NOT_SUPPORTED_SID,
		.fe_id = VS_FE_NONE,
		.crtc_id = 0x0,
		.type = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(rcd_format),
		.formats = rcd_format,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = VS_PLANE_NO_SCALING,
		.max_scale = VS_PLANE_NO_SCALING,
		.zpos = 12,
		.axi_id = 1,
		.rcd_plane = 1,
	},
	{
		.name = "RCD_1",
		.id = HW_PLANE_15,
		.sid = HW_PLANE_NOT_SUPPORTED_SID,
		.fe_id = VS_FE_NONE,
		.crtc_id = 0x1,
		.type = DRM_PLANE_TYPE_OVERLAY,
		.num_formats = ARRAY_SIZE(rcd_format),
		.formats = rcd_format,
		.num_modifiers = ARRAY_SIZE(format_modifier),
		.modifiers = format_modifier,
		.min_width = 8,
		.min_height = 8,
		.max_width = 8192,
		.max_yuv_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = VS_PLANE_NO_SCALING,
		.max_scale = VS_PLANE_NO_SCALING,
		.zpos = 13,
		.axi_id = 1,
		.rcd_plane = 1,
	},
#endif
};

static const struct vs_display_info crtc_be_info[] = {
	/* DC_REV_0 */
	{
		.name = "Out_ctrl0",
		.id = HW_DISPLAY_0,
		.color_formats = DRM_COLOR_FORMAT_RGB444,
		.max_width = 3840,
		.max_height = 2160,
		.min_scale = FRAC_16_16(1, 4), /* src << 16 / dst, means up scale max 4 times */
		.max_scale = FRAC_16_16(1, 1),
		.background = 1,
		.bld_oetf = 1,
		.bld_dth = 1,
		.gamma_dth = 1,
		.panel_dth = 1,
		.llv_dth = 1,
		.ltm = 1,
		.sharpness = 1,
		.brightness = 1,
		.degamma = 1,
		.gamma = 1,
		.ccm_non_linear = 1,
		.ccm_linear = 1,
		.cgm_lut = 1,
		.lut_roi0 = 1,
		.lut_roi1 = 0,
		.blur = 1,
		.sec_roi = 1,
		.data_mode = 1,
		.histogram = 1,
		.rgb_hist = 1,
		.test_pattern = 1,
		.disp_in_wb = 1,
		.disp_cc_wb = 1,
		.disp_out_wb = 1,
		.ofifo_in_wb = 1,
		.ofifo_out_wb = 1,
		.decompress = 1,
		.dsc = 1,
		.vdc = 1,
	},
	{
		.name = "Out_ctrl1",
		.id = HW_DISPLAY_1,
		.color_formats = DRM_COLOR_FORMAT_RGB444,
		.max_width = 3840,
		.max_height = 2160,
		.min_scale = FRAC_16_16(1, 1),
		.max_scale = FRAC_16_16(1, 1),
		.background = 1,
		.bld_oetf = 1,
		.bld_dth = 1,
		.gamma_dth = 1,
		.panel_dth = 1,
		.llv_dth = 1,
		.gtm = 1,
		.sharpness = 1,
		.brightness = 1,
		.degamma = 1,
		.gamma = 1,
		.ccm_non_linear = 1,
		.ccm_linear = 1,
		.cgm_lut = 1,
		.lut_roi0 = 0,
		.lut_roi1 = 0,
		.blur = 0,
		.sec_roi = 1,
		.data_mode = 1,
		.histogram = 1,
		.test_pattern = 1,
		.disp_in_wb = 1,
		.disp_cc_wb = 1,
		.disp_out_wb = 1,
		.ofifo_in_wb = 1,
		.ofifo_out_wb = 1,
		.decompress = 1,
		.dsc = 1,
		.vdc = 1,
	},
	{
		.name = "Out_ctrl2",
		.id = HW_DISPLAY_2,
		.color_formats = DRM_COLOR_FORMAT_RGB444 | DRM_COLOR_FORMAT_YCBCR444 |
				 DRM_COLOR_FORMAT_YCBCR422, /* TBD */
		.max_width = 7680,
		.max_height = 4320,
		.min_scale = FRAC_16_16(1, 1),
		.max_scale = FRAC_16_16(1, 1),
		.background = 1,
		.bld_cgm = 1,
		.bld_oetf = 1,
		.bld_dth = 1,
		.panel_dth = 1,
		.test_pattern = 1,
		.disp_in_wb = 1,
		.disp_out_wb = 1,
		.ofifo_in_wb = 1,
		.ofifo_out_wb = 1,
		.decompress = 0,
		.dsc = 1,
		.data_mode = 1,
	},
	{
		.name = "Out_ctrl3",
		.id = HW_DISPLAY_3,
		.color_formats = DRM_COLOR_FORMAT_RGB444 | DRM_COLOR_FORMAT_YCBCR444 |
				 DRM_COLOR_FORMAT_YCBCR422, /* TBD */
		.max_width = 7680,
		.max_height = 4320,
		.min_scale = FRAC_16_16(1, 1),
		.max_scale = FRAC_16_16(1, 1),
		.background = 1,
		.bld_cgm = 1,
		.bld_oetf = 1,
		.bld_dth = 1,
		.panel_dth = 1,
		.test_pattern = 1,
		.disp_in_wb = 1,
		.disp_out_wb = 1,
		.ofifo_in_wb = 1,
		.ofifo_out_wb = 1,
		.decompress = 0,
		.dsc = 0,
		.data_mode = 1,
	},
	{
		.name = "Out_ctrl4",
		.id = HW_DISPLAY_4,
		.color_formats = DRM_COLOR_FORMAT_RGB444 | DRM_COLOR_FORMAT_YCBCR422 |
				 DRM_COLOR_FORMAT_YCBCR420,
		.max_width = 7680,
		.max_height = 4320,
		.min_scale = FRAC_16_16(1, 1),
		.max_scale = FRAC_16_16(1, 1),
		.background = 1,
		.bld_oetf = 1,
		.bld_dth = 1,
		.panel_dth = 1,
		.test_pattern = 1,
		.disp_out_wb = 1,
		.decompress = 0,
		.data_mode = 1,
	},
};

static const struct vs_wb_info crtc_wb_info[] = {
	/* DC_REV_0 */
	{
		.name = "Post_write_back0",
		.id = HW_WB_0,
		.num_formats = ARRAY_SIZE(wb_format0),
		.formats = wb_format0,
		.modifiers = wb_modifier,
		.max_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = FRAC_16_16(1, 1),
		.max_scale = FRAC_16_16(1, 1),
		.src_mask = 0xF,
		.program_point = 1,
		.compressed = 1,
		.spliter = 0,
		.wb_stall = 1,
	},
	{
		.name = "Post_write_back1",
		.id = HW_WB_1,
		.num_formats = ARRAY_SIZE(wb_format0),
		.formats = wb_format0,
		.modifiers = wb_modifier,
		.max_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = FRAC_16_16(1, 1),
		.max_scale = FRAC_16_16(1, 1),
		.src_mask = 0xF,
		.program_point = 1,
		.compressed = 1,
		.spliter = 0,
		.wb_stall = 1,
	},
	{
		.name = "Blend_write_back",
		.id = HW_BLEND_WB,
		.num_formats = ARRAY_SIZE(wb_format2),
		.formats = wb_format2,
		.modifiers = wb_modifier,
		.max_width = 7680,
		.max_height = 4320,
		.rotation = DRM_MODE_ROTATE_0,
		.min_scale = FRAC_16_16(1, 1),
		.max_scale = FRAC_16_16(1, 1),
		.src_mask = 0x10,
		.program_point = 0,
		.spliter = 0,
		.wb_stall = 0,
	},
};

const struct vs_dc_info dc_info_9400_32a = {
	/* DC_REV_0 */
	.name = "DC9400",
	.chip_id = VS_CHIP_ID_9400,
	.revision = 0x7002,
	.pid = 0x02094000,
	.cid = VS_CHIP_CID_32A,
	.plane_fe0_num = ARRAY_SIZE(plane_fe0_info),
	.plane_fe1_num = ARRAY_SIZE(plane_fe1_info),
	.plane_num = ARRAY_SIZE(plane_fe0_info) + ARRAY_SIZE(plane_fe1_info),
	.planes_fe0 = plane_fe0_info,
	.planes_fe1 = plane_fe1_info,
	.layer_fe0_num = ARRAY_SIZE(plane_fe0_info),
	.layer_fe1_num = ARRAY_SIZE(plane_fe1_info),
	.layer_num = ARRAY_SIZE(plane_fe0_info) + ARRAY_SIZE(plane_fe1_info),
	.display_num = ARRAY_SIZE(crtc_be_info),
	.displays = crtc_be_info,
	.wb_num = ARRAY_SIZE(crtc_wb_info),
	.write_back = crtc_wb_info,
	.max_bpc = 24,
	.pitch_alignment = 64,
	.addr_alignment = 256,
	.fe0_dma_sram_size = 512,
	.fe1_dma_sram_size = 512,
	.fe0_scl_sram_size = 144,
	.fe1_scl_sram_size = 144,
	.max_blend_layer = 14,
	.max_ext_layer = 14,
	.max_seg_num = 10,
	.dump_reg_access_table = &dump_reg_access_table,
	.dump_reg_access_table_secure = &dump_reg_access_table_secure,
	.urgent_cmd_config = &urgent_cmd_config,
	.urgent_vid_config = &urgent_vid_config,
	.max_eotf_size = 129,
	.max_tonemap_size = 33,
	.max_oetf_size = 65,
	.max_degamma_size = 129,
	.cgm_lut_size = 4913,
	.cgm_ex_lut_size = 729,
	.max_gamma_size = 257,
	.pre_eotf_bits = 12,
	.hdr_bits = 24,
	.oetf_bits = 24,
	.bld_cgm_bits = 24,
	.pre_degamma_bits = 10,
	.degamma_bits = 14,
	.cgm_lut_bits = 14,
	.gamma_bits = 14,
	.blur_coef_bits = 6,
	.intr_dest = 0xF,
	.std_color_lut = false,
	.multi_roi = 1,
	.cap_dec = (1 << DRM_FORMAT_MOD_VS_TYPE_PVRIC),
	.roi_y_gap = 0,
	.vrr = 0,
	.dma_sram_alignment = 1024,
	.dma_sram_extra_buffer = 1,
	.dma_sram_unit_size = SRAM_UNIT_SIZE_64KB,
	.linear_yuv_rotation = 1,
	.ppc = 2,
	.vblank_margin_pct = 10,
	.h_bubble_pct = 5,
	.rotation_prefetch_line = 32,
	.rotation_pipeline_delay_us = 50,
	.rotation_pipeline_latency_us = 160,
	.rotation_nominal_cycle_num = 32000,
	.rotation_nominal_cycle_denom = 1440,
	.axi_bus_bit_width = 256,
	.axi_bus_util_pct = 70,
	.max_nv12_uncomp_rot_width = 1920,
	.max_nv12_uncomp_rot_height = 1080,
	.fe_axqos_threshold_mbps = 5401,
	.fe_axqos_high = 0x03030303,
	.fe_axqos_low = 0,
	.default_rd_peak_bw_margin_pct = 22,
};
