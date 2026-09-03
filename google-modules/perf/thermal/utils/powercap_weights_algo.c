// SPDX-License-Identifier: GPL-2.0-only
/*
 * powercap_weights_algo.c driver providing weights based power distribution algorithm.
 *
 * Copyright (c) 2026, Google LLC. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/static_stub.h>
#include <kunit/visibility.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "google_powercap_helper.h"
#include "google_powercap_stats.h"
#include "powercap_weights_algo.h"

VISIBLE_IF_KUNIT int gpc_weights_of_property_count_strings(struct device_node *np,
								const char *propname)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_weights_of_property_count_strings, np, propname);
	return of_property_count_strings(np, propname);
}

VISIBLE_IF_KUNIT int gpc_weights_of_property_read_string_index(
	struct device_node *np, const char *propname, int index, const char **out_string)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_weights_of_property_read_string_index, np, propname, index,
				   out_string);
	return of_property_read_string_index(np, propname, index, out_string);
}

VISIBLE_IF_KUNIT int gpc_weights_kstrtou32(const char *s, unsigned int base, u32 *res)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_weights_kstrtou32, s, base, res);
	return kstrtou32(s, base, res);
}

static void __gpc_weights_algo_distribute(struct gpowercap_weights_algo *gpc_weights,
					  struct gpowercap *gpowercap,
					  u64 power_limit)
{
	struct gpowercap_weight_child *wc;
	u64 remaining_power;
	u32 remaining_weight;
	u64 total_surplus = 0;
	u32 total_receiver_weight = 0;
	u64 threshold_power;

	if (gpc_weights->total_weight == 0)
		return;

	if (power_limit >= gpc_weights->gpowercap.power_max) {
		list_for_each_entry(wc, &gpc_weights->weighted_children, node) {
			if (wc->gpc && wc->gpc->power_limit != wc->gpc->power_max)
				gpowercap_set_parent_power_limit(wc->gpc, wc->gpc->power_max);
		}
		goto update_stats;
	}

	if (power_limit <= gpc_weights->gpowercap.power_min) {
		list_for_each_entry(wc, &gpc_weights->weighted_children, node) {
			if (wc->gpc && wc->gpc->power_limit != wc->gpc->power_min)
				gpowercap_set_parent_power_limit(wc->gpc, wc->gpc->power_min);
		}
		goto update_stats;
	}

	remaining_power = power_limit;
	remaining_weight = gpc_weights->total_weight;

	/* Pass 1: Calculate base shares and identify donors */
	list_for_each_entry(wc, &gpc_weights->weighted_children, node) {
		if (!wc->gpc)
			continue;

		if (remaining_weight == 0) {
			wc->limit_uw = 0;
			continue;
		}

		wc->limit_uw = div_u64(remaining_power * wc->weight, remaining_weight);
		remaining_power -= wc->limit_uw;
		remaining_weight -= wc->weight;

		if (wc->limit_uw > wc->gpc->userspace_power_limit) {
			total_surplus += (wc->limit_uw - wc->gpc->userspace_power_limit);
			wc->limit_uw = wc->gpc->userspace_power_limit;
		}

		wc->current_power_uw = wc->gpc->current_power_uw;

		pr_debug("Node[%s]: child[%s] base_limit:%llu current:%llu\n",
			 gpowercap->zone.name, wc->gpc->zone.name, wc->limit_uw,
			 wc->current_power_uw);

		threshold_power = div_u64(wc->limit_uw *
						gpc_weights->redistribution_threshold,
					100);
		wc->is_receiver = false;
		/*
		 * If mitigation is increasing (tightening), check for under-utilization.
		 * Donors are nodes that are being squeezed but still have headroom.
		 */
		if (wc->limit_uw <= wc->gpc->power_limit &&
		    wc->current_power_uw < threshold_power) {
			total_surplus += (wc->limit_uw - wc->current_power_uw);
		} else if (wc->current_power_uw >= threshold_power) {
			if (wc->limit_uw < wc->gpc->userspace_power_limit) {
				total_receiver_weight += wc->weight;
				wc->is_receiver = true;
			}
		}
	}

	pr_debug("Node[%s]: total_surplus:%llu total_receiver_weight:%u\n",
		 gpowercap->zone.name, total_surplus, total_receiver_weight);
	/* Pass 2: Distribute surplus to receivers */
	if (total_surplus > 0 && total_receiver_weight > 0) {
		u64 remaining_surplus = total_surplus;
		u32 remaining_receiver_weight = total_receiver_weight;
		u64 distributed_this_round;
		u64 pass_surplus;
		u32 pass_weight;

		do {
			pass_surplus = remaining_surplus;
			pass_weight = remaining_receiver_weight;

			distributed_this_round = 0;

			list_for_each_entry(wc, &gpc_weights->weighted_children, node) {
				if (!wc->gpc || !wc->is_receiver || !pass_weight)
					continue;

				u64 extra = div_u64(pass_surplus * wc->weight, pass_weight);
				u64 space = wc->gpc->userspace_power_limit - wc->limit_uw;
				u64 apply = min_t(u64, extra, space);

				wc->limit_uw += apply;
				remaining_surplus -= apply;
				pass_surplus -= apply;
				pass_weight -= wc->weight;
				distributed_this_round += apply;

				if (wc->limit_uw >= wc->gpc->userspace_power_limit) {
					wc->is_receiver = false;
					remaining_receiver_weight -= wc->weight;
				}
			}
		} while (remaining_surplus > 0 && remaining_receiver_weight > 0 &&
			 distributed_this_round > 0);
	}

	/* Pass 3: Apply limits */
	list_for_each_entry(wc, &gpc_weights->weighted_children, node) {
		if (!wc->gpc)
			continue;

		pr_debug("Node[%s]: child[%s] new limit:%llu\n",
				 gpowercap->zone.name, wc->gpc->zone.name, wc->limit_uw);
		gpowercap_set_parent_power_limit(wc->gpc, wc->limit_uw);
	}

