// SPDX-License-Identifier: GPL-2.0
/*
 * Google Touch Interface Status Event Device  for Pixel devices.
 *
 * Copyright 2026 Google LLC.
 */

#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include "goog_interface_manager.h"
#include "goog_touch_interface.h"

/* -------------------------------------------------------------------------
 * File Operations
 * -------------------------------------------------------------------------
 */
static int gti_status_open(struct inode *inode, struct file *file)
{
	struct gti_status_event_dev *dev =
		container_of(inode->i_cdev, struct gti_status_event_dev, cdev);
	struct goog_touch_interface *gti =
		container_of(dev, struct goog_touch_interface, status_event_dev);

	file->private_data = dev;

	if (!atomic_dec_and_test(&dev->available)) {
		atomic_inc(&dev->available);
		GOOG_LOGW(gti, "Device already in use\n");
		return -EBUSY;
	}

	GOOG_LOGI(gti, "connected successfully\n");
	return 0;
}

static int gti_status_release(struct inode *inode, struct file *file)
{
	struct gti_status_event_dev *dev = file->private_data;
	struct goog_touch_interface *gti =
		container_of(dev, struct goog_touch_interface, status_event_dev);

	atomic_inc(&dev->available);

	GOOG_LOGI(gti, "disconnected\n");
	return 0;
}

static ssize_t gti_status_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct gti_status_event_dev *dev = file->private_data;
	struct goog_touch_interface *gti =
		container_of(dev, struct goog_touch_interface, status_event_dev);
	struct gti_status_event event;
	struct gti_status_packet_header pkg_header;
	struct gti_status_event_header event_header;
	ssize_t ret;
	ssize_t written_len = 0;
	unsigned long flags;
	unsigned long remaining;

	if (kfifo_is_empty(&dev->fifo)) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_interruptible(dev->waitq, !kfifo_is_empty(&dev->fifo));
		if (ret != 0)
			return ret;
	}

	spin_lock_irqsave(&dev->fifo_lock, flags);
	ret = kfifo_out(&dev->fifo, &event, sizeof(event));
	spin_unlock_irqrestore(&dev->fifo_lock, flags);

	if (ret == 0)
		return -EAGAIN;

	pkg_header.packet_size = sizeof(pkg_header) + sizeof(event_header) + event.size;

	switch (event.type) {
	case GTI_STATUS_EVENT_TYPE_INVALID_GESTURE:
		pkg_header.packet_type = GTI_STATUS_PACKET_TYPE_EXTEND_TOUCH_STATUS_EVENT;
		break;
	default:
		pkg_header.packet_type = GTI_STATUS_PACKET_TYPE_INVALID;
		break;
	}

	if (pkg_header.packet_type == GTI_STATUS_PACKET_TYPE_INVALID) {
		GOOG_LOGW(gti, "received an unknown status event\n");
		return -EAGAIN;
	}

	event_header.size = sizeof(event_header) + event.size;
	event_header.event_type = event.type;

	if (count < pkg_header.packet_size) {
		GOOG_LOGW(gti, "user buffer too small: %zu < %u\n", count, pkg_header.packet_size);
		return -EINVAL;
	}

	remaining = copy_to_user(buf, &pkg_header, sizeof(pkg_header));
	if (remaining != 0) {
		GOOG_LOGW(gti, "failed to copy data to user: buf=%p\n", buf);
		return -EFAULT;
	}
	written_len += sizeof(pkg_header);

	remaining = copy_to_user(buf + written_len, &event_header, sizeof(event_header));
	if (remaining != 0) {
		GOOG_LOGW(gti, "failed to copy data to user: buf=%p\n", buf);
		return -EFAULT;
	}
	written_len += sizeof(event_header);

	remaining = copy_to_user(buf + written_len, event.data, event.size);
	if (remaining != 0) {
		GOOG_LOGW(gti, "failed to copy data to user: buf=%p\n", buf);
		return -EFAULT;
	}
	written_len += event.size;

	return written_len;
}

static __poll_t gti_status_poll(struct file *file, poll_table *wait)
{
	struct gti_status_event_dev *dev = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &dev->waitq, wait);
	if (!kfifo_is_empty(&dev->fifo))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

