// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/media-bus-format.h>
#include <linux/of_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_plane.h>
#include <drm/drm_of.h>
#include <drm/vs_drm.h>

#include <trace/dpu_trace.h>

#include "vs_dc.h"
#include "vs_dc_pre.h"
#include "vs_dc_hw.h"
#include "vs_drv.h"
#include "vs_plane.h"
#include "vs_dc_info.h"
#include "vs_gem.h"
#include "display_compress/vs_dc_dsc.h"
#include "vs_trace.h"
#include "vs_trace_9x00.h"

#include <drm/vs_drm_fourcc.h>
#include <drm/drm_vblank.h>

static int dc_get_fe_idx(const struct device *dev)
{
	struct vs_dc *dc = dev_get_drvdata(dev->parent);
	int i;

	if (!dc)
		return -ENODEV;

	for (i = 0; i < DC_FE_NUM; i++) {
		if (dc->fe_dev[i] == dev)
			return i;
	}

	return -ENODEV;
}

static inline u8 to_vs_format(u32 format, u64 mod)
{
	u8 f = FORMAT_A8R8G8B8;

	switch (format) {
	case DRM_FORMAT_XRGB4444:
	case DRM_FORMAT_RGBX4444:
	case DRM_FORMAT_XBGR4444:
	case DRM_FORMAT_BGRX4444:
		f = FORMAT_X4R4G4B4;
		break;
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_BGRA4444:
		f = FORMAT_A4R4G4B4;
		break;
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_RGBX5551:
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_BGRX5551:
		f = FORMAT_X1R5G5B5;
		break;
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_BGRA5551:
		f = FORMAT_A1R5G5B5;
		break;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		f = FORMAT_R5G6B5;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_BGRX8888:
		f = FORMAT_X8R8G8B8;
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_BGRA8888:
		f = FORMAT_A8R8G8B8;
		break;
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_BGRA1010102:
		f = FORMAT_A2R10G10B10;
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_RGBX1010102:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_BGRX1010102:
		f = FORMAT_X2R10G10B10;
		break;
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_ABGR16161616F:
		f = FORMAT_A16R16G16B16;
		break;
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
		f = FORMAT_YV12;
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		f = FORMAT_NV12;
		break;
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
		f = FORMAT_NV16;
		break;
	case DRM_FORMAT_P010:
		f = FORMAT_P010;
		break;
	case DRM_FORMAT_P210:
		f = FORMAT_P210;
		break;
	case DRM_FORMAT_YUV420_10BIT:
		if (fourcc_mod_vs_get_type(mod) == DRM_FORMAT_MOD_VS_TYPE_CUSTOM)
			f = FORMAT_YUV420_PACKED_10BIT;
		break;
	default:
		break;
	}

	return f;
}

static inline void update_swizzle(u32 format, struct dc_hw_fb *fb)
{
	fb->swizzle = SWIZZLE_ARGB;
	fb->uv_swizzle = 0;

	switch (format) {
	case DRM_FORMAT_RGBX4444:
	case DRM_FORMAT_RGBA4444:
	case DRM_FORMAT_RGBX5551:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_RGBX1010102:
		fb->swizzle = SWIZZLE_RGBA;
		break;
	case DRM_FORMAT_XBGR4444:
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR16161616F:
		fb->swizzle = SWIZZLE_ABGR;
		break;
	case DRM_FORMAT_BGRX4444:
	case DRM_FORMAT_BGRA4444:
	case DRM_FORMAT_BGRX5551:
	case DRM_FORMAT_BGRA5551:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_BGRA1010102:
	case DRM_FORMAT_BGRX1010102:
		fb->swizzle = SWIZZLE_BGRA;
		break;
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_YVU420:
		fb->uv_swizzle = 1;
		break;
	default:
		break;
	}
}

static inline u8 to_vs_tile_mode(const struct drm_framebuffer *fb)
{
	u8 norm_mode, tile = TILE_MODE_LINEAR;

	norm_mode = get_fb_modifier_norm_mode(fb);

	switch (norm_mode) {
	case DRM_FORMAT_MOD_VS_TILE_16X4:
		tile = TILE_MODE_16X4;
		break;
	case DRM_FORMAT_MOD_VS_TILE_16X8:
		tile = TILE_MODE_16X8;
		break;
	case DRM_FORMAT_MOD_VS_TILE_32X2:
		tile = TILE_MODE_32X2;
		break;
	case DRM_FORMAT_MOD_VS_TILE_32X4:
		tile = TILE_MODE_32X4;
		break;
	case DRM_FORMAT_MOD_VS_TILE_32X8:
		tile = TILE_MODE_32X8;
		break;
	case DRM_FORMAT_MOD_VS_TILE_16X16:
		tile = TILE_MODE_16X16;
		break;
	case DRM_FORMAT_MOD_VS_TILE_8X8_UNIT2X2:
		tile = TILE_MODE_8X8_UNIT2X2;
		break;
	case DRM_FORMAT_MOD_VS_TILE_8X4_UNIT2X2:
		tile = TILE_MODE_8X4_UNIT2X2;
		break;
	default:
		break;
	}

	return tile;
}

bool vs_dc_is_yuv_format(u32 format)
{
	bool is_yuv = false;

	switch (format) {
	case FORMAT_YV12:
	case FORMAT_NV12:
	case FORMAT_NV16:
	case FORMAT_P010:
	case FORMAT_P210:
	case FORMAT_YUV420_PACKED_10BIT:
		is_yuv = true;
		break;
	default:
		break;
	}

	return is_yuv;
}

static inline u8 to_vs_display_id(struct vs_dc *dc, struct drm_crtc *crtc)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	const struct vs_display_info *display_info;
	const struct vs_dc_info *dc_info = dc->hw.info;
	u8 id = 0;

	display_info = &dc_info->displays[vs_crtc->id];
	id = display_info->id;

	return id;
}

static inline u16 get_dma_image_width(const struct drm_vs_dma *dma, struct dc_hw_fb *fb)
{
	u16 width = 0, height = 0;

	if (dma) {
		switch (dma->mode) {
		case VS_DMA_NORMAL:
		case VS_DMA_ONE_ROI:
			pr_err("%s Invalid drm_vs_dma_mode set: %d.\n", __func__, dma->mode);
			break;
		default:
			width = fb->width;
			height = fb->height;
			break;
		}
	} else {
		width = fb->width;
		height = fb->height;
	}

	return (fb->rotation == ROT_90 || fb->rotation == ROT_270) ? height : width;
}

static void vs_dc_update_sram(struct vs_dc *dc, struct vs_plane *plane, struct dc_hw_fb *fb,
			      struct dc_hw_plane *hw_plane, const struct dc_hw_scale *scale,
			      const struct drm_vs_dma *dma)
{
	struct device *dev = dc->hw.dev;
	u32 sp_size = 0;
	u16 width = 0;
	int32_t ret = 0;
	bool sp_dirty = false;
	bool dma_sram_alloc = false;
	bool scaler_sram_alloc = false;
	bool scale_enable = false;
	int32_t realloc = 0;
	struct dc_hw_sram_pool sram_pool = { 0 };
	u32 hw_id = hw_plane->info->id;
	u16 sp_alignment = dc->hw.info->dma_sram_alignment;
	bool sp_extra_buffer = dc->hw.info->dma_sram_extra_buffer;
	plane->sram.sp_unit_size = dc->hw.info->dma_sram_unit_size;

	/* cursor layer doesn't use sram pool */
	if (hw_plane->info->type == DRM_PLANE_TYPE_CURSOR)
		return;

	if (scale)
		scale_enable = scale->enable;

	if (fb->enable) {
		/* Manage SRAM allocation for DMA */
		enum vs_dpu_sram_pool_type type = (hw_id < 8) ? VS_DPU_SPOOL_FE0_DMA :
								VS_DPU_SPOOL_FE1_DMA;

		/* b/451837126: pass (NULL, fb) to force using full FB dimensions */
		width = get_dma_image_width(NULL, fb);

		vs_dpu_get_dma_sram_size(fb->format, fb->tile_mode, fb->rotation, width,
					 &sp_size, sp_alignment, sp_extra_buffer,
					 plane->sram.sp_unit_size);
		if (plane->sram.sp_handle && plane->sram.sp_size != sp_size) {
			realloc = 1;
			dma_sram_alloc = true;
		} else if (!plane->sram.sp_handle) {
			realloc = 0;
			dma_sram_alloc = true;
		}

		if (dma_sram_alloc) {
			ret = vs_dpu_sram_alloc(type, sp_size, &plane->sram.sp_handle, 0, realloc,
						plane->id);
			if (ret) {
				dev_err(dev,
					"request DMA SRAM failed. err:%d plane:%d pool:%d realloc:%d old_size:%uK requested_size:%uK format:%d tile:%d rot:%d width:%d alignment:%d",
					ret, plane->id, type, realloc, plane->sram.sp_size >> 10,
					sp_size >> 10, fb->format, fb->tile_mode, fb->rotation,
					fb->width, sp_alignment);
				vs_dpu_sram_pool_dump_usage(dev, type);
			}
			plane->sram.sp_size = sp_size;
			sp_dirty = true;
		}

		/* Manage SRAM allocation for Scaling */
		type = (hw_id < 8) ? VS_DPU_SPOOL_FE0_SCL : VS_DPU_SPOOL_FE1_SCL;
		if (scale_enable) {
			vs_dpu_get_fescl_sram_size(fb->format,
						   min(scale->src_w >> 16, scale->dst_w), &sp_size);
			if (plane->sram.scl_sp_handle && plane->sram.scl_sp_size != sp_size) {
				realloc = 1;
				scaler_sram_alloc = true;
			} else if (!plane->sram.scl_sp_handle) {
				realloc = 0;
				scaler_sram_alloc = true;
			}

			if (scaler_sram_alloc) {
				ret = vs_dpu_sram_alloc(type, sp_size, &plane->sram.scl_sp_handle,
							0, realloc, plane->id);
				if (ret) {
					dev_err(dev,
						"request Scaler SRAM failed. err:%d plane:%d pool:%d realloc:%d old_size:%uK requested_size:%uK format:%d rot:%d src_w:%u dst_w:%u alignment:%d",
						ret, plane->id, type, realloc,
						plane->sram.scl_sp_size >> 10, sp_size >> 10,
						fb->format, fb->rotation, scale->src_w >> 16,
						scale->dst_w, sp_alignment);
					vs_dpu_sram_pool_dump_usage(dev, type);
				}
				plane->sram.scl_sp_size = sp_size;
				sp_dirty = true;
			}
		} else {
			if (plane->sram.scl_sp_handle) {
				ret = vs_dpu_sram_free(type, plane->sram.scl_sp_handle, plane->id);
				plane->sram.scl_sp_handle = 0;
				plane->sram.scl_sp_size = 0;
				sp_dirty = true;
			}
		}
	} else {
		enum vs_dpu_sram_pool_type type = (hw_id < 8) ? VS_DPU_SPOOL_FE0_DMA :
								VS_DPU_SPOOL_FE1_DMA;

		if (plane->sram.sp_handle) {
			ret = vs_dpu_sram_free(type, plane->sram.sp_handle, plane->id);
			plane->sram.sp_handle = 0;
			plane->sram.sp_size = 0;
			sp_dirty = true;
		}

		type = (hw_id < 8) ? VS_DPU_SPOOL_FE0_SCL : VS_DPU_SPOOL_FE1_SCL;
		if (plane->sram.scl_sp_handle) {
			ret = vs_dpu_sram_free(type, plane->sram.scl_sp_handle, plane->id);
			plane->sram.scl_sp_handle = 0;
			plane->sram.scl_sp_size = 0;
			sp_dirty = true;
		}
	}

	if (sp_dirty) {
		memcpy(&sram_pool, &plane->sram, sizeof(struct vs_plane_sram_pool));
		dc_hw_update_plane_sram(&dc->hw, plane->id, &sram_pool);
	}
}

