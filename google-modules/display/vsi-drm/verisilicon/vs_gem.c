// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>

#include <asm/set_memory.h>

#include <drm/drm_drv.h>
#include "drm/vs_drm.h"
#include "vs_drv.h"
#include "vs_gem.h"

static const struct drm_gem_object_funcs vs_gem_default_funcs;

static int vs_gem_alloc_buf(struct vs_gem_object *vs_obj)
{
	struct drm_device *dev = vs_obj->base.dev;
	struct device *dma_dev = to_dma_dev(dev);
	int ret;

	if (vs_obj->sgt || vs_obj->cookie) {
		DRM_DEV_DEBUG(dev->dev, "already allocated.\n");
		return 0;
	}

	/* Non-IOMMU case: must be contiguous.
	 * Use dma_alloc_attrs with FORCE_CONTIGUOUS.
	 * We skip NO_KERNEL_MAPPING here to ensure we can get an sgt reliably
	 * via dma_get_sgtable, as some implementations require a vaddr.
	 */
	vs_obj->dma_attrs = DMA_ATTR_WRITE_COMBINE;
	if (!is_iommu_enabled(dev))
		vs_obj->dma_attrs |= DMA_ATTR_FORCE_CONTIGUOUS;

	vs_obj->cookie = dma_alloc_attrs(dma_dev, vs_obj->size, &vs_obj->dma_addr, GFP_KERNEL,
					 vs_obj->dma_attrs);
	if (!vs_obj->cookie) {
		DRM_DEV_ERROR(dev->dev, "failed to allocate buffer.\n");
		return -ENOMEM;
	}

	vs_obj->sgt = kzalloc(sizeof(*vs_obj->sgt), GFP_KERNEL);
	if (!vs_obj->sgt) {
		ret = -ENOMEM;
		goto err_free_attrs;
	}

	ret = dma_get_sgtable(dma_dev, vs_obj->sgt, vs_obj->cookie, vs_obj->dma_addr, vs_obj->size);
	if (ret) {
		DRM_DEV_ERROR(dev->dev, "failed to get sgtable for buffer.\n");
		goto err_free_sgt;
	}

	vs_obj->iova = (u64)vs_obj->dma_addr;

	return 0;

err_free_sgt:
	kfree(vs_obj->sgt);
	vs_obj->sgt = NULL;
err_free_attrs:
	dma_free_attrs(dma_dev, vs_obj->size, vs_obj->cookie,
		       vs_obj->dma_addr, vs_obj->dma_attrs);
	vs_obj->cookie = NULL;
	return ret;
}

static void vs_gem_free_buf(struct vs_gem_object *vs_obj)
{
	struct drm_device *dev = vs_obj->base.dev;
	struct device *dma_dev = to_dma_dev(dev);

	if (!vs_obj->sgt && !vs_obj->cookie) {
		DRM_DEV_DEBUG(dev->dev, "buffer is not allocated.\n");
		return;
	}

	if (vs_obj->sgt) {
		sg_free_table(vs_obj->sgt);
		kfree(vs_obj->sgt);
		vs_obj->sgt = NULL;
	}
	if (vs_obj->cookie) {
		dma_free_attrs(dma_dev, vs_obj->size, vs_obj->cookie,
			       vs_obj->dma_addr, vs_obj->dma_attrs);
		vs_obj->cookie = NULL;
	}

	vs_obj->dma_addr = 0;
}

void vs_gem_free_object(struct drm_gem_object *obj)
{
	struct vs_gem_object *vs_obj = to_vs_gem_object(obj);

	if (obj->import_attach)
		drm_prime_gem_destroy(obj, vs_obj->sgt);
	else
		vs_gem_free_buf(vs_obj);

	drm_gem_object_release(obj);

	kfree(vs_obj);
}

static struct vs_gem_object *vs_gem_alloc_object(struct drm_device *dev, size_t size)
{
	struct vs_gem_object *vs_obj;
	struct drm_gem_object *obj;
	int ret;

