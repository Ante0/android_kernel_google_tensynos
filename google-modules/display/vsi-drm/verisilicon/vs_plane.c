// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/file.h>
#include <linux/seq_file.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>

#include <drm/vs_drm_fourcc.h>
#include "vs_crtc.h"
#include "vs_fb.h"
#include "vs_gem.h"
#include "vs_plane.h"
#include "vs_dc_info.h"
#include "vs_dc_drm_property.h"
#include "vs_trace.h"

#define fourcc_mod_vs_get_type(val) (((val) & DRM_FORMAT_MOD_VS_TYPE_MASK) >> 53)

static void vs_plane_atomic_destroy_state(struct drm_plane *plane, struct drm_plane_state *state)
{
	struct vs_plane_state *vs_plane_state = to_vs_plane_state(state);
	struct vs_plane *vs_plane = to_vs_plane(plane);

	/* destroy standard properties */
	__drm_atomic_helper_plane_destroy_state(state);

	/* destroy custom properties */
	drm_property_blob_put(vs_plane_state->watermark);
	drm_property_blob_put(vs_plane_state->y2r_coef);
	drm_property_blob_put(vs_plane_state->lut_3d);
	drm_property_blob_put(vs_plane_state->clear);

	if (vs_plane_state->fb_ext)
		drm_framebuffer_put(vs_plane_state->fb_ext);

	vs_dc_destroy_drm_properties(vs_plane_state->drm_states, &vs_plane->properties);

	kfree(vs_plane_state);
}

void vs_plane_destroy(struct drm_plane *plane)
{
	struct vs_plane *vs_plane = to_vs_plane(plane);

	/* cleanup plane variable */
	drm_plane_cleanup(plane);

	kfree(vs_plane);
}

static void vs_plane_reset(struct drm_plane *plane)
{
	struct vs_plane_state *state;
	struct vs_plane *vs_plane = to_vs_plane(plane);
	u32 i;

	if (plane->state) {
		plane->funcs->atomic_destroy_state(plane, plane->state);
		plane->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return;

	/* reset standard properties */
	__drm_atomic_helper_plane_reset(plane, &state->base);

	/*reset custom properties */
	bitmap_fill(state->changed, VS_PLANE_CHANGED_MAX);
	state->blend_id = vs_plane->id;
	state->dma_sram_size = 0;
	state->scl_sram_size = 0;
	state->pre_swap_crtc = NULL;

	for (i = 0; i < vs_plane->properties.num; i++) {
		state->drm_states[i].proto = vs_plane->properties.items[i].proto;
		state->drm_states[i].is_changed = true;
	}

	/* reset internal state variables */
	memset(&state->status, 0, sizeof(state->status));
}

static void _vs_plane_duplicate_blob(struct vs_plane_state *state, struct vs_plane_state *ori_state)
{
	state->watermark = ori_state->watermark;
	state->y2r_coef = ori_state->y2r_coef;
	state->lut_3d = ori_state->lut_3d;
	state->clear = ori_state->clear;
	state->fb_ext = ori_state->fb_ext;

	if (state->watermark)
		drm_property_blob_get(state->watermark);
	if (state->y2r_coef)
		drm_property_blob_get(state->y2r_coef);
	if (state->lut_3d)
		drm_property_blob_get(state->lut_3d);
	if (state->clear)
		drm_property_blob_get(state->clear);
	if (state->fb_ext)
		drm_framebuffer_get(state->fb_ext);
}

static int _vs_plane_set_property_blob_from_id(struct drm_device *dev,
					       struct drm_property_blob **blob, uint64_t blob_id,
					       size_t expected_size, u32 bit_index,
					       unsigned long *bitmap)
{
	struct drm_property_blob *new_blob = NULL;
	bool data_changed = false;

	if (blob_id) {
		new_blob = drm_property_lookup_blob(dev, blob_id);
		if (new_blob == NULL)
			return -EINVAL;

		if (new_blob->length != expected_size) {
			drm_property_blob_put(new_blob);
			return -EINVAL;
		}
	}

	data_changed = drm_property_replace_blob(blob, new_blob);
	if (bitmap && data_changed)
		set_bit(bit_index, bitmap);

	drm_property_blob_put(new_blob);

	return 0;
}

static struct drm_plane_state *vs_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct vs_plane_state *ori_state;
	struct vs_plane_state *state;
	const struct vs_plane *vs_plane = to_vs_plane(plane);

	if (WARN_ON(!plane->state))
		return NULL;

	ori_state = to_vs_plane_state(plane->state);
	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &state->base);

	state->blend_id = ori_state->blend_id;
	state->pre_swap_crtc = ori_state->pre_swap_crtc;

	bitmap_clear(state->changed, 0, VS_PLANE_CHANGED_MAX);

	_vs_plane_duplicate_blob(state, ori_state);
	memcpy(&state->status, &ori_state->status, sizeof(ori_state->status));

