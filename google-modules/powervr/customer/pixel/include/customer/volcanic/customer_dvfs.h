/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include "img_types.h"
#include "rgxdevice.h"

/**
 * pixel_fw_dvfs_set_rate - set the GPU frequency/OPP via firmware instruction
 * @dev_info: device info
 * @rate: desired clock rate in Hz
 *
 * Instruct the firmware to perform a clock rate change via a custom command.
 *
 * Returns success (0) or PVRSRV_ERROR.
 */
PVRSRV_ERROR pixel_fw_dvfs_set_rate(PVRSRV_RGXDEV_INFO *dev_info, uint32_t rate);

