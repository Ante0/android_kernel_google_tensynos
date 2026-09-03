// SPDX-License-Identifier: GPL-2.0-only
/*
 * google_powercap_devfreq_helper.c driver to register the devfreq nodes.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/static_stub.h>
#include <linux/units.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "google_powercap.h"
#include "google_powercap_devfreq.h"
#include "google_powercap_stats.h"
#include "google_thermal_odpm_helper.h"
#include "perf/core/google_pm_qos.h"

void gpc_devfreq_of_node_put(struct device_node *node)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_devfreq_of_node_put, node);
	of_node_put(node);
}

bool gpc_devfreq_cancel_delayed_work_sync(struct delayed_work *dwork)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_devfreq_cancel_delayed_work_sync, dwork);
	return cancel_delayed_work_sync(dwork);
}

void gpc_warn_on_once(void)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_warn_on_once);
	WARN_ON_ONCE(1);
}

int gpc_cdev_devfreq_init(struct cdev_devfreq_data *cdev, struct device_node *np,
			  enum hw_dev_type cdev_id, cdev_cb success_cb, cdev_cb release_cb)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_cdev_devfreq_init, cdev, np, cdev_id, success_cb,
				   release_cb);
	return cdev_devfreq_init(cdev, np, cdev_id, success_cb, release_cb);
}

void gpc_cdev_pm_qos_update_request(struct cdev_devfreq_data *cdev, unsigned long freq)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_cdev_pm_qos_update_request, cdev, freq);
	cdev_pm_qos_update_request(cdev, freq);
}

static char devfreq_names[HW_CDEV_MAX][20] = {
	[HW_CDEV_GPU] "gpu",
	[HW_CDEV_TPU] "tpu",
	[HW_CDEV_AUR] "aurora",
};

int gpc_devfreq_update_pd_power_uw(struct gpowercap *gpowercap)
{
	struct gpowercap_devfreq *gpowercap_devfreq = to_gpowercap_devfreq(gpowercap);
	struct cdev_devfreq_data *cdev = &gpowercap_devfreq->cdev;

	if (!cdev->devfreq)
		return -ENODEV;

	gpowercap->power_min = cdev->opp_table[0].power;
	gpowercap->power_max = cdev->opp_table[cdev->num_opps - 1].power;
	gpowercap->power_limit = gpowercap->power_max;
	gpowercap->num_opps = cdev->num_opps;
	gpowercap_devfreq->target_opp_idx = cdev->num_opps - 1;
	gpowercap_devfreq->util_percentage = 100;
	gpowercap->opp_table = cdev->opp_table;
	pr_debug("Devfreq:%s powercap min power:%llu max power:%llu\n",
		 dev_name(&cdev->devfreq->dev), gpowercap->power_min, gpowercap->power_max);

	return 0;
}

static void __gpc_devfreq_apply_limit(struct gpowercap_devfreq *gpowercap_devfreq)
{
	unsigned long target_freq;
	int i, last_opp_idx;
	u64 estimated_power_uw;

	last_opp_idx = gpowercap_devfreq->target_opp_idx;

	/* No mitigation needed, or cap is being removed */
	if (gpowercap_devfreq->gpowercap.power_limit >= gpowercap_devfreq->gpowercap.power_max) {
		if (last_opp_idx == gpowercap_devfreq->cdev.num_opps - 1)
			return;
		gpowercap_devfreq->target_opp_idx = gpowercap_devfreq->cdev.num_opps - 1;
		target_freq =
		gpowercap_devfreq->cdev.opp_table[gpowercap_devfreq->target_opp_idx].freq;
		gpc_cdev_pm_qos_update_request(&gpowercap_devfreq->cdev, target_freq);
		gpc_stats_update(&gpowercap_devfreq->gpowercap, GPC_STAT_QOS, target_freq);
		gpc_stats_update(&gpowercap_devfreq->gpowercap, GPC_STAT_POWER_LIMIT,
				 gpowercap_devfreq->gpowercap.power_limit);
		pr_debug("Devfreq:%s Mitigation is removed.\n",
			 dev_name(&gpowercap_devfreq->cdev.devfreq->dev));
		return;
	}

	/* Find new frequency based on adjusted power target */
	for (i = 1; i < gpowercap_devfreq->cdev.num_opps; i++) {
		estimated_power_uw = div64_u64(
			gpowercap_devfreq->cdev.opp_table[i].power *
			gpowercap_devfreq->util_percentage, 100);

		if (estimated_power_uw > gpowercap_devfreq->gpowercap.power_limit)
			break;
	}
	gpowercap_devfreq->target_opp_idx = i - 1;
	target_freq = gpowercap_devfreq->cdev.opp_table[gpowercap_devfreq->target_opp_idx].freq;

	if (last_opp_idx != gpowercap_devfreq->target_opp_idx) {
		pr_debug("%s PL:%llu util:%llu opp:%d => %d freq:%u => %lu\n",
			 dev_name(&gpowercap_devfreq->cdev.devfreq->dev),
			 gpowercap_devfreq->gpowercap.power_limit,
			 gpowercap_devfreq->util_percentage, last_opp_idx,
			 gpowercap_devfreq->target_opp_idx,
			 gpowercap_devfreq->cdev.opp_table[last_opp_idx].freq,
			 target_freq);
		gpc_cdev_pm_qos_update_request(&gpowercap_devfreq->cdev, target_freq);
		gpc_stats_update(&gpowercap_devfreq->gpowercap, GPC_STAT_QOS, target_freq);
		gpc_stats_update(&gpowercap_devfreq->gpowercap, GPC_STAT_POWER_LIMIT,
				 gpowercap_devfreq->gpowercap.power_limit);
	}
}

