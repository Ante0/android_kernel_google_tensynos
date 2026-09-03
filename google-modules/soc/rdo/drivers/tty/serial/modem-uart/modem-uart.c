// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC.
 *
 */

#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <soc/google/goog_gdmc_service_ids.h>
#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_mba_nq_xport.h>
#include <uapi/misc/google-modem-uart.h>

#include "modem-uart.h"
#include "modem_uart_service.h"

static int64_t modem_uart_handle_mailbox_error(struct device *dev, int message_res,
					       uint32_t *header)
{
	if (goog_mba_nq_xport_get_error(header)) {
		/* GDMC firmware error code is int16_t, alignment is required */
		int16_t gdmc_error = (int16_t)(goog_mba_nq_xport_get_data(header) & GENMASK(15, 0));

		return gdmc_error;
	}

	return message_res;
}

static int64_t modem_uart_send_mba_request(struct device *dev,
					   enum gdmc_mba_modem_uart_op_type request)
{
	struct gdmc_mba_modem_uart_msg msg;
	int message_res;
	struct modem_uart_base *base = dev_get_drvdata(dev);

	msg.header = goog_mba_nq_xport_create_hdr(GDMC_MBA_SERVICE_ID_MODEM_UART, request);
	if (request == GDMC_MBA_MODEM_UART_START) {
		msg.payload.send_start_req.pa_low = (uint32_t)base->memory_region.paddr;
		msg.payload.send_start_req.pa_high = (uint32_t)(base->memory_region.paddr >> 32);
		msg.payload.send_start_req.size = base->memory_region.size;
	}
	message_res = gdmc_send_message(base->gdmc_iface, &msg);

	return modem_uart_handle_mailbox_error(dev, message_res, &msg.header);
}

static int64_t modem_uart_send_get_tail_offset_request(struct device *dev, uint32_t *offset)
{
	struct modem_uart_base *base = dev_get_drvdata(dev);
	struct gdmc_mba_modem_uart_msg msg;
	int message_res;
	int64_t ret;

	msg.header = goog_mba_nq_xport_create_hdr(GDMC_MBA_SERVICE_ID_MODEM_UART,
						  GDMC_MBA_MODEM_UART_GET_TAIL_OFFSET);
	message_res = gdmc_send_message(base->gdmc_iface, &msg);

	ret = modem_uart_handle_mailbox_error(dev, message_res, &msg.header);
	if (ret == 0)
		*offset = msg.payload.get_tail_offset_res.offset;
	return ret;
}

static long modem_uart_ioc_start(struct device *dev)
{
	struct modem_uart_base *base = dev_get_drvdata(dev);
	long ret = 0;

	if (base->char_dev.logging_enabled == 1)
		return 0;

	ret = modem_uart_send_mba_request(base->dev, GDMC_MBA_MODEM_UART_START);
	if (ret != 0)
		return ret;

	base->char_dev.logging_enabled = 1;
	base->memory_region.head = 0;

	return 0;
}

static long modem_uart_ioc_stop(struct device *dev)
{
	struct modem_uart_base *base = dev_get_drvdata(dev);
	long ret;

	if (base->char_dev.logging_enabled == 0)
		return 0;

	ret = modem_uart_send_mba_request(base->dev, GDMC_MBA_MODEM_UART_STOP);
	if (ret == 0)
		base->char_dev.logging_enabled = 0;

	return ret;
}

static long modem_uart_cdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct modem_uart_base *base = file->private_data;
	long ret = -ENOTTY;

	mutex_lock(&base->char_dev.cdev_lock);
	switch (cmd) {
	case MODEM_UART_IOC_START:
		ret = modem_uart_ioc_start(base->dev);
		break;

	case MODEM_UART_IOC_STOP:
		ret = modem_uart_ioc_stop(base->dev);
		break;

	case MODEM_UART_IOC_CLEAR:
		ret = modem_uart_send_mba_request(base->dev, GDMC_MBA_MODEM_UART_CLEAR);
		if (ret == 0)
			base->memory_region.head = 0;
		break;

	case MODEM_UART_IOC_STATUS:
		ret = put_user(base->char_dev.logging_enabled, (int __user *)arg);
		break;

	default:
		break;
	}
	mutex_unlock(&base->char_dev.cdev_lock);

	return ret;
}

