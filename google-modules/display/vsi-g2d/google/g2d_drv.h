/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#ifndef _G2D_DRV_H_
#define _G2D_DRV_H_

#include <linux/devfreq.h>
#include <drm/drm.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_writeback.h>

#include "g2d_qos.h"

#define to_g2d_device(drm) container_of(drm, struct g2d_device, drm)

struct g2d_sc;
struct g2d_device {
	struct drm_device drm;
	struct g2d_sc *sc;
	struct google_icc_path *icc_path;
	struct devfreq *core_devfreq;
	struct mutex g2d_qos_lock; /* protect qos update */
	struct g2d_qos_config min_qos_config;

	struct dentry *debugfs;
};

#endif /* _G2D_DRV_H_ */
