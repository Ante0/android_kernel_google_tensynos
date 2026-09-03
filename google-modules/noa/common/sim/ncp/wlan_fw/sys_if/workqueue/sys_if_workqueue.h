#ifndef SYS_IF_WORKQUEUE_H
#define SYS_IF_WORKQUEUE_H

#include "sys_if/types/types.h"

/// @brief Callback function type for delayed work.
typedef void (*SysIfDelayedWorkCallback)(void *);

/// @brief Type definition for a delayed work structure.
typedef void *SysIfDelayedWorkStruct;

#if !defined(__KERNEL__)
#include <stdint.h>
#include "linux_port/workqueue.h"

struct DelayedWorkInternal {
	delayed_work work;
	bool used;
};

#define CONCAT_IMPL_WORK(a, b) a##b
#define CONCAT_WORK(a, b) CONCAT_IMPL_WORK(a, b)

// Static Linker Section Allocation Architecture:
// Instead of dynamic memory allocation (malloc/new) or hardcoded global pools,
// each call to SysIfInitDelayedWorkWithResource injects a static slot directly
// into the custom linker section "sys_if_delayed_work_slots".
// Because the slot is declared with concrete C++ type DelayedWorkInternal, C++
// static initialization constructs it upon startup and cleanly runs RAII
// destructors at process exit, avoiding AddressSanitizer leaks.
/// @brief Allocates static storage for a delayed work slot in the linker section.
/// Uses __COUNTER__ to automatically generate a unique variable name.
#define SysIfAllocStaticDelayedWork()                                          \
	__attribute__((used, section("sys_if_delayed_work_slots")))            \
	static DelayedWorkInternal CONCAT_WORK(g_delayed_work_slot_, __COUNTER__)
#else
#define SysIfAllocStaticDelayedWork()
#endif

/// @brief Initializes a delayed work structure.
///
/// @param[in] work The work struct reference.
/// @param[in] work_fn Callback function to be executed when the work is
/// processed.
/// @param[in] ctx User-provided context pointer that will be passed to the
/// callback.
extern void SysIfInitDelayedWork(SysIfDelayedWorkStruct *work, SysIfDelayedWorkCallback work_fn,
				 void *ctx);

/// @brief Allocates a static slot in caller scope and initializes delayed work.
/// Guaranteed zero dynamic allocation and safe process-exit cleanup.
#if !defined(__KERNEL__)
#define SysIfInitDelayedWorkWithResource(work, work_fn, ctx)                   \
	do {                                                                   \
		SysIfAllocStaticDelayedWork();                                 \
		SysIfInitDelayedWork((work), (work_fn), (ctx));                \
	} while (0)
#else
#define SysIfInitDelayedWorkWithResource(work, work_fn, ctx)                   \
	SysIfInitDelayedWork((work), (work_fn), (ctx))
#endif

/// @brief Deinitializes a delayed work structure.
///
/// @param[in] work The work struct reference.
extern void SysIfDeinitDelayedWork(SysIfDelayedWorkStruct *work);

/// @brief Schedules delayed work for execution.
///
/// @param[in] work The work struct reference.
/// @param[in] msecs Delay in milliseconds before the work is executed.
/// @return `true` if the work was successfully scheduled, `false` otherwise.
extern bool SysIfScheduleDelayedWork(SysIfDelayedWorkStruct *work, uint32_t msecs);

/// @brief Cancels delayed work.
///
/// @param[in] work The work struct reference.
/// @return `true` if the work was successfully canceled, `false` otherwise.
extern bool SysIfCancelDelayedWork(SysIfDelayedWorkStruct *work);

#endif // SYS_IF_WORKQUEUE_H
