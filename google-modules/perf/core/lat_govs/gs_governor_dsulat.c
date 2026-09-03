// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 *
 * DSU Latency Governor Main Module.
 */
#define pr_fmt(fmt) "gs_governor_dsulat: " fmt

#include <linux/device.h>
#include <linux/platform_device.h>

#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <perf/core/google_pm_qos.h>
#include <perf/core/gs_governor_dsulat.h>
#include <perf/core/gs_perf_mon.h>
#include <perf/core/perf_domain.h>
#include <trace/events/power.h>

#include "gs_governor_utils.h"
#include "gs_lat_governors_trace.h"

/**
 * struct frequency_vote - Contains configs and voting data.
 * @vote_name:		The name of the device we will vote frequency for.
 * @min_freq_req:	The min vote we assert for the device frequency.
 */
struct frequency_vote {
	const char *vote_name;
	struct devfreq *vote_devfreq;
	struct dev_pm_qos_request vote_request;
};

/**
 * struct dsulat_data - Node containing dsulat's global data.
 * @gov_is_on:		Governor's active state.
 * @attr_grp:		Tuneable governor parameters exposed to userspace.
 * @dev:		Reference to the governor's device.
 * @num_cpu_clusters:	Number of CPU clusters the governor will service.
 * @cpu_configs_arr:	Configurations for each cluster's latency vote.
 * @target_freq_vote:	Primary target domain.
 */
struct dsulat_data {
	bool gov_is_on;
	struct attribute_group *attr_grp;
	struct device *dev;
	int num_cpu_clusters;
	struct cluster_config *cpu_configs_arr;
	struct frequency_vote target_freq_vote;
	struct fast_devfreq_vote fdf_vote;
	bool fast_devfreq_vote_is_on;
	spinlock_t latgov_state_lock;
	struct gs_governor_latgov_state *latgov_state_arr;
	bool enable_latency_mode;
};

static void update_dsulat_gov(struct gs_cpu_perf_data *data, void *private_data);

/* Global monitor client used to get callbacks when gs_perf_mon data is updated. */
static struct gs_perf_mon_client dsulat_perf_client = {
	.client_callback = update_dsulat_gov,
	.name = "dsulat"
};

/* Dsulat datastructure holding dsulat governor configurations and metadata. */
static struct dsulat_data dsulat_node;

/* Macro expansions for sysfs nodes. */
MAKE_CLUSTER_ATTR(dsulat_node, stall_floor);
MAKE_CLUSTER_ATTR(dsulat_node, ratio_ceil);
MAKE_CLUSTER_ATTR(dsulat_node, cpuidle_state_depth_threshold);
MAKE_CLUSTER_ATTR(dsulat_node, stale_threshold_ms);

SHOW_CLUSTER_FREQ_MAP_ATTR(dsulat_node, latency_freq_table);

static ssize_t enable_latency_mode_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", dsulat_node.enable_latency_mode);
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

	if (dsulat_node.enable_latency_mode != val)
		dsulat_node.enable_latency_mode = val;

	return count;
}
static DEVICE_ATTR_RW(enable_latency_mode);

/* These sysfs emitters also depend on the macro expansions from above. */
static struct attribute *dsulat_dev_attr[] = {
	&dev_attr_dsulat_node_stall_floor.attr,
	&dev_attr_dsulat_node_ratio_ceil.attr,
	&dev_attr_dsulat_node_cpuidle_state_depth_threshold.attr,
	&dev_attr_dsulat_node_latency_freq_table.attr,
	&dev_attr_enable_latency_mode.attr,
	&dev_attr_dsulat_node_stale_threshold_ms.attr,
	NULL,
};

/* Sysfs files to expose. */
static struct attribute_group dsulat_dev_attr_group = {
	.name = "dsulat_attr",
	.attrs = dsulat_dev_attr,
};

/**
 * gs_governor_dsulat_update_target_freq_vote - Registers the vote for the dsulat governor.
 *
 * Inputs:
 * @vote:		The domain to vote on.
 * @target_freq:	New target frequency.
 *
 * Outputs:
 *			Non-zero on error.
 */
