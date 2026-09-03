// SPDX-License-Identifier: GPL-2.0-only
/*
 * google_powercap_cpu_helper.c driver providing helper for powercap CPU.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <linux/math64.h>

#include "cdev_cpufreq_helper.h"
#include "google_powercap.h"
#include "google_powercap_cpu.h"
#include "google_powercap_stats.h"
#include "perf/core/google_pm_qos.h"

int gpc_freq_qos_update_request(struct freq_qos_request *req, s32 new_value)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_freq_qos_update_request, req, new_value);
	return freq_qos_update_request(req, new_value);
}

unsigned int gpc_cpufreq_quick_get(unsigned int cpu)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_cpufreq_quick_get, cpu);
	return cpufreq_quick_get(cpu);
}

struct cpufreq_policy *gpc_cpufreq_cpu_get(unsigned int cpu)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_cpufreq_cpu_get, cpu);
	return cpufreq_cpu_get(cpu);
}

int gpc_google_pm_qos_remove_cpufreq_request(struct cpufreq_policy *policy,
					     struct freq_qos_request *req)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_google_pm_qos_remove_cpufreq_request, policy, req);
	return google_pm_qos_remove_cpufreq_request(policy, req);
}

void gpc_cpufreq_cpu_put(struct cpufreq_policy *policy)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_cpufreq_cpu_put, policy);
	cpufreq_cpu_put(policy);
}

int gpc_google_pm_qos_add_cpufreq_request(struct cpufreq_policy *policy,
					  struct freq_qos_request *req, enum freq_qos_req_type type,
					  s32 value)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_google_pm_qos_add_cpufreq_request, policy, req, type, value);
	return google_pm_qos_add_cpufreq_request(policy, req, type, value);
}

int gpc_cdev_cpufreq_get_opp_count(unsigned int cpu)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_cdev_cpufreq_get_opp_count, cpu);
	return cdev_cpufreq_get_opp_count(cpu);
}

int gpc_cdev_cpufreq_update_opp_table(unsigned int cpu, enum hw_dev_type cdev_id,
				      struct cdev_opp_table *cdev_table, unsigned int num_opp)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_cdev_cpufreq_update_opp_table, cpu, cdev_id, cdev_table,
				   num_opp);
	return cdev_cpufreq_update_opp_table(cpu, cdev_id, cdev_table, num_opp);
}

bool gpc_cancel_delayed_work_sync(struct delayed_work *dwork)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_cancel_delayed_work_sync, dwork);
	return cancel_delayed_work_sync(dwork);
}

LIST_HEAD(gpowercap_cpu_list);

static void __gpc_cpu_apply_limit(struct gpowercap_cpu *gpowercap_cpu)
{
	unsigned long target_freq;
	int i, last_opp_idx;
	u64 estimated_power_uw;

	last_opp_idx = gpowercap_cpu->target_opp_idx;

	/* No mitigation needed, or cap is being removed */
	if (gpowercap_cpu->power_limit >= gpowercap_cpu->gpowercap.power_max) {
		if (last_opp_idx == gpowercap_cpu->num_opps - 1)
			return;
		gpowercap_cpu->target_opp_idx = gpowercap_cpu->num_opps - 1;
		target_freq = gpowercap_cpu->opp_table[gpowercap_cpu->target_opp_idx].freq;
		gpc_freq_qos_update_request(&gpowercap_cpu->qos_req, target_freq);
		gpc_stats_update(&gpowercap_cpu->gpowercap, GPC_STAT_QOS, target_freq);
		gpc_stats_update(&gpowercap_cpu->gpowercap, GPC_STAT_POWER_LIMIT,
				 gpowercap_cpu->power_limit);
		pr_debug("CPU:%d Mitigation is removed.\n", gpowercap_cpu->cpu);
		return;
	}

	/* Find new frequency based on adjusted power target */
	for (i = 1; i < gpowercap_cpu->num_opps; i++) {
		estimated_power_uw = div64_u64(
			gpowercap_cpu->opp_table[i].power * gpowercap_cpu->util_percentage, 100);

		if (estimated_power_uw > gpowercap_cpu->power_limit)
			break;
	}
	gpowercap_cpu->target_opp_idx = i - 1;
	target_freq = gpowercap_cpu->opp_table[gpowercap_cpu->target_opp_idx].freq;

	if (last_opp_idx != gpowercap_cpu->target_opp_idx) {
		pr_debug("CPU:%d PL:%llu util:%llu opp:%d => %d freq:%u => %lu\n",
			gpowercap_cpu->cpu, gpowercap_cpu->power_limit,
			gpowercap_cpu->util_percentage,
			last_opp_idx, gpowercap_cpu->target_opp_idx,
			gpowercap_cpu->opp_table[last_opp_idx].freq,
			target_freq);
		gpc_freq_qos_update_request(&gpowercap_cpu->qos_req, target_freq);
		gpc_stats_update(&gpowercap_cpu->gpowercap, GPC_STAT_QOS, target_freq);
		gpc_stats_update(&gpowercap_cpu->gpowercap, GPC_STAT_POWER_LIMIT,
				 gpowercap_cpu->power_limit);
	}
}

