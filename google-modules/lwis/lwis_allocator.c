// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS Recycling Memory Allocator
 *
 * Copyright (c) 2021 Google, LLC
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-allocator: " fmt

#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "lwis_allocator.h"
#include "lwis_commands.h"

#define LWIS_MIN_SLAB_IDX 12
#define LWIS_MAX_SLAB_IDX 19

static void allocator_block_pool_free_locked(struct lwis_device *lwis_dev,
					     struct lwis_allocator_block_pool *block_pool)
{
	struct lwis_allocator_block *block_to_free;
	unsigned long flags_alloc;

	if (block_pool == NULL) {
		dev_err(lwis_dev->dev, "block_pool is NULL\n");
		return;
	}
	if (block_pool->in_use_count != 0 || block_pool->in_use != NULL)
		dev_warn(lwis_dev->dev,
			 "block_pool %s still has %d block(s) in use during release\n",
			 block_pool->name, block_pool->in_use_count);

	/* Loop until both free and in_use lists are empty */
	for (;;) {
		spin_lock_irqsave(&lwis_dev->allocator_lock, flags_alloc);

		if (block_pool->free != NULL) {
			block_to_free = block_pool->free;
			/* Unlink from block_pool->free list */
			block_pool->free = block_to_free->next;
			if (block_pool->free != NULL)
				block_pool->free->prev = NULL;
			block_pool->free_count--;
		} else if (block_pool->in_use != NULL) {
			block_to_free = block_pool->in_use;
			/* Unlink from block_pool->in_use list */
			block_pool->in_use = block_to_free->next;
			if (block_pool->in_use != NULL)
				block_pool->in_use->prev = NULL;
			block_pool->in_use_count--;
		} else {
			/* No more blocks to free */
			spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags_alloc);
			break;
		}

		/* Remove from the global allocated_blocks hash table */
		hash_del(&block_to_free->node);

		spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags_alloc);

		/* Perform actual free operations outside the spinlock */
		kvfree(block_to_free->ptr);
		kfree(block_to_free);
	}
}

static struct lwis_allocator_block *
allocator_free_block_get_locked(struct lwis_allocator_block_pool *block_pool)
{
	struct lwis_allocator_block *head;

	if (block_pool == NULL) {
		pr_err("block_pool is NULL\n");
		return NULL;
	}
	if (block_pool->free == NULL)
		return NULL;

	head = block_pool->free;
	block_pool->free = head->next;
	if (block_pool->free != NULL)
		block_pool->free->prev = NULL;

	block_pool->free_count--;

	head->next = block_pool->in_use;
	if (head->next != NULL)
		head->next->prev = head;

	block_pool->in_use = head;
	block_pool->in_use_count++;

	return head;
}

static void allocator_free_block_put_locked(struct lwis_allocator_block_pool *block_pool,
					    struct lwis_allocator_block *block)
{
	if (block_pool == NULL) {
		pr_err("block_pool is NULL\n");
		return;
	}
	if (block == NULL) {
		pr_err("block is NULL\n");
		return;
	}

	if (block->next != NULL)
		block->next->prev = block->prev;

	if (block->prev != NULL)
		block->prev->next = block->next;
	else
		block_pool->in_use = block->next;

	block_pool->in_use_count--;

	if (block_pool->free != NULL)
		block_pool->free->prev = block;

	block->next = block_pool->free;
	block->prev = NULL;
	block_pool->free = block;
	block_pool->free_count++;
}

static struct lwis_allocator_block_pool *
allocator_get_block_pool(struct lwis_allocator_block_mgr *block_mgr, int idx)
{
	struct lwis_allocator_block_pool *block_pool;

	switch (idx) {
	case 12:
		block_pool = &block_mgr->pool_4k;
		break;
	case 13:
		block_pool = &block_mgr->pool_8k;
		break;
	case 14:
		block_pool = &block_mgr->pool_16k;
		break;
	case 15:
		block_pool = &block_mgr->pool_32k;
		break;
	case 16:
		block_pool = &block_mgr->pool_64k;
		break;
	case 17:
		block_pool = &block_mgr->pool_128k;
		break;
	case 18:
		block_pool = &block_mgr->pool_256k;
		break;
	case 19:
		block_pool = &block_mgr->pool_512k;
		break;
	default:
		pr_err("size is not supportted\n");
		return NULL;
	}

	return block_pool;
}

