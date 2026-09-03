/*************************************************************************/ /*!
@File
@Title          RGX system layer custom fast path fence interface
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX fast path fence system layer interface.

                This is a custom implementation of the Fast Path Fence
                protocol.

                To enable this configuration (and disable the circular buffer
                reference implementation) set following build options as
                follows:

                    SUPPORT_FASTPATH_FENCE_CUSTOM_COMMS=1

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

#include "sys_fpf_common.h"
#include "sys_fpf_custom.h"

#include <linux/module.h>
#include <linux/types.h>

/*
 * Custom implementation code goes here....
 */
PVRSRV_ERROR SysFpfCustomDeviceInit(PVRSRV_DEVICE_CONFIG *psDeviceConfig)
{
    return PVRSRV_OK;
}

/*
 * Custom implementation code goes here....
 */
void SysFpfCustomDeviceDeInit(PVRSRV_DEVICE_CONFIG *psDeviceConfig)
{

}

/*
 * Custom implementation code goes here....
 */
PVRSRV_ERROR SysFpfCustomCommunicationInit(PVRSRV_DEVICE_CONFIG *psDeviceConfig,
                                            RGX_FPF_KICK_COMMS_FWCTX *psContext)
{
	return PVRSRV_OK;
}

/*
 * Custom implementation code goes here....
 */
void SysFpfCustomCommunicationDeInit(PVRSRV_DEVICE_CONFIG *psDeviceConfig)
{

}
