/*************************************************************************/ /*!
@File           rgxinit_common.c
@Title          RGX device specific internal/external initialisation routines
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX device specific functions
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

#include "rgxinit.h"
#include "rgxinit_internal.h"

#include "allocmem.h"
#include "rgx_heaps_server.h"

#include "rgxmem.h"
#include "rgxbvnc.h"

#include "rgxhwperf_common.h"
#include "rgxdebug_common.h"

#include "rgxfwutils.h"
#include "rgx_fwif_alignchecks.h"

#include "pvrsrv_apphint.h"
#include "os_apphint.h"
#include "vmm_pvz_client.h"

static PVRSRV_ERROR RGXAllocUFOBlock(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     IMG_UINT32 ui32RequestedSize,
                                     DEVMEM_MEMDESC **psMemDesc,
                                     IMG_UINT32 *puiSyncPrimVAddr,
                                     IMG_UINT32 *puiSyncPrimBlockSize)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PVRSRV_ERROR eError;
	RGXFWIF_DEV_VIRTADDR pFirmwareAddr;
	IMG_DEVMEM_ALIGN_T uiUFOBlockAlign = MAX(sizeof(IMG_UINT32), sizeof(SYNC_CHECKPOINT_FW_OBJ));
	IMG_DEVMEM_SIZE_T uiUFOBlockSize = PVR_ALIGN(ui32RequestedSize, uiUFOBlockAlign);

	psDevInfo = psDeviceNode->pvDevice;

	/* Size and align are 'expanded' because we request an Exportalign allocation */
	eError = DevmemExportalignAdjustSizeAndAlign(DevmemGetHeapLog2PageSize(psDevInfo->psFirmwareMainHeap),
	                                             &uiUFOBlockSize,
	                                             &uiUFOBlockAlign);

	if (eError != PVRSRV_OK)
	{
		goto e0;
	}

	eError = DevmemFwAllocateExportable(psDeviceNode,
	                                    uiUFOBlockSize,
	                                    uiUFOBlockAlign,
	                                    PVRSRV_MEMALLOCFLAG_PHYS_HEAP_HINT(FW_MAIN) |
	                                    PVRSRV_MEMALLOCFLAG_DEVICE_FLAG(PMMETA_PROTECT) |
	                                    PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE |
	                                    PVRSRV_MEMALLOCFLAG_ZERO_ON_ALLOC |
	                                    PVRSRV_MEMALLOCFLAG_GPU_READABLE |
	                                    PVRSRV_MEMALLOCFLAG_GPU_WRITEABLE |
	                                    PVRSRV_MEMALLOCFLAG_CPU_READABLE |
	                                    PVRSRV_MEMALLOCFLAG_CPU_WRITEABLE |
	                                    PVRSRV_MEMALLOCFLAG_UNCACHED,
	                                    "FwExUFOBlock",
	                                    psMemDesc);
	if (eError != PVRSRV_OK)
	{
		goto e0;
	}

	eError = RGXSetFirmwareAddress(&pFirmwareAddr, *psMemDesc, 0, RFW_FWADDR_FLAG_NONE);
	PVR_GOTO_IF_ERROR(eError, e1);

	*puiSyncPrimVAddr = pFirmwareAddr.ui32Addr;
	*puiSyncPrimBlockSize = TRUNCATE_64BITS_TO_32BITS(uiUFOBlockSize);

	return PVRSRV_OK;

e1:
	DevmemFwUnmapAndFree(psDevInfo, *psMemDesc);
e0:
	return eError;
}

static void RGXFreeUFOBlock(PVRSRV_DEVICE_NODE *psDeviceNode,
                            DEVMEM_MEMDESC *psMemDesc)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	RGXUnsetFirmwareAddress(psMemDesc);
	DevmemFwUnmapAndFree(psDevInfo, psMemDesc);
}

/**************************************************************************/ /*!
@Function       RGXDevClockSpeed
@Description    Gets the clock speed for the given device node and returns
                it in pui32RGXClockSpeed.
@Input          psDeviceNode        Device node
@Output         pui32RGXClockSpeed  Variable for storing the clock speed
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static PVRSRV_ERROR RGXDevClockSpeed(PVRSRV_DEVICE_NODE *psDeviceNode,
                                     IMG_PUINT32  pui32RGXClockSpeed)
{
	RGX_DATA *psRGXData = (RGX_DATA*) psDeviceNode->psDevConfig->hDevData;

	/* get clock speed */
	*pui32RGXClockSpeed = psRGXData->psRGXTimingInfo->ui32CoreClockSpeed;

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       RGXDevVersionString
@Description    Gets the version string for the given device node and returns
                a pointer to it in ppszVersionString. It is then the
                responsibility of the caller to free this memory.
