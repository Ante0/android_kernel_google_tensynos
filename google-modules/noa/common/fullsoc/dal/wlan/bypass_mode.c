// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM Device Abstraction Layer for Bypass Mode
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 *
 * This file provides the glue logic between the WLAN driver and the
 * underlying WLAN device. It implements the platform bus operations,
 * including initialization, de-initialization, packet transmission,
 * reception, and control operations.
 */

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/types.h>
#include <core/print.h>
#include <dal/wlan/api.h>
#include <drivers/wlan/format.h>
#include <drivers/wlan/interface.h>
#include <drivers/wlan/mcu.h>
#include <drivers/wlan/topbank.h>

/**
 * lvm_dal_rxbm_sync - Synchronize RX buffer metadata with the WLAN device
 * @priv: Pointer to the WLAN data structure
 * @count: The number of RX buffers to synchronize
 * @rxbm: An array of pointers to the RX buffers to synchronize
 *
 * This function synchronizes the RX buffer metadata with the WLAN device.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int lvm_dal_rxbm_sync(void *priv, u32 count, struct lvm_buffer **rxbm)
{
	struct wlan_data *data = priv;
	struct wlan_topbank *topbank;
	struct wlan_ring *ring;
	struct host_rxbuf_post *desc;
	int i;

	if (!data || !rxbm)
		return -EINVAL;

	topbank = data->topbank;
	ring = data->ring[WLAN_RING_ID_RX_POST0];

	if (!topbank || !ring)
		return -EINVAL;

	for (i = 0; i < count; ++i) {
		desc = (struct host_rxbuf_post *)
			lvm_wlan_ring_write_base_get(ring);

		if (!desc)
			return -EINVAL;

		desc->cmn_hdr.request_id = rxbm[i]->tkid;
		desc->data_buf_len = rxbm[i]->data_len;
		desc->data_buf_addr = rxbm[i]->data_pa;

		lvm_wlan_ring_write_pos_move(ring);

		LVM_DBG("LVM wlan refilled a rx buffer (tkid=%d, pa=0x%llx) to fake device\n",
			rxbm[i]->tkid, rxbm[i]->data_pa);
	}

	lvm_wlan_ring_reg_write(ring, WLAN_RING_REG_WRITE, ring->write_pos);
	lvm_wlan_topbank_reg_write(topbank, WLAN_TOP_REG_IN_RING_DOORBELL,
				   1 << ring->hw_id);

	return 0;
}

/**
 * lvm_dal_init - Initialize the WLAN DAL
 * @_dev: Pointer to the device structure
 * @priv: Pointer to the WLAN data structure
 *
 * This function initializes the WLAN DAL based on the
 * provided WLAN data structure.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int lvm_dal_init(void *_dev, void *priv)
{
	lvm_dal_rxbm_sync_cb_register(lvm_dal_rxbm_sync);

	return 0;
}

/**
 * lvm_dal_exit - De-initialize the WLAN DAL
 * @priv: Pointer to the WLAN data structure
 *
 * This function de-initializes the WLAN DAL.
 */
static void lvm_dal_exit(void *priv)
{
	lvm_dal_rxbm_sync_cb_register(NULL);
}

static int lvm_dal_request_irq(void *priv)
{
	struct wlan_data *data = priv;
	int ret = 0;

	if (!data)
		return -EINVAL;

	ret = lvm_wlan_irq_request(data);
	if (ret)
		return -EINVAL;

	return 0;
}

/**
 * lvm_dal_tx - Transmit a packet through the WLAN DAL
 * @priv: Pointer to the WLAN data structure
 * @buf: Pointer to the buffer containing the packet data
 * @ifidx: Interface index for the packet
 *
 * This function transmits a packet through the WLAN DAL.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int lvm_dal_tx(void *priv, void *buf, u32 ifidx)
{
	struct wlan_data *data = priv;
	struct wlan_topbank *topbank;
	struct wlan_ring *ring;
	void *write_base;
	u32 available;

	if (!data || !buf)
		return -EINVAL;

	topbank = data->topbank;
	ring = data->ring[WLAN_RING_ID_TX_DATA0];

	if (!topbank || !ring)
		return -EINVAL;

	available = lvm_wlan_ring_write_cnt_get(ring);
	if (!available)
		return -EINVAL;

	write_base = lvm_wlan_ring_write_base_get(ring);
	if (!write_base)
		return -EINVAL;

	lvm_wlan_txd_prepare(data, buf, ifidx, write_base);

	lvm_wlan_ring_write_pos_move(ring);
	lvm_wlan_ring_reg_write(ring, WLAN_RING_REG_WRITE, ring->write_pos);
	lvm_wlan_topbank_reg_write(topbank, WLAN_TOP_REG_IN_RING_DOORBELL,
				   1 << ring->hw_id);

	LVM_DBG("LVM wlan wrote a tx packet (tkid=%d) to fake device\n",
		((struct lvm_buffer *)buf)->tkid);

	return 0;
}

/**
 * lvm_dal_tx_cpl - Process TX completions from the WLAN device
 * @priv: Pointer to the WLAN data structure
 * @cnt: Pointer to store the number of TX completions processed
 *
 * This function processes TX completions from the WLAN device. It checks
 * the TX completion ring for completed packets and frees the corresponding
 * TX buffers back to the TX buffer manager.
 *
 * Return: True if there are more TX completions to process, false otherwise.
 */
