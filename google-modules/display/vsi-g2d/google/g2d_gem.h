/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#ifndef _G2D_GEM_H_
#define _G2D_GEM_H_

#include <linux/dma-buf.h>

#include <drm/drm.h>
#include <drm/drm_gem.h>

int g2d_dumb_create(struct drm_file *file, struct drm_device *dev,
		    struct drm_mode_create_dumb *args);
int g2d_gem_mmap(struct file *filp, struct vm_area_struct *vma);
struct drm_gem_object *g2d_gem_prime_import_sg_table(struct drm_device *dev,
						     struct dma_buf_attachment *attach,
						     struct sg_table *sgt);

struct g2d_bo {
	struct drm_gem_object gem;
	size_t size;
	void *virtual_region;
	dma_addr_t dma_addr;
	unsigned long dma_attrs;
	struct sg_table *sgt;
};

static inline struct g2d_bo *to_g2d_buffer_object(struct drm_gem_object *obj)
{
	return container_of(obj, struct g2d_bo, gem);
}

#endif /* _G2D_GEM_H_ */
