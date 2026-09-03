// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2024-2025 Google LLC */

#include <linux/container_of.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/stringify.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/sched/clock.h>

#include "pd_latency_profile.h"
#include "pd_latency_stats.h"
#include "../power_controller.h"

#define CREATE_TRACE_POINTS
#include "pd_latency_trace.h"

static const char *const profiled_stat_name[PD_STATE_COUNT][LATENCY_TYPE_COUNT] = {
	[PD_STATE_ON] = {
		[EXEC] = "exec_on",
		[FW] = "fw_on",
		[ACK] = "ack_on",
		[E2E] = "e2e_on",
	},
	[PD_STATE_OFF] = {
		[EXEC] = "exec_off",
		[FW] = "fw_off",
		[ACK] = "ack_off",
		[E2E] = "e2e_off",
	}
};

static inline struct power_controller *to_power_controller(struct kobject *kobj)
{
	struct device *dev = kobj_to_dev(kobj);
	struct platform_device *pdev = to_platform_device(dev);

	return platform_get_drvdata(pdev);
}

static int pd_latency_index(struct power_domain *pd)
{
	int idx;
	struct power_controller *pc = pd->power_controller;

	for (idx = 0; idx < pc->privdata->pd_count; idx++)
		if (pc->latency->pm_resource_id[idx] == pd->pm_resource_id)
			return idx;

	return -EINVAL;
}

static inline struct latency_data *get_latency_data_entry(struct latency_profile *latency,
							  int idx_pd,
							  int state,
							  enum latency_type lat_type)
{
	/* Get the entry from a flattened 3d array */
	return &latency->data[idx_pd * (PD_STATE_COUNT  * LATENCY_TYPE_COUNT) +
			      state * (LATENCY_TYPE_COUNT) +
			      lat_type];
}

static struct latency_data *find_latency_record(struct kobject *kobj)
{
	/* on/off->power_domain->latency_stats->power_controller */
	struct power_controller *pc =
		to_power_controller(kobj->parent->parent->parent);

	struct latency_profile *latency = pc->latency;
	int data_count = latency->pd_count * PD_STATE_COUNT * LATENCY_TYPE_COUNT;
	int idx_data;

	for (idx_data = 0; idx_data < data_count; idx_data++) {
		if (latency->data[idx_data].parent == kobj)
			return &latency->data[idx_data];
	}

	return NULL;
}

static ssize_t min_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	struct latency_data *data = find_latency_record(kobj);

	if (!data)
		return -EINVAL;

	return sysfs_emit(buf, "%u\n", latency_get_min(&data->stats));
}

static ssize_t max_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	struct latency_data *data = find_latency_record(kobj);

	if (!data)
		return -EINVAL;

	return sysfs_emit(buf, "%u\n", latency_get_max(&data->stats));
}

static ssize_t average_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buf)
{
	struct latency_data *data = find_latency_record(kobj);

	if (!data)
		return -EINVAL;

	return sysfs_emit(buf, "%u\n", latency_get_average(&data->stats));
}

static ssize_t count_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	struct latency_data *data = find_latency_record(kobj);

	if (!data)
		return -EINVAL;

	return sysfs_emit(buf, "%u\n", latency_get_count(&data->stats));
}

static ssize_t median_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	struct latency_data *data = find_latency_record(kobj);

	if (!data)
		return -EINVAL;

	return sysfs_emit(buf, "%u\n", latency_get_median(&data->stats));
}

static struct kobj_attribute stats_attrs[] = {
	__ATTR(min, 0444, min_show, NULL),
	__ATTR(max, 0444, max_show, NULL),
	__ATTR(average, 0444, average_show, NULL),
	__ATTR(count, 0444, count_show, NULL),
	__ATTR(median, 0444, median_show, NULL),
	__ATTR_NULL,
};

static struct attribute *attrs[] = {
	&stats_attrs[0].attr,
	&stats_attrs[1].attr,
	&stats_attrs[2].attr,
	&stats_attrs[3].attr,
	&stats_attrs[4].attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static ssize_t collect_latency_data_show(struct kobject *kobj,
					 struct kobj_attribute *attr, char *buf)
{
	/* latency_stats->power_controller */
	struct power_controller *pc = to_power_controller(kobj->parent);

