/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef __GOOGLE_DPA_DMA_H__
#define __GOOGLE_DPA_DMA_H__

#include <linux/types.h>

#include "google_dpa_internal.h"

/*
 * Allocates a contiguous, cache-coherent DMA memory region of the requested `size`
 * using dma_alloc_coherent(). The allocated memory is then mapped to the
 * specified IOMMU virtual address (IOVA) range using iommu_map_sgtable().
 *
 * @dpa: Pointer to the google_dpa device instance.
 * @size: The size of the memory region to allocate, in bytes.
 * @da: Pointer to store the DMA address of the allocated memory.
 * @gfp: GFP flags for memory allocation.
 * @iova: The target IOMMU virtual address for mapping.
 * @prot: Protection flags for the IOMMU mapping.
 *
 * Returns: A kernel virtual address (KVA) pointer to the allocated memory on success,
 * or NULL on failure.
 */
void *google_dpa_dma_alloc_coherent_link_range(struct google_dpa *dpa, size_t size, dma_addr_t *da,
					       gfp_t gfp, phys_addr_t iova, int prot);

/*
 * Unmaps a previously allocated coherent DMA memory region from its IOMMU virtual
 * address (IOVA) range using iommu_unmap_sgtable(). After unmapping, the memory
 * is freed using dma_free_coherent().
 *
 * @dpa: Pointer to the google_dpa device instance.
 * @size: The size of the memory region to free, in bytes.
 * @cpu_addr: The kernel virtual address (KVA) of the allocated memory.
 * @dma_handle: The DMA address of the allocated memory.
 * @iova: The target IOMMU virtual address from which the memory was mapped.
 */
void google_dpa_dma_free_coherent_unlink_range(struct google_dpa *dpa, size_t size, void *cpu_addr,
					       dma_addr_t dma_handle, phys_addr_t iova);

#endif // __GOOGLE_DPA_DMA_H__
