// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */


#include <linux/debugfs.h>
#include <linux/media-bus-format.h>
#include <linux/seq_file.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_writeback.h>

#include <drm/vs_drm.h>

#include "vs_crtc.h"
#include "vs_gem.h"
#include "vs_crtc.h"
#include "vs_dc_info.h"
#include "vs_dc.h"
#include "vs_writeback.h"
#include "vs_drv.h"
#include "vs_dc_drm_property.h"
#include "vs_trace.h"
#include "trace/dpu_trace.h"

static int wb_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *drm_dev = connector->dev;
	struct device *dev = drm_dev->dev;

	struct drm_display_mode *mode = NULL;
	unsigned int i;
	static const struct display_mode {
		int w, h, refresh;
	} cvt_mode[] = {
		{ 320, 480, 60 },    { 640, 480, 60 },	  { 720, 480, 60 },    { 720, 1612, 60 },
		{ 800, 600, 60 },    { 1080, 2400, 60 },  { 1024, 768, 60 },   { 1280, 720, 60 },
		{ 1280, 1024, 60 },  { 1400, 1050, 60 },  { 1440, 3520, 60 },  { 1440, 3216, 60 },
		{ 1440, 3360, 60 },  { 1680, 1050, 60 },  { 1600, 1200, 60 },  { 1920, 1080, 60 },
		{ 1920, 1200, 60 },  { 2340, 3404, 60 },  { 2500, 2820, 60 },  { 2700, 2600, 60 },
		{ 3200, 1920, 60 },  { 3840, 2160, 60 },  { 4096, 2160, 60 },  { 1080, 2340, 60 },
		{ 7680, 4320, 30 },  { 1280, 720, 120 },  { 1920, 1080, 120 }, { 3840, 2160, 120 },
		{ 3440, 1440, 160 }, { 5120, 2880, 120 }, { 4096, 2160, 120 }, { 6144, 3456, 60 }
	};

	for (i = 0; i < ARRAY_SIZE(cvt_mode); i++) {
		mode = drm_cvt_mode(drm_dev, cvt_mode[i].w, cvt_mode[i].h, cvt_mode[i].refresh,
				    false, false, false);
		if (!mode) {
			DRM_DEV_ERROR(dev,
				      "[CONNECTOR:%u:%s]: no mode %dx%dx%d could be allocated\n",
				      connector->base.id, connector->name, cvt_mode[i].w,
				      cvt_mode[i].h, cvt_mode[i].refresh);
			return -ENOMEM;
		}

		mode->hdisplay = cvt_mode[i].w;
		mode->vdisplay = cvt_mode[i].h;
		scnprintf(mode->name, DRM_DISPLAY_MODE_LEN, "%dx%dx%d", mode->hdisplay,
			  mode->vdisplay, cvt_mode[i].refresh);
		drm_mode_probed_add(connector, mode);
	}

	return 0;
}

static enum drm_mode_status wb_connector_mode_valid(struct drm_connector *connector,
						    struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	int w = mode->hdisplay, h = mode->vdisplay;

	if ((w < mode_config->min_width) || (w > mode_config->max_width))
		return MODE_BAD_HVALUE;

	if ((h < mode_config->min_height) || (h > mode_config->max_height))
		return MODE_BAD_VVALUE;

	return MODE_OK;
}

static int wb_connector_prepare_writeback_job(struct drm_writeback_connector *connector,
					      struct drm_writeback_job *job)
{
	struct drm_framebuffer *fb;
	struct vs_writeback_connector *vs_wb_connector = to_vs_writeback_connector(connector);
	u8 num_planes;
	int i;

	if (!job->fb)
		return 0;

	fb = job->fb;

	num_planes = fb->format->num_planes;
	for (i = 0; i < num_planes; i++) {
		struct vs_gem_object *vs_obj;

		vs_obj = to_vs_gem_object(fb->obj[i]);
		vs_wb_connector->dma_addr[i] = vs_obj->iova + fb->offsets[i];
		vs_wb_connector->pitch[i] = fb->pitches[i];
	}

	vs_wb_connector->armed = 1;
	DRM_DEV_DEBUG(vs_wb_connector->dev, "%s: [wb-%d] armed=%d frame_pending=%d\n", __func__,
		      vs_wb_connector->id, vs_wb_connector->armed, vs_wb_connector->frame_pending);

	return 0;
}

