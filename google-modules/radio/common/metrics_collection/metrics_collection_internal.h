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

#ifndef __METRICS_COLLECTION_INTERNAL_H__
#define __METRICS_COLLECTION_INTERNAL_H__

#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/spinlock.h>

#include "include/metrics_collection.h"

struct common_metric_manager;

typedef int (*mcf_common_pull_helper_cb_t)(struct common_metric_manager *mgr,
					   void *data);
typedef ssize_t (*print_info_cb_t)(const void *data, char *buf, size_t buf_len);
typedef void (*diff_info_cb_t)(void *cur, const void *prev);

struct kobject_pair {
	struct kobject *base;
	struct kobject *reported;
};

struct common_metric_config {
	const char *name;
	struct kobject_pair *parent;
	print_info_cb_t print_info_fn;
	diff_info_cb_t diff_info_fn;
	mcf_common_pull_helper_cb_t pull_helper_fn;
	size_t info_size;
};

struct common_metric_manager {
	const struct common_metric_config *cfg;

	struct kobj_attribute base_attr;
	struct kobj_attribute reported_attr;
	void *last_reported_info;
};

struct pcie_link_state_info_manager {
	struct common_metric_manager mgr;

	mcf_pull_pcie_link_state_cb_t pull_fn;
	void *priv;
};

struct pcie_link_updown_info_manager {
	struct common_metric_manager mgr;
	mcf_pull_pcie_link_updown_cb_t pull_fn;
	void *priv;
};

struct pcie_link_duration_info_manager {
	struct common_metric_manager mgr;
	mcf_pull_pcie_link_duration_cb_t pull_fn;
	void *priv;
};

/**
 * @brief Structure used to manage pcie link error/recovery statistic
 */
struct pcie_link_stats_info_manager {
	struct common_metric_manager mgr;
	mcf_pull_pcie_link_stats_cb_t pull_fn;
	void *priv;
};

/**
 * @brief Boot duration information of one boot type
 */
struct modem_boot_duration_item {
	u64 success_count;
	u64 fail_count;
	u64 total_success_duration_ms;
	u64 average_success_duration_ms;
	u64 max_success_duration_ms;
	u64 last_success_duration_ms;
};

/**
 * @brief Modem boot duration statistics
 */
struct modem_boot_duration_info {
	struct modem_boot_duration_item boot_durations[MODEM_BOOT_TYPE_MAX];
};

/**
 * @brief Information used to calculate modem boot duration.
 */
struct modem_boot_duration_calculator {
	struct timer_list boot_timer;
	u64 start_boot_ms;
	enum modem_boot_type fail_type;
	u32 start_boot_types_mask;
};

/**
 * @brief Structure used to manage modem boot duration metric
 */
struct modem_boot_duration_manager {
	struct common_metric_manager mgr;
	struct modem_boot_duration_calculator calculator;
	spinlock_t info_lock;
	struct modem_boot_duration_info info;
};

/**
 * @brief Structure used to manage modem wakeup ap statistic
 */
struct modem_wakeup_ap_manager {
	struct common_metric_manager mgr;
	mcf_pull_modem_wakeup_ap_cb_t pull_fn;
	void *priv;
};

struct metrics_collection_manager {
	struct kobject *root;
	struct kobject_pair modem;
	struct pcie_link_state_info_manager pcie_link_state_mgr;
	struct pcie_link_updown_info_manager pcie_link_updown_mgr;
	struct pcie_link_duration_info_manager pcie_link_duration_mgr;
	struct pcie_link_stats_info_manager pcie_link_stats_mgr;
	struct modem_boot_duration_manager modem_boot_duration_mgr;
	struct modem_wakeup_ap_manager modem_wakeup_ap_mgr;
};

// metrics collection framework
#define LOG_TAG "mcf: "

#define mcf_err(fmt, ...) \
	pr_err(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)

#define mcf_info(fmt, ...) \
	pr_info(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)

#define mcf_debug(fmt, ...) \
	pr_debug(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)

struct metrics_collection_manager *get_manager(void);

void mcf_duration_info_diff(struct mcf_duration_info *cur,
			    const struct mcf_duration_info *prev);

int common_metric_manager_init(struct common_metric_manager *mgr,
			       const struct common_metric_config *cfg);
void common_metric_manager_deinit(struct common_metric_manager *mgr);

int common_metric_manager_enable(struct common_metric_manager *mgr);
int common_metric_manager_disable(struct common_metric_manager *mgr);

int init_pcie_link_state_manager(struct metrics_collection_manager *manager);
void deinit_pcie_link_state_manager(struct metrics_collection_manager *manager);

int init_pcie_link_updown_manager(struct metrics_collection_manager *manager);
void deinit_pcie_link_updown_manager(struct metrics_collection_manager *manager);

int init_pcie_link_duration_manager(struct metrics_collection_manager *manager);
void deinit_pcie_link_duration_manager(
	struct metrics_collection_manager *manager);

int init_pcie_link_stats_manager(struct metrics_collection_manager *manager);
void deinit_pcie_link_stats_manager(struct metrics_collection_manager *manager);

int init_modem_boot_duration_manager(struct metrics_collection_manager *manager);
void deinit_modem_boot_duration_manager(
	struct metrics_collection_manager *manager);

int init_modem_wakeup_ap_manager(struct metrics_collection_manager *manager);
void deinit_modem_wakeup_ap_manager(struct metrics_collection_manager *manager);

#endif //__METRICS_COLLECTION_INTERNAL_H__
