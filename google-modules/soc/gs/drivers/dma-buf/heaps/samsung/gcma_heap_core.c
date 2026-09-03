// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF GCMA heap
 *
 */

#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/kernel.h>
#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include <linux/pfn.h>
#include <soc/google/gcma.h>

#include "samsung-dma-heap.h"
#include "gcma_heap.h"
#include "gcma_heap_sysfs.h"
#include "gcma_arbitrator.h"

#define BASE_GFP (GFP_HIGHUSER | __GFP_ZERO | __GFP_COMP)
#define LIGHT_EFFORT_GFP  ((BASE_GFP | __GFP_NOWARN | __GFP_NORETRY) & ~__GFP_RECLAIM)
#define HARD_EFFORT_GFP BASE_GFP
/*
 * The selection of the orders used for allocation (2MB, 1MB, 64K, 4K) is designed
 * to match with the sizes often found in IOMMUs. Using high order pages instead
 * of order 0 pages can significantly improve the performance of many IOMMUs
 * by reducing TLB pressure and time spent updating page tables.
 *
 * Note: When the order is 0, the minimum allocation is PAGE_SIZE. The possible
 * page sizes for ARM devices could be 4K, 16K and 64K.
 */
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

static int param_set_min_gcma_dmabuf_kb(const char *val, const struct kernel_param *kp)
{
	unsigned long threshold_kb;
	int ret;

	ret = kstrtoul(val, 0, &threshold_kb);
	if (ret)
		return ret;

	/* Validate input: limit threshold to 128MB (128 * 1024 KB) to prevent unreasonable settings. */
	if (threshold_kb > 128 * SZ_1K)
		return -EINVAL;

	*(unsigned long *)kp->arg = threshold_kb * SZ_1K;
	return 0;
}

static int param_get_min_gcma_dmabuf_kb(char *buffer, const struct kernel_param *kp)
{
	unsigned long bytes = *(unsigned long *)kp->arg;

	return sysfs_emit(buffer, "%lu\n", bytes / SZ_1K);
}

static const struct kernel_param_ops min_gcma_dmabuf_kb_ops = {
	.set = param_set_min_gcma_dmabuf_kb,
	.get = param_get_min_gcma_dmabuf_kb,
};

static unsigned long min_gcma_dmabuf_bytes = 512 * SZ_1K;
module_param_cb(min_gcma_dmabuf_kb, &min_gcma_dmabuf_kb_ops, &min_gcma_dmabuf_bytes, 0644);
MODULE_PARM_DESC(min_gcma_dmabuf_kb, "Minimum size in KB to attempt GCMA allocation");

#define MAX_SKIP_HEAPS 5
#define MAX_HEAP_NAME_LEN 32
static char skip_heaps[MAX_SKIP_HEAPS][MAX_HEAP_NAME_LEN];
static int num_skip_heaps;

static int param_set_skip_heaps(const char *val, const struct kernel_param *kp)
{
	char *str, *orig_str;
	char *p;
	int i = 0;

	if (!val)
		return -EINVAL;

	orig_str = kstrdup(val, GFP_KERNEL);
	if (!orig_str)
		return -ENOMEM;

	str = orig_str;
	num_skip_heaps = 0;

	while ((p = strsep(&str, ",")) != NULL && i < MAX_SKIP_HEAPS) {
		if (*p == '\0')
			continue;
		strscpy(skip_heaps[i], p, sizeof(skip_heaps[i]));
		i++;
	}
	num_skip_heaps = i;

	kfree(orig_str);
	return 0;
}

static int param_get_skip_heaps(char *buffer, const struct kernel_param *kp)
{
	char local_buf[256] = "";
	int i;

	for (i = 0; i < num_skip_heaps; i++) {
		strcat(local_buf, skip_heaps[i]);
		if (i < num_skip_heaps - 1)
			strcat(local_buf, ",");
	}

	return sysfs_emit(buffer, "%s\n", local_buf);
}

static const struct kernel_param_ops gcma_skip_heaps_ops = {
	.set = param_set_skip_heaps,
	.get = param_get_skip_heaps,
};

module_param_cb(gcma_skip_heaps, &gcma_skip_heaps_ops, NULL, 0444);
MODULE_PARM_DESC(gcma_skip_heaps, "Comma-separated list of heap names to skip probing (max 5)");

