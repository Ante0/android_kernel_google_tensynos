/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * google_thermal_odpm_helper.h Google thermal odpm related functions.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */
#ifndef _GOOGLE_THERMAL_ODPM_HELPER_H_
#define _GOOGLE_THERMAL_ODPM_HELPER_H_

#include <linux/notifier.h>
#include <linux/of.h>

#if IS_ENABLED(CONFIG_KUNIT)
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include "google_odpm.h"
#endif /* CONFIG_KUNIT */

/*
 * Internal structures for ODPM client management.
 */

#define ODPM_REGULATOR_NAME_LEN 32

struct odpm_client {
	struct list_head node;
	struct notifier_block *nb;
};

struct odpm_client_sub_group {
	struct list_head node;
	unsigned int polling_interval_ms;
	struct blocking_notifier_head notifier_head;
	struct list_head clients;
	ktime_t next_notification_time;
	u64 last_acc_energy;
	u64 last_timestamp_ms;
};

struct odpm_regulator_group {
	struct list_head node;
	char regulator_name[ODPM_REGULATOR_NAME_LEN];
	struct list_head sub_groups; /* list of odpm_client_sub_group */
	int channel_index;
};

/**
 * google_thermal_odpm_register_client() - Register a client for ODPM data by rail name.
 * @regulator_name: String name of the power rail regulator.
 * @nb: Notifier block to be called with ODPM data.
 * @polling_interval_ms: Desired polling interval in milliseconds.
 *
 * This function registers a client to receive ODPM energy data for a specific
 * power rail. Clients are grouped by rail and polling interval. The library
 * enforces a minimum polling interval of 100ms. Requested polling intervals
 * that are not a multiple of 100ms will be rounded down to the nearest
 * multiple.
 *
 * Return: 0 on success, or a negative error code.
 */
int google_thermal_odpm_register_client(const char *regulator_name,
					struct notifier_block *nb,
					unsigned int polling_interval_ms);

/**
 * google_thermal_odpm_unregister_client() - Unregister a client for ODPM data by rail name.
 * @regulator_name: String name of the power rail regulator.
 * @nb: Notifier block that was registered.
 * @polling_interval_ms: The polling interval used for registration.
 *
 * This function unregisters a client from receiving ODPM energy data. The
 * polling interval should match the value provided during registration.
 */
int google_thermal_odpm_unregister_client(const char *regulator_name,
					  struct notifier_block *nb,
					  unsigned int polling_interval_ms);

#if IS_ENABLED(CONFIG_KUNIT)
const struct odpm_rail_energy *godpm_get_rail_energy(void);
int godpm_of_property_read_string(const struct device_node *np,
				  const char *propname,
				  const char **out_string);
void *godpm_kzalloc(size_t size, gfp_t flags);
void godpm_kfree(const void *p);
int godpm_blocking_notifier_chain_register(struct blocking_notifier_head *nh,
					   struct notifier_block *nb);
int godpm_blocking_notifier_chain_unregister(struct blocking_notifier_head *nh,
					     struct notifier_block *nb);
int godpm_blocking_notifier_call_chain(struct blocking_notifier_head *nh,
				       unsigned long val, void *v);
bool godpm_schedule_delayed_work(struct delayed_work *dwork,
				 unsigned long delay);
bool godpm_mod_delayed_work(struct workqueue_struct *wq,
			    struct delayed_work *dwork,
			    unsigned long delay);
bool godpm_cancel_delayed_work_sync(struct delayed_work *dwork);
ktime_t godpm_ktime_get(void);
void odpm_polling_work(struct work_struct *work);

extern struct list_head odpm_regulator_groups;
extern struct delayed_work odpm_global_work;
extern unsigned int min_polling_interval_ms;
#endif /* CONFIG_KUNIT */

#endif /* _GOOGLE_THERMAL_ODPM_HELPER_H_ */

