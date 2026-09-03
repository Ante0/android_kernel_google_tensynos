// SPDX-License-Identifier: GPL-2.0-only
/*
 * google_powercap_stats.c driver providing stats for powercap nodes.
 *
 * Copyright (c) 2026, Google LLC. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/static_stub.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/time.h>

#include "google_powercap.h"
#include "google_powercap_stats.h"

static const char *const gpc_stat_names[GPC_STAT_MAX] = {
	[GPC_STAT_POWER_LIMIT] = "power_limit",
	[GPC_STAT_UTIL] = "utility",
	[GPC_STAT_QOS] = "qos_request",
};

static const char *const gpc_stat_help[GPC_STAT_MAX] = {
	[GPC_STAT_POWER_LIMIT] =
		"Residency(ms) where bucket_val <= power_limit < next_bucket_val (uW)",
	[GPC_STAT_UTIL] = "Residency(ms) where bucket_val <= utility < next_bucket_val (%)",
	[GPC_STAT_QOS] = "Residency(ms) where qos_request == bucket_val (kHz)",
};

int gpc_stats_init(struct gpowercap *gpc)
{
	struct gpowercap_stats *stats = gpc->stats;
	bool newly_allocated = false;
	struct gpc_stat_bucket *buckets;
	int j;

	if (!stats) {
		stats = kzalloc(sizeof(*stats), GFP_KERNEL);
		if (!stats)
			return -ENOMEM;

		spin_lock_init(&stats->lock);
		gpc->stats = stats;
		newly_allocated = true;
	}

	/* Initialize stats based on opp_table if available */
	if (gpc->opp_table && gpc->num_opps > 0) {
		/*
		 * Heuristic: If frequency is 0, it's a virtual node (e.g. Weights Algo).
		 * Virtual nodes don't have QoS or Util, and their "OPP table" is just
		 * min/max power. We want more buckets for them.
		 */
		bool is_virtual = (gpc->opp_table[0].freq == 0);
		int num_opps;

		if (is_virtual)
			num_opps = GPC_STATS_MAX_BUCKETS;
		else
			num_opps = min_t(int, gpc->num_opps, GPC_STATS_MAX_BUCKETS);

		/* Power Limit Stats */
		if (!stats->stats[GPC_STAT_POWER_LIMIT].enabled) {
			buckets = kcalloc(num_opps, sizeof(struct gpc_stat_bucket), GFP_KERNEL);
			if (!buckets)
				goto err_alloc;

			if (is_virtual) {
				/* Interpolate buckets between min and max */
				u64 min_p = gpc->opp_table[0].power;
				u64 max_p = gpc->opp_table[gpc->num_opps - 1].power;
				u64 step = (num_opps > 1) ?
						div_u64(max_p - min_p, num_opps - 1) : 0;

				for (j = 0; j < num_opps; j++)
					buckets[j].value = min_p + step * j;
				buckets[num_opps - 1].value = max_p;
			} else {
				for (j = 0; j < num_opps; j++)
					buckets[j].value = gpc->opp_table[j].power;
			}

			stats->stats[GPC_STAT_POWER_LIMIT].num_buckets = num_opps;
			stats->stats[GPC_STAT_POWER_LIMIT].buckets = buckets;
			stats->stats[GPC_STAT_POWER_LIMIT].current_value = gpc->power_limit;
			stats->stats[GPC_STAT_POWER_LIMIT].last_update_time = ktime_get();
			stats->stats[GPC_STAT_POWER_LIMIT].enabled = true;
		} else if (is_virtual) {
			/* Update virtual node buckets if they've changed */
			unsigned long flags;
			struct gpc_stat *stat = &stats->stats[GPC_STAT_POWER_LIMIT];
			u64 min_p = gpc->opp_table[0].power;
			u64 max_p = gpc->opp_table[gpc->num_opps - 1].power;
			u64 step = (num_opps > 1) ? div_u64(max_p - min_p, num_opps - 1) : 0;

			spin_lock_irqsave(&stats->lock, flags);
			if (stat->buckets && stat->num_buckets == num_opps) {
				/* Update buckets power values and reset times */
				for (j = 0; j < num_opps; j++) {
					stat->buckets[j].value = min_p + step * j;
					stat->buckets[j].time_ms = 0;
					stat->current_value = gpc->power_limit;
					stat->last_update_time = ktime_get();
				}
				stat->buckets[num_opps - 1].value = max_p;
			}
			spin_unlock_irqrestore(&stats->lock, flags);
		}

		/* QoS Stats - Skip for virtual nodes */
		/* Only if we can determine QoS metric (freq).
		 * Usually we update QoS with frequency.
		 */
		if (!is_virtual && !stats->stats[GPC_STAT_QOS].enabled) {
			buckets = kcalloc(num_opps, sizeof(struct gpc_stat_bucket), GFP_KERNEL);
			if (!buckets)
				goto err_alloc;

			for (j = 0; j < num_opps; j++)
				buckets[j].value = gpc->opp_table[j].freq;

			stats->stats[GPC_STAT_QOS].num_buckets = num_opps;
			stats->stats[GPC_STAT_QOS].buckets = buckets;
			stats->stats[GPC_STAT_QOS].current_value =
				gpc->opp_table[gpc->num_opps - 1].freq;
			stats->stats[GPC_STAT_QOS].last_update_time = ktime_get();
			stats->stats[GPC_STAT_QOS].enabled = true;
		}

		/* Utility Stats - Always enabled with fixed buckets 0-150. Only for leaf nodes */
		if (!is_virtual && !stats->stats[GPC_STAT_UTIL].enabled) {
			buckets = kcalloc(GPC_STATS_UTIL_BUCKETS, sizeof(struct gpc_stat_bucket),
					  GFP_KERNEL);
			if (!buckets)
				goto err_alloc;

			for (j = 0; j < GPC_STATS_UTIL_BUCKETS; j++)
				buckets[j].value = j * 10;

			stats->stats[GPC_STAT_UTIL].num_buckets = GPC_STATS_UTIL_BUCKETS;
			stats->stats[GPC_STAT_UTIL].buckets = buckets;
			stats->stats[GPC_STAT_UTIL].current_value = 0;
			stats->stats[GPC_STAT_UTIL].last_update_time = ktime_get();
			stats->stats[GPC_STAT_UTIL].enabled = true;
		}
	}

	return 0;

