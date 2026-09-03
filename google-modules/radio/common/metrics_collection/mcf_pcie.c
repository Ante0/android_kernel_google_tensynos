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
#include "include/metrics_collection.h"
#include "metrics_collection_internal.h"

/* Metric - PCIe link state */

static ssize_t pcie_link_state_print(const void *data, char *buf,
				     size_t buf_len)
{
	const struct mcf_pcie_link_state_info *info = data;
	ssize_t len = 0;

	len += scnprintf(buf + len, buf_len - len, "%d\n", info->link_state);

	return len;
}

static int pcie_link_state_pull_helper(struct common_metric_manager *mgr,
				       void *data)
{
	struct mcf_pcie_link_state_info *info = data;
	struct pcie_link_state_info_manager *pcie_mgr =
		container_of(mgr, struct pcie_link_state_info_manager, mgr);

	if (!pcie_mgr->pull_fn)
		return -EINVAL;

	return pcie_mgr->pull_fn(info, pcie_mgr->priv);
}

static struct common_metric_config pcie_link_state_cfg = {
	.name = "pcie_link_state",
	.parent = NULL, // Need to fill at runtime
	.print_info_fn = pcie_link_state_print,
	.diff_info_fn = NULL, // Doesn't support diff for pcie link state
	.pull_helper_fn = pcie_link_state_pull_helper,
	.info_size = sizeof(struct mcf_pcie_link_state_info),
};

int init_pcie_link_state_manager(struct metrics_collection_manager *manager)
{
	int ret = 0;

	pcie_link_state_cfg.parent = &manager->modem;

	ret = common_metric_manager_init(&manager->pcie_link_state_mgr.mgr,
					 &pcie_link_state_cfg);
	mcf_info("Init metric manager for pcie link state return %d\n", ret);

	manager->pcie_link_state_mgr.pull_fn = NULL;
	manager->pcie_link_state_mgr.priv = NULL;

	return ret;
}

void deinit_pcie_link_state_manager(struct metrics_collection_manager *manager)
{
	common_metric_manager_deinit(&manager->pcie_link_state_mgr.mgr);

	manager->pcie_link_state_mgr.pull_fn = NULL;
	manager->pcie_link_state_mgr.priv = NULL;
}

int mcf_register_pcie_link_state(mcf_pull_pcie_link_state_cb_t callback,
				 void *priv)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct pcie_link_state_info_manager *mgr =
		&global_mgr->pcie_link_state_mgr;
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

	return 0;
}
EXPORT_SYMBOL_GPL(mcf_register_pcie_link_state);

int mcf_unregister_pcie_link_state(mcf_pull_pcie_link_state_cb_t callback,
				   void *priv)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct pcie_link_state_info_manager *mgr =
		&global_mgr->pcie_link_state_mgr;
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
EXPORT_SYMBOL_GPL(mcf_unregister_pcie_link_state);

/* Metric - PCIe link updown */

static ssize_t pcie_link_updown_print(const void *data, char *buf,
				      size_t buf_len)
{
	const struct mcf_pcie_link_updown_info *info = data;
	ssize_t len = 0;

	len += scnprintf(buf + len, buf_len - len,
			 "Link up:\n  Cumulative count: 0x%llx\n"
			 "  Cumulative duration msec: 0x%llx\n"
			 "  Last entry timestamp msec: 0x%llx\n",
			 info->link_up.count, info->link_up.duration_ms,
			 info->link_up.last_entry_ms);

	len += scnprintf(buf + len, buf_len - len,
			 "Link down:\n  Cumulative count: 0x%llx\n"
			 "  Cumulative duration msec: 0x%llx\n"
			 "  Last entry timestamp msec: 0x%llx\n",
			 info->link_down.count, info->link_down.duration_ms,
			 info->link_down.last_entry_ms);

	return len;
}

static void pcie_link_updown_diff(void *cur, const void *prev)
{
	struct mcf_pcie_link_updown_info *cur_info = cur;
	const struct mcf_pcie_link_updown_info *prev_info = prev;

	mcf_duration_info_diff(&cur_info->link_up, &prev_info->link_up);
	mcf_duration_info_diff(&cur_info->link_down, &prev_info->link_down);
}

static int pcie_link_updown_pull_helper(struct common_metric_manager *mgr,
					void *data)
{
	struct mcf_pcie_link_updown_info *info = data;
	struct pcie_link_updown_info_manager *pcie_mgr =
		container_of(mgr, struct pcie_link_updown_info_manager, mgr);

	if (!pcie_mgr->pull_fn)
		return -EINVAL;

	return pcie_mgr->pull_fn(info, pcie_mgr->priv);
}

