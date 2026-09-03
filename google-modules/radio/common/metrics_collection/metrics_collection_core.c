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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include "metrics_collection_internal.h"

#define KOBJECT_ROOT_NAME "pixel_metrics"
#define KOBJECT_REPORTED_NAME "reported"
#define KOBJECT_PAIR_MODEM_NAME "modem"

struct metrics_collection_manager mcf_mgr = { 0 };

struct metrics_collection_manager *get_manager(void)
{
	return &mcf_mgr;
}

void mcf_duration_info_diff(struct mcf_duration_info *cur,
			    const struct mcf_duration_info *prev)
{
	cur->count -= prev->count;
	cur->duration_ms -= prev->duration_ms;
}

static int init_kobject_pair(struct kobject_pair *pair, const char *name)
{
	if (!pair || !name)
		return -EINVAL;

	pair->base = kobject_create_and_add(name, mcf_mgr.root);
	if (!pair->base) {
		mcf_err("Failed to create base kobject for %s\n", name);
		return -ENOMEM;
	}

	pair->reported =
		kobject_create_and_add(KOBJECT_REPORTED_NAME, pair->base);
	if (!pair->reported) {
		mcf_err("Failed to create reported kobject for %s\n", name);
		kobject_put(pair->base);
		return -ENOMEM;
	}

	mcf_info("Create kobject pair for %s success\n", name);
	return 0;
}

static void deinit_kobject_pair(const struct kobject_pair *pair)
{
	if (pair->reported)
		kobject_put(pair->reported);

	if (pair->base)
		kobject_put(pair->base);
}

static ssize_t common_metric_manager_show(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  char *buf)
{
	struct common_metric_manager *mgr =
		container_of(attr, struct common_metric_manager, base_attr);
	const struct common_metric_config *cfg = mgr->cfg;
	void *info = NULL;
	int ret = 0;
	ssize_t len = 0;

	if (WARN_ON(kobj != cfg->parent->base)) {
		mcf_err("Invalid common metric manager, kobject %s, attr %s\n",
			kobj->name, attr->attr.name);
		return -EINVAL;
	}

	info = vmalloc(cfg->info_size);
	if (unlikely(!info)) {
		mcf_err("Failed to allocate info memory(%zu) for metric %s\n",
			cfg->info_size, cfg->name);
		return -ENOMEM;
	}

	memset(info, 0, cfg->info_size);
	ret = cfg->pull_helper_fn(mgr, info);
	if (ret) {
		mcf_err("Failed to pull info for metric %s, ret = %d\n",
			cfg->name, ret);
		vfree(info);
		return -EIO;
	}

	len += cfg->print_info_fn(info, &buf[len], PAGE_SIZE - len);
	vfree(info);

	return len;
}

static ssize_t common_metric_manager_reported_show(struct kobject *kobj,
						   struct kobj_attribute *attr,
						   char *buf)
{
	struct common_metric_manager *mgr =
		container_of(attr, struct common_metric_manager, reported_attr);
	const struct common_metric_config *cfg = mgr->cfg;
	void *info = NULL;
	void *diff_info = NULL;
	int ret = 0;
	ssize_t len = 0;

	if (WARN_ON(kobj != cfg->parent->reported)) {
		mcf_err("Invalid common metric manager, kobject %s, attr %s\n",
			kobj->name, attr->attr.name);
		return -EINVAL;
	}

	info = vmalloc(cfg->info_size);
	if (unlikely(!info)) {
		mcf_err("Failed to allocate info memory(%zu) for metric %s\n",
			cfg->info_size, cfg->name);
		return -ENOMEM;
	}

	diff_info = vmalloc(cfg->info_size);
	if (unlikely(!diff_info)) {
		mcf_err("Failed to allocate diff info memory(%zu) for metric %s\n",
			cfg->info_size, cfg->name);
		vfree(info);
		return -ENOMEM;
	}

	memset(info, 0, cfg->info_size);
	ret = cfg->pull_helper_fn(mgr, info);
	if (ret) {
		mcf_err("Failed to pull info for metric %s, ret = %d\n",
			cfg->name, ret);
		vfree(diff_info);
		vfree(info);
		return -EIO;
	}

	memcpy(diff_info, info, cfg->info_size);
	cfg->diff_info_fn(diff_info, mgr->last_reported_info);
	memcpy(mgr->last_reported_info, info, cfg->info_size);

	len += cfg->print_info_fn(diff_info, &buf[len], PAGE_SIZE - len);

	vfree(diff_info);
	vfree(info);

	return len;
}

