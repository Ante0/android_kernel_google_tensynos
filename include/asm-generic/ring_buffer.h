/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic arch dependent ring_buffer macros.
 */
#ifndef __ASM_GENERIC_RING_BUFFER_H__
#define __ASM_GENERIC_RING_BUFFER_H__

#include <linux/cacheflush.h>

/* Flush cache on ring buffer range if needed */
#define arch_ring_buffer_flush_range(start, end)	flush_cache_vmap(start, end)

#endif /* __ASM_GENERIC_RING_BUFFER_H__ */
