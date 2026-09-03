// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "fwmt_driver.h"
#include "fwmt_service.h"

/**
 * struct cdev_state - FWMT character device session state.
 *
 * @cdevi: FWMT char device instance.
 * @total_size: The total size of the resource payload. Populated after the
 *              first chunk is returned by the firmware.
 */
struct cdev_state {
	struct fwmt_cdev_instance *cdevi;
	u32 total_size;
};

/**
 * fwmt_cdev_open - Character device open handler.
 * @inodep: Pointer to the inode.
 * @filep: Pointer to the file structure.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int fwmt_cdev_open(struct inode *inodep, struct file *filep)
{
	struct fwmt_cdev_instance *cdevi =
		container_of(inodep->i_cdev, struct fwmt_cdev_instance, cdev);
	struct cdev_state *cdev_state;

	cdev_state = kzalloc(sizeof(*cdev_state), GFP_KERNEL);
	if (!cdev_state)
		return -ENOMEM;

	cdev_state->cdevi = cdevi;
	/*
	 * We don't know the actual payload size until the firmware responds
	 * to the first request. Initialize to U32_MAX so the first read()
	 * isn't treated as EOF.
	 */
	cdev_state->total_size = U32_MAX;

	filep->private_data = cdev_state;
	return 0;
}

/**
 * fwmt_init_mba_msg - Initializes an FWMT MBA message.
 * @msg: The message to initialize.
 * @buffer: The shared MBA buffer.
 * @req_size: The size of the request payload.
 * @max_resp_size: The maximum size of the response payload.
 */
static void fwmt_init_mba_msg(struct fwmt_mba_msg *msg, struct fwmt_mba_buffer *buffer,
			      u32 req_size, u32 max_resp_size)
{
	msg->msg_phys_addr_lo = lower_32_bits(buffer->paddr);
	msg->msg_phys_addr_hi = upper_32_bits(buffer->paddr);
	msg->msg_data_size = req_size;
	msg->msg_buffer_size = min_t(u32, max_resp_size, buffer->size);
}

/**
 * fwmt_cdev_read - Character device read handler.
 * @filep: Pointer to the file structure.
 * @user_buffer: User buffer.
 * @len: Number of bytes requested.
 * @offset: Current read offset.
 *
 * Return: The number of bytes read, 0 on EOF, or a negative error code.
 */
static ssize_t fwmt_cdev_read(struct file *filep, char __user *user_buffer, size_t len,
			      loff_t *offset)
{
	struct cdev_state *cdev_state = filep->private_data;
	struct fwmt_dev *dev = cdev_state->cdevi->dev;
	struct fwmt_msg_retrieve_request *req;
	struct fwmt_msg_retrieve_response *resp;
	struct fwmt_mba_msg mba_msg;
	u32 bytes_to_copy;
	ssize_t ret;

	if (*offset >= cdev_state->total_size)
		return 0;

	if (sizeof(*resp) > dev->buffer->size)
		return -ENOMEM;

	/* Determine maximum chunk size bounded by shared buffer and user buffer. */
	bytes_to_copy = dev->buffer->size - sizeof(*resp);
	if (bytes_to_copy > len)
		bytes_to_copy = len;

	mutex_lock(&dev->buffer->lock);

	/* Setup the FWMT request. */
	req = dev->buffer->vaddr;
	req->base.type = cdev_state->cdevi->msg_type;
	req->base.error = 0;
	req->base.reserved = 0;
	req->resource_offset = *offset;

	fwmt_init_mba_msg(&mba_msg, dev->buffer, sizeof(*req), sizeof(*resp) + bytes_to_copy);

	ret = dev->send_req(dev, &mba_msg);
	if (ret)
		goto out;

	resp = dev->buffer->vaddr;
	if (resp->base.error) {
		dev_err(dev->dev, "Firmware returned error: %d\n", resp->base.error);
		ret = -EFAULT;
		goto out;
	}

	if (resp->size > bytes_to_copy) {
		dev_err(dev->dev, "Malformed MBA response\n");
		ret = -EFAULT;
		goto out;
	}

	/* Store the total payload size reported by the firmware. */
	cdev_state->total_size = resp->total_size;
	if (resp->size == 0) {
		ret = 0;
		goto out;
	}

	if (copy_to_user(user_buffer, resp->data, resp->size)) {
		dev_err(dev->dev, "Failed to copy data to user\n");
		ret = -EFAULT;
		goto out;
	}

	/* Advance the session offset for subsequent reads. */
	*offset += resp->size;
	ret = resp->size;

out:
	mutex_unlock(&dev->buffer->lock);
	return ret;
}

/**
 * fwmt_cdev_release - Character device release handler.
 * @inodep: Pointer to the inode.
 * @filep: Pointer to the file structure.
 */
static int fwmt_cdev_release(struct inode *inodep, struct file *filep)
{
	kfree(filep->private_data);
	return 0;
}

static const struct file_operations fwmt_fops = {
	.open = fwmt_cdev_open,
	.read = fwmt_cdev_read,
	.release = fwmt_cdev_release,
	.owner = THIS_MODULE,
};

/**
 * fwmt_cdev_create - Registers and creates a single char device node.
 * @dev: Base FWMT device.
 * @cdevi: The cdev instance to initialize.
 * @class: Character device class.
 * @devt: The major/minor numbers.
 * @msg_type: The FWMT retrieve operation type.
 * @name: Name of the char device node.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int fwmt_cdev_create(struct fwmt_dev *dev, struct fwmt_cdev_instance *cdevi, struct class *class,
		     dev_t devt, enum fwmt_msg_type msg_type, const char *name)
{
	int ret;

	cdev_init(&cdevi->cdev, &fwmt_fops);
	cdevi->cdev.owner = THIS_MODULE;
	cdevi->devt = devt;
	cdevi->dev = dev;
	cdevi->msg_type = msg_type;

	ret = cdev_add(&cdevi->cdev, cdevi->devt, 1);
	if (ret) {
		dev_err(dev->dev, "Failed to add cdev for %s\n", name);
		return ret;
	}

	cdevi->dev_node = device_create(class, dev->dev, cdevi->devt, NULL, "%s", name);
	if (IS_ERR(cdevi->dev_node)) {
		dev_err(dev->dev, "Failed to create device file for %s\n", name);
		ret = PTR_ERR(cdevi->dev_node);
		cdev_del(&cdevi->cdev);
		return ret;
	}

	return 0;
}

/**
 * fwmt_cdev_destroy - Unregisters and destroys a char device node.
 * @cdevi: The cdev instance to tear down.
 * @class: Character device class.
 */
void fwmt_cdev_destroy(struct fwmt_cdev_instance *cdevi, struct class *class)
{
	if (IS_ERR_OR_NULL(cdevi->dev_node))
		return;

	device_destroy(class, cdevi->devt);
	cdev_del(&cdevi->cdev);
}
