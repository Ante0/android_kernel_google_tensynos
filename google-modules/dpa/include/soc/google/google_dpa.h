/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Declaration of exported DPA APIs.
 *
 * Copyright (C) 2025 Google LLC.
 */

#ifndef _GOOGLE_DPA_H
#define _GOOGLE_DPA_H

#include <linux/device.h>

struct google_dpa;

/**
 * google_dpa_get - Get a DPA handle from a device.
 *
 * @dev: The device to get a DPA handle from.
 *
 * Returns:
 *   On success, a DPA handle.
 *   ERR_PTR(-ENODEV) if the function failed to find a DPA device. This function
 *		looks for "dpa" property in the passed device. The "dpa"
 *		property should contain a phandle to a DPA device in the device
 *		tree.
 *   ERR_PTR(-EPROBE_DEFER) if the phandle is valid but the DPA device has not
 *		been probed yet.
 *
 * It is expected that this function is called when a DPA client device is
 * probed and the returned dpa handle is stored in the client driver's context.
 * The client driver should call google_dpa_put() when it no longer need the DPA
 * handle.
 */
struct google_dpa *google_dpa_get(struct device *dev);

/**
 * google_dpa_put - Put a DPA handle for the device.
 *
 * @dpa: A DPA handle returned from google_dpa_get().
 */
void google_dpa_put(struct google_dpa *dpa);

/**
 * google_dpa_da_to_va - Convert a Device Address (DA) within the DPA's
 * SRAM to a Kernel AP-understandable Virtual Address (VA).
 *
 * Note: If you want to access the DPA's SRAM, you can choose either the
 * NCP or the NEP. But if you want to access the NCP or NEP's DTCM, you
 * have to specify the specific MCU you want to access.
 *
 * @dpa: The google_dpa structure providing device-specific information.
 * @mcu_kind: The google_dpa_mcu enum type.
 * @da: The Device Address (DA) to be converted.
 * @len: The size of the memory region.
 * @is_iomem: Return whether the Virtual Address (VA) is I/O memory.
 *
 * @return The Virtual Address (VA) on success, or NULL on failure.
 */
enum google_dpa_mcu_kind {
	GOOGLE_DPA_MCU_NCP,
	GOOGLE_DPA_MCU_NEP,
};
void *google_dpa_da_to_va(struct google_dpa *dpa,
			  enum google_dpa_mcu_kind mcu_kind, u64 da, size_t len,
			  bool *is_iomem);

/**
 * google_dpa_get_shared_ring_info_device_addr - Retrieves the shared_ring_info_addr.
 * It provides the DPA's device address where shared ring information is stored. To
 * access this share ring info on the AP side, you must first use da_to_va() to map
 * the address to VA.
 *
 * @dpa: The google_dpa structure containing the shared ring info.
 * @out_device_addr: Return the ring_info_addr from DPA's shared info.
 *
 * @return 0 on success, negative value for error code.
 */
int google_dpa_get_shared_ring_info_device_addr(struct google_dpa *dpa,
						u32 *out_device_addr);

/**
 *
 * google_dpa_get_dpa_dev - Get the dpa's struct device handle.
 *
 * @dpa: Google dpa handle.
 *
 * This is a temporary API to allow client drivers to make DMA allocations
 * on the DPA device. Eventually, only the DPA device should be allowed to
 * directly make DMA allocations.
 */
struct device *google_dpa_get_dpa_dev(struct google_dpa *dpa);

#endif /* _GOOGLE_DPA_H */
