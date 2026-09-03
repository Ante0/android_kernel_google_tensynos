/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * google_powercap.h Google powercap related functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */
#ifndef __GOOGLE_POWERCAP_H__
#define __GOOGLE_POWERCAP_H__

#include <dt-bindings/soc/google/google-thermal-def.h>
#include <linux/powercap.h>

#include "cdev_helper.h"
#include "thermal_cpm_mbox.h"

#define MAX_GOOGLE_POWERCAP_CONSTRAINTS 1

struct gpowercap {
	struct powercap_zone zone;
	struct gpowercap *parent;
	struct list_head siblings;
	struct list_head children;
	struct gpowercap_ops *ops;
	unsigned long flags;
	u64 power_limit;
	u64 userspace_power_limit;
	u64 parent_power_limit;
	u64 power_max;
	u64 power_min;
	unsigned int num_opps;
	struct cdev_opp_table *opp_table;
	struct mutex lock;
	struct delayed_work bypass_work;
	u64 time_window_us;
	u64 current_power_uw;
	bool power_updated;
	struct gpowercap_stats *stats;
};

struct gpowercap_ops {
	u64 (*set_power_uw)(struct gpowercap *gpc, u64 power_limit);
	u64 (*get_power_uw)(struct gpowercap *gpc);
	int (*update_power_uw)(struct gpowercap *gpc);
	int (*evaluate)(struct gpowercap *gpc);
	void (*release)(struct gpowercap *gpc);
	int (*set_time_window_us)(struct gpowercap *gpc, u64 time_window_us);
	int (*rebalance)(struct gpowercap *gpc);
};

struct device_node;

struct gpowercap_subsys_ops {
	const char *name;
	void (*exit)(void);
	int (*setup)(struct gpowercap *gpc, struct device_node *np, enum hw_dev_type cdev_id,
			const char *odpm_rail_name, u64 polling_interval_us);
	struct gpowercap *(*algo_setup)(struct device_node *dn, struct gpowercap *parent);
};

extern struct gpowercap_subsys_ops gpc_cpu_dev_ops;
extern struct gpowercap_subsys_ops gpc_devfreq_dev_ops;
extern struct gpowercap_subsys_ops gpc_virt_volt_dev_ops;
extern struct gpowercap_subsys_ops gpc_virt_weights_dev_ops;

static struct gpowercap_subsys_ops *gpc_device_ops[] = {
	&gpc_cpu_dev_ops, // GPOWERCAP_NODE_CPU
	&gpc_devfreq_dev_ops, // GPOWERCAP_NODE_DEVFREQ
	NULL, // GPOWERCAP_NODE_VIRTUAL
	&gpc_virt_volt_dev_ops, // GPOWERCAP_NODE_VIRTUAL_VOLTAGE
	&gpc_virt_weights_dev_ops, // GPOWERCAP_NODE_VIRTUAL_WEIGHTS
};



struct gpowercap_node {
	u32 type;
	const char *name;
	struct gpowercap_node *parent;
	enum hw_dev_type cdev_id;
};

static inline struct powercap_zone *to_powercap_zone(struct device *dev)
{
	return container_of(dev, struct powercap_zone, dev);
}

static inline struct gpowercap *to_gpowercap(struct powercap_zone *zone)
{
	return container_of(zone, struct gpowercap, zone);
}
void gpowercap_init(struct gpowercap *gpowercap, struct gpowercap_ops *ops);
int gpowercap_update_power(struct gpowercap *gpowercap);
int gpowercap_release_zone(struct powercap_zone *pcz);
void gpowercap_unregister(struct gpowercap *gpowercap);
int gpowercap_register(const char *name, struct gpowercap *gpowercap, struct gpowercap *parent);
int gpowercap_create_hierarchy(struct of_device_id *gpowercap_match_table);
void gpowercap_destroy_hierarchy(void);

bool gpowercap_report_power_uw(struct gpowercap *gpowercap, u64 power_uw);
#define gpowercap_for_each_children(gpc, child_ptr) \
		list_for_each_entry((child_ptr), &(gpc)->children, siblings)

int gpc_google_thermal_odpm_register_client(const char *rail_name,
	struct notifier_block *nb,
	unsigned int polling_interval_ms);
int gpc_google_thermal_odpm_unregister_client(const char *rail_name,
	struct notifier_block *nb,
	unsigned int polling_interval_ms);
#endif //__GOOGLE_POWERCAP_H__
