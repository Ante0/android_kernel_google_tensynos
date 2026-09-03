// SPDX-License-Identifier: GPL-2.0-only
/*
 * google_powercap_helper.c driver to register the powercap nodes and tree.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/static_stub.h>
#include <linux/platform_device.h>

#include "google_thermal_odpm_helper.h"
#include "google_powercap_helper.h"
#include "google_powercap_stats.h"

struct powercap_zone *
gpc_powercap_register_zone(struct powercap_zone *power_zone,
			   struct powercap_control_type *control_type, const char *name,
			   struct powercap_zone *parent, const struct powercap_zone_ops *ops,
			   int nr_constraints, const struct powercap_zone_constraint_ops *const_ops)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_powercap_register_zone, power_zone, control_type, name,
				   parent, ops, nr_constraints, const_ops);
	return powercap_register_zone(power_zone, control_type, name, parent, ops, nr_constraints,
				      const_ops);
}

int gpc_device_create_file(struct device *device, const struct device_attribute *entry)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_device_create_file, device, entry);
	return device_create_file(device, entry);
}

void gpc_device_remove_file(struct device *device, const struct device_attribute *entry)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_device_remove_file, device, entry);
	device_remove_file(device, entry);
}

int gpc_powercap_unregister_control_type(struct powercap_control_type *control_type)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_powercap_unregister_control_type, control_type);
	return powercap_unregister_control_type(control_type);
}

struct powercap_control_type *
gpc_powercap_register_control_type(struct powercap_control_type *control_type, const char *name,
				   const struct powercap_control_type_ops *ops)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_powercap_register_control_type, control_type, name, ops);
	return powercap_register_control_type(control_type, name, ops);
}

int gpc_powercap_unregister_zone(struct powercap_control_type *control_type,
				 struct powercap_zone *power_zone)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_powercap_unregister_zone, control_type, power_zone);
	return powercap_unregister_zone(control_type, power_zone);
}

struct device_node *gpc_of_parse_phandle(const struct device_node *np, const char *phandle_name,
					 int index)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_of_parse_phandle, np, phandle_name, index);
	return of_parse_phandle(np, phandle_name, index);
}

int gpc_of_property_read_u32(const struct device_node *np, const char *propname, u32 *out_value)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_of_property_read_u32, np, propname, out_value);
	return of_property_read_u32(np, propname, out_value);
}

int gpc_google_thermal_odpm_register_client(const char *rail_name,
					  struct notifier_block *nb,
					  unsigned int polling_interval_ms)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_google_thermal_odpm_register_client, rail_name, nb,
				   polling_interval_ms);
	return google_thermal_odpm_register_client(rail_name, nb, polling_interval_ms);
}

int gpc_google_thermal_odpm_unregister_client(const char *rail_name,
					  struct notifier_block *nb,
					  unsigned int polling_interval_ms)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_google_thermal_odpm_unregister_client, rail_name, nb,
				   polling_interval_ms);
	return google_thermal_odpm_unregister_client(rail_name, nb, polling_interval_ms);
}

static DEFINE_MUTEX(gpowercap_lock);
static struct powercap_control_type *pct;
static struct gpowercap *root;

static const char * const constraint_name[] = {
	"Average",
};

static int get_time_window_us(struct powercap_zone *pcz, int cid, u64 *window)
{
	struct gpowercap *gpowercap = to_gpowercap(pcz);

	*window = gpowercap->time_window_us;

	return 0;
}

static int set_time_window_us(struct powercap_zone *pcz, int cid, u64 window)
{
	struct gpowercap *gpowercap = to_gpowercap(pcz);

	if (gpowercap->ops && gpowercap->ops->set_time_window_us)
		return gpowercap->ops->set_time_window_us(gpowercap, window);

	return -EOPNOTSUPP;
}

static int get_max_power_range_uw(struct powercap_zone *pcz, u64 *max_power_uw)
{
	struct gpowercap *gpowercap = to_gpowercap(pcz);

	*max_power_uw = gpowercap->power_max - gpowercap->power_min;

	return 0;
}

static int get_power_uw(struct powercap_zone *pcz, u64 *power_uw)
{
	return __get_power_uw(to_gpowercap(pcz), power_uw);
}

/**
 * gpowercap_update_power - Update the power on the gpowercap
 * @gpowercap: a pointer to a gpowercap structure to update
 *
 * Function to update the power values of the gpowercap node specified in
 * parameter. These new values will be propagated to the tree.
 *
 * Return: zero on success, -EINVAL if the values are inconsistent
 */
