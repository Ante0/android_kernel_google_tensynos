// SPDX-License-Identifier: GPL-2.0

#include "pixelmd_client.h"
#include "pixelmd_device.h"

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>

#define DEVICE_NAME "pixelmd"
#define CLASS_NAME "pixelmd"

int pixelmd_device_init(struct pixelmd_device *pixel_dev)
{
	int ret = alloc_chrdev_region(&pixel_dev->dev_num, 0, 1, DEVICE_NAME);

	if (ret < 0)
		return ret;

	cdev_init(&pixel_dev->cdev, &pixelmd_device_fops);
	ret = cdev_add(&pixel_dev->cdev, pixel_dev->dev_num, 1);
	if (ret < 0)
		goto unregister_region;

	pixel_dev->class = class_create(CLASS_NAME);
	if (IS_ERR(pixel_dev->class)) {
		ret = PTR_ERR(pixel_dev->class);
		goto delete_cdev;
	}

	pixel_dev->device =
		device_create(pixel_dev->class, NULL, pixel_dev->dev_num, NULL, DEVICE_NAME);
	if (IS_ERR(pixel_dev->device)) {
		ret = PTR_ERR(pixel_dev->device);
		goto destroy_class;
	}

	return 0;

destroy_class:
	class_destroy(pixel_dev->class);
delete_cdev:
	cdev_del(&pixel_dev->cdev);
unregister_region:
	unregister_chrdev_region(pixel_dev->dev_num, 1);
	return ret;
}

void pixelmd_device_destroy(struct pixelmd_device *pixel_dev)
{
	device_destroy(pixel_dev->class, pixel_dev->dev_num);
	class_destroy(pixel_dev->class);
	cdev_del(&pixel_dev->cdev);
	unregister_chrdev_region(pixel_dev->dev_num, 1);
}
