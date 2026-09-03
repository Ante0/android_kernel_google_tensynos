/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_GEM_H__
#define __VS_GEM_H__

#include <linux/dma-buf.h>
#include <drm/drm_gem.h>
#include <drm/drm_prime.h>

#include "vs_drv.h"

/**
 * struct vs_gem_object - Display driver GEM object.
 * @base: The base DRM GEM object.
 * @size: The size of the allocated buffer, page-aligned.
 * @cookie: Opaque cookie used for the contiguous (non-IOMMU) allocation path.
 *          Represents the kernel virtual address returned by dma_alloc_attrs().
 *          Set to NULL for IOMMU-based non-contiguous allocations.
 * @dma_addr: The DMA bus address of the buffer, accessible by the device.
 * @iova: The I/O virtual address for this object.
 * @dma_attrs: DMA attributes used for the contiguous (non-IOMMU) allocation path.
 * @sgt: Scatter-gather table for the buffer. Required for all objects to support
 *       on-demand mapping via dma_vmap_noncontiguous() and dma_mmap_noncontiguous().
 */
struct vs_gem_object {
	struct drm_gem_object base;

	size_t size;
	void *cookie;
	dma_addr_t dma_addr;
	u64 iova;
	unsigned long dma_attrs;
	struct sg_table *sgt;
};

/**
 * to_vs_gem_object - Cast a drm_gem_object to a vs_gem_object.
 * @obj: The drm_gem_object to cast.
 *
 * Return: A pointer to the containing vs_gem_object.
 */
static inline struct vs_gem_object *to_vs_gem_object(struct drm_gem_object *obj)
{
	return container_of(obj, struct vs_gem_object, base);
}

/**
 * vs_gem_create_object - Create a new GEM object with a DMA buffer.
 * @dev: The DRM device.
 * @size: The desired size of the buffer.
 *
 * This function creates a display driver GEM object, including allocating
 * the backing DMA buffer. The size is page-aligned before allocation.
 *
 * Return: A pointer to the new vs_gem_object on success, or an ERR_PTR on failure.
 */
struct vs_gem_object *vs_gem_create_object(struct drm_device *dev, size_t size);

/**
 * vs_gem_free_object - Free a vs_gem_object.
 * @obj: The drm_gem_object to free.
 *
 * This is the .free callback for GEM objects. It handles freeing both locally
 * allocated buffers and buffers imported via PRIME. It then releases the base
 * GEM object and frees the container vs_gem_object.
 */
void vs_gem_free_object(struct drm_gem_object *obj);

/**
 * vs_gem_dumb_create - Create a dumb buffer.
 * @file_priv: The DRM file private data for the client.
 * @drm: The DRM device.
 * @args: Arguments for creating the dumb buffer.
 *
 * This function implements the DUMB_CREATE ioctl. It calculates the required
 * pitch and size based on the requested width, height, and bpp, then creates
 * a GEM object with a handle for userspace.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int vs_gem_dumb_create(struct drm_file *file_priv, struct drm_device *drm,
		       struct drm_mode_create_dumb *args);

/**
 * vs_gem_mmap - Memory map a GEM object.
 * @filp: The file pointer.
 * @vma: The user's virtual memory area.
 *
 * This is the top-level mmap handler. It calls the generic drm_gem_mmap helper
 * and then dispatches to the appropriate backend (dma-buf or local) for the
 * actual mapping.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int vs_gem_mmap(struct file *filp, struct vm_area_struct *vma);

/**
 * vs_gem_prime_import - Import a dma-buf as a GEM object.
 * @dev: The DRM device.
 * @dma_buf: The dma-buf to import.
 *
 * This is a wrapper around drm_gem_prime_import_dev to specify the correct
 * DMA device for the import operation.
 *
 * Return: A pointer to the new drm_gem_object on success, or an ERR_PTR on failure.
 */
struct drm_gem_object *vs_gem_prime_import(struct drm_device *dev, struct dma_buf *dma_buf);

/**
 * vs_gem_prime_import_sg_table - Import an external sg_table as a GEM object.
 * @dev: The DRM device.
 * @attach: The dma-buf attachment.
 * @sgt: The scatter-gather table describing the buffer memory.
 *
 * This function creates a new GEM object from an existing sg_table. It verifies
 * that the memory is contiguous, as required by the display hardware.
 *
 * Return: A pointer to the new drm_gem_object on success, or an ERR_PTR on failure.
 */
struct drm_gem_object *vs_gem_prime_import_sg_table(struct drm_device *dev,
						    struct dma_buf_attachment *attach,
						    struct sg_table *sgt);

/**
 * vs_gem_prime_export - Export a GEM object as a dma-buf.
 * @obj: The GEM object to export.
 * @flags: Export flags (e.g., O_CLOEXEC).
 *
 * This function exports a GEM object as a dma-buf, enabling cross-device
 * buffer sharing. It utilizes custom dma_buf_ops to ensure that any kernel
 * importer receives a valid virtual address via on-demand mapping.
 *
 * Return: A pointer to the new dma_buf on success, or an ERR_PTR on failure.
 */
struct dma_buf *vs_gem_prime_export(struct drm_gem_object *obj, int flags);

/**
 * vs_gem_query_ioctl - Handle the GEM_QUERY ioctl.
 * @dev: The DRM device.
 * @data: Pointer to the ioctl data.
 * @file: The DRM file private data for the client.
 *
 * This function is the entry point for the VS_GEM_QUERY ioctl. It dispatches
 * the request to the appropriate handler based on the query type.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int vs_gem_query_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
#endif /* __VS_GEM_H__ */
