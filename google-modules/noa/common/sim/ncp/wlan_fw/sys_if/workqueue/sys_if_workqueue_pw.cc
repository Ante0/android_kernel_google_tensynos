#include "sys_if_workqueue.h"

#include "linux_port/workqueue.h"
#include "pw_chrono/system_clock.h"
#include "pw_log/log.h"

namespace {

// Linker section boundary symbols for "sys_if_delayed_work_slots".
// Marked weak so binaries without registered static slots resolve to nullptr
// and report 0 slots instead of failing with undefined symbol errors.
extern "C" {
extern __attribute__((weak)) DelayedWorkInternal __start_sys_if_delayed_work_slots[];
extern __attribute__((weak)) DelayedWorkInternal __stop_sys_if_delayed_work_slots[];
}

size_t GetTotalSlots()
{
	if (__start_sys_if_delayed_work_slots == nullptr ||
	    __stop_sys_if_delayed_work_slots == nullptr) {
		return 0;
	}
	return __stop_sys_if_delayed_work_slots -
	       __start_sys_if_delayed_work_slots;
}

// Allocates an unused slot from the custom linker section.
// No placement new or dynamic memory allocation is performed here because
// C++ static initialization constructs all section slots upon program startup.
DelayedWorkInternal *AllocateSlot()
{
	size_t total_slots = GetTotalSlots();
	for (size_t i = 0; i < total_slots; ++i) {
		DelayedWorkInternal *internal =
			&__start_sys_if_delayed_work_slots[i];
		if (!internal->used) {
			internal->used = true;
			return internal;
		}
	}
	return nullptr;
}

// Frees a slot for reuse across sequential test runs or driver re-inits.
// Explicitly cancels and flushes any active work on the background dispatcher
// thread before marking the slot unused, preventing use-after-free conditions.
void FreeSlot(delayed_work *work_ptr)
{
	size_t total_slots = GetTotalSlots();
	for (size_t i = 0; i < total_slots; ++i) {
		DelayedWorkInternal *internal =
			&__start_sys_if_delayed_work_slots[i];
		if (&internal->work == work_ptr) {
			cancel_delayed_work(&internal->work);
			flush_delayed_work(&internal->work);
			internal->used = false;
			return;
		}
	}
}

}  // namespace

void SysIfInitDelayedWork(SysIfDelayedWorkStruct *work,
			  SysIfDelayedWorkCallback work_fn, void *ctx)
{
	DelayedWorkInternal *slot = AllocateSlot();
	if (slot != nullptr) {
		*work = &slot->work;
		INIT_DELAYED_WORK(reinterpret_cast<delayed_work *>(*work),
				  work_fn, ctx);
	} else {
		*work = nullptr;
		PW_LOG_ERROR("%s(): Unable to allocate slot.", __func__);
	}
}

void SysIfDeinitDelayedWork(SysIfDelayedWorkStruct *work)
{
	SysIfCancelDelayedWork(work);

	if (*work) {
		FreeSlot(reinterpret_cast<delayed_work *>(*work));
		*work = nullptr;
	}
}

bool SysIfScheduleDelayedWork(SysIfDelayedWorkStruct *work, uint32_t msecs)
{
	if (*work) {
		return schedule_delayed_work(reinterpret_cast<delayed_work *>(*work),
					     PW_SYSTEM_CLOCK_MS(msecs).ticks);
	}

	return false;
}

bool SysIfCancelDelayedWork(SysIfDelayedWorkStruct *work)
{
	if (*work) {
		return cancel_delayed_work_sync(reinterpret_cast<delayed_work *>(*work));
	}

	return true;
}
