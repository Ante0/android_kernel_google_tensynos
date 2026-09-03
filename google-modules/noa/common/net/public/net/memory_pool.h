/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Memory pool management utility for NEP map tables.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
// NOLINTBEGIN
#ifndef NEP_MEMORY_POOL_H_
#define NEP_MEMORY_POOL_H_

#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>
#else
#include "linux_port/spinlock.h"
#include "linux_port/types.h"
#endif

#define MINIMUM_BLOCK_SIZE sizeof(struct memory_block)

enum {
  NEP_MEMORY_POOL_ERR_NONE = 0,
  NEP_MEMORY_POOL_ERR_INVALID_PARAMETER,
  NEP_MEMORY_POOL_ERR_NO_RESOURCE,
  NEP_MEMORY_POOL_ERR_MEMORY_CORRUPTION,
  NEP_MEMORY_POOL_ERR_MAX
};

/* Memory block */
struct memory_block {
  u32 free_block_magic_word;
  struct memory_block* next;
};

/* Memory pool */
struct memory_pool {
  void* memory_buffer; /* Pointer to the large memory area */
  struct memory_block* head; /* Head of the linked list of memory blocks */
  uint32_t memory_size;      /* Size of the large memory */
  uint32_t block_size;       /* Size of each small block */
  spinlock_t lock;           /* Memory pool lock */
};

int32_t memory_pool_init(struct memory_pool* pool, uint32_t size,
                         uint32_t block_size, u8* mem);
int32_t memory_pool_allocate(struct memory_pool* pool, void** ptr);
void memory_pool_free(struct memory_pool* pool, void* ptr);
void memory_pool_destroy(struct memory_pool* pool);

#endif  // NEP_MEMORY_POOL_H_
// NOLINTEND