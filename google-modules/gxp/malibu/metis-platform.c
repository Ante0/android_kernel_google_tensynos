// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for Metis.
 *
 * Copyright (C) 2024-2025 Google LLC
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "gxp-kci.h"
#include "gxp-mcu-fs.h"
#include "gxp-uci.h"
#include "metis-platform.h"

#include "gxp-common-platform.c"

void gxp_iommu_setup_shareability(struct gxp_dev *gxp)
{
	/* IO coherency not supported yet */
}

static int metis_platform_parse_dt(struct platform_device *pdev, struct gxp_dev *gxp)
{
	return 0;
}

static int gxp_platform_probe(struct platform_device *pdev)
{
	struct metis_dev *metis = devm_kzalloc(&pdev->dev, sizeof(*metis), GFP_KERNEL);
	struct gxp_mcu_dev *mcu_dev = &metis->mcu_dev;
	struct gxp_dev *gxp = &mcu_dev->gxp;

	if (!metis)
		return -ENOMEM;

	gxp_mcu_dev_init(mcu_dev);
	gxp->parse_dt = metis_platform_parse_dt;
	gxp->coresight_remote_init = metis_coresight_remote_init;
	gxp->coresight_remote_exit = metis_coresight_remote_exit;
	gxp->coresight_remote_restore = metis_coresight_remote_restore;

	return gxp_common_platform_probe(pdev, gxp);
}

static const struct of_device_id gxp_of_match[] = {
	{
		.compatible = "google,gxp-mbu",
	},
	{
		.compatible = "google,gxp",
	},
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

MODULE_DESCRIPTION("Google GXP platform driver");
MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL");
#ifdef GIT_REPO_TAG
MODULE_INFO(gitinfo, GIT_REPO_TAG);
#endif
module_init(gxp_platform_init);
module_exit(gxp_platform_exit);
