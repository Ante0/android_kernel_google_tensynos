/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * powercap_weights_algo.h power cap weights algo functions.
 *
 * Copyright (c) 2026, Google LLC. All rights reserved.
 */
#ifndef _POWERCAP_WEIGHTS_ALGO_H_
#define _POWERCAP_WEIGHTS_ALGO_H_

#include <linux/mutex.h>

#include "google_powercap.h"

#define GPC_WEIGHTS_ALGO_DEFAULT_WEIGHT 1
#define GPC_WEIGHTS_ALGO_NUM_OPPS 2
#define GPC_WEIGHTS_ALGO_RECEIVER_THRESHOLD_PERCENT 85

struct gpowercap_weight_child {
	struct list_head node;
	struct gpowercap *gpc;
	char *name;
	u32 weight;
	u64 limit_uw;
	u64 current_power_uw;
	bool is_receiver;
};

struct gpowercap_weights_algo {
	struct gpowercap gpowercap;
	struct mutex lock;
	struct list_head weighted_children;
	u32 total_weight;
	u32 redistribution_threshold;
};

static inline struct gpowercap_weights_algo *to_gpowercap_weights_algo(struct gpowercap *gpowercap)
{
	return container_of(gpowercap, struct gpowercap_weights_algo, gpowercap);
}

#if IS_ENABLED(CONFIG_KUNIT)
u64 __gpc_weights_algo_set_power_limit(struct gpowercap *gpowercap, u64 power_limit);
u64 __gpc_weights_algo_get_power(struct gpowercap *gpowercap);
int __gpc_weights_algo_update_power_uw(struct gpowercap *gpowercap);
int __gpc_weights_algo_evaluate(struct gpowercap *gpowercap);
void __gpc_weights_algo_release(struct gpowercap *gpowercap);
struct gpowercap *__gpc_weights_algo_setup(struct device_node *dn, struct gpowercap *parent);
ssize_t power_distribution_weights_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count);
ssize_t power_distribution_weights_show(struct device *dev, struct device_attribute *attr,
						char *buf);
ssize_t power_redistribution_threshold_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count);
ssize_t power_redistribution_threshold_show(struct device *dev, struct device_attribute *attr,
						char *buf);
int gpc_weights_of_property_count_strings(struct device_node *np, const char *propname);
int gpc_weights_of_property_read_string_index(struct device_node *np, const char *propname,
						int index, const char **out_string);
int gpc_weights_kstrtou32(const char *s, unsigned int base, u32 *res);
int __gpc_weights_algo_rebalance(struct gpowercap *gpowercap);
#endif /* IS_ENABLED(CONFIG_KUNIT) */

#endif  // _POWERCAP_WEIGHTS_ALGO_H_
