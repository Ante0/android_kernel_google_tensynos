/* SPDX-License-Identifier: GPL-2.0 */

#pragma once

#include "img_types.h"
#include "sysconfig.h"

#if defined(PVRSRV_ANDROID_TRACE_GPU_WORK_PERIOD) && defined(SUPPORT_LINUX_DVFS)
/**
 * init_pixel_uid_tis() - Initialise time-in-state infrastructure.
 * @pixel_dev:	System layer private data
 * Return: PVRSRV_OK if initialisation was successful,
 * otherwise PVRSRV_ERROR_<CODE>
 */
int init_pixel_uid_tis(struct pixel_gpu_device *pixel_dev);

/**
 * deinit_pixel_uid_tis() - Deinitialise time-in-state infrastructure.
 * @pixel_dev:	System layer private data
 */
void deinit_pixel_uid_tis(struct pixel_gpu_device *pixel_dev);

/**
 * work_period_callback() - Record work periods information.
 * @hSysData: System layer private data
 * @uid: UID responsible for this work packet
 * @frequency: Main GPU clock during work packet [Hz]
 * @time_ns: Duration of the work packet
 */
void work_period_callback(IMG_HANDLE hSysData,
			  IMG_UINT32 uid,
			  IMG_UINT32 frequency,
			  IMG_UINT64 time_ns);

#else /* defined(PVRSRV_ANDROID_TRACE_GPU_WORK_PERIOD) && defined(SUPPORT_LINUX_DVFS) */

static __maybe_unused
int init_pixel_uid_tis(struct pixel_gpu_device *pixel_dev)
{
	return 0;
}

static __maybe_unused
void deinit_pixel_uid_tis(struct pixel_gpu_device *pixel_dev)
{
}

static __maybe_unused
void work_period_callback(IMG_HANDLE hSysData,
			  IMG_UINT32 uid,
			  IMG_UINT32 frequency,
			  IMG_UINT64 time_ns)
{
}

#endif /* defined(PVRSRV_ANDROID_TRACE_GPU_WORK_PERIOD) */