int gpowercap_update_power(struct gpowercap *gpowercap)
{
	return __gpowercap_update_power(gpowercap);
}

/**
 * gpowercap_release_zone - Cleanup when the node is released
 * @pcz: a pointer to a powercap_zone structure
 *
 * Do some housecleaning and update the power on the tree. The
 * release will be denied if the node has children. This function must
 * be called by the specific release callback of the different
 * backends.
 *
 * Return: 0 on success, -EBUSY if there are children
 */
int gpowercap_release_zone(struct powercap_zone *pcz)
{
	return __gpowercap_release_zone(pcz);
}

static int get_power_limit_uw(struct powercap_zone *pcz, int cid, u64 *power_limit)
{
	*power_limit = to_gpowercap(pcz)->userspace_power_limit;

	return 0;
}

static int set_power_limit_uw(struct powercap_zone *pcz, int cid, u64 power_limit)
{
	struct gpowercap *gpowercap = to_gpowercap(pcz);

	return __set_power_limit_uw(gpowercap, power_limit);
}

static const char *get_constraint_name(struct powercap_zone *pcz, int cid)
{
	return constraint_name[cid];
}

static int get_max_power_uw(struct powercap_zone *pcz, int id, u64 *max_power)
{
	*max_power = to_gpowercap(pcz)->power_max;

	return 0;
}

static struct powercap_zone_constraint_ops constraint_ops = {
	.set_power_limit_uw = set_power_limit_uw,
	.get_power_limit_uw = get_power_limit_uw,
	.set_time_window_us = set_time_window_us,
	.get_time_window_us = get_time_window_us,
	.get_max_power_uw = get_max_power_uw,
	.get_name = get_constraint_name,
};

static struct powercap_zone_ops zone_ops = {
	.get_max_power_range_uw = get_max_power_range_uw,
	.get_power_uw = get_power_uw,
	.release = gpowercap_release_zone,
};

int __get_power_uw(struct gpowercap *gpowercap, u64 *power_uw)
{
	struct gpowercap *child;
	u64 power;
	int ret = 0;

	if (gpowercap->ops) {
		// Opps not initialized yet.
		if (!gpowercap->num_opps)
			return -EAGAIN;

		*power_uw = gpowercap->ops->get_power_uw(gpowercap);
		return 0;
	}

	*power_uw = 0;

	list_for_each_entry(child, &gpowercap->children, siblings) {
		ret = __get_power_uw(child, &power);
		if (ret)
			break;
		*power_uw += power;
	}

	return ret;
}

void __gpowercap_sub_power(struct gpowercap *gpowercap)
{
	struct gpowercap *parent = gpowercap->parent;

	while (parent) {
		parent->power_max -= gpowercap->power_max;
		parent->power_limit -= gpowercap->power_limit;
		parent->power_min = 0;
		if (parent->ops && parent->ops->evaluate)
			parent->ops->evaluate(parent);

		gpc_stats_init(parent);
		parent = parent->parent;
	}
}

void __gpowercap_add_power(struct gpowercap *gpowercap)
{
	struct gpowercap *parent = gpowercap->parent;

	while (parent) {
		parent->power_max += gpowercap->power_max;
		parent->power_limit += gpowercap->power_limit;
		parent->power_min = 0;
		if (parent->ops && parent->ops->evaluate)
			parent->ops->evaluate(parent);

		gpc_stats_init(parent);
		parent = parent->parent;
	}
}

