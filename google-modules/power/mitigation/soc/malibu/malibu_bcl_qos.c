// SPDX-License-Identifier: GPL-2.0-only
/*
 * mbu_bcl_qos.c Google bcl PMQOS driver
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 *
 */

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/devfreq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <perf/core/google_pm_qos.h>
#include "bcl.h"
#include "bcl_trace.h"
#include "soc/soc_defs.h"
#define CREATE_TRACE_POINTS

struct qos_data {
	struct list_head list;
	struct bcl_zone *zone;
};

static LIST_HEAD(qos_data_list);

static void trace_qos(int throttle_lvl, const char *devname)
{
	char buf[64];

	if (!trace_clock_set_rate_enabled())
		return;
	scnprintf(buf, sizeof(buf), "BCL_ZONE_%s_QOS", devname);
	trace_clock_set_rate(buf, throttle_lvl, raw_smp_processor_id());
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

int google_bcl_setup_qos_device(struct bcl_device *bcl_dev, enum non_cpu_qos_device_idx qidx)
{
	struct device_node *np = bcl_dev->device->of_node;
	struct device_node *df_node;
	struct device_node *p_np __free(device_node) = NULL;
	int idx, ret;
	struct qos_throttle_limit *throttle;

	p_np = of_get_child_by_name(np, "freq_qos");
	if (!p_np)
		return -ENODEV;
	df_node = of_parse_phandle(np, "devfreq", qidx);
	if (!df_node) {
		dev_err(bcl_dev->device, "devfreq_index %d not found\n", qidx);
		return -ENODEV;
	}
	for_each_child_of_node_scoped(p_np, child) {
		idx = get_idx_from_zone(bcl_dev, child->name);
		if (idx < 0)
			continue;
		if (!bcl_dev->zone[idx]->bcl_qos)
			continue;
		throttle = bcl_dev->zone[idx]->bcl_qos;
		if (throttle->df[qidx])
			continue;

		throttle->df[qidx] = devfreq_get_devfreq_by_node(df_node);
		if (IS_ERR_OR_NULL(throttle->df[qidx])) {
			dev_err(bcl_dev->device, "unable to retrieve devfreq for %d\n", qidx);
			throttle->df[qidx] = NULL;
			goto retry;
		}
		ret = google_pm_qos_add_devfreq_request(throttle->df[qidx],
							&throttle->qos_max[qidx],
							DEV_PM_QOS_MAX_FREQUENCY,
							PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);
		if (ret) {
			throttle->df[qidx] = NULL;
			continue;
		}
		dev_info(bcl_dev->device, "Zone %d QOS %d registered\n", idx, qidx);
	}
	of_node_put(df_node);
	return 0;
retry:
	of_node_put(df_node);
	return -EAGAIN;
}

void google_bcl_setup_qos_work(struct work_struct *work)
{
	int idx;
	struct bcl_device *bcl_dev = container_of(work, struct bcl_device, init_qos_work.work);
	/*
	 * The software mitigation should be initialized and enabled
	 * since QOS initialization is much slower than bcl probe.
	 */
	if (!smp_load_acquire(&bcl_dev->sw_mitigation_enabled)) {
		goto retry;
	}

	for (idx = 0; idx < NON_CPU_CORES_MAX; idx++) {
		if (google_bcl_setup_qos_device(bcl_dev, idx) != 0)
			goto retry;
	}
	if (bcl_dev->init_qos_work.work.func != NULL)
		cancel_delayed_work(&bcl_dev->init_qos_work);
	return;
retry:
	if (bcl_dev->qos_init_count < INIT_GPU_MAX_COUNT) {
		schedule_delayed_work(&bcl_dev->init_qos_work,
				      msecs_to_jiffies(5 * TIMEOUT_1000MS));
		bcl_dev->qos_init_count++;
	}
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
		if (of_property_read_u32_array(child, "gxp",
					       &bcl_dev->zone[idx]->bcl_qos->df_limit[QOS_GXP][0],
					       QOS_PARAM_CNT) != 0) {
			bcl_dev->zone[idx]->bcl_qos->df_limit[QOS_GXP][QOS_LIGHT_IND] = INT_MAX;
			bcl_dev->zone[idx]->bcl_qos->df_limit[QOS_GXP][QOS_HEAVY_IND] = INT_MAX;
		}
		bcl_dev->zone[idx]->bcl_qos->df[QOS_GPU] = NULL;
		bcl_dev->zone[idx]->bcl_qos->df[QOS_TPU] = NULL;
		bcl_dev->zone[idx]->bcl_qos->df[QOS_GXP] = NULL;
	}
	bcl_dev->qos_init_count = 0;
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
			if (bcl_dev->cpu_cluster_on[idx] &&
			    freq_qos_request_active(&qos->cpu_qos_max[idx])) {
				freq_qos_update_request(&qos->cpu_qos_max[idx],
							qos->cpu_freq[idx]);
				trace_bcl_qos_mitigation(idx, "bcl_cpu", qos->cpu_freq[idx]);
			}
		}

		for (idx = 0; idx < NON_CPU_CORES_MAX; idx++) {
			if (qos->df[idx]) {
				dev_pm_qos_update_request(&qos->qos_max[idx], qos->df_freq[idx]);
				trace_bcl_qos_mitigation(idx, "bcl_module", qos->df_freq[idx]);
			}
		}
		if (mutex_lock_interruptible(&bcl_dev->qos_update_lock))
			continue;
		list_del(&data->list);
		kfree(data);
		mutex_unlock(&bcl_dev->qos_update_lock);
	}

}

