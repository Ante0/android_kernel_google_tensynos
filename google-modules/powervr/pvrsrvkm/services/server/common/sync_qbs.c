/**************************************************************************/ /*!
@File
@Title          Queue and Block Synchroniser
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provides work queue sync block reference tracking facilities
                for the KMD.
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
*/ /***************************************************************************/

#include "sync_qbs.h"

#if defined(SYNC_QBS_ENABLED)

#include "devicemem_typedefs.h"
#include "img_defs.h"
#include "lock_types.h"
#include "log2.h"
#include "rgx_fwif_shared.h"
#include "allocmem.h"
#include "device_connection.h"
#include "devicemem.h"
#include "lock.h"
#include "ra.h"
#include "device.h"
#include "osfunc.h"

#define QBS_BLOCK_SIZE 1024

struct _SYNC_QBS_CONTEXT_TAG_
{
	SHARED_DEV_CONNECTION psDeviceNode;
	RA_ARENA              *psSubAllocRA;               /*!< RA context */
	ATOMIC_T              hCheckpointCount;            /*!< Checkpoint count for this context */
};

typedef struct _SYNC_QBS_BLOCK_
{
	SYNC_QBS_CONTEXT          *psContext;       /*!< Context on which block was created */
	IMG_UINT32                ui32QBSBlockSize; /*!< Size of the QBS checkpoint block */
	IMG_UINT32                ui32FirmwareAddr; /*!< Firmware address */
	DEVMEM_MEMDESC            *hMemDesc;        /*!< DevMem allocation for block */
	volatile IMG_UINT32       *pui32LinAddr;    /*!< Server-code CPU mapping */
} SYNC_QBS_BLOCK;

struct _SYNC_QBS_TAG_
{
	ATOMIC_T                        hEnqueuedCCBCount;              /*!< Num times QBS has been put in CCBs */
	SYNC_QBS_BLOCK                  *psQBSBlock;                    /*!< QBS block this checkpoint is allocated on */
	RA_BASE_T                       uiAllocatedAddr;                /*!< Allocated RA Base address of the QBS, in effect the FW Addr */
	volatile SYNC_CHECKPOINT_FW_OBJ *psCheckpointFwObj;             /*!< CPU view of the data held in the QBS block */
	IMG_CHAR                        azName[PVRSRV_QBS_NAME_LENGTH]; /*!< Name of the checkpoint */
	IMG_UINT32                      ui32FWAddr;                     /*!< FWAddr stored at QBS alloc time */
};

static PVRSRV_ERROR
_AllocQBSBlock(SYNC_QBS_CONTEXT *psContext,
               SYNC_QBS_BLOCK   **ppsQBSBlock)
{
	PVRSRV_DEVICE_NODE *psDevNode;
	SYNC_QBS_BLOCK *psQBSBlock;
	PVRSRV_ERROR eError;

	psQBSBlock = OSAllocMem(sizeof(*psQBSBlock));
	PVR_LOG_GOTO_IF_NOMEM(psQBSBlock, eError, fail_exit);

	psQBSBlock->psContext = psContext;

	psDevNode = psContext->psDeviceNode;
	PVR_LOG_GOTO_IF_INVALID_PARAM(psDevNode, eError, fail_free_block_mem);

	/* Allocate sync checkpoint block */
	eError = psDevNode->pfnAllocUFOBlock(psDevNode,
	                                     QBS_BLOCK_SIZE * sizeof(SYNC_CHECKPOINT_FW_OBJ),
	                                     &psQBSBlock->hMemDesc,
	                                     &psQBSBlock->ui32FirmwareAddr,
	                                     &psQBSBlock->ui32QBSBlockSize);
	PVR_LOG_GOTO_IF_ERROR(eError, "pfnAllocUFOBlock", fail_free_block_mem);

	eError = DevmemAcquireCpuVirtAddr(psQBSBlock->hMemDesc,
	                                  (void **) &psQBSBlock->pui32LinAddr);
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemAcquireCpuVirtAddr", fail_free_ufo_block);


	*ppsQBSBlock = psQBSBlock;
	return PVRSRV_OK;

fail_free_ufo_block:
	psDevNode->pfnFreeUFOBlock(psDevNode, psQBSBlock->hMemDesc);
fail_free_block_mem:
	OSFreeMem(psQBSBlock);
fail_exit:
	return eError;
}

static INLINE void _FreeQBSBlock(SYNC_QBS_BLOCK *psQBSBlk)
{
	PVRSRV_DEVICE_NODE *psDevNode = psQBSBlk->psContext->psDeviceNode;

	DevmemReleaseCpuVirtAddr(psQBSBlk->hMemDesc);
	psDevNode->pfnFreeUFOBlock(psDevNode, psQBSBlk->hMemDesc);
	OSFreeMem(psQBSBlk);
}


