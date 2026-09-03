// SPDX-License-Identifier: GPL-2.0

#pragma once

/* The order of the PM domains in pm_domain must match the order of the PM
   domains in the `power-domains` property of the GPU DT node. */
enum pixel_gpu_pm_domain {
	PIXEL_GPU_PM_DOMAIN_SSWRP_GPU_PD,
	PIXEL_GPU_PM_DOMAIN_GPU_CORE_LOGIC_PD,
	PIXEL_GPU_PM_DOMAIN_COUNT,
};
