/*************************************************************************/ /*!
@File
@Title          RGX system layer fast path fence interface
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX fast path fence system layer interface. Circular buffer
                implementation.
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

#include "pvr_debug.h"
#include "allocmem.h"
#include "pvrsrv_device.h"
#include "rgxfwutils.h"
#include "sys_fpf_fpt_cb.h"

/* These stub functions are defined for internal build purposes.
 *
 * On real platforms they would be defined in an APU driver.
 */

int fpf_apu2gpu_acquire_cb(struct fpf_cb_config *cb_config)
{
	return -1;
}

int fpf_gpu2apu_acquire_cb(struct fpf_cb_config *cb_config)
{
	return -1;
}

int fpf_apu2gpu_supply_kickreg(struct fpf_kickreg_config *mts_config)
{
	return -1;
}

int fpf_gpu2apu_acquire_kickreg(struct fpf_kickreg_config *apu_kickreg_config)
{
	return -1;
}

int fpf_apu_res_get(void)
{
	return -1;
}

int fpf_apu_res_put(void)
{
	return -1;
}

#define LINUX_LOG_GOTO_IF_ERROR(_err, _call, _goto) \
	if (_err != 0) { \
		PVR_DPF((PVR_DBG_ERROR, "%s: Failed with %d", _call, _err)); \
		goto _goto; \
	}

PVRSRV_ERROR SysFpfFptCbDeviceInit(PVRSRV_DEVICE_CONFIG *psDeviceConfig)
{
	RGX_FPF_KICK_COMMS_CONFIG *psFpfConfig;
	IMG_INT iError;

	PVR_LOG_RETURN_IF_INVALID_PARAM(psDeviceConfig != NULL, "psDeviceConfig");

	psFpfConfig = psDeviceConfig->psFpfConfigPrivData;

	iError = fpf_apu_res_get();
	if (iError != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "fpf_apu_res_get: Failed with %d", iError));
		return PVRSRV_ERROR_FPF_INIT_FAILED;
	}

	iError = fpf_apu2gpu_acquire_cb(&psFpfConfig->sApu2GpuConfig);
	LINUX_LOG_GOTO_IF_ERROR(iError, "fpf_apu2gpu_acquire_cb", err);

	iError = fpf_gpu2apu_acquire_cb(&psFpfConfig->sGpu2ApuConfig);
	LINUX_LOG_GOTO_IF_ERROR(iError, "fpf_gpu2apu_acquire_cb", err);

	{
		struct fpf_kickreg_config sMTSKickRegDetails = {
			.kickreg_regbank = {
				.regbank_paddr = psDeviceConfig->sRegsCpuPBase,
				.regbank_size = psDeviceConfig->ui32RegsSize,
			},
			.kickreg_details = {
				.reg_size = FPF_KICKREG_WRITE_SIZE_32B,
#if defined(RGX_FEATURE_NUM_OSIDS)
				.reg_offset = RGX_CR_MTS_SCHEDULE1,
				.write_value = RGXFWIF_DM_GP & ~RGX_CR_MTS_SCHEDULE1_DM_CLRMSK,
#else
				.reg_offset = RGX_CR_MTS_SCHEDULE,
				.write_value = RGXFWIF_DM_GP & ~RGX_CR_MTS_SCHEDULE_DM_CLRMSK,
#endif
			}
		};

		iError = fpf_apu2gpu_supply_kickreg(&sMTSKickRegDetails);
		LINUX_LOG_GOTO_IF_ERROR(iError, "fpf_apu2gpu_supply_kickreg", err);
	}

	iError = fpf_gpu2apu_acquire_kickreg(&psFpfConfig->sAPUKickRegConfig);
	LINUX_LOG_GOTO_IF_ERROR(iError, "fpf_gpu2apu_acquire_kickreg", err);


	FPFCommonDeviceInit(psDeviceConfig);

	psDeviceConfig->eGPUKickType = RGXFWIF_FPF_GPU_KICKREG_TYPE_MTS;

	return PVRSRV_OK;

err:
	fpf_apu_res_put();

	return PVRSRV_ERROR_FPF_INIT_FAILED;
}

void SysFpfFptCbDeviceDeInit(PVRSRV_DEVICE_CONFIG *psDeviceConfig)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceConfig);

	fpf_apu_res_put();
}

