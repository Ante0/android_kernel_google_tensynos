/*************************************************************************/ /*!
@File           rgxpower.c
@Title          Device specific power routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Device specific functions
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

#if defined(__linux__)
#include <linux/stddef.h>
#else
#include <stddef.h>
#endif

#include "rgxpower.h"
#include "rgxinit.h"
#include "rgx_fwif_km.h"
#include "rgxfwutils.h"
#include "rgxfwriscv.h"
#include "pdump_km.h"
#include "pvr_debug.h"
#include "trace_events.h"
#include "osfunc.h"
#include "rgxdebug_common.h"
#include "devicemem.h"
#include "devicemem_pdump.h"
#include "rgxtimecorr.h"
#include "devicemem_utils.h"
#include "htbserver.h"
#include "rgxstartstop.h"
#include "rgxfwimageutils.h"
#include "sync.h"
#include "rgxdefs_km.h"

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#include "process_stats.h"
#endif
#if defined(SUPPORT_LINUX_DVFS)
#include "pvr_dvfs_device.h"
#endif
#if defined(SUPPORT_PDVFS)
#include "pvr_dvfs_proactive.h"
#endif

#if defined(PVRSRV_ANDROID_TRACE_GPU_FREQ)
#include "power_trace_events.h"
#endif

#if defined(SUPPORT_PDVFS) && (PDVFS_COM == PDVFS_COM_HOST)
#include "rgxpdvfs.h"
#endif

#if defined(SUPPORT_PDVFS) && defined(PVRSRV_ANDROID_TRACE_GPU_FREQ)
#include <trace/events/power.h>
#endif

static PVRSRV_ERROR _RGXFWNotifyHostTimeout(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	RGXFWIF_KCCB_CMD sCmd;
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32CmdKCCBSlot;

	/* Send the Timeout notification to the FW */
	sCmd.eCmdType = RGXFWIF_KCCB_CMD_POW;
	sCmd.ui32KCCBFlags = 0;
	sCmd.uCmdData.sPowData.ePowType = RGXFWIF_POW_FORCED_IDLE_REQ;
	sCmd.uCmdData.sPowData.uPowerReqData.ePowRequestType = RGXFWIF_POWER_HOST_TIMEOUT;

	eError = RGXSendCommandAndGetKCCBSlot(psDevInfo,
	                                      &sCmd,
	                                      PDUMP_FLAGS_NONE,
	                                      &ui32CmdKCCBSlot);

	return eError;
}

void RGXInitGpuUtilStats(PVRSRV_DEVICE_NODE *psDeviceNode,
						 RGX_GPU_UTIL_STATS	*psGpuUtilStats)
{
	PVRSRV_ERROR eError;
	OS_SPINLOCK_FLAGS uiFlags = 0;
	IMG_UINT64 ui64DeviceTimestampTicks = 0;
	IMG_UINT64 ui64OsTimestampNS;

#if defined(SUPPORT_SOC_TIMER)
	eError = PVRSRVRGXCurrentTime(NULL, psDeviceNode, RGX_QUERY_SOC_TIMER, &ui64DeviceTimestampTicks);
	ui64OsTimestampNS = RGXTimeGetSOCTimerValueNS(psDeviceNode);
#else
	eError = PVRSRVRGXCurrentTime(NULL, psDeviceNode, RGX_QUERY_DEVICE_TIMESTAMP, &ui64DeviceTimestampTicks);
	ui64OsTimestampNS = OSClockns64();
#endif
	PVR_ASSERT(eError == PVRSRV_OK);

	OSSpinLockAcquire(psGpuUtilStats->hSpinlock, uiFlags);
	psGpuUtilStats->ui64LastCheckTimestampTicks = ui64DeviceTimestampTicks;
	psGpuUtilStats->ui64LastCheckTimestampNS = ui64OsTimestampNS;
	OSSpinLockRelease(psGpuUtilStats->hSpinlock, uiFlags);
}

static void _RGXGetGpuBasicUtilData(RGXFWIF_SYSDATA *psFwSysData,
									volatile IMG_UINT64 *pui64GpuActiveTimeNS,
									volatile IMG_UINT64 *pui64FwStatsTimestampNS,
									volatile IMG_BOOL *pbGpuActive)
{
	/*
	 * Obtain coherent values from shared memory using
	 * repeated reads as the Firmware might update them at any time.
	 * The lack of explicit synchronisation mechanisms reduces the
	 * load on the Firmware and allows the driver to retrieve data
	 * faster without having to wait for the Fw.
	 */
	do
	{
		*pui64GpuActiveTimeNS = psFwSysData->ui64GpuActiveTimeNS;
		*pbGpuActive = psFwSysData->bGpuActive;
		*pui64FwStatsTimestampNS = psFwSysData->ui64FwStatsTimestampNS;
	} while ((*pui64GpuActiveTimeNS != psFwSysData->ui64GpuActiveTimeNS) ||
			(*pbGpuActive != psFwSysData->bGpuActive) ||
			(*pui64FwStatsTimestampNS != psFwSysData->ui64FwStatsTimestampNS));
}

static void _RGXProcessBasicUtilStats(RGX_GPU_UTIL_STATS *psReturnStats,
									  IMG_UINT64 ui64GpuActiveTimeNS,
									  IMG_UINT64 ui64FwStatsTimestampNS,
									  IMG_BOOL bGpuActive,
									  PVRSRV_DEVICE_NODE *psDeviceNode)
{
	IMG_UINT64 ui64MeasurementPeriodNS, ui64GpuActivePeriodNS, ui64OsTimeNS;
	IMG_BOOL bFwOverestimate;

#if !defined(SUPPORT_SOC_TIMER)
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	ui64OsTimeNS = OSClockns64();
#else
	ui64OsTimeNS = RGXTimeGetSOCTimerValueNS(psDeviceNode);
#endif
	bFwOverestimate = (ui64OsTimeNS - ui64FwStatsTimestampNS) > (ui64FwStatsTimestampNS - ui64OsTimeNS);
#if defined(SUPPORT_LINUX_DVFS)
	IMG_DVFS_DEVICE *psDVFSDevice = &psDeviceNode->psDevConfig->sDVFS.sDVFSDevice;
	IMG_BOOL bSuspended = psDVFSDevice->suspend_duration >
						  (psDVFSDevice->off_period_ms * 1000000);
#endif

	/* account for the Firmware's activity since the last utilisation check */
	if (bGpuActive && !bFwOverestimate)
	{
		/*
		 * Estimated GPU active time since the last firmware stats update,
		 * allowing adjustments before updating the cumulative active time.
		 */
		IMG_UINT64 ui64EstActivePeriodNS = ui64OsTimeNS - ui64FwStatsTimestampNS;
#if defined(SUPPORT_LINUX_DVFS)
		if (bSuspended)
		{
			ui64EstActivePeriodNS -= MIN(ui64EstActivePeriodNS, psDVFSDevice->suspend_duration);
		}
#endif
		ui64GpuActiveTimeNS += ui64EstActivePeriodNS;
	}

	ui64MeasurementPeriodNS = ui64OsTimeNS - psReturnStats->ui64LastCheckTimestampNS;

#if defined(SUPPORT_LINUX_DVFS)
	if (bSuspended)
	{
		ui64MeasurementPeriodNS -= MIN(ui64MeasurementPeriodNS, psDVFSDevice->suspend_duration);
		psDVFSDevice->suspend_duration = 0;
	}
#endif

	/*
	 * Clamp the active time to be at least the previous baseline to prevent underflow.
	 * If an underflow is avoided, we calculate the active duration normally;
	 * otherwise, we set the active period to 0.
	 */
	ui64GpuActivePeriodNS = MAX(ui64GpuActiveTimeNS, psReturnStats->ui64LastGpuActiveTimeNS) -
							psReturnStats->ui64LastGpuActiveTimeNS;

	/*
	 * usage = active period / measurement period:
	 * The measurement period is the difference between OS timestamps.
	 * The active period is the difference between two snapshots of the
	 * Firmware's GPU active time counter. The timestamps the firmware uses
	 * to update the counters are obtained by correlating the GPU Timer
	 * with the OS timer. This estimation has a margin of error.
	 * Due to this inaccuracy, we can end up with active > measured here.
	 * A consequence of one active period being longer than in reality,
	 * is that the following period might end up shorter.
	 * To mitigate this, when active > measured time, deduct the excess
	 * difference from the active time saved for the next period
	 * computation.
	 * This way the current active cycle is 100% (active=measured) and the
	 * the difference will carry over to the next cycle. Limit the maximum
	 * carry over amount to the length of the current measurement period.
	 */
	if (ui64GpuActivePeriodNS > ui64MeasurementPeriodNS)
	{
		IMG_UINT64 ui64ActiveTimeDiff = ui64GpuActivePeriodNS - ui64MeasurementPeriodNS;

		ui64GpuActiveTimeNS -= MIN(ui64ActiveTimeDiff, ui64MeasurementPeriodNS);
		ui64GpuActivePeriodNS = ui64MeasurementPeriodNS;
	}

	psReturnStats->ui64LastCheckTimestampNS = ui64OsTimeNS;
	psReturnStats->ui64LastGpuActiveTimeNS = ui64GpuActiveTimeNS;
	psReturnStats->ui64GpuActivePeriodNS = ui64GpuActivePeriodNS;
	psReturnStats->ui64MeasurementPeriodNS = ui64MeasurementPeriodNS;

	if (ui64MeasurementPeriodNS == 0)
	{
		psReturnStats->bBasicStatsValid = IMG_FALSE;
	}
	else
	{
		psReturnStats->bBasicStatsValid = IMG_TRUE;

		/* The active > measured check above ensures the division result can't exceed 100 */
		psReturnStats->ui32GpuUsage =
			(IMG_UINT32) OSDivide64By64((ui64GpuActivePeriodNS * 100), ui64MeasurementPeriodNS);
	}
}

/* Basic GPU activity tracking: can be called from a kernel IRQ context */
PVRSRV_ERROR RGXGetGpuBasicUtilStats(PVRSRV_DEVICE_NODE *psDeviceNode,
								RGX_GPU_UTIL_STATS *psReturnStats)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	IMG_UINT64 ui64GpuActiveTimeNS, ui64FwStatsTimestampNS;
	IMG_BOOL bGpuActive;
	OS_SPINLOCK_FLAGS uiFlags = 0;

	OSSpinLockAcquire(psReturnStats->hSpinlock, uiFlags);

	_RGXGetGpuBasicUtilData(psDevInfo->psRGXFWIfFwSysData,
							&ui64GpuActiveTimeNS,
							&ui64FwStatsTimestampNS,
							&bGpuActive);

	_RGXProcessBasicUtilStats(psReturnStats,
							  ui64GpuActiveTimeNS,
							  ui64FwStatsTimestampNS,
							  bGpuActive,
							  psDeviceNode);

	OSSpinLockRelease(psReturnStats->hSpinlock, uiFlags);

	return PVRSRV_OK;
}