static void wb_connector_cleanup_writeback_job(struct drm_writeback_connector *connector,
					       struct drm_writeback_job *job)
{
	struct vs_writeback_connector *vs_wb_connector = to_vs_writeback_connector(connector);

	vs_wb_connector->funcs->cleanup(vs_wb_connector);
}

static void wb_connector_atomic_commit(struct drm_connector *connector,
				       struct drm_atomic_state *atomic_state)
{
	struct drm_framebuffer *fb;
	struct drm_connector_state *state =
		drm_atomic_get_new_connector_state(atomic_state, connector);
	struct drm_writeback_connector *wb_connector = drm_connector_to_writeback(connector);
	struct vs_writeback_connector *vs_wb_connector = to_vs_writeback_connector(wb_connector);

	if (WARN_ON(!state || !state->writeback_job))
		return;

	fb = state->writeback_job->fb;
	drm_writeback_queue_job(wb_connector, state);
	vs_wb_connector->crtc = to_vs_crtc(state->crtc);

	vs_wb_connector->funcs->config(vs_wb_connector, fb);

	vs_wb_connector->frame_pending++;
	DRM_DEV_DEBUG(vs_wb_connector->dev, "%s: [wb-%d] armed=%d frame_pending=%d\n", __func__,
		      vs_wb_connector->id, vs_wb_connector->armed, vs_wb_connector->frame_pending);
}

static const struct drm_connector_helper_funcs wb_connector_helper_funcs = {
	.get_modes = wb_connector_get_modes,
	.mode_valid = wb_connector_mode_valid,
	.prepare_writeback_job = wb_connector_prepare_writeback_job,
	.cleanup_writeback_job = wb_connector_cleanup_writeback_job,
	.atomic_commit = wb_connector_atomic_commit,
};

static enum drm_connector_status wb_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void wb_connector_atomic_destroy_state(struct drm_connector *connector,
					      struct drm_connector_state *state)
{
	struct vs_writeback_connector_state *vs_wb_state = to_vs_writeback_connector_state(state);
	struct drm_writeback_connector *drm_wb = drm_connector_to_writeback(connector);
	struct vs_writeback_connector *vs_wb = to_vs_writeback_connector(drm_wb);

	/* destroy strandard properties */
	__drm_atomic_helper_connector_destroy_state(state);

	/* destroy custom properties */
	vs_dc_destroy_drm_properties(vs_wb_state->drm_states, &vs_wb->properties);

	kfree(vs_wb_state);
}

static void wb_connector_reset(struct drm_connector *connector)
{
	struct vs_writeback_connector_state *state;
	struct drm_writeback_connector *drm_wb = drm_connector_to_writeback(connector);
	struct vs_writeback_connector *vs_wb = to_vs_writeback_connector(drm_wb);
	struct drm_mode_object *vs_wb_objs = &vs_wb->base.base.base;
	u32 i;
	u64 wb_point = VS_WB_DISP_OUT;

	if (connector->state) {
		connector->funcs->atomic_destroy_state(connector, connector->state);
		connector->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL)
		return;

	/* reset standard properties */
	__drm_atomic_helper_connector_reset(connector, &state->base);

	/* Get the writeback point property initialization value */
	if (vs_wb->point_prop) {
		for (i = 0; i < vs_wb_objs->properties->count; i++) {
			if (vs_wb_objs->properties->properties[i] == vs_wb->point_prop)
				wb_point = vs_wb_objs->properties->values[i];
		}
		state->wb_point = (u32)wb_point;
	}

	for (i = 0; i < vs_wb->properties.num; i++) {
		state->drm_states[i].proto = vs_wb->properties.items[i].proto;
		state->drm_states[i].is_changed = true;
	}

	/* reset internal state variables */
	vs_wb->armed = 0;
	vs_wb->frame_pending = 0;
	vs_wb->crtc = NULL;
}

static struct drm_connector_state *
wb_connector_atomic_duplicate_state(struct drm_connector *connector)
{
	struct vs_writeback_connector_state *ori_state;
	struct vs_writeback_connector_state *state;
	struct drm_writeback_connector *drm_wb = drm_connector_to_writeback(connector);
	const struct vs_writeback_connector *vs_wb = to_vs_writeback_connector(drm_wb);

	if (WARN_ON(!connector->state))
		return NULL;

	ori_state = to_vs_writeback_connector_state(connector->state);
	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_connector_duplicate_state(connector, &state->base);

	if (vs_wb->point_prop)
		state->wb_point = ori_state->wb_point;

#ifdef CONFIG_DEBUG_FS
	state->qos = ori_state->qos;
#endif

	/* dc properties */
	vs_dc_duplicate_drm_properties(state->drm_states, ori_state->drm_states,
				       &vs_wb->properties);

	return &state->base;
}

