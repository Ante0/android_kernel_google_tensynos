// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google LLC
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <soc/google/exynos-usbdrd.h>
#include <soc/google/pkvm-s2mpu.h>

#define REG_NS_V9_CTRL_PROT_EN_PER_VID_CLR	0x54
#define REG_NS_CTRL0				0x0

static const char version_v1;
static const char version_v9;

struct s2mpu_bypass_data {
	struct device *dev;
	void __iomem *base;
	const char *ver;
};

int __pkvm_s2mpu_of_link(struct device *parent)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct device_link *link;

	np = of_parse_phandle(parent->of_node, "s2mpus", 0);
	if (!np)
		return 0;

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return -EPROBE_DEFER;

	if (!platform_get_drvdata(pdev))
		return -EAGAIN;

	link = device_link_add(parent, &pdev->dev,
			       DL_FLAG_AUTOREMOVE_CONSUMER | DL_FLAG_PM_RUNTIME);

	if (!link)
		return -EINVAL;

	return 0;
}

int pkvm_s2mpu_of_link(struct device *parent)
{
	return __pkvm_s2mpu_of_link(parent);
}
EXPORT_SYMBOL_GPL(pkvm_s2mpu_of_link);

int pkvm_s2mpu_of_link_v9(struct device *parent)
{
	return __pkvm_s2mpu_of_link(parent);
}
EXPORT_SYMBOL_GPL(pkvm_s2mpu_of_link_v9);

static int __s2mpu_resume(struct device *dev)
{
	struct s2mpu_bypass_data *data = dev_get_drvdata(dev);

	if (data->ver == &version_v9)
		writel_relaxed(0xFF, data->base + REG_NS_V9_CTRL_PROT_EN_PER_VID_CLR);
	else
		writel_relaxed(0, data->base + REG_NS_CTRL0);

	return 0;
}

static int s2mpu_pm_control(struct device *dev, bool on)
{
	if (on)
		return __s2mpu_resume(dev);
	return 0;
}

static int s2mpu_late_resume(struct device *dev)
{
	if (pm_runtime_status_suspended(dev))
		return 0;

	return __s2mpu_resume(dev);
}

static int s2mpu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct s2mpu_bypass_data *data;
	bool always_on;

	data = devm_kmalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to parse 'reg'");
		return -EINVAL;
	}

	/* devm_ioremap_resource internally calls devm_request_mem_region. */
	data->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->base)) {
		dev_err(dev, "could not ioremap resource: %ld", PTR_ERR(data->base));
		return PTR_ERR(data->base);
	}

	data->ver = of_device_get_match_data(dev);
	always_on = !!of_get_property(np, "always-on", NULL);

	dev_warn(dev, "S2MPU is in bypass mode!\n");

	dev_set_drvdata(dev, data);

	pm_runtime_enable(dev);
	if (always_on)
		pm_runtime_get_sync(dev);

	return 0;
}

static const struct dev_pm_ops s2mpu_pm_ops = {
	SET_RUNTIME_PM_OPS(NULL, __s2mpu_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(NULL, s2mpu_late_resume)
};

static const struct of_device_id s2mpu_of_match[] = {
	{ .compatible = "google,s2mpu", .data = &version_v1 },
	{ .compatible = "google,s2mpu-v9", .data = &version_v9 },
	{},
};
MODULE_DEVICE_TABLE(of, s2mpu_of_match);

static struct platform_driver s2mpu_driver = {
	.probe = s2mpu_probe,
	.driver = {
		.name = "s2mpu-bypass",
		.of_match_table = s2mpu_of_match,
		.pm = &s2mpu_pm_ops,
	},
};

static int s2mpu_driver_register(struct platform_driver *driver)
{
	int ret = 0;

	ret = exynos_usbdrd_set_s2mpu_pm_ops(s2mpu_pm_control);
	if (ret) {
		pr_err("%s: Failed to set S2MPU PM OPS\n", s2mpu_driver.driver.name);
		return ret;
	}

	return platform_driver_register(driver);
}

module_driver(s2mpu_driver, s2mpu_driver_register, platform_driver_unregister);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ryan Huang <tzukui@google.com>");
MODULE_DESCRIPTION("S2MPU bypass mode driver");
