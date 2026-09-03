/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024-2025 Google LLC */

#ifndef PD_LATENCY_PROFILE_H
#define PD_LATENCY_PROFILE_H

#include <linux/pm_domain.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/log2.h>

#include "pd_latency_stats.h"

#define LATENCY_TYPE_BIT(__type) (1u << (__type))

/* Debug-only usage */
#define LATENCY_BIT_TO_TYPE(__bit) (ilog2(__bit))

typedef u64 latency_type_bitmask_t;

enum latency_type {
	EXEC, /* start to cpm handler latency */
	FW, /* cpm FW latency */
	ACK, /* start to cpm scheduling latency */
	E2E, /* end2end latency */
	LATENCY_TYPE_COUNT
};

enum latency_type_bit {
	EXEC_BIT = LATENCY_TYPE_BIT(EXEC),
	FW_BIT = LATENCY_TYPE_BIT(FW),
	ACK_BIT = LATENCY_TYPE_BIT(ACK),
	E2E_BIT = LATENCY_TYPE_BIT(E2E),
	LATENCY_TYPE_BIT_MAX /* Careful here: it's not count */
};
static_assert(LATENCY_TYPE_COUNT <= 8 * sizeof(latency_type_bitmask_t) &&
	      LATENCY_BIT_TO_TYPE(LATENCY_TYPE_BIT_MAX - 1) + 1 == LATENCY_TYPE_COUNT);

/*
 * Latency type that outlier run gathering is based off
 * We fetch an outlier (max, min, ...) when this latency type is stopped or stored
 */
#define MAIN_LATENCY_TYPE (E2E)
#define MAIN_LATENCY_TYPE_BIT (LATENCY_TYPE_BIT(MAIN_LATENCY_TYPE))

struct latency_data {
	struct kobject *parent;
	struct latency_stats stats;
	u64 start_time;
};

struct latency_profile {
	bool collect_data; /* flag to enable/disable the data collection */
	struct latency_data *data;
	u32 *pm_resource_id;
	int pd_count;

	struct kobject *latencies_kobj;
	struct kobject **power_domain_kobj;
	struct kobject **data_kobj;

	/* for the gathering of outlier runs (max, min,...) */
	struct latency_stats_group **stats_groups;
	u32 **most_recent_latencies;
};

int pd_latency_profile_init(struct platform_device *pdev);
void pd_latency_profile_remove(struct platform_device *pdev);

int pd_latency_profile_start(struct generic_pm_domain *domain, int state,
			     latency_type_bitmask_t type_mask);

/* profile_stop takes start_time as an argument in order to compute delta */
int pd_latency_profile_stop(struct generic_pm_domain *domain, int state,
			    latency_type_bitmask_t type_mask, bool cancel);

/* pd_latency_profile_store() - Store the latency for the given domain in the
 *                              approprioate stat.
 *
 *  @domain: power domain where to store the latency
 *  @latency: the latecy value to store
 *  @lat_type: the latency type to store
 */
int pd_latency_profile_store(struct generic_pm_domain *domain, u32 latency,
			     int state, enum latency_type lat_type);

#endif /* PD_LATENCY_PROFILE_H */
