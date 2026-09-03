// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM Core Module
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 *
 * This file handles the initialization and de-initialization of the LVM
 * core components.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <core/print.h>
#include <net/platform.h>
#include <unittest/platform.h>

/**
 * lvm_core_start - Initialize the LVM core components
 *
 * This function initializes the networking and unittest frameworks
 * of the LVM.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int lvm_core_start(void)
{
	int ret = 0;

	/* Initialize the LVM common network layer */
	ret = lvm_net_framework_init();
	if (ret) {
		LVM_ERR("LVM net framework init failed: %d\n", ret);
		return ret;
	}

	/* Initialize the LVM unittest framework */
	ret = lvm_unittest_framework_init();
	if (ret) {
		LVM_ERR("LVM unittest framework init failed: %d\n", ret);
		goto err_net;
	}

	return 0;

err_net:
	lvm_net_framework_deinit();

	return ret;
}

/**
 * lvm_core_stop - De-initialize the LVM core components
 *
 * This function de-initializes the networking and unittest frameworks
 * of the LVM.
 */
static void lvm_core_stop(void)
{
	/* De-initialize the LVM unittest framework */
	lvm_unittest_framework_deinit();

	/* De-initialize the LVM common network layer */
	lvm_net_framework_deinit();
}

/**
 * lvm_init - Initialize the LVM module
 *
 * This function is called when the LVM module is loaded into the kernel.
 * It initializes the LVM core components.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int __init lvm_init(void)
{
	int ret = 0;

	LVM_INFO("LVM module init started\n");

	/* Start the LVM core */
	ret = lvm_core_start();
	if (ret) {
		LVM_ERR("LVM core start failed: %d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * lvm_exit - Exit the LVM module
 *
 * This function is called when the LVM module is unloaded from the kernel.
 * It de-initializes the LVM core components.
 */
static void __exit lvm_exit(void)
{
	/* Stop the LVM core */
	lvm_core_stop();

	LVM_INFO("LVM module exited\n");
}

module_init(lvm_init);
module_exit(lvm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Henry Yen <henryyen@google.com>");
MODULE_DESCRIPTION("NOA Linux Verification Model");