update_stats:
	gpc_stats_update(gpowercap, GPC_STAT_POWER_LIMIT, power_limit);
}

VISIBLE_IF_KUNIT u64 __gpc_weights_algo_set_power_limit(struct gpowercap *gpowercap,
							      u64 power_limit)
{
	struct gpowercap_weights_algo *gpc_weights = to_gpowercap_weights_algo(gpowercap);

	mutex_lock(&gpc_weights->lock);

	power_limit = clamp_val(power_limit, gpc_weights->gpowercap.power_min,
					gpc_weights->gpowercap.power_max);

	__gpc_weights_algo_distribute(gpc_weights, gpowercap, power_limit);

	mutex_unlock(&gpc_weights->lock);
	return power_limit;
}

VISIBLE_IF_KUNIT int __gpc_weights_algo_rebalance(struct gpowercap *gpowercap)
{
	struct gpowercap_weights_algo *gpc_weights = to_gpowercap_weights_algo(gpowercap);

	mutex_lock(&gpc_weights->lock);
	__gpc_weights_algo_distribute(gpc_weights, gpowercap, gpowercap->power_limit);
	mutex_unlock(&gpc_weights->lock);

	return 0;
}

VISIBLE_IF_KUNIT u64 __gpc_weights_algo_get_power(struct gpowercap *gpowercap)
{
	struct gpowercap *child;
	u64 power = 0, child_power = 0;
	int ret = 0;

	gpowercap_for_each_children(gpowercap, child) {
		ret = __get_power_uw(child, &child_power);
		if (!ret)
			power += child_power;
	}

	return power;
}

VISIBLE_IF_KUNIT int __gpc_weights_algo_update_power_uw(struct gpowercap *gpowercap)
{
	/*
	 * This is a virtual node. Power is derived from children. At registration
	 * time, there are no children, so power is 0. When children are added,
	 * evaluate() will be called to update the power values.
	 */
	return 0;
}

