/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include "sysconfig.h"

/**
 * init_genpd() - Initialize genpd integration
 * @pixel_dev:	System layer private data
 * Return: This function returns PVRSRV_OK if initialisation
 * was successful, other wise PVRSRV_ERROR_<CODE>
 */
int init_genpd(struct pixel_gpu_device *pixel_dev);

/**
 * deinit_genpd() - Deinitialize genpd integration
 * @pixel_dev:	System layer private data
 *
 * This function releases any resources required by
 * the system layers genpd integration
 */
void deinit_genpd(struct pixel_gpu_device *pixel_dev);
