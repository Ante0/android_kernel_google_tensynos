// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA WLAN DMA Mapper
 *
 * Copyright (c) 2025 Google LLC.
 *
 * This file provides functions for managing DMA mappings of WLAN resources.
 * It includes functions for translating DMA address view, and releasing DMA
 * mappings.
 */

#include <common/wlan/noa_wlan.h>
#include "noa_wlan_client.h"
#include "noa_wlan_mapper.h"

static const struct rhashtable_params htable_params = {
	.key_len = sizeof(void *),
	.key_offset = offsetof(struct noa_wlan_mapping_node, cpu_addr),
	.head_offset = offsetof(struct noa_wlan_mapping_node, rhash),
	.automatic_shrinking = true,
};

/**
 * noa_wlan_mapper_remap_contiguous - Remap a contiguous DMA address
 * @dev: Pointer to the device structure
 * @cpu_addr: CPU virtual address to be mapped
 * @size: Size of the buffer to be mapped
 * @mapped_addr: Pointer to store the mapped DMA address
 *
 * This function remaps a contiguous DMA address into a different SMMU view.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int noa_wlan_mapper_remap_contiguous(struct device *dev, void *cpu_addr, size_t size,
					    u64 *mapped_addr)
{
	int ret;

	if (!dev)
		return -EINVAL;

	*mapped_addr = dma_map_single(dev, cpu_addr, size, DMA_BIDIRECTIONAL);
	ret = dma_mapping_error(dev, *mapped_addr);
	if (ret) {
		dev_err(dev, "%s(): failed to map single, err: %d\n", __func__, ret);

		return ret;
	}

	return 0;
}

/**
 * noa_wlan_mapper_remap_sg - Remap a scatter-gather DMA address
 * @sdev: Pointer to the source device structure
 * @tdev: Pointer to the target device structure
 * @cpu_addr: CPU virtual address to be mapped
 * @phy_addr: Physical address corresponding to the CPU address
 * @size: Size of the buffer to be mapped
 * @sgt: Pointer to the scatter-gather table
 * @mapped_addr: Pointer to store the mapped DMA address
 *
 * This function remaps a scatter-gather DMA address into a different SMMU view.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int noa_wlan_mapper_remap_sg(struct device *sdev, struct device *tdev, void *cpu_addr,
				    dma_addr_t phy_addr, size_t size, struct sg_table *sgt,
				    u64 *mapped_addr)
{
	int ret;

	if (!sdev || !tdev)
		return -EINVAL;

	ret = dma_get_sgtable(sdev, sgt, cpu_addr, phy_addr, size);
	if (ret) {
		dev_err(sdev, "%s(): failed to get sgtable, err: %d\n", __func__, ret);

		return ret;
	}

	ret = dma_map_sgtable(tdev, sgt, DMA_BIDIRECTIONAL, 0);
	if (ret) {
		dev_err(sdev, "%s(): failed to map sgtable, err: %d\n", __func__, ret);

		return ret;
	}

	*mapped_addr = sg_dma_address(sgt->sgl);

	return 0;
}

/**
 * noa_wlan_mapper_remap_noncache - Remap a Non-cacheable DMA address
 * @sdev: Pointer to the source device structure
 * @tdev: Pointer to the target device structure
 * @cpu_addr: CPU virtual address to be mapped
 * @phy_addr: Physical address corresponding to the CPU address
 * @size: Size of the buffer to be mapped
 * @sgt: Pointer to the scatter-gather table
 * @mapped_addr: Pointer to store the mapped DMA address
 *
 * This function remaps a non-cacheable DMA address into a specific SMMU view.
 *
 * Return: 0 on success, negative error code otherwise.
 */
#define IOMMU_MIN_PAGES 0x1000
static phys_addr_t iova_cur = NONCACHE_IOVA_START;

static int noa_wlan_mapper_remap_noncache(struct device *sdev, struct device *tdev, void *cpu_addr,
				    dma_addr_t phy_addr, size_t size, struct sg_table *sgt,
				    u64 *mapped_addr)
{
	int ret;
	ssize_t mapped = 0;
	phys_addr_t iova = iova_cur;
	size_t alloc_size = ((size + IOMMU_MIN_PAGES -1) / IOMMU_MIN_PAGES) * IOMMU_MIN_PAGES;

	if ((iova + alloc_size) > NONCACHE_IOVA_END)
		return -ENOMEM;

	if (!sdev || !tdev)
		return -EINVAL;

	ret = dma_get_sgtable(sdev, sgt, cpu_addr, phy_addr, alloc_size);
	if (ret) {
		dev_err(sdev, "%s(): failed to get sgtable, err: %d\n", __func__, ret);

		return ret;
	}

	mapped = iommu_map_sgtable(iommu_get_domain_for_dev(tdev),
					iova, sgt, IOMMU_WRITE | IOMMU_READ);
	if (mapped < 0) {
		dev_err(sdev, "%s(): failed to map DMA region for 0x%llx, size %zx, %zx\n", __func__, iova, size, alloc_size);
		return -ENOMEM;
	} else {
		dev_err(sdev, "%s(): success to map DMA region for 0x%llx, 0x%lx, size %zx, %zx\\n",
			__func__, iova, (unsigned long)phy_addr, size, alloc_size);
	}

	*mapped_addr = iova;
	iova_cur += alloc_size;
	return 0;
}

