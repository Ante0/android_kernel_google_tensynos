// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS PWRSEQ Interface
 *
 * Copyright (c) 2025 Google, LLC
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-pwrseq: " fmt

#include <linux/kernel.h>
#include <linux/slab.h>

#include "lwis_pwrseq.h"
#include "lwis_device_pwrseq.h"

int lwis_pwrseq_list_add_info(struct device *dev, struct list_head *list, const char *name)
{
	struct lwis_pwrseq_info *pwrseq_info;
	struct pwrseq_desc *pwrseq;

	/* Check pwrseq already exist or not */
	pwrseq_info = lwis_pwrseq_get_info(list, name);
	if (pwrseq_info)
		return 0;

	/*
	 * Since we use devm_pwrseq_get to get the pwrseq, so we won't need to
	 * call pwrseq_put() by ourselves. devm will help to call pwrseq_put()
	 * when device release.
	 */
	pwrseq = lwis_devm_pwrseq_get(dev, name);
	if (IS_ERR(pwrseq))
		return PTR_ERR(pwrseq);

	pwrseq_info = kmalloc(sizeof(struct lwis_pwrseq_info), GFP_KERNEL);
	if (!pwrseq_info)
		return -ENOMEM;

	pwrseq_info->pwrseq = pwrseq;
	strscpy(pwrseq_info->name, name, LWIS_MAX_NAME_STRING_LEN);
	list_add(&pwrseq_info->node, list);

	return 0;
}

void lwis_pwrseq_list_free(struct list_head *list)
{
	struct lwis_pwrseq_info *pwrseq_node, *pwrseq_node_tmp;

	list_for_each_entry_safe(pwrseq_node, pwrseq_node_tmp, list, node) {
		list_del(&pwrseq_node->node);
		kfree(pwrseq_node);
	}
}

struct lwis_pwrseq_info *lwis_pwrseq_get_info(struct list_head *list, const char *name)
{
	struct lwis_pwrseq_info *pwrseq_node;

	list_for_each_entry(pwrseq_node, list, node) {
		if (!strcmp(pwrseq_node->name, name))
			return pwrseq_node;
	}

	return NULL;
}

int lwis_pwrseq_enable(struct list_head *list, const char *name)
{
	struct lwis_pwrseq_info *pwrseq_info;

	/* Check pwrseq already exist or not */
	pwrseq_info = lwis_pwrseq_get_info(list, name);
	if (!pwrseq_info)
		return -EINVAL;

	return pwrseq_power_on(pwrseq_info->pwrseq);
}

int lwis_pwrseq_disable(struct list_head *list, const char *name)
{
	struct lwis_pwrseq_info *pwrseq_info;

	/* Check pwrseq already exist or not */
	pwrseq_info = lwis_pwrseq_get_info(list, name);
	if (!pwrseq_info)
		return -EINVAL;

	return pwrseq_power_off(pwrseq_info->pwrseq);
}

void lwis_pwrseq_print(struct list_head *list)
{
	struct lwis_pwrseq_info *pwrseq_node;

	list_for_each_entry(pwrseq_node, list, node) {
		pr_info("%s\n", pwrseq_node->name);
	}
}