static void update_plane_fb(struct vs_plane *plane, u8 display_id, struct dc_hw_fb *fb,
			    struct vs_plane_info *plane_info)
{
	struct drm_plane_state *state = plane->base.state;
	struct vs_plane_state *vs_plane_state = to_vs_plane_state(state);
	struct drm_framebuffer *drm_fb = state->fb;
	struct device *dev = plane->dev;
	const void *prop;
	bool is_secure = false;

	if (plane_info->sid != HW_PLANE_NOT_SUPPORTED_SID) {
		prop = vs_dc_drm_plane_property_get(vs_plane_state, "SECURE_BUFFER", NULL);
		if (prop)
			is_secure = *((const bool *)prop);
		else
			dev_err(dev, "%s: Failed to get SECURE_BUFFER property!\n", __func__);
	}

	/* TBD !
	 * developer should update the function implementation
	 * according to actual requirements during developing !
	 */

	fb->display_id = display_id;
	fb->address = (u64)plane->dma_addr[0];
	fb->stride = drm_fb->pitches[0];
	fb->u_address = (u64)plane->dma_addr[1];
	fb->v_address = (u64)plane->dma_addr[2];
	fb->u_stride = drm_fb->pitches[1];
	fb->v_stride = drm_fb->pitches[2];
	fb->width = drm_fb->width;
	fb->height = drm_fb->height;
	fb->rotation = to_vs_rotation(state->rotation);
	fb->zpos = vs_plane_state->blend_id;
	fb->enable = state->visible;
	fb->secure = is_secure;
	fb->format = to_vs_format(drm_fb->format->format, drm_fb->modifier);
	update_swizzle(drm_fb->format->format, fb);
	fb->tile_mode = to_vs_tile_mode(drm_fb);

	vs_plane_state->status.tile_mode = fb->tile_mode;
}

static void update_rcd_plane(struct vs_dc *dc, struct vs_plane *plane, u8 display_id)
{
	struct drm_plane_state *state = plane->base.state;
	struct drm_framebuffer *drm_fb = state->fb;
	struct dc_hw_rcd_mask rcd_mask = { 0 };

	rcd_mask.address = (u64)vs_fb_get_dma_addr(drm_fb, 0);
	rcd_mask.stride = drm_fb->pitches[0];
	rcd_mask.display_id = display_id;
	rcd_mask.type = VS_MASK_BLD_NORM;
	rcd_mask.roi_data.top_enable = !!rcd_mask.address;

	rcd_mask.roi_data.top_roi.x = state->dst.x1;
	rcd_mask.roi_data.top_roi.y = state->dst.y1;
	rcd_mask.roi_data.top_roi.w = drm_rect_width(&state->dst);
	rcd_mask.roi_data.top_roi.h = drm_rect_height(&state->dst);

	dc_hw_update_plane_rcd_mask(&dc->hw, plane->id, &rcd_mask);
}

static void update_plane_y2r(struct vs_dc *dc, u8 id, struct vs_plane_state *vs_plane_state)
{
	struct dc_hw_y2r y2r_conf = { 0 };
	struct drm_vs_y2r_config *data;
	struct drm_plane_state *plane_state = &vs_plane_state->base;

	if (!test_bit(VS_PLANE_CHANGED_Y2R, vs_plane_state->changed))
		return;

	y2r_conf.enable = plane_state->fb->format->is_yuv;
	if (vs_plane_state->y2r_coef) {
		data = vs_plane_state->y2r_coef->data;
		y2r_conf.mode = CSC_MODE_USER_DEF;
		memcpy(y2r_conf.coef, data->coef, sizeof(data->coef));
	} else {
		y2r_conf.gamut = to_vs_yuv_gamut(plane_state->color_encoding);

		if (plane_state->color_range == DRM_COLOR_YCBCR_FULL_RANGE)
			y2r_conf.mode = CSC_MODE_F2F;
		else
			y2r_conf.mode = CSC_MODE_L2F;
	}

	dc_hw_update_plane_y2r(&dc->hw, id, &y2r_conf);
}

static int populate_layer_scale(const struct drm_plane_state *plane_state,
				struct dc_hw_scale *scale)
{
	if (!plane_state || !scale)
		return -EINVAL;

	scale->src_w = drm_rect_width(&plane_state->src);
	scale->src_h = drm_rect_height(&plane_state->src);
	scale->dst_w = drm_rect_width(&plane_state->dst);
	scale->dst_h = drm_rect_height(&plane_state->dst);

	if (plane_state->rotation & (DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270))
		swap(scale->src_w, scale->src_h);

	// Since src_w/h are in 16.16 and crtc_w/h are plain ints, this division will
	// result in 16.16 output. Note that subtracting 1 is a HW constraint for no-stretch mode
	scale->factor_x = (scale->src_w - (1 << 16)) / (scale->dst_w - 1);
	scale->factor_y = (scale->src_h - (1 << 16)) / (scale->dst_h - 1);

	scale->enable =
		(scale->factor_x != VS_PLANE_NO_SCALING || scale->factor_y != VS_PLANE_NO_SCALING);

	return 0;
}

static void update_scale(struct vs_dc *dc, u8 id, struct vs_plane_state *vs_plane_state)
{
	struct drm_plane_state *plane_state = &vs_plane_state->base;
	const struct drm_vs_preprocess_scale_config *new_coeff = NULL;
	struct dc_hw_scale scale = { 0 };

	populate_layer_scale(plane_state, &scale);

	if (test_bit(VS_PLANE_CHANGED_SCALING, vs_plane_state->changed))
		scale.dirty = true;

	if (test_bit(VS_PLANE_CHANGED_SCALING_COEFF, vs_plane_state->changed)) {
		scale.coefficients_dirty = true;
		scale.dirty = true;

		new_coeff = vs_dc_drm_plane_property_get(vs_plane_state, "SCALER", NULL);
		if (new_coeff) {
			memcpy(&scale.coefficients, new_coeff,
			       sizeof(struct drm_vs_preprocess_scale_config));
			scale.coefficients_enable = true;
		} else {
			scale.coefficients_enable = false;
		}
	}

	dc_hw_update_plane_scale(&dc->hw, id, &scale);
}

static void update_roi(struct vs_dc *dc, u8 id, struct vs_plane_state *vs_plane_state)
{
	struct device *dev = dc->hw.dev;
	struct drm_plane_state *plane_state = &vs_plane_state->base;
	struct drm_framebuffer *drm_fb = plane_state->fb;
	const struct drm_vs_dma *dma_blob = NULL;
	struct dc_hw_roi roi = { 0 };

	dma_blob = vs_dc_drm_plane_property_get(vs_plane_state, "DMA_CONFIG", NULL);
	if (dma_blob == NULL) {
		roi.mode = (plane_state->src_w >> 16 != drm_fb->width ||
			    plane_state->src_h >> 16 != drm_fb->height) ?
				   VS_DMA_ONE_ROI :
				   VS_DMA_NORMAL;
	} else {
		roi.mode = dma_blob->mode;
		switch (dma_blob->mode) {
		case VS_DMA_SKIP_ROI:
			roi.in_rect[0].x = dma_blob->in_rect.x;
			roi.in_rect[0].y = dma_blob->in_rect.y;
			roi.in_rect[0].w = dma_blob->in_rect.w;
			roi.in_rect[0].h = dma_blob->in_rect.h;
			break;
		case VS_DMA_TWO_ROI:
		case VS_DMA_EXT_LAYER:
		case VS_DMA_EXT_LAYER_EX:
			roi.in_rect[1].x = dma_blob->in_rect.x;
			roi.in_rect[1].y = dma_blob->in_rect.y;
			roi.in_rect[1].w = dma_blob->in_rect.w;
			roi.in_rect[1].h = dma_blob->in_rect.h;
			roi.out_rect[1].x = dma_blob->out_rect.x;
			roi.out_rect[1].y = dma_blob->out_rect.y;
			roi.out_rect[1].w = dma_blob->out_rect.w;
			roi.out_rect[1].h = dma_blob->out_rect.h;
			break;
		default:
			dev_err(dev,
				"%s: Invalid drm_vs_dma_mode: %d configured in DMA_CONFIG blob.",
				__func__, dma_blob->mode);
			return;
		}
	}

	/* Only SKIP mode uses the blob property for region 0 */
	if (!dma_blob || (dma_blob->mode != VS_DMA_SKIP_ROI)) {
		roi.in_rect[0].x = plane_state->src.x1 >> 16;
		roi.in_rect[0].y = plane_state->src.y1 >> 16;
		roi.in_rect[0].w = drm_rect_width(&plane_state->src) >> 16;
		roi.in_rect[0].h = drm_rect_height(&plane_state->src) >> 16;
	}

	roi.out_rect[0].x = plane_state->dst.x1;
	roi.out_rect[0].y = plane_state->dst.y1;
	roi.out_rect[0].w = drm_rect_width(&plane_state->dst);
	roi.out_rect[0].h = drm_rect_height(&plane_state->dst);
	roi.enable = true;

	dc_hw_update_plane_roi(&dc->hw, id, &roi);
}

static void update_plane_clear(struct vs_dc *dc, u8 id, struct vs_plane_state *vs_plane_state)
{
	struct drm_vs_color *color;
	struct dc_hw_clear clear = { 0 };

	if (vs_plane_state->clear) {
		color = vs_plane_state->clear->data;
		clear.color.a = color->a;
		clear.color.r = color->r;
		clear.color.g = color->g;
		clear.color.b = color->b;
		clear.enable = true;
	}

	dc_hw_update_plane_clear(&dc->hw, id, &clear);
}

static void update_plane_3d_lut(struct vs_dc *dc, u8 id, struct vs_plane_state *plane_state,
				const struct vs_dc_info *dc_info)
{
	struct device *dev = dc->hw.dev;
	struct dc_hw_block lut_3d = { 0 };
	struct drm_vs_data_block *dblock;
	u32 lut_entry = 0, i = 0;
	u32 *block_addr = NULL;
	u32 lut_bit = dc_info->cgm_lut_bits;
	u32 max_lut_size = dc_info->cgm_lut_size;

	if (test_bit(VS_PLANE_CHANGED_LUT_3D, plane_state->changed) && plane_state->lut_3d) {
		dblock = plane_state->lut_3d->data;

		if (!dblock || !dblock->size || !dblock->logical) {
			dev_err(dev, "%s: Invalid data block.\n", __func__);
			return;
		}

		lut_3d.enable = true;

		/* This buffer will be free in vs_dc_hw.c/plane_set_3d_lut() */
		lut_3d.vaddr = kzalloc(dblock->size, GFP_KERNEL);
		if (!lut_3d.vaddr)
			return;

		if (copy_from_user(lut_3d.vaddr, u64_to_user_ptr(dblock->logical), dblock->size)) {
			dev_err(dev, "%s: Failed to read data.\n", __func__);
			goto err_free_block;
		}

		block_addr = (u32 *)lut_3d.vaddr;
		lut_entry = *block_addr;
		block_addr++;

		/* check the effectiveness of entry number*/
		if (lut_entry != max_lut_size) {
			dev_err(dev, "%s: Invalid 3D LUT entry count.\n", __func__);
			goto err_free_block;
		}

		/* check the effectiveness of 3D LUT entry */
		for (i = 0; i < 3 * lut_entry; i++) {
			if (*block_addr >> lut_bit) {
				dev_err(dev, "%s: The entry of 3D LUT over valid bit.\n", __func__);
				goto err_free_block;
			}
			block_addr++;
		}

		return dc_hw_update_plane_3d_lut(&dc->hw, id, &lut_3d);

err_free_block:
		kfree(lut_3d.vaddr);
	} else if (test_bit(VS_PLANE_CHANGED_LUT_3D, plane_state->changed) &&
		   !plane_state->lut_3d) {
		lut_3d.enable = false;
		return dc_hw_update_plane_3d_lut(&dc->hw, id, &lut_3d);
	}
}

static void update_plane_position(struct vs_dc *dc, u8 id, struct vs_plane_state *plane_state)
{
	struct dc_hw_position pos = { 0 };
	struct drm_plane_state *state = &plane_state->base;

	pos.rect[0].x = state->crtc_x;
	pos.rect[0].y = state->crtc_y;
	pos.rect[0].w = state->crtc_w;
	pos.rect[0].h = state->crtc_h;

	dc_hw_update_plane_position(&dc->hw, id, &pos);
}

