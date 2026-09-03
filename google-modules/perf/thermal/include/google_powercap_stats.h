/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * google_powercap_stats.h Google powercap stats related functions.
 *
 * Copyright (c) 2026, Google LLC. All rights reserved.
 */

#ifndef _GOOGLE_POWERCAP_STATS_H_
#define _GOOGLE_POWERCAP_STATS_H_

#include <linux/ktime.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "google_powercap.h"

#define GPC_STATS_MAX_BUCKETS 32
#define GPC_STATS_UTIL_BUCKETS 16

enum gpc_stat_type {
	GPC_STAT_POWER_LIMIT,
	GPC_STAT_UTIL,
	GPC_STAT_QOS,
	GPC_STAT_MAX,
};

struct gpc_stat_bucket {
	u64 value; /* The key: power (uW), freq (kHz), or util (%) */
	u64 time_ms; /* Accumulated residency time */
};

struct gpc_stat {
	bool enabled;
	int num_buckets;
	struct gpc_stat_bucket *buckets;
	ktime_t last_update_time;
	u64 current_value;
};

struct gpowercap_stats {
	struct gpc_stat stats[GPC_STAT_MAX];
	spinlock_t lock;
	int current_read_index; /* For cycling through stats */
};

int gpc_stats_init(struct gpowercap *gpc);
void gpc_stats_exit(struct gpowercap *gpc);
void gpc_stats_update(struct gpowercap *gpc, enum gpc_stat_type type, u64 value);
ssize_t gpc_stats_show(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t gpc_stats_avail_show(struct device *dev, struct device_attribute *attr, char *buf);

#endif /* _GOOGLE_POWERCAP_STATS_H_ */
