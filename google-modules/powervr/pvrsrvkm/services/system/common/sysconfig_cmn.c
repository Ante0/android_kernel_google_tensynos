/*************************************************************************/ /*!
@File
@Title          Sysconfig layer common to all platforms
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements system layer functions common to all platforms
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

#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv.h"
#include "pvrsrv_device.h"
#include "sysconfig_cmn.h"
#include "pvr_debug.h"
#include "os_apphint.h"
#include "rgx_fwif_shared.h"

#if defined(VIRTUAL_PLATFORM)
#define FPGA_RGX_TB_ERYX_REG_WRAPPER_OFFSET 0x2000000
#define FPGA_RGX_TB_ERYX_REG_WRAPPER_SIZE   0x8000

#define FPGA_RGX_TB_ECHO_REG_WRAPPER_OFFSET 0x4000000
#define FPGA_RGX_TB_ECHO_REG_WRAPPER_SIZE   0x8000

#define FPGA_RGX_TB_DIVANO_REG_WRAPPER_OFFSET 0x2000000
#define FPGA_RGX_TB_DIVANO_REG_WRAPPER_SIZE   0x8000

#define FPGA_RGX_TB_CATURIX_REG_WRAPPER_OFFSET 0x800000
#define FPGA_RGX_TB_CATURIX_REG_WRAPPER_SIZE   0x4000

#define FPGA_RGX_TB_BARREX_REG_WRAPPER_OFFSET 0x800000
#define FPGA_RGX_TB_BARREX_REG_WRAPPER_SIZE   0x4000

#define FPGA_RGX_TB_ALBIORIX_REG_WRAPPER_OFFSET 0x800000
#define FPGA_RGX_TB_ALBIORIX_REG_WRAPPER_SIZE   0x4000

#define FPGA_RGX_TB_DEFAULT_REG_WRAPPER_OFFSET 0x1000000
#define FPGA_RGX_TB_DEFAULT_REG_WRAPPER_SIZE   0x8000
#else
#define FPGA_RGX_TB_ERYX_REG_WRAPPER_OFFSET 0x1000000
#define FPGA_RGX_TB_ERYX_REG_WRAPPER_SIZE   0x8000

#define FPGA_RGX_TB_ECHO_REG_WRAPPER_OFFSET 0x1000000
#define FPGA_RGX_TB_ECHO_REG_WRAPPER_SIZE   0x8000

#define FPGA_RGX_TB_DIVANO_REG_WRAPPER_OFFSET 0x1000000
#define FPGA_RGX_TB_DIVANO_REG_WRAPPER_SIZE   0x8000

#define FPGA_RGX_TB_CATURIX_REG_WRAPPER_OFFSET 0x1000000
#define FPGA_RGX_TB_CATURIX_REG_WRAPPER_SIZE   0x8000

#define FPGA_RGX_TB_BARREX_REG_WRAPPER_OFFSET 0x1000000
#define FPGA_RGX_TB_BARREX_REG_WRAPPER_SIZE   0x8000

#define FPGA_RGX_TB_ALBIORIX_REG_WRAPPER_OFFSET 0x1000000
#define FPGA_RGX_TB_ALBIORIX_REG_WRAPPER_SIZE   0x8000

#define FPGA_RGX_TB_DEFAULT_REG_WRAPPER_OFFSET 0x1000000
#define FPGA_RGX_TB_DEFAULT_REG_WRAPPER_SIZE   0x8000
#endif

void SysRGXErrorNotify(IMG_HANDLE hSysData,
                       PVRSRV_ROBUSTNESS_NOTIFY_DATA *psErrorData)
{
	PVR_UNREFERENCED_PARAMETER(hSysData);

#if defined(PVRSRV_NEED_PVR_DPF)
	{
		IMG_UINT32 ui32DgbLvl;

		switch (psErrorData->eResetReason)
		{
			case RGX_CONTEXT_RESET_REASON_NONE:
			case RGX_CONTEXT_RESET_REASON_GUILTY_LOCKUP:
			case RGX_CONTEXT_RESET_REASON_INNOCENT_LOCKUP:
			case RGX_CONTEXT_RESET_REASON_GUILTY_OVERRUNING:
			case RGX_CONTEXT_RESET_REASON_INNOCENT_OVERRUNING:
			case RGX_CONTEXT_RESET_REASON_HARD_CONTEXT_SWITCH:
			case RGX_CONTEXT_RESET_REASON_GPU_ECC_OK:
			case RGX_CONTEXT_RESET_REASON_FW_ECC_OK:
			{
				ui32DgbLvl = PVR_DBG_MESSAGE;
				break;
			}
			case RGX_CONTEXT_RESET_REASON_GPU_ECC_HWR:
			case RGX_CONTEXT_RESET_REASON_FW_EXEC_ERR:
			case RGX_CONTEXT_RESET_REASON_GPU_PARITY_HWR:
			case RGX_CONTEXT_RESET_REASON_GPU_LATENT_HWR:
			{
				ui32DgbLvl = PVR_DBG_WARNING;
				break;
			}
			case RGX_CONTEXT_RESET_REASON_WGP_CHECKSUM:
			case RGX_CONTEXT_RESET_REASON_TRP_CHECKSUM:
			case RGX_CONTEXT_RESET_REASON_FW_ECC_ERR:
			case RGX_CONTEXT_RESET_REASON_FW_PTE_PARITY_ERR:
			case RGX_CONTEXT_RESET_REASON_FW_PARITY_ERR:
			case RGX_CONTEXT_RESET_REASON_FW_WATCHDOG:
			case RGX_CONTEXT_RESET_REASON_FW_PAGEFAULT:
			case RGX_CONTEXT_RESET_REASON_HOST_WDG_FW_ERR:
			case RGX_CONTEXT_PVRIC_SIGNATURE_MISMATCH:
			case RGX_CONTEXT_RESET_REASON_DCLS_ERR:
			case RGX_CONTEXT_RESET_REASON_GPU_PAGE_FAULT_HWR:
			case RGX_CONTEXT_RESET_REASON_CPU_PAGE_FAULT:
			case RGX_CONTEXT_RESET_REASON_GPU_LOCKUP_HWR:
			{
				ui32DgbLvl = PVR_DBG_ERROR;
				break;
			}
			default:
			{
				PVR_ASSERT(false && "Unhandled reset reason");
				ui32DgbLvl = PVR_DBG_ERROR;
				break;
			}
		}

		if (psErrorData->pid > 0)
		{
			PVRSRVDebugPrintf(ui32DgbLvl, __FILE__, __LINE__, " PID %d experienced error %d",
					 psErrorData->pid, psErrorData->eResetReason);
		}
		else
		{
			PVRSRVDebugPrintf(ui32DgbLvl, __FILE__, __LINE__, " Device experienced error %d",
					 psErrorData->eResetReason);
		}

		switch (psErrorData->eResetReason)
		{
			case RGX_CONTEXT_RESET_REASON_WGP_CHECKSUM:
			case RGX_CONTEXT_RESET_REASON_TRP_CHECKSUM:
			{
				PVRSRVDebugPrintf(ui32DgbLvl, __FILE__, __LINE__, "   ExtJobRef 0x%x, DM %d",
						 psErrorData->uErrData.sChecksumErrData.ui32ExtJobRef,
						 psErrorData->uErrData.sChecksumErrData.eDM);
			break;
			}
			default:
			{
				break;
			}
		}
	}
#else
	PVR_UNREFERENCED_PARAMETER(psErrorData);
#endif /* PVRSRV_NEED_PVR_DPF */
}

