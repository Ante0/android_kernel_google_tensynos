/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_DC_PRE_H__
#define __VS_DC_PRE_H__

#include <linux/mm_types.h>
#include <linux/version.h>
#include <drm/drm_modes.h>

#include "vs_dc_hw.h"
#include "vs_plane.h"
#include "vs_dc.h"
#include "vs_dc_sram.h"

extern struct platform_driver dc_fe_platform_driver;

int check_format_limit(struct drm_framebuffer *fb, const struct vs_dc_info *info, u16 width,
		       u16 height, u16 type);
int check_roi_offset_align(struct drm_framebuffer *fb, const struct vs_dc_info *info, u16 offset_x,
			   u16 offset_y);
int dc_fe_power_get(struct device *dev, bool sync);
int dc_fe_power_put(struct device *dev, bool sync);

#endif /* __VS_SUB_DC_FE_H__ */
