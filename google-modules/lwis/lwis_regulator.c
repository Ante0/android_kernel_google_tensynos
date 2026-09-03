// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS Regulator Interface
 *
 * Copyright (c) 2018 Google, LLC
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-reg: " fmt

#include <linux/kernel.h>
#include <linux/slab.h>

#include "lwis_regulator.h"

#define REG_MODE_PREFIX_FAST "fast-"
#define REG_MODE_PREFIX_NORMAL "normal-"
#define REG_MODE_PREFIX_IDLE "idle-"
#define REG_MODE_PREFIX_STANDBY "standby-"

int lwis_regulator_list_add_info(struct device *dev, struct list_head *list, const char *name)
{
	struct lwis_regulator_info *reg_info;
	struct regulator *reg;

	/* Check regulator already exist or not */
	reg_info = lwis_regulator_get_info(list, name);
	if (!IS_ERR_OR_NULL(reg_info))
		return 0;

	/* Make sure regulator exists */
	reg = devm_regulator_get(dev, name);
	if (IS_ERR_OR_NULL(reg))
		return PTR_ERR(reg);

	reg_info = kmalloc(sizeof(struct lwis_regulator_info), GFP_KERNEL);
	if (!reg_info)
		return -ENOMEM;

	reg_info->reg = reg;
	strscpy(reg_info->name, name, LWIS_MAX_NAME_STRING_LEN);
	list_add(&reg_info->node, list);

	return 0;
}

int lwis_regulator_list_add_info_by_set_mode(struct device *dev, struct list_head *list,
					     const char *name)
{
	const char *reg_name;

	if (strncmp(REG_MODE_PREFIX_FAST, name, strlen(REG_MODE_PREFIX_FAST)) == 0) {
		reg_name = name + strlen(REG_MODE_PREFIX_FAST);
	} else if (strncmp(REG_MODE_PREFIX_NORMAL, name, strlen(REG_MODE_PREFIX_NORMAL)) == 0) {
		reg_name = name + strlen(REG_MODE_PREFIX_NORMAL);
	} else if (strncmp(REG_MODE_PREFIX_IDLE, name, strlen(REG_MODE_PREFIX_IDLE)) == 0) {
		reg_name = name + strlen(REG_MODE_PREFIX_IDLE);
	} else if (strncmp(REG_MODE_PREFIX_STANDBY, name, strlen(REG_MODE_PREFIX_STANDBY)) == 0) {
		reg_name = name + strlen(REG_MODE_PREFIX_STANDBY);
	} else {
		pr_err("Invalid regulator mode string %s\n", name);
		return -EINVAL;
	}

	return lwis_regulator_list_add_info(dev, list, reg_name);
}

void lwis_regulator_list_free(struct list_head *list)
{
	struct lwis_regulator_info *reg_node, *reg_node_tmp;

	list_for_each_entry_safe(reg_node, reg_node_tmp, list, node) {
		list_del(&reg_node->node);
		kfree(reg_node);
	}
}

struct lwis_regulator_info *lwis_regulator_get_info(struct list_head *list, const char *name)
{
	struct lwis_regulator_info *reg_node, *reg_node_tmp;

	list_for_each_entry_safe(reg_node, reg_node_tmp, list, node) {
		if (!strcmp(reg_node->name, name))
			return reg_node;
	}

	return ERR_PTR(-EINVAL);
}

int lwis_regulator_put(struct list_head *list, char *name)
{
	struct lwis_regulator_info *reg_info;

	/* Check regulator already exist or not */
	reg_info = lwis_regulator_get_info(list, name);
	if (IS_ERR_OR_NULL(reg_info))
		return -EINVAL;

	devm_regulator_put(reg_info->reg);
	return 0;
}

int lwis_regulator_put_all(struct list_head *list)
{
	struct lwis_regulator_info *reg_node, *reg_node_tmp;

	list_for_each_entry_safe(reg_node, reg_node_tmp, list, node) {
		devm_regulator_put(reg_node->reg);
	}

	return 0;
}

int lwis_regulator_enable(struct list_head *list, char *name)
{
	struct lwis_regulator_info *reg_info;

	/* Check regulator already exist or not */
	reg_info = lwis_regulator_get_info(list, name);
	if (IS_ERR_OR_NULL(reg_info))
		return -EINVAL;

	return regulator_enable(reg_info->reg);
}

int lwis_regulator_disable(struct list_head *list, char *name)
{
	struct lwis_regulator_info *reg_info;

	/* Check regulator already exist or not */
	reg_info = lwis_regulator_get_info(list, name);
	if (IS_ERR_OR_NULL(reg_info))
		return -EINVAL;

	return regulator_disable(reg_info->reg);
}

int lwis_regulator_set_mode(struct list_head *list, char *name)
{
	struct lwis_regulator_info *reg_info;
	char *reg_name;
	uint mode;

	if (strncmp(REG_MODE_PREFIX_FAST, name, strlen(REG_MODE_PREFIX_FAST)) == 0) {
		mode = REGULATOR_MODE_FAST;
		reg_name = name + strlen(REG_MODE_PREFIX_FAST);
	} else if (strncmp(REG_MODE_PREFIX_NORMAL, name, strlen(REG_MODE_PREFIX_NORMAL)) == 0) {
		mode = REGULATOR_MODE_NORMAL;
		reg_name = name + strlen(REG_MODE_PREFIX_NORMAL);
	} else if (strncmp(REG_MODE_PREFIX_IDLE, name, strlen(REG_MODE_PREFIX_IDLE)) == 0) {
		mode = REGULATOR_MODE_IDLE;
		reg_name = name + strlen(REG_MODE_PREFIX_IDLE);
	} else if (strncmp(REG_MODE_PREFIX_STANDBY, name, strlen(REG_MODE_PREFIX_STANDBY)) == 0) {
		mode = REGULATOR_MODE_STANDBY;
		reg_name = name + strlen(REG_MODE_PREFIX_STANDBY);
	} else {
		pr_err("Invalid regulator mode string %s\n", name);
		return -EINVAL;
	}

	/* Check regulator already exist or not */
	reg_info = lwis_regulator_get_info(list, reg_name);
	if (IS_ERR_OR_NULL(reg_info))
		return -EINVAL;

	return regulator_set_mode(reg_info->reg, mode);
}

void lwis_regulator_print(struct list_head *list)
{
	struct lwis_regulator_info *reg_node, *reg_node_tmp;

	list_for_each_entry_safe(reg_node, reg_node_tmp, list, node) {
		pr_info("lwis regulator: %s\n", reg_node->name);
	}
}
