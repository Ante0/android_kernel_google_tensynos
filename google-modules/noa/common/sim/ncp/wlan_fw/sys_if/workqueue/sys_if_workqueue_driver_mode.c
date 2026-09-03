#include "sys_if_workqueue.h"

#include <linux/workqueue.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/slab.h>

typedef struct SysIfDelayedWorkStructDriverMode {
	/// @brief The kernel's delayed work structure.
	struct delayed_work w;
	/// @brief User-provided context pointer.
	void *ctx;
	/// @brief Callback function to be executed.
	SysIfDelayedWorkCallback callback;
} SysIfDelayedWorkStructDriverMode;

static void SysIfDelayedWorkCallbackHelper(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	SysIfDelayedWorkStructDriverMode *sys_if_work =
		container_of(delayed_work, struct SysIfDelayedWorkStructDriverMode, w);

	if (sys_if_work->callback) {
		sys_if_work->callback(sys_if_work->ctx);
	}
}

void SysIfInitDelayedWork(SysIfDelayedWorkStruct *work, SysIfDelayedWorkCallback work_fn, void *ctx)
{
	SysIfDelayedWorkStructDriverMode *sys_if_work;
	*work = kmalloc(sizeof(SysIfDelayedWorkStructDriverMode), GFP_ATOMIC);
	if (*work) {
		sys_if_work = (SysIfDelayedWorkStructDriverMode *)*work;
		sys_if_work->ctx = ctx;
		sys_if_work->callback = work_fn;
		INIT_DELAYED_WORK(&sys_if_work->w, SysIfDelayedWorkCallbackHelper);
	} else {
		pr_err("%s(): Unable to allocate memory.", __func__);
	}
}

void SysIfDeinitDelayedWork(SysIfDelayedWorkStruct *work)
{
	SysIfDelayedWorkStructDriverMode *sys_if_work = (SysIfDelayedWorkStructDriverMode *)*work;

	if (*work) {
		cancel_delayed_work(&sys_if_work->w);
		kfree(*work);
	}
}

bool SysIfScheduleDelayedWork(SysIfDelayedWorkStruct *work, uint32_t msecs)
{
	if (*work) {
		return schedule_delayed_work(&((SysIfDelayedWorkStructDriverMode *)*work)->w,
					     msecs_to_jiffies(msecs));
	}

	return false;
}

bool SysIfCancelDelayedWork(SysIfDelayedWorkStruct *work)
{
	if (*work) {
		return cancel_delayed_work_sync(&((SysIfDelayedWorkStructDriverMode *)*work)->w);
	}

	return true;
}
