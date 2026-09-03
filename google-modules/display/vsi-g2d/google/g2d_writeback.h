/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#ifndef _G2D_WRITEBACK_H_
#define _G2D_WRITEBACK_H_

#include <drm/drm.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_writeback.h>

// Todo(b/390265640): Move to dts
#define MAX_WB_NUM_PLANES 3 /* color format plane */

#define to_g2d_writeback_connector(wb) container_of(wb, struct g2d_writeback_connector, base)

struct g2d_writeback_connector {
	struct drm_writeback_connector base;
	struct device *dev;

	dma_addr_t dma_addr[MAX_WB_NUM_PLANES];
	unsigned int pitch[MAX_WB_NUM_PLANES];
	u32 is_yuv;

	const struct g2d_writeback_funcs *funcs;
	u8 armed;
	u8 id;
};

struct g2d_writeback_funcs {
	void (*commit)(struct g2d_writeback_connector *wb_connector, struct drm_framebuffer *fb);
	int (*check)(struct drm_device *drm_dev, const struct drm_framebuffer *fb);
};

struct g2d_device;
int g2d_enable_writeback_connector(struct g2d_device *g2d_device, uint32_t possible_crtcs);
void g2d_handle_writeback_frm_done(struct g2d_writeback_connector *g2d_wb_connector);
void g2d_signal_wb_error(struct g2d_writeback_connector *g2d_wb_connector);

#endif // _G2D_WRITEBACK_H_
