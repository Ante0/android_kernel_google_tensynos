/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GXP generic event monitor interface.
 *
 * Copyright (C) 2024 Google LLC
 */

#ifndef __GXP_GEM_H__
#define __GXP_GEM_H__

#include "gxp-internal.h"

#define GEM_CNTR_CONFIG_STRIDE 0x4
#define GEM_SNAPSHOT_CNTR_STRIDE 0x4

#define GEM_CONFIG_ARMED_MASK 0x1
#define GEM_COUNT_EN_MASK 0x1
#define GEM_EVENT_SHIFT 2

/*
 * Available GEM Events
 *
 * Passed to gxp_gem_configure() to specify the type of event being counted.
 */
#define GEM_EVENT_READ_REQ 0x0
#define GEM_EVENT_WRITE_REQ 0x1
#define GEM_EVENT_WRITE_DATA 0x2
#define GEM_EVENT_READ_DATA 0x3

/**
 * gxp_gem_set_reg_resources() - Initialize the resource of the GEM CSRs.
 * @gxp: The gxp object to get dev and store the resources.
 *
 * Return:
 * * 0      - Registers are initialized successfully.
 * * Others - Negative errno.
 */
int gxp_gem_set_reg_resources(struct gxp_dev *gxp);

/**
 * gxp_gem_set_config() - Write the config value to the GEM_CONFIG register.
 * @gxp: The gxp object which contains the CSR address information.
 * @armed: To arm or disarm the GEM.
 */
void gxp_gem_set_config(struct gxp_dev *gxp, bool armed);

/**
 * gxp_gem_set_counter_config() - Write the config value of counter to the GEM_CNTR_CONFIG register.
 * @gxp: The gxp object which contains the CSR address information.
 * @count_id: The id of the counter to set.
 * @count_enable: To enable or disable the counter.
 * @event: The target event to record.
 */
void gxp_gem_set_counter_config(struct gxp_dev *gxp, int count_id, bool count_enable, u32 event);

/**
 * gxp_gem_get_counter_snapshot() -  Get the result of the counter.
 * @gxp: The gxp object which contains the CSR address information.
 * @count_id: The id of the counter to read value from.
 */
u32 gxp_gem_get_counter_snapshot(struct gxp_dev *gxp, int count_id);

#endif /* __GXP_GEM_H__ */
