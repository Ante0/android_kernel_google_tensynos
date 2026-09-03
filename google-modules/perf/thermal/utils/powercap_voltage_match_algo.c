// SPDX-License-Identifier: GPL-2.0-only
/*
 * powercap_voltage_match_algo.c driver providing voltage based mitigation algorithm.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */

#define pr_fmt(fmt) "powercap_voltage_match_algo: " fmt
#include <kunit/static_stub.h>
#include <linux/of.h>

#include "powercap_voltage_match_algo.h"
#include "google_powercap_helper.h"
#include "google_powercap_stats.h"

#define MAX_CHILD_CT 2

struct powercap_volt_algo_platform_data {
	const void *freq_table;
	unsigned int num_opps;
	unsigned int num_children;
};

struct device_node *gpc_of_find_node_by_path(const char *path)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_of_find_node_by_path, path);
	return of_find_node_by_path(path);
}

void gpc_of_node_put(struct device_node *np)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_of_node_put, np);
	return of_node_put(np);
}

const struct of_device_id *gpc_match_of_node(const struct of_device_id *matches,
					      const struct device_node *node)
{
	KUNIT_STATIC_STUB_REDIRECT(gpc_match_of_node, matches, node);
	return of_match_node(matches, node);
}

u64 __gpc_volt_algo_set_power_limit(struct gpowercap *gpowercap, u64 power_limit)
{
	struct gpowercap_volt_algo *gpc_volt = to_gpowercap_volt_algo(gpowercap);
	int i = 0, child_idx = 0;
	struct gpowercap *child;
	u64 power;

	mutex_lock(&gpc_volt->lock);
	if (!gpc_volt->opp_table) {
		mutex_unlock(&gpc_volt->lock);
		return 0;
	}

	for (i = 0; i < gpc_volt->num_opps; i++) {
		if (gpc_volt->opp_table[i].power >= power_limit)
			break;
	}
	if (i >= gpc_volt->num_opps)
		i = gpc_volt->num_opps - 1;

	power = gpc_volt->opp_table[i].power;
	gpowercap_for_each_children(gpowercap, child) {
		pr_debug("[%s] set_power_limit total:%u child idx:%d child power:%u.\n",
			 gpowercap->zone.name, gpc_volt->opp_table[i].power, child_idx,
			 gpc_volt->opp_table[i].children_power[child_idx]);
		gpowercap_set_parent_power_limit(child,
			gpc_volt->opp_table[i].children_power[child_idx++]);
	}
	mutex_unlock(&gpc_volt->lock);
	gpc_stats_update(gpowercap, GPC_STAT_POWER_LIMIT, power);

	return power;
}

u64 __gpc_volt_algo_get_power(struct gpowercap *gpowercap)
{
	struct gpowercap *child;
	u64 power = 0, child_power = 0;
	int ret = 0;

	gpowercap_for_each_children(gpowercap, child) {
		ret = __get_power_uw(child, &child_power);
		if (!ret)
			power += child_power;
		pr_debug("[%s] get_power total:%llu child power:%llu.\n",
			 gpowercap->zone.name, power, child_power);
	}
	return power;
}

int __gpc_volt_algo_update_power_uw(struct gpowercap *gpowercap)
{
	struct gpowercap_volt_algo *gpc_volt = to_gpowercap_volt_algo(gpowercap);

	gpowercap->power_min = gpc_volt->num_opps ? gpc_volt->opp_table[0].power : 0;
	gpowercap->power_max = gpc_volt->num_opps ?
		gpc_volt->opp_table[gpc_volt->num_opps - 1].power : 0;
	gpowercap->num_opps = gpc_volt->num_opps;
	gpowercap->opp_table = gpc_volt->opp_table;

	return 0;
}

static void __gpc_free_opp_table_mem(struct gpowercap_volt_algo *gpc_volt)
{
	int i = 0;

	if (!gpc_volt)
		return;
	for (; i < gpc_volt->num_opps; i++)
		kfree(gpc_volt->opp_table[i].children_power);
	kfree(gpc_volt->opp_table);
}

