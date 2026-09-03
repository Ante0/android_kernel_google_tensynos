/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Google LLC
 */

#ifndef __VS_SYSFS_H__
#define __VS_SYSFS_H__

struct drm_crtc;

int vs_sysfs_create_crtc_files(struct drm_crtc *crtc);
void vs_sysfs_remove_crtc_files(struct drm_crtc *crtc);

#endif /* __VS_SYSFS_H__ */
