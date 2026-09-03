// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF GCMA default (non-secure) ops
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <soc/google/gcma.h>

#include "samsung-dma-heap.h"
#include "gcma_heap.h"
#include "gcma_heap_sysfs.h"
#include "gcma_arbitrator.h"

#define BASE_GFP (GFP_HIGHUSER | __GFP_ZERO | __GFP_COMP)
#define LIGHT_EFFORT_GFP  ((BASE_GFP | __GFP_NOWARN | __GFP_NORETRY) & ~__GFP_RECLAIM)
#define HARD_EFFORT_GFP BASE_GFP

#define ORDER_2M (21 - PAGE_SHIFT)
#define ORDER_1M (20 - PAGE_SHIFT)
#define ORDER_64K (16 - PAGE_SHIFT)
#define ORDER_FOR_PAGE_SIZE (0)

static const unsigned int buddy_pages_orders[] = {
	ORDER_2M, ORDER_1M, ORDER_64K, ORDER_FOR_PAGE_SIZE};
static const gfp_t buddy_pages_flags[] = {
	LIGHT_EFFORT_GFP, LIGHT_EFFORT_GFP, LIGHT_EFFORT_GFP, HARD_EFFORT_GFP};
static const unsigned int gcma_pages_orders[] = {
	ORDER_2M, ORDER_1M, ORDER_64K};

static unsigned long min_gcma_dmabuf_bytes = 512 * SZ_1K;

static void free_gcma_heap_page(struct gcma_heap *gcma_heap, struct page *page)
{
	if (unlikely(!page))
		return;

	if (page_is_gcma(page)) {
		gcma_free(gcma_heap, page);
		dec_gcma_heap_stat(gcma_heap, USAGE, gcma_get_size(page));
	} else {
		unsigned int order = compound_order(page);

		__free_pages(page, order);
		dma_heap_dec_inuse(1 << order);
		dec_gcma_heap_stat(gcma_heap, BUDDY, PAGE_SIZE << order);
	}
}

/*
 * 1. Try GCMA allocation
 * 2. Try Buddy allocator (light effort for high orders, hard for order-0)
 */
static struct page *alloc_largest_available(struct gcma_heap *gcma_heap,
					    unsigned long size,
					    unsigned int max_order)
{
	struct page *page = NULL;
	int i;

	if (size >= min_gcma_dmabuf_bytes) {
		for (i = 0; i < ARRAY_SIZE(gcma_pages_orders); i++) {
			unsigned long gcma_size = PAGE_SIZE << gcma_pages_orders[i];

			if (size < gcma_size)
				continue;

			page = gcma_alloc(gcma_heap, gcma_size);
			if (page)
				goto out;
		}
	}

	for (i = 0; i < ARRAY_SIZE(buddy_pages_orders); i++) {
		unsigned long buddy_size = PAGE_SIZE << buddy_pages_orders[i];
		gfp_t flags = buddy_pages_flags[i];

		if (size < buddy_size && i != ARRAY_SIZE(buddy_pages_orders) - 1)
			continue;
		if (max_order < buddy_pages_orders[i])
			continue;

		if (flags == HARD_EFFORT_GFP)
			inc_gcma_heap_stat(gcma_heap, ALLOCSTALL,
					   PAGE_SIZE << buddy_pages_orders[i]);

		page = alloc_pages(flags, buddy_pages_orders[i]);
		if (page) {
			inc_gcma_heap_stat(gcma_heap, BUDDY,
					   PAGE_SIZE << buddy_pages_orders[i]);
			goto out;
		}
	}
out:
	if (page && !page_is_gcma(page))
		dma_heap_inc_inuse(1 << compound_order(page));
	return page;
}

static int allocate_flexible_pages(struct gcma_heap *gcma_heap, unsigned long len,
				   struct heap_pages *heap_pages)
{
	struct page *page, *tmp_page;
	unsigned long size_remaining = len;
	unsigned int max_order = buddy_pages_orders[0];
	unsigned int count = 0;
	int ret = 0;

	while (size_remaining > 0) {
		unsigned long allocated_size;

		if (fatal_signal_pending(current)) {
			pr_err("Fatal signal pending pid #%d", current->pid);
			ret = -EINTR;
			goto free_flexible_pages;
		}

		page = alloc_largest_available(gcma_heap, size_remaining, max_order);
		if (!page) {
			ret = -ENOMEM;
			goto free_flexible_pages;
		}

		list_add_tail(&page->lru, &heap_pages->pages_list);
		allocated_size = page_is_gcma(page) ? gcma_get_size(page) : page_size(page);
		if (allocated_size > size_remaining)
			size_remaining = 0;
		else
			size_remaining -= allocated_size;
		if (!page_is_gcma(page))
			max_order = compound_order(page);
		count++;
	}
	heap_pages->count = count;
	goto out_flexible_alloc;

free_flexible_pages:
	list_for_each_entry_safe(page, tmp_page, &heap_pages->pages_list, lru) {
		list_del(&page->lru);
		free_gcma_heap_page(gcma_heap, page);
	}
out_flexible_alloc:
	return ret;
}

static int allocate_fixed_pages(struct gcma_heap *gcma_heap, unsigned long len,
			       struct heap_pages *heap_pages)
{
	struct page *page = gcma_alloc(gcma_heap, len);

	if (page) {
		list_add_tail(&page->lru, &heap_pages->pages_list);
		heap_pages->count = 1;
		return 0;
	}
	return -ENOMEM;
}

int gcma_heap_nonsecure_alloc_pages(struct samsung_dma_heap *samsung_heap, unsigned long size,
				    struct heap_pages *pages)
{
	struct gcma_heap *heap = samsung_heap->priv;

	if (heap->flexible_alloc)
		return allocate_flexible_pages(heap, size, pages);
	else
		return allocate_fixed_pages(heap, size, pages);
}

void gcma_heap_nonsecure_free_pages(struct samsung_dma_heap *samsung_heap, struct page *page)
{
	struct gcma_heap *heap = samsung_heap->priv;

	free_gcma_heap_page(heap, page);
}

ssize_t gcma_force_empty(struct gcma_heap *heap, unsigned int req_pages)
{
	struct page *page;
	unsigned long req_size;

	req_size = req_pages * PAGE_SIZE;
	page = gcma_alloc(heap, req_size);
	pr_info("req_pages %d force_empty %s\n", req_pages,
		page ? "succeeded" : "failed");
	if (page)
		gcma_free(heap, page);
	return page ? req_size : -ENOMEM;
}

const struct gcma_heap_ops gcma_heap_nonsecure_ops = {
	.alloc        = gcma_heap_nonsecure_alloc_pages,
	.free         = gcma_heap_nonsecure_free_pages,
	.force_empty  = gcma_force_empty,
};
