// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPS Subsystem-coredump driver
 *
 * Copyright 2025 Google LLC
 */
#include "uapi/t900_gps_dump.h"
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/sscoredump.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define DRIVER_NAME "sscd_gps"
#define DEVICE_COUNT 1
#define SUBSYSTEM_NAME "gnss"

static dev_t dev_num;
static struct class *gpsdump_class;

enum gps_dump_state {
	GPS_DUMP_STATE_IDLE,
	GPS_DUMP_STATE_OPENED,
	GPS_DUMP_STATE_WAIT_FOR_TRANSFER,
	GPS_DUMP_STATE_COMPLETE,
	GPS_DUMP_STATE_UNKNOWN_ERROR,
};

struct t900_gps_dump_priv {
	struct cdev cdev;
	struct device *dev;
	struct platform_device *pdev;
	struct platform_device sscd_dev;
	struct sscd_platform_data sscd_pdata;
	struct mutex lock;
	enum gps_dump_state state;
	size_t total_coredump_size;
	size_t current_coredump_pos;
	void *coredump_buffer;
	struct gps_ssrdump_info *header_info;
};

static int t900_gps_dump_open(struct inode *inodep, struct file *filep)
{
	struct t900_gps_dump_priv *priv;
	int ret = 0;

	priv = container_of(inodep->i_cdev, struct t900_gps_dump_priv, cdev);
	filep->private_data = priv;

	mutex_lock(&priv->lock);
	if (priv->state != GPS_DUMP_STATE_IDLE) {
		ret = -EBUSY;
	} else {
		priv->state = GPS_DUMP_STATE_OPENED;
		priv->total_coredump_size = 0;
		priv->current_coredump_pos = 0;
		priv->coredump_buffer = NULL;
		priv->header_info = NULL;
	}
	mutex_unlock(&priv->lock);

	return ret;
}

static int t900_gps_dump_release(struct inode *inodep, struct file *filep)
{
	struct t900_gps_dump_priv *priv = filep->private_data;

	mutex_lock(&priv->lock);

	kvfree(priv->coredump_buffer);
	priv->coredump_buffer = NULL;

	kfree(priv->header_info);
	priv->header_info = NULL;

	priv->state = GPS_DUMP_STATE_IDLE;
	priv->total_coredump_size = 0;
	priv->current_coredump_pos = 0;
	mutex_unlock(&priv->lock);

	return 0;
}

static ssize_t t900_gps_handle_header_write(struct t900_gps_dump_priv *priv,
					    const char __user *buffer,
					    size_t count)
{
	long ret;

	if (count != sizeof(struct gps_ssrdump_info)) {
		dev_err(priv->dev, "count=%zu, header size=%zu", count,
				sizeof(struct gps_ssrdump_info));
		return -EINVAL;
	}

	priv->header_info = kmalloc(sizeof(struct gps_ssrdump_info), GFP_KERNEL);
	if (!priv->header_info) {
		priv->state = GPS_DUMP_STATE_UNKNOWN_ERROR;
		return -ENOMEM;
	}

	ret = copy_from_user(priv->header_info, buffer, sizeof(struct gps_ssrdump_info));
	if (ret)
		goto header_failed;


	if (priv->header_info->magic != GPS_SSRDUMP_HEADER_MAGIC_NUMBER) {
		dev_err(priv->dev, "Invalid header magic number: %#x.", priv->header_info->magic);
		goto header_failed;
	}

	priv->total_coredump_size = priv->header_info->coredump_size;
	priv->current_coredump_pos = 0;

	if (priv->total_coredump_size == 0)
		goto header_failed;

	priv->coredump_buffer = vmalloc(priv->total_coredump_size);
	if (!priv->coredump_buffer) {
		priv->state = GPS_DUMP_STATE_UNKNOWN_ERROR;
		kfree(priv->header_info);
		return -ENOMEM;
	}

	priv->state = GPS_DUMP_STATE_WAIT_FOR_TRANSFER;
	return sizeof(struct gps_ssrdump_info);

header_failed:
	priv->state = GPS_DUMP_STATE_UNKNOWN_ERROR;
	kfree(priv->header_info);
	priv->header_info = NULL;
	return -EINVAL;
}

static ssize_t t900_gps_handle_coredump_write(struct t900_gps_dump_priv *priv,
					      const char __user *buffer,
					      size_t count)
{
	ssize_t remaining_expected;
	size_t bytes_to_copy;
	long ret;

	if (!priv->coredump_buffer) {
		priv->state = GPS_DUMP_STATE_UNKNOWN_ERROR;
		return -EFAULT;
	}

	remaining_expected =
		priv->total_coredump_size - priv->current_coredump_pos;

	if (remaining_expected <= 0 || count == 0) {
		priv->state = GPS_DUMP_STATE_UNKNOWN_ERROR;
		dev_err(priv->dev, "Invalid size count=%zu remaining=%zd.",
				count, remaining_expected);
		return -EFAULT;
	}

	bytes_to_copy = min(count, remaining_expected);

	ret = copy_from_user(priv->coredump_buffer + priv->current_coredump_pos,
		    buffer, bytes_to_copy);

	if (ret) {
		priv->state = GPS_DUMP_STATE_UNKNOWN_ERROR;
		return -EFAULT;
	}

	priv->current_coredump_pos += bytes_to_copy;

	if (priv->current_coredump_pos == priv->total_coredump_size) {
		priv->state = GPS_DUMP_STATE_COMPLETE;

		if (priv->sscd_pdata.sscd_report && priv->coredump_buffer) {
			struct sscd_segment seg = {
				.addr = priv->coredump_buffer,
				.size = priv->total_coredump_size,
			};

			priv->sscd_pdata.sscd_report(
				&priv->sscd_dev, &seg, 1, 0,
				priv->header_info->crashinfo);
		}
	}
	return bytes_to_copy;
}

