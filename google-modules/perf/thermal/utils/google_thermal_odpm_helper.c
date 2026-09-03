// SPDX-License-Identifier: GPL-2.0-only
/*
 * google_thermal_odpm_helper.c driver providing helper for interfacing with
 * ODPM.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/static_stub.h>
#include <kunit/visibility.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/limits.h>
#include <linux/math64.h>
#include <linux/mutex.h>

#include "google_odpm.h"
#include "google_thermal_odpm_helper.h"

const struct odpm_rail_energy *godpm_get_rail_energy(void)
{
	KUNIT_STATIC_STUB_REDIRECT(godpm_get_rail_energy);
	return google_odpm_get_rail_energy();
}

int godpm_of_property_read_string(const struct device_node *np,
					 const char *propname,
					 const char **out_string)
{
	KUNIT_STATIC_STUB_REDIRECT(godpm_of_property_read_string, np, propname, out_string);
	return of_property_read_string(np, propname, out_string);
}

void *godpm_kzalloc(size_t size, gfp_t flags)
{
	KUNIT_STATIC_STUB_REDIRECT(godpm_kzalloc, size, flags);
	return kzalloc(size, flags);
}

void godpm_kfree(const void *p)
{
	KUNIT_STATIC_STUB_REDIRECT(godpm_kfree, p);
	kfree(p);
}

int godpm_blocking_notifier_chain_register(struct blocking_notifier_head *nh,
						  struct notifier_block *nb)
{
	KUNIT_STATIC_STUB_REDIRECT(godpm_blocking_notifier_chain_register, nh, nb);
	return blocking_notifier_chain_register(nh, nb);
}

int godpm_blocking_notifier_chain_unregister(struct blocking_notifier_head *nh,
						    struct notifier_block *nb)
{
	KUNIT_STATIC_STUB_REDIRECT(godpm_blocking_notifier_chain_unregister, nh, nb);
	return blocking_notifier_chain_unregister(nh, nb);
}

int godpm_blocking_notifier_call_chain(struct blocking_notifier_head *nh,
					      unsigned long val, void *v)
{
	KUNIT_STATIC_STUB_REDIRECT(godpm_blocking_notifier_call_chain, nh, val, v);
	return blocking_notifier_call_chain(nh, val, v);
}

bool godpm_schedule_delayed_work(struct delayed_work *dwork,
					unsigned long delay)
{
	KUNIT_STATIC_STUB_REDIRECT(godpm_schedule_delayed_work, dwork, delay);
	return schedule_delayed_work(dwork, delay);
}

bool godpm_mod_delayed_work(struct workqueue_struct *wq,
				   struct delayed_work *dwork,
				   unsigned long delay)
{
	KUNIT_STATIC_STUB_REDIRECT(godpm_mod_delayed_work, wq, dwork, delay);
	return mod_delayed_work(wq, dwork, delay);
}

bool godpm_cancel_delayed_work_sync(struct delayed_work *dwork)
{
	KUNIT_STATIC_STUB_REDIRECT(godpm_cancel_delayed_work_sync, dwork);
	return cancel_delayed_work_sync(dwork);
}

ktime_t godpm_ktime_get(void)
{
	KUNIT_STATIC_STUB_REDIRECT(godpm_ktime_get);
	return ktime_get();
}

#define MIN_POLLING_INTERVAL_MS 100

static DEFINE_MUTEX(odpm_groups_lock);

VISIBLE_IF_KUNIT LIST_HEAD(odpm_regulator_groups);
VISIBLE_IF_KUNIT DECLARE_DELAYED_WORK(odpm_global_work, odpm_polling_work);
VISIBLE_IF_KUNIT unsigned int min_polling_interval_ms = UINT_MAX;

#ifdef CONFIG_GOOGLE_POWERCAP_KUNIT_TEST
EXPORT_SYMBOL_IF_KUNIT(odpm_regulator_groups);
EXPORT_SYMBOL_IF_KUNIT(odpm_global_work);
EXPORT_SYMBOL_IF_KUNIT(min_polling_interval_ms);
#endif /* CONFIG_GOOGLE_POWERCAP_KUNIT_TEST */

