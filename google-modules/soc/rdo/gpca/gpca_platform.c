// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for the GPCA(General Purpose Crypto Accelerator)
 *
 * Copyright (C) 2022-2023 Google LLC.
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "gpca_cmd.h"
#include "gpca_crypto.h"
#include "gpca_internal.h"
#include "gpca_keys_internal.h"
#include "gpca_op_queue.h"
/*#include "gpca_ioctl.h"*/

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	/*.unlocked_ioctl = gpca_ioctl,*/
};

static int construct_device(struct gpca_dev *gpca_dev)
{
	int ret = 0;
	struct device *dev = NULL;

	/* Register the character device. */
	ret = alloc_chrdev_region(&gpca_dev->gpca_dev_num, 0, 1, "gpca");
	if (ret) {
		dev_err(gpca_dev->dev,
			"Error while alloc gpca chrdev ret=%d.\n", ret);
		goto exit;
	}

	cdev_init(&gpca_dev->gpca_cdev, &fops);

	/* Adding character device to the system. */
	ret = cdev_add(&gpca_dev->gpca_cdev, gpca_dev->gpca_dev_num, 1);
	if (ret) {
		dev_err(gpca_dev->dev, "Error to add GPCA cdev ret=%d.\n", ret);
		goto unregister_device;
	}

	/* Creating device struct class. */
	gpca_dev->gpca_class = class_create("gpca");
	if (IS_ERR(gpca_dev->gpca_class)) {
		dev_err(gpca_dev->dev, "GPCA Device class creation failed.\n");
		ret = PTR_ERR(gpca_dev->gpca_class);
		goto unregister_device;
	}

	/* Creating device. */
	dev = device_create(gpca_dev->gpca_class, NULL, gpca_dev->gpca_dev_num,
			    NULL, "gpca");
	if (IS_ERR(dev)) {
		dev_err(gpca_dev->dev, "GPCA device file creation failed.\n");
		ret = PTR_ERR(dev);
		goto destroy_class;
	}
	return 0;

destroy_class:
	class_destroy(gpca_dev->gpca_class);
unregister_device:
	unregister_chrdev_region(gpca_dev->gpca_dev_num, 1);
exit:
	return ret;
}

static void destroy_device(struct gpca_dev *gpca_dev)
{
	device_destroy(gpca_dev->gpca_class, gpca_dev->gpca_dev_num);
	class_destroy(gpca_dev->gpca_class);
	cdev_del(&gpca_dev->gpca_cdev);
	unregister_chrdev_region(gpca_dev->gpca_dev_num, 1);
}

static int gpca_regmap_init(struct gpca_dev *gpca_dev)
{
	/* Regmap configuration parameters for GPCA */
	const struct regmap_config gpca_regmap_cfg = {
		.reg_bits = 32, /* No. of bits in a register address */
		.val_bits = 32, /* No. of bits in a register value */
		.reg_stride = 4, /* Stride denoting valid addr offsets */
		.name = "gpca", /* Name of the regmap */
	};

	gpca_dev->gpca_regmap = devm_regmap_init_mmio(
		gpca_dev->dev, gpca_dev->reg_base, &gpca_regmap_cfg);

	if (IS_ERR_OR_NULL(gpca_dev->gpca_regmap))
		return -ENXIO;
	return 0;
}

