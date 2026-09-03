/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */
#pragma once

#if IS_ENABLED(CONFIG_POWERVR_PIXEL_IIF)
int fence_manager_init(struct pixel_gpu_device *pixel_dev);

void fence_manager_term(struct pixel_gpu_device *pixel_dev);

void fence_manager_iif_retire(struct pixel_gpu_device *pixel_dev,
			      union pixel_rgxfwif_iif_handle const *handle);

union pixel_rgxfwif_iif_handle fence_manager_extract_iif(
	struct pixel_gpu_device *pixel_dev, struct dma_fence *fence);
#else
static __maybe_unused int fence_manager_init(struct pixel_gpu_device *pixel_dev)
{
	return 0;
}

static __maybe_unused void fence_manager_term(struct pixel_gpu_device *pixel_dev)
{
}

static __maybe_unused void
fence_manager_iif_retire(struct pixel_gpu_device *pixel_dev,
			 union pixel_rgxfwif_iif_handle const *handle)
{
}

static __maybe_unused union pixel_rgxfwif_iif_handle fence_manager_extract_iif(
	struct pixel_gpu_device *pixel_dev, struct dma_fence *fence)
{
	union pixel_rgxfwif_iif_handle handle = {0};
	return handle;
}
#endif /* IS_ENABLED(CONFIG_POWERVR_PIXEL_IIF) */
