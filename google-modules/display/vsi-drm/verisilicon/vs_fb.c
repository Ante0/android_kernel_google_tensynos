// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/module.h>

#include <drm/vs_drm_fourcc.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "vs_fb.h"
#include "vs_gem.h"

#define fourcc_mod_vs_get_type(val) (((val) & DRM_FORMAT_MOD_VS_TYPE_MASK) >> 53)

static struct drm_framebuffer_funcs vs_fb_funcs = {
	.create_handle = drm_gem_fb_create_handle,
	.destroy = drm_gem_fb_destroy,
	.dirty = drm_atomic_helper_dirtyfb,
};

inline uint64_t vs_fb_parse_fourcc_modifier(uint64_t mode_cmd_modifier)
{
	switch (mode_cmd_modifier) {
	case DRM_FORMAT_MOD_PVR_FBCDC_16x4_V14:
		return fourcc_mod_vs_pvric_code(DRM_FORMAT_MOD_VS_DEC_TILE_16X4,
						DRM_FORMAT_MOD_VS_DEC_LOSSLESS);
		break;
	case DRM_FORMAT_MOD_PVR_FBCDC_8x8_V14:
		return fourcc_mod_vs_pvric_code(DRM_FORMAT_MOD_VS_DEC_TILE_8X8,
						DRM_FORMAT_MOD_VS_DEC_LOSSLESS);
		break;
	default:
		return mode_cmd_modifier;
	}
}

bool vs_fb_is_shallow(uint64_t modifier)
{
	return fourcc_mod_vs_get_type(vs_fb_parse_fourcc_modifier(modifier)) ==
	       DRM_FORMAT_MOD_VS_TYPE_SHALLOW;
}

static struct drm_framebuffer *vs_fb_alloc(struct drm_device *dev,
					   const struct drm_mode_fb_cmd2 *mode_cmd,
					   struct vs_gem_object **obj, unsigned int num_planes)
{
	struct drm_framebuffer *fb;
	int ret, i;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return ERR_PTR(-ENOMEM);

	drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd);

	for (i = 0; i < num_planes; i++) {
		if (vs_fb_is_shallow(mode_cmd->modifier[i]))
			continue;
		fb->obj[i] = &obj[i]->base;
	}

	ret = drm_framebuffer_init(dev, fb, &vs_fb_funcs);
	if (ret) {
		dev_err(dev->dev, "Failed to initialize framebuffer: %d\n", ret);
		kfree(fb);
		return ERR_PTR(ret);
	}

	return fb;
}

struct drm_framebuffer *vs_fb_create(struct drm_device *dev, struct drm_file *file_priv,
				     const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_framebuffer *fb;
	const struct drm_format_info *info;
	struct vs_gem_object *objs[MAX_NUM_PLANES];
	struct drm_gem_object *obj;
	unsigned int height, size;
	unsigned char i, num_planes;
	uint64_t vs_modifier;
	int ret = 0;

	info = drm_get_format_info(dev, mode_cmd);
	if (!info)
		return ERR_PTR(-EINVAL);

	num_planes = info->num_planes;
	if (num_planes > MAX_NUM_PLANES)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < num_planes; i++) {
		if (vs_fb_is_shallow(mode_cmd->modifier[i]))
			continue;

		obj = drm_gem_object_lookup(file_priv, mode_cmd->handles[i]);
		if (!obj) {
			dev_err(dev->dev, "Failed to lookup GEM object.\n");
			ret = -ENXIO;
			goto err;
		}

		vs_modifier = vs_fb_parse_fourcc_modifier(mode_cmd->modifier[i]);
		if (!((fourcc_mod_vs_get_type(vs_modifier) == DRM_FORMAT_MOD_VS_TYPE_PVRIC) &&
		      (vs_modifier & DRM_FORMAT_MOD_VS_DEC_LOSSY))) {
			height = drm_format_info_plane_height(info, mode_cmd->height, i);
			size = height * mode_cmd->pitches[i] + mode_cmd->offsets[i];

			if (obj->size < size) {
				drm_gem_object_put(obj);
				ret = -EINVAL;
				goto err;
			}
		}

		objs[i] = to_vs_gem_object(obj);
	}

	fb = vs_fb_alloc(dev, mode_cmd, objs, i);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		goto err;
	}

	return fb;

err:
	for (; i > 0; i--) {
		if (vs_fb_is_shallow(mode_cmd->modifier[i - 1]))
			continue;
		drm_gem_object_put(&objs[i - 1]->base);
	}

	return ERR_PTR(ret);
}

