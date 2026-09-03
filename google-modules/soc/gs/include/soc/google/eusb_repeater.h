/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */

#ifndef __SOC_GOOGLE_EUSB_REPEATER_H
#define __SOC_GOOGLE_EUSB_REPEATER_H

#if IS_ENABLED(CONFIG_PHY_EXYNOS_EUSB_REPEATER)
void eusb_repeater_update_usb_state(bool on);
int eusb_repeater_power_on(void);
int eusb_repeater_power_off(void);
#else
static inline void eusb_repeater_update_usb_state(bool on) {}
static inline int eusb_repeater_power_on(void)
{
	return -ENODEV;
}
static inline int eusb_repeater_power_off(void)
{
	return -ENODEV;
}
#endif

#endif /* __SOC_GOOGLE_EUSB_REPEATER_H */