int lwis_allocator_init(struct lwis_device *lwis_dev)
{
	struct lwis_allocator_block_mgr *block_mgr;

	if (lwis_dev == NULL)
		return -EINVAL;

	mutex_lock(&lwis_dev->client_lock);

	if (lwis_dev->block_mgr != NULL) {
		block_mgr = lwis_dev->block_mgr;
		block_mgr->ref_count++;
		mutex_unlock(&lwis_dev->client_lock);
		return 0;
	}

	block_mgr = kzalloc(sizeof(struct lwis_allocator_block_mgr), GFP_KERNEL);
	if (block_mgr == NULL) {
		mutex_unlock(&lwis_dev->client_lock);
		return -ENOMEM;
	}

	/* Empty hash table for allocated blocks */
	hash_init(block_mgr->allocated_blocks);

	/* Initialize block pools */
	strscpy(block_mgr->pool_4k.name, "lwis-block-4k", LWIS_MAX_NAME_STRING_LEN);
	strscpy(block_mgr->pool_8k.name, "lwis-block-8k", LWIS_MAX_NAME_STRING_LEN);
	strscpy(block_mgr->pool_16k.name, "lwis-block-16k", LWIS_MAX_NAME_STRING_LEN);
	strscpy(block_mgr->pool_32k.name, "lwis-block-32k", LWIS_MAX_NAME_STRING_LEN);
	strscpy(block_mgr->pool_64k.name, "lwis-block-64k", LWIS_MAX_NAME_STRING_LEN);
	strscpy(block_mgr->pool_128k.name, "lwis-block-128k", LWIS_MAX_NAME_STRING_LEN);
	strscpy(block_mgr->pool_256k.name, "lwis-block-256k", LWIS_MAX_NAME_STRING_LEN);
	strscpy(block_mgr->pool_512k.name, "lwis-block-512k", LWIS_MAX_NAME_STRING_LEN);
	strscpy(block_mgr->pool_large.name, "lwis-block-large", LWIS_MAX_NAME_STRING_LEN);

	/* Initialize reference count */
	block_mgr->ref_count = 1;

	lwis_dev->block_mgr = block_mgr;
	mutex_unlock(&lwis_dev->client_lock);
	return 0;
}

void lwis_allocator_release(struct lwis_device *lwis_dev)
{
	struct lwis_allocator_block_mgr *block_mgr;

	if (lwis_dev == NULL)
		return;

	mutex_lock(&lwis_dev->client_lock);

	block_mgr = lwis_dev->block_mgr;
	if (block_mgr == NULL) {
		dev_err(lwis_dev->dev, "%s: block_mgr is NULL\n", __func__);
		mutex_unlock(&lwis_dev->client_lock);
		return;
	}

	block_mgr->ref_count--;
	/*
	 * A ref_count of 1 means only the probe reference remains (no active
	 * clients). In this case, we free all blocks in the pools to return
	 * memory to the system, but keep the block_mgr structure allocated
	 * to support early allocations before the first open or after all closes.
	 */
	if (block_mgr->ref_count > 1) {
		mutex_unlock(&lwis_dev->client_lock);
		return;
	}

	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_4k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_8k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_16k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_32k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_64k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_128k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_256k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_512k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_large);

	mutex_unlock(&lwis_dev->client_lock);
}

static void
allocator_block_pool_purge_free_only_locked(struct lwis_device *lwis_dev,
					    struct lwis_allocator_block_pool *block_pool)
{
	struct lwis_allocator_block *block_to_free;
	unsigned long flags_alloc;

	if (block_pool == NULL)
		return;

	if (block_pool->in_use_count > 0) {
		dev_dbg(lwis_dev->dev,
			"Power down purge: pool %s belonging to device %s leaves %u block(s) in_use intact\n",
			block_pool->name, lwis_dev->name, block_pool->in_use_count);
	}

	/* Loop until free list is empty */
	for (;;) {
		spin_lock_irqsave(&lwis_dev->allocator_lock, flags_alloc);

		if (block_pool->free != NULL) {
			block_to_free = block_pool->free;
			/* Unlink from block_pool->free list */
			block_pool->free = block_to_free->next;
			if (block_pool->free != NULL)
				block_pool->free->prev = NULL;
			block_pool->free_count--;
		} else {
			/* No more free blocks to purge */
			spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags_alloc);
			break;
		}

