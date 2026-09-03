// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 *
 * MEMSS/GMC Latency Governor Main Module.
 */
#define pr_fmt(fmt) "gs_governor_memlat: " fmt

#include <linux/device.h>
#include <linux/platform_device.h>

#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <perf/core/google_pm_qos.h>
#include <perf/core/gs_governor_memlat.h>
#include <perf/core/gs_perf_mon.h>
#include <perf/core/perf_domain.h>
#include <trace/events/power.h>

#include "gs_governor_utils.h"
#include "gs_lat_governors_trace.h"

/**
 * struct frequency_vote - Contains configs and voting data.
 * @vote_name:	The name of the device we will vote frequency for.
 * @vote_devfreq:	The devfreq to make a vote on.
 */
struct frequency_vote {
	const char *vote_name;
	struct devfreq *vote_devfreq;
	struct dev_pm_qos_request vote_request;
	struct fast_devfreq_vote fdf_vote;
	bool fast_devfreq_vote_is_on;
	ktime_t last_vote_time;
};

/**
 * struct secondary_frequency_domain - Contains voting data for secondary domains.
 * @vote:	Frequency voting mechanism for the secondary device frequency.
 * @freq_map:	The mapping from primary freq vote to secondary vote.
 */
struct secondary_frequency_domain {
	struct frequency_vote target_freq_vote;
	struct gs_governor_core_dev_map *freq_map;
};

/**
 * struct memlat_data - Node containing memlat's global data.
 * @gov_is_on:			Governor's active state.
 * @gov_paused:			Governor paused during resume boosting period.
 * @attr_grp:			Tuneable governor parameters exposed to userspace.
 * @dev:			Reference to the governor's device.
 * @target_freq_vote:		Primary target domain.
 * @num_cpu_clusters:		Number of CPU clusters the governor will service.
 * @cpu_configs_arr:		Configurations for each cluster's latency vote.
 * @num_secondary_votes:	Number of secondary vote domains.
 * @secondary_vote_arr:		Array of secondary vote domains.
 * @enable_fast_update:	Enable fast update feature.
 * @resume_boost_freq_khz:	Minimum frequency during suspend and resume process.
 * @resume_boost_duration_ms:	Duration of extended boosting and the governor pausing.
 * @resume_boost_extend:	Worker to extend boosting and governor pausing.
 */
struct memlat_data {
	bool gov_is_on;
	bool gov_paused;
	struct attribute_group *attr_grp;
	struct device *dev;
	struct frequency_vote target_freq_vote;

	int num_cpu_clusters;
	struct cluster_config *cpu_configs_arr;

	int num_secondary_votes;
	struct secondary_frequency_domain *secondary_vote_arr;
	bool enable_fast_update;
	unsigned int ratelimit_ms;
	spinlock_t latgov_state_lock;
	struct gs_governor_latgov_state *latgov_state_arr;

	unsigned long resume_boost_freq_khz;
	unsigned int resume_boost_duration_ms;
	struct delayed_work resume_boost_extend;

	bool enable_latency_mode;
};

static void update_memlat_gov(struct gs_cpu_perf_data *data, void *private_data);
static void gs_governor_memlat_update_all_freq_votes(unsigned long primary_vote);

/* Global monitor client used to get callbacks when gs_perf_mon data is updated. */
static struct gs_perf_mon_client memlat_perf_client = {
	.client_callback = update_memlat_gov,
	.name = "memlat"
};

/* memlat datastructure holding memlat governor configurations and metadata. */
static struct memlat_data memlat_node;

/* Macro expansions for sysfs nodes.*/
MAKE_CLUSTER_ATTR(memlat_node, stall_floor);
MAKE_CLUSTER_ATTR(memlat_node, ratio_ceil);
MAKE_CLUSTER_ATTR(memlat_node, cpuidle_state_depth_threshold);
MAKE_CLUSTER_ATTR(memlat_node, stale_threshold_ms);

SHOW_CLUSTER_FREQ_MAP_ATTR(memlat_node, latency_freq_table);
SHOW_CLUSTER_FREQ_MAP_ATTR(memlat_node, base_freq_table);

