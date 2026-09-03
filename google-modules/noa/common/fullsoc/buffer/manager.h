/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM Buffer Manager Definitions
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_BUF_MANAGER_H__
#define __LVM_BUF_MANAGER_H__

#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/types.h>

/**
 * enum manager_type - The type of the buffer manager.
 * @MANAGER_TYPE_RX: Buffer manager for receive path.
 * @MANAGER_TYPE_TX: Buffer manager for transmit path.
 * @__MANAGER_TYPE_MAX: Number of buffer manager types.
 */
enum {
	MANAGER_TYPE_RX,
	MANAGER_TYPE_TX,

	__MANAGER_TYPE_MAX,
};


/**
 * struct lvm_buffer - Structure representing a buffer in the LVM buffer manager
 * @tkid: Unique identifier (Token ID) for the buffer
 * @entry: List head for managing the buffer in the free list
 * @netdev: Pointer to the network device associated with this buffer (if any)
 * @head_len: Length of the header section of the buffer
 * @data_len: Length of the data section of the buffer
 * @head_va: Virtual address of the header section
 * @data_va: Virtual address of the data section
 * @head_pa: Physical address of the header section
 * @data_pa: Physical address of the data section
 */
struct lvm_buffer {
	u32			tkid;
	struct list_head	entry;
	struct lvm_netdev	*netdev;

	size_t			head_len;
	size_t			data_len;

	void			*head_va;
	void			*data_va;

	dma_addr_t		head_pa;
	dma_addr_t		data_pa;
};


/**
 * struct lvm_manager - Structure representing an LVM buffer manager
 * @type: The type of the buffer manager
 * @available: Number of buffers currently available in the manager
 * @state: State flags for the buffer manager
 * @priv: Private data for the buffer manager
 * @hashtable: Hashtable for looking up buffers by their TKID
 * @queue: List head for the free list of buffers
 * @buf_num: Total number of buffers managed by the manager
 * @buf_len: Length of each buffer in the manager
 * @pool_va: Virtual address of the buffer pool
 * @pool_pa: Physical address of the buffer pool
 */
struct lvm_manager {
	u8			type;
	u32			available;
	unsigned long		state;
	void			*priv;
	struct lvm_buffer	*hashtable;
	struct list_head	queue;

	size_t			buf_num;
	size_t			buf_len;

	void			*pool_va;
	dma_addr_t		pool_pa;
};


struct lvm_buffer *lvm_buffer_alloc(struct lvm_manager *manager);
void lvm_buffer_free(struct lvm_manager *manager, struct lvm_buffer *buf);
void lvm_buffer_free_by_tkid(struct lvm_manager *manager, u32 tkid);
struct lvm_buffer *lvm_buffer_lookup(struct lvm_manager *manager, u32 tkid);
struct lvm_manager *lvm_manager_alloc(struct device *dev, u8 type, size_t count,
				      size_t buf_len, size_t head_len);
void lvm_manager_free(struct device *dev, struct lvm_manager *manager);

#endif  /* __LVM_BUF_MANAGER_H__ */