#ifdef CONFIG_DEBUG_FS
	state->qos = ori_state->qos;
#endif

	/* dc properties */
	vs_dc_duplicate_drm_properties(state->drm_states, ori_state->drm_states,
				       &vs_plane->properties);
	return &state->base;
}

static int _vs_plane_set_fb_from_id(struct drm_device *dev, struct drm_framebuffer **fb,
				uint64_t val)
{
	const uint32_t fb_id = lower_32_bits(val);

	if (unlikely(!fb))
		return -EINVAL;

	if (*fb)
		drm_framebuffer_put(*fb);
	*fb = drm_framebuffer_lookup(dev, NULL, fb_id);

	return 0;
}

static int vs_plane_atomic_set_property(struct drm_plane *plane, struct drm_plane_state *state,
					struct drm_property *property, uint64_t val)
{
	struct drm_device *dev = plane->dev;
	struct vs_plane *vs_plane = to_vs_plane(plane);
	struct vs_plane_state *vs_plane_state = to_vs_plane_state(state);
	int ret = 0;

	if (property == vs_plane->ext_layer_prop) {
		ret = _vs_plane_set_fb_from_id(plane->dev, &vs_plane_state->fb_ext, val);
	} else if (property == vs_plane->watermark_prop) {
		ret = _vs_plane_set_property_blob_from_id(dev, &vs_plane_state->watermark, val,
							  sizeof(struct drm_vs_watermark), 0, NULL);
	} else if (property == vs_plane->y2r_prop) {
		ret = _vs_plane_set_property_blob_from_id(dev, &vs_plane_state->y2r_coef, val,
							  sizeof(struct drm_vs_y2r_config),
							  VS_PLANE_CHANGED_Y2R,
							  vs_plane_state->changed);
	} else if (property == vs_plane->lut_3d_prop) {
		ret = _vs_plane_set_property_blob_from_id(dev, &vs_plane_state->lut_3d, val,
							  sizeof(struct drm_vs_data_block),
							  VS_PLANE_CHANGED_LUT_3D,
							  vs_plane_state->changed);
	} else if (property == vs_plane->clear_prop) {
		ret = _vs_plane_set_property_blob_from_id(dev, &vs_plane_state->clear, val,
							  sizeof(struct drm_vs_color),
							  VS_PLANE_CHANGED_CLEAR,
							  vs_plane_state->changed);
	} else {
		/* dc property */
		ret = vs_dc_set_drm_property(dev, vs_plane_state->drm_states, &vs_plane->properties,
					     property, val);
	}

	drm_dbg_state(dev, "[PLANE:%u:%s]: SET custom property %s value %llu\n", plane->base.id,
		      plane->name, property->name, val);
	trace_disp_set_property(plane->name, property->name, val);

	return ret;
}

static int vs_plane_atomic_get_property(struct drm_plane *plane,
					const struct drm_plane_state *state,
					struct drm_property *property, uint64_t *val)
{
	struct vs_plane *vs_plane = to_vs_plane(plane);
	const struct vs_plane_state *vs_plane_state =
		container_of(state, const struct vs_plane_state, base);

