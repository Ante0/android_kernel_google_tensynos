/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */
#ifndef _G2D_PLANE_H_
#define _G2D_PLANE_H_

#include "g2d_fb.h"

struct g2d_plane;
struct g2d_plane_funcs {
	void (*update)(struct g2d_sc *sc, struct g2d_plane *g2d_plane,
		       struct drm_atomic_state *state);
	int (*check)(struct drm_device *dev, struct g2d_plane *plane,
		     struct drm_plane_state *state);
};

struct g2d_plane_state {
	struct drm_plane_state base;

	struct drm_property_blob *y2r_coef;
	struct drm_property_blob *r2y_coef;
};

struct g2d_plane {
	struct drm_plane base;
	u8 id;
	struct device *dev;
	dma_addr_t dma_addr[MAX_NUM_PLANES];
	const struct g2d_plane_funcs *funcs;

	struct drm_property *hw_caps_prop;
};

struct g2d_plane *g2d_plane_init(struct g2d_device *g2d_device, unsigned int possible_crtcs,
				 int idx);
#define to_g2d_plane(plane) container_of(plane, struct g2d_plane, base)
#define to_g2d_plane_state(state) container_of(state, struct g2d_plane_state, base)

#endif // _G2D_PLANE_H_