static void update_plane_ext_fb(struct vs_dc *dc, u8 id, struct vs_plane_state *plane_state)
{

	struct dc_hw_fb fb = { 0 };

	if (plane_state->fb_ext) {
		struct drm_framebuffer *drm_fb = plane_state->fb_ext;
		const struct drm_format_info *info = drm_format_info(drm_fb->format->format);
		u32 i = 0;
		dma_addr_t dma_addr[MAX_NUM_PLANES] = { 0 };

		for (i = 0; i < info->num_planes; i++) {
			dma_addr[i] = vs_fb_get_dma_addr(drm_fb, i);
		}

		fb.address = (u64)dma_addr[0];
		fb.stride = drm_fb->pitches[0];

		fb.u_address = (u64)dma_addr[1];
		fb.v_address = (u64)dma_addr[2];
		fb.u_stride = drm_fb->pitches[1];
		fb.v_stride = drm_fb->pitches[2];

		fb.width = drm_fb->width;
		fb.height = drm_fb->height;
		fb.enable = true;
	}

	dc_hw_update_ext_fb(&dc->hw, id, &fb);
}

static void update_plane_blend(struct vs_dc *dc, u8 id, struct vs_plane_state *plane_state,
			       struct vs_plane_info *plane_info)
{
	struct dc_hw_std_bld std_bld = { 0 };
	struct vs_drm_property_state *bld_mode = NULL;
	struct device *dev = dc->hw.dev;

	if (!plane_info->blend_config && !plane_info->blend_mode)
		return;

	bld_mode = vs_dc_get_drm_property_state(dev, plane_state->drm_states,
						VS_DC_MAX_PROPERTY_NUM, "BLEND_MODE");

	if (bld_mode && !bld_mode->is_changed && plane_info->blend_mode) {
		std_bld.alpha = (plane_state->base.alpha >> 6) & VS_BLEND_ALPHA_OPAQUE;
		std_bld.blend_mode = plane_state->base.pixel_blend_mode;

		dc_hw_update_plane_std_bld(&dc->hw, id, &std_bld);
	}
}

static void update_fbc(struct vs_dc *dc, struct vs_plane *plane)
{
	struct drm_plane_state *state = plane->base.state;
	struct drm_framebuffer *drm_fb = state->fb;
	struct dc_pvric_reqt *reqt = &dc->planes[plane->id].pvric.reqt;
	uint64_t vs_fourcc = vs_fb_parse_fourcc_modifier(drm_fb->modifier);
	u8 hw_id = dc_hw_get_plane_id(plane->id, &dc->hw);

	if (!dc->hw.info->cap_dec)
		return;

	if (fourcc_mod_vs_get_type(vs_fourcc) != DRM_FORMAT_MOD_VS_TYPE_PVRIC) {
		if (reqt->enable)
			reqt->enable = false;
		else
			return;
	} else {
		reqt->enable = true;
	}

	dc_pvric_reqt_config(reqt, drm_fb);

	trace_update_hw_layer_feature_en_dirty("PVRIC", hw_id, reqt->enable, reqt->dirty);
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static void vs_dc_set_plane_pattern(struct device *dev, struct vs_plane *plane)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	struct drm_plane_state *state = plane->base.state;
	struct vs_plane_state *plane_state = to_vs_plane_state(state);
	struct vs_plane_info *plane_info;
	struct dc_hw_pattern pattern;
	u16 src_w = drm_rect_width(&state->src) >> 16;
	u16 src_h = drm_rect_height(&state->src) >> 16;
	u8 hw_id;

	plane_info = get_plane_info(plane->id, dc->hw.info);
	if (!plane_info) {
		dev_err(dev, "%s: invalid plane id: %d\n", __func__, plane->id);
		return;
	}

	if (!plane_info->test_pattern) {
		dev_err(dev, "%s: vs_plane[%u] does not support test pattern.\n", __func__,
			plane->id);
		return;
	}

	hw_id = plane_info->id;

	pattern.enable = plane_state->pattern.enable;
	if (!pattern.enable)
		dc_hw_set_plane_pattern(&dc->hw, hw_id, &pattern);

	pattern.mode = plane_state->pattern.mode;
	if (pattern.mode > VS_CURSOR_PATRN) {
		dev_err(dev, "%s: Invalid test pattern mode.\n", __func__);
		return;
	}

	pattern.color = plane_state->pattern.color;
	pattern.rect.x = plane_state->pattern.rect.x;
	pattern.rect.y = plane_state->pattern.rect.y;
	pattern.rect.w = plane_state->pattern.rect.w;
	pattern.rect.h = plane_state->pattern.rect.h;

	if (pattern.mode == VS_CURSOR_PATRN && (pattern.rect.x > src_w || pattern.rect.y > src_h)) {
		dev_err(dev, "%s: Coordinate of cursor out of range.\n", __func__);
		return;
	} else if ((pattern.mode == VS_COLOR_BAR_H || pattern.mode == VS_RMAP_H) &&
		   pattern.rect.h > src_h) {
		dev_err(dev, "%s: Horizontal color pattern h out of range.\n", __func__);
		return;
	} else if ((pattern.mode == VS_COLOR_BAR_V || pattern.mode == VS_RMAP_V) &&
		   pattern.rect.w > src_w) {
		dev_err(dev, "%s: Vertical color pattern w out of range.\n", __func__);
		return;
	} else if ((pattern.mode == VS_BLACK_WHITE_H || pattern.mode == VS_BLACK_WHITE_SQR) &&
		   (pattern.rect.h < 1 || pattern.rect.h > 256)) {
		dev_err(dev, "%s: Horizontal black white pattern out of range.\n", __func__);
		return;
	} else if (pattern.mode == VS_BLACK_WHITE_V &&
		   (pattern.rect.w < 1 || pattern.rect.w > 256)) {
		dev_err(dev, "%s: Vertical black white pattern out of range.\n", __func__);
		return;
	}

	if (pattern.mode == VS_RMAP_H)
		pattern.ramp_step = 65535 / src_w * 2048;
	else if (pattern.mode == VS_RMAP_V)
		pattern.ramp_step = 65535 / src_h * 2048;

	if (dc_fe_power_get(dev, true) < 0) {
		dev_err(dev, "Unable to get FE power to set plane pattern\n");
		return;
	}
	dc_hw_set_plane_pattern(&dc->hw, hw_id, &pattern);
	dc_fe_power_put(dev, false);
}

static void _get_u32_value_after_str(char *p, char *str, u32 size, u32 *value)
{
	char *cur = p;
	int ret = 0;

	cur = strstr(p, str);
	if (cur) {
		cur += size;
		ret = kstrtoul(cur, 10, (unsigned long *)value);
		if (ret) {
			pr_err("%s: failed to kstrtoul: %d\n", __func__, ret);
		}
	}
}

static void vs_dc_set_plane_qos(struct device *dev, struct vs_plane *plane, const char __user *ubuf,
				size_t len)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	struct drm_plane_state *state = plane->base.state;
	struct vs_plane_state *plane_state = to_vs_plane_state(state);
	char buf[256];
	u8 i, hw_id;

	hw_id = dc->planes[plane->id].id;

	if (len > sizeof(buf) - 1) {
		pr_err("%s: input string size out of range.\n", __func__);
		return;
	}

	if (copy_from_user(buf, ubuf, len)) {
		pr_err("%s: copy from user failed.\n", __func__);
		return;
	}

	for (i = 0; i < MAX_QOS_LEVLE; i++) {
		char temp[14];
		char temp_1[14];
		char temp_2[11];

		snprintf(temp, 14, "fe0QosValue%d:", i);
		snprintf(temp_1, 14, "fe1QosValue%d:", i);
		snprintf(temp_2, 11, "qosValue%d:", i);

		_get_u32_value_after_str(buf, temp, 14, &plane_state->qos.fe0_qos_value[i]);
		_get_u32_value_after_str(buf, temp_1, 14, &plane_state->qos.fe1_qos_value[i]);
		_get_u32_value_after_str(buf, temp_2, 11, &plane_state->qos.plane_qos_value[i]);
	}

	plane_state->qos.dirty = true;

	/* update status to hw status */
	memcpy(&dc->hw.plane[hw_id].qos, &plane_state->qos, sizeof(struct dc_hw_plane_qos));
}

static int vs_dc_show_plane_qos_status(struct seq_file *s)
{
	struct drm_plane *plane = s->private;
	struct vs_plane_state *plane_state = to_vs_plane_state(plane->state);
	u8 i;

	if (!plane_state->qos.dirty)
		dc_hw_get_fe_qos_value(&plane_state->qos);

	for (i = 0; i < MAX_QOS_LEVLE; i++) {
		seq_printf(s, "\tFE0 QOS value %d= [%d]\n", i, plane_state->qos.fe0_qos_value[i]);
		seq_printf(s, "\tFE1 QOS value %d= [%d]\n", i, plane_state->qos.fe1_qos_value[i]);
		seq_printf(s, "\tplane QOS value %d= [%d]\n", i,
			   plane_state->qos.plane_qos_value[i]);
	}

	return 0;
}
#endif /* CONFIG_DEBUG_FS */

static void update_plane(struct vs_dc *dc, struct vs_plane *plane)
{
	struct device *dev = dc->hw.dev;
	struct drm_plane_state *state = plane->base.state;
	struct vs_plane_state *plane_state = to_vs_plane_state(state);
	struct vs_plane_info *plane_info;
	struct dc_hw_fb fb = { 0 };
	struct dc_hw_plane *hw_plane = &dc->hw.plane[plane->id];
	const struct dc_hw_scale *scale = &hw_plane->scale;
	const struct drm_vs_dma *dma = NULL;
	u8 display_id = 0;
	u64 fb_modifier = plane_state->base.fb->modifier;
	int i;
	bool ret;

	plane_info = get_plane_info(plane->id, dc->hw.info);
	if (!plane_info) {
		dev_err(dev, "%s: invalid plane id: %d\n", __func__, plane->id);
		return;
	}

	if (plane_info->roi)
		dma = vs_dc_drm_plane_property_get(plane_state, "DMA_CONFIG", NULL);

	display_id = to_vs_display_id(dc, state->crtc);

	update_fbc(dc, plane);

	update_plane_fb(plane, display_id, &fb, plane_info);
	plane_state->base.fb->modifier = fb_modifier;

	dc_hw_update_plane(&dc->hw, plane->id, &fb);

	dc_hw_config_plane_status(&dc->hw, plane->id, true);

	if (plane_info->rcd_plane) {
		update_rcd_plane(dc, plane, display_id);
		return;
	}

	update_scale(dc, plane->id, plane_state);

	update_roi(dc, plane->id, plane_state);

	update_plane_y2r(dc, plane->id, plane_state);

	update_plane_position(dc, plane->id, plane_state);

	update_plane_blend(dc, plane->id, plane_state, plane_info);

	update_plane_3d_lut(dc, plane->id, plane_state, dc->hw.info);

	update_plane_ext_fb(dc, plane->id, plane_state);

	update_plane_clear(dc, plane->id, plane_state);
	vs_dc_update_sram(dc, plane, &fb, hw_plane, scale, dma);

	for (i = 0; i < plane->properties.num; i++) {
		ret = vs_dc_update_drm_property(dc, plane_info->id, &plane_state->drm_states[i],
						hw_plane->states.items[i].proto,
						&hw_plane->states.items[i], plane_state);
		if (ret && !hw_plane->states.items[i].proto->update)
			trace_update_hw_layer_feature_en_dirty(
				hw_plane->states.items[i].proto->name, plane_info->id,
				hw_plane->states.items[i].enable, hw_plane->states.items[i].dirty);
	}

	vs_dpu_link_node_config(&dc->hw, VS_DPU_LINK_LAYER, hw_plane->info->id,
				dc->hw.display[hw_plane->fb.display_id].sbs_split_dirty ?
					((display_id >> 1) ? 2 : 0) :
					display_id,
				true);
}

