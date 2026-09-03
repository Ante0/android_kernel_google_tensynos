/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * DOC: wlan_hdd_sysfs_dp_dal.h
 *
 * implementation for creating dal sysfs file
 */

#ifndef _WLAN_HDD_SYSFS_DP_DAL_H
#define _WLAN_HDD_SYSFS_DP_DAL_H

#if defined(WLAN_SYSFS) && defined(FEATURE_DP_DAL_SIM) && \
defined(FEATURE_DAL_DP_SUPPORT)
/**
 * hdd_sysfs_dp_dal_create() - create DAL SIM mode sysfs file
 * @driver_kobject: sysfs driver kobject
 *
 * file path: /sys/kernel/wcn7760/dal_sim_mode
 *
 * usage:
 *      mode switch:
 *      0 - bypass mode , 1 - offload mode
 *      echo 0/1 > /sys/kernel/wcn7760/dal_sim_mode
 *
 *      Get current dal sim mode:
 *      cat /sys/kernel/wcn7760/dal_sim_mode
 * Return: 0 on success, errno on failure
 */
int hdd_sysfs_dp_dal_create(struct kobject *driver_kobject);

/**
 * hdd_sysfs_dp_dal_destroy() - remove DAL SIM mode sysfs file
 * @driver_kobject: sysfs driver kobject
 *
 * Return: None
 */
void hdd_sysfs_dp_dal_destroy(struct kobject *driver_kobject);
#else
static inline int hdd_sysfs_dp_dal_create(struct kobject *driver_kobject)
{
	return 0;
}

static inline void hdd_sysfs_dp_dal_destroy(struct kobject *driver_kobject)
{
}
#endif
#endif /* _WLAN_HDD_SYSFS_DP_DAL_H */
