/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * powercap_voltage_match_algo.h power cap voltage algo functions.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */
#ifndef _POWERCAP_VOLTAGE_ALGO_H_
#define _POWERCAP_VOLTAGE_ALGO_H_

#include <linux/mutex.h>

#include "google_powercap.h"

struct gpowercap_volt_algo {
	struct gpowercap gpowercap;
	struct mutex lock;
	unsigned int num_opps;
	struct cdev_opp_table *opp_table;
};

static inline struct gpowercap_volt_algo *to_gpowercap_volt_algo(struct gpowercap *gpowercap)
{
	return container_of(gpowercap, struct gpowercap_volt_algo, gpowercap);
}

u64 __gpc_volt_algo_set_power_limit(struct gpowercap *gpowercap, u64 power_limit);
u64 __gpc_volt_algo_get_power(struct gpowercap *gpowercap);
int __gpc_volt_algo_update_power_uw(struct gpowercap *gpowercap);
int __gpc_volt_algo_evaluate(struct gpowercap *gpowercap);
void __gpc_volt_algo_release(struct gpowercap *gpowercap);
struct gpowercap *__gpc_volt_algo_setup(struct device_node *dn, struct gpowercap *parent);

extern const struct of_device_id powercap_volt_algo_platform_table[];
extern void gpc_of_node_put(struct device_node *np);
extern struct device_node *gpc_of_find_node_by_path(const char *path);
extern const struct of_device_id *gpc_match_of_node(const struct of_device_id *matches,
					const struct device_node *node);
#endif  // _POWERCAP_VOLTAGE_ALGO_H_