static int wb_connector_atomic_set_property(struct drm_connector *connector,
					    struct drm_connector_state *state,
					    struct drm_property *property, uint64_t val)
{
	int ret = 0;
	struct drm_writeback_connector *wb_connector = drm_connector_to_writeback(connector);
	struct vs_writeback_connector *vs_wb_connector = to_vs_writeback_connector(wb_connector);
	struct vs_writeback_connector_state *vs_wb_state = to_vs_writeback_connector_state(state);
	struct drm_device *dev = connector->dev;

	if (property == vs_wb_connector->point_prop)
		vs_wb_state->wb_point = val;
	else {
		/* dc property */
		ret = vs_dc_set_drm_property(dev, vs_wb_state->drm_states,
					     &vs_wb_connector->properties, property, val);
	}

	drm_dbg_state(dev, "[CONNECTOR:%u:%s]: SET custom property %s value %llu\n",
		      connector->base.id, connector->name, property->name, val);
	trace_disp_set_property(connector->name, property->name, val);

	return ret;
}

static int wb_connector_atomic_get_property(struct drm_connector *connector,
					    const struct drm_connector_state *state,
					    struct drm_property *property, uint64_t *val)
{
	struct drm_writeback_connector *wb_connector = drm_connector_to_writeback(connector);
	struct vs_writeback_connector *vs_wb_connector = to_vs_writeback_connector(wb_connector);
	const struct vs_writeback_connector_state *vs_wb_state =
		container_of(state, const struct vs_writeback_connector_state, base);

	if (property == vs_wb_connector->point_prop)
		*val = vs_wb_state->wb_point;
	else
		return vs_dc_get_drm_property(vs_wb_state->drm_states, &vs_wb_connector->properties,
					      property, val);

	return 0;
}

static void wb_connector_atomic_print_state(struct drm_printer *p,
					    const struct drm_connector_state *state)
{
	const struct vs_writeback_connector_state *vs_wb_state =
		container_of(state, const struct vs_writeback_connector_state, base);
	struct drm_writeback_connector *wb_connector = drm_connector_to_writeback(state->connector);
	struct vs_writeback_connector *vs_writeback = to_vs_writeback_connector(wb_connector);

	if (vs_writeback->point_prop)
		drm_printf(p, "\twb_point = %d\n", vs_wb_state->wb_point);

	vs_dc_print_drm_properties(vs_wb_state->drm_states, &vs_writeback->properties, p);
}

#ifdef CONFIG_DEBUG_FS
static int vs_wb_qos_show(struct seq_file *s, void *data)
{
	struct drm_connector *connector = s->private;
	struct drm_writeback_connector *drm_wb_connector = drm_connector_to_writeback(connector);
	struct vs_writeback_connector *vs_wb_connector =
		to_vs_writeback_connector(drm_wb_connector);

	if (vs_wb_connector->funcs->show_qos)
		return vs_wb_connector->funcs->show_qos(s);

	return 0;
}

static int wb_connector_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, vs_wb_qos_show, inode->i_private);
}

static ssize_t wb_connector_debugfs_write(struct file *file, const char __user *ubuf, size_t len,
					  loff_t *offp)
{
	struct seq_file *s = file->private_data;
	struct drm_connector *connector = s->private;
	struct drm_writeback_connector *drm_wb_connector = drm_connector_to_writeback(connector);
	struct vs_writeback_connector *vs_wb_connector =
		to_vs_writeback_connector(drm_wb_connector);

	if (vs_wb_connector->funcs->set_qos)
		vs_wb_connector->funcs->set_qos(vs_wb_connector->dev, vs_wb_connector, ubuf, len);

	return len;
}

static const struct file_operations wb_connector_debugfs_fops = {
	.open = wb_connector_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = wb_connector_debugfs_write,
};

static int wb_connector_debugfs_init(struct drm_connector *connector)
{
	debugfs_create_file("QOS", 0644, connector->debugfs_entry, connector,
			    &wb_connector_debugfs_fops);

	return 0;
}
#else
static int wb_connector_debugfs_init(struct drm_connector *connector)
{
	return 0;
}
#endif
static int wb_connector_late_register(struct drm_connector *connector)
{
	return wb_connector_debugfs_init(connector);
}