IMG_UINT64 SysRestrictGpuLocalPhysheap(IMG_UINT64 uiHeapSize)
{
	return uiHeapSize;
}

IMG_BOOL SysRestrictGpuLocalAddPrivateHeap(void)
{
	return IMG_FALSE;
}

IMG_BOOL SysDefaultToCpuLocalHeap(void)
{
//#if (TC_MEMORY_CONFIG == TC_MEMORY_HYBRID)
	void *pvAppHintState = NULL;
	IMG_BOOL bAppHintDefault = IMG_FALSE;
	IMG_BOOL bSetToCPULocal = IMG_FALSE;

	OSCreateAppHintState(&pvAppHintState);
	OSGetAppHintBOOL(APPHINT_NO_DEVICE, pvAppHintState,
			PhysHeapHybridDefault2CpuLocal, &bAppHintDefault, &bSetToCPULocal);
	OSFreeAppHintState(pvAppHintState);

	return bSetToCPULocal;
//#else
//	return IMG_FALSE;
//#endif
}

/*
 * CPU to Device physical address translation
 */
static
void UMAPhysHeapCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
				   IMG_UINT32 ui32NumOfAddr,
				   IMG_DEV_PHYADDR *psDevPAddr,
				   IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	if (!ui32NumOfAddr)
	{
		return;
	}

	/* Optimise common case */
	psDevPAddr[0].uiAddr = psCpuPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psDevPAddr[ui32Idx].uiAddr = psCpuPAddr[ui32Idx].uiAddr;
		}
	}
}
/*
 * Device to CPU physical address translation
 */
