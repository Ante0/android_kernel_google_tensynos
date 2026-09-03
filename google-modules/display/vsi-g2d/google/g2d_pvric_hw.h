/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#ifndef G2D_PVRIC_HW_H_
#define G2D_PVRIC_HW_H_

#include <linux/types.h>

enum pvric_hw_format {
	PVRIC_FORMAT_U8 = 0x00,
	PVRIC_FORMAT_RGB565 = 0x05,
	PVRIC_FORMAT_U8U8U8U8 = 0x0C,
	PVRIC_FORMAT_ARGB2101010 = 0x0E,
	PVRIC_FORMAT_FP16 = 0x1C,
	PVRIC_FORMAT_A8 = 0x28,
	PVRIC_FORMAT_ABGR8888 = 0x29,
	PVRIC_FORMAT_YUV420_2PLANE = 0x36,
	PVRIC_FORMAT_YVU420_2PLANE = 0x37,
	PVRIC_FORMAT_YUV420_BIT10_PACK16 = 0x65,
};

enum pvric_hw_tile {
	PVRIC_TILE_RESERVED = 0x00,
	PVRIC_TILE_8X8 = 0x01,
	PVRIC_TILE_16X4 = 0x02,
	PVRIC_TILE_32X2 = 0x03,
};

enum pvric_hw_swizzle {
	PVRIC_SWIZZLE_ARGB = 0x00,
	PVRIC_SWIZZLE_ARBG = 0x01,
	PVRIC_SWIZZLE_AGRB = 0x02,
	PVRIC_SWIZZLE_AGBR = 0x03,
	PVRIC_SWIZZLE_ABGR = 0x04,
	PVRIC_SWIZZLE_ABRG = 0x05,
	PVRIC_SWIZZLE_RGBA = 0x08,
	PVRIC_SWIZZLE_RBGA = 0x09,
	PVRIC_SWIZZLE_GRBA = 0x0A,
	PVRIC_SWIZZLE_GBRA = 0x0B,
	PVRIC_SWIZZLE_BGRA = 0x0C,
	PVRIC_SWIZZLE_BRGA = 0x0D,
};

struct pvric_hw_config {
	u8 format;
	u8 num_planes;
	u8 swizzle;
	/*
	 * Num of color planes, analogous to drm_format_info.num_planes
	 * RGB = 1, YUV semi-planar = 2. The PVRIC decoder supports 2 max.
	 */
	u8 tile_mode;
	bool lossy;
	bool enable;
};

struct pvric_hw_thresholds {
	u32 argb10;
	u32 alpha;
	u32 yuv8;
	u32 yuv10_p10;
	u32 yuv10_p16;
	u32 color_diff8;
	u32 color_diff10;
};

struct pvric_hw_const_color {
	u32 ch0123_0;
	u32 ch0123_1;
	u32 y_0;
	u32 uv_0;
	u32 y_1;
	u32 uv_1;
};

struct sc_hw;
void pvric_hw_update_plane(struct sc_hw *hw, u8 id, struct pvric_hw_config *pvric_conf);
void pvric_hw_update_wb(struct sc_hw *hw, u8 id, struct pvric_hw_config *pvric_conf);
void pvric_hw_plane_commit(struct sc_hw *hw, u8 id);
void pvric_hw_wb_commit(struct sc_hw *h, u8 id);
void pvric_hw_configure_thresholds(struct sc_hw *hw, u8 id,
				   struct pvric_hw_thresholds *pvric_hw_thresholds);
void pvric_hw_configure_const_color(struct sc_hw *hw, u8 id,
				    struct pvric_hw_const_color *pvric_hw_const_color,
				    bool decoder);

#endif /* G2D_PVRIC_HW_H_ */
