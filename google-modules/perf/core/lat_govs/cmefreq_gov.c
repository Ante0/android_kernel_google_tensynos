// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 *
 * CME Frequency Governor Main Module.
 */
#define pr_fmt(fmt) "cmefreq_gov: " fmt

#include <linux/device.h>
#include <linux/platform_device.h>

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <perf/core/google_pm_qos.h>
#include <perf/core/gs_perf_mon.h>
#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
#include <perf/core/perf_domain.h>
#endif
#include <trace/events/power.h>

#include "gs_governor_utils.h"
#include "gs_lat_governors_trace.h"

/**
 * struct frequency_vote - Contains configs and voting data.
 * @vote_name:		The name of the device we will vote frequency for.
 * @vote_devfreq:	The devfreq to make a vote on.
 */
struct frequency_vote {
	const char *vote_name;
	struct devfreq *vote_devfreq;
	struct dev_pm_qos_request vote_request;
};

/**
 * struct cmefreq_data - Node containing cmefreq's global data.
 * @gov_is_on:		Governor's active state.
 * @attr_grp:		Tuneable governor parameters exposed to userspace.
 * @dev:		Reference to the governor's device.
 * @num_cpu_clusters:	Number of CPU clusters the governor will service.
 * @cpu_configs_arr:	Configurations for each cluster's latency vote.
 * @target_freq_vote:	Primary target domain.
 */
struct cmefreq_data {
	bool gov_is_on;
	u32 boost_freq;
	u32 base_freq;
	struct attribute_group *attr_grp;
	struct device *dev;
	int num_cpu_clusters;
	struct cluster_config *cpu_configs_arr;
	struct frequency_vote target_freq_vote;
};

static void update_cme_target_freq(struct gs_cpu_perf_data *data, void *private_data);
static void cmefreq_remove_all_votes(void);

/* Global monitor client used to get callbacks when gs_perf_mon data is updated. */
static struct gs_perf_mon_client cmefreq_perf_client = {
	.client_callback = update_cme_target_freq,
	.name = "cmefreq_gov"
};

/* Cmefreq datastructure holding cmefreq governor configurations and metadata. */
static struct cmefreq_data cmefreq_node;

/**
 * update_cme_target_freq_vote - Registers the vote for the cmefreq governor.
 *
 * Inputs:
 * @vote: The domain to vote on.
 * @target_freq: New target frequency.
 *
 * Outputs:
 *			Non-zero on error.
 */
static int cmefreq_update_target_freq_vote(struct frequency_vote *vote,
					    unsigned long target_freq)
{
	if (trace_clock_set_rate_enabled())
		trace_clock_set_rate(vote->vote_name, target_freq, raw_smp_processor_id());
	return dev_pm_qos_update_request(&vote->vote_request, target_freq);
}

/**
 * is_cme_boost
 *
 * This function determines cme frequency boost
 *
 * Input:
 * @cpu_perf_data_arr: CPU data to use as input.
 *
 */
static bool is_cme_boost(struct gs_cpu_perf_data *cpu_perf_data_arr)
{
	int cpu;
	int cluster_idx;
	struct cluster_config *cluster;

	/* For each cluster, we make a frequency decision. */
	for (cluster_idx = 0; cluster_idx < cmefreq_node.num_cpu_clusters; cluster_idx++) {
		cluster = &cmefreq_node.cpu_configs_arr[cluster_idx];
		for_each_cpu(cpu, &cluster->cpus) {
			unsigned long cme_inst;
			struct gs_cpu_perf_data *cpu_data = &cpu_perf_data_arr[cpu];

			/* Check if the cpu monitor is up. */
			if (!cpu_data->cpu_mon_on)
				return false;

			cme_inst = cpu_data->perf_ev_last_delta[PERF_CME_INST_RETIRED_IDX];

			/* If we pass the threshold, boost CME freq to Fmax */
			if (cme_inst > 0)
				return true;
		}
	}

	return false;
}

/**
 * update_cme_target_freq
 *
 * Callback function from the perf monitor to service the cmefreq governor.
 *
 * Input:
 * @data: Performance data from the monitor.
 * @private_data: Unused.
 *
 */
static void update_cme_target_freq(struct gs_cpu_perf_data *data, void *private_data)
{
	bool cme_boost;

	/* If the cmefreq governor is not active. Reset our vote to minimum. */
	if (!cmefreq_node.gov_is_on || !data) {
		dev_dbg(cmefreq_node.dev, "CMEfreq governor is not active. Leaving vote unchanged.\n");
		return;
	}

	/* Step 1: deciding cme boost. */
	cme_boost = is_cme_boost(data);

	/* Step 2: enable/disable cme boost. */

	cmefreq_update_target_freq_vote(&cmefreq_node.target_freq_vote,
			cme_boost ? cmefreq_node.boost_freq : cmefreq_node.base_freq);
}

/**
 * cmefreq_remove_all_votes - Removes all the votes for cmefreq governor.
 */
static void cmefreq_remove_all_votes(void)
{
	/* Remove cmefreq vote. */
	struct frequency_vote *vote = &cmefreq_node.target_freq_vote;

	google_pm_qos_remove_devfreq_request(vote->vote_devfreq, &vote->vote_request);
}

/**
 * gov_start - Starts the governor.
 */