u64 gpc_devfreq_set_pd_power_limit(struct gpowercap *gpowercap, u64 power_limit)
{
	struct gpowercap_devfreq *gpowercap_devfreq = to_gpowercap_devfreq(gpowercap);
	struct devfreq *devfreq = gpowercap_devfreq->cdev.devfreq;

	if (!devfreq)
		return 0;

	gpowercap->power_limit = power_limit;
	__gpc_devfreq_apply_limit(gpowercap_devfreq);

	return power_limit;
}

u64 gpc_devfreq_get_pd_power_uw(struct gpowercap *gpowercap)
{
	struct gpowercap_devfreq *gpowercap_devfreq = to_gpowercap_devfreq(gpowercap);
	struct devfreq *devfreq = gpowercap_devfreq->cdev.devfreq;
	struct devfreq_dev_status status;
	unsigned long freq;
	int i;

	if (gpowercap_devfreq->last_power_uw)
		return gpowercap_devfreq->last_power_uw;

	if (!devfreq)
		return 0;

	mutex_lock(&devfreq->lock);
	status = devfreq->last_status;
	mutex_unlock(&devfreq->lock);

	freq = DIV_ROUND_UP(status.current_frequency, HZ_PER_KHZ);

	for (i = 0; i < gpowercap->num_opps; i++) {
		if (gpowercap->opp_table[i].freq < freq)
			continue;

		pr_debug("Devfreq:%s freq:%u power:%uuw\n", dev_name(&devfreq->dev),
			 gpowercap->opp_table[i].freq, gpowercap->opp_table[i].power);
		return gpowercap->opp_table[i].power;
	}

	return 0;
}

void gpc_devfreq_pd_release(struct gpowercap *gpowercap)
{
	struct gpowercap_devfreq *gpowercap_devfreq = to_gpowercap_devfreq(gpowercap);

	if (gpowercap_devfreq->odpm_rail_name) {
		if (gpowercap_devfreq->gpowercap.time_window_us)
			gpc_google_thermal_odpm_unregister_client(
				gpowercap_devfreq->odpm_rail_name,
				&gpowercap_devfreq->odpm_nb,
				gpowercap_devfreq->gpowercap.time_window_us / 1000);
		kfree(gpowercap_devfreq->odpm_rail_name);
		gpc_devfreq_cancel_delayed_work_sync(&gpowercap_devfreq->odpm_work);
	}

	cdev_devfreq_exit(&gpowercap_devfreq->cdev);
	kfree(gpowercap_devfreq);
}

