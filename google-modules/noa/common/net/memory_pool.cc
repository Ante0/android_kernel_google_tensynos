// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Memory pool management utility for NEP map tables.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifdef linux
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include "util/memory_pool.h"
#else
#include "linux_port/memory-alloc.h"
#include "net/memory_pool.h"
#endif

#define FREE_BLOCK_MAGIC_WORD (u32)0x56781234

/* Initialize the memory pool */
int32_t memory_pool_init(struct memory_pool* pool, uint32_t mem_size,
                         uint32_t block_size, u8* p_mem) {
  u8* ptr;
  struct memory_block* cur;
  struct memory_block* prev;

  if (pool == NULL || mem_size < MINIMUM_BLOCK_SIZE ||
      block_size < MINIMUM_BLOCK_SIZE || mem_size % block_size != 0) {
    return NEP_MEMORY_POOL_ERR_INVALID_PARAMETER;
  }

#ifdef linux
  p_mem = (u8*)kzalloc(mem_size, GFP_KERNEL);
  if (p_mem == NULL) {
    return NEP_MEMORY_POOL_ERR_NO_RESOURCE;
  }

#endif /* linux */
  pool->memory_buffer = p_mem;
  pool->memory_size = mem_size;
  pool->block_size = block_size;
  spin_lock_init(&pool->lock);

  /* Add all blocks into the free list */
  prev = NULL;
  for (ptr = p_mem; ptr + block_size <= p_mem + mem_size; ptr += block_size) {
    cur = (struct memory_block*)ptr;
    cur->free_block_magic_word = FREE_BLOCK_MAGIC_WORD;
    cur->next = prev;
    prev = cur;
  }
  pool->head = prev;

  return NEP_MEMORY_POOL_ERR_NONE;
}

/* Allocate a block from memory pool */
int32_t memory_pool_allocate(struct memory_pool* pool, void** ptr) {
  struct memory_block* cur;

  *ptr = NULL;
  spin_lock(&pool->lock);
  cur = pool->head;
  if (cur == NULL) {
    spin_unlock(&pool->lock);
    return NEP_MEMORY_POOL_ERR_NO_RESOURCE;
  }
  if (cur->free_block_magic_word != FREE_BLOCK_MAGIC_WORD) {
    spin_unlock(&pool->lock);
    return NEP_MEMORY_POOL_ERR_MEMORY_CORRUPTION;
  }

  cur->free_block_magic_word = ~FREE_BLOCK_MAGIC_WORD;
  pool->head = cur->next;
  spin_unlock(&pool->lock);
  *ptr = (void*)cur;
  return NEP_MEMORY_POOL_ERR_NONE;
}

/* Free a block of memory */
void memory_pool_free(struct memory_pool* pool, void* ptr) {
  struct memory_block* cur = (struct memory_block*)ptr;
#ifdef linux
  void *ptr_size = ptr;
  void *memory_buffer = pool->memory_buffer;
#else
  u32 ptr_size = (u32)ptr;
  u32 memory_buffer = (u32)pool->memory_buffer;
#endif

  spin_lock(&pool->lock);
  if (ptr == NULL || ptr < pool->memory_buffer ||
      ptr_size + pool->block_size >
          memory_buffer + pool->memory_size ||
      (ptr_size - memory_buffer) % pool->block_size != 0) {
    spin_unlock(&pool->lock);
    return;
  }

  cur->free_block_magic_word = FREE_BLOCK_MAGIC_WORD;
  cur->next = pool->head;
  pool->head = cur;
  spin_unlock(&pool->lock);
}

/* Destroy the memory pool */
void memory_pool_destroy(struct memory_pool* pool) {
  spin_lock(&pool->lock);
#ifdef linux
  kfree(pool->memory_buffer);

#endif
  pool->memory_buffer = NULL;
  pool->head = NULL;
  spin_unlock(&pool->lock);
}