int __gpowercap_update_power(struct gpowercap *gpowercap)
{
	int ret = 0;

	__gpowercap_sub_power(gpowercap);

	ret = gpowercap->ops->update_power_uw(gpowercap);
	if (ret)
		pr_err("Failed to update power for '%s': %d\n", gpowercap->zone.name, ret);

	gpc_stats_init(gpowercap);

	if (!test_bit(GPOWERCAP_USERSPACE_LIMIT_FLAG, &gpowercap->flags))
		gpowercap->userspace_power_limit = gpowercap->power_max;

	if (!test_bit(GPOWERCAP_PARENT_LIMIT_FLAG, &gpowercap->flags))
		gpowercap->parent_power_limit = gpowercap->power_max;

	gpowercap->power_limit = min(gpowercap->userspace_power_limit,
				     gpowercap->parent_power_limit);

	if (gpowercap->power_limit >= gpowercap->power_max)
		clear_bit(GPOWERCAP_POWER_LIMIT_FLAG, &gpowercap->flags);
	else
		set_bit(GPOWERCAP_POWER_LIMIT_FLAG, &gpowercap->flags);

	__gpowercap_add_power(gpowercap);

	return ret;
}

int __gpowercap_release_zone(struct powercap_zone *pcz)
{
	struct gpowercap *gpowercap = to_gpowercap(pcz);
	struct gpowercap *parent = gpowercap->parent;

	if (!list_empty(&gpowercap->children))
		return -EBUSY;

	if (parent)
		list_del(&gpowercap->siblings);

	__gpowercap_sub_power(gpowercap);

	gpc_stats_exit(gpowercap);

	if (gpowercap->ops)
		gpowercap->ops->release(gpowercap);
	else
		kfree(gpowercap);

	return 0;
}

static int __gpowercap_apply_power_limit(struct gpowercap *gpowercap)
{
	u64 power_limit;

	power_limit = min(gpowercap->userspace_power_limit, gpowercap->parent_power_limit);

	/*
	 * A max power limitation means we remove the power limit,
	 * otherwise we set a constraint and flag the gpowercap node.
	 */
	if (power_limit >= gpowercap->power_max)
		clear_bit(GPOWERCAP_POWER_LIMIT_FLAG, &gpowercap->flags);
	else
		set_bit(GPOWERCAP_POWER_LIMIT_FLAG, &gpowercap->flags);

	if (test_bit(GPOWERCAP_POWER_LIMIT_BYPASS_FLAG, &gpowercap->flags))
		gpowercap->power_limit = power_limit;
	else
		gpowercap->power_limit = gpowercap->ops->set_power_uw(gpowercap, power_limit);
	gpc_stats_update(gpowercap, GPC_STAT_POWER_LIMIT, gpowercap->power_limit);

	return 0;
}

int __set_power_limit_uw(struct gpowercap *gpowercap, u64 power_limit)
{
	// Intermediate nodes will be a NOP for set power for now.
	if (!gpowercap->ops)
		return 0;

	// Opps not initialized yet.
	if (!gpowercap->num_opps)
		return -EAGAIN;

	/*
	 * Don't allow values outside of the power range previously
	 * set when initializing the power numbers.
	 */
	power_limit = clamp_val(power_limit, gpowercap->power_min, gpowercap->power_max);

	mutex_lock(&gpowercap->lock);

	if (power_limit >= gpowercap->power_max) {
		clear_bit(GPOWERCAP_USERSPACE_LIMIT_FLAG, &gpowercap->flags);
		gpowercap->userspace_power_limit = gpowercap->power_max;
	} else {
		set_bit(GPOWERCAP_USERSPACE_LIMIT_FLAG, &gpowercap->flags);
		gpowercap->userspace_power_limit = power_limit;
	}

	pr_debug("Node[%s]: userspace_power_limit:%llu\n", gpowercap->zone.name,
		 gpowercap->userspace_power_limit);

	__gpowercap_apply_power_limit(gpowercap);

	mutex_unlock(&gpowercap->lock);

	return 0;
}

int gpowercap_set_parent_power_limit(struct gpowercap *gpowercap, u64 power_limit)
{
	// Intermediate nodes will be a NOP for set power for now.
	if (!gpowercap->ops)
		return 0;

	// Opps not initialized yet.
	if (!gpowercap->num_opps)
		return -EAGAIN;

	/*
	 * Don't allow values outside of the power range previously
	 * set when initializing the power numbers.
	 */
	power_limit = clamp_val(power_limit, gpowercap->power_min, gpowercap->power_max);

	mutex_lock(&gpowercap->lock);

	if (power_limit >= gpowercap->power_max) {
		clear_bit(GPOWERCAP_PARENT_LIMIT_FLAG, &gpowercap->flags);
		gpowercap->parent_power_limit = gpowercap->power_max;
	} else {
		set_bit(GPOWERCAP_PARENT_LIMIT_FLAG, &gpowercap->flags);
		gpowercap->parent_power_limit = power_limit;
	}

	__gpowercap_apply_power_limit(gpowercap);

	mutex_unlock(&gpowercap->lock);

	return 0;
}