static ssize_t enable_fast_update_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", memlat_node.enable_fast_update);
}

static ssize_t enable_fast_update_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret < 0)
		return ret;

	if (memlat_node.enable_fast_update != val) {
		/* Reset current votes to minimum before switching the update method. */
		gs_governor_memlat_update_all_freq_votes(0);
		memlat_node.enable_fast_update = val;
	}

	return count;
}
static DEVICE_ATTR_RW(enable_fast_update);

static ssize_t resume_boost_freq_khz_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	return sysfs_emit(buf, "%lu\n", memlat_node.resume_boost_freq_khz);
}

static ssize_t resume_boost_freq_khz_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned long freq;
	int ret;

	ret = kstrtoul(buf, 10, &freq);
	if (ret)
		return ret;

	memlat_node.resume_boost_freq_khz = freq;

	return count;
}

static DEVICE_ATTR_RW(resume_boost_freq_khz);

static ssize_t resume_boost_duration_ms_show(struct device *dev,
						struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", memlat_node.resume_boost_duration_ms);
}

static ssize_t resume_boost_duration_ms_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	memlat_node.resume_boost_duration_ms = val;

	return count;
}

static DEVICE_ATTR_RW(resume_boost_duration_ms);

static void gs_governor_memlat_resume_boost_extend_fn(struct work_struct *work)
{
	memlat_node.gov_paused = false;
}

static ssize_t ratelimit_ms_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", memlat_node.ratelimit_ms);
}

static ssize_t ratelimit_ms_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned int val;
	int ret;
	unsigned long flags;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;

	spin_lock_irqsave(&memlat_node.latgov_state_lock, flags);
	memlat_node.ratelimit_ms = val;
	spin_unlock_irqrestore(&memlat_node.latgov_state_lock, flags);
	return count;
}
static DEVICE_ATTR_RW(ratelimit_ms);

static ssize_t enable_latency_mode_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", memlat_node.enable_latency_mode);
}

static ssize_t enable_latency_mode_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret < 0)
		return ret;

	if (memlat_node.enable_latency_mode != val)
		memlat_node.enable_latency_mode = val;

	return count;
}
static DEVICE_ATTR_RW(enable_latency_mode);

/* These sysfs emitters also depend on the macro expansions from above. */
static struct attribute *memlat_dev_attr[] = {
	&dev_attr_memlat_node_stall_floor.attr,
	&dev_attr_memlat_node_ratio_ceil.attr,
	&dev_attr_memlat_node_cpuidle_state_depth_threshold.attr,
	&dev_attr_memlat_node_latency_freq_table.attr,
	&dev_attr_memlat_node_base_freq_table.attr,
	&dev_attr_enable_fast_update.attr,
	&dev_attr_resume_boost_freq_khz.attr,
	&dev_attr_resume_boost_duration_ms.attr,
	&dev_attr_ratelimit_ms.attr,
	&dev_attr_enable_latency_mode.attr,
	&dev_attr_memlat_node_stale_threshold_ms.attr,
	NULL,
};

/* Sysfs files to expose. */
static struct attribute_group memlat_dev_attr_group = {
	.name = "memlat_attr",
	.attrs = memlat_dev_attr,
};

static bool __gs_governor_memlat_should_vote(struct frequency_vote *vote)
{
	ktime_t now = ktime_get();
	bool allow_vote = true;

	if (memlat_node.ratelimit_ms > 0) {
		if (ktime_ms_delta(now, vote->last_vote_time) < memlat_node.ratelimit_ms)
			allow_vote = false;
	}

	if (allow_vote)
		vote->last_vote_time = now;

	return allow_vote;
}

static bool gs_governor_memlat_should_vote(struct frequency_vote *vote)
{
	bool allow_vote;
	unsigned long flags;

	spin_lock_irqsave(&memlat_node.latgov_state_lock, flags);
	allow_vote = __gs_governor_memlat_should_vote(vote);
	spin_unlock_irqrestore(&memlat_node.latgov_state_lock, flags);

	return allow_vote;
}

