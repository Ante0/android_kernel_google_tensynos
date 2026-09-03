// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <linux/errno.h>
#include <linux/bitops.h>

#include "include/metrics_collection.h"
#include "metrics_collection_internal.h"

/* Metric - Modem boot duration */

/**
 * @brief Max modem boot duration
 * @details If modem can't boot up within this period, it will be identified
 * as boot failure.
 */
#define MODEM_BOOT_DURATION_MAX (60 * HZ)

static const char *boot_type_names[MODEM_BOOT_TYPE_MAX] = {
	[MODEM_BOOT_TYPE_NORMAL] = "cold boot",
	[MODEM_BOOT_TYPE_WARM_RESET] = "warm reset boot",
	[MODEM_BOOT_TYPE_PARTIAL_RESET] = "partial reset boot",
	[MODEM_BOOT_TYPE_DUMP] = "dump boot",
};

static const char *get_boot_type_name(enum modem_boot_type type)
{
	if (type >= MODEM_BOOT_TYPE_MAX)
		return "invalid";

	if (!boot_type_names[type])
		return "undefined";

	return boot_type_names[type];
}

static ssize_t modem_boot_duration_info_print(const void *data, char *buf,
					      size_t buf_len)
{
	const struct modem_boot_duration_info *info = data;
	ssize_t len = 0;
	int type_idx;

	for (type_idx = 0; type_idx < MODEM_BOOT_TYPE_MAX; type_idx++) {
		const struct modem_boot_duration_item *duration =
			&info->boot_durations[type_idx];

		len += scnprintf(buf + len, buf_len - len,
				 "Boot type: %s\n"
				 "  Cumulative success count: 0x%llx\n"
				 "  Cumulative fail count: 0x%llx\n"
				 "  Cumulative duration msec: 0x%llx\n"
				 "  Average duration msec: 0x%llx\n"
				 "  Max duration msec: 0x%llx\n"
				 "  Last duration msec: 0x%llx\n",
				 get_boot_type_name(type_idx),
				 duration->success_count, duration->fail_count,
				 duration->total_success_duration_ms,
				 duration->average_success_duration_ms,
				 duration->max_success_duration_ms,
				 duration->last_success_duration_ms);
	}

	return len;
}

static void modem_boot_duration_info_diff(void *cur, const void *prev)
{
	struct modem_boot_duration_info *cur_info = cur;
	const struct modem_boot_duration_info *prev_info = prev;
	int type_idx;

	for (type_idx = 0; type_idx < MODEM_BOOT_TYPE_MAX; type_idx++) {
		struct modem_boot_duration_item *cur_boot =
			&cur_info->boot_durations[type_idx];
		const struct modem_boot_duration_item *prev_boot =
			&prev_info->boot_durations[type_idx];

		cur_boot->success_count -= prev_boot->success_count;
		cur_boot->fail_count -= prev_boot->fail_count;
		cur_boot->total_success_duration_ms -=
			prev_boot->total_success_duration_ms;
	}
}

static int modem_boot_duration_pull_helper(struct common_metric_manager *mgr,
					   void *data)
{
	struct modem_boot_duration_info *info = data;
	struct modem_boot_duration_manager *modem_mgr =
		container_of(mgr, struct modem_boot_duration_manager, mgr);
	unsigned long flags;

	spin_lock_irqsave(&modem_mgr->info_lock, flags);
	*info = modem_mgr->info;
	spin_unlock_irqrestore(&modem_mgr->info_lock, flags);

	return 0;
}

