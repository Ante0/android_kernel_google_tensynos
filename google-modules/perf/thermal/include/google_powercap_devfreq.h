/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * google_powercap_devfreq.h Google devfreq powercap related functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */
#ifndef _GOOGLE_POWERCAP_DEVFREQ_H_
#define _GOOGLE_POWERCAP_DEVFREQ_H_

#include <linux/devfreq.h>
#include <linux/energy_model.h>
#include <linux/of.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <kunit/visibility.h>
#include <linux/units.h>
#include <linux/workqueue.h>

#include "cdev_devfreq_helper.h"
#include "google_powercap.h"

#define GPOWERCAP_DEVFREQ_UTIL_PERCENTAGE_INITIAL 10

struct gpowercap_devfreq {
	struct gpowercap gpowercap;
	struct cdev_devfreq_data cdev;
	struct device_node *odpm_phandle;
	struct notifier_block odpm_nb;
	struct delayed_work odpm_work;
	u64 last_power_uw;
	int target_opp_idx;
	u64 util_percentage;
	char *odpm_rail_name;
};

static inline struct gpowercap_devfreq *to_gpowercap_devfreq(struct gpowercap *gpowercap)
{
	return container_of(gpowercap, struct gpowercap_devfreq, gpowercap);
}

void gpc_warn_on_once(void);
int gpc_devfreq_update_pd_power_uw(struct gpowercap *gpowercap);
u64 gpc_devfreq_set_pd_power_limit(struct gpowercap *gpowercap, u64 power_limit);
u64 gpc_devfreq_get_pd_power_uw(struct gpowercap *gpowercap);
void gpc_devfreq_pd_release(struct gpowercap *gpowercap);
int __gpc_devfreq_setup(struct gpowercap *parent, struct device_node *np, enum hw_dev_type cdev_id,
			const char *odpm_rail_name, u64 polling_interval_ms);
int gpc_cdev_devfreq_init(struct cdev_devfreq_data *cdev, struct device_node *np,
			  enum hw_dev_type cdev_id, cdev_cb success_cb, cdev_cb release_cb);

#if IS_ENABLED(CONFIG_KUNIT)
void gpc_cdev_pm_qos_update_request(struct cdev_devfreq_data *cdev, unsigned long freq);
void __gpc_devfreq_odpm_work_func(struct work_struct *work);
int __gpc_devfreq_set_time_window_us(struct gpowercap *gpowercap, u64 time_window_us);
void gpc_devfreq_of_node_put(struct device_node *node);
bool gpc_devfreq_cancel_delayed_work_sync(struct delayed_work *dwork);
#endif // IS_ENABLED(CONFIG_KUNIT)

#endif // _GOOGLE_POWERCAP_DEVFREQ_H_