		/* Remove from the global allocated_blocks hash table */
		hash_del(&block_to_free->node);

		spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags_alloc);

		/* Perform actual free operations outside the spinlock */
		kvfree(block_to_free->ptr);
		kfree(block_to_free);
	}
}

void lwis_allocator_purge_pools_locked(struct lwis_device *lwis_dev)
{
	struct lwis_allocator_block_mgr *block_mgr;

	if (lwis_dev == NULL)
		return;

	block_mgr = lwis_dev->block_mgr;
	if (block_mgr == NULL)
		return;

	allocator_block_pool_purge_free_only_locked(lwis_dev, &block_mgr->pool_4k);
	allocator_block_pool_purge_free_only_locked(lwis_dev, &block_mgr->pool_8k);
	allocator_block_pool_purge_free_only_locked(lwis_dev, &block_mgr->pool_16k);
	allocator_block_pool_purge_free_only_locked(lwis_dev, &block_mgr->pool_32k);
	allocator_block_pool_purge_free_only_locked(lwis_dev, &block_mgr->pool_64k);
	allocator_block_pool_purge_free_only_locked(lwis_dev, &block_mgr->pool_128k);
	allocator_block_pool_purge_free_only_locked(lwis_dev, &block_mgr->pool_256k);
	allocator_block_pool_purge_free_only_locked(lwis_dev, &block_mgr->pool_512k);
	allocator_block_pool_purge_free_only_locked(lwis_dev, &block_mgr->pool_large);
}

void lwis_allocator_destroy(struct lwis_device *lwis_dev)
{
	struct lwis_allocator_block_mgr *block_mgr;
	unsigned long flags;

	if (lwis_dev == NULL)
		return;

	mutex_lock(&lwis_dev->client_lock);

	block_mgr = lwis_dev->block_mgr;
	if (block_mgr == NULL) {
		mutex_unlock(&lwis_dev->client_lock);
		return;
	}

	/* Free all pools */
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_4k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_8k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_16k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_32k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_64k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_128k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_256k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_512k);
	allocator_block_pool_free_locked(lwis_dev, &block_mgr->pool_large);

	spin_lock_irqsave(&lwis_dev->allocator_lock, flags);
	kfree(block_mgr);
	lwis_dev->block_mgr = NULL;
	spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);

	mutex_unlock(&lwis_dev->client_lock);
}

