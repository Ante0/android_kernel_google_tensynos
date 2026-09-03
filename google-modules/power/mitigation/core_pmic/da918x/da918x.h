/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __DA918X_H
#define __DA918X_H

#include <linux/types.h>

struct bcl_device;

int core_pmic_main_write_register(struct bcl_device *bcl_dev, u16 reg, u8 value, bool is_meter);
int core_pmic_main_read_register(struct bcl_device *bcl_dev, u16 reg, u8 *value, bool is_meter);
int core_pmic_sub_read_register(struct bcl_device *bcl_dev, u16 reg, u8 *value, bool is_meter);
int core_pmic_sub_write_register(struct bcl_device *bcl_dev, u16 reg, u8 value, bool is_meter);

#endif /* __DA918X_H */
