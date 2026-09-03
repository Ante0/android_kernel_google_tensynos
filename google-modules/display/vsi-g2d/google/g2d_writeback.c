// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */

/*
 * Writeback connector and associated virtual encoder
 *
 * We want to allow for 2 connectors and encoders to be configured, one for each
 * pipeline.
 * The Connector will have 1 KMS property:
 *  1) Input Framebuffer ID and Fence
 * The encoder will be a passthrough.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_writeback.h>

#include "g2d_drv.h"
#include "g2d_writeback.h"
#include "g2d_gem.h"
#include "g2d_writeback_hw.h"
#include "g2d_sc.h"

static const u32 g2d_wb_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_P010,
};

// TODO(rushikesh@): Since this is exclusively a writeback engine, it may be acceptable to
// export only the largest mode that might be requested.
static int g2d_wb_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	struct drm_display_mode *mode = NULL;
	unsigned int i;
	int count = 0;
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
		mode = drm_cvt_mode(dev, cvt_mode[i].w, cvt_mode[i].h, cvt_mode[i].refresh, false,
				    false, false);
		if (!mode) {
			dev_warn(dev->dev, "Failed to create CVT mode %dx%d@%d\n", cvt_mode[i].w,
				 cvt_mode[i].h, cvt_mode[i].refresh);
			continue;
		}

		mode->hdisplay = cvt_mode[i].w;
		mode->vdisplay = cvt_mode[i].h;
		scnprintf(mode->name, DRM_DISPLAY_MODE_LEN, "%dx%dx%d", mode->hdisplay,
			  mode->vdisplay, cvt_mode[i].refresh);
		drm_mode_probed_add(connector, mode);
		count++;
	}

	return count;
}

static enum drm_mode_status g2d_wb_connector_mode_valid(struct drm_connector *connector,
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

static int g2d_wb_connector_atomic_check(struct drm_connector *connector,
					 struct drm_atomic_state *state)
{
	struct drm_connector_state *connector_state;
	struct drm_writeback_connector *wb_connector;
	struct g2d_writeback_connector *g2d_wb_connector;
	struct drm_framebuffer *fb;

	connector_state = drm_atomic_get_new_connector_state(state, connector);
	if (!connector_state)
		return 0;

	wb_connector = drm_connector_to_writeback(connector);
	g2d_wb_connector = to_g2d_writeback_connector(wb_connector);

	if (!connector_state->writeback_job)
		return 0;

	fb = connector_state->writeback_job->fb;
	return g2d_wb_connector->funcs->check(state->dev, fb);
}

static int g2d_wb_connector_atomic_prepare(struct drm_writeback_connector *connector,
					   struct drm_writeback_job *job)
{
	struct drm_framebuffer *fb;
	struct g2d_writeback_connector *g2d_wb_connector = to_g2d_writeback_connector(connector);

	u8 num_planes;
	int i;

	if (!job->fb)
		return 0;

	fb = job->fb;

	num_planes = fb->format->num_planes;
	for (i = 0; i < num_planes; i++) {
		struct g2d_bo *g2d_obj;

		g2d_obj = to_g2d_buffer_object(fb->obj[i]);
		g2d_wb_connector->dma_addr[i] = g2d_obj->dma_addr + fb->offsets[i];
		g2d_wb_connector->pitch[i] = fb->pitches[i];
		g2d_wb_connector->is_yuv = fb->format->is_yuv;
	}

	return 0;
}

static void g2d_wb_connector_atomic_commit(struct drm_connector *connector,
					   struct drm_atomic_state *state)
{
	struct drm_connector_state *connector_state;
	struct drm_writeback_connector *wb_connector;
	struct g2d_writeback_connector *g2d_wb_connector;
	struct drm_device *drm = connector->dev;
	struct g2d_device *g2d_device = to_g2d_device(drm);
	struct g2d_sc *sc = g2d_device->sc;
	struct drm_framebuffer *fb;

	if (sc->requires_reset && !sc->allow_reset) {
		dev_dbg(drm->dev, "Skipping G2D writeback because G2D requires reset");
		return;
	}

	connector_state = drm_atomic_get_new_connector_state(state, connector);
	if (!connector_state)
		return;

	if (!connector_state->writeback_job)
		return;

	wb_connector = drm_connector_to_writeback(connector);
	g2d_wb_connector = to_g2d_writeback_connector(wb_connector);
	fb = connector_state->writeback_job->fb;

	drm_writeback_queue_job(wb_connector, connector_state);

	g2d_wb_connector->funcs->commit(g2d_wb_connector, fb);
	dev_dbg(drm->dev, "%s: hardware config complete", __func__);

	g2d_wb_connector->armed++;
}

static void g2d_wb_encoder_atomic_disable(struct drm_encoder *encoder,
					  struct drm_atomic_state *state)
{
	struct drm_writeback_job *job;
	struct drm_writeback_connector *wb_connector =
		container_of(encoder, struct drm_writeback_connector, encoder);

	struct drm_device *drm = encoder->dev;

	job = list_first_entry_or_null(&wb_connector->job_queue, struct drm_writeback_job,
				       list_entry);

	/* TODO(b/392161138): We should wait for the wb job to finish before signalling */
	if (job) {
		dev_warn(drm->dev, "%s: job pending during disable\n", __func__);
		drm_writeback_signal_completion(wb_connector, -EIO);
	}
}

