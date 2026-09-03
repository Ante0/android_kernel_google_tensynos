// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM WLAN Driver Core Setup
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 *
 * This file prepares the core data structures and initialization routines
 * for the LVM WLAN driver.
 */

#include <linux/io.h>
#include <dal/wlan/api.h>
#include "mcu.h"
#include "format.h"
#include "interface.h"

extern struct lvm_netdev_ops		wlan_netdev_ops;
extern struct lvm_platform_driver	lvm_wlan_driver;
extern struct platform_bus_ops		vendor_bus_ops;
extern struct platform_bus_ops		google_bus_ops;
struct platform_bus_ops			*plat_ops;
lvm_dal_rxbm_sync_t			rxbm_sync_cb;

/**
 * WLAN driver parameters
 *
 * This structure defines various parameters for the WLAN driver,
 * including its type, interrupt number, interface index, register map,
 * and buffer management parameters.
 */
static const struct wlan_param wlan_param = {
	.type				= WLAN_TYPE_FAKE_BCM4390,
	.ifidx				= 7,

	.irq = {
		.id			= 1002,
		.poll_interval		= 100,
	},

	/* WLAN register map */
	.reg = {
		.fake_dev_base		= 0x72000000,
		.ncp_dev_base		= 0xA2000000,
		.noa_dram_base		= 0x80000000,
		.ncp_dram_base		= 0x60000000,
		.fd_dram_base		= 0x60000000,
	},

	/* WLAN RX buffer management parameters */
	.rxbm = {
		.buf_num		= WLAN_RX_BUF_NUM,
		.buf_len		= WLAN_RX_BUF_LEN,
		.head_len		= WLAN_RX_HEAD_LEN,
	},

	/* WLAN TX buffer management parameters */
	.txbm = {
		.buf_num		= WLAN_TX_BUF_NUM,
		.buf_len		= WLAN_TX_BUF_LEN,
		.head_len		= WLAN_TX_HEAD_LEN,
		.txbm_buf_num		= WLAN_TXBM_BUF_NUM,
	},

	/* WLAN ring parameters */
	.ring = {
		[WLAN_RING_ID_RX_DATA0]	= {
			.name		= "rx data",
			.hw_id		= 2,
			.desc_num	= 1024,
			.desc_len	= sizeof(struct wlan_rxbuf_cmpl),
			.type		= WLAN_RING_TYPE_RX_DATA,
			.dir		= WLAN_DIR_FROM_DEV,
		},
		[WLAN_RING_ID_RX_POST0]	= {
			.name		= "rx post",
			.hw_id		= 0,
			.desc_num	= 2048,
			.desc_len	= sizeof(struct host_rxbuf_post),
			.type		= WLAN_RING_TYPE_RX_POST,
			.dir		= WLAN_DIR_TO_DEV,
		},
		[WLAN_RING_ID_TX_CPL0]	= {
			.name		= "tx cpl",
			.hw_id		= 1,
			.desc_num	= 2048,
			.desc_len	= sizeof(struct host_txbuf_cmpl),
			.type		= WLAN_RING_TYPE_TX_CPL,
			.dir		= WLAN_DIR_FROM_DEV,
		},
		[WLAN_RING_ID_TX_DATA0]	= {
			.name		= "tx data0",
			.hw_id		= 1,
			.desc_num	= 512,
			.desc_len	= sizeof(struct host_txbuf_post),
			.type		= WLAN_RING_TYPE_TX_DATA,
			.dir		= WLAN_DIR_TO_DEV,
		},
		[WLAN_RING_ID_TX_DATA1]	= {
			.name		= "tx data1",
			.hw_id		= 2,
			.desc_num	= 2048,
			.desc_len	= sizeof(struct host_txbuf_post),
			.type		= WLAN_RING_TYPE_TX_DATA,
			.dir		= WLAN_DIR_TO_DEV,
		},
	},
};

/**
 * lvm_wlan_dal_init - Initialize the WLAN Device Abstraction Layer (DAL)
 * @data: Pointer to the WLAN data structure
 *
 * This function initializes the WLAN DAL based on the selected
 * operation mode (NOA or Bypass). It sets the platform bus operations
 * structure accordingly and calls the platform bus initialization
 * function.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_wlan_dal_init(struct wlan_data *data)
{
	int ret = 0;

	if (!data)
		return -EINVAL;

	plat_ops = (is_noa_mode(data)) ? &google_bus_ops : &vendor_bus_ops;

	if (plat_ops && plat_ops->init) {
		ret = plat_ops->init(data->dev, data);
		if (ret)
			return -EINVAL;
	}

	return 0;
}

/**
 * lvm_wlan_dal_deinit - Deinitialize the WLAN DAL
 * @data: Pointer to the WLAN data structure
 *
 * This function deinitializes the WLAN DAL by calling the platform
 * bus exit function and clearing the platform bus operations structure.
 */