/**
 * gs_governor_memlat_update_target_freq_vote - Registers the vote for the memlat governor.
 *
 * Inputs:
 * @vote:		The node to vote on.
 * @target_freq:	New target frequency.
 *
 * Outputs:
 *			Non-zero on error.
 */
static int gs_governor_memlat_update_target_freq_vote(struct frequency_vote *vote,
						      unsigned long target_freq)
{
	int ret = 0;

	if (memlat_node.enable_fast_update && vote->fast_devfreq_vote_is_on)
		ret = update_fast_devfreq_vote(&vote->fdf_vote, target_freq);
	else
		ret = dev_pm_qos_update_request(&vote->vote_request, target_freq);

	trace_clock_set_rate(vote->vote_name, target_freq, raw_smp_processor_id());
	return ret;
}

/**
 * gs_governor_memlat_update_all_freq_votes - Updates the vote for primary
 * 						and secondary vote comonents.
 *
 * Inputs:
 * @primary_vote:	Primary frequency vote component.
 *
 * Outputs:
 *			Non-zero on error.
 */
static void gs_governor_memlat_update_all_freq_votes(unsigned long primary_vote)
{
	unsigned long secondary_vote;
	struct secondary_frequency_domain *sub_vote;
	int secondary_idx;

	gs_governor_memlat_update_target_freq_vote(&memlat_node.target_freq_vote, primary_vote);

	for (secondary_idx = 0; secondary_idx < memlat_node.num_secondary_votes;
	     secondary_idx++) {
		sub_vote = &memlat_node.secondary_vote_arr[secondary_idx];
		secondary_vote =
			gs_governor_core_to_dev_freq(sub_vote->freq_map, primary_vote);
		gs_governor_memlat_update_target_freq_vote(&sub_vote->target_freq_vote,
							   secondary_vote);
	}
}

/**
 * gs_governor_memlat_compute_freq - Calculates memlat freq votes desired by each CPU cluster.
 *
 * This function determines the memlat target frequency.
 *
 * Input:
 * @cpu_perf_data_arr:	CPU data to use as input.
 *
 * Returns:
 * @max_freq:		The computed target frequency.
 */