const struct kobj_attribute base_template =
	__ATTR("template", 0444, common_metric_manager_show, NULL);
const struct kobj_attribute reported_template =
	__ATTR("template", 0444, common_metric_manager_reported_show, NULL);

int common_metric_manager_init(struct common_metric_manager *mgr,
			       const struct common_metric_config *cfg)
{
	if (!mgr || !cfg)
		return -EINVAL;

	if (!cfg->name || !cfg->parent || !cfg->print_info_fn ||
	    !cfg->pull_helper_fn) {
		mcf_err("Invalid metric config data\n");
		return -EINVAL;
	}

	if (mgr->cfg) {
		mcf_err("Refuse to re-init metric %s with new cfg %s\n",
			mgr->cfg->name, cfg->name);
		return -EINVAL;
	}

	mgr->base_attr = base_template;
	mgr->base_attr.attr.name = cfg->name;

	if (cfg->diff_info_fn) {
		mgr->last_reported_info = vmalloc(cfg->info_size);
		if (!mgr->last_reported_info) {
			mcf_err("Failed to alloc memory(%zu) for metric %s\n",
				cfg->info_size, cfg->name);
			return -ENOMEM;
		}

		mgr->reported_attr = reported_template;
		mgr->reported_attr.attr.name = cfg->name;
	}

	mgr->cfg = cfg;
	return 0;
}

void common_metric_manager_deinit(struct common_metric_manager *mgr)
{
	common_metric_manager_disable(mgr);

	vfree(mgr->last_reported_info);

	mgr->cfg = NULL;
}

int common_metric_manager_enable(struct common_metric_manager *mgr)
{
	const struct common_metric_config *cfg = NULL;
	int ret = 0;

	if (!mgr)
		return -EINVAL;

	cfg = mgr->cfg;
	if (!cfg) {
		mcf_err("Metric is uninitialized yet\n");
		return -EINVAL;
	}

	/* Init last reported data */
	if (cfg->diff_info_fn) {
		memset(mgr->last_reported_info, 0, cfg->info_size);
		ret = cfg->pull_helper_fn(mgr, mgr->last_reported_info);
		if (ret) {
			mcf_err("Failed to pull data for metric %s, ret = %d\n",
				cfg->name, ret);

			return ret;
		}
	}

	ret = sysfs_create_file(cfg->parent->base, &mgr->base_attr.attr);
	if (ret) {
		mcf_err("Failed to create base sysnode for metric %s, ret = %d\n",
			cfg->name, ret);

		return ret;
	}

	if (cfg->diff_info_fn) {
		ret = sysfs_create_file(cfg->parent->reported,
					&mgr->reported_attr.attr);
		if (ret) {
			mcf_err("Failed to create reported sysnode for metric %s, ret = %d\n",
				cfg->name, ret);

			sysfs_remove_file(cfg->parent->base,
					  &mgr->base_attr.attr);
			return ret;
		}
	}

	return 0;
}

int common_metric_manager_disable(struct common_metric_manager *mgr)
{
	const struct common_metric_config *cfg = NULL;

	if (!mgr)
		return -EINVAL;

	cfg = mgr->cfg;
	if (!cfg) {
		mcf_err("Metric is uninitialized yet\n");
		return -EINVAL;
	}

	if (cfg->diff_info_fn) {
		sysfs_remove_file(cfg->parent->reported,
				  &mgr->reported_attr.attr);
	}
	sysfs_remove_file(cfg->parent->base, &mgr->base_attr.attr);

	return 0;
}

