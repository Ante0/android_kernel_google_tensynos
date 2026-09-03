// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS PWRSEQ Device Driver
 *
 * Copyright (c) 2025 Google, LLC
 */
#define pr_fmt(fmt) KBUILD_MODNAME "-pwrseq-dev: " fmt

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pwrseq/provider.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <linux/slab.h>
#include <uapi/linux/sched/types.h>

#include "lwis_commands.h"
#include "lwis_device.h"
#include "lwis_device_pwrseq.h"
#include "lwis_platform.h"
#include "lwis_pwrseq.h"
#include "lwis_trace.h"
#include "lwis_util.h"

#ifdef CONFIG_OF
#include "lwis_dt.h"
#endif

#define LWIS_DRIVER_NAME "lwis-pwrseq"

static struct lwis_device_subclass_operations pwrseq_vops = {};

/* Global lock and target name for matching */
static DEFINE_MUTEX(lwis_pwrseq_match_lock);
static const char *lwis_pwrseq_match_target;

/*
 * Helper to set the target before calling get(). This is a workaround for the
 * pwrseq framework not passing the target name to the match function.
 */
struct pwrseq_desc *lwis_devm_pwrseq_get(struct device *dev, const char *target)
{
	struct pwrseq_desc *desc;

	mutex_lock(&lwis_pwrseq_match_lock);
	lwis_pwrseq_match_target = target;

	desc = devm_pwrseq_get(dev, target);

	lwis_pwrseq_match_target = NULL;
	mutex_unlock(&lwis_pwrseq_match_lock);

	return desc;
}

static int lwis_pwrseq_device_enable(struct pwrseq_device *pwrseq)
{
	struct lwis_pwrseq_device *pwrseq_dev = pwrseq_device_get_drvdata(pwrseq);
	struct lwis_device *lwis_dev;
	int ret;

	if (!pwrseq || !pwrseq_dev)
		return -EINVAL;

	lwis_dev = &pwrseq_dev->base_dev;
	mutex_lock(&lwis_dev->client_lock);
	if (lwis_dev->enabled > 0) {
		if (lwis_dev->enabled == INT_MAX) {
			dev_err(lwis_dev->dev, "Enable counter has reached INT_MAX\n");
			mutex_unlock(&lwis_dev->client_lock);
			return -EINVAL;
		}
		lwis_dev->enabled++;
		mutex_unlock(&lwis_dev->client_lock);
		return 0;
	}

	ret = lwis_dev_power_up_locked(lwis_dev);
	if (!ret)
		lwis_dev->enabled++;
	mutex_unlock(&lwis_dev->client_lock);

	return ret;
}

static int lwis_pwrseq_device_disable(struct pwrseq_device *pwrseq)
{
	struct lwis_pwrseq_device *pwrseq_dev = pwrseq_device_get_drvdata(pwrseq);
	struct lwis_device *lwis_dev;
	int ret;

	if (!pwrseq || !pwrseq_dev)
		return -EINVAL;

	lwis_dev = &pwrseq_dev->base_dev;
	mutex_lock(&lwis_dev->client_lock);

	if (lwis_dev->enabled > 1) {
		lwis_dev->enabled--;
		mutex_unlock(&lwis_dev->client_lock);
		return 0;
	} else if (lwis_dev->enabled <= 0) {
		dev_err(lwis_dev->dev, "Disabling a device that is already disabled\n");
		mutex_unlock(&lwis_dev->client_lock);
		return -EINVAL;
	}
	lwis_dev->enabled--;
	ret = lwis_dev_power_down_locked(lwis_dev, /*error_handling=*/false);
	mutex_unlock(&lwis_dev->client_lock);

	return ret;
}

static int lwis_pwrseq_device_match(struct pwrseq_device *pwrseq, struct device *consumer_dev)
{
	struct lwis_pwrseq_device *pwrseq_dev = pwrseq_device_get_drvdata(pwrseq);
	struct device_node *pwr_node = pwrseq_dev->base_dev.k_dev->of_node;
	struct device_node *consumer_node = consumer_dev->of_node;
	struct device_node *matched_node;
	struct of_phandle_iterator it;
	int ret;

	/* WORKAROUND: If we are looking for a specific target, check it here */
	if (lwis_pwrseq_match_target) {
		if (strcmp(pwrseq_dev->target_name, lwis_pwrseq_match_target) != 0)
			return false; /* Wrong target, tell framework to keep searching */
	}

	/* Check if the consumer is the provider itself */
	if (pwr_node == consumer_node)
		return true;

	of_for_each_phandle(&it, ret, consumer_node, "sequencer-sources", /*cells_name=*/NULL,
			    /*cells_count=*/0) {
		if (it.node == pwr_node) {
			of_node_put(it.node);
			return true;
		}
	}
	if (ret && ret != -ENOENT)
		dev_err_probe(pwrseq_dev->base_dev.dev, ret, "Error parsing sequencer-sources\n");

	matched_node = of_parse_phandle(consumer_node, "power-sequencer", 0);
	if (matched_node == pwr_node) {
		of_node_put(matched_node);
		return true;
	}
	of_node_put(matched_node);

	return false;
}