static void __power_limit_bypass_clear_work(struct work_struct *work)
{
	struct gpowercap *gpowercap = container_of(work, struct gpowercap, bypass_work.work);

	mutex_lock(&gpowercap->lock);
	clear_bit(GPOWERCAP_POWER_LIMIT_BYPASS_FLAG, &gpowercap->flags);
	if (test_bit(GPOWERCAP_POWER_LIMIT_FLAG, &gpowercap->flags)) {
		gpowercap->power_limit =
			gpowercap->ops->set_power_uw(gpowercap, gpowercap->power_limit);
		pr_info("%s: Power limit bypass cleared. limit:%llu uW applied\n",
			gpowercap->zone.name, gpowercap->power_limit);
	}
	mutex_unlock(&gpowercap->lock);
}

int __power_limit_bypass(struct gpowercap *gpowercap, int time_ms)
{
	int bypass_time_msec = 0;

	mutex_lock(&gpowercap->lock);
	bypass_time_msec = clamp_val(time_ms, 0, GPOWERCAP_POWER_LIMIT_BYPASS_TIME_MSEC_MAX);

	set_bit(GPOWERCAP_POWER_LIMIT_BYPASS_FLAG, &gpowercap->flags);
	if (test_bit(GPOWERCAP_POWER_LIMIT_FLAG, &gpowercap->flags) && bypass_time_msec) {
		gpowercap->ops->set_power_uw(gpowercap, gpowercap->power_max);
		pr_info("%s: Power limit bypass for %d milli-seconds\n", gpowercap->zone.name,
			bypass_time_msec);
	}
	mutex_unlock(&gpowercap->lock);

	mod_delayed_work(system_highpri_wq, &gpowercap->bypass_work,
			 msecs_to_jiffies(bypass_time_msec));

	return bypass_time_msec;
}

static ssize_t power_limit_bypass_msec_store(struct device *dev, struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct gpowercap *gpowercap;
	struct powercap_zone *pcz = to_powercap_zone(dev);
	int time_ms;

	if (!pcz)
		return -ENODEV;

	gpowercap = to_gpowercap(pcz);
	if (!gpowercap)
		return -ENODEV;

	if (kstrtoint(buf, 10, &time_ms))
		return -EINVAL;

	__power_limit_bypass(gpowercap, time_ms);

	return count;
}

static DEVICE_ATTR_WO(power_limit_bypass_msec);

ssize_t __power_levels_uw_show(struct gpowercap *gpowercap, char *buf)
{
	int i, buf_ct = 0;

	// Opps not initialized yet.
	if (!gpowercap->num_opps)
		return -EAGAIN;

	for (i = (gpowercap->num_opps - 1); i >= 0; i--)
		buf_ct += sysfs_emit_at(buf, buf_ct, "%u ", gpowercap->opp_table[i].power);
	buf_ct += sysfs_emit_at(buf, buf_ct, "\n");

	return buf_ct;
}

static ssize_t power_levels_uw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gpowercap *gpowercap;
	struct powercap_zone *pcz = to_powercap_zone(dev);

	if (!pcz)
		return -ENODEV;

	gpowercap = to_gpowercap(pcz);
	if (!gpowercap)
		return -ENODEV;

	return __power_levels_uw_show(gpowercap, buf);
}

static DEVICE_ATTR_RO(power_levels_uw);

static ssize_t stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return gpc_stats_show(dev, attr, buf);
}
static DEVICE_ATTR_RO(stats);

static ssize_t stats_avail_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return gpc_stats_avail_show(dev, attr, buf);
}
static DEVICE_ATTR_RO(stats_avail);

