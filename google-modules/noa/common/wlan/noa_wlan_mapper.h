/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NOA WLAN DMA Mapper
 *
 * Copyright (c) 2025 Google LLC.
 */

#ifndef __NOA_WLAN_MAPPER_H__
#define __NOA_WLAN_MAPPER_H__

#include <linux/list.h>
#include <linux/rhashtable.h>
#include <linux/scatterlist.h>
#include <linux/types.h>
#include <linux/iommu.h>

#define PROFILE_NUM_MAX			8

struct noa_wlan_client;

/**
 * enum noa_wlan_mapping_type - Type of DMA mapping
 * @MAPPING_TYPE_NONE: No mapping
 * @MAPPING_TYPE_CONTIGUOUS: Contiguous memory mapping
 * @MAPPING_TYPE_SCATTER: Scatter-gather memory mapping
 * @MAPPING_TYPE_NONCACHE: Non-cacheable memory mapping
 */
enum noa_wlan_mapping_type {
	MAPPING_TYPE_NONE,
	MAPPING_TYPE_CONTIGUOUS,
	MAPPING_TYPE_SCATTER,
	MAPPING_TYPE_NONCACHE,

	__MAPPING_TYPE_MAX,
};

/**
 * struct noa_wlan_mapping_setup - Setup parameters for DMA mapping profiles
 * @profile_num: Number of specified profiles
 * @pool_id: Identifier for a specific buffer pool
 * @tkid_range: The tkid range available for the specified buffer pool
 */
struct noa_wlan_mapping_setup {
	u32				profile_num;

	struct {
		u32			pool_id;
		u32			tkid_range;
	} profile[PROFILE_NUM_MAX];
};

/**
 * struct noa_wlan_mapping_params - Parameters for DMA mapping
 * @cpu_addr: CPU virtual address to be mapped
 * @phy_addr: Physical address corresponding to the CPU address
 * @size: Size of the buffer to be mapped
 * @contiguous: Flag indicating if the memory is contiguous
 * @noncache: Flag indicating if the memory is non-cacheable
 * @tkid_in_use: Flag indicating if the tkid is in use
 * @pool_id: Identifier of the buffer pool to which the buffer belongs
 * @tkid: The tkid associated with the buffer
 */
struct noa_wlan_mapping_params {
	void				*cpu_addr;
	dma_addr_t			phy_addr;
	size_t				size;
	bool				contiguous;
	bool				noncache;
	bool				tkid_in_use;
	u32				pool_id;
	u32				tkid;
};

/**
 * struct noa_wlan_mapping_node - Structure representing a DMA mapping node
 * @dev: Pointer to the target device structure
 * @sgt: Scatter-gather table describing the mapped buffer
 * @rhash: rhash head for the node, used for address-based lookup
 * @list: List head for the node, used for traversing all mappings
 * @mapping_type: Type of mapping
 * @cpu_addr: CPU virtual address of the buffer
 * @phy_addr: Physical address of the buffer
 * @mapped_addr: The mapped address returned by the DMA mapping
 * @size: Size of the buffer to be mapped
 * @tkid_in_use: Flag indicating if the tkid is in use
 * @pool_id: Identifier of the buffer pool to which the buffer belongs
 * @tkid: The tkid associated with the buffer
 */
struct noa_wlan_mapping_node {
	struct device			*dev;
	struct sg_table			sgt;
	struct rhash_head		rhash;
	struct list_head		list;
	enum noa_wlan_mapping_type	mapping_type;
	void				*cpu_addr;
	dma_addr_t			phy_addr;
	dma_addr_t			mapped_addr;
	size_t				size;
	bool				tkid_in_use;
	u32				pool_id;
	u32				tkid;
};

/**
 * struct noa_wlan_tkid_htable - Structure representing a tkid-based hashtable
 * @tkid_to_node: Array of mapping nodes, indexed by tkid
 * @pool_id: Identifier of the buffer pool to which the buffer belongs
 * @tkid_range: The tkid range available for the specified buffer pool
 */
struct noa_wlan_tkid_htable {
	struct noa_wlan_mapping_node	**tkid_to_node;
	u32				pool_id;
	u32				tkid_range;
};

/**
 * struct noa_wlan_mapper - Structure representing the DMA mapper
 * @collection: List head for all mapped nodes
 * @addr_htable: rhashtable for mapping nodes, indexed by CPU virtual address
 * @tkid_htable_group: Array of tkid-based hashtables, indexed by pool_id
 * @tkid_htable_group_sz: Number of hashtables in the @tkid_htable_group
 * @mapped_num: Number of nodes currently mapped
 */
struct noa_wlan_mapper {
	struct list_head		collection;
	struct rhashtable		addr_htable;
	struct noa_wlan_tkid_htable	**tkid_htable_group;
	spinlock_t			node_lock;
	u32				tkid_htable_group_sz;
	u32				mapped_num;
};

struct list_head *noa_wlan_mapper_node_collect(struct noa_wlan_client *client);
int noa_wlan_mapper_remap(struct noa_wlan_client *client,
			  struct noa_wlan_mapping_params *params,
			  u64 *mapped_addr);
void noa_wlan_mapper_unmap_all(struct noa_wlan_client *client);
void noa_wlan_mapper_unmap_by_addr(struct noa_wlan_client *client,
				   void *cpu_addr);
void noa_wlan_mapper_unmap_by_tkid(struct noa_wlan_client *client,
				   u32 pool_id, u32 tkid);
int noa_wlan_mapper_init(struct noa_wlan_client *client,
			 struct noa_wlan_mapping_setup *setup);
void noa_wlan_mapper_deinit(struct noa_wlan_client *client);

static inline bool is_setup_valid(struct noa_wlan_mapping_setup *setup)
{
	return setup->profile_num <= PROFILE_NUM_MAX;
}

static inline bool is_pool_valid(struct noa_wlan_mapper *mapper, u32 pool_id)
{
	return pool_id < mapper->tkid_htable_group_sz;
}

static inline bool is_mapper_empty(struct noa_wlan_mapper *mapper)
{
	return mapper->mapped_num == 0;
}

#endif /* __NOA_WLAN_MAPPER_H__ */