static int gov_start(void)
{
	int ret;

	if (cmefreq_node.gov_is_on)
		return 0;

	/* Add clients. */
	ret = gs_perf_mon_add_client(&cmefreq_perf_client);
	if (ret)
		return ret;

	cmefreq_node.gov_is_on = true;

	return 0;
}

/**
 * gov_stop - Stops the governor.
 */
static void gov_stop(void)
{
	if (!cmefreq_node.gov_is_on)
		return;

	cmefreq_node.gov_is_on = false;

	/* Remove the client. */
	gs_perf_mon_remove_client(&cmefreq_perf_client);

	/* Reset the vote to minimum. */
	cmefreq_update_target_freq_vote(&cmefreq_node.target_freq_vote, 0);

}

/**
 * cmefreq_initialize_vote - Initializes the votes for the cmefreq.
 *
 * Input:
 * @vote_node:	Node containing the vote config.
 * @dev:	The device the governor is binded on.
 *
 * Output:	Non-zero on error.
 */
static int cmefreq_initialize_vote(struct device_node *vote_node, struct device *dev)
{
	int ret;
	struct devfreq *df;
	struct frequency_vote *vote = &cmefreq_node.target_freq_vote;

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
#endif

	ret = google_pm_qos_add_devfreq_request(df, &vote->vote_request,
		DEV_PM_QOS_MIN_FREQUENCY, PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
	if (ret)
		return ret;

	vote->vote_devfreq = df;

	return 0;
}

/**
 * cmefreq_initialize - Initializes the cmefreq governor from a DT Node.
 *
 * Inputs:
 * @governor_node:	The tree node contanin governor data.
 * @data:		The devfreq data to update frequencies.
 *
 * Returns:		Non-zero on error.
 */
static int cmefreq_initialize(struct device_node *governor_node, struct device *dev)
{
	int ret = 0;
	struct device_node *cluster_node = NULL;
	struct cluster_config *cluster;
	int cluster_idx;

	cmefreq_node.num_cpu_clusters = of_get_child_count(governor_node);

	/* Allocate a container for clusters. */
	cmefreq_node.cpu_configs_arr = devm_kzalloc(
		dev, sizeof(struct cluster_config) * cmefreq_node.num_cpu_clusters, GFP_KERNEL);
	if (!cmefreq_node.cpu_configs_arr)
		return -ENOMEM;

	/* Populate the Components. */
	cluster_idx = 0;
	cluster_node = of_get_next_child(governor_node, cluster_node);
	while (cluster_node) {
		cluster = &cmefreq_node.cpu_configs_arr[cluster_idx];
		ret = populate_cluster_config(dev, cluster_node, cluster);

		/* Increment pointer. */
		cluster_idx += 1;
		cluster_node = of_get_next_child(governor_node, cluster_node);
	}
	return 0;
}

static int cmefreq_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *governor_config_node, *frequency_vote_node;
	int ret;

	cmefreq_node.dev = &pdev->dev;

	ret = of_property_read_u32(dev->of_node, "boost_freq", &(cmefreq_node.boost_freq));
	if (ret) {
		dev_err(dev, "Can't parse boost freq for CME.\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(dev->of_node, "base_freq", &(cmefreq_node.base_freq));
	if (ret) {
		dev_err(dev, "Can't parse base freq for CME.\n");
		return -EINVAL;
	}

	/* Find and intitialize frequency votes. */
	frequency_vote_node = of_get_child_by_name(dev->of_node, "primary_vote_config");
	if (!frequency_vote_node) {
		dev_err(dev, "CME frequency_votes not defined.\n");
		return -ENODEV;
	}

	ret = cmefreq_initialize_vote(frequency_vote_node, dev);
	if (ret) {
		dev_err(dev, "Failed to parse cmefreq governor node data.\n");
		return ret;
	}

	/* Find and initialize governor. */
	governor_config_node = of_get_child_by_name(dev->of_node, "governor_config");
	if (!governor_config_node) {
		dev_err(dev, "Cmefreq governor node not defined.\n");
		ret = -ENODEV;
		goto err_out;
	}

	ret = cmefreq_initialize(governor_config_node, dev);
	if (ret) {
		dev_err(dev, "Failed to parse private governor data.\n");
		goto err_out;
	}

	/* Start the governor servicing. */
	ret = gov_start();
	if (ret) {
		dev_err(dev, "Failed to start cmefreq governor.\n");
		goto err_gov_start;
	}

	return 0;

err_gov_start:
	sysfs_remove_group(&cmefreq_node.dev->kobj, cmefreq_node.attr_grp);
err_out:
	cmefreq_remove_all_votes();

	return ret;
}

static void cmefreq_driver_remove(struct platform_device *pdev)
{
	/* Stop governor servicing. */
	gov_stop();

	/* Remove Sysfs here. */
	sysfs_remove_group(&cmefreq_node.dev->kobj, cmefreq_node.attr_grp);

	/* Remove pm_qos vote here. */
	cmefreq_remove_all_votes();
}

static const struct of_device_id cmefreq_of_match[] = {
	{ .compatible = "google,cmefreq_gov" },
	{}
};

static struct platform_driver cmefreq_platform_driver = {
	.probe = cmefreq_driver_probe,
	.remove = cmefreq_driver_remove,
	.driver = {
		.name = "cmefreq_gov",
		.owner = THIS_MODULE,
		.of_match_table = cmefreq_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(cmefreq_platform_driver);
MODULE_AUTHOR("Taeju Park <taeju@google.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Google Cmefreq governor");
