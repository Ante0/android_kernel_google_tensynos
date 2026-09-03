/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef _VS_DC_PVRIC_H_
#define _VS_DC_PVRIC_H_

#include <drm/drm_framebuffer.h>
#include <drm/vs_drm_fourcc.h>
#include <drm/vs_drm.h>

#include "vs_dc_hw.h"

#define fourcc_mod_vs_get_type(val) (((val) & DRM_FORMAT_MOD_VS_TYPE_MASK) >> 53)
#define fourcc_mod_vs_get_tile_mode(val) ((u8)((val) & DRM_FORMAT_MOD_VS_DEC_TILE_MODE_MASK))

#define PVRIC_PLANE_MAX 3
#define PVRIC_BLOCK_HEIGHT 8

enum dc_pvric_format {
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

enum dc_pvric_tile {
	PVRIC_TILE_RESERVED = 0x00,
	PVRIC_TILE_8X8 = 0x01,
	PVRIC_TILE_16X4 = 0x02,
	PVRIC_TILE_32X2 = 0x03,
};

enum dc_pvric_swizzle {
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

enum dc_pvric_type {
	PVRIC_PLANE = 0x00,
	PVRIC_DISP_BRT,
	PVRIC_DISP_BLIUR,
	PVRIC_DISP_RCD,
};

struct dc_pvric_reqt {
	u8 format;
	u8 swizzle;
	u8 tile_mode;
	u8 req_num;
	bool enable;
	bool lossy;
	bool dirty;
};

struct dc_pvric {
	struct dc_pvric_reqt reqt;
};

int dc_pvric_reqt_config(struct dc_pvric_reqt *reqt, struct drm_framebuffer *drm_fb);
int dc_pvric_commit(struct dc_pvric *pvric, struct dc_hw *hw, u8 pvric_type, u8 id);
uint8_t get_fb_modifier_norm_mode(const struct drm_framebuffer *drm_fb);

#endif /* _VS_DC_PVRIC_H_ */