void *lwis_allocator_allocate(struct lwis_device *lwis_dev, size_t size, gfp_t gfp_flags)
{
	struct lwis_allocator_block_mgr *block_mgr;
	struct lwis_allocator_block_pool *block_pool;
	struct lwis_allocator_block *block;
	uint32_t idx;
	size_t block_size;
	unsigned long flags;

	if (lwis_dev == NULL || size == 0)
		return NULL;

	/*
	 * fls64() has better performance profile, it's currently used to mimic the
	 * behavior of kmalloc_index().
	 *
	 * kmalloc_index() return value as following:
	 *   if (size <=          8) return 3;
	 *   if (size <=         16) return 4;
	 *   if (size <=         32) return 5;
	 *   if (size <=         64) return 6;
	 *   if (size <=        128) return 7;
	 *   if (size <=        256) return 8;
	 *   if (size <=        512) return 9;
	 *   if (size <=       1024) return 10;
	 *   if (size <=   2 * 1024) return 11;
	 *   if (size <=   4 * 1024) return 12;
	 *   if (size <=   8 * 1024) return 13;
	 *   if (size <=  16 * 1024) return 14;
	 *   if (size <=  32 * 1024) return 15;
	 *   if (size <=  64 * 1024) return 16;
	 *   if (size <= 128 * 1024) return 17;
	 *   if (size <= 256 * 1024) return 18;
	 *   if (size <= 512 * 1024) return 19;
	 *   if (size <= 1024 * 1024) return 20;
	 *   if (size <=  2 * 1024 * 1024) return 21;
	 *   if (size <=  4 * 1024 * 1024) return 22;
	 *   if (size <=  8 * 1024 * 1024) return 23;
	 *   if (size <=  16 * 1024 * 1024) return 24;
	 *   if (size <=  32 * 1024 * 1024) return 25;
	 */
	idx = fls64(size - 1);

	/* Set 4K as the minimal block size */
	if (idx < LWIS_MIN_SLAB_IDX)
		idx = LWIS_MIN_SLAB_IDX;

	spin_lock_irqsave(&lwis_dev->allocator_lock, flags);
	block_mgr = lwis_dev->block_mgr;
	if (block_mgr == NULL) {
		spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);
		dev_err(lwis_dev->dev, "%s: block_mgr is NULL\n", __func__);
		return NULL;
	}

	/*
	 * For the large size memory allocation, we usually use kvmalloc() to allocate
	 * the memory, but kvmalloc() does not take advantage of slab. For this case,
	 * we define several memory pools and recycle to use these memory blocks. For the
	 * size large than 512K, we do not have such use case yet. In current
	 * implementation, I do not cache it due to prevent keeping too much unused
	 * memory on hand.
	 */
	if (idx > LWIS_MAX_SLAB_IDX) {
		spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);

		block = kmalloc(sizeof(struct lwis_allocator_block), gfp_flags);
		if (block == NULL)
			return NULL;

		block->type = idx;
		block->req_size = size;
		block->next = NULL;
		block->prev = NULL;
		block->in_use = true;
		block->ptr = kvmalloc(size, gfp_flags);
		if (block->ptr == NULL) {
			kfree(block);
			return NULL;
		}

		spin_lock_irqsave(&lwis_dev->allocator_lock, flags);
		block_mgr->pool_large.in_use_count++;
		hash_add(block_mgr->allocated_blocks, &block->node, (unsigned long long)block->ptr);
		spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);
		return block->ptr;
	}

	block_pool = allocator_get_block_pool(block_mgr, idx);
	if (block_pool == NULL) {
		spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);
		return NULL;
	}

	/* Try to get free block from recycling block pool */
	block = allocator_free_block_get_locked(block_pool);
	if (block != NULL) {
		block->req_size = size;
		block->in_use = true;
		spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);
		if (gfp_flags & __GFP_ZERO)
			memset(block->ptr, 0, size);
		return block->ptr;
	}

	spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);

	/* Allocate new block */
	block = kmalloc(sizeof(struct lwis_allocator_block), gfp_flags);
	if (block == NULL)
		return NULL;

	block->type = idx;
	block->req_size = size;
	block->next = NULL;
	block->prev = NULL;
	block->in_use = true;
	block_size = 1 << idx;
	block->ptr = kvmalloc(block_size, gfp_flags);
	if (block->ptr == NULL) {
		kfree(block);
		return NULL;
	}

	spin_lock_irqsave(&lwis_dev->allocator_lock, flags);
	block->next = block_pool->in_use;
	if (block->next != NULL)
		block->next->prev = block;

	block_pool->in_use = block;
	block_pool->in_use_count++;
	hash_add(block_mgr->allocated_blocks, &block->node, (unsigned long long)block->ptr);
	spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);

	return block->ptr;
}

void lwis_allocator_free(struct lwis_device *lwis_dev, void *ptr)
{
	struct lwis_allocator_block_mgr *block_mgr;
	struct lwis_allocator_block_pool *block_pool;
	struct lwis_allocator_block *block = NULL;
	struct lwis_allocator_block *blk;
	unsigned long flags;

	if (lwis_dev == NULL || ptr == NULL)
		return;

	spin_lock_irqsave(&lwis_dev->allocator_lock, flags);

	block_mgr = lwis_dev->block_mgr;
	if (block_mgr == NULL) {
		dev_warn(lwis_dev->dev, "%s: block_mgr is NULL, is release called?\n", __func__);
		spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);
		return;
	}
	hash_for_each_possible(block_mgr->allocated_blocks, blk, node, (unsigned long long)ptr) {
		if (blk->ptr == ptr) {
			block = blk;
			break;
		}
	}

	if (block == NULL) {
		dev_err(lwis_dev->dev, "Allocator free ptr not found\n");
		spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);
		return;
	}

	if (!block->in_use) {
		dev_err(lwis_dev->dev, "Allocator double free detected for ptr %p\n", ptr);
		spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);
		return;
	}
	block->in_use = false;

	if (block->type > LWIS_MAX_SLAB_IDX) {
		struct lwis_allocator_block *b;
		struct hlist_node *n;

		hash_for_each_possible_safe(block_mgr->allocated_blocks, b, n, node,
					    (unsigned long long)ptr) {
			if (b->ptr == block->ptr) {
				hash_del(&b->node);
				break;
			}
		}
		kvfree(block->ptr);
		kfree(block);
		block_mgr->pool_large.in_use_count--;
		spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);
		return;
	}

	block_pool = allocator_get_block_pool(block_mgr, block->type);
	if (block_pool == NULL) {
		dev_err(lwis_dev->dev, "block type is invalid\n");
		spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);
		return;
	}

	allocator_free_block_put_locked(block_pool, block);

	spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);
}

