// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */

#include <linux/debugfs.h>
#include <linux/gpio/consumer.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>

#define MAX_CONFIG_PARAM_LEN 16

static char *sswrp = "";
module_param(sswrp, charp, 0000);
MODULE_PARM_DESC(sswrp, "SSWRP name to test");

static int test_id;
module_param(test_id, int, 0000);
MODULE_PARM_DESC(test_id, "Pinctrl test configuration id");

struct pinctrl_test_dev {
	struct device *dev;
	struct platform_device *pdev;
	struct pinctrl *pinctrl;
	struct dentry *de;
	int gpio_count;
	int *gpios;
	int *irqs;
	int irq_triggered;
};

static int irq_to_pin_idx(int irq, struct pinctrl_test_dev *ptest_dev)
{
	int i;

	for (i = 0; i < ptest_dev->gpio_count; i++)
		if (irq == ptest_dev->irqs[i])
			return i;

	return -1;
}

static irqreturn_t pinctrl_test_interrupt(int irq, void *id)
{
	struct pinctrl_test_dev *ptest_dev = id;
	int pin_idx = irq_to_pin_idx(irq, ptest_dev);

	dev_info(ptest_dev->dev, "IRQ handler %d for gpio: %d\n", irq, ptest_dev->gpios[pin_idx]);
	ptest_dev->irq_triggered = irq;

	return IRQ_HANDLED;
}

static int irq_registered_show(struct seq_file *s, void *p)
{
	struct pinctrl_test_dev *ptest_dev = s->private;
	int i;

	seq_puts(s, "    SSWRP     GPIO IRQ\n");
	seq_puts(s, "-----------------------\n");
	for (i = 0; i < ptest_dev->gpio_count; i++)
		seq_printf(s, "%2d  %-10s %3d %3d\n",
			   i, sswrp, ptest_dev->gpios[i], ptest_dev->irqs[i]);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(irq_registered);

static int irq_triggered_show(struct seq_file *s, void *p)
{
	struct pinctrl_test_dev *ptest_dev = s->private;

	seq_printf(s, "%d\n", ptest_dev->irq_triggered);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(irq_triggered);

static int pinctrl_test_init_debugfs(struct pinctrl_test_dev *ptest_dev)
{
	ptest_dev->de = debugfs_create_dir("pinctrl-test", NULL);
	if (IS_ERR(ptest_dev->de))
		return PTR_ERR(ptest_dev->de);

	debugfs_create_file("irq-registered", 0400, ptest_dev->de,
			    (void *)ptest_dev, &irq_registered_fops);
	debugfs_create_file("irq-triggered", 0400, ptest_dev->de,
			    (void *)ptest_dev, &irq_triggered_fops);

	return 0;
}

static int pinctrl_test_probe(struct platform_device *pdev)
{
	struct pinctrl_test_dev *ptest_dev;
	char buf[MAX_CONFIG_PARAM_LEN];
	int i, res;

	ptest_dev = devm_kzalloc(&pdev->dev, sizeof(*ptest_dev), GFP_KERNEL);
	if (!ptest_dev)
		return -ENOMEM;

	ptest_dev->pdev = pdev;
	ptest_dev->dev = &pdev->dev;

	scnprintf(buf, MAX_CONFIG_PARAM_LEN, "%s-test-%d", sswrp, test_id);
	dev_info(&pdev->dev, "requested pinctrl configuration: %s\n", buf);

	ptest_dev->pinctrl = devm_pinctrl_get_select(&pdev->dev, buf);
	if (IS_ERR(ptest_dev->pinctrl)) {
		dev_err(&pdev->dev, "cannot select configuration: %s\n", buf);
		return PTR_ERR(ptest_dev->pinctrl);
	}

	ptest_dev->gpio_count = gpiod_count(&pdev->dev, sswrp);
	if (ptest_dev->gpio_count < 0) {
		dev_err(&pdev->dev, "failed to count gpios: %d\n", ptest_dev->gpio_count);
		return ptest_dev->gpio_count;
	}

	dev_info(&pdev->dev, "device tree declares %d gpios\n", ptest_dev->gpio_count);
	ptest_dev->gpios = devm_kcalloc(&pdev->dev,
					ptest_dev->gpio_count,
					sizeof(*ptest_dev->gpios),
					GFP_KERNEL);
	if (!ptest_dev->gpios) {
		dev_err(&pdev->dev, "could not allocate memory for gpios\n");
		return -ENOMEM;
	}

	ptest_dev->irqs = devm_kcalloc(&pdev->dev,
				       ptest_dev->gpio_count,
				       sizeof(*ptest_dev->irqs),
				       GFP_KERNEL);
	if (!ptest_dev->irqs) {
		dev_err(&pdev->dev, "could not allocate memory for irqs\n");
		return -ENOMEM;
	}

	scnprintf(buf, MAX_CONFIG_PARAM_LEN, "%s-gpios", sswrp);

	for (i = 0; i < ptest_dev->gpio_count; i++) {
		res = of_get_named_gpio(pdev->dev.of_node, buf, i);
		if (res < 0) {
			dev_err(&pdev->dev, "cannot get gpio with index %d from DT\n", i);
			return res;
		}

		ptest_dev->gpios[i] = res;
		ptest_dev->irqs[i] = gpio_to_irq(ptest_dev->gpios[i]);

		res = devm_request_irq(&pdev->dev,
				       ptest_dev->irqs[i],
				       pinctrl_test_interrupt,
				       IRQF_TRIGGER_RISING,
				       "pinctrl-test",
				       ptest_dev);
		if (res < 0) {
			dev_err(&pdev->dev,
				"unable to acquire interrupt for GPIO line %i\n",
				ptest_dev->gpios[i]);
			return res;
		}
		dev_info(&pdev->dev, "irq %d requested for gpio %d with DT index %d\n",
			 ptest_dev->irqs[i], ptest_dev->gpios[i], i);
	}

	res = pinctrl_test_init_debugfs(ptest_dev);
	if (res < 0)
		dev_warn(&pdev->dev,
			"cannot create debugfs files. Check if CONFIG_DEBUG_FS is enabled\n");

	platform_set_drvdata(pdev, ptest_dev);

	return 0;
}

static const struct of_device_id pinctrl_test_of_match[] = {
	{ .compatible = "google,pinctrl-test", },
	{ }
};

static struct platform_driver pinctrl_test_platform_driver = {
	.probe = pinctrl_test_probe,
	.driver = {
		.name = "pinctrl_test",
		.of_match_table = pinctrl_test_of_match,
	},
};

static int __init pinctrl_test_init(void)
{
	return platform_driver_register(&pinctrl_test_platform_driver);
}

static void __exit pinctrl_test_exit(void)
{
	platform_driver_unregister(&pinctrl_test_platform_driver);
}

module_init(pinctrl_test_init);
module_exit(pinctrl_test_exit);

MODULE_AUTHOR("Ivan Zaitsev <zaitsev@google.com>");
MODULE_DESCRIPTION("Pinctrl Test Driver");
MODULE_LICENSE("GPL");
