/* SPDX-License-Identifier: GPL-2.0 */

/**
 * @copyright Copyright (c) 2023 Samsung Electronics Co., Ltd
 *
 */

#ifndef __UWB_GPIO_H__
#define __UWB_GPIO_H__

#include "uwb.h"

#define GPIO_DELAY_MS 50

/* uci gpio irq */
int uwb_init_irq(struct u100_ctx *u100_ctx, unsigned int num);

void set_gpio_value(void *desc, int value);
int get_gpio_value(void *desc);

unsigned long get_power_switch_delay(void);

#endif /* __UWB_GPIO_H__ */
