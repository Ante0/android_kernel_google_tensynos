/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Platform device driver for Metis.
 *
 * Copyright (C) 2024 Google LLC
 */

#ifndef __METIS_PLATFORM_H__
#define __METIS_PLATFORM_H__

#include "gxp-mcu-platform.h"

#define to_metis_dev(gxp) container_of(to_mcu_dev(gxp), struct metis_dev, mcu_dev)

struct gcip_coresight_remote;

struct metis_dev {
	struct gxp_mcu_dev mcu_dev;
	struct gcip_coresight_remote *coresight_remote;
};

/**
 * metis_coresight_remote_init() - Initialize CoreSight remote for Metis.
 * @gxp: Pointer to the GXP device.
 */
void metis_coresight_remote_init(struct gxp_dev *gxp);

/**
 * metis_coresight_remote_exit() - Teardown CoreSight remote for Metis.
 * @gxp: Pointer to the GXP device.
 */
void metis_coresight_remote_exit(struct gxp_dev *gxp);

/**
 * metis_coresight_remote_restore() - Restore CoreSight remote state for Metis.
 * @gxp: Pointer to the GXP device.
 */
void metis_coresight_remote_restore(struct gxp_dev *gxp);

#endif /* __METIS_PLATFORM_H__ */
