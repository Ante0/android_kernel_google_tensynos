/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_PLANE_H__
#define __VS_PLANE_H__

#include <linux/bitops.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_plane.h>
#include <drm/drm_plane_helper.h>
#include <drm/vs_drm.h>

#include "vs_fb.h"
#include "vs_dc_info.h"
#include "vs_dc_hw.h"
#include "vs_dc_property.h"
#include "vs_dc_drm_property.h"

#define MAX_NUM_PLANES 3 /* colour format plane */

struct vs_plane;
struct vs_gem_object;

struct vs_plane_funcs {
	void (*update)(struct device *dev, struct vs_plane *plane);
#if IS_ENABLED(CONFIG_DEBUG_FS)
	void (*set_pattern)(struct device *dev, struct vs_plane *plane);
	void (*set_qos)(struct device *dev, struct vs_plane *plane, const char __user *ubuf,
			size_t len);
	int (*show_qos)(struct seq_file *s);
#endif /* CONFIG_DEBUG_FS */
	void (*disable)(struct device *dev, struct vs_plane *plane);
	int (*check)(struct device *dev, struct vs_plane *plane, struct drm_plane_state *state);
	bool (*format_mod_support)(struct device *dev, struct vs_plane *plane, u32 format,
				   u64 modifier);
};

struct vs_plane_status {
	u32 tile_mode;
	struct drm_rect src;
	struct drm_rect dest;
};

struct vs_plane_pattern {
	bool enable;
	u8 mode;
	u64 color;
	struct drm_vs_rect rect;
};

struct vs_plane_sram_pool {
	u32 sp_handle;
	u32 sp_size;
	u8 sp_unit_size;
	u32 scl_sp_handle;
	u32 scl_sp_size;
};

enum vs_plane_changed {
	VS_PLANE_CHANGED_LUT_3D = 0,
	VS_PLANE_CHANGED_SCALING,
	VS_PLANE_CHANGED_SCALING_COEFF,
	VS_PLANE_CHANGED_Y2R,
	VS_PLANE_CHANGED_CLEAR,
	VS_PLANE_CHANGED_MAX,
};

struct vs_plane_state {
	struct drm_plane_state base;
	struct vs_plane_status status; /* for debugfs */
	struct vs_plane_pattern pattern; /* for pattern debugfs */
#ifdef CONFIG_DEBUG_FS
	struct dc_hw_plane_qos qos;
#endif

	struct drm_property_blob *watermark;
	struct drm_property_blob *y2r_coef;
	struct drm_property_blob *lut_3d;
	struct drm_property_blob *clear;
	struct drm_framebuffer *fb_ext;

	DECLARE_BITMAP(changed, VS_PLANE_CHANGED_MAX);

	struct vs_drm_property_state drm_states[VS_DC_MAX_PROPERTY_NUM];
	u32 blend_id;
	u32 dma_sram_size;
	u32 scl_sram_size;

	/**
	 * @pre_swap_crtc: A pointer to the CRTC that this plane was attached to
	 * when it was previously enabled. This is used to detect when a plane is
	 * moved between CRTCs, which requires special synchronization.
	 */
	struct drm_crtc *pre_swap_crtc;
};

struct vs_plane {
	struct drm_plane base;
	u8 id;
	struct device *dev;
	dma_addr_t dma_addr[MAX_NUM_PLANES];

#if IS_ENABLED(CONFIG_DEBUG_FS)
	/**
	 * @debugfs_entry:
	 *
	 * Debugfs directory for this plane.
	 */
	struct dentry *debugfs_entry;
#endif

	struct drm_property *degamma_mode_prop;
	struct drm_property *watermark_prop;
	struct drm_property *y2r_prop;
	struct drm_property *lut_3d_prop;
	struct drm_property *ext_layer_prop;
	struct drm_property *clear_prop;
	struct drm_property *hw_caps_prop;

	struct vs_drm_property_group properties;

	struct vs_plane_sram_pool sram;

	const struct vs_plane_funcs *funcs;
};

void vs_plane_destroy(struct drm_plane *plane);

struct vs_plane *vs_plane_create(const struct dc_hw_plane *hw_plane, struct drm_device *drm_dev,
				 const struct vs_dc_info *info, enum drm_plane_type plane_type,
				 u8 index, unsigned int possible_crtcs,
				 const struct vs_plane_funcs *dc_plane_funcs);

#define to_vs_plane(state) container_of(state, struct vs_plane, base)
#define to_vs_plane_state(state) container_of(state, struct vs_plane_state, base)
#endif /* __VS_PLANE_H__ */
