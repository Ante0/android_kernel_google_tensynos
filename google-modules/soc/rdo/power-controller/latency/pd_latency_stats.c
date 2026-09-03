// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2025 Google LLC */

#include "pd_latency_stats.h"
#include "linux/limits.h"
#include "linux/random.h"
#include <linux/sort.h>

/* Conscious decision to not null-guard any of these */

/* Returns the replaced element, if no replacement happened then U32_MAX */
static u32 add_reservouir_sample(struct latency_stats *stats, u32 latency)
{
	u8 rng_idx;
	u32 replaced_value;

	if (stats->n_samples < PD_LATENCY_SAMPLE_SIZE) {
		stats->reservoir_samples[stats->n_samples++] = latency;
		return U32_MAX;
	}

	rng_idx = get_random_u32_below(stats->count);
	if (rng_idx < PD_LATENCY_SAMPLE_SIZE) {
		replaced_value = stats->reservoir_samples[rng_idx];
		stats->reservoir_samples[rng_idx] = latency;
		return replaced_value;
	}

	return U32_MAX;
}

void latency_reset(struct latency_stats *stats)
{
	stats->latency_total_sum = 0;
	stats->max_latency = 0;
	stats->min_latency = 0;
	stats->count = 0;
	stats->n_samples = 0;
}

void latency_store_data_point(struct latency_stats *stats, u32 latency)
{
	stats->latency_total_sum += latency;

	stats->count++;

	add_reservouir_sample(stats, latency);
}

void latency_group_set_max(struct latency_stats_group *group, const u32 *latencies)
{
	if (group->stats[group->main_stat_idx]->max_latency > latencies[group->main_stat_idx])
		return;

	for (int i = 0; i < group->group_size; ++i)
		group->stats[i]->max_latency = latencies[i];
}

void latency_group_set_min(struct latency_stats_group *group, const u32 *latencies)
{
	if (group->stats[group->main_stat_idx]->min_latency != 0 &&
	    group->stats[group->main_stat_idx]->min_latency < latencies[group->main_stat_idx])
		return;

	for (int i = 0; i < group->group_size; ++i)
		group->stats[i]->min_latency = latencies[i];
}

u32 latency_get_min(const struct latency_stats *stats)
{
	return stats->min_latency;
}

u32 latency_get_max(const struct latency_stats *stats)
{
	return stats->max_latency;
}

u32 latency_get_average(const struct latency_stats *stats)
{
	if (stats->count == 0)
		return 0;

	return stats->latency_total_sum / stats->count;
}

u32 latency_get_count(const struct latency_stats *stats)
{
	return stats->count;
}

static int cmp_u32(const void *lhs, const void *rhs)
{
	u32 lhs_cast = *(const u32 *)lhs;
	u32 rhs_cast = *(const u32 *)rhs;

	if (lhs_cast < rhs_cast)
		return -1;
	if (lhs_cast > rhs_cast)
		return 1;
	return 0;
}

u32 latency_get_median(struct latency_stats *stats)
{
	if (stats->n_samples == 0)
		return 0;

	/*
	 * TODO: This can be improved with quickselect.
	 * 2 heaps solution would create some overhead in the sampler which needs to be lightweight
	 * Getting the median is used only for debugging, so it can be slower
	 */
	sort(stats->reservoir_samples, stats->n_samples, sizeof(u32), &cmp_u32, NULL);
	if (stats->n_samples % 2 == 0)
		return (stats->reservoir_samples[stats->n_samples / 2 - 1] +
			stats->reservoir_samples[stats->n_samples / 2]) / 2;
	return stats->reservoir_samples[stats->n_samples / 2];
}
