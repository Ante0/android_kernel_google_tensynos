// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_mba_cpm_iface.h>

#include "fwmt_driver.h"
#include "fwmt_service.h"

#define DEVICE_NUMBER 2

/**
 * struct fwmt_driver - The core FWMT platform driver state.
 *
 * @dev_gdmc: GDMC FWMT device subclass.
 * @dev_cpm: CPM FWMT device subclass.
 * @dev_legacy: Legacy FWMT device subclass that points to GDMC.
 * @buffer_gdmc: Memory buffer for GDMC communication.
 * @buffer_cpm: Memory buffer for CPM communication.
 * @fwmt_class: FWMT character device class.
 */
struct fwmt_driver {
	struct fwmt_dev_gdmc dev_gdmc;
	struct fwmt_dev_cpm dev_cpm;
	struct fwmt_dev_gdmc dev_legacy;

	struct fwmt_mba_buffer buffer_gdmc;
	struct fwmt_mba_buffer buffer_cpm;

	struct class *fwmt_class;
};

/**
 * fwmt_dev_init - Initializes an FWMT device and its character nodes.
 * @dev: The FWMT device to initialize.
 * @parent: The kernel device.
 * @class: Character device class.
 * @name: Name of the subsystem.
 * @send_req: Function to send the FWMT request via the MBA.
 * @buffer: Shared DMA memory buffer.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int fwmt_dev_init(struct fwmt_dev *dev, struct device *parent, struct class *class,
			 const char *name, fwmt_send_req_t send_req, struct fwmt_mba_buffer *buffer)
{
	int ret;
	char name_buf[64];

	dev->dev = parent;
	dev->name = name;
	dev->send_req = send_req;
	dev->buffer = buffer;

	ret = alloc_chrdev_region(&dev->devt, 0, DEVICE_NUMBER, KBUILD_MODNAME);
	if (ret) {
		dev_err(dev->dev, "Failed to allocate a major number.\n");
		return ret;
	}

	if (dev->name) {
		snprintf(name_buf, sizeof(name_buf), "%s_%s_strings", KBUILD_MODNAME, dev->name);
	} else {
		/* Legacy node. */
		snprintf(name_buf, sizeof(name_buf), "%s_strings", KBUILD_MODNAME);
	}
	ret = fwmt_cdev_create(dev, &dev->cdev_strings, class,
			       MKDEV(MAJOR(dev->devt), kFwmtMsgTypeRetrieveString),
			       kFwmtMsgTypeRetrieveString, name_buf);
	if (ret)
		return ret;

	if (dev->name) {
		snprintf(name_buf, sizeof(name_buf), "%s_%s_metrics", KBUILD_MODNAME, dev->name);
	} else {
		/* Legacy node. */
		snprintf(name_buf, sizeof(name_buf), "%s_metrics", KBUILD_MODNAME);
	}
	ret = fwmt_cdev_create(dev, &dev->cdev_metrics, class,
			       MKDEV(MAJOR(dev->devt), kFwmtMsgTypeRetrieveMetric),
			       kFwmtMsgTypeRetrieveMetric, name_buf);
	if (ret)
		return ret;

	return 0;
}

/**
 * fwmt_dev_remove - Cleans up an FWMT device and its character nodes.
 * @dev: The FWMT device to remove.
 * @class: Shared character device class.
 */
static void fwmt_dev_remove(struct fwmt_dev *dev, struct class *class)
{
	fwmt_cdev_destroy(&dev->cdev_metrics, class);
	fwmt_cdev_destroy(&dev->cdev_strings, class);
	if (dev->devt)
		unregister_chrdev_region(dev->devt, DEVICE_NUMBER);
}