static int noa_wlan_mapper_remap_internal(struct noa_wlan_client *client,
					  struct noa_wlan_mapping_node *node)
{
	switch (node->mapping_type) {
	case MAPPING_TYPE_CONTIGUOUS:
		return noa_wlan_mapper_remap_contiguous(
					client->dpa_dev, node->cpu_addr,
					node->size, &node->mapped_addr);
	case MAPPING_TYPE_SCATTER:
		return noa_wlan_mapper_remap_sg(client->dev, client->dpa_dev,
						node->cpu_addr, node->phy_addr,
						node->size, &node->sgt, &node->mapped_addr);
	case MAPPING_TYPE_NONCACHE:
		return noa_wlan_mapper_remap_noncache(client->dev, client->dpa_dev,
						node->cpu_addr, node->phy_addr,
						node->size, &node->sgt, &node->mapped_addr);
	default:
		dev_err(client->dev, "%s(): failed to unmap unknown mapping type: %d\n",
			__func__, node->mapping_type);
		return -EINVAL;
	}

	return 0;
}

static void noa_wlan_mapper_unmap_internal(struct noa_wlan_client *client,
					   struct noa_wlan_mapping_node *node)
{
	switch (node->mapping_type) {
	case MAPPING_TYPE_CONTIGUOUS:
		dma_unmap_single(node->dev, node->mapped_addr, node->size,
				 DMA_BIDIRECTIONAL);
		break;
	case MAPPING_TYPE_SCATTER:
		dma_unmap_sgtable(node->dev, &node->sgt, DMA_BIDIRECTIONAL, 0);
		break;
	case MAPPING_TYPE_NONCACHE:
		iommu_unmap(iommu_get_domain_for_dev(node->dev), node->mapped_addr, node->size);
		break;
	default:
		dev_err(client->dev, "%s(): failed to unmap unknown mapping type: %d\n",
			__func__, node->mapping_type);
		break;
	}
}

static struct noa_wlan_mapping_node **noa_wlan_mapper_tkid_htable_get(
					struct noa_wlan_client *client,
					u32 pool_id, u32 tkid)
{
	struct noa_wlan_mapper *mapper = &client->mapper;
	struct noa_wlan_tkid_htable *target;

	if (!is_pool_valid(mapper, pool_id))
		return NULL;

	target = mapper->tkid_htable_group[pool_id];

	if (tkid > target->tkid_range)
		return NULL;

	return target->tkid_to_node;
}

static struct noa_wlan_mapping_node *noa_wlan_mapper_node_create(
				struct noa_wlan_client *client,
				struct noa_wlan_mapping_params *params)
{
	struct noa_wlan_mapping_node *node;

	node = kzalloc(sizeof(struct noa_wlan_mapping_node), GFP_KERNEL);
	if (!node)
		return NULL;

	node->dev = client->dpa_dev;
	node->cpu_addr = params->cpu_addr;
	node->phy_addr = params->phy_addr;
	node->size = params->size;
	node->mapping_type = (params->contiguous) ?
			      MAPPING_TYPE_CONTIGUOUS : MAPPING_TYPE_SCATTER;
	if (params->noncache) {
		node->mapping_type = MAPPING_TYPE_NONCACHE;
	}
	node->tkid_in_use = params->tkid_in_use;

	if (node->tkid_in_use) {
		node->pool_id = params->pool_id;
		node->tkid = params->tkid;
	} else {
		node->pool_id = 0;
		node->tkid = 0;
	}

	return node;
}

static int noa_wlan_mapper_node_insert(struct noa_wlan_client *client,
				       struct noa_wlan_mapping_node *node)
{
	struct noa_wlan_mapper *mapper = &client->mapper;
	struct noa_wlan_mapping_node **tkid_htable;
	u32 tkid;

	if (!node)
		return -EINVAL;

	spin_lock_bh(&mapper->node_lock);

	if (node->tkid_in_use) {
		tkid = node->tkid;
		tkid_htable = noa_wlan_mapper_tkid_htable_get(client,
							      node->pool_id, tkid);
		if (!tkid_htable)
			return -EINVAL;

		tkid_htable[tkid] = node;
	} else {
		rhashtable_insert_fast(&mapper->addr_htable, &node->rhash,
				       htable_params);
	}

