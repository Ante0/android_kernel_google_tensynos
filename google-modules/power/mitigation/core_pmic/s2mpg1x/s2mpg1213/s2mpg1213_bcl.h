/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __S2MPG1213_H
#define __S2MPG1213_H

struct bcl_device;
struct gpio_desc;

int get_throttle_lvl_addr(int id);
int register_zone(struct bcl_device *bcl_dev, int idx, const char *devname,
		  struct gpio_desc *pin, int irq, int type, int irq_config,
		  int polarity, u32 flag);

#endif /* __S2MPG1213_H */
