// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM Common Network Layer
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 *
 * This file provides the framework for the LVM common network layer,
 * which includes platform device and driver management, as well as
 * the initialization and deinitialization of the network framework.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <core/print.h>
#include <net/pktgen.h>
#include <net/platform.h>
#include <net/sysfs.h>
#include <drivers/wlan/module.h>

struct lvm_net *net;

const char * const lvm_state_str[] = {
	[LVM_STATE_IDLE]		= "Idle",
	[LVM_STATE_FRAMEWORK_READY]	= "Framework Ready",
	[LVM_STATE_DRIVER_READY]	= "Driver Ready",
	[LVM_STATE_INTERFACE_RUNNING]	= "Interface Running",
	[LVM_STATE_DATA_TRANSFERRING]	= "Data Transferring",
};

/**
 * lvm_platform_device_alloc - Allocate a LVM platform device structure
 * @driver: Pointer to the LVM platform driver structure
 *
 * This function allocates and initializes a LVM platform device structure.
 * It also creates a mock device for further DMA-mapping usage.
 *
 * Return: Pointer to the allocated LVM platform device structure,
 *         NULL on error.
 */
static struct lvm_platform_device *
lvm_platform_device_alloc(struct lvm_platform_driver *driver)
{
	struct lvm_platform_device *pdev;
	static u64 dma_mask = DMA_BIT_MASK(32);

	/* Allocate memory for the LVM platform device structure */
	pdev = kzalloc(sizeof(struct lvm_platform_device), GFP_KERNEL);
	if (!pdev)
		return NULL;

	/* Initialize the mock device for further DMA-mapping usage*/
	pdev->dev = device_create(net->class, NULL, 0, NULL, driver->name);
	if (IS_ERR(pdev->dev)) {
		LVM_ERR("LVM driver device creation failed\n");
		return NULL;
	}

	pdev->dev->dma_mask = &dma_mask;
	pdev->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	return pdev;
}

/**
 * lvm_platform_device_free - Free a LVM platform device structure
 * @pdev: Pointer to the LVM platform device structure
 *
 * This function frees the memory allocated for a LVM platform device structure
 * and its associated mock device.
 */
static void lvm_platform_device_free(struct lvm_platform_device *pdev)
{
	if (!pdev)
		return;

	/* Free the mock device and the LVM platform device structure */
	device_destroy(net->class, 0);
	kfree(pdev);
}

/**
 * lvm_net_alloc - Allocate the global LVM network structure
 *
 * This function allocates memory for the global LVM network structure.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int lvm_net_alloc(void)
{
	/* Allocate memory for the global LVM network structure */
	net = kzalloc(sizeof(struct lvm_net), GFP_KERNEL);
	if (!net)
		return -ENOMEM;

	net->class = class_create(LVM_CLASS_NAME);
	if (IS_ERR(net->class)) {
		LVM_ERR("LVM class creation failed\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * lvm_net_free - Free the global LVM network structure
 * @net: Pointer to the global LVM network structure
 *
 * This function frees the memory allocated for the global LVM network structure.
 */
static void lvm_net_free(struct lvm_net *net)
{
	if (!net)
		return;

	class_destroy(net->class);

	kfree(net);
}

/**
 * lvm_platform_probe - Probe function for the LVM platform driver
 * @driver: Pointer to the LVM platform driver structure
 *
 * This function is called when the LVM platform driver is being probed.
 * It calls the driver's probe function to initialize the underlying device.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_platform_probe(struct lvm_platform_driver *driver)
{
	int ret = 0;

	if (!driver || !driver->ops || !driver->ops->probe)
		return -EINVAL;

	/* Call the registered driver's probe function */
	ret = driver->ops->probe(driver->pdev);
	if (ret)
		return ret;

	return 0;
}

/**
 * lvm_platform_remove - Remove function for the LVM platform driver
 * @driver: Pointer to the LVM platform driver structure
 *
 * This function is called when the LVM platform driver is being removed.
 * It calls the driver's remove function to deinitialize the underlying device.
 */
int lvm_platform_remove(struct lvm_platform_driver *driver)
{
	int ret = 0;

	if (!driver || !driver->ops || !driver->ops->remove)
		return -EINVAL;

	/* Call the registered driver's remove function */
	ret = driver->ops->remove(driver->pdev);
	if (ret)
		return ret;

	return 0;
}

/**
 * lvm_manager_register - Register a buffer manager with a platform driver
 * @driver: Pointer to the LVM platform driver structure
 * @manager: Pointer to the buffer manager structure
 *
 * This function registers a buffer manager with a platform driver.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_manager_register(struct lvm_platform_driver *driver,
			 struct lvm_manager *manager)
{
	if (!driver || !manager)
		return -EINVAL;

	/* Register the buffer manager based on its type */
	switch (manager->type) {
	case MANAGER_TYPE_RX:
		driver->rx_manager = manager;
		break;
	case MANAGER_TYPE_TX:
		driver->tx_manager = manager;
		break;
	default:
		break;
	}

	return 0;
}

