/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * google_powercap_cpu.h Google cpu powercap related functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */
#ifndef _GOOGLE_POWERCAP_CPU_H_
#define _GOOGLE_POWERCAP_CPU_H_

#include <kunit/visibility.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/pm_qos.h>
#include <linux/version.h>

#include "google_powercap.h"

#define GPOWERCAP_CPU_UTIL_PERCENTAGE_INITIAL 10

extern struct list_head gpowercap_cpu_list;

struct gpowercap_cpu {
	struct gpowercap gpowercap;
	struct freq_qos_request qos_req;
	unsigned int cpu;
	unsigned int num_opps;
	cpumask_t related_cpus;
	struct cdev_opp_table *opp_table;
	const char *odpm_rail_name;
	struct notifier_block odpm_nb;
	struct delayed_work odpm_work;
	u64 last_power_uw;
	u64 power_limit;
	int target_opp_idx;
	u64 util_percentage;
	struct list_head node;
};

static inline struct gpowercap_cpu *to_gpowercap_cpu(struct gpowercap *gpowercap)
{
	return container_of(gpowercap, struct gpowercap_cpu, gpowercap);
}
int gpc_google_pm_qos_add_cpufreq_request(struct cpufreq_policy *policy,
					  struct freq_qos_request *req, enum freq_qos_req_type type,
					  s32 value);
void gpc_cpufreq_cpu_put(struct cpufreq_policy *policy);
int gpc_google_pm_qos_remove_cpufreq_request(struct cpufreq_policy *policy,
					     struct freq_qos_request *req);
struct cpufreq_policy *gpc_cpufreq_cpu_get(unsigned int cpu);
unsigned int gpc_cpufreq_quick_get(unsigned int cpu);
int gpc_freq_qos_update_request(struct freq_qos_request *req, s32 new_value);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
int gpc_apply_thermal_pressure(cpumask_t cpus, unsigned long frequency,
			       enum thermal_pressure_type type)
#endif
	int gpc_cdev_cpufreq_get_opp_count(unsigned int cpu);
int gpc_cdev_cpufreq_update_opp_table(unsigned int cpu, enum hw_dev_type cdev_id,
				      struct cdev_opp_table *cdev_table, unsigned int num_opp);
u64 __gpc_cpu_set_cluster_power_limit(struct gpowercap *gpowercap, u64 power_limit);
u64 __gpc_cpu_get_cluster_power_uw(struct gpowercap *gpowercap);
int __gpc_cpu_update_cluster_power_uw(struct gpowercap *gpowercap);
void __gpc_cpu_pd_release(struct gpowercap *gpowercap);

#if IS_ENABLED(CONFIG_KUNIT)
void __gpc_cpu_odpm_work_func(struct work_struct *work);
int __gpc_cpu_set_time_window_us(struct gpowercap *gpowercap, u64 time_window_us);
bool gpc_cancel_delayed_work_sync(struct delayed_work *dwork);
#endif // IS_ENABLED(CONFIG_KUNIT)

int __gpc_cpu_setup(int cpu, struct gpowercap *parent, enum hw_dev_type cdev_id,
		    const char *odpm_rail_name, u64 polling_interval_ms);
#endif // _GOOGLE_POWERCAP_CPU_H_
