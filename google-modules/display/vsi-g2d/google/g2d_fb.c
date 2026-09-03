// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */

#include <drm/drm.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "g2d_fb.h"
#include "g2d_gem.h"

static struct drm_framebuffer_funcs g2d_fb_funcs = {
	.create_handle = drm_gem_fb_create_handle,
	.destroy = drm_gem_fb_destroy,
	.dirty = drm_atomic_helper_dirtyfb,
};

static struct drm_framebuffer *g2d_fb_alloc(struct drm_device *dev,
					    const struct drm_mode_fb_cmd2 *mode_cmd,
					    struct g2d_bo **obj, unsigned int num_planes)
{
	struct drm_framebuffer *fb;
	int ret, i;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return ERR_PTR(-ENOMEM);

	drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd);

	for (i = 0; i < num_planes; i++)
		fb->obj[i] = &obj[i]->gem;

	ret = drm_framebuffer_init(dev, fb, &g2d_fb_funcs);
	if (ret) {
		dev_err(dev->dev, "Failed to initialize framebuffer: %d\n", ret);
		kfree(fb);
		return ERR_PTR(ret);
	}

	return fb;
}

struct drm_framebuffer *g2d_fb_create(struct drm_device *dev, struct drm_file *file_priv,
				      const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_framebuffer *fb;
	const struct drm_format_info *info;
	struct g2d_bo *objs[MAX_NUM_PLANES];
	struct drm_gem_object *obj;
	unsigned int height, size;
	unsigned char i, num_planes;
	int ret = 0;

	info = drm_get_format_info(dev, mode_cmd);
	if (!info)
		return ERR_PTR(-EINVAL);

	num_planes = info->num_planes;
	if (num_planes > MAX_NUM_PLANES)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < num_planes; i++) {
		obj = drm_gem_object_lookup(file_priv, mode_cmd->handles[i]);
		if (!obj) {
			dev_err(dev->dev, "Failed to lookup GEM object.\n");
			ret = -ENXIO;
			goto err;
		}

		/*
		 * TODO(b/279522245) This assumption doesn't hold true when compression is enabled.
		 * Fix when implementing PVRIC.
		 */
		height = drm_format_info_plane_height(info, mode_cmd->height, i);
		size = height * mode_cmd->pitches[i] + mode_cmd->offsets[i];

		if (obj->size < size) {
			dev_err(dev->dev,
				"plane size error! plane size is: %u, gem object size is %zu", size,
				obj->size);
			drm_gem_object_put(obj);
			ret = -EINVAL;
			goto err;
		}

		objs[i] = to_g2d_buffer_object(obj);
	}

	fb = g2d_fb_alloc(dev, mode_cmd, objs, i);
	if (IS_ERR(fb)) {
		dev_err(dev->dev, "%s, failed to allocate framebuffer!", __func__);
		ret = PTR_ERR(fb);
		goto err;
	}

	return fb;

err:
	for (; i > 0; i--)
		drm_gem_object_put(&objs[i - 1]->gem);

	return ERR_PTR(ret);
}
