// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC.
 */

#include <linux/gpio.h>
#include <linux/platform_device.h>

#include "radio-google.h"
#include "radio-utils.h"
#include "ap-awake.h"

static struct gpio_desc *suspend_gpio;
#define SUSPEND_GPIO_NAME "ap2cp-apawake"

int ap_awake_gpio_init(struct radio_google *goog)
{
	int ret;

	suspend_gpio = devm_gpiod_get(goog->mdev->dev, SUSPEND_GPIO_NAME, GPIOD_OUT_HIGH);
	if (IS_ERR(suspend_gpio)) {
		ret = PTR_ERR(suspend_gpio);
		suspend_gpio = NULL;
		LOG_ERR("gpiod_get() failed! (rc: %d)\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ap_awake_gpio_init);

int ap_awake_gpio_set(bool state)
{
	if (!suspend_gpio)
		return -EINVAL;

	gpiod_set_value(suspend_gpio, state);
	return 0;
}
EXPORT_SYMBOL_GPL(ap_awake_gpio_set);
