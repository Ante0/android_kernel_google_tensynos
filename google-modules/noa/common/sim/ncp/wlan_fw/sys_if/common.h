#ifndef SYS_IF_COMMON_H
#define SYS_IF_COMMON_H

// To maintain a consistent interface, this header includes the
// required APIs with same interfaces from different path
// depending on the target platform (driver mode or pigweed).

#if defined(__KERNEL__)
// driver mode
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>
#include <linux/taskstats.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/limits.h>
#include <linux/stddef.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
#include <stdarg.h>
#else
#include <linux/stdarg.h>
#endif
#include <common/core.h>
#include <common/ring.h>
#include <common/ring_id.h>
#include <common/wlan_ring_id.h>
#include <common/noa_hw_ring.h>
#include <nep/ring_manager.h>
#include "netengine.h"
#include "nep_helpers.h"
#include "common/noa_share/completion.h"
#else
// pigweed
#include <cerrno>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <cstddef>
#include <climits>
#include <stdarg.h>
#include "linux_port/spinlock.h"
#include "linux_port/list.h"
#include "linux_port/dma_mapping.h"
#include "linux_port/tasklet.h"
#include "linux_port/device.h"
#include "linux_port/completion.h"
#include "linux_port/container_of.h"
#include "common/core.h"
#include "common/noa_hw_ring.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "net/nep_helpers.h"
#include "interrupt/interrupt.h"
#include "ring_mgmt/ring_manager.h"
#endif // defined(__KERNEL__)

#if defined(__KERNEL__)
// driver mode
#define UNUSED(x) UNUSED_##x
#else // defined(__KERNEL__)
// pigweed
#define UNUSED(x) UNUSED_##x __attribute__((__unused__))
#if !defined(IS_ENABLED)
// Concatenates a suffix token to the macro to control the timing of
// preprocessor expansion.
#define IS_ENABLED(x) (defined(x##_PW) && (x##_PW))
#endif // !defined(IS_ENABLED)
#endif // defined(__KERNEL__)

/// @brief Checks if the current execution environment is in driver mode.
static inline bool SysIfIsDriverMode(void)
{
#if defined(__KERNEL__)
	return true;
#else
	return false;
#endif // defined(__KERNEL__)
}

#endif /* SYS_IF_COMMON_H */
