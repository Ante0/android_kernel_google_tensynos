/*************************************************************************/ /*!
@File           rgx_fwif_fpf.h
@Title          RGX firmware fast path fence
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX fast path fence implementation
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

#if !defined(RGX_FWIF_FPF_H)
#define RGX_FWIF_FPF_H

#include "img_types.h"
#include "rgx_common.h"

#if defined(SUPPORT_FASTPATH_FENCE_CUSTOM_COMMS)
#include "sys_fwif_fpf_custom.h"
#else
#include "sys_fwif_fpf_fpt_cb.h"
#endif

#define FPF_CHECKPOINT_BLOCK_COUNT 64
#define FPF_CHECKPOINT_BLOCK_SIZE  1024

#define FPF_HASH_INDEX_BIT_SHIFT (10)
#define FPF_HASH_INDEX_BIT_MASK (0xFC00)
#define FPF_CHECKPOINT_FW_OBJ_OFFSET_BIT_SHIFT (0)
#define FPF_CHECKPOINT_FW_OBJ_OFFSET_BIT_MASK (0x03FF)

#define FPF_EXTRACT_INDEX_FROM_TOKEN(uiToken) \
	((uiToken & FPF_HASH_INDEX_BIT_MASK) >> FPF_HASH_INDEX_BIT_SHIFT)
#define FPF_EXTRACT_OFFSET_FROM_TOKEN(uiToken) \
	((uiToken & FPF_CHECKPOINT_FW_OBJ_OFFSET_BIT_MASK) >> FPF_CHECKPOINT_FW_OBJ_OFFSET_BIT_SHIFT)

typedef IMG_UINT16 PVRSRV_FAST_PATH_FENCE_TOKEN;

typedef struct
{
	RGXFWIF_DEV_VIRTADDR auiBlockAddrs[FPF_CHECKPOINT_BLOCK_COUNT];
} RGXFWIF_FPF_LOOKUP_HASH;

typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_FPF_KICK_COMMS_CONTEXT;
typedef RGXFWIF_DEV_VIRTADDR  PRGXFWIF_FPF_LOOKUP_HASH;

typedef enum
{
	RGXFWIF_FPF_GPU_KICKREG_TYPE_MTS, /* GPU will be kicked via MTS */
	RGXFWIF_FPF_GPU_KICKREG_TYPE_CUSTOM /* GPU will be kicked via custom method */
} RGXFWIF_FPF_GPU_KICKREG_TYPE;

typedef struct
{
	RGXFWIF_FPF_GPU_KICKREG_TYPE eGPUKickType;
	PRGXFWIF_FPF_KICK_COMMS_CONTEXT sKickAndCommCtx;
	PRGXFWIF_FPF_LOOKUP_HASH sLookupHash;
	RGXFWIF_DEV_VIRTADDR sLookupHashMMUSync;
} RGXFWIF_FPF_DATA;

#endif /* RGX_FWIF_FPF_H */