@Input          psDeviceNode        Device node from which to obtain the
                                    version string
@Output.        ppszVersionString   Contains the version string upon return
@Return         PVRSRV_ERROR
*/ /**************************************************************************/
static PVRSRV_ERROR RGXDevVersionString(PVRSRV_DEVICE_NODE *psDeviceNode,
                                        IMG_CHAR **ppszVersionString)
{
#if defined(NO_HARDWARE)
	const IMG_CHAR *pszFormatString = "GPU variant BVNC: %s (SW)";
#else
	const IMG_CHAR *pszFormatString = PVRSRVIsEmulatorPlatform(psDeviceNode) ?
		"GPU variant BVNC: %s (SW)" : "GPU variant BVNC: %s (HW)";
#endif
	PVRSRV_RGXDEV_INFO *psDevInfo;
	IMG_PCHAR pszBVNC;
	size_t uiStringLength;

	if (psDeviceNode == NULL || ppszVersionString == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (PVRSRV_RGXDEV_INFO *)psDeviceNode->pvDevice;
	pszBVNC = RGXDevBVNCString(psDevInfo);

	if (NULL == pszBVNC)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* -2 for %s. +1 for NULL terminate */
	uiStringLength = OSStringLength(pszBVNC) + (OSStringLength(pszFormatString) - 2) + 1;
	*ppszVersionString = OSAllocMem(uiStringLength * sizeof(IMG_CHAR));
	if (*ppszVersionString == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	OSSNPrintf(*ppszVersionString, uiStringLength, pszFormatString, pszBVNC);

	return PVRSRV_OK;
}

/*
	RGXDevMMUAttributes
*/
static MMU_DEVICEATTRIBS *RGXDevMMUAttributes(PVRSRV_DEVICE_NODE *psDeviceNode,
                                              IMG_BOOL bKernelFWMemoryCtx)
{
	MMU_DEVICEATTRIBS *psMMUDevAttrs = NULL;

	if (psDeviceNode->pfnCheckDeviceFeature)
	{
#if defined(RGX_FEATURE_MIPS_BIT_MASK)
		if (PVRSRV_IS_FEATURE_SUPPORTED(psDeviceNode, MIPS))
		{
			psMMUDevAttrs = bKernelFWMemoryCtx ?
			                psDeviceNode->psFirmwareMMUDevAttrs :
			                psDeviceNode->psMMUDevAttrs;
		}
		else
#endif
		{
			PVR_UNREFERENCED_PARAMETER(bKernelFWMemoryCtx);
			psMMUDevAttrs = psDeviceNode->psMMUDevAttrs;
		}
	}

	return psMMUDevAttrs;
}

static PVRSRV_ERROR RGXAlignmentCheck(PVRSRV_DEVICE_NODE *psDevNode,
                                      IMG_UINT32 ui32AlignChecksSizeUM,
                                      IMG_UINT32 aui32AlignChecksUM[])
{
	static const IMG_UINT32 aui32AlignChecksKM[] = {RGXFW_ALIGN_CHECKS_INIT_KM};
	IMG_UINT32 ui32UMChecksOffset = ARRAY_SIZE(aui32AlignChecksKM) + 1;
	PVRSRV_RGXDEV_INFO *psDevInfo = psDevNode->pvDevice;
	IMG_UINT32 i, *paui32FWAlignChecks;
	PVRSRV_ERROR eError = PVRSRV_OK;

	/* Skip the alignment check if the driver is guest
	   since there is no firmware to check against */
	PVRSRV_VZ_RET_IF_MODE(GUEST, DEVNODE, psDevNode, eError);

	if (psDevInfo->psRGXFWAlignChecksMemDesc == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: FW Alignment Check Mem Descriptor is NULL",
		         __func__));
		return PVRSRV_ERROR_ALIGNMENT_ARRAY_NOT_AVAILABLE;
	}

	eError = DevmemAcquireCpuVirtAddr(psDevInfo->psRGXFWAlignChecksMemDesc,
	                                  (void **) &paui32FWAlignChecks);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed to acquire kernel address for alignment checks (%u)",
		         __func__,
		         eError));
		return eError;
	}

	paui32FWAlignChecks += ui32UMChecksOffset;
	/* Invalidate the size value, check the next region size (UM) and invalidate */
	RGXFwSharedMemCacheOpPtr(paui32FWAlignChecks, INVALIDATE);
	if (*paui32FWAlignChecks != ui32AlignChecksSizeUM)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Mismatching sizes of RGXFW_ALIGN_CHECKS_INIT"
		         " array between UM(%d) and FW(%d)",
		         __func__,
		         ui32AlignChecksSizeUM,
		         *paui32FWAlignChecks));
		eError = PVRSRV_ERROR_INVALID_ALIGNMENT;
		goto return_;
	}
	paui32FWAlignChecks++;

	RGXFwSharedMemCacheOpExec(paui32FWAlignChecks,
	                          ui32AlignChecksSizeUM * sizeof(IMG_UINT32),
	                          PVRSRV_CACHE_OP_INVALIDATE);

	for (i = 0; i < ui32AlignChecksSizeUM; i++)
	{
		if (aui32AlignChecksUM[i] != paui32FWAlignChecks[i])
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: size/offset mismatch in RGXFW_ALIGN_CHECKS_INIT[%d]"
					" between UM(%d) and FW(%d)",
					__func__, i, aui32AlignChecksUM[i], paui32FWAlignChecks[i]));
			eError = PVRSRV_ERROR_INVALID_ALIGNMENT;
		}
	}

	if (eError == PVRSRV_ERROR_INVALID_ALIGNMENT)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Check for FW/KM structure"
				" alignment failed.", __func__));
	}

