// SPDX-License-Identifier: GPL-2.0-only
/*
 * google_powercap_tree.c driver to register the powercap tree.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "google_powercap.h"
#include "google_powercap_helper.h"

static const struct of_device_id gpowercap_of_match[] = {
	{ .compatible = "google,powercap-tree" },
	{},
};
MODULE_DEVICE_TABLE(of, gpowercap_of_match);

static struct platform_driver gpowercap_tree_driver = {
	.probe = gpowercap_dt_probe,
	.remove_new = gpowercap_dt_remove,
	.driver = {
		.name = "google-powercap-tree",
		.of_match_table = gpowercap_of_match,
	},
};
module_platform_driver(gpowercap_tree_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ram Chandrasekar <rchandrasekar@google.com>");
MODULE_DESCRIPTION("Google LLC powercap driver");
MODULE_ALIAS("platform:google_powercap");