static
void UMAPhysHeapDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
				   IMG_UINT32 ui32NumOfAddr,
				   IMG_CPU_PHYADDR *psCpuPAddr,
				   IMG_DEV_PHYADDR *psDevPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	if (!ui32NumOfAddr)
	{
		return;
	}

	if (sizeof(*psCpuPAddr) < sizeof(*psDevPAddr))
	{
		/* Check we are not dropping any data from the 64bit dev addr */
		PVR_ASSERT(!(psDevPAddr[0].uiAddr >> 32));
	}

	/* Optimise common case */
	psCpuPAddr[0].uiAddr = IMG_CAST_TO_CPUPHYADDR_UINT(psDevPAddr[0].uiAddr);
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psCpuPAddr[ui32Idx].uiAddr = IMG_CAST_TO_CPUPHYADDR_UINT(psDevPAddr[ui32Idx].uiAddr);
		}
	}
}

PHYS_HEAP_FUNCTIONS g_sUmaHeapFns = {
	.pfnCpuPAddrToDevPAddr = UMAPhysHeapCpuPAddrToDevPAddr,
	.pfnDevPAddrToCpuPAddr = UMAPhysHeapDevPAddrToCpuPAddr,
};

#if defined(SUPPORT_NATIVE_FENCE_SYNC)


IMG_BOOL SysDevExtractFFToken(IMG_HANDLE hSysData,
                              IMG_HANDLE hEnvFenceObjPtr,
                              IMG_UINT16 *pui16FFToken)
{
	struct dma_fence *fence = hEnvFenceObjPtr;
	unsigned long flags = DMA_FENCE_EXTRACT_USER_BITS(fence->flags);

	/* Check token valid before extracting it */
	if (flags & (SYNC_CHECKPOINT_FW_UD_FF_TOKEN_VALID_EN))
	{
		*pui16FFToken = flags & SYNC_CHECKPOINT_FW_UD_FF_TOKEN_MASK;


		return IMG_TRUE;
	}

	return IMG_FALSE;
}

#endif

void __iomem *SysDevMapEMURegbank(IMG_UINT64 ui64Base, IMG_UINT64 *pui64Size)
{
	IMG_UINT64		ui64EMURegBankOffset;
	IMG_UINT64		ui64EMURegBankSize;
	IMG_CPU_PHYADDR	sWrapperRegsCpuPBase;

#if defined(RGX_FEATURE_ERYX_TOP_INFRASTRUCTURE)
	ui64EMURegBankOffset = FPGA_RGX_TB_ERYX_REG_WRAPPER_OFFSET;
	ui64EMURegBankSize = FPGA_RGX_TB_ERYX_REG_WRAPPER_SIZE;
#elif defined(RGX_FEATURE_ECHO_TOP_INFRASTRUCTURE)
	ui64EMURegBankOffset = FPGA_RGX_TB_ECHO_REG_WRAPPER_OFFSET;
	ui64EMURegBankSize = FPGA_RGX_TB_ECHO_REG_WRAPPER_SIZE;
#elif defined(RGX_FEATURE_DIVANO_TOP_INFRASTRUCTURE)
	ui64EMURegBankOffset = FPGA_RGX_TB_DIVANO_REG_WRAPPER_OFFSET;
	ui64EMURegBankSize = FPGA_RGX_TB_DIVANO_REG_WRAPPER_SIZE;
#elif defined(RGX_FEATURE_CATURIX_TOP_INFRASTRUCTURE)
	ui64EMURegBankOffset = FPGA_RGX_TB_CATURIX_REG_WRAPPER_OFFSET;
	ui64EMURegBankSize = FPGA_RGX_TB_CATURIX_REG_WRAPPER_SIZE;
#elif defined(RGX_FEATURE_BARREX_TOP_INFRASTRUCTURE)
	ui64EMURegBankOffset = FPGA_RGX_TB_BARREX_REG_WRAPPER_OFFSET;
	ui64EMURegBankSize = FPGA_RGX_TB_BARREX_REG_WRAPPER_SIZE;
#elif defined(RGX_FEATURE_ALBIORIX_TOP_INFRASTRUCTURE)
	ui64EMURegBankOffset = FPGA_RGX_TB_ALBIORIX_REG_WRAPPER_OFFSET;
	ui64EMURegBankSize = FPGA_RGX_TB_ALBIORIX_REG_WRAPPER_SIZE;
#else
	ui64EMURegBankOffset = FPGA_RGX_TB_DEFAULT_REG_WRAPPER_OFFSET;
	ui64EMURegBankSize = FPGA_RGX_TB_DEFAULT_REG_WRAPPER_SIZE;
#endif

	sWrapperRegsCpuPBase.uiAddr = ui64Base + ui64EMURegBankOffset;

	if (pui64Size)
	{
		*pui64Size = ui64EMURegBankSize;
	}

	return (void __iomem*) OSMapPhysToLin(sWrapperRegsCpuPBase,
	                                      ui64EMURegBankSize,
	                                      PVRSRV_MEMALLOCFLAG_CPU_UNCACHED);
}

