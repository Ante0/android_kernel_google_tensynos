// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIF driver sync file.
 *
 * To export fences to the userspace, the driver will allocate a sync file to a fence and will
 * return its file descriptor to the user. The user can distinguish fences with it. The driver will
 * convert the file descriptor to the corresponding fence ID and will pass it to the IP.
 *
 * Copyright (C) 2023-2024 Google LLC
 */

#include <linux/anon_inodes.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sync_file.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <iif/iif-dma-fence.h>
#include <iif/iif-fence.h>
#include <iif/iif-sync-file.h>
#include <iif/iif.h>

/*
 * struct iif_sync_file_signaled - Store the fence status of each poll callback invocation.
 * @status: The status of the fence when the poll callback was invoked.
 * @node: The list node to be tracked at @signaled_list.
 */
struct iif_sync_file_signaled {
	struct iif_fence_status status;
	struct list_head node;
};

static void iif_sync_file_tracker_fence_signaled(struct iif_fence *fence,
						 struct iif_fence_poll_cb *poll_cb)
{
	struct iif_sync_file_tracker *tracker =
		container_of(poll_cb, struct iif_sync_file_tracker, poll_cb);
	struct iif_sync_file_signaled *signaled;

	signaled = kzalloc(sizeof(*signaled), GFP_ATOMIC);
	if (!signaled) {
		iif_warn(fence, "Failed to allocate memory for reusable fence poll");
		return;
	}

	scoped_guard(spinlock_irqsave, &tracker->poll_lock) {
		signaled->status = poll_cb->status;
		list_add_tail(&signaled->node, &tracker->signaled_list);
	}

	wake_up_all(&tracker->wq);
}

static int iif_sync_file_tracker_init(struct iif_sync_file_tracker *tracker,
				      struct iif_fence *fence)
{
	/*
	 * Allocate a page-aligned page using vmalloc_user. Since this page will be mapped to the
	 * userspace via mmap(), using a slab allocation (like kzalloc) is prohibited to prevent
	 * leaking other kernel objects residing in the same slab cache page.
	 */
	tracker->user_status = vmalloc_user(PAGE_SIZE);
	if (!tracker->user_status)
		return -ENOMEM;

	tracker->fence = fence;
	init_waitqueue_head(&tracker->wq);
	INIT_LIST_HEAD(&tracker->poll_cb.node);
	spin_lock_init(&tracker->poll_lock);
	INIT_LIST_HEAD(&tracker->signaled_list);

	return 0;
}

static void iif_sync_file_tracker_exit(struct iif_sync_file_tracker *tracker)
{
	struct iif_sync_file_signaled *cur, *nxt;

	iif_fence_remove_poll_callback(tracker->fence, &tracker->poll_cb);

	/* Clean-up @signaled_list. It is safe to not hold the lock, but follow the convention. */
	scoped_guard(spinlock_irqsave, &tracker->poll_lock) {
		list_for_each_entry_safe(cur, nxt, &tracker->signaled_list, node) {
			list_del(&cur->node);
			kfree(cur);
		}
	}

	vfree(tracker->user_status);
}

static int iif_sync_file_tracker_release(struct inode *inode, struct file *file)
{
	struct iif_sync_file_tracker *tracker = file->private_data;

	iif_sync_file_tracker_exit(tracker);
	if (atomic_dec_and_test(&tracker->fence->num_sync_file))
		iif_fence_on_sync_file_release(tracker->fence);
	/* Puts the fence refcount held at `iif_sync_file_tracker_create()`. */
	iif_fence_put(tracker->fence);
	kfree(tracker);

	return 0;
}