return_:

	DevmemReleaseCpuVirtAddr(psDevInfo->psRGXFWAlignChecksMemDesc);

	return eError;
}

/*
	RGXGetTFBCLossyGroup
*/
static IMG_UINT32 RGXGetTFBCLossyGroup(PVRSRV_DEVICE_NODE *psDeviceNode)
{
#if defined(RGX_FEATURE_TFBC_LOSSY_37_PERCENT_BIT_MASK)
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	return psDevInfo->ui32TFBCLossyGroup;
#else
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
	return 0;
#endif
}

void RGXDeviceInitCallbacks(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_ASSERT(psDeviceNode != NULL);

	/* Callback for getting the MMU device attributes */
	psDeviceNode->pfnGetMMUDeviceAttributes = RGXDevMMUAttributes;
	psDeviceNode->pfnMMUCacheInvalidate = RGXMMUCacheInvalidate;
	psDeviceNode->pfnMMUCacheInvalidateKick = RGXMMUCacheInvalidateKick;
	psDeviceNode->pfnMMUCacheInvalidateKickAndWait = RGXMMUCacheInvalidateKickAndWait;

#if defined(RGX_BRN71422_TARGET_HARDWARE_PHYSICAL_ADDR)
	psDeviceNode->pfnMMUTopLevelPxWorkarounds = RGXMapBRN71422TargetPhysicalAddress;
#else
	psDeviceNode->pfnMMUTopLevelPxWorkarounds = NULL;
#endif

	psDeviceNode->pfnRegisterMemoryContext = RGXRegisterMemoryContext;
	psDeviceNode->pfnUnregisterMemoryContext = RGXUnregisterMemoryContext;

	psDeviceNode->pfnValidateAddressPermissions = RGXValidateAddressPermissions;
	psDeviceNode->pfnValidateExportableFlags = RGXValidateExportableFlags;

	/* Register callbacks for Unified Fence Objects */
	psDeviceNode->pfnAllocUFOBlock = RGXAllocUFOBlock;
	psDeviceNode->pfnFreeUFOBlock = RGXFreeUFOBlock;

#if defined(SUPPORT_AUTOVZ)
	/* Register callback for updating the virtualization watchdog */
	psDeviceNode->pfnUpdateAutoVzWatchdog = RGXUpdateAutoVzWatchdog;
#endif

	/* Register method to service the FW HWPerf buffer */
	psDeviceNode->pfnServiceHWPerf = RGXHWPerfDataStoreCB;

	/* Register callback for getting the device version information string */
	psDeviceNode->pfnDeviceVersionString = RGXDevVersionString;

	/* Register callback for getting the device clock speed */
	psDeviceNode->pfnDeviceClockSpeed = RGXDevClockSpeed;

	/* Register callback for resetting the HWR logs */
	psDeviceNode->pfnResetHWRLogs = RGXResetHWRLogs;

	/* Register callback for resetting the HWR logs */
	psDeviceNode->pfnVerifyBVNC = RGXVerifyBVNC;

	/* Register callback for checking alignment of UM structures */
	psDeviceNode->pfnAlignmentCheck = RGXAlignmentCheck;

	/* Register callback for checking the supported features and getting the
	 * corresponding values */
	psDeviceNode->pfnCheckDeviceFeature = RGXBvncCheckFeatureSupported;
	psDeviceNode->pfnGetDeviceFeatureValue = RGXBvncGetSupportedFeatureValue;

	/* Callback for getting TFBC configuration */
	psDeviceNode->pfnGetTFBCLossyGroup = RGXGetTFBCLossyGroup;
}