void lvm_wlan_dal_deinit(struct wlan_data *data)
{
	if (!data)
		return;

	platform_bus_exit(data);

	plat_ops = NULL;
}

/**
 * lvm_wlan_buffer_init - Initialize WLAN buffers
 * @pdev: Pointer to the LVM platform device structure
 *
 * This function initializes the WLAN buffers by allocating and registering
 * the RX and TX buffer managers. It retrieves the necessary parameters
 * from the WLAN device parameters structure.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_wlan_buffer_init(struct lvm_platform_device *pdev)
{
	struct lvm_platform_driver *driver;
	struct lvm_manager *manager;
	struct wlan_data *data;
	const struct wlan_param *param;

	if (!pdev)
		return -EINVAL;

	data = dev_get_drvdata(pdev->dev);
	if (!data)
		return -EINVAL;

	driver = data->driver;
	param = data->param;

	/* Allocate and register RX buffer manager */
	manager = lvm_manager_alloc(data->dev, MANAGER_TYPE_RX,
				    param->rxbm.buf_num, param->rxbm.buf_len,
				    param->rxbm.head_len);
	lvm_manager_register(driver, manager);

	/* Allocate and register TX buffer manager */
	manager = lvm_manager_alloc(data->dev, MANAGER_TYPE_TX,
				    param->txbm.buf_num, param->txbm.buf_len,
				    param->txbm.head_len);
	lvm_manager_register(driver, manager);

	return 0;
}

/**
 * lvm_wlan_buffer_deinit - Deinitialize WLAN buffers
 * @pdev: Pointer to the LVM platform device structure
 *
 * This function deinitializes the WLAN buffers by unregistering and freeing
 * the RX and TX buffer managers.
 */
void lvm_wlan_buffer_deinit(struct lvm_platform_device *pdev)
{
	struct lvm_platform_driver *driver;
	struct wlan_data *data;

	if (!pdev)
		return;

	data = dev_get_drvdata(pdev->dev);
	if (!data)
		return;

	driver = data->driver;

	/* Unregister and free RX buffer manager */
	lvm_manager_unregister(driver, driver->rx_manager);
	lvm_manager_free(data->dev, driver->rx_manager);

	/* Unregister and free TX buffer manager */
	lvm_manager_unregister(driver, driver->tx_manager);
	lvm_manager_free(data->dev, driver->tx_manager);
}

/**
 * lvm_wlan_mcu_init - Initialize WLAN driver main data structure
 * @pdev: Pointer to the LVM platform device structure
 *
 * This function initializes the WLAN basic infra by allocating memory for the
 * main data structure, registering the network device, and setting up
 * the driver data.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_wlan_mcu_init(struct lvm_platform_device *pdev)
{
	struct lvm_netdev *netdev;
	struct wlan_data *data;

	if (!pdev)
		return -EINVAL;

	/* Allocate memory for WLAN driver main data structure */
	data = kzalloc(sizeof(struct wlan_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* Allocate and initialize LVM network device */
	netdev = lvm_netdev_alloc("wlan0");
	netdev->netdev_ops = &wlan_netdev_ops;
	netdev->priv = data;
	netdev->ifidx = wlan_param.ifidx;

	/* Initialize WLAN driver main data structure */
	data->param = &wlan_param;
	data->netdev = netdev;
	data->driver = &lvm_wlan_driver;
	data->dev = pdev->dev;
	data->irq.id = wlan_param.irq.id;
	data->irq.poll_interval = wlan_param.irq.poll_interval;
	data->wq = create_workqueue("wlan workqueue");
	if (!data->wq)
		return -ENOMEM;

	/* Set driver data and register network device */
	dev_set_drvdata(pdev->dev, data);
	lvm_netdev_register(data->driver, netdev);

	return 0;
}

/**
 * lvm_wlan_mcu_deinit - Deinitialize WLAN driver main data structure
 * @pdev: Pointer to the LVM platform device structure
 *
 * This function deinitializes the WLAN basic infra by unregistering the network
 * device and freeing the allocated memory for the WLAN driver main data structure.
 */
void lvm_wlan_mcu_deinit(struct lvm_platform_device *pdev)
{
	struct wlan_data *data;

	if (!pdev)
		return;

	data = dev_get_drvdata(pdev->dev);
	if (!data)
		return;

	destroy_workqueue(data->wq);

	lvm_netdev_unregister(data->driver);

	lvm_netdev_free(data->netdev);

	kfree(data);
}
