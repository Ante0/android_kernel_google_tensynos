// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM Network Device
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 *
 * This file provides functions for managing LVM network devices. This
 * includes allocating and freeing network device structures, as well as
 * handling packet transmission and reception in coordination with the
 * LVM platform driver and buffer manager.
 */

#include <linux/errno.h>
#include <core/print.h>
#include <net/netdev.h>
#include <net/pktgen.h>
#include <net/platform.h>

extern struct lvm_net *net;

/**
 * lvm_netdev_start_xmit - Start transmitting a packet
 * @driver: Pointer to the LVM platform driver structure
 *
 * This function starts the transmission of a packet. It allocates a buffer
 * from the driver's TX buffer manager, sets the buffer's network device,
 * and calls the driver's start_xmit callback function to initiate the
 * transmission. It also updates the network device's statistics based on
 * the result of the transmission.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_netdev_start_xmit(struct lvm_platform_driver *driver)
{
	struct lvm_netdev *netdev;
	struct lvm_buffer *buf;
	netdev_tx_t ret;

	if (!driver)
		return -EINVAL;

	netdev = driver->netdev;

	if (!netdev || !netdev->netdev_ops ||
	    !netdev->netdev_ops->ndo_start_xmit)
		return -EINVAL;

	/* Allocate a buffer from the driver's TX buffer manager */
	buf = lvm_buffer_alloc(driver->tx_manager);
	if (!buf)
		return -ENOMEM;

	buf->netdev = netdev;
	buf->data_len = net->pktgen->len;
	memcpy(buf->data_va, net->pktgen->raw, net->pktgen->len);

	LVM_DBG("LVM network layer generated a tx packet (tkid=%d, len=%zu)\n", buf->tkid,
		buf->data_len);

	ret = netdev->netdev_ops->ndo_start_xmit(buf, netdev);

	/* Update the network device's statistics */
	spin_lock_bh(&netdev->stats_lock);

	if (ret == NETDEV_TX_OK) {
		netdev->stats.tx_packets += 1;
		netdev->stats.tx_bytes   += buf->data_len;
	} else if (ret == NETDEV_TX_BUSY) {
		netdev->stats.tx_dropped += 1;
	}

	spin_unlock_bh(&netdev->stats_lock);

	return 0;
}

/**
 * lvm_netdev_netif_rx - Receive a packet from the network interface
 * @manager: Pointer to the buffer manager structure
 * @buf: Pointer to the buffer containing the received packet
 *
 * This function receives a packet from the network interface. It frees the
 * buffer and updates the network device's statistics.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_netdev_netif_rx(struct lvm_manager *manager, struct lvm_buffer *buf)
{
	struct lvm_netdev *netdev;

	if (!manager || !buf)
		return -EINVAL;

	netdev = buf->netdev;

	/* Update the network device's statistics */
	spin_lock_bh(&netdev->stats_lock);
	netdev->stats.rx_packets += 1;
	netdev->stats.rx_bytes += buf->data_len;
	spin_unlock_bh(&netdev->stats_lock);

	LVM_DBG("LVM network layer released a rx packet (tkid=%d) back to buffer manager\n",
		buf->tkid);

	lvm_buffer_free(manager, buf);

	return 0;
}

/**
 * lvm_netdev_open - Open the LVM network device
 * @driver: Pointer to the LVM platform driver structure
 *
 * This function opens the LVM network device associated with the given
 * platform driver. It retrieves the network device from the driver
 * structure and calls the ndo_open callback function of the network
 * device operations.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_netdev_open(struct lvm_platform_driver *driver)
{
	struct lvm_netdev *netdev;
	int ret = 0;

	if (!driver)
		return -EINVAL;

	netdev = driver->netdev;

	if (!netdev || !netdev->netdev_ops ||
	    !netdev->netdev_ops->ndo_open)
		return -EINVAL;

	ret = netdev->netdev_ops->ndo_open(netdev);
	if (ret)
		return ret;

	return 0;
}

/**
 * lvm_netdev_stop - Stop the LVM network device
 * @driver: Pointer to the LVM platform driver structure
 *
 * This function stops the LVM network device associated with the given
 * platform driver. It retrieves the network device from the driver
 * structure and calls the ndo_stop callback function of the network
 * device operations.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_netdev_stop(struct lvm_platform_driver *driver)
{
	struct lvm_netdev *netdev;
	int ret = 0;

	if (!driver)
		return -EINVAL;

	netdev = driver->netdev;

	if (!netdev || !netdev->netdev_ops ||
	    !netdev->netdev_ops->ndo_stop)
		return -EINVAL;

	ret = netdev->netdev_ops->ndo_stop(netdev);
	if (ret)
		return ret;

	return 0;
}

/**
 * lvm_netdev_alloc - Allocate a LVM network device structure
 * @name: Name of the network device
 *
 * This function allocates and initializes a LVM network device structure.
 *
 * Return: Pointer to the allocated LVM network device structure,
 *         NULL on error.
 */
struct lvm_netdev *lvm_netdev_alloc(const char *name)
{
	struct lvm_netdev *netdev;

	WARN_ON(strlen(name) >= sizeof(netdev->name));

	/* Initialize the LVM network device structure */
	netdev = kzalloc(sizeof(struct lvm_netdev), GFP_KERNEL);
	if (!netdev)
		return NULL;

	spin_lock_init(&netdev->stats_lock);
	strcpy(netdev->name, name);

	return netdev;
}

/**
 * lvm_netdev_free - Free a LVM network device structure
 * @netdev: Pointer to the LVM network device structure
 *
 * This function frees the memory allocated for a LVM network device structure.
 */
void lvm_netdev_free(struct lvm_netdev *netdev)
{
	if (!netdev)
		return;

	kfree(netdev);
}
