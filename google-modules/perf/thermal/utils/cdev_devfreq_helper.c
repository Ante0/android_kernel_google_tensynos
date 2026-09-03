// SPDX-License-Identifier: GPL-2.0-only
/*
 * cdev_devfreq_helper.c Helper functions for devfreq cooling devices.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/static_stub.h>
#include <linux/units.h>

#include "cdev_devfreq_helper.h"

void __cdev_devfreq_fetch_work(struct work_struct *work)
{
	struct cdev_devfreq_data *cdev = container_of(work, struct cdev_devfreq_data, work.work);
	int ret = 0;

	ret = __cdev_devfreq_fetch_register(cdev);
	if (ret) {
		if (ret == -EAGAIN) {
			if (cdev->retry_ct++ < CDEV_PROBE_MAX_RETRY_CT) {
				schedule_delayed_work(&cdev->work,
						      msecs_to_jiffies(CDEV_PROBE_LOOP_DELAY_MS));
				pr_debug("Devfreq not available yet for node:%s. retry ct:%d\n",
					 of_node_full_name(cdev->np), cdev->retry_ct);
				return;
			}
			pr_err("Devfreq[%s] not available. Max retry count reached.\n",
			       of_node_full_name(cdev->np));
		}
		cdev->release_cb(cdev);
		return;
	}

	cdev->success_cb(cdev);
}

int __cdev_devfreq_update_opp_from_pd(struct cdev_devfreq_data *cdev,
				     struct device *dev)
{
	int i = 0, ret = 0;
	struct em_perf_state *table;
	struct em_perf_domain *pd;

	pd = em_pd_get(dev);
	if (!pd) {
		ret = dev_pm_opp_of_register_em(dev, NULL);
		if (ret) {
			pr_err("No energy model available for '%s' in devicetree.\n",
			       dev_name(dev));
			return ret;
		}
		pd = em_pd_get(dev);
	}
	if (!pd || !pd->nr_perf_states)
		return -ENODATA;

	cdev->num_opps = pd->nr_perf_states;
	cdev->opp_table = kcalloc(pd->nr_perf_states, sizeof(*cdev->opp_table), GFP_KERNEL);
	if (!cdev->opp_table)
		return -ENOMEM;

	rcu_read_lock();
	table = cdev_em_perf_state_from_pd(pd);
	for (i = 0; i < pd->nr_perf_states; i++) {
		cdev->opp_table[i].freq = table[i].frequency;
		cdev->opp_table[i].power = table[i].power;
		dev_dbg(dev,
			"lvl:%d devfreq:%s freq:%u power:%uuw\n",
			i, dev_name(dev),
			cdev->opp_table[i].freq,
			cdev->opp_table[i].power);
	}
	rcu_read_unlock();

	return 0;
}

int __cdev_devfreq_update_opp_from_firmware(struct cdev_devfreq_data *cdev,
					   struct device *dev)
{
	int i, opp_ct, power, j, ret;
	unsigned long freq;
	struct dev_pm_opp *opp;

	/* We get the freq from the devfreq OPP table, since the devfreq client driver
	 * also gets this information from firmware.
	 */
	opp_ct = cdev_dev_pm_opp_get_opp_count(dev);
	if (opp_ct <= 0) {
		dev_err(dev, "No OPP table.\n");
		return -EINVAL;
	}

	cdev->num_opps = opp_ct;
	cdev->opp_table = kcalloc(opp_ct, sizeof(*cdev->opp_table),
					       GFP_KERNEL);
	if (!cdev->opp_table)
		return -ENOMEM;

	for (i = 0, freq = 0; i < opp_ct; i++, freq++) {
		opp = cdev_dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp))
			return PTR_ERR(opp);
		cdev_dev_pm_opp_put(opp);
		cdev->opp_table[i].freq = freq;
		dev_dbg(dev, "idx:%d freq:%lu\n", i, freq);
	}

	/* Now get the power number for the OPP levels. */
	ret = cdev_msg_tmu_get_power_table(cdev->cdev_id, 0, &power, &opp_ct);
	if (ret) {
		dev_err(dev, "fetching power table error:%d.\n", ret);
		return ret;
	}
	// Firmware gives the max cdev state as output. Increment to get the max OPP levels.
	opp_ct++;
	if (opp_ct < cdev->num_opps) {
		dev_err(dev, "Invalid OPP count %d in firmware.\n", opp_ct);
		return -EINVAL;
	}
	for (i = 0, j = cdev->num_opps - 1; j >= 0; i++, j--) {
		ret = cdev_msg_tmu_get_power_table(cdev->cdev_id, i,
						  &power, NULL);
		if (ret) {
			dev_err(dev, "Error fetching power for OPP idx:%d. ret = %d\n.",
				i, ret);
			return ret;
		}
		dev_dbg(dev, "idx:%d power:%d\n", j, power);
		cdev->opp_table[j].power = power * MICROWATT_PER_MILLIWATT;
	}

	return 0;
}

int __cdev_devfreq_fetch_register(struct cdev_devfreq_data *cdev)
{
	int ret = 0;
	struct devfreq *devfreq;
	struct device *dev;

	devfreq = cdev_get_devfreq_by_node(cdev->np);
	if (IS_ERR(devfreq))
		return -EAGAIN; // The devfreq hasn't probed yet. check later.

	dev = devfreq->dev.parent;
	ret = __cdev_devfreq_update_opp_from_firmware(cdev, dev);
	if (ret)
		ret = __cdev_devfreq_update_opp_from_pd(cdev, dev);

	if (ret) {
		dev_err(dev, "No OPP entry available.\n");
		ret = -ENODATA;
		goto out_cdev_devfreq_unregister;
	}

	ret = cdev_pm_qos_add_devfreq_request(devfreq,
					      &cdev->qos_req,
					      DEV_PM_QOS_MAX_FREQUENCY,
					      PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);
	if (ret) {
		pr_err("Failed to add QoS request: %d\n", ret);
		goto out_cdev_devfreq_unregister;
	}
	cdev->devfreq = devfreq;

out_cdev_devfreq_unregister:
	return ret;
}

int cdev_devfreq_init(struct cdev_devfreq_data *cdev,
		      struct device_node *np,
		      enum hw_dev_type cdev_id,
		      cdev_cb success_cb,
		      cdev_cb release_cb)
{
	if (!cdev || !np || cdev_id >= HW_CDEV_MAX || !success_cb ||
	    !release_cb)
		return -EINVAL;

	if (cdev->num_opps && cdev->opp_table && cdev->devfreq)
		return 0;

	cdev->np = of_node_get(np);
	cdev->cdev_id = cdev_id;
	cdev->success_cb = success_cb;
	cdev->release_cb = release_cb;
	INIT_DEFERRABLE_WORK(&cdev->work, __cdev_devfreq_fetch_work);
	schedule_delayed_work(&cdev->work, 0);

	return 0;
}

void cdev_devfreq_exit(struct cdev_devfreq_data *cdev)
{
	if (!cdev)
		return;

	cancel_delayed_work(&cdev->work);
	if (dev_pm_qos_request_active(&cdev->qos_req))
		cdev_pm_qos_remove_devfreq_request(cdev->devfreq,
						  &cdev->qos_req);
	kfree(cdev->opp_table);
	of_node_put(cdev->np);
}

void cdev_pm_qos_update_request(struct cdev_devfreq_data *cdev, unsigned long freq)
{
	if (cdev && cdev->devfreq)
		cdev_dev_pm_qos_update_request(&cdev->qos_req, freq / HZ_PER_KHZ);
}
