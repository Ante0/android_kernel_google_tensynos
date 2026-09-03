/*************************************************************************/ /*!
@File           pvr_dvfs_common.h
@Title          System level interface for DVFS and PDVFS
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#ifndef PVR_DVFS_COMMON_H
#define PVR_DVFS_COMMON_H

#include "opaque_types.h"
#include "pvrsrv_error.h"
#if defined(SUPPORT_FW_OPP_TABLE) && defined(CONFIG_OF) && defined(CONFIG_PM_OPP)
#include "rgx_fwif_km.h"
#endif

#define TO_IMG_ERR(err) ((err == -EPROBE_DEFER) ? PVRSRV_ERROR_PROBE_DEFER : PVRSRV_ERROR_INIT_FAILURE)

struct pvr_opp_freq_table
{
#if ((LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)) && !defined(CHROMIUMOS_KERNEL))
	unsigned int *freq_table;
#else
	unsigned long *freq_table;
#endif
	int num_levels;
};

/*************************************************************************/ /*!
@Function       GetOPPValues

@Description    Common code to store the OPP points in the pvr_freq_table,
                for use with the IMG DVFS/devfreq implementation, or with
                the Proactive DVFS.
                Requires CONFIG_PM_OPP support in the kernel.

@Input          dev        OS Device node
@Output         min_freq   Min clock freq (Hz)
@Output         min_volt   Min voltage (V)
@Output         max_freq   Max clock freq (Hz)
@Return			0 on success
*/ /**************************************************************************/
#if defined(CONFIG_PM_OPP)
int GetOPPValues(struct device *dev,
                 unsigned long *min_freq,
                 unsigned long *min_volt,
                 unsigned long *max_freq,
                 struct pvr_opp_freq_table *pvr_freq_table);

/*************************************************************************/ /*!
@Function       FindOPPFreq

@Description    Common code to look up the OPP entry for the given frequency.
                Returns the level (OPP table index) if the exact freq exists,
                -ENODATA otherwise.
                Requires CONFIG_PM_OPP support in the kernel.

@Input          dev        OS Device node
@Input          freq_table  array of valid frequency points
@Input          freq       Clock freq (Hz)
@Output         level      OPP table index
@Return			0 on success
*/ /**************************************************************************/
int FindOPPFreq(struct device *dev,
                unsigned long *freq_table,
                unsigned long freq,
                unsigned int *level);

/*************************************************************************/ /*!
@Function       FindOPPFreqNearest

@Description    Common code to look up the OPP entry nearest to the given
                frequency and its level in the devicetree OPP table.
                Returns 0 if the nearest freq exists, -ENODATA otherwise.
                Requires CONFIG_PM_OPP support in the kernel.

@Input          dev        OS Device node
@Input          freq_table  array of valid frequency points
@Input          freq       Clock freq (Hz)
@Output         level      OPP table index
@Return			0 on success
*/ /**************************************************************************/
int FindOPPFreqNearest(struct device *dev,
                       unsigned int *level,
                       unsigned long *freq);

PVRSRV_ERROR
PVRSRVFindOPPFreqNearest(PPVRSRV_DEVICE_NODE psDeviceNode,
						 IMG_UINT32 *pui32Level,
						 IMG_UINT32 *pui32Freq);
#endif

/*************************************************************************/ /*!
@Function       DVFSCopyOPPTable

@Description    Copies the OPP points (voltage/frequency) to the target
                firmware structure RGXFWIF_OPP_INFO which must be pre-allocated.
                The source values are expected to be read into the kernel
                Power Manager from the platform's Device Tree.
                Requires CONFIG_OF and CONFIG_PM_OPP support in the kernel.

@Input          psDeviceNode       Device node
@Input          psOPPInfo          Target OPP data buffer
@Input          ui32MaxOPPLevels   Maximum number of OPP levels allowed in buffer.
@Return			PVRSRV_ERROR
*/ /**************************************************************************/
#if defined(SUPPORT_FW_OPP_TABLE) && defined(CONFIG_OF) && defined(CONFIG_PM_OPP)
PVRSRV_ERROR DVFSCopyOPPTable(PPVRSRV_DEVICE_NODE psDeviceNode,
							  RGXFWIF_OPP_INFO   *psOPPInfo,
							  IMG_UINT32          ui32MaxOPPLevels);
#endif

/*************************************************************************/ /*!
@Function       GetDevID

@Description    Extract the Linux minor number corresponding to a PVR device, for
				future use with PVRSRVGetDeviceInstanceByKernelDevID().


@Input          dev        OS Device node
@Return			The corresponding Linux device minor number.
*/ /**************************************************************************/
int GetDevID(struct device *dev);

#endif /* PVR_DVFS_COMMON_H */
