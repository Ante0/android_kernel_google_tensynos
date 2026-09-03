/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#ifndef __G2D_DRM_H__
#define __G2D_DRM_H__

#include <linux/types.h>

// 9 coefficient regs, 3 Pre Offsets, 3 Y2R Offsets
#define VS_MAX_Y2R_COEF_NUM 15
#define VS_MAX_R2Y_COEF_NUM 15

#define VS_MAX_GAMUT_COEF_NUM 12

struct drm_g2d_plane_hw_caps {
	__u32 hw_id;
	__u32 fe_id;
	__u32 min_width;
	__u32 min_height;
	__u32 max_width;
	__u32 max_height;
	__s32 min_scale;
	__s32 max_scale;
	__u32 axi_id;
};

#endif /* __G2D_DRM_H__ */
