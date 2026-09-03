/* SPDX-License-Identifier: GPL-2.0 */
/*
 * google_uclamp_cdev_helper.h Helper for uclamp cooling device.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */
#ifndef _GOOGLE_UCLAMP_CDEV_HELPER_H_
#define _GOOGLE_UCLAMP_CDEV_HELPER_H_

#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

struct thermal_uclamp_cdev {
	unsigned int cpu;
	unsigned int cur_state;
	unsigned int max_state;
	struct thermal_cooling_device *cdev;
	struct cdev_opp_table *opp_table;
	struct list_head cdev_list;
};

int thermal_uclamp_probe_helper(struct platform_device *pdev);
void thermal_uclamp_remove_helper(struct platform_device *pdev);

#if IS_ENABLED(CONFIG_KUNIT)
enum hw_dev_type;
int guc_of_property_count_u32_elems(const struct device_node *np, const char *propname);
int guc_of_property_read_u32_index(const struct device_node *np, const char *propname,
				   u32 index, u32 *out_value);
struct device_node *guc_of_find_node_by_phandle(u32 phandle);
int guc_of_cpu_node_to_id(struct device_node *node);
void guc_of_node_put(struct device_node *node);
int guc_cdev_cpufreq_get_opp_count(unsigned int cpu);
int guc_cdev_cpufreq_update_opp_table(unsigned int cpu, enum hw_dev_type cdev_id,
				      struct cdev_opp_table *cdev_table,
				      unsigned int num_opp);
struct thermal_cooling_device *
guc_thermal_cooling_device_register(const char *type, void *devdata,
				    const struct thermal_cooling_device_ops *ops);
void guc_thermal_cooling_device_unregister(struct thermal_cooling_device *cdev);
void guc_sched_thermal_freq_cap(unsigned int cpu, unsigned long freq);
int guc_device_create_file(struct device *device, const struct device_attribute *entry);
void guc_device_remove_file(struct device *dev, const struct device_attribute *attr);
ssize_t state2power_table_show(struct device *dev, struct device_attribute *attr, char *buf);
#endif /* CONFIG_KUNIT */

#endif // _GOOGLE_UCLAMP_CDEV_HELPER_H_
