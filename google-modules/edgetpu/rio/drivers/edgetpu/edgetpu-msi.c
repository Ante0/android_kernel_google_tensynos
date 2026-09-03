// SPDX-License-Identifier: GPL-2.0-only
/*
 * EdgeTPU MSI support.
 *
 * NOTE: For non-CMF architectures, there won't be any GIA-MSI software support and this edgetpu-msi
 * was introduced to initialize MSI and allocate IRQs at the TPU kernel driver.
 *
 * For CMF architectures, regardless of the existence of native GIA-MSI software support, as the MBA
 * interrupt set-up is handled in the MBA driver, this edgetpu-msi will be unlikely used. It will be
 * the GIA or MBA driver's responsibility to set up MSI.
 *
 * Copyright (C) 2026 Google LLC
 */

#include <linux/device.h>
#include <linux/msi.h>
#include <linux/platform_device.h>

#include "edgetpu-config.h"
#include "edgetpu-internal.h"
#include "edgetpu-msi.h"

static void edgetpu_msi_write_msi_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	/*
	 * The TPU firmware will configure GIA registers to write the physical ITS doorbell address
	 * and EventIDs. So the ITS/MSI information here is not used.
	 */
}

int edgetpu_msi_init(struct edgetpu_dev *etdev)
{
	struct edgetpu_msi *msi;
	int ret, virq, i;

	if (!of_find_property(etdev->dev->of_node, "msi-parent", NULL)) {
		/*
		 * If msi-parent is not present, then this device uses wired interrupts. We can
		 * return gracefully.
		 */
		return 0;
	}

	msi = devm_kzalloc(etdev->dev, sizeof(*msi), GFP_KERNEL);
	if (!msi)
		return -ENOMEM;

	/*
	 * Note: Set etdev->msi before `alloc_irqs()` so the `edgetpu_msi_write_msi_msg()` callback
	 * may access it in the future.
	 */
	etdev->msi = msi;

	ret = platform_device_msi_init_and_alloc_irqs(etdev->dev, EDGETPU_MAX_MSI_VECTORS,
						      edgetpu_msi_write_msi_msg);
	if (ret) {
		etdev_err(etdev, "Failed to allocate MSI IRQs: %d", ret);
		goto err_out;
	}

	for (i = 0; i < EDGETPU_MAX_MSI_VECTORS; i++) {
		virq = msi_get_virq(etdev->dev, i);
		if (!virq) {
			ret = -ENODEV;
			etdev_err(etdev, "Failed to get MSI IRQ for index %d", i);
			goto err_free_irqs;
		}

		msi->data[i].virq = virq;
	}

	return 0;

err_free_irqs:
	platform_device_msi_free_irqs_all(etdev->dev);
err_out:
	etdev->msi = NULL;

	return ret;
}

void edgetpu_msi_exit(struct edgetpu_dev *etdev)
{
	if (etdev->msi) {
		platform_device_msi_free_irqs_all(etdev->dev);
		etdev->msi = NULL;
	}
}

int edgetpu_msi_get_virq(struct edgetpu_dev *etdev, uint idx)
{
	if (!etdev->msi) {
		etdev_err(etdev, "MSI is not initialized");
		return -ENODEV;
	}

	if (idx >= EDGETPU_MAX_MSI_VECTORS) {
		etdev_err(etdev, "No valid MSI for index %d", idx);
		return -EINVAL;
	}

	return etdev->msi->data[idx].virq;
}
