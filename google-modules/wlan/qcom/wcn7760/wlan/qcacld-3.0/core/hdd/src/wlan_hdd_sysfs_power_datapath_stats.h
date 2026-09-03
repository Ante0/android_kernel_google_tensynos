/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * DOC: wlan_hdd_sysfs_power_datapath_stats.h
 *
 * Implementation for creating sysfs files for power and datapath statistics
 */

#ifndef _WLAN_HDD_SYSFS_POWER_DATAPATH_STATS_H
#define _WLAN_HDD_SYSFS_POWER_DATAPATH_STATS_H

#if defined(WLAN_FEATURE_POWER_STATISTICS)

#endif

#if defined(WLAN_SYSFS)
/**
 * hdd_sysfs_power_datapath_stats_create() - API to create power_datapath_stats
 * @adapter: pointer to adapter
 *
 * This creates sysfs file for power and datapath statistics.
 * file path: /sys/class/net/wlanxx/power_datapath_stats
 * where wlanxx is adapter name
 *
 * File operations:
 *   - Write: Send commands to firmware
 *     Usage: echo "retrieve <stats_type> <core_index>" > power_datapath_stats
 *
 *     Supported commands:
 *       echo "retrieve power 0" > power_datapath_stats
 *       echo "retrieve datapath 0" > power_datapath_stats
 *       echo "retrieve all 0" > power_datapath_stats
 *       echo "retrieve power 0xFF" > power_datapath_stats
 *
 *   - Read: Display collected statistics
 *     Usage: cat power_datapath_stats
 *
 * Return: 0 on success and errno on failure
 */
int hdd_sysfs_power_datapath_stats_create(struct hdd_adapter *adapter);

/**
 * hdd_sysfs_power_datapath_stats_destroy() - API to destroy
 *                                             power_datapath_stats
 * @adapter: pointer to adapter
 *
 * Return: none
 */
void hdd_sysfs_power_datapath_stats_destroy(struct hdd_adapter *adapter);
#else
static inline int
hdd_sysfs_power_datapath_stats_create(struct hdd_adapter *adapter)
{
	return 0;
}

static inline void
hdd_sysfs_power_datapath_stats_destroy(struct hdd_adapter *adapter)
{
}
#endif
#endif /* #ifndef _WLAN_HDD_SYSFS_POWER_DATAPATH_STATS_H */