static int gs_governor_dsulat_update_target_freq_vote(struct frequency_vote *vote,
					    unsigned long target_freq)
{
	struct dsulat_data *dsulat = container_of(vote, struct dsulat_data, target_freq_vote);

	if (trace_clock_set_rate_enabled())
		trace_clock_set_rate(vote->vote_name, target_freq, raw_smp_processor_id());

	if (dsulat->fast_devfreq_vote_is_on)
		return update_fast_devfreq_vote(&dsulat->fdf_vote, target_freq);

	return dev_pm_qos_update_request(&vote->vote_request, target_freq);
}

/**
 * gs_governor_dsulat_compute_freq - Calculates dsulat freq votes for each CPU cluster.
 *
 * This function determines the dsulat target frequency.
 *
 * Input:
 * @cpu_perf_data_arr:	CPU data to use as input.
 *
 * Returns:
 * @max_freq:		The computed target frequency.
 */
static unsigned long gs_governor_dsulat_compute_freq(struct gs_cpu_perf_data *cpu_perf_data_arr)
{
	int cpu;
	int cluster_idx;
	struct cluster_config *cluster;
	unsigned long max_freq = 0;
	unsigned long flags;
	ktime_t now;
	char trace_name[] = { 'c', 'p', 'u', '0', 'd', 's', 'u', '\0' };

	now = ktime_get();

	/* For each cluster, we make a frequency decision. */
	for (cluster_idx = 0; cluster_idx < dsulat_node.num_cpu_clusters; cluster_idx++) {
		unsigned long ratio_ceil;
		unsigned long mem_stall_floor;
		bool dsulat_cpuidle_state_aware;
		enum gs_perf_cpu_idle_state configured_idle_depth_threshold;

		cluster = &dsulat_node.cpu_configs_arr[cluster_idx];
		ratio_ceil = cluster->ratio_ceil;
		mem_stall_floor = cluster->stall_floor;
		dsulat_cpuidle_state_aware = cluster->cpuidle_state_aware;
		configured_idle_depth_threshold =
			cluster->cpuidle_state_depth_threshold;

		for_each_cpu(cpu, &cluster->cpus) {
			unsigned long ratio = 0, mem_stall_pct = 0;
			unsigned long l2_cachemiss, mem_stall, cyc, last_delta_us, inst;
			unsigned long dsu_freq = 0, effective_cpu_freq_khz = 0;
			struct gs_cpu_perf_data *cpu_data = &cpu_perf_data_arr[cpu];
			trace_name[3] = '0' + cpu;

			/* Check if the cpu monitor is up. */
			if (!cpu_data->cpu_mon_on)
				goto early_exit;

			l2_cachemiss = cpu_data->perf_ev_last_delta[PERF_L2D_CACHE_REFILL_IDX];
			mem_stall = cpu_data->perf_ev_last_delta[PERF_STALL_BACKEND_MEM_IDX];
			cyc = cpu_data->perf_ev_last_delta[PERF_CYCLE_IDX];
			inst = cpu_data->perf_ev_last_delta[PERF_INST_IDX];
			last_delta_us = cpu_data->time_delta_us;

			/* Compute threshold data. */
			if (l2_cachemiss != 0)
				ratio = inst / l2_cachemiss;
			else
				ratio = inst;

			mem_stall_pct = mult_frac(10000, mem_stall, cyc);
			effective_cpu_freq_khz = MHZ_TO_KHZ * cyc / last_delta_us;

			spin_lock_irqsave(&dsulat_node.latgov_state_lock, flags);
			if ((dsulat_cpuidle_state_aware &&
			     cpu_data->cpu_idle_state >= configured_idle_depth_threshold) ||
			    (cluster->stale_threshold_ms &&
			     ktime_ms_delta(now, cpu_data->last_update_ts) >
			      (s64)cluster->stale_threshold_ms)) {
				dsulat_node.latgov_state_arr[cpu].state = LATGOV_IDLE;
				dsulat_node.latgov_state_arr[cpu].freq_khz = 0;
				spin_unlock_irqrestore(&dsulat_node.latgov_state_lock, flags);
				/* Zeroing vote for sufficiently idle or stale CPUs. */
				goto early_exit_trace;
			}

			/* If we pass the threshold, use the latency table. */
			if (dsulat_node.enable_latency_mode ||
			    (ratio <= ratio_ceil && mem_stall_pct >= mem_stall_floor)) {
				dsu_freq = gs_governor_core_to_dev_freq(cluster->latency_freq_table,
									effective_cpu_freq_khz);
				dsulat_node.latgov_state_arr[cpu].state = LATGOV_LATENCY;
			} else if (cluster->base_freq_table) {
				dsu_freq = gs_governor_core_to_dev_freq(cluster->base_freq_table,
									effective_cpu_freq_khz);
				dsulat_node.latgov_state_arr[cpu].state = LATGOV_BASE;
			}
			dsulat_node.latgov_state_arr[cpu].freq_khz = dsu_freq;
			spin_unlock_irqrestore(&dsulat_node.latgov_state_lock, flags);

			/* Keep a running max of the DSU frequency. */
			if (dsu_freq > max_freq)
				max_freq = dsu_freq;

		early_exit_trace:
			trace_gs_lat_governor("dsulat", cpu, ratio, mem_stall_pct,
				dsu_freq, effective_cpu_freq_khz);
		early_exit:
			/* Leave a trace for the cluster desired DSU frequency. */
			if (trace_clock_set_rate_enabled())
				trace_clock_set_rate(trace_name, dsu_freq, cpu);
		}
	}

	return max_freq;
}

