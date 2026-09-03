// SPDX-License-Identifier: GPL-2.0
/*
 * gs_bcl_qos.c Google bcl PMQOS driver
 *
 * Copyright (c) 2023, Google LLC. All rights reserved.
 *
 */

#include <linux/of.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#if IS_ENABLED(CONFIG_REGULATOR_S2MPG14)
#include <linux/mfd/samsung/s2mpg1415.h>
#include <linux/mfd/samsung/s2mpg1415-register.h>
#endif
#include "bcl.h"
#include "soc/soc_defs.h"

#define CREATE_TRACE_POINTS
#include <trace/events/bcl_exynos.h>

struct qos_data {
	struct list_head list;
	struct bcl_zone *zone;
};

static LIST_HEAD(qos_data_list);

static void trace_qos(bool throttle, const char *devname)
{
	char buf[64];

	if (!trace_clock_set_rate_enabled())
		return;
	scnprintf(buf, sizeof(buf), "BCL_ZONE_%s_QOS", devname);
	trace_clock_set_rate(buf, throttle ? 1 : 0, raw_smp_processor_id());
}

static int get_idx_from_zone(struct bcl_device *bcl_dev, const char *name)
{
	int i;
	struct bcl_zone *zone;

	for (i = 0; i < TRIGGERED_SOURCE_MAX; i++) {
		zone = bcl_dev->zone[i];
		if (!zone)
			continue;
		if (!strcmp(name, zone->devname))
			return i;
	}
	return -EINVAL;
}

int google_bcl_parse_qos(struct bcl_device *bcl_dev)
{
	struct device_node *np = bcl_dev->device->of_node;
	struct device_node *p_np __free(device_node) = NULL;
	int idx;

	/* parse qos */
	p_np = of_get_child_by_name(np, "freq_qos");
	if (!p_np)
		return -EINVAL;
	for_each_child_of_node_scoped(p_np, child) {
		idx = get_idx_from_zone(bcl_dev, child->name);
		if (idx < 0)
			continue;
		bcl_dev->zone[idx]->bcl_qos = devm_kzalloc(bcl_dev->device,
							   sizeof(struct qos_throttle_limit),
							   GFP_KERNEL);
		if (!bcl_dev->zone[idx]->bcl_qos)
			return -ENOMEM;
		if (of_property_read_u32_array(child, "cpucl0",
					       &bcl_dev->zone[idx]->bcl_qos->cpu_limit[QOS_CPU0][0],
					       QOS_PARAM_CNT) != 0) {
			bcl_dev->zone[idx]->bcl_qos->cpu_limit[QOS_CPU0][QOS_LIGHT_IND] = INT_MAX;
			bcl_dev->zone[idx]->bcl_qos->cpu_limit[QOS_CPU0][QOS_HEAVY_IND] = INT_MAX;
		}
		if (of_property_read_u32_array(child, "cpucl1",
					       &bcl_dev->zone[idx]->bcl_qos->cpu_limit[QOS_CPU1][0],
					       QOS_PARAM_CNT) != 0) {
			bcl_dev->zone[idx]->bcl_qos->cpu_limit[QOS_CPU1][QOS_LIGHT_IND] = INT_MAX;
			bcl_dev->zone[idx]->bcl_qos->cpu_limit[QOS_CPU1][QOS_HEAVY_IND] = INT_MAX;
		}
		if (of_property_read_u32_array(child, "cpucl2",
					       &bcl_dev->zone[idx]->bcl_qos->cpu_limit[QOS_CPU2][0],
					       QOS_PARAM_CNT) != 0) {
			bcl_dev->zone[idx]->bcl_qos->cpu_limit[QOS_CPU2][QOS_LIGHT_IND] = INT_MAX;
			bcl_dev->zone[idx]->bcl_qos->cpu_limit[QOS_CPU2][QOS_HEAVY_IND] = INT_MAX;
		}
		if (of_property_read_u32_array(child, "gpu",
					       &bcl_dev->zone[idx]->bcl_qos->df_limit[QOS_GPU][0],
					       QOS_PARAM_CNT) != 0) {
			bcl_dev->zone[idx]->bcl_qos->df_limit[QOS_GPU][QOS_LIGHT_IND] = INT_MAX;
			bcl_dev->zone[idx]->bcl_qos->df_limit[QOS_GPU][QOS_HEAVY_IND] = INT_MAX;
		}
		if (of_property_read_u32_array(child, "tpu",
					       &bcl_dev->zone[idx]->bcl_qos->df_limit[QOS_TPU][0],
					       QOS_PARAM_CNT) != 0) {
			bcl_dev->zone[idx]->bcl_qos->df_limit[QOS_TPU][QOS_LIGHT_IND] = INT_MAX;
			bcl_dev->zone[idx]->bcl_qos->df_limit[QOS_TPU][QOS_HEAVY_IND] = INT_MAX;
		}
	}
	bcl_dev->throttle = false;
	return 0;
}

