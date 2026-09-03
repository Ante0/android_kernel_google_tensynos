// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */

#include <drm/drm_atomic.h>

#include <interconnect/google_icc_helper.h>

#include <linux/devfreq.h>
#include <linux/of.h>
#include <linux/pm_qos.h>
#include <linux/units.h>
#include <perf/core/google_pm_qos.h>
#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
#include <perf/core/perf_domain.h>
#endif

#include "g2d_crtc.h"
#include "g2d_drv.h"
#include "g2d_qos.h"
#include "g2d_sc.h"

#define SOFT_REALTIME_VIRTUAL_CHANNEL 1

void g2d_qos_init(struct device *dev, struct g2d_device *g2d_device)
{
#if !IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
	struct device_link *link = NULL;
#else
	int ret;
#endif

	mutex_init(&g2d_device->g2d_qos_lock);

#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
	g2d_device->core_devfreq = gs_perf_domain_find_devfreq("g2d", GS_RECOMMENDED_DEVFREQ);
	if (IS_ERR(g2d_device->core_devfreq)) {
		ret = PTR_ERR(g2d_device->core_devfreq);
		if (ret == -ENODEV)
			dev_err(dev, "Failed to get core_devfreq source, ret=%d\n", ret);
		g2d_device->core_devfreq = NULL;
	}
#else /* CONFIG_GS_PERF_DOMAIN */
	if (of_property_present(dev->of_node, "devfreq")) {
		g2d_device->core_devfreq = devfreq_get_devfreq_by_phandle(dev, "devfreq", 0);
		if (IS_ERR(g2d_device->core_devfreq)) {
			dev_err(dev, "failed to get core_devfreq source, ret=%ld",
				PTR_ERR(g2d_device->core_devfreq));
			g2d_device->core_devfreq = NULL;
		} else {
			link = device_link_add(dev, g2d_device->core_devfreq->dev.parent,
					       DL_FLAG_AUTOREMOVE_CONSUMER | DL_FLAG_PM_RUNTIME);
			if (!link)
				dev_err(dev, "failed to add devlink to the devfreq dev\n");

			of_property_read_u32(dev->of_node, "min-core-clk",
					     &g2d_device->min_qos_config.core_clk);
		}
	} else {
		dev_err(dev, "core_devfreq not presented in dts");
		g2d_device->core_devfreq = NULL;
	}
#endif /* CONFIG_GS_PERF_DOMAIN */

	g2d_device->icc_path = google_devm_of_icc_get(dev, "sswrp-g2d");
	if (IS_ERR_OR_NULL(g2d_device->icc_path)) {
		dev_err(dev, "failed to get icc path: %ld\n", PTR_ERR(g2d_device->icc_path));
		g2d_device->icc_path = NULL;
	} else {
		of_property_read_u32(dev->of_node, "min-rd-avg-bw",
				     &g2d_device->min_qos_config.rd_avg_bw_mbps);
		of_property_read_u32(dev->of_node, "min-rd-peak-bw",
				     &g2d_device->min_qos_config.rd_peak_bw_mbps);
		of_property_read_u32(dev->of_node, "min-wr-avg-bw",
				     &g2d_device->min_qos_config.wr_avg_bw_mbps);
		of_property_read_u32(dev->of_node, "min-wr-peak-bw",
				     &g2d_device->min_qos_config.wr_peak_bw_mbps);
	}
}