static unsigned long gs_governor_memlat_compute_freq(struct gs_cpu_perf_data *cpu_perf_data_arr)
{
	int cpu;
	int cluster_idx;
	struct cluster_config *cluster;
	unsigned long max_freq = 0;
	unsigned long flags;
	ktime_t now;
	char trace_name[] = { 'c', 'p', 'u', '0', 'g', 'm', 'c', '\0' };

	now = ktime_get();

	/* For each cluster, we make a frequency decision. */
	for (cluster_idx = 0; cluster_idx < memlat_node.num_cpu_clusters; cluster_idx++) {
		cluster = &memlat_node.cpu_configs_arr[cluster_idx];
		for_each_cpu(cpu, &cluster->cpus) {
			struct gs_cpu_perf_data *cpu_data = &cpu_perf_data_arr[cpu];
			unsigned long ratio, mem_stall_pct, mem_stall_floor, ratio_ceil;
			bool memlat_cpuidle_state_aware;
			enum gs_perf_cpu_idle_state configured_idle_depth_threshold;
			unsigned long l3_cachemiss, mem_stall, cyc, last_delta_us, inst;
			unsigned long gmc_freq = 0, effective_cpu_freq_khz = 0;
			trace_name[3] = '0' + cpu;

			/* Check if the cpu monitor is up. */
			if (!cpu_data->cpu_mon_on)
				goto early_exit;

			l3_cachemiss = cpu_data->perf_ev_last_delta[PERF_L3_CACHE_MISS_IDX];
			mem_stall = cpu_data->perf_ev_last_delta[PERF_STALL_BACKEND_MEM_IDX];
			cyc = cpu_data->perf_ev_last_delta[PERF_CYCLE_IDX];
			inst = cpu_data->perf_ev_last_delta[PERF_INST_IDX];
			last_delta_us = cpu_data->time_delta_us;

			ratio_ceil = cluster->ratio_ceil;
			mem_stall_floor = cluster->stall_floor;
			memlat_cpuidle_state_aware = cluster->cpuidle_state_aware;
			configured_idle_depth_threshold = cluster->cpuidle_state_depth_threshold;

			/* Compute threshold data. */
			if (l3_cachemiss != 0)
				ratio = inst / l3_cachemiss;
			else
				ratio = inst;

			mem_stall_pct = mult_frac(10000, mem_stall, cyc);
			effective_cpu_freq_khz = MHZ_TO_KHZ * cyc / last_delta_us;

			spin_lock_irqsave(&memlat_node.latgov_state_lock, flags);
			if (memlat_cpuidle_state_aware &&
			    cpu_data->cpu_idle_state >= configured_idle_depth_threshold) {
				memlat_node.latgov_state_arr[cpu].state = LATGOV_IDLE;
				memlat_node.latgov_state_arr[cpu].freq_khz = 0;
				spin_unlock_irqrestore(&memlat_node.latgov_state_lock, flags);
				goto early_exit_trace; // Zeroing vote for sufficiently idle CPUs.
			}

			if (ktime_ms_delta(now, cpu_data->last_update_ts) > (s64)cluster->stale_threshold_ms) {
				memlat_node.latgov_state_arr[cpu].state = LATGOV_IDLE;
				memlat_node.latgov_state_arr[cpu].freq_khz = 0;
				spin_unlock_irqrestore(&memlat_node.latgov_state_lock, flags);
				goto early_exit_trace; // Zeroing vote for stale CPU data.
			}

			/* If the stall ratio threshold is exceeded, use the latency table. */
			if (memlat_node.enable_latency_mode ||
			    (ratio <= ratio_ceil && mem_stall_pct >= mem_stall_floor)) {
				gmc_freq = gs_governor_core_to_dev_freq(cluster->latency_freq_table,
									effective_cpu_freq_khz);
				memlat_node.latgov_state_arr[cpu].state = LATGOV_LATENCY;
			} else if (cluster->base_freq_table) {
				gmc_freq = gs_governor_core_to_dev_freq(cluster->base_freq_table,
									effective_cpu_freq_khz);
				memlat_node.latgov_state_arr[cpu].state = LATGOV_BASE;
			} else {
				memlat_node.latgov_state_arr[cpu].state = LATGOV_IDLE;
			}
			memlat_node.latgov_state_arr[cpu].freq_khz = gmc_freq;
			spin_unlock_irqrestore(&memlat_node.latgov_state_lock, flags);

			/* Keep a running max of the MIF frequency. */
			if (gmc_freq > max_freq)
				max_freq = gmc_freq;

		early_exit_trace:
			trace_gs_lat_governor("gmclat", cpu, ratio, mem_stall_pct,
					      gmc_freq, effective_cpu_freq_khz);

		early_exit:
			/* Leave a trace for the cluster desired MIF frequency. */
			trace_clock_set_rate(trace_name, gmc_freq, cpu);
		}
	}

	return max_freq;
}

/**
 * update_memlat_gov	-	Callback function from the perf monitor to service
 * 			the memlat governor.
 *
 * Input:
 * @data:		Performance data from the monitor.
 * @private_data:	Unused.
 *
 */
static void update_memlat_gov(struct gs_cpu_perf_data *data, void *private_data)
{
	unsigned long next_frequency;

	/* Governor is not active or paused, leave the vote unchanged. */
	if (!memlat_node.gov_is_on || memlat_node.gov_paused || !data) {
		dev_dbg(memlat_node.dev,
			"memlat governor is not active or paused. Leaving vote unchanged.\n");

		return;
	}

	/* Step 1: compute the frequency. */
	next_frequency = gs_governor_memlat_compute_freq(data);

	/* Step 2: process the frequency vote. */
	if (!gs_governor_memlat_should_vote(&memlat_node.target_freq_vote))
		return;

	gs_governor_memlat_update_all_freq_votes(next_frequency);
}

