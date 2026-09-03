/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#ifndef __VS_DRM_ATOMIC_H__
#define __VS_DRM_ATOMIC_H__

#include <linux/types.h>
#include <drm/drm_atomic.h>
#include <drm/drm_device.h>

int vs_drm_atomic_commit(struct drm_device *dev, struct drm_atomic_state *state, bool nonblock);
void vs_drm_atomic_commit_tail(struct drm_atomic_state *old_state);
int vs_drm_atomic_check(struct drm_device *dev, struct drm_atomic_state *state);
int vs_drm_atomic_disable_all(struct drm_device *dev, struct drm_modeset_acquire_ctx *ctx);
void vs_drm_atomic_print_old_state(const struct drm_atomic_state *state, struct drm_printer *p);

#endif /* __VS_DRM_ATOMIC_H__ */
