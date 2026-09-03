/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Google LLC.
 */
#ifndef _DPMAIF_GOOGLE_H_
#define _DPMAIF_GOOGLE_H_
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/scatterlist.h>
#include <linux/types.h>
#include "../common/radio-google.h"

/* Affinity Hook */
#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_AFFINITY)
int dpmaif_google_fill_affinity(unsigned int index, u32 cpu_count, u64 *speed, u8 *napi,
				size_t napi_len, u8 *steer, size_t steer_len, u8 *reload,
				size_t reload_len, u8 *doorbell);
int dpmaif_google_affinity_init(void);
void dpmaif_google_affinity_exit(void);
#else
static inline int dpmaif_google_fill_affinity(unsigned int index, u32 cpu_count, u64 *speed, u8 *napi,
				size_t napi_len, u8 *steer, size_t steer_len, u8 *reload,
				size_t reload_len, u8 *doorbell)
{
	return -ENODEV;
}

static inline int dpmaif_google_affinity_init(void)
{
	return 0;
}

static inline void dpmaif_google_affinity_exit(void)
{
}
#endif /* CONFIG_GOOGLE_MODEM_DATA_AFFINITY */

/* Fast DMA sync */
#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_FAST_DMA_SYNC)
/**
 * dpmaif_google_dma_sync_fast - Perform a fast DMA sync for a single page.
 * @dev:	The device pointer.
 * @dma_addr:	The DMA address of the buffer.
 * @size:	The size of the buffer.
 * @dir:	The direction of the DMA transfer.
 * @page:	The page containing the data.
 * @offset:	The offset within the page.
 *
 * Context: This function is used when the page address is already known.
 *
 * This function is an optimization of dma_sync_single_for_cpu().
 * In systems with an active IOMMU, standard sync calls often involve expensive
 * address translations (e.g., iommu_iova_to_phys). Since the driver already
 * possesses both the DMA address and the underlying page/offset, we manually
 * construct a scatterlist and pre-fill sg_dma_address().
 */
static inline void dpmaif_google_dma_sync_fast(struct device *dev, dma_addr_t dma_addr, size_t size,
					       enum dma_data_direction dir, struct page *page,
					       unsigned long offset)
{
	struct scatterlist sg;

	// TODO: b/448317182 - Remove this check after confirm with MediaTek
	/* Ensure we don't cross page boundaries.
	 * The driver assumes Order-0 pages, so crossing a page means
	 * discontiguous physical memory, which sg_set_page cannot handle
	 * with a single entry.
	 */
	if (unlikely(offset + size > PAGE_SIZE)) {
		WARN_ON_ONCE(1);
		/* Fallback to standard sync */
		dma_sync_single_for_cpu(dev, dma_addr, size, dir);
		return;
	}

	sg_init_table(&sg, 1);
	sg_set_page(&sg, page, size, offset);
	sg_dma_address(&sg) = dma_addr;
	sg_dma_len(&sg) = size;

	dma_sync_sg_for_cpu(dev, &sg, 1, dir);
}
#endif /* CONFIG_GOOGLE_MODEM_DATA_FAST_DMA_SYNC */

#define DEFAULT_LRO_SIZE (GRO_MAX_SIZE)
/* The USB NCM driver TX SKB size is limited to 32KB, when WWAN aggregate a SKB bigger than 32KB,
 * it will report kernel panic immediately. We choose to cap the max LRO SKB to 31.5KB to
 * workaround this issue.
 */
#define NCM_LRO_SIZE_LIMIT (32256)

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_LRO_SIZE_LIMIT)
unsigned int dpmaif_google_get_lro_limit(void);
int dpmaif_google_lro_size_limit_init(void);
void dpmaif_google_lro_size_limit_exit(void);
#else
static inline unsigned int dpmaif_google_get_lro_limit(void)
{
	return DEFAULT_LRO_SIZE;
}

static inline int dpmaif_google_lro_size_limit_init(void)
{
	return 0;
}

static inline void dpmaif_google_lro_size_limit_exit(void)
{
}
#endif

#endif /* _DPMAIF_GOOGLE_H_ */
