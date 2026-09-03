// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "g2d_drv.h"
#include "g2d_crtc.h"
#include "g2d_sc.h"

/*
 *
 * CRTC
 *
 * We want to allow for 2 CRTCs to be configured, one for each pipeline.
 * In this design CRTCs are passthroughs.
 */

static void g2d_crtc_destroy(struct drm_crtc *crtc)
{
	struct g2d_crtc *g2d_crtc = to_g2d_crtc(crtc);

	g2d_qos_remove_devfreq_request(crtc);

	if (!IS_ERR(g2d_crtc->commit_worker))
		kthread_destroy_worker(g2d_crtc->commit_worker);

	drm_crtc_cleanup(crtc);
	kfree(g2d_crtc);
}

static void g2d_crtc_atomic_destroy_state(struct drm_crtc *crtc, struct drm_crtc_state *state)
{
	struct g2d_crtc_state *g2d_crtc_state = to_g2d_crtc_state(state);

	__drm_atomic_helper_crtc_destroy_state(state);

	kfree(g2d_crtc_state);
}

static struct drm_crtc_state *g2d_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct g2d_crtc_state *g2d_crtc_state;

	if (WARN_ON(!crtc->state))
		return NULL;

	g2d_crtc_state =
		kmemdup(to_g2d_crtc_state(crtc->state), sizeof(*g2d_crtc_state), GFP_KERNEL);

	if (!g2d_crtc_state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &g2d_crtc_state->base);

	return &g2d_crtc_state->base;
}

static void g2d_crtc_reset(struct drm_crtc *crtc)
{
	struct g2d_crtc_state *g2d_crtc_state;

	if (crtc->state) {
		g2d_crtc_atomic_destroy_state(crtc, crtc->state);
		crtc->state = NULL;
	}

	g2d_crtc_state = kzalloc(sizeof(*g2d_crtc_state), GFP_KERNEL);
	if (!g2d_crtc_state)
		return;

	__drm_atomic_helper_crtc_reset(crtc, &g2d_crtc_state->base);
}

static int g2d_crtc_atomic_set_property(struct drm_crtc *crtc, struct drm_crtc_state *state,
					struct drm_property *property, uint64_t val)
{
	struct g2d_crtc *g2d_crtc = to_g2d_crtc(crtc);

	if (property == g2d_crtc->core_clk)
		g2d_crtc->req_qos_config.core_clk = val;
	else if (property == g2d_crtc->rd_avg_bw_mbps)
		g2d_crtc->req_qos_config.rd_avg_bw_mbps = val;
	else if (property == g2d_crtc->rd_peak_bw_mbps)
		g2d_crtc->req_qos_config.rd_peak_bw_mbps = val;
	else if (property == g2d_crtc->wr_avg_bw_mbps)
		g2d_crtc->req_qos_config.wr_avg_bw_mbps = val;
	else if (property == g2d_crtc->wr_peak_bw_mbps)
		g2d_crtc->req_qos_config.wr_peak_bw_mbps = val;

	return 0;
}

static int g2d_crtc_atomic_get_property(struct drm_crtc *crtc, const struct drm_crtc_state *state,
					struct drm_property *property, uint64_t *val)
{
	struct g2d_crtc *g2d_crtc = to_g2d_crtc(crtc);

	if (property == g2d_crtc->core_clk)
		*val = g2d_crtc->req_qos_config.core_clk;
	else if (property == g2d_crtc->rd_avg_bw_mbps)
		*val = g2d_crtc->req_qos_config.rd_avg_bw_mbps;
	else if (property == g2d_crtc->rd_peak_bw_mbps)
		*val = g2d_crtc->req_qos_config.rd_peak_bw_mbps;
	else if (property == g2d_crtc->wr_avg_bw_mbps)
		*val = g2d_crtc->req_qos_config.wr_avg_bw_mbps;
	else if (property == g2d_crtc->wr_peak_bw_mbps)
		*val = g2d_crtc->req_qos_config.wr_peak_bw_mbps;

	return 0;
}

static int g2d_crtc_atomic_check(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	return 0;
}

static void g2d_crtc_atomic_flush(struct drm_crtc *crtc, struct drm_atomic_state *old_crtc_state)
{
	struct g2d_crtc *g2d_crtc = to_g2d_crtc(crtc);
	struct drm_device *drm = crtc->dev;
	struct g2d_device *g2d_device = to_g2d_device(drm);
	struct g2d_sc *sc = g2d_device->sc;

	if (!g2d_crtc->dev) {
		pr_err("%s: invalid dev", __func__);
		return;
	}
	if (sc->requires_reset && !sc->allow_reset) {
		dev_err(sc->hw.dev,
			"The g2d hw must be reset before committing new planes. Skipping commit");
		return;
	}
	g2d_crtc->funcs->commit(g2d_crtc->dev, crtc);
}

static void g2d_crtc_atomic_enable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct g2d_crtc *g2d_crtc = to_g2d_crtc(crtc);

	if (!g2d_crtc->dev) {
		pr_err("%s: invalid dev", __func__);
		return;
	}

	g2d_crtc->funcs->enable(g2d_crtc->dev, crtc);
}

static void g2d_crtc_atomic_disable(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct g2d_crtc *g2d_crtc = to_g2d_crtc(crtc);

	if (!g2d_crtc->dev) {
		pr_err("%s: invalid dev", __func__);
		return;
	}

	g2d_crtc->funcs->disable(g2d_crtc->dev, crtc);
}

static const struct drm_crtc_helper_funcs g2d_crtc_helper_funcs = {
	.atomic_check = g2d_crtc_atomic_check,
	.atomic_flush = g2d_crtc_atomic_flush,
	.atomic_enable = g2d_crtc_atomic_enable,
	.atomic_disable = g2d_crtc_atomic_disable,
};

static const struct drm_crtc_funcs g2d_crtc_funcs = {
	.destroy = g2d_crtc_destroy,
	.reset = g2d_crtc_reset,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = g2d_crtc_atomic_duplicate_state,
	.atomic_destroy_state = g2d_crtc_atomic_destroy_state,
	.atomic_set_property = g2d_crtc_atomic_set_property,
	.atomic_get_property = g2d_crtc_atomic_get_property,
};

int g2d_crtc_init(struct g2d_device *g2d_device)
{
	struct drm_device *drm = &g2d_device->drm;
	int i, ret;
	struct g2d_crtc *g2d_crtc;

	for (i = 0; i < NUM_PIPELINES; i++) {
		g2d_crtc = kzalloc(sizeof(*g2d_crtc), GFP_KERNEL);
		if (!g2d_crtc)
			return -ENOMEM;

		ret = drm_crtc_init_with_planes(drm, &g2d_crtc->base, NULL, NULL, &g2d_crtc_funcs,
						"g2d_core%d", i);
		if (ret) {
			kfree(g2d_crtc);
			return ret;
		}

		g2d_crtc->commit_worker = kthread_create_worker(0, "g2d_kthread%d", i);
		if (IS_ERR(g2d_crtc->commit_worker))
			pr_err("failed to create g2d_kthread%d (%pe)\n", i,
				g2d_crtc->commit_worker);

		if (ret) {
			kfree(g2d_crtc);
			return ret;
		}

		g2d_device->sc->crtc[i] = g2d_crtc;

		g2d_crtc->dev = drm->dev;
		g2d_crtc->id = i;

		drm_crtc_helper_add(&g2d_crtc->base, &g2d_crtc_helper_funcs);

		sc_crtc_init(g2d_crtc);

		g2d_qos_create_crtc_qos_properties(g2d_device, g2d_crtc);
	}

	return 0;
}