static void _RGXProcessDetailedUtilStats(PVRSRV_RGXDEV_INFO *psDevInfo,
										 RGX_GPU_UTIL_STATS *psReturnStats,
										 IMG_UINT64 ui64GpuTimeTicks)
{
	IMG_UINT32 ui32DriverID;
	RGXFWIF_DM eDM;
	IMG_BOOL bDetailedStatsValid = IMG_TRUE;
	IMG_UINT64 ui64MeasurementPeriodTicks;

	ui64MeasurementPeriodTicks = ui64GpuTimeTicks - psReturnStats->ui64LastCheckTimestampTicks;

	for (eDM = 0; (eDM < RGXFWIF_GPU_UTIL_DM_MAX) && bDetailedStatsValid; eDM++)
	{
		if (ui64MeasurementPeriodTicks > IMG_UINT32_MAX || ui64MeasurementPeriodTicks == 0)
		{
			/* The lower 32 bits of the GPU Timer value have overflowed since the last reading.
			 * The computed values can't be correct */
			bDetailedStatsValid = IMG_FALSE;
			break;
		}
		else
		{
			FOREACH_ACTIVE_DRIVER(psDevInfo, ui32DriverID)
			{
				IMG_UINT64 ui64DmActivePeriodTicks = psReturnStats->aaui32DmActiveTimeTicksCurrent[eDM][ui32DriverID] -
													 psReturnStats->aaui32DmActiveTimeTicksPrev[eDM][ui32DriverID];

				if (ui64DmActivePeriodTicks > ui64MeasurementPeriodTicks)
				{
					IMG_UINT64 ui64ActiveTimeDiff = ui64DmActivePeriodTicks - ui64MeasurementPeriodTicks;

					ui64GpuTimeTicks -= MIN(ui64ActiveTimeDiff, ui64MeasurementPeriodTicks);
					ui64DmActivePeriodTicks = ui64MeasurementPeriodTicks;
				}

				if (ui64MeasurementPeriodTicks == 0)
				{
					bDetailedStatsValid = IMG_FALSE;
					break;
				}
				else
				{
					/* The active > measured check above ensures the division result can't exceed 100 */
					psReturnStats->aaui32DriverDmUsage[eDM][ui32DriverID] =
						(IMG_UINT32) OSDivide64By64((ui64DmActivePeriodTicks * 100),
													ui64MeasurementPeriodTicks);
					psReturnStats->ui64MeasurementPeriodTicks = ui64MeasurementPeriodTicks;
				}
			}
			END_FOREACH_ACTIVE_DRIVER
		}
	}

	/* The state of the DM changed since the last read.
	 * End the current measurement period and start another. */
	psReturnStats->ui64LastCheckTimestampTicks = ui64GpuTimeTicks;

	psReturnStats->bDetailedStatsValid = bDetailedStatsValid;

	/* update the previous copy */
	memcpy(&psReturnStats->aaui32DmActiveTimeTicksPrev,
		   &psReturnStats->aaui32DmActiveTimeTicksCurrent,
		   sizeof(psReturnStats->aaui32DmActiveTimeTicksCurrent));
}

/* Detailed per-DM/per-VM activity tracking: can't be called from a kernel IRQ context */
PVRSRV_ERROR RGXGetGpuDetailedUtilStats(PVRSRV_DEVICE_NODE *psDeviceNode,
										RGX_GPU_UTIL_STATS *psReturnStats)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;
	IMG_UINT64 ui64GpuTimeTicks;
	OS_SPINLOCK_FLAGS uiFlags = 0;
	IMG_UINT64 ui64GpuActiveTimeNS, ui64FwStatsTimestampNS;
	IMG_BOOL bGpuActive;

	OSLockAcquire(psDevInfo->hGPUUtilLock);

	if (psDevInfo->bRGXPowered)
	{
		RGXFWIF_KCCB_CMD sCmpKCCBCmd = { 0 };
		IMG_UINT32 ui32kCCBCommandSlot;
		sCmpKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_EXPORT_DETAILED_UTIL_STATS;

		/* The Firmware only exports the detailed statistics to shared memory
		 * when explicitly requested via KCCB command due to the performance
		 * penalty associated with accessing system memory. */
		LOOP_UNTIL_TIMEOUT_US(MAX_HW_TIME_US)
		{
			eError = RGXScheduleCommandAndGetKCCBSlotPwrManaged(psDevInfo,
																RGXFWIF_DM_GP,
																&sCmpKCCBCmd,
																PDUMP_FLAGS_CONTINUOUS,
																&ui32kCCBCommandSlot);

			if ((eError != PVRSRV_ERROR_RETRY) &&
				(eError != PVRSRV_ERROR_KERNEL_CCB_FULL))
			{
				break;
			}
			OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT_US();

		if (eError == PVRSRV_OK)
		{
			/* Wait for FW to process the cmd */
			eError = RGXWaitForKCCBSlotUpdate(psDevInfo, ui32kCCBCommandSlot, PDUMP_FLAGS_CONTINUOUS);
		}

		if (eError != PVRSRV_OK)
		{
			OSLockRelease(psDevInfo->hGPUUtilLock);
			OSSpinLockAcquire(psReturnStats->hSpinlock, uiFlags);
			psReturnStats->bDetailedStatsValid = IMG_FALSE;
			psReturnStats->bBasicStatsValid = IMG_FALSE;
			OSSpinLockRelease(psReturnStats->hSpinlock, uiFlags);

			return eError;
		}
	}

#if defined (SUPPORT_SOC_TIMER)
	eError = PVRSRVRGXCurrentTime(NULL, psDeviceNode, RGX_QUERY_SOC_TIMER, &ui64GpuTimeTicks);
#else
	eError = PVRSRVRGXCurrentTime(NULL, psDeviceNode, RGX_QUERY_DEVICE_TIMESTAMP, &ui64GpuTimeTicks);
#endif
	PVR_ASSERT(eError == PVRSRV_OK);

	OSSpinLockAcquire(psReturnStats->hSpinlock, uiFlags);

	_RGXGetGpuBasicUtilData(psDevInfo->psRGXFWIfFwSysData,
		&ui64GpuActiveTimeNS,
		&ui64FwStatsTimestampNS,
		&bGpuActive);

	memcpy(&psReturnStats->aaui32DmActiveTimeTicksCurrent,
		   &psFwSysData->aaui32DmActiveTimeTicks,
		   sizeof(psReturnStats->aaui32DmActiveTimeTicksCurrent));

	OSLockRelease(psDevInfo->hGPUUtilLock);

	_RGXProcessBasicUtilStats(psReturnStats,
							  ui64GpuActiveTimeNS,
							  ui64FwStatsTimestampNS,
							  bGpuActive,
							  psDeviceNode);

	_RGXProcessDetailedUtilStats(psDevInfo, psReturnStats, ui64GpuTimeTicks);

	OSSpinLockRelease(psReturnStats->hSpinlock, uiFlags);

	return eError;
}

static INLINE PVRSRV_ERROR _RGXDoStop(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVNODE, psDeviceNode, PVRSRV_OK);

#if defined(SUPPORT_TRUSTED_DEVICE) && !defined(NO_HARDWARE) && !defined(SUPPORT_SECURITY_VALIDATION)
	if (psDeviceNode->psDevConfig->pfnTDRGXStop == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXPrePowerState: TDRGXStop not implemented!"));
		return PVRSRV_ERROR_NOT_IMPLEMENTED;
	}

	eError = psDeviceNode->psDevConfig->pfnTDRGXStop(psDeviceNode->psDevConfig->hSysData);
	psDevInfo->bRGXPowered = IMG_FALSE;
#else
	eError = RGXStop(&psDevInfo->sLayerParams);
#endif

	return eError;
}

/*************************************************************************/ /*!
@Function       _RGXSendPowerOffKick
@Description    Send a KCCB kick to power off the GPU FW. This function will wait
                for completion of the command before exiting.

@Input          psDeviceNode    The device node struct associated with the GPU.
@Input          bForce          A boolean indicating if the power off command
                                should be forced.

@Return         Failure code if the virtual address is invalid.
*/ /**************************************************************************/
static PVRSRV_ERROR _RGXSendPowerOffKick(PVRSRV_DEVICE_NODE *psDeviceNode,
                                        IMG_BOOL bForce)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	RGXFWIF_KCCB_CMD sPowCmd;
	IMG_UINT32 ui32CmdKCCBSlot;

	PVR_ASSERT(psDeviceNode != NULL);
	PVR_ASSERT(psDeviceNode->pvDevice != NULL);

	psDevInfo = psDeviceNode->pvDevice;

	/* Send the Power off request to the FW */
	sPowCmd.eCmdType = RGXFWIF_KCCB_CMD_POW;
	sPowCmd.ui32KCCBFlags = 0;
	sPowCmd.uCmdData.sPowData.ePowType = RGXFWIF_POW_OFF_REQ;
	sPowCmd.uCmdData.sPowData.uPowerReqData.bForced = bForce;

	return RGXSendPowerCommandAndWaitOnCompletion(psDevInfo,
												  &sPowCmd,
												  PDUMP_FLAGS_NONE,
												  __func__,
												  &ui32CmdKCCBSlot);
}

/*************************************************************************/ /*!
@Function       _RGXFinalisePowerOff
@Description    Finalises the GPU power transition to off.

@Input          psDeviceNode    The device node struct associated with the GPU.

@Return         Result code indicating the success or reason for failure.
*/ /**************************************************************************/
static PVRSRV_ERROR _RGXFinalisePowerOff(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PVRSRV_ERROR eError = PVRSRV_OK;

#if !defined(NO_HARDWARE) && !defined(SUPPORT_SYNC_IRQ)
	IMG_UINT32 ui32idx;
#endif

	PVR_ASSERT(psDeviceNode != NULL);
	PVR_ASSERT(psDeviceNode->pvDevice != NULL);

	psDevInfo = psDeviceNode->pvDevice;

#if !defined(NO_HARDWARE)
	/* Driver takes the VZ Fw-KM connection down, preventing the
		* firmware from submitting further interrupts */
	KM_SET_OS_CONNECTION(OFFLINE, psDevInfo);
	KM_CONNECTION_CACHEOP(Os, FLUSH);

#if defined(SUPPORT_SYNC_IRQ)
	/* Wait for the pending IRQ handlers to complete. */
	OSSyncIRQ(psDeviceNode->psDevConfig->ui32IRQ);
#else
#if defined(RGX_FW_IRQ_OS_COUNTERS)
	ui32idx = RGXFW_HOST_DRIVER_ID;
#else
	for_each_irq_cnt(ui32idx)
#endif /* RGX_FW_IRQ_OS_COUNTERS */
	{
		IMG_UINT32 ui32IrqCnt;

		get_irq_cnt_val(ui32IrqCnt, ui32idx, psDevInfo);

		/* Wait for the pending FW processor to host interrupts to come back. */
		eError = PVRSRVPollForValueKM(psDeviceNode,
		                              (IMG_UINT32 __iomem *)&psDevInfo->aui32SampleIRQCount[ui32idx],
		                              ui32IrqCnt,
		                              0xffffffff,
		                              POLL_FLAG_LOG_ERROR | POLL_FLAG_DEBUG_DUMP,
		                              NULL);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Wait for pending interrupts failed (DevID %u)." MSG_IRQ_CNT_TYPE " %u Host: %u, FW: %u",
			         __func__,
			         psDeviceNode->sDevId.ui32InternalID,
			         ui32idx,
			         psDevInfo->aui32SampleIRQCount[ui32idx],
			         ui32IrqCnt));

			RGX_WaitForInterruptsTimeout(psDevInfo);
#if !defined(RGX_FW_IRQ_OS_COUNTERS)
			break;
#endif
		}
	}
#endif /* SUPPORT_SYNC_IRQ */
#endif /* NO_HARDWARE */

	/* Update GPU frequency and timer correlation related data */
	RGXTimeCorrEnd(psDeviceNode, RGXTIMECORR_EVENT_POWER);

