/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Google LLC.
 *
 */

#ifndef GOOGLE_MODEM_UART_H
#define GOOGLE_MODEM_UART_H

#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/types.h>

struct class;
struct device;
struct gdmc_iface;

struct modem_uart_memory_region {
	void *vaddr;
	dma_addr_t paddr;
	uint32_t size;

	/* Current read offset */
	size_t head;
};

struct modem_uart_character_device {
	struct cdev cdev;
	dev_t devt;
	struct class *class;

	int logging_enabled;

	struct mutex cdev_lock;
};

struct modem_uart_base {
	/* Parent platform device */
	struct device *dev;

	struct modem_uart_character_device char_dev;
	struct modem_uart_memory_region memory_region;
	struct gdmc_iface *gdmc_iface;
};

#endif /* GOOGLE_MODEM_UART_H */