IMG_PCHAR RGXDevBVNCString(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	IMG_PCHAR psz = psDevInfo->sDevFeatureCfg.pszBVNCString;
	if (NULL == psz)
	{
		IMG_CHAR pszBVNCInfo[RGX_HWPERF_MAX_BVNC_LEN];
		size_t uiBVNCStringSize;
		size_t uiStringLength;

		uiStringLength = OSSNPrintf(pszBVNCInfo,
		                            RGX_HWPERF_MAX_BVNC_LEN,
		                            "%d.%d.%d.%d",
		                            psDevInfo->sDevFeatureCfg.ui32B,
		                            psDevInfo->sDevFeatureCfg.ui32V,
		                            psDevInfo->sDevFeatureCfg.ui32N,
		                            psDevInfo->sDevFeatureCfg.ui32C);
		PVR_ASSERT(uiStringLength < RGX_HWPERF_MAX_BVNC_LEN);

		uiBVNCStringSize = (uiStringLength + 1) * sizeof(IMG_CHAR);
		psz = OSAllocMem(uiBVNCStringSize);
		if (NULL != psz)
		{
			OSCachedMemCopy(psz, pszBVNCInfo, uiBVNCStringSize);
			psDevInfo->sDevFeatureCfg.pszBVNCString = psz;
		}
		else
		{
			PVR_DPF((PVR_DBG_MESSAGE,
			         "%s: Allocating memory for BVNC Info string failed",
			         __func__));
		}
	}

	return psz;
}

IMG_UINT32 RGXHeapDerivePageSize(IMG_UINT32 uiLog2PageSize)
{
	IMG_BOOL bFound = IMG_FALSE;
	IMG_UINT32 ui32PageSizeMask = RGXGetValidHeapPageSizeMask();

	/* OS page shift must be at least RGX_HEAP_4KB_PAGE_SHIFT,
	 * max RGX_HEAP_2MB_PAGE_SHIFT, non-zero and a power of two */
	if (uiLog2PageSize == 0U ||
	    (uiLog2PageSize < RGX_HEAP_4KB_PAGE_SHIFT) ||
	    (uiLog2PageSize > RGX_HEAP_2MB_PAGE_SHIFT))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Provided incompatible log2 page size %u",
				__func__,
				uiLog2PageSize));
		PVR_ASSERT(0);
		return 0;
	}

	do
	{
		if ((IMG_PAGE2BYTES32(uiLog2PageSize) & ui32PageSizeMask) == 0)
		{
			/* We have to fall back to a smaller device
			 * page size than given page size because there
			 * is no exact match for any supported size. */
			uiLog2PageSize -= 1U;
		}
		else
		{
			/* All good, RGX page size equals given page size
			 * => use it as default for heaps */
			bFound = IMG_TRUE;
		}
	} while (!bFound);

	return uiLog2PageSize;
}

#if !defined(NO_HARDWARE)
IMG_BOOL SampleIRQCount(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	IMG_BOOL bReturnVal = IMG_FALSE;
	volatile IMG_UINT32 *pui32SampleIrqCount = psDevInfo->aui32SampleIRQCount;
	IMG_UINT32 ui32IrqCnt;

#if defined(RGX_FW_IRQ_OS_COUNTERS)
	if (PVRSRV_VZ_MODE_IS(GUEST, DEVINFO, psDevInfo))
	{
		bReturnVal = IMG_TRUE;
	}
	else
	{
		get_irq_cnt_val(ui32IrqCnt, RGXFW_HOST_DRIVER_ID, psDevInfo);

		if (ui32IrqCnt != pui32SampleIrqCount[RGXFW_THREAD_0])
		{
			pui32SampleIrqCount[RGXFW_THREAD_0] = ui32IrqCnt;
			bReturnVal = IMG_TRUE;
		}
	}
#else
	IMG_UINT32 ui32TID;

	for_each_irq_cnt(ui32TID)
	{
		get_irq_cnt_val(ui32IrqCnt, ui32TID, psDevInfo);

		/* treat unhandled interrupts here to align host count with fw count */
		if (pui32SampleIrqCount[ui32TID] != ui32IrqCnt)
		{
			pui32SampleIrqCount[ui32TID] = ui32IrqCnt;
			bReturnVal = IMG_TRUE;
		}
	}
#endif

	return bReturnVal;
}

static IMG_BOOL _WaitForInterruptsTimeoutCheck(PVRSRV_RGXDEV_INFO *psDevInfo)
{
#if defined(PVRSRV_DEBUG_LISR_EXECUTION)
	PVRSRV_DEVICE_NODE *psDeviceNode = psDevInfo->psDeviceNode;
	IMG_UINT32 ui32idx;
#endif

	RGXDEBUG_PRINT_IRQ_COUNT(psDevInfo);

#if defined(PVRSRV_DEBUG_LISR_EXECUTION)
	PVR_DPF((PVR_DBG_ERROR,
	        "Last RGX_LISRHandler State (DevID %u): 0x%08X Clock: %" IMG_UINT64_FMTSPEC,
	        psDeviceNode->sDevId.ui32InternalID,
	        psDeviceNode->sLISRExecutionInfo.ui32Status,
	        psDeviceNode->sLISRExecutionInfo.ui64Clockns));

	for_each_irq_cnt(ui32idx)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         MSG_IRQ_CNT_TYPE " %u: InterruptCountSnapshot: 0x%X",
		         ui32idx, psDeviceNode->sLISRExecutionInfo.aui32InterruptCountSnapshot[ui32idx]));
	}
