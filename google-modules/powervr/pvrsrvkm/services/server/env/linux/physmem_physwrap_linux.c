/*************************************************************************/ /*!
@File
@Title          Implementation of PMR functions to wrap non-services allocated
                physical memory.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Part of the memory management.  This module is responsible for
                implementing the function callbacks for physical memory.
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

#include <linux/mm_types.h>
#include <linux/uaccess.h>
#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_error.h"
#include "pvr_debug.h"
#include "devicemem_server_utils.h"

#include "physmem_physwrap.h"
#include "pmr.h"
#include "pmr_impl.h"
#include "physmem.h"
#include "physheap.h"

#include "kernel_compatibility.h"

typedef struct _PMR_PHYSWRAP_DATA_
{
	/* Device for which this allocation has been made */
	PVRSRV_DEVICE_NODE *psDevNode;

	/* Total Number of pages in the allocation */
	IMG_UINT32 uiTotalNumPages;

	/* Log2 page size */
	IMG_UINT32 uiLog2PageSize;

	/* This should always be filled and hold the physical addresses */
	IMG_DEV_PHYADDR *ppvPhysAddr;

} PMR_PHYSWRAP_DATA;


/* Free the PMR private data */
static void _FreeWrapData(PMR_PHYSWRAP_DATA *psPrivData)
{
	OSFreeMem(psPrivData->ppvPhysAddr);
	OSFreeMem(psPrivData);
}

/* Allocate the PMR private data */
static PVRSRV_ERROR _AllocWrapData(PMR_PHYSWRAP_DATA **ppsPrivData,
                                   PVRSRV_DEVICE_NODE *psDevNode,
                                   IMG_DEVMEM_SIZE_T uiSize,
                                   IMG_UINT32 uiLog2PageSize,
                                   PVRSRV_MEMALLOCFLAGS_T uiFlags)
{
	PVRSRV_ERROR eError;
	PMR_PHYSWRAP_DATA *psPrivData;

	/* Allocate and initialise private factory data */
	psPrivData = OSAllocZMem(sizeof(*psPrivData));
	if (psPrivData == NULL)
	{
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto eReturn;
	}

	psPrivData->psDevNode = psDevNode;
	psPrivData->uiLog2PageSize = uiLog2PageSize;
	psPrivData->uiTotalNumPages = uiSize >> uiLog2PageSize;

	psPrivData->ppvPhysAddr = OSAllocZMem(sizeof(*(psPrivData->ppvPhysAddr)) * psPrivData->uiTotalNumPages);
	if (psPrivData->ppvPhysAddr == NULL)
	{
		OSFreeMem(psPrivData);
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto eReturn;
	}

	*ppsPrivData = psPrivData;

	eError = PVRSRV_OK;

eReturn:
	return eError;
}

static void _WrapPhysAddrs(PMR_PHYSWRAP_DATA *psPrivData,
                           PHYS_HEAP *psPhysHeap,
                           IMG_CPU_PHYADDR *pasCPUPhysAddrs)
{
	/* Translate between CPU phys addrs and DEV phys addrs
	 * that can be mapped by the GPU.
	 */

#if defined(PVR_PMR_TRANSLATE_UMA_ADDRESSES)
	IMG_UINT32 i;

	/* Don't translate here since they will be translated at the PMR level before mapping */
	for (i = 0; i < psPrivData->uiTotalNumPages; i++)
	{
		psPrivData->ppvPhysAddr[i].uiAddr = pasCPUPhysAddrs[i].uiAddr;
	}
#else
	PhysHeapCpuPAddrToDevPAddr(psPhysHeap,
	                           psPrivData->uiTotalNumPages,
	                           psPrivData->ppvPhysAddr,
	                           pasCPUPhysAddrs);
#endif
}