void google_bcl_qos_update(struct bcl_zone *zone, int throttle_lvl)
{
	struct bcl_device *bcl_dev;
	int cpu0_freq = INT_MAX, cpu1_freq = INT_MAX, cpu2_freq = INT_MAX;
	int tpu_freq = INT_MAX, gpu_freq = INT_MAX, gxp_freq = INT_MAX;

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
	gxp_freq = umin(gxp_freq, zone->bcl_qos->df_limit[QOS_GXP]
			[zone->throttle_lvl - QOS_PARAM_OFFSET]);


end_qos_update:
	zone->bcl_qos->cpu_freq[QOS_CPU0] = cpu0_freq;
	zone->bcl_qos->cpu_freq[QOS_CPU1] = cpu1_freq;
	zone->bcl_qos->cpu_freq[QOS_CPU2] = cpu2_freq;
	zone->bcl_qos->df_freq[QOS_TPU] = tpu_freq;
	zone->bcl_qos->df_freq[QOS_GPU] = gpu_freq;
	zone->bcl_qos->df_freq[QOS_GXP] = gxp_freq;

	add_qos_request(bcl_dev, zone);

	/* Ensure BCL is enabled */
	if (smp_load_acquire(&bcl_dev->sw_mitigation_enabled))
		schedule_delayed_work(&bcl_dev->qos_work, 0);
	trace_qos(zone->throttle_lvl, zone->devname);
}

static int init_freq_qos(struct bcl_device *bcl_dev, struct qos_throttle_limit *throttle)
{
	int idx, ret = 0;

	for (idx = 0; idx < CPU_CORES_MAX; idx++) {
		throttle->cpu_policy[idx] = cpufreq_cpu_get(bcl_dev->cpu_cluster[idx]);
		if (!throttle->cpu_policy[idx]) {
			ret = -EINVAL;
			goto fail;
		}

		bcl_dev->cpu_cluster_on[idx] = true;
		google_pm_qos_add_cpufreq_request(throttle->cpu_policy[idx],
						  &throttle->cpu_qos_max[idx],
						  FREQ_QOS_MAX, INT_MAX);
		cpufreq_cpu_put(throttle->cpu_policy[idx]);
	}

	return 0;
fail:
	dev_err(bcl_dev->device, "Fail to register CPU%d qos\n", idx);
	idx -= 1;
	for (; idx >= 0; idx--) {
		google_pm_qos_remove_cpufreq_request(throttle->cpu_policy[idx],
					 &throttle->cpu_qos_max[idx]);
		bcl_dev->cpu_cluster_on[idx] = false;
	}
	return ret;
}