VISIBLE_IF_KUNIT void odpm_polling_work(struct work_struct *work)
{
	const struct odpm_rail_energy *rail_energy;
	struct odpm_regulator_group *reg_group;
	struct odpm_client_sub_group *sub_group;
	ktime_t now;
	int i;

	rail_energy = godpm_get_rail_energy();
	if (!rail_energy)
		goto reschedule;

	now = godpm_ktime_get();

	mutex_lock(&odpm_groups_lock);

	list_for_each_entry(reg_group, &odpm_regulator_groups, node) {
		const struct odpm_rail_energy *matched_rail;

		if (reg_group->channel_index < 0) {
			for (i = 0; i < ODPM_CHANNEL_NUM; ++i) {
				if (rail_energy[i].schematic_name &&
				    !strcmp(rail_energy[i].schematic_name,
					    reg_group->regulator_name)) {
					reg_group->channel_index = i;
					break;
				}
			}
			if (reg_group->channel_index < 0) {
				pr_warn_ratelimited("Failed to find ODPM channel for regulator %s\n",
						    reg_group->regulator_name);
				continue;
			}
		}

		if (reg_group->channel_index < 0 ||
		    reg_group->channel_index >= ODPM_CHANNEL_NUM)
			continue;

		matched_rail = &rail_energy[reg_group->channel_index];

		list_for_each_entry(sub_group, &reg_group->sub_groups, node) {
			if (ktime_compare(now, sub_group->next_notification_time) < 0)
				continue;

			/*
			 * On the first run, or if data is stale/invalid, just store
			 * baseline and skip notification.
			 */
			if (sub_group->last_timestamp_ms > 0 &&
			    matched_rail->timestamp_ms > sub_group->last_timestamp_ms &&
			    matched_rail->acc_energy >= sub_group->last_acc_energy) {
				u64 delta_energy, delta_time_ms, power_uw = 0;

				delta_energy = matched_rail->acc_energy -
					       sub_group->last_acc_energy;
				delta_time_ms = matched_rail->timestamp_ms -
						sub_group->last_timestamp_ms;

				/*
				 * Calculate power in microwatts.
				 * Power (uW) = delta_energy (uW-s) * 1000 / delta_time (ms)
				 */
				if (delta_time_ms > 0)
					power_uw = div_u64(delta_energy * 1000,
							   delta_time_ms);

				/*
				 * Pass the calculated power (uW) to clients. The u64 value
				 * is cast to void *. Clients must cast it back.
				 */
				godpm_blocking_notifier_call_chain(
					&sub_group->notifier_head, 0,
					(void *)(uintptr_t)power_uw);
			} else if (sub_group->last_timestamp_ms > 0) {
				if (matched_rail->timestamp_ms <=
				    sub_group->last_timestamp_ms)
					pr_warn_ratelimited(
						"ODPM timestamp not advanced for %s\n",
						reg_group->regulator_name);
				if (matched_rail->acc_energy <
				    sub_group->last_acc_energy)
					pr_warn_ratelimited(
						"ODPM energy accumulator reset for %s\n",
						reg_group->regulator_name);
			}

			sub_group->last_acc_energy = matched_rail->acc_energy;
			sub_group->last_timestamp_ms = matched_rail->timestamp_ms;
			sub_group->next_notification_time =
				ktime_add_ms(now, sub_group->polling_interval_ms);
		}
	}

	mutex_unlock(&odpm_groups_lock);

reschedule:
	if (min_polling_interval_ms != UINT_MAX)
		godpm_schedule_delayed_work(&odpm_global_work,
				      msecs_to_jiffies(min_polling_interval_ms));
}

static void odpm_cleanup_empty_groups(struct odpm_regulator_group *reg_group,
				      struct odpm_client_sub_group *sub_group)
{
	/* If the sub_group has no clients, it was newly created, so clean it up. */
	if (list_empty(&sub_group->clients)) {
		list_del(&sub_group->node);
		godpm_kfree(sub_group);
		/* If the reg_group has no sub_groups, it was also new, so clean it up. */
		if (list_empty(&reg_group->sub_groups)) {
			list_del(&reg_group->node);
			godpm_kfree(reg_group);
		}
	}
}