static int gpca_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpca_dev *gpca_dev;
	int ret = 0;
	u32 reversed_iv = 0;

	gpca_dev = devm_kzalloc(dev, sizeof(*gpca_dev), GFP_KERNEL);

	if (!gpca_dev)
		return -ENOMEM;
	gpca_dev->dev = dev;

	ret = construct_device(gpca_dev);
	if (ret != 0)
		return ret;

	platform_set_drvdata(pdev, gpca_dev);

	gpca_dev->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR_OR_NULL(gpca_dev->reg_base)) {
		dev_err(dev, "devm_ioremap_resource failed\n");
		ret = -ENODEV;
		goto destroy_device;
	}

	of_property_read_u32(gpca_dev->dev->of_node, "reversed-iv",
			     &reversed_iv);
	gpca_dev->reversed_iv = !!reversed_iv;

	gpca_dev->drv_data = of_device_get_match_data(dev);

	/* Initialize Binary semaphore to handle GPCA power management */
	sema_init(&gpca_dev->gpca_busy, 1);

	ret = gpca_regmap_init(gpca_dev);
	if (ret != 0) {
		dev_err(dev, "Register map initialization failed with ret = %d\n", ret);
		goto destroy_device;
	}

	ret = gpca_cmd_init(gpca_dev);
	if (ret != 0) {
		dev_err(dev, "GPCA command processing initialization failed with ret = %d\n", ret);
		goto destroy_device;
	}

	ret = gpca_op_queue_init(gpca_dev);
	if (ret != 0) {
		dev_err(dev, "GPCA operations queue initialization failed with ret = %d\n",
			ret);
		goto destroy_device;
	}

	ret = gpca_key_init(gpca_dev);
	if (ret != 0) {
		dev_err(dev, "GPCA key initialization failed with ret = %d\n", ret);
		goto destroy_device;
	}

	ret = gpca_crypto_init(gpca_dev);
	if (ret != 0) {
		dev_err(dev, "GPCA crypto initialization failed with ret = %d\n", ret);
		goto destroy_device;
	}

	dev_info(dev, "GPCA probe complete\n");
	goto exit;

destroy_device:
	destroy_device(gpca_dev);
exit:
	return ret;
}

static void gpca_platform_remove(struct platform_device *pdev)
{
	struct gpca_dev *gpca_dev;

	gpca_dev = platform_get_drvdata(pdev);
	if (!gpca_dev)
		return;

	cancel_work_sync(&gpca_dev->gpca_op_process);
	cancel_work_sync(&gpca_dev->gpca_cmd_process_req);
	cancel_work_sync(&gpca_dev->gpca_cmd_process_rsp);

	gpca_crypto_deinit(gpca_dev);

	gpca_key_deinit(gpca_dev);

	gpca_op_queue_deinit(gpca_dev);

	destroy_device(gpca_dev);
}

static int gpca_system_suspend(struct device *dev)
{
	struct gpca_dev *gpca_dev;
	int gpca_busy = 0;

	gpca_dev = dev_get_drvdata(dev);
	if (!gpca_dev)
		return -ENODEV;

	/* Mark GPCA busy to prevent next GPCA requests being sent to Hardware */
	gpca_busy = down_trylock(&gpca_dev->gpca_busy);
	if (gpca_busy)
		return -EBUSY;

	return 0;
}

static int gpca_system_resume(struct device *dev)
{
	struct gpca_dev *gpca_dev;

	gpca_dev = dev_get_drvdata(dev);
	if (!gpca_dev)
		return -ENODEV;

	up(&gpca_dev->gpca_busy);

	return 0;
}

static const struct dev_pm_ops gpca_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(gpca_system_suspend, gpca_system_resume)
};

static const struct gpca_driverdata rdo_drvdata = {
	.hw_bug_hmac_digest_size_keys = true,
};

static const struct gpca_driverdata lga_drvdata =  {
	.hw_bug_hmac_digest_size_keys = false,
};

static const struct of_device_id gpca_of_match[] = {
	{
		.compatible = "google,gpca",
		.data = &rdo_drvdata,
	},
	{
		.compatible = "google,gpca-lga",
		.data = &lga_drvdata,
	},
	{},
};
MODULE_DEVICE_TABLE(of, gpca_of_match);

static struct platform_driver gpca_platform_driver = {
	.probe  = gpca_platform_probe,
	.remove = gpca_platform_remove,
	.driver = {
			.name           = "gpca",
			.owner          = THIS_MODULE,
			.of_match_table = of_match_ptr(gpca_of_match),
			.pm = &gpca_pm_ops,
		},
};

module_platform_driver(gpca_platform_driver);

#if IS_ENABLED(CONFIG_GOOGLE_GPCA_KEY_TEST)
struct platform_device *get_gpca_platform_device(void)
{
	return to_platform_device(platform_find_device_by_driver(
		NULL, &gpca_platform_driver.driver));
}
EXPORT_SYMBOL(get_gpca_platform_device);
#endif

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google GPCA platform driver");
MODULE_LICENSE("GPL");