#if defined(SUPPORT_LINUX_DVFS)
	eError = SuspendDVFS(psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to suspend DVFS", __func__));
		return eError;
	}
#endif

#if defined(PVRSRV_ANDROID_TRACE_GPU_FREQ)
	/* On frequency traces, 0 means "core logic is off" */
	trace_gpu_frequency(0, psDevInfo->psDeviceNode->sDevId.ui32InternalID);
#if defined(SUPPORT_PDVFS)
	trace_clock_set_rate("gpu_util", 0, raw_smp_processor_id());
#endif
#endif /* defined(PVRSRV_ANDROID_TRACE_GPU_FREQ) */

	eError = _RGXDoStop(psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		/* Power down failures are treated as successful since the power was removed but logged. */
		PVR_DPF((PVR_DBG_WARNING, "%s: _RGXDoStop failed (%s)",
		         __func__, PVRSRVGetErrorString(eError)));
		psDevInfo->ui32ActivePMReqNonIdle++;
		eError = PVRSRV_OK;
	}

	return eError;
}

/*************************************************************************/ /*!
@Function       RGXPrePowerState
@Description    Initial step for setting power state, to be followed by
                RGXPostPowerState.

@Input          psDeviceNode        The device node struct associated with the GPU.
@Input          eNewPowerState      The power state the GPU is to transition to.
@Input          eCurrentPowerState  The current power state of the GPU.
@Input          ePwrFlags           Flags indicating the behaviour of the transition.

@Return         Result code indicating the success or reason for failure.
*/ /**************************************************************************/
PVRSRV_ERROR RGXPrePowerState(PVRSRV_DEVICE_NODE *psDeviceNode,
                              PVRSRV_DEV_POWER_STATE eNewPowerState,
                              PVRSRV_DEV_POWER_STATE eCurrentPowerState,
                              PVRSRV_POWER_FLAGS ePwrFlags)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = (PVRSRV_RGXDEV_INFO*)psDeviceNode->pvDevice;
	const RGXFWIF_SYSDATA *psFwSysData;
#if defined(PVRSRV_FORCE_UNLOAD_IF_BAD_STATE)
	IMG_BOOL              bDeviceOk;
#endif
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT(psDevInfo != NULL);

	psFwSysData = psDevInfo->psRGXFWIfFwSysData;

	if ((eNewPowerState == eCurrentPowerState) ||
	    (eNewPowerState == PVRSRV_DEV_POWER_STATE_ON))
	{
		return PVRSRV_OK;
	}

#if defined(PVRSRV_FORCE_UNLOAD_IF_BAD_STATE)
	bDeviceOk = (OSAtomicRead(&psDeviceNode->eHealthStatus) == PVRSRV_DEVICE_HEALTH_STATUS_OK);
	if (bDeviceOk)
#endif
	{
		IMG_BOOL bForce = IMG_FALSE;

		if (BITMASK_HAS(ePwrFlags, PVRSRV_POWER_FLAGS_FORCED))
		{
			bForce = IMG_TRUE;
		}

		trace_event_begin("RGXSendPowerOffKick");
		eError = _RGXSendPowerOffKick(psDeviceNode, bForce);
		trace_event_end();
		if (eError == PVRSRV_ERROR_TIMEOUT)
		{
			/* timeout waiting for the FW to ack the request: return timeout */
			PVR_DPF((PVR_DBG_WARNING,
			         "%s: Timeout waiting for powoff ack from the FW",
			         __func__));
			return eError;
		}
		else if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Error waiting for powoff ack from the FW (%s)",
			         __func__, PVRSRVGetErrorString(eError)));
			return PVRSRV_ERROR_DEVICE_POWER_CHANGE_FAILURE;
		}
	}

	/* Check the Power state after the answer */
	RGXFwSharedMemCacheOpValue(psFwSysData->ePowState, INVALIDATE);
	if ((psFwSysData->ePowState != RGXFWIF_POW_OFF)
#if defined(PVRSRV_FORCE_UNLOAD_IF_BAD_STATE)
	    && (bDeviceOk)
#endif
		)
	{
		if (BITMASK_HAS(ePwrFlags, PVRSRV_POWER_FLAGS_FORCED))
		{	/* It is an error for a forced request to be denied */
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Failure to power off during a forced power off. FW: %d",
			         __func__, psFwSysData->ePowState));
		}

		/* the sync was updated but the pow state isn't off -> the FW denied the transition */
		return PVRSRV_ERROR_DEVICE_POWER_CHANGE_DENIED;
	}

	return _RGXFinalisePowerOff(psDeviceNode);
}

#if defined(RGX_SUPPORT_DUAL_MEMCTX) && !defined(PDUMP)
static void _RGXSetOsDataDualCtxFlag(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, ALBIORIX_TOP_INFRASTRUCTURE))
	{
		RGXFwSharedMemCacheOpValue(psDevInfo->psRGXFWIfFwOsData->ui32FwOsDataFlags, INVALIDATE);
		if (BITMASK_HAS(psDevInfo->psRGXFWIfFwOsData->ui32FwOsDataFlags,
		                RGXFWIF_OSDATA_FLAG_FW_SUPPORTS_DUALCTX))
		{
			RGXFwSharedMemCacheOpValue(psDevInfo->psRGXFWIfFwOsData->ui32FwOsConfigFlags, INVALIDATE);
			/* Set the flag to let FW know that KM configured the dual-ctx PC address */
			BITMASK_SET(psDevInfo->psRGXFWIfFwOsData->ui32FwOsConfigFlags,
			            RGXFWIF_INICFG_OS_KM_SUPPORTS_DUALCTX);
			RGXFwSharedMemCacheOpValue(psDevInfo->psRGXFWIfFwOsData->ui32FwOsConfigFlags, FLUSH);
		}
	}
}
#endif

#if defined(SUPPORT_AUTOVZ)
static PVRSRV_ERROR _RGXWaitForGuestsToDisconnect(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR eError = PVRSRV_ERROR_TIMEOUT;
	IMG_UINT32 ui32FwTimeoutUs = (20 * SECONDS_TO_MICROSECONDS);
	IMG_UINT32 ui32FwAutoVzWdgKickMs = MIN(10, PVR_AUTOVZ_WDG_KICK_PERIOD_MS);

	LOOP_UNTIL_TIMEOUT_US(ui32FwTimeoutUs)
	{
		IMG_UINT32 ui32DriverID;
		IMG_BOOL bGuestOnline = IMG_FALSE;

		RGXFwSharedMemCacheOpValue(psDevInfo->psRGXFWIfFwSysData->aui32OsRuntimeFlagsMirror,
		                           INVALIDATE);

		for (ui32DriverID = RGXFW_GUEST_DRIVER_ID_START;
			 ui32DriverID < RGX_NUM_DRIVERS_SUPPORTED; ui32DriverID++)
		{
			IMG_UINT32 ui32GuestRunFlags = psDevInfo->psRGXFWIfFwSysData->aui32OsRuntimeFlagsMirror[ui32DriverID];
			RGXFWIF_CONNECTION_FW_STATE eGuestState = (RGXFWIF_CONNECTION_FW_STATE)
					((ui32GuestRunFlags & ~RGXFWIF_OS_RUNTIME_FLAGS_OS_STATE_CLRMSK) >> RGXFWIF_OS_RUNTIME_FLAGS_OS_STATE_SHIFT);

			if ((eGuestState == RGXFW_CONNECTION_FW_ACTIVE) ||
				(eGuestState == RGXFW_CONNECTION_FW_GRACEFUL_OFFLOAD) ||
				(eGuestState == RGXFW_CONNECTION_FW_FORCED_OFFLOAD))
			{
				bGuestOnline = IMG_TRUE;
				PVR_DPF((PVR_DBG_WARNING, "%s: Guest OS %u still online.", __func__, ui32DriverID));
			}
		}

		if (!bGuestOnline)
		{
			PVR_DPF((PVR_DBG_MESSAGE, "%s: All Guest connections are down. "
									  "Host can power down the GPU.", __func__));
			eError = PVRSRV_OK;
			break;
		}
		else
		{
			PVR_DPF((PVR_DBG_WARNING, "%s: Waiting for Guests to disconnect "
									  "before powering down GPU.", __func__));

			/* Handle the AutoVz watchdog in this loop, as the kernel vz wdg
			 * thread will not have read access to the power lock while this
			 * sequence runs. The fw-km connection must be kept alive until
			 * the host shuts everything down gracefully. */
			RGXUpdateAutoVzWdgToken(psDevInfo);
		}

		OSSleepms(ui32FwAutoVzWdgKickMs);
	} END_LOOP_UNTIL_TIMEOUT_US();

	/* Allow Guests to finish reading Connection state registers before disconnecting. */
	OSSleepms(ui32FwAutoVzWdgKickMs);
	RGXUpdateAutoVzWdgToken(psDevInfo);

	return eError;
}
#endif /* defined(SUPPORT_AUTOVZ) */

/*
	RGXVzPrePowerState
*/
PVRSRV_ERROR RGXVzPrePowerState(PVRSRV_DEVICE_NODE		*psDeviceNode,
                                PVRSRV_DEV_POWER_STATE	eNewPowerState,
                                PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
                                PVRSRV_POWER_FLAGS		ePwrFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_LOG_RETURN_IF_FALSE((eNewPowerState != eCurrentPowerState), "no power change", eError);

	if (eNewPowerState != PVRSRV_DEV_POWER_STATE_ON)
	{
		/* powering down */
#if defined(SUPPORT_AUTOVZ)
		if (PVRSRV_VZ_MODE_IS(HOST, DEVNODE, psDeviceNode) && (!psDeviceNode->bAutoVzFwIsUp || psDeviceNode->bAutoVzAllowGPUPowerdown))
		{
			if (psDeviceNode->bAutoVzFwIsUp)
			{
				/* bAutoVzAllowGPUPowerdown must be TRUE here and
				 * bAutoVzFwIsUp=TRUE indicates that this is a powerdown event
				 * so send requests to the FW to disconnect all guest connections
				 * before GPU is powered down. */
				eError = RGXDisconnectAllGuests(psDeviceNode);
				PVR_LOG_RETURN_IF_ERROR(eError, "RGXDisconnectAllGuests");
			}

			/* The Host must ensure all Guest drivers have disconnected from the GPU before powering it down.
			 * Guest drivers regularly access hardware registers during runtime. If an attempt is made to
			 * access a GPU register while the GPU is down, the SoC might lock up. */
			eError = _RGXWaitForGuestsToDisconnect(psDeviceNode);
			PVR_LOG_RETURN_IF_ERROR(eError, "_RGXWaitForGuestsToDisconnect");

			if (psDeviceNode->bAutoVzAllowGPUPowerdown)
			{
				psDeviceNode->bAutoVzFwIsUp = IMG_FALSE;
			}

			/* Temporarily restore all power callbacks used by the driver to fully power down the GPU.
			 * Under AutoVz, power transitions requests (e.g. on driver deinitialisation and unloading)
			 * are generally ignored and the GPU power state is unaffected. Special power requests like
			 * those triggered by Suspend/Resume calls must reinstate the callbacks when needed. */
			PVRSRVSetPowerCallbacks(psDeviceNode, psDeviceNode->psPowerDev,
									&RGXVzPrePowerState, &RGXVzPostPowerState,
									psDeviceNode->psDevConfig->pfnPrePowerState,
									psDeviceNode->psDevConfig->pfnPostPowerState,
									&RGXForcedIdleRequest, &RGXCancelForcedIdleRequest,
									&RGXCancelForcedIdleRequestAsync);
		}
		else
		{
			PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

			KM_CONNECTION_CACHEOP(Fw, INVALIDATE);
			KM_CONNECTION_CACHEOP(Os, INVALIDATE);

			if (KM_FW_CONNECTION_IS(ACTIVE, psDevInfo) &&
				KM_OS_CONNECTION_IS(ACTIVE, psDevInfo))
			{
				PVRSRV_ERROR eError = RGXFWSetFwOsState(psDevInfo, 0, RGXFWIF_OS_OFFLINE);
				PVR_LOG_RETURN_IF_ERROR(eError, "RGXFWSetFwOsState");
			}
		}

		PVR_LOG(("%s: %s driver powering down: bAutoVzFwIsUp = %s",
								__func__, PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode)? "GUEST" : "HOST",
								psDeviceNode->bAutoVzFwIsUp ? "TRUE" : "FALSE"));
#endif
	}
	else if (eCurrentPowerState != PVRSRV_DEV_POWER_STATE_ON)
	{
#if defined(SUPPORT_AUTOVZ)
		/* powering up */
		PVR_LOG(("%s: %s driver powering up: bAutoVzFwIsUp = %s",
								__func__, PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode)? "GUEST" : "HOST",
								psDeviceNode->bAutoVzFwIsUp ? "TRUE" : "FALSE"));