static void vs_dc_update_plane(struct device *dev, struct vs_plane *plane)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	enum drm_plane_type type = plane->base.type;

	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
	case DRM_PLANE_TYPE_OVERLAY:
	case DRM_PLANE_TYPE_CURSOR:
		trace_disp_plane(plane->id, true);
		update_plane(dc, plane);
		break;
	default:
		break;
	}
}

static void vs_dc_disable_plane(struct device *dev, struct vs_plane *plane)
{
	struct drm_plane_state *state = plane->base.state;
	struct vs_plane_state *plane_state = to_vs_plane_state(state);
	struct vs_plane_info *plane_info;
	struct vs_dc *dc = dev_get_drvdata(dev);
	struct dc_hw_plane *hw_plane = &dc->hw.plane[plane->id];
	const struct dc_hw_scale *scale = &hw_plane->scale;
	const struct drm_vs_dma *dma = NULL;
	enum drm_plane_type type = plane->base.type;
	struct dc_hw_fb fb = { 0 };
	struct dc_hw_rcd_mask rcd_mask = { 0 };
	struct dc_hw_roi roi = { 0 };
	struct dc_hw_scale plane_scale = { 0 };
	struct dc_hw_position pos = { 0 };
	struct dc_hw_std_bld std_bld = { 0 };
	struct dc_hw_clear clear = { 0 };

	plane_info = get_plane_info(plane->id, dc->hw.info);
	if (!plane_info) {
		dev_err(dev, "%s: invalid plane id: %d\n", __func__, plane->id);
		return;
	}

	if (plane_info->roi)
		dma = vs_dc_drm_plane_property_get(plane_state, "DMA_CONFIG", NULL);

	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
	case DRM_PLANE_TYPE_OVERLAY:
	case DRM_PLANE_TYPE_CURSOR:
		trace_disp_plane(plane->id, false);
		fb.enable = false;
		dc_hw_update_plane(&dc->hw, plane->id, &fb);
		dc_hw_config_plane_status(&dc->hw, plane->id, true);
		if (plane_info->rcd_plane) {
			rcd_mask.display_id = plane_info->crtc_id;
			dc_hw_update_plane_rcd_mask(&dc->hw, plane->id, &rcd_mask);
			break;
		}
		dc_hw_update_plane_scale(&dc->hw, plane->id, &plane_scale);
		dc_hw_update_plane_roi(&dc->hw, plane->id, &roi);
		dc_hw_update_plane_position(&dc->hw, plane->id, &pos);
		dc_hw_update_plane_std_bld(&dc->hw, plane->id, &std_bld);
		dc_hw_update_plane_clear(&dc->hw, plane->id, &clear);
		vs_dc_update_sram(dc, plane, &fb, &dc->hw.plane[plane->id], scale, dma);
		break;
	default:
		break;
	}
}

static bool vs_dc_mod_supported(const struct vs_plane_info *plane_info, u64 modifier)
{
	const u64 *mods;

	uint64_t vs_modifier = vs_fb_parse_fourcc_modifier(modifier);

	if (plane_info->modifiers == NULL)
		return false;
	for (mods = plane_info->modifiers; *mods != DRM_FORMAT_MOD_INVALID; mods++) {
		if (*mods == vs_modifier)
			return true;
	}

	return false;
}

static int check_plane_sram_dma_size(const struct vs_dc_info *info,
				     const struct dc_hw_plane *hw_plane,
				     struct vs_plane_state *vs_plane_state,
				     const struct vs_plane_info *plane_info)
{
	int ret;
	struct device *dev = vs_plane_state->base.plane->dev->dev;
	const struct drm_plane_state *plane_state = &vs_plane_state->base;
	const struct drm_framebuffer *fb = plane_state->fb;
	u32 dma_sram_unit_size = info->dma_sram_unit_size;
	u16 dma_sram_alignment = info->dma_sram_alignment;
	bool dma_sram_extra_buffer = info->dma_sram_extra_buffer;
	u32 dma_sram_max_size_kb = plane_info->dma_sram_max_size_kb;
	u32 dma_sram_max_size = dma_sram_max_size_kb << 10;
	u32 dma_sram_size = 0;
	u16 width;

	if (!dma_sram_max_size_kb)
		return 0;

	width = plane_state->rotation & (DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270) ? fb->height :
										     fb->width;
	ret = vs_dpu_get_dma_sram_size(to_vs_format(fb->format->format, fb->modifier),
				       to_vs_tile_mode(fb), to_vs_rotation(plane_state->rotation),
				       width, &dma_sram_size, dma_sram_alignment,
				       dma_sram_extra_buffer, dma_sram_unit_size);
	if (ret) {
		dev_warn(dev, "[Reject] plane %d failed to calculate sram dma size\n",
			 plane_info->id);
		return ret;
	}

	dev_dbg(dev, "%s: plane %d dma_sram_size %u, dma_sram_max_size %u\n", __func__,
		plane_info->id, dma_sram_size, dma_sram_max_size);

	if (dma_sram_size > dma_sram_max_size) {
		dev_warn(dev, "[Reject] plane %d dma_sram_size %u > %u\n", plane_info->id,
			 dma_sram_size, dma_sram_max_size);
		return -EINVAL;
	}

	/* save calculated plane dma sram size */
	vs_plane_state->dma_sram_size = dma_sram_size;

	return 0;
}

static int check_plane_sram_scl_size(const struct vs_dc_info *info,
				     const struct dc_hw_plane *hw_plane,
				     struct vs_plane_state *vs_plane_state,
				     const struct vs_plane_info *plane_info)
{
	int ret;
	struct device *dev = vs_plane_state->base.plane->dev->dev;
	const struct drm_plane_state *plane_state = &vs_plane_state->base;
	const struct drm_framebuffer *fb = plane_state->fb;
	u32 scl_sram_max_size_kb = plane_info->scl_sram_max_size_kb;
	u32 scl_sram_max_size = scl_sram_max_size_kb << 10;
	struct dc_hw_scale scale = { 0 };
	u32 scl_sram_size = 0;

	if (!scl_sram_max_size_kb)
		return 0;

	ret = populate_layer_scale(plane_state, &scale);
	if (ret)
		return ret;

	if (!scale.enable)
		return 0;

	ret = vs_dpu_get_fescl_sram_size(to_vs_format(fb->format->format, fb->modifier),
					 min(scale.src_w >> 16, scale.dst_w), &scl_sram_size);
	if (ret) {
		dev_warn(dev, "[Reject] plane %d failed to calculate sram scaler size\n",
			 plane_info->id);
		return ret;
	}

	dev_dbg(dev, "%s: plane %d scl_sram_size %u, scl_sram_max_size %u\n", __func__,
		plane_info->id, scl_sram_size, scl_sram_max_size);

	if (scl_sram_size > scl_sram_max_size) {
		dev_warn(dev, "[Reject] plane %d scl_sram_size %u > %u\n", plane_info->id,
			 scl_sram_size, scl_sram_max_size);
		return -EINVAL;
	}

	/* save calculated plane scl sram size */
	vs_plane_state->scl_sram_size = scl_sram_size;

	return 0;
}

static int check_plane_sram(const struct vs_dc_info *info, const struct dc_hw_plane *hw_plane,
			    struct vs_plane_state *vs_plane_state,
			    const struct vs_plane_info *plane_info)
{
	int ret;
	const struct drm_plane_state *plane_state = &vs_plane_state->base;
	const struct drm_framebuffer *fb = plane_state->fb;

	if (vs_fb_is_shallow(fb->modifier))
		return 0;

	if (hw_plane->info->type == DRM_PLANE_TYPE_CURSOR)
		return 0;

	ret = check_plane_sram_dma_size(info, hw_plane, vs_plane_state, plane_info);
	if (ret)
		return ret;

	ret = check_plane_sram_scl_size(info, hw_plane, vs_plane_state, plane_info);
	if (ret)
		return ret;

	return 0;
}

static int check_dma_constraints(struct vs_plane_state *vs_plane_state,
				 const struct vs_plane_info *plane_info,
				 const struct drm_framebuffer *fb, struct drm_rect *roi)
{
	struct device *dev = vs_plane_state->base.plane->dev->dev;
	bool is_yuv = fb->format->is_yuv;
	uint64_t vs_fourcc_mod = vs_fb_parse_fourcc_modifier(fb->modifier);
	bool is_pvric = fourcc_mod_vs_get_type(vs_fourcc_mod) == DRM_FORMAT_MOD_VS_TYPE_PVRIC;

	int src_w = drm_rect_width(roi);
	int src_h = drm_rect_height(roi);
	int src_x = roi->x1;
	int src_y = roi->y1;
	unsigned int h_stride = fb->pitches[0];
	unsigned int fb_lines;

	/* DMA constraints are not relevant to shallow FBs, since we don't use the backing memory */
	if (vs_fb_is_shallow(fb->modifier))
		return 0;

	fb_lines = (fb->obj[0]->size - fb->offsets[0]) / h_stride;

	if (src_w < plane_info->min_width || fb->width > plane_info->max_width ||
	    src_h < plane_info->min_height || fb->height > plane_info->max_height) {
		dev_err(dev,
			"[Reject] fb or crop exceeds limits on plane %d. fb %dx%d crop %dx%d\n",
			plane_info->id, fb->width, fb->height, src_w, src_h);
		return -EINVAL;
	}

	if (is_pvric) {
		u64 buffer_base;
		u64 tile_base;
		unsigned int requirement;
		int i = 0;
		const struct drm_format_info *format_info;
		struct vs_gem_object *vs_obj;

		if (is_yuv)
			requirement = VS_ALIGN_STRIDE;
		else
			requirement = VS_ALIGN_PVRIC_STRIDE;

		if (!IS_ALIGNED(h_stride, requirement)) {
			dev_err(dev,
				"[Reject] Invalid PVRIC stride alignment on plane: %d. stride: %d. requirement: %d\n",
				plane_info->id, h_stride, requirement);
			return -EINVAL;
		}

		format_info = drm_format_info(fb->format->format);
		if (!format_info || format_info->num_planes > MAX_NUM_PLANES) {
			dev_err(dev, "[Reject] Invalid format info attached to framebuffer\n");
			return -EINVAL;
		}

		for (i = 0; i < format_info->num_planes; i++) {
			vs_obj = to_vs_gem_object(fb->obj[i]);
			buffer_base = vs_obj->iova;
			tile_base = buffer_base + fb->offsets[i];
			if (fb->offsets[i] < VS_ALIGN_PVRIC_BASE) {
				dev_err(dev,
					"[Reject] PVRIC header is smaller than the minimum size %d: header addr: 0x%llx, tile addr 0x%llx for plane %d in layer %d.",
					VS_ALIGN_PVRIC_BASE, buffer_base, tile_base, i,
					plane_info->id);
				return -EINVAL;
			}
			if (!IS_ALIGNED(tile_base, VS_ALIGN_PVRIC_BASE)) {
				dev_err(dev,
					"[Reject] PVRIC tile_base: 0x%llx (base_addr: 0x%llx + offset 0x%x) for plane %d in layer %d is not aligned to %d!",
					tile_base, buffer_base, fb->offsets[i], i, plane_info->id,
					VS_ALIGN_PVRIC_BASE);
				return -EINVAL;
			}
		}
	} else {
		if (!IS_ALIGNED(h_stride, VS_ALIGN_STRIDE)) {
			dev_err(dev,
				"[Reject] Invalid stride alignment on plane: %d. stride: %d. requirement: %d\n",
				plane_info->id, h_stride, VS_ALIGN_STRIDE);
			return -EINVAL;
		}
	}

	/*
	 * Note that YUV422 and PVRIC FP16 do have special constraints, but are not supported by
	 * the driver currently so are not supported here.
	 */

	/* For vertical stride DMA requirements, we ensure that the buffer is *at least* as large as
	 * ALIGN(fb->height, <format_alignment_requirement>). We are not concerned with the exact
	 * fb_lines value, since there may be additional padding present.
	 */
	if (is_yuv) {
		if (!IS_ALIGNED(h_stride, VS_ALIGN_YUV420) ||
		    (fb_lines < ALIGN(fb->height, VS_ALIGN_YUV420)) ||
		    !IS_ALIGNED(src_w, VS_ALIGN_YUV420) || !IS_ALIGNED(src_h, VS_ALIGN_YUV420) ||
		    !IS_ALIGNED(src_x, VS_ALIGN_YUV420) || !IS_ALIGNED(src_y, VS_ALIGN_YUV420)) {
			dev_err(dev,
				"[Reject] Invalid alignment for YUV420 on plane: %d. buffer %dx%d crop %dx%d @ %d,%d\n",
				plane_info->id, h_stride, fb_lines, src_w, src_h, src_x, src_y);
			return -EINVAL;
		}

		if (fb->width > plane_info->max_yuv_width) {
			dev_err(dev,
				"[Reject] Exceeding max YUV width on plane: %d. h_stride: %d, crop width %d\n",
				plane_info->id, h_stride, src_w);
			return -EINVAL;
		}

		/* Specifically handling PVRIC YUV420 case here*/
		if (is_pvric && (fb_lines < ALIGN(fb->height, VS_ALIGN_YUV420_PVRIC_FB_HEIGHT))) {
			dev_err(dev,
				"[Reject] Invalid alignment for PVRIC YUV420 on plane: %d. vert stride %d\n",
				plane_info->id, fb_lines);
			return -EINVAL;
		}
	} else if (is_pvric && (fb_lines < ALIGN(fb->height, VS_ALIGN_ARGB_PVRIC_FB_HEIGHT))) {
		dev_err(dev,
			"[Reject] Invalid fb alignment for PVRIC ARGB on plane: %d. stride %dx%d\n",
			plane_info->id, h_stride, fb_lines);
		return -EINVAL;
	}

	return 0;
}