u64 __gpc_cpu_set_cluster_power_limit(struct gpowercap *gpowercap, u64 power_limit)
{
	struct gpowercap_cpu *gpowercap_cpu = to_gpowercap_cpu(gpowercap);

	gpowercap_cpu->power_limit = power_limit;
	__gpc_cpu_apply_limit(gpowercap_cpu);

	return power_limit;
}

u64 __gpc_cpu_get_cluster_power_uw(struct gpowercap *gpowercap)
{
	struct gpowercap_cpu *gpowercap_cpu = to_gpowercap_cpu(gpowercap);
	unsigned long freq;
	int i;

	if (gpowercap_cpu->last_power_uw)
		return gpowercap_cpu->last_power_uw;

	freq = gpc_cpufreq_quick_get(gpowercap_cpu->cpu);

	for (i = 0; i < gpowercap_cpu->num_opps; i++) {
		if (gpowercap_cpu->opp_table[i].freq < freq)
			continue;

		pr_debug("CPU:%d freq:%u power:%uuw\n", gpowercap_cpu->cpu,
			 gpowercap_cpu->opp_table[i].freq, gpowercap_cpu->opp_table[i].power);
		return gpowercap_cpu->opp_table[i].power;
	}

	return 0;
}

int __gpc_cpu_update_cluster_power_uw(struct gpowercap *gpowercap)
{
	struct gpowercap_cpu *gpowercap_cpu = to_gpowercap_cpu(gpowercap);

	gpowercap->power_min = gpowercap_cpu->opp_table[0].power;
	gpowercap->power_max = gpowercap_cpu->opp_table[gpowercap_cpu->num_opps - 1].power;
	gpowercap->power_limit = gpowercap->power_max;
	gpowercap_cpu->power_limit = gpowercap->power_max;
	gpowercap->num_opps = gpowercap_cpu->num_opps;
	gpowercap->opp_table = gpowercap_cpu->opp_table;
	gpowercap_cpu->target_opp_idx = gpowercap_cpu->num_opps - 1;
	gpowercap_cpu->util_percentage = 100;
	pr_debug("CPU%d powercap min power:%llu max power:%llu\n", gpowercap_cpu->cpu,
		 gpowercap->power_min, gpowercap->power_max);

	return 0;
}

void __gpc_cpu_pd_release(struct gpowercap *gpowercap)
{
	struct gpowercap_cpu *gpowercap_cpu = to_gpowercap_cpu(gpowercap);
	struct cpufreq_policy *policy;

	if (!gpowercap_cpu) {
		pr_err("Invalid gpowercap CPU node.\n");
		return;
	}
	pr_info("Releasing QOS request for CPU:%d", gpowercap_cpu->cpu);

	if (gpowercap_cpu->odpm_rail_name) {
		if (gpowercap_cpu->gpowercap.time_window_us)
			gpc_google_thermal_odpm_unregister_client(gpowercap_cpu->odpm_rail_name,
					&gpowercap_cpu->odpm_nb,
					gpowercap_cpu->gpowercap.time_window_us / 1000);
		gpc_cancel_delayed_work_sync(&gpowercap_cpu->odpm_work);
	}

	if (freq_qos_request_active(&gpowercap_cpu->qos_req)) {
		policy = gpc_cpufreq_cpu_get(gpowercap_cpu->cpu);
		if (policy) {
			gpc_google_pm_qos_remove_cpufreq_request(policy, &gpowercap_cpu->qos_req);
			gpc_cpufreq_cpu_put(policy);
		} else
			pr_warn("CPU:%d cpufreq policy get error. QOS unregister skip.\n",
				gpowercap_cpu->cpu);
	}

	list_del(&gpowercap_cpu->node);
	kfree(gpowercap_cpu->opp_table);
	kfree(gpowercap_cpu);
}

