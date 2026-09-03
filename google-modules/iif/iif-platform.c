// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for inter-IP fence (IIF).
 *
 * Copyright (C) 2024 Google LLC
 */

#define pr_fmt(fmt) "iif: " fmt

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <iif/iif-fs.h>
#include <iif/iif-manager.h>
#include <iif/iif-platform.h>

#ifdef IIF_UNIT_TEST
#include "unittests/helper/iif-platform-probe-controller.h"
#define TEST_GET_PROBE_DEFER() iif_platform_probe_controller_get_probe_err()
#else
#define TEST_GET_PROBE_DEFER() (0)
#endif

#define IIF_DEV_COUNT 1

/* Objects to register a char device of IIF driver. */
static struct class *iif_class;
static dev_t iif_base_devno;

const char *iif_platform_get_driver_commit(void)
{
#if IS_ENABLED(CONFIG_MODULE_SCMVERSION)
	return THIS_MODULE->scmversion ?: "scmversion missing";
#elif defined(GIT_REPO_TAG)
	return GIT_REPO_TAG;
#else
	return "Unknown";
#endif
}

/* Initializes the char device of IIF driver. */
static int iif_cdev_add(struct iif_manager *mgr)
{
	int ret;
	struct device *dev;

	/* Initializes the char device and registers it to the kernel. */
	mgr->char_dev_no = MKDEV(MAJOR(iif_base_devno), 0);
	cdev_init(&mgr->char_dev, &iif_fops);
	ret = cdev_add(&mgr->char_dev, mgr->char_dev_no, 1);
	if (ret) {
		dev_err(mgr->dev, "Failed in adding cdev for dev %d:%d (ret=%d)",
			MAJOR(mgr->char_dev_no), MINOR(mgr->char_dev_no), ret);
		return ret;
	}

	/*
	 * Creates /dev/iif node. The users are expected to open it and utilize ioctls.
	 * We only need char_dev_no for device_destroy, no need to record the returned dev.
	 */
	dev = device_create(iif_class, mgr->dev, mgr->char_dev_no, mgr, "%s", IIF_DRIVER_NAME);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		dev_err(mgr->dev, "Failed in creating a char device: %d", ret);
		cdev_del(&mgr->char_dev);
	}

	return ret;
}

/* Destroys the char device initialized by the `iif_cdev_add` function above. */
static void iif_cdev_del(struct iif_manager *mgr)
{
	device_destroy(iif_class, mgr->char_dev_no);
	cdev_del(&mgr->char_dev);
}

static int iif_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iif_manager *mgr;
	int ret;

	dev_notice(dev, "Probing IIF driver (commit: %s)", iif_platform_get_driver_commit());

	ret = TEST_GET_PROBE_DEFER();
	if (ret)
		return ret;

	mgr = iif_manager_init(dev->of_node);
	if (IS_ERR(mgr)) {
		dev_err(dev, "Failed to initialize IIF manager, ret=%ld", PTR_ERR(mgr));
		return PTR_ERR(mgr);
	}

	mgr->dev = dev;

	ret = iif_cdev_add(mgr);
	if (ret) {
		dev_err(dev, "Failed to add IIF cdev, ret=%d", ret);
		goto err_put_manager;
	}

	/* Set the manager pointer to the device only if the probe succeeds. */
	platform_set_drvdata(pdev, mgr);

	dev_info(dev, "IIF driver is probed");

	return 0;

err_put_manager:
	iif_manager_put(mgr);
	return ret;
}

static void iif_platform_remove(struct platform_device *pdev)
{
	struct iif_manager *mgr = platform_get_drvdata(pdev);

	iif_cdev_del(mgr);
	iif_manager_put(mgr);
}

static const struct of_device_id iif_of_match[] = {
	{
		.compatible = "google,iif",
	},
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, iif_of_match);

static struct platform_driver iif_platform_driver = {
	.probe = iif_platform_probe,
	.remove = iif_platform_remove,
	.driver = {
			.name = IIF_DRIVER_NAME,
			.of_match_table = of_match_ptr(iif_of_match),
		},
};

/* Initializes the variables to create a char device and enable file operations of IIF device. */
static __init int iif_fs_init(void)
{
	int ret;

	/*
	 * Creates the `/sys/class/iif` class.
	 * It will be used when we create a char device during the probe.
	 */
	iif_class = class_create(IIF_DRIVER_NAME);
	if (IS_ERR(iif_class)) {
		pr_err("Failed on creating IIF class: %ld\n", PTR_ERR(iif_class));
		return PTR_ERR(iif_class);
	}

	/*
	 * Allocates a char device number.
	 * It will be used when we create a char device during the probe.
	 */
	ret = alloc_chrdev_region(&iif_base_devno, 0, IIF_DEV_COUNT, IIF_DRIVER_NAME);
	if (ret) {
		pr_err("Failed on allocating IIF char device region: %d\n", ret);
		class_destroy(iif_class);
		return ret;
	}

	return 0;
}

/* Destroys the variables initialized by the `iif_fs_init` function above. */
static __exit void iif_fs_exit(void)
{
	unregister_chrdev_region(iif_base_devno, IIF_DEV_COUNT);
	class_destroy(iif_class);
}

static int __init iif_platform_init(void)
{
	int ret;

	ret = iif_fs_init();
	if (ret)
		return ret;

	return platform_driver_register(&iif_platform_driver);
}

static void __exit iif_platform_exit(void)
{
	platform_driver_unregister(&iif_platform_driver);
	iif_fs_exit();
}

MODULE_DESCRIPTION("Google IIF platform driver");
MODULE_LICENSE("GPL");
#ifdef GIT_REPO_TAG
MODULE_INFO(gitinfo, GIT_REPO_TAG);
#endif
module_init(iif_platform_init);
module_exit(iif_platform_exit);

bool iif_platform_is_iif_device(struct platform_device *pdev)
{
	return of_match_device(iif_of_match, &pdev->dev);
}
