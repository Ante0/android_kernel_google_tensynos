// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM WLAN Device Ring
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 *
 * This file implements the management of WLAN device rings for the WLAN
 * driver. It provides functions for allocating, configuring, freeing, and
 * accessing WLAN device rings.
 */

#include "ring.h"

/**
 * lvm_wlan_single_ring_config - Configure the specified WLAN device ring
 * @data: Pointer to the WLAN driver main data structure
 * @ring: Pointer to the WLAN device ring structure
 *
 * This function configures the specified WLAN device ring by writing its parameters
 * to the corresponding registers.
 */
static void lvm_wlan_single_ring_config(struct wlan_data *data,
					struct wlan_ring *ring)
{
	if (!data || !ring)
		return;

	/* Enable the device ring */
	lvm_wlan_ring_reg_write(ring, WLAN_RING_REG_CTRL, true);

	/* Set the number of descriptors in the ring */
	lvm_wlan_ring_reg_write(ring, WLAN_RING_REG_NUM, ring->desc_num);

	/* Set the length of each descriptor in the ring */
	lvm_wlan_ring_reg_write(ring, WLAN_RING_REG_LEN, ring->desc_len);

	/* Set the read pointer of the ring */
	lvm_wlan_ring_reg_write(ring, WLAN_RING_REG_READ, ring->read_pos);

	/* Set the write pointer of the ring */
	lvm_wlan_ring_reg_write(ring, WLAN_RING_REG_WRITE, ring->write_pos);

	/* Set the base address of the descriptor ring */
	lvm_wlan_ring_reg_write(ring, WLAN_RING_REG_DESCBASE, ring->desc_pa);
}

/**
 * lvm_wlan_single_ring_alloc - Allocate and initialize a WLAN device ring
 * @data: Pointer to the WLAN driver main data structure
 * @ring_id: Ring ID
 *
 * This function allocates memory for a single WLAN device ring, maps its registers,
 * and initializes its parameters.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int lvm_wlan_single_ring_alloc(struct wlan_data *data, u8 ring_id)
{
	const struct wlan_param *param;
	struct wlan_ring *ring;
	size_t desc_num, desc_len;

	if (!data || ring_id >= __WLAN_RING_ID_MAX)
		return -EINVAL;

	/* Allocate memory for the WLAN device ring structure */
	ring = kzalloc(sizeof(struct wlan_ring), GFP_KERNEL);
	if (!ring)
		return -ENOMEM;

	param = data->param;
	desc_num = param->ring[ring_id].desc_num;
	desc_len = param->ring[ring_id].desc_len;

	/* Allocate DMA-coherent memory for the descriptors */
	ring->desc_va = dma_alloc_coherent(data->dev, desc_num * desc_len,
					   &ring->desc_pa, GFP_KERNEL);
	if (!ring->desc_va)
		return -ENOMEM;

	/* Get and map the base address of the ring registers */
	ring->regbase_pa = WLAN_RING_REG_BASE(ring_id);
	ring->regbase_va = ioremap(ring->regbase_pa, WLAN_RING_REG_RANGE);
	if (!ring->regbase_va)
		return -EINVAL;

	/* Initialize the WLAN device ring parameters */
	ring->ring_id = ring_id;
	ring->hw_id = param->ring[ring_id].hw_id;
	ring->type = param->ring[ring_id].type;
	ring->dir = param->ring[ring_id].dir;
	ring->read_pos = 0;
	ring->write_pos = 0;
	ring->desc_num = desc_num;
	ring->desc_len = desc_len;

	strcpy(ring->name, param->ring[ring_id].name);
	lvm_wlan_single_ring_config(data, ring);
	data->ring[ring_id] = ring;

	return 0;
}

/**
 * lvm_wlan_single_ring_free - Free a WLAN device ring
 * @data: Pointer to the WLAN driver main data structure
 * @ring_id: Ring ID
 *
 * This function frees the memory for the specified WLAN device ring
 * and unmaps its registers.
 */
static void lvm_wlan_single_ring_free(struct wlan_data *data, u8 ring_id)
{
	struct wlan_ring *ring;

	if (!data || ring_id >= __WLAN_RING_ID_MAX)
		return;

	ring = data->ring[ring_id];

	/* Unmap the ring registers */
	iounmap(ring->regbase_va);

	/* Free the memory allocated for the descriptors and the ring structure */
	dma_free_coherent(data->dev, ring->desc_num * ring->desc_len,
			  ring->desc_va, ring->desc_pa);
	kfree(ring);
}

/**
 * lvm_wlan_ring_write_cnt_get - Get the number of available write slots
 *                               in a WLAN device ring
 * @ring: Pointer to the WLAN device ring structure
 *
 * This function calculates the number of available write slots in a WLAN device ring
 * based on the current read and write pointers.
 *
 * Return: Number of available write slots.
 */
u32 lvm_wlan_ring_write_cnt_get(struct wlan_ring *ring)
{
	u32 cnt = 0;

	if (!ring)
		return 0;

	ring->read_pos = lvm_wlan_ring_reg_read(ring, WLAN_RING_REG_READ);

	/*
	 * Calculate the number of available write slots based on the current
	 * read and write pointers. Handle the case where the write pointer
	 * wraps around the end of the device ring.
	 */
	cnt = (ring->write_pos < ring->read_pos) ?
	       ring->read_pos - ring->write_pos - 1 :
	       ring->desc_num - ring->write_pos - 1 + ring->read_pos;

	if (cnt > ring->desc_num)
		cnt = 0;

	return cnt;
}

/**
 * lvm_wlan_ring_read_cnt_get - Get the number of available read slots
 *                              in a WLAN device ring
 * @ring: Pointer to the WLAN device ring structure
 *
 * This function calculates the number of available read slots in a WLAN device ring
 * based on the current read and write pointers.
 *
 * Return: Number of available read slots.
 */
u32 lvm_wlan_ring_read_cnt_get(struct wlan_ring *ring)
{
	u32 cnt = 0;

	if (!ring)
		return 0;

	ring->write_pos = lvm_wlan_ring_reg_read(ring, WLAN_RING_REG_WRITE);

	cnt = (ring->write_pos < ring->read_pos) ?
	       ring->desc_num - ring->read_pos + ring->write_pos :
	       ring->write_pos - ring->read_pos;

	if (cnt > ring->desc_num)
		cnt = 0;

	return cnt;
}

/**
 * lvm_wlan_ring_init - Initialize all WLAN device rings
 * @data: Pointer to the WLAN driver main data structure
 *
 * This function initializes all WLAN device rings by allocating and configuring
 * each device ring
 *
 * Return: 0 on success, negative error code otherwise
 */
int lvm_wlan_ring_init(struct wlan_data *data)
{
	int ret = 0, i;

	/* Allocate and initialize each WLAN device ring */
	for (i = 0; i < __WLAN_RING_ID_MAX; ++i) {
		ret = lvm_wlan_single_ring_alloc(data, i);
		if (ret)
			return -ENOMEM;
	}

	return 0;
}

/**
 * lvm_wlan_ring_deinit - Deinitialize all WLAN device rings
 * @data: Pointer to the WLAN driver main data structure
 *
 * This function deinitializes all WLAN device rings by freeing the memory and
 * unmapping the registers for each ring
 */
void lvm_wlan_ring_deinit(struct wlan_data *data)
{
	int i;

	/* Free each WLAN device ring */
	for (i = 0; i < __WLAN_RING_ID_MAX; ++i)
		lvm_wlan_single_ring_free(data, i);
}