#else
	PVR_DPF((PVR_DBG_ERROR, "No further information available. Please enable PVRSRV_DEBUG_LISR_EXECUTION"));
#endif

	return SampleIRQCount(psDevInfo);
}

void RGX_WaitForInterruptsTimeout(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	IMG_BOOL bScheduleMISR;

	if (PVRSRV_VZ_MODE_IS(GUEST, DEVINFO, psDevInfo))
	{
		bScheduleMISR = IMG_TRUE;
	}
	else
	{
		bScheduleMISR = _WaitForInterruptsTimeoutCheck(psDevInfo);
	}

	if (bScheduleMISR)
	{
		OSScheduleMISR(psDevInfo->pvMISRData);

		if (psDevInfo->pvAPMISRData != NULL)
		{
			OSScheduleMISR(psDevInfo->pvAPMISRData);
		}
	}
}

IMG_BOOL RGXAckHwIrq(PVRSRV_RGXDEV_INFO *psDevInfo,
                     IMG_UINT32 ui32IRQStatusReg,
                     IMG_UINT32 ui32IRQStatusEventMask,
                     IMG_UINT32 ui32IRQClearReg,
                     IMG_UINT32 ui32IRQClearMask)
{
	if (OSReadHWReg32(psDevInfo->pvRegsBaseKM, ui32IRQStatusReg) & ui32IRQStatusEventMask)
	{
		/* acknowledge and clear the interrupt */
		OSWriteHWReg32(psDevInfo->pvRegsBaseKM, ui32IRQClearReg, ui32IRQClearMask);

		/* Perform a readback as barrier here after clearing the interrupt.
		 * If host side mem read happens before we clear the interrupt it is possible
		 * that we read the stale value then fw updates the second interrupt which is
		 * ignored and then we clear interrupt which would mean host will end up with
		 * a stale IRQCount value.
		 */
		(void) OSReadHWReg32(psDevInfo->pvRegsBaseKM, ui32IRQClearReg);

		return IMG_TRUE;
	}
	else
	{
		/* spurious interrupt */
		return IMG_FALSE;
	}
}

IMG_BOOL RGXAckIrqDedicated(PVRSRV_RGXDEV_INFO *psDevInfo)
{
	/* status & clearing registers are available on both Host and Guests
	 * and are agnostic of the Fw CPU type. Due to the remappings done by
	 * the 2nd stage device MMU, all drivers assume they are accessing
	 * register bank 0 */
	return RGXAckHwIrq(psDevInfo,
	                   RGX_CR_IRQ_OS0_EVENT_STATUS,
	                   ~RGX_CR_IRQ_OS0_EVENT_STATUS_SOURCE_CLRMSK,
	                   RGX_CR_IRQ_OS0_EVENT_CLEAR,
	                   ~RGX_CR_IRQ_OS0_EVENT_CLEAR_SOURCE_CLRMSK);
}
#endif

PVRSRV_ERROR RGXInitCreateFWKernelMemoryContext(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	/* set up fw memory contexts */
	PVRSRV_RGXDEV_INFO   *psDevInfo = psDeviceNode->pvDevice;
	__maybe_unused PVRSRV_DEVICE_CONFIG *psDevConfig = psDeviceNode->psDevConfig;
	PVRSRV_ERROR eError;

#if defined(RGX_PREMAP_FW_HEAPS) || defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS)
	IMG_BOOL  bNativeFwUMAHeap = PVRSRV_VZ_MODE_IS(NATIVE, DEVNODE, psDeviceNode) &&
	                             (PhysHeapGetType(psDeviceNode->apsPhysHeap[FIRST_PHYSHEAP_MAPPED_TO_FW_MAIN_DEVMEM]) == PHYS_HEAP_TYPE_UMA);
#endif

#if defined(RGX_PREMAP_FW_HEAPS)
	PHYS_HEAP *psDefaultPhysHeap = psDeviceNode->psMMUPhysHeap;

	if ((!PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode)) && (!bNativeFwUMAHeap))
	{
		PHYS_HEAP *psFwPageTableHeap =
				psDeviceNode->apsPhysHeap[PVRSRV_PHYS_HEAP_FW_PREMAP_PT];

		PVR_LOG_GOTO_IF_INVALID_PARAM((psFwPageTableHeap != NULL),
		                              eError, failed_to_create_ctx);

		/* Temporarily swap the MMU and default GPU physheap to allow the page
		 * tables of all memory mapped by the FwKernel context to be placed
		 * in a dedicated memory carveout. This should allow the firmware mappings to
		 * persist after a Host kernel crash or driver reset. */
		psDeviceNode->psMMUPhysHeap = psFwPageTableHeap;
	}