static void add_qos_request(struct bcl_device *bcl_dev, struct bcl_zone *zone)
{
	struct qos_data *data = kmalloc(sizeof(struct qos_data), GFP_KERNEL);

	if (!data)
		return;

	data->zone = zone;
	if (mutex_lock_interruptible(&bcl_dev->qos_update_lock)) {
		kfree(data);
		return;
	}
	list_add_tail(&data->list, &qos_data_list);
	mutex_unlock(&bcl_dev->qos_update_lock);
}

static void process_qos_request(struct work_struct *work)
{
	struct bcl_device *bcl_dev = container_of(work, struct bcl_device, qos_work.work);
	struct list_head *n, *pos;
	int idx;

	list_for_each_safe(pos, n, &qos_data_list) {
		struct qos_data *data = list_entry(pos, struct qos_data, list);
		struct qos_throttle_limit *qos;

		qos = data->zone->bcl_qos;

		for (idx = 0; idx < CPU_CORES_MAX; idx++) {
			if (bcl_dev->cpu_cluster_on[idx])
				freq_qos_update_request(&qos->cpu_qos_max[idx],
							qos->cpu_freq[idx]);
		}

		for (idx = 0; idx < NON_CPU_CORES_MAX; idx++) {
			if (!exynos_pm_qos_request_active(&qos->qos_max[idx]))
				continue;
			exynos_pm_qos_update_request_async(&qos->qos_max[idx], qos->df_freq[idx]);
		}
		if (mutex_lock_interruptible(&bcl_dev->qos_update_lock))
			continue;
		list_del(pos);
		kfree(data);
		mutex_unlock(&bcl_dev->qos_update_lock);
	}

}

void google_bcl_qos_update(struct bcl_zone *zone, int throttle_lvl)
{
	struct bcl_device *bcl_dev;
	int cpu0_freq = INT_MAX, cpu1_freq = INT_MAX, cpu2_freq = INT_MAX;
	int tpu_freq = INT_MAX, gpu_freq = INT_MAX;

	if (!zone->bcl_qos)
		return;
	bcl_dev = zone->parent;

	if (zone->throttle_lvl && (zone->throttle_lvl == throttle_lvl))
		return;
	zone->throttle_lvl = throttle_lvl;

	if (throttle_lvl == QOS_NONE)
		goto end_qos_update;

	cpu0_freq = umin(cpu0_freq, zone->bcl_qos->cpu_limit[QOS_CPU0]
			 [zone->throttle_lvl - QOS_PARAM_OFFSET]);
	cpu1_freq = umin(cpu1_freq, zone->bcl_qos->cpu_limit[QOS_CPU1]
			 [zone->throttle_lvl - QOS_PARAM_OFFSET]);
	cpu2_freq = umin(cpu2_freq, zone->bcl_qos->cpu_limit[QOS_CPU2]
			 [zone->throttle_lvl - QOS_PARAM_OFFSET]);
	tpu_freq = umin(tpu_freq, zone->bcl_qos->df_limit[QOS_TPU]
			[zone->throttle_lvl - QOS_PARAM_OFFSET]);
	gpu_freq = umin(gpu_freq, zone->bcl_qos->df_limit[QOS_GPU]
			[zone->throttle_lvl - QOS_PARAM_OFFSET]);

end_qos_update:
	zone->bcl_qos->cpu_freq[QOS_CPU0] = cpu0_freq;
	zone->bcl_qos->cpu_freq[QOS_CPU1] = cpu1_freq;
	zone->bcl_qos->cpu_freq[QOS_CPU2] = cpu2_freq;
	zone->bcl_qos->df_freq[QOS_TPU] = tpu_freq;
	zone->bcl_qos->df_freq[QOS_GPU] = gpu_freq;

	add_qos_request(bcl_dev, zone);

	/* Ensure SW mitigation is enabled */
	if (smp_load_acquire(&bcl_dev->sw_mitigation_enabled))
		schedule_delayed_work(&bcl_dev->qos_work, 0);
	trace_bcl_irq_trigger(zone->idx, zone->throttle_lvl,
			      cpu0_freq, cpu1_freq, cpu2_freq,
			      tpu_freq, gpu_freq, zone->bcl_stats.voltage,
			      zone->bcl_stats.capacity);
	trace_qos(zone->throttle_lvl, zone->devname);
}