static void iif_sync_file_tracker_update_status(struct iif_sync_file_tracker *tracker,
						const struct iif_fence_status *status)
{
	if (tracker->fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		tracker->user_status->signaled = status->signaled;
	else
		tracker->user_status->timeline = status->timeline;

	tracker->user_status->error = status->error;
}

static __poll_t iif_sync_file_tracker_do_poll(struct iif_sync_file_tracker *tracker,
					      struct file *file, poll_table *wait)
{
	struct iif_fence *fence = tracker->fence;
	struct iif_fence_status *status = &tracker->poll_cb.status;
	struct iif_sync_file_signaled *signaled = NULL;
	int ret;

	if (unlikely(fence->params.flags & IIF_FLAGS_DISABLE_POLL))
		return 0;

	poll_wait(file, &tracker->wq, wait);

	scoped_guard(spinlock_irqsave, &tracker->poll_lock) {
		if (!list_empty(&tracker->signaled_list)) {
			signaled = list_first_entry(&tracker->signaled_list,
						    struct iif_sync_file_signaled, node);
			iif_sync_file_tracker_update_status(tracker, &signaled->status);
			list_del(&signaled->node);
			kfree(signaled);
		}
	}

	/*
	 * If one of these conditions meets, wake up the user directly:
	 * 1. @signaled is not NULL: There were fence unblock events which haven't been notified to
	 *    the userspace yet.
	 * 2. The single-shot fence is signaled: If single-shot fence is once signaled, it will
	 *    never be blocked anymore.
	 * 3. The reusable fence is errored: If there aren't any pending unblock events (i.e.,
	 *    @signaled is NULL) and the reusable fence is errored, it will never be blocked
	 *    anymore.
	 */
	if (signaled ||
	    (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT && status->signaled) ||
	    (fence->params.fence_type == IIF_FENCE_TYPE_REUSABLE && status->error))
		return EPOLLIN;

	if (list_empty(&tracker->poll_cb.node)) {
		ret = iif_fence_add_poll_callback(fence, &tracker->poll_cb,
						  iif_sync_file_tracker_fence_signaled);
		/* If the fence was already signaled, just wake up all. */
		if (ret < 0) {
			iif_sync_file_tracker_update_status(tracker, status);
			wake_up_all(&tracker->wq);
			return EPOLLIN;
		}
	}

	return 0;
}

static __poll_t iif_sync_file_tracker_poll(struct file *file, poll_table *wait)
{
	struct iif_sync_file_tracker *tracker = file->private_data;

	return iif_sync_file_tracker_do_poll(tracker, file, wait);
}

static int iif_sync_file_tracker_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct iif_sync_file_tracker *tracker = file->private_data;

	if (vma->vm_end - vma->vm_start > PAGE_SIZE)
		return -EINVAL;

	return remap_vmalloc_range(vma, tracker->user_status, 0);
}

static const struct file_operations iif_sync_file_tracker_fops = {
	.release = iif_sync_file_tracker_release,
	.poll = iif_sync_file_tracker_poll,
	.mmap = iif_sync_file_tracker_mmap,
};

static int iif_sync_file_release(struct inode *inode, struct file *file)
{
	struct iif_sync_file *sync_file = file->private_data;

	iif_sync_file_tracker_exit(&sync_file->tracker);
	if (atomic_dec_and_test(&sync_file->fence->num_sync_file))
		iif_fence_on_sync_file_release(sync_file->fence);
	iif_fence_put(sync_file->fence);
	kfree(sync_file);

	return 0;
}

static __poll_t iif_sync_file_poll(struct file *file, poll_table *wait)
{
	struct iif_sync_file *sync_file = file->private_data;

	return iif_sync_file_tracker_do_poll(&sync_file->tracker, file, wait);
}

static int iif_sync_file_ioctl_get_information(struct iif_sync_file *sync_file,
					       struct iif_fence_get_information_ioctl __user *argp)
{
	struct iif_fence *fence = sync_file->fence;
	const struct iif_fence_get_information_ioctl ibuf = {
		.signaler_ip = fence->params.signaler_ip,
		.total_signalers = fence->params.remaining_signalers,
		.submitted_signalers = iif_fence_submitted_signalers(fence),
		.signaled_signalers = iif_fence_signaled_signalers(fence),
		.outstanding_waiters = iif_fence_outstanding_waiters(fence),
		.signal_status = iif_fence_get_signal_status(fence),
	};

	if (copy_to_user(argp, &ibuf, sizeof(ibuf)))
		return -EFAULT;

	return 0;
}

static int iif_sync_file_ioctl_submit_signaler(struct iif_sync_file *sync_file)
{
	struct iif_fence *fence = sync_file->fence;

	if (fence->params.signaler_ip != IIF_IP_AP && !fence->propagate) {
		iif_err(fence,
			"Only fences with signaler type AP are allowed to submit a signaler (signaler_ip=%d)\n",
			fence->params.signaler_ip);
		return -EPERM;
	}

	return iif_fence_submit_signaler(fence);
}

static int iif_sync_file_ioctl_signal(struct iif_sync_file *sync_file,
				      struct iif_fence_signal_ioctl __user *argp)
{
	struct iif_fence_signal_ioctl ibuf;
	struct iif_fence *fence = sync_file->fence;
	int ret;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	if (fence->params.signaler_ip != IIF_IP_AP && !fence->propagate) {
		iif_err(fence,
			"Only fences with signaler type AP are allowed to signal (signaler_ip=%d)\n",
			fence->params.signaler_ip);
		return -EPERM;
	}