VISIBLE_IF_KUNIT void __gpc_devfreq_odpm_work_func(struct work_struct *work)
{
	struct gpowercap_devfreq *gpowercap_devfreq =
		container_of(work, struct gpowercap_devfreq, odpm_work.work);
	u64 expected_power_uw;

	mutex_lock(&gpowercap_devfreq->gpowercap.lock);
	if (!gpowercap_devfreq->gpowercap.opp_table) {
		mutex_unlock(&gpowercap_devfreq->gpowercap.lock);
		return;
	}

	expected_power_uw =
		gpowercap_devfreq->cdev.opp_table[gpowercap_devfreq->target_opp_idx].power;

	if (gpowercap_devfreq->last_power_uw > 0)
		gpowercap_devfreq->util_percentage = MAX(1UL,
			div64_u64(gpowercap_devfreq->last_power_uw * 100, expected_power_uw));
	else
		gpowercap_devfreq->util_percentage = GPOWERCAP_DEVFREQ_UTIL_PERCENTAGE_INITIAL;
	mutex_unlock(&gpowercap_devfreq->gpowercap.lock);

	gpc_stats_update(&gpowercap_devfreq->gpowercap, GPC_STAT_UTIL,
				gpowercap_devfreq->util_percentage);

	pr_debug("Devfreq:%s PL:%llu Expected_P:%llu Actual_P:%llu util:%llu\n",
		 dev_name(&gpowercap_devfreq->cdev.devfreq->dev),
		 gpowercap_devfreq->gpowercap.power_limit, expected_power_uw,
		 gpowercap_devfreq->last_power_uw, gpowercap_devfreq->util_percentage);

	/* We just report the power to the gpowercap core. The power limit will be reevaluated
	 * as a part of the reporting and new power limit will be applied.
	 */
	gpowercap_report_power_uw(&gpowercap_devfreq->gpowercap, gpowercap_devfreq->last_power_uw);
}

static
int __gpc_devfreq_odpm_notifier_call(struct notifier_block *nb, unsigned long event, void *data)
{
	struct gpowercap_devfreq *gpowercap_devfreq =
		container_of(nb, struct gpowercap_devfreq, odpm_nb);
	u64 power_uw = (uintptr_t)data;

	gpowercap_devfreq->last_power_uw = power_uw;
	mod_delayed_work(system_highpri_wq, &gpowercap_devfreq->odpm_work, 0);

	return NOTIFY_OK;
}

VISIBLE_IF_KUNIT
int __gpc_devfreq_set_time_window_us(struct gpowercap *gpowercap, u64 time_window_us)
{
	struct gpowercap_devfreq *gpowercap_devfreq = to_gpowercap_devfreq(gpowercap);
	int ret = 0;

	if (!gpowercap_devfreq->odpm_rail_name)
		return -EOPNOTSUPP;

	if (gpowercap_devfreq->gpowercap.time_window_us == time_window_us)
		return 0;

	if (gpowercap->time_window_us) {
		ret = gpc_google_thermal_odpm_unregister_client(gpowercap_devfreq->odpm_rail_name,
							    &gpowercap_devfreq->odpm_nb,
							    gpowercap->time_window_us / 1000);
		if (ret) {
			pr_err("Failed to unregister ODPM client for %s: %d\n",
			       dev_name(&gpowercap_devfreq->cdev.devfreq->dev), ret);
			return ret;
		}
	}
	mutex_lock(&gpowercap->lock);
	gpowercap_devfreq->last_power_uw = 0;
	gpowercap_devfreq->util_percentage = 100;
	if (time_window_us) {
		ret = gpc_google_thermal_odpm_register_client(gpowercap_devfreq->odpm_rail_name,
							  &gpowercap_devfreq->odpm_nb,
							  time_window_us / 1000);
		if (ret) {
			pr_err("Failed to register ODPM client for %s: %d\n",
			       dev_name(&gpowercap_devfreq->cdev.devfreq->dev), ret);
			gpowercap->time_window_us = 0;
			goto set_time_window_exit;
		}
	} else {
		__gpc_devfreq_apply_limit(gpowercap_devfreq);
	}
	gpowercap->time_window_us = time_window_us;

set_time_window_exit:
	mutex_unlock(&gpowercap->lock);
	return ret;
}