#endif
	}

	if (!(PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode) || (psDeviceNode->bAutoVzFwIsUp)))
	{
		/* call regular device power function */
		eError = RGXPrePowerState(psDeviceNode, eNewPowerState, eCurrentPowerState, ePwrFlags);
	}

	return eError;
}

#if defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS)
static PVRSRV_ERROR _RGXVzWaitFirmwareReady(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	KM_CONNECTION_CACHEOP(Fw, INVALIDATE);
	if (!KM_FW_CONNECTION_IS(READY, psDevInfo))
	{
		PVR_LOG(("%s: Firmware Connection is not in Ready state. Waiting for Firmware ...", __func__));
	}

	LOOP_UNTIL_TIMEOUT_US(RGX_VZ_CONNECTION_TIMEOUT_US)
	{
		KM_CONNECTION_CACHEOP(Fw, INVALIDATE);
		if (KM_FW_CONNECTION_IS(READY, psDevInfo))
		{
			PVR_LOG(("%s: Firmware Connection is Ready. Initialisation proceeding.", __func__));
			break;
		}
		else
		{
			OSSleepms(10);
		}
	} END_LOOP_UNTIL_TIMEOUT_US();

	KM_CONNECTION_CACHEOP(Fw, INVALIDATE);
	if (!KM_FW_CONNECTION_IS(READY, psDevInfo))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Timed out waiting for the Firmware to enter Ready state.", __func__));
		return PVRSRV_ERROR_TIMEOUT;
	}

	return PVRSRV_OK;
}
#endif

/*
	RGXVzPostPowerState
*/
PVRSRV_ERROR RGXVzPostPowerState(PVRSRV_DEVICE_NODE		*psDeviceNode,
                                 PVRSRV_DEV_POWER_STATE	eNewPowerState,
                                 PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
                                 PVRSRV_POWER_FLAGS		ePwrFlags)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	PVR_LOG_RETURN_IF_FALSE((eNewPowerState != eCurrentPowerState), "no power change", eError);

	if (!(PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode) || (psDeviceNode->bAutoVzFwIsUp)))
	{
		if (eCurrentPowerState != PVRSRV_DEV_POWER_STATE_ON)
		{
			KM_SET_OS_CONNECTION(READY, psDevInfo);
			KM_CONNECTION_CACHEOP(Os, FLUSH);
		}
		/* call regular device power function */
		eError = RGXPostPowerState(psDeviceNode, eNewPowerState, eCurrentPowerState, ePwrFlags);
	}
	else
	{
		KM_SET_OS_CONNECTION(OFFLINE, psDevInfo);
		KM_CONNECTION_CACHEOP(Os, FLUSH);
	}

	if (eNewPowerState != PVRSRV_DEV_POWER_STATE_ON)
	{
		/* powering down */
		if (psDeviceNode->bAutoVzFwIsUp)
		{
			PVR_LOG(("%s: AutoVz Fw active, power not changed", __func__));
			return eError;
		}

#if defined(SUPPORT_AUTOVZ)
		PVR_LOG(("%s: %s driver powering down: bAutoVzFwIsUp = %s",
								__func__, PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode)? "GUEST" : "HOST",
								psDeviceNode->bAutoVzFwIsUp ? "TRUE" : "FALSE"));
#endif

#if !defined(SUPPORT_AUTOVZ_HW_REGS)
		/* The connection states must be reset on a GPU power cycle. If the states are kept
		 * in hardware scratch registers, they will be cleared on power down. When using shared
		 * memory the connection data must be explicitly cleared by the driver. */
		OSCachedMemSetWMB(psDevInfo->psRGXFWIfConnectionCtl, 0, sizeof(RGXFWIF_CONNECTION_CTL));
		RGXFwSharedMemCacheOpPtr(psDevInfo->psRGXFWIfConnectionCtl, FLUSH);
#endif /* defined(SUPPORT_AUTOVZ) && !defined(SUPPORT_AUTOVZ_HW_REGS) */

		if (PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode) || (psDeviceNode->bAutoVzFwIsUp))
		{
#if defined(SUPPORT_AUTOVZ)
			/* AutoVz Guests attempting to suspend have updated their connections earlier in RGXVzPrePowerState.
			 * Skip this redundant register write, as the Host could have powered down the GPU by now. */
			if (psDeviceNode->bAutoVzFwIsUp)
#endif
			{
				/* Take the VZ connection down to prevent firmware from submitting further interrupts */
				KM_SET_OS_CONNECTION(OFFLINE, psDevInfo);
				KM_CONNECTION_CACHEOP(Os, FLUSH);
			}
			/* Power transition callbacks were not executed, update RGXPowered flag here */
			psDevInfo->bRGXPowered = IMG_FALSE;
		}
	}
	else if (eCurrentPowerState != PVRSRV_DEV_POWER_STATE_ON)
	{
		/* powering up */
		IMG_UINT32 ui32FwTimeout = (3 * SECONDS_TO_MICROSECONDS);
		volatile IMG_BOOL *pbUpdatedFlag;
		RGXFwSharedMemCacheOpValue(psDevInfo->psRGXFWIfOsInit->sRGXCompChecks.bUpdated,
		                           INVALIDATE);
		pbUpdatedFlag = &psDevInfo->psRGXFWIfOsInit->sRGXCompChecks.bUpdated;

#if defined(SUPPORT_AUTOVZ)
		PVR_LOG(("%s: %s driver powering up: bAutoVzFwIsUp = %s",
								__func__, PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode)? "GUEST" : "HOST",
								psDeviceNode->bAutoVzFwIsUp ? "TRUE" : "FALSE"));
#endif

		if (PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode))
		{
			/* Guests don't execute the power transition callbacks, so update their RGXPowered flag here */
			psDevInfo->bRGXPowered = IMG_TRUE;

#if defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS)
			/* Guest drivers expect the firmware to have set its end of the
			 * connection to Ready state by now. */
			eError = _RGXVzWaitFirmwareReady(psDevInfo);
			PVR_LOG_RETURN_IF_ERROR(eError, "_RGXVzWaitFirmwareReady()");
#endif /* RGX_VZ_STATIC_CARVEOUT_FW_HEAPS */

			OSWriteDeviceMem32WithWMB(pbUpdatedFlag, IMG_FALSE);

			/* Kick an initial dummy command to make the firmware initialise all
			 * its internal guest OS data structures and compatibility information.
			 * Use the lower-level RGXSendCommand() for the job, to make
			 * sure only 1 KCCB command is issued to the firmware.
			 * The default RGXFWHealthCheckCmd() prefaces each HealthCheck command with
			 * a pre-kick cache command which can interfere with the FW-KM init handshake. */
			{
				RGXFWIF_KCCB_CMD sCmpKCCBCmd = {0};
				sCmpKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_HEALTH_CHECK;

				KM_SET_OS_CONNECTION(READY, psDevInfo);
				KM_CONNECTION_CACHEOP(Os, FLUSH);

				eError = RGXSendCommand(psDevInfo, &sCmpKCCBCmd, PDUMP_FLAGS_CONTINUOUS);
				PVR_LOG_RETURN_IF_ERROR(eError, "RGXSendCommand()");
			}
		}
		else
		{
#if defined(SUPPORT_AUTOVZ)
			/* Disable power callbacks that should not be run on virtualised drivers after the GPU
			 * is fully initialised: system layer pre/post functions and driver idle requests.
			 * The original device RGX Pre/Post functions are called from this Vz wrapper. */
			PVRSRVSetPowerCallbacks(psDeviceNode, psDeviceNode->psPowerDev,
									&RGXVzPrePowerState, &RGXVzPostPowerState,
									NULL, NULL, NULL, NULL, NULL);

			/* AutoVz Host driver reconnecting to running Firmware */
			if (psDeviceNode->bAutoVzFwIsUp)
			{
				/* Firmware already running, send a KCCB command to establish the new connection */
				RGXFWIF_KCCB_CMD sCmpKCCBCmd = {0};
				sCmpKCCBCmd.eCmdType = RGXFWIF_KCCB_CMD_HEALTH_CHECK;

				eError = _RGXVzWaitFirmwareReady(psDevInfo);
				PVR_LOG_RETURN_IF_ERROR(eError, "_RGXVzWaitFirmwareReady()");

				KM_SET_OS_CONNECTION(READY, psDevInfo);
				KM_CONNECTION_CACHEOP(Os, FLUSH);

				eError = RGXSendCommand(psDevInfo, &sCmpKCCBCmd, PDUMP_FLAGS_CONTINUOUS);
				PVR_LOG_RETURN_IF_ERROR(eError, "RGXSendCommand()");
			}
			else
			{
				/* During first-time boot the flag is set here, while subsequent reboots will already
				 * have set it earlier in RGXInit. Set to true from this point on. */
				psDeviceNode->bAutoVzFwIsUp = IMG_TRUE;
			}
#endif
		}

		/* Wait for the firmware to accept and enable the connection with this OS by setting its state to Active */
		LOOP_UNTIL_TIMEOUT_US(RGX_VZ_CONNECTION_TIMEOUT_US)
		{
			KM_CONNECTION_CACHEOP(Fw, INVALIDATE);
			if (KM_FW_CONNECTION_IS(ACTIVE, psDevInfo))
			{
				if (!PVRSRV_VZ_MODE_IS(NATIVE, DEVNODE, psDeviceNode))
				{
					PVR_LOG(("%s: Firmware Connection is Active."
							" Initialisation proceeding.", __func__));
				}
				break;
			}
			else
			{
				PVR_DPF((PVR_DBG_WARNING, "%s: Firmware Connection is not in Active state. Waiting for Firmware ...", __func__));
				OSSleepms(10);
			}
		} END_LOOP_UNTIL_TIMEOUT_US();

		KM_CONNECTION_CACHEOP(Fw, INVALIDATE);
		if (!KM_FW_CONNECTION_IS(ACTIVE, psDevInfo))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Timed out waiting for the Firmware to enter Active state.", __func__));
			return PVRSRV_ERROR_TIMEOUT;
		}

		if (PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode))
		{
			/* poll on the Firmware supplying the compatibility data */
			LOOP_UNTIL_TIMEOUT_US(ui32FwTimeout)
			{
				if (*pbUpdatedFlag)
				{
					break;
				}
				OSSleepms(10);
			} END_LOOP_UNTIL_TIMEOUT_US();

			PVR_LOG_RETURN_IF_FALSE(*pbUpdatedFlag, "Firmware does not respond with compatibility data.", PVRSRV_ERROR_TIMEOUT);

#if defined(RGX_SUPPORT_DUAL_MEMCTX) && !defined(PDUMP)
			_RGXSetOsDataDualCtxFlag(psDevInfo);
#endif
		}

		KM_SET_OS_CONNECTION(ACTIVE, psDevInfo);
		KM_CONNECTION_CACHEOP(Os, FLUSH);
	}

	return PVRSRV_OK;
}

