/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * EdgeTPU MSI support.
 *
 * Copyright (C) 2026 Google LLC
 */

#ifndef __EDGETPU_MSI_H__
#define __EDGETPU_MSI_H__

#include "edgetpu-internal.h"

/* 3 MSI vectors (EventIDs): 0=KCI, 1=IKV and 2=IIF. */
#define EDGETPU_MAX_MSI_VECTORS (3)

struct edgetpu_msi_data {
	int virq;
};

struct edgetpu_msi {
	struct edgetpu_msi_data data[EDGETPU_MAX_MSI_VECTORS];
};

/**
 * edgetpu_msi_init() - Initialize MSI support for the given device.
 * @etdev: Pointer to the EdgeTPU device structure.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int edgetpu_msi_init(struct edgetpu_dev *etdev);

/**
 * edgetpu_msi_exit() - Clean up MSI support for the given device.
 * @etdev: Pointer to the EdgeTPU device structure.
 *
 * This function is safe to be called even when `edgetpu_msi_init()` failed.
 */
void edgetpu_msi_exit(struct edgetpu_dev *etdev);

/**
 * edgetpu_msi_get_virq() - Get the virtual IRQ number for a given mailbox.
 * @etdev: Pointer to the EdgeTPU device structure.
 * @idx: The mailbox index.
 *
 * Return: The virtual IRQ number on success (> 0), or a negative error code on failure.
 */
int edgetpu_msi_get_virq(struct edgetpu_dev *etdev, uint idx);

#endif /* __EDGETPU_MSI_H__ */