VISIBLE_IF_KUNIT int __gpc_weights_algo_evaluate(struct gpowercap *gpowercap)
{
	struct gpowercap_weights_algo *gpc_weights = to_gpowercap_weights_algo(gpowercap);
	struct gpowercap *child;
	struct gpowercap_weight_child *wc;
	u64 power_max = 0;
	bool found;

	if (!gpowercap)
		return -EINVAL;

	mutex_lock(&gpc_weights->lock);

	/* Unlink stale children */
	list_for_each_entry(wc, &gpc_weights->weighted_children, node) {
		if (!wc->gpc)
			continue;
		found = false;
		gpowercap_for_each_children(gpowercap, child) {
			if (wc->gpc == child) {
				found = true;
				break;
			}
		}
		if (!found)
			wc->gpc = NULL;
	}

	/* Link new children to existing entries or add new entries */
	gpowercap_for_each_children(gpowercap, child) {
		found = false;
		list_for_each_entry(wc, &gpc_weights->weighted_children, node) {
			if (wc->gpc == child) {
				found = true;
				break;
			}
		}
		if (found)
			continue;

		/* New child, try to find a pre-populated slot */
		list_for_each_entry(wc, &gpc_weights->weighted_children, node) {
			if (!wc->gpc && wc->name && child->zone.name &&
			    !strcmp(wc->name, child->zone.name)) {
				wc->gpc = child;
				found = true;
				break;
			}
		}

		if (!found) {
			struct gpowercap_weight_child *new_wc;

			new_wc = kzalloc(sizeof(*new_wc), GFP_KERNEL);
			if (!new_wc)
				continue;

			new_wc->name = kstrdup(child->zone.name, GFP_KERNEL);
			if (!new_wc->name) {
				kfree(new_wc);
				continue;
			}
			pr_warn("Node[%s]: child[%s] not defined in devicetree. Default weight.\n",
				gpowercap->zone.name, child->zone.name);
			new_wc->weight = GPC_WEIGHTS_ALGO_DEFAULT_WEIGHT;
			new_wc->gpc = child;
			list_add_tail(&new_wc->node, &gpc_weights->weighted_children);
		}
	}

	gpc_weights->total_weight = 0;
	list_for_each_entry(wc, &gpc_weights->weighted_children, node) {
		if (wc->gpc)
			gpc_weights->total_weight += wc->weight;
	}

	gpowercap_for_each_children(gpowercap, child) {
		power_max += child->power_max;
	}
	gpowercap->power_min = 0;
	gpowercap->power_max = power_max;
	if (!test_bit(GPOWERCAP_PARENT_LIMIT_FLAG, &gpowercap->flags))
		gpowercap->parent_power_limit = gpowercap->power_max;
	if (!test_bit(GPOWERCAP_USERSPACE_LIMIT_FLAG, &gpowercap->flags))
		gpowercap->userspace_power_limit = gpowercap->power_max;
	if (!test_bit(GPOWERCAP_POWER_LIMIT_FLAG, &gpowercap->flags))
		gpowercap->power_limit = gpowercap->power_max;

	if (gpowercap->opp_table) {
		gpowercap->opp_table[0].power = 0;
		gpowercap->opp_table[1].power = power_max;
	}

	mutex_unlock(&gpc_weights->lock);

	return 0;
}



VISIBLE_IF_KUNIT ssize_t power_distribution_weights_show(struct device *dev,
					       struct device_attribute *attr, char *buf)
{
	struct gpowercap *gpowercap = to_gpowercap(to_powercap_zone(dev));
	struct gpowercap_weights_algo *gpc_weights = to_gpowercap_weights_algo(gpowercap);
	struct gpowercap_weight_child *wc;
	int count = 0;

	mutex_lock(&gpc_weights->lock);
	list_for_each_entry(wc, &gpc_weights->weighted_children, node) {
		if (wc->name)
			count += sysfs_emit_at(buf, count, "%s : %u\n", wc->name, wc->weight);
	}
	mutex_unlock(&gpc_weights->lock);

	return count;
}

