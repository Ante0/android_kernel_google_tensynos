// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>

#include "radio-utils.h"
#include "remote-wakeup.h"

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
#include "md2ap-wakemon.h"
#endif

#define PEWAKE_GPIO_NAME "pe-wake"

static irqreturn_t modem_pewake_handler(int irq, void *arg)
{
	struct remote_wakeup *remote_wakeup = arg;
	struct device *dev = remote_wakeup->goog->mdev->dev;

	LOG_INFO("Enter PEWAKE handler\n");

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
	md2ap_wakemon_pewake(remote_wakeup->goog);
#endif

	pm_wakeup_event(dev, 0);
	pm_request_resume(dev);

	return IRQ_HANDLED;
}

static int remote_wakeup_request_gpio(struct remote_wakeup *remote_wakeup)
{
	struct device *dev = remote_wakeup->goog->mdev->dev;
	struct gpio_desc *gpio;
	int ret;

	gpio = devm_gpiod_get(dev, PEWAKE_GPIO_NAME, GPIOD_IN);
	if (IS_ERR(gpio)) {
		LOG_ERR("Failed to get GPIOD!\n");
		ret = PTR_ERR(gpio);
		return ret;
	}

	remote_wakeup->pewake_gpio = gpio;
	LOG_INFO("Requested GPIOD<%s:%d>\n", PEWAKE_GPIO_NAME, desc_to_gpio(gpio));

	return 0;
}

static int remote_wakeup_request_irq(struct remote_wakeup *remote_wakeup)
{
	struct device *dev = remote_wakeup->goog->mdev->dev;
	int irq;
	int ret;

	irq = gpiod_to_irq(remote_wakeup->pewake_gpio);
	if (irq < 0) {
		LOG_ERR("Failed to get IRQ number!\n");
		return irq;
	}

	ret = devm_request_irq(dev, irq, modem_pewake_handler,
			       IRQF_TRIGGER_FALLING | IRQF_NO_AUTOEN, "modem_pewake",
			       remote_wakeup);
	if (ret) {
		LOG_ERR("Failed to request IRQ: %d\n", ret);
		return ret;
	}

	remote_wakeup->pewake_irq = irq;
	LOG_INFO("Requested IRQ<%d>\n", irq);

	return 0;
}

static int remote_wakeup_free_irq(struct remote_wakeup *remote_wakeup)
{
	struct device *dev = remote_wakeup->goog->mdev->dev;

	if (remote_wakeup->pewake_irq == IRQ_NOTCONNECTED)
		return -ENODEV;

	disable_irq(remote_wakeup->pewake_irq);
	devm_free_irq(dev, remote_wakeup->pewake_irq, NULL);

	return 0;
}

int remote_wakeup_init(struct radio_google *goog)
{
	struct remote_wakeup *remote_wakeup;
	int ret;

	remote_wakeup = devm_kzalloc(goog->mdev->dev, sizeof(*remote_wakeup), GFP_KERNEL);
	if (!remote_wakeup)
		return -ENOMEM;

	remote_wakeup->goog = goog;
	goog->remote_wakeup = remote_wakeup;
	remote_wakeup->pewake_gpio = NULL;
	remote_wakeup->pewake_irq = IRQ_NOTCONNECTED;
	remote_wakeup->ready = false;
	remote_wakeup->enabled = false;

	ret = remote_wakeup_request_gpio(remote_wakeup);
	if (ret)
		return ret;

	ret = remote_wakeup_request_irq(remote_wakeup);
	if (ret)
		return ret;

	remote_wakeup->ready = true;

	return 0;
}

void remote_wakeup_exit(struct radio_google *goog)
{
	struct remote_wakeup *remote_wakeup = goog->remote_wakeup;

	remote_wakeup_free_irq(remote_wakeup);
	devm_kfree(goog->mdev->dev, remote_wakeup);
}

int remote_wakeup_enable(struct radio_google *goog)
{
	struct remote_wakeup *remote_wakeup = goog->remote_wakeup;
	int ret = 0;

	if (!remote_wakeup->ready) {
		LOG_ERR("Remote wakeup was not ready!\n");
		return -EAGAIN;
	}

	if (remote_wakeup->enabled) {
		LOG_ERR("Remote wakeup has already been enabled!\n");
		return -EBUSY;
	}

	enable_irq(remote_wakeup->pewake_irq);

	ret = enable_irq_wake(remote_wakeup->pewake_irq);
	if (ret) {
		LOG_ERR("Failed to enable wakeup mode: %d\n", ret);
		disable_irq(remote_wakeup->pewake_irq);
		return ret;
	}

	remote_wakeup->enabled = true;
	LOG_INFO("Remote wakeup enabled\n");

	return 0;
}
EXPORT_SYMBOL_GPL(remote_wakeup_enable);

int remote_wakeup_disable(struct radio_google *goog)
{
	struct remote_wakeup *remote_wakeup = goog->remote_wakeup;
	int ret = 0;

	if (!remote_wakeup->ready) {
		LOG_ERR("Remote wakeup was not ready!\n");
		return -EAGAIN;
	}

	if (!remote_wakeup->enabled) {
		LOG_ERR("Remote wakeup was not enabled!\n");
		return -EPERM;
	}

	disable_irq(remote_wakeup->pewake_irq);

	ret = disable_irq_wake(remote_wakeup->pewake_irq);
	if (ret) {
		LOG_ERR("Failed to disable wakeup mode: %d\n", ret);
	}

	remote_wakeup->enabled = false;
	LOG_INFO("Remote wakeup disabled\n");

	return ret;
}
EXPORT_SYMBOL_GPL(remote_wakeup_disable);
