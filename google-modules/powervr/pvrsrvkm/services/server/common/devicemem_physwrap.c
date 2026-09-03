/*************************************************************************/ /*!
@File           devicemem_physwrap.c
@Title          Common server-side function interface for physwrap
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Additional Memory Physical Wrapping Functionality internal API.
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

#include "pvrsrv_devmem.h"
#include "devicemem_utils.h"
#include "devicemem_physwrap.h"
#include "physmem_physwrap.h"

IMG_INTERNAL PVRSRV_ERROR
DevmemPhysWrapMem(PVRSRV_DEVICE_NODE *psDevNode,
                  IMG_CPU_PHYADDR *psPhysAddrs,
                  IMG_UINT32 uiLog2PageSize,
                  IMG_DEVMEM_SIZE_T uiSize,
                  IMG_DEVMEM_ALIGN_T uiAlign,
                  PVRSRV_MEMALLOCFLAGS_T uiFlags,
                  const IMG_CHAR *pszText,
                  DEVMEM_MEMDESC **ppsMemDescPtr)
{
	PVRSRV_ERROR eError;
	PVRSRV_MEMALLOCFLAGS_T uiPMRFlags = uiFlags & PVRSRV_MEMALLOCFLAGS_PMRFLAGSMASK;
	DEVMEM_PROPERTIES_T uiDevMemProperties = DEVMEM_PROPERTIES_EXPORTABLE |
	    DEVMEM_PROPERTIES_NO_LAYOUT_CHANGE;
	IMG_UINT32 uiNumPages;

	PMR *psWrappedPMR;
	DEVMEM_IMPORT *psImport;
	DEVMEM_MEMDESC *psMemDesc;

	/* Use of cast below is justified by the assertion that follows to
	prove that no significant bits have been truncated */
	uiNumPages = (IMG_UINT32)(((uiSize - 1) >> uiLog2PageSize) + 1);
	PVR_LOG_IF_FALSE(((PMR_SIZE_T)uiNumPages << uiLog2PageSize) == uiSize, "Size not multiple of log2 page size.");

	eError = DevmemImportStructAlloc(psDevNode,
	                                  &psImport);
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemImportStructAlloc", failAlloc);

	eError = DevmemMemDescAlloc(&psMemDesc);
	PVR_LOG_GOTO_IF_ERROR(eError, "DevmemMemDescAlloc", failMemDescAlloc);

	eError = PhysmemPhysWrapMem(psDevNode,
	                            psPhysAddrs,
	                            uiLog2PageSize,
	                            uiSize,
	                            uiPMRFlags,
	                            &psWrappedPMR);
	PVR_LOG_GOTO_IF_ERROR(eError, "PhysmemPhysWrapMem", failPhysWrapMem);


	DevmemImportStructInit(psImport,
	                        uiSize,
	                        uiAlign,
	                        uiFlags,
	                        psWrappedPMR,
	                        uiDevMemProperties);

	DevmemMemDescInit(psMemDesc,
	                   0,
	                   psImport,
	                   uiSize);

	/* copy the allocation descriptive name so it can be passed to
	 * DevicememHistory when the allocation gets mapped/unmapped
	 */
	 if (pszText)
	 {
		OSStringSafeCopy(psMemDesc->szText,
		              pszText,
		              sizeof(psMemDesc->szText));
	 }

	*ppsMemDescPtr = psMemDesc;

	return PVRSRV_OK;

failPhysWrapMem:
	DevmemMemDescDiscard(psMemDesc);
failMemDescAlloc:
	DevmemImportDiscard(psImport);
failAlloc:
	return eError;
}
