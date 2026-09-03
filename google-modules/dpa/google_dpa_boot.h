/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 */

#ifndef _GOOGLE_DPA_BOOT_H
#define _GOOGLE_DPA_BOOT_H

#include "google_dpa_internal.h"

/**
 * google_dpa_boot() - boot the DPA firmware
 * @dpa: google_dpa device
 *
 * Loads NCP and NEP firmware images from file system to DPA SRAM and boot DPA.
 *
 * Return: Zero in case of success. Negative error value in case of failure.
 */
int google_dpa_boot(struct google_dpa *dpa);

/**
 * google_dpa_shutdown() - shutdown the DPA firmware forcefully or gracefully
 * depending on @is_graceful
 * @dpa: google_dpa device
 *
 * Forcefully shutdown DPA firmware. DPA fw does not receive any notification
 * and it does not have chances to clean up the resources.
 *
 * Gracefully shutdown DPA firmware. DPA fw is notified before cleaning up the resources.
 *
 * Return: Zero in case of success. Negative error value in case of failure.
 */
int google_dpa_shutdown(struct google_dpa *dpa, bool is_graceful);

/**
 * google_dpa_unload() - unload the firmware image from the driver
 * @dpa: google_dpa device
 *
 * The firmware image is kept on DRAM for the image authentication when DPA boot
 * is requested. Unload the firmware image to clean up it.
 *
 * Return: Zero in case of success. Negative error value in case of failure.
 */
int google_dpa_unload(struct google_dpa *dpa);

/**
 * google_dpa_boot_cleanup() - Perform clean up of firmware.
 * @dpa: google_dpa device
 *
 * This function is expected to be called by the remove() function of DPA
 * driver. This function shuts down DPA firmware and releases resources.
 *
 * Return: Zero in case of success. Negative error value in case of failure.
 */
int google_dpa_boot_cleanup(struct google_dpa *dpa);

void google_dpa_crash_callback(void *reg_dump, unsigned int reg_dump_len, void *priv_data);

int google_dpa_power_domain_notify_callback(struct notifier_block *nb, unsigned long action,
					    void *data);

/*
 * Shared struct initialied by the NCP firmware boot stage.
 * All addresses are device addresses and must be converted to Kernel addresses
 * before they can be accessed.
 */
struct google_dpa_shared_info {
	u32 magic;
	u32 ipc_info_addr;
	u32 ring_info_addr;
	u32 boot_info_addr;
	u32 noa_gem5_info_addr;
} __packed;

/*
 * Get the DPA fw's shared info struct address
 * Returns NULL if the shared struct cannot be found.
 */
struct google_dpa_shared_info __iomem *google_dpa_get_shared_info(struct google_dpa *dpa);

#endif /* _GOOGLE_DPA_BOOT_H */