static const struct drm_connector_funcs g2d_wb_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs g2d_wb_connector_helper_funcs = {
	.get_modes = g2d_wb_connector_get_modes,
	.mode_valid = g2d_wb_connector_mode_valid,
	.atomic_check = g2d_wb_connector_atomic_check,
	.prepare_writeback_job = g2d_wb_connector_atomic_prepare,
	.atomic_commit = g2d_wb_connector_atomic_commit,
};

static const struct drm_encoder_helper_funcs g2d_wb_encoder_helper_funcs = {
	.atomic_disable = g2d_wb_encoder_atomic_disable,
};

int g2d_enable_writeback_connector(struct g2d_device *g2d_device, uint32_t possible_crtcs)
{
	int i;
	struct drm_device *drm = &g2d_device->drm;

	for (i = 0; i < NUM_PIPELINES; i++) {
		int ret;
		struct g2d_writeback_connector *g2d_wb_connector;
		struct drm_writeback_connector *wb_connector;

		g2d_wb_connector = kzalloc(sizeof(struct g2d_writeback_connector), GFP_KERNEL);
		if (!g2d_wb_connector)
			return -ENOMEM;
		g2d_device->sc->writeback[i] = g2d_wb_connector;
		wb_connector = &(g2d_wb_connector->base);
		g2d_wb_connector->dev = drm->dev;
		g2d_wb_connector->id = i;

		ret = drm_writeback_connector_init(drm, wb_connector, &g2d_wb_connector_funcs,
						   &g2d_wb_encoder_helper_funcs, g2d_wb_formats,
						   ARRAY_SIZE(g2d_wb_formats), possible_crtcs);
		if (ret) {
			dev_err(drm->dev, "Failed to initialize writeback connector #%d, error %d!",
				i, ret);
			return ret;
		}
		drm_connector_helper_add(&wb_connector->base, &g2d_wb_connector_helper_funcs);

		sc_wb_init(g2d_wb_connector);
		dev_info(drm->dev, "Initialized writeback connector %d", i);
	}

	return 0;
}

void g2d_handle_writeback_frm_done(struct g2d_writeback_connector *g2d_wb_connector)
{
	struct drm_writeback_job *job;

	if (!g2d_wb_connector)
		return;

	job = list_first_entry_or_null(&g2d_wb_connector->base.job_queue, struct drm_writeback_job,
				       list_entry);

	dev_dbg(g2d_wb_connector->dev, "%s: connector_id=%d armed=%d\n", __func__,
		g2d_wb_connector->id, g2d_wb_connector->armed);

	if (job) {
		drm_writeback_signal_completion(&g2d_wb_connector->base, 0);
		g2d_wb_connector->armed--;
	}

	if (!g2d_wb_connector->armed)
		dev_dbg(g2d_wb_connector->dev, "wb_connector idle");
}

void g2d_signal_wb_error(struct g2d_writeback_connector *g2d_wb_connector)
{

	struct drm_writeback_job *job;

	if (!g2d_wb_connector)
		return;

	job = list_first_entry_or_null(&g2d_wb_connector->base.job_queue, struct drm_writeback_job,
				       list_entry);

	if (job) {
		drm_writeback_signal_completion(&g2d_wb_connector->base, -EIO);
		dev_warn(g2d_wb_connector->dev, "signalled error for writeback job");
		g2d_wb_connector->armed--;
	}
}