#if defined(TRACK_FW_BOOT)
static INLINE void _RGXCheckFWBootStage(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	FW_BOOT_STAGE eStage;

#if defined(RGX_FEATURE_META_MAX_VALUE_IDX)
	if (RGX_IS_FEATURE_VALUE_SUPPORTED(psDevInfo, META))
	{
		/* Boot stage temporarily stored to the register below */
		eStage = OSReadHWReg32(psDevInfo->pvRegsBaseKM,
		                       RGX_FW_BOOT_STAGE_REGISTER);
	}
#if defined(RGX_FEATURE_MIPS_BIT_MASK)
	else if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, MIPS))
	{
		IMG_BYTE *pbBootData;

		if (PVRSRV_OK != DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWDataMemDesc,
		                                          (void**)&pbBootData))
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Could not acquire pointer to FW boot stage", __func__));
			eStage = FW_BOOT_STAGE_NOT_AVAILABLE;
		}
		else
		{
			pbBootData += RGXGetFWImageSectionOffset(NULL, MIPS_BOOT_DATA);

			eStage = *(FW_BOOT_STAGE*)&pbBootData[RGXMIPSFW_BOOT_STAGE_OFFSET];

			if (eStage == FW_BOOT_STAGE_TLB_INIT_FAILURE)
			{
				RGXMIPSFW_BOOT_DATA *psBootData =
					(RGXMIPSFW_BOOT_DATA*) (pbBootData + RGXMIPSFW_BOOTLDR_CONF_OFFSET);

				PVR_LOG(("MIPS TLB could not be initialised. Boot data info:"
						 " num PT pages %u, log2 PT page size %u, PT page addresses"
						 " %"IMG_UINT64_FMTSPECx " %"IMG_UINT64_FMTSPECx
						 " %"IMG_UINT64_FMTSPECx " %"IMG_UINT64_FMTSPECx,
						 psBootData->ui32PTNumPages,
						 psBootData->ui32PTLog2PageSize,
						 psBootData->aui64PTPhyAddr[0U],
						 psBootData->aui64PTPhyAddr[1U],
						 psBootData->aui64PTPhyAddr[2U],
						 psBootData->aui64PTPhyAddr[3U]));
			}

			DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWDataMemDesc);
		}
	}
#endif
	else
#endif
	{
		eStage = OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_SCRATCH14);
	}

	PVR_LOG(("%s: FW reached boot stage %i/%i.",
	         __func__, eStage, FW_BOOT_INIT_DONE));
}
#endif

static INLINE PVRSRV_ERROR _RGXDoStart(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
#if defined(SUPPORT_TRUSTED_DEVICE) && !defined(NO_HARDWARE) && !defined(SUPPORT_SECURITY_VALIDATION)
	PVRSRV_DEVICE_CONFIG *psDevConfig = psDeviceNode->psDevConfig;
#endif

	PVR_ASSERT(psDeviceNode->eCurrentSysPowerState == PVRSRV_SYS_POWER_STATE_ON);

#if defined(DEBUG)
	/*
	 * Jones/Sparrow domain must be powered before loading the FW.
	 */
	PVR_ASSERT( PVRSRVIsSystemPowered(psDeviceNode) == IMG_TRUE );
#endif

#if defined(SUPPORT_TRUSTED_DEVICE) && !defined(NO_HARDWARE) && !defined(SUPPORT_SECURITY_VALIDATION)
	if (psDevConfig->pfnTDRGXStart == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXPostPowerState: TDRGXStart not implemented!"));
		return PVRSRV_ERROR_NOT_IMPLEMENTED;
	}

	psDevInfo->bRGXPowered = IMG_TRUE;
	eError = psDevConfig->pfnTDRGXStart(psDevConfig->hSysData);
	if (eError != PVRSRV_OK)
	{
		psDevInfo->bRGXPowered = IMG_FALSE;
	}
#else
	eError = RGXStart(&psDevInfo->sLayerParams);
#endif

	return eError;
}

#if defined(SUPPORT_GPUVIRT_VALIDATION_MTS)
/*
 * To validate the MTS unit we do the following:
 *  - Immediately after firmware loading for each OSID
 *    - Write the OSid to a memory location shared with FW
 *    - Kick the register of that OSid
 *         (Uncounted, DM 0)
 *    - FW clears the memory location if OSid matches
 *    - Host checks that memory location is cleared
 *
 *  See firmware/rgxfw_bg.c
 */
static PVRSRV_ERROR _RGXVirtualisationPowerupSidebandTest(PVRSRV_DEVICE_NODE	 *psDeviceNode,
														 RGXFWIF_SYSINIT *psFwSysInit,
														 PVRSRV_RGXDEV_INFO	 *psDevInfo)
{
	IMG_UINT32 ui32ScheduleRegister;
	IMG_UINT32 ui32OSid;
	IMG_UINT32 ui32KickType;
	IMG_UINT32 ui32OsRegBanksMapped = (psDeviceNode->psDevConfig->ui32RegsSize / RGX_VIRTUALISATION_REG_SIZE_PER_OS);

	/* Nothing to do if the device does not support GPU_VIRTUALISATION */
	if (!PVRSRV_IS_FEATURE_SUPPORTED(psDeviceNode, GPU_VIRTUALISATION))
	{
		return PVRSRV_OK;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "Testing per-os kick registers:"));

	ui32OsRegBanksMapped = MIN(ui32OsRegBanksMapped, GPUVIRT_VALIDATION_NUM_OS);

	if (ui32OsRegBanksMapped != RGXFW_MAX_NUM_OSIDS)
	{
		PVR_DPF((PVR_DBG_WARNING, "The register bank mapped into kernel VA does not cover all OS' registers:"));
		PVR_DPF((PVR_DBG_WARNING, "Maximum OS count = %d / Per-os register banks mapped = %d", RGXFW_MAX_NUM_OSIDS, ui32OsRegBanksMapped));
		PVR_DPF((PVR_DBG_WARNING, "Only first %d MTS registers will be tested", ui32OsRegBanksMapped));
	}

	ui32KickType = RGX_CR_MTS_SCHEDULE_DM_DM0 | RGX_CR_MTS_SCHEDULE_TASK_NON_COUNTED;

	for (ui32OSid = 0; ui32OSid < ui32OsRegBanksMapped; ui32OSid++)
	{
		/* set Test field */
		psFwSysInit->ui32OSKickTest = (ui32OSid << RGXFWIF_KICK_TEST_OSID_SHIFT) | RGXFWIF_KICK_TEST_ENABLED_BIT;

#if defined(PDUMP)
		DevmemPDumpLoadMem(psDevInfo->psRGXFWIfSysInitMemDesc,
						   offsetof(RGXFWIF_SYSINIT, ui32OSKickTest),
						   sizeof(psFwSysInit->ui32OSKickTest),
						   PDUMP_FLAGS_CONTINUOUS);
#endif

		/* Force a read-back to memory to avoid posted writes on certain buses */
		OSWriteMemoryBarrier(&psFwSysInit->ui32OSKickTest);
		RGXFwSharedMemCacheOpValue(psFwSysInit->ui32OSKickTest, FLUSH);

		/* kick register */
		ui32ScheduleRegister = RGX_CR_MTS_SCHEDULE + (ui32OSid * RGX_VIRTUALISATION_REG_SIZE_PER_OS);
		PVR_DPF((PVR_DBG_MESSAGE, "  Testing OS: %u, Kick Reg: %X",
				 ui32OSid,
				 ui32ScheduleRegister));
		OSWriteHWReg32(psDevInfo->pvRegsBaseKM, ui32ScheduleRegister, ui32KickType);

#if defined(PDUMP)
		PDUMPCOMMENTWITHFLAGS(psDevInfo->psDeviceNode, PDUMP_FLAGS_CONTINUOUS, "VZ sideband test, kicking MTS register %u", ui32OSid);

		PDUMPREG32(psDeviceNode, RGX_PDUMPREG_NAME,
				ui32ScheduleRegister, ui32KickType, PDUMP_FLAGS_CONTINUOUS);

		DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfSysInitMemDesc,
							   offsetof(RGXFWIF_SYSINIT, ui32OSKickTest),
							   0,
							   0xFFFFFFFF,
							   PDUMP_POLL_OPERATOR_EQUAL,
							   PDUMP_FLAGS_CONTINUOUS);
#endif

#if !defined(NO_HARDWARE)
		OSMemoryBarrier((IMG_BYTE*) psDevInfo->pvRegsBaseKM + ui32ScheduleRegister);

		/* Wait test enable bit to be unset */
		if (PVRSRVPollForValueKM(psDeviceNode,
								 (volatile IMG_UINT32 __iomem *)&psFwSysInit->ui32OSKickTest,
								 0,
								 RGXFWIF_KICK_TEST_ENABLED_BIT,
								 POLL_FLAG_LOG_ERROR | POLL_FLAG_DEBUG_DUMP,
								 RGXFwSharedMemCacheOpExecPfn) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "Testing OS %u kick register failed: firmware did not clear test location (contents: 0x%X)",
					 ui32OSid,
					 psFwSysInit->ui32OSKickTest));

			return PVRSRV_ERROR_TIMEOUT;
		}

		/* Check that the value is what we expect */
		if (psFwSysInit->ui32OSKickTest != 0)
		{
			PVR_DPF((PVR_DBG_ERROR, "Testing OS %u kick register failed: firmware wrote 0x%X to test location",
					 ui32OSid,
					 psFwSysInit->ui32OSKickTest));
			return PVRSRV_ERROR_INIT_FAILURE;
		}

		PVR_DPF((PVR_DBG_MESSAGE, "    PASS"));
#endif
	}

	PVR_LOG(("MTS passed sideband tests"));
	return PVRSRV_OK;
}
#endif /* defined(SUPPORT_GPUVIRT_VALIDATION_MTS) */


