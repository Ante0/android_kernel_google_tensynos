/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Google LLC
 */

#ifndef __VS_GEM_POOL_H__
#define __VS_GEM_POOL_H__

#include <drm/vs_drm.h>
#include <drm/vs_drm.h>
#include <linux/iosys-map.h>
#include <linux/types.h>

#include "vs_gem.h"

/*
 * vs_gem_node: split gem_object memory into chunks organized as list.
 */
struct vs_gem_node {
	dma_addr_t paddr; /* physical address of buffer */
	void *vaddr; /* virtual address of buffer */

	refcount_t refcnt; /* track number of users */

	struct list_head node;
};

/*
 * vs_gem_pool: container based on gem_obj.
 * Serves as container to multiple gem_nodes organized as list.
 */
struct vs_gem_pool {
	struct vs_gem_object *gem_obj; /* gem object */
	struct iosys_map vmap; /* mapping of gma_obj */

	u16 count; /* number of nodes at the time of creation */
	u16 used; /* number of used nodes */
	struct vs_gem_node *nodes; /* memory allocation of list entries */

	struct list_head list; /* list of nodes to serve clients */
};


/*
 * initialize gem_pool
 */
int vs_gem_pool_init(struct drm_device *drm_dev, struct vs_gem_pool *gem_pool,
		     size_t node_count, size_t node_size);

/*
 * removes node from the pool
 * internally calls node_acquire to increase node use_count
 */
struct vs_gem_node *vs_gem_pool_node_get(struct vs_gem_pool *gem_pool);

/*
 * internally increase use count
 */
void vs_gem_pool_node_acquire(struct vs_gem_pool *gem_pool, struct vs_gem_node *gem_node);

/*
 * internally decreases use count.
 * calls node_put once node use count reaches 0.
 */
void vs_gem_pool_node_release(struct vs_gem_pool *gem_pool, struct vs_gem_node *gem_node);

/*
 * explicitly returns node to the pool
 */
void vs_gem_pool_node_put(struct vs_gem_pool *gem_pool, struct vs_gem_node *node);

/*
 * release pool resources
 */
void vs_gem_pool_deinit(struct vs_gem_pool *gem_pool);

#endif /* __VS_GEM_POOL_H__ */
