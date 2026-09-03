// SPDX-License-Identifier: GPL-2.0-only
/*
 * File system operations for the IIF device.
 *
 * Copyright (C) 2024 Google LLC
 */

#include <linux/container_of.h>
#include <linux/export.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <iif/iif-fence.h>
#include <iif/iif-manager.h>
#include <iif/iif-platform.h>
#include <iif/iif-signaler-submission-waiter.h>
#include <iif/iif-sync-file.h>
#include <iif/iif.h>

static int iif_open(struct inode *inode, struct file *file)
{
	struct iif_manager *mgr = container_of(inode->i_cdev, struct iif_manager, char_dev);

	file->private_data = iif_manager_get(mgr);

	return 0;
}

static int iif_release(struct inode *inode, struct file *file)
{
	struct iif_manager *mgr = file->private_data;

	iif_manager_put(mgr);

	return 0;
}

/* Releases @iif_fence. It will be called when the refcount of @iif_fence becomes 0. */
static void iif_fence_on_release(struct iif_fence *iif_fence)
{
	kfree(iif_fence);
}

/* The operators for the fences. */
static const struct iif_fence_ops iif_fence_ops = {
	.on_release = iif_fence_on_release,
};

static int iif_ioctl_create_fence(struct iif_manager *mgr,
				  struct iif_create_fence_ioctl __user *argp)
{
	struct iif_create_fence_ioctl ibuf;
	struct iif_sync_file *sync_file;
	struct iif_fence *iif_fence;
	int fd, ret;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	if (ibuf.signaler_ip >= IIF_IP_NUM)
		return -EINVAL;

	iif_fence = kzalloc(sizeof(*iif_fence), GFP_KERNEL);
	if (!iif_fence)
		return -ENOMEM;

	ret = iif_fence_init(mgr, iif_fence, &iif_fence_ops, ibuf.signaler_ip,
			     ibuf.total_signalers);
	if (ret) {
		kfree(iif_fence);
		return ret;
	}

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto err_put_iif_fence;
	}

	ibuf.fence = fd;

	if (copy_to_user(argp, &ibuf, sizeof(ibuf))) {
		ret = -EFAULT;
		goto err_put_fd;
	}

	sync_file = iif_sync_file_create(iif_fence);
	if (IS_ERR(sync_file)) {
		ret = PTR_ERR(sync_file);
		goto err_put_fd;
	}

	/* @sync_file holds a reference of the fence and it's fine to release one here. */
	iif_fence_put(iif_fence);

	/* Installs @fd to @sync_file. */
	fd_install(fd, sync_file->file);

	return 0;

err_put_fd:
	put_unused_fd(fd);
err_put_iif_fence:
	/* @iif_fence will be released at `iif_fence_on_release()` callback. */
	iif_fence_put(iif_fence);

	return ret;
}

/*
 * Helper to fetch an array of fence file descriptors from the user-space, convert them to a
 * `iif_fence` pointer array and return it.
 *
 * The caller must release the returned array using the `put_fences_from_user` function below once
 * it is not needed anymore.
 */
static struct iif_fence **get_fences_from_user(struct iif_manager *mgr, u32 count,
					       const int __user *uaddr)
{
	struct iif_fence **fences;
	unsigned int i;
	int *fence_fds;
	int ret = 0;

	if (!count)
		return NULL;

	if (count > IIF_MAX_NUM_FENCES)
		return ERR_PTR(-EINVAL);

	fence_fds = kcalloc(count, sizeof(*fence_fds), GFP_KERNEL);
	if (!fence_fds)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(fence_fds, uaddr, count * sizeof(*fence_fds))) {
		ret = -EFAULT;
		goto err_free_fds;
	}

	fences = kcalloc(count, sizeof(*fences), GFP_KERNEL);
	if (!fences) {
		ret = -ENOMEM;
		goto err_free_fds;
	}

	for (i = 0; i < count; i++) {
		fences[i] = iif_fence_fdget(fence_fds[i]);
		if (IS_ERR(fences[i])) {
			ret = -EINVAL;
			goto err_put_fences;
		}
	}

	kfree(fence_fds);
	return fences;

err_put_fences:
	while (i--)
		iif_fence_put(fences[i]);
	kfree(fences);
err_free_fds:
	kfree(fence_fds);
	return ERR_PTR(ret);
}

/* Releases @fences returned by the `get_fences_from_user` function above. */
static void put_fences_from_user(struct iif_fence **fences, u32 count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		iif_fence_put(fences[i]);
	kfree(fences);
}

static int
iif_ioctl_fence_remaining_signalers(struct iif_manager *mgr,
				    struct iif_fence_remaining_signalers_ioctl __user *argp)
{
	struct iif_fence_remaining_signalers_ioctl ibuf;
	struct iif_fence **fences;
	int *remaining_signalers;
	int ret;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	fences = get_fences_from_user(mgr, ibuf.fences_count, (int __user *)ibuf.fences);
	if (IS_ERR(fences))
		return PTR_ERR(fences);