/*
	RGXPostPowerState
*/
PVRSRV_ERROR RGXPostPowerState(PVRSRV_DEVICE_NODE		*psDeviceNode,
                               PVRSRV_DEV_POWER_STATE	eNewPowerState,
                               PVRSRV_DEV_POWER_STATE	eCurrentPowerState,
                               PVRSRV_POWER_FLAGS		ePwrFlags)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(ePwrFlags);

	if ((eNewPowerState == eCurrentPowerState) ||
	    (eCurrentPowerState != PVRSRV_DEV_POWER_STATE_OFF))
	{
		PDUMPCOMMENT(psDeviceNode,
		             "RGXPostPowerState: Current state: %d, New state: %d",
		             eCurrentPowerState, eNewPowerState);

		return PVRSRV_OK;
	}

	/* Update timer correlation related data */
	RGXTimeCorrBegin(psDeviceNode, RGXTIMECORR_EVENT_POWER);

	trace_event_begin("FirmwareStarting");
	eError = _RGXDoStart(psDeviceNode);
	PVR_LOG_GOTO_IF_ERROR(eError, "_RGXDoStart", fail_trace);

	OSMemoryBarrier(NULL);


	/*
		* Check whether the FW has started by polling on bFirmwareStarted flag
		*/
	RGXFwSharedMemCacheOpValue(psDevInfo->psRGXFWIfSysInit->bFirmwareStarted,
	                           INVALIDATE);
	if (PVRSRVPollForValueKM(psDeviceNode,
	                         (IMG_UINT32 __iomem *)&psDevInfo->psRGXFWIfSysInit->bFirmwareStarted,
	                         IMG_TRUE,
	                         0xFFFFFFFF,
	                         POLL_FLAG_LOG_ERROR | POLL_FLAG_DEBUG_DUMP,
	                         RGXFwSharedMemCacheOpExecPfn) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "RGXPostPowerState: Polling for 'FW started' flag failed."));
		eError = PVRSRV_ERROR_TIMEOUT;

#if defined(TRACK_FW_BOOT)
		_RGXCheckFWBootStage(psDevInfo);
#endif

		/*
		* When bFirmwareStarted fails some info may be gained by doing the following
		* debug dump but unfortunately it could be potentially dangerous if the reason
		* for not booting is the GPU power is not ON. However, if we have reached this
		* point the System Layer has returned without errors, we assume the GPU power
		* is indeed ON.
		*/
		RGXDumpRGXDebugSummary(NULL, NULL, psDeviceNode->pvDevice, IMG_TRUE);
		RGXDumpRGXRegisters(NULL, NULL, psDeviceNode->pvDevice);

		PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVPollForValueKM(bFirmwareStarted)", fail_trace);
	}
	trace_event_end();

#if defined(PDUMP)
	PDUMPCOMMENTWITHFLAGS(psDeviceNode, PDUMP_FLAGS_CONTINUOUS, "Wait for the Firmware to start.");
	eError = DevmemPDumpDevmemPol32(psDevInfo->psRGXFWIfSysInitMemDesc,
	                                offsetof(RGXFWIF_SYSINIT, bFirmwareStarted),
	                                IMG_TRUE,
	                                0xFFFFFFFFU,
	                                PDUMP_POLL_OPERATOR_EQUAL,
	                                PDUMP_FLAGS_CONTINUOUS);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "RGXPostPowerState: problem pdumping POL for psRGXFWIfSysInitMemDesc (%d)",
		         eError));
		PVR_LOG_GOTO_IF_ERROR(eError, "DevmemPDumpDevmemPol32", fail);
	}

#endif /* defined(PDUMP) */

#if defined(RGX_SUPPORT_DUAL_MEMCTX) && !defined(PDUMP)
	_RGXSetOsDataDualCtxFlag(psDevInfo);
#endif

#if defined(SUPPORT_GPUVIRT_VALIDATION_MTS)
	eError = _RGXVirtualisationPowerupSidebandTest(psDeviceNode, psDevInfo->psRGXFWIfSysInit, psDevInfo);
	PVR_LOG_GOTO_IF_ERROR(eError, "_RGXVirtualisationPowerupSidebandTest", fail);
#endif


#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	RGXFwSharedMemCacheOpValue(psDevInfo->psRGXFWIfSysInit->ui32FirmwareStartedTimeStamp,
	                           INVALIDATE);
	PVRSRVSetFirmwareStartTime(psDeviceNode->psPowerDev,
	                           psDevInfo->psRGXFWIfSysInit->ui32FirmwareStartedTimeStamp);
	RGXFwSharedMemCacheOpValue(psDevInfo->psRGXFWIfSysInit->ui32FirmwareStartedTimeStamp,
	                           FLUSH);
#endif

	RGXFwSharedMemCacheOpValue(psDevInfo->psRGXFWIfSysInit->ui32MarkerVal,
	                           INVALIDATE);
	HTBSyncPartitionMarker(psDevInfo->psRGXFWIfSysInit->ui32MarkerVal);

#if defined(SUPPORT_LINUX_DVFS)
	eError = ResumeDVFS(psDeviceNode);
	PVR_LOG_GOTO_IF_ERROR(eError, "ResumeDVFS", fail);
#elif defined(SUPPORT_PDVFS)
	eError = ResumePDVFS(psDeviceNode);
	PVR_LOG_GOTO_IF_ERROR(eError, "ResumePDVFS", fail);
#endif

#if defined(PVRSRV_ANDROID_TRACE_GPU_FREQ)
	{
		RGX_DATA *psRGXData = (RGX_DATA *)psDeviceNode->psDevConfig->hDevData;
		trace_gpu_frequency(psRGXData->psRGXTimingInfo->ui32CoreClockSpeed,
				    psDevInfo->psDeviceNode->sDevId.ui32InternalID);
#if defined(SUPPORT_PDVFS)
		trace_clock_set_rate("gpu_util", psDevInfo->ui32UtilisationSnapshot,
					raw_smp_processor_id());
#endif
	}
#endif /* defined(PVRSRV_ANDROID_TRACE_GPU_FREQ) */

	PDUMPCOMMENT(psDeviceNode,
	             "RGXPostPowerState: Current state: %d, New state: %d",
	             eCurrentPowerState, eNewPowerState);

	return eError;

fail_trace:
	trace_event_end();
#if (defined(SUPPORT_LINUX_DVFS) && !defined(NO_HARDWARE)) || \
	(defined(SUPPORT_PDVFS) && !defined(NO_HARDWARE)) || \
	defined(SUPPORT_GPUVIRT_VALIDATION_MTS) || defined(PDUMP)
fail:
#endif
	psDevInfo->bRGXPowered = IMG_FALSE;

	return eError;
}

/*
	RGXPreClockSpeedChange
*/
PVRSRV_ERROR RGXPreClockSpeedChange(PVRSRV_DEVICE_NODE		*psDeviceNode,
                                    PVRSRV_DEV_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	const PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	__maybe_unused const RGX_DATA *psRGXData = (RGX_DATA*)psDeviceNode->psDevConfig->hDevData;
	const RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;
	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVNODE, psDeviceNode, PVRSRV_OK);

	PVR_DPF((PVR_DBG_MESSAGE, "RGXPreClockSpeedChange: RGX clock speed was %uHz",
			psRGXData->psRGXTimingInfo->ui32CoreClockSpeed));

	RGXFwSharedMemCacheOpValue(psFwSysData->ePowState,
	                           INVALIDATE);

	if ((eCurrentPowerState != PVRSRV_DEV_POWER_STATE_OFF) &&
	    (psFwSysData->ePowState != RGXFWIF_POW_OFF))
	{
		/* Update GPU frequency and timer correlation related data */
		RGXTimeCorrEnd(psDeviceNode, RGXTIMECORR_EVENT_DVFS);
	}

	return eError;
}

/*
	RGXPostClockSpeedChange
*/
PVRSRV_ERROR RGXPostClockSpeedChange(PVRSRV_DEVICE_NODE		*psDeviceNode,
                                     PVRSRV_DEV_POWER_STATE	eCurrentPowerState)
{
	const PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	const RGX_DATA *psRGXData = (RGX_DATA*)psDeviceNode->psDevConfig->hDevData;
	const RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32NewClockSpeed = psRGXData->psRGXTimingInfo->ui32CoreClockSpeed;

	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVNODE, psDeviceNode, PVRSRV_ERROR_NOT_SUPPORTED);

	/* Update runtime configuration with the new value */
	OSWriteDeviceMem32WithWMB(&psDevInfo->psRGXFWIfRuntimeCfg->ui32CoreClockSpeed,
	                          ui32NewClockSpeed);
	RGXFwSharedMemCacheOpValue(psDevInfo->psRGXFWIfRuntimeCfg->ui32CoreClockSpeed, FLUSH);

	RGXFwSharedMemCacheOpValue(psFwSysData->ePowState,
	                           INVALIDATE);
	if ((eCurrentPowerState != PVRSRV_DEV_POWER_STATE_OFF) &&
	    (psFwSysData->ePowState != RGXFWIF_POW_OFF))
	{
		RGXFWIF_KCCB_CMD sCOREClkSpeedChangeCmd;
		IMG_UINT32 ui32CmdKCCBSlot;

		RGXTimeCorrBegin(psDeviceNode, RGXTIMECORR_EVENT_DVFS);

		sCOREClkSpeedChangeCmd.eCmdType = RGXFWIF_KCCB_CMD_CORECLKSPEEDCHANGE;
		sCOREClkSpeedChangeCmd.ui32KCCBFlags = 0;
		sCOREClkSpeedChangeCmd.uCmdData.sCoreClkSpeedChangeData.ui32NewClockSpeed = ui32NewClockSpeed;

		PDUMPCOMMENT(psDeviceNode, "Scheduling CORE clock speed change command");

		PDUMPPOWCMDSTART(psDeviceNode);
		eError = RGXSendCommandAndGetKCCBSlot(psDeviceNode->pvDevice,
		                                      &sCOREClkSpeedChangeCmd,
		                                      PDUMP_FLAGS_NONE,
		                                      &ui32CmdKCCBSlot);
		PDUMPPOWCMDEND(psDeviceNode);

		if (eError != PVRSRV_OK)
		{
			PDUMPCOMMENT(psDeviceNode, "Scheduling CORE clock speed change command failed");
			PVR_DPF((PVR_DBG_ERROR, "RGXPostClockSpeedChange: Scheduling KCCB command failed. Error:%u", eError));
			return eError;
		}

		PVR_DPF((PVR_DBG_MESSAGE, "RGXPostClockSpeedChange: RGX clock speed changed to %uHz",
				psRGXData->psRGXTimingInfo->ui32CoreClockSpeed));
	}

	return eError;
}

/*!
******************************************************************************

 @Function	RGXPowUnitsChange

 @Description Change power units state

 @Input	   psDeviceNode : RGX Device Node
 @Input	   ui32PowUnits : On Rogue: Number of DUSTs to make transition to.
                          On Volcanic: Mask containing power state of SPUs.
                          Each bit corresponds to an SPU and value must be non-zero.

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR RGXPowUnitsChange(PVRSRV_DEVICE_NODE *psDeviceNode,
                               IMG_UINT32 ui32PowUnits)

{
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_KCCB_CMD	sPowUnitsChange;
	IMG_UINT32			ui32AvailablePowUnits;
	IMG_UINT32			ui32CmdKCCBSlot;
	RGXFWIF_RUNTIME_CFG *psRuntimeCfg = psDevInfo->psRGXFWIfRuntimeCfg;
	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVNODE, psDeviceNode, PVRSRV_OK);

#if defined(PVR_ARCH_VOLCANIC)
	ui32AvailablePowUnits = psDevInfo->ui32AvailablePowUnitsMask;

	/**
	 * Validate the input. At-least one PU must be powered on and all requested
	 * PU's must be a subset of full PU mask.
	 */
	if ((ui32PowUnits == 0) || (ui32PowUnits & ~ui32AvailablePowUnits))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Invalid Power Units mask requested (0x%X). Value should be non-zero and sub-set of 0x%X mask",
				__func__,
				ui32PowUnits,
				ui32AvailablePowUnits));
		return PVRSRV_ERROR_INVALID_SPU_MASK;
	}