/**
 * fwmt_probe - Probes the FWMT platform device.
 * @pdev: The platform device to probe.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int fwmt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwmt_driver *drv;
	struct device_node *rmem_node;
	struct reserved_mem *rmem;
	struct gdmc_iface *gdmc_iface = NULL;
	struct cpm_iface_client *cpm_iface = NULL;
	u32 offset = 0;
	u32 size = 0;
	u32 half_size;
	void *vaddr;
	int ret;

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	mutex_init(&drv->buffer_gdmc.lock);
	mutex_init(&drv->buffer_cpm.lock);
	platform_set_drvdata(pdev, drv);

	gdmc_iface = gdmc_iface_get(dev);
	if (IS_ERR(gdmc_iface)) {
		ret = PTR_ERR(gdmc_iface);
		goto err;
	}

	cpm_iface = cpm_iface_request_client(dev, 0, NULL, NULL);
	if (IS_ERR(cpm_iface)) {
		ret = PTR_ERR(cpm_iface);
		goto err;
	}

	drv->fwmt_class = class_create(KBUILD_MODNAME);
	if (IS_ERR(drv->fwmt_class)) {
		dev_err(dev, "Failed to create device class.\n");
		ret = PTR_ERR(drv->fwmt_class);
		goto err;
	}

	rmem_node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!rmem_node) {
		dev_err(dev, "Failed to parse memory-region phandle\n");
		ret = -ENODEV;
		goto err;
	}

	rmem = of_reserved_mem_lookup(rmem_node);
	of_node_put(rmem_node);
	if (!rmem) {
		dev_err(dev, "Failed to lookup reserved memory\n");
		ret = -ENODEV;
		goto err;
	}

	ret = of_property_read_u32(dev->of_node, "buffer-offset", &offset);
	if (ret) {
		dev_err(dev, "Failed to read buffer-offset property\n");
		goto err;
	}

	ret = of_property_read_u32(dev->of_node, "buffer-size", &size);
	if (ret) {
		dev_err(dev, "Failed to read buffer-size property\n");
		goto err;
	}

	if (offset + size > rmem->size) {
		dev_err(dev, "Offset and size exceed reserved memory bounds\n");
		ret = -EINVAL;
		goto err;
	}

	/*
	 * MEM remap the assigned region.
	 * Note:
	 * This has to be the same for Modem UART driver, as it is
	 * within the same kernel page on 16KB build.
	 */
	vaddr = devm_memremap(dev, rmem->base + offset, size, MEMREMAP_WC);
	if (!vaddr) {
		dev_err(dev, "Failed to map reserved memory\n");
		ret = -ENOMEM;
		goto err;
	}

	/* The buffer is split between GDMC and CPM. */
	half_size = size / 2;
	if (half_size < sizeof(struct fwmt_msg_retrieve_response)) {
		dev_err(dev, "Buffer size too small\n");
		ret = -EINVAL;
		goto err;
	}

	/* GDMC buffer */
	drv->buffer_gdmc.size = min_t(u32, half_size, U16_MAX);
	drv->buffer_gdmc.paddr = rmem->base + offset;
	drv->buffer_gdmc.vaddr = vaddr;

	/* CPM buffer */
	drv->buffer_cpm.size = min_t(u32, half_size, U16_MAX);
	drv->buffer_cpm.paddr = rmem->base + offset + half_size;
	drv->buffer_cpm.vaddr = (u8 *)vaddr + half_size;

	/* Initialize the character devices. */
	drv->dev_gdmc.iface = gdmc_iface;
	ret = fwmt_dev_init(&drv->dev_gdmc.dev, dev, drv->fwmt_class, "gdmc", fwmt_gdmc_send_req,
			    &drv->buffer_gdmc);
	if (ret)
		goto err;

	drv->dev_cpm.iface = cpm_iface;
	ret = fwmt_dev_init(&drv->dev_cpm.dev, dev, drv->fwmt_class, "cpm", fwmt_cpm_send_req,
			    &drv->buffer_cpm);
	if (ret)
		goto err;

	drv->dev_legacy.iface = gdmc_iface;
	ret = fwmt_dev_init(&drv->dev_legacy.dev, dev, drv->fwmt_class, NULL, fwmt_gdmc_send_req,
			    &drv->buffer_gdmc);
	if (ret)
		goto err;

	return 0;

err:
	fwmt_dev_remove(&drv->dev_legacy.dev, drv->fwmt_class);
	fwmt_dev_remove(&drv->dev_cpm.dev, drv->fwmt_class);
	fwmt_dev_remove(&drv->dev_gdmc.dev, drv->fwmt_class);
	class_destroy(drv->fwmt_class);
	if (!IS_ERR_OR_NULL(cpm_iface))
		cpm_iface_free_client(cpm_iface);
	if (!IS_ERR_OR_NULL(gdmc_iface))
		gdmc_iface_put(gdmc_iface);
	return ret;
}

/**
 * fwmt_remove - Removes the FWMT platform device.
 * @pdev: The platform device to remove.
 */
static void fwmt_remove(struct platform_device *pdev)
{
	struct fwmt_driver *drv = platform_get_drvdata(pdev);

	fwmt_dev_remove(&drv->dev_legacy.dev, drv->fwmt_class);
	fwmt_dev_remove(&drv->dev_cpm.dev, drv->fwmt_class);
	fwmt_dev_remove(&drv->dev_gdmc.dev, drv->fwmt_class);
	class_destroy(drv->fwmt_class);
	cpm_iface_free_client(drv->dev_cpm.iface);
	gdmc_iface_put(drv->dev_gdmc.iface);
}

static const struct of_device_id fwmt_of_match[] = {
	{ .compatible = "google,fwmt-gdmc" },
	{},
};
MODULE_DEVICE_TABLE(of, fwmt_of_match);

static struct platform_driver fwmt_platform_driver = {
	.probe = fwmt_probe,
	.remove = fwmt_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = fwmt_of_match,
	},
};
module_platform_driver(fwmt_platform_driver);

MODULE_AUTHOR("Filip Konieczny <filipkonieczny@google.com>");
MODULE_DESCRIPTION("Firmware Metrics");
MODULE_LICENSE("GPL");
