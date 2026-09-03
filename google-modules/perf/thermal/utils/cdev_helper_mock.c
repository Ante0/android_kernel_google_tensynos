// SPDX-License-Identifier: GPL-2.0-only
/*
 * cdev_helper_mock.c Mock functions for cdev helper functions.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */
#include <kunit/static_stub.h>
#include <linux/cpu.h>
#include <linux/devfreq.h>
#include <linux/energy_model.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>

#include "cdev_cpufreq_helper.h"
#include "cdev_devfreq_helper.h"
#include "cdev_helper.h"
#include "perf/core/google_pm_qos.h"
#include "thermal_msg_helper.h"

struct device *cdev_get_cpu_device(unsigned int cpu)
{
	KUNIT_STATIC_STUB_REDIRECT(cdev_get_cpu_device, cpu);
	return get_cpu_device(cpu);
}

int cdev_dev_pm_opp_get_opp_count(struct device *dev)
{
	KUNIT_STATIC_STUB_REDIRECT(cdev_dev_pm_opp_get_opp_count, dev);
	return dev_pm_opp_get_opp_count(dev);
}

struct dev_pm_opp *cdev_dev_pm_opp_find_freq_ceil(struct device *dev,
						  unsigned long *freq)
{
	KUNIT_STATIC_STUB_REDIRECT(cdev_dev_pm_opp_find_freq_ceil, dev, freq);
	return dev_pm_opp_find_freq_ceil(dev, freq);
}

unsigned long cdev_dev_pm_opp_get_voltage(struct dev_pm_opp *opp)
{
	KUNIT_STATIC_STUB_REDIRECT(cdev_dev_pm_opp_get_voltage, opp);
	return dev_pm_opp_get_voltage(opp);
}

int cdev_msg_tmu_get_power_table(enum hw_dev_type cdev_id, u8 idx,
				 int *val, int *max_state_idx)
{
	KUNIT_STATIC_STUB_REDIRECT(cdev_msg_tmu_get_power_table, cdev_id, idx, val, max_state_idx);
	return msg_tmu_get_power_table(cdev_id, idx, val, max_state_idx);
}

void cdev_dev_pm_opp_put(struct dev_pm_opp *opp)
{
	KUNIT_STATIC_STUB_REDIRECT(cdev_dev_pm_opp_put, opp);
	dev_pm_opp_put(opp);
}

struct devfreq *cdev_get_devfreq_by_node(struct device_node *node)
{
	KUNIT_STATIC_STUB_REDIRECT(cdev_get_devfreq_by_node, node);
	return devfreq_get_devfreq_by_node(node);
}

struct em_perf_state *cdev_em_perf_state_from_pd(struct em_perf_domain *pd)
{
	KUNIT_STATIC_STUB_REDIRECT(cdev_em_perf_state_from_pd, pd);
	return em_perf_state_from_pd(pd);
}

int cdev_pm_qos_add_devfreq_request(struct devfreq *devfreq,
				    struct dev_pm_qos_request *req,
				    enum dev_pm_qos_req_type type, s32 value)
{
	KUNIT_STATIC_STUB_REDIRECT(cdev_pm_qos_add_devfreq_request, devfreq, req, type, value);
	return google_pm_qos_add_devfreq_request(devfreq, req, type, value);
}

int cdev_pm_qos_remove_devfreq_request(struct devfreq *devfreq,
				       struct dev_pm_qos_request *req)
{
	KUNIT_STATIC_STUB_REDIRECT(cdev_pm_qos_remove_devfreq_request, devfreq, req);
	return google_pm_qos_remove_devfreq_request(devfreq, req);
}

int cdev_dev_pm_qos_update_request(struct dev_pm_qos_request *req, s32 new_value)
{
	KUNIT_STATIC_STUB_REDIRECT(cdev_dev_pm_qos_update_request, req, new_value);
	return dev_pm_qos_update_request(req, new_value);
}
