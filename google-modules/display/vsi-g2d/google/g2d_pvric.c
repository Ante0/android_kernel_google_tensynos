// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */
#include <linux/printk.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/g2d_drm_fourcc.h>

#include "g2d_pvric.h"
#include "g2d_pvric_hw.h"

bool is_buffer_pvric(u64 modifier)
{
	switch (modifier) {
	case DRM_FORMAT_MOD_PVR_FBCDC_16x4_V14:
	case DRM_FORMAT_MOD_PVR_FBCDC_8x8_V14:
		return true;
	default:
		return false;
	}
}

static bool pvric_is_lossy(u64 modifier)
{
	switch (modifier) {
	case DRM_FORMAT_MOD_PVR_FBCDC_16x4_V14:
	case DRM_FORMAT_MOD_PVR_FBCDC_8x8_V14:
		return false;
	default:
		return false;
	}
}

static u8 pvric_get_swizzle(u32 format)
{
	u8 swizzle = PVRIC_SWIZZLE_ARGB;

	switch (format) {
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_ABGR16161616F:
		swizzle = PVRIC_SWIZZLE_ABGR;
		break;
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGBA1010102:
		swizzle = PVRIC_SWIZZLE_RGBA;
		break;
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_BGRA1010102:
		swizzle = PVRIC_SWIZZLE_BGRA;
		break;
	default:
		break;
	}

	return swizzle;
}

static u8 pvric_get_tile(u64 modifier)
{
	u8 tile_mode = PVRIC_TILE_RESERVED;

	switch (modifier) {
	case DRM_FORMAT_MOD_PVR_FBCDC_8x8_V14:
		tile_mode = PVRIC_TILE_8X8;
		break;
	case DRM_FORMAT_MOD_PVR_FBCDC_16x4_V14:
		tile_mode = PVRIC_TILE_16X4;
		break;
	default:
		WARN(1, "Unknown modifier:%#llx! Defaulting to tile_mode to PVRIC_TILE_RESERVED",
		     modifier);
		break;
	}

	return tile_mode;
}

static u8 pvric_get_hw_format(u32 format)
{
	u8 hw_format = PVRIC_FORMAT_U8U8U8U8;

	switch (format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		hw_format = PVRIC_FORMAT_RGB565;
		break;
	/*
	 * Note: The Imagination GPU always uses U8U8U8U8 to represent ARGB8888
	 * and its various swizzles. See b/303492207 for more information.
	 */
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_RGBX8888:
		hw_format = PVRIC_FORMAT_U8U8U8U8;
		break;
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_BGRA1010102:
		hw_format = PVRIC_FORMAT_ARGB2101010;
		break;
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_ABGR16161616F:
		hw_format = PVRIC_FORMAT_FP16;
		break;
	case DRM_FORMAT_NV12:
		hw_format = PVRIC_FORMAT_YVU420_2PLANE;
		break;
	case DRM_FORMAT_NV21:
		hw_format = PVRIC_FORMAT_YUV420_2PLANE;
		break;
	case DRM_FORMAT_P010:
		hw_format = PVRIC_FORMAT_YUV420_BIT10_PACK16;
		break;
	default:
		WARN(1, "Unknown DRM format %p4cc! Defaulting to PVRIC_FORMAT_U8U8U8U8", &format);
		break;
	}
	return hw_format;
}

void pvric_populate_hw_config(const struct drm_format_info *info, const struct drm_framebuffer *fb,
			      struct pvric_hw_config *pvric)
{
	pvric->enable = is_buffer_pvric(fb->modifier);

	if (pvric->enable) {
		pvric->format = pvric_get_hw_format(info->format);
		pvric->num_planes = info->num_planes;
		pvric->swizzle = pvric_get_swizzle(info->format);
		pvric->tile_mode = pvric_get_tile(fb->modifier);
		pvric->lossy = pvric_is_lossy(fb->modifier);
	}
}
