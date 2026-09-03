/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#ifndef G2D_PVRIC_H_
#define G2D_PVRIC_H_

struct drm_format_info;
struct drm_framebuffer;
struct pvric_hw_config;
bool is_buffer_pvric(u64 modifier);
void pvric_populate_hw_config(const struct drm_format_info *info, const struct drm_framebuffer *fb,
			      struct pvric_hw_config *pvric);

#endif /* G2D_PVRIC_H_ */
