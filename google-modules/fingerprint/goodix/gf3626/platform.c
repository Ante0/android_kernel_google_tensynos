// SPDX-License-Identifier: GPL-2.0
/*
 * platform indepent driver interface
 *
 * Coypritht (c) 2017 Goodix
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include "gf_spi.h"

#if defined(USE_SPI_BUS)
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#elif defined(USE_PLATFORM_BUS)
#include <linux/platform_device.h>
#endif

int gf_parse_dts(struct gf_dev *gf_dev)
{
	int rc = 0;
	struct device *dev = &gf_dev->spi->dev;
	struct device_node *np = dev->of_node;
	struct device_node *pinctrl_np;

	gf_dev->reset_gpio = of_get_named_gpio(np, "fp-gpio-reset", 0);
	if (gf_dev->reset_gpio < 0) {
		pr_err("failed to get reset gpio!\n");
		return gf_dev->reset_gpio;
	}

	rc = devm_gpio_request(dev, gf_dev->reset_gpio, "goodix_reset");
	if (rc) {
		pr_err("failed to request reset gpio, rc = %d\n", rc);
		return rc;
	}
	gpio_direction_output(gf_dev->reset_gpio, 0);

	gf_dev->irq_gpio = of_get_named_gpio(np, "fp-gpio-irq", 0);
	if (gf_dev->irq_gpio < 0) {
		pr_err("falied to get irq gpio!\n");
		return gf_dev->irq_gpio;
	}

	rc = devm_gpio_request(dev, gf_dev->irq_gpio, "goodix_irq");
	if (rc) {
		pr_err("failed to request irq gpio, rc = %d\n", rc);
		return rc;
	}
	gpio_direction_input(gf_dev->irq_gpio);

	gf_dev->vdd_io = devm_regulator_get(dev, "vdd_io");
	if (IS_ERR(gf_dev->vdd_io)) {
		pr_err("failed to get vdd_io regulator, rc = %ld\n",
		       PTR_ERR(gf_dev->vdd_io));
		return PTR_ERR(gf_dev->vdd_io);
	}

	if (IS_ENABLED(USE_PLATFORM_BUS)) {
		pinctrl_np = of_parse_phandle(np, "spi-gpio-provider", 0);
		if (!pinctrl_np) {
			dev_err(dev, "Failed to get spi-gpio-provider phandle\n");
			return -EINVAL;
		}

		gf_dev->spi_pinctrl_pdev = of_find_device_by_node(pinctrl_np);
		of_node_put(pinctrl_np);
		if (!gf_dev->spi_pinctrl_pdev) {
			dev_err(dev, "Failed to find spi pinctrl platform device\n");
			return -EPROBE_DEFER;
		}
	}

	return rc;
}

int gf_power_on(struct gf_dev *gf_dev)
{
	int rc = 0;

	rc = regulator_enable(gf_dev->vdd_io);
	if (rc)
		pr_err("failed to enable vdd_io regulator, rc = %d\n", rc);

	return rc;
}

int gf_power_off(struct gf_dev *gf_dev)
{
	int rc = 0;

	// Make sure reset pin is low before turning off power
	gpio_direction_output(gf_dev->reset_gpio, 0);
	mdelay(10);

	rc = regulator_disable(gf_dev->vdd_io);
	if (rc)
		pr_err("failed to disable vdd_io regulator, rc = %d\n", rc);

	return rc;
}

int gf_hw_reset(struct gf_dev *gf_dev, unsigned int delay_ms)
{
	if (gf_dev == NULL) {
		pr_info("Input buff is NULL.\n");
		return -EINVAL;
	}
	gpio_direction_output(gf_dev->reset_gpio, 0);
	gpio_set_value(gf_dev->reset_gpio, 0);
	mdelay(delay_ms);
	gpio_set_value(gf_dev->reset_gpio, 1);
	mdelay(delay_ms);
	return 0;
}

int gf_irq_num(struct gf_dev *gf_dev)
{
	if (gf_dev == NULL) {
		pr_info("Input buff is NULL.\n");
		return -EINVAL;
	}

	return gpio_to_irq(gf_dev->irq_gpio);
}

MODULE_LICENSE("GPL");