static void modem_boot_duration_record_success(enum modem_boot_type type,
					       u64 duration)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct modem_boot_duration_manager *mgr =
		&global_mgr->modem_boot_duration_mgr;
	struct modem_boot_duration_item *boot_item =
		&mgr->info.boot_durations[type];
	unsigned long flags;
	u64 temp_success_count;
	u64 temp_total_duration;

	spin_lock_irqsave(&mgr->info_lock, flags);

	/* When the boot success data is going to overflow, reset all the fields
	 * of boot_item, including success and failure info.
	 */
	if (unlikely(check_add_overflow(boot_item->success_count, 1ULL,
					&temp_success_count)) ||
	    unlikely(check_add_overflow(boot_item->total_success_duration_ms,
					duration, &temp_total_duration))) {
		/* In case the boot_item has new fields in the future, here we use
		 * memset to clear all fields instead clear each field explicitly.
		 */
		memset(boot_item, 0, sizeof(struct modem_boot_duration_item));

		boot_item->success_count = 1;
		boot_item->total_success_duration_ms = duration;
		boot_item->max_success_duration_ms = duration;
		boot_item->average_success_duration_ms = duration;
	} else {
		boot_item->success_count = temp_success_count;
		boot_item->total_success_duration_ms = temp_total_duration;
		if (duration > boot_item->max_success_duration_ms)
			boot_item->max_success_duration_ms = duration;
		boot_item->average_success_duration_ms =
			temp_total_duration / temp_success_count;
	}
	boot_item->last_success_duration_ms = duration;

	spin_unlock_irqrestore(&mgr->info_lock, flags);
}

static void modem_boot_duration_record_failure(enum modem_boot_type type)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct modem_boot_duration_manager *mgr =
		&global_mgr->modem_boot_duration_mgr;
	struct modem_boot_duration_item *boot_item =
		&mgr->info.boot_durations[type];
	unsigned long flags;

	spin_lock_irqsave(&mgr->info_lock, flags);

	/* When the fail count is going to overflow, keep it as the maximum value,
	 * it will be reset when the success data is overflow.
	 */
	if (likely(boot_item->fail_count < U64_MAX))
		boot_item->fail_count++;

	spin_unlock_irqrestore(&mgr->info_lock, flags);
}

static void modem_boot_duration_timeout_handler(struct timer_list *timer)
{
	struct modem_boot_duration_calculator *calculator = container_of(
		timer, struct modem_boot_duration_calculator, boot_timer);

	modem_boot_duration_record_failure(calculator->fail_type);
}

static struct common_metric_config modem_boot_duration_cfg = {
	.name = "modem_boot_duration",
	.parent = NULL, // Need to fill at runtime
	.print_info_fn = modem_boot_duration_info_print,
	.diff_info_fn = modem_boot_duration_info_diff,
	.pull_helper_fn = modem_boot_duration_pull_helper,
	.info_size = sizeof(struct modem_boot_duration_info),
};

int init_modem_boot_duration_manager(struct metrics_collection_manager *manager)
{
	struct modem_boot_duration_manager *modem_mgr =
		&manager->modem_boot_duration_mgr;
	int ret = 0;

	spin_lock_init(&modem_mgr->info_lock);

	timer_setup(&modem_mgr->calculator.boot_timer,
		    modem_boot_duration_timeout_handler, 0);

	modem_boot_duration_cfg.parent = &manager->modem;
	ret = common_metric_manager_init(&modem_mgr->mgr,
					 &modem_boot_duration_cfg);
	if (ret) {
		mcf_err("Failed to init metric manager for modem boot duration, ret = %d\n",
			ret);
		return ret;
	}

	ret = common_metric_manager_enable(&modem_mgr->mgr);

	mcf_info("Init metric manager for modem boot duration return %d\n",
		 ret);

	return ret;
}

void deinit_modem_boot_duration_manager(
	struct metrics_collection_manager *manager)
{
	common_metric_manager_disable(&manager->modem_boot_duration_mgr.mgr);

	common_metric_manager_deinit(&manager->modem_boot_duration_mgr.mgr);

	del_timer_sync(&manager->modem_boot_duration_mgr.calculator.boot_timer);
}