static struct common_metric_config pcie_link_updown_cfg = {
	.name = "pcie_link_updown",
	.parent = NULL, // Need to fill at runtime
	.print_info_fn = pcie_link_updown_print,
	.diff_info_fn = pcie_link_updown_diff,
	.pull_helper_fn = pcie_link_updown_pull_helper,
	.info_size = sizeof(struct mcf_pcie_link_updown_info),
};

int init_pcie_link_updown_manager(struct metrics_collection_manager *manager)
{
	int ret = 0;

	pcie_link_updown_cfg.parent = &manager->modem;

	ret = common_metric_manager_init(&manager->pcie_link_updown_mgr.mgr,
					 &pcie_link_updown_cfg);
	mcf_info("Init metric manager for pcie link updown return %d\n", ret);

	manager->pcie_link_updown_mgr.pull_fn = NULL;
	manager->pcie_link_updown_mgr.priv = NULL;

	return ret;
}

void deinit_pcie_link_updown_manager(struct metrics_collection_manager *manager)
{
	common_metric_manager_deinit(&manager->pcie_link_updown_mgr.mgr);

	manager->pcie_link_updown_mgr.pull_fn = NULL;
	manager->pcie_link_updown_mgr.priv = NULL;
}

int mcf_register_pcie_link_updown(mcf_pull_pcie_link_updown_cb_t callback,
				  void *priv)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct pcie_link_updown_info_manager *mgr =
		&global_mgr->pcie_link_updown_mgr;
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

	return 0;
}
EXPORT_SYMBOL_GPL(mcf_register_pcie_link_updown);

int mcf_unregister_pcie_link_updown(mcf_pull_pcie_link_updown_cb_t callback,
				    void *priv)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct pcie_link_updown_info_manager *mgr =
		&global_mgr->pcie_link_updown_mgr;
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
EXPORT_SYMBOL_GPL(mcf_unregister_pcie_link_updown);

/* Metric - PCIe link duration */

static ssize_t pcie_link_duration_print(const void *data, char *buf,
					size_t buf_len)
{
	const struct mcf_pcie_link_duration_info *info = data;
	ssize_t len = 0;
	int idx;

	len += scnprintf(buf + len, buf_len - len, "link_speed:\n  GEN%d\n",
			 info->last_link_speed);

	for (idx = 0; idx < ARRAY_SIZE(info->speed); ++idx)
		len += scnprintf(
			buf + len, buf_len - len,
			"Gen%d:\n  count: %#llx\n  duration msec: %#llx\n",
			idx + 1, info->speed[idx].count,
			info->speed[idx].duration_ms);

	return len;
}

static void pcie_link_duration_diff(void *cur, const void *prev)
{
	struct mcf_pcie_link_duration_info *cur_info = cur;
	const struct mcf_pcie_link_duration_info *prev_info = prev;
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(cur_info->speed); ++idx)
		mcf_duration_info_diff(&cur_info->speed[idx],
				       &prev_info->speed[idx]);
}

static int pcie_link_duration_pull_helper(struct common_metric_manager *mgr,
					  void *data)
{
	struct mcf_pcie_link_duration_info *info = data;
	struct pcie_link_duration_info_manager *pcie_mgr =
		container_of(mgr, struct pcie_link_duration_info_manager, mgr);

	if (!pcie_mgr->pull_fn)
		return -EINVAL;

	return pcie_mgr->pull_fn(info, pcie_mgr->priv);
}

static struct common_metric_config pcie_link_duration_cfg = {
	.name = "pcie_link_duration",
	.parent = NULL, // Need to fill at runtime
	.print_info_fn = pcie_link_duration_print,
	.diff_info_fn = pcie_link_duration_diff,
	.pull_helper_fn = pcie_link_duration_pull_helper,
	.info_size = sizeof(struct mcf_pcie_link_duration_info),
};

int init_pcie_link_duration_manager(struct metrics_collection_manager *manager)
{
	int ret = 0;

	pcie_link_duration_cfg.parent = &manager->modem;

	ret = common_metric_manager_init(&manager->pcie_link_duration_mgr.mgr,
					 &pcie_link_duration_cfg);
	mcf_info("Init metric manager for pcie link duration return %d\n", ret);

	manager->pcie_link_duration_mgr.pull_fn = NULL;
	manager->pcie_link_duration_mgr.priv = NULL;

	return ret;
}

void deinit_pcie_link_duration_manager(
	struct metrics_collection_manager *manager)
{
	common_metric_manager_deinit(&manager->pcie_link_duration_mgr.mgr);