	vs_obj = kzalloc(sizeof(*vs_obj), GFP_KERNEL);
	if (!vs_obj)
		return ERR_PTR(-ENOMEM);

	vs_obj->size = size;
	obj = &vs_obj->base;
	vs_obj->base.funcs = &vs_gem_default_funcs;

	ret = drm_gem_object_init(dev, obj, size);
	if (ret)
		goto err_free;

	ret = drm_gem_create_mmap_offset(obj);
	if (ret) {
		drm_gem_object_release(obj);
		goto err_free;
	}

	return vs_obj;

err_free:
	kfree(vs_obj);
	return ERR_PTR(ret);
}

struct vs_gem_object *vs_gem_create_object(struct drm_device *dev, size_t size)
{
	struct vs_gem_object *vs_obj;
	int ret;

	size = PAGE_ALIGN(size);

	vs_obj = vs_gem_alloc_object(dev, size);
	if (IS_ERR(vs_obj))
		return vs_obj;

	ret = vs_gem_alloc_buf(vs_obj);
	if (ret) {
		drm_gem_object_release(&vs_obj->base);
		kfree(vs_obj);
		return ERR_PTR(ret);
	}

	return vs_obj;
}

static struct vs_gem_object *vs_gem_create_with_handle(struct drm_device *dev,
						       struct drm_file *file, size_t size,
						       unsigned int *handle)
{
	struct vs_gem_object *vs_obj;
	struct drm_gem_object *obj;
	int ret;

	vs_obj = vs_gem_create_object(dev, size);
	if (IS_ERR(vs_obj))
		return vs_obj;

	obj = &vs_obj->base;

	ret = drm_gem_handle_create(file, obj, handle);

	drm_gem_object_put(obj);

	if (ret)
		return ERR_PTR(ret);

	return vs_obj;
}

static int vs_gem_mmap_obj(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct vs_gem_object *vs_obj = to_vs_gem_object(obj);
	struct drm_device *drm_dev = vs_obj->base.dev;
	unsigned long vm_size;

	vm_size = vma->vm_end - vma->vm_start;
	if (vm_size > vs_obj->size)
		return -EINVAL;

	vma->vm_pgoff = 0;

	vm_flags_clear(vma, VM_PFNMAP);

	return dma_mmap_attrs(to_dma_dev(drm_dev), vma, vs_obj->cookie, vs_obj->dma_addr,
			      vs_obj->size, vs_obj->dma_attrs);
}

static int vs_gem_prime_vmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	struct vs_gem_object *vs_obj = to_vs_gem_object(obj);

	if (obj->import_attach)
		return dma_buf_vmap(obj->dma_buf, map);

	if (!vs_obj->cookie)
		return -ENOMEM;

	iosys_map_set_vaddr(map, vs_obj->cookie);

	return 0;
}

static void vs_gem_prime_vunmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	if (obj->import_attach)
		dma_buf_vunmap(obj->dma_buf, map);
}

static int vs_gem_dmabuf_vmap(struct dma_buf *dma_buf, struct iosys_map *map)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return vs_gem_prime_vmap(obj, map);
}

static void vs_gem_dmabuf_vunmap(struct dma_buf *dma_buf, struct iosys_map *map)
{
	struct drm_gem_object *obj = dma_buf->priv;

	vs_gem_prime_vunmap(obj, map);
}

static const struct dma_buf_ops vs_dmabuf_ops = {
	.map_dma_buf = drm_gem_map_dma_buf,
	.unmap_dma_buf = drm_gem_unmap_dma_buf,
	.vmap = vs_gem_dmabuf_vmap,
	.vunmap = vs_gem_dmabuf_vunmap,
	.mmap = drm_gem_dmabuf_mmap,
	.release = drm_gem_dmabuf_release,
};

struct dma_buf *vs_gem_prime_export(struct drm_gem_object *obj, int flags)
{
	struct drm_device *dev = obj->dev;
	struct dma_buf_export_info exp_info = {
		.exp_name = KBUILD_MODNAME,
		.owner = dev->driver->fops->owner,
		.ops = &vs_dmabuf_ops,
		.size = obj->size,
		.flags = flags,
		.priv = obj,
		.resv = obj->resv,
	};