int mcf_notify_modem_boot_start(u32 boot_types_mask,
				enum modem_boot_type fail_type)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct modem_boot_duration_manager *mgr =
		&global_mgr->modem_boot_duration_mgr;

	if (!boot_types_mask) {
		mcf_err("Invalid boot types mask\n");
		return -EINVAL;
	}

	if (fail_type >= MODEM_BOOT_TYPE_MAX) {
		mcf_err("Invalid fail type %u\n", fail_type);
		return -EINVAL;
	}

	if (del_timer_sync(&mgr->calculator.boot_timer))
		modem_boot_duration_record_failure(mgr->calculator.fail_type);

	mgr->calculator.start_boot_ms = ktime_to_ms(ktime_get_boottime());
	mgr->calculator.fail_type = fail_type;
	mgr->calculator.start_boot_types_mask = boot_types_mask;
	mod_timer(&mgr->calculator.boot_timer,
		  jiffies + MODEM_BOOT_DURATION_MAX);

	return 0;
}
EXPORT_SYMBOL_GPL(mcf_notify_modem_boot_start);

int mcf_notify_modem_boot_end(u32 boot_types_mask)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct modem_boot_duration_manager *mgr =
		&global_mgr->modem_boot_duration_mgr;
	u32 final_types_mask;
	enum modem_boot_type boot_type;
	u64 duration;

	if (!del_timer_sync(&mgr->calculator.boot_timer)) {
		mcf_err("Unexpected boot end, the boot timer is inactive\n");
		return -EINVAL;
	}

	/* Identify the boot type */
	final_types_mask = mgr->calculator.start_boot_types_mask &
			   boot_types_mask;
	boot_type = (enum modem_boot_type)(ffs(final_types_mask) - 1);
	if (hweight_long(final_types_mask) != 1 ||
	    boot_type >= MODEM_BOOT_TYPE_MAX) {
		mcf_err("Invalid boot types mask, start 0x%x end 0x%x\n",
			mgr->calculator.start_boot_types_mask, boot_types_mask);

		modem_boot_duration_record_failure(mgr->calculator.fail_type);
		return -EINVAL;
	}

	/* Record duration */
	duration = ktime_to_ms(ktime_get_boottime()) -
		   mgr->calculator.start_boot_ms;
	modem_boot_duration_record_success(boot_type, duration);

	return 0;
}
EXPORT_SYMBOL_GPL(mcf_notify_modem_boot_end);

/* Metric - modem wakeup ap statistics */

static const char * const wakeup_source_names[WAKEUP_SRC_MAX_ID] = {
	[WAKEUP_SRC_ID_NETWORK] = "network",
	[WAKEUP_SRC_ID_GNSS] = "gnss",
	[WAKEUP_SRC_ID_LOG] = "log",
	[WAKEUP_SRC_ID_CONTROL] = "control",
	[WAKEUP_SRC_ID_MISC] = "misc",
};

static const char *get_wakeup_source_name(enum mcf_modem_wakeup_src_id id)
{
	if (id >= WAKEUP_SRC_MAX_ID)
		return "invalid";

	if (!wakeup_source_names[id])
		return "undefined";

	return wakeup_source_names[id];
}

static ssize_t modem_wakeup_ap_print(const void *data, char *buf,
				     size_t buf_len)
{
	const struct mcf_modem_wakeup_ap_stats *info = data;
	ssize_t len = 0;

	for (int i = 0; i < ARRAY_SIZE(info->counts); ++i)
		len += scnprintf(
			buf + len, buf_len - len,
			"wakeup source: %s[%d], count %lld\n",
			get_wakeup_source_name((enum mcf_modem_wakeup_src_id)i),
			i, info->counts[i]);

	return len;
}

static void modem_wakeup_ap_diff(void *cur, const void *prev)
{
	struct mcf_modem_wakeup_ap_stats *cur_info = cur;
	const struct mcf_modem_wakeup_ap_stats *prev_info = prev;

	for (int i = 0; i < ARRAY_SIZE(cur_info->counts); ++i)
		cur_info->counts[i] -= prev_info->counts[i];
}

