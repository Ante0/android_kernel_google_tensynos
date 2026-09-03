/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef _WLAN_HDD_SYSFS_TAS_H
#define _WLAN_HDD_SYSFS_TAS_H

#if defined(WLAN_SYSFS) && defined(WLAN_TAS_SYSFS)
/**
 * hdd_sysfs_tas_create() - Create TAS sysfs node
 * @driver_kobject: sysfs driver kobject
 *
 * Return: 0 on success, negative errno on failure
 */
int hdd_sysfs_tas_create(struct kobject *driver_kobject);

/**
 * hdd_sysfs_tas_destroy() - Destroy TAS sysfs node
 * @driver_kobject: sysfs driver kobject
 *
 * Return: None
 */
void hdd_sysfs_tas_destroy(struct kobject *driver_kobject);

#else
static inline int hdd_sysfs_tas_create(struct kobject *driver_kobject)
{
	return 0;
}

static inline void hdd_sysfs_tas_destroy(struct kobject *driver_kobject)
{
}
#endif
#endif /* _WLAN_HDD_SYSFS_TAS_H */