	return sysfs_emit(buf, "%s\n",
			  pc->latency->collect_data ? "on" : "off");
}

static ssize_t collect_latency_data_store(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  const char *buf, size_t count)
{
	/* latency_stats->power_controller */
	struct power_controller *pc = to_power_controller(kobj->parent);

	if (kstrtobool(buf, &pc->latency->collect_data))
		return -EINVAL;

	return count;
}
static struct kobj_attribute collect_latency_data_attr =
	__ATTR(collect_latency_data, 0644, collect_latency_data_show,
	       collect_latency_data_store);

static ssize_t reset_latencies_store(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	/* latency_stats->power_controller */
	struct power_controller *pc = to_power_controller(kobj->parent);

	struct latency_profile *latency = pc->latency;
	int data_count = latency->pd_count * PD_STATE_COUNT * LATENCY_TYPE_COUNT;
	int idx_data;

	for (idx_data = 0; idx_data < data_count; idx_data++)
		latency_reset(&latency->data[idx_data].stats);

	return count;
}

/* create_stats_directory() - Create a directory that contains the stats
 *                            and register it with sysfs.
 *
 * @name: the name of the directory
 * @power_domain_kobj: the parent kobject of the data_kobj
 * @attr_group: the attribute group to register to the sysfs
 */
static struct kobject *create_stats_directory(const char *name,
					      struct kobject *power_domain_kobj,
					      struct attribute_group *attr_group)
{
	struct kobject *data_kobj =
		kobject_create_and_add(name, power_domain_kobj);

	if (!data_kobj)
		return NULL;

	if (sysfs_create_group(data_kobj, attr_group))
		return NULL;