struct vs_gem_object *vs_fb_get_gem_obj(struct drm_framebuffer *fb, unsigned char plane)
{
	if (plane > MAX_NUM_PLANES)
		return NULL;

	return to_vs_gem_object(fb->obj[plane]);
}

dma_addr_t vs_fb_get_dma_addr(struct drm_framebuffer *fb, unsigned char plane)
{
	struct vs_gem_object *vs_obj;

	if (plane > MAX_NUM_PLANES)
		return 0;

	if (vs_fb_is_shallow(fb->modifier))
		return 0;

	vs_obj = to_vs_gem_object(fb->obj[plane]);

	return vs_obj->iova + fb->offsets[plane];
}

static const struct drm_format_info vs_formats[] = {
	{ .format = DRM_FORMAT_NV12,
	  .depth = 0,
	  .num_planes = 2,
	  .char_per_block = { 20, 40, 0 },
	  .block_w = { 4, 4, 0 },
	  .block_h = { 4, 4, 0 },
	  .hsub = 2,
	  .vsub = 2,
	  .is_yuv = true },
	{ .format = DRM_FORMAT_YUV444,
	  .depth = 0,
	  .num_planes = 3,
	  .char_per_block = { 20, 20, 20 },
	  .block_w = { 4, 4, 4 },
	  .block_h = { 4, 4, 4 },
	  .hsub = 1,
	  .vsub = 1,
	  .is_yuv = true },
	{ .format = DRM_FORMAT_RGB565_A8,
	  .depth = 0,
	  .num_planes = 1,
	  .char_per_block = { 3, 0, 0 },
	  .block_w = { 1, 0, 0 },
	  .block_h = { 1, 0, 0 },
	  .hsub = 1,
	  .vsub = 1,
	  .has_alpha = true },
	{ .format = DRM_FORMAT_BGR565_A8,
	  .depth = 0,
	  .num_planes = 1,
	  .char_per_block = { 3, 0, 0 },
	  .block_w = { 1, 0, 0 },
	  .block_h = { 1, 0, 0 },
	  .hsub = 1,
	  .vsub = 1,
	  .has_alpha = true },
	{ .format = DRM_FORMAT_RGB888, /* RGB888-planer */
	  .num_planes = 3,
	  .cpp = { 1, 1, 1 },
	  .hsub = 1,
	  .vsub = 1 },
	{ .format = DRM_FORMAT_YUV420_10BIT,
	  .depth = 0,
	  .num_planes = 2,
	  .char_per_block = { 4, 8, 0 },
	  .block_w = { 3, 3, 0 },
	  .block_h = { 1, 1, 0 },
	  .hsub = 2,
	  .vsub = 2,
	  .is_yuv = true }
};

static const struct drm_format_info *vs_lookup_format_info(const struct drm_format_info formats[],
							   int num_formats, u32 format)
{
	int i;

	for (i = 0; i < num_formats; i++) {
		if (formats[i].format == format)
			return &formats[i];
	}

	return NULL;
}

const struct drm_format_info *vs_get_format_info(const struct drm_mode_fb_cmd2 *cmd)
{
	if (fourcc_mod_vs_get_type(cmd->modifier[0]) == DRM_FORMAT_MOD_VS_TYPE_CUSTOM)
		return vs_lookup_format_info(vs_formats, ARRAY_SIZE(vs_formats), cmd->pixel_format);
	else
		return NULL;
}

int vs_get_fbc_offset_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_vs_pvric_offset *args = data;
	const struct drm_format_info *info;
	struct drm_gem_object *obj;
	struct vs_gem_object *objs[MAX_NUM_PLANES];
	u64 base_addr = 0;
	u8 i, num_planes;
	int ret = 0;

	info = drm_format_info(args->format);
	if (!info)
		return -EINVAL;

	num_planes = info->num_planes;
	if (num_planes > MAX_NUM_PLANES)
		return -EINVAL;

	for (i = 0; i < num_planes; i++) {
		obj = drm_gem_object_lookup(file_priv, args->handles[i]);
		if (!obj) {
			dev_err(dev->dev, "Failed to lookup GEM object.\n");
			ret = -ENXIO;
			goto err;
		}

		objs[i] = to_vs_gem_object(obj);

		base_addr = ALIGN(objs[i]->iova + args->header_size[i], 256);
		args->offsets[i] = base_addr - objs[i]->iova;
	}

	return 0;

err:
	for (; i > 0; i--)
		drm_gem_object_put(&objs[i - 1]->base);

	return ret;
}
