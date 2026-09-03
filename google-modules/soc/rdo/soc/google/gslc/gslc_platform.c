// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for the Google System level cache(GSLC)
 *
 * Copyright (C) 2021 Google LLC
 */

#include "gslc_platform.h"

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "gslc_cpm_mba.h"
#include "gslc_debugfs.h"
#include "gslc_pt_ops.h"
#include "gslc_regmap.h"

static int gslc_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gslc_dev *gslc_dev;
	int ret = 0;

	gslc_dev = devm_kzalloc(dev, sizeof(*gslc_dev), GFP_KERNEL);

	if (!gslc_dev)
		return -ENOMEM;
	gslc_dev->dev = dev;

	platform_set_drvdata(pdev, gslc_dev);

	gslc_dev->csr_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR_OR_NULL(gslc_dev->csr_base)) {
		dev_err(dev, "devm_ioremap_resource failed\n");
		return -ENODEV;
	}

	ret = gslc_regmap_init(gslc_dev);
	if (ret < 0) {
		dev_err(dev, "Register map initialization failed\n");
		return ret;
	}

	gslc_dev->dbg = devm_kzalloc(dev, sizeof(*gslc_dev->dbg), GFP_KERNEL);
	if (!gslc_dev->dbg)
		return -ENOMEM;
	gslc_create_debugfs(gslc_dev);

	gslc_dev->cpm_mba =
		devm_kzalloc(dev, sizeof(*gslc_dev->cpm_mba), GFP_KERNEL);
	if (!gslc_dev->cpm_mba)
		return -ENOMEM;
	ret = gslc_cpm_mba_init(gslc_dev);
	if (ret < 0) {
		dev_err(dev, "CPM mailbox initialization failed\n");
		return ret;
	}

	spin_lock_init(&gslc_dev->pid_lock);
	gslc_cpm_pt_driver_init(gslc_dev);

	dev_info(dev, "GSLC probe complete\n");
	return ret;
}

static void gslc_platform_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gslc_dev *gslc_dev = platform_get_drvdata(pdev);

	gslc_remove_debugfs(gslc_dev);
	gslc_cpm_mba_deinit(gslc_dev);

	dev_info(dev, "GSLC remove complete\n");
}

static const struct of_device_id gslc_of_match[] = {
	{
		.compatible = "google,gslc",
	},
	{},
};
MODULE_DEVICE_TABLE(of, gslc_of_match);

static struct platform_driver gslc_platform_driver = {
	.probe  = gslc_platform_probe,
	.remove = gslc_platform_remove,
	.driver = {
			.name           = "gslc",
			.owner	        = THIS_MODULE,
			.of_match_table = of_match_ptr(gslc_of_match),
		},
};

module_platform_driver(gslc_platform_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("GSLC platform driver");
MODULE_LICENSE("GPL");
