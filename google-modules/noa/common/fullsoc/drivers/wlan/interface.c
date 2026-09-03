// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM WLAN Interface
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 *
 * This file implements the network interface operations for the LVM WLAN
 * driver. It handles the activation and shutdown of the WLAN interface,
 * as well as the transmission and reception of data packets.
 * It interacts with the DAL layer to perform low-level hardware operations.
 */

#include <linux/errno.h>
#include <core/print.h>
#include <dal/wlan/api.h>
#include "interface.h"

/**
 * lvm_wlan_open - Activate the WLAN interface
 * @netdev: Pointer to the LVM network device structure
 *
 * This function activates the WLAN interface. It initializes the WLAN rings
 * and starts the platform bus operation.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int lvm_wlan_open(struct lvm_netdev *netdev)
{
	struct wlan_data *data;
	int ret = 0;
	int i;

	if (!netdev)
		return -EINVAL;

	data = (struct wlan_data *)netdev->priv;

	/* Initialize the WLAN rings */
	ret = lvm_wlan_ring_init(data);
	if (ret) {
		LVM_ERR("LVM wlan ring init failed: %d\n", ret);
		return -EINVAL;
	}

	lvm_wlan_rxpost_process(data);

	/* Start the platform bus operation through DAL layer */
	ret = platform_bus_start(data);
	if (ret)
		goto err_ring;

	/* Activate the TX queues through DAL layer */
	for (i = 0; i < WLAN_RING_TX_DATA_NUM; ++i)
		platform_bus_tx_queue_active(data, i, true);

	return 0;

err_ring:
	/* De-initialize the WLAN rings on error */
	lvm_wlan_ring_deinit(data);

	return ret;
}

/**
 * lvm_wlan_stop - Shutdown the WLAN interface
 * @netdev: Pointer to the LVM network device structure
 *
 * This function shutdowns the WLAN interface. It deactivates the TX queues,
 * stops the platform bus operation, and de-initializes the WLAN rings.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int lvm_wlan_stop(struct lvm_netdev *netdev)
{
	struct wlan_data *data;
	int i;

	if (!netdev)
		return -EINVAL;

	data = (struct wlan_data *)netdev->priv;

	/* Deactivate the TX queues through DAL layer */
	for (i = 0; i < WLAN_RING_TX_DATA_NUM; ++i)
		platform_bus_tx_queue_active(data, i, false);

	/* Stop the platform bus operation through DAL layer */
	platform_bus_stop(data);

	/* De-initialize the WLAN rings */
	lvm_wlan_ring_deinit(data);

	return 0;
}

/**
 * lvm_wlan_start_xmit - Transmit a packet on the WLAN interface
 * @buf: Pointer to the LVM buffer containing the packet data
 * @netdev: Pointer to the LVM network device structure
 *
 * This function transmits a packet on the WLAN interface. It calls the
 * DAL layer TX function to send the packet.
 *
 * Return: NETDEV_TX_OK on success, NETDEV_TX_BUSY on failure.
 */
static netdev_tx_t lvm_wlan_start_xmit(struct lvm_buffer *buf,
				       struct lvm_netdev *netdev)
{
	struct wlan_data *data;
	int ret = 0;

	if (!buf || !netdev)
		return -EINVAL;

	data = netdev->priv;

	/* Transmit the packet through DAL layer */
	ret = platform_bus_tx(data, buf, netdev->ifidx);
	if (ret < 0)
		return NETDEV_TX_BUSY;

	return NETDEV_TX_OK;
}

/**
 * wlan_netdev_ops - LVM network device operations for the WLAN driver
 *
 * This structure defines the LVM network device operations for the WLAN driver.
 */
struct lvm_netdev_ops wlan_netdev_ops = {
	.ndo_open		= lvm_wlan_open,
	.ndo_stop		= lvm_wlan_stop,
	.ndo_start_xmit		= lvm_wlan_start_xmit,
};

/**
 * lvm_wlan_txd_prepare - Prepare a WLAN TX vendor descriptor
 * @data: Pointer to the WLAN data structure
 * @_buf: Pointer to the LVM buffer containing the packet data
 * @ifidx: Interface index for the packet
 * @txd: Pointer to the WLAN TX descriptor to prepare
 *
 * This function prepares a WLAN TX descriptor for transmission. It fills
 * in the necessary fields in the descriptor based on the provided
 * packet buffer and interface index.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int lvm_wlan_txd_prepare(struct wlan_data *data, void *_buf,
			 u32 ifidx, struct host_txbuf_post *txd)
{
	struct lvm_buffer *buf = _buf;

	if (!data || !buf)
		return -EINVAL;

	/* Clear the TX descriptor */
	memset(txd, 0, sizeof(struct host_txbuf_post));

	/* Fill in the TX descriptor fields */
	txd->cmn_hdr.request_id = buf->tkid;
	txd->cmn_hdr.msg_type = 0xA;
	txd->data_buf_addr = buf->data_pa;
	txd->data_len = buf->data_len;
	txd->cmn_hdr.if_id = ifidx;

	return 0;
}

