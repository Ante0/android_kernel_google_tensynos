/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 */

#ifndef _GOOGLE_DPA_LOG_CDEV_H
#define _GOOGLE_DPA_LOG_CDEV_H

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/kfifo.h>

#include "google_dpa_internal.h"

#define MAX_DEVICE_NAME_LEN 64
#define LOG_BUFF_INIT_SIZE ((uint32_t)(1) << 14) // to accommodate boot log

/* Struct for storing associated char dev */
struct google_dpa_log_cdev {
	char device_node_name[MAX_DEVICE_NAME_LEN];
	struct device *dev;
	dev_t char_dev_major;
	struct cdev cdev;
	struct class *dev_class;
	atomic_t open_count;
	struct mutex mutex; /* Mutex to protect access to the ring buffer */
	wait_queue_head_t wq;

	DECLARE_KFIFO(fifo, char, LOG_BUFF_INIT_SIZE);
};

int google_dpa_log_cdev_init(struct google_dpa *dpa, struct google_dpa_log_cdev *log_cdev,
			     const struct file_operations *fops, const char *device_node_name);
void google_dpa_log_cdev_deinit(struct google_dpa *dpa, struct google_dpa_log_cdev *log_cdev);

/* google_dpa_log_cdev file_operations */

int google_dpa_log_cdev_open(struct inode *inode, struct file *file);

int google_dpa_log_cdev_release(struct inode *inode, struct file *file);

ssize_t google_dpa_log_cdev_read_to_user(struct file *file, char __user *buf, size_t count_user,
					 loff_t *f_pos);
__poll_t google_dpa_log_cdev_poll(struct file *file, poll_table *wait);

/* google_dpa_log_cdev file_operations */

void google_dpa_log_cdev_write(struct google_dpa_log_cdev *priv_data, const void *data, size_t len);
void google_dpa_log_cdev_write_suffixed(struct google_dpa_log_cdev *priv_data, const char *msg,
					const char *end);

#endif /* _GOOGLE_DPA_LOG_CDEV_H */