unsigned long dma_heap_gcma_inuse_pages(void)
{
	return atomic64_read(&inuse_pages);
}

struct page *gcma_alloc(struct gcma_heap *gcma_heap, unsigned long size)
{
	phys_addr_t paddr;
	unsigned long pfn;
	struct page *page = NULL;

	paddr = gcma_arbitrator_alloc(gcma_heap->arb, size);
	if (!paddr)
		return NULL;

	pfn = PFN_DOWN(paddr);
	page = phys_to_page(paddr);
	pixel_gcma_alloc_range(pfn, pfn + (size >> PAGE_SHIFT) - 1);
	gcma_set_size(page, size);
	inc_gcma_heap_stat(gcma_heap, USAGE, size);
	/*
	 * zero out pages to align with the strategy in buddy allocator GFP flag
	 */
	if (BASE_GFP & __GFP_ZERO)
		heap_page_clean(page, size);

	return page;
}

void gcma_free(struct gcma_heap *gcma_heap, struct page *page)
{
	unsigned long size, pfn;

	size = gcma_get_size(page);
	pfn = page_to_pfn(page);
	pixel_gcma_free_range(pfn, pfn + (size >> PAGE_SHIFT) - 1);
	gcma_arbitrator_free(gcma_heap->arb, page_to_phys(page), size);
}

static struct dma_buf *gcma_heap_allocate(struct dma_heap *heap, unsigned long len,
					    u32 fd_flags, u64 heap_flags)
{
	struct samsung_dma_heap *samsung_dma_heap = dma_heap_get_drvdata(heap);
	struct gcma_heap *gcma_heap = samsung_dma_heap->priv;
	const struct gcma_heap_ops *ops = gcma_heap->ops;
	struct samsung_dma_buffer *buffer;
	struct scatterlist *sg;
	struct dma_buf *dmabuf;
	unsigned int alignment = samsung_dma_heap->alignment;
	struct page *page, *tmp_page;
	struct heap_pages heap_pages;
	int ret = -ENOMEM;

	if (dma_heap_flags_video_aligned(samsung_dma_heap->flags))
		len = dma_heap_add_video_padding(len);

	if (len / PAGE_SIZE > totalram_pages() / 2) {
		pr_err("pid %d requested too large allocation of size %lu from %s heap\n",
		       current->pid, len, samsung_dma_heap->name);
		return ERR_PTR(ret);
	}

	INIT_LIST_HEAD(&heap_pages.pages_list);
	len = ALIGN(len, alignment);

	ret = ops->alloc(samsung_dma_heap, len, &heap_pages);
	if (ret)
		goto out;

	buffer = samsung_dma_buffer_alloc(samsung_dma_heap, len, heap_pages.count);
	if (IS_ERR(buffer)) {
		ret = PTR_ERR(buffer);
		goto free_buffer;
	}

	sg = buffer->sg_table.sgl;
	list_for_each_entry_safe(page, tmp_page, &heap_pages.pages_list, lru) {
		sg_set_page(sg, page,
			   page_is_gcma(page) ? gcma_get_size(page) : page_size(page), 0);
		sg = sg_next(sg);
		list_del(&page->lru);
	}

	heap_cache_flush(buffer);

	if (ops->buffer_protect) {
		unsigned long paddr = page_to_phys(sg_page(buffer->sg_table.sgl));

		buffer->priv = ops->buffer_protect(buffer, len, heap_pages.count, paddr);
		if (IS_ERR(buffer->priv)) {
			ret = PTR_ERR(buffer->priv);
			buffer->priv = NULL;
			goto free_export;
		}
	}

	dmabuf = samsung_export_dmabuf(buffer, fd_flags);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_export;
	}

	return dmabuf;

free_export:
	if (ops->buffer_unprotect ? !ops->buffer_unprotect(buffer) : 1)
		for_each_sgtable_sg(&buffer->sg_table, sg, heap_pages.count)
			ops->free(samsung_dma_heap, sg_page(sg));
free_buffer:
	list_for_each_entry_safe(page, tmp_page, &heap_pages.pages_list, lru) {
		list_del(&page->lru);
		ops->free(samsung_dma_heap, page);
	}

	samsung_dma_buffer_free(buffer);
