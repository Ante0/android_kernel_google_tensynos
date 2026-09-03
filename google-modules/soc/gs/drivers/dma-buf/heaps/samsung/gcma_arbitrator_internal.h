/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __GCMA_ARBITRATOR_INTERNAL_H
#define __GCMA_ARBITRATOR_INTERNAL_H

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/kref.h>
#include <linux/list.h>

#include "gcma_arbitrator.h"

struct device_node;
struct gen_pool;

#include <linux/spinlock.h>

struct gcma_region_stat;

/* Shared object representing a reserved memory region */
struct gcma_region {
	struct list_head list;
	struct device_node *rmem_np;	/* Key for lookup, also needed for put */
	const char *name;
	phys_addr_t base;
	size_t size;
	struct gen_pool *pool;
	struct kref refcount;
	spinlock_t stat_lock;
	struct gcma_region_stat *stat;
};

#define MAX_GCMA_REGIONS 4

/* Per-client (per-heap) handle */
struct gcma_arbitrator {
	struct device *dev;
	struct gcma_region *regions[MAX_GCMA_REGIONS];
	atomic_long_t region_allocated_bytes[MAX_GCMA_REGIONS];
	int nr_regions;
};

#endif /* __GCMA_ARBITRATOR_INTERNAL_H */
