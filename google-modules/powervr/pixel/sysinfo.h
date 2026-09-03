// SPDX-License-Identifier: GPL-2.0

#if !defined(__SYSINFO_H__)
#define __SYSINFO_H__

#include "sysconfig.h"

#define EVENT_OBJECT_TIMEOUT_US (100000)

// Maximum time the driver will wait for firmware/hardware to respond in various cases.
// I (jessehall@) think this is used for cases where the HW should be able to respond without
// waiting for userspace-provided tasks to complete; see `RGXFWIF_MAX_*WORKLOAD_DEADLINE_MS` for
// the more general task-running-too-long watchdog timeout. This value is copied from some of the
// reference devices and seems reasonable but should be adjusted if it turns out to be unsuitable.
#define MAX_HW_TIME_US (500000 * get_time_multiplier())

// These use the EMU_TIME_MULTIPLIER even though our emulation platforms don't currently emulate
// power in a way that would need it; we might in the future. These numbers are reasonable guesses
// at an upper bound in a healthy system; they should be adjusted based on measurements on real
// systems when we can.
#define DEVICES_WATCHDOG_POWER_ON_SLEEP_TIMEOUT  (2000 * get_time_multiplier())
#define DEVICES_WATCHDOG_POWER_OFF_SLEEP_TIMEOUT (2000 * get_time_multiplier())

// MAX_HW_TIME_US / WAIT_TRY_COUNT is often used as a delay period, so WAIT_TRY_COUNT needs
// to be smaller than MAX_HW_TIME_US (so the truncating division result is >= 1us) and ideally
// at least a factor of 10 smaller.
#define WAIT_TRY_COUNT                           (10000)

#define SYS_RGX_OF_COMPATIBLE _COMPAT("google," PIXEL_GPU_GENERATION "-gpu")
#define SYS_RGX_DEV_NAME "rgx"

// The offset in bytes from the IOVA returned by the dma-heap driver, to the IPA that should be
// presented to the IOMMU for secure buffers (and therefore what is stored in the GPU page-table).
#define SECURE_DMABUF_IPA_OFFSET 0x100000000ULL

#endif	/* !defined(__SYSINFO_H__) */