VISIBLE_IF_KUNIT int __gpc_cpu_set_time_window_us(struct gpowercap *gpowercap, u64 time_window_us)
{
	struct gpowercap_cpu *gpowercap_cpu = to_gpowercap_cpu(gpowercap);
	int ret = 0;

	if (!gpowercap_cpu->odpm_rail_name)
		return -EOPNOTSUPP;

	if (gpowercap_cpu->gpowercap.time_window_us == time_window_us)
		return 0;

	if (gpowercap->time_window_us) {
		ret = gpc_google_thermal_odpm_unregister_client(gpowercap_cpu->odpm_rail_name,
							    &gpowercap_cpu->odpm_nb,
							    gpowercap->time_window_us / 1000);
		if (ret) {
			pr_err("Failed to unregister ODPM client for CPU%d: %d\n",
			       gpowercap_cpu->cpu, ret);
			return ret;
		}
	}
	mutex_lock(&gpowercap->lock);
	gpowercap_cpu->last_power_uw = 0;
	gpowercap_cpu->util_percentage = 100;
	if (time_window_us) {
		ret = gpc_google_thermal_odpm_register_client(gpowercap_cpu->odpm_rail_name,
							  &gpowercap_cpu->odpm_nb,
							  time_window_us / 1000);
		if (ret) {
			pr_err("Failed to register ODPM client for CPU%d: %d\n",
			       gpowercap_cpu->cpu, ret);
			ret = gpc_google_thermal_odpm_register_client(gpowercap_cpu->odpm_rail_name,
								&gpowercap_cpu->odpm_nb,
								gpowercap->time_window_us / 1000);
			if (ret)
				pr_err("Failed to register ODPM client for CPU%d: %d\n",
				       gpowercap_cpu->cpu, ret);
			gpowercap->time_window_us = 0;
			goto set_time_window_exit;
		}
	} else {
		__gpc_cpu_apply_limit(gpowercap_cpu);
	}
	gpowercap->time_window_us = time_window_us;

set_time_window_exit:
	mutex_unlock(&gpowercap->lock);
	return ret;
}

VISIBLE_IF_KUNIT void __gpc_cpu_odpm_work_func(struct work_struct *work)
{
	struct gpowercap_cpu *gpowercap_cpu =
		container_of(work, struct gpowercap_cpu, odpm_work.work);
	u64 expected_power_uw;

	mutex_lock(&gpowercap_cpu->gpowercap.lock);
	expected_power_uw = gpowercap_cpu->opp_table[gpowercap_cpu->target_opp_idx].power;

	if (gpowercap_cpu->last_power_uw > 0)
		gpowercap_cpu->util_percentage = MAX(1UL,
			div64_u64(gpowercap_cpu->last_power_uw * 100, expected_power_uw));
	else
		gpowercap_cpu->util_percentage = GPOWERCAP_CPU_UTIL_PERCENTAGE_INITIAL;
	mutex_unlock(&gpowercap_cpu->gpowercap.lock);

	gpc_stats_update(&gpowercap_cpu->gpowercap, GPC_STAT_UTIL, gpowercap_cpu->util_percentage);

	pr_debug("CPU:%d PL:%llu Expected_P:%llu Actual_P:%llu util:%llu\n",
		gpowercap_cpu->cpu, gpowercap_cpu->gpowercap.power_limit,
		expected_power_uw, gpowercap_cpu->last_power_uw, gpowercap_cpu->util_percentage);

	/* We just report the power to the gpowercap core. The power limit will be reevaluated
	 * as a part of the reporting and new power limit will be applied.
	 */
	gpowercap_report_power_uw(&gpowercap_cpu->gpowercap, gpowercap_cpu->last_power_uw);
}

static int __gpc_cpu_odpm_notifier_call(struct notifier_block *nb, unsigned long event, void *data)
{
	struct gpowercap_cpu *gpowercap_cpu = container_of(nb, struct gpowercap_cpu, odpm_nb);
	u64 power_uw = (uintptr_t)data;

	gpowercap_cpu->last_power_uw = power_uw;
	mod_delayed_work(system_highpri_wq, &gpowercap_cpu->odpm_work, 0);

	return NOTIFY_OK;
}

static struct gpowercap_ops gpowercap_cpu_ops = {
	.set_power_uw = __gpc_cpu_set_cluster_power_limit,
	.get_power_uw = __gpc_cpu_get_cluster_power_uw,
	.update_power_uw = __gpc_cpu_update_cluster_power_uw,
	.release = __gpc_cpu_pd_release,
	.set_time_window_us = __gpc_cpu_set_time_window_us,
};

