/* SPDX-License-Identifier: MIT */
#ifndef _GS_SSCD_DPU_H_
#define _GS_SSCD_DPU_H_

#include <linux/device.h>

struct display_sscd_section_config;
struct drm_device;

/**
 * struct sscd_payload - Container for sub-system specific coredump data
 * @cfgs: Array of configuration sections to be included in the coredump.
 * @num_cfgs: Number of entries in @cfgs.
 *
 * Populated by sub-drivers during the prepare phase to describe memory regions
 * and metadata for inclusion in the final coredump.
 */
struct sscd_payload {
	struct display_sscd_section_config *cfgs;
	int num_cfgs;
};

/**
 * struct sscd_funcs - Sub-System Core Dump (SSCD) coordination vtable
 *
 * Defines the contract between the main DPU driver and its sub-drivers for
 * coordinated state collection during a hardware fault.
 */
struct sscd_funcs {
	/**
	 * @sscd_payload_prepare: Snapshot sub-driver state into @payload.
	 * @dev: Sub-driver device.
	 * @payload: Container to be populated with coredump sections.
	 *
	 * Sub-driver is responsible for @payload->cfgs allocation and lifetime
	 * until @sscd_payload_destroy is called.
	 *
	 * Return: 0 on success, negative error code on failure.
	 */
	int (*sscd_payload_prepare)(struct device *dev, struct sscd_payload *payload);

	/**
	 * @sscd_payload_destroy: Release resources from the prepare phase.
	 * @dev: Sub-driver device.
	 * @payload: Payload to be cleaned up.
	 *
	 * Return: 0 on success, negative error code on failure.
	 */
	int (*sscd_payload_destroy)(struct device *dev, struct sscd_payload *payload);

	/**
	 * @trigger_coredump: Initiate sub-system-wide recovery and coredump.
	 * @drm_dev: Main DRM device handle.
	 * @calling_dev: Device initiating the trigger.
	 * @reason: Human-readable fault description.
	 *
	 * Return: 0 on success, negative error code on failure.
	 */
	int (*trigger_coredump)(struct drm_device *drm_dev, struct device *calling_dev,
				const char *reason);
};

#endif // _GS_SSCD_DPU_H_
