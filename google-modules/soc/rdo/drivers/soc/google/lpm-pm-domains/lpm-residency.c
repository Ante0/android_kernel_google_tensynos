// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include "lpm-pm-domains.h"

#include <linux/string.h>
#include <soc/google/google_gtc.h>

static const char * const lpm_state_names[] = {
	[LPM_ON] = "on",
	[LPM_OFF] = "off",
	[LPM_UNTRACKED] = "untracked"
};

static inline u64 get_now_us(void)
{
	return goog_gtc_get_time_ns() / 1000;
}

static void reset_residency(struct lpm_residency_desc *desc)
{
	memset(&desc->residency, 0, sizeof(desc->residency));
}

static inline struct lpm_residency_desc *to_desc(struct kobject *kobj)
{
	return container_of(kobj, struct lpm_residency_desc, lpm_pm_domain_kobj);
}

static const struct lpm_residency_time_state *to_residency(struct kobject *kobj)
{
	struct lpm_residency_desc *desc = to_desc(kobj->parent);

	if (!desc)
		return NULL;

	struct lpm_residency_time_state *residency =
		container_of(kobj, struct lpm_residency_time_state, residency_kobj);
	if (!residency)
		return NULL;

	if (desc->curr_state != LPM_UNTRACKED && &desc->residency[desc->curr_state] == residency)
		return lpm_get_residency(desc);
	return residency;
}

static void update_time(struct lpm_residency_desc *desc, u64 now_ts)
{
	desc->residency[desc->curr_state].time_in_state +=
		now_ts - desc->residency[desc->curr_state].last_updated;
	desc->residency[desc->curr_state].last_updated = now_ts;
}

static void enter_state(struct lpm_residency_desc *desc, int new_state, u64 now_ts)
{
	desc->residency[new_state].last_updated = now_ts;
	desc->residency[new_state].entry_count++;
	desc->residency[new_state].last_entry = now_ts;
	desc->curr_state = new_state;
}

static void exit_state(struct lpm_residency_desc *desc, u64 now_ts)
{
	update_time(desc, now_ts);
	desc->residency[desc->curr_state].last_exit = now_ts;
}

int lpm_update_residency(struct lpm_residency_desc *desc, int new_state)
{
	if (!desc || desc->curr_state == LPM_UNTRACKED || new_state >= LPM_STATE_COUNT)
		return 1;
	if (new_state == desc->curr_state)
		return 0;

	u64 now_ts = get_now_us();

	exit_state(desc, now_ts);
	enter_state(desc, new_state, now_ts);

	return 0;
}

const struct lpm_residency_time_state *lpm_get_residency(struct lpm_residency_desc *desc)
{
	if (!desc || desc->curr_state == LPM_UNTRACKED)
		return NULL;

	u64 now_ts = get_now_us();

	update_time(desc, now_ts);

	return &desc->residency[desc->curr_state];
}

void lpm_residency_init(struct lpm_residency_desc *desc)
{
	lpm_stop_residency_tracking(desc);
}

int lpm_start_residency_tracking(struct lpm_residency_desc *desc, int init_state, bool from_zero)
{
	if (!desc || init_state >= LPM_STATE_COUNT || desc->curr_state != LPM_UNTRACKED)
		return 1;

	u64 now_ts = from_zero ? 0 : get_now_us();

	enter_state(desc, init_state, now_ts);

	return 0;
}

void lpm_stop_residency_tracking(struct lpm_residency_desc *desc)
{
	reset_residency(desc);
	desc->curr_state = LPM_UNTRACKED;
}

#define MAX_NODE_NAME_LENGTH 32
#define NODE_CMP(_field_name) (strncmp(attr->name, #_field_name, MAX_NODE_NAME_LENGTH) == 0)
#define EMIT_FIELD(_field_name) (sysfs_emit(buf, "%llu\n", lpm_residency->_field_name))