static int g2d_qos_add_devfreq_request(struct g2d_device *g2d_device, struct g2d_crtc *g2d_crtc)
{
	int ret;

	if (!g2d_device->core_devfreq)
		return -EINVAL;

#if IS_ENABLED(CONFIG_GOOGLE_PM_QOS)
	ret = google_pm_qos_add_devfreq_request(g2d_device->core_devfreq,
						&g2d_crtc->core_devfreq_req,
						DEV_PM_QOS_MIN_FREQUENCY,
						PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
#else
	ret = dev_pm_qos_add_request(g2d_device->core_devfreq->dev.parent,
				     &g2d_crtc->core_devfreq_req, DEV_PM_QOS_MIN_FREQUENCY,
				     PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
#endif

	if (ret)
		dev_err(g2d_crtc->dev, "fail to add devfreq, %d", ret);

	return ret;
}

int g2d_qos_remove_devfreq_request(struct drm_crtc *crtc)
{
	struct drm_device *drm = crtc->dev;
	struct g2d_device *g2d_device = to_g2d_device(drm);
	struct g2d_crtc *g2d_crtc = to_g2d_crtc(crtc);

	int ret;

	if (!g2d_device->core_devfreq)
		return -EINVAL;

#if IS_ENABLED(CONFIG_GOOGLE_PM_QOS)
	ret = google_pm_qos_remove_devfreq_request(g2d_device->core_devfreq,
						   &g2d_crtc->core_devfreq_req);
#else
	ret = dev_pm_qos_remove_request(&g2d_crtc->core_devfreq_req);
#endif

	if (ret)
		dev_warn(g2d_crtc->dev, "fail to remove devfreq, %d", ret);

	return ret;
}

void g2d_qos_create_crtc_qos_properties(struct g2d_device *g2d_device, struct g2d_crtc *g2d_crtc)
{
	struct drm_device *drm = &g2d_device->drm;

	if (g2d_device->core_devfreq) {
		g2d_qos_add_devfreq_request(g2d_device, g2d_crtc);
		g2d_crtc->core_clk = drm_property_create_range(
			drm, 0, "CORE_CLOCK_HZ", 0, g2d_device->core_devfreq->scaling_max_freq);
		if (g2d_crtc->core_clk)
			drm_object_attach_property(&g2d_crtc->base.base, g2d_crtc->core_clk, 0);
		else
			dev_err(drm->dev, "CRTC%d: create CORE_CLOCK_HZ fail\n", g2d_crtc->id);
	}

	if (!IS_ERR_OR_NULL(g2d_device->icc_path)) {
		g2d_crtc->rd_avg_bw_mbps =
			drm_property_create_range(drm, 0, "RD_AVG_BW_MBPS", 0, UINT_MAX);
		if (g2d_crtc->rd_avg_bw_mbps)
			drm_object_attach_property(&g2d_crtc->base.base, g2d_crtc->rd_avg_bw_mbps,
						   0);
		else
			dev_err(drm->dev, "CRTC%d: create RD_AVG_BW_MBPS fail\n", g2d_crtc->id);

		g2d_crtc->rd_peak_bw_mbps =
			drm_property_create_range(drm, 0, "RD_PEAK_BW_MBPS", 0, UINT_MAX);
		if (g2d_crtc->rd_peak_bw_mbps)
			drm_object_attach_property(&g2d_crtc->base.base, g2d_crtc->rd_peak_bw_mbps,
						   0);
		else
			dev_err(drm->dev, "CRTC%d: create RD_PEAK_BW_MBPS fail\n", g2d_crtc->id);

		g2d_crtc->wr_avg_bw_mbps =
			drm_property_create_range(drm, 0, "WR_AVG_BW_MBPS", 0, UINT_MAX);
		if (g2d_crtc->wr_avg_bw_mbps)
			drm_object_attach_property(&g2d_crtc->base.base, g2d_crtc->wr_avg_bw_mbps,
						   0);
		else
			dev_err(drm->dev, "CRTC%d: create WR_AVG_BW_MBPS fail\n", g2d_crtc->id);

		g2d_crtc->wr_peak_bw_mbps =
			drm_property_create_range(drm, 0, "WR_PEAK_BW_MBPS", 0, UINT_MAX);
		if (g2d_crtc->wr_peak_bw_mbps)
			drm_object_attach_property(&g2d_crtc->base.base, g2d_crtc->wr_peak_bw_mbps,
						   0);
		else
			dev_err(drm->dev, "CRTC%d: create WR_PEAK_BW_MBPS fail\n", g2d_crtc->id);
	}
}

static bool update_qos_config(u32 *req, u32 *pending, u32 cur)
{
	bool qos_updated = false;

	if (*req == cur) {
		*pending = 0;
	} else if (*req > cur) {
		*pending = 0;
		qos_updated = true;
	} else if (*pending) {
		if (*req >= *pending) {
			*pending = 0;
			qos_updated = true;
		} else {
			/* apply and update pending qos resource */
			u32 new_pending = *req;
			*req = *pending;
			*pending = new_pending;
			qos_updated = true;
		}
	} else {
		*pending = *req;
	}

	return qos_updated;
}

static bool g2d_override_min_core_clk(struct g2d_device *g2d_device, u32 *core_clk)
{
	bool updated = false;
	u32 min_core_clk = g2d_device->min_qos_config.core_clk;

	/* don't override when releasing voting */
	if (*core_clk == 0)
		return false;

	if (*core_clk < min_core_clk) {
		*core_clk = min_core_clk;
		updated = true;
	}

	return updated;
}

static bool g2d_override_min_bw(struct g2d_device *g2d_device, u32 *avg_bw, u32 *peak_bw,
				bool is_read)
{
	bool updated = false;
	u32 min_avg_bw, min_peak_bw;

	/* don't override when releasing voting */
	if (*avg_bw == 0 && *peak_bw == 0)
		return false;

	if (is_read) {
		min_avg_bw = g2d_device->min_qos_config.rd_avg_bw_mbps;
		min_peak_bw = g2d_device->min_qos_config.rd_peak_bw_mbps;
	} else {
		min_avg_bw = g2d_device->min_qos_config.wr_avg_bw_mbps;
		min_peak_bw = g2d_device->min_qos_config.wr_peak_bw_mbps;
	}

	if (*avg_bw < min_avg_bw) {
		*avg_bw = min_avg_bw;
		updated = true;
	}

	if (*peak_bw < min_peak_bw) {
		*peak_bw = min_peak_bw;
		updated = true;
	}

	return updated;
}

static void g2d_qos_calculate_sum_rd_bw(struct g2d_device *g2d_device, u32 *avg_bw, u32 *peak_bw,
					u8 id)
{
	int i;

	for (i = 0; i < NUM_PIPELINES; i++) {
		if (!g2d_device->sc->crtc[i] || i == id)
			continue;
		*avg_bw += g2d_device->sc->crtc[i]->cur_qos_config.rd_avg_bw_mbps;
		*peak_bw += g2d_device->sc->crtc[i]->cur_qos_config.rd_peak_bw_mbps;
	}
}

static void g2d_qos_calculate_sum_wr_bw(struct g2d_device *g2d_device, u32 *avg_bw, u32 *peak_bw,
					u8 id)
{
	int i;

	for (i = 0; i < NUM_PIPELINES; i++) {
		if (!g2d_device->sc->crtc[i] || i == id)
			continue;
		*avg_bw += g2d_device->sc->crtc[i]->cur_qos_config.wr_avg_bw_mbps;
		*peak_bw += g2d_device->sc->crtc[i]->cur_qos_config.wr_peak_bw_mbps;
	}
}

static bool g2d_qos_set_rd_bw(struct g2d_device *g2d_device, struct g2d_crtc *g2d_crtc,
			      bool force_updated)
{
	u32 rd_avg_bw_mbps = g2d_crtc->req_qos_config.rd_avg_bw_mbps;
	u32 rd_peak_bw_mbps = g2d_crtc->req_qos_config.rd_peak_bw_mbps;
	u32 req_rd_avg_bw_mbps, req_rd_peak_bw_mbps;
	bool rd_bw_updated = force_updated;
	int ret;

	rd_bw_updated |= update_qos_config(&rd_avg_bw_mbps,
					   &g2d_crtc->pending_qos_config.rd_avg_bw_mbps,
					   g2d_crtc->cur_qos_config.rd_avg_bw_mbps);
	rd_bw_updated |= update_qos_config(&rd_peak_bw_mbps,
					   &g2d_crtc->pending_qos_config.rd_peak_bw_mbps,
					   g2d_crtc->cur_qos_config.rd_peak_bw_mbps);
	rd_bw_updated |= g2d_override_min_bw(g2d_device, &rd_avg_bw_mbps, &rd_peak_bw_mbps, true);

	if (rd_bw_updated) {
		req_rd_avg_bw_mbps = rd_avg_bw_mbps;
		req_rd_peak_bw_mbps = rd_peak_bw_mbps;
		g2d_qos_calculate_sum_rd_bw(g2d_device, &req_rd_avg_bw_mbps, &req_rd_peak_bw_mbps,
					    g2d_crtc->id);
		ret = google_icc_set_read_bw_gmc(g2d_device->icc_path, req_rd_avg_bw_mbps,
						 req_rd_peak_bw_mbps, 0,
						 SOFT_REALTIME_VIRTUAL_CHANNEL);
		if (ret) {
			rd_bw_updated = false;
			dev_err(g2d_crtc->dev, "failed to set read bandwidth, %d\n", ret);
			goto end;
		}
		g2d_crtc->cur_qos_config.rd_avg_bw_mbps = rd_avg_bw_mbps;
		g2d_crtc->cur_qos_config.rd_peak_bw_mbps = rd_peak_bw_mbps;
	}

end:
	return rd_bw_updated;
}

static bool g2d_qos_set_wr_bw(struct g2d_device *g2d_device, struct g2d_crtc *g2d_crtc,
			      bool force_updated)
{
	u32 wr_avg_bw_mbps = g2d_crtc->req_qos_config.wr_avg_bw_mbps;
	u32 wr_peak_bw_mbps = g2d_crtc->req_qos_config.wr_peak_bw_mbps;
	u32 req_wr_avg_bw_mbps, req_wr_peak_bw_mbps;
	bool wr_bw_updated = force_updated;
	int ret;

	wr_bw_updated |= update_qos_config(&wr_avg_bw_mbps,
					   &g2d_crtc->pending_qos_config.wr_avg_bw_mbps,
					   g2d_crtc->cur_qos_config.wr_avg_bw_mbps);
	wr_bw_updated |= update_qos_config(&wr_peak_bw_mbps,
					   &g2d_crtc->pending_qos_config.wr_peak_bw_mbps,
					   g2d_crtc->cur_qos_config.wr_peak_bw_mbps);
	wr_bw_updated |= g2d_override_min_bw(g2d_device, &wr_avg_bw_mbps, &wr_peak_bw_mbps, false);

	if (wr_bw_updated) {
		req_wr_avg_bw_mbps = wr_avg_bw_mbps;
		req_wr_peak_bw_mbps = wr_peak_bw_mbps;
		g2d_qos_calculate_sum_wr_bw(g2d_device, &req_wr_avg_bw_mbps, &req_wr_peak_bw_mbps,
					    g2d_crtc->id);
		ret = google_icc_set_write_bw_gmc(g2d_device->icc_path, req_wr_avg_bw_mbps,
						  req_wr_peak_bw_mbps, 0,
						  SOFT_REALTIME_VIRTUAL_CHANNEL);
		if (ret) {
			wr_bw_updated = false;
			dev_err(g2d_crtc->dev, "failed to set write bandwidth, %d\n", ret);
			goto end;
		}
		g2d_crtc->cur_qos_config.wr_avg_bw_mbps = wr_avg_bw_mbps;
		g2d_crtc->cur_qos_config.wr_peak_bw_mbps = wr_peak_bw_mbps;
	}

end:
	return wr_bw_updated;
}

static void g2d_qos_set_bw(struct g2d_device *g2d_device, struct g2d_crtc *g2d_crtc,
			   bool force_updated)
{
	bool rd_bw_updated, wr_bw_updated;
	int ret;

	if (IS_ERR_OR_NULL(g2d_device->icc_path))
		return;

	rd_bw_updated = g2d_qos_set_rd_bw(g2d_device, g2d_crtc, force_updated);
	wr_bw_updated = g2d_qos_set_wr_bw(g2d_device, g2d_crtc, force_updated);
	if (rd_bw_updated || wr_bw_updated) {
		ret = google_icc_update_constraint_async(g2d_device->icc_path);
		if (ret)
			dev_err(g2d_crtc->dev, "failed to update bandwidth, %d\n", ret);
	}
}

static void g2d_qos_set_core_clk(struct g2d_device *g2d_device, struct g2d_crtc *g2d_crtc,
				 bool force_updated)
{
	u32 core_clk = g2d_crtc->req_qos_config.core_clk;
	u32 core_clk_khz;
	bool core_clk_updated = force_updated;
	int ret = 0;

	if (!g2d_device->core_devfreq)
		return;

	core_clk_updated |= update_qos_config(&core_clk, &g2d_crtc->pending_qos_config.core_clk,
					      g2d_crtc->cur_qos_config.core_clk);
	core_clk_updated |= g2d_override_min_core_clk(g2d_device, &core_clk);

	if (core_clk_updated) {
		core_clk_khz = core_clk / HZ_PER_KHZ;
		ret = dev_pm_qos_update_request(&g2d_crtc->core_devfreq_req, core_clk_khz);

		if (ret < 0)
			dev_err(g2d_crtc->dev, "failed to update core_clk to %u kHz, %d\n",
				core_clk_khz, ret);
		else
			g2d_crtc->cur_qos_config.core_clk = core_clk;
	}
}

static void __g2d_qos_set_qos_config_locked(struct g2d_device *g2d_device,
					    struct g2d_crtc *g2d_crtc, bool force_updated)
{
	g2d_qos_set_bw(g2d_device, g2d_crtc, force_updated);
	g2d_qos_set_core_clk(g2d_device, g2d_crtc, force_updated);
}

void g2d_qos_set_qos_config(struct g2d_device *g2d_device, struct drm_crtc *crtc)
{
	struct g2d_crtc *g2d_crtc = to_g2d_crtc(crtc);

	mutex_lock(&g2d_device->g2d_qos_lock);
	__g2d_qos_set_qos_config_locked(g2d_device, g2d_crtc, false);
	mutex_unlock(&g2d_device->g2d_qos_lock);
}

void g2d_qos_clear_qos_configs(struct g2d_device *g2d_device, struct drm_crtc *crtc)
{
	struct g2d_crtc *g2d_crtc = to_g2d_crtc(crtc);

	mutex_lock(&g2d_device->g2d_qos_lock);
	memcpy(&g2d_crtc->last_qos_config, &g2d_crtc->req_qos_config,
	       sizeof(g2d_crtc->last_qos_config));
	memset(&g2d_crtc->req_qos_config, 0, sizeof(g2d_crtc->req_qos_config));
	memset(&g2d_crtc->cur_qos_config, 0, sizeof(g2d_crtc->cur_qos_config));
	memset(&g2d_crtc->pending_qos_config, 0, sizeof(g2d_crtc->pending_qos_config));
	__g2d_qos_set_qos_config_locked(g2d_device, g2d_crtc, true);
	mutex_unlock(&g2d_device->g2d_qos_lock);
}

void g2d_qos_restore_qos_configs(struct g2d_device *g2d_device, struct drm_crtc *crtc)
{
	struct g2d_crtc *g2d_crtc = to_g2d_crtc(crtc);

	mutex_lock(&g2d_device->g2d_qos_lock);
	if (g2d_crtc->req_qos_config.core_clk == 0 && g2d_crtc->last_qos_config.core_clk != 0) {
		memcpy(&g2d_crtc->req_qos_config, &g2d_crtc->last_qos_config,
		       sizeof(g2d_crtc->req_qos_config));
		memset(&g2d_crtc->last_qos_config, 0, sizeof(g2d_crtc->last_qos_config));
	}
	mutex_unlock(&g2d_device->g2d_qos_lock);
}
