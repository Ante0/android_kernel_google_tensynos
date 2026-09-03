/*************************************************************************/ /*!
@File           rgxpdvfs.h
@Title          RGX Proactive DVFS Functionality
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Header for the kernel mode Proactive DVFS Functionality.
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

#ifndef RGXPDVFS_H
#define RGXPDVFS_H

#include "img_types.h"
#include "rgxdevice.h"
#include "rgx_options.h"
#include "pvrsrv.h"
#include "pvr_dvfs.h"

static inline IMG_BOOL _PDVFSEnabled(void)
{
	PVRSRV_DATA *psSRVData = PVRSRVGetPVRSRVData();

	if (psSRVData->sDriverInfo.sKMBuildInfo.ui32BuildOptions &
	    psSRVData->sDriverInfo.sUMBuildInfo.ui32BuildOptions &
	    OPTIONS_PDVFS_EN)
	{
		return IMG_TRUE;
	}

	return IMG_FALSE;
}

/*************************************************************************/ /*!
 @Function	PDVFSLimitMaxFrequency

 @Description
 Apply a maximum GPU frequency constraint to the PDVFS firmware governor.
 If a maximum GPU freq constraint is already in place, it is replaced
 with the new OPP point.

 @Input	   psDevInfo : RGX Device
 @Input	   ui32MaxOPPPoint : Maximum OPP level (must be a valid OPP)

 @Return   PVRSRV_ERROR : OK - the firmware processed the config update.
*/ /**************************************************************************/
PVRSRV_ERROR PDVFSLimitMaxFrequency(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32MaxOPPPoint);
PVRSRV_ERROR PDVFSLimitMaxFrequencyLocked(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32MaxOPPPoint);
/*************************************************************************/ /*!
 @Function	PDVFSLimitMinFrequency

 @Description
 Apply a minimum GPU frequency constraint to the PDVFS firmware governor.
 If a minimum GPU freq constraint is already in place, it is replaced
 with the new OPP point.

 @Input	   psDevInfo : RGX Device
 @Input	   ui32MinOPPPoint : Minimum OPP level (must be a valid OPP)

 @Return   PVRSRV_ERROR : OK - the firmware processed the config update.
*/ /**************************************************************************/
PVRSRV_ERROR PDVFSLimitMinFrequency(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32MinOPPPoint);
PVRSRV_ERROR PDVFSLimitMinFrequencyLocked(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32MinOPPPoint);
/*************************************************************************/ /*!
 @Function	PDVFSResetFrequencyConstraints

 @Description
 Reset the minimum/maximum GPU frequency constraints in the PDVFS firmware
 governor. Both the constraints are reset to their default values,
 usually the lowest and highest available operating performance points.

 @Input	   psDevInfo : RGX Device

 @Return   PVRSRV_ERROR : OK - the firmware processed the config update.
*/ /**************************************************************************/
PVRSRV_ERROR PDVFSResetFrequencyConstraints(PVRSRV_RGXDEV_INFO *psDevInfo);

/*************************************************************************/ /*!
 @Function	PDVFSSetFrequencyHeadroom

 @Description
 Set a headroom frequency which is added (or subtracted, if less than zero)
 to the DVFS governor best-available frequency. This should be set to
 provide capacity headroom when the GPU load is anticipated to increase
 in a short period of time. The headroom should be removed when the GPU
 load returns to steady/normal operation.

 @Input	   psDevInfo : RGX Device
 @Input	   i32Headroom : Signed frequency value (in Hz). A large value
           relative to the operating frequency will reduce power
           efficiency and/or performance.

 @Return   PVRSRV_ERROR : OK - the firmware processed the config update.
*/ /**************************************************************************/
#if defined(SUPPORT_PDVFS_HEADROOM_EXT)
PVRSRV_ERROR PDVFSSetFrequencyHeadroom(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_INT32 i32Headroom);
#endif

/*************************************************************************/ /*!
 @Function	PDVFSSetReactivePollingInterval

 @Description
 Set the polling interval between reactive frequency updates.
 Analogous to the 'polling_interval' parameter in Linux devfreq.

 @Input	   psDevInfo : RGX Device
 @Input	   ui32MaxOPPPoint : Maximum OPP level (must be a valid OPP)

 @Return   PVRSRV_ERROR : OK - the firmware processed the config update.
*/ /**************************************************************************/
#if defined(SUPPORT_PDVFS_POLLINT_EXT)
PVRSRV_ERROR PDVFSSetReactivePollingInterval(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32PollingMs);
#endif

PVRSRV_ERROR PDVFSSetUpThreshold(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32UpThresholdInPct);

PVRSRV_ERROR PDVFSSetDownDifferential(PVRSRV_RGXDEV_INFO *psDevInfo, IMG_UINT32 ui32DownDifferentialInPct);

void RGXPDVFSCheckUtilisationChange(PVRSRV_RGXDEV_INFO *psDevInfo);

#endif /* RGXPDVFS_H */