static const struct drm_connector_funcs wb_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.late_register = wb_connector_late_register,
	.detect = wb_connector_detect,
	.reset = wb_connector_reset,
	.atomic_duplicate_state = wb_connector_atomic_duplicate_state,
	.atomic_destroy_state = wb_connector_atomic_destroy_state,
	.atomic_set_property = wb_connector_atomic_set_property,
	.atomic_get_property = wb_connector_atomic_get_property,
	.atomic_print_state = wb_connector_atomic_print_state,
};

static int wb_encoder_atomic_check(struct drm_encoder *encoder, struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state)
{
	struct drm_framebuffer *fb;
	struct vs_writeback_connector *vs_wb_connector;
	struct vs_crtc_state *vs_crtc_state;

	if (!conn_state->writeback_job)
		return 0;

	fb = conn_state->writeback_job->fb;
	if (!fb->format || fb->format->num_planes > MAX_WB_NUM_PLANES || !crtc_state->active)
		return -EINVAL;

	vs_crtc_state = to_vs_crtc_state(crtc_state);

	if (crtc_state->connector_mask == drm_connector_mask(conn_state->connector)) {
		/* if this is standalone writeback connector, vblank is not expected */
		crtc_state->no_vblank = true;
		vs_crtc_state->output_mode = VS_OUTPUT_MODE_CMD | VS_OUTPUT_MODE_CMD_DE_SYNC;
	}

	/* writeback connector default output bus format set as MEDIA_BUS_FMT_RGB888_1X24 */
	vs_crtc_state->output_fmt = MEDIA_BUS_FMT_RGB888_1X24;
	vs_crtc_state->wb_connectors_updated = true;

	vs_wb_connector = to_vs_writeback_connector(conn_state->writeback_job->connector);

	return vs_wb_connector->funcs->check(vs_wb_connector, fb, &crtc_state->mode, conn_state);
}

static void wb_encoder_atomic_disable(struct drm_encoder *encoder, struct drm_atomic_state *state)
{
	struct drm_writeback_connector *wb_connector =
		container_of(encoder, struct drm_writeback_connector, encoder);
	struct vs_writeback_connector *vs_wb_connector = to_vs_writeback_connector(wb_connector);

	if (!vs_wb_connector->armed && !vs_wb_connector->frame_pending) {
		DRM_DEV_DEBUG(vs_wb_connector->dev, "%s: [wb-%d] wb not armed, skip disable\n",
			      __func__, vs_wb_connector->id);
		return;
	}

	DPU_ATRACE_BEGIN("%s [wb-%d] armed %u frame_pending %u", __func__, vs_wb_connector->id,
			 vs_wb_connector->armed, vs_wb_connector->frame_pending);

	DRM_DEV_DEBUG(vs_wb_connector->dev, "%s: [wb-%d] wb job completed, disable hardware\n",
		      __func__, vs_wb_connector->id);

	/* wb module has shadow register, can disable during active state */
	vs_wb_connector->funcs->disable(vs_wb_connector);
	vs_wb_connector->armed = 0;
	vs_wb_connector->frame_pending = 0;
	vs_wb_connector->crtc = NULL;

	DPU_ATRACE_END("%s [wb-%d] armed %u frame_pending %u", __func__, vs_wb_connector->id,
		       vs_wb_connector->armed, vs_wb_connector->frame_pending);
}

static const struct drm_encoder_helper_funcs wb_encoder_helper_funcs = {
	.atomic_check = wb_encoder_atomic_check,
	.atomic_disable = wb_encoder_atomic_disable,
};

