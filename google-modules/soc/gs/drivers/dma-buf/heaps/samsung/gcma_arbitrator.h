/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __GCMA_ARBITRATOR_H
#define __GCMA_ARBITRATOR_H

#include <linux/types.h>

struct device;
struct gcma_arbitrator;
struct gcma_heap;

/**
 * gcma_arbitrator_create() - Create an arbitrator for a heap
 * @heap: The GCMA heap that owns this arbitrator.
 *
 * Returns a pointer to the new gcma_arbitrator handle on success, or an ERR_PTR on failure.
 * Automatically discovers and attaches associated memory regions.
 */
struct gcma_arbitrator *gcma_arbitrator_create(struct gcma_heap *heap);

/**
 * gcma_arbitrator_destroy() - Destroy the arbitrator handle
 * @arb: The handle to destroy.
 */
void gcma_arbitrator_destroy(struct gcma_arbitrator *arb);

/**
 * gcma_arbitrator_alloc() - Allocate memory through the arbitrator
 * @arb: The arbitrator handle.
 * @size: Size of the allocation in bytes.
 *
 * Returns the physical address of the allocated block, or 0 on failure.
 */
phys_addr_t gcma_arbitrator_alloc(struct gcma_arbitrator *arb, size_t size);

/**
 * gcma_arbitrator_free() - Free memory back through the arbitrator
 * @arb: The arbitrator handle.
 * @addr: Physical address of the block to free.
 * @size: Size of the block.
 */
void gcma_arbitrator_free(struct gcma_arbitrator *arb, phys_addr_t addr, size_t size);
ssize_t gcma_arbitrator_show_allocation(struct gcma_arbitrator *arb, char *buf, size_t max_len);

#endif /* __GCMA_ARBITRATOR_H */
