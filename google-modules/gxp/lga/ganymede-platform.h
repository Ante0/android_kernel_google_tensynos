/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Platform device driver for Ganymede.
 *
 * Copyright (C) 2024 Google LLC
 */

#ifndef __GANYMEDE_PLATFORM_H__
#define __GANYMEDE_PLATFORM_H__

#include "gxp-mcu-platform.h"

#define to_ganymede_dev(gxp)                                                     \
	container_of(to_mcu_dev(gxp), struct ganymede_dev, mcu_dev)

struct ganymede_dev {
	struct gxp_mcu_dev mcu_dev;
};

#endif /* __GANYMEDE_PLATFORM_H__ */