	list_add_tail(&node->list, &mapper->collection);
	mapper->mapped_num++;

	spin_unlock_bh(&mapper->node_lock);

	return 0;
}

static void noa_wlan_mapper_node_remove(struct noa_wlan_client *client,
					struct noa_wlan_mapping_node *node)
{
	struct noa_wlan_mapper *mapper = &client->mapper;
	struct noa_wlan_mapping_node **tkid_htable;
	u32 tkid;

	if (!node)
		return;

	spin_lock_bh(&mapper->node_lock);

	if (node->tkid_in_use) {
		tkid = node->tkid;
		tkid_htable = noa_wlan_mapper_tkid_htable_get(client,
							      node->pool_id, tkid);
		if (!tkid_htable)
			return;

		tkid_htable[tkid] = NULL;
	} else {
		rhashtable_remove_fast(&mapper->addr_htable, &node->rhash,
				       htable_params);
	}

	list_del(&node->list);
	mapper->mapped_num--;

	spin_unlock_bh(&mapper->node_lock);
	kfree(node);
}

/**
 * noa_wlan_mapper_node_collect - Collect all mapping nodes
 * @client: Pointer to the noa wlan client structure
 *
 * This function collects all the mapping nodes into a list.
 *
 * Return: A pointer to the list head containing the collected nodes.
 */
struct list_head *noa_wlan_mapper_node_collect(struct noa_wlan_client *client)
{
	return &client->mapper.collection;
}
EXPORT_SYMBOL_GPL(noa_wlan_mapper_node_collect);

/**
 * noa_wlan_mapper_remap - Translate a DMA address into a different SMMU view
 * @client: Pointer to the noa wlan client structure
 * @params: Pointer to the noa wlan mapping parameters structure
 * @mapped_addr: Pointer to store the mapped DMA address
 *
 * This function remaps a DMA address into a different SMMU view. It handles
 * both contiguous and scatter-gather memory.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int noa_wlan_mapper_remap(struct noa_wlan_client *client,
			  struct noa_wlan_mapping_params *params,
			  u64 *mapped_addr)
{
	struct noa_wlan_mapping_node *node;
	int ret;

	if (!IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)) {
		/*
		 * In simulator mode, the NOA view and the AP view are the same, so
		 * the `cpu_addr` is assigned directly to `mapped_addr`.
		 */
		*mapped_addr = (u64)params->cpu_addr;
		return 0;
	}

	if (!client)
		return -EINVAL;

	node = noa_wlan_mapper_node_create(client, params);
	if (!node)
		return -ENOMEM;

	ret = noa_wlan_mapper_remap_internal(client, node);
	if (ret)
		goto err_node;

	ret = noa_wlan_mapper_node_insert(client, node);
	if (ret)
		goto err_unmap;

	*mapped_addr = node->mapped_addr;

	return 0;

err_unmap:
	noa_wlan_mapper_unmap_internal(client, node);

err_node:
	kfree(node);

	return ret;
}
EXPORT_SYMBOL_GPL(noa_wlan_mapper_remap);

/**
 * noa_wlan_mapper_unmap_all - Release all the DMA remappings
 * @client: Pointer to the noa wlan client structure
 *
 * This function unmaps all DMA addresses and releases all mapping nodes
 * from the mapper's hashtable.
 */
void noa_wlan_mapper_unmap_all(struct noa_wlan_client *client)
{
	struct noa_wlan_mapper *mapper;
	struct noa_wlan_mapping_node *node;
	struct noa_wlan_mapping_node *next;

	if (!IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT) || !client)
		return;

	mapper = &client->mapper;

	list_for_each_entry_safe(node, next, &mapper->collection, list) {
		noa_wlan_mapper_unmap_internal(client, node);
		noa_wlan_mapper_node_remove(client, node);
	}

	client->mapper.mapped_num = 0;
}
EXPORT_SYMBOL_GPL(noa_wlan_mapper_unmap_all);

/**
 * noa_wlan_mapper_unmap_by_addr - Release the DMA remapping by given cpu_addr
 * @client: Pointer to the noa wlan client structure
 * @cpu_addr: CPU virtual address used to lookup the node to be freed
 *
 * This function unmaps a DMA address and releases the mapping node if the
 * cpu_addr matches.
 */
void noa_wlan_mapper_unmap_by_addr(struct noa_wlan_client *client,
				   void *cpu_addr)
{
	struct noa_wlan_mapping_node *node;

	if (!IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT) || !client || !cpu_addr)
		return;

	node = rhashtable_lookup_fast(&client->mapper.addr_htable, &cpu_addr,
				      htable_params);
	if (!node)
		return;

	noa_wlan_mapper_unmap_internal(client, node);
	noa_wlan_mapper_node_remove(client, node);
}
EXPORT_SYMBOL_GPL(noa_wlan_mapper_unmap_by_addr);

