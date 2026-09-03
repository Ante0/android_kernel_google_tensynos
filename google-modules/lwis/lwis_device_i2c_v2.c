// SPDX-License-Identifier: GPL-2.0
/*
 * Google LWIS I2C Device Driver v2
 *
 * Copyright (c) 2024 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME "-i2c-dev-v2: " fmt

#include "lwis_device_i2c_v2.h"

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>

#include "lwis_device_i2c.h"

#ifdef CONFIG_OF
#include "lwis_dt.h"
#endif

#define LWIS_DRIVER_NAME "lwis-i2c-v2"

static int lwis_i2c_device_probe(struct i2c_client *i2c_client)
{
	int ret = 0;
	struct lwis_i2c_device *i2c_dev;
	struct device *dev = &i2c_client->dev;

	/* Allocate I2C device specific data construct */
	i2c_dev = devm_kzalloc(dev, sizeof(struct lwis_i2c_device), GFP_KERNEL);

	i2c_dev->base_dev.type = DEVICE_TYPE_I2C;
	i2c_dev->base_dev.vops = i2c_vops;
	i2c_dev->base_dev.plat_dev = NULL;
	i2c_dev->base_dev.k_dev = &i2c_client->dev;

	/* Call the base device probe function */
	ret = lwis_base_probe(&i2c_dev->base_dev);
	if (ret) {
		dev_err(dev, "Error in lwis base probe\n");
		return ret;
	}

	/* Setup client data */
	i2c_set_clientdata(i2c_client, &i2c_dev->base_dev);

	/* Call I2C device specific setup function */
	ret = lwis_i2c_device_setup(i2c_dev);
	if (ret) {
		dev_err(i2c_dev->base_dev.dev, "Error in i2c device initialization\n");
		lwis_base_unprobe(&i2c_dev->base_dev);
		return ret;
	}

	/* Create I2C Bus Manager */
	ret = lwis_bus_manager_create(&i2c_dev->base_dev);
	if (ret) {
		dev_err(i2c_dev->base_dev.dev, "Error in i2c bus manager creation\n");
		lwis_base_unprobe(&i2c_dev->base_dev);
		return ret;
	}

	dev_info(i2c_dev->base_dev.dev, "I2C Device V2 Probe: Success\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id lwis_id_match[] = {
	{ .compatible = LWIS_I2C_DEVICE_V2_COMPAT },
	{},
};
// MODULE_DEVICE_TABLE(of, lwis_id_match);

static struct i2c_driver lwis_driver = {
	.probe = lwis_i2c_device_probe,
	.driver = {
			.name = LWIS_DRIVER_NAME,
			.owner = THIS_MODULE,
			.of_match_table = lwis_id_match,
		},
};
#else /* CONFIG_OF not defined */
static struct i2c_device_id lwis_driver_id[] = {
	{
		.name = LWIS_DRIVER_NAME,
		.driver_data = 0,
	},
	{},
};
MODULE_DEVICE_TABLE(i2c, lwis_driver_id);

static struct i2c_driver lwis_driver = { .probe = lwis_i2c_device_probe,
					 .id_table = lwis_driver_id,
					 .driver = {
						 .name = LWIS_DRIVER_NAME,
						 .owner = THIS_MODULE,
					 } };
#endif /* CONFIG_OF */

int __init lwis_i2c_device_v2_init(void)
{
	int ret = 0;

	pr_info("I2C device v2 initialization\n");

	ret = i2c_add_driver(&lwis_driver);

	return ret;
}

int lwis_i2c_device_v2_deinit(void)
{
	i2c_del_driver(&lwis_driver);
	return 0;
}