static struct gpowercap_ops gpc_devfreq_ops = {
	.set_power_uw = gpc_devfreq_set_pd_power_limit,
	.get_power_uw = gpc_devfreq_get_pd_power_uw,
	.update_power_uw = gpc_devfreq_update_pd_power_uw,
	.release = gpc_devfreq_pd_release,
	.set_time_window_us = __gpc_devfreq_set_time_window_us,
};

static void __gpc_devfreq_cdev_success(struct cdev_devfreq_data *cdev)
{
	struct gpowercap_devfreq *gpowercap_devfreq =
		container_of(cdev, struct gpowercap_devfreq, cdev);

	gpowercap_update_power(&gpowercap_devfreq->gpowercap);
}

static void __gpc_devfreq_cdev_exit(struct cdev_devfreq_data *cdev)
{
	struct gpowercap_devfreq *gpowercap_devfreq =
		container_of(cdev, struct gpowercap_devfreq, cdev);

	gpowercap_unregister(&gpowercap_devfreq->gpowercap);
	gpc_warn_on_once();
}

int __gpc_devfreq_setup(struct gpowercap *parent, struct device_node *np, enum hw_dev_type cdev_id,
			const char *odpm_rail_name, u64 polling_interval_ms)
{
	struct gpowercap_devfreq *gpowercap_devfreq;
	int ret = 0;

	if (cdev_id >= HW_CDEV_MAX) {
		pr_err("Invalid cdev ID:%u. Node:%s\n", cdev_id, np->name);
		return -EINVAL;
	}

	gpowercap_devfreq = kzalloc(sizeof(*gpowercap_devfreq), GFP_KERNEL);
	if (!gpowercap_devfreq)
		return -ENOMEM;

	gpowercap_init(&gpowercap_devfreq->gpowercap, &gpc_devfreq_ops);
	if (odpm_rail_name) {
		gpowercap_devfreq->odpm_rail_name = kstrdup(odpm_rail_name, GFP_KERNEL);
		if (!gpowercap_devfreq->odpm_rail_name) {
			kfree(gpowercap_devfreq);
			return -ENOMEM;
		}
	}

	gpowercap_devfreq->gpowercap.time_window_us = polling_interval_ms * 1000;
	gpowercap_devfreq->odpm_nb.notifier_call = __gpc_devfreq_odpm_notifier_call;
	INIT_DEFERRABLE_WORK(&gpowercap_devfreq->odpm_work, __gpc_devfreq_odpm_work_func);

	ret = gpc_cdev_devfreq_init(&gpowercap_devfreq->cdev, np, cdev_id,
				    __gpc_devfreq_cdev_success, __gpc_devfreq_cdev_exit);
	if (ret) {
		pr_err("Setup error. node:%s ret:%d.\n", np->name, ret);
		goto out_free_gpc_devfreq;
	}

	ret = gpowercap_register(devfreq_names[cdev_id], &gpowercap_devfreq->gpowercap, parent);
	if (ret) {
		pr_err("Failed to register. node:%s ret:%d\n", np->name, ret);
		cdev_devfreq_exit(&gpowercap_devfreq->cdev);
		goto out_free_gpc_devfreq;
	}

	if (gpowercap_devfreq->odpm_rail_name && polling_interval_ms) {
		ret = gpc_google_thermal_odpm_register_client(gpowercap_devfreq->odpm_rail_name,
							      &gpowercap_devfreq->odpm_nb,
							      polling_interval_ms);
		if (ret) {
			pr_err("Failed to register ODPM client for %s: %d\n",
				devfreq_names[cdev_id], ret);
			goto out_gpowercap_unregister;
		}
	}

	return 0;

out_gpowercap_unregister:
	gpowercap_unregister(&gpowercap_devfreq->gpowercap);
	return ret;

out_free_gpc_devfreq:
	kfree(gpowercap_devfreq->odpm_rail_name);
	kfree(gpowercap_devfreq);
	return ret;
}
