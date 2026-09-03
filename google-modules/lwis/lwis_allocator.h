/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LWIS Recycling Memory Allocator
 *
 * Copyright (c) 2021 Google Inc.
 */

#ifndef LWIS_ALLOCATOR_H_
#define LWIS_ALLOCATOR_H_

#include <linux/mutex.h>
#include "lwis_commands.h"
#include "lwis_device.h"

struct lwis_allocator_block {
	int type;
	void *ptr;
	struct lwis_allocator_block *next;
	struct lwis_allocator_block *prev;
	struct hlist_node node;
	size_t req_size;
	bool in_use;
};

struct lwis_allocator_block_pool {
	char name[LWIS_MAX_NAME_STRING_LEN];
	struct lwis_allocator_block *free;
	uint32_t free_count;
	struct lwis_allocator_block *in_use;
	uint32_t in_use_count;
};

struct lwis_allocator_block_mgr {
	struct lwis_allocator_block_pool pool_4k;
	struct lwis_allocator_block_pool pool_8k;
	struct lwis_allocator_block_pool pool_16k;
	struct lwis_allocator_block_pool pool_32k;
	struct lwis_allocator_block_pool pool_64k;
	struct lwis_allocator_block_pool pool_128k;
	struct lwis_allocator_block_pool pool_256k;
	struct lwis_allocator_block_pool pool_512k;
	struct lwis_allocator_block_pool pool_large;
	/* Hash table of allocated buffers keyed by allocated addresses */
	DECLARE_HASHTABLE(allocated_blocks, BUFFER_HASH_BITS);
	int ref_count;
};

/*
 *  lwis_allocator_init: Initialize the recycling memory allocator
 */
int lwis_allocator_init(struct lwis_device *lwis_dev);

/*
 *  lwis_allocator_release: Release the recycling memory allocator
 *  and its resources
 */
void lwis_allocator_release(struct lwis_device *lwis_dev);

/*
 *  lwis_allocator_purge_pools_locked: Forcefully purge all empty cached blocks in all pools
 */
void lwis_allocator_purge_pools_locked(struct lwis_device *lwis_dev);

/*
 *  lwis_allocator_destroy: Destroy the recycling memory allocator
 *  and its resources
 */
void lwis_allocator_destroy(struct lwis_device *lwis_dev);

/*
 *  lwis_allocator_allocate: Allocate a block from the recycling memory allocator
 */
void *lwis_allocator_allocate(struct lwis_device *lwis_dev, size_t size, gfp_t gfp_flags);

static inline void *lwis_allocator_zallocate(struct lwis_device *lwis_dev, size_t size,
					     gfp_t gfp_flags)
{
	return lwis_allocator_allocate(lwis_dev, size, gfp_flags | __GFP_ZERO);
}

/*
 *  lwis_allocator_calloc: Allocate an array from the recycling memory allocator
 */
void *lwis_allocator_calloc(struct lwis_device *lwis_dev, size_t n, size_t size, gfp_t gfp_flags);

/*
 *  lwis_allocator_array: Allocate an array from the recycling memory allocator without zeroing
 */
void *lwis_allocator_array(struct lwis_device *lwis_dev, size_t n, size_t size, gfp_t gfp_flags);

/*
 *  lwis_allocator_realloc: Reallocate a block from the recycling memory allocator
 */
void *lwis_allocator_realloc(struct lwis_device *lwis_dev, void *ptr, size_t new_size,
			     gfp_t gfp_flags);

/*
 *  lwis_allocator_memdup: Duplicate a memory region using the recycling memory allocator
 */
void *lwis_allocator_memdup(struct lwis_device *lwis_dev, const void *src, size_t len,
			    gfp_t gfp_flags);

/*
 *  lwis_allocator_strdup: Duplicate a string using the recycling memory allocator
 */
char *lwis_allocator_strdup(struct lwis_device *lwis_dev, const char *s, gfp_t gfp_flags);

/*
 *  lwis_allocator_free: Free a block to the recycling memory allocator
 */
void lwis_allocator_free(struct lwis_device *lwis_dev, void *ptr);

#endif /* LWIS_ALLOCATOR_H_ */