#endif

#if !defined(SUPPORT_TRUSTED_DEVICE)
#if defined(RGX_FEATURE_AXI_ACE_BIT_MASK) || defined(RGX_FEATURE_AXI_ACELITE_BIT_MASK)
	/* Set the GPU device coherency before FW context creation */
	eError = RGXGetCoreSnoopMode(psDevConfig,
	                            &psDeviceNode->eGpuSnoopingFeature);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed RGXGetCoreSnoopMode (%u)",
		         __func__,
		         eError));
		goto failed_to_create_ctx;
	}

	/* Initializing the cache snooping early, see DDKSERV-91384 */
	DoRGXInitialiseCacheSnooping(psDeviceNode);
#endif
#endif

	RGXFwSharedMemCheckSnoopMode(psDeviceNode);

	/* Create the memory context for the firmware. */
	eError = DevmemCreateContext(psDeviceNode, DEVMEM_HEAPCFG_FORFW,
	                             &psDevInfo->psKernelDevmemCtx);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed DevmemCreateContext (%u)",
		         __func__,
		         eError));
		goto failed_to_create_ctx;
	}

	if (!PVRSRV_VZ_MODE_IS(GUEST, DEVINFO, psDevInfo))
	{
		eError = DevmemFindHeapByName(psDevInfo->psKernelDevmemCtx, RGX_FIRMWARE_CODE_HEAP_IDENT,
		                              &psDevInfo->psFirmwareCodeHeap);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Failed DevmemFindHeapByName (%u)",
			         __func__,
			         eError));
			goto failed_to_find_heap;
		}

		eError = DevmemFindHeapByName(psDevInfo->psKernelDevmemCtx, RGX_FIRMWARE_PRIV_DATA_HEAP_IDENT,
		                              &psDevInfo->psFirmwarePrivDataHeap);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Failed DevmemFindHeapByName (%u)",
			         __func__,
			         eError));
			goto failed_to_find_heap;
		}

		eError = DevmemFindHeapByName(psDevInfo->psKernelDevmemCtx, RGX_FIRMWARE_PRIV_RODATA_HEAP_IDENT,
		                              &psDevInfo->psFirmwarePrivRoDataHeap);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Failed DevmemFindHeapByName (%u)",
			         __func__,
			         eError));
			goto failed_to_find_heap;
		}

		eError = DevmemFindHeapByName(psDevInfo->psKernelDevmemCtx, RGX_FIRMWARE_CUSTOM_HEAP_IDENT,
		                              &psDevInfo->psFirmwareCustomHeap);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,
			         "%s: Failed DevmemFindHeapByName (%u)",
			         __func__,
			         eError));
			goto failed_to_find_heap;
		}
	}

	eError = DevmemFindHeapByName(psDevInfo->psKernelDevmemCtx, RGX_FIRMWARE_MAIN_HEAP_IDENT,
	                              &psDevInfo->psFirmwareMainHeap);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed DevmemFindHeapByName (%u)",
		         __func__,
		         eError));
		goto failed_to_find_heap;
	}

	eError = DevmemFindHeapByName(psDevInfo->psKernelDevmemCtx, RGX_FIRMWARE_CONFIG_HEAP_IDENT,
	                              &psDevInfo->psFirmwareConfigHeap);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
		         "%s: Failed DevmemFindHeapByName (%u)",
		         __func__,
		         eError));
		goto failed_to_find_heap;
	}

#if (defined(RGX_PREMAP_FW_HEAPS)) || (defined(RGX_NUM_DRIVERS_SUPPORTED) && (RGX_NUM_DRIVERS_SUPPORTED > 1))
	if (!PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode))
	{
		IMG_UINT32 ui32DriverID;

		FOREACH_DRIVER_RAW_DEVMEM_HEAP(ui32DriverID, DEVNODE, psDeviceNode)
		{
			IMG_CHAR szHeapName[RA_MAX_NAME_LENGTH];

			OSSNPrintf(szHeapName, sizeof(szHeapName), RGX_FIRMWARE_GUEST_RAW_HEAP_IDENT, ui32DriverID);
			eError = DevmemFindHeapByName(psDevInfo->psKernelDevmemCtx, szHeapName,
										  &psDevInfo->psPremappedFwRawHeap[ui32DriverID]);
			PVR_LOG_GOTO_IF_ERROR(eError, "DevmemFindHeapByName", failed_to_find_heap);
		}
	}