static const struct file_operations gti_fops = {
	.owner = THIS_MODULE,
	.open = gti_status_open,
	.release = gti_status_release,
	.read = gti_status_read,
	.poll = gti_status_poll,
};

static int gti_send_status_event(struct gti_status_event_dev *dev, struct gti_status_event *event)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->fifo_lock, flags);

	if (kfifo_is_full(&dev->fifo)) {
		int ret = 0;
		struct gti_status_event dropped_event;

		// Drop the oldest event if the queue is full.
		ret = kfifo_out(&dev->fifo, &dropped_event, sizeof(dropped_event));
	}

	kfifo_in(&dev->fifo, event, sizeof(*event));

	spin_unlock_irqrestore(&dev->fifo_lock, flags);

	wake_up_interruptible(&dev->waitq);

	return 0;
}

int gti_status_send_invalid_gesture_event(struct gti_status_event_dev *dev,
					  struct gti_status_invalid_gesture_event *event)
{
	struct gti_status_event packed_event;
	struct goog_touch_interface *gti =
		container_of(dev, struct goog_touch_interface, status_event_dev);

	if (dev == NULL) {
		GOOG_LOGE(gti, "invalid interface context!");
		return -ENODEV;
	}

	packed_event.type = GTI_STATUS_EVENT_TYPE_INVALID_GESTURE;
	packed_event.size = sizeof(*event);
	memcpy(packed_event.data, event, packed_event.size);

	return gti_send_status_event(dev, &packed_event);
}
EXPORT_SYMBOL(gti_status_send_invalid_gesture_event);

int gti_status_event_probe(struct goog_touch_interface *gti)
{
	int ret;
	struct gti_status_event_dev *dev = NULL;
	struct class *gti_class = gim_get_interface_class(GOOG_INTERFACE_TYPE_TOUCH);
	char *name;
	u32 dev_id;

	if (gti == NULL || gti_class == NULL) {
		GOOG_LOGE(gti, "invalid interface context!");
		return -EINVAL;
	}

	dev = &gti->status_event_dev;

	dev_id = gim_vendor_get_interface_dev_id(gti->vendor_dev);
	name = kasprintf(GFP_KERNEL, "gti_status_event.%d", dev_id);
	if (name == NULL) {
		GOOG_LOGE(gti, "failed to build device name !\n");
		return -ENOMEM;
	}

	init_waitqueue_head(&dev->waitq);
	spin_lock_init(&dev->fifo_lock);
	atomic_set(&dev->available, 1);

	ret = kfifo_alloc(&dev->fifo, GTI_STATUS_FIFO_SIZE * sizeof(struct gti_status_event),
			  GFP_KERNEL);
	if (ret != 0) {
		GOOG_LOGE(gti, "failed to allocate fifo");
		goto free_name;
	}

	ret = alloc_chrdev_region(&dev->dev_num, 0, 1, name);
	if (ret != 0) {
		GOOG_LOGE(gti, "failed to allocate chrdev");
		goto free_fifo;
	}

	cdev_init(&dev->cdev, &gti_fops);
	ret = cdev_add(&dev->cdev, dev->dev_num, 1);
	if (ret != 0) {
		GOOG_LOGE(gti, "failed to add cdev\n");
		goto unreg_region;
	}

	dev->device = device_create(gti_class, NULL, dev->dev_num, dev, name);

	kfree(name);

	GOOG_LOGI(gti, "initialized\n");
	return 0;

unreg_region:
	unregister_chrdev_region(dev->dev_num, 1);
free_fifo:
	kfifo_free(&dev->fifo);
free_name:
	kfree(name);
	return ret;
}
EXPORT_SYMBOL(gti_status_event_probe);

void gti_status_event_remove(struct goog_touch_interface *gti)
{
	struct gti_status_event_dev *dev = NULL;
	struct class *gti_class = gim_get_interface_class(GOOG_INTERFACE_TYPE_TOUCH);

	if (gti == NULL) {
		GOOG_LOGE(gti, "invalid interface context!");
		return;
	}

	dev = &gti->status_event_dev;
	device_destroy(gti_class, dev->dev_num);
	cdev_del(&dev->cdev);
	unregister_chrdev_region(dev->dev_num, 1);
	kfifo_free(&dev->fifo);
}
EXPORT_SYMBOL(gti_status_event_remove);
