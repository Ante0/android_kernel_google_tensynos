/*************************************************************************/ /*!
@File           rgxpdvfs.c
@Title          RGX Proactive DVFS Functionality
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Kernel mode Proactive DVFS Functionality.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include "rgxpdvfs.h"
#include "rgxfwutils.h"
#include "rgxpower.h"
#include "rgx_options.h"
#include "rgxtimecorr.h"

#define USEC_TO_MSEC 1000

PVRSRV_ERROR PDVFSLimitMaxFrequencyLocked(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32MaxOPPPoint)
{
	RGXFWIF_RUNTIME_CFG		*psRuntimeCfg = psDevInfo->psRGXFWIfRuntimeCfg;
	PVRSRV_ERROR			eError = PVRSRV_OK;

	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVINFO, psDevInfo, PVRSRV_ERROR_NOT_SUPPORTED);

	if (!_PDVFSEnabled())
	{
		/* No log message to avoid excessive messages */
		return PVRSRV_OK;
	}

	/* Update the firmware config */
	psRuntimeCfg->ui32MaxOPPPoint = ui32MaxOPPPoint;
	OSWriteMemoryBarrier(&psRuntimeCfg->ui32MaxOPPPoint);
	RGXFwSharedMemCacheOpValue(psRuntimeCfg->ui32MaxOPPPoint, FLUSH);

	if (PVRSRVIsDevicePowered(psDevInfo->psDeviceNode))
	{
		RGXFWIF_KCCB_CMD		sGPCCBCmd;
		IMG_UINT32				ui32CmdKCCBSlot;

		/* send feedback */
		sGPCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_PDVFS_SET_CONFIG;
		sGPCCBCmd.uCmdData.sDVFSData.eReqType = RGXFWIF_DVFS_MAX_FREQ;

		/* Submit command to the firmware. */
		eError = RGXScheduleCommandAndGetKCCBSlot(psDevInfo,
												  RGXFWIF_DM_GP,
												  &sGPCCBCmd,
												  PDUMP_FLAGS_CONTINUOUS,
												  &ui32CmdKCCBSlot);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Unable to send command (%u).", __func__, eError));
		}
	}

	return eError;
}

PVRSRV_ERROR PDVFSLimitMaxFrequency(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32MaxOPPPoint)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVINFO, psDevInfo, PVRSRV_ERROR_NOT_SUPPORTED);
	if (!_PDVFSEnabled())
	{
		/* No log message to avoid excessive messages */
		return PVRSRV_OK;
	}

	PVRSRVPowerLockWrite(psDevInfo->psDeviceNode);
	eError = PDVFSLimitMaxFrequencyLocked(psDevInfo, ui32MaxOPPPoint);
	PVRSRVPowerUnlockWrite(psDevInfo->psDeviceNode);

    return eError;
}

PVRSRV_ERROR PDVFSLimitMinFrequencyLocked(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32MinOPPPoint) {
	RGXFWIF_RUNTIME_CFG		*psRuntimeCfg = psDevInfo->psRGXFWIfRuntimeCfg;
	PVRSRV_ERROR			eError = PVRSRV_OK;

	/* Update the firmware config */
	psRuntimeCfg->ui32MinOPPPoint = ui32MinOPPPoint;
	OSWriteMemoryBarrier(&psRuntimeCfg->ui32MinOPPPoint);
	RGXFwSharedMemCacheOpValue(psRuntimeCfg->ui32MinOPPPoint, FLUSH);

	if (PVRSRVIsDevicePowered(psDevInfo->psDeviceNode))
	{
		RGXFWIF_KCCB_CMD		sGPCCBCmd;
		IMG_UINT32				ui32CmdKCCBSlot;

		/* send feedback */
		sGPCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_PDVFS_SET_CONFIG;
		sGPCCBCmd.uCmdData.sDVFSData.eReqType = RGXFWIF_DVFS_MIN_FREQ;

		/* Submit command to the firmware. */
		eError = RGXScheduleCommandAndGetKCCBSlot(psDevInfo,
												  RGXFWIF_DM_GP,
												  &sGPCCBCmd,
												  PDUMP_FLAGS_CONTINUOUS,
												  &ui32CmdKCCBSlot);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Unable to send command (%u).", __func__, eError));
		}
	}
	return eError;
}

PVRSRV_ERROR PDVFSLimitMinFrequency(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32MinOPPPoint)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVINFO, psDevInfo, PVRSRV_ERROR_NOT_SUPPORTED);
	if (!_PDVFSEnabled())
	{
		/* No log message to avoid excessive messages */
		return PVRSRV_OK;
	}
	PVRSRVPowerLockWrite(psDevInfo->psDeviceNode);
	eError = PDVFSLimitMinFrequencyLocked(psDevInfo, ui32MinOPPPoint);
	PVRSRVPowerUnlockWrite(psDevInfo->psDeviceNode);

    return eError;
}

