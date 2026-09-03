/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */
#ifndef _G2D_CRTC_H_
#define _G2D_CRTC_H_

#include <drm/drm.h>

#include <linux/pm_qos.h>

#include "g2d_qos.h"

#include <linux/kthread.h>

#define to_g2d_crtc_state(state) container_of(state, struct g2d_crtc_state, base)
#define to_g2d_crtc(crtc) container_of(crtc, struct g2d_crtc, base)

struct g2d_crtc_state {
	struct drm_crtc_state base;
	struct kthread_work commit_work;
};

struct g2d_crtc {
	struct drm_crtc base;
	u8 id;
	struct device *dev;
	unsigned int max_bpc;
	unsigned int color_formats; /* supported color format */

	const struct g2d_crtc_funcs *funcs;

	struct dev_pm_qos_request core_devfreq_req;
	struct g2d_qos_config req_qos_config;
	struct g2d_qos_config cur_qos_config;
	struct g2d_qos_config pending_qos_config;
	struct g2d_qos_config last_qos_config;

	struct drm_property *core_clk;
	struct drm_property *rd_avg_bw_mbps;
	struct drm_property *rd_peak_bw_mbps;
	struct drm_property *wr_avg_bw_mbps;
	struct drm_property *wr_peak_bw_mbps;

	struct kthread_worker *commit_worker;
};

struct g2d_crtc_funcs {
	void (*commit)(struct device *dev, struct drm_crtc *crtc);
	void (*enable)(struct device *dev, struct drm_crtc *crtc);
	void (*disable)(struct device *dev, struct drm_crtc *crtc);
};

struct g2d_device;
int g2d_crtc_init(struct g2d_device *g2d_device);

#endif // _G2D_CRTC_H_