static bool lvm_dal_tx_cpl(void *priv, u32 *cnt)
{
	struct wlan_data *data = priv;
	struct wlan_ring *ring;
	struct host_txbuf_cmpl *desc;
	u32 rx_status;
	u32 available;
	u32 mask;

	if (!data)
		return false;

	ring = data->ring[WLAN_RING_ID_TX_CPL0];
	rx_status = data->irq.rx_status;
	mask = 1 << ring->hw_id;

	if (!ring || !(rx_status & mask))
		return false;

	available = lvm_wlan_ring_read_cnt_get(ring);
	if (!available)
		return false;

	while (available) {
		desc = (struct host_txbuf_cmpl *)
			lvm_wlan_ring_read_base_get(ring);

		if (!desc)
			break;

		lvm_wlan_txcpl_process(data, desc);
		lvm_wlan_ring_read_pos_move(ring);

		available--;
	}

	if (!available)
		data->irq.rx_status &= ~mask;

	lvm_wlan_ring_reg_write(ring, WLAN_RING_REG_READ, ring->read_pos);

	return !!available;
}

/**
 * lvm_dal_rx - Receive packets through the WLAN DAL
 * @priv: Pointer to the WLAN data structure
 * @cnt: Pointer to store the number of packets received
 *
 * This function receives packets through the WLAN DAL.
 *
 * Return: True if there are more packets to receive, false otherwise.
 */
static bool lvm_dal_rx(void *priv, u32 *cnt)
{
	struct wlan_data *data = priv;
	struct wlan_ring *ring;
	struct wlan_rxbuf_cmpl *desc;
	u32 rx_status;
	u32 available;
	u32 mask;

	if (!data)
		return false;

	ring = data->ring[WLAN_RING_ID_RX_DATA0];
	rx_status = data->irq.rx_status;
	mask = 1 << ring->hw_id;

	if (!ring || !(rx_status & mask))
		return false;

	available = lvm_wlan_ring_read_cnt_get(ring);
	if (!available)
		return false;

	while (available) {
		desc = (struct wlan_rxbuf_cmpl *)
			lvm_wlan_ring_read_base_get(ring);

		if (!desc)
			break;

		lvm_wlan_rxcpl_process(data, desc);
		lvm_wlan_ring_read_pos_move(ring);

		available--;
	}

	if (!available)
		data->irq.rx_status &= ~mask;

	lvm_wlan_ring_reg_write(ring, WLAN_RING_REG_READ, ring->read_pos);
	lvm_wlan_rxpost_process(data);

	return !!available;
}

/**
 * lvm_dal_rx_replenish - Replenish RX buffers
 * @priv: Pointer to the WLAN data structure
 * @count: Number of RX buffers to replenish
 * @reuse_tkid: Whether to reuse the TKID
 *
 * This function replenishes RX buffers in the WLAN DAL. It calls the
 * WLAN driver's RX buffer allocation function to allocate new buffers.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
static int lvm_dal_rx_replenish(void *priv, u32 count, bool reuse_tkid)
{
	struct wlan_data *data = priv;

	if (!data)
		return -EINVAL;

	return lvm_wlan_rxpost_buf_alloc(data, count);
}

/**
 * vendor_bus_ops - Platform bus operations for the bypass mode
 *
 * This structure defines the platform bus operations for the bypass mode.
 * It is used by the WLAN driver to interact with the underlying hardware.
 */
struct platform_bus_ops vendor_bus_ops = {
	.init			= lvm_dal_init,
	.exit			= lvm_dal_exit,
	.start			= NULL,
	.stop			= NULL,
	.request_irq		= lvm_dal_request_irq,
	.tx			= lvm_dal_tx,
	.tx_cpl			= lvm_dal_tx_cpl,
	.rx			= lvm_dal_rx,
	.rx_replenish		= lvm_dal_rx_replenish,
	.tx_queue_active	= NULL,
	.sta_active		= NULL,
};

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Henry Yen <henryyen@google.com>");
MODULE_DESCRIPTION("LVM Device Abstraction Layer for Bypass Mode");
