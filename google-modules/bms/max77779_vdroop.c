// SPDX-License-Identifier: GPL-2.0-only
/*
 * max77779 vdroop driver
 *
 * Copyright (C) 2023 Google, LLC.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "max77779.h"

struct max77779_vdroop_info {
	struct device		*dev;
};

static irqreturn_t max77779_vdroop_irq_handler(int irq, void *ptr)
{
	struct max77779_vdroop_info *info = ptr;
	struct device *fg;

	fg = max77779_get_dev(info->dev, "max77779,fg");
	if (!fg) {
		dev_info(info->dev, "%s: fg not found\n", __func__);
		return IRQ_NONE;
	}

	max77779_fg_vdroop_snapshot(fg);
	return IRQ_HANDLED;
}

static int max77779_vdroop_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max77779_vdroop_info *info;
	int err;
	int irq;

	dev_info(dev, "Probe start\n");

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;
	platform_set_drvdata(pdev, info);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		if (irq == -EPROBE_DEFER) {
			dev_warn(dev, "IRQ wait, deferring probe\n");
			return irq;
		}
		dev_err(dev, "%s filed to get irq (%d)\n", __func__, irq);
		return irq;
	}

	err = devm_request_threaded_irq(dev, irq, NULL, max77779_vdroop_irq_handler,
				   IRQF_TRIGGER_LOW | IRQF_SHARED | IRQF_ONESHOT,
				   "vdroop",
				   info);
	if (err < 0)
		dev_err(dev, "Error setting up vdroop irq (%d)\n", err);

	dev_info(dev, "Probe done\n");

	return 0;
}

static void max77779_vdroop_remove(struct platform_device *pdev)
{
}

static const struct platform_device_id max77779_vdroop_id[] = {
	{ "max77779-vdroop", 0},
	{},
};

MODULE_DEVICE_TABLE(platform, max77779_vdroop_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id max77779_vdroop_match_table[] = {
	{ .compatible = "max77779-vdroop",},
	{ },
};
#endif

static struct platform_driver max77779_vdroop_driver = {
	.probe = max77779_vdroop_probe,
	.remove = max77779_vdroop_remove,
	.id_table = max77779_vdroop_id,
	.driver = {
		.name = "max77779-vdroop",
		.owner = THIS_MODULE,
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = max77779_vdroop_match_table,
#endif
	},
};

module_platform_driver(max77779_vdroop_driver);

MODULE_DESCRIPTION("Maxim 77779 vdroop driver");
MODULE_AUTHOR("Prasanna Prapancham <prapancham@google.com>");
MODULE_LICENSE("GPL");