	return data_kobj;
}

static struct kobj_attribute reset_latencies_attr =
	__ATTR(reset_latencies, 0200, NULL, reset_latencies_store);

static void trace_start(struct power_domain *pd, int state,
			latency_type_bitmask_t type_mask)
{
	enum latency_type lat_type;

	if (type_mask & MAIN_LATENCY_TYPE_BIT)
		PD_LATENCY_TRACE_BEGIN(pd, profiled_stat_name[state][MAIN_LATENCY_TYPE]);

	for (lat_type = 0; lat_type < LATENCY_TYPE_COUNT; ++lat_type) {
		if (lat_type != MAIN_LATENCY_TYPE && type_mask & BIT(lat_type))
			PD_LATENCY_TRACE_BEGIN(pd, profiled_stat_name[state][lat_type]);
	}
}

static void trace_stop(struct power_domain *pd, int state,
		      latency_type_bitmask_t type_mask)
{
	int lat_type;

	for (lat_type = LATENCY_TYPE_COUNT - 1; lat_type >= 0; --lat_type) {
		if (lat_type != MAIN_LATENCY_TYPE && type_mask & BIT(lat_type))
			PD_LATENCY_TRACE_END(pd, profiled_stat_name[state][lat_type]);
	}

	if (type_mask & MAIN_LATENCY_TYPE_BIT)
		PD_LATENCY_TRACE_END(pd, profiled_stat_name[state][MAIN_LATENCY_TYPE]);
}

int pd_latency_profile_start(struct generic_pm_domain *domain, int state,
			     latency_type_bitmask_t type_mask)
{
	struct power_domain *pd =
		container_of(domain, struct power_domain, genpd);
	struct power_controller *pc = pd->power_controller;
	int idx;
	u64 start_time;
	enum latency_type lat_type;

	/* FW latency should be directly stored from a cpm response */
	if (type_mask & FW_BIT)
		return -EINVAL;

	if (!pc->latency || !pc->latency->collect_data)
		return 0;

	/* we won't start profiling if the device is already in a same state */
	if (atomic_read(&pd->state) == state) {
		dev_dbg(pc->dev, "Same state/none profiling requested. Skipping");
		return 0;
	}

	idx = pd_latency_index(pd);
	if (idx < 0)
		return idx;

	start_time = sched_clock();
	trace_start(pd, state, type_mask);
	for (lat_type = 0; lat_type < LATENCY_TYPE_COUNT; ++lat_type) {
		if (type_mask & BIT(lat_type))
			get_latency_data_entry(pc->latency,
					       idx, state, lat_type)->start_time = start_time;
	}

	return 0;
}

static void store_data_point(int idx_pd, int state, enum latency_type lat_type,
			     u32 latency, struct power_controller *pc)
{
	struct latency_data *data = get_latency_data_entry(pc->latency,
							   idx_pd, state, lat_type);

	latency_store_data_point(&data->stats, latency);
}

static void try_fetch_outliers(struct power_controller *pc,
			       int idx_pd,
			       int state,
			       latency_type_bitmask_t type_mask)
{
	struct latency_stats_group **stats_groups = pc->latency->stats_groups;
	u32 **most_recent_latencies = pc->latency->most_recent_latencies;

	if (!(type_mask & MAIN_LATENCY_TYPE_BIT))
		return;

	latency_group_set_max(&stats_groups[idx_pd][state],
			      most_recent_latencies[state]);
	latency_group_set_min(&stats_groups[idx_pd][state],
			      most_recent_latencies[state]);
}

int pd_latency_profile_stop(struct generic_pm_domain *domain,
			    int state,
			    latency_type_bitmask_t type_mask,
			    bool cancel)
{
	struct power_domain *pd =
		container_of(domain, struct power_domain, genpd);
	struct power_controller *pc = pd->power_controller;
	int idx;
	u64 end_time;
	u32 delta;
	enum latency_type lat_type;
	struct latency_data *data;

	if (!pc->latency || !pc->latency->collect_data)
		return 0;

	idx = pd_latency_index(pd);
	if (idx < 0)
		return idx;

	end_time = sched_clock();
	trace_stop(pd, state, type_mask);
	for (lat_type = 0; lat_type < LATENCY_TYPE_COUNT; ++lat_type) {
		if (!(type_mask & BIT(lat_type)))
			continue;

		data = get_latency_data_entry(pc->latency, idx, state, lat_type);
		/* there was no start call before the stop call */
		if (data->start_time == 0)
			continue;

		/* if the cancel flag is set, we won't store the latency */
		if (!cancel) {
			delta = (end_time - data->start_time) / 1000;

			pc->latency->most_recent_latencies[state][lat_type] = delta;

			store_data_point(idx, state, lat_type, delta, pc);
		}

		/* signal that now the start call needs to be invoked */
		data->start_time = 0;
	}

	try_fetch_outliers(pc, idx, state, type_mask);

	return 0;
}

int pd_latency_profile_store(struct generic_pm_domain *domain, u32 latency,
			     int state, enum latency_type lat_type)
{
	struct power_domain *pd =
		container_of(domain, struct power_domain, genpd);
	struct power_controller *pc = pd->power_controller;
	int idx;

	if (!pc->latency || !pc->latency->collect_data)
		return 0;

	idx = pd_latency_index(pd);
	if (idx < 0)
		return idx;

	pc->latency->most_recent_latencies[state][lat_type] = latency;

	PD_LATENCY_TRACE_INSTANT(pd, profiled_stat_name[state][lat_type], latency);

	store_data_point(idx, state, lat_type, latency, pc);

	try_fetch_outliers(pc, idx, state, LATENCY_TYPE_BIT(lat_type));

	return 0;
}

static int init_per_state_type(struct power_controller *pc, int idx_pd, int *idx_data)
{
	struct latency_profile *latency = pc->latency;
	struct latency_stats_group *stats_group;
	int lat_type, state;

	for (state = 0; state < PD_STATE_COUNT; ++state) {
		stats_group = &pc->latency->stats_groups[idx_pd][state];
		stats_group->stats = devm_kcalloc(pc->dev, LATENCY_TYPE_COUNT,
						  sizeof(struct latency_stats *), GFP_KERNEL);
		if (!stats_group->stats)
			return -ENOMEM;

		stats_group->group_size = LATENCY_TYPE_COUNT;
		stats_group->main_stat_idx = MAIN_LATENCY_TYPE;

		/* Create switch on/off/fw stats directory */
		for (lat_type = 0; lat_type < LATENCY_TYPE_COUNT; ++lat_type) {
			latency->data_kobj[*idx_data] =
				create_stats_directory(profiled_stat_name[state][lat_type],
						       latency->power_domain_kobj[idx_pd],
						       &attr_group);
			if (!latency->data_kobj[*idx_data])
				return -ENOMEM;

			latency->data[*idx_data].parent = latency->data_kobj[*idx_data];

			stats_group->stats[lat_type] = &latency->data[*idx_data].stats;

			(*idx_data)++;
		}
	}

	return 0;
}

/* TODO (kkorczynski): Consider if on-device data is even useful if we can just have tracepoints */
int pd_latency_profile_init(struct platform_device *pdev)
{
	struct power_controller *pc = platform_get_drvdata(pdev);
	struct device_node *child_np, *np = pdev->dev.of_node;
	int ret, idx_pd, idx_data, state;

	pc->latency = devm_kzalloc(pc->dev, sizeof(struct latency_profile),
				   GFP_KERNEL);
	if (!pc->latency)
		return -ENOMEM;

	struct latency_profile *latency = pc->latency;

	latency->pd_count = 0;
	for_each_available_child_of_node(np, child_np)
		latency->pd_count++;

	latency->data = devm_kcalloc(pc->dev,
				     latency->pd_count * PD_STATE_COUNT * LATENCY_TYPE_COUNT,
				     sizeof(struct latency_data), GFP_KERNEL);
	if (!latency->data)
		return -ENOMEM;

	latency->data_kobj =
		devm_kcalloc(pc->dev, latency->pd_count * PD_STATE_COUNT * LATENCY_TYPE_COUNT,
			     sizeof(struct kobject *), GFP_KERNEL);
	if (!latency->data_kobj)
		return -ENOMEM;

	latency->power_domain_kobj = devm_kcalloc(pc->dev, latency->pd_count,
						  sizeof(struct kobject *),
						  GFP_KERNEL);
	if (!latency->power_domain_kobj)
		return -ENOMEM;

	latency->pm_resource_id =
		devm_kcalloc(pc->dev, latency->pd_count, sizeof(u32), GFP_KERNEL);
	if (!latency->pm_resource_id)
		return -ENOMEM;

	latency->most_recent_latencies =
		devm_kcalloc(pc->dev, PD_STATE_COUNT, sizeof(u32 *), GFP_KERNEL);
	if (!latency->most_recent_latencies)
		return -ENOMEM;

	for (state = 0; state < PD_STATE_COUNT; ++state) {
		latency->most_recent_latencies[state] =
			devm_kcalloc(pc->dev, LATENCY_TYPE_COUNT, sizeof(u32),
				     GFP_KERNEL);
		if (!latency->most_recent_latencies[state])
			return -ENOMEM;
	}

	latency->stats_groups =
		devm_kcalloc(pc->dev, latency->pd_count, sizeof(struct latency_stats_group *),
			     GFP_KERNEL);
	if (!latency->stats_groups)
		return -ENOMEM;

	latency->latencies_kobj =
		kobject_create_and_add("latency_stats", &pc->dev->kobj);
	if (!latency->latencies_kobj)
		return -ENOMEM;

	idx_pd = 0;
	idx_data = 0;
	for_each_available_child_of_node(np, child_np) {
		/* Create power domain directory */
		latency->power_domain_kobj[idx_pd] =
			kobject_create_and_add(child_np->name,
					       latency->latencies_kobj);
		if (!latency->power_domain_kobj[idx_pd])
			goto remove_sysfs_groups;

		latency->stats_groups[idx_pd] =
			devm_kcalloc(pc->dev, PD_STATE_COUNT,
				     sizeof(struct latency_stats_group), GFP_KERNEL);
		if (!latency->stats_groups[idx_pd])
			goto remove_sysfs_groups;

		if (init_per_state_type(pc, idx_pd, &idx_data))
			goto remove_sysfs_groups;

		latency->pm_resource_id[idx_pd] = pc->privdata->pds[idx_pd].pm_resource_id;

		idx_pd++;
	}

	latency->collect_data = 0;
	if (sysfs_create_file(latency->latencies_kobj,
			      &reset_latencies_attr.attr)) {
		dev_err(pc->dev,
			"Couldn't create collect_latency_stats sysfs node.\n");
		goto remove_sysfs_groups;
	}

	ret = sysfs_create_file(latency->latencies_kobj,
				&collect_latency_data_attr.attr);
	if (ret) {
		dev_err(pc->dev,
			"Couldn't create reset_latencies sysfs node.\n");
		goto remove_control_files;
	}

	return 0;

remove_control_files:
	sysfs_remove_file(latency->latencies_kobj,
			  &collect_latency_data_attr.attr);
remove_sysfs_groups:
	for (; idx_data >= 0; idx_data--) {
		sysfs_remove_group(latency->data_kobj[idx_data], &attr_group);
		kobject_put(latency->power_domain_kobj[idx_data]);
	}
	for (; idx_pd >= 0; idx_pd--)
		kobject_put(latency->power_domain_kobj[idx_pd]);

	kobject_put(latency->latencies_kobj);

	return -ENOMEM;
}

void pd_latency_profile_remove(struct platform_device *pdev)
{
	struct power_controller *pc = platform_get_drvdata(pdev);
	int idx_pd = pc->latency->pd_count - 1;
	int idx_data = pc->latency->pd_count * PD_STATE_COUNT * LATENCY_TYPE_COUNT - 1;

	sysfs_remove_file(pc->latency->latencies_kobj,
			  &reset_latencies_attr.attr);
	sysfs_remove_file(pc->latency->latencies_kobj,
			  &collect_latency_data_attr.attr);
	for (; idx_data >= 0; idx_data--) {
		sysfs_remove_group(pc->latency->data_kobj[idx_data],
				   &attr_group);
		kobject_put(pc->latency->power_domain_kobj[idx_data]);
	}
	for (; idx_pd >= 0; idx_pd--)
		kobject_put(pc->latency->power_domain_kobj[idx_pd]);

	kobject_put(pc->latency->latencies_kobj);
}
