// SPDX-License-Identifier: GPL-2.0-only
/*
 * thermal_pressure_tj.c driver to apply Tj thermal pressure
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include "cdev_cpufreq_helper.h"
#include "thermal_cpm_mbox.h"
#include "thermal_sm_helper.h"

typedef struct {
	uint8_t tz_id;
	uint8_t frequency_cap_idx;
	uint8_t switch_on_status;
} __packed thermal_sm_tj_pressure_cpu_t;

typedef struct {
	uint8_t num_cpu_tzs;
	thermal_sm_tj_pressure_cpu_t cpu_tz_arr[];
} __packed thermal_sm_tj_pressure_data_t;

typedef struct {
	struct device *dev;
	unsigned int num_cpu_tzs;
	unsigned int active_polling_delay_ms;
	unsigned int passive_polling_delay_ms;
	unsigned int current_polling_delay_ms;
	unsigned int mapping_cdev_id[HW_THERMAL_ZONE_MAX];
	unsigned int num_opps[HW_THERMAL_ZONE_MAX];
	struct cpumask related_cpus[HW_THERMAL_ZONE_MAX];
	struct cdev_opp_table *opp_tables[HW_THERMAL_ZONE_MAX];
} thermal_tj_pressure_data_t;

static thermal_tj_pressure_data_t tj_pressure_data;
static thermal_sm_tj_pressure_data_t *sm_buffer_copy;
static u32 sm_buffer_size;
static struct notifier_block thermal_tj_pressure_irq_notifier;

static void apply_tj_thermal_pressure(struct work_struct *work);
static DECLARE_DEFERRABLE_WORK(thermal_pressure_tj_work, apply_tj_thermal_pressure);

static const struct of_device_id thermal_pressure_match_table[] = {
	{
		.compatible = "google,thermal-pressure-tj",
	},
	{}
};
MODULE_DEVICE_TABLE(of, thermal_pressure_match_table);

static int thermal_tj_pressure_process_irq(struct notifier_block *nb, unsigned long val, void *data)
{
	// Check immediately if the device is in passive polling
	if (tj_pressure_data.current_polling_delay_ms == tj_pressure_data.passive_polling_delay_ms)
		mod_delayed_work(system_unbound_wq, &thermal_pressure_tj_work, 0);

	return NOTIFY_OK;
}

static void apply_tj_thermal_pressure(struct work_struct *work)
{
	int ret;
	int polling_delay_ms = tj_pressure_data.passive_polling_delay_ms;

	ret = thermal_sm_get_tj_pressure_data((u8 *)sm_buffer_copy, sm_buffer_size);
	if (ret) {
		dev_err_ratelimited(
			tj_pressure_data.dev,
			"Failed to get tj thermal pressure data from shared memory. ret=%d\n", ret);
		goto out;
	}

	if (sm_buffer_copy->num_cpu_tzs != tj_pressure_data.num_cpu_tzs) {
		dev_err_ratelimited(tj_pressure_data.dev, "Invalid num_cpu_tzs: %d. Expected: %d\n",
				    sm_buffer_copy->num_cpu_tzs, tj_pressure_data.num_cpu_tzs);
		goto out;
	}

	for (int i = 0; i < sm_buffer_copy->num_cpu_tzs; i++) {
		uint8_t frequency_cap_idx = sm_buffer_copy->cpu_tz_arr[i].frequency_cap_idx;
		uint8_t tz_id = sm_buffer_copy->cpu_tz_arr[i].tz_id;
		unsigned long freq = 0;
		int opp_idx;

		if ((tz_id >= HW_THERMAL_ZONE_MAX) ||
		    (cpumask_empty(&tj_pressure_data.related_cpus[tz_id])) ||
		    (!tj_pressure_data.opp_tables[tz_id])) {
			dev_err_ratelimited(tj_pressure_data.dev, "Invalid tz_id: %d\n", tz_id);
			continue;
		}

		opp_idx = tj_pressure_data.num_opps[tz_id] - frequency_cap_idx - 1;
		if ((opp_idx < 0) || (opp_idx >= tj_pressure_data.num_opps[tz_id])) {
			dev_err_ratelimited(
				tj_pressure_data.dev,
				"Invalid opp_idx: %d for tz_id: %d num_opps: %d freq_idx: %d\n",
				opp_idx, tz_id, tj_pressure_data.num_opps[tz_id],
				frequency_cap_idx);
			continue;
		}

		freq = tj_pressure_data.opp_tables[tz_id][opp_idx].freq;
		arch_update_hw_pressure(&tj_pressure_data.related_cpus[tz_id], freq);

		// Set active polling delay if any of the zones is switched on
		polling_delay_ms = (sm_buffer_copy->cpu_tz_arr[i].switch_on_status) ?
					   tj_pressure_data.active_polling_delay_ms :
					   polling_delay_ms;
	}

out:
	tj_pressure_data.current_polling_delay_ms = polling_delay_ms;
	queue_delayed_work(system_unbound_wq, &thermal_pressure_tj_work,
			   msecs_to_jiffies(polling_delay_ms));
}

static void thermal_pressure_tj_remove(struct platform_device *pdev)
{
	// unregister from mbox notifications
	for (int i = 0; i < HW_THERMAL_ZONE_MAX; i++) {
		if (cpumask_empty(&tj_pressure_data.related_cpus[i]))
			continue;

		thermal_cpm_mbox_unregister_notification(i, &thermal_tj_pressure_irq_notifier);
	}

	cancel_delayed_work_sync(&thermal_pressure_tj_work);
}

static int parse_cluster_config(struct device *dev)
{
	unsigned int parsed_clusters = 0;
	struct device_node *cluster_config = NULL;
	struct device_node *firstcpu_node = NULL;
	struct device_node *child_node;
	int ret = 0;

	cluster_config = of_get_next_child(dev_of_node(dev), NULL);
	if (!cluster_config) {
		dev_err(dev, "Failed to read cluster config\n");
		ret = -ENODATA;
		goto err;
	}

	for_each_child_of_node(cluster_config, child_node) {
		uint32_t tz_id, cdev_id;
		struct cpufreq_policy *policy;
		int cpu;

		firstcpu_node = of_parse_phandle(child_node, "first-cpu", 0);
		if (!firstcpu_node) {
			dev_err(dev, "Failed to read first-cpu property\n");
			ret = -EINVAL;
			goto err;
		}

		if (of_property_read_u32(child_node, "cdev-id", &cdev_id)) {
			dev_err(dev, "Failed to read cdev-id property\n");
			ret = -EINVAL;
			goto err;
		}

		ret = thermal_cpm_mbox_cdev_to_hw_tzid(cdev_id, &tz_id);
		if (ret) {
			dev_err(dev, "Failed to get tz_id for cdev_id = %d\n", cdev_id);
			goto err;
		}

		if (tz_id >= HW_THERMAL_ZONE_MAX) {
			dev_err(dev, "Invalid tz_id = %d\n", tz_id);
			ret = -EINVAL;
			goto err;
		}

		cpu = of_cpu_node_to_id(firstcpu_node);
		if (cpu < 0) {
			dev_err(dev, "Failed to get cpu id from firstcpu node for cdev_id = %d\n",
				cdev_id);
			ret = -EINVAL;
			goto err;
		}

		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			dev_err(dev, "Failed to get cpufreq policy for cpu = %d\n", cpu);
			ret = -ENODEV;
			goto err;
		}

		cpumask_copy(&tj_pressure_data.related_cpus[tz_id], policy->related_cpus);
		cpufreq_cpu_put(policy);

		parsed_clusters++;
		dev_dbg(dev, "tz_id = %d cpu = %d\n", tz_id, cpu);

		of_node_put(firstcpu_node);
		firstcpu_node = NULL;

		tj_pressure_data.mapping_cdev_id[tz_id] = cdev_id;
	}

	if (!parsed_clusters) {
		dev_err(dev, "No cpu clusters configured for thermal pressure");
		ret = -EINVAL;
		goto err;
	}

	if (parsed_clusters != of_get_child_count(cluster_config)) {
		dev_err(dev,
			"parsed_clusters: %d does not match child count: %d\n",
			parsed_clusters, of_get_child_count(cluster_config));
		ret = -EINVAL;
		goto err;
	}

	tj_pressure_data.num_cpu_tzs = parsed_clusters;

err:
	if (firstcpu_node)
		of_node_put(firstcpu_node);
	if (cluster_config)
		of_node_put(cluster_config);
	return ret;
}

static int thermal_pressure_tj_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;

	tj_pressure_data.dev = dev;
	if (of_property_read_u32(dev->of_node, "active-polling-delay-ms",
				 &tj_pressure_data.active_polling_delay_ms)) {
		dev_err(dev, "Failed to read active_polling_delay_ms property\n");
		return -ENODATA;
	}

	if (of_property_read_u32(dev->of_node, "passive-polling-delay-ms",
				 &tj_pressure_data.passive_polling_delay_ms)) {
		dev_err(dev, "Failed to read passive_polling_delay_ms property\n");
		return -ENODATA;
	}

	if (tj_pressure_data.active_polling_delay_ms > tj_pressure_data.passive_polling_delay_ms) {
		dev_err(dev,
			"active_polling_delay_ms = %d is > passive_polling_delay_ms = %d",
			tj_pressure_data.active_polling_delay_ms,
			tj_pressure_data.passive_polling_delay_ms);
		return -EINVAL;
	}

	tj_pressure_data.current_polling_delay_ms = tj_pressure_data.active_polling_delay_ms;
	ret = parse_cluster_config(dev);
	if (ret) {
		dev_err(dev, "Failed to parse cluster config. ret=%d\n", ret);
		return ret;
	}

	thermal_tj_pressure_irq_notifier.notifier_call = thermal_tj_pressure_process_irq;

	ret = thermal_sm_initialize_section(dev, THERMAL_SM_TJ_PRESSURE);
	if (ret) {
		dev_err(dev, "Failed to initialize shared memory section during probe. ret=%d\n",
			ret);
		return ret;
	}

	sm_buffer_size = struct_size(sm_buffer_copy, cpu_tz_arr, tj_pressure_data.num_cpu_tzs);
	sm_buffer_copy = devm_kzalloc(dev, sm_buffer_size, GFP_KERNEL);
	if (!sm_buffer_copy)
		return -ENOMEM;

	for (unsigned int tz_id = 0; tz_id < HW_THERMAL_ZONE_MAX; tz_id++) {
		unsigned int cpu;
		int opp_count;

		if (cpumask_empty(&tj_pressure_data.related_cpus[tz_id]))
			continue;

		cpu = cpumask_first(&tj_pressure_data.related_cpus[tz_id]);
		opp_count = cdev_cpufreq_get_opp_count(cpu);
		if (opp_count <= 0) {
			dev_err(dev, "Failed to get opp count for cpu:%d. ret=%d\n", cpu,
				opp_count);
			ret = -EINVAL;
			goto err;
		}

		tj_pressure_data.opp_tables[tz_id] = devm_kcalloc(
			dev, opp_count, sizeof(*tj_pressure_data.opp_tables[tz_id]), GFP_KERNEL);
		if (!tj_pressure_data.opp_tables[tz_id])
			return -ENOMEM;

		ret = cdev_cpufreq_update_opp_table(cpu, tj_pressure_data.mapping_cdev_id[tz_id],
						    tj_pressure_data.opp_tables[tz_id], opp_count);
		if (ret) {
			dev_err(dev, "Failed to update opp table for cpu:%d. ret=%d\n", cpu, ret);
			goto err;
		}
		tj_pressure_data.num_opps[tz_id] = opp_count;

		ret = thermal_cpm_mbox_register_notification(tz_id,
							     &thermal_tj_pressure_irq_notifier);
		if (ret) {
			dev_err(dev, "thermal mbox registration failed for tz_id: %d\n", tz_id);
			goto err;
		}
	}

	queue_delayed_work(system_unbound_wq, &thermal_pressure_tj_work,
			   msecs_to_jiffies(tj_pressure_data.current_polling_delay_ms));
	dev_dbg(
		dev,
		"Tj thermal pressure module probe complete. Scheduled a delayed work after %d ms\n",
		tj_pressure_data.current_polling_delay_ms);

err:
	if (ret)
		thermal_pressure_tj_remove(pdev);
	return ret;
}

static struct platform_driver thermal_pressure_tj_driver = {
	.driver = {
		.name = "thermal-pressure-tj",
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
		.owner = THIS_MODULE,
		.of_match_table = thermal_pressure_match_table,
	},
	.probe = thermal_pressure_tj_probe,
	.remove_new = thermal_pressure_tj_remove,
};
module_platform_driver(thermal_pressure_tj_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sayanna Chandula <sayanna@google.com>");
MODULE_DESCRIPTION("Google LLC Tj Thermal Pressure Driver.");
MODULE_ALIAS("platform:thermal-pressure-tj");