#else
	ui32AvailablePowUnits = psDevInfo->sDevFeatureCfg.ui32MAXPowUnitCount;

	if (ui32PowUnits > ui32AvailablePowUnits)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Invalid number of DUSTs (%u) while expecting value within <0,%u>",
				__func__,
				ui32PowUnits,
				ui32AvailablePowUnits));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
#endif

	psRuntimeCfg->ui32PowUnitsState = ui32PowUnits;
	OSWriteMemoryBarrier(&psRuntimeCfg->ui32PowUnitsState);
	RGXFwSharedMemCacheOpValue(psRuntimeCfg->ui32PowUnitsState, FLUSH);

#if !defined(NO_HARDWARE)
	{
		PVRSRV_ERROR eError;
		const RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;
		RGXFwSharedMemCacheOpValue(psFwSysData->ePowState,
		                           INVALIDATE);

		if (psFwSysData->ePowState == RGXFWIF_POW_OFF)
		{
			return PVRSRV_OK;
		}

		if (psFwSysData->ePowState != RGXFWIF_POW_FORCED_IDLE)
		{
			eError = PVRSRV_ERROR_DEVICE_POWER_CHANGE_DENIED;
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: Powered units state can not be changed when not IDLE",
					 __func__));
			return eError;
		}
	}
#endif

	sPowUnitsChange.eCmdType = RGXFWIF_KCCB_CMD_POW;
	sPowUnitsChange.ui32KCCBFlags = 0;
	sPowUnitsChange.uCmdData.sPowData.ePowType = RGXFWIF_POW_NUM_UNITS_CHANGE;
	sPowUnitsChange.uCmdData.sPowData.uPowerReqData.ui32PowUnits = ui32PowUnits;
#if defined(RGX_FEATURE_POWER_ISLAND_VERSION_MAX_VALUE_IDX)
	sPowUnitsChange.uCmdData.sPowData.uPowerReqData.ui32RACUnits = 0;

	if (RGX_GET_FEATURE_VALUE(psDevInfo, POWER_ISLAND_VERSION) >= 3)
	{
		sPowUnitsChange.uCmdData.sPowData.uPowerReqData.ui32RACUnits =
			(1U << psDevInfo->sDevFeatureCfg.ui32MAXRACCount) - 1;
	}
#endif

	PDUMPCOMMENT(psDeviceNode,
	             "Scheduling command to change power units state to 0x%X",
	             ui32PowUnits);
	return RGXSendPowerCommandAndWaitOnCompletion(psDeviceNode->pvDevice,
												  &sPowUnitsChange,
												  PDUMP_FLAGS_NONE,
												  __func__,
												  &ui32CmdKCCBSlot);
}

/*
 @Function	RGXAPMLatencyChange
*/
PVRSRV_ERROR RGXAPMLatencyChange(PVRSRV_DEVICE_NODE	*psDeviceNode,
                                 IMG_UINT32			ui32ActivePMLatencyms,
                                 IMG_BOOL			bActivePMLatencyPersistant)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR		eError;
	RGXFWIF_RUNTIME_CFG	*psRuntimeCfg = psDevInfo->psRGXFWIfRuntimeCfg;
	IMG_UINT32			ui32CmdKCCBSlot;
	PVRSRV_DEV_POWER_STATE	ePowerState;
	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVNODE, psDeviceNode, PVRSRV_OK);

	if (psRuntimeCfg == NULL)
	{
		return PVRSRV_ERROR_NOT_INITIALISED;
	}

	/* Update runtime configuration with the new values and ensure the
	 * new APM latency is written to memory before requesting the FW to
	 * read it
	 */
	psRuntimeCfg->ui32ActivePMLatencyms = ui32ActivePMLatencyms;
	psRuntimeCfg->bActivePMLatencyPersistant = bActivePMLatencyPersistant;
	OSWriteMemoryBarrier(&psRuntimeCfg->bActivePMLatencyPersistant);
	RGXFwSharedMemCacheOpValue(psRuntimeCfg->ui32ActivePMLatencyms, FLUSH);
	RGXFwSharedMemCacheOpValue(psRuntimeCfg->bActivePMLatencyPersistant, FLUSH);

	PVRSRVPowerLockWrite(psDeviceNode);

	eError = PVRSRVGetDevicePowerState(psDeviceNode, &ePowerState);

	if ((eError == PVRSRV_OK) && (ePowerState != PVRSRV_DEV_POWER_STATE_OFF))
	{
		RGXFWIF_KCCB_CMD	sActivePMLatencyChange;
		sActivePMLatencyChange.eCmdType = RGXFWIF_KCCB_CMD_POW;
		sActivePMLatencyChange.ui32KCCBFlags = 0;
		sActivePMLatencyChange.uCmdData.sPowData.ePowType = RGXFWIF_POW_APM_LATENCY_CHANGE;

		PDUMPCOMMENT(psDeviceNode,
		             "Scheduling command to change APM latency to %u",
		             ui32ActivePMLatencyms);
		eError = RGXSendCommandAndGetKCCBSlot(psDeviceNode->pvDevice,
		                                      &sActivePMLatencyChange,
		                                      PDUMP_FLAGS_NONE,
		                                      &ui32CmdKCCBSlot);

		if (eError != PVRSRV_OK)
		{
			PDUMPCOMMENT(psDeviceNode,
			             "Scheduling command to change APM latency failed. Error:%u",
			             eError);
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Scheduling KCCB to change APM latency failed. Error:%u",
			         __func__, eError));
			goto ErrorExit;
		}
	}

ErrorExit:
	PVRSRVPowerUnlockWrite(psDeviceNode);

	return eError;
}

/*
	RGXActivePowerRequest
*/
PVRSRV_ERROR RGXActivePowerRequest(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	const RGXFWIF_SYSDATA *psFwSysData = psDevInfo->psRGXFWIfFwSysData;
	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVNODE, psDeviceNode, PVRSRV_OK);


	psDevInfo->ui32ActivePMReqTotal++;

	/* Powerlock to avoid further requests from racing with the FW hand-shake
	 * from now on (previous kicks to this point are detected by the FW)
	 * PVRSRVPowerLock is replaced with PVRSRVPowerTryLock to avoid
	 * potential dead lock between PDumpWriteLock and PowerLock
	 * during 'DriverLive + PDUMP=1 + EnableAPM=1'.
	 */
	eError = PVRSRVPowerTryLockWrite(psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		if (eError != PVRSRV_ERROR_RETRY)
		{
			PVR_LOG_ERROR(eError, "PVRSRVPowerTryLockWrite");
		}
		else
		{
			psDevInfo->ui32ActivePMReqRetry++;
		}
		goto _RGXActivePowerRequest_PowerLock_failed;
	}

	RGXFwSharedMemCacheOpValue(psFwSysData->ePowState,
	                           INVALIDATE);

	/* Check again for IDLE once we have the power lock */
	if (psFwSysData->ePowState == RGXFWIF_POW_IDLE)
	{
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
		PVRSRVSetFirmwareHandshakeIdleTime(psDeviceNode->psPowerDev,
		                                   RGXReadHWTimerReg(psDevInfo)-psFwSysData->ui64StartIdleTime);
#endif

		PDUMPPOWCMDSTART(psDeviceNode);
		eError = PVRSRVSetDevicePowerStateKM(psDeviceNode,
		                                     PVRSRV_DEV_POWER_STATE_OFF,
		                                     PVRSRV_POWER_FLAGS_NONE);
		PDUMPPOWCMDEND(psDeviceNode);

		if (eError == PVRSRV_OK)
		{
			psDevInfo->ui32ActivePMReqOk++;
		}
		else if (eError == PVRSRV_ERROR_DEVICE_POWER_CHANGE_DENIED)
		{
			psDevInfo->ui32ActivePMReqDenied++;
		}
	}
	else
	{
		psDevInfo->ui32ActivePMReqNonIdle++;
	}

	PVRSRVPowerUnlockWrite(psDeviceNode);

_RGXActivePowerRequest_PowerLock_failed:

	return eError;
}
/*
	RGXForcedIdleRequest
*/

PVRSRV_ERROR RGXForcedIdleRequest(PVRSRV_DEVICE_NODE *psDeviceNode, IMG_BOOL bDeviceOffPermitted)
{
	PVRSRV_RGXDEV_INFO    *psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_KCCB_CMD      sPowCmd;
	PVRSRV_ERROR          eError;
	IMG_UINT32            ui32CmdKCCBSlot;
#if !defined(NO_HARDWARE)
	const RGXFWIF_SYSDATA *psFwSysData;
#endif
	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVNODE, psDeviceNode, PVRSRV_OK);

#if !defined(NO_HARDWARE)
	psFwSysData = psDevInfo->psRGXFWIfFwSysData;
	RGXFwSharedMemCacheOpValue(psFwSysData->ePowState,
	                           INVALIDATE);

	/* Firmware already forced idle */
	if (psFwSysData->ePowState == RGXFWIF_POW_FORCED_IDLE)
	{
		return PVRSRV_OK;
	}

	/* Firmware is not powered. Sometimes this is permitted, for instance we were forcing idle to power down. */
	if (psFwSysData->ePowState == RGXFWIF_POW_OFF)
	{
		PVR_DPF((PVR_DBG_WARNING, "Firmware is powered OFF (bDeviceOffPermitted = %s)",
				 bDeviceOffPermitted ? "Yes" : "No"));
		return (bDeviceOffPermitted) ? PVRSRV_OK : PVRSRV_ERROR_DEVICE_IDLE_REQUEST_DENIED;
	}
#endif

	sPowCmd.eCmdType = RGXFWIF_KCCB_CMD_POW;
	sPowCmd.ui32KCCBFlags = 0;
	sPowCmd.uCmdData.sPowData.ePowType = RGXFWIF_POW_FORCED_IDLE_REQ;
	sPowCmd.uCmdData.sPowData.uPowerReqData.ePowRequestType = RGXFWIF_POWER_FORCE_IDLE;

	PDUMPCOMMENT(psDeviceNode,
	             "RGXForcedIdleRequest: Sending forced idle command");
	eError = RGXSendPowerCommandAndWaitOnCompletion(psDevInfo,
													&sPowCmd,
													PDUMP_FLAGS_NONE,
													__func__,
													&ui32CmdKCCBSlot);

	if (eError == PVRSRV_ERROR_UNKNOWN_POWER_STATE)
	{
		_RGXFWNotifyHostTimeout(psDevInfo);
		PVR_DPF((PVR_DBG_ERROR,
				 "%s: Idle request failed. Firmware potentially left in forced idle state",
				 __func__));
		return eError;
	}

#if !defined(NO_HARDWARE)
	/* Check the firmware state for idleness */
	RGXFwSharedMemCacheOpValue(psFwSysData->ePowState,
	                           INVALIDATE);
	if (psFwSysData->ePowState != RGXFWIF_POW_FORCED_IDLE)
	{
		psDevInfo->ui32FWNonIdleTimeoutCount++;
		return PVRSRV_ERROR_DEVICE_IDLE_REQUEST_DENIED;
	}
#endif

	return PVRSRV_OK;
}