int __gpowercap_register(const char *name, struct gpowercap *gpowercap, struct gpowercap *parent)
{
	struct powercap_zone *pcz;
	int ret = 0;

	if (!pct)
		return -EAGAIN;

	if (root && !parent)
		return -EBUSY;

	if (!root && parent)
		return -EINVAL;

	if (!gpowercap)
		return -EINVAL;

	if (gpowercap->ops && !(gpowercap->ops->set_power_uw && gpowercap->ops->get_power_uw &&
				gpowercap->ops->update_power_uw && gpowercap->ops->release))
		return -EINVAL;

	pcz = gpc_powercap_register_zone(&gpowercap->zone, pct, name, parent ? &parent->zone : NULL,
					 &zone_ops, MAX_GOOGLE_POWERCAP_CONSTRAINTS,
					 &constraint_ops);
	if (IS_ERR(pcz))
		return PTR_ERR(pcz);

	if (parent) {
		list_add_tail(&gpowercap->siblings, &parent->children);
		gpowercap->parent = parent;
	} else {
		root = gpowercap;
	}

	gpowercap->flags = 0;
	if (gpowercap->ops && !gpowercap->ops->update_power_uw(gpowercap)) {
		gpowercap->userspace_power_limit = gpowercap->power_max;
		gpowercap->parent_power_limit = gpowercap->power_max;
		gpowercap->power_limit = gpowercap->power_max;
		__gpowercap_add_power(gpowercap);
	}

	gpc_stats_init(gpowercap);

	if (gpowercap->ops) {
		pr_debug("power cap zone:%s create power_level\n", gpowercap->zone.name);
		ret = gpc_device_create_file(&gpowercap->zone.dev, &dev_attr_power_levels_uw);
		if (ret)
			pr_warn("Power cap zone:%s power level sysfs creation err:%d\n",
				gpowercap->zone.name, ret);
		ret = gpc_device_create_file(&gpowercap->zone.dev,
					     &dev_attr_power_limit_bypass_msec);
		if (ret)
			pr_warn("Power cap zone:%s power level sysfs creation err:%d\n",
				gpowercap->zone.name, ret);
		ret = gpc_device_create_file(&gpowercap->zone.dev, &dev_attr_stats);
		if (ret)
			pr_warn("Power cap zone:%s stats sysfs creation err:%d\n",
				gpowercap->zone.name, ret);
		ret = gpc_device_create_file(&gpowercap->zone.dev, &dev_attr_stats_avail);
		if (ret)
			pr_warn("Power cap zone:%s stats_avail sysfs creation err:%d\n",
				gpowercap->zone.name, ret);
	}

	pr_debug("Registered gpowercap node '%s' / %llu-%llu uW.\n", gpowercap->zone.name,
		 gpowercap->power_min, gpowercap->power_max);

	return 0;
}

void __gpowercap_destroy_tree_recursive(struct gpowercap *gpowercap)
{
	struct gpowercap *child, *aux;

	list_for_each_entry_safe(child, aux, &gpowercap->children, siblings)
		__gpowercap_destroy_tree_recursive(child);

	/*
	 * At this point, we know all children were removed from the
	 * recursive call before
	 */
	gpowercap_unregister(gpowercap);
}

void __gpowercap_destroy_hierarchy(void)
{
	int i;

	mutex_lock(&gpowercap_lock);
	if (!pct)
		goto out_unlock;
	if (root)
		__gpowercap_destroy_tree_recursive(root);

	for (i = 0; i < ARRAY_SIZE(gpc_device_ops); i++) {
		if (!gpc_device_ops[i] || !gpc_device_ops[i]->exit)
			continue;

		gpc_device_ops[i]->exit();
	}
	gpc_powercap_unregister_control_type(pct);
	pct = NULL;
	root = NULL;
out_unlock:
	mutex_unlock(&gpowercap_lock);
}

static int gpowercap_dt_parse_nodes(struct device *dev, struct device_node *parent_dn,
				    struct gpowercap *parent_gpc)
{
	struct device_node *child_dn;
	int ret = 0;

