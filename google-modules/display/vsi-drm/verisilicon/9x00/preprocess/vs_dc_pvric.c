// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/errno.h>
#include <linux/types.h>

#include <drm/vs_drm_fourcc.h>

#include "preprocess/vs_dc_pvric.h"
#include "vs_dc_reg_fe0.h"
#include "vs_dc_reg_fe1.h"
#include "vs_fb.h"
#include "vs_trace.h"

static void dc_hw_set_plane_fbc_enable(struct dc_hw *hw, u8 id, bool enable)
{
	u8 hw_id = 0;
	u32 config = 0;

	hw_id = dc_hw_get_plane_id(id, hw);

	trace_config_hw_layer_feature_en("PVRIC", hw_id, enable);

	config = dc_read(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, COMPRESS_Address));
	config = VS_SET_FIELD(config, DCREG_SH_LAYER0_COMPRESS, VALUE, enable);

	dc_write(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, COMPRESS_Address), config);

	/* Invalidate PVRIC header cache to avoid corruption - see b/362350529 for details */
	if (enable) {
		config = VS_SET_FIELD(0, DCREG_SH_LAYER0_PVRIC_INVALIDATION_CONTROL, NOTIFY, 1);
		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_PVRIC_INVALIDATION_CONTROL,
				      INVALIDATE_ALL, 1);
		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_PVRIC_INVALIDATION_CONTROL,
				      INVALIDATE_OVERRIDE, 1);
		dc_write(hw,
			 VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id, PVRIC_INVALIDATION_CONTROL_Address),
			 config);
	}
}

static void dc_hw_set_plane_fbc_req(struct dc_hw *hw, u8 id, u8 num, u8 format, u8 swizzle, u8 tile,
				    bool lossy)
{
	u8 i, hw_id = 0;
	u32 config = 0, base_offset = 0x04;

	hw_id = dc_hw_get_plane_id(id, hw);

	trace_config_hw_layer_feature("PVRIC", hw_id,
				      "num:%d lossy:%d format:%d tile:%d swizzle:%d", num, lossy,
				      format, tile, swizzle);

	for (i = 0; i < num; i++) {
		config = dc_read(hw, VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id,
						     PVRIC_REQUESTER_CONTROL_REQT0_Address) +
					     i * base_offset);

		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_PVRIC_REQUESTER_CONTROL_REQT0,
				      ENABLE_LOSSY, lossy);
		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_PVRIC_REQUESTER_CONTROL_REQT0, FORMAT,
				      format);
		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_PVRIC_REQUESTER_CONTROL_REQT0, TILE,
				      tile);
		config = VS_SET_FIELD(config, DCREG_SH_LAYER0_PVRIC_REQUESTER_CONTROL_REQT0,
				      SWIZZLE, swizzle);

		dc_write(hw,
			 VS_SET_FE_FIELD(DCREG_SH_LAYER, hw_id,
					 PVRIC_REQUESTER_CONTROL_REQT0_Address) +
				 i * base_offset,
			 config);
	}
}

static inline void update_pvric_format(struct dc_pvric_reqt *reqt, u32 format)
{
	u8 f = PVRIC_FORMAT_U8U8U8U8;

	switch (format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		f = PVRIC_FORMAT_RGB565;
		break;
	// Note: The Imagination GPU always uses U8U8U8U8 to represent ARGB8888 and
	// and its various swizzles. See b/303492207 for more information.
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_RGBX8888:
		f = PVRIC_FORMAT_U8U8U8U8;
		break;
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_BGRA1010102:
		f = PVRIC_FORMAT_ARGB2101010;
		break;
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_ABGR16161616F:
		f = PVRIC_FORMAT_FP16;
		break;
	case DRM_FORMAT_NV12:
		f = PVRIC_FORMAT_YVU420_2PLANE;
		break;
	case DRM_FORMAT_NV21:
		f = PVRIC_FORMAT_YUV420_2PLANE;
		break;
	case DRM_FORMAT_P010:
		f = PVRIC_FORMAT_YUV420_BIT10_PACK16;
		break;
	default:
		WARN(1, "%s: Unknown DRM format %p4cc! Defaulting to PVRIC_FORMAT_U8U8U8U8",
		     __func__, &format);
		break;
	}

	reqt->format = f;
}

static inline void update_pvric_swizzle(struct dc_pvric_reqt *pvric, u32 format)
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

	pvric->swizzle = swizzle;
}

static inline void update_pvric_tile(struct dc_pvric_reqt *pvric, u8 tile)
{
	u8 tile_mode = PVRIC_TILE_RESERVED;

	switch (tile) {
	case DRM_FORMAT_MOD_VS_DEC_TILE_8X8:
		tile_mode = PVRIC_TILE_8X8;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_16X4:
		tile_mode = PVRIC_TILE_16X4;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_32X2:
		tile_mode = PVRIC_TILE_32X2;
		break;
	default:
		WARN(1, "%s: Unknown tile:%#x! Defaulting to tile_mode to PVRIC_TILE_RESERVED",
		     __func__, tile);
		break;
	}

	pvric->tile_mode = tile_mode;
}