	return drm_gem_dmabuf_export(dev, &exp_info);
}

static const struct vm_operations_struct vs_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct drm_gem_object_funcs vs_gem_default_funcs = {
	.free = vs_gem_free_object,
	.export = vs_gem_prime_export,
	.vmap = vs_gem_prime_vmap,
	.vunmap = vs_gem_prime_vunmap,
	.vm_ops = &vs_vm_ops,
};

int vs_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		       struct drm_mode_create_dumb *args)
{
	struct vs_drm_private *priv = dev->dev_private;
	struct vs_gem_object *vs_obj;
	unsigned int pitch = DIV_ROUND_UP(args->width * args->bpp, 8);

	if (args->bpp % 10)
		args->pitch = ALIGN(pitch, priv->pitch_alignment);
	else
		/* for custom 10bit format with no bit gaps */
		args->pitch = pitch;

	args->size = PAGE_ALIGN(args->pitch * args->height);

	vs_obj = vs_gem_create_with_handle(dev, file, args->size, &args->handle);

	return PTR_ERR_OR_ZERO(vs_obj);
}

struct drm_gem_object *vs_gem_prime_import(struct drm_device *dev, struct dma_buf *dma_buf)
{
	return drm_gem_prime_import_dev(dev, dma_buf, to_dma_dev(dev));
}

struct drm_gem_object *vs_gem_prime_import_sg_table(struct drm_device *dev,
						    struct dma_buf_attachment *attach,
						    struct sg_table *sgt)
{
	struct vs_gem_object *vs_obj;
	int ret;
	struct scatterlist *s;
	u32 i;
	dma_addr_t expected;
	size_t size = attach->dmabuf->size;

	size = PAGE_ALIGN(size);

	vs_obj = vs_gem_alloc_object(dev, size);
	if (IS_ERR(vs_obj))
		return ERR_CAST(vs_obj);

	expected = sg_dma_address(sgt->sgl);
	for_each_sg(sgt->sgl, s, sgt->nents, i) {
		if (sg_dma_address(s) != expected) {
			DRM_DEV_ERROR(dev->dev, "sg_table is not contiguous");
			ret = -EINVAL;
			goto err;
		}

		if (sg_dma_len(s) & (PAGE_SIZE - 1)) {
			ret = -EINVAL;
			goto err;
		}

		if (i == 0)
			vs_obj->iova = (u64)sg_dma_address(s);

		expected = sg_dma_address(s) + sg_dma_len(s);
	}

	vs_obj->dma_addr = sg_dma_address(sgt->sgl);

	vs_obj->sgt = sgt;

	return &vs_obj->base;

err:
	vs_gem_free_object(&vs_obj->base);

	return ERR_PTR(ret);
}

int vs_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_gem_object *obj;
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	obj = vma->vm_private_data;
	if (!obj)
		return -EINVAL;

	if (obj->import_attach)
		return dma_buf_mmap(obj->dma_buf, vma, 0);

	return vs_gem_mmap_obj(obj, vma);
}

static int query_handle(struct drm_device *dev, struct drm_vs_gem_query_info *info,
			struct drm_file *file)
{
	struct drm_gem_object *obj;
	struct vs_gem_object *vs_obj;

	obj = drm_gem_object_lookup(file, info->handle);
	if (!obj) {
		dev_err(dev->dev, "Failed to GEM object with handle %#x.\n", info->handle);
		return -ENXIO;
	}

	vs_obj = to_vs_gem_object(obj);
	info->data = vs_obj->iova;
	drm_gem_object_put(obj);

	return 0;
}

int vs_gem_query_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_vs_gem_query_info *info = data;

	switch (info->type) {
	case VS_GEM_QUERY_HANDLE:
		return query_handle(dev, info, file);
	default:
		dev_err(dev->dev, "Unknown type %#x.\n", info->type);
		break;
	}

	return -EINVAL;
}