static ssize_t t900_gps_write(struct file *filep, const char __user *buffer,
			      size_t count, loff_t *ppos)
{
	struct t900_gps_dump_priv *priv = filep->private_data;
	ssize_t ret;

	mutex_lock(&priv->lock);

	switch (priv->state) {
	case GPS_DUMP_STATE_OPENED:
		ret = t900_gps_handle_header_write(priv, buffer, count);
		break;
	case GPS_DUMP_STATE_WAIT_FOR_TRANSFER:
		ret = t900_gps_handle_coredump_write(priv, buffer, count);
		break;
	default:
		dev_err(priv->dev, "Unexpected state=%d.", priv->state);
		ret = -EFAULT;
		break;
	}

	mutex_unlock(&priv->lock);

	if (ret > 0)
		*ppos += ret;

	return ret;
}

static const struct file_operations t900_gps_dump_fops = {
	.owner = THIS_MODULE,
	.open = t900_gps_dump_open,
	.write = t900_gps_write,
	.release = t900_gps_dump_release,
};

static void sscd_release(struct device *dev)
{
	(void)dev;
}

static int t900_gps_dump_probe(struct platform_device *pdev)
{
	struct t900_gps_dump_priv *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->lock);
	priv->pdev = pdev;

	cdev_init(&priv->cdev, &t900_gps_dump_fops);
	priv->cdev.owner = THIS_MODULE;

	ret = cdev_add(&priv->cdev, dev_num, 1);
	if (ret < 0)
		return ret;

	priv->dev = device_create(gpsdump_class, &pdev->dev, dev_num, NULL, DRIVER_NAME);
	if (IS_ERR(priv->dev)) {
		ret = PTR_ERR(priv->dev);
		cdev_del(&priv->cdev);
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	priv->sscd_dev = (struct platform_device){
		.name            = SUBSYSTEM_NAME,
		.driver_override = SSCD_NAME,
		.id              = -1,
		.dev             = {
			.platform_data = &priv->sscd_pdata,
			.release       = sscd_release,
			},
	};

	platform_device_register(&priv->sscd_dev);

	return 0;
}

static void t900_gps_dump_remove(struct platform_device *pdev)
{
	struct t900_gps_dump_priv *priv = platform_get_drvdata(pdev);

	if (!priv)
		return;

	mutex_destroy(&priv->lock);

	platform_device_unregister(&priv->sscd_dev);

	if (priv->dev)
		device_destroy(gpsdump_class, dev_num);

	cdev_del(&priv->cdev);
}

static struct platform_driver t900_gps_dump_driver = {
	.probe      = t900_gps_dump_probe,
	.remove     = t900_gps_dump_remove,
	.driver     = {
		.name   = DRIVER_NAME,
		.owner  = THIS_MODULE,
	},
};

static struct platform_device t900_gps_dump_platform_device = {
	.name = DRIVER_NAME,
	.id = -1,
};

static int __init t900_gps_dump_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&dev_num, 0, DEVICE_COUNT, DRIVER_NAME);
	if (ret < 0)
		goto out_err;

	gpsdump_class = class_create(DRIVER_NAME);
	if (IS_ERR(gpsdump_class)) {
		ret = PTR_ERR(gpsdump_class);
		goto out_unregister_chrdev;
	}

	ret = platform_driver_register(&t900_gps_dump_driver);
	if (ret < 0)
		goto out_class_destroy;

	ret = platform_device_register(&t900_gps_dump_platform_device);
	if (ret < 0)
		goto out_device_unregister;

	return 0;

out_device_unregister:
	platform_driver_unregister(&t900_gps_dump_driver);
out_class_destroy:
	class_destroy(gpsdump_class);
out_unregister_chrdev:
	unregister_chrdev_region(dev_num, DEVICE_COUNT);
out_err:
	return ret;
}

static void __exit t900_gps_dump_exit(void)
{
	platform_device_unregister(&t900_gps_dump_platform_device);

	platform_driver_unregister(&t900_gps_dump_driver);

	if (gpsdump_class)
		class_destroy(gpsdump_class);

	unregister_chrdev_region(dev_num, DEVICE_COUNT);
}

module_init(t900_gps_dump_init);
module_exit(t900_gps_dump_exit);
MODULE_AUTHOR("Cheng Change <chengcha@google.com>");
MODULE_DESCRIPTION("GPS Coredump Handler with Platform Device Model");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
