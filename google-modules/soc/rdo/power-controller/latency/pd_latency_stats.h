/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2025 Google LLC */

#ifndef PD_LATENCY_STATS_H
#define PD_LATENCY_STATS_H

#include "linux/build_bug.h"
#include "linux/limits.h"
#include <linux/types.h>

/*
 * ~10% sampling error with 95% confidence level
 * In practice the error will be ~200us on average
 *  if the data is guassian in range [0, 30000us]
 */
#define PD_LATENCY_SAMPLE_SIZE 50
static_assert(PD_LATENCY_SAMPLE_SIZE <= U8_MAX);

struct latency_stats {
	u32 count;
	u64 latency_total_sum;
	u32 min_latency;
	u32 max_latency;

	/* Can be used to estimate any metric */
	u32 reservoir_samples[PD_LATENCY_SAMPLE_SIZE];
	u8 n_samples;
};

struct latency_stats_group {
	struct latency_stats **stats;
	int group_size;

	int main_stat_idx;
};

void latency_reset(struct latency_stats *stats);

void latency_store_data_point(struct latency_stats *stats, u32 latency);

void latency_group_set_max(struct latency_stats_group *group, const u32 *latencies);
void latency_group_set_min(struct latency_stats_group *group, const u32 *latencies);

u32 latency_get_min(const struct latency_stats *stats);
u32 latency_get_max(const struct latency_stats *stats);
u32 latency_get_average(const struct latency_stats *stats);
u32 latency_get_count(const struct latency_stats *stats);
u32 latency_get_median(struct latency_stats *stats);

#endif /* PD_LATENCY_STATS_H */
