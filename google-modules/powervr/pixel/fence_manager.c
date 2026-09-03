// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Jack Diver <diverj@google.com>
 */
#include <linux/sync_file.h>
#include <linux/uaccess.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>

#include <iif/iif-dma-fence.h>
#include <iif/iif-fence.h>
#include <iif/iif-manager.h>

#include "sysconfig.h"
#include "fence_manager.h"
#include "mba.h"

/**
 * get_iif_manager - Traverse device node tree to get an iif_manager handle
 *
 * @pixel_dev The pixel_gpu_device
 *
 * Returns a handle to the global iif_manager on success, otherwise NULL
 */
static struct iif_manager *get_iif_manager(struct pixel_gpu_device *pixel_dev)
{
	struct iif_manager *iif_mgr = NULL;
	struct device_node *iif_node;
	struct platform_device *pdev;

	iif_node = of_parse_phandle(pixel_dev->dev->of_node, "iif-device", 0);
	if (IS_ERR_OR_NULL(iif_node)) {
		dev_err(pixel_dev->dev, "There is no iif-device node in the device tree");
		goto exit;
	}

	pdev = of_find_device_by_node(iif_node);
	of_node_put(iif_node);

	if (!pdev) {
		dev_err(pixel_dev->dev, "Could not find iif-device");
		goto exit;
	}

	iif_mgr = platform_get_drvdata(pdev);
	if (iif_mgr)
		iif_manager_get(iif_mgr);
	put_device(&pdev->dev);

exit:
	return iif_mgr;
}

void fence_manager_iif_retire(struct pixel_gpu_device *pixel_dev,
			      union pixel_rgxfwif_iif_handle const *handle)
{
	struct iif_fence *iif;

	iif = iif_manager_get_fence_from_id(pixel_dev->iif_mgr, handle->id);

	if (WARN_ON_ONCE(!iif))
		return;

	iif_fence_waited(iif, IIF_IP_GPU);

	/* iif_manager_get_fence_from_id takes an additional ref to the iif, drop twice */
	iif_fence_put(iif);
	iif_fence_put(iif);
}

/**
 * fence_manager_extract_iif - Extract an IIF handle from a dma_fence
 *
 * @pixel_dev The pixel_gpu_device
 * @fence     The dma_fence
 *
 * Returns a `struct pixel_rgxfwif_iif_handle` encoded as a u32
 */
union pixel_rgxfwif_iif_handle fence_manager_extract_iif(
	struct pixel_gpu_device *pixel_dev, struct dma_fence *fence)
{
	/* This ref is held until the sync checkpoint is retired */
	struct iif_fence *iif = dma_iif_fence_get_base(fence);

	if (!iif)
		return (union pixel_rgxfwif_iif_handle) {0};

	iif_fence_submit_waiter(iif, IIF_IP_GPU);

	/* Hold a ref to tie the iif lifetime with the sync checkpoint */
	return (union pixel_rgxfwif_iif_handle) {
		.valid = 1,
		.id = iif_fence_get(iif)->id,
	};
}

/**
 * signal_gpu_local_mba - Write the local payload ID to the GPU MBA
 *
 * @fence Unused
 * @data  Opaque handle to the pixel_gpu_device
 *
 * This function is intended to be registered as a `fence_unblocked` callback
 */
static void signal_gpu_local_mba(struct iif_fence *fence, void *data)
{
	struct pixel_gpu_device *pixel_dev = data;

	if (fence->signal_error)
		dev_warn(pixel_dev->dev, "IIF has been unblocked with an error, id=%d, error=%d",
			 fence->id, fence->signal_error);

	if (!fence->propagate)
		return;

	/* If the sswrp is up, take a PM ref to prevent it from powering down*/
	if (pm_runtime_get_if_active(pixel_dev->sswrp_gpu_pd)) {
		/* Only write to the MBA if the sswrp is up.
		 * If the sswrp is down, then the iif-blocked workload has not yet been submitted
		 * to the GPU, and the MBA message is not required.
		 */
		mba_signal(pixel_dev, 0);
		pm_runtime_put(pixel_dev->sswrp_gpu_pd);
	}
}

static const struct iif_manager_ops gpu_iif_ops = {
	.fence_unblocked = signal_gpu_local_mba,
};

/**
 * fence_manager_init - Register fence ops
 *
 * @pixel_dev The pixel_gpu_device
 *
 * Returns 0 on success, or an error code
 */
int fence_manager_init(struct pixel_gpu_device *pixel_dev)
{
	int err = -ENODEV;

	pixel_dev->iif_mgr = get_iif_manager(pixel_dev);

	if (!pixel_dev->iif_mgr) {
		dev_err(pixel_dev->dev, "Failed to get iif_manager handle");
		goto exit;
	}

	iif_manager_register_ops(pixel_dev->iif_mgr, IIF_IP_GPU, &gpu_iif_ops, pixel_dev);

	err = 0;
exit:
	return err;
}

/**
 * fence_manager_term - Unregister fence ops
 *
 * @pixel_dev The pixel_gpu_device
 */
void fence_manager_term(struct pixel_gpu_device *pixel_dev)
{
	if (pixel_dev->iif_mgr) {
		iif_manager_unregister_ops(pixel_dev->iif_mgr, IIF_IP_GPU);
		iif_manager_put(pixel_dev->iif_mgr);
	}
}

MODULE_SOFTDEP("pre: iif");
