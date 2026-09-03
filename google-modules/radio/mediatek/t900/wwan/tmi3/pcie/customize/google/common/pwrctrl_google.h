/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Google LLC.
 */

#ifndef __PWRCTRL_GOOGLE_H__
#define __PWRCTRL_GOOGLE_H__

#include <linux/platform_device.h>

/**
 * pwrctrl_google_init - Initialize the Google power control module.
 * @pdev: The platform device for the modem.
 *
 * This function changes pinctrl state to `idle` and caches the platform
 * device pointer for later use by the pinctrl and GPIO control functions.
 * It should be called when the modem device driver is being probed.
 *
 * Return: 0 on success, negative error code on failure.
 */
int pwrctrl_google_init(struct platform_device *pdev);

/**
 * pwrctrl_google_exit - Exit/cleanup the Google power control module.
 * @pdev: The platform device for the modem.
 *
 * Clears the cached platform device pointer. It should be called when
 * the modem device driver is being deinitialized.
 */
void pwrctrl_google_exit(struct platform_device *pdev);

/**
 * mtk_gpio_sap_ctrl_set_off - Configure modem GPIOs/pinctrl for the OFF state.
 *
 * This is intended to be called when the modem is being powered down. This
 * function provides the strong implementation for the weak symbol in the
 * Alcedo power control driver.
 *
 * Return: 0 on success, negative error code on failure.
 */
int mtk_gpio_sap_ctrl_set_off(void);

/**
 * mtk_gpio_sap_ctrl_set_working - Configure modem GPIOs/pinctrl for the WORKING state.
 *
 * This is intended to be called when the modem is being powered up. This
 * function provides the strong implementation for the weak symbol in the
 * Alcedo power control driver.
 *
 * Return: 0 on success, negative error code on failure.
 */
int mtk_gpio_sap_ctrl_set_working(void);

#endif /* __PWRCTRL_GOOGLE_H__ */