static int __init metrics_collection_init(void)
{
	int ret = 0;

	mcf_info("Starting metrics collection framework\n");

	mcf_mgr.root = kobject_create_and_add(KOBJECT_ROOT_NAME, kernel_kobj);
	if (!mcf_mgr.root) {
		mcf_err("Failed to create root kobject\n");
		return -ENOMEM;
	}

	ret = init_kobject_pair(&mcf_mgr.modem, KOBJECT_PAIR_MODEM_NAME);
	if (ret) {
		mcf_err("Failed to create kobject pair %s, ret = %d\n",
			KOBJECT_PAIR_MODEM_NAME, ret);
		goto exit_free_kobject_root;
	}

	ret = init_pcie_link_state_manager(&mcf_mgr);
	if (ret) {
		mcf_err("Failed to init pcie link state manager, ret = %d\n",
			ret);
		goto exit_deinit_modem;
	}

	ret = init_pcie_link_updown_manager(&mcf_mgr);
	if (ret) {
		mcf_err("Failed to init pcie link updown manager, ret = %d\n",
			ret);
		goto exit_deinit_pcie_link_state;
	}

	ret = init_modem_boot_duration_manager(&mcf_mgr);
	if (ret) {
		mcf_err("Failed to init modem boot duration manager, ret = %d\n",
			ret);
		goto exit_deinit_pcie_link_updown;
	}

	ret = init_pcie_link_duration_manager(&mcf_mgr);
	if (ret) {
		mcf_err("Failed to init pcie link gen manager, ret = %d\n",
			ret);
		goto exit_deinit_modem_boot_duration;
	}

	ret = init_pcie_link_stats_manager(&mcf_mgr);
	if (ret) {
		mcf_err("Failed to init pcie link stats manager, ret = %d\n",
			ret);
		goto exit_deinit_pcie_link_duration;
	}

	ret = init_modem_wakeup_ap_manager(&mcf_mgr);
	if (ret) {
		mcf_err("Failed to init modem wakeup ap manager, ret = %d\n",
			ret);
		goto exit_deinit_pcie_link_stats;
	}

	return ret;

exit_deinit_pcie_link_stats:
	deinit_pcie_link_stats_manager(&mcf_mgr);
exit_deinit_pcie_link_duration:
	deinit_pcie_link_duration_manager(&mcf_mgr);
exit_deinit_modem_boot_duration:
	deinit_modem_boot_duration_manager(&mcf_mgr);
exit_deinit_pcie_link_updown:
	deinit_pcie_link_updown_manager(&mcf_mgr);
exit_deinit_pcie_link_state:
	deinit_pcie_link_state_manager(&mcf_mgr);
exit_deinit_modem:
	deinit_kobject_pair(&mcf_mgr.modem);
exit_free_kobject_root:
	kobject_put(mcf_mgr.root);
	return ret;
}

static void __exit metrics_collection_exit(void)
{
	mcf_info("Exiting metrics collection framework\n");

	deinit_modem_wakeup_ap_manager(&mcf_mgr);
	deinit_pcie_link_stats_manager(&mcf_mgr);
	deinit_pcie_link_duration_manager(&mcf_mgr);
	deinit_modem_boot_duration_manager(&mcf_mgr);
	deinit_pcie_link_updown_manager(&mcf_mgr);
	deinit_pcie_link_state_manager(&mcf_mgr);
	deinit_kobject_pair(&mcf_mgr.modem);
	kobject_put(mcf_mgr.root);
}

module_init(metrics_collection_init);
module_exit(metrics_collection_exit);

MODULE_AUTHOR("Minghao Wang <minghaowang@google.com>");
MODULE_DESCRIPTION("Pixel kernel metrics collection framework driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:metrics-collection");
