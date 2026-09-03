/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 */

#include "google_dpa_log_cdev.h"

int google_dpa_log_cdev_init(struct google_dpa *dpa, struct google_dpa_log_cdev *log_cdev,
			     const struct file_operations *fops, const char *device_node_name)
{
	struct device *dev_file = NULL;
	int ret;

	log_cdev->dev = dpa->dev;
	INIT_KFIFO(log_cdev->fifo);

	/* Initialize the mutex and the wait queue */
	mutex_init(&log_cdev->mutex);
	init_waitqueue_head(&log_cdev->wq);

	/* Initialize device node */
	ret = strscpy(log_cdev->device_node_name, device_node_name,
		      sizeof(log_cdev->device_node_name));
	if (ret == -E2BIG) {
		dev_err(log_cdev->dev, "Failed to initialize device node name.\n");
		return ret;
	}

	ret = alloc_chrdev_region(&log_cdev->char_dev_major, 0, 1, log_cdev->device_node_name);
	if (ret < 0) {
		dev_err(log_cdev->dev, "Failed to allocate char device region.\n");
		return ret;
	}

	cdev_init(&log_cdev->cdev, fops);
	log_cdev->cdev.owner = THIS_MODULE;

	ret = cdev_add(&log_cdev->cdev, log_cdev->char_dev_major, 1);
	if (ret < 0) {
		dev_err(log_cdev->dev, "Failed to add cdev.\n");
		goto err_cdev_add;
	}

	log_cdev->dev_class = class_create(log_cdev->device_node_name);
	if (IS_ERR(log_cdev->dev_class)) {
		dev_err(log_cdev->dev, "Failed to create device class.\n");
		ret = PTR_ERR(log_cdev->dev_class);
		goto err_class_create;
	}

	dev_file = device_create(log_cdev->dev_class, log_cdev->dev, log_cdev->char_dev_major, NULL,
				 "%s", log_cdev->device_node_name);
	if (IS_ERR(dev_file)) {
		dev_err(log_cdev->dev, "Failed to create device.\n");
		ret = PTR_ERR(dev_file);
		goto err_device_create;
	}

	atomic_set(&log_cdev->open_count, 0);
	return 0;

err_device_create:
	class_destroy(log_cdev->dev_class);

err_class_create:
	cdev_del(&log_cdev->cdev);

err_cdev_add:
	unregister_chrdev_region(log_cdev->char_dev_major, 1);

	return ret;
}

void google_dpa_log_cdev_deinit(struct google_dpa *dpa, struct google_dpa_log_cdev *log_cdev)
{
	device_destroy(log_cdev->dev_class, log_cdev->char_dev_major);
	class_destroy(log_cdev->dev_class);
	cdev_del(&log_cdev->cdev);
	unregister_chrdev_region(log_cdev->char_dev_major, 1);
}

int google_dpa_log_cdev_open(struct inode *inode, struct file *file)
{
	struct google_dpa_log_cdev *priv_data =
		container_of(inode->i_cdev, struct google_dpa_log_cdev, cdev);
	int expected_open_count = 0;

	if (atomic_try_cmpxchg(&priv_data->open_count, &expected_open_count, 1)) {
		mutex_lock(&priv_data->mutex);
		file->private_data = priv_data;
		mutex_unlock(&priv_data->mutex);
		return 0;
	} else
		return -EBUSY;
}

int google_dpa_log_cdev_release(struct inode *inode, struct file *file)
{
	struct google_dpa_log_cdev *priv_data = (struct google_dpa_log_cdev *)file->private_data;

	atomic_set_release(&priv_data->open_count, 0);

	return 0;
}

ssize_t google_dpa_log_cdev_read_to_user(struct file *file, char __user *buf, size_t count_user,
					 loff_t *f_pos)
{
	struct google_dpa_log_cdev *priv_data = (struct google_dpa_log_cdev *)file->private_data;
	unsigned int bytes_to_read;

	if (mutex_lock_interruptible(&priv_data->mutex))
		return -ERESTARTSYS; /* Interrupted while trying to acquire lock */

	if (kfifo_to_user(&priv_data->fifo, buf, count_user, &bytes_to_read)) {
		dev_err(priv_data->dev, "Failed to copy some data to userspace.\n");
		// we still return the content that has been successfully copied because
		// fifo is consumed
		if (bytes_to_read == 0)
			bytes_to_read = -EFAULT;
	}

	mutex_unlock(&priv_data->mutex);
	return bytes_to_read;
}

__poll_t google_dpa_log_cdev_poll(struct file *file, poll_table *wait)
{
	struct google_dpa_log_cdev *priv_data = (struct google_dpa_log_cdev *)file->private_data;
	__poll_t ret = 0;

	poll_wait(file, &priv_data->wq, wait);

	if (!kfifo_is_empty(&priv_data->fifo)) {
		ret |= POLLIN | POLLRDNORM; /* Signal that data is readable */
		dev_dbg(priv_data->dev,
			"Poll: Data available (POLLIN). Current queue size: %d/%d\n",
			kfifo_len(&priv_data->fifo), kfifo_size(&priv_data->fifo));
	} else {
		dev_dbg(priv_data->dev, "Poll: No data yet. Waiting...\n");
	}

	return ret;
}

static void google_dpa_log_cdev_write_locked(struct google_dpa_log_cdev *priv_data,
					     const void *data, size_t len)
{
	const int cap = kfifo_size(&priv_data->fifo);
	// calculate the chars that can finally be in the fifo
	if (cap < len) {
		// the data cannot fit in, store the last few chars
		dev_warn(priv_data->dev, "%s log too long, truncated\n",
			 priv_data->device_node_name);
		data += len - cap;
		len = cap;
		kfifo_reset(&priv_data->fifo);
	} else if (kfifo_avail(&priv_data->fifo) < len) {
		// drop until we have enough slots
		kfifo_skip_count(&priv_data->fifo, len - kfifo_avail(&priv_data->fifo));
	}
	kfifo_in(&priv_data->fifo, data, len);
}

void google_dpa_log_cdev_write(struct google_dpa_log_cdev *priv_data, const void *data, size_t len)
{
	mutex_lock(&priv_data->mutex);

	google_dpa_log_cdev_write_locked(priv_data, data, len);

	mutex_unlock(&priv_data->mutex);

	/* Wake up processes waiting on the wait queue (i.e., those using poll()) */
	wake_up_interruptible(&priv_data->wq);
}

/**
 * google_dpa_log_cdev_write_suffixed() - Write a message and an optional end string to the log buffer.
 * @priv_data:	The google_dpa_log_cdev structure.
 * @msg:	The primary message to write.
 * @end:	An optional string to append after the message. If NULL, a null terminator
 *		is appended to @msg instead.
 */
void google_dpa_log_cdev_write_suffixed(struct google_dpa_log_cdev *priv_data, const char *msg,
					const char *end)
{
	mutex_lock(&priv_data->mutex);

	if (end) {
		google_dpa_log_cdev_write_locked(priv_data, msg, strlen(msg));
		google_dpa_log_cdev_write_locked(priv_data, end, strlen(end));
	} else {
		google_dpa_log_cdev_write_locked(priv_data, msg, strlen(msg) + 1);
	}

	mutex_unlock(&priv_data->mutex);

	/* Wake up processes waiting on the wait queue (i.e., those using poll()) */
	wake_up_interruptible(&priv_data->wq);
}
