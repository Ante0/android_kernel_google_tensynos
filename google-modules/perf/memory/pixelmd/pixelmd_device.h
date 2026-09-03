/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PIXELMD_DEVICE_
#define PIXELMD_DEVICE_

#include <linux/cdev.h>
#include <linux/types.h>

struct pixelmd_device {
	dev_t dev_num;
	struct cdev cdev;
	struct class *class;
	struct device *device;
};

int pixelmd_device_init(struct pixelmd_device *device);
void pixelmd_device_destroy(struct pixelmd_device *device);

#endif /* PIXELMD_DEVICE_ */
