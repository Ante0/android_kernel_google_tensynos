/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Memory buffer management for NOA driver
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Les Lee <lesl@google.com>
 */
#ifndef __NOA_BUFFER_MANAGER_H_
#define __NOA_BUFFER_MANAGER_H_
#ifndef linux
#include "linux_port/memory-alloc.h"
#endif
#define __GFB_COHERENCE (0x0u)
#define __GFB_NON_COHERENCE (0x01u)
#define __GFB_SRAM (0x02u)
#define __GFB_DRAM (0x04u)

/**
 * GFB (Get free buffer) bitmasks..
 */
#define GFB_DRAM_COHERENCE (__GFB_DRAM | __GFB_COHERENCE)
#define GFB_SRAM_COHERENCE (__GFB_SRAM | __GFB_COHERENCE)
#define GFB_DRAM_NON_COHERENCE (__GFB_DRAM | __GFB_NON_COHERENCE)
#define GFB_SRAM_NON_COHERENCE (__GFB_SRAM | __GFB_NON_COHERENCE)
#ifdef linux
typedef unsigned int __bitwise gfb_t;
#else
typedef unsigned int gfb_t;
#endif

static inline void *nmalloc(size_t size, gfb_t flags)
{
  if (GFB_DRAM_COHERENCE) {
    return kzalloc(size, GFP_KERNEL);
  }
}
#endif /* __NOA_BUFFER_MANAGER_H_ */