static PVRSRV_ERROR _RGXSendCancelForceIdleCommand(PVRSRV_DEVICE_NODE *psDeviceNode,
                                                   IMG_UINT32         *pui32CmdKCCBSlot,
                                                   IMG_BOOL            bPollOnCompletion)
{
	PVRSRV_RGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	RGXFWIF_KCCB_CMD	sPowCmd;
	PVRSRV_ERROR		eError = PVRSRV_OK;
	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVNODE, psDeviceNode, PVRSRV_OK);

	/* Send the IDLE request to the FW */
	sPowCmd.eCmdType = RGXFWIF_KCCB_CMD_POW;
	sPowCmd.ui32KCCBFlags = 0;
	sPowCmd.uCmdData.sPowData.ePowType = RGXFWIF_POW_FORCED_IDLE_REQ;
	sPowCmd.uCmdData.sPowData.uPowerReqData.ePowRequestType = RGXFWIF_POWER_CANCEL_FORCED_IDLE;

	PDUMPCOMMENT(psDeviceNode,
	             "RGXForcedIdleRequest: Sending cancel forced idle command");

	if (bPollOnCompletion)
	{
		/* Send cancel forced IDLE command to GP and wait for response */
		eError = RGXSendPowerCommandAndWaitOnCompletion(psDevInfo,
														&sPowCmd,
														PDUMP_FLAGS_NONE,
														"RGXCancelForcedIdleRequest",
														pui32CmdKCCBSlot);
	}
	else
	{
		/* Send cancel forced IDLE command to GP */
		eError = RGXSendCommandAndGetKCCBSlot(psDevInfo,
											  &sPowCmd,
											  PDUMP_FLAGS_NONE,
											  pui32CmdKCCBSlot);
	}
	if (eError != PVRSRV_OK)
	{
		PDUMPCOMMENT(psDeviceNode,
					 "RGXCancelForcedIdleRequest: Failed to send cancel IDLE request for DM%d",
					 RGXFWIF_DM_GP);
	}

	return eError;
}
/*
	RGXCancelForcedIdleRequest
*/
PVRSRV_ERROR RGXCancelForcedIdleRequest(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	IMG_UINT32			ui32CmdKCCBSlot;
	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVNODE, psDeviceNode, PVRSRV_OK);

	eError = _RGXSendCancelForceIdleCommand(psDeviceNode, &ui32CmdKCCBSlot, IMG_TRUE);
	if (eError == PVRSRV_ERROR_UNKNOWN_POWER_STATE)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Firmware potentially left in forced idle state err: %u",
				 __func__,
				 eError));
	}
	return eError;
}

PVRSRV_ERROR RGXCancelForcedIdleRequestAsync(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_ERROR		eError;

	eError = _RGXSendCancelForceIdleCommand(psDeviceNode, NULL, IMG_FALSE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Firmware potentially left in forced idle state err: %u",
		         __func__,
		         eError));
	}
	return eError;
}

#if defined(SUPPORT_FW_CORE_CLK_RATE_CHANGE_NOTIFY)
#if defined(SUPPORT_PDVFS) && (PDVFS_COM == PDVFS_COM_HOST)
/*************************************************************************/ /*!
@Function       PDVFSProcessCoreClkChangeRequest
@Description    Processes a core clock rate change request.
@Input          psDevInfo            A pointer to PVRSRV_RGXDEV_INFO.
@Input          ui32CoreClockRate    New core clock rate.
@Return         PVRSRV_ERROR.
*/ /**************************************************************************/
PVRSRV_ERROR RGXProcessCoreClkChangeRequest(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32CoreClockRate)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = psDevInfo->psDeviceNode->psDevConfig;
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;
	IMG_DVFS_DEVICE_CFG *psDVFSDeviceCfg = &psDevConfig->sDVFS.sDVFSDeviceCfg;
	RGX_TIMING_INFORMATION *psRGXTimingInfo = ((RGX_DATA*)(psDevConfig->hDevData))->psRGXTimingInfo;
	IMG_UINT32 ui32CoreClockRateCurrent = psRGXTimingInfo->ui32CoreClockSpeed;
	const IMG_OPP *psOpp = NULL;
	IMG_UINT32 ui32Index;
	PVRSRV_ERROR eError;

	if (!_PDVFSEnabled())
	{
		/* No error message to avoid excessive messages */
		return PVRSRV_OK;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "Core clock rate = %u", ui32CoreClockRate));

	if (!psDVFSDeviceCfg->bDTConfig)
	{
		PVR_ASSERT(psDVFSDeviceCfg->pasOPPTable);

		/* Find the matching OPP (Exact). */
		for (ui32Index = 0; ui32Index < psDVFSDeviceCfg->ui32OPPTableSize; ui32Index++)
		{
			if (ui32CoreClockRate == psDVFSDeviceCfg->pasOPPTable[ui32Index].ui32Freq)
			{
				psOpp = &psDVFSDeviceCfg->pasOPPTable[ui32Index];
				break;
			}
		}

		if (! psOpp)
		{
			PVR_DPF((PVR_DBG_ERROR, "Frequency not present in OPP table - %u", ui32CoreClockRate));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}

	PVRSRVPowerLockWrite(psDeviceNode);

	eError = PVRSRVDevicePreClockSpeedChange(psDeviceNode, psDVFSDeviceCfg->bIdleReq, NULL);
	if (eError != PVRSRV_OK)
	{
		PVRSRVPowerUnlockWrite(psDeviceNode);

		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDevicePreClockSpeedChange failed"));
		return eError;
	}

#if defined(SUPPORT_PDVFS_DEVFREQ)
	{
		IMG_DVFS_DEVICE_CFG	*psDVFSDeviceCfg = &psDevConfig->sDVFS.sDVFSDeviceCfg;

		if (psDVFSDeviceCfg->pfnNotifyCoreClkChange)
		{
			/* Update the devfreq module. Call this before updating the
			 * clock frequency value in RGX_TIMING_INFORMATION to ensure
			 * the transition table is updated correctly. */
			eError = psDVFSDeviceCfg->pfnNotifyCoreClkChange(psDeviceNode, ui32CoreClockRate);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_WARNING, "%s: failed to update devfreq frequency (%s)",
						 __func__, PVRSRVGetErrorString(eError)));
			}
		}
	}
#endif

	psRGXTimingInfo->ui32CoreClockSpeed = ui32CoreClockRate;
	if (psDVFSDeviceCfg->pfnSetFrequency == NULL ||
	    psDVFSDeviceCfg->pfnSetVoltage == NULL)
	{
		PVR_DPF((PVR_DBG_WARNING, "Missing system-layer callbacks to SetVoltage and SetFrequency."
			" System/GPU frequency will not be changed."));
		goto _PostClockSpeedChange;
	}

	/* Increasing frequency, change voltage first */
	if (ui32CoreClockRate > ui32CoreClockRateCurrent)
	{
		if (psOpp != NULL)
		{
			psDVFSDeviceCfg->pfnSetVoltage(psDevConfig->hSysData, psOpp->ui32Volt);
		}
	}

	psDVFSDeviceCfg->pfnSetFrequency(psDevConfig->hSysData, ui32CoreClockRate);

	/* Decreasing frequency, change frequency first */
	if (ui32CoreClockRate < ui32CoreClockRateCurrent)
	{
		if (psOpp != NULL)
		{
			psDVFSDeviceCfg->pfnSetVoltage(psDevConfig->hSysData, psOpp->ui32Volt);
		}
	}

_PostClockSpeedChange:
	PVRSRVDevicePostClockSpeedChange(psDeviceNode, psDVFSDeviceCfg->bIdleReq, NULL);

	PVRSRVPowerUnlockWrite(psDeviceNode);

	return PVRSRV_OK;
}
#else
/*************************************************************************/ /*!
@Function       PDVFSProcessCoreClkChangeNotification
@Description    Processes a core clock rate change notification.
@Input          psDevInfo            A pointer to PVRSRV_RGXDEV_INFO.
@Input          ui32CoreClockRate    New core clock rate.
@Return         PVRSRV_ERROR.
*/ /**************************************************************************/
PVRSRV_ERROR RGXProcessCoreClkChangeNotification(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32CoreClockRate)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = psDevInfo->psDeviceNode->psDevConfig;
	RGX_TIMING_INFORMATION *psRGXTimingInfo = ((RGX_DATA*)(psDevConfig->hDevData))->psRGXTimingInfo;
	PVRSRV_DEV_POWER_STATE ePowerState;
	PVRSRV_ERROR eError;

	PVRSRVPowerLockWrite(psDevInfo->psDeviceNode);

	eError = PVRSRVGetDevicePowerState(psDevInfo->psDeviceNode, &ePowerState);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: failed to acquire power state (%s)",
				 __func__, PVRSRVGetErrorString(eError)));
		PVRSRVPowerUnlockWrite(psDevInfo->psDeviceNode);
		return eError;
	}

#if defined(SUPPORT_PDVFS_DEVFREQ)
	{
		IMG_DVFS_DEVICE_CFG	*psDVFSDeviceCfg = &psDevConfig->sDVFS.sDVFSDeviceCfg;

		if (psDVFSDeviceCfg->pfnNotifyCoreClkChange)
		{
			/* Update the devfreq module. Call this before updating the
			 * clock frequency value in RGX_TIMING_INFORMATION to ensure
			 * the transition table is updated correctly. */
			eError = psDVFSDeviceCfg->pfnNotifyCoreClkChange(psDevInfo->psDeviceNode, ui32CoreClockRate);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_WARNING, "%s: failed to update devfreq frequency (%s)",
						 __func__, PVRSRVGetErrorString(eError)));
			}
		}
	}
#endif

	/* Guest drivers do not initialize psRGXFWIfFwSysData */
	RGXFwSharedMemCacheOpValue(psDevInfo->psRGXFWIfFwSysData->ePowState,
	                           INVALIDATE);
	if ((ePowerState != PVRSRV_DEV_POWER_STATE_OFF)
	    && ((psDevInfo->psRGXFWIfFwSysData == NULL) || (psDevInfo->psRGXFWIfFwSysData->ePowState != RGXFWIF_POW_OFF)))
	{
		/* Update GPU frequency and timer correlation related data */
		RGXTimeCorrEnd(psDevInfo->psDeviceNode, RGXTIMECORR_EVENT_DVFS);
		psRGXTimingInfo->ui32CoreClockSpeed = ui32CoreClockRate;
		RGXTimeCorrBegin(psDevInfo->psDeviceNode, RGXTIMECORR_EVENT_DVFS);
	}
	else
	{
		psRGXTimingInfo->ui32CoreClockSpeed = ui32CoreClockRate;
	}

	PVRSRVPowerUnlockWrite(psDevInfo->psDeviceNode);

#if defined(PVRSRV_ANDROID_TRACE_GPU_FREQ)
	trace_gpu_frequency(psRGXTimingInfo->ui32CoreClockSpeed,
			    psDevInfo->psDeviceNode->sDevId.ui32InternalID);
#endif /* defined(PVRSRV_ANDROID_TRACE_GPU_FREQ) */

	return PVRSRV_OK;
}
#endif

/*************************************************************************/ /*!
@Function       PDVFSProcessUtilisationChange
@Description    Processes a utilisation change notification.
@Input          psDevInfo            A pointer to PVRSRV_RGXDEV_INFO.
@Input          ui32Utilisation      New utilisation.
@Return         PVRSRV_ERROR.
*/ /**************************************************************************/
void RGXProcessUtilisationChange(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32Utilisation)
{
#if defined(SUPPORT_PDVFS) && defined(PVRSRV_ANDROID_TRACE_GPU_FREQ)
	trace_clock_set_rate("gpu_util", ui32Utilisation, raw_smp_processor_id());
#endif /* defined(PVRSRV_ANDROID_TRACE_GPU_FREQ) */
}
#endif /* SUPPORT_FW_CORE_CLK_RATE_CHANGE_NOTIFY */




/******************************************************************************
 End of file (rgxpower.c)
******************************************************************************/
