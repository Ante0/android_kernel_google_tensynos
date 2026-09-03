// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_stats.c driver to register and update CPM thermal stats to kernel metrics
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include <linux/err.h>
#include <linux/platform_device.h>

#include "soc/google/google_gtc.h"
#include "thermal_stats_helper.h"

static const struct of_device_id thermal_stats_match_table[] = {
	{
		.compatible = "google,tmu-stats",
	},
	{}
};
MODULE_DEVICE_TABLE(of, thermal_stats_match_table);

static void thermal_stats_remove(struct platform_device *pdev)
{
	thermal_stats_cleanup(pdev);
}

static int thermal_stats_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *dev_id = NULL;
	struct thermal_stats_driver_data *drv_data = NULL;
	int ret;

	dev_id = of_match_node(thermal_stats_match_table, pdev->dev.of_node);
	if (!dev_id) {
		dev_err(dev, "No matching table for the compatible flag:%s\n",
			pdev->dev.of_node->name);
		return -ENODEV;
	}
	drv_data = devm_kzalloc(dev, sizeof(*drv_data), GFP_KERNEL);
	if (IS_ERR_OR_NULL(drv_data))
		return -ENOMEM;

	ret = thermal_stats_parse_dt(pdev, drv_data);
	if (ret)
		goto error_exit;

	ret = thermal_stats_init(pdev, drv_data);
	if (ret)
		goto error_exit;

	platform_set_drvdata(pdev, drv_data);
	return 0;

error_exit:
	thermal_stats_remove(pdev);
	return ret;
}

static struct platform_driver thermal_stats_driver = {
	.driver = {
		.name = "thermal-sensor-stats",
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
		.owner = THIS_MODULE,
		.of_match_table = thermal_stats_match_table,
	},
	.probe = thermal_stats_probe,
	.remove_new = thermal_stats_remove,
};
module_platform_driver(thermal_stats_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jikai Ma <jikai@google.com>");
MODULE_DESCRIPTION("Google LLC Thermal Stats Driver");
MODULE_ALIAS("platform:thermal-stats");