int google_thermal_odpm_register_client(const char *regulator_name,
					struct notifier_block *nb, unsigned int polling_interval_ms)
{
	struct odpm_regulator_group *reg_group, *found_reg_group = NULL;
	struct odpm_client_sub_group *sub_group, *found_sub_group = NULL;
	struct odpm_client *client;
	int ret;
	bool first_client = false;
	unsigned int original_polling_interval_ms = polling_interval_ms;

	if (!regulator_name || !nb)
		return -EINVAL;

	if (polling_interval_ms < MIN_POLLING_INTERVAL_MS) {
		polling_interval_ms = MIN_POLLING_INTERVAL_MS;
	} else if (polling_interval_ms % MIN_POLLING_INTERVAL_MS) {
		polling_interval_ms = (polling_interval_ms / MIN_POLLING_INTERVAL_MS) *
			MIN_POLLING_INTERVAL_MS;
	}

	if (original_polling_interval_ms != polling_interval_ms)
		pr_info("ODPM polling interval adjusted from %u ms to %u ms\n",
			 original_polling_interval_ms, polling_interval_ms);

	mutex_lock(&odpm_groups_lock);

	first_client = list_empty(&odpm_regulator_groups);

	list_for_each_entry(reg_group, &odpm_regulator_groups, node) {
		if (!strcmp(reg_group->regulator_name, regulator_name)) {
			found_reg_group = reg_group;
			break;
		}
	}

	if (!found_reg_group) {
		reg_group = godpm_kzalloc(sizeof(*reg_group), GFP_KERNEL);
		if (!reg_group) {
			ret = -ENOMEM;
			goto unlock_return;
		}
		strscpy(reg_group->regulator_name, regulator_name, ODPM_REGULATOR_NAME_LEN);
		INIT_LIST_HEAD(&reg_group->sub_groups);
		reg_group->channel_index = -1;

		list_add(&reg_group->node, &odpm_regulator_groups);
		found_reg_group = reg_group;
	}

	list_for_each_entry(sub_group, &found_reg_group->sub_groups, node) {
		if (sub_group->polling_interval_ms == polling_interval_ms) {
			found_sub_group = sub_group;
			break;
		}
	}

	if (!found_sub_group) {
		sub_group = godpm_kzalloc(sizeof(*sub_group), GFP_KERNEL);
		if (!sub_group) {
			ret = -ENOMEM;
			if (list_empty(&found_reg_group->sub_groups)) {
				list_del(&found_reg_group->node);
				godpm_kfree(found_reg_group);
			}
			goto unlock_return;
		}
		sub_group->polling_interval_ms = polling_interval_ms;
		BLOCKING_INIT_NOTIFIER_HEAD(&sub_group->notifier_head);
		INIT_LIST_HEAD(&sub_group->clients);
		sub_group->next_notification_time = godpm_ktime_get();
		list_add(&sub_group->node, &found_reg_group->sub_groups);
		found_sub_group = sub_group;
	}

	client = godpm_kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client) {
		ret = -ENOMEM;
		odpm_cleanup_empty_groups(found_reg_group, found_sub_group);
		goto unlock_return;
	}

	client->nb = nb;
	list_add(&client->node, &found_sub_group->clients);

	ret = godpm_blocking_notifier_chain_register(&found_sub_group->notifier_head, nb);
	if (ret) {
		list_del(&client->node);
		godpm_kfree(client);
		odpm_cleanup_empty_groups(found_reg_group, found_sub_group);
		goto unlock_return;
	}

	if (first_client) {
		min_polling_interval_ms = polling_interval_ms;
		godpm_schedule_delayed_work(&odpm_global_work,
				      msecs_to_jiffies(min_polling_interval_ms));
	} else if (polling_interval_ms < min_polling_interval_ms) {
		min_polling_interval_ms = polling_interval_ms;
		godpm_mod_delayed_work(system_wq, &odpm_global_work,
				 msecs_to_jiffies(min_polling_interval_ms));
	}

	ret = 0;

