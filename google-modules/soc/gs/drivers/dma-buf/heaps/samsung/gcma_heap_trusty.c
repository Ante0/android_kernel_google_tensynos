// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF GCMA Trusty secure backend ops
 *
 * Uses the same arbitrator + buddy allocation as the default non-secure
 * ops, but adds per-buffer protection via trusty_transfer_memory() and
 * trusty_reclaim_memory().
 */

#include "samsung-dma-heap.h"
#include "gcma_heap.h"
#include "gcma_heap_sysfs.h"
#include "gcma_arbitrator.h"

static void *gcma_heap_trusty_buffer_protect(struct samsung_dma_buffer *buffer,
					unsigned int chunk_size,
					unsigned int nr_pages,
					unsigned long paddr)
{
	return samsung_dma_buffer_protect(buffer, chunk_size, nr_pages, paddr);
}

static int gcma_heap_trusty_buffer_unprotect(struct samsung_dma_buffer *buffer)
{
	return samsung_dma_buffer_unprotect(buffer);
}

const struct gcma_heap_ops gcma_heap_trusty_ops = {
	.alloc            = gcma_heap_nonsecure_alloc_pages,
	.free             = gcma_heap_nonsecure_free_pages,
	.buffer_protect   = gcma_heap_trusty_buffer_protect,
	.buffer_unprotect = gcma_heap_trusty_buffer_unprotect,
	.force_empty      = gcma_force_empty,
};