uint8_t get_fb_modifier_norm_mode(const struct drm_framebuffer *drm_fb)
{
	u32 format = drm_fb->format->format;
	uint64_t vs_modifier = vs_fb_parse_fourcc_modifier(drm_fb->modifier);
	u8 tile_mode = fourcc_mod_vs_get_tile_mode(vs_modifier);
	u8 norm_mode = DRM_FORMAT_MOD_VS_LINEAR;

	if (fourcc_mod_vs_get_type(vs_modifier) != DRM_FORMAT_MOD_VS_TYPE_PVRIC)
		return vs_modifier;

	switch (tile_mode) {
	case DRM_FORMAT_MOD_VS_DEC_TILE_8X8:
		switch (format) {
		case DRM_FORMAT_NV12:
		case DRM_FORMAT_NV21:
			norm_mode = DRM_FORMAT_MOD_VS_TILE_32X8;
			break;
		case DRM_FORMAT_P010:
			norm_mode = DRM_FORMAT_MOD_VS_TILE_16X8;
			break;
		default:
			break;
		}
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_16X4:
		switch (format) {
		case DRM_FORMAT_ARGB8888:
		case DRM_FORMAT_ABGR8888:
		case DRM_FORMAT_RGBA8888:
		case DRM_FORMAT_BGRA8888:
		case DRM_FORMAT_XRGB8888:
		case DRM_FORMAT_XBGR8888:
		case DRM_FORMAT_RGBX8888:
		case DRM_FORMAT_BGRX8888:
		case DRM_FORMAT_ARGB2101010:
		case DRM_FORMAT_ABGR2101010:
		case DRM_FORMAT_RGBA1010102:
		case DRM_FORMAT_BGRA1010102:
			norm_mode = DRM_FORMAT_MOD_VS_TILE_16X4;
			break;
		case DRM_FORMAT_RGB565:
		case DRM_FORMAT_BGR565:
			norm_mode = DRM_FORMAT_MOD_VS_TILE_32X4;
			break;
		default:
			break;
		}
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_32X2:
		switch (format) {
		case DRM_FORMAT_ARGB16161616F:
		case DRM_FORMAT_ABGR16161616F:
			norm_mode = DRM_FORMAT_MOD_VS_TILE_32X2;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return fourcc_mod_vs_norm_code(norm_mode) & DRM_FORMAT_MOD_VS_NORM_MODE_MASK;
}

static void _pvric_reqt_config(struct dc_pvric_reqt *reqt, struct drm_framebuffer *drm_fb)
{
	const struct drm_format_info *info = drm_fb->format;
	uint64_t vs_modifier;
	u8 tile_mode;

	reqt->req_num = info->num_planes;
	vs_modifier = vs_fb_parse_fourcc_modifier(drm_fb->modifier);
	tile_mode = fourcc_mod_vs_get_tile_mode(vs_modifier);

	update_pvric_format(reqt, drm_fb->format->format);
	if (!drm_fb->format->is_yuv)
		update_pvric_swizzle(reqt, drm_fb->format->format);
	update_pvric_tile(reqt, tile_mode);

	reqt->lossy = (vs_modifier & DRM_FORMAT_MOD_VS_DEC_LOSSY) != 0;
	reqt->dirty = true;
}

static void plane_pvric_commit(struct dc_pvric *pvric, struct dc_hw *hw, u8 id)
{
	if (pvric->reqt.dirty) {
		dc_hw_set_plane_fbc_enable(hw, id, pvric->reqt.enable);

		if (pvric->reqt.enable)
			dc_hw_set_plane_fbc_req(hw, id, pvric->reqt.req_num, pvric->reqt.format,
						pvric->reqt.swizzle, pvric->reqt.tile_mode,
						pvric->reqt.lossy);

		pvric->reqt.dirty = false;
	}
}

int dc_pvric_reqt_config(struct dc_pvric_reqt *reqt, struct drm_framebuffer *drm_fb)
{
	if (!reqt->enable) {
		reqt->dirty = true;
		return 0;
	}

	if (!drm_fb || fourcc_mod_vs_get_type(vs_fb_parse_fourcc_modifier(drm_fb->modifier)) !=
			       DRM_FORMAT_MOD_VS_TYPE_PVRIC)
		return -EINVAL;

	_pvric_reqt_config(reqt, drm_fb);

	return 0;
}

int dc_pvric_commit(struct dc_pvric *pvric, struct dc_hw *hw, u8 pvric_type, u8 id)
{
	if (pvric_type == PVRIC_PLANE)
		plane_pvric_commit(pvric, hw, id);

	/* TODO: display brightness, blur, rcd compression data processing.
	 * This part will be supported in parallel with the development of
	 * the corresponding modules.
	 */

	return 0;
}
