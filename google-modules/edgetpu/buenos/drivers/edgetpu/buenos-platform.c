// SPDX-License-Identifier: GPL-2.0-only
/*
 * Buenos platform device driver for the Google Edge TPU ML accelerator.
 *
 * Copyright (C) 2024 Google LLC
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "edgetpu-config.h"
#include "edgetpu-internal.h"
#include "edgetpu-mobile-platform.h"
#include "edgetpu-pm.h"

#include "edgetpu-mobile-platform.c"

static const struct of_device_id buenos_of_match[] = {
	{
		.compatible = "google,edgetpu-laguna",
	},
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, buenos_of_match);


static struct platform_driver buenos_driver = {
	.probe = edgetpu_mobile_platform_probe,
	.remove = edgetpu_mobile_platform_remove,
	.driver = {
			.name = "edgetpu_buenos",
			.of_match_table = buenos_of_match,
			.pm = &edgetpu_pm_ops,
		},
};

static int __init buenos_init(void)
{
	int ret;

	ret = edgetpu_init();
	if (ret)
		return ret;
	return platform_driver_register(&buenos_driver);
}

static void __exit buenos_exit(void)
{
	platform_driver_unregister(&buenos_driver);
	edgetpu_exit();
}

MODULE_DESCRIPTION("Google Buenos Edge TPU driver");
MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL");
module_init(buenos_init);
module_exit(buenos_exit);
MODULE_FIRMWARE(EDGETPU_DEFAULT_FIRMWARE_NAME);
