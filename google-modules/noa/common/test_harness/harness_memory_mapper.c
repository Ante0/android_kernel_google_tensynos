#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <soc/google/google_dpa.h>

#include "harness_memory_mapper.h"

struct noa_harness_mapping_node {
	void *cpu_addr;
	u64 mapped_addr;
	size_t size;
	struct rhash_head rhash;
	struct list_head list;
};

static const struct rhashtable_params htable_params = {
	.key_len = sizeof(void *),
	.key_offset = offsetof(struct noa_harness_mapping_node, cpu_addr),
	.head_offset = offsetof(struct noa_harness_mapping_node, rhash),
	.automatic_shrinking = true,
};

int noa_harness_memory_mapper_init(struct noa_harness_memory_mapper *mapper, struct device *dev,
				   struct google_dpa *dpa)
{
	int ret;

	if (!mapper || !dev || !dpa)
		return -EINVAL;

	mapper->dev = dev;
	mapper->dpa = dpa;
	mapper->dpa_dev = google_dpa_get_dpa_dev(dpa);
	if (!mapper->dpa_dev) {
		dev_err(dev, "Failed to get DPA device\n");
		return -ENODEV;
	}

	INIT_LIST_HEAD(&mapper->collection);
	spin_lock_init(&mapper->node_lock);

	ret = rhashtable_init(&mapper->addr_htable, &htable_params);
	if (ret) {
		dev_err(dev, "Failed to init rhashtable, err: %d\n", ret);
		return ret;
	}

	return 0;
}

void noa_harness_memory_mapper_deinit(struct noa_harness_memory_mapper *mapper)
{
	struct noa_harness_mapping_node *node, *next;

	if (!mapper)
		return;

	list_for_each_entry_safe (node, next, &mapper->collection, list) {
		dma_unmap_single(mapper->dpa_dev, node->mapped_addr, node->size, DMA_BIDIRECTIONAL);
		list_del(&node->list);
		kfree(node);
	}

	rhashtable_destroy(&mapper->addr_htable);
}

static struct noa_harness_mapping_node *
noa_harness_memory_mapper_node_create(struct noa_harness_mapping_params *params)
{
	struct noa_harness_mapping_node *node;

	node = kzalloc(sizeof(*node), GFP_ATOMIC); // Use GFP_ATOMIC in xmit path
	if (!node)
		return NULL;

	node->cpu_addr = params->cpu_addr;
	node->size = params->size;

	return node;
}

static void noa_harness_memory_mapper_node_remove(struct noa_harness_memory_mapper *mapper,
						  struct noa_harness_mapping_node *node)
{
	if (!node)
		return;

	spin_lock_bh(&mapper->node_lock);
	rhashtable_remove_fast(&mapper->addr_htable, &node->rhash, htable_params);
	list_del(&node->list);
	spin_unlock_bh(&mapper->node_lock);

	kfree(node);
}

int noa_harness_memory_mapper_remap(struct noa_harness_memory_mapper *mapper,
				    struct noa_harness_mapping_params *params, u64 *dpa_va)
{
	struct noa_harness_mapping_node *node;
	int ret;

	if (!mapper || !params || !dpa_va)
		return -EINVAL;

	node = noa_harness_memory_mapper_node_create(params);
	if (!node)
		return -ENOMEM;

	node->mapped_addr =
		dma_map_single(mapper->dpa_dev, params->cpu_addr, params->size, DMA_BIDIRECTIONAL);
	ret = dma_mapping_error(mapper->dpa_dev, node->mapped_addr);
	if (ret) {
		dev_err(mapper->dev, "Failed to map single, err: %d\n", ret);
		kfree(node);
		return ret;
	}

	*dpa_va = node->mapped_addr;

	spin_lock_bh(&mapper->node_lock);
	rhashtable_insert_fast(&mapper->addr_htable, &node->rhash, htable_params);
	list_add_tail(&node->list, &mapper->collection);
	spin_unlock_bh(&mapper->node_lock);

	return 0;
}

void noa_harness_memory_mapper_unmap(struct noa_harness_memory_mapper *mapper,
				     struct noa_harness_mapping_params *params)
{
	struct noa_harness_mapping_node *node;

	if (!mapper || !params)
		return;

	node = rhashtable_lookup_fast(&mapper->addr_htable, &params->cpu_addr, htable_params);
	if (!node) {
		dev_WARN(mapper->dev, "Unmap request for non-mapped address: %p\n",
			 params->cpu_addr);
		return;
	}

	dma_unmap_single(mapper->dpa_dev, node->mapped_addr, node->size, DMA_BIDIRECTIONAL);
	noa_harness_memory_mapper_node_remove(mapper, node);
}
