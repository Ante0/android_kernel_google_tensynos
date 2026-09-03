// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 * This driver serves as a verification tool for power-related
 * functionalities. It can be used to power on/off a power domain
 * via device PM's sysfs:
 * - to rpm_get: echo on > /sys/devices/platform/${DEVICE}/power/control
 * - to rpm_put: echo auto > /sys/devices/platform/${DEVICE}/power/control
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

static int genpd_test_consumer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dev_info(dev, "genpd consumer test driver");

	/* TODO(b/320174280): Make this driver consume multiple power domains */
	if (!of_find_property(dev->of_node, "power-domains", NULL)) {
		dev_err(dev, "power-domains prop is missing");
		return -EINVAL;
	}

	devm_pm_runtime_enable(dev);
	return 0;
};

static void genpd_test_consumer_remove(struct platform_device *pdev)
{
}

static const struct of_device_id genpd_test_consumer_of_match_table[] = {
	{ .compatible = "google,genpd-test-consumer" },
	{},
};
MODULE_DEVICE_TABLE(of, genpd_test_consumer_of_match_table);

static struct platform_driver genpd_test_consumer_driver = {
	.probe = genpd_test_consumer_probe,
	.remove = genpd_test_consumer_remove,
	.driver = {
		.name = "genpd-test-consumer",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(genpd_test_consumer_of_match_table),
	},
};

module_platform_driver(genpd_test_consumer_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("genpd test consumer driver");
MODULE_LICENSE("GPL");
