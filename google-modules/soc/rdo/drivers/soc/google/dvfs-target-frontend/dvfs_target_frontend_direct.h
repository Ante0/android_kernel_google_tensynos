/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2025 Google LLC */
#ifndef _DVFS_TARGET_FRONTEND_DIRECT_H
#define _DVFS_TARGET_FRONTEND_DIRECT_H

#include <linux/platform_device.h>
#include <linux/types.h>

/**
 * TODO(b/441700036): The hard coded pwrblk id should be removed once the complete
 * implementation of frontend infra is available
 */
#define PWRBLK_CODEC_3P 5
#define PWRBLK_CPUACC 6
#define PWRBLK_DPU 7
#define PWRBLK_G2D 13
#define PWRBLK_GCV 14
#define PWRBLK_ISPBE 26
#define PWRBLK_ISPFE 27
#define PWRBLK_PCIE 31
#define PWRBLK_MAX_ID 32

int dvfs_fe_prepare_domain_direct(int domain_id);
void dvfs_fe_set_pf_level_direct(int domain_id, uint8_t pf_level);
uint8_t dvfs_fe_get_pf_level_direct(int domain_id);
int dvfs_target_frontend_direct_init(struct device *target_dev);
void dvfs_target_frontend_direct_exit(void);

#endif /* _DVFS_TARGET_FRONTEND_DIRECT_H */
