// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF GCMA heap
 *
 */

#ifndef __GCMA_HEAP_H
#define __GCMA_HEAP_H

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/list.h>

struct device;
struct platform_device;
struct dma_buf;
struct samsung_dma_heap;
struct samsung_dma_buffer;
struct gcma_arbitrator;
struct page;

/* Forward declaration — gcma_heap_ops references gcma_heap */
struct gcma_heap;

/**
 * struct heap_pages - Collection of allocated pages
 * @pages_list: List of pages
 * @count: Number of pages in the list
 *
 * Used to manage allocations that consist of multiple scatter-gather pages.
 */
struct heap_pages {
	struct list_head pages_list;
	unsigned int count;
};

/**
 * struct gcma_heap_ops - GCMA heap operations
 * @heap_init: Initialize the heap backend.
 * @heap_exit: Tear down the heap backend.
 * @alloc: Allocate pages for the heap. Fills @pages with the allocated list.
 *         Returns 0 on success, or a negative error code on failure.
 * @free: Free pages previously allocated via @alloc.
 * @buffer_protect: Optional. Protect a buffer after allocation. Returns an
 *                  opaque pointer to protection metadata, or an ERR_PTR.
 * @buffer_unprotect: Optional. Unprotect a buffer. Returns 0 on success.
 * @get_ffa_tag: Optional. Retrieve the FFA tag associated with the dmabuf.
 * @get_shared_mem_id: Optional. Retrieve the shared memory ID and offset.
 * @force_empty: Optional. Reclaim up to @req_pages from the heap pool.
 *
 * This structure defines the callbacks provided by GCMA heap implementations.
 */
struct gcma_heap_ops {
	int  (*heap_init)(struct gcma_heap *heap, struct platform_device *pdev);
	void (*heap_exit)(struct gcma_heap *heap);

	int (*alloc)(struct samsung_dma_heap *samsung_heap, unsigned long size,
			   struct heap_pages *pages);
	void (*free)(struct samsung_dma_heap *samsung_heap, struct page *page);

	void *(*buffer_protect)(struct samsung_dma_buffer *buffer,
				unsigned int chunk_size,
				unsigned int nr_pages,
				unsigned long paddr);
	int (*buffer_unprotect)(struct samsung_dma_buffer *buffer);

	u64 (*get_ffa_tag)(struct dma_buf *dmabuf);
	int (*get_shared_mem_id)(struct dma_buf *dmabuf, u64 *id, u64 *poff);

	ssize_t (*force_empty)(struct gcma_heap *heap, unsigned int req_pages);
};

/**
 * struct gcma_heap - GCMA heap driver instance
 * @dev: Parent device
 * @arb: Arbitrator instance managing physical memory pool
 * @ops: Heap operations
 * @priv: Private data
 * @flexible_alloc: True if the heap can gracefully fallback to buddy allocator
 * @stat: Optional sysfs statistics block
 *
 * Represents a single instance of a GCMA heap device.
 */
struct gcma_heap {
	struct device *dev;
	struct gcma_arbitrator *arb;
	const struct gcma_heap_ops *ops;
	void *priv;
	bool flexible_alloc;

#ifdef CONFIG_SYSFS
	struct gcma_heap_stat *stat;
#endif
};

/* Helpers */
static inline unsigned long gcma_get_size(struct page *page)
{
	return page_private(page);
}

static inline void gcma_set_size(struct page *page, unsigned long size)
{
	set_page_private(page, size);
}

static inline bool page_is_gcma(struct page *page)
{
	return page_private(page) ? true : false;
}

struct page *gcma_alloc(struct gcma_heap *gcma_heap, unsigned long size);
void gcma_free(struct gcma_heap *gcma_heap, struct page *page);
extern const struct gcma_heap_ops gcma_heap_nonsecure_ops;

int gcma_heap_nonsecure_alloc_pages(struct samsung_dma_heap *samsung_heap, unsigned long size,
			     struct heap_pages *pages);
void gcma_heap_nonsecure_free_pages(struct samsung_dma_heap *samsung_heap, struct page *page);
ssize_t gcma_force_empty(struct gcma_heap *heap, unsigned int req_pages);

extern const struct gcma_heap_ops gcma_heap_trusty_ops;

#endif
