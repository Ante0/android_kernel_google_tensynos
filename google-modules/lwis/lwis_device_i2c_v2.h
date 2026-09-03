/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Google LWIS I2C Device Driver V2
 *
 * Copyright (c) 2024 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef LWIS_DEVICE_I2C_V2_H_
#define LWIS_DEVICE_I2C_V2_H_

extern struct lwis_device_subclass_operations i2c_vops;

int lwis_i2c_device_v2_init(void);
int lwis_i2c_device_v2_deinit(void);

#endif /* LWIS_DEVICE_I2C_V2_H_ */