static ssize_t residency_show(struct kobject *kobj, struct attribute *attr,
	char *buf)
{
	const struct lpm_residency_time_state *lpm_residency = to_residency(kobj);

	if (!lpm_residency)
		return -EINVAL;

	if (NODE_CMP(time_in_state))
		return EMIT_FIELD(time_in_state);
	if (NODE_CMP(entry_count))
		return EMIT_FIELD(entry_count);
	if (NODE_CMP(last_exit))
		return EMIT_FIELD(last_exit);
	if (NODE_CMP(last_entry))
		return EMIT_FIELD(last_entry);

	return -EINVAL;
}

static ssize_t curr_state_show(struct kobject *kobj, struct attribute *attr,
	char *buf)
{
	const struct lpm_residency_desc *desc = to_desc(kobj);

	if (!desc)
		return -EINVAL;

	return sysfs_emit(buf, "%s\n", lpm_state_names[desc->curr_state]);
}

#define __ATTR_RO_NAME(_name) {.name = #_name, .mode = 0444}

static struct attribute residency_attrs[] = {
	__ATTR_RO_NAME(time_in_state),
	__ATTR_RO_NAME(entry_count),
	__ATTR_RO_NAME(last_exit),
	__ATTR_RO_NAME(last_entry),
};

static struct attribute curr_state_attr = __ATTR_RO_NAME(curr_state);

static struct attribute *attrs[] = {
	&residency_attrs[0],
	&residency_attrs[1],
	&residency_attrs[2],
	&residency_attrs[3],
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static const struct sysfs_ops residency_sysfs_ops = {
	.show = residency_show,
};
static const struct sysfs_ops directory_sysfs_ops = {
	.show = curr_state_show
};

static const struct kobj_type directory_ktype = {
	.sysfs_ops = &directory_sysfs_ops
};
static const struct kobj_type residency_ktype = {
	.sysfs_ops = &residency_sysfs_ops
};

static struct kobject *root;

int lpm_residency_sysfs_init(struct platform_device *pdev)
{
	struct lpm_pm_domains *lpm_pm_domains = platform_get_drvdata(pdev);

	if (!root)
		root = kobject_create_and_add("lpm_pm_residency", pdev->dev.kobj.parent);
	if (!root)
		return -ENOMEM;

	lpm_pm_domains->residency_root =
		kobject_create_and_add(pdev->name, root);
	if (!lpm_pm_domains)
		goto cleanup;

	for (u32 idx = 0; idx < lpm_pm_domains->pd_count; ++idx) {
		struct lpm_residency_desc *desc = lpm_pm_domains->pds[idx].residency;

		if (kobject_init_and_add(&desc->lpm_pm_domain_kobj,
				&directory_ktype,
				lpm_pm_domains->residency_root, "%s",
				lpm_pm_domains->pds[idx].genpd.name))
			goto cleanup;

		if (sysfs_create_file(&desc->lpm_pm_domain_kobj, &curr_state_attr))
			goto cleanup;

		for (int state = 0; state < LPM_STATE_COUNT; ++state) {
			if (kobject_init_and_add(&desc->residency[state].residency_kobj,
					&residency_ktype,
					&desc->lpm_pm_domain_kobj, "%s",
					lpm_state_names[state]))
				goto cleanup;

			if (sysfs_create_group(&desc->residency[state].residency_kobj, &attr_group))
				goto cleanup;
		}
	}

	return 0;
cleanup:
	lpm_residency_sysfs_remove(pdev);
	return -ENOMEM;
}

void lpm_residency_sysfs_remove(struct platform_device *pdev)
{
	struct lpm_pm_domains *lpm_pm_domains = platform_get_drvdata(pdev);

	for (u32 idx = 0; idx < lpm_pm_domains->pd_count; ++idx) {
		struct lpm_residency_desc *desc = lpm_pm_domains->pds[idx].residency;

		for (int state = 0; state < LPM_STATE_COUNT; ++state) {
			sysfs_remove_group(&desc->residency[state].residency_kobj, &attr_group);
			kobject_put(&desc->residency[state].residency_kobj);
		}
		sysfs_remove_file(&desc->lpm_pm_domain_kobj, &curr_state_attr);
		kobject_put(&desc->lpm_pm_domain_kobj);

		lpm_stop_residency_tracking(desc);
	}

	kobject_put(lpm_pm_domains->residency_root);
	kobject_put(root);

	root = NULL;
}