/**
 * update_dsulat_gov  -	Callback function from the perf monitor to service
 * 			the dsulat governor.
 *
 * Input:
 * @data:		Performance data from the monitor.
 * @private_data:	Unused.
 *
 */
static void update_dsulat_gov(struct gs_cpu_perf_data *data, void *private_data)
{
	unsigned long next_frequency;

	/* If the dsulat governor is not active. Reset our vote to minimum. */
	if (!dsulat_node.gov_is_on || !data) {
		dev_dbg(dsulat_node.dev, "Dsulat governor is not active. Leaving vote unchanged.\n");
		return;
	}

	/* Step 1: compute the frequency. */
	next_frequency = gs_governor_dsulat_compute_freq(data);

	/* Step 2: send it as a vote. */
	gs_governor_dsulat_update_target_freq_vote(&dsulat_node.target_freq_vote, next_frequency);
}

void gs_governor_dsulat_cpufreq_update(struct cpufreq_policy *policy, unsigned int target_freq)
{
	int cpu;
	int cluster_idx;
	struct cluster_config *cluster;
	unsigned long max_freq = 0;
	unsigned long flags;

	if (!dsulat_node.gov_is_on)
		return;

	if (!dsulat_node.fast_devfreq_vote_is_on)
		return;

	spin_lock_irqsave(&dsulat_node.latgov_state_lock, flags);
	for (cluster_idx = 0; cluster_idx < dsulat_node.num_cpu_clusters; cluster_idx++) {
		cluster = &dsulat_node.cpu_configs_arr[cluster_idx];
		for_each_cpu(cpu, &cluster->cpus) {
			unsigned long dsu_freq = 0;

			if (cpumask_intersects(policy->cpus, cpumask_of(cpu))) {
				switch (dsulat_node.latgov_state_arr[cpu].state) {
				case LATGOV_IDLE:
					break;
				case LATGOV_LATENCY:
					dsu_freq = gs_governor_core_to_dev_freq(
						cluster->latency_freq_table, target_freq);
					break;
				case LATGOV_BASE:
					dsu_freq = gs_governor_core_to_dev_freq(
						cluster->base_freq_table, target_freq);
					break;
				}
				dsulat_node.latgov_state_arr[cpu].freq_khz = dsu_freq;
			} else {
				dsu_freq = dsulat_node.latgov_state_arr[cpu].freq_khz;
			}

			if (dsu_freq > max_freq)
				max_freq = dsu_freq;
		}
	}
	spin_unlock_irqrestore(&dsulat_node.latgov_state_lock, flags);
	trace_clock_set_rate(dsulat_node.fdf_vote.vote_name, max_freq, raw_smp_processor_id());
	update_fast_devfreq_vote(&dsulat_node.fdf_vote, max_freq);
}
EXPORT_SYMBOL_GPL(gs_governor_dsulat_cpufreq_update);