#if defined(RGX_FEATURE_AXI_ACE_BIT_MASK)
PVRSRV_ERROR SysGetSystemCoherencyMode(PVRSRV_DEVICE_CONFIG *psDevConfig,
                                               PVRSRV_DEVICE_SNOOP_MODE *peCacheSnoopingMode)
{
	PVRSRV_DEVICE_SNOOP_MODE eSystemCoherencyMode;
#if !defined(NO_HARDWARE)
	IMG_UINT32 ui32SystemCoherency;
#endif
#if !defined(NO_HARDWARE)
	void *pvRegsBaseKM;
	IMG_BOOL bPowerDown = IMG_TRUE;
	PVRSRV_ERROR eError;
	IMG_CPU_PHYADDR sRegsCpuPBase = psDevConfig->sRegsCpuPBase;
	IMG_UINT32 ui32RegsSize = psDevConfig->ui32RegsSize;

	if (!sRegsCpuPBase.uiAddr || !ui32RegsSize)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid RGX register base/size parameters", __func__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	pvRegsBaseKM = OSMapPhysToLin(sRegsCpuPBase, ui32RegsSize, PVRSRV_MEMALLOCFLAG_CPU_UNCACHED);
	if (!pvRegsBaseKM)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed to create RGX register mapping", __func__));
		return PVRSRV_ERROR_BAD_MAPPING;
	}

	bPowerDown = ! PVRSRVIsSystemPowered(psDevConfig->psDevNode);

	/* Power-up the device as required to read the registers */
	if (!PVRSRV_VZ_MODE_IS(GUEST, DEVCFG, psDevConfig) && bPowerDown)
	{
		eError = PVRSRVSetSystemPowerState(psDevConfig, PVRSRV_SYS_POWER_STATE_ON);
		PVR_LOG_RETURN_IF_ERROR(eError, "PVRSRVSetSystemPowerState ON");
	}

	/* AXI support within the SoC, bitfield COHERENCY_SUPPORT [1 .. 0]
		value NO_COHERENCY        0x0 {SoC does not support any form of Coherency}
		value ACE_LITE_COHERENCY  0x1 {SoC supports ACE-Lite or I/O Coherency}
		value FULL_ACE_COHERENCY  0x2 {SoC supports full ACE or 2-Way Coherency} */
	ui32SystemCoherency = OSReadHWReg32((void __iomem *)pvRegsBaseKM, RGX_CR_SOC_AXI);
	PVR_LOG(("AXI system coherency (RGX_CR_SOC_AXI): 0x%x", ui32SystemCoherency));
#if defined(DEBUG)
	if (ui32SystemCoherency & ~((IMG_UINT32)RGX_CR_SOC_AXI_MASKFULL))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid RGX_CR_SOC_AXI value.", __func__));
		return PVRSRV_ERROR_INVALID_DEVICE;
	}
#endif
	ui32SystemCoherency &= ~((IMG_UINT32)RGX_CR_SOC_AXI_COHERENCY_SUPPORT_CLRMSK);
	ui32SystemCoherency >>= RGX_CR_SOC_AXI_COHERENCY_SUPPORT_SHIFT;

	if (!PVRSRV_VZ_MODE_IS(GUEST, DEVCFG, psDevConfig) && bPowerDown)
	{
		eError = PVRSRVSetSystemPowerState(psDevConfig, PVRSRV_SYS_POWER_STATE_OFF);
		PVR_LOG_RETURN_IF_ERROR(eError, "PVRSRVSetSystemPowerState OFF");
	}

	/* UnMap Regs */
	OSUnMapPhysToLin(pvRegsBaseKM, ui32RegsSize);

	switch (ui32SystemCoherency)
	{
	case RGX_CR_SOC_AXI_COHERENCY_SUPPORT_FULL_ACE_COHERENCY:
		eSystemCoherencyMode = PVRSRV_DEVICE_SNOOP_CROSS;
		break;

	case RGX_CR_SOC_AXI_COHERENCY_SUPPORT_ACE_LITE_COHERENCY:
		eSystemCoherencyMode = PVRSRV_DEVICE_SNOOP_CPU_ONLY;
		break;

	case RGX_CR_SOC_AXI_COHERENCY_SUPPORT_NO_COHERENCY:
	default:
		eSystemCoherencyMode = PVRSRV_DEVICE_SNOOP_NONE;
		break;
	}
#else /* !defined(NO_HARDWARE) */
	eSystemCoherencyMode = PVRSRV_DEVICE_SNOOP_CPU_ONLY;
#endif /* !defined(NO_HARDWARE) */


	*peCacheSnoopingMode = eSystemCoherencyMode;
	return PVRSRV_OK;
}
#endif

/******************************************************************************
 End of file (sysconfig_cmn.c)
******************************************************************************/