	ret = iif_fence_signal_with_status(fence, ibuf.error);
	if (ret < 0)
		return ret;

	ibuf.remaining_signals = ret;

	if (copy_to_user(argp, &ibuf, sizeof(ibuf)))
		return -EFAULT;

	return 0;
}

static int iif_sync_file_ioctl_set_flags(struct iif_sync_file *sync_file,
					 struct iif_fence_set_flags_ioctl __user *argp)
{
	struct iif_fence_set_flags_ioctl ibuf;
	struct iif_fence *fence = sync_file->fence;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	return iif_fence_set_flags(fence, ibuf.flags, ibuf.clear);
}

static int iif_sync_file_ioctl_get_flags(struct iif_sync_file *sync_file, u32 __user *argp)
{
	struct iif_fence *fence = sync_file->fence;

	if (copy_to_user(argp, &fence->params.flags, sizeof(fence->params.flags)))
		return -EFAULT;

	return 0;
}

static int iif_sync_file_ioctl_bridge_dma_fence(struct iif_sync_file *sync_file, s32 __user *argp)
{
	struct iif_fence *iif_fence = sync_file->fence;
	struct dma_fence *dma_fence;
	struct sync_file *dma_sync_file;
	int fd, ret;

	dma_fence = dma_iif_fence_bridge(iif_fence);
	if (IS_ERR(dma_fence))
		return PTR_ERR(dma_fence);

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto err_put_fence;
	}

	if (copy_to_user(argp, &fd, sizeof(fd))) {
		ret = -EFAULT;
		goto err_put_fd;
	}

	dma_sync_file = sync_file_create(dma_fence);
	if (!dma_sync_file) {
		ret = -ENOMEM;
		goto err_put_fd;
	}

	/*
	 * @dma_sync_file holds the refcount of @dma_fence. We can release one which was held when
	 * `dma_iif_fence_bridge()` was called.
	 */
	dma_fence_put(dma_fence);

	/* Installs @fd to @dma_sync_file. */
	fd_install(fd, dma_sync_file->file);

	return 0;

err_put_fd:
	put_unused_fd(fd);
err_put_fence:
	dma_fence_put(dma_fence);

	return ret;
}

static int iif_sync_file_ioctl_signaler_completed(struct iif_sync_file *sync_file)
{
	struct iif_fence *fence = sync_file->fence;

	if (fence->params.signaler_ip != IIF_IP_AP && !fence->propagate) {
		iif_err(fence,
			"IIF_FENCE_SIGNALER_COMPLETED ioctl is allowed for AP-signaled fences only (signaler_ip=%d)\n",
			fence->params.signaler_ip);
		return -EPERM;
	}

	iif_fence_signaler_completed(fence);

	return 0;
}

static int iif_sync_file_ioctl_add_sync_point(struct iif_sync_file *sync_file, s32 __user *argp)
{
	struct iif_fence_add_sync_point_ioctl ibuf;
	struct iif_fence *fence = sync_file->fence;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	return iif_fence_add_sync_point(fence, ibuf.timeline, ibuf.count);
}

static int iif_sync_file_ioctl_get_information_with_details(
	struct iif_sync_file *sync_file,
	struct iif_fence_get_information_with_details_ioctl __user *argp)
{
	struct iif_fence *fence = sync_file->fence;
	struct iif_fence_get_information_with_details_ioctl ibuf = {
		.signaler_type = fence->params.signaler_type,
		.fence_type = fence->params.fence_type,
		.signaler_ip = fence->params.signaler_ip,
		.total_signalers = fence->params.remaining_signalers,
		.waiters = fence->params.waiters,
		.timeout = fence->params.timeout,
		.flags = fence->params.flags,
		.submitted_signalers = iif_fence_submitted_signalers(fence),
		.signaled_signalers = iif_fence_signaled_signalers(fence),
		.outstanding_waiters = iif_fence_outstanding_waiters(fence),
		.error = fence->signal_error,
	};

	if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		ibuf.signaled = fence->signaled;
	else if (fence->params.fence_type == IIF_FENCE_TYPE_REUSABLE)
		ibuf.timeline = fence->timeline;

	if (copy_to_user(argp, &ibuf, sizeof(ibuf)))
		return -EFAULT;

	return 0;
}

