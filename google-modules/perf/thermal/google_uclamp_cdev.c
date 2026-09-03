// SPDX-License-Identifier: GPL-2.0
/*
 * cdev_uclamp.c Cooling device to place thermal uclamp vote.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "google_uclamp_cdev_helper.h"

static int thermal_uclamp_probe(struct platform_device *pdev)
{
	return thermal_uclamp_probe_helper(pdev);
}

static void thermal_uclamp_remove(struct platform_device *pdev)
{
	thermal_uclamp_remove_helper(pdev);
}

static const struct of_device_id thermal_uclamp_of_match[] = {
	{
		.compatible = "google,thermal-uclamp",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, thermal_uclamp_of_match);

static struct platform_driver thermal_uclamp_driver = {
	.probe = thermal_uclamp_probe,
	.remove_new = thermal_uclamp_remove,
	.driver = {
		.name = "google_thermal_uclamp",
		.of_match_table = of_match_ptr(thermal_uclamp_of_match),
	},
};

module_platform_driver(thermal_uclamp_driver);

MODULE_DESCRIPTION("Cooling device for placing uclamp max for CPU clusters");
MODULE_AUTHOR("Ram Chandrasekar <rchandrasekar@google.com>");
MODULE_AUTHOR("Peter YM <peterym@google.com>");
MODULE_LICENSE("GPL");
