// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC.
 */

#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>

#include "radio-utils.h"
#include "pwrctrl_google.h"
#include "ap-awake.h"

static struct platform_device *modem_dev;

int pwrctrl_google_init(struct platform_device *pdev)
{
	int ret = pinctrl_pm_select_idle_state(&pdev->dev);
	if (ret < 0) {
		LOG_ERR("Failed to set pinctrl idle state, ret = %d\n", ret);
		return ret;
	}

	modem_dev = pdev;
	return 0;
}
EXPORT_SYMBOL_GPL(pwrctrl_google_init);

void pwrctrl_google_exit(struct platform_device *pdev)
{
	modem_dev = NULL;
}
EXPORT_SYMBOL_GPL(pwrctrl_google_exit);

int mtk_gpio_sap_ctrl_set_off(void)
{
	int ret = 0;
	int tmp_ret;

	if (!modem_dev) {
		LOG_ERR("modem_dev not initialized\n");
		return -ENODEV;
	}

	LOG_INFO("Changing modem GPIOs to off state\n");

	tmp_ret = pinctrl_pm_select_idle_state(&modem_dev->dev);
	if (tmp_ret < 0) {
		LOG_ERR("Failed to set pinctrl idle state, ret = %d\n", tmp_ret);
		ret = tmp_ret;
	}

	tmp_ret = ap_awake_gpio_set(0);
	if (tmp_ret < 0) {
		LOG_ERR("Failed to set ap_awake low, ret = %d\n", tmp_ret);
		if (!ret)
			ret = tmp_ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mtk_gpio_sap_ctrl_set_off);

int mtk_gpio_sap_ctrl_set_working(void)
{
	int ret = 0;
	int tmp_ret;

	if (!modem_dev) {
		LOG_ERR("modem_dev not initialized\n");
		return -ENODEV;
	}

	LOG_INFO("Changing modem GPIOs to work state\n");

	tmp_ret = pinctrl_pm_select_default_state(&modem_dev->dev);
	if (tmp_ret < 0) {
		LOG_ERR("Failed to set pinctrl default state, ret = %d\n", tmp_ret);
		ret = tmp_ret;
	}

	tmp_ret = ap_awake_gpio_set(1);
	if (tmp_ret < 0) {
		LOG_ERR("Failed to set ap_awake high, ret = %d\n", tmp_ret);
		if (!ret)
			ret = tmp_ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mtk_gpio_sap_ctrl_set_working);