#endif

#if defined(RGX_PREMAP_FW_HEAPS) || defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS)
	if (!PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode) && !bNativeFwUMAHeap)
	{
		IMG_DEV_PHYADDR sPhysHeapBase;
		IMG_UINT32 ui32DriverID;
		void *pvAppHintState = NULL;
		IMG_UINT64 ui64DefaultHeapStride;
		IMG_UINT64 ui64GuestHeapDevBaseStride;

		OSCreateAppHintState(&pvAppHintState);
		ui64DefaultHeapStride = PVRSRV_APPHINT_GUESTFWHEAPSTRIDE;
		OSGetAppHintUINT64(APPHINT_NO_DEVICE,
							pvAppHintState,
							GuestFWHeapStride,
							&ui64DefaultHeapStride,
							&ui64GuestHeapDevBaseStride);
		OSFreeAppHintState(pvAppHintState);
		pvAppHintState = NULL;

#if defined(RGX_PREMAP_FW_HEAPS)
		eError = RGXPremapHostHeaps(psDeviceNode);
		PVR_LOG_GOTO_IF_ERROR(eError, "RGXPremapHostHeaps", failed_to_find_heap);
#endif

		eError = PhysHeapGetDevPAddr(psDeviceNode->apsPhysHeap[FIRST_PHYSHEAP_MAPPED_TO_FW_MAIN_DEVMEM], &sPhysHeapBase);
		PVR_LOG_GOTO_IF_ERROR(eError, "PhysHeapGetDevPAddr", failed_to_find_heap);

		FOREACH_DRIVER_RAW_PHYS_HEAP(ui32DriverID, DEVNODE, psDeviceNode)
		{
			IMG_DEV_PHYADDR sRawFwHeapBase = {sPhysHeapBase.uiAddr + (ui32DriverID * ui64GuestHeapDevBaseStride)};

			eError = RGXFwRawHeapAllocMap(psDeviceNode,
										  ui32DriverID,
										  sRawFwHeapBase,
										  RGX_FIRMWARE_RAW_HEAP_SIZE);
			if (eError != PVRSRV_OK)
			{
				for (; ui32DriverID > RGXFW_GUEST_DRIVER_ID_START; ui32DriverID--)
				{
					RGXFwRawHeapUnmapFree(psDeviceNode, ui32DriverID);
				}
				PVR_LOG_GOTO_IF_ERROR(eError, "RGXFwRawHeapAllocMap", failed_to_find_heap);
			}
		}

#if defined(RGX_PREMAP_FW_HEAPS)
		/* restore default Px setup */
		psDeviceNode->psMMUPhysHeap = psDefaultPhysHeap;
#endif
	}
#endif /* defined(RGX_PREMAP_FW_HEAPS) || defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS) */

#if !defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS)
	/* On setups with dynamically mapped Guest heaps, the Guest makes
	 * a PVZ call to the Host to request the mapping during init. */
	if (PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode))
	{
		eError = PvzClientMapDevPhysHeap(psDevConfig);
		PVR_LOG_GOTO_IF_ERROR(eError, "PvzClientMapDevPhysHeap", failed_to_find_heap);
	}
#endif /* !defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS) */

	if (PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode))
	{
		DevmemHeapSetPremapStatus(psDevInfo->psFirmwareMainHeap, IMG_TRUE);
		DevmemHeapSetPremapStatus(psDevInfo->psFirmwareConfigHeap, IMG_TRUE);
	}

	return eError;

failed_to_find_heap:
	/*
	 * Clear the mem context create callbacks before destroying the RGX firmware
	 * context to avoid a spurious callback.
	 */
	psDeviceNode->pfnRegisterMemoryContext = NULL;
	psDeviceNode->pfnUnregisterMemoryContext = NULL;
	DevmemDestroyContext(psDevInfo->psKernelDevmemCtx);
	psDevInfo->psKernelDevmemCtx = NULL;
failed_to_create_ctx:
	return eError;
}

