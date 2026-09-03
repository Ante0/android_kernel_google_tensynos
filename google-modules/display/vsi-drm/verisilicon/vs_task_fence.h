/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#ifndef __VS_TASK_FENCE_H_
#define __VS_TASK_FENCE_H_

#include <drm/drm_device.h>
#include <drm/drm_file.h>

int vs_task_fence_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);

#endif /* __VS_TASK_FENCE_H_ */
