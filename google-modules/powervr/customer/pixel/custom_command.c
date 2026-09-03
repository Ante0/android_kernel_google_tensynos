/* SPDX-License-Identifier: GPL-2.0 */

#include <customer/volcanic/custom_command.h>

#include <pvrsrvkm/services/server/devices/rgxfwutils.h>

PVRSRV_ERROR pixel_send_custom_command(PVRSRV_RGXDEV_INFO *info, RGXFWIF_KCCB_CMD *cmd)
{
	PCPVRSRV_DEVICE_NODE dev_node = info->psDeviceNode;

	PVRSRV_ERROR error = PVRSRV_OK;

	PVR_ASSERT(PVRSRVPwrLockIsLockedByMe(dev_node));

	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVNODE, dev_node, PVRSRV_ERROR_NOT_SUPPORTED);

	/* Submit command to the firmware */
	LOOP_UNTIL_TIMEOUT_US(MAX_HW_TIME_US)
	{
		error = RGXSendCommand(info, cmd, PDUMP_FLAGS_NONE);

		if (!PVRSRVIsRetryError(error))
			break;

		OSWaitus(MAX_HW_TIME_US / WAIT_TRY_COUNT);
	}
	END_LOOP_UNTIL_TIMEOUT_US();

	PVR_LOG_IF_ERROR_VA(PVR_DBG_ERROR, error, "%s failed", __func__);

	return error;
}