void RGXDeInitDestroyFWKernelMemoryContext(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR       eError;
#if defined(RGX_PREMAP_FW_HEAPS)
	PHYS_HEAP *psDefaultPhysHeap = psDeviceNode->psMMUPhysHeap;
#endif

#if defined(RGX_PREMAP_FW_HEAPS) || defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS)
	if (!PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode))
	{
		IMG_UINT32 ui32DriverID;
#if defined(RGX_PREMAP_FW_HEAPS)
		IMG_BOOL bNativeFwUMAHeap = PVRSRV_VZ_MODE_IS(NATIVE, DEVNODE, psDeviceNode) &&
		                            (PhysHeapGetType(psDeviceNode->apsPhysHeap[FIRST_PHYSHEAP_MAPPED_TO_FW_MAIN_DEVMEM]) == PHYS_HEAP_TYPE_UMA);

		if (!bNativeFwUMAHeap)
		{
			psDeviceNode->psMMUPhysHeap =
					psDeviceNode->apsPhysHeap[PVRSRV_PHYS_HEAP_FW_PREMAP_PT];
		}
#endif

#if defined(RGX_PREMAP_FW_HEAPS)
		RGXUnmapHostHeaps(psDeviceNode);
#endif

		FOREACH_DRIVER_RAW_PHYS_HEAP(ui32DriverID, DEVNODE, psDeviceNode)
		{
			RGXFwRawHeapUnmapFree(psDeviceNode, ui32DriverID);
		}
	}
#endif /* defined(RGX_PREMAP_FW_HEAPS) || defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS) */

#if !defined(RGX_VZ_STATIC_CARVEOUT_FW_HEAPS)
	if (PVRSRV_VZ_MODE_IS(GUEST, DEVNODE, psDeviceNode))
	{
		(void) PvzClientUnmapDevPhysHeap(psDeviceNode->psDevConfig);

		if (psDevInfo->psFirmwareMainHeap)
		{
			DevmemHeapSetPremapStatus(psDevInfo->psFirmwareMainHeap, IMG_FALSE);
		}
		if (psDevInfo->psFirmwareConfigHeap)
		{
			DevmemHeapSetPremapStatus(psDevInfo->psFirmwareConfigHeap, IMG_FALSE);
		}
	}
#endif

	/*
	 * Clear the mem context create callbacks before destroying the RGX firmware
	 * context to avoid a spurious callback.
	 */
	psDeviceNode->pfnRegisterMemoryContext = NULL;
	psDeviceNode->pfnUnregisterMemoryContext = NULL;

	if (psDevInfo->psKernelDevmemCtx)
	{
		eError = DevmemDestroyContext(psDevInfo->psKernelDevmemCtx);
		PVR_ASSERT(eError == PVRSRV_OK);
	}

#if defined(RGX_PREMAP_FW_HEAPS)
	psDeviceNode->psMMUPhysHeap = psDefaultPhysHeap;
#endif
}

#if !defined(SUPPORT_TRUSTED_DEVICE)
#if defined(RGX_FEATURE_AXI_ACE_BIT_MASK) || defined(RGX_FEATURE_AXI_ACELITE_BIT_MASK)

void DoRGXInitialiseCacheSnooping(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig = psDeviceNode->psDevConfig;

	/* if the system supports coherency and the device is configured to support coherency (see RGXGetCoreSnoopMode) */
	if (psDevConfig->eSystemCoherencyMode == PVRSRV_DEVICE_SNOOP_CPU_ONLY)
	{
		if (psDeviceNode->eGpuSnoopingFeature == PVRSRV_DEVICE_SNOOP_CPU_ONLY)
		{
			/* Provisionally initialise cache snooping, as it may be
			* downgraded a bit later when we run coherency tests */
			psDeviceNode->eConfirmedCacheSnoopingMode = PVRSRV_DEVICE_SNOOP_CPU_ONLY;
			return;
		}
#if defined(DEBUG)
		else
		{
			PVR_DPF((PVR_DBG_ERROR,"%s Device cache coherency feature not supported", __func__));
		}
#endif
	}
    psDeviceNode->eConfirmedCacheSnoopingMode = PVRSRV_DEVICE_SNOOP_NONE;
}

void RGXInitialiseCacheSnooping(const void *hPrivate)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PVRSRV_DEVICE_NODE *psDeviceNode;

	PVR_ASSERT(hPrivate != NULL);

	psDevInfo = ((RGX_LAYER_PARAMS*) hPrivate)->psDevInfo;
	psDeviceNode = psDevInfo->psDeviceNode;
	DoRGXInitialiseCacheSnooping(psDeviceNode);
}


void RGXDisableCacheSnooping(const void *hPrivate)
{
	PVRSRV_RGXDEV_INFO *psDevInfo;
	PVRSRV_DEVICE_NODE *psDeviceNode;

	PVR_ASSERT(hPrivate != NULL);

	psDevInfo = ((RGX_LAYER_PARAMS*) hPrivate)->psDevInfo;
	psDeviceNode = psDevInfo->psDeviceNode;

	/* disable the confirmed coherency level */
	psDeviceNode->eConfirmedCacheSnoopingMode = PVRSRV_DEVICE_SNOOP_NONE;
}
#endif /* RGX_FEATURE_AXI_ACE_BIT_MASK || RGX_FEATURE_AXI_ACELITE_BIT_MASK */
#endif /* !SUPPORT_TRUSTED_DEVICE */