err_alloc:
	if (newly_allocated)
		gpc_stats_exit(gpc);
	return -ENOMEM;
}

void gpc_stats_exit(struct gpowercap *gpc)
{
	int i;

	if (!gpc->stats)
		return;

	for (i = 0; i < GPC_STAT_MAX; i++)
		kfree(gpc->stats->stats[i].buckets);

	kfree(gpc->stats);
	gpc->stats = NULL;
}

/* Find the nearest bucket index */
static int find_bucket_idx(struct gpc_stat *stat, u64 value)
{
	int l = 0, r = stat->num_buckets;
	int mid;

	/* Find first bucket > value (upper_bound) */
	while (l < r) {
		mid = l + (r - l) / 2;
		if (stat->buckets[mid].value > value)
			r = mid;
		else
			l = mid + 1;
	}

	/* We want the bucket <= value, so prev bucket */
	if (l > 0)
		return l - 1;
	return 0;
}

void gpc_stats_update(struct gpowercap *gpc, enum gpc_stat_type type, u64 value)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_stats_update, gpc, type, value);

	struct gpowercap_stats *stats = gpc->stats;
	struct gpc_stat *stat;
	ktime_t now;
	u64 delta_ms;
	int idx;
	unsigned long flags;

	if (!stats || type >= GPC_STAT_MAX)
		return;

	stat = &stats->stats[type];
	if (!stat->enabled)
		return;

	spin_lock_irqsave(&stats->lock, flags);

	now = ktime_get();
	delta_ms = ktime_to_ms(ktime_sub(now, stat->last_update_time));

	idx = find_bucket_idx(stat, stat->current_value);
	stat->buckets[idx].time_ms += delta_ms;

	stat->current_value = value;
	stat->last_update_time = now;

	spin_unlock_irqrestore(&stats->lock, flags);
}

ssize_t gpc_stats_avail_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gpowercap *gpc = to_gpowercap(to_powercap_zone(dev));
	struct gpowercap_stats *stats = gpc->stats;
	int i, ret = 0;

	if (!stats)
		return 0;

	for (i = 0; i < GPC_STAT_MAX; i++) {
		if (stats->stats[i].enabled)
			ret += sysfs_emit_at(buf, ret, "%s ", gpc_stat_names[i]);
	}
	if (ret)
		buf[ret - 1] = '\n'; /* Replace last space with newline */

	return ret;
}

ssize_t gpc_stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gpowercap *gpc = to_gpowercap(to_powercap_zone(dev));
	struct gpowercap_stats *stats = gpc->stats;
	struct gpc_stat *stat;
	int i, ret = 0;
	unsigned long flags;
	int stat_idx;

	if (!stats)
		return 0;

	spin_lock_irqsave(&stats->lock, flags);

	/* Find next enabled stat */
	for (i = 0; i < GPC_STAT_MAX; i++) {
		stat_idx = (stats->current_read_index + i) % GPC_STAT_MAX;
		if (stats->stats[stat_idx].enabled)
			break;
	}

	if (i == GPC_STAT_MAX) {
		spin_unlock_irqrestore(&stats->lock, flags);
		return 0; /* No enabled stats */
	}

	stat = &stats->stats[stat_idx];

	/* Update current residency before showing */
	{
		ktime_t now = ktime_get();
		u64 delta_ms = ktime_to_ms(ktime_sub(now, stat->last_update_time));
		int idx = find_bucket_idx(stat, stat->current_value);

		stat->buckets[idx].time_ms += delta_ms;
		stat->last_update_time = now;
	}

	ret += sysfs_emit_at(buf, ret, "%s:\n", gpc_stat_names[stat_idx]);
	ret += sysfs_emit_at(buf, ret, "# %s\n", gpc_stat_help[stat_idx]);
	for (i = 0; i < stat->num_buckets; i++) {
		ret += sysfs_emit_at(buf, ret, "%llu %llu\n",
			stat->buckets[i].value, stat->buckets[i].time_ms);
	}

	/* Reset stats after read */
	for (i = 0; i < stat->num_buckets; i++)
		stat->buckets[i].time_ms = 0;

	/* Advance index for next read */
	stats->current_read_index = (stat_idx + 1) % GPC_STAT_MAX;

	spin_unlock_irqrestore(&stats->lock, flags);

	return ret;
}