/**
 * lvm_wlan_txcpl_process - Process a WLAN TX completion descriptor
 * @data: Pointer to the WLAN data structure
 * @msg: Pointer to the WLAN TX completion descriptor
 *
 * This function processes a WLAN TX completion descriptor. It frees the
 * corresponding TX buffer back to the TX buffer manager.
 */
void lvm_wlan_txcpl_process(struct wlan_data *data, void *msg)
{
	struct lvm_manager *manager;
	struct host_txbuf_cmpl *txcpl = msg;

	if (!data || !txcpl)
		return;

	LVM_DBG("LVM wlan read a tx cpl (tkid=%d) from fake device\n", txcpl->cmn_hdr.request_id);

	manager = data->driver->tx_manager;

	/* Free the TX buffer based on TKID */
	lvm_buffer_free_by_tkid(manager, txcpl->cmn_hdr.request_id);
}

/**
 * lvm_wlan_rxcpl_process - Process a WLAN RX completion descriptor
 * @data: Pointer to the WLAN data structure
 * @msg: Pointer to the WLAN RX completion descriptor
 *
 * This function processes a WLAN RX completion descriptor. It retrieves
 * the corresponding RX buffer, sets the network device for the buffer,
 * and passes the buffer to the network stack for further processing.
 */
void lvm_wlan_rxcpl_process(struct wlan_data *data, void *msg)
{
	struct lvm_manager *manager;
	struct lvm_buffer *buf;
	struct wlan_rxbuf_cmpl *rxcpl = msg;

	if (!data || !rxcpl)
		return;

	LVM_DBG("LVM wlan read a rx packet (tkid=%d, len=%d) from fake device\n",
		rxcpl->cmn_hdr.request_id, rxcpl->data_len);

	manager = data->driver->rx_manager;

	/* Retrieve the RX buffer based on TKID */
	buf = lvm_buffer_lookup(manager, rxcpl->cmn_hdr.request_id);
	if (!buf) {
		LVM_ERR("%s(): failed to lookup buffer for tkid=%d\n",
			__func__, rxcpl->cmn_hdr.request_id);
		return;
	}

	/* Set the network device for the buffer */
	buf->netdev = data->netdev;

	/* Pass the buffer to the common network layer */
	lvm_netdev_netif_rx(manager, buf);
}

/**
 * lvm_wlan_rxpost_process - Process RX buffer posting
 * @data: Pointer to the WLAN data structure
 *
 * This function processes RX buffer posting. It calculates the number
 * of RX buffers to allocate based on the available buffers in the RX
 * buffer manager and the available space in the RX post ring. It then
 * calls the DAL layer function to allocate and post new RX buffers.
 */
void lvm_wlan_rxpost_process(struct wlan_data *data)
{
	struct wlan_ring *ring;
	u32 allocated = 0;

	if (!data)
		return;

	ring = data->ring[WLAN_RING_ID_RX_POST0];

	/*
	 * Calculate the number of RX buffers to allocate, taking the minimum
	 * of the available buffers in the RX buffer manager and the available
	 * space in the RX post ring.
	 */
	allocated = min(data->driver->rx_manager->available,
			lvm_wlan_ring_write_cnt_get(ring));

	/* Allocate and post new RX buffers through DAL layer */
	platform_bus_rx_replenish(data, allocated, false);
}

/**
 * lvm_wlan_rxpost_buf_alloc - Allocate and post RX buffers
 * @data: Pointer to the WLAN data structure
 * @count: Number of RX buffers to allocate and post
 *
 * This function allocates and posts RX buffers. It allocates buffers
 * from the RX buffer manager and synchronizes the RX buffer metadata
 * with the DAL layer.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int lvm_wlan_rxpost_buf_alloc(struct wlan_data *data, u32 count)
{
	struct lvm_manager *manager;
	struct lvm_buffer **rxbm;
	int i;

	if (!data)
		return -EINVAL;

	manager = data->driver->rx_manager;

	if (count > manager->available)
		return -EINVAL;

	/* Allocate an array to store pointers to the new RX buffers */
	rxbm = kzalloc(count * sizeof(struct lvm_buffer *), GFP_KERNEL);
	if (!rxbm)
		return -ENOMEM;

	/* Allocate new RX buffers from the RX buffer manager */
	for (i = 0; i < count; ++i)
		rxbm[i] = lvm_buffer_alloc(manager);

	/* Synchronize the RX buffer metadata with the DAL layer */
	lvm_dal_rxbm_sync_cb_invoke(data, count, rxbm);

	kfree(rxbm);

	return 0;
}