static ssize_t modem_uart_cdev_read(struct file *filp, char __user *buf, size_t size, loff_t *pos)
{
	struct modem_uart_base *base = filp->private_data;
	uint32_t tail = 0;
	size_t available = 0;
	size_t to_copy;
	int64_t ret;

	mutex_lock(&base->char_dev.cdev_lock);

	if (base->memory_region.vaddr == NULL) {
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = modem_uart_send_get_tail_offset_request(base->dev, &tail);
	if (ret != 0) {
		dev_err(base->dev, "Failed to get tail offset: %lld.\n", ret);
		goto out_unlock;
	}

	/* The buffer is linear */
	if (tail > base->memory_region.head && tail <= base->memory_region.size)
		available = tail - base->memory_region.head;

	to_copy = min_t(size_t, size, available);
	if (to_copy == 0) {
		ret = 0;
		goto out_unlock;
	}

	/*
	 * Ensure the tail pointer update from the mailbox is visible before
	 * we begin reading the actual data from the reserved memory region.
	 */
	rmb();

	if (copy_to_user(buf, (uint8_t *)base->memory_region.vaddr + base->memory_region.head,
			 to_copy)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	base->memory_region.head += to_copy;
	ret = to_copy;

out_unlock:
	mutex_unlock(&base->char_dev.cdev_lock);
	return ret;
}

static int modem_uart_cdev_open(struct inode *inode, struct file *file)
{
	struct modem_uart_character_device *char_dev =
		container_of(inode->i_cdev, struct modem_uart_character_device, cdev);
	struct modem_uart_base *base = container_of(char_dev, struct modem_uart_base, char_dev);

	file->private_data = base;

	return 0;
}

static int modem_uart_cdev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations modem_uart_chardev_fops = {
	.owner = THIS_MODULE,
	.open = modem_uart_cdev_open,
	.release = modem_uart_cdev_release,
	.unlocked_ioctl = modem_uart_cdev_ioctl,
	.read = modem_uart_cdev_read,
};

static void modem_uart_destroy_chardev(struct device *dev)
{
	struct modem_uart_base *base = dev_get_drvdata(dev);

	cdev_del(&base->char_dev.cdev);
	class_destroy(base->char_dev.class);
	unregister_chrdev_region(base->char_dev.devt, 1);
}

static int modem_uart_create_chardev(struct device *dev)
{
	struct modem_uart_base *base = dev_get_drvdata(dev);
	struct device *cdev_node;
	int ret;

	ret = alloc_chrdev_region(&base->char_dev.devt, 0, 1, KBUILD_MODNAME);
	if (ret) {
		dev_err(dev, "Failed to allocate char device region\n");
		return ret;
	}

	base->char_dev.class = class_create(KBUILD_MODNAME);
	if (IS_ERR(base->char_dev.class)) {
		ret = PTR_ERR(base->char_dev.class);
		dev_err(dev, "Failed to create device class\n");
		goto err_class_create;
	}

	cdev_init(&base->char_dev.cdev, &modem_uart_chardev_fops);
	base->char_dev.cdev.owner = THIS_MODULE;

	ret = cdev_add(&base->char_dev.cdev, base->char_dev.devt, 1);
	if (ret) {
		dev_err(dev, "Failed to add modem-uart cdev\n");
		goto err_cdev_add;
	}

	cdev_node =
		device_create(base->char_dev.class, dev, base->char_dev.devt, NULL, KBUILD_MODNAME);
	if (IS_ERR(cdev_node)) {
		ret = PTR_ERR(cdev_node);
		dev_err(dev, "Failed to create character device file\n");
		goto err_device_create;
	}

	mutex_init(&base->char_dev.cdev_lock);

	return 0;

err_device_create:
	cdev_del(&base->char_dev.cdev);
err_cdev_add:
	class_destroy(base->char_dev.class);
err_class_create:
	unregister_chrdev_region(base->char_dev.devt, 1);
	return ret;
}

static int modem_uart_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct modem_uart_base *base;
	struct device_node *rmem_node;
	struct reserved_mem *rmem;
	u32 offset = 0;
	u32 size = 0;
	int ret;

	base = devm_kzalloc(dev, sizeof(struct modem_uart_base), GFP_KERNEL);
	if (!base)
		return -ENOMEM;
	platform_set_drvdata(pdev, base);

	/* Store device pointer for use in char device context */
	base->dev = dev;

	base->gdmc_iface = gdmc_iface_get(dev);
	if (IS_ERR(base->gdmc_iface)) {
		dev_err(dev, "Failed to get GDMC interface\n");
		return PTR_ERR(base->gdmc_iface);
	}

	rmem_node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!rmem_node) {
		dev_err(dev, "Failed to parse memory-region phandle\n");
		ret = -ENODEV;
		goto err_put_iface;
	}

	rmem = of_reserved_mem_lookup(rmem_node);
	of_node_put(rmem_node);
	if (!rmem) {
		dev_err(dev, "Failed to lookup reserved memory\n");
		ret = -ENODEV;
		goto err_put_iface;
	}

	ret = of_property_read_u32(dev->of_node, "buffer-offset", &offset);
	if (ret) {
		dev_err(dev, "Failed to read buffer-offset property\n");
		goto err_put_iface;
	}

	ret = of_property_read_u32(dev->of_node, "buffer-size", &size);
	if (ret) {
		dev_err(dev, "Failed to read buffer-size property\n");
		goto err_put_iface;
	}

	if (offset + size > rmem->size) {
		dev_err(dev, "Offset and size exceed reserved memory bounds\n");
		ret = -EINVAL;
		goto err_put_iface;
	}

	base->memory_region.size = size;
	base->memory_region.paddr = rmem->base + offset;

	base->memory_region.vaddr = devm_memremap(dev,
						  base->memory_region.paddr,
						  size, MEMREMAP_WC);
	if (!base->memory_region.vaddr) {
		dev_err(dev, "Failed to map reserved memory\n");
		ret = -ENOMEM;
		goto err_put_iface;
	}

	ret = modem_uart_create_chardev(dev);
	if (ret != 0)
		goto err_put_iface;

	return 0;

err_put_iface:
	gdmc_iface_put(base->gdmc_iface);
	return ret;
}

static void modem_uart_remove(struct platform_device *pdev)
{
	struct modem_uart_base *base = platform_get_drvdata(pdev);

	mutex_lock(&base->char_dev.cdev_lock);
	modem_uart_ioc_stop(&pdev->dev);
	mutex_unlock(&base->char_dev.cdev_lock);

	modem_uart_destroy_chardev(&pdev->dev);

	gdmc_iface_put(base->gdmc_iface);
}

static const struct of_device_id modem_uart_of_match[] = {
	{ .compatible = "google,modem-uart" },
	{},
};
MODULE_DEVICE_TABLE(of, modem_uart_of_match);

static struct platform_driver modem_uart_driver = {
	.probe = modem_uart_probe,
	.remove = modem_uart_remove,
	.driver = {
		.name  = "modem-uart",
		.of_match_table = of_match_ptr(modem_uart_of_match),
	},
};
module_platform_driver(modem_uart_driver);

MODULE_AUTHOR("Kanstantsin Yarmash <kyarmash@google.com>");
MODULE_DESCRIPTION("Google Modem UART");
MODULE_LICENSE("GPL");
