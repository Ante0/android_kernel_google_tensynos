/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __S2MPG1X_H
#define __S2MPG1X_H

#include <linux/types.h>

struct i2c_client;

int core_pmic_main_write_register(struct i2c_client *i2c, u8 reg, u8 value);
int core_pmic_main_read_register(struct i2c_client *i2c, u8 reg, u8 *value);
int core_pmic_sub_read_register(struct i2c_client *i2c, u8 reg, u8 *value);
int core_pmic_sub_write_register(struct i2c_client *i2c, u8 reg, u8 value);
int get_rtc_scratch1_register(u8 *value);

#endif /* __S2MPG1X_H */