static PVRSRV_ERROR
PMRDevPhysAddrPhysWrapMem(PMR_IMPL_PRIVDATA pvPriv,
                          IMG_UINT32 ui32Log2PageSize,
                          IMG_UINT32 ui32NumOfPages,
                          IMG_DEVMEM_OFFSET_T *puiOffset,
#if defined(SUPPORT_STATIC_IPA)
                          IMG_UINT64 ui64IPAPolicyValue,
                          IMG_UINT64 ui64IPAClearMask,
#endif
                          IMG_BOOL *pbValid,
                          IMG_DEV_PHYADDR *psDevPAddr)
{
	const PMR_PHYSWRAP_DATA *psWrapData = pvPriv;
	IMG_UINT32 uiPageSize = 1U << PAGE_SHIFT;
	IMG_UINT32 uiInPageOffset;
	IMG_UINT32 uiPageIndex;
	IMG_UINT32 uiIdx;

#if defined(SUPPORT_STATIC_IPA)
	PVR_UNREFERENCED_PARAMETER(ui64IPAPolicyValue);
	PVR_UNREFERENCED_PARAMETER(ui64IPAClearMask);
#endif


	if (psWrapData->uiLog2PageSize != ui32Log2PageSize)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Requested ui32Log2PageSize %u is different from "
				"Wrapped page size %u. Not supported.",
				__func__,
				ui32Log2PageSize,
				psWrapData->uiLog2PageSize));
		return PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
	}

	for (uiIdx=0; uiIdx < ui32NumOfPages; uiIdx++)
	{
		uiPageIndex = puiOffset[uiIdx] >> PAGE_SHIFT;
		uiInPageOffset = puiOffset[uiIdx] - ((IMG_DEVMEM_OFFSET_T)uiPageIndex << PAGE_SHIFT);

		PVR_LOG_RETURN_IF_FALSE(uiPageIndex < psWrapData->uiTotalNumPages,
		                        "puiOffset out of range", PVRSRV_ERROR_OUT_OF_RANGE);

		PVR_ASSERT(uiInPageOffset < uiPageSize);

		psDevPAddr[uiIdx].uiAddr = psWrapData->ppvPhysAddr[uiPageIndex].uiAddr;
		pbValid[uiIdx] = IMG_TRUE;

		psDevPAddr[uiIdx].uiAddr += uiInPageOffset;
#if defined(SUPPORT_STATIC_IPA)
		psDevPAddr[uiIdx].uiAddr &= ~ui64IPAClearMask;
		psDevPAddr[uiIdx].uiAddr |= ui64IPAPolicyValue;
#endif	/* SUPPORT_STATIC_IPA */
	}

	return PVRSRV_OK;
}

static void
PMRFinalizePhysWrapMem(PMR_IMPL_PRIVDATA pvPriv)
{
	PMR_PHYSWRAP_DATA *psWrapData = pvPriv;

	_FreeWrapData(psWrapData);
}

static const PMR_IMPL_FUNCTAB _sPMRWrapPFuncTab = {
    .pfnDevPhysAddr = &PMRDevPhysAddrPhysWrapMem,
    .pfnFinalize = &PMRFinalizePhysWrapMem,
};

