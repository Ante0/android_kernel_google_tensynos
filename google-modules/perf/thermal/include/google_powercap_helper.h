/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * google_powercap_helper.h Google powercap related helper functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */
#ifndef _GOOGLE_POWERCAP_HELPER_H_
#define _GOOGLE_POWERCAP_HELPER_H_

#include <linux/devfreq.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/version.h>

#include "google_powercap.h"

#define GPOWERCAP_POWER_LIMIT_FLAG			0
#define GPOWERCAP_POWER_LIMIT_BYPASS_FLAG		1
#define GPOWERCAP_USERSPACE_LIMIT_FLAG			2
#define GPOWERCAP_PARENT_LIMIT_FLAG			3
#define GPOWERCAP_POWER_LIMIT_BYPASS_TIME_MSEC_MAX	5000
#define GPOWERCAP_CONTROL_TYPE "gpc"

int __get_power_uw(struct gpowercap *gpowercap, u64 *power_uw);
void __gpowercap_sub_power(struct gpowercap *gpowercap);
void __gpowercap_add_power(struct gpowercap *gpowercap);
int __gpowercap_update_power(struct gpowercap *gpowercap);
int __gpowercap_release_zone(struct powercap_zone *pcz);
int __set_power_limit_uw(struct gpowercap *gpowercap, u64 power_limit);
int gpowercap_set_parent_power_limit(struct gpowercap *gpowercap, u64 power_limit);
ssize_t __power_levels_uw_show(struct gpowercap *gpowercap, char *buf);
int __gpowercap_register(const char *name, struct gpowercap *gpowercap,
			struct gpowercap *parent);
void __gpowercap_destroy_tree_recursive(struct gpowercap *gpowercap);
void __gpowercap_destroy_hierarchy(void);
int __power_limit_bypass(struct gpowercap *gpowercap, int time_ms);

int gpowercap_dt_probe(struct platform_device *pdev);
void gpowercap_dt_remove(struct platform_device *pdev);
struct device_node *gpc_of_parse_phandle(const struct device_node *np,
					const char *phandle_name, int index);
int gpc_of_property_read_u32(const struct device_node *np, const char *propname,
				u32 *out_value);

#if IS_ENABLED(CONFIG_KUNIT)
void __gpc_init_pct_test(void);
#endif
extern struct device_node *gpc_of_parse_phandle(const struct device_node *np,
						const char *phandle_name, int index);
extern struct powercap_control_type *gpc_powercap_register_control_type(
	struct powercap_control_type *control_type, const char *name,
	const struct powercap_control_type_ops *ops);
extern int gpc_powercap_unregister_control_type(struct powercap_control_type *control_type);
extern struct powercap_zone *gpc_powercap_register_zone(
		struct powercap_zone *power_zone,
		struct powercap_control_type *control_type, const char *name,
		struct powercap_zone *parent, const struct powercap_zone_ops *ops,
		int nr_constraints,
		const struct powercap_zone_constraint_ops *const_ops);
extern int gpc_powercap_unregister_zone(struct powercap_control_type *control_type,
					struct powercap_zone *power_zone);
extern struct cpufreq_policy *gpc_cpufreq_cpu_get(unsigned int cpu);
extern int gpowercap_register(const char *name, struct gpowercap *gpowercap,
				struct gpowercap *parent);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
extern int gpc_apply_thermal_pressure(cpumask_t cpus, unsigned long frequency,
					enum thermal_pressure_type type);
#endif
extern int gpc_google_pm_qos_add_cpufreq_request(struct cpufreq_policy *policy,
					     struct freq_qos_request *req,
					     enum freq_qos_req_type type, s32 value);
extern int gpc_google_pm_qos_remove_cpufreq_request(struct cpufreq_policy *policy,
						struct freq_qos_request *req);
extern unsigned int gpc_cpufreq_quick_get(unsigned int cpu);
extern int cdev_cpufreq_get_opp_count(unsigned int cpu);
extern int cdev_cpufreq_update_opp_table(unsigned int cpu, enum hw_dev_type cdev_id,
					 struct cdev_opp_table *cdev_table, unsigned int num_opp);
extern int gpc_device_create_file(struct device *device, const struct device_attribute *entry);
extern void gpc_device_remove_file(struct device *device, const struct device_attribute *entry);
extern int gpc_freq_qos_update_request(struct freq_qos_request *req, s32 new_value);
extern void gpc_cpufreq_cpu_put(struct cpufreq_policy *policy);
extern void gpc_warn_on_once(void);
extern int cdev_dev_pm_qos_update_request(struct dev_pm_qos_request *req, s32 new_value);
extern int cdev_pm_qos_add_devfreq_request(struct devfreq *devfreq,
					   struct dev_pm_qos_request *req,
					   enum dev_pm_qos_req_type type, s32 value);

#endif  // _GOOGLE_POWERCAP_HELPER_H_
