/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cdev_devfreq_helper.h Thermal devfreq cooling device helper.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */
#ifndef _CDEV_DEVFREQ_HELPER_H_
#define _CDEV_DEVFREQ_HELPER_H_

#include <linux/devfreq.h>
#include <linux/energy_model.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/units.h>
#include <linux/workqueue.h>

#include "cdev_helper.h"
#include "thermal_cpm_mbox.h"

#define CDEV_PROBE_LOOP_DELAY_MS	500
#define CDEV_PROBE_MAX_RETRY_CT		200 // (200 * 500) / 1000 = 100 seconds retry.

struct cdev_devfreq_data;
typedef void (*cdev_cb)(struct cdev_devfreq_data *cdev);

struct cdev_devfreq_data {
	unsigned int num_opps, retry_ct;
	struct cdev_opp_table *opp_table;
	struct devfreq *devfreq;
	struct dev_pm_qos_request qos_req;
	struct device_node *np;
	struct delayed_work work;
	enum hw_dev_type cdev_id;
	cdev_cb success_cb, release_cb;
};

int cdev_devfreq_init(struct cdev_devfreq_data *cdev,
		      struct device_node *np,
		      enum hw_dev_type cdev_id,
		      cdev_cb success_cb,
		      cdev_cb release_cb);
void cdev_devfreq_exit(struct cdev_devfreq_data *cdev);
void cdev_pm_qos_update_request(struct cdev_devfreq_data *cdev, unsigned long freq);
int cdev_dev_pm_qos_update_request(struct dev_pm_qos_request *req, s32 new_value);
int cdev_pm_qos_add_devfreq_request(struct devfreq *devfreq,
				    struct dev_pm_qos_request *req,
				    enum dev_pm_qos_req_type type, s32 value);
int cdev_pm_qos_remove_devfreq_request(struct devfreq *devfreq,
				       struct dev_pm_qos_request *req);
void __cdev_devfreq_fetch_work(struct work_struct *work);
int __cdev_devfreq_fetch_register(struct cdev_devfreq_data *cdev);
int __cdev_devfreq_update_opp_from_pd(struct cdev_devfreq_data *cdev, struct device *dev);
int __cdev_devfreq_update_opp_from_firmware(struct cdev_devfreq_data *cdev,
					   struct device *dev);
struct devfreq *cdev_get_devfreq_by_node(struct device_node *node);
struct em_perf_state *cdev_em_perf_state_from_pd(struct em_perf_domain *pd);
#endif  // _CDEV_DEVFREQ_HELPER_H_