out:
	pr_err("failed to allocate from %s heap, size %lu ret %d",
		      samsung_dma_heap->name, len, ret);

	return ERR_PTR(ret);
}

static void gcma_heap_release(struct samsung_dma_buffer *buffer)
{
	struct samsung_dma_heap *samsung_dma_heap = buffer->heap;
	struct gcma_heap *gcma_heap = samsung_dma_heap->priv;
	const struct gcma_heap_ops *ops = gcma_heap->ops;
	int ret = 0;

	if (ops->buffer_unprotect)
		ret = ops->buffer_unprotect(buffer);

	if (!ret) {
		struct sg_table *table;
		struct scatterlist *sg;
		int i;

		table = &buffer->sg_table;
		for_each_sgtable_sg(table, sg, i)
			ops->free(samsung_dma_heap, sg_page(sg));
	}
	samsung_dma_buffer_free(buffer);
}

static const struct dma_heap_ops gcma_heap_ops = {
	.allocate = gcma_heap_allocate,
};

static void gcma_arbitrator_destroy_action(void *data)
{
	gcma_arbitrator_destroy(data);
}

static int gcma_heap_probe(struct platform_device *pdev)
{
	struct gcma_heap *gcma_heap;
	const char *heap_name;
	bool is_secure_heap;
	int ret;
	int i;

	if (of_property_read_string(pdev->dev.of_node, "dma-heap,name", &heap_name))
		heap_name = pdev->name;

	for (i = 0; i < num_skip_heaps; i++) {
		if (strcmp(heap_name, skip_heaps[i]) == 0 ||
		    strcmp(pdev->name, skip_heaps[i]) == 0) {
			dev_info(&pdev->dev, "Skipping heap %s as requested\n", heap_name);
			return 0;
		}
	}

	gcma_heap = devm_kzalloc(&pdev->dev, sizeof(*gcma_heap), GFP_KERNEL);
	if (!gcma_heap)
		return -ENOMEM;

	gcma_heap->dev = &pdev->dev;
	gcma_heap->arb = gcma_arbitrator_create(gcma_heap);
	if (IS_ERR(gcma_heap->arb)) {
		perrdev(&pdev->dev, "Failed to create GCMA arbitrator\n");
		return PTR_ERR(gcma_heap->arb);
	}

	ret = devm_add_action_or_reset(&pdev->dev, gcma_arbitrator_destroy_action, gcma_heap->arb);
	if (ret)
		return ret;

	gcma_heap->flexible_alloc =
		of_property_read_bool(pdev->dev.of_node, "dma-heap-gcam,fleixble-alloc");

	is_secure_heap = of_property_read_bool(pdev->dev.of_node, "dma-heap,secure") ||
			 of_property_read_bool(pdev->dev.of_node, "dma-heap,dynamic-secure");

	if (is_secure_heap)
		gcma_heap->ops = &gcma_heap_trusty_ops;
	else
		gcma_heap->ops = &gcma_heap_nonsecure_ops;

	/* Secure + flexible is not allowed */
	if (is_secure_heap && gcma_heap->flexible_alloc) {
		perrfn("Don't support a secure heap with a flexible_alloc strategy");
		return -EPERM;
	}

	ret = samsung_heap_add(&pdev->dev, gcma_heap, gcma_heap_release,
			       &gcma_heap_ops);
	if (ret == -ENODEV)
		return 0;

	register_heap_sysfs(gcma_heap, pdev->name);

	return ret;
}

static const struct of_device_id gcma_heap_of_match[] = {
	{ .compatible = "google,dma-heap-gcma", },
	{ },
};
MODULE_DEVICE_TABLE(of, gcma_heap_of_match);

static struct platform_driver gcma_heap_driver = {
	.driver		= {
		.name	= "google,dma-heap-gcma",
		.of_match_table = gcma_heap_of_match,
	},
	.probe		= gcma_heap_probe,
};

int __init gcma_dma_heap_init(void)
{
	gcma_heap_sysfs_init();
	return platform_driver_register(&gcma_heap_driver);
}

void gcma_dma_heap_exit(void)
{
	platform_driver_unregister(&gcma_heap_driver);
}