static int __gpc_allocate_opp_table_mem(struct gpowercap_volt_algo *gpc_volt, unsigned int child_ct)
{
	int i = 0, opp_ct;

	if (!gpc_volt || !child_ct || !gpc_volt->num_opps)
		return -EINVAL;

	opp_ct = gpc_volt->num_opps;
	gpc_volt->opp_table = kcalloc(opp_ct, sizeof(*gpc_volt->opp_table), GFP_KERNEL);
	if (!gpc_volt->opp_table)
		return -ENOMEM;

	for (; i < opp_ct; i++) {
		gpc_volt->opp_table[i].children_power =
				kcalloc(child_ct, sizeof(*gpc_volt->opp_table[i].children_power),
					GFP_KERNEL);
		if (!gpc_volt->opp_table[i].children_power)
			goto free_memory;
	}

	return 0;
free_memory:
	__gpc_free_opp_table_mem(gpc_volt);

	return -ENOMEM;
}

static unsigned int lga_children_freq[][MAX_CHILD_CT] = {
	/* BIG-MID, BIG freq in KHz */
	{ 177000, 266000 },
	{ 400000, 533000 },
	{ 533000, 800000 },
	{ 652000, 883000 },
	{ 729000, 1036000 },
	{ 921000, 1152000 },
	{ 1075000, 1305000 },
	{ 1267000, 1420000 },
	{ 1401000, 1593000 },
	{ 1536000, 1766000 },
	{ 1670000, 1920000 },
	{ 1785000, 1920000 },
	{ 1862000, 2208000 },
	{ 1939000, 2342000 },
	{ 2092000, 2457000 },
	{ 2188000, 2592000 },
	{ 2284000, 2707000 },
	{ 2400000, 2707000 },
	{ 2534000, 2937000 },
	{ 2688000, 3168000 },
	{ 2841000, 3168000 },
	{ 2937000, 3398000 },
	{ 3052000, 3782400 },
};

static const struct powercap_volt_algo_platform_data lga_platform_data = {
	.freq_table = lga_children_freq,
	.num_opps = ARRAY_SIZE(lga_children_freq),
	.num_children = MAX_CHILD_CT,
};

const struct of_device_id powercap_volt_algo_platform_table[] = {
	{
		.compatible = "google,lga",
		.data = &lga_platform_data,
	},
	{}
};

static int __gpc_find_opp_for_freq(struct gpowercap *gpc, unsigned int freq)
{
	int i;

	if (!gpc || !gpc->opp_table)
		return -EINVAL;

	for (i = 0; i < gpc->num_opps; i++) {
		if (gpc->opp_table[i].freq >= freq)
			return i;
	}

	/* If no frequency is high enough, return the highest OPP */
	return gpc->num_opps - 1;
}

static int __gpc_load_static_table(struct gpowercap_volt_algo *gpc_volt,
				       const struct powercap_volt_algo_platform_data *pdata,
				       struct gpowercap *child_gpc[],
				       unsigned int child_ct)
{
	const int num_opps = pdata->num_opps;
	const unsigned int (*freq_table)[MAX_CHILD_CT] = pdata->freq_table;
	int i, j, ret, opp_idx;
	struct cdev_opp_table *child_opp;

	if (child_ct != pdata->num_children) {
		pr_err("Static table expects %u children, found %u\n",
		       pdata->num_children, child_ct);
		return -EINVAL;
	}

	if (gpc_volt->opp_table) {
		__gpc_free_opp_table_mem(gpc_volt);
		gpc_volt->opp_table = NULL;
	}
	gpc_volt->num_opps = num_opps;

	ret = __gpc_allocate_opp_table_mem(gpc_volt, child_ct);
	if (ret) {
		gpc_volt->num_opps = 0;
		return ret;
	}

	for (i = 0; i < num_opps; i++) {
		for (j = 0; j < child_ct; j++) {
			opp_idx = __gpc_find_opp_for_freq(child_gpc[j], freq_table[i][j]);
			if (opp_idx < 0) {
				ret = opp_idx;
				goto free_memory;
			}
			child_opp = &child_gpc[j]->opp_table[opp_idx];
			gpc_volt->opp_table[i].power += child_opp->power;
			gpc_volt->opp_table[i].children_power[j] = child_opp->power;
		}
	}

	return 0;

free_memory:
	__gpc_free_opp_table_mem(gpc_volt);
	gpc_volt->opp_table = NULL;
	gpc_volt->num_opps = 0;
	return ret;
}

