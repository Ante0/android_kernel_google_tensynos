// SPDX-License-Identifier: GPL-2.0-only.
/*
 * Google AOSS Watchdog Handler Driver
 *
 * Copyright 2026 Google LLC.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/pm_wakeirq.h>

static irqreturn_t goog_aoss_wdt_irq_handler(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static int goog_aoss_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Failed to get IRQ number: %d", irq);
		return irq;
	}

	ret = devm_request_irq(dev, irq, goog_aoss_wdt_irq_handler,
			       IRQF_TRIGGER_NONE, dev_name(dev), dev);
	if (ret) {
		dev_err(dev, "Failed to request IRQ %d: %d", irq, ret);
		return ret;
	}

	device_init_wakeup(dev, of_property_read_bool(dev->of_node,
						      "wakeup-source"));

	ret = dev_pm_set_wake_irq(dev, irq);
	if (ret) {
		dev_err(dev, "Failed to set wake irq, err: %d", ret);
		device_init_wakeup(dev, false);
		return ret;
	}

	dev_dbg(dev, "AOSS Watchdog Handler Initialized (IRQ: %d)", irq);
	return 0;
}

static void goog_aoss_wdt_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	device_init_wakeup(dev, false);
	dev_pm_clear_wake_irq(dev);
}

static const struct of_device_id goog_aoss_wdt_of_match[] = {
	{ .compatible = "google,aoss-wdt-handler" },
	{}
};
MODULE_DEVICE_TABLE(of, goog_aoss_wdt_of_match);

static struct platform_driver goog_aoss_wdt_driver = {
	.driver = {
		.name = "goog-aoss-wdt-handler",
		.owner = THIS_MODULE,
		.of_match_table = goog_aoss_wdt_of_match,
	},
	.probe = goog_aoss_wdt_probe,
	.remove = goog_aoss_wdt_remove,
};

module_platform_driver(goog_aoss_wdt_driver);

MODULE_AUTHOR("Mariv Mosaad <marivmosaad@google.com>");
MODULE_DESCRIPTION("Google AOSS Watchdog Handler Driver");
MODULE_LICENSE("GPL");
