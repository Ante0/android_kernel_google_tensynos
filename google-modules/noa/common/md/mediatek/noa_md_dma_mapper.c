// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA MD DMA Mapper
 *
 * Copyright (c) 2025 Google LLC.
 *
 * This file provides functions for managing DMA mappings of MD resources.
 * It includes functions for translating DMA address view, and releasing DMA
 * mappings.
 */

#include <linux/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/rhashtable.h>
#include <linux/slab.h>  /* For kmem_cache */

#include "noa_md.h"
#include "noa_md_dma_mapper.h"
#include "noa_md_trace.h"

static const struct rhashtable_params htable_params = {
	.key_len = sizeof(void *),
	.key_offset = offsetof(struct noa_md_dma_mapping_node, cpu_addr),
	.head_offset = offsetof(struct noa_md_dma_mapping_node, rhash),
	.automatic_shrinking = true,
};

/**
 * noa_md_dma_unmap_internal - Unmap a DMA mapping node
 * @node: Pointer to the node to unmap
 *
 * This function unmaps a DMA address based on the node instance.
 */
static void noa_md_dma_unmap_internal(struct noa_md_dma_mapping_node *node)
{
	switch (node->type) {
	case NOA_MD_DMA_MAPPING_TYPE_CONTIGUOUS:
		dma_unmap_single(node->dev, node->mapped_addr, node->size,
				 DMA_BIDIRECTIONAL);
		break;
	case NOA_MD_DMA_MAPPING_TYPE_SCATTER:
		dma_unmap_sgtable(node->dev, &node->sgt, DMA_BIDIRECTIONAL, 0);
		break;
	default:
		NOA_MD_ERROR("failed to unmap unknown mapping type: %d", node->type);
		break;
	}
}

int noa_md_dma_mapper_remap_single(struct noa_md_dma_mapper *p_mapper,
	struct device *tdev, void *cpu_addr,
	size_t size, u64 *mapped_addr, gfp_t gfp_flags)
{
	struct noa_md_dma_mapping_node *node;
	unsigned long flags;
	int ret = 0;

	CHECK_PTR_OR_RETURN_ERR(tdev, -EINVAL);

	node = kmem_cache_zalloc(p_mapper->node_cache, gfp_flags);
	CHECK_PTR_OR_RETURN_ERR(node, -ENOMEM);

	node->type = NOA_MD_DMA_MAPPING_TYPE_CONTIGUOUS;
	*mapped_addr = dma_map_single(tdev, cpu_addr, size, DMA_BIDIRECTIONAL);
	ret = dma_mapping_error(tdev, *mapped_addr);
	if (ret) {
		NOA_MD_ERROR("failed to map single addr=%p size=%zu, err: %d",
			cpu_addr, size, ret);
		kmem_cache_free(p_mapper->node_cache, node);
		return ret;
	}

	node->dev = tdev;
	node->cpu_addr = cpu_addr;
	node->mapped_addr = *mapped_addr;
	node->size = size;

	spin_lock_irqsave(&p_mapper->lock, flags);
	ret = rhashtable_insert_fast(&p_mapper->htable, &node->rhash, htable_params);
	if (ret) {
		spin_unlock_irqrestore(&p_mapper->lock, flags);
		NOA_MD_ERROR("failed to insert node to rhashtable, err: %d", ret);
		noa_md_dma_unmap_internal(node);
		kmem_cache_free(p_mapper->node_cache, node);
		return ret;
	}
	atomic_inc(&p_mapper->mapped_num);
	spin_unlock_irqrestore(&p_mapper->lock, flags);

	return 0;
}