static int check_plane_fb(struct drm_plane_state *state, struct vs_plane_state *vs_plane_state,
			  const struct vs_plane_info *plane_info)
{
	int ret;
	struct device *dev = vs_plane_state->base.plane->dev->dev;
	struct vs_drm_property_state *dma_cfg_state = NULL;
	const struct drm_vs_dma *dma_blob;
	struct drm_rect roi;

	drm_rect_fp_to_int(&roi, &state->src);

	/* Check primary FB DMA constraints */
	ret = check_dma_constraints(vs_plane_state, plane_info, state->fb, &roi);
	if (ret) {
		dev_err(dev, "%s Invalid dma constraints for fb and src\n", __func__);
		return ret;
	}

	/* Check VS_DMA_TWO_ROI configuration */
	if (plane_info->roi) {
		dma_cfg_state = vs_dc_get_drm_property_state(dev, vs_plane_state->drm_states,
							     VS_DC_MAX_PROPERTY_NUM, "DMA_CONFIG");
		dma_blob = (dma_cfg_state && dma_cfg_state->value.blob) ?
				   dma_cfg_state->value.blob->data :
				   NULL;

		if (dma_blob && dma_blob->mode == VS_DMA_TWO_ROI) {
			struct drm_rect roi1;

			/* Check VS_DMA_TWO_ROI ROI-1 DMA constraints */
			drm_rect_init(&roi1, dma_blob->in_rect.x, dma_blob->in_rect.y,
				      dma_blob->in_rect.w, dma_blob->in_rect.h);
			ret = check_dma_constraints(vs_plane_state, plane_info, state->fb, &roi1);
			if (ret) {
				dev_err(dev,
					"%s: Invalid DMA constraints for FB and in_rect "
					DRM_RECT_FMT "\n", __func__, DRM_RECT_ARG(&roi1));
				return ret;
			}

			/**
			 * Set is_changed to force checking frame buffer size against ROI
			 * (by dma_config_check()) for the case where only FB is changed.
			 */
			dma_cfg_state->is_changed = true;
		}
	}

	return 0;
}

static inline bool size_supported_for_rotation(u32 width, u32 height, bool compressed, u32 format,
					       u32 max_nv12_uncomp_rot_width,
					       u32 max_nv12_uncomp_rot_height)
{
	u32 tcu_size;

	/* Note that the multiplication by 1.5 (3/2) is for NV12 */
	if (compressed) {
		tcu_size = (height * 3) / (PVRIC_BLOCK_HEIGHT * 2);
	} else {
		/* b/402363421: use DPU to directly read the camera preview buffer */
		if (format == DRM_FORMAT_NV12 && width <= max_nv12_uncomp_rot_width &&
		    height <= max_nv12_uncomp_rot_height)
			return true;
		tcu_size = mult_frac(height * 3, width, 4096 * 2);
	}
	return (tcu_size < VS_MAX_TCU_SIZE);
}

static int check_plane_rotation(struct drm_plane_state *plane_state,
				const struct vs_plane_info *plane_info,
				const struct vs_dc_info *dc_info)
{
	const struct device *dev = plane_state->plane->dev->dev;
	const struct drm_framebuffer *fb = plane_state->fb;
	bool is_yuv = fb->format->is_yuv;
	uint64_t vs_fourcc_mod = vs_fb_parse_fourcc_modifier(fb->modifier);
	bool is_pvric = fourcc_mod_vs_get_type(vs_fourcc_mod) == DRM_FORMAT_MOD_VS_TYPE_PVRIC;
	u32 src_w = drm_rect_width(&plane_state->src) >> 16;
	u32 src_h = drm_rect_height(&plane_state->src) >> 16;
	u32 rotate_mask = plane_state->rotation & DRM_MODE_ROTATE_MASK;
	u32 flip_mask = plane_state->rotation & DRM_MODE_REFLECT_MASK;

	if (plane_state->rotation == DRM_MODE_ROTATE_0)
		return 0;

	if (!is_yuv) {
		dev_err(dev, "[Reject] Rotation/flip only supported for YUV.\n");
		return -EINVAL;
	}

	/* Reduce complex Flip + Rotate */
	if (flip_mask == (DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y)) {
		switch (rotate_mask) {
		case DRM_MODE_ROTATE_0:
			rotate_mask = DRM_MODE_ROTATE_180;
			break;
		case DRM_MODE_ROTATE_90:
			rotate_mask = DRM_MODE_ROTATE_270;
			break;
		case DRM_MODE_ROTATE_180:
			rotate_mask = DRM_MODE_ROTATE_0;
			break;
		case DRM_MODE_ROTATE_270:
			rotate_mask = DRM_MODE_ROTATE_90;
			break;
		default:
			dev_err(dev, "Invalid Rotate Mask!\n");
			return -EINVAL;
		}
		flip_mask = 0;
	} else if (rotate_mask == DRM_MODE_ROTATE_180) {
		if (flip_mask) {
			flip_mask ^= DRM_MODE_REFLECT_X | DRM_MODE_REFLECT_Y;
			rotate_mask = DRM_MODE_ROTATE_0;
		}
	}

	/* Several constraints around 90/270 degree rotation:
	 *  1) b/318783657 - Rotation + x or y flip not supported
	 *  2) Portrait-mode rotation not supported
	 *  3) TCU size limitation
	 */

	if (rotate_mask & (DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270)) {
		if (flip_mask != 0) {
			dev_err(dev, "[Reject] Rotate 90/270 + flip_x/flip_y not supported!\n");
			return -EINVAL;
		}

		if (src_w < src_h) {
			dev_err(dev,
				"[Reject] Rotation not supported on portrait aspect ratios!\n");
			return -EINVAL;
		}

		if (!size_supported_for_rotation(src_w, src_h, is_pvric, fb->format->format,
						 dc_info->max_nv12_uncomp_rot_width,
						 dc_info->max_nv12_uncomp_rot_height)) {
			dev_err(dev, "[Reject] Plane size exceeds TCU limits for rotation.\n");
			return -EINVAL;
		}
	}

	/* Atomic check allows the freestanding state to be modified. */
	plane_state->rotation = rotate_mask | flip_mask;

	return 0;
}

static int check_plane_ext_fb(struct vs_plane_state *plane_state,
			      const struct vs_plane_info *plane_info)
{
	struct device *dev = plane_state->base.plane->dev->dev;
	const struct drm_framebuffer *fb;
	const struct drm_framebuffer *fb_ext = plane_state->fb_ext;
	struct vs_drm_property_state *dma_cfg_state = NULL;
	const struct drm_vs_dma *dma_blob;
	struct drm_rect roi1;
	int ret;

	if (plane_info->roi)
		dma_cfg_state = vs_dc_get_drm_property_state(dev, plane_state->drm_states,
							     VS_DC_MAX_PROPERTY_NUM, "DMA_CONFIG");

	dma_blob = (dma_cfg_state && dma_cfg_state->value.blob) ? dma_cfg_state->value.blob->data :
								  NULL;

	if (fb_ext) {
		if (!plane_info->layer_ext && !plane_info->layer_ext_ex) {
			dev_err(dev, "%s The plane is not support layer extend DMA mode.\n",
				__func__);
			return -EINVAL;
		}

		fb = plane_state->base.fb;
		if (fb->format->format != fb_ext->format->format) {
			dev_err(dev, "%s Invalid extend layer fb format\n", __func__);
			return -EINVAL;
		}

		if (!dma_blob) {
			dev_err(dev, "%s Invalid DMA_CONFIG blob with fb_ext\n", __func__);
			return -EINVAL;
		}
	}

	if (dma_blob) {
		/* DMA_MODE should be VS_DMA_EXT_LAYER or VS_DMA_EXT_LAYER_EX with fb_ext */
		if (dma_blob->mode != VS_DMA_EXT_LAYER && dma_blob->mode != VS_DMA_EXT_LAYER_EX) {
			if (fb_ext) {
				dev_err(dev, "%s Invalid DMA_CONFIG mode with fb_ext\n", __func__);
				return -EINVAL;
			}

			/* nothing to do if not in ext layer mode */
			return 0;
		}

		if (!fb_ext) {
			dev_err(dev, "%s Invalid fb_ext with DMA_CONFIG mode\n", __func__);
			return -EINVAL;
		}

		/* Check VS_DMA_EXT_LAYER or VS_DMA_EXT_LAYER_EX ROI-1 DMA constraints */
		drm_rect_init(&roi1, dma_blob->in_rect.x, dma_blob->in_rect.y, dma_blob->in_rect.w,
			      dma_blob->in_rect.h);
		ret = check_dma_constraints(plane_state, plane_info, fb_ext, &roi1);
		if (ret) {
			dev_err(dev,
				"%s: Invalid DMA constraints for fb_ext and in_rect " DRM_RECT_FMT
				"\n",
				__func__, DRM_RECT_ARG(&roi1));
			return ret;
		}

		/**
		 * Set is_changed to force checking frame buffer size against ROI
		 * (by dma_config_check()) for the case where only fb_ext is changed.
		 */
		dma_cfg_state->is_changed = true;
	}

	return 0;
}