static int iif_sync_file_ioctl_create_tracker(struct iif_sync_file *sync_file, s32 __user *argp)
{
	struct iif_sync_file_tracker *tracker;
	int fd, ret;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

	if (copy_to_user(argp, &fd, sizeof(fd))) {
		ret = -EFAULT;
		goto err_put_fd;
	}

	tracker = iif_sync_file_tracker_create(sync_file);
	if (IS_ERR(tracker)) {
		ret = PTR_ERR(tracker);
		goto err_put_fd;
	}

	/* Installs @fd to @tracker. */
	fd_install(fd, tracker->file);

	return 0;

err_put_fd:
	put_unused_fd(fd);

	return ret;
}

static int iif_sync_file_ioctl_delegate_to_ap(struct iif_sync_file *sync_file)
{
	return iif_fence_delegate_to_ap(sync_file->fence);
}

static long iif_sync_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct iif_sync_file *sync_file = file->private_data;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case IIF_FENCE_GET_INFORMATION:
		return iif_sync_file_ioctl_get_information(sync_file, argp);
	case IIF_FENCE_SUBMIT_SIGNALER:
		return iif_sync_file_ioctl_submit_signaler(sync_file);
	case IIF_FENCE_SIGNAL:
		return iif_sync_file_ioctl_signal(sync_file, argp);
	case IIF_FENCE_SET_FLAGS:
		return iif_sync_file_ioctl_set_flags(sync_file, argp);
	case IIF_FENCE_GET_FLAGS:
		return iif_sync_file_ioctl_get_flags(sync_file, argp);
	case IIF_FENCE_BRIDGE_DMA_FENCE:
		return iif_sync_file_ioctl_bridge_dma_fence(sync_file, argp);
	case IIF_FENCE_SIGNALER_COMPLETED:
		return iif_sync_file_ioctl_signaler_completed(sync_file);
	case IIF_FENCE_ADD_SYNC_POINT:
		return iif_sync_file_ioctl_add_sync_point(sync_file, argp);
	case IIF_FENCE_GET_INFORMATION_WITH_DETAILS:
		return iif_sync_file_ioctl_get_information_with_details(sync_file, argp);
	case IIF_FENCE_CREATE_TRACKER:
		return iif_sync_file_ioctl_create_tracker(sync_file, argp);
	case IIF_FENCE_DELEGATE_TO_AP:
		return iif_sync_file_ioctl_delegate_to_ap(sync_file);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations iif_sync_file_fops = {
	.release = iif_sync_file_release,
	.poll = iif_sync_file_poll,
	.unlocked_ioctl = iif_sync_file_ioctl,
};

struct iif_sync_file *iif_sync_file_create(struct iif_fence *fence)
{
	struct iif_sync_file *sync_file;
	int ret;

	sync_file = kzalloc(sizeof(*sync_file), GFP_KERNEL);
	if (!sync_file)
		return ERR_PTR(-ENOMEM);

	sync_file->file = anon_inode_getfile("iif_file", &iif_sync_file_fops, sync_file, 0);
	if (IS_ERR(sync_file->file)) {
		ret = PTR_ERR(sync_file->file);
		goto err_free_sync_file;
	}

	ret = iif_sync_file_tracker_init(&sync_file->tracker, fence);
	if (ret)
		goto err_fput;

	sync_file->fence = iif_fence_get(fence);
	atomic_inc(&fence->num_sync_file);

	return sync_file;

err_fput:
	fput(sync_file->file);
err_free_sync_file:
	kfree(sync_file);
	return ERR_PTR(ret);
}

struct iif_sync_file *iif_sync_file_fdget(int fd)
{
	struct file *file = fget(fd);

	if (!file)
		return ERR_PTR(-EBADF);

	if (file->f_op != &iif_sync_file_fops) {
		fput(file);
		return ERR_PTR(-EINVAL);
	}

	return file->private_data;
}

struct iif_sync_file_tracker *iif_sync_file_tracker_create(struct iif_sync_file *sync_file)
{
	struct iif_sync_file_tracker *tracker;
	int ret;

	tracker = kzalloc(sizeof(*tracker), GFP_KERNEL);
	if (!tracker)
		return ERR_PTR(-ENOMEM);

	tracker->file =
		anon_inode_getfile("iif_tracker_file", &iif_sync_file_tracker_fops, tracker, 0);
	if (IS_ERR(tracker->file)) {
		ret = PTR_ERR(tracker->file);
		goto err_free_tracker_file;
	}

	ret = iif_sync_file_tracker_init(tracker, iif_fence_get(sync_file->fence));
	if (ret)
		goto err_fput;

	atomic_inc(&sync_file->fence->num_sync_file);

	return tracker;

err_fput:
	fput(tracker->file);
err_free_tracker_file:
	kfree(tracker);

	return ERR_PTR(ret);
}