PVRSRV_ERROR PDVFSResetFrequencyConstraints(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	PVRSRV_ERROR			eError = PVRSRV_OK;

	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVINFO, psDevInfo, PVRSRV_ERROR_NOT_SUPPORTED);

	if (!_PDVFSEnabled())
	{
		/* No log message to avoid excessive messages */
		return PVRSRV_OK;
	}

	/* Take power lock */
	PVRSRVPowerLockWrite(psDevInfo->psDeviceNode);

	if (PVRSRVIsDevicePowered(psDevInfo->psDeviceNode))
	{
		RGXFWIF_KCCB_CMD		sGPCCBCmd;
		IMG_UINT32				ui32CmdKCCBSlot;

		/* send feedback */
		sGPCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_PDVFS_SET_CONFIG;
		sGPCCBCmd.uCmdData.sDVFSData.eReqType = RGXFWIF_DVFS_RESET_CONSTRAINTS;

		/* Submit command to the firmware. */
		eError = RGXScheduleCommandAndGetKCCBSlot(psDevInfo,
												  RGXFWIF_DM_GP,
												  &sGPCCBCmd,
												  PDUMP_FLAGS_CONTINUOUS,
												  &ui32CmdKCCBSlot);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Unable to send command (%u).", __func__, eError));
			goto _ErrorUnlock;
		}

	}

_ErrorUnlock:
	PVRSRVPowerUnlockWrite(psDevInfo->psDeviceNode);
	return eError;
}

#if defined(SUPPORT_PDVFS_HEADROOM_EXT)
PVRSRV_ERROR PDVFSSetFrequencyHeadroom(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_INT32 i32Headroom)
{
	PVRSRV_ERROR			eError = PVRSRV_OK;
	RGXFWIF_RUNTIME_CFG		*psRuntimeCfg = psDevInfo->psRGXFWIfRuntimeCfg;

	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVINFO, psDevInfo, PVRSRV_ERROR_NOT_SUPPORTED);

	if (!_PDVFSEnabled())
	{
		/* No log message to avoid excessive messages */
		return PVRSRV_OK;
	}

	/* Update the firmware config */
	psRuntimeCfg->i32Headroom = i32Headroom;
	OSWriteMemoryBarrier(&psRuntimeCfg->i32Headroom);
	RGXFwSharedMemCacheOpValue(psRuntimeCfg->i32Headroom, FLUSH);

	PVR_DPF((PVR_DBG_WARNING, "%s: Headroom = %d", __func__, i32Headroom));

	/* Take power lock */
	PVRSRVPowerLockWrite(psDevInfo->psDeviceNode);

	if (PVRSRVIsDevicePowered(psDevInfo->psDeviceNode))
	{
		RGXFWIF_KCCB_CMD		sGPCCBCmd;
		IMG_UINT32				ui32CmdKCCBSlot;

		sGPCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_PDVFS_SET_CONFIG;
		sGPCCBCmd.uCmdData.sDVFSData.eReqType = RGXFWIF_DVFS_CAPACITY_HEADROOM;

		/* Submit command to the firmware. */
		eError = RGXScheduleCommandAndGetKCCBSlot(psDevInfo,
												  RGXFWIF_DM_GP,
												  &sGPCCBCmd,
												  PDUMP_FLAGS_CONTINUOUS,
												  &ui32CmdKCCBSlot);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Unable to send command (%u).", __func__, eError));
			goto _ErrorUnlock;
		}
	}

_ErrorUnlock:
	PVRSRVPowerUnlockWrite(psDevInfo->psDeviceNode);
	return eError;
}
#endif

#if defined(SUPPORT_PDVFS_POLLINT_EXT)
PVRSRV_ERROR PDVFSSetReactivePollingInterval(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32PollingMs)
{
	PVRSRV_ERROR			eError = PVRSRV_OK;
	RGXFWIF_RUNTIME_CFG		*psRuntimeCfg = psDevInfo->psRGXFWIfRuntimeCfg;

	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVINFO, psDevInfo, PVRSRV_ERROR_NOT_SUPPORTED);

	if (!_PDVFSEnabled())
	{
		/* No log message to avoid excessive messages */
		return PVRSRV_OK;
	}

	/* Update the firmware config */
	psRuntimeCfg->ui32PDVFSPollingMs = ui32PollingMs;
	OSWriteMemoryBarrier(&psRuntimeCfg->ui32PDVFSPollingMs);
	RGXFwSharedMemCacheOpValue(psRuntimeCfg->ui32PDVFSPollingMs, FLUSH);

	/* Take power lock */
	PVRSRVPowerLockWrite(psDevInfo->psDeviceNode);

	if (PVRSRVIsDevicePowered(psDevInfo->psDeviceNode))
	{
		RGXFWIF_KCCB_CMD		sGPCCBCmd;
		IMG_UINT32				ui32CmdKCCBSlot;

		sGPCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_PDVFS_SET_CONFIG;
		sGPCCBCmd.uCmdData.sDVFSData.eReqType = RGXFWIF_DVFS_POLLING_INTERVAL;

		/* Submit command to the firmware. */
		eError = RGXScheduleCommandAndGetKCCBSlot(psDevInfo,
												  RGXFWIF_DM_GP,
												  &sGPCCBCmd,
												  PDUMP_FLAGS_CONTINUOUS,
												  &ui32CmdKCCBSlot);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Unable to send command (%u).", __func__, eError));
			goto _ErrorUnlock;
		}

	}

