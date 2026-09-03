// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>

#include "google_dpa_dma.h"

/*
 * Obtains the scatter-gather list (sgtable) for the provided DMA-coherent memory
 * region using dma_get_sgtable(). It then maps this scatter-gather list to the
 * specified target IOMMU virtual address (IOVA).
 *
 * @src_dev: The source device associated with the DMA memory.
 * @va: The kernel virtual address of the memory.
 * @da: The DMA address of the memory.
 * @size: The size of the memory region to link, in bytes.
 * @dest_dev: The destination device (IOMMU context) for the mapping.
 * @iova: The target IOMMU virtual address for the mapping.
 * @prot: Protection flags for the IOMMU mapping.
 *
 * Returns: The number of bytes successfully mapped, or a negative errno on failure.
 */
static ssize_t google_dpa_dma_link_range(struct device *src_dev, void *va, dma_addr_t da,
					 size_t size, struct device *dest_dev, phys_addr_t iova,
					 int prot)
{
	ssize_t mapped = 0;
	struct sg_table sgt;
	if (dma_get_sgtable(src_dev, &sgt, va, da, size) < 0) {
		dev_err(src_dev, "Cannot get sg table for 0x%llx\n", iova);
		return -ENOMEM;
	}

	mapped = iommu_map_sgtable(iommu_get_domain_for_dev(dest_dev), iova, &sgt, prot);
	sg_free_table(&sgt);

	if (mapped < 0) {
		dev_err(dest_dev, "Cannot map DMA region for 0x%llx\n", iova);
		return -ENOMEM;
	}
	return mapped;
}

/*
 * Unmaps a memory region that was previously linked to the IOMMU address space.
 * This function removes the mapping for a specified `size` of memory starting
 * at the given `iova` in the `dest_dev`'s IOMMU context.
 *
 * @dest_dev: The device (IOMMU context) from which the memory will be unmapped.
 * @iova: The starting IOMMU virtual address of the memory range to unlink.
 * @size: The size of the memory region to unlink, in bytes.
 *
 * Returns: The number of bytes successfully unmapped, or a negative errno on failure.
 */
static size_t google_dpa_dma_unlink_range(struct device *dest_dev, phys_addr_t iova, size_t size)
{
	return iommu_unmap(iommu_get_domain_for_dev(dest_dev), iova, size);
}

void *google_dpa_dma_alloc_coherent_link_range(struct google_dpa *dpa, size_t size, dma_addr_t *da,
					       gfp_t gfp, phys_addr_t iova, int prot)
{
	struct device *dev = dpa->dev;
	void *va = dma_alloc_coherent(dev, size, da, gfp);
	if (!va) {
		dev_err(dev, "Cannot allocate DMA region for 0x%llx\n", iova);
		return NULL;
	}
	if (google_dpa_dma_link_range(dev, va, *da, size, dev, iova, prot) < 0) {
		dev_err(dev, "Cannot link DMA region for 0x%llx\n", iova);
		dma_free_coherent(dev, size, va, *da);
		return NULL;
	}
	return va;
}

void google_dpa_dma_free_coherent_unlink_range(struct google_dpa *dpa, size_t size, void *cpu_addr,
					       dma_addr_t dma_handle, phys_addr_t iova)
{
	struct device *dev = dpa->dev;
	google_dpa_dma_unlink_range(dev, iova, size);
	dma_free_coherent(dev, size, cpu_addr, dma_handle);
}