static inline PVRSRV_ERROR PhysmemValidateParam(IMG_CPU_PHYADDR *psPhysAddrs,
                                                PHYS_HEAP *psPhysHeap,
                                                IMG_DEVMEM_SIZE_T uiSize,
                                                IMG_UINT32 uiLog2PageSize,
                                                PVRSRV_MEMALLOCFLAGS_T uiFlags)
{
	IMG_UINT32 uiNumPages = uiSize >> uiLog2PageSize;
	IMG_UINT32 i;

	PVR_LOG_RETURN_IF_INVALID_PARAM(uiSize != 0, "uiSize");

	if (uiLog2PageSize != PhysHeapGetPageShift(psPhysHeap))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Incompatible log2pagesize, actual %u, expected %u.",
				__func__,
				uiLog2PageSize,
				PhysHeapGetPageShift(psPhysHeap)));
		return PVRSRV_ERROR_PMR_INCOMPATIBLE_CONTIGUITY;
	}

	if (uiSize > PMR_MAX_SUPPORTED_SIZE)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Requested size too large (max supported ""size 0x%llx Bytes).",
				__func__,
				PMR_MAX_SUPPORTED_SIZE));
		return PVRSRV_ERROR_PMR_TOO_LARGE;
	}

	if (uiSize & (uiLog2PageSize - 1))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Given size %llu is not multiple of given page size (%u)",
				__func__,
				uiSize,
				uiLog2PageSize));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	for (i = 0; i < uiNumPages; i++)
	{
		if (psPhysAddrs[i].uiAddr & (uiLog2PageSize - 1))
		{
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Given address 0x%llx is not aligned to given page size (%u)",
					__func__,
					(unsigned long long)psPhysAddrs[i].uiAddr,
					uiLog2PageSize));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}

	/* Fail if requesting coherency on one side but uncached on the other */
	if ((PVRSRV_CHECK_CPU_CACHE_COHERENT(uiFlags) &&
			(PVRSRV_CHECK_GPU_UNCACHED(uiFlags) || PVRSRV_CHECK_GPU_WRITE_COMBINE(uiFlags))))
	{
		PVR_DPF((PVR_DBG_ERROR, "Request for CPU coherency but specifying GPU uncached "
				"Please use GPU cached flags for coherency."));
		return PVRSRV_ERROR_UNSUPPORTED_CACHE_MODE;
	}

	if ((PVRSRV_CHECK_GPU_CACHE_COHERENT(uiFlags) &&
			(PVRSRV_CHECK_CPU_UNCACHED(uiFlags) || PVRSRV_CHECK_CPU_WRITE_COMBINE(uiFlags))))
	{
		PVR_DPF((PVR_DBG_ERROR, "Request for GPU coherency but specifying CPU uncached "
				"Please use CPU cached flags for coherency."));
		return PVRSRV_ERROR_UNSUPPORTED_CACHE_MODE;
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR
PhysmemPhysWrapMem(PVRSRV_DEVICE_NODE *psDevNode,
                   IMG_CPU_PHYADDR *pasPhysAddrs,
                   IMG_UINT32 uiLog2PageSize,
                   IMG_DEVMEM_SIZE_T uiSize,
                   PVRSRV_MEMALLOCFLAGS_T uiFlags,
                   PMR **ppsPMRPtr)
{
	PVRSRV_ERROR eError;
	IMG_UINT32	ui32MappingTable = 0;
	PMR_PHYSWRAP_DATA *psPrivData;
	PMR *psPMR;
	PHYS_HEAP *psPhysHeap = psDevNode->apsPhysHeap[PVRSRV_PHYS_HEAP_CPU_LOCAL];

	eError = PhysmemValidateParam(pasPhysAddrs,
	                              psPhysHeap,
	                              uiSize,
	                              uiLog2PageSize,
	                              uiFlags);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	/* Allocate private factory data */
	eError = _AllocWrapData(&psPrivData,
	                        psDevNode,
	                        uiSize,
                            uiLog2PageSize,
	                        uiFlags);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	_WrapPhysAddrs(psPrivData,
	               psPhysHeap,
	               pasPhysAddrs);

	/* Create a suitable PMR */
	eError = PMRCreatePMR(psPhysHeap,
	                      uiSize,    /* PMR_SIZE_T uiLogicalSize                             */
	                      1,    /* IMG_UINT32 ui32NumPhysChunks                */
	                      1,    /* IMG_UINT32 ui32NumLogicalChunks          */
	                      &ui32MappingTable,
	                      uiLog2PageSize,           /* PMR_LOG2ALIGN_T uiLog2ContiguityGuarantee */
	                      (uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK), /* PMR_FLAGS_T uiFlags */
	                      "PhysWrapMem",      /* const IMG_CHAR *pszAnnotation             */
	                      &_sPMRWrapPFuncTab,   /* const PMR_IMPL_FUNCTAB *psFuncTab         */
	                      psPrivData,           /* PMR_IMPL_PRIVDATA pvPrivData              */
	                      PMR_TYPE_EXTMEM,
	                      &psPMR,               /* PMR **ppsPMRPtr                           */
	                      PDUMP_NONE);          /* IMG_UINT32 ui32PDumpFlags                 */
	if (eError != PVRSRV_OK)
	{
		goto e0;
	}

	/* Mark the PMR such that no layout changes can happen.
	 * The memory is allocated in the CPU domain and hence
	 * no changes can be made through any of our API */
	PMR_SetLayoutFixed(psPMR, IMG_TRUE);

	*ppsPMRPtr = psPMR;

	return PVRSRV_OK;

e0:
	(void)_FreeWrapData(psPrivData);
	return eError;
}