	if (property == vs_plane->ext_layer_prop)
		*val = (vs_plane_state->fb_ext) ? vs_plane_state->fb_ext->base.id : 0;
	else if (property == vs_plane->watermark_prop)
		*val = (vs_plane_state->watermark) ? vs_plane_state->watermark->base.id : 0;
	else if (property == vs_plane->y2r_prop)
		*val = (vs_plane_state->y2r_coef) ? vs_plane_state->y2r_coef->base.id : 0;
	else if (property == vs_plane->lut_3d_prop)
		*val = (vs_plane_state->lut_3d) ? vs_plane_state->lut_3d->base.id : 0;
	else if (property == vs_plane->clear_prop)
		*val = (vs_plane_state->clear) ? vs_plane_state->clear->base.id : 0;
	else
		return vs_dc_get_drm_property(vs_plane_state->drm_states, &vs_plane->properties,
					      property, val);

	return 0;
}

static bool vs_plane_format_mod_supported(struct drm_plane *plane, u32 format, u64 modifier)
{
	struct vs_plane *vs_plane = to_vs_plane(plane);

	if (WARN_ON(modifier == DRM_FORMAT_MOD_INVALID))
		return false;

	if (vs_plane->funcs && vs_plane->funcs->format_mod_support)
		return vs_plane->funcs->format_mod_support(vs_plane->dev, vs_plane, format,
							   modifier);
	else
		return true;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int vs_plane_pattern_show(struct seq_file *s, void *data)
{
	struct drm_plane *plane = s->private;
	struct vs_plane_state *plane_state = to_vs_plane_state(plane->state);

	seq_printf(s, "plane[%u]: %s\n", plane->base.id, plane->name);

	seq_printf(s, "\tenable: %d\n", plane_state->pattern.enable);
	seq_printf(s, "\tmode setting instructions:\n\t\tmode\t\tid\n\t\tPURE_CLR\
\t0\n\t\tCLR_BAR_H\t1\n\t\tCLR_BAR_V\t2\n\t\tRMAP_H\t\t3\n\t\tRMAP_V\t\t4\n\t\
\tBLK_WHT_H\t5\n\t\tBLK_WHT_V\t6\n\t\tBLK_WHT_S\t7\n\t\tBORDER\t\t8\n\t\t\
CURSOR\t\t9\n");
	seq_printf(s, "\tmode-id = %d\n", plane_state->pattern.mode);
	seq_printf(s, "\tcurser = {%d,%d}\n", plane_state->pattern.rect.x,
		   plane_state->pattern.rect.y);
	seq_printf(s, "\twidth = %d\n", plane_state->pattern.rect.w);
	seq_printf(s, "\theight = %d\n", plane_state->pattern.rect.h);
	seq_printf(s, "\tcolor = 0x%llx\n", plane_state->pattern.color);

	return 0;
}

static int vs_plane_pattern_open(struct inode *inode, struct file *file)
{
	return single_open(file, vs_plane_pattern_show, inode->i_private);
}

static ssize_t vs_plane_pattern_write(struct file *file, const char __user *ubuf, size_t len,
				      loff_t *offp)
{
	struct seq_file *s = file->private_data;
	struct drm_plane *plane = s->private;
	struct drm_device *drm_dev = plane->dev;
	struct vs_plane *vs_plane = to_vs_plane(plane);
	struct vs_plane_state *plane_state = to_vs_plane_state(plane->state);
	struct vs_drm_private *priv = NULL;
	char buf[96], *cur = buf;
	unsigned long value;
	unsigned long long color_val;

	if (!drm_dev)
		return -ENXIO;

	priv = drm_dev->dev_private;
	if (!priv || !priv->dc_dev)
		return -ENXIO;

	if (!vs_plane->funcs->set_pattern)
		return -EINVAL;

	if (len > sizeof(buf) - 1)
		return -EINVAL;

	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;

	buf[len] = '\0';

	cur = strstr(buf, "enable:");
	if (cur) {
		cur += 7;
		value = simple_strtoul(cur, NULL, 10);
		plane_state->pattern.enable = value;
	} else {
		return -EINVAL;
	}

	cur = strstr(buf, "mode:");
	if (cur) {
		cur += 5;
		value = simple_strtoul(cur, NULL, 10);
		plane_state->pattern.mode = value;
	}

	cur = strstr(buf, "size.x:");
	if (cur) {
		cur += 7;
		value = simple_strtoul(cur, NULL, 10);
		plane_state->pattern.rect.x = value;
	}

	cur = strstr(buf, "size.y:");
	if (cur) {
		cur += 7;
		value = simple_strtoul(cur, NULL, 10);
		plane_state->pattern.rect.y = value;
	}

	cur = strstr(buf, "size.w:");
	if (cur) {
		cur += 7;
		value = simple_strtoul(cur, NULL, 10);
		plane_state->pattern.rect.w = value;
	}

	cur = strstr(buf, "size.h:");
	if (cur) {
		cur += 7;
		value = simple_strtoul(cur, NULL, 10);
		plane_state->pattern.rect.h = value;
	}

	cur = strstr(buf, "color:0x");
	if (cur) {
		cur += 8;
		if (kstrtoull(cur, 16, &color_val))
			return -EINVAL;

		plane_state->pattern.color = color_val;
	}

	vs_plane->funcs->set_pattern(priv->dc_dev, vs_plane);

	return len;
}

static int vs_plane_qos_show(struct seq_file *s, void *data)
{
	struct drm_plane *plane = s->private;
	struct vs_plane *vs_plane = to_vs_plane(plane);

	if (vs_plane->funcs->show_qos)
		return vs_plane->funcs->show_qos(s);

	return 0;
}

static int vs_plane_qos_open(struct inode *inode, struct file *file)
{
	return single_open(file, vs_plane_qos_show, inode->i_private);
}

static ssize_t vs_plane_qos_write(struct file *file, const char __user *ubuf, size_t len,
				  loff_t *offp)
{
	struct seq_file *s = file->private_data;
	struct drm_plane *plane = s->private;
	struct vs_plane *vs_plane = to_vs_plane(plane);

	if (vs_plane->funcs->set_qos)
		vs_plane->funcs->set_qos(vs_plane->base.dev->dev, vs_plane, ubuf, len);

	return len;
}

static const struct file_operations vs_plane_qos_fops = {
	.open = vs_plane_qos_open,
	.read = seq_read,
	.write = vs_plane_qos_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations vs_plane_pattern_fops = {
	.open = vs_plane_pattern_open,
	.read = seq_read,
	.write = vs_plane_pattern_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int vs_plane_debugfs_init(struct drm_plane *plane)
{
	struct vs_plane *vs_plane = to_vs_plane(plane);
	char *name;

	name = kasprintf(GFP_KERNEL, "plane-%d", plane->index);
	if (!name)
		return 0;

	vs_plane->debugfs_entry = debugfs_create_dir(name, plane->dev->primary->debugfs_root);

	kfree(name);

	debugfs_create_file("pattern", 0644, vs_plane->debugfs_entry, plane,
			    &vs_plane_pattern_fops);
	debugfs_create_file("QOS", 0644, vs_plane->debugfs_entry, plane, &vs_plane_qos_fops);

	return 0;
}
#else
static int vs_plane_debugfs_init(struct drm_plane *plane)
{
	return 0;
}
#endif /* CONFIG_DEBUG_FS */

static void vs_plane_atomic_print_state(struct drm_printer *p,
					const struct drm_plane_state *plane_state)
{
	const struct vs_plane_state *vs_plane_state = to_vs_plane_state(plane_state);
	struct drm_plane *plane = vs_plane_state->base.plane;
	struct vs_plane *vs_plane = to_vs_plane(plane);

	if (plane->blend_mode_property)
		drm_printf(p, "\t%s=%d\n", plane->blend_mode_property->name,
			   plane_state->pixel_blend_mode);

	if (plane->alpha_property)
		drm_printf(p, "\t%s=%d\n", plane->alpha_property->name, plane_state->alpha);

	drm_printf(p, "\tblend_id = %u\n", vs_plane_state->blend_id);
	drm_printf(p, "\tdma_sram_size = %u\n", vs_plane_state->dma_sram_size);
	drm_printf(p, "\tscl_sram_size = %u\n", vs_plane_state->scl_sram_size);

	if (vs_plane->ext_layer_prop)
		drm_printf(p, "\t%s=%d\n", vs_plane->ext_layer_prop->name,
			   vs_plane_state->fb_ext ? vs_plane_state->fb_ext->base.id : 0);

	if (vs_plane->watermark_prop)
		drm_printf(p, "\t%s=%d\n", vs_plane->watermark_prop->name,
			   vs_plane_state->watermark ? vs_plane_state->watermark->base.id : 0);

	if (vs_plane->y2r_prop)
		drm_printf(p, "\t%s=%d\n", vs_plane->y2r_prop->name,
			   vs_plane_state->y2r_coef ? vs_plane_state->y2r_coef->base.id : 0);

	if (vs_plane->lut_3d_prop)
		drm_printf(p, "\t%s=%d\n", vs_plane->lut_3d_prop->name,
			   vs_plane_state->lut_3d ? vs_plane_state->lut_3d->base.id : 0);

	if (vs_plane->clear_prop)
		drm_printf(p, "\t%s=%d\n", vs_plane->clear_prop->name,
			   vs_plane_state->clear ? vs_plane_state->clear->base.id : 0);

	vs_dc_print_drm_properties(vs_plane_state->drm_states, &vs_plane->properties, p);
}

static int vs_plane_late_register(struct drm_plane *plane)
{
	return vs_plane_debugfs_init(plane);
}

static void vs_plane_early_unregister(struct drm_plane *plane)
{
	struct vs_plane *vs_plane = to_vs_plane(plane);

	debugfs_remove_recursive(vs_plane->debugfs_entry);
}

const struct drm_plane_funcs vs_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.late_register = vs_plane_late_register,
	.early_unregister = vs_plane_early_unregister,
	.destroy = vs_plane_destroy,
	.reset = vs_plane_reset,
	.atomic_print_state = vs_plane_atomic_print_state,
	.atomic_duplicate_state = vs_plane_atomic_duplicate_state,
	.atomic_destroy_state = vs_plane_atomic_destroy_state,
	.atomic_set_property = vs_plane_atomic_set_property,
	.atomic_get_property = vs_plane_atomic_get_property,
	.format_mod_supported = vs_plane_format_mod_supported,
};

static unsigned char vs_get_plane_number(struct drm_framebuffer *fb)
{
	const struct drm_format_info *info;

	if (!fb)
		return 0;

	info = drm_format_info(fb->format->format);
	if (!info || info->num_planes > MAX_NUM_PLANES)
		return 0;

	return info->num_planes;
}

static int vs_plane_atomic_check(struct drm_plane *plane, struct drm_atomic_state *atomic_state)
{
	struct vs_plane *vs_plane = to_vs_plane(plane);
	struct drm_plane_state *state = drm_atomic_get_new_plane_state(atomic_state, plane);
	struct drm_framebuffer *fb = state->fb;
	struct drm_crtc *crtc = state->crtc;
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);

	if (!crtc || !fb)
		return 0;

	return vs_plane->funcs->check(vs_crtc->dev, vs_plane, state);
}

static void vs_plane_atomic_update(struct drm_plane *plane, struct drm_atomic_state *atomic_state)
{
	unsigned char i, num_planes;
	struct drm_framebuffer *fb;
	struct vs_plane *vs_plane = to_vs_plane(plane);
	struct drm_plane_state *state = plane->state;
	struct vs_crtc *vs_crtc = to_vs_crtc(state->crtc);
	struct vs_plane_state *plane_state = to_vs_plane_state(state);

	if (!state->fb || !state->crtc || !state->visible)
		return;

	fb = state->fb;

	num_planes = vs_get_plane_number(fb);

	for (i = 0; i < num_planes; i++)
		vs_plane->dma_addr[i] = vs_fb_get_dma_addr(fb, i);

	plane_state->status.src = drm_plane_state_src(state);
	plane_state->status.dest = drm_plane_state_dest(state);
	vs_plane->funcs->update(vs_crtc->dev, vs_plane);
}

static void vs_plane_atomic_disable(struct drm_plane *plane, struct drm_atomic_state *atomic_state)
{
	struct vs_plane *vs_plane = to_vs_plane(plane);
	struct vs_drm_private *priv = plane->dev->dev_private;

	vs_plane->funcs->disable(priv->dc_dev, vs_plane);
}

static int vs_plane_prepare_fb(struct drm_plane *plane, struct drm_plane_state *state)
{
	/*
	 * Since shallow buffers have no backing memory, there is no associated gem object and
	 * therefore no need to call the prepare helper
	 */

	if (state->fb && vs_fb_is_shallow(state->fb->modifier))
		return 0;

	return drm_gem_plane_helper_prepare_fb(plane, state);
}

const struct drm_plane_helper_funcs vs_plane_helper_funcs = {
	.prepare_fb = vs_plane_prepare_fb,
	.atomic_check = vs_plane_atomic_check,
	.atomic_update = vs_plane_atomic_update,
	.atomic_disable = vs_plane_atomic_disable,
};

static int vs_plane_create_hw_capability_blob(struct drm_device *drm_dev, struct vs_plane *plane,
					      struct vs_plane_info *plane_info)
{
	struct drm_vs_plane_hw_caps *hw_caps;
	struct drm_property_blob *blob;

	plane->hw_caps_prop = drm_property_create(
		drm_dev, DRM_MODE_PROP_IMMUTABLE | DRM_MODE_PROP_BLOB, "HW_CAPS", 0);
	if (!plane->hw_caps_prop)
		return -EINVAL;

	blob = drm_property_create_blob(drm_dev, sizeof(struct drm_vs_plane_hw_caps), 0);
	if (IS_ERR(blob))
		return -EINVAL;

	hw_caps = blob->data;
	hw_caps->hw_id = plane_info->id;
	hw_caps->fe_id = plane_info->fe_id;
	hw_caps->min_width = plane_info->min_width;
	hw_caps->min_height = plane_info->min_height;
	hw_caps->max_width = plane_info->max_width;
	hw_caps->max_height = plane_info->max_height;
	hw_caps->min_scale = plane_info->min_scale;
	hw_caps->max_scale = plane_info->max_scale;
	hw_caps->axi_id = plane_info->axi_id;

	drm_object_attach_property(&plane->base.base, plane->hw_caps_prop, blob->base.id);

	return 0;
}

struct vs_plane *vs_plane_create(const struct dc_hw_plane *hw_plane, struct drm_device *drm_dev,
				 const struct vs_dc_info *info, enum drm_plane_type plane_type,
				 u8 index, unsigned int possible_crtcs,
				 const struct vs_plane_funcs *dc_plane_funcs)
{
	struct vs_plane *plane;
	struct vs_plane_info *plane_info = NULL;
	int ret;
	u8 temp = 0;

	if (!info)
		return NULL;

	if (index >= info->plane_fe0_num) {
		temp = index - (info->plane_fe0_num);
		plane_info = (struct vs_plane_info *)&info->planes_fe1[temp];
	} else {
		plane_info = (struct vs_plane_info *)&info->planes_fe0[index];
	}

	if (!plane_info)
		return NULL;

	plane = kzalloc(sizeof(struct vs_plane), GFP_KERNEL);
	if (!plane)
		return NULL;

	plane->funcs = dc_plane_funcs;

	ret = drm_universal_plane_init(drm_dev, &plane->base, possible_crtcs, &vs_plane_funcs,
				       plane_info->formats, plane_info->num_formats,
				       plane_info->modifiers, plane_type, "%s", plane_info->name);
	if (ret)
		goto err_free_plane;

	drm_plane_helper_add(&plane->base, &vs_plane_helper_funcs);

	/* Standard properties */
	if (plane_info->rotation != DRM_MODE_ROTATE_0) {
		ret = drm_plane_create_rotation_property(&plane->base, DRM_MODE_ROTATE_0,
							 plane_info->rotation);
		if (ret)
			goto error_cleanup_plane;
	}

	if (plane_info->blend_mode) {
		ret = drm_plane_create_blend_mode_property(&plane->base, plane_info->blend_mode);
		if (ret)
			goto error_cleanup_plane;
		ret = drm_plane_create_alpha_property(&plane->base);
		if (ret)
			goto error_cleanup_plane;
	}

	if (plane_info->color_encoding) {
		ret = drm_plane_create_color_properties(&plane->base, plane_info->color_encoding,
							plane_info->color_range,
							DRM_COLOR_YCBCR_BT2020,
							DRM_COLOR_YCBCR_LIMITED_RANGE);
		if (ret)
			goto error_cleanup_plane;

		/* For user-defined Y2R conversion coefficients */
		if (plane_info->program_csc) {
			plane->y2r_prop =
				drm_property_create(drm_dev, DRM_MODE_PROP_BLOB, "Y2R_CONFIG", 0);
			if (!plane->y2r_prop)
				goto error_cleanup_plane;

			drm_object_attach_property(&plane->base.base, plane->y2r_prop, 0);
		}
	}

	if (plane_info->zpos != 255) {
		ret = drm_plane_create_zpos_property(&plane->base, plane_info->zpos, 0,
						     info->max_blend_layer - 1);
		if (ret)
			goto error_cleanup_plane;
	} else {
		ret = drm_plane_create_zpos_immutable_property(&plane->base, plane_info->zpos);
		if (ret)
			goto error_cleanup_plane;
	}

	/* Private properties */
	if (plane_info->layer_ext || plane_info->layer_ext_ex) {
		plane->ext_layer_prop = drm_property_create_object(
			drm_dev, DRM_MODE_PROP_ATOMIC, "EXT_LAYER_FB", DRM_MODE_OBJECT_FB);
		if (!plane->ext_layer_prop)
			goto error_cleanup_plane;

		drm_object_attach_property(&plane->base.base, plane->ext_layer_prop, 0);
	}

	if (plane_info->watermark) {
		plane->watermark_prop =
			drm_property_create(drm_dev, DRM_MODE_PROP_BLOB, "WATERMARK", 0);
		if (!plane->watermark_prop)
			goto error_cleanup_plane;

		drm_object_attach_property(&plane->base.base, plane->watermark_prop, 0);
	}

	if (plane_info->cgm_lut) {
		plane->lut_3d_prop = drm_property_create(drm_dev, DRM_MODE_PROP_BLOB, "3DLUT", 0);
		if (!plane->lut_3d_prop)
			goto error_cleanup_plane;

		drm_object_attach_property(&plane->base.base, plane->lut_3d_prop, 0);
	}

	if (plane_info->color_mgmt) {
		plane->clear_prop = drm_property_create(drm_dev, DRM_MODE_PROP_BLOB, "CLEAR", 0);
		if (!plane->clear_prop)
			goto error_cleanup_plane;

		drm_object_attach_property(&plane->base.base, plane->clear_prop, 0);
	}

	ret = vs_plane_create_hw_capability_blob(drm_dev, plane, plane_info);
	if (ret)
		goto error_cleanup_plane;

	if (hw_plane && vs_dc_create_drm_properties(drm_dev, &plane->base.base, &hw_plane->states,
						    &plane->properties))
		goto error_cleanup_plane;

	return plane;

error_cleanup_plane:
	drm_plane_cleanup(&plane->base);
err_free_plane:
	kfree(plane);
	return NULL;
}