int __gpc_cpu_setup(int cpu, struct gpowercap *parent, enum hw_dev_type cdev_id,
		    const char *odpm_rail_name, u64 polling_interval_ms)
{
	struct gpowercap_cpu *gpowercap_cpu;
	struct cpufreq_policy *policy;
	char name[CPUFREQ_NAME_LEN];
	int ret = 0;
	struct list_head *pos;

	policy = gpc_cpufreq_cpu_get(cpu);
	if (!policy)
		return -ENODEV;

	list_for_each(pos, &gpowercap_cpu_list) {
		struct gpowercap_cpu *p_cpu = list_entry(pos, struct gpowercap_cpu, node);

		if (cpumask_test_cpu(p_cpu->cpu, policy->related_cpus)) {
			pr_warn("powercap for cpu:[%d] already initialized.\n", cpu);
			goto release_policy;
		}
	}

	gpowercap_cpu = kzalloc(sizeof(*gpowercap_cpu), GFP_KERNEL);
	if (!gpowercap_cpu) {
		ret = -ENOMEM;
		goto release_policy;
	}

	gpowercap_init(&gpowercap_cpu->gpowercap, &gpowercap_cpu_ops);
	gpowercap_cpu->cpu = cpu;
	gpowercap_cpu->odpm_rail_name = odpm_rail_name;
	gpowercap_cpu->gpowercap.time_window_us = polling_interval_ms * 1000;
	gpowercap_cpu->odpm_nb.notifier_call = __gpc_cpu_odpm_notifier_call;
	INIT_DEFERRABLE_WORK(&gpowercap_cpu->odpm_work, __gpc_cpu_odpm_work_func);
	cpumask_copy(&gpowercap_cpu->related_cpus, policy->related_cpus);

	ret = gpc_cdev_cpufreq_get_opp_count(cpu);
	if (ret <= 0) {
		ret = ret ?: -ENODEV;
		goto out_kfree_gpowercap_cpu;
	}
	gpowercap_cpu->num_opps = ret;
	gpowercap_cpu->opp_table =
		kcalloc(gpowercap_cpu->num_opps, sizeof(*gpowercap_cpu->opp_table), GFP_KERNEL);
	if (!gpowercap_cpu->opp_table) {
		ret = -ENOMEM;
		goto out_kfree_gpowercap_cpu;
	}
	ret = gpc_cdev_cpufreq_update_opp_table(cpu, cdev_id, gpowercap_cpu->opp_table,
						gpowercap_cpu->num_opps);
	if (ret)
		goto out_kfree_power_table;

	snprintf(name, sizeof(name), "cpufreq-cpu%d", gpowercap_cpu->cpu);

	ret = gpowercap_register(name, &gpowercap_cpu->gpowercap, parent);
	if (ret)
		goto out_kfree_power_table;

	if (gpowercap_cpu->odpm_rail_name && polling_interval_ms) {
		ret = gpc_google_thermal_odpm_register_client(gpowercap_cpu->odpm_rail_name,
							  &gpowercap_cpu->odpm_nb,
							  polling_interval_ms);
		if (ret) {
			pr_err("Failed to register ODPM client for CPU%d: %d\n", cpu, ret);
			goto out_gpowercap_unregister;
		}
	}

	ret = gpc_google_pm_qos_add_cpufreq_request(
		policy, &gpowercap_cpu->qos_req, FREQ_QOS_MAX,
		gpowercap_cpu->opp_table[gpowercap_cpu->num_opps - 1].freq);
	if (ret < 0) {
		pr_err("QOS request init error:%d\n", ret);
		goto out_odpm_unregister;
	}

	gpc_cpufreq_cpu_put(policy);
	list_add_tail(&gpowercap_cpu->node, &gpowercap_cpu_list);

	return 0;

out_odpm_unregister:
	if (gpowercap_cpu->odpm_rail_name && gpowercap_cpu->gpowercap.time_window_us)
		gpc_google_thermal_odpm_unregister_client(gpowercap_cpu->odpm_rail_name,
						      &gpowercap_cpu->odpm_nb,
						      gpowercap_cpu->gpowercap.time_window_us /
							      1000);
out_gpowercap_unregister:
	gpowercap_unregister(&gpowercap_cpu->gpowercap);
out_kfree_power_table:
	kfree(gpowercap_cpu->opp_table);
out_kfree_gpowercap_cpu:
	kfree(gpowercap_cpu);

release_policy:
	gpc_cpufreq_cpu_put(policy);
	return ret;
}