void *lwis_allocator_array(struct lwis_device *lwis_dev, size_t n, size_t size, gfp_t gfp_flags)
{
	if (size != 0 && n > SIZE_MAX / size)
		return NULL;

	return lwis_allocator_allocate(lwis_dev, n * size, gfp_flags);
}

void *lwis_allocator_calloc(struct lwis_device *lwis_dev, size_t n, size_t size, gfp_t gfp_flags)
{
	return lwis_allocator_array(lwis_dev, n, size, gfp_flags | __GFP_ZERO);
}

void *lwis_allocator_realloc(struct lwis_device *lwis_dev, void *ptr, size_t new_size,
			     gfp_t gfp_flags)
{
	struct lwis_allocator_block_mgr *block_mgr;
	struct lwis_allocator_block *block = NULL, *blk;
	unsigned long flags;
	size_t old_size = 0;
	int old_type = 0;
	int new_idx;
	bool keep_old = false;
	void *new_ptr;

	if (lwis_dev == NULL)
		return NULL;

	if (new_size == 0) {
		lwis_allocator_free(lwis_dev, ptr);
		return NULL;
	}

	if (ptr == NULL)
		return lwis_allocator_allocate(lwis_dev, new_size, gfp_flags);

	new_idx = fls64(new_size - 1);
	if (new_idx < LWIS_MIN_SLAB_IDX)
		new_idx = LWIS_MIN_SLAB_IDX;

	spin_lock_irqsave(&lwis_dev->allocator_lock, flags);
	block_mgr = lwis_dev->block_mgr;
	if (block_mgr == NULL) {
		dev_err(lwis_dev->dev, "%s: block_mgr is NULL\n", __func__);
		spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);
		return NULL;
	}

	hash_for_each_possible(block_mgr->allocated_blocks, blk, node, (unsigned long long)ptr) {
		if (blk->ptr == ptr) {
			block = blk;
			old_size = block->req_size;
			old_type = block->type;

			if (old_type <= LWIS_MAX_SLAB_IDX && new_idx <= old_type) {
				keep_old = true;
				block->req_size = new_size;
			}
			break;
		}
	}
	spin_unlock_irqrestore(&lwis_dev->allocator_lock, flags);

	if (block == NULL) {
		dev_err(lwis_dev->dev, "%s: ptr not found\n", __func__);
		return NULL;
	}

	if (keep_old) {
		if (new_size > old_size && (gfp_flags & __GFP_ZERO))
			memset((char *)ptr + old_size, 0, new_size - old_size);
		return ptr;
	}

	new_ptr = lwis_allocator_allocate(lwis_dev, new_size, gfp_flags);
	if (new_ptr != NULL) {
		memcpy(new_ptr, ptr, min(old_size, new_size));
		lwis_allocator_free(lwis_dev, ptr);
	}

	return new_ptr;
}

void *lwis_allocator_memdup(struct lwis_device *lwis_dev, const void *src, size_t len,
			    gfp_t gfp_flags)
{
	void *p;

	if (src == NULL)
		return NULL;

	p = lwis_allocator_allocate(lwis_dev, len, gfp_flags);
	if (p != NULL)
		memcpy(p, src, len);

	return p;
}

char *lwis_allocator_strdup(struct lwis_device *lwis_dev, const char *s, gfp_t gfp_flags)
{
	size_t len;
	char *buf;

	if (s == NULL)
		return NULL;

	len = strlen(s) + 1;
	buf = lwis_allocator_allocate(lwis_dev, len, gfp_flags);
	if (buf != NULL)
		memcpy(buf, s, len);

	return buf;
}