static int pwrseq_dev_register(struct lwis_pwrseq_device *pwrseq_dev)
{
	struct device *dev = pwrseq_dev->base_dev.k_dev;
	struct pwrseq_unit_data unit;
	struct pwrseq_target_data target;
	const struct pwrseq_target_data *targets[2];
	struct pwrseq_config config = {};

	unit.name = pwrseq_dev->target_name;
	unit.deps = NULL;
	unit.enable = lwis_pwrseq_device_enable;
	unit.disable = lwis_pwrseq_device_disable;

	target.name = pwrseq_dev->target_name;
	target.unit = &unit;
	target.post_enable = NULL;
	targets[0] = &target;
	targets[1] = NULL;

	config.parent = dev;
	config.owner = THIS_MODULE;
	config.drvdata = pwrseq_dev;
	config.match = lwis_pwrseq_device_match;
	config.targets = targets;

	pwrseq_dev->pwrseq = devm_pwrseq_device_register(dev, &config);
	if (IS_ERR(pwrseq_dev->pwrseq))
		return dev_err_probe(dev, PTR_ERR(pwrseq_dev->pwrseq),
				     "Failed to register the power sequencer\n");

	return 0;
}

static int lwis_pwrseq_device_setup(struct lwis_pwrseq_device *pwrseq_dev)
{
	int ret;

#ifndef CONFIG_OF
	/* Non-device-tree init: Save for future implementation */
	return -EINVAL;
#endif

	ret = lwis_pwrseq_device_parse_dt(pwrseq_dev);
	if (ret)
		return ret;

	ret = pwrseq_dev_register(pwrseq_dev);
	if (ret)
		return ret;

	return 0;
}

static int lwis_pwrseq_device_probe(struct platform_device *plat_dev)
{
	int ret = 0;
	struct lwis_pwrseq_device *pwrseq_dev;
	struct device *dev = &plat_dev->dev;

	/* Allocate pwrseq device specific data */
	pwrseq_dev = devm_kzalloc(dev, sizeof(struct lwis_pwrseq_device), GFP_KERNEL);
	if (!pwrseq_dev)
		return -ENOMEM;

	pwrseq_dev->base_dev.type = DEVICE_TYPE_PWRSEQ;
	pwrseq_dev->base_dev.vops = pwrseq_vops;
	pwrseq_dev->base_dev.plat_dev = plat_dev;
	pwrseq_dev->base_dev.k_dev = &plat_dev->dev;

	/* Call the base device probe function */
	ret = lwis_base_probe(&pwrseq_dev->base_dev);
	if (ret) {
		dev_err_probe(dev, ret, "Error in lwis base probe\n");
		return ret;
	}
	platform_set_drvdata(plat_dev, &pwrseq_dev->base_dev);

	/* Call pwrseq device specific setup function */
	ret = lwis_pwrseq_device_setup(pwrseq_dev);
	if (ret) {
		dev_err(pwrseq_dev->base_dev.dev, "Error in pwrseq device initialization\n");
		lwis_base_unprobe(&pwrseq_dev->base_dev);
		return ret;
	}

	dev_info(pwrseq_dev->base_dev.dev, "Power Sequence Device Probe: Success\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id lwis_id_match[] = {
	{ .compatible = LWIS_PWRSEQ_DEVICE_COMPAT },
	{},
};
// MODULE_DEVICE_TABLE(of, lwis_id_match);

static struct platform_driver lwis_pwrseq_driver = {
	.probe = lwis_pwrseq_device_probe,
	.driver = {
			.name = LWIS_DRIVER_NAME,
			.owner = THIS_MODULE,
			.of_match_table = lwis_id_match,
		},
};
#else /* CONFIG_OF not defined */
static struct platform_device_id lwis_driver_id[] = {
	{
		.name = LWIS_DRIVER_NAME,
		.driver_data = 0,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, lwis_driver_id);

static struct platform_driver lwis_pwrseq_driver = { .probe = lwis_pwrseq_device_probe,
						     .id_table = lwis_driver_id,
						     .driver = {
							     .name = LWIS_DRIVER_NAME,
							     .owner = THIS_MODULE,
						     } };
#endif /* CONFIG_OF */

/*
 *  lwis_pwrseq_device_init: Init function that will be called by the kernel
 *  initialization routines.
 */
int __init lwis_pwrseq_device_init(void)
{
	int ret = 0;

	pr_info("PWRSEQ device initialization\n");

	ret = platform_driver_register(&lwis_pwrseq_driver);
	if (ret)
		pr_err("platform_driver_register failed: %d\n", ret);

	return ret;
}

int lwis_pwrseq_device_deinit(void)
{
	platform_driver_unregister(&lwis_pwrseq_driver);
	return 0;
}