PVRSRV_ERROR SysFpfFptCbCommunicationInit(PVRSRV_DEVICE_CONFIG *psDeviceConfig,
                                          RGX_FPF_KICK_COMMS_FWCTX *psContext)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = psDeviceConfig->psDevNode;
	RGX_FPF_KICK_COMMS_CONFIG *psFpfConfig = psDeviceConfig->psFpfConfigPrivData;
	IMG_CPU_PHYADDR sApu2GpuBuffPAddr = { psFpfConfig->sApu2GpuConfig.buff_paddr };
	IMG_CPU_PHYADDR sApu2GpuCtrlPAddr = { psFpfConfig->sApu2GpuConfig.ctrl_paddr };
	IMG_CPU_PHYADDR sGpu2ApuBuffPAddr = { psFpfConfig->sGpu2ApuConfig.buff_paddr };
	IMG_CPU_PHYADDR sGpu2ApuCtrlPAddr = { psFpfConfig->sGpu2ApuConfig.ctrl_paddr };
	struct fpf_kickreg_config *psApuRegConfig = &psFpfConfig->sAPUKickRegConfig;
	IMG_CPU_PHYADDR *psApuKickRegBankPAddr = &psApuRegConfig->kickreg_regbank.regbank_paddr;
	IMG_UINT32 uiApuKickRegBankPageCount = psApuRegConfig->kickreg_regbank.regbank_size >> PAGE_SHIFT;
	IMG_HANDLE hBuffGpu2Apu, hCtrlGpu2Apu;
	IMG_HANDLE hApuKickReg;
	IMG_HANDLE hBuffApu2Gpu, hCtrlApu2Gpu;
	PVRSRV_ERROR eError;

	if (uiApuKickRegBankPageCount == 0)
	{
		PVR_LOG_GOTO_WITH_ERROR("uiApuKickRegBankPageCount",
		                        eError,
		                        PVRSRV_ERROR_INVALID_PARAMS,
		                        ErrReturn);
	}
	else
	{
		IMG_UINT8 ui8RegSize;
		IMG_UINT64 ui64EndAddr;
		IMG_UINT32 ui32RegOffset = psApuRegConfig->kickreg_details.reg_offset;

		switch (psApuRegConfig->kickreg_details.reg_size)
		{
			case FPF_KICKREG_WRITE_SIZE_32B: ui8RegSize = sizeof(IMG_UINT32); break;
			case FPF_KICKREG_WRITE_SIZE_64B: ui8RegSize = sizeof(IMG_UINT64); break;
			default:
			{
				PVR_LOG_GOTO_WITH_ERROR("Reg size is invalid",
				                        eError,
				                        PVRSRV_ERROR_INVALID_PARAMS,
				                        ErrReturn);
			}
		}

		if (ui32RegOffset != PVR_ALIGN(ui32RegOffset, ui8RegSize))
		{
			PVR_LOG_GOTO_WITH_ERROR("Reg offset not aligned to register size",
			                        eError,
			                        PVRSRV_ERROR_INVALID_PARAMS,
			                        ErrReturn);
		}

		ui64EndAddr = (IMG_UINT64)ui32RegOffset + ui8RegSize;

		/* Validate Regbank offset given */
		if (ui64EndAddr > psApuRegConfig->kickreg_regbank.regbank_size)
		{
			PVR_LOG_GOTO_WITH_ERROR("Reg offset exceeds reg bank range",
			                        eError,
			                        PVRSRV_ERROR_INVALID_PARAMS,
			                        ErrReturn);
		}
	}

	eError = RGXRequestFWGPUMapResource(psDeviceNode,
	                                    &sApu2GpuBuffPAddr,
	                                    psFpfConfig->sApu2GpuConfig.page_shift,
	                                    psFpfConfig->sApu2GpuConfig.buff_pages,
	                                    &hBuffApu2Gpu,
	                                    &psContext->sFPTCBAPU2GPUFWAddr);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXRequestFWGPUMapResource", ErrReturn);

	eError = RGXRequestFWGPUMapResource(psDeviceNode,
	                                    &sApu2GpuCtrlPAddr,
	                                    psFpfConfig->sApu2GpuConfig.page_shift,
	                                    psFpfConfig->sApu2GpuConfig.ctrl_pages,
	                                    &hCtrlApu2Gpu,
	                                    &psContext->sFPTCBAPU2GPUCTRLFWAddr);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXRequestFWGPUMapResource", ErrUnmapApuBuffer);

	eError = RGXRequestFWGPUMapResource(psDeviceNode,
	                                    &sGpu2ApuBuffPAddr,
	                                    psFpfConfig->sGpu2ApuConfig.page_shift,
	                                    psFpfConfig->sGpu2ApuConfig.buff_pages,
	                                    &hBuffGpu2Apu,
	                                    &psContext->sFPTCBGPU2APUFWAddr);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXRequestFWGPUMapResource", ErrUnmapApuCtrl);

	eError = RGXRequestFWGPUMapResource(psDeviceNode,
	                                    &sGpu2ApuCtrlPAddr,
	                                    psFpfConfig->sGpu2ApuConfig.page_shift,
	                                    psFpfConfig->sGpu2ApuConfig.buff_pages,
	                                    &hCtrlGpu2Apu,
	                                    &psContext->sFPTCBGPU2APUCTRLFWAddr);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXRequestFWGPUMapResource", ErrUnmapGpuBuffer);

	eError = RGXRequestFWGPUMapResource(psDeviceNode,
	                                    psApuKickRegBankPAddr,
	                                    PAGE_SHIFT,
	                                    uiApuKickRegBankPageCount,
	                                    &hApuKickReg,
	                                    &psContext->sFWAPUKickRegDetails.sFPFApuKickRegBankMapping);
	PVR_LOG_GOTO_IF_ERROR(eError, "RGXRequestFWGPUMapResource", ErrUnmapGpuCtrl);

	psContext->sFWAPUKickRegDetails.sKickRegDetails.reg_size = psApuRegConfig->kickreg_details.reg_size;
	psContext->sFWAPUKickRegDetails.sKickRegDetails.reg_offset = psApuRegConfig->kickreg_details.reg_offset;
	psContext->sFWAPUKickRegDetails.sKickRegDetails.write_value = psApuRegConfig->kickreg_details.write_value;
	psFpfConfig->hFpfApuKickReg = hApuKickReg;
	psFpfConfig->hFpfGpu2ApuBuff = hBuffGpu2Apu;
	psFpfConfig->hFpfGpu2ApuCtrl = hCtrlGpu2Apu;

	psFpfConfig->hFpfApu2GpuBuff = hBuffApu2Gpu;
	psFpfConfig->hFpfApu2GpuCtrl = hCtrlApu2Gpu;

	return PVRSRV_OK;

