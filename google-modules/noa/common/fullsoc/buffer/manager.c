// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM Buffer Manager
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 *
 * This file implements the LVM buffer manager. The buffer manager is
 * responsible for dynamically allocating and managing buffers from
 * a shared pool. These buffers are primarily used for data transfer
 * operations within the LVM.
 *
 * Key features include:
 *  - Allocation of buffers from a pre-allocated pool
 *  - Freeing of buffers back to the linkedlist for reuse
 *  - Efficient lookup of buffers using a hashtable
 */

#include <linux/errno.h>
#include <linux/list.h>
#include <core/print.h>
#include <buffer/manager.h>

/**
 * lvm_buffer_alloc - Allocate a buffer from the manager
 * @manager: The buffer manager
 *
 * This function allocates a buffer from the buffer manager. The allocated
 * buffer is removed from the manager's free list.
 *
 * Returns: A pointer to the allocated buffer, or NULL on failure.
 */
struct lvm_buffer *lvm_buffer_alloc(struct lvm_manager *manager)
{
	struct lvm_buffer *buf;

	if (!manager)
		return NULL;

	/* Check if the free list is empty */
	if (list_empty(&manager->queue))
		return NULL;

	/* Get the buffer from the tail of the free list */
	buf = list_last_entry(&manager->queue, struct lvm_buffer, entry);
	list_del(&buf->entry);
	manager->available--;

	return buf;
}

/**
 * lvm_buffer_free - Free a buffer back to the manager
 * @manager: The buffer manager
 * @buf: The buffer to free
 *
 * This function frees a buffer back to the buffer manager. The freed buffer
 * is added back to the manager's free list.
 */
void lvm_buffer_free(struct lvm_manager *manager, struct lvm_buffer *buf)
{
	if (!manager || !buf)
		return;

	/* Add the buffer to the tail of the free list */
	list_add_tail(&buf->entry, &manager->queue);
	manager->available++;
}

/**
 * lvm_buffer_free_by_tkid - Free a buffer back to the manager by its TKID
 * @manager: The buffer manager
 * @tkid: The TKID of the buffer to free
 *
 * This function frees a buffer back to the buffer manager by its TKID.
 * It first looks up the buffer using the TKID and then frees it.
 */
void lvm_buffer_free_by_tkid(struct lvm_manager *manager, u32 tkid)
{
	struct lvm_buffer *buf;

	if (!manager)
		return;

	/* Check for invalid TKID */
	if (tkid > manager->buf_num || !tkid)
		return;

	/* Look up the buffer using the TKID */
	buf = lvm_buffer_lookup(manager, tkid);

	/* Free the buffer if found */
	if (buf)
		lvm_buffer_free(manager, buf);
}

/**
 * lvm_buffer_lookup - Lookup a buffer by its TKID
 * @manager: The buffer manager
 * @tkid: The TKID of the buffer to lookup
 *
 * This function looks up a buffer in the buffer manager by its TKID.
 *
 * Returns: A pointer to the buffer, or NULL if not found.
 */
struct lvm_buffer *lvm_buffer_lookup(struct lvm_manager *manager, u32 tkid)
{
	if (!manager)
		return NULL;

	/* Check for invalid TKID */
	if (tkid > manager->buf_num || !tkid)
		return NULL;

	/* Return the buffer from the hashtable */
	return &manager->hashtable[tkid - 1];
}

/**
 * lvm_manager_alloc - Allocate a buffer manager
 * @dev: The device to allocate memory from
 * @type: The type of the buffer manager
 * @count: The number of buffers to allocate
 * @buf_len: The length of each buffer
 * @head_len: The length of the header for each buffer
 *
 * This function allocates a buffer manager and initializes its internal
 * structures. It allocates memory for the manager, the buffer hashtable,
 * and the buffer pool. It then initializes each buffer in the pool and
 * adds it to the free list.
 *
 * Returns: A pointer to the allocated buffer manager, or NULL on failure.
 */
struct lvm_manager *lvm_manager_alloc(struct device *dev, u8 type, size_t count,
				      size_t buf_len, size_t head_len)
{
	struct lvm_manager *manager;
	struct lvm_buffer *buf;
	int i;

	/* Allocate structure for the buffer manager */
	manager = kzalloc(sizeof(struct lvm_manager), GFP_KERNEL);
	if (!manager)
		return NULL;

	/* Allocate memory for the buffer hashtable */
	manager->hashtable = kzalloc(count * sizeof(struct lvm_buffer),
				     GFP_KERNEL);
	if (!manager->hashtable)
		goto err_manager;

	manager->pool_va = kzalloc(count * buf_len, GFP_KERNEL);
	if (!manager->pool_va) {
		LVM_ERR("%s(): failed to allocate buffer pool\n", __func__);
		goto err_hashtable;
	}

	manager->pool_pa = dma_map_single(dev, manager->pool_va,
					  count * buf_len, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, manager->pool_pa)) {
		LVM_ERR("%s(): failed to map buffer pool\n", __func__);
		goto err_buffer;
	}

	/* Initialize the buffer manager */
	memset(manager->hashtable, 0, count * sizeof(struct lvm_buffer));
	manager->type = type;
	manager->available = count;
	manager->buf_num = count;
	manager->buf_len = buf_len;
	INIT_LIST_HEAD(&manager->queue);

	/*
	 * Initialize each buffer in the pool and add it to the free list.
	 *
	 * The loop iterates through each buffer in the pool and sets up
	 * its TKID, header and data lengths, virtual and physical addresses.
	 * The buffer is then added to the tail of the free list.
	 */
	for (i = 0; i < count; ++i) {
		buf = &manager->hashtable[i];
		buf->tkid = i + 1;
		buf->head_len = head_len;
		buf->data_len = buf_len - head_len;

		/* Calculate the virtual and physical addresses of the header */
		buf->head_va = manager->pool_va + (i * buf_len);
		buf->head_pa = manager->pool_pa + (i * buf_len);

		/* Calculate the virtual and physical addresses of the data */
		buf->data_va = buf->head_va + head_len;
		buf->data_pa = buf->head_pa + head_len;

		/* Add the buffer to the tail of the free list */
		list_add_tail(&buf->entry, &manager->queue);
	}

	return manager;

err_buffer:
	kfree(manager->pool_va);

err_hashtable:
	kfree(manager->hashtable);

err_manager:
	kfree(manager);

	return NULL;
}

/**
 * lvm_manager_free - Free a buffer manager
 * @dev: The device to free memory to
 * @manager: The buffer manager to free
 *
 * This function frees a buffer manager and all its associated resources.
 * It frees the memory allocated for the buffer pool, the buffer hashtable,
 * and the manager itself.
 */
void lvm_manager_free(struct device *dev, struct lvm_manager *manager)
{
	if (!manager)
		return;

	/* Free the DMA buffer used for the buffer pool */
	dma_free_coherent(dev, manager->buf_num * manager->buf_len,
			  manager->pool_va, manager->pool_pa);

	/* Free the buffer hashtable */
	kfree(manager->hashtable);

	/* Free the buffer manager */
	kfree(manager);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Henry Yen <henryyen@google.com>");
MODULE_DESCRIPTION("LVM Buffer Manager");