VISIBLE_IF_KUNIT ssize_t power_distribution_weights_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct gpowercap *gpowercap = to_gpowercap(to_powercap_zone(dev));
	struct gpowercap_weights_algo *gpc_weights = to_gpowercap_weights_algo(gpowercap);
	char *p, *orig_buf;
	int ret = 0;
	struct gpowercap_weight_child *wc;
	int n_children = 0;
	struct {
		char *name;
		u32 weight;
	} *parsed_weights = NULL;
	int parsed_count = 0;
	char *token;
	int i;

	orig_buf = kstrndup(buf, count, GFP_KERNEL);
	if (!orig_buf)
		return -ENOMEM;
	p = strim(orig_buf);

	mutex_lock(&gpc_weights->lock);

	list_for_each_entry(wc, &gpc_weights->weighted_children, node)
		n_children++;
	if (n_children > 0) {
		parsed_weights = kcalloc(n_children, sizeof(*parsed_weights), GFP_KERNEL);
		if (!parsed_weights) {
			ret = -ENOMEM;
			goto out;
		}
	}

	while ((token = strsep(&p, ",")) != NULL) {
		char *name, *weight_str;

		if (parsed_count >= n_children) {
			pr_err("Too many weight entries\n");
			ret = -EINVAL;
			goto out;
		}

		weight_str = strchr(token, ':');
		if (!weight_str) {
			ret = -EINVAL;
			goto out;
		}
		*weight_str = '\0';
		weight_str++;

		name = strim(token);
		weight_str = strim(weight_str);
		if (gpc_weights_kstrtou32(weight_str, 10, &parsed_weights[parsed_count].weight) ||
		    parsed_weights[parsed_count].weight == 0) {
			ret = -EINVAL;
			goto out;
		}
		parsed_weights[parsed_count].name = name;
		parsed_count++;
	}

	for (i = 0; i < parsed_count; i++) {
		bool found = false;

		list_for_each_entry(wc, &gpc_weights->weighted_children, node) {
			if (wc->name && !strcmp(wc->name, parsed_weights[i].name)) {
				found = true;
				break;
			}
		}

		if (!found) {
			ret = -EINVAL;
			goto out;
		}
	}

	for (i = 0; i < parsed_count; i++) {
		list_for_each_entry(wc, &gpc_weights->weighted_children, node) {
			if (wc->name && !strcmp(wc->name, parsed_weights[i].name)) {
				wc->weight = parsed_weights[i].weight;
				break;
			}
		}
	}

	gpc_weights->total_weight = 0;
	list_for_each_entry(wc, &gpc_weights->weighted_children, node)
		gpc_weights->total_weight += wc->weight;

	ret = count;
out:
	mutex_unlock(&gpc_weights->lock);
	kfree(parsed_weights);
	kfree(orig_buf);
	return ret;
}

static DEVICE_ATTR_RW(power_distribution_weights);

VISIBLE_IF_KUNIT ssize_t power_redistribution_threshold_show(struct device *dev,
						       struct device_attribute *attr, char *buf)
{
	struct gpowercap *gpowercap = to_gpowercap(to_powercap_zone(dev));
	struct gpowercap_weights_algo *gpc_weights = to_gpowercap_weights_algo(gpowercap);
	int count;

	mutex_lock(&gpc_weights->lock);
	count = sysfs_emit(buf, "%u\n", gpc_weights->redistribution_threshold);
	mutex_unlock(&gpc_weights->lock);

	return count;
}

VISIBLE_IF_KUNIT ssize_t power_redistribution_threshold_store(struct device *dev,
							struct device_attribute *attr,
							const char *buf, size_t count)
{
	struct gpowercap *gpowercap = to_gpowercap(to_powercap_zone(dev));
	struct gpowercap_weights_algo *gpc_weights = to_gpowercap_weights_algo(gpowercap);
	u32 threshold;
	int ret;

	ret = gpc_weights_kstrtou32(buf, 10, &threshold);
	if (ret)
		return ret;

	if (threshold == 0 || threshold > 100)
		return -EINVAL;

	mutex_lock(&gpc_weights->lock);
	gpc_weights->redistribution_threshold = threshold;
	mutex_unlock(&gpc_weights->lock);

	return count;
}

static DEVICE_ATTR_RW(power_redistribution_threshold);

VISIBLE_IF_KUNIT void __gpc_weights_algo_release(struct gpowercap *gpowercap)
{
	struct gpowercap_weights_algo *gpc_weights = to_gpowercap_weights_algo(gpowercap);
	struct gpowercap_weight_child *wc, *tmp;

	gpc_device_remove_file(&gpowercap->zone.dev, &dev_attr_power_redistribution_threshold);
	gpc_device_remove_file(&gpowercap->zone.dev, &dev_attr_power_distribution_weights);

	list_for_each_entry_safe(wc, tmp, &gpc_weights->weighted_children, node) {
		list_del(&wc->node);
		kfree(wc->name);
		kfree(wc);
	}

	kfree(gpowercap->opp_table);
	kfree(gpc_weights);
}