int noa_md_dma_mapper_remap_sg(struct noa_md_dma_mapper *p_mapper,
	struct device *sdev, struct device *tdev,
	void *cpu_addr, dma_addr_t phy_addr,
	size_t size, u64 *mapped_addr, gfp_t gfp_flags)
{
	struct noa_md_dma_mapping_node *node;
	unsigned long flags;
	int ret;

	CHECK_PTR_OR_RETURN_ERR(sdev, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(tdev, -EINVAL);

	node = kmem_cache_zalloc(p_mapper->node_cache, gfp_flags);
	CHECK_PTR_OR_RETURN_ERR(node, -ENOMEM);

	node->type = NOA_MD_DMA_MAPPING_TYPE_SCATTER;

	/* Get the sg table for the page containing cpu_addr. */
	ret = dma_get_sgtable(sdev, &node->sgt, cpu_addr, phy_addr, size);
	if (ret) {
		NOA_MD_ERROR("failed to get sgtable, err: %d", ret);
		kmem_cache_free(p_mapper->node_cache, node);
		return ret;
	}

	/* Map the physical pages in the sg table for DMA access. */
	ret = dma_map_sgtable(tdev, &node->sgt, DMA_BIDIRECTIONAL, 0);
	if (ret) {
		NOA_MD_ERROR("failed to map sgtable, err: %d", ret);
		sg_free_table(&node->sgt);
		kmem_cache_free(p_mapper->node_cache, node);
		return ret;
	}

	/* The mapped address is the page head, so add the offset. */
	*mapped_addr = sg_dma_address(node->sgt.sgl) + offset_in_page(cpu_addr);

	node->dev = tdev;
	node->cpu_addr = cpu_addr;
	node->phy_addr = phy_addr;
	node->mapped_addr = *mapped_addr;
	node->size = size;

	spin_lock_irqsave(&p_mapper->lock, flags);
	ret = rhashtable_insert_fast(&p_mapper->htable, &node->rhash, htable_params);
	if (ret) {
		spin_unlock_irqrestore(&p_mapper->lock, flags);
		NOA_MD_ERROR("failed to insert node to rhashtable, err: %d", ret);
		noa_md_dma_unmap_internal(node);
		kmem_cache_free(p_mapper->node_cache, node);
		return ret;
	}
	atomic_inc(&p_mapper->mapped_num);
	spin_unlock_irqrestore(&p_mapper->lock, flags);

	return 0;
}

void noa_md_dma_unmap_all(struct noa_md_dma_mapper *p_mapper)
{
	struct rhashtable_iter iter;
	struct noa_md_dma_mapping_node *node;
	unsigned long flags;

	LIST_HEAD(to_free_list);

	CHECK_PTR_OR_RETURN(p_mapper);

	spin_lock_irqsave(&p_mapper->lock, flags);

	rhashtable_walk_enter(&p_mapper->htable, &iter);
	rhashtable_walk_start(&iter);

	while ((node = rhashtable_walk_next(&iter))) {
		if (IS_ERR(node))
			continue;

		rhashtable_remove_fast(&p_mapper->htable, &node->rhash,
			htable_params);
		list_add(&node->list, &to_free_list);
	}
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);

	atomic_set(&p_mapper->mapped_num, 0);

	spin_unlock_irqrestore(&p_mapper->lock, flags);

	while (!list_empty(&to_free_list)) {
		node = list_first_entry(&to_free_list,
					struct noa_md_dma_mapping_node, list);
		list_del(&node->list);

		noa_md_dma_unmap_internal(node);
		kmem_cache_free(p_mapper->node_cache, node);
	}
}

void noa_md_dma_unmap_by_addr(struct noa_md_dma_mapper *p_mapper,
	void *cpu_addr)
{
	struct noa_md_dma_mapping_node *node;
	unsigned long flags;

	CHECK_PTR_OR_RETURN(p_mapper);
	CHECK_PTR_OR_RETURN(cpu_addr);

	spin_lock_irqsave(&p_mapper->lock, flags);

	node = rhashtable_lookup_fast(&p_mapper->htable, &cpu_addr,
		htable_params);

	if (node) {
		rhashtable_remove_fast(&p_mapper->htable, &node->rhash, htable_params);
		atomic_dec(&p_mapper->mapped_num);
	}

	spin_unlock_irqrestore(&p_mapper->lock, flags);

	if (node) {
		noa_md_dma_unmap_internal(node);
		kmem_cache_free(p_mapper->node_cache, node);
	}
}

int noa_md_dma_mapper_init(struct noa_md_dma_mapper *p_mapper,
	const char* cache_name)
{
	int ret;

	CHECK_PTR_OR_RETURN_ERR(p_mapper, -EINVAL);

	p_mapper->node_cache = kmem_cache_create(cache_name,
		sizeof(struct noa_md_dma_mapping_node),
		0, SLAB_HWCACHE_ALIGN, NULL);

	if (!p_mapper->node_cache) {
		NOA_MD_ERROR("failed to create kmem_cache for %s", cache_name);
		return -ENOMEM;
	}

	ret = rhashtable_init(&p_mapper->htable, &htable_params);
	if (ret) {
		kmem_cache_destroy(p_mapper->node_cache);
		p_mapper->node_cache = NULL;
		return ret;
	}

	spin_lock_init(&p_mapper->lock);
	atomic_set(&p_mapper->mapped_num, 0);

	return 0;
}

void noa_md_dma_mapper_release(struct noa_md_dma_mapper *p_mapper)
{
	CHECK_PTR_OR_RETURN(p_mapper);

	if (atomic_read(&p_mapper->mapped_num) != 0) {
		NOA_MD_ERROR("mapped_num:%d", atomic_read(&p_mapper->mapped_num));
		noa_md_dma_unmap_all(p_mapper);
	}

	rhashtable_destroy(&p_mapper->htable);
	atomic_set(&p_mapper->mapped_num, 0);
	kmem_cache_destroy(p_mapper->node_cache);
	p_mapper->node_cache = NULL;
}
