// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/sync_file.h>
#include <linux/uaccess.h>

#include "sysconfig.h"
#include "ioctl.h"

#define copy_struct_from_user(dest, user_data)                                                     \
	do {                                                                                       \
		if (copy_from_user(&(dest), (user_data), sizeof(dest))) {                          \
			dev_err(pixel_dev->dev, "%s: failed copy_from_user", __func__);            \
			return -EFAULT;                                                            \
		}                                                                                  \
	} while (0)

#define copy_struct_to_user(user_data, src)                                                        \
	do {                                                                                       \
		if (copy_to_user((user_data), &(src), sizeof(src))) {                              \
			dev_err(pixel_dev->dev, "%s: failed copy_to_user", __func__);              \
			return -EFAULT;                                                            \
		}                                                                                  \
	} while (0)

static long pixel_gpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pixel_gpu_device *pixel_dev = file->private_data;
	void __user *user_data = (void __user *)arg;
	(void)user_data;

	switch (cmd) {
	default:
		dev_info(pixel_dev->dev, "%s: unknown ioctl (%d)", __func__, cmd);
		return -ENOTTY;
	}
}

static int pixel_gpu_open(struct inode *inode, struct file *file)
{
	struct miscdevice *mdev = file->private_data;
	struct pixel_gpu_device *pixel_dev = container_of(mdev, struct pixel_gpu_device, mdev);

	/* Store a handle to the pixel device for easy access */
	file->private_data = pixel_dev;

	return 0;
}

static int pixel_gpu_close(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations pixel_gpu_fops = {
	.owner          = THIS_MODULE,
	.open           = pixel_gpu_open,
	.release        = pixel_gpu_close,
	.unlocked_ioctl = pixel_gpu_ioctl,
	.compat_ioctl   = pixel_gpu_ioctl,
};

int pixel_ioctl_init(struct pixel_gpu_device *pixel_dev)
{
	int err;

	pixel_dev->mdev = (struct miscdevice){
		.minor = MISC_DYNAMIC_MINOR,
		.name = "pixel_gpu_mdev",
		.fops = &pixel_gpu_fops,
		.mode = 0666,
		.parent = get_device(pixel_dev->dev),
	};

	err = misc_register(&pixel_dev->mdev);
	if (err)
		dev_err(pixel_dev->dev, "%s: failed to register mdev (%d)", __func__, err);

	return err;
}

void pixel_ioctl_term(struct pixel_gpu_device *pixel_dev)
{
	put_device(pixel_dev->dev);
	misc_deregister(&pixel_dev->mdev);
}

