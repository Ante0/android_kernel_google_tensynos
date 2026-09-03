// SPDX-License-Identifier: GPL-2.0
/*
 * GCMA Arbitrator Sysfs Statistics
 */

#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "gcma_arbitrator_internal.h"
#include "gcma_arbitrator_sysfs.h"

/* External kobject from gcma_heap_sysfs.c */
extern struct kobject *gcma_heap_kobj;

struct gcma_region_stat {
	struct kobject kobj;
	struct gcma_region *region;
	unsigned long cur_usage;
	unsigned long max_usage;
};

static struct kobject *gcma_regions_kobj;

static void gcma_region_stat_release(struct kobject *kobj)
{
	struct gcma_region_stat *stat = container_of(kobj, struct gcma_region_stat, kobj);

	kfree(stat);
}

static ssize_t cur_usage_kb_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct gcma_region_stat *stat = container_of(kobj, struct gcma_region_stat, kobj);
	unsigned long cur;

	spin_lock(&stat->region->stat_lock);
	cur = stat->cur_usage;
	spin_unlock(&stat->region->stat_lock);

	return sysfs_emit(buf, "%lu\n", cur / 1024);
}

static ssize_t max_usage_kb_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct gcma_region_stat *stat = container_of(kobj, struct gcma_region_stat, kobj);
	unsigned long max;

	spin_lock(&stat->region->stat_lock);
	max = stat->max_usage;
	spin_unlock(&stat->region->stat_lock);

	return sysfs_emit(buf, "%lu\n", max / 1024);
}

static ssize_t max_usage_kb_store(struct kobject *kobj, struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	struct gcma_region_stat *stat = container_of(kobj, struct gcma_region_stat, kobj);

	spin_lock(&stat->region->stat_lock);
	stat->max_usage = stat->cur_usage;
	spin_unlock(&stat->region->stat_lock);

	return count;
}

static struct kobj_attribute cur_usage_kb_attr = __ATTR_RO(cur_usage_kb);
static struct kobj_attribute max_usage_kb_attr = __ATTR_RW(max_usage_kb);

static struct attribute *gcma_region_attrs[] = {
	&cur_usage_kb_attr.attr,
	&max_usage_kb_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gcma_region);

static const struct kobj_type gcma_region_ktype = {
	.release = gcma_region_stat_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = gcma_region_groups,
};

int gcma_arbitrator_sysfs_init_region(struct gcma_region *region)
{
	int ret;
	struct gcma_region_stat *stat;

	if (!gcma_regions_kobj) {
		gcma_regions_kobj = kobject_create_and_add("gcma_regions", gcma_heap_kobj);
		if (!gcma_regions_kobj)
			return -ENOMEM;
	}

	spin_lock(&region->stat_lock);
	if (region->stat) {
		spin_unlock(&region->stat_lock);
		return 0;
	}
	spin_unlock(&region->stat_lock);

	stat = kzalloc(sizeof(*stat), GFP_KERNEL);
	if (!stat)
		return -ENOMEM;

	stat->region = region;

	ret = kobject_init_and_add(&stat->kobj, &gcma_region_ktype, gcma_regions_kobj,
				   "%s", region->name);
	if (ret) {
		kobject_put(&stat->kobj);
		return ret;
	}

	spin_lock(&region->stat_lock);
	region->stat = stat;
	spin_unlock(&region->stat_lock);

	return 0;
}

void gcma_arbitrator_sysfs_exit_region(struct gcma_region *region)
{
	struct gcma_region_stat *stat;

	spin_lock(&region->stat_lock);
	stat = region->stat;
	region->stat = NULL;
	spin_unlock(&region->stat_lock);

	if (stat) {
		kobject_del(&stat->kobj);
		kobject_put(&stat->kobj);
	}
}

void gcma_arbitrator_add_stat(struct gcma_region *region, size_t size)
{
	struct gcma_region_stat *stat = region->stat;

	spin_lock(&region->stat_lock);
	stat->cur_usage += size;
	if (stat->cur_usage > stat->max_usage)
		stat->max_usage = stat->cur_usage;
	spin_unlock(&region->stat_lock);
}

void gcma_arbitrator_sub_stat(struct gcma_region *region, size_t size)
{
	struct gcma_region_stat *stat = region->stat;

	spin_lock(&region->stat_lock);
	stat->cur_usage -= size;
	spin_unlock(&region->stat_lock);
}