static int init_freq_qos(struct bcl_device *bcl_dev, struct qos_throttle_limit *throttle)
{
	struct cpufreq_policy *policy = NULL;
	int ret, idx;

	for (idx = 0; idx < CPU_CORES_MAX; idx++) {
		policy = cpufreq_cpu_get(bcl_dev->cpu_cluster[idx]);
		if (!policy) {
			ret = -EINVAL;
			goto fail;
		}
		bcl_dev->cpu_cluster_on[idx] = true;
		ret = freq_qos_add_request(&policy->constraints, &throttle->cpu_qos_max[idx],
					   FREQ_QOS_MAX, INT_MAX);
		cpufreq_cpu_put(policy);
		if (ret < 0)
			goto fail;
	}

	return 0;
fail:
	dev_err(bcl_dev->device, "Fail to register CPU%d qos\n", idx);
	idx -= 1;
	for (; idx >= 0; idx--) {
		freq_qos_remove_request(&throttle->cpu_qos_max[idx]);
		bcl_dev->cpu_cluster_on[idx] = false;
	}
	return ret;
}

void google_bcl_remove_qos(struct bcl_device *bcl_dev)
{
	int i, idx;
	struct bcl_zone *zone;
	struct list_head *n, *pos;

	for (i = 0; i < TRIGGERED_SOURCE_MAX; i++) {
		zone = bcl_dev->zone[i];
		if ((!zone) || (!zone->bcl_qos))
			continue;
		if (zone->conf_qos) {
			for (idx = 0; idx < CPU_CORES_MAX; idx++) {
				if (bcl_dev->cpu_cluster_on[idx])
					freq_qos_remove_request(&zone->bcl_qos->cpu_qos_max[idx]);
			}
			exynos_pm_qos_remove_request(&zone->bcl_qos->qos_max[QOS_TPU]);
			exynos_pm_qos_remove_request(&zone->bcl_qos->qos_max[QOS_GPU]);
			zone->bcl_qos = NULL;
			zone->conf_qos = false;
		}
	}

	list_for_each_safe(pos, n, &qos_data_list) {
		struct qos_data *data = list_entry(pos, struct qos_data, list);

		list_del(pos);
		kfree(data);
	}
	if (bcl_dev->qos_work.work.func != NULL)
		cancel_delayed_work_sync(&bcl_dev->qos_work);

	mutex_destroy(&bcl_dev->qos_update_lock);

}

int google_bcl_setup_qos(struct bcl_device *bcl_dev)
{
	int ret = 0;
	int i;
	struct bcl_zone *zone;

	for (i = 0; i < TRIGGERED_SOURCE_MAX; i++) {
		zone = bcl_dev->zone[i];
		if ((!zone) || (!zone->bcl_qos))
			continue;

		ret = init_freq_qos(bcl_dev, zone->bcl_qos);
		if (ret < 0) {
			dev_err(bcl_dev->device, "Cannot init pm qos on %d for cpu\n",
				zone->idx);
			goto fail;
		}
		exynos_pm_qos_add_request(&zone->bcl_qos->qos_max[QOS_TPU], PM_QOS_TPU_FREQ_MAX,
				  	  INT_MAX);
		exynos_pm_qos_add_request(&zone->bcl_qos->qos_max[QOS_GPU], PM_QOS_GPU_FREQ_MAX,
				  	  INT_MAX);
		zone->conf_qos = true;
		zone->throttle_lvl = QOS_NONE;
	}
	mutex_init(&bcl_dev->qos_update_lock);
	INIT_DELAYED_WORK(&bcl_dev->qos_work, process_qos_request);
	INIT_LIST_HEAD(&qos_data_list);
	return 0;
fail:
	google_bcl_remove_qos(bcl_dev);
	return ret;
}
