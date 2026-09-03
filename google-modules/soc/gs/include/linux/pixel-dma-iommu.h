/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Pixel specific DMA mapping attributes and IOMMU mapping flags
 *
 * Copyright 2026 Google LLC
 */

#ifndef _PIXEL_DMA_IOMMU_H
#define _PIXEL_DMA_IOMMU_H

/*
 * List of possible attributes associated with a DMA mapping. Each attribute
 * should not overlap with bits defined in include/linux/dma-mapping.h
 */
/*
 * DMA_ATTR_GFP_KERNEL: Signal that the DMA-API has been called from a context
 * in which it is safe to use GFP_KERNEL.
 */
#define DMA_ATTR_GFP_KERNEL            (1UL << 31)
/*
 * DMA_ATTR_NO_CACHE: Create non-cacheable mapping
 */
#define DMA_ATTR_NO_CACHE		(1UL << 30)

/*
 * List of possible flags passed to iommu_map and similar functions. Each flag
 * should not overlap with bits defined in include/linux/iommu.h.
 */
/*
 * IOMMU_GFP_KERNEL: Signal that the DMA-API has been called from a context in
 * which it is safe to use GFP_KERNEL.
 */
#define IOMMU_GFP_KERNEL               (1 << 31)

#endif /* _PIXEL_DMA_IOMMU_H */
