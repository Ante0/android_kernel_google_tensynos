// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 */

#include <drm/drm_device.h>
#include <linux/refcount.h>
#include <linux/types.h>

#include "vs_gem_pool.h"

/*
 * initialize gem_pool
 */
int vs_gem_pool_init(struct drm_device *drm_dev, struct vs_gem_pool *gem_pool,
		     size_t node_count, size_t node_size)
{
	u64 paddr;
	void *vaddr;

	/*
	 * allocate gem object: minimal allocation is PAGE_SIZE
	 */
	gem_pool->gem_obj = vs_gem_create_object(drm_dev, node_count * node_size);
	if (IS_ERR(gem_pool->gem_obj)) {
		drm_err(drm_dev, "unable to allocate gem_obj\n");
		return -ENOMEM;
	}

	if (drm_gem_vmap_unlocked(&gem_pool->gem_obj->base, &gem_pool->vmap)) {
		vs_gem_free_object(&gem_pool->gem_obj->base);
		gem_pool->gem_obj = NULL;
		drm_err(drm_dev, "unable to map gem object\n");
		return -EFAULT;
	}

	paddr = gem_pool->gem_obj->dma_addr;
	vaddr = gem_pool->vmap.vaddr;

	/* split allocation into list of the nodes */
	INIT_LIST_HEAD(&gem_pool->list);
	gem_pool->count = node_count;
	gem_pool->used = 0;
	gem_pool->nodes = kzalloc(node_size * node_count, GFP_KERNEL);
	if (!gem_pool->nodes) {
		drm_gem_vunmap_unlocked(&gem_pool->gem_obj->base, &gem_pool->vmap);
		vs_gem_free_object(&gem_pool->gem_obj->base);
		gem_pool->gem_obj = NULL;

		drm_err(drm_dev, "unable to allocate gem_list nodes\n");
		return -ENOMEM;
	}

	for (int i = 0; i < node_count; i++) {
		struct vs_gem_node *node = &gem_pool->nodes[i];

		INIT_LIST_HEAD(&node->node);
		node->paddr = paddr + node_size * i;
		node->vaddr = vaddr + node_size * i;
		refcount_set(&node->refcnt, 0);

		list_add_tail(&node->node, &gem_pool->list);
	}

	return 0;
}

/*
 * internally increase reference count
 */
void vs_gem_pool_node_acquire(struct vs_gem_pool *gem_pool, struct vs_gem_node *gem_node)
{
	/* make sure node is valid */
	if (!gem_node)
		return;

	refcount_inc(&gem_node->refcnt);
}

/*
 * removes node from the pool
 */
struct vs_gem_node *vs_gem_pool_node_get(struct vs_gem_pool *gem_pool)
{
	struct vs_gem_node *gem_node;

	if (list_empty(&gem_pool->list))
		return NULL;

	gem_node = list_first_entry(&gem_pool->list, struct vs_gem_node, node);
	list_del(&gem_node->node);
	++gem_pool->used;

	/* mark node as used */
	refcount_set(&gem_node->refcnt, 1);

	return gem_node;
}

/*
 * internally decreases use count.
 * calls node_put once node use count reaches 0.
 */
void vs_gem_pool_node_release(struct vs_gem_pool *gem_pool, struct vs_gem_node *gem_node)
{
	/* make sure node is valid */
	if (!gem_node)
		return;

	/* decrease refcount and move back to pool if not referenced */
	if (refcount_dec_and_test(&gem_node->refcnt))
		vs_gem_pool_node_put(gem_pool, gem_node);
}

/*
 * explicitly returns node to the pool
 */
void vs_gem_pool_node_put(struct vs_gem_pool *gem_pool, struct vs_gem_node *gem_node)
{
	/* make sure node is valid */
	if (!gem_node)
		return;

	refcount_set(&gem_node->refcnt, 0); /* explicitly reset number of users */
	list_add_tail(&gem_node->node, &gem_pool->list);

	/* track used count */
	if (gem_pool->used)
		--gem_pool->used;
}

/*
 * release pool resources
 */
void vs_gem_pool_deinit(struct vs_gem_pool *gem_pool)
{
	struct drm_gem_object *gem_obj = gem_pool->gem_obj ? &gem_pool->gem_obj->base : NULL;

	if (!gem_obj)
		return;

	/* release nodes */
	kfree(gem_pool->nodes);
	gem_pool->nodes = NULL;
	INIT_LIST_HEAD(&gem_pool->list);

	/* release gem object */
	drm_gem_vunmap_unlocked(gem_obj, &gem_pool->vmap);
	vs_gem_free_object(gem_obj);
	gem_pool->gem_obj = NULL;
}