static PVRSRV_ERROR
_QBSBlockImport(RA_PERARENA_HANDLE hArena,
                RA_LENGTH_T uSize,
                RA_FLAGS_T uFlags,
                RA_LENGTH_T uBaseAlignment,
                const IMG_CHAR *pszAnnotation,
                RA_IMPORT *psImport)
{
	SYNC_QBS_CONTEXT *psContext = hArena;
	SYNC_QBS_BLOCK *psQBSBlock = NULL;
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(uFlags);
	PVR_UNREFERENCED_PARAMETER(uBaseAlignment);

	PVR_LOG_RETURN_IF_INVALID_PARAM((hArena != NULL), "hArena");

	/* Check we've not be called with an unexpected size */
	PVR_LOG_RETURN_IF_INVALID_PARAM((uSize == sizeof(SYNC_CHECKPOINT_FW_OBJ)), "uSize");

	/* Allocate the block of memory */
	eError = _AllocQBSBlock(psContext, &psQBSBlock);
	PVR_GOTO_IF_ERROR(eError, fail_exit);

	psImport->base = psQBSBlock->ui32FirmwareAddr;
	psImport->uSize = psQBSBlock->ui32QBSBlockSize;
	psImport->hPriv = psQBSBlock;

	return PVRSRV_OK;

fail_exit:
	return eError;
}

static void
_QBSBlockUnimport(RA_PERARENA_HANDLE hArena,
                  RA_BASE_T uiBase,
                  RA_PERISPAN_HANDLE hImport)
{
	SYNC_QBS_CONTEXT *psContext = hArena;
	SYNC_QBS_BLOCK   *psQBSBlock = hImport;

	PVR_LOG_RETURN_VOID_IF_FALSE((psContext != NULL), "hArena invalid");
	PVR_LOG_RETURN_VOID_IF_FALSE((psQBSBlock != NULL), "hImport invalid");
	PVR_LOG_RETURN_VOID_IF_FALSE((uiBase == psQBSBlock->ui32FirmwareAddr), "uiBase invalid");

	_FreeQBSBlock(psQBSBlock);
}

PVRSRV_ERROR
QBSContextCreate(PPVRSRV_DEVICE_NODE psDevNode,
                 SYNC_QBS_CONTEXT **ppsQBSContext)
{
	SYNC_QBS_CONTEXT *psContext = NULL;
	IMG_CHAR azTempName[PVRSRV_QBS_NAME_LENGTH] = {0};
	PVRSRV_ERROR eError;

	PVR_LOG_RETURN_IF_FALSE((ppsQBSContext != NULL),
	                  "ppsQBSContext invalid",
	                  PVRSRV_ERROR_INVALID_PARAMS);

	psContext = OSAllocMem(sizeof(*psContext));
	PVR_LOG_GOTO_IF_NOMEM(psContext, eError, fail_alloc); /* Sets OOM error code */

	psContext->psDeviceNode = (SHARED_DEV_CONNECTION)psDevNode;
	OSAtomicWrite(&psContext->hCheckpointCount, 0);

	OSSNPrintf(azTempName, PVRSRV_QBS_NAME_LENGTH,"QBS Checkpoint RA-%p", psContext);
	psContext->psSubAllocRA = RA_Create(azTempName,
	                                    /* Params for imports */
	                                    ExactLog2(sizeof(IMG_UINT32)),
	                                    RA_LOCKCLASS_SYNC_QBS,
	                                    _QBSBlockImport,
	                                    _QBSBlockUnimport,
	                                    psContext,
	                                    RA_POLICY_DEFAULT);
	PVR_LOG_GOTO_IF_NOMEM(psContext->psSubAllocRA, eError, fail_ra_alloc);

	*ppsQBSContext = psContext;

	return PVRSRV_OK;

fail_ra_alloc:
	OSFreeMem(psContext);
fail_alloc:
	return eError;
}

PVRSRV_ERROR QBSContextDestroy(SYNC_QBS_CONTEXT *psQBSContext)
{
	IMG_INT iRf = 0;

	PVR_LOG_RETURN_IF_FALSE((psQBSContext != NULL),
	                  "psQBSContext invalid",
	                  PVRSRV_ERROR_INVALID_PARAMS);

	iRf = OSAtomicRead(&psQBSContext->hCheckpointCount);
	if (iRf != 0)
	{
		return PVRSRV_ERROR_NOT_READY;
	}

	RA_Delete(psQBSContext->psSubAllocRA);

	OSFreeMem(psQBSContext);

	return PVRSRV_OK;
}

static INLINE IMG_UINT32 _QBSCheckpointGetOffset(SYNC_QBS *psQBS)
{
	IMG_UINT64 ui64Temp;

	ui64Temp = (IMG_UINT64)psQBS->uiAllocatedAddr -
	           (IMG_UINT64)psQBS->psQBSBlock->ui32FirmwareAddr;
	PVR_ASSERT(ui64Temp<IMG_UINT32_MAX);
	return (IMG_UINT32)ui64Temp;
}