unlock_return:
	mutex_unlock(&odpm_groups_lock);
	return ret;
}

static void odpm_recalculate_min_interval(void)
{
	struct odpm_regulator_group *reg_group;
	struct odpm_client_sub_group *sub_group;
	unsigned int new_min = UINT_MAX;

	list_for_each_entry(reg_group, &odpm_regulator_groups, node) {
		list_for_each_entry(sub_group, &reg_group->sub_groups, node) {
			if (sub_group->polling_interval_ms < new_min)
				new_min = sub_group->polling_interval_ms;
		}
	}

	min_polling_interval_ms = new_min;
}

int google_thermal_odpm_unregister_client(const char *regulator_name,
					  struct notifier_block *nb,
					  unsigned int polling_interval_ms)
{
	struct odpm_regulator_group *reg_group, *found_reg_group = NULL;
	struct odpm_client_sub_group *sub_group, *found_sub_group = NULL;
	struct odpm_client *client, *found_client = NULL;
	int ret;
	bool min_interval_changed = false;
	unsigned int original_polling_interval_ms = polling_interval_ms;

	if (!regulator_name || !nb)
		return -EINVAL;

	if (polling_interval_ms < MIN_POLLING_INTERVAL_MS) {
		polling_interval_ms = MIN_POLLING_INTERVAL_MS;
	} else if (polling_interval_ms % MIN_POLLING_INTERVAL_MS) {
		polling_interval_ms = (polling_interval_ms / MIN_POLLING_INTERVAL_MS) *
			MIN_POLLING_INTERVAL_MS;
	}

	if (original_polling_interval_ms != polling_interval_ms)
		pr_info("ODPM polling interval adjusted from %u ms to %u ms for unregistration\n",
			 original_polling_interval_ms, polling_interval_ms);

	mutex_lock(&odpm_groups_lock);

	list_for_each_entry(reg_group, &odpm_regulator_groups, node) {
		if (!strcmp(reg_group->regulator_name, regulator_name)) {
			found_reg_group = reg_group;
			break;
		}
	}

	if (!found_reg_group) {
		pr_warn("ODPM regulator group not found for unregistration for %s\n",
			regulator_name);
		ret = -ENOENT;
		goto unlock_return;
	}

	list_for_each_entry(sub_group, &found_reg_group->sub_groups, node) {
		if (sub_group->polling_interval_ms == polling_interval_ms) {
			found_sub_group = sub_group;
			break;
		}
	}

	if (!found_sub_group) {
		pr_warn("ODPM client sub group not found for unregistration for %s at %ums\n",
			regulator_name, polling_interval_ms);
		ret = -ENOENT;
		goto unlock_return;
	}

	list_for_each_entry(client, &found_sub_group->clients, node) {
		if (client->nb == nb) {
			found_client = client;
			break;
		}
	}

	if (!found_client) {
		pr_warn("ODPM client not found in group for unregistration for %s\n",
			regulator_name);
		ret = -ENOENT;
		goto unlock_return;
	}

	godpm_blocking_notifier_chain_unregister(&found_sub_group->notifier_head, nb);
	list_del(&found_client->node);
	godpm_kfree(found_client);

	if (list_empty(&found_sub_group->clients)) {
		if (found_sub_group->polling_interval_ms == min_polling_interval_ms)
			min_interval_changed = true;

		list_del(&found_sub_group->node);
		godpm_kfree(found_sub_group);

		if (list_empty(&found_reg_group->sub_groups)) {
			list_del(&found_reg_group->node);
			godpm_kfree(found_reg_group);
		}
	}

	if (list_empty(&odpm_regulator_groups)) {
		godpm_cancel_delayed_work_sync(&odpm_global_work);
		min_polling_interval_ms = UINT_MAX;
	} else if (min_interval_changed) {
		odpm_recalculate_min_interval();
		godpm_mod_delayed_work(system_wq, &odpm_global_work,
				 msecs_to_jiffies(min_polling_interval_ms));
	}

	ret = 0;

unlock_return:
	mutex_unlock(&odpm_groups_lock);
	return ret;
}
