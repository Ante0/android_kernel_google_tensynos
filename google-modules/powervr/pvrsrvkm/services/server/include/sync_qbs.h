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

#ifndef SYNC_QBS_H
#define SYNC_QBS_H

#include "img_types.h"
#include "pvrsrv_error.h"
#include "opaque_types.h"

#define PVRSRV_QBS_NAME_LENGTH 64U

typedef struct _SYNC_QBS_CONTEXT_TAG_ SYNC_QBS_CONTEXT;
typedef struct _SYNC_QBS_TAG_ SYNC_QBS;

#if !defined(PDUMP) && !defined(NO_HARDWARE)
#define SYNC_QBS_ENABLED
#endif

#if defined(SYNC_QBS_ENABLED)
PVRSRV_ERROR
QBSContextCreate(PPVRSRV_DEVICE_NODE psDevNode,
                 SYNC_QBS_CONTEXT **ppsQBSContext);

PVRSRV_ERROR QBSContextDestroy(SYNC_QBS_CONTEXT *psQBSContext);

PVRSRV_ERROR
QBSAlloc(SYNC_QBS_CONTEXT *psQBSContext,
         const IMG_CHAR *pszQBSName,
         SYNC_QBS **ppsQBS);

void
QBSDestroy(SYNC_QBS *psQBS);

IMG_UINT32
QBSGetFirmwareAddrEnqueue(SYNC_QBS *psQBS);

void
QBSCCBRollback(SYNC_QBS *psQBS);

IMG_UINT32
QBSGetEnqueuedCount(SYNC_QBS *psQBS);

IMG_UINT32
QBSGetFWAckCount(SYNC_QBS *psQBS);
#else
static INLINE PVRSRV_ERROR
QBSContextCreate(PPVRSRV_DEVICE_NODE psDevNode,
                 SYNC_QBS_CONTEXT **ppsQBSContext)
{
	PVR_UNREFERENCED_PARAMETER(psDevNode);
	PVR_UNREFERENCED_PARAMETER(ppsQBSContext);

	return PVRSRV_OK;
}

static INLINE PVRSRV_ERROR QBSContextDestroy(SYNC_QBS_CONTEXT *psQBSContext)
{
	PVR_UNREFERENCED_PARAMETER(psQBSContext);


	return PVRSRV_OK;
}

static INLINE PVRSRV_ERROR
QBSAlloc(SYNC_QBS_CONTEXT *psQBSContext,
         const IMG_CHAR *pszQBSName,
         SYNC_QBS **ppsQBS)
{
	PVR_UNREFERENCED_PARAMETER(psQBSContext);
	PVR_UNREFERENCED_PARAMETER(pszQBSName);
	PVR_UNREFERENCED_PARAMETER(ppsQBS);


	return PVRSRV_OK;
}

static INLINE void
QBSDestroy(SYNC_QBS *psQBS)
{
	PVR_UNREFERENCED_PARAMETER(psQBS);
}

static INLINE IMG_UINT32
QBSGetFirmwareAddrEnqueue(SYNC_QBS *psQBS)
{
	PVR_UNREFERENCED_PARAMETER(psQBS);

	return 0;
}

static INLINE void
QBSCCBRollback(SYNC_QBS *psQBS)
{
	PVR_UNREFERENCED_PARAMETER(psQBS);
}

static INLINE IMG_UINT32
QBSGetEnqueuedCount(SYNC_QBS *psQBS)
{
	PVR_UNREFERENCED_PARAMETER(psQBS);

	return 0;
}

static INLINE IMG_UINT32
QBSGetFWAckCount(SYNC_QBS *psQBS)
{
	PVR_UNREFERENCED_PARAMETER(psQBS);

	return 0;
}
#endif

#endif /* SYNC_QBS_H */