static int check_plane_scale(struct drm_plane_state *plane_state,
			     const struct vs_plane_info *plane_info)
{
	struct drm_plane *plane = plane_state->plane;
	struct drm_atomic_state *state = plane_state->state;
	struct vs_plane_state *vs_plane_state = to_vs_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;

	struct drm_plane_state *old_plane_state = NULL;
	struct dc_hw_scale scale = { 0 };
	struct device *dev = plane->dev->dev;
	int ret;

	if ((plane_state->src_w >> 16 != fb->width || plane_state->src_h >> 16 != fb->height) &&
	    !plane_info->roi) {
		dev_err(dev, "%s src roi not match to fb dimension\n", __func__);
		return -EINVAL;
	}

	ret = populate_layer_scale(plane_state, &scale);
	if (ret) {
		dev_err(dev, "%s populate_layer_scale fail: %d\n", __func__, ret);
		return ret;
	}

	if (plane_info->min_scale == VS_PLANE_NO_SCALING &&
	    (scale.factor_x != VS_PLANE_NO_SCALING || scale.factor_y != VS_PLANE_NO_SCALING)) {
		dev_err(dev, "%s scaling factor mismatch\n", __func__);
		return -EINVAL;
	}

	if (state && plane)
		old_plane_state = drm_atomic_get_old_plane_state(state, plane);

	if (old_plane_state) {
		struct dc_hw_scale old_scale = { 0 };
		struct vs_plane_state *old_vs_plane_state;
		const struct drm_vs_preprocess_scale_config *old_coeff;
		const struct drm_vs_preprocess_scale_config *new_coeff;

		ret = populate_layer_scale(old_plane_state, &old_scale);
		if (ret) {
			dev_err(dev, "%s populate_layer_scale fail: %d\n", __func__, ret);
			return ret;
		}

		/*
		 * Dirty bit should be based on dimensions, not scale factor. Even if the scale
		 * factor remains the same between 2 frames the SRAM requirement may change.
		 *
		 * TODO(b/439088578) Out of an abundance of caution we still check the scale
		 * factors and stretch mode. This can be removed in a later release.
		 */
		if (scale.factor_x != old_scale.factor_x || scale.factor_y != old_scale.factor_y ||
		    scale.src_w != old_scale.src_w || scale.dst_w != old_scale.dst_w ||
		    scale.src_h != old_scale.src_h || scale.dst_h != old_scale.dst_h)
			set_bit(VS_PLANE_CHANGED_SCALING, vs_plane_state->changed);

		old_vs_plane_state = to_vs_plane_state(old_plane_state);
		old_coeff = vs_dc_drm_plane_property_get(old_vs_plane_state, "SCALER", NULL);
		new_coeff = vs_dc_drm_plane_property_get(vs_plane_state, "SCALER", NULL);

		if (old_coeff != new_coeff) {
			if (!old_coeff || !new_coeff ||
			    memcmp(old_coeff, new_coeff,
				   sizeof(struct drm_vs_preprocess_scale_config)))
				set_bit(VS_PLANE_CHANGED_SCALING_COEFF, vs_plane_state->changed);
		}
	} else {
		set_bit(VS_PLANE_CHANGED_SCALING, vs_plane_state->changed);
	}

	return 0;
}

static int check_secure_buffer(struct drm_plane_state *plane_state, struct vs_dc *dc)
{
	struct drm_plane *plane = plane_state->plane;
	struct device *dev = plane->dev->dev;
	struct vs_plane_state *vs_plane_state = to_vs_plane_state(plane_state);
	const void *prop;
	bool is_secure = false;

	prop = vs_dc_drm_plane_property_get(vs_plane_state, "SECURE_BUFFER", NULL);
	if (!prop) {
		dev_err(dev, "%s: Failed to get SECURE_BUFFER property!\n", __func__);
		return -EINVAL;
	}

	is_secure = *((const bool *)prop);

	if (is_secure) {
		if (!dc->tzprot_pdev) {
			dev_err(dev, "%s: SECURE_BUFFER set but tzprot_pdev is null.", __func__);
			return -EINVAL;
		}
		dev_dbg(dev,
			"%s: [No-Op]. Plane is Secure! TODO(b/355083830): add checks for disallowed features.",
			__func__);
	}

	return 0;
}

static int check_rcd_mask(struct drm_plane_state *plane_state,
			  const struct vs_plane_info *plane_info)
{
	unsigned int width;
	unsigned int height;
	int i;
	struct device *dev = plane_state->plane->dev->dev;

	/* Check if the plane is used with correct crtc_id */
	if (plane_state->crtc->index != plane_info->crtc_id) {
		dev_err(dev, "This RCD plane does not support crtc[%u]", plane_state->crtc->index);
		return -EINVAL;
	}

	/* Check if the framebuffer dimensions are acceptable */
	width = drm_rect_width(&plane_state->dst);
	height = drm_rect_height(&plane_state->dst);

	if (plane_info->max_width < width || plane_info->min_width > width ||
	    plane_info->max_height < height || plane_info->min_height > height) {
		dev_err(dev, "Frame buffer size %u x %u does not fit plane size constraints", width,
			height);
		return -EINVAL;
	}

	/* Check if the framebuffer format is supported */
	for (i = 0; i < plane_info->num_formats; i++) {
		if (plane_info->formats[i] == plane_state->fb->format->format)
			break;
	}

	if (i == plane_info->num_formats) {
		dev_err(dev, "RCD frame format is not supported");
		return -EINVAL;
	}

	return 0;
}

static int check_y2r_changed(struct drm_plane_state *plane_state)
{
	struct device *dev = plane_state->plane->dev->dev;
	struct drm_plane *plane = plane_state->plane;
	struct drm_atomic_state *state = plane_state->state;
	struct vs_plane_state *vs_plane_state = to_vs_plane_state(plane_state);
	struct drm_plane_state *old_plane_state = NULL;
	bool is_yuv = plane_state->fb->format->is_yuv;

	if (state && plane)
		old_plane_state = drm_atomic_get_old_plane_state(state, plane);

	if (old_plane_state && old_plane_state->fb) {
		// Note that if y2r_coef changed the flag will already be set by
		// vs_plane_atomic_set_property
		if (is_yuv != old_plane_state->fb->format->is_yuv ||
		    (is_yuv && (plane_state->color_range != old_plane_state->color_range ||
				plane_state->color_encoding != old_plane_state->color_encoding)))
			set_bit(VS_PLANE_CHANGED_Y2R, vs_plane_state->changed);
	} else {
		// first yuv plane or first plane after disable, mark changed
		set_bit(VS_PLANE_CHANGED_Y2R, vs_plane_state->changed);
	}

	if (vs_plane_state->y2r_coef && !is_yuv) {
		dev_err(dev, "%s: Y2R coefficients provided for an RGB frame!", __func__);
		return -EINVAL;
	}

	return 0;
}

static u32 _map_format_uncompress(u32 format)
{
	u32 f = FORMAT_ARGB;

	switch (format) {
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_ABGR16161616F:
		f = FORMAT_ARGB16161616;
		break;
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_P210:
		f = FORMAT_YUV422;
		break;
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_P010:
		f = FORMAT_YUV420;
		break;
	default:
		f = FORMAT_ARGB;
		break;
	}

	return f;
}

static u32 _map_format_pvric(u32 format)
{
	u32 f = FORMAT_ARGB_PV;

	switch (format) {
	case DRM_FORMAT_ARGB16161616F:
	case DRM_FORMAT_ABGR16161616F:
		f = FORMAT_ARGB16161616_PV;
		break;
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		f = FORMAT_YUV420_8BIT_PV;
		break;
	case DRM_FORMAT_P010:
		f = FORMAT_YUV420_10BIT_PV;
		break;
	default:
		f = FORMAT_ARGB_PV;
		break;
	}

	return f;
}

int check_roi_offset_align(struct drm_framebuffer *fb, const struct vs_dc_info *info, u16 offset_x,
			   u16 offset_y)
{
	u16 format_mod;
	u32 format = 0;

	if (!info->format_limit)
		return 0;

	format_mod = fourcc_mod_vs_get_type(fb->modifier);

	if (format_mod == DRM_FORMAT_MOD_VS_TYPE_NORMAL)
		format = _map_format_uncompress(fb->format->format);
	else if (format_mod == DRM_FORMAT_MOD_VS_TYPE_PVRIC)
		format = _map_format_pvric(fb->format->format);
	switch (format) {
	case FORMAT_YUV420:
		if (offset_x % 2 != 0 || offset_y % 2 != 0) {
			pr_err("%s: roi offset size does not meet format alignnment requirments.\n",
			       __func__);
			return -EINVAL;
		}
		break;
	case FORMAT_YUV422:
		if (offset_x % 2 != 0) {
			pr_err("%s: roi offset size does not meet format alignnment requirments.\n",
			       __func__);
			return -EINVAL;
		}
		break;
	case FORMAT_YUV420_8BIT_PV:
	case FORMAT_YUV420_10BIT_PV:
		if (offset_x % 2 != 0 || offset_y % 2 != 0) {
			pr_err("%s: roi offset size does not meet format alignnment requirments.\n",
			       __func__);
			return -EINVAL;
		}
		break;
	default:
		break;
	}
	return 0;
}

static void vs_get_format_limit(u32 format, const struct format_limit_info *format_list, u16 size,
				u16 type)
{
	u32 i;

	for (i = 0; i < size; i++) {
		if (format_list->format == format && format_list->type == type)
			break;
		format_list++;
	}
}

int check_format_limit(struct drm_framebuffer *fb, const struct vs_dc_info *info, u16 width,
		       u16 height, u16 type)
{
	u64 format_mod;
	u32 format = 0;
	const struct format_limit_info *format_limit;
	char *type_str;

	format_mod = fourcc_mod_vs_get_type(fb->modifier);

	if (format_mod == DRM_FORMAT_MOD_VS_TYPE_NORMAL)
		format = _map_format_uncompress(fb->format->format);
	else if (format_mod == DRM_FORMAT_MOD_VS_TYPE_PVRIC)
		format = _map_format_pvric(fb->format->format);

	vs_get_format_limit(format, info->format_limit, info->format_limit_size, type);
	format_limit = info->format_limit;
	switch (type) {
	case SRC:
		type_str = "src";
		break;
	case ROI:
		type_str = "roi";
		break;
	case SCALER:
		type_str = "scaler";
		break;
	default:
		type_str = "src";
		break;
	}

	if (width < format_limit->min_width || width > format_limit->max_width ||
	    height < format_limit->min_height || height > format_limit->max_height) {
		pr_err("%s: %s size exceed the format limit.\n", __func__, type_str);
		return -EINVAL;
	}
	if (fb->width % format_limit->width_align != 0 ||
	    fb->height % format_limit->height_align != 0) {
		pr_err("%s: %s size does not meet format alignnment requirments.\n", __func__,
		       type_str);
		return -EINVAL;
	}

	return 0;
}

static int check_tile_size(struct drm_framebuffer *fb)
{
	u32 format = 0;
	u8 tile_mode = 0;
	int ret = 0;

	if (fourcc_mod_vs_get_type(fb->modifier) != DRM_FORMAT_MOD_VS_TYPE_PVRIC)
		return 0;

	tile_mode = fourcc_mod_vs_get_tile_mode(fb->modifier);
	if (tile_mode == DRM_FORMAT_MOD_VS_DEC_LINEAR)
		return 0;

	format = _map_format_pvric(fb->format->format);
	switch (tile_mode) {
	case DRM_FORMAT_MOD_VS_DEC_TILE_16X4:
		if (fb->width % 16 != 0 || fb->height % 4 != 0)
			ret = -EINVAL;
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_8X8:
		if (format == FORMAT_YUV420_8BIT_PV) {
			if (fb->width % 32 != 0 || fb->height % 8 != 0)
				ret = -EINVAL;
		} else if (format == FORMAT_YUV420_10BIT_PV) {
			if (fb->width % 16 != 0 || fb->height % 8 != 0)
				ret = -EINVAL;
		}
		break;
	case DRM_FORMAT_MOD_VS_DEC_TILE_32X2:
		if (fb->width % 32 != 0 || fb->height % 2 != 0)
			ret = -EINVAL;
		break;
	default:
		break;
	}

	if (ret)
		pr_err("%s: format size does not meet tile alignnment requirments.\n", __func__);

	return ret;
}

