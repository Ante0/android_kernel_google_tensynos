/*
 * @File
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* This is the interface we're implementing */
#include "drm_nulldisp_gem.h"

#include "drm_pdp_gem.h"
#include "nulldisp_drm.h"
#include "pdp_drm.h"

/* Upstream headers */
#include <drm/drm_file.h>
#include <drm/drm_print.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0))
const struct vm_operations_struct nulldisp_gem_vm_ops = {
	.fault = pdp_gem_object_vm_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};
#endif

int nulldisp_gem_object_create_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file)
{
	struct drm_nulldisp_gem_create *args = data;
	struct drm_pdp_gem_create pdp_args;
	int err;

	if (args->flags) {
		DRM_ERROR("invalid flags: %#08x\n", args->flags);
		return -EINVAL;
	}

	if (args->handle) {
		DRM_ERROR("invalid handle (this should always be 0)\n");
		return -EINVAL;
	}

	/*
	 * Remapping of nulldisp create args to pdp create args.
	 *
	 * Note: even though the nulldisp and pdp args are identical
	 * in this case, they may potentially change in future.
	 */
	pdp_args.size = args->size;
	pdp_args.flags = args->flags;
	pdp_args.handle = args->handle;

	err = pdp_gem_object_create_ioctl(dev, &pdp_args, file);
	if (!err)
		args->handle = pdp_args.handle;

	return err;
}

int nulldisp_gem_object_mmap_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file)
{
	struct drm_nulldisp_gem_mmap *args = data;
	struct drm_pdp_gem_mmap pdp_args;
	int err;

	pdp_args.handle = args->handle;
	pdp_args.pad = args->pad;
	pdp_args.offset = args->offset;

	err = pdp_gem_object_mmap_ioctl(dev, &pdp_args, file);

	if (!err)
		args->offset = pdp_args.offset;

	return err;
}

int nulldisp_gem_object_cpu_prep_ioctl(struct drm_device *dev, void *data,
				       struct drm_file *file)
{
	struct drm_nulldisp_gem_cpu_prep *args =
		(struct drm_nulldisp_gem_cpu_prep *)data;
	struct drm_pdp_gem_cpu_prep pdp_args;

	pdp_args.handle = args->handle;
	pdp_args.flags = args->flags;

	return pdp_gem_object_cpu_prep_ioctl(dev, &pdp_args, file);
}

int nulldisp_gem_object_cpu_fini_ioctl(struct drm_device *dev, void *data,
				       struct drm_file *file)
{
	struct drm_nulldisp_gem_cpu_fini *args =
		(struct drm_nulldisp_gem_cpu_fini *)data;
	struct drm_pdp_gem_cpu_fini pdp_args;

	pdp_args.handle = args->handle;
	pdp_args.pad = args->pad;

	return pdp_gem_object_cpu_fini_ioctl(dev, &pdp_args, file);
}

struct drm_gem_object *
nulldisp_gem_prime_import_sg_table(struct drm_device *dev,
				   struct dma_buf_attachment *attach,
				   struct sg_table *sgt)
{
	return pdp_gem_prime_import_sg_table(dev, attach, sgt);
}

struct drm_gem_object *nulldisp_gem_prime_import(struct drm_device *dev,
						 struct dma_buf *dma_buf)
{
	return pdp_gem_prime_import(dev, dma_buf);
}

int nulldisp_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
			     struct drm_mode_create_dumb *args)
{
	return pdp_gem_dumb_create(file, dev, args);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
struct dma_buf *nulldisp_gem_prime_export(struct drm_device *dev,
					  struct drm_gem_object *obj, int flags)
{
	return pdp_gem_prime_export(dev, obj, flags);
}

void nulldisp_gem_object_free(struct drm_gem_object *obj)
{
	pdp_gem_object_free(obj);
}
#endif

int nulldisp_gem_init_devres(struct drm_device *dev)
{
	return pdp_gem_init_devres(dev);
}
