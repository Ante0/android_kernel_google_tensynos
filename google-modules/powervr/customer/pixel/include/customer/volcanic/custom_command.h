/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include "rgxdevice.h"

/**
 * pixel_send_custom_command - helper to send a custom command to firmware.
 * @info: device info
 * @cmd: kccb containing the custom command.
 *
 * Returns success (0) or PVRSRV_ERROR.
 */
PVRSRV_ERROR pixel_send_custom_command(PVRSRV_RGXDEV_INFO *info, RGXFWIF_KCCB_CMD *cmd);