/**
 * lvm_manager_unregister - Unregister a buffer manager from a platform driver
 * @driver: Pointer to the LVM platform driver structure
 * @manager: Pointer to the buffer manager structure
 *
 * This function unregisters a buffer manager from a platform driver.
 */
void lvm_manager_unregister(struct lvm_platform_driver *driver,
			    struct lvm_manager *manager)
{
	if (!driver || !manager)
		return;

	/* Unregister the buffer manager based on its type */
	switch (manager->type) {
	case MANAGER_TYPE_RX:
		driver->rx_manager = NULL;
		break;
	case MANAGER_TYPE_TX:
		driver->tx_manager = NULL;
		break;
	default:
		break;
	}
}

/**
 * lvm_netdev_register - Register a network device with a platform driver
 * @driver: Pointer to the LVM platform driver structure
 * @netdev: Pointer to the network device structure
 *
 * This function registers a network device with a platform driver.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_netdev_register(struct lvm_platform_driver *driver,
			struct lvm_netdev *netdev)
{
	if (!driver || !netdev)
		return -EINVAL;

	driver->netdev = netdev;

	return 0;
}

/**
 * lvm_netdev_unregister - Unregister a network device from a platform driver
 * @driver: Pointer to the LVM platform driver structure
 *
 * This function unregisters a network device from a platform driver.
 */
void lvm_netdev_unregister(struct lvm_platform_driver *driver)
{
	if (!driver)
		return;

	driver->netdev = NULL;
}

/**
 * lvm_platform_driver_register - Register a LVM platform driver
 * @driver: Pointer to the LVM platform driver structure
 *
 * This function registers a LVM platform driver and allocates a platform
 * device for it.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_platform_driver_register(struct lvm_platform_driver *driver)
{
	struct lvm_platform_device *pdev;

	if (!driver) {
		LVM_ERR("LVM platform driver registration failed\n");
		return -EINVAL;
	}

	pdev = lvm_platform_device_alloc(driver);
	if (!pdev)
		return -EINVAL;

	/* Register the platform driver with the global LVM network structure */
	net->driver = driver;
	net->driver->pdev = pdev;
	net->driver->kobj = &pdev->dev->kobj;

	return 0;
}

/**
 * lvm_platform_driver_unregister - Unregister a LVM platform driver
 * @driver: Pointer to the LVM platform driver structure
 *
 * This function unregisters a LVM platform driver and frees its associated
 * platform device.
 */
void lvm_platform_driver_unregister(struct lvm_platform_driver *driver)
{
	if (!driver)
		return;

	/*
	 * Free the platform device and unregister the platform driver from
	 * the global LVM network structure.
	 */
	lvm_platform_device_free(driver->pdev);
	net->driver = NULL;
}