static int modem_wakeup_ap_pull_helper(struct common_metric_manager *mgr,
				       void *data)
{
	struct mcf_modem_wakeup_ap_stats *info = data;
	struct modem_wakeup_ap_manager *modem_mgr =
		container_of(mgr, struct modem_wakeup_ap_manager, mgr);

	if (!modem_mgr->pull_fn)
		return -EINVAL;

	return modem_mgr->pull_fn(info, modem_mgr->priv);
}

static struct common_metric_config modem_wakeup_ap_cfg = {
	.name = "modem_wakeup_ap",
	.parent = NULL, // Need to fill at runtime
	.print_info_fn = modem_wakeup_ap_print,
	.diff_info_fn = modem_wakeup_ap_diff,
	.pull_helper_fn = modem_wakeup_ap_pull_helper,
	.info_size = sizeof(struct mcf_modem_wakeup_ap_stats),
};

int init_modem_wakeup_ap_manager(struct metrics_collection_manager *manager)
{
	struct modem_wakeup_ap_manager *modem_mgr =
		&manager->modem_wakeup_ap_mgr;
	int ret = 0;

	modem_wakeup_ap_cfg.parent = &manager->modem;

	ret = common_metric_manager_init(&modem_mgr->mgr, &modem_wakeup_ap_cfg);
	mcf_info("Init metric manager for modem wakeup ap return %d\n", ret);

	modem_mgr->pull_fn = NULL;
	modem_mgr->priv = NULL;

	return ret;
}

void deinit_modem_wakeup_ap_manager(struct metrics_collection_manager *manager)
{
	struct modem_wakeup_ap_manager *modem_mgr =
		&manager->modem_wakeup_ap_mgr;

	common_metric_manager_deinit(&modem_mgr->mgr);

	modem_mgr->pull_fn = NULL;
	modem_mgr->priv = NULL;
}

int mcf_register_modem_wakeup_ap(mcf_pull_modem_wakeup_ap_cb_t callback,
				 void *priv)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct modem_wakeup_ap_manager *mgr = &global_mgr->modem_wakeup_ap_mgr;
	int ret = 0;

	if (!callback) {
		mcf_err("Invalid pull function\n");
		return -EINVAL;
	}

	if (mgr->pull_fn) {
		mcf_err("Failed to register %ps, metric already registered with %ps\n",
			callback, mgr->pull_fn);
		return -EINVAL;
	}

	mgr->pull_fn = callback;
	mgr->priv = priv;

	ret = common_metric_manager_enable(&mgr->mgr);
	if (ret) {
		mcf_err("Failed to enable metric, ret = %d\n", ret);
		mgr->pull_fn = NULL;
		mgr->priv = NULL;
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mcf_register_modem_wakeup_ap);

int mcf_unregister_modem_wakeup_ap(mcf_pull_modem_wakeup_ap_cb_t callback,
				   void *priv)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct modem_wakeup_ap_manager *mgr = &global_mgr->modem_wakeup_ap_mgr;
	int ret = 0;

	if (!callback) {
		mcf_err("Invalid pull function\n");
		return -EINVAL;
	}

	if (!mgr->pull_fn) {
		mcf_err("Failed to unregister %ps, metric hasn't registered yet\n",
			callback);
		return -EINVAL;
	}

	if (mgr->pull_fn != callback) {
		mcf_err("Pull function mismatch, old %ps new %ps\n",
			mgr->pull_fn, callback);
		return -EINVAL;
	}

	if (mgr->priv != priv) {
		mcf_err("Private data mismatch, old %p new %p\n", mgr->priv,
			priv);
		return -EINVAL;
	}

	ret = common_metric_manager_disable(&mgr->mgr);
	if (ret) {
		mcf_err("Failed to unregister %ps return %d\n", callback, ret);
		return ret;
	}

	mgr->pull_fn = NULL;
	mgr->priv = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(mcf_unregister_modem_wakeup_ap);
