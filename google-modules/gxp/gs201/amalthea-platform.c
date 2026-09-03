// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for Amalthea.
 *
 * Copyright (C) 2021-2025 Google LLC
 */

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <gcip/gcip-iommu.h>

#include "gxp-internal.h"

#include "gxp-common-platform.c"

void gxp_iommu_setup_shareability(struct gxp_dev *gxp)
{
	/* IO coherency not supported */
}

static int gxp_platform_probe(struct platform_device *pdev)
{
	struct gxp_dev *gxp =
		devm_kzalloc(&pdev->dev, sizeof(*gxp), GFP_KERNEL);

	if (!gxp)
		return -ENOMEM;

	return gxp_common_platform_probe(pdev, gxp);
}

static const struct of_device_id gxp_of_match[] = {
	{ .compatible = "google,gxp", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, gxp_of_match);

static struct platform_driver gxp_platform_driver = {
	.probe = gxp_platform_probe,
	.remove = gxp_common_platform_remove,
	.driver = {
			.name = GXP_DRIVER_NAME,
			.of_match_table = of_match_ptr(gxp_of_match),
#if IS_ENABLED(CONFIG_PM_SLEEP)
			.pm = &gxp_pm_ops,
#endif
		},
};

static int __init gxp_platform_init(void)
{
	int ret;

	ret = gxp_common_platform_init();
	if (ret)
		return ret;

	return platform_driver_register(&gxp_platform_driver);
}

static void __exit gxp_platform_exit(void)
{
	platform_driver_unregister(&gxp_platform_driver);
	gxp_common_platform_exit();
}

bool gxp_is_direct_mode(struct gxp_dev *gxp)
{
	return true;
}

enum gxp_chip_revision gxp_get_chip_revision(struct gxp_dev *gxp)
{
	return GXP_CHIP_ANY;
}

MODULE_DESCRIPTION("Google GXP platform driver");
MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL");
#ifdef GIT_REPO_TAG
MODULE_INFO(gitinfo, GIT_REPO_TAG);
#endif
module_init(gxp_platform_init);
module_exit(gxp_platform_exit);