void gs_governor_memlat_cpufreq_update(struct cpufreq_policy *policy, unsigned int target_freq)
{
	int cpu;
	int cluster_idx;
	struct cluster_config *cluster;
	unsigned long max_freq = 0;
	unsigned long flags;

	if (!memlat_node.gov_is_on || memlat_node.gov_paused)
		return;

	if (!memlat_node.enable_fast_update ||
	    !memlat_node.target_freq_vote.fast_devfreq_vote_is_on)
		return;

	spin_lock_irqsave(&memlat_node.latgov_state_lock, flags);
	for (cluster_idx = 0; cluster_idx < memlat_node.num_cpu_clusters; cluster_idx++) {
		cluster = &memlat_node.cpu_configs_arr[cluster_idx];
		for_each_cpu(cpu, &cluster->cpus) {
			unsigned long gmc_freq = 0;

			if (cpumask_intersects(policy->cpus, cpumask_of(cpu))) {
				switch (memlat_node.latgov_state_arr[cpu].state) {
				case LATGOV_IDLE:
					break;
				case LATGOV_LATENCY:
					gmc_freq = gs_governor_core_to_dev_freq(
						cluster->latency_freq_table, target_freq);
					break;
				case LATGOV_BASE:
					gmc_freq = gs_governor_core_to_dev_freq(
						cluster->base_freq_table, target_freq);
					break;
				}
				memlat_node.latgov_state_arr[cpu].freq_khz = gmc_freq;
			} else {
				gmc_freq = memlat_node.latgov_state_arr[cpu].freq_khz;
			}

			if (gmc_freq > max_freq)
				max_freq = gmc_freq;
		}
	}

	if (!__gs_governor_memlat_should_vote(&memlat_node.target_freq_vote)) {
		spin_unlock_irqrestore(&memlat_node.latgov_state_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&memlat_node.latgov_state_lock, flags);

	trace_clock_set_rate("gmc_cpufreq_update",	max_freq, raw_smp_processor_id());
	update_fast_devfreq_vote(&memlat_node.target_freq_vote.fdf_vote, max_freq);
}
EXPORT_SYMBOL_GPL(gs_governor_memlat_cpufreq_update);
/**
 * gs_memlat_governor_remove_all_votes - Initializes all the votes for memlat governor.
 */
static void gs_memlat_governor_remove_all_votes(void)
{
	struct frequency_vote *vote = &memlat_node.target_freq_vote;
	int secondary_idx;

	if (vote->fast_devfreq_vote_is_on) {
		remove_fast_devfreq_vote(&vote->fdf_vote);
		vote->fast_devfreq_vote_is_on = false;
	}

	/* Remove primary votes. */
	if (vote->vote_devfreq)
		google_pm_qos_remove_devfreq_request(vote->vote_devfreq, &vote->vote_request);

	/* Remove secondary votes. */
	for (secondary_idx = 0; secondary_idx < memlat_node.num_secondary_votes;
	     secondary_idx++) {
		vote = &memlat_node.secondary_vote_arr[secondary_idx].target_freq_vote;

		if (vote->fast_devfreq_vote_is_on) {
			remove_fast_devfreq_vote(&vote->fdf_vote);
			vote->fast_devfreq_vote_is_on = false;
		}

		if (vote->vote_devfreq)
			google_pm_qos_remove_devfreq_request(vote->vote_devfreq,
							     &vote->vote_request);
	}
}

/**
 * gov_start - Starts the governor.
 */
static int gov_start(void)
{
	int ret;
	if (memlat_node.gov_is_on)
		return 0;

	/* Add clients. */
	ret = gs_perf_mon_add_client(&memlat_perf_client);
	if (ret)
		return ret;

	memlat_node.gov_is_on = true;

	return 0;
}

/**
 * gov_stop - Stops the governor.
 */
static void gov_stop(void)
{
	if (!memlat_node.gov_is_on)
		return;

	memlat_node.gov_is_on = false;

	/* Remove the client. */
	gs_perf_mon_remove_client(&memlat_perf_client);

	/* Reset all the votes to minimum. */
	gs_governor_memlat_update_all_freq_votes(0);
}

/**
 * gs_memlat_governor_vote_parse - Initializes the votes for the memlat.
 *
 * Input:
 * @vote_node:	Node containing the vote config.
 * @dev:	The device the governor is bound to.
 *
 * Output:	Non-zero on error.
 */
static int gs_memlat_governor_vote_parse(struct device_node *vote_node,
					 struct frequency_vote *vote, struct device *dev)
{
	struct device_node *vote_df_node;
	struct devfreq *df;
	int ret;

	if (of_property_read_string(vote_node, "vote_name",
				    &vote->vote_name)) {
		dev_err(dev, "vote_name undefined\n");
		return -ENODEV;
	}

	vote_df_node = of_parse_phandle(vote_node, "devfreq", 0);
	if (!vote_df_node) {
		dev_err(dev, "devfreq undefined\n");
		return -ENODEV;
	}

	df = devfreq_get_devfreq_by_node(vote_df_node);
	of_node_put(vote_df_node);
	if (IS_ERR_OR_NULL(df)) {
		dev_err(dev, "unable to retrieve devfreq for %s\n", vote->vote_name);
		return -EPROBE_DEFER;
	}

	ret = google_pm_qos_add_devfreq_request(df, &vote->vote_request,
						DEV_PM_QOS_MIN_FREQUENCY,
						PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
	if (ret)
		return ret;

	vote->vote_devfreq = df;

#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
	/* TODO: Fetch the "gmc" string from the device tree instead of hardcoding. */
	struct devfreq *fast_df = gs_perf_domain_find_devfreq("gmc", GS_RECOMMENDED_DEVFREQ);

	if (IS_ERR_OR_NULL(fast_df)) {
		dev_warn(dev, "Failed to find fast devfreq node for %s\n", vote->vote_name);
		vote->fast_devfreq_vote_is_on = false;
	} else {
		ret = add_fast_devfreq_vote(fast_df, vote->vote_name, &vote->fdf_vote);
		if (ret) {
			dev_warn(dev, "Failed to register fast devfreq vote path for %s: %d\n",
				 vote->vote_name, ret);
			vote->fast_devfreq_vote_is_on = false;
		} else {
			vote->fast_devfreq_vote_is_on = true;
		}
	}
#else
	vote->fast_devfreq_vote_is_on = false;
#endif

	return 0;
}

/**
 * gs_memlat_initialize_secondary_votes - Initializes the secondary governors from a DT Node.
 *
 * Inputs:
 * @secondary_votes_node:	The tree node containing the list of secondary governor data.
 * @dev:			The device the governor is bound to.
 *
 * Returns:			Non-zero on error.
 */
static int gs_memlat_initialize_secondary_votes(struct device_node *secondary_votes_node,
						struct device *dev)
{
	struct device_node *sub_votes_node = NULL;
	struct secondary_frequency_domain *sub_vote;
	int sub_vote_idx;
	int ret = 0;

	memlat_node.num_secondary_votes = of_get_child_count(secondary_votes_node);

	/* Allocate a container for secondary domains. */
	memlat_node.secondary_vote_arr = devm_kzalloc(
		dev, sizeof(struct secondary_frequency_domain) * memlat_node.num_secondary_votes,
		GFP_KERNEL);
	if (!memlat_node.secondary_vote_arr) {
		dev_err(dev, "No memory for secondary_vote_arr.\n");
		return -ENOMEM;
	}

	/* Populate the Components. */
	sub_vote_idx = 0;
	sub_votes_node = of_get_next_child(secondary_votes_node, sub_votes_node);
	while (sub_votes_node) {
		sub_vote = &memlat_node.secondary_vote_arr[sub_vote_idx];

		/* Initialize the vote structure. */
		ret = gs_memlat_governor_vote_parse(sub_votes_node,
						    &sub_vote->target_freq_vote, dev);
		if (ret)
			return ret;

		/* Initialize the secondary translation map. */
		sub_vote->freq_map = gs_governor_init_core_dev_map(dev, sub_votes_node,
								   "core-dev-table");
		if (!sub_vote->freq_map) {
			dev_err(dev, "Can't parse freq-table for sub-domain.");
			return -ENODEV;
		}
		/*
		 * Note: It seems we don't have any secondary vote in GMC on LGA
		 * and MBU; all of them vote secondary via IRM.
		 */

		/* TODO: implement fast devfreq for secondary vote, e.g., read from mbfs or dtsi */

		sub_vote->target_freq_vote.fast_devfreq_vote_is_on = false;

		/* Increment pointer. */
		sub_vote_idx += 1;
		sub_votes_node = of_get_next_child(secondary_votes_node, sub_votes_node);
	}
	return 0;
}

/**
 * gs_memlat_governor_initialize - Initializes the memlat governor from a DT Node.
 *
 * Inputs:
 * @governor_node:	The tree node containing governor data.
 * @data:		The devfreq data to update frequencies.
 *
 * Returns:		Non-zero on error.
 */
static int gs_memlat_governor_initialize(struct device_node *governor_node, struct device *dev)
{
	int ret = 0;
	struct device_node *cluster_node = NULL;
	struct cluster_config *cluster;
	int cluster_idx;

	memlat_node.num_cpu_clusters = of_get_child_count(governor_node);

	/* Allocate a container for clusters. */
	memlat_node.cpu_configs_arr = devm_kzalloc(
		dev, sizeof(struct cluster_config) * memlat_node.num_cpu_clusters, GFP_KERNEL);
	if (!memlat_node.cpu_configs_arr) {
		dev_err(dev, "No memory for cluster_configs.\n");
		return -ENOMEM;
	}

	/* Populate the Components. */
	cluster_idx = 0;
	cluster_node = of_get_next_child(governor_node, cluster_node);
	while (cluster_node) {
		cluster = &memlat_node.cpu_configs_arr[cluster_idx];
		ret = populate_cluster_config(dev, cluster_node, cluster);
		if (ret)
			return ret;

		/* Increment pointer. */
		cluster_idx += 1;
		cluster_node = of_get_next_child(governor_node, cluster_node);
	}

	/* Allocate a container for cpu_latgov_state. */
	memlat_node.latgov_state_arr =
		devm_kzalloc(dev, sizeof(struct gs_governor_latgov_state) * num_possible_cpus(),
			GFP_KERNEL);
	if (!memlat_node.latgov_state_arr) {
		dev_err(dev, "No memory for latgov_state_arr.\n");
		return -ENOMEM;
	}
	spin_lock_init(&memlat_node.latgov_state_lock);

	return 0;
}

/**
 * gs_memlat_resume_boost_config - Set up resume boost configuration
 *
 * Inputs:
 * @resume_boost_node:		The tree node containing the boost configuration.
 * @dev:			The device the governor is bound to.
 */
static void gs_memlat_resume_boost_config(struct device_node *resume_boost_node,
						struct device *dev)
{
	u32 freq = 0;
	u32 duration_ms = 0;

	if (of_property_read_u32(resume_boost_node, "freq-khz", &freq)) {
		dev_dbg(dev, "freq-khz not found in resume_boost node, using default 0\n");
	}
	memlat_node.resume_boost_freq_khz = freq;

	if (of_property_read_u32(resume_boost_node, "duration-ms", &duration_ms)) {
		dev_dbg(dev, "duration-ms not found in resume_boost node, using default 0\n");
	}
	memlat_node.resume_boost_duration_ms = duration_ms;

}

static int gs_governor_memlat_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *governor_config_node, *frequency_vote_node,
		*secondary_votes_node, *resume_boost_node;
	int ret;

	memlat_node.dev = &pdev->dev;
	memlat_node.attr_grp = &memlat_dev_attr_group;
	memlat_node.enable_fast_update = false;

	INIT_DELAYED_WORK(&memlat_node.resume_boost_extend,
		gs_governor_memlat_resume_boost_extend_fn);

	/* Find and intitialize frequency votes. */
	frequency_vote_node = of_get_child_by_name(dev->of_node, "primary_vote_config");
	if (!frequency_vote_node) {
		dev_err(dev, "memlat frequency_votes not defined.\n");
		return -ENODEV;
	}

	ret = gs_memlat_governor_vote_parse(frequency_vote_node, &memlat_node.target_freq_vote,
					    dev);
	if (ret) {
		dev_err(dev, "Failed to parse memlat primary vote node data.\n");
		return ret;
	}

	/* Find and initialize secondary votes. */
	secondary_votes_node = of_get_child_by_name(dev->of_node, "secondary_frequency_votes");
	if (secondary_votes_node) {
		ret = gs_memlat_initialize_secondary_votes(secondary_votes_node, dev);
		if (ret) {
			dev_err(dev, "Failed to parse secondary vote data.\n");
			goto err_out;
		}
	} else {
		dev_dbg(dev, "memlat secondary vote node not defined. Skipping\n");
	}

	/* Find and initialize governor. */
	governor_config_node = of_get_child_by_name(dev->of_node, "governor_config");
	if (!governor_config_node) {
		dev_err(dev, "memlat governor node not defined.\n");
		ret = -ENODEV;
		goto err_out;
	}

	ret = gs_memlat_governor_initialize(governor_config_node, dev);
	if (ret) {
		dev_err(dev, "Failed to parse private governor data.\n");
		goto err_out;
	}

	/* Resume Boost Configuration. Optional */
	resume_boost_node = of_get_child_by_name(dev->of_node, "resume-boost");
	if (!resume_boost_node) {
		dev_dbg(dev, "resume-boost node not defined. Skipping setup\n");
	} else {
		gs_memlat_resume_boost_config(resume_boost_node, dev);
		of_node_put(resume_boost_node);
	}

	/* Add sysfs nodes here. */
	ret = sysfs_create_group(&dev->kobj, memlat_node.attr_grp);
	if (ret) {
		dev_err(dev, "Failed to initialize governor sysfs groups.\n");
		goto err_out;
	}

	/* Start the governor servicing. */
	ret = gov_start();
	if (ret) {
		dev_err(dev, "Failed to start memlat governor.\n");
		goto err_gov_start;
	}

	return 0;

err_gov_start:
	sysfs_remove_group(&memlat_node.dev->kobj, memlat_node.attr_grp);
err_out:
	gs_memlat_governor_remove_all_votes();

	return ret;
}

static void gs_governor_memlat_driver_remove(struct platform_device *pdev)
{
	cancel_delayed_work_sync(&memlat_node.resume_boost_extend);

	/* Stop governor servicing. */
	gov_stop();

	/* Remove Sysfs here. */
	sysfs_remove_group(&memlat_node.dev->kobj, memlat_node.attr_grp);

	/* Remove pm_qos vote here. */
	gs_memlat_governor_remove_all_votes();
}

static const struct of_device_id gs_governor_memlat_root_match[] = {
	{ .compatible = "google,gs_governor_memlat" },
	{}
};

static int gs_governor_memlat_suspend(struct device *dev)
{
	if (!memlat_node.gov_is_on || !memlat_node.resume_boost_freq_khz)
		return 0;

	cancel_delayed_work_sync(&memlat_node.resume_boost_extend);
	memlat_node.gov_paused = true;

	gs_governor_memlat_update_all_freq_votes(
		memlat_node.resume_boost_freq_khz);

	return 0;
}

static int gs_governor_memlat_resume(struct device *dev)
{
	if (!memlat_node.gov_is_on || !memlat_node.resume_boost_freq_khz)
		return 0;

	if (memlat_node.resume_boost_duration_ms)
		schedule_delayed_work(&memlat_node.resume_boost_extend,
			msecs_to_jiffies(memlat_node.resume_boost_duration_ms));
	else
		memlat_node.gov_paused = false;

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(gs_governor_memlat_pm_ops,
				gs_governor_memlat_suspend,
				gs_governor_memlat_resume);

static struct platform_driver gs_governor_memlat_platform_driver = {
	.probe = gs_governor_memlat_driver_probe,
	.remove = gs_governor_memlat_driver_remove,
	.driver = {
		.name = "gs_governor_memlat",
		.owner = THIS_MODULE,
		.of_match_table = gs_governor_memlat_root_match,
		.suppress_bind_attrs = true,
		.pm = &gs_governor_memlat_pm_ops,
	},
};

module_platform_driver(gs_governor_memlat_platform_driver);
MODULE_AUTHOR("Will Song <jinpengsong@google.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Google Source memlat Governor");