static int check_fb_address(struct vs_plane *plane, struct drm_plane_state *state)
{
	u64 address, u_address, v_address = 0x0;
	u32 stride, u_stride, v_stride = 0;
	u16 format_mod;
	u32 format = 0;
	u8 num_planes, i;
	struct drm_framebuffer *fb;
	dma_addr_t dma_addr[3];

	fb = state->fb;
	stride = fb->pitches[0];
	u_stride = fb->pitches[1];
	v_stride = fb->pitches[2];
	num_planes = fb->format->num_planes;

	for (i = 0; i < num_planes; i++) {
		dma_addr[i] = vs_fb_get_dma_addr(fb, i);
	}
	address = (u64)dma_addr[0];
	u_address = (u64)dma_addr[1];
	v_address = (u64)dma_addr[2];

	format_mod = fourcc_mod_vs_get_type(fb->modifier);
	if (format_mod == DRM_FORMAT_MOD_VS_TYPE_NORMAL)
		format = _map_format_uncompress(fb->format->format);
	else if (format_mod == DRM_FORMAT_MOD_VS_TYPE_PVRIC)
		format = _map_format_pvric(fb->format->format);

	if (!format)
		return 0;

	switch (format) {
	case FORMAT_ARGB:
		if ((address & 0xFF) != 0 || (stride & 0x1F) != 0) {
			pr_err("%s: format base address/stride does not meet 256Byte/32Byte alignnment requirments.\n",
			       __func__);
			return -EINVAL;
		}
		break;
	case FORMAT_YUV420:
	case FORMAT_YUV422:
		if ((address & 0xFF) != 0 || (u_address & 0xFF) != 0 || (v_address & 0xFF) != 0 ||
		    (stride & 0x1F) != 0 || (u_stride & 0x1F) != 0 || (v_stride & 0x1F) != 0) {
			pr_err("%s: format base address/stride does not meet 256Byte/32Byte alignnment requirments.\n",
			       __func__);
			return -EINVAL;
		}
		break;
	case FORMAT_ARGB16161616:
		if ((address & 0xFF) != 0 || (stride & 0x3F) != 0) {
			pr_err("%s: format base address/stride does not meet 256Byte/64Byte alignnment requirments.\n",
			       __func__);
			return -EINVAL;
		}
		break;
	case FORMAT_ARGB_PV:
	case FORMAT_ARGB16161616_PV:
		if ((address & 0xFF) != 0 || (stride & 0xFF) != 0) {
			pr_err("%s: format base address/stride does not meet 256Byte/256Byte alignnment requirments.\n",
			       __func__);
			return -EINVAL;
		}
		break;
	case FORMAT_YUV420_10BIT_PV:
		if ((address & 0xFF) != 0 || (u_address & 0xFF) != 0 || (v_address & 0xFF) != 0 ||
		    (stride & 0xFF) != 0 || (u_stride & 0xFF) != 0 || (v_stride & 0xFF) != 0) {
			pr_err("%s: format base address/stride does not meet 256Byte/256Byte alignnment requirments.\n",
			       __func__);
			return -EINVAL;
		}
		break;
	default:
		break;
	}

	return 0;
}

/**
 * check_plane_swap() - Check if a plane is being swapped between CRTCs.
 * @vs_plane_state: The plane state being checked.
 * @crtc: The new CRTC for the plane.
 * @crtc_state: The state of the new CRTC.
 *
 * This function detects if a plane is being moved from one CRTC to another.
 * If a swap is detected, it updates the swapped_plane_mask of the new CRTC's
 * state to ensure proper synchronization during the commit process.
 *
 * Return: 0 on success, or -EINVAL if any of the input parameters are NULL.
 */
static int check_plane_swap(struct vs_plane_state *vs_plane_state, struct drm_crtc *crtc,
			    struct drm_crtc_state *crtc_state)
{
	struct vs_crtc_state *vs_crtc_state;

	if (!vs_plane_state || !crtc || !crtc_state)
		return -EINVAL;

	vs_crtc_state = to_vs_crtc_state(crtc_state);
	if (vs_plane_state->pre_swap_crtc && vs_plane_state->pre_swap_crtc != crtc)
		vs_crtc_state->swapped_plane_mask |= drm_plane_mask(vs_plane_state->base.plane);

	vs_plane_state->pre_swap_crtc = crtc;

	return 0;
}

static int vs_dc_check_plane(struct device *dev, struct vs_plane *plane,
			     struct drm_plane_state *state)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	struct drm_framebuffer *fb = state->fb;
	const struct vs_plane_info *plane_info;
	struct drm_crtc *crtc = state->crtc;
	struct drm_crtc_state *crtc_state;
	struct vs_plane_state *vs_plane_state = to_vs_plane_state(state);
	const struct dc_hw_plane *hw_plane = NULL;
	const struct drm_vs_sbs *sbs = NULL;
	int ret = 0;

	if (dc->hw.info->format_limit) {
		if (check_format_limit(fb, dc->hw.info, fb->width, fb->height, SRC))
			return -EINVAL;
		if (check_tile_size(fb))
			return -EINVAL;
		if (check_fb_address(plane, state))
			return -EINVAL;
	}

	plane_info = get_plane_info(plane->id, dc->hw.info);
	if (!plane_info) {
		dev_err(dev, "%s: invalid plane id: %d\n", __func__, plane->id);
		return -EINVAL;
	}

	hw_plane = vs_dc_hw_get_plane(&dc->hw, plane_info->id);

	if (plane_info->sbs)
		sbs = vs_dc_drm_plane_property_get(vs_plane_state, "SBS", NULL);

	crtc_state = drm_atomic_get_existing_crtc_state(state->state, crtc);
	if (IS_ERR(crtc_state)) {
		dev_err(dev, "%s: plane id %d has invalid crtc_state\n", __func__, plane->id);
		return -EINVAL;
	}

	ret = drm_atomic_helper_check_plane_state(state, crtc_state, plane_info->min_scale,
						  plane_info->max_scale, true, true);
	if (ret) {
		dev_err(dev, "%s: plane id %d check_plane_state fail\n", __func__, plane->id);
		return ret;
	}

	if (!state->visible)
		return 0;

	if ((plane->base.type != DRM_PLANE_TYPE_CURSOR) &&
	    (!vs_dc_mod_supported(plane_info, fb->modifier))) {
		dev_err(dev, "unsupported modifier on plane%d.\n", plane->id);
		return -EINVAL;
	}

	ret = check_plane_fb(state, vs_plane_state, plane_info);
	if (ret)
		return ret;

	ret = check_plane_rotation(state, plane_info, dc->hw.info);
	if (ret)
		return ret;

	ret = check_plane_ext_fb(vs_plane_state, plane_info);
	if (ret)
		return ret;

	if (!vs_dc_check_drm_property(dc, plane_info->id, vs_plane_state->drm_states,
				      plane->properties.num, vs_plane_state))
		return -EINVAL;

	ret = check_plane_sram(dc->hw.info, hw_plane, vs_plane_state, plane_info);
	if (ret)
		return ret;

	ret = check_plane_swap(vs_plane_state, crtc, crtc_state);
	if (ret)
		return ret;

	/* Configure the related display connection planes side by side split info in plane check insted of in update plane,
	 * because the writeback configure needs to use the dirty state.
	 */
	if (!sbs)
		dc->hw.display[hw_plane->fb.display_id].sbs_split_dirty = false;
	else if (sbs->mode == VS_SBS_SPLIT) {
		dc->hw.display[hw_plane->fb.display_id / HW_DISPLAY_2].sbs_split_dirty = true;
		dc->hw.display[(hw_plane->fb.display_id / HW_DISPLAY_2) + 1].sbs_split_dirty = true;
	} else
		dc->hw.display[hw_plane->fb.display_id].sbs_split_dirty = false;

	ret = check_plane_scale(state, plane_info);
	if (ret)
		return ret;

	if (plane_info->sid != HW_PLANE_NOT_SUPPORTED_SID) {
		ret = check_secure_buffer(state, dc);
		if (ret)
			return ret;
	}

	ret = check_y2r_changed(state);
	if (ret)
		return ret;

	if (plane_info->rcd_plane) {
		ret = check_rcd_mask(state, plane_info);
		if (ret)
			return ret;
	}

	return 0;
}

static bool vs_dc_plane_format_mode_support(struct device *dev, struct vs_plane *plane, u32 format,
					    u64 modifier)
{
	u8 tile_mode = 0;

	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	// Official DRM fourcc codes for PVRIC
	/* TODO(b/299950026): Check specific formats once we support more than one
	 * PVRIC format
	 */
	if (modifier == DRM_FORMAT_MOD_PVR_FBCDC_8x8_V14 ||
	    modifier == DRM_FORMAT_MOD_PVR_FBCDC_16x4_V14) {
		return true;
	}

	if ((modifier >> 56) != DRM_FORMAT_MOD_VENDOR_VS) {
		dev_err(dev, "%s: Unknown modifier (not VSI). modifier = 0x%llx\n", __func__,
			modifier);
		return false;
	}

	/* For 9x00, the current format mod check is mainly for
	 * PVRIC or DEC400 tile and format
	 */
	if ((modifier & DRM_FORMAT_MOD_VS_TYPE_MASK) >> 53 == DRM_FORMAT_MOD_VS_TYPE_PVRIC) {
		tile_mode = modifier & DRM_FORMAT_MOD_VS_DEC_TILE_MODE_MASK;
		switch (tile_mode) {
		case DRM_FORMAT_MOD_VS_DEC_TILE_8X8:
			switch (format) {
			case DRM_FORMAT_NV12:
			case DRM_FORMAT_NV21:
			case DRM_FORMAT_P010:
				return true;
			default:
				return false;
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
			case DRM_FORMAT_RGB565:
			case DRM_FORMAT_BGR565:
				return true;
			default:
				return false;
			}
			break;
		case DRM_FORMAT_MOD_VS_DEC_TILE_32X2:
			switch (format) {
			case DRM_FORMAT_ARGB16161616F:
			case DRM_FORMAT_ABGR16161616F:
				return true;
			default:
				return false;
			}
			break;
		default:
			dev_err(dev, "%s: Invalid PVRIC tile mode.\n", __func__);
			return false;
		}
	} else if ((modifier & DRM_FORMAT_MOD_VS_TYPE_MASK) >> 53 ==
		   DRM_FORMAT_MOD_VS_TYPE_COMPRESSED) {
		tile_mode = modifier & DRM_FORMAT_MOD_VS_DEC_TILE_MODE_MASK;
		switch (tile_mode) {
		case DRM_FORMAT_MOD_VS_DEC_TILE_8X8_UNIT2X2:
			switch (format) {
			case DRM_FORMAT_ARGB8888:
			case DRM_FORMAT_ABGR8888:
			case DRM_FORMAT_RGBA8888:
			case DRM_FORMAT_BGRA8888:
			case DRM_FORMAT_ARGB2101010:
			case DRM_FORMAT_ABGR2101010:
			case DRM_FORMAT_RGBA1010102:
			case DRM_FORMAT_BGRA1010102:
				return true;
			default:
				return false;
			}
			break;
		case DRM_FORMAT_MOD_VS_DEC_TILE_8X4_UNIT2X2:
			switch (format) {
			case DRM_FORMAT_ARGB16161616F:
			case DRM_FORMAT_ABGR16161616F:
				return true;
			default:
				return false;
			}
			break;
		default:
			dev_err(dev, "%s: Invalid DEC400 tile mode.\n", __func__);
			return false;
		}
	}

	return true;
}

const struct vs_plane_funcs dc_plane_funcs = {
	.update = vs_dc_update_plane,
#if IS_ENABLED(CONFIG_DEBUG_FS)
	.set_pattern = vs_dc_set_plane_pattern,
	.set_qos = vs_dc_set_plane_qos,
	.show_qos = vs_dc_show_plane_qos_status,
#endif /* CONFIG_DEBUG_FS */
	.disable = vs_dc_disable_plane,
	.check = vs_dc_check_plane,
	.format_mod_support = vs_dc_plane_format_mode_support,
};

/* FE power functions */

int dc_fe_power_get(struct device *dev, bool sync)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	int ret = 0, ret_tmp = 0;
	int i;

	WARN_ONCE(!sync, "Async fe power_get called from %ps\n", __builtin_return_address(0));
	DPU_ATRACE_BEGIN(__func__);

	for (i = 0; i < DC_FE_NUM; i++) {
		if (!dc->fe_dev[i])
			continue;

		if (sync)
			ret_tmp = pm_runtime_get_sync(dc->fe_dev[i]);
		else
			ret_tmp = pm_runtime_get(dc->fe_dev[i]);

		trace_disp_dc_fe_power_get(i, dc->fe_dev[i], sync, ret_tmp);

		if (unlikely(ret_tmp < 0))
			dev_err(dev, "fe%d_power_get: %ps: sync:%d ret:%d\n", i,
				__builtin_return_address(0), sync, ret_tmp);
		ret = ret ? ret : ret_tmp;
	}

	DPU_ATRACE_END(__func__);

	return ret;
}