PVRSRV_ERROR
QBSAlloc(SYNC_QBS_CONTEXT *psQBSContext,
         const IMG_CHAR *pszQBSName,
         SYNC_QBS **ppsQBS)
{
	SYNC_QBS *psNewQBS = NULL;
	PVRSRV_ERROR eError;

	PVR_LOG_RETURN_IF_FALSE((psQBSContext != NULL), "psQBSContext invalid", PVRSRV_ERROR_INVALID_PARAMS);
	PVR_LOG_RETURN_IF_FALSE((ppsQBS != NULL), "ppsQBS invalid", PVRSRV_ERROR_INVALID_PARAMS);

	/* Allocate sync checkpoint */
	psNewQBS = OSAllocZMem(sizeof(*psNewQBS));
	PVR_LOG_RETURN_IF_NOMEM(psNewQBS, "OSAllocZMem"); /* Sets OOM error code */

	eError = RA_Alloc(psQBSContext->psSubAllocRA,
	                  sizeof(*psNewQBS->psCheckpointFwObj),
	                  RA_NO_IMPORT_MULTIPLIER,
	                  0,
	                  sizeof(IMG_UINT32),
	                  NULL,
	                  &psNewQBS->uiAllocatedAddr,
	                  NULL,
	                  (RA_PERISPAN_HANDLE *) &psNewQBS->psQBSBlock);
	PVR_LOG_GOTO_IF_ERROR(eError, "RA_Alloc", fail_free_alloc);

	psNewQBS->psCheckpointFwObj =
	        (volatile SYNC_CHECKPOINT_FW_OBJ*)(void *)(psNewQBS->psQBSBlock->pui32LinAddr +
	                (_QBSCheckpointGetOffset(psNewQBS)/sizeof(IMG_UINT32)));

	psNewQBS->ui32FWAddr = TRUNCATE_64BITS_TO_32BITS(psNewQBS->uiAllocatedAddr);

	PVRSRV_UFO_TYPE_SET(psNewQBS->ui32FWAddr, PVRSRV_UFO_TYPE_SYNC_CP);

	OSAtomicIncrement(&psNewQBS->psQBSBlock->psContext->hCheckpointCount);

	OSAtomicWrite(&psNewQBS->hEnqueuedCCBCount, 0);
	psNewQBS->psCheckpointFwObj->ui32FwRefCount = 0;
	/* We don't care about state for this type of checkpoint.
	 * It will be set to signalled with each update command anyway.
	 * This can be put into both fence and update commands so needs to be signalled
	 * by default.
	 */
	psNewQBS->psCheckpointFwObj->ui32State = PVRSRV_SYNC_CHECKPOINT_SIGNALLED;
	psNewQBS->psCheckpointFwObj->ui32UserData = SYNC_CHECKPOINT_FW_UD_UNDEF;

	*ppsQBS = psNewQBS;

	return PVRSRV_OK;

fail_free_alloc:
	OSFreeMem(psNewQBS);
	return eError;
}

void
QBSDestroy(SYNC_QBS *psQBS)
{
	SYNC_QBS_CONTEXT *psContext = psQBS->psQBSBlock->psContext;

	psQBS->psCheckpointFwObj = NULL;

	RA_Free(psQBS->psQBSBlock->psContext->psSubAllocRA,
	        psQBS->uiAllocatedAddr);
	psQBS->psQBSBlock = NULL;

	OSAtomicDecrement(&psContext->hCheckpointCount);

	OSFreeMem(psQBS);
}

IMG_UINT32
QBSGetFirmwareAddrEnqueue(SYNC_QBS *psQBS)
{
	PVR_LOG_RETURN_IF_FALSE((psQBS != NULL), "psQBS", 0);

	OSAtomicIncrement(&psQBS->hEnqueuedCCBCount);

	return psQBS->ui32FWAddr;
}

void
QBSCCBRollback(SYNC_QBS *psQBS)
{
	OSAtomicDecrement(&psQBS->hEnqueuedCCBCount);
}

IMG_UINT32
QBSGetEnqueuedCount(SYNC_QBS *psQBS)
{
	PVR_LOG_RETURN_IF_FALSE(psQBS != NULL, "psQBS", 0);

	return (IMG_UINT32) OSAtomicRead(&psQBS->hEnqueuedCCBCount);
}

IMG_UINT32
QBSGetFWAckCount(SYNC_QBS *psQBS)
{
	PVR_LOG_RETURN_IF_FALSE(psQBS != NULL, "psQBS", 0);

	return psQBS->psCheckpointFwObj->ui32FwRefCount;
}

#endif /* defined(SYNC_QBS_ENABLED) */
