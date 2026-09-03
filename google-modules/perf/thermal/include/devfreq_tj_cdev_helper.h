/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * devfreq_tj_cdev_helper.h devfreq cooling device helper.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */
#ifndef _DEVFREQ_TJ_CDEV_HELPER_H_
#define _DEVFREQ_TJ_CDEV_HELPER_H_

#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/mutex.h>

#include "cdev_devfreq_helper.h"

struct devfreq_tj_cdev {
	struct cdev_devfreq_data cdev;
	enum hw_dev_type cdev_id;
	struct device_node *np;
	struct device *dev;
	unsigned long cur_cdev_state;
	struct notifier_block nb;
	struct mutex lock;
};

void devfreq_tj_cdev_cleanup(struct devfreq_tj_cdev *cdev_tj);
int devfreq_tj_cdev_probe_helper(struct platform_device *pdev);
void __devfreq_tj_cdev_success(struct cdev_devfreq_data *cdev);
void __devfreq_tj_cdev_exit(struct cdev_devfreq_data *cdev);
int __devfreq_tj_cdev_cb(struct notifier_block *nb, unsigned long val, void *data);
#endif  // _DEVFREQ_TJ_CDEV_HELPER_H_
