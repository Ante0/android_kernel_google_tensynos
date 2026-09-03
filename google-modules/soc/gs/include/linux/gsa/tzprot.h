/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2024 Google LLC
 */
#ifndef __LINUX_TZPROT_H
#define __LINUX_TZPROT_H

#include <linux/device.h>
#include <linux/types.h>

/*
 * Trusty Media Prot Interface
 */


/**
 * trusty_protect_ip() - protect/unprotect hardware IP
 * @dev: pointer to TZPROT &struct device
 * @prot_id: protection ID of the IP
 * @enable: whether to protect or unprotect
 *
 * Return: 0 on success, negative error otherwise
 */
int trusty_protect_ip(struct device *dev, uint32_t prot_id, bool enable);

/**
 * trusty_protect_ip_bulk() - bulk protect/unprotect hardware IP
 * @dev: pointer to TZPROT &struct device
 * @dev_enable_mask: protection IP IDs to enable
 * @dev_disable_mask: protection IP IDs to disable
 *
 * Return: 0 on success, negative error otherwise
 */
int trusty_protect_ip_bulk(struct device *dev, uint32_t dev_enable_mask,
	uint32_t dev_disable_mask);


/**
 * trusty_get_histogram() - get histogram for channel(s) in panel
 * @dev: pointer to TZPROT &struct device
 * @hist_id: DPU histogram ID
 * @channel_mask: mask of histogram channels which will be accessed
 * @luma_arr: array where luma results are saved in ascending channel order
 * @arr_size: luma_arr size, must be >= number of requested channels
 *
 * Return: 0 on success, otherwise a negative error code
 */
int trusty_get_histogram(struct device *dev, uint8_t hist_id, uint8_t channel_mask,
	uint16_t *luma_arr, uint8_t arr_size);

#endif /* __LINUX_TZPROT_H */