	remaining_signalers = kcalloc(ibuf.fences_count, sizeof(*remaining_signalers), GFP_KERNEL);
	if (!remaining_signalers) {
		ret = -ENOMEM;
		goto put_fences;
	}

	ret = iif_wait_signaler_submission(fences, ibuf.fences_count, ibuf.eventfd,
					   remaining_signalers);
	if (ret)
		goto free_remaining_signalers;

	if (copy_to_user((int __user *)ibuf.remaining_signalers, remaining_signalers,
			 ibuf.fences_count * sizeof(*remaining_signalers)))
		ret = -EFAULT;

free_remaining_signalers:
	kfree(remaining_signalers);
put_fences:
	put_fences_from_user(fences, ibuf.fences_count);
	return ret;
}

static int
iif_ioctl_create_fence_with_params(struct iif_manager *mgr,
				   struct iif_create_fence_with_params_ioctl __user *argp)
{
	struct iif_create_fence_with_params_ioctl ibuf;
	struct iif_sync_file *sync_file;
	struct iif_fence_params params;
	struct iif_fence *iif_fence;
	int fd, ret;

	if (copy_from_user(&ibuf, argp, sizeof(ibuf)))
		return -EFAULT;

	iif_fence = kzalloc(sizeof(*iif_fence), GFP_KERNEL);
	if (!iif_fence)
		return -ENOMEM;

	params.signaler_type = ibuf.signaler_type;
	params.fence_type = ibuf.fence_type;
	params.signaler_ip = ibuf.signaler_ip;
	params.remaining_signalers = ibuf.remaining_signalers;
	params.waiters = ibuf.waiters;
	params.timeout = ibuf.timeout;
	params.flags = ibuf.flags;

	ret = iif_fence_init_with_params(mgr, iif_fence, &iif_fence_ops, &params);
	if (ret) {
		kfree(iif_fence);
		return ret;
	}

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto err_put_iif_fence;
	}

	ibuf.fence = fd;

	if (copy_to_user(argp, &ibuf, sizeof(ibuf))) {
		ret = -EFAULT;
		goto err_put_fd;
	}

	sync_file = iif_sync_file_create(iif_fence);
	if (IS_ERR(sync_file)) {
		ret = PTR_ERR(sync_file);
		goto err_put_fd;
	}

	/* @sync_file holds a reference of the fence and it's fine to release one here. */
	iif_fence_put(iif_fence);

	/* Installs @fd to @sync_file. */
	fd_install(fd, sync_file->file);

	return 0;

err_put_fd:
	put_unused_fd(fd);
err_put_iif_fence:
	/* @iif_fence will be released at `iif_fence_on_release()` callback. */
	iif_fence_put(iif_fence);

	return ret;
}

static int iif_ioctl_get_interface_version(struct iif_manager *mgr,
					   struct iif_interface_version_ioctl __user *argp)
{
	struct iif_interface_version_ioctl ibuf;
	int ret;

	ibuf.version_major = IIF_INTERFACE_VERSION_MAJOR;
	ibuf.version_minor = IIF_INTERFACE_VERSION_MINOR;
	memset(ibuf.version_build, 0, IIF_INTERFACE_VERSION_BUILD_BUFFER_SIZE);
	ret = scnprintf(ibuf.version_build, IIF_INTERFACE_VERSION_BUILD_BUFFER_SIZE - 1, "%s",
			iif_platform_get_driver_commit());

	if (ret < 0 || ret >= IIF_INTERFACE_VERSION_BUILD_BUFFER_SIZE) {
		dev_warn(mgr->dev, "Buffer size insufficient to hold git build info (size=%d)",
			 ret);
	}

	if (copy_to_user(argp, &ibuf, sizeof(ibuf)))
		return -EFAULT;

	return 0;
}

static long iif_ioctl(struct file *file, uint cmd, ulong arg)
{
	struct iif_manager *mgr = file->private_data;
	void __user *argp = (void __user *)arg;
	long ret;

	switch (cmd) {
	case IIF_CREATE_FENCE:
		ret = iif_ioctl_create_fence(mgr, argp);
		break;
	case IIF_FENCE_REMAINING_SIGNALERS:
		ret = iif_ioctl_fence_remaining_signalers(mgr, argp);
		break;
	case IIF_CREATE_FENCE_WITH_PARAMS:
		ret = iif_ioctl_create_fence_with_params(mgr, argp);
		break;
	case IIF_GET_INTERFACE_VERSION:
		ret = iif_ioctl_get_interface_version(mgr, argp);
		break;
	default:
		ret = -ENOTTY; /* unknown command */
	}

	return ret;
}

const struct file_operations iif_fops = {
	.owner = THIS_MODULE,
	.open = iif_open,
	.release = iif_release,
	.unlocked_ioctl = iif_ioctl,
};
