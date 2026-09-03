/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include "sysconfig.h"

/**
 * init_pixel_dvfs() - Initialize Power Frequency data
 * for the system layer.
 * @pixel_dev:	System layer private data
 * Return: This function returns PVRSRV_OK if initialisation
 * was successful, other wise PVRSRV_ERROR_<CODE>
 */
int init_pixel_dvfs(struct pixel_gpu_device *pixel_dev);

/**
 * deinit_pixel_dvfs() - Deinitialize Power Frequency data
 * for the system layer.
 * @pixel_dev:	System layer private data
 *
 * This function releases any resources required by
 * the system layers Power Frequency data
 */
void deinit_pixel_dvfs(struct pixel_gpu_device *pixel_dev);
