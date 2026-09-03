// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of NEP Pipeline Bitmap-Based Task Scheduler.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#include "network_pipeline_framework/task_scheduler_bitmap.h"

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/list.h>

#include "network_pipeline_framework/nested_ring_task.h"
#else /* linux */
#include "network_pipeline_framework/task_scheduler_bitmap.h"

#include <cerrno>
#include <cstdint>
#include <atomic>

#include "linux_port/log.h"
#include "network_pipeline_framework/nested_ring_task.h"
#endif /* linux */

#ifdef linux
void NepBitmapTaskSchedulerInit(NepBitmapTaskScheduler *scheduler, uint8_t group_num,
				unsigned long *bitmap, NestedRingTask **groups)
#else /* linux */
void NepBitmapTaskSchedulerInit(NepBitmapTaskScheduler *scheduler, uint8_t group_num,
				std::atomic<uint16_t> *bitmap, NestedRingTask **groups)
#endif /* linux */
{
	uint8_t i = 0;
	scheduler->should_stop = false;
	scheduler->group_num = group_num;
	scheduler->bitmap = bitmap;
	scheduler->groups = groups;
	for (i = 0; i < group_num; i++) {
		bitmap[i] = 0;
	}
#ifdef linux
	init_waitqueue_head(&scheduler->wait);
#endif /* linux */
}

static inline bool HasTodoTask(NepBitmapTaskScheduler *scheduler)
{
	uint8_t i = 0;
	for (; i < scheduler->group_num; i++) {
		if (scheduler->bitmap[i])
			return true;
	}
	return false;
}

static inline bool ShouldStop(const NepBitmapTaskScheduler *scheduler)
{
	return scheduler->should_stop
#ifdef linux
	       || kthread_should_stop()
#endif /* linux */
		;
}

static inline void WaitTask(NepBitmapTaskScheduler *scheduler)
{
#ifdef linux
	wait_event_interruptible(scheduler->wait, ShouldStop(scheduler) || HasTodoTask(scheduler));
#else /* linux */
	while (!HasTodoTask(scheduler) && !ShouldStop(scheduler)) {
		scheduler->wait.acquire();
	}
#endif /* linux */
	return;
}

static inline void RunTasks(NepBitmapTaskScheduler *scheduler, uint8_t group_id)
{
	uint16_t bitmap;
	uint16_t i;
	NestedRingTask *group = scheduler->groups[group_id];
#ifdef linux
	bitmap = scheduler->bitmap[group_id];
#else /* linux */
	bitmap = scheduler->bitmap[group_id].load();
#endif /* linux */

	if (!bitmap) {
		return;
	}

	for (i = 0; i < 16; i++) {
		if (!(bitmap & (1U << i))) {
			continue;
		}
		if (NestedRingTaskShouldDefer(&group[i])) {
			continue;
		}
		if (NestedRingTaskRun(&group[i])) {
			break;
		}
	}
}

int32_t NepBitmapTaskSchedulerRunner(void *context)
{
	int32_t ret = 0;
	NepBitmapTaskScheduler *scheduler = (NepBitmapTaskScheduler *)context;

	if (!context) {
		pr_err("Invalid arguments on scheduler runner\n");
		return -EINVAL;
	}

	while (!ShouldStop(scheduler)) {
		int8_t group_id = 0;

		WaitTask(scheduler);

		for (; group_id < scheduler->group_num; group_id++) {
			RunTasks(scheduler, group_id);
		}
	}
	ret = 0;

	return ret;
}