void google_bcl_remove_qos(struct bcl_device *bcl_dev)
{
	int i, idx;
	struct bcl_zone *zone;
	struct qos_throttle_limit *t;
	struct list_head *n, *pos;

	for (i = 0; i < TRIGGERED_SOURCE_MAX; i++) {
		zone = bcl_dev->zone[i];
		if ((!zone) || (!zone->bcl_qos))
			continue;
		if (!zone->conf_qos)
			continue;
		t = zone->bcl_qos;
		for (idx = 0; idx < CPU_CORES_MAX; idx++) {
			if (bcl_dev->cpu_cluster_on[idx])
				google_pm_qos_remove_cpufreq_request(t->cpu_policy[idx],
								     &t->cpu_qos_max[idx]);
		}
		for (idx = 0; idx < NON_CPU_CORES_MAX; idx++) {
			if (t->df[idx])
				google_pm_qos_remove_devfreq_request(t->df[idx], &t->qos_max[idx]);
		}

		zone->bcl_qos = NULL;
		zone->conf_qos = false;
	}
	list_for_each_safe(pos, n, &qos_data_list) {
		struct qos_data *data = list_entry(pos, struct qos_data, list);

		list_del(&data->list);
		kfree(data);
	}
	if (bcl_dev->qos_work.work.func != NULL)
		cancel_delayed_work_sync(&bcl_dev->qos_work);

	mutex_destroy(&bcl_dev->qos_update_lock);
	if (bcl_dev->init_qos_work.work.func != NULL)
		cancel_delayed_work_sync(&bcl_dev->init_qos_work);
	if (bcl_dev->mailbox_init_work.work.func != NULL)
		cancel_delayed_work(&bcl_dev->mailbox_init_work);
}

static void retry_google_bcl_setup_qos(struct work_struct *work)
{
	struct bcl_device *bcl_dev =
		container_of(work, struct bcl_device, setup_qos_work.work);

	google_bcl_setup_qos(bcl_dev);
}

static int google_bcl_setup_all_zones_qos(struct bcl_device *bcl_dev)
{
	int i, ret;
	struct bcl_zone *zone;

	for (i = 0; i < TRIGGERED_SOURCE_MAX; i++) {
		zone = bcl_dev->zone[i];
		if (!zone || !zone->bcl_qos)
			continue;

		ret = init_freq_qos(bcl_dev, zone->bcl_qos);
		if (ret < 0) {
			dev_err(bcl_dev->device,
				"Failed init QOS %d for cpu, retry=%d\n",
				zone->idx, bcl_dev->retry_setup_qos_count);
			return ret;
		}
		zone->conf_qos = true;
	}

	return 0;
}

int google_bcl_setup_qos(struct bcl_device *bcl_dev)
{
	int ret;

	ret = google_bcl_setup_all_zones_qos(bcl_dev);

	if (ret) {
		if (bcl_dev->retry_setup_qos_count < INIT_CPU_MAX_COUNT) {
			bcl_dev->retry_setup_qos_count++;
			INIT_DELAYED_WORK(&bcl_dev->setup_qos_work,
					  retry_google_bcl_setup_qos);
			schedule_delayed_work(
				&bcl_dev->setup_qos_work,
				msecs_to_jiffies(1 * TIMEOUT_1000MS));
			return 0;
		}

		dev_err(bcl_dev->device,
			"Retry limit exceeded for bcl setup qos\n");
		google_bcl_remove_thermal(bcl_dev);
		dev_err(bcl_dev->device,
			"BCL QOS setup failed, using HW mitigation\n");

		return ret;
	}

	mutex_init(&bcl_dev->qos_update_lock);
	INIT_DELAYED_WORK(&bcl_dev->qos_work, process_qos_request);
	INIT_LIST_HEAD(&qos_data_list);

	return 0;
}