/**
 * lvm_state_transit - Transit the LVM state machine to a new state
 * @new_state: The new state to transit to
 *
 * This function transits the LVM state machine to a new state. It first
 * checks if the current state is the same as the new state. Also, it checks
 * if the transition from the current state to the new state is valid.
 *
 * The valid state transitions are:
 *   - LVM_STATE_IDLE <-> LVM_STATE_FRAMEWORK_READY
 *   - LVM_STATE_FRAMEWORK_READY <-> LVM_STATE_DRIVER_READY
 *   - LVM_STATE_DRIVER_READY <-> LVM_STATE_INTERFACE_RUNNING
 *   - LVM_STATE_INTERFACE_RUNNING <-> LVM_STATE_DATA_TRANSFERRING
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_state_transit(enum lvm_state new_state)
{
	if (net->state == new_state)
		return 0;

	switch (new_state) {
	case LVM_STATE_IDLE:
		if (net->state != LVM_STATE_FRAMEWORK_READY)
			return -EINVAL;
		break;
	case LVM_STATE_FRAMEWORK_READY:
		if (net->state != LVM_STATE_IDLE &&
		    net->state != LVM_STATE_DRIVER_READY)
			return -EINVAL;
		break;
	case LVM_STATE_DRIVER_READY:
		if (net->state != LVM_STATE_FRAMEWORK_READY &&
		    net->state != LVM_STATE_INTERFACE_RUNNING)
			return -EINVAL;
		break;
	case LVM_STATE_INTERFACE_RUNNING:
		if (net->state != LVM_STATE_DRIVER_READY &&
		    net->state != LVM_STATE_DATA_TRANSFERRING)
			return -EINVAL;
		break;
	case LVM_STATE_DATA_TRANSFERRING:
		if (net->state != LVM_STATE_INTERFACE_RUNNING)
			return -EINVAL;
		break;
	default:
		LVM_ERR("LVM state transition failed: %d\n", new_state);
		return -EINVAL;
	}

	LVM_INFO("LVM state transited from \"%s\" to \"%s\"\n",
		 lvm_state_str[net->state], lvm_state_str[new_state]);

	net->state = new_state;

	return 0;
}

/**
 * lvm_net_framework_init - Initialize the LVM network framework
 *
 * This function initializes the LVM network framework, including allocating
 * the global network structure, initializing the driver module.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_net_framework_init(void)
{
	int ret = 0;

	/* Allocate the global LVM network structure */
	ret = lvm_net_alloc();
	if (ret) {
		LVM_ERR("LVM net structure allocation failed: %d\n", ret);
		return ret;
	}

	/* Initialize the WLAN driver module */
	ret = lvm_wlan_module_init();
	if (ret) {
		LVM_ERR("LVM wlan module init failed: %d\n", ret);
		goto err_net;
	}

	/* Initialize sysfs */
	ret = lvm_net_sysfs_init(net);
	if (ret) {
		LVM_ERR("LVM sysfs init failed: %d\n", ret);
		goto err_wlan;
	}

	/* Initialize packet generator */
	ret = lvm_pktgen_init(net);
	if (ret) {
		LVM_ERR("LVM pktgen init failed: %d\n", ret);
		goto err_sysfs;
	}

	/* Transit LVM state machine */
	lvm_state_transit(LVM_STATE_FRAMEWORK_READY);

	return 0;

err_sysfs:
	lvm_net_sysfs_deinit(net);

err_wlan:
	lvm_wlan_module_exit();

err_net:
	lvm_net_free(net);

	return ret;
}

/**
 * lvm_net_framework_deinit - Deinitialize the LVM network framework
 *
 * This function deinitializes the LVM network framework, including deinitializing
 * the WLAN driver module, and freeing the global network structure.
 */
void lvm_net_framework_deinit(void)
{
	/* Transit LVM state machine */
	lvm_state_transit(LVM_STATE_IDLE);

	/* Deinitialize packet generator */
	lvm_pktgen_deinit(net);

	/* Deinitialize sysfs */
	lvm_net_sysfs_deinit(net);

	/* Deinitialize the WLAN driver module */
	lvm_wlan_module_exit();

	/* Free the global LVM network structure */
	lvm_net_free(net);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Henry Yen <henryyen@google.com>");
MODULE_DESCRIPTION("LVM Common Network Layer");
