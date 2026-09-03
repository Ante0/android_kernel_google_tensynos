// SPDX-License-Identifier: GPL-2.0-only
/*
 * devfreq_tj_cdev.c driver to register the devfreq tj cooling device.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#include "devfreq_tj_cdev_helper.h"

static void devfreq_tj_cdev_exit(struct platform_device *pdev)
{
	struct devfreq_tj_cdev *cdev_tj = platform_get_drvdata(pdev);

	return devfreq_tj_cdev_cleanup(cdev_tj);
}

static int devfreq_tj_cdev_probe(struct platform_device *pdev)
{
	return devfreq_tj_cdev_probe_helper(pdev);
}

static const struct of_device_id devfreq_tj_cdev_table[] = {
	{ .compatible = "google,devfreq-tj-cdev", },
	{}
};
MODULE_DEVICE_TABLE(of, devfreq_tj_cdev_table);

static struct platform_driver devfreq_tj_cdev_driver = {
	.probe = devfreq_tj_cdev_probe,
	.remove_new = devfreq_tj_cdev_exit,
	.driver = {
		.name = "devfreq-tj-cdev",
		.of_match_table = devfreq_tj_cdev_table,
	},
};
module_platform_driver(devfreq_tj_cdev_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ram Chandrasekar <rchandrasekar@google.com>");
MODULE_DESCRIPTION("Google LLC devfreq TJ cooling device driver");
MODULE_ALIAS("platform:devfreq_tj_cdev");