ErrUnmapGpuCtrl:
	RGXRequestFWGPUUnmapResource(hCtrlGpu2Apu);
ErrUnmapGpuBuffer:
	RGXRequestFWGPUUnmapResource(hBuffGpu2Apu);
ErrUnmapApuCtrl:
	RGXRequestFWGPUUnmapResource(hCtrlApu2Gpu);
ErrUnmapApuBuffer:
	RGXRequestFWGPUUnmapResource(hBuffApu2Gpu);
ErrReturn:
	return eError;
}

void SysFpfFptCbCommunicationDeInit(PVRSRV_DEVICE_CONFIG *psDeviceConfig)
{
	RGX_FPF_KICK_COMMS_CONFIG *psFpfConfig = psDeviceConfig->psFpfConfigPrivData;

	if (psFpfConfig->hFpfApu2GpuBuff != NULL)
	{
		RGXRequestFWGPUUnmapResource(psFpfConfig->hFpfApu2GpuBuff);
	}
	if (psFpfConfig->hFpfApu2GpuCtrl != NULL)
	{
		RGXRequestFWGPUUnmapResource(psFpfConfig->hFpfApu2GpuCtrl);
	}

	if (psFpfConfig->hFpfGpu2ApuBuff != NULL)
	{
		RGXRequestFWGPUUnmapResource(psFpfConfig->hFpfGpu2ApuBuff);
	}
	if (psFpfConfig->hFpfGpu2ApuCtrl != NULL)
	{
		RGXRequestFWGPUUnmapResource(psFpfConfig->hFpfGpu2ApuCtrl);
	}
	if (psFpfConfig->hFpfApuKickReg != NULL)
	{
		RGXRequestFWGPUUnmapResource(psFpfConfig->hFpfApuKickReg);
	}

	psFpfConfig->hFpfApuKickReg = NULL;
	psFpfConfig->hFpfGpu2ApuBuff = NULL;
	psFpfConfig->hFpfGpu2ApuCtrl = NULL;

	psFpfConfig->hFpfApu2GpuBuff = NULL;
	psFpfConfig->hFpfApu2GpuCtrl = NULL;
}