	for_each_child_of_node(parent_dn, child_dn) {
		struct device_node *dev_dn = NULL;
		struct gpowercap *child_gpc = NULL;
		const char *odpm_rail_name = NULL;
		u32 type;
		u32 cdev_id = 0;
		u32 polling_interval_ms = 0;

		if (gpc_of_property_read_u32(child_dn, "google,node-type", &type)) {
			dev_err(dev, "missing 'google,node-type' for %s\n", child_dn->name);
			ret = -EINVAL;
			break;
		}
		of_property_read_string(child_dn, "odpm-rail-name", &odpm_rail_name);
		gpc_of_property_read_u32(child_dn, "polling-interval-ms", &polling_interval_ms);

		switch (type) {
		case GPOWERCAP_NODE_VIRTUAL:
			child_gpc = kzalloc(sizeof(*child_gpc), GFP_KERNEL);
			if (!child_gpc) {
				ret = -ENOMEM;
				break;
			}
			gpowercap_init(child_gpc, NULL);

			ret = gpowercap_register(child_dn->name, child_gpc, parent_gpc);
			if (ret) {
				dev_err(dev, "Failed to register gpowercap node '%s': %d\n",
					child_dn->name, ret);
				kfree(child_gpc);
				child_gpc = NULL;
			}
			break;
		case GPOWERCAP_NODE_VIRTUAL_VOLTAGE:
		case GPOWERCAP_NODE_VIRTUAL_WEIGHTS:
			child_gpc = gpc_device_ops[type]->algo_setup(child_dn, parent_gpc);
			if (IS_ERR(child_gpc)) {
				ret = PTR_ERR(child_gpc);
				dev_err(dev,
					"Failed to allocate gpowercap voltage node '%s'. err:%d\n",
					child_dn->name, ret);
				child_gpc = NULL;
			}
			break;
		case GPOWERCAP_NODE_CPU:
		case GPOWERCAP_NODE_DEVFREQ:
			dev_dn = gpc_of_parse_phandle(child_dn, "google,device-phandle", 0);
			if (!dev_dn) {
				dev_err(dev, "missing 'google,device-phandle' for %s\n",
					child_dn->name);
				ret = -ENODEV;
				break;
			}

			if (gpc_of_property_read_u32(child_dn, "google,cdev-id", &cdev_id)) {
				dev_err(dev, "missing 'google,cdev-id' for %s\n", child_dn->name);
				ret = -EINVAL;
			} else if (cdev_id >= HW_CDEV_MAX) {
				dev_err(dev, "Invalid cdev ID:%u for node %s\n", cdev_id,
					child_dn->name);
				ret = -EINVAL;
			} else if (type >= ARRAY_SIZE(gpc_device_ops) ||
				   !gpc_device_ops[type]->setup) {
				dev_err(dev, "Missing setup for type:%d\n", type);
				ret = -ENODEV;
			} else {
				ret = gpc_device_ops[type]->setup(parent_gpc, dev_dn, cdev_id,
								   odpm_rail_name,
								   polling_interval_ms);
				if (ret)
					dev_err(dev, "Failed to setup leaf node for %s\n",
						child_dn->name);
			}
			of_node_put(dev_dn);
			child_gpc = NULL;
			break;
		default:
			dev_err(dev, "invalid 'google,node-type' for %s\n", child_dn->name);
			ret = -EINVAL;
			break;
		}

		if (ret)
			break;

		if (child_gpc) {
			ret = gpowercap_dt_parse_nodes(dev, child_dn, child_gpc);
			if (ret)
				break;
		}
	}
	return ret;
}

#if IS_ENABLED(CONFIG_KUNIT)
void __gpc_init_pct_test(void)
{
	pct = gpc_powercap_register_control_type(NULL, GPOWERCAP_CONTROL_TYPE, NULL);
}
#endif

int gpowercap_dt_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!np)
		return -ENODEV;

	mutex_lock(&gpowercap_lock);

	if (pct) {
		mutex_unlock(&gpowercap_lock);
		return -EBUSY;
	}

	pct = gpc_powercap_register_control_type(NULL, GPOWERCAP_CONTROL_TYPE, NULL);
	if (IS_ERR(pct)) {
		dev_err(&pdev->dev, "Failed to register control type\n");
		ret = PTR_ERR(pct);
		pct = NULL;
		mutex_unlock(&gpowercap_lock);
		return ret;
	}

	ret = gpowercap_dt_parse_nodes(&pdev->dev, np, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to create hierarchy from DT: %d\n", ret);
		mutex_unlock(&gpowercap_lock);
		__gpowercap_destroy_hierarchy();
		return ret;
	}

	mutex_unlock(&gpowercap_lock);
	return 0;
}

