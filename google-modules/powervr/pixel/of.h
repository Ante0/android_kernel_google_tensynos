/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include <linux/platform_device.h>

struct pixel_gpu_device;

/**
 * init_pixel_of() - Initialize open firmware for the system layer.
 * @pixel_dev: System layer private data
 * Return: This function returns 0 if initialisation
 */
int init_pixel_of(struct pixel_gpu_device *pixel_dev);

/**
 * deinit_pixel_of() - Deinitialize open firmware for the system layer.
 * @pixel_dev: System layer private data
 *
 * This function releases any resources required by
 * the system layers Power Frequency data
 */
void deinit_pixel_of(struct pixel_gpu_device *pixel_dev);

#if defined(SUPPORT_TRUSTED_DEVICE)
/**
 * fill_carveout_resource() - Fill out carveout resource.
 * @carveout: empty resource to be filled
 * Return: 0 on success
 */
int fill_carveout_resource(struct resource *carveout);
#endif

struct pixel_of_properties {
	bool spu_gating;
	bool apm;
	bool pre_silicon;
	bool jones_force_on;
	bool virtual_platform;
	bool emulator;
	bool pdvfs_com_host;
	uint32_t apm_latency;
	uint32_t autosuspend_latency;
};

struct pixel_of_pdevs {
#if defined(SUPPORT_LINUX_DVFS) || defined(SUPPORT_PDVFS)
	struct platform_device *gpu_pf_state_pdev;
#endif
};
