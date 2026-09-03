/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef _GOOGLE_DPA_SECURE_H
#define _GOOGLE_DPA_SECURE_H

#include <linux/device.h>
#include <linux/err.h>
#include <linux/kconfig.h>

struct google_dpa_secure_channel;

struct google_dpa_secure_fw_image {
	phys_addr_t pa;
	size_t size;
};

#if IS_ENABLED(CONFIG_TRUSTY_VIRTIO_IPC)

/**
 * google_dpa_secure_connect() - connect to DPA TZ App.
 * @dev: google_dpa device
 *
 * Connects to DPA TZ App and create a channel to communicate with TZ App.
 * The caller shall call google_dpa_secure_shutdown_connection() to shutdown the
 * connection with TZ App when the caller does not need the secure channel
 * anymore.
 *
 * Return: Secure channel pointer in case of success. Error pointer value in case of failure.
 */
struct google_dpa_secure_channel *google_dpa_secure_connect(struct device *dev);

/**
 * google_dpa_secure_shutdown_connection() - shutdown the connection with DPA TZ App.
 * @sec_chan: Secure channel returned by google_dpa_secure_connect()
 *
 * Shutdown the connection with DPA TZ App and destroy the channel.
 */
int google_dpa_secure_shutdown_connection(struct google_dpa_secure_channel *sec_chan);

/**
 * google_dpa_secure_img_auth_v1() - Sends a image authentication request to TZ App.
 * @sec_chan: Secure channel
 * @ncp_image: NCP FW image
 * @nep_image: NEP FW image
 *
 * Sends a request to DPA TZ App to authenticate the signed DPA firmware image.
 * DPA TZ App keeps the firmware image loaded/protected in DRAM until Kernel requests the unload
 * request. Note that Kernel cannot read/write the DRAM carveout region for firmware image after
 * the invocation of this function because the DRAM region will be TZ-protected. Kernel needs to
 * call google_dpa_secure_img_unload() to remove the protection.
 *
 * Return: Zero in case of image authentication success. Negative error value in case of failure.
 */
/* TODO(b/426465018): Remove this function after the migration */
int google_dpa_secure_img_auth_v1(struct google_dpa_secure_channel *secure_chan,
				  const struct google_dpa_secure_fw_image *ncp_image,
				  const struct google_dpa_secure_fw_image *nep_image);

/**
 * google_dpa_secure_img_auth_v2() - Sends a image authentication request to TZ App.
 * @sec_chan: Secure channel
 * @dpa_image: DPA FW image
 *
 * Sends a request to DPA TZ App to authenticate the signed DPA firmware image.
 * DPA TZ App keeps the firmware image loaded/protected in DRAM until Kernel requests the unload
 * request. Note that Kernel cannot read/write the DRAM carveout region for firmware image after
 * the invocation of this function because the DRAM region will be TZ-protected. Kernel needs to
 * call google_dpa_secure_img_unload() to remove the protection.
 *
 * Return: Zero in case of image authentication success. Negative error value in case of failure.
 */
int google_dpa_secure_img_auth_v2(struct google_dpa_secure_channel *secure_chan,
				  const struct google_dpa_secure_fw_image *dpa_image);

/**
 * google_dpa_secure_img_unload() - Sends a image unload request to TZ App.
 * @sec_chan: Secure channel
 *
 * Sends a request to unload the firmware image passed by google_dpa_secure_img_auth().
 *
 * Return: Zero in case of image unload success. Negative error value in case of failure.
 */
int google_dpa_secure_img_unload(struct google_dpa_secure_channel *secure_chan);

/**
 * google_dpa_secure_boot() - Sends a DPA boot request to DPA TZ App.
 * @sec_chan: Secure channel
 *
 * Sends a DPA boot request to DPA TZ App. TZ App loads ELF segments to DPA SRAM
 * and releases NCP from the wait state. TZ App does not check if both NCP and
 * NEP boot to the main task. Kernel needs to poll for the shared_info structure
 * on SRAM to check if the NCP/NEP boot actually complete.
 *
 * Return: Zero in case of boot success. Negative error value in case of failure.
 */
int google_dpa_secure_boot(struct google_dpa_secure_channel *secure_chan);

#else

static inline struct google_dpa_secure_channel *google_dpa_secure_connect(struct device *dev)
{
	return ERR_PTR(-EINVAL);
}

int google_dpa_secure_shutdown_connection(struct google_dpa_secure_channel *sec_chan)
{
	return -EINVAL;
}

// TODO(b/426465018): Remove this function after the migration
int google_dpa_secure_img_auth_v1(struct google_dpa_secure_channel *secure_chan,
				  const struct google_dpa_secure_fw_image *ncp_image,
				  const struct google_dpa_secure_fw_image *nep_image)
{
	return -EINVAL;
}

int google_dpa_secure_img_auth_v2(struct google_dpa_secure_channel *secure_chan,
				  const struct google_dpa_secure_fw_image *dpa_image)
{
	return -EINVAL;
}

int google_dpa_secure_img_unload(struct google_dpa_secure_channel *secure_chan)
{
	return -EINVAL;
}

int google_dpa_secure_boot(struct google_dpa_secure_channel *secure_chan);
{
	return -EINVAL;
}

#endif

#endif /* _GOOGLE_DPA_SECURE_H */