void gpowercap_dt_remove(struct platform_device *pdev)
{
	__gpowercap_destroy_hierarchy();
}

/**
 * gpowercap_register - Register a gpowercap node in the hierarchy tree
 * @name: a string specifying the name of the node
 * @gpowercap: a pointer to a gpowercap structure corresponding to the new node
 * @parent: a pointer to a gpowercap structure corresponding to the parent node
 *
 * Create a gpowercap node in the tree. If no parent is specified, the node
 * is the root node of the hierarchy. If the root node already exists,
 * then the registration will fail. The powercap controller must be
 * initialized before calling this function.
 *
 * The gpowercap structure must be initialized with the power numbers
 * before calling this function.
 *
 * Return: zero on success, a negative value in case of error:
 *  -EAGAIN: the function is called before the framework is initialized.
 *  -EBUSY: the root node is already inserted
 *  -EINVAL: * there is no root node yet and @parent is specified
 *           * no all ops are defined
 *           * parent have ops which are reserved for leaves
 *   Other negative values are reported back from the powercap framework
 */
int gpowercap_register(const char *name, struct gpowercap *gpowercap, struct gpowercap *parent)
{
	KUNIT_STATIC_STUB_REDIRECT(gpowercap_register, name, gpowercap, parent);
	return __gpowercap_register(name, gpowercap, parent);
}

/**
 * gpowercap_unregister - Unregister a gpowercap node from the hierarchy tree
 * @gpowercap: a pointer to a gpowercap structure corresponding to the node to be removed
 *
 * Call the underlying powercap unregister function. That will call
 * the release callback of the powercap zone.
 */
void gpowercap_unregister(struct gpowercap *gpowercap)
{
	pr_debug("Unregistered gpowercap node '%s'\n", gpowercap->zone.name);
	cancel_delayed_work_sync(&gpowercap->bypass_work);
	gpc_powercap_unregister_zone(pct, &gpowercap->zone);
}

void gpowercap_destroy_hierarchy(void)
{
	return __gpowercap_destroy_hierarchy();
}

/**
 * gpowercap_report_power_uw - Report power for the gpowercap
 * @gpowercap: a pointer to a gpowercap structure to update
 * @power_uw: current power in uw
 *
 * Function to update the current power values of the gpowercap node specified in
 * parameter. This will trigger rebalance of the tree.
 *
 * Return: true on success, false if the values are inconsistent
 */
bool gpowercap_report_power_uw(struct gpowercap *gpowercap, u64 power_uw)
{
	KUNIT_STATIC_STUB_REDIRECT(gpowercap_report_power_uw, gpowercap, power_uw);

	struct gpowercap *parent = gpowercap->parent;
	struct gpowercap *child;
	bool all_updated = true;
	u64 total_power = 0;
	bool parent_rebalanced = false;

	gpowercap->current_power_uw = power_uw;
	gpowercap->power_updated = true;

	if (!parent)
		return false;

	mutex_lock(&parent->lock);
	list_for_each_entry(child, &parent->children, siblings) {
		if (!child->power_updated) {
			all_updated = false;
			break;
		}
		total_power += child->current_power_uw;
	}

	if (all_updated) {
		list_for_each_entry(child, &parent->children, siblings) {
			child->power_updated = false;
		}
		mutex_unlock(&parent->lock);

		parent_rebalanced = gpowercap_report_power_uw(parent, total_power);

		if (!parent_rebalanced && parent->ops && parent->ops->rebalance) {
			parent->ops->rebalance(parent);
			return true;
		}
		return parent_rebalanced;
	}
	mutex_unlock(&parent->lock);

	return false;
}

/**
 * gpowercap_init - Allocate and initialize a gpowercap struct.
 *
 * @gpowercap: The gpowercap struct pointer to be initialized
 * @ops: The gpowercap device specific ops, NULL for a virtual node
 */
void gpowercap_init(struct gpowercap *gpowercap, struct gpowercap_ops *ops)
{
	if (gpowercap) {
		INIT_LIST_HEAD(&gpowercap->children);
		INIT_LIST_HEAD(&gpowercap->siblings);
		gpowercap->ops = ops;
		mutex_init(&gpowercap->lock);
		INIT_DEFERRABLE_WORK(&gpowercap->bypass_work, __power_limit_bypass_clear_work);
	}
}