static struct gpowercap_ops gpc_weights_algo_ops = {
	.set_power_uw = __gpc_weights_algo_set_power_limit,
	.get_power_uw = __gpc_weights_algo_get_power,
	.update_power_uw = __gpc_weights_algo_update_power_uw,
	.evaluate = __gpc_weights_algo_evaluate,
	.release = __gpc_weights_algo_release,
	.rebalance = __gpc_weights_algo_rebalance,
};

VISIBLE_IF_KUNIT struct gpowercap *__gpc_weights_algo_setup(struct device_node *dn,
								   struct gpowercap *parent)
{
	if (!dn || !parent)
		return ERR_PTR(-EINVAL);

	int ret = 0;
	struct gpowercap_weights_algo *gpc_weights;
	int count, i;
	const char *name = dn->name;

	gpc_weights = kzalloc(sizeof(*gpc_weights), GFP_KERNEL);
	if (!gpc_weights)
		return ERR_PTR(-ENOMEM);
	mutex_init(&gpc_weights->lock);
	INIT_LIST_HEAD(&gpc_weights->weighted_children);
	gpc_weights->redistribution_threshold = GPC_WEIGHTS_ALGO_RECEIVER_THRESHOLD_PERCENT;

	gpc_weights->gpowercap.opp_table =
		kcalloc(GPC_WEIGHTS_ALGO_NUM_OPPS, sizeof(*gpc_weights->gpowercap.opp_table),
			GFP_KERNEL);
	if (!gpc_weights->gpowercap.opp_table) {
		kfree(gpc_weights);
		return ERR_PTR(-ENOMEM);
	}
	gpc_weights->gpowercap.num_opps = GPC_WEIGHTS_ALGO_NUM_OPPS;

	count = gpc_weights_of_property_count_strings(dn, "power-distribution-weights");
	if (count > 0 && count % 2 == 0) {
		for (i = 0; i < count; i += 2) {
			struct gpowercap_weight_child *wc;
			const char *name_str;
			const char *weight_str;

			if (gpc_weights_of_property_read_string_index(
				dn, "power-distribution-weights", i, &name_str) ||
			    gpc_weights_of_property_read_string_index(
				dn, "power-distribution-weights", i + 1, &weight_str)) {
				pr_err("Failed to read weight pair at index %d for %s\n", i, name);
				continue;
			}

			wc = kzalloc(sizeof(*wc), GFP_KERNEL);
			if (!wc)
				continue;

			wc->name = kstrdup(name_str, GFP_KERNEL);
			if (!wc->name) {
				kfree(wc);
				continue;
			}

			if (gpc_weights_kstrtou32(weight_str, 10, &wc->weight) || wc->weight == 0) {
				pr_err("Invalid weight value '%s' for child '%s' in %s\n",
				       weight_str, name_str, name);
				kfree(wc->name);
				kfree(wc);
				continue;
			}
			list_add_tail(&wc->node, &gpc_weights->weighted_children);
		}
	} else if (count > 0) {
		pr_err("Invalid power-distribution-weights in %s, must be pairs of strings\n",
		       name);
	}

	gpowercap_init(&gpc_weights->gpowercap, &gpc_weights_algo_ops);
	ret = gpowercap_register(name, &gpc_weights->gpowercap, parent);
	if (ret) {
		__gpc_weights_algo_release(&gpc_weights->gpowercap);
		return ERR_PTR(ret);
	}

	ret = gpc_device_create_file(&gpc_weights->gpowercap.zone.dev,
				     &dev_attr_power_distribution_weights);
	if (ret) {
		pr_err("Failed to create power_distribution_weights sysfs for %s\n", name);
		gpowercap_unregister(&gpc_weights->gpowercap);
		return ERR_PTR(ret);
	}

	ret = gpc_device_create_file(&gpc_weights->gpowercap.zone.dev,
				     &dev_attr_power_redistribution_threshold);
	if (ret) {
		pr_err("Failed to create power_redistribution_threshold sysfs for %s\n", name);
		gpc_device_remove_file(&gpc_weights->gpowercap.zone.dev,
				       &dev_attr_power_distribution_weights);
		gpowercap_unregister(&gpc_weights->gpowercap);
		return ERR_PTR(ret);
	}

	return &gpc_weights->gpowercap;
}

struct gpowercap_subsys_ops gpc_virt_weights_dev_ops = {
	.name = "powercap_weights_algo",
	.algo_setup = __gpc_weights_algo_setup,
};
