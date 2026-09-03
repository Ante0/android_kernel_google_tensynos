/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include "sysconfig.h"

/**
 * init_scheduling() - Initialize scheduling integration
 * @pixel_dev:	System layer private data
 * Return: This function returns PVRSRV_OK if initialisation
 * was successful, other wise PVRSRV_ERROR_<CODE>
 */
int init_scheduling(struct pixel_gpu_device *pixel_dev);

/**
 * deinit_scheduling() - Deinitialize scheduling integration
 * @pixel_dev:	System layer private data
 *
 * This function releases any resources required by
 * the system layers scheduling integration
 */
void deinit_scheduling(struct pixel_gpu_device *pixel_dev);

