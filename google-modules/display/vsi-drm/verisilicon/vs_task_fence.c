// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include <linux/dma-fence.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sync_file.h>

#include "uapi/drm/vs_drm.h"
#include "vs_task_fence.h"

/**
 * struct vs_task_fence - simple fence to assist user space task to sync with
 * the deriver
 */
struct vs_task_fence {
	struct dma_fence base;
	unsigned int context;
	spinlock_t lock;
};

static const char *vs_task_fence_get_driver_name(struct dma_fence *f)
{
	return "vs_drm";
}

static const char *vs_task_fence_get_timeline_name(struct dma_fence *f)
{
	return "vs_task";
}

static void vs_task_fence_release(struct dma_fence *fence)
{
	pr_debug("%s %p\n", __func__, fence);
	dma_fence_free(fence);
}

static const struct dma_fence_ops vs_task_fence_ops = {
	.get_driver_name = vs_task_fence_get_driver_name,
	.get_timeline_name = vs_task_fence_get_timeline_name,
	.release = vs_task_fence_release,
};

static int vs_task_fence_create(int *out_fd)
{
	struct vs_task_fence *fence;
	struct sync_file *sync_file;
	int err;
	int fd;

	if (!out_fd)
		return -EINVAL;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence) {
		err = -ENOMEM;
		goto err;
	}
	pr_debug("%s %p\n", __func__, fence);

	fence->context = dma_fence_context_alloc(1);
	spin_lock_init(&fence->lock);

	dma_fence_init(&fence->base, &vs_task_fence_ops, &fence->lock,
		       fence->context, 0 /* seqno */);

	sync_file = sync_file_create(&fence->base);
	dma_fence_put(&fence->base);
	if (!sync_file) {
		err = -ENOMEM;
		goto err;
	}

	fd_install(fd, sync_file->file);
	*out_fd = fd;

	return 0;

err:
	put_unused_fd(fd);

	return err;
}

static void vs_task_fence_signal(struct dma_fence *fence, int err)
{
	if (err)
		dma_fence_set_error(fence, err);
	dma_fence_signal(fence);
}

int vs_task_fence_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct dma_fence *fence;
	struct drm_vs_task_fence_arg *arg = (struct drm_vs_task_fence_arg *)data;

	if (!data)
		return -EINVAL;

	switch (arg->op) {
	case VS_TASK_FENCE_CREATE:
		return vs_task_fence_create(&arg->fd);

	case VS_TASK_FENCE_SIGNAL:
		fence = sync_file_get_fence(arg->fd);
		if (!fence)
			return -EINVAL;
		vs_task_fence_signal(fence, arg->err);
		dma_fence_put(fence);

		return 0;

	default:
		return -EINVAL;
	}
}
