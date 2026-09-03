// SPDX-License-Identifier: GPL-2.0-only
/*
 * SantaFe platform device driver for the Google Edge TPU ML accelerator.
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

static const struct of_device_id santafe_of_match[] = {
	{
		.compatible = "google,edgetpu-malibu",
	},
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(of, santafe_of_match);

static struct platform_driver santafe_driver = {
	.probe = edgetpu_mobile_platform_probe,
	.remove = edgetpu_mobile_platform_remove,
	.driver = {
			.name = "edgetpu_santafe",
			.of_match_table = santafe_of_match,
			.pm = &edgetpu_pm_ops,
		},
};

static int __init santafe_init(void)
{
	int ret;

	ret = edgetpu_init();
	if (ret)
		return ret;
	return platform_driver_register(&santafe_driver);
}

static void __exit santafe_exit(void)
{
	platform_driver_unregister(&santafe_driver);
	edgetpu_exit();
}

MODULE_DESCRIPTION("Google SantaFe Edge TPU driver");
MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL");
module_init(santafe_init);
module_exit(santafe_exit);
MODULE_FIRMWARE(EDGETPU_DEFAULT_FIRMWARE_NAME);
