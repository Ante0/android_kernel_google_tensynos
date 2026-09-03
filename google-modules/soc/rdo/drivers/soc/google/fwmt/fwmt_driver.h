/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef FWMT_DRIVER_H
#define FWMT_DRIVER_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>

#include "fwmt_service.h"

/* Forward declarations. */
struct fwmt_dev;
struct gdmc_iface;
struct cpm_iface_client;

/**
 * struct fwmt_mba_buffer - Shared memory buffer for FWMT requests.
 *
 * @vaddr: Kernel virtual address of the buffer.
 * @paddr: Physical address of the buffer.
 * @size: Size of the buffer in bytes.
 * @lock: Mutex to serialize access to the buffer.
 */
struct fwmt_mba_buffer {
	void *vaddr;
	phys_addr_t paddr;
	u16 size;
	struct mutex lock;
};

/**
 * struct fwmt_cdev_instance - FWMT char device node.
 *
 * @cdev: The internal kernel cdev structure.
 * @dev_node: Character device node.
 * @devt: The allocated device number (major/minor).
 * @dev: Pointer to the parent FWMT device.
 * @msg_type: The &enum fwmt_msg_type the device node is responsible for.
 */
struct fwmt_cdev_instance {
	struct cdev cdev;
	struct device *dev_node;
	dev_t devt;
	struct fwmt_dev *dev;
	enum fwmt_msg_type msg_type;
};

/**
 * typedef fwmt_send_req_t - Function pointer for sending MBA request.
 * @dev: Pointer to the FWMT device triggering the request.
 * @msg: The FWMT MBA message structure to be sent.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
typedef int (*fwmt_send_req_t)(struct fwmt_dev *dev, struct fwmt_mba_msg *msg);

/**
 * struct fwmt_dev - Base FWMT device class.
 *
 * @dev: Kernel device instance.
 * @name: Name of the subsystem.
 * @buffer: Shared DMA memory buffer.
 * @send_req: Function to send the FWMT request via the MBA.
 * @devt: Base device number for the allocated character devices.
 * @cdev_metrics: Character device instance for metrics retrieval.
 * @cdev_strings: Character device instance for strings retrieval.
 */
struct fwmt_dev {
	struct device *dev;
	const char *name;
	struct fwmt_mba_buffer *buffer;
	fwmt_send_req_t send_req;

	dev_t devt;
	struct fwmt_cdev_instance cdev_metrics;
	struct fwmt_cdev_instance cdev_strings;
};

/**
 * struct fwmt_dev_gdmc - GDMC FWMT device subclass.
 *
 * @dev: Base FWMT device.
 * @iface: Mailbox interface to GDMC.
 */
struct fwmt_dev_gdmc {
	struct fwmt_dev dev;
	struct gdmc_iface *iface;
};

/**
 * struct fwmt_dev_cpm - CPM FWMT device subclass.
 *
 * @dev: Base FWMT device.
 * @iface: Mailbox interface to CPM.
 */
struct fwmt_dev_cpm {
	struct fwmt_dev dev;
	struct cpm_iface_client *iface;
};

int fwmt_gdmc_send_req(struct fwmt_dev *dev, struct fwmt_mba_msg *msg);
int fwmt_cpm_send_req(struct fwmt_dev *dev, struct fwmt_mba_msg *msg);

int fwmt_cdev_create(struct fwmt_dev *dev, struct fwmt_cdev_instance *cdevi, struct class *class,
		     dev_t devt, enum fwmt_msg_type msg_type, const char *name);
void fwmt_cdev_destroy(struct fwmt_cdev_instance *cdevi, struct class *class);

#endif /* FWMT_DRIVER_H */