_ErrorUnlock:
	PVRSRVPowerUnlockWrite(psDevInfo->psDeviceNode);
	return eError;
}
#endif

PVRSRV_ERROR PDVFSSetUpThreshold(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32UpThresholdInPct)
{
	PVRSRV_ERROR			eError = PVRSRV_OK;
	RGXFWIF_RUNTIME_CFG		*psRuntimeCfg = psDevInfo->psRGXFWIfRuntimeCfg;

	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVINFO, psDevInfo, PVRSRV_ERROR_NOT_SUPPORTED);

	if (!_PDVFSEnabled())
	{
		/* No log message to avoid excessive messages */
		return PVRSRV_OK;
	}

	/* Update the firmware config */
	psRuntimeCfg->ui32UpThresholdInPct = ui32UpThresholdInPct;
	OSWriteMemoryBarrier(&psRuntimeCfg->ui32UpThresholdInPct);
	RGXFwSharedMemCacheOpValue(psRuntimeCfg->ui32UpThresholdInPct, FLUSH);

	/* Take power lock */
	PVRSRVPowerLockWrite(psDevInfo->psDeviceNode);

	if (PVRSRVIsDevicePowered(psDevInfo->psDeviceNode))
	{
		RGXFWIF_KCCB_CMD		sGPCCBCmd;
		IMG_UINT32				ui32CmdKCCBSlot;

		sGPCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_PDVFS_SET_CONFIG;
		sGPCCBCmd.uCmdData.sDVFSData.eReqType = RGXFWIF_DVFS_UP_THRESHOLD;

		/* Submit command to the firmware. */
		eError = RGXScheduleCommandAndGetKCCBSlot(psDevInfo,
												  RGXFWIF_DM_GP,
												  &sGPCCBCmd,
												  PDUMP_FLAGS_CONTINUOUS,
												  &ui32CmdKCCBSlot);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Unable to send command (%u).", __func__, eError));
			goto _ErrorUnlock;
		}

	}

_ErrorUnlock:
	PVRSRVPowerUnlockWrite(psDevInfo->psDeviceNode);
	return eError;
}

PVRSRV_ERROR PDVFSSetDownDifferential(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32DownDifferentialInPct)
{
	PVRSRV_ERROR			eError = PVRSRV_OK;
	RGXFWIF_RUNTIME_CFG		*psRuntimeCfg = psDevInfo->psRGXFWIfRuntimeCfg;

	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVINFO, psDevInfo, PVRSRV_ERROR_NOT_SUPPORTED);

	if (!_PDVFSEnabled())
	{
		/* No log message to avoid excessive messages */
		return PVRSRV_OK;
	}

	/* Update the firmware config */
	psRuntimeCfg->ui32DownDifferentialInPct = ui32DownDifferentialInPct;
	OSWriteMemoryBarrier(&psRuntimeCfg->ui32DownDifferentialInPct);
	RGXFwSharedMemCacheOpValue(psRuntimeCfg->ui32DownDifferentialInPct, FLUSH);

	/* Take power lock */
	PVRSRVPowerLockWrite(psDevInfo->psDeviceNode);

	if (PVRSRVIsDevicePowered(psDevInfo->psDeviceNode))
	{
		RGXFWIF_KCCB_CMD		sGPCCBCmd;
		IMG_UINT32				ui32CmdKCCBSlot;

		sGPCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_PDVFS_SET_CONFIG;
		sGPCCBCmd.uCmdData.sDVFSData.eReqType = RGXFWIF_DVFS_DOWN_DIFFERENTIAL;

		/* Submit command to the firmware. */
		eError = RGXScheduleCommandAndGetKCCBSlot(psDevInfo,
												  RGXFWIF_DM_GP,
												  &sGPCCBCmd,
												  PDUMP_FLAGS_CONTINUOUS,
												  &ui32CmdKCCBSlot);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Unable to send command (%u).", __func__, eError));
			goto _ErrorUnlock;
		}

	}

_ErrorUnlock:
	PVRSRVPowerUnlockWrite(psDevInfo->psDeviceNode);
	return eError;
}

/*************************************************************************/ /*!
@Function       RGXPDVFSCheckUtilisationChange
@Description    Checks if utilisation has changed since the last snap-shot.
@Input          psDevInfo    A pointer to PVRSRV_RGXDEV_INFO.
@Return         None.
*/ /**************************************************************************/
void RGXPDVFSCheckUtilisationChange(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	IMG_UINT32 ui32Utilisation = *psDevInfo->pui32RGXFWIFUtilisation;

	if (!_PDVFSEnabled())
	{
		/* No error message to avoid excessive messages */
		return;
	}

	if (ui32Utilisation != 0 && psDevInfo->ui32UtilisationSnapshot != ui32Utilisation)
	{
		psDevInfo->ui32UtilisationSnapshot = ui32Utilisation;
		RGXProcessUtilisationChange(psDevInfo, ui32Utilisation);
	}
}