int dc_fe_power_put(struct device *dev, bool sync)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	int ret = 0, ret_tmp = 0;
	int i;

	DPU_ATRACE_BEGIN(__func__);

	for (i = 0; i < DC_FE_NUM; i++) {
		if (!dc->fe_dev[i])
			continue;

		if (sync)
			ret_tmp = pm_runtime_put_sync(dc->fe_dev[i]);
		else
			ret_tmp = pm_runtime_put(dc->fe_dev[i]);

		trace_disp_dc_fe_power_put(i, dc->fe_dev[i], sync, ret_tmp);

		if (unlikely(ret_tmp < 0))
			dev_err(dev, "fe%d_power_put: %ps: sync:%d ret:%d\n", i,
				__builtin_return_address(0), sync, ret_tmp);
		ret = ret ? ret : ret_tmp;
	}

	DPU_ATRACE_END(__func__);

	return ret;
}

/* FE irq handling */

static void dc_handle_fe_interrupts(struct device *dev)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	struct dc_hw_interrupt_status status = { 0 };
	unsigned long *bus_errs;
	bool *hw_has_bus_errs;
	const char *bus_errs_trace_name;
	enum dc_hw_reset_pos reset_pos;
	int fe_idx = dc_get_fe_idx(dev);

	if (fe_idx < 0 || fe_idx >= DC_FE_NUM)
		return;

	if (fe_idx == 0) {
		dc_hw_get_fe0_interrupt(&dc->hw, &status);
		bus_errs = status.fe0_bus_errors;
		hw_has_bus_errs = &dc->hw.fe0_has_bus_errors;
		bus_errs_trace_name = "fe0_bus_errors";
		reset_pos = FE0_SW_RESET;
	} else {
		dc_hw_get_fe1_interrupt(&dc->hw, &status);
		bus_errs = status.fe1_bus_errors;
		hw_has_bus_errs = &dc->hw.fe1_has_bus_errors;
		bus_errs_trace_name = "fe1_bus_errors";
		reset_pos = FE1_SW_RESET;
	}

	if (!bitmap_empty(bus_errs, DC_HW_FE_BUS_ERROR_COUNT)) {
		dev_warn(dev, "handle_fe_interrupts: fe%d_bus_errors: %#lx\n", fe_idx, bus_errs[0]);
		trace_disp_bus_err_irqs(bus_errs_trace_name, bus_errs[0]);
		*hw_has_bus_errs = true;
	}

	trace_disp_frame_irqs(0, 0, 0, status.layer_frm_done, 0, 0);
	trace_disp_reset_irqs(status.layer_reset_done, status.reset_status[reset_pos], 0, 0);

	if (status.reset_status[reset_pos])
		dc->hw.reset_status[reset_pos] = status.reset_status[reset_pos];
}

static irqreturn_t dc_fe_isr(int irq, void *data)
{
	struct device *dev = data;
	struct vs_dc *dc = dev_get_drvdata(dev);

	spin_lock(&dc->int_lock);
	dc_handle_fe_interrupts(dev);
	spin_unlock(&dc->int_lock);

	return IRQ_HANDLED;
}

/* FE common probe/bind functions */

static int dc_fe_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm_dev = data;
	struct vs_drm_private *priv = drm_dev->dev_private;
	struct vs_dc *dc = dev_get_drvdata(priv->dc_dev);
	const struct vs_dc_info *dc_info;
	struct vs_plane *plane;
	struct drm_plane *drm_plane, *tmp;
	const struct vs_plane_info *plane_info_array;
	int plane_num;
	int plane_offset;
	int i, j, ret;
	int fe_idx = dc_get_fe_idx(dev);
	u32 max_width = 0, max_height = 0;
	u32 min_width = 0xffff, min_heigth = 0xffff;
	enum drm_plane_type plane_type;
	u32 possible_crtcs;

	if (fe_idx < 0 || fe_idx >= DC_FE_NUM)
		return (fe_idx < 0) ? fe_idx : -EINVAL;

	if (!drm_dev || !dc) {
		dev_err(dev, "devices are not created.\n");
		return -ENODEV;
	}

	dc_component_enable_irqs(dev, &dc->fe_irq_info[fe_idx]);

	if (fe_idx == 0)
		ret = dc_fe0_hw_init(&dc->hw);
	else
		ret = dc_fe1_hw_init(&dc->hw);

	if (ret < 0) {
		dev_err(dev, "Failed to initialize fe%d hardware.\n", fe_idx);
		return ret;
	}

	dc_info = dc->hw.info;
	if (fe_idx == 0) {
		plane_info_array = dc_info->planes_fe0;
		plane_num = dc_info->plane_fe0_num;
		plane_offset = 0;
	} else {
		plane_info_array = dc_info->planes_fe1;
		plane_num = dc_info->plane_fe1_num;
		plane_offset = dc_info->plane_fe0_num;
	}

	for (i = 0; i < plane_num; i++) {
		j = i + plane_offset;
		const struct vs_plane_info *plane_info = &plane_info_array[i];
		struct dc_hw_plane *hw_plane = &dc->hw.plane[j];

		/*
		 * Convert the plane type from PRIMARY to OVERLAY when there is no matching CRTC.
		 * This will prevent the drm_mode_config_validate function from logging a warning
		 * on an unbalanced number of PRIMARY planes and CRTCs.
		 */
		plane_type = plane_info->type;
		if ((plane_type == DRM_PLANE_TYPE_PRIMARY) && !(dc->crtc[plane_info->crtc_id]))
			plane_type = DRM_PLANE_TYPE_OVERLAY;

		possible_crtcs = dc->crtc_mask;

		if (plane_info->rcd_plane) {
			/*
			 * rcd planes are bound to a single outctrl,
			 * skip creation if not created
			 */
			if (!dc->crtc[plane_info->crtc_id])
				continue;
			possible_crtcs = 1 << plane_info->crtc_id;
		}

		plane = vs_plane_create(hw_plane, drm_dev, dc_info, plane_type, j, possible_crtcs,
					&dc_plane_funcs);
		if (!plane) {
			dev_err(dev, "Failed to create plane.\n");
			ret = -ENOMEM;
			goto err_cleanup_planes;
		}

		plane->id = j;
		dc->planes[j].base = plane;
		dc->planes[j].id = plane_info->id;

		plane->dev = dev;
		plane->funcs = &dc_plane_funcs;

		if ((plane_type == DRM_PLANE_TYPE_PRIMARY) && (dc->crtc[plane_info->crtc_id]))
			dc->crtc[plane_info->crtc_id]->base.primary = &plane->base;

		if ((plane_info->type == DRM_PLANE_TYPE_CURSOR) &&
		    (dc->crtc[plane_info->crtc_id])) {
			dc->crtc[plane_info->crtc_id]->base.cursor = &plane->base;

			drm_dev->mode_config.cursor_width = plane_info->max_width;
			drm_dev->mode_config.cursor_height = plane_info->max_height;
		}

		min_width = (min_width > plane_info->min_width) ? plane_info->min_width : min_width;
		min_heigth = (min_heigth > plane_info->min_height) ? plane_info->min_height :
								     min_heigth;
		/*
		 * Note: these values are used for multiple independent things:
		 * hw display mode filtering, plane buffer sizes, writeback buffer size ...
		 * Use the combined maximum values here to cover all use cases, and do more
		 * specific checking in the respective code paths.
		 */
		max_width = (max_width < plane_info->max_width) ? plane_info->max_width : max_width;
		max_height = (max_height < plane_info->max_height) ? plane_info->max_height :
								     max_height;
	}

	drm_dev->mode_config.min_width = (min_width > drm_dev->mode_config.min_width) ?
						 drm_dev->mode_config.min_width :
						 min_width;
	drm_dev->mode_config.min_height = (min_heigth > drm_dev->mode_config.min_height) ?
						  drm_dev->mode_config.min_height :
						  min_heigth;
	drm_dev->mode_config.max_width = (max_width < drm_dev->mode_config.max_width) ?
						 drm_dev->mode_config.max_width :
						 max_width;
	drm_dev->mode_config.max_height = (max_height < drm_dev->mode_config.max_height) ?
						  drm_dev->mode_config.max_height :
						  max_height;

	vs_drm_update_alignment(drm_dev, dc_info->pitch_alignment, dc_info->addr_alignment);

	return 0;

err_cleanup_planes:
	list_for_each_entry_safe(drm_plane, tmp, &drm_dev->mode_config.plane_list, head)
		if (drm_plane->possible_crtcs & dc->crtc_mask)
			vs_plane_destroy(drm_plane);

	return ret;
}

static void dc_fe_unbind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm_dev = data;
	struct drm_plane *drm_plane, *tmp;
	struct vs_dc *dc = dev_get_drvdata(dev);
	int fe_idx = dc_get_fe_idx(dev);

	if (fe_idx < 0 || fe_idx >= DC_FE_NUM)
		return;

	dc_component_disable_irqs(dev, &dc->fe_irq_info[fe_idx]);

	if (fe_idx == 0)
		dc_fe0_hw_deinit(&dc->hw);
	else
		dc_fe1_hw_deinit(&dc->hw);

	list_for_each_entry_safe(drm_plane, tmp, &drm_dev->mode_config.plane_list, head)
		if (drm_plane->possible_crtcs & dc->crtc_mask)
			vs_plane_destroy(drm_plane);
}

static const struct component_ops dc_fe_component_ops = {
	.bind = dc_fe_bind,
	.unbind = dc_fe_unbind,
};

static int dc_fe_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vs_dc *dc = dev_get_drvdata(dev->parent);
	u32 fe_idx;
	int ret;

	if (!dc)
		return -ENODEV;

	ret = of_property_read_u32(dev->of_node, "fe_id", &fe_idx);
	if (ret) {
		dev_err(dev, "failed to read fe_id property\n");
		return ret;
	}

	if (fe_idx >= DC_FE_NUM) {
		dev_err(dev, "invalid fe index: %u\n", fe_idx);
		return -EINVAL;
	}

	dev_set_drvdata(dev, dc);
	dc->fe_dev[fe_idx] = dev;

	ret = dc_component_parse_irqs(dev, &dc->fe_irq_info[fe_idx], dc_fe_isr);
	if (ret)
		return ret;

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		dev_err(dev, "runtime pm enabled but failed to add disable action: %d\n", ret);

	return component_add(dev, &dc_fe_component_ops);
}

static void dc_fe_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vs_dc *dc = dev_get_drvdata(dev);
	int fe_idx;

	if (dc) {
		fe_idx = dc_get_fe_idx(dev);
		if (fe_idx >= 0)
			dc->fe_dev[fe_idx] = NULL;
	}

	component_del(dev, &dc_fe_component_ops);
	dev_set_drvdata(dev, NULL);
}

static const struct of_device_id fe_driver_dt_match[] = {
	{ .compatible = "verisilicon,dpu_fe" },
	{},
};
MODULE_DEVICE_TABLE(of, fe_driver_dt_match);

#if IS_ENABLED(CONFIG_PM_SLEEP)
static int dc_fe_pm_runtime_suspend(struct device *dev)
{
	int fe_idx = dc_get_fe_idx(dev);

	trace_disp_dc_fe_disable(fe_idx);

	return 0;
}

static int dc_fe_pm_runtime_resume(struct device *dev)
{
	int fe_idx = dc_get_fe_idx(dev);

	trace_disp_dc_fe_enable(fe_idx);

	return 0;
}

static const struct dev_pm_ops dc_fe_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(dc_fe_pm_runtime_suspend, dc_fe_pm_runtime_resume, NULL)
};
#endif

struct platform_driver dc_fe_platform_driver = {
	.probe = dc_fe_probe,
	.remove = dc_fe_remove,
	.driver = {
		.name = "vs-dc-fe",
		.of_match_table = of_match_ptr(fe_driver_dt_match),
#if IS_ENABLED(CONFIG_PM_SLEEP)
		.pm = &dc_fe_pm_ops,
#endif
	},
};

MODULE_DESCRIPTION("VeriSilicon FE Driver");
MODULE_LICENSE("GPL v2");
