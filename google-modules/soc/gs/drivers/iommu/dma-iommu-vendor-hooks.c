// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2026 Google LLC
 */

#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/pixel-dma-iommu.h>
#include <trace/hooks/iommu.h>

static void pixel_dma_info_to_prot(void *unused, unsigned long attrs, int *prot)
{
	/*
	 * Add vendor-specific mapping between DMA attributes and
	 * IOMMU protection flags here.
	 */
	if (attrs & DMA_ATTR_GFP_KERNEL)
		*prot |= IOMMU_GFP_KERNEL;
	/* Clear IOMMU_CACHE flag if a non-cacheable mapping is desired. */
	if (attrs & DMA_ATTR_NO_CACHE)
		*prot &= ~IOMMU_CACHE;
}

static int __init dma_iommu_vendor_hooks_init(void)
{
	int ret;

	ret = register_trace_android_rvh_iommu_dma_info_to_prot(pixel_dma_info_to_prot, NULL);
	if (ret)
		pr_err("Failed to register iommu_dma_info_to_prot vendor hook: %d\n", ret);

	return ret;
}

module_init(dma_iommu_vendor_hooks_init);
/*
 * Load this module before IOMMU drivers (e.g., samsung_iommu) and DMA heap
 * drivers to ensure the hook is active before the first IOMMU mapping occurs.
 */
MODULE_SOFTDEP("post: samsung_dma_heap");
MODULE_SOFTDEP("post: samsung_iommu_v9");
MODULE_SOFTDEP("post: samsung_iommu");
MODULE_SOFTDEP("post: arm_smmu_v3");
MODULE_SOFTDEP("post: arm_smmu_v3_kvm");
MODULE_DESCRIPTION("Google Pixel DMA IOMMU Vendor Hook Module");
MODULE_LICENSE("GPL");