/**
 * noa_wlan_mapper_unmap_by_tkid - Release the DMA remapping by given tkid
 * @client: Pointer to the noa wlan client structure
 * @pool_id: The pool ID used to lookup the tkid hashtable
 * @tkid: The tkid to be unmapped
 *
 * This function unmaps a DMA address and releases the mapping node if the
 * tkid matches.
 */
void noa_wlan_mapper_unmap_by_tkid(struct noa_wlan_client *client,
				   u32 pool_id, u32 tkid)
{
	struct noa_wlan_mapping_node **tkid_htable;
	struct noa_wlan_mapping_node *node;

	if (!IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT) || !client ||
	    !is_pool_valid(&client->mapper, pool_id))
		return;

	tkid_htable = noa_wlan_mapper_tkid_htable_get(client, pool_id, tkid);
	if (!tkid_htable)
		return;

	node = tkid_htable[tkid];
	if (!node)
		return;

	noa_wlan_mapper_unmap_internal(client, node);
	noa_wlan_mapper_node_remove(client, node);
}
EXPORT_SYMBOL_GPL(noa_wlan_mapper_unmap_by_tkid);

/**
 * noa_wlan_mapper_init - Initialize the mapper instance
 * @client: Pointer to the noa wlan client structure
 * @setup: Pointer to the noa wlan mapping setup structure
 *
 * This function initializes the hashtables and the mapped counter for the
 * client's mapper. It initializes the hashtables based on the provided setup
 * parameters, and initializes the list head for the collection of all
 * mapped nodes.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int noa_wlan_mapper_init(struct noa_wlan_client *client,
			 struct noa_wlan_mapping_setup *setup)
{
	struct noa_wlan_mapper *mapper;
	struct noa_wlan_tkid_htable *tkid_htable;
	u32 profile_num;
	int ret;
	int i;

	if (!IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT))
		return 0;

	if (!client || !setup || !is_setup_valid(setup))
		return -EINVAL;

	mapper = &client->mapper;
	profile_num = setup->profile_num;

	INIT_LIST_HEAD(&mapper->collection);
	spin_lock_init(&mapper->node_lock);

	ret = rhashtable_init(&mapper->addr_htable, &htable_params);
	if (ret)
		return -EINVAL;

	mapper->tkid_htable_group = kzalloc(sizeof(*mapper->tkid_htable_group) *
					    profile_num, GFP_KERNEL);
	if (!mapper->tkid_htable_group)
		goto err_rhashtable;

	for (i = 0; i < profile_num; i++) {
		tkid_htable = kzalloc(sizeof(*tkid_htable), GFP_KERNEL);
		if (!tkid_htable)
			goto err_htable_group;

		tkid_htable->pool_id = setup->profile[i].pool_id;
		tkid_htable->tkid_range = setup->profile[i].tkid_range;
		tkid_htable->tkid_to_node =
			kzalloc(sizeof(*tkid_htable->tkid_to_node) *
				(tkid_htable->tkid_range + 1), GFP_KERNEL);
		if (!tkid_htable->tkid_to_node)
			goto err_htable;

		mapper->tkid_htable_group[i] = tkid_htable;
	}

	mapper->tkid_htable_group_sz = profile_num;
	mapper->mapped_num = 0;

	return 0;

err_htable:
	kfree(tkid_htable);

err_htable_group:
	kfree(mapper->tkid_htable_group);

err_rhashtable:
	rhashtable_destroy(&mapper->addr_htable);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(noa_wlan_mapper_init);

/**
 * noa_wlan_mapper_deinit - Deinitialize the mapper instance
 * @client: Pointer to the noa wlan client structure
 *
 * This function destroy the hashtables and reset the mapper.
 */
void noa_wlan_mapper_deinit(struct noa_wlan_client *client)
{
	struct noa_wlan_mapper *mapper;
	int i;

	if (!IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT) || !client)
		return;

	mapper = &client->mapper;

	noa_wlan_mapper_unmap_all(client);
	rhashtable_destroy(&mapper->addr_htable);

	if (!mapper->tkid_htable_group)
		return;

	for (i = 0; i < mapper->tkid_htable_group_sz; i++) {
		if (!mapper->tkid_htable_group[i])
			continue;

		kfree(mapper->tkid_htable_group[i]->tkid_to_node);
		kfree(mapper->tkid_htable_group[i]);
	}

	kfree(mapper->tkid_htable_group);
	mapper->tkid_htable_group = NULL;
	mapper->tkid_htable_group_sz = 0;
}
EXPORT_SYMBOL_GPL(noa_wlan_mapper_deinit);