int __gpc_volt_algo_evaluate(struct gpowercap *gpowercap)
{
	struct gpowercap_volt_algo *gpc_volt = to_gpowercap_volt_algo(gpowercap);
	int ret = 0, i = 0;
	unsigned int child_ct;
	struct gpowercap **children_gpc;
	struct gpowercap *child;
	const struct of_device_id *match;
	const struct powercap_volt_algo_platform_data *pdata;
	struct device_node *np = NULL;

	mutex_lock(&gpc_volt->lock);
	child_ct = list_count_nodes(&gpowercap->children);

	if (!child_ct) {
		// do cleanup.
		pr_err("No children for the powercap:%s\n", gpowercap->zone.name);
		goto unlock_exit;
	}

	children_gpc = kcalloc(child_ct, sizeof(*children_gpc), GFP_KERNEL);
	if (!children_gpc) {
		ret = -ENOMEM;
		goto unlock_exit;
	}

	gpowercap_for_each_children(gpowercap, child) {
		children_gpc[i++] = child;
	}

	np = gpc_of_find_node_by_path("/");
	if (!np) {
		ret = -ENODEV;
		goto free_mem_exit;
	}

	match = gpc_match_of_node(powercap_volt_algo_platform_table, np);
	gpc_of_node_put(np);

	if (match && match->data) {
		pdata = match->data;
		ret = __gpc_load_static_table(gpc_volt, pdata, children_gpc, child_ct);
		if (ret) {
			pr_err("powercap:%s failed to load static table:%d\n",
			       gpowercap->zone.name, ret);
			goto free_mem_exit;
		}
		__gpc_volt_algo_update_power_uw(gpowercap);

		if (!test_bit(GPOWERCAP_PARENT_LIMIT_FLAG, &gpowercap->flags))
			gpowercap->parent_power_limit = gpowercap->power_max;
		if (!test_bit(GPOWERCAP_USERSPACE_LIMIT_FLAG, &gpowercap->flags))
			gpowercap->userspace_power_limit = gpowercap->power_max;
		if (!test_bit(GPOWERCAP_POWER_LIMIT_FLAG, &gpowercap->flags))
			gpowercap->power_limit = gpowercap->power_max;
	} else {
		pr_err("powercap:%s: No compatible platform data found.\n",
		       gpowercap->zone.name);
		ret = -ENODEV;
	}

free_mem_exit:
	kfree(children_gpc);
unlock_exit:
	mutex_unlock(&gpc_volt->lock);
	return ret;
}

void __gpc_volt_algo_release(struct gpowercap *gpowercap)
{
	struct gpowercap_volt_algo *gpc_volt = to_gpowercap_volt_algo(gpowercap);

	__gpc_free_opp_table_mem(gpc_volt);
	kfree(gpc_volt);
}

static struct gpowercap_ops gpc_volt_algo_ops = {
	.set_power_uw		= __gpc_volt_algo_set_power_limit,
	.get_power_uw		= __gpc_volt_algo_get_power,
	.update_power_uw	= __gpc_volt_algo_update_power_uw,
	.evaluate		= __gpc_volt_algo_evaluate,
	.release		= __gpc_volt_algo_release,
};

struct gpowercap *__gpc_volt_algo_setup(struct device_node *dn, struct gpowercap *parent)
{
	if (!dn || !parent)
		return ERR_PTR(-EINVAL);

	int ret = 0;
	struct gpowercap_volt_algo *gpc_volt;
	const char *name = dn->name;

	gpc_volt = kzalloc(sizeof(*gpc_volt), GFP_KERNEL);
	if (!gpc_volt)
		return ERR_PTR(-ENOMEM);
	mutex_init(&gpc_volt->lock);
	gpowercap_init(&gpc_volt->gpowercap, &gpc_volt_algo_ops);
	ret = gpowercap_register(name, &gpc_volt->gpowercap, parent);
	if (ret)
		goto out_kfree_exit;

	return &gpc_volt->gpowercap;

out_kfree_exit:
	kfree(gpc_volt);
	return ERR_PTR(ret);
}

struct gpowercap_subsys_ops gpc_virt_volt_dev_ops = {
	.name = "powercap_voltage_match_algo",
	.algo_setup = __gpc_volt_algo_setup,
};
