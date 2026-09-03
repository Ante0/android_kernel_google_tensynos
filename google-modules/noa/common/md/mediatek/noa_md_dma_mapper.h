/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NOA MD DMA Mapper
 *
 * Copyright (c) 2025 Google LLC.
 */

#ifndef __NOA_MD_DMA_MAPPER_H__
#define __NOA_MD_DMA_MAPPER_H__

#include <linux/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/rhashtable.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>  /* For struct kmem_cache */
#include <linux/spinlock.h>
#include <linux/types.h>

/**
 * enum noa_md_dma_mapping_type - Type of DMA mapping
 * @NOA_MD_DMA_MAPPING_TYPE_NONE: No mapping
 * @NOA_MD_DMA_MAPPING_TYPE_CONTIGUOUS: Contiguous memory mapping
 * @NOA_MD_DMA_MAPPING_TYPE_SCATTER: Scatter-gather memory mapping
 */
enum noa_md_dma_mapping_type {
	NOA_MD_DMA_MAPPING_TYPE_NONE,
	NOA_MD_DMA_MAPPING_TYPE_CONTIGUOUS,
	NOA_MD_DMA_MAPPING_TYPE_SCATTER,
	NOA_MD_DMA_MAPPING_TYPE_MAX,
};

/**
 * struct noa_md_mapping_node - Structure representing a DMA mapping node
 * @dev: Pointer to the target device structure
 * @sgt: The SG table structure
 * @rhash: rhash head for the node
 * @list: List head for the node
 * @type: Type of mapping
 * @cpu_addr: CPU virtual address to be mapped
 * @phy_addr: Physical address corresponding to the CPU address
 * @mapped_addr: The mapped address returned by the DMA mapping
 * @size: Size of the buffer to be mapped
 */
struct noa_md_dma_mapping_node {
	struct device *dev;
	struct sg_table sgt;
	struct rhash_head rhash;
	struct list_head list;
	enum noa_md_dma_mapping_type type;
	void *cpu_addr;
	dma_addr_t phy_addr;
	dma_addr_t mapped_addr;
	size_t size;
};

/**
 * struct noa_md_dma_mapper - Structure representing the DMA mapper.
 * @htable:     The rhashtable used to store mapping nodes for efficient lookup.
 * @lock:       Protects concurrent access to the htable.
 * @mapped_num: The atomic count of currently active DMA mappings.
 * @node_cache: A dedicated slab cache (kmem_cache) for allocating.
 */
struct noa_md_dma_mapper {
	struct rhashtable htable;
	spinlock_t lock;
	atomic_t mapped_num;
	struct kmem_cache *node_cache;
};

/**
 * noa_md_dma_mapper_remap_single() - Maps a single, contiguous memory buffer.
 * @p_mapper: Pointer to the mapper instance.
 * @tdev: Target device for which the mapping is created.
 * @cpu_addr: CPU virtual address of the buffer to map.
 * @size: Size of the buffer.
 * @mapped_addr: Pointer to store the resulting DMA address for the target device.
 * @gfp_flags: GFP flags for internal node allocation (e.g., GFP_ATOMIC).
 *
 * Return: 0 on success, negative error code on failure.
 */
int noa_md_dma_mapper_remap_single(struct noa_md_dma_mapper *p_mapper,
	struct device *tdev, void *cpu_addr,
	size_t size, u64 *mapped_addr, gfp_t gfp_flags);

/**
 * noa_md_dma_mapper_remap_sg() - Maps a potentially non-contiguous (scatter-gather) buffer.
 * @p_mapper: Pointer to the mapper instance.
 * @sdev: Source device where the buffer originates.
 * @tdev: Target device for which the mapping is created.
 * @cpu_addr: CPU virtual address of the buffer to map.
 * @phy_addr: Physical DMA address of the buffer from the source device's perspective.
 * @size: Size of the buffer.
 * @mapped_addr: Pointer to store the resulting DMA address for the target device.
 * @gfp_flags: GFP flags for internal node allocation (e.g., GFP_ATOMIC).
 *
 * Return: 0 on success, negative error code on failure.
 */
int noa_md_dma_mapper_remap_sg(struct noa_md_dma_mapper *p_mapper,
	struct device *sdev, struct device *tdev,
	void *cpu_addr, dma_addr_t phy_addr,
	size_t size, u64 *mapped_addr, gfp_t gfp_flags);

/**
 * noa_md_dma_unmap_all - Release all the DMA remappings
 * @p_mapper: Pointer to the mapper structure.
 *
 * This function unmaps all DMA addresses and releases all mapping nodes
 * from the rhashtable.
 */
void noa_md_dma_unmap_all(struct noa_md_dma_mapper *p_mapper);

/**
 * noa_md_dma_unmap_by_addr - Release the DMA remapping by given cpu_addr
 * @p_mapper: Pointer to the mapper structure.
 * @cpu_addr: CPU virtual address used to lookup the node to be freed.
 *
 * This function unmaps a DMA address and releases the mapping node if the
 * cpu_addr matches.
 */
void noa_md_dma_unmap_by_addr(struct noa_md_dma_mapper *p_mapper,
	void *cpu_addr);

/**
 * noa_md_dma_mapper_init - Initialize the mapper instance
 * @p_mapper: Pointer to the mapper structure.
 *
 * This function initializes the rhash head and the mapped counter for the
 * mapper.
 */
int noa_md_dma_mapper_init(struct noa_md_dma_mapper *p_mapper,
	const char* cache_name);

/**
 * noa_md_dma_mapper_release - Deinitialize the mapper instance
 * @p_mapper: Pointer to the mapper structure.
 *
 * This function destroy the rhashtable and reset the mapper.
 */
void noa_md_dma_mapper_release(struct noa_md_dma_mapper *p_mapper);
#endif /* __NOA_MD_DMA_MAPPER_H__ */