void vs_writeback_handle_vblank(struct vs_writeback_connector *vs_wb_connector)
{
	struct drm_writeback_job *job;

	if (!vs_wb_connector)
		return;

	DPU_ATRACE_BEGIN("%s [wb-%d] armed %u frame_pending %u", __func__, vs_wb_connector->id,
			 vs_wb_connector->armed, vs_wb_connector->frame_pending);

	DRM_DEV_DEBUG(vs_wb_connector->dev, "%s: [wb-%d] armed=%d frame_pending=%d\n", __func__,
		      vs_wb_connector->id, vs_wb_connector->armed, vs_wb_connector->frame_pending);
	job = list_first_entry_or_null(&vs_wb_connector->base.job_queue, struct drm_writeback_job,
				       list_entry);
	if (job) {
		DPU_ATRACE_BEGIN("signal wb completion");
		drm_writeback_signal_completion(&vs_wb_connector->base, 0);
		DPU_ATRACE_END("signal wb completion");
	}
	/* Disable WB after each single commit */
	if (vs_wb_connector->armed && !vs_wb_connector->frame_pending) {
		DRM_DEV_DEBUG(vs_wb_connector->dev,
			      "%s: [wb-%d] frame captured, disable hardware\n", __func__,
			      vs_wb_connector->id);
		vs_wb_connector->funcs->disable(vs_wb_connector);
		vs_wb_connector->armed = 0;
	}
	DPU_ATRACE_END("%s [wb-%d] armed %u frame_pending %u", __func__, vs_wb_connector->id,
		       vs_wb_connector->armed, vs_wb_connector->frame_pending);
}

static const struct drm_prop_enum_list vs_wb_point_enum_list[] = {
	{ VS_WB_DISP_IN, "post panel input" },	 { VS_WB_DISP_CC, "post color calibration out" },
	{ VS_WB_DISP_OUT, "post panel output" }, { VS_WB_OFIFO_IN, "ofifo input" },
	{ VS_WB_OFIFO_OUT, "ofifo output" },
};

struct vs_writeback_connector *vs_writeback_create(const struct dc_hw_wb *hw_wb,
						   struct drm_device *drm_dev,
						   const struct vs_wb_info *info,
						   unsigned int possible_crtcs)
{
	struct vs_writeback_connector *vs_writeback;
	struct drm_encoder *encoder;
	int ret;

	if (!info)
		return ERR_PTR(-EINVAL);

	vs_writeback = drmm_kzalloc(drm_dev, sizeof(struct vs_writeback_connector), GFP_KERNEL);
	if (!vs_writeback)
		return ERR_PTR(-ENOMEM);

	encoder = &vs_writeback->base.encoder;

	drm_encoder_helper_add(encoder, &wb_encoder_helper_funcs);
	encoder->possible_crtcs = possible_crtcs;

	ret = drmm_encoder_init(drm_dev, encoder, NULL, DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret)
		return ERR_PTR(ret);

	ret = drmm_writeback_connector_init(drm_dev, &vs_writeback->base, &wb_connector_funcs,
					    encoder, info->formats, info->num_formats);
	if (ret)
		return ERR_PTR(ret);

	drm_connector_helper_add(&vs_writeback->base.base, &wb_connector_helper_funcs);

	/* Set up the writeback properties */
	if (info->program_point) {
		vs_writeback->point_prop = drm_property_create_enum(
			drm_dev, DRM_MODE_PROP_ATOMIC, "WB_POINT", vs_wb_point_enum_list,
			ARRAY_SIZE(vs_wb_point_enum_list));
		if (!vs_writeback->point_prop)
			return NULL;

		drm_object_attach_property(&vs_writeback->base.base.base, vs_writeback->point_prop,
					info->init_wb_point ? info->init_wb_point : VS_WB_DISP_OUT);
	}

	if (hw_wb && vs_dc_create_drm_properties(drm_dev, &vs_writeback->base.base.base,
						 &hw_wb->states, &vs_writeback->properties))
		return NULL;

	return vs_writeback;
}

struct drm_writeback_connector *find_wb_connector(struct drm_crtc *crtc)
{
	struct drm_connector_list_iter iter;
	struct drm_connector *connector;
	struct drm_writeback_connector *wb_connector = NULL;

	drm_connector_list_iter_begin(crtc->dev, &iter);
	drm_for_each_connector_iter(connector, &iter) {
		if ((crtc->state->connector_mask & drm_connector_mask(connector)) &&
		    connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			break;
	}
	drm_connector_list_iter_end(&iter);

	if (connector)
		wb_connector = drm_connector_to_writeback(connector);

	return wb_connector;
}

/**
 * vs_writeback_is_active() - Check if writeback is active in hardware.
 * @dev: The DRM device structure.
 *
 * This function scans all writeback connectors to check if any of them have
 * frames currently in flight in the hardware.
 *
 * Return: true if writeback is active in hardware, false otherwise.
 */
bool vs_writeback_is_active(const struct drm_device *dev)
{
	struct vs_drm_private *priv = dev->dev_private;
	struct vs_dc *dc = dev_get_drvdata(priv->dc_dev);

	if (!dc)
		return false;

	return dc_hw_writeback_is_active(&dc->hw);
}
