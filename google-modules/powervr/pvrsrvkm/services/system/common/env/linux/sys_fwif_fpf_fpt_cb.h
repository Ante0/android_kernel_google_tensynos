/*************************************************************************/ /*!
@File           sys_fwif_fpf_fpt_cb.h
@Title          RGX firmware fast path fence FPT-CB system/fw interface file
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX fast path fence system/fw  interface implementation for FPT-CB
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

#ifndef SYS_FWIF_FPF_FPT_CB_H
#define SYS_FWIF_FPF_FPT_CB_H

#include "img_types.h"
#include "rgx_common.h"
#include "sys_fwif_fpf_common.h"
#include "sys_fpf_kickreg.h"

/* Circular buffer control structure. */
typedef struct _RGX_FPF_FPTCB_CTRL_
{
	volatile IMG_UINT32 ui32WriteOffset; /*!< write offset into array of Tokens */
	volatile IMG_UINT32 ui32ReadOffset;  /*!< read offset into array of Tokens */
	IMG_UINT32 ui32WrapMask;             /*!< Offset wrapping mask (Total capacity of the CB - 1), total capacity should be pow2 */
} RGX_FPF_FPTCB_CTRL;

typedef enum fpf_kickreg_write_size RGX_FPF_APU_KICK_REG_SIZE;
typedef struct fpf_kickreg_details RGX_FPF_KICK_REG_DETAILS;

typedef struct _RGX_FPF_FW_APU_KICKREG_DETAILS_
{
	RGXFWIF_DEV_VIRTADDR sFPFApuKickRegBankMapping;
	RGX_FPF_KICK_REG_DETAILS sKickRegDetails;
} RGX_FPF_FW_APU_KICKREG_DETAILS;

// Populated in the "RGX_FPF_KICK_COMMS_FWCTX *ppsContext"
struct _RGX_FPF_KICK_COMMS_FWCTX_
{
	RGX_FPF_FW_APU_KICKREG_DETAILS sFWAPUKickRegDetails;
	RGXFWIF_DEV_VIRTADDR sFPTCBAPU2GPUCTRLFWAddr;
	RGXFWIF_DEV_VIRTADDR sFPTCBAPU2GPUFWAddr;
	RGXFWIF_DEV_VIRTADDR sFPTCBGPU2APUCTRLFWAddr;
	RGXFWIF_DEV_VIRTADDR sFPTCBGPU2APUFWAddr;
};

#endif /* SYS_FWIF_FPF_FPT_CB_H */
