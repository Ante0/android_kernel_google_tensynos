// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */

#include <linux/dma-buf.h>
#include <linux/err.h>

#include <drm/drm.h>
#include <drm/drm_gem.h>
#include <drm/drm_device.h>
#include <drm/drm_prime.h>

#include "g2d_gem.h"

static void g2d_gem_free_buffer(struct g2d_bo *g2d_obj)
{
	struct drm_device *drm = g2d_obj->gem.dev;

	if ((!g2d_obj->dma_addr)) {
		dev_err(drm->dev, "dma_addr is invalid.\n");
		return;
	}

	dev_dbg(drm->dev, "Freeing the DMA allocation at %pK", g2d_obj->virtual_region);
	dma_free_attrs(drm->dev, g2d_obj->size, g2d_obj->virtual_region,
		       (dma_addr_t)g2d_obj->dma_addr, g2d_obj->dma_attrs);
}

static void g2d_gem_free_object(struct drm_gem_object *obj)
{
	struct g2d_bo *g2d_obj = to_g2d_buffer_object(obj);

	if (obj->import_attach)
		drm_prime_gem_destroy(obj, g2d_obj->sgt);
	else
		g2d_gem_free_buffer(g2d_obj);

	drm_gem_object_release(obj);

	kfree(g2d_obj);
}

static const struct vm_operations_struct g2d_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct drm_gem_object_funcs g2d_gem_object_funcs = {
	.free = g2d_gem_free_object,
	.vm_ops = &g2d_vm_ops,
};

static int g2d_mmap_obj(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct g2d_bo *g2d_obj = to_g2d_buffer_object(obj);
	struct drm_device *drm = obj->dev;
	unsigned long vm_size;
	int ret = 0;

	vm_size = vma->vm_end - vma->vm_start;
	if (vm_size > g2d_obj->size)
		return -EINVAL;

	vma->vm_pgoff = 0;

	vm_flags_clear(vma, VM_PFNMAP);

	ret = dma_mmap_attrs(drm->dev, vma, g2d_obj->virtual_region, g2d_obj->dma_addr,
			     g2d_obj->size, g2d_obj->dma_attrs);

	if (ret) {
		dev_err(drm->dev, "dma_map attrs failed ret: %d!!", ret);
		drm_gem_vm_close(vma);
	} else {
		dev_dbg(drm->dev, "%s: mmap success!", __func__);
	}

	return ret;
}

int g2d_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret = 0;
	struct drm_gem_object *obj;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	obj = vma->vm_private_data;

	if (!obj)
		return -EINVAL;

	if (obj->import_attach)
		return dma_buf_mmap(obj->dma_buf, vma, 0);

	return g2d_mmap_obj(obj, vma);
}

static int g2d_gem_alloc(struct g2d_bo *g2d_obj)
{
	struct drm_device *drm = g2d_obj->gem.dev;

	/*
	 * TODO(b/391428072): Add iommu support so that we no longer need to require contiguous
	 * memory allocations.
	 */

	g2d_obj->dma_attrs = DMA_ATTR_WRITE_COMBINE | DMA_ATTR_NO_KERNEL_MAPPING |
			     DMA_ATTR_FORCE_CONTIGUOUS;

	g2d_obj->virtual_region = dma_alloc_attrs(drm->dev, g2d_obj->size, &g2d_obj->dma_addr,
						  GFP_KERNEL, g2d_obj->dma_attrs);

	if (!g2d_obj->virtual_region) {
		dev_err(drm->dev, "failed to allocate buffer.\n");
		return -ENOMEM;
	}
	dev_dbg(drm->dev, "Created dma virtual region of size %zu at kernel vaddr: %pK",
		g2d_obj->size, g2d_obj->virtual_region);

	return 0;
}

static struct g2d_bo *g2d_alloc_buffer_object(struct drm_device *drm, size_t size)
{
	struct g2d_bo *g2d_obj;
	int ret;

	g2d_obj = kzalloc(sizeof(*g2d_obj), GFP_KERNEL);
	if (!g2d_obj)
		return ERR_PTR(-ENOMEM);

	g2d_obj->gem.funcs = &g2d_gem_object_funcs;

	g2d_obj->size = size;

	ret = drm_gem_object_init(drm, &g2d_obj->gem, size);

	if (ret) {
		kfree(g2d_obj);
		dev_err(drm->dev, "%s: Gem object init failure!", __func__);
		return ERR_PTR(ret);
	}
	dev_dbg(drm->dev, "Gem object init success!");

	ret = drm_gem_create_mmap_offset(&g2d_obj->gem);
	if (ret) {
		drm_gem_object_release(&g2d_obj->gem);
		kfree(g2d_obj);
		dev_err(drm->dev, "%s: Gem create mmap offset failure!", __func__);
		return ERR_PTR(ret);
	}
	dev_dbg(drm->dev, "Gem create mmap offset success!");

	return g2d_obj;
}

int g2d_dumb_create(struct drm_file *file, struct drm_device *drm,
		    struct drm_mode_create_dumb *args)
{
	int ret;
	unsigned int min_pitch;
	size_t size;
	struct g2d_bo *g2d_obj;

	min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	/* Use the max pitch alignment since we don't have a way to identify the pixel format */
	args->pitch = ALIGN(min_pitch, 64 /*pitch alignment*/);
	size = args->pitch * args->height;
	args->size = round_up(size, PAGE_SIZE);

	g2d_obj = g2d_alloc_buffer_object(drm, args->size);
	if (IS_ERR(g2d_obj))
		return PTR_ERR(g2d_obj);

	ret = g2d_gem_alloc(g2d_obj);
	if (ret)
		goto gem_alloc_err;

	ret = drm_gem_handle_create(file, &g2d_obj->gem, &args->handle);
	if (ret) {
		dev_err(drm->dev, "%s: Handle create failure!", __func__);
		goto handle_create_err;
	}

	dev_dbg(drm->dev, "Handle create success! Handle: 0x%x", args->handle);
	drm_gem_object_put(&g2d_obj->gem);
	return 0;

handle_create_err:
	drm_gem_object_release(&g2d_obj->gem);
gem_alloc_err:
	kfree(g2d_obj);
	return ret;
}

struct drm_gem_object *g2d_gem_prime_import_sg_table(struct drm_device *drm,
						     struct dma_buf_attachment *attach,
						     struct sg_table *sgt)
{
	struct g2d_bo *g2d_obj;
	int ret;
	struct scatterlist *s;
	u32 i;
	dma_addr_t expected;
	size_t size = attach->dmabuf->size;

	size = PAGE_ALIGN(size);

	g2d_obj = g2d_alloc_buffer_object(drm, size);
	if (IS_ERR(g2d_obj))
		return ERR_CAST(g2d_obj);

	/* Check for invalid sg table */
	expected = sg_dma_address(sgt->sgl);
	for_each_sg(sgt->sgl, s, sgt->nents, i) {
		if (sg_dma_address(s) != expected) {
			DRM_DEV_ERROR(drm->dev, "sg_table is not contiguous");
			ret = -EINVAL;
			goto err;
		}

		if (sg_dma_len(s) & (PAGE_SIZE - 1)) {
			ret = -EINVAL;
			goto err;
		}

		expected = sg_dma_address(s) + sg_dma_len(s);
	}

	g2d_obj->dma_addr = sg_dma_address(sgt->sgl);

	g2d_obj->sgt = sgt;

	return &g2d_obj->gem;

err:
	g2d_gem_free_object(&g2d_obj->gem);

	// Todo(b/390035554): Standardize the G2D driver on ERR_PTR or NULL, this can cause bugs.
	return ERR_PTR(ret);
}
