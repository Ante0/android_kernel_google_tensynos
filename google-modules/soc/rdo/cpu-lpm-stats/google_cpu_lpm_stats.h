/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC
 */
#ifndef __GOOGLE_CPU_LPM_STATS_H__
#define __GOOGLE_CPU_LPM_STATS_H__

#include "linux/kobject.h"
#include "linux/notifier.h"
#include "linux/types.h"

#include <cap-sysfs/google_cap_sysfs.h>

struct cpu_lpm_stats_elem {
	/* Total residency of the power domain */
	u64 total_residency;
	/* Minimum residency for the power domain */
	u64 min;
	/* Maximum residency for the power domain */
	u64 max;
	/* Timestamp of the last exit from the power domain */
	u64 last_exit_ts;
	/* Timestamp of the last entry to the power domain */
	u64 last_entry_ts;
	/* Number of times the power domain has been entered */
	u64 count;
};

struct latency_stats {
	/* Average latency of specific transition */
	u64 avg;
	/* Maximum latency of specific transition */
	u64 max;
	/* Minimum latency of specific transition */
	u64 min;
};

struct cpu_latency_stats_elem {
	/* Firmware entry latency stats */
	struct latency_stats fw_entry;
	/* Firmware exit latency stats */
	struct latency_stats fw_exit;
	/* Firmware entry scheduling latency stats */
	struct latency_stats fw_sched_entry;
	/* Firmware exit scheduling latency stats */
	struct latency_stats fw_sched_exit;
	/* Firmware entry transition latency stats */
	struct latency_stats fw_entry_transition;
	/* Firmware exit transition latency stats */
	struct latency_stats fw_exit_transition;
	/* TF-A entry latency stats */
	struct latency_stats tf_a_entry;
	/* TF-A exit latency stats */
	struct latency_stats tf_a_exit;
	/* TF-A boot latency stats */
	struct latency_stats tf_a_boot;
	/* End-to-end entry latency stats */
	struct latency_stats e2e_entry;
	/* End-to-end exit latency stats */
	struct latency_stats e2e_exit;
};

struct cpu_lpm_data {
	bool collect_data;
	struct notifier_block nb;
	struct list_head list;
	struct cpu_lpm_stats_elem data;
	struct cpu_latency_stats_elem latency_data;
	struct device_node *power_domain_phandle;

	/* cluster PD for cores, NULL for clusters*/
	struct device_node *cluster_pd_phandle;
	/* -1 for clusters */
	unsigned int core_id;

	u32 *power_state;
	u32 power_state_count;

	struct device *genpd_dev;
	const char *name;
	spinlock_t lock;

	/* power domain sysfs directory */
	struct kobject pd_dir;
	/* <power_domain>/residency_data sysfs directory */
	struct kobject pd_residency_data;
	/* <power_domain>/latency_data sysfs directory */
	struct kobject pd_latency_data;
	/* <power_domain>/latency_data/fw_entry sysfs directory */
	struct kobject pd_fw_entry_stats;
	/* <power_domain>/latency_data/fw_exit sysfs directory */
	struct kobject pd_fw_exit_stats;
	/* <power_domain>/latency_data/fw_sched_entry sysfs directory */
	struct kobject pd_fw_sched_entry_stats;
	/* <power_domain>/latency_data/fw_sched_exit sysfs directory */
	struct kobject pd_fw_sched_exit_stats;
	/* <power_domain>/latency_data/fw_entry_transition sysfs directory */
	struct kobject pd_fw_entry_transition_stats;
	/* <power_domain>/latency_data/fw_exit_transition sysfs directory */
	struct kobject pd_fw_exit_transition_stats;
	/* <power_domain>/latency_data/tf_a_entry sysfs directory */
	struct kobject pd_tf_a_entry_stats;
	/* <power_domain>/latency_data/tf_a_exit sysfs directory */
	struct kobject pd_tf_a_exit_stats;
	/* <power_domain>/latency_data/tf_a_boot sysfs directory */
	struct kobject pd_tf_a_boot_stats;
	/* <power_domain>/latency_data/e2e_entry sysfs directory */
	struct kobject pd_e2e_entry_stats;
	/* <power_domain>/latency_data/e2e_exit sysfs directory */
	struct kobject pd_e2e_exit_stats;
};

#endif // __GOOGLE_CPU_LPM_STATS_H__
