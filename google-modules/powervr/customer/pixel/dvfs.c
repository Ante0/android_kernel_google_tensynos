/* SPDX-License-Identifier: GPL-2.0 */

#include <customer/volcanic/customer_dvfs.h>
#include <customer/volcanic/custom_command.h>

PVRSRV_ERROR pixel_fw_dvfs_set_rate(PVRSRV_RGXDEV_INFO *info, uint32_t rate)
{
	/* Send opp instruction via generic platform cmd */
	RGXFWIF_KCCB_CMD cmd = {
		.eCmdType = RGXFWIF_KCCB_CMD_PLATFORM_CMD,
		.uCmdData.sPlatformData = {
			.ui32PlatformCmd = PIXEL_RGXFWIF_PLATFORM_CMD_DVFS_SET_RATE,
			.cmd_data.set_rate.opp = rate,
		},
	};

	return pixel_send_custom_command(info, &cmd);
}