	manager->pcie_link_duration_mgr.pull_fn = NULL;
	manager->pcie_link_duration_mgr.priv = NULL;
}

int mcf_register_pcie_link_duration(mcf_pull_pcie_link_duration_cb_t callback,
				    void *priv)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct pcie_link_duration_info_manager *mgr =
		&global_mgr->pcie_link_duration_mgr;
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

	return 0;
}
EXPORT_SYMBOL_GPL(mcf_register_pcie_link_duration);

int mcf_unregister_pcie_link_duration(mcf_pull_pcie_link_duration_cb_t callback,
				      void *priv)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct pcie_link_duration_info_manager *mgr =
		&global_mgr->pcie_link_duration_mgr;
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
EXPORT_SYMBOL_GPL(mcf_unregister_pcie_link_duration);

/* Metric - PCIe link error/recovery statistics */

static ssize_t pcie_link_stats_print(const void *data, char *buf,
				     size_t buf_len)
{
	const struct mcf_pcie_link_stats_info *info = data;
	ssize_t len = 0;

	len += scnprintf(buf + len, buf_len - len,
			 "link up failure count: %#llx\n"
			 "link recovery failure count: %#llx\n"
			 "link down irq count: %#llx\n"
			 "link cmpl timeout irq count: %#llx\n"
			 "link up average time: %#llx\n",
			 info->link_up_failure_count,
			 info->link_recovery_failure_count,
			 info->link_down_irq_count,
			 info->cmpl_timeout_irq_count, info->link_up_time_avg);

	return len;
}

static void pcie_link_stats_diff(void *cur, const void *prev)
{
	struct mcf_pcie_link_stats_info *cur_info = cur;
	const struct mcf_pcie_link_stats_info *prev_info = prev;

	cur_info->link_up_failure_count -= prev_info->link_up_failure_count;
	cur_info->link_recovery_failure_count -=
		prev_info->link_recovery_failure_count;
	cur_info->link_down_irq_count -= prev_info->link_down_irq_count;
	cur_info->cmpl_timeout_irq_count -= prev_info->cmpl_timeout_irq_count;
}

static int pcie_link_stats_pull_helper(struct common_metric_manager *mgr,
				       void *data)
{
	struct mcf_pcie_link_stats_info *info = data;
	struct pcie_link_stats_info_manager *pcie_mgr =
		container_of(mgr, struct pcie_link_stats_info_manager, mgr);

	if (!pcie_mgr->pull_fn)
		return -EINVAL;

	return pcie_mgr->pull_fn(info, pcie_mgr->priv);
}

static struct common_metric_config pcie_link_stats_cfg = {
	.name = "pcie_link_stats",
	.parent = NULL, // Need to fill at runtime
	.print_info_fn = pcie_link_stats_print,
	.diff_info_fn = pcie_link_stats_diff,
	.pull_helper_fn = pcie_link_stats_pull_helper,
	.info_size = sizeof(struct mcf_pcie_link_stats_info),
};

int init_pcie_link_stats_manager(struct metrics_collection_manager *manager)
{
	struct pcie_link_stats_info_manager *pcie_mgr =
		&manager->pcie_link_stats_mgr;
	int ret = 0;

	pcie_link_stats_cfg.parent = &manager->modem;

	ret = common_metric_manager_init(&pcie_mgr->mgr, &pcie_link_stats_cfg);
	mcf_info("Init metric manager for pcie link stats return %d\n", ret);

	pcie_mgr->pull_fn = NULL;
	pcie_mgr->priv = NULL;

	return ret;
}

void deinit_pcie_link_stats_manager(struct metrics_collection_manager *manager)
{
	struct pcie_link_stats_info_manager *pcie_mgr =
		&manager->pcie_link_stats_mgr;

	common_metric_manager_deinit(&pcie_mgr->mgr);

	pcie_mgr->pull_fn = NULL;
	pcie_mgr->priv = NULL;
}

int mcf_register_pcie_link_stats(mcf_pull_pcie_link_stats_cb_t callback,
				 void *priv)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct pcie_link_stats_info_manager *mgr =
		&global_mgr->pcie_link_stats_mgr;
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

	return 0;
}
EXPORT_SYMBOL_GPL(mcf_register_pcie_link_stats);

int mcf_unregister_pcie_link_stats(mcf_pull_pcie_link_stats_cb_t callback,
				   void *priv)
{
	struct metrics_collection_manager *global_mgr = get_manager();
	struct pcie_link_stats_info_manager *mgr =
		&global_mgr->pcie_link_stats_mgr;
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
EXPORT_SYMBOL_GPL(mcf_unregister_pcie_link_stats);