/**
 * gs_dsulat_governor_remove_all_votes - Removes all the votes for dsulat governor.
 */
static void gs_dsulat_governor_remove_all_votes(void)
{
	/* Remove dsulat vote. */
	struct frequency_vote *vote = &dsulat_node.target_freq_vote;
	google_pm_qos_remove_devfreq_request(vote->vote_devfreq, &vote->vote_request);
}

/**
 * gov_start - Starts the governor.
 */
static int gov_start(void)
{
	int ret;
	if (dsulat_node.gov_is_on)
		return 0;

	/* Add clients. */
	ret = gs_perf_mon_add_client(&dsulat_perf_client);
	if (ret)
		return ret;

	dsulat_node.gov_is_on = true;

	return 0;
}

/**
 * gov_stop - Stops the governor.
 */
static void gov_stop(void)
{
	if (!dsulat_node.gov_is_on)
		return;

	dsulat_node.gov_is_on = false;

	/* Remove the client. */
	gs_perf_mon_remove_client(&dsulat_perf_client);

	/* Reset the vote to minimum. */
	gs_governor_dsulat_update_target_freq_vote(&dsulat_node.target_freq_vote, 0);

}

/**
 * gs_dsulat_governor_initialize_vote - Initializes the votes for the dsulat.
 *
 * Input:
 * @vote_node:	Node containing the vote config.
 * @dev:	The device the governor is binded on.
 *
 * Output:	Non-zero on error.
 */
