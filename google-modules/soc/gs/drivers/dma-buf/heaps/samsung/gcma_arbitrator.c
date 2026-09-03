// SPDX-License-Identifier: GPL-2.0
/*
 * GCMA Unified Memory Arbitration Layer
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <soc/google/gcma.h>
#include "gcma_heap.h"

#include "gcma_arbitrator.h"
#include "gcma_arbitrator_internal.h"
#include "gcma_arbitrator_sysfs.h"

static LIST_HEAD(gcma_region_list);
static DEFINE_MUTEX(gcma_region_list_lock);

static void gcma_region_release(struct kref *kref)
{
	struct gcma_region *region = container_of(kref, struct gcma_region, refcount);

	/* kref_put_mutex guarantees that gcma_region_list_lock is held here */
	list_del(&region->list);
	mutex_unlock(&gcma_region_list_lock);

	gen_pool_destroy(region->pool);
	of_node_put(region->rmem_np); /* Release the DT node reference */
	kfree(region);
}

static struct gcma_region *gcma_find_region(struct device_node *rmem_np)
{
	struct gcma_region *region;

	list_for_each_entry(region, &gcma_region_list, list) {
		if (region->rmem_np == rmem_np)
			return region;
	}
	return NULL;
}

static struct gcma_region *gcma_get_or_create_region(struct device_node *rmem_np)
{
	struct gcma_region *region;
	struct reserved_mem *rmem;
	int ret;

	mutex_lock(&gcma_region_list_lock);
	region = gcma_find_region(rmem_np);
	if (region) {
		kref_get(&region->refcount);
		mutex_unlock(&gcma_region_list_lock);
		of_node_put(rmem_np);
		return region;
	}

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region) {
		mutex_unlock(&gcma_region_list_lock);
		return ERR_PTR(-ENOMEM);
	}

	region->rmem_np = rmem_np;
	kref_init(&region->refcount);
	spin_lock_init(&region->stat_lock);

	rmem = of_reserved_mem_lookup(rmem_np);
	if (!rmem) {
		ret = -ENODEV;
		goto out_free_region;
	}

	region->name = rmem->name;
	region->base = rmem->base;
	region->size = rmem->size;
	region->pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!region->pool) {
		ret = -ENOMEM;
		goto out_free_region;
	}

	ret = gen_pool_add(region->pool, rmem->base, rmem->size, -1);
	if (ret)
		goto out_destroy_pool;

	ret = gcma_arbitrator_sysfs_init_region(region);
	if (ret)
		goto out_destroy_pool;

	list_add(&region->list, &gcma_region_list);
	register_pixel_gcma_area(rmem->name, rmem->base, rmem->size);

	mutex_unlock(&gcma_region_list_lock);
	return region;

out_destroy_pool:
	gen_pool_destroy(region->pool);
out_free_region:
	kfree(region);
	mutex_unlock(&gcma_region_list_lock);
	of_node_put(rmem_np);
	return ERR_PTR(ret);
}

struct gcma_arbitrator *gcma_arbitrator_create(struct gcma_heap *heap)
{
	struct device *dev = heap->dev;
	struct device_node *np = dev->of_node;
	struct device_node *rmem_np;
	struct gcma_arbitrator *arb;
	struct gcma_region *region;
	int i;
	int ret;

	arb = kzalloc(sizeof(*arb), GFP_KERNEL);
	if (!arb)
		return ERR_PTR(-ENOMEM);
	arb->dev = dev;

	for (i = 0; i < MAX_GCMA_REGIONS; i++) {
		rmem_np = of_parse_phandle(np, "memory-region", i);
		if (!rmem_np) {
			if (i > 0)
				break;
			pr_err("%s: no memory-region found\n", __func__);
			ret = -ENODEV;
			goto err_put_regions;
		}

		region = gcma_get_or_create_region(rmem_np);
		if (IS_ERR(region)) {
			ret = PTR_ERR(region);
			goto err_put_regions;
		}

		arb->regions[i] = region;
		arb->nr_regions++;
	}

	return arb;

err_put_regions:
	for (i = 0; i < arb->nr_regions; i++)
		/*
		 * Use kref_put_mutex to ensure the region is removed from
		 * gcma_region_list atomically when the refcount hits 0,
		 * preventing concurrent lookups from finding a dead region.
		 */
		kref_put_mutex(&arb->regions[i]->refcount, gcma_region_release,
			       &gcma_region_list_lock);
	kfree(arb);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(gcma_arbitrator_create);

void gcma_arbitrator_destroy(struct gcma_arbitrator *arb)
{
	int i;

	for (i = 0; i < arb->nr_regions; i++)
		/*
		 * Use kref_put_mutex to ensure the region is removed from
		 * gcma_region_list atomically when the refcount hits 0,
		 * preventing concurrent lookups from finding a dead region.
		 */
		kref_put_mutex(&arb->regions[i]->refcount, gcma_region_release,
			       &gcma_region_list_lock);
	kfree(arb);
}
EXPORT_SYMBOL_GPL(gcma_arbitrator_destroy);

phys_addr_t gcma_arbitrator_alloc(struct gcma_arbitrator *arb, size_t size)
{
	phys_addr_t addr = 0;
	int i;

	for (i = 0; i < arb->nr_regions; i++) {
		addr = gen_pool_alloc(arb->regions[i]->pool, size);
		if (addr) {
			atomic_long_add(size, &arb->region_allocated_bytes[i]);
			gcma_arbitrator_add_stat(arb->regions[i], size);
			break;
		}
	}

	return addr;
}
EXPORT_SYMBOL_GPL(gcma_arbitrator_alloc);

void gcma_arbitrator_free(struct gcma_arbitrator *arb, phys_addr_t addr, size_t size)
{
	int i;

	for (i = 0; i < arb->nr_regions; i++) {
		struct gcma_region *region = arb->regions[i];

		if (addr >= region->base && addr < region->base + region->size) {
			gen_pool_free(region->pool, addr, size);
			atomic_long_sub(size, &arb->region_allocated_bytes[i]);
			gcma_arbitrator_sub_stat(region, size);
			return;
		}
	}

	pr_warn_ratelimited("gcma_arbitrator_free: addr 0x%llx not found in any region!\n",
			    (unsigned long long)addr);
}
EXPORT_SYMBOL_GPL(gcma_arbitrator_free);

ssize_t gcma_arbitrator_show_allocation(struct gcma_arbitrator *arb, char *buf, size_t max_len)
{
	ssize_t count = 0;
	int i;

	for (i = 0; i < arb->nr_regions; i++) {
		unsigned long usage = atomic_long_read(&arb->region_allocated_bytes[i]);

		count += scnprintf(buf + count, max_len - count,
				   "%s: %lu KB\n", arb->regions[i]->name,
				   usage / 1024);
	}
	return count;
}
EXPORT_SYMBOL_GPL(gcma_arbitrator_show_allocation);
