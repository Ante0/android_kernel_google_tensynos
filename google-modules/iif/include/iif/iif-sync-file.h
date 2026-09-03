/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IIF driver sync file.
 *
 * To export fences to the userspace, the driver will allocate a sync file to a fence and will
 * return its file descriptor to the user. The user can distinguish fences with it. The driver will
 * convert the file descriptor to the corresponding fence ID and will pass it to the IP.
 *
 * Copyright (C) 2023-2024 Google LLC
 */

#ifndef __IIF_IIF_SYNC_FILE_H__
#define __IIF_IIF_SYNC_FILE_H__

#include <linux/file.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <iif/iif-fence.h>

/*
 * The tracker which will poll on the fence to be unblocked.
 *
 * Each waiter is expected to create a tracker for each fence to wait on the fence. Especially for
 * reusable fences, waiting on the sync file by multiple waiters concurrently won't notify the fence
 * unblock to the waiters properly since they will share the same timeline viewpoint. This tracker
 * is for separating the timeline viewpoint for each waiter.
 */
struct iif_sync_file_tracker {
	/* File pointer. */
	struct file *file;
	/* The fence object. */
	struct iif_fence *fence;
	/* Queue of polling the file. */
	wait_queue_head_t wq;
	/* Node which will be added to the callback list of the fence. */
	struct iif_fence_poll_cb poll_cb;
	/* Lock for polling reusable fences. */
	spinlock_t poll_lock;
	/* List of fence status of poll callback invocations. */
	struct list_head signaled_list;
	/* Shared mmap status page. */
	struct iif_fence_tracker_status *user_status;
};

/*
 * Sync file which will be exported to the userspace to sync with the fence.
 *
 * To poll the fence, the user can directly poll on the sync file if the fence is single-shot, or
 * the fence is reusable, but the user is the only one polling on the fence. Otherwise, the user
 * must create a tracker for polling the fence with `iif_sync_file_tracker_create()` and poll on
 * the tracker's file.
 */
struct iif_sync_file {
	/* File pointer. */
	struct file *file;
	/* Fence object. */
	struct iif_fence *fence;
	/* The default tracker to poll on the fence directly through the sync file. */
	struct iif_sync_file_tracker tracker;
};

/* Opens a file which will be exported to the userspace to sync with @fence. */
struct iif_sync_file *iif_sync_file_create(struct iif_fence *fence);

/*
 * Gets the sync file from @fd. If @fd is not for iif_sync_file, it will return a negative error
 * pointer.
 *
 * The caller must put the file pointer (i.e., fput(sync_file->file)) to release the file.
 */
struct iif_sync_file *iif_sync_file_fdget(int fd);

/**
 * iif_sync_file_tracker_create() - Opens a file for polling the fence associated with @sync_file.
 * @sync_file: The sync file of the fence to poll.
 *
 * The caller must put the file pointer (i.e., fput(tracker->file)) to release the file.
 *
 * Return: The tracker for polling the fence associated with @sync_file or a negative error pointer.
 */
struct iif_sync_file_tracker *iif_sync_file_tracker_create(struct iif_sync_file *sync_file);

#endif /* __IIF_IIF_SYNC_FILE_H__ */