static int gs_dsulat_governor_initialize_vote(struct device_node *vote_node, struct device *dev)
{
	int ret;
	struct devfreq *df;
	struct frequency_vote *vote = &dsulat_node.target_freq_vote;

	if (of_property_read_string(vote_node, "vote_name",
				    &vote->vote_name)) {
		dev_err(dev, "vote_name undefined\n");
		return -ENODEV;
	}

#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
	df = gs_perf_domain_find_devfreq("dsu", GS_RECOMMENDED_DEVFREQ);
	if (IS_ERR_OR_NULL(df)) {
		ret = PTR_ERR(df);
		dev_err(dev, "failed to get devfreq: %d\n", ret);
		return ret;
	}
#else /* CONFIG_GS_PERF_DOMAIN */
	struct device_node *vote_df_node;

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
#endif /* CONFIG_GS_PERF_DOMAIN */

	ret = google_pm_qos_add_devfreq_request(df, &vote->vote_request,
		DEV_PM_QOS_MIN_FREQUENCY, PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
	if (ret)
		return ret;

	vote->vote_devfreq = df;

	return 0;
}

/**
 * gs_dsulat_governor_initialize - Initializes the dsulat governor from a DT Node.
 *
 * Inputs:
 * @governor_node:	The tree node contanin governor data.
 * @data:		The devfreq data to update frequencies.
 *
 * Returns:		Non-zero on error.
 */
static int gs_dsulat_governor_initialize(struct device_node *governor_node, struct device *dev)
{
	int ret = 0;
	struct device_node *cluster_node = NULL;
	struct cluster_config *cluster;
	int cluster_idx;

	dsulat_node.num_cpu_clusters = of_get_child_count(governor_node);

	/* Allocate a container for clusters. */
	dsulat_node.cpu_configs_arr = devm_kzalloc(
		dev, sizeof(struct cluster_config) * dsulat_node.num_cpu_clusters, GFP_KERNEL);
	if (!dsulat_node.cpu_configs_arr) {
		dev_err(dev, "No memory for cluster_configs.\n");
		return -ENOMEM;
	}

	/* Populate the Components. */
	cluster_idx = 0;
	cluster_node = of_get_next_child(governor_node, cluster_node);
	while (cluster_node) {
		cluster = &dsulat_node.cpu_configs_arr[cluster_idx];
		ret = populate_cluster_config(dev, cluster_node, cluster);
		if (ret)
			return ret;

		/* Increment pointer. */
		cluster_idx += 1;
		cluster_node = of_get_next_child(governor_node, cluster_node);
	}

	/* Allocate a container for cpu_latgov_state. */
	dsulat_node.latgov_state_arr =
		devm_kzalloc(dev, sizeof(struct gs_governor_latgov_state) * num_possible_cpus(),
				GFP_KERNEL);
	if (!dsulat_node.latgov_state_arr) {
		dev_err(dev, "No memory for latgov_state_arr.\n");
		return -ENOMEM;
	}
	spin_lock_init(&dsulat_node.latgov_state_lock);

	return 0;
}

static int gs_governor_dsulat_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *governor_config_node, *frequency_vote_node;
	int ret;

	dsulat_node.dev = &pdev->dev;
	dsulat_node.attr_grp = &dsulat_dev_attr_group;

	/* Find and intitialize frequency votes. */
	frequency_vote_node = of_get_child_by_name(dev->of_node, "primary_vote_config");
	if (!frequency_vote_node) {
		dev_err(dev, "Dsulat frequency_votes not defined.\n");
		return -ENODEV;
	}

	ret = gs_dsulat_governor_initialize_vote(frequency_vote_node, dev);
	if (ret) {
		dev_err(dev, "Failed to parse dsulat governor node data.\n");
		return ret;
	}

	ret = add_fast_devfreq_vote(dsulat_node.target_freq_vote.vote_devfreq,
				    dsulat_node.target_freq_vote.vote_name,
				    &dsulat_node.fdf_vote);
	if (ret) {
		dev_warn(dev, "Failed to register fast devfreq vote path.\n");
		dsulat_node.fast_devfreq_vote_is_on = false;
	} else
		dsulat_node.fast_devfreq_vote_is_on = true;

	/* Find and initialize governor. */
	governor_config_node = of_get_child_by_name(dev->of_node, "governor_config");
	if (!governor_config_node) {
		dev_err(dev, "Dsulat Governor node not defined.\n");
		ret = -ENODEV;
		goto err_out;
	}

	ret = gs_dsulat_governor_initialize(governor_config_node, dev);
	if (ret) {
		dev_err(dev, "Failed to parse private governor data.\n");
		goto err_out;
	}

	/* Add sysfs nodes here. */
	ret = sysfs_create_group(&dev->kobj, dsulat_node.attr_grp);
	if (ret) {
		dev_err(dev, "Failed to initialize governor sysfs groups.\n");
		goto err_out;
	}

	/* Start the governor servicing. */
	ret = gov_start();
	if (ret) {
		dev_err(dev, "Failed to start dsulat governor.\n");
		goto err_gov_start;
	}

	return 0;

err_gov_start:
	sysfs_remove_group(&dsulat_node.dev->kobj, dsulat_node.attr_grp);
err_out:
	gs_dsulat_governor_remove_all_votes();

	return ret;
}

static void gs_governor_dsulat_driver_remove(struct platform_device *pdev)
{
	/* Stop governor servicing. */
	gov_stop();

	/* Remove Sysfs here. */
	sysfs_remove_group(&dsulat_node.dev->kobj, dsulat_node.attr_grp);

	/* Remove pm_qos vote here. */
	gs_dsulat_governor_remove_all_votes();

	if (dsulat_node.fast_devfreq_vote_is_on)
		remove_fast_devfreq_vote(&dsulat_node.fdf_vote);
}

static const struct of_device_id gs_governor_dsulat_root_match[] = {
	{ .compatible = "google,gs_governor_dsulat" },
	{}
};

static struct platform_driver gs_governor_dsulat_platform_driver = {
	.probe = gs_governor_dsulat_driver_probe,
	.remove = gs_governor_dsulat_driver_remove,
	.driver = {
		.name = "gs_governor_dsulat",
		.owner = THIS_MODULE,
		.of_match_table = gs_governor_dsulat_root_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(gs_governor_dsulat_platform_driver);
MODULE_AUTHOR("Will Song <jinpengsong@google.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Google Source Dsulat Governor");
