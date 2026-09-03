/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for NEP Pipeline Bitmap-Based Task Scheduler.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_NETWORK_PIPELINE_FRAMEWORK_TASK_SCHEDULER_BITMAP_H
#define NOA_NETWORK_PIPELINE_FRAMEWORK_TASK_SCHEDULER_BITMAP_H

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include "network_pipeline_framework/nested_ring_task.h"
#else /* linux */
#include <cerrno>
#include <cstdint>
#include <atomic>

#include "pw_sync/thread_notification.h"
#include "network_pipeline_framework/nested_ring_task.h"
#endif /* linux */

typedef struct {
	bool should_stop;
	uint8_t group_num;
#ifdef linux
	unsigned long *bitmap;
#else /* linux */
	std::atomic<uint16_t> *bitmap;
#endif /* linux */
	NestedRingTask **groups;
#ifdef linux
	wait_queue_head_t wait;
#else /* linux */
	pw::sync::ThreadNotification wait;
#endif /* linux */
} NepBitmapTaskScheduler;

#ifdef linux
void NepBitmapTaskSchedulerInit(NepBitmapTaskScheduler *scheduler, uint8_t group_num,
				unsigned long *bitmap, NestedRingTask **groups);
#else /* linux */
void NepBitmapTaskSchedulerInit(NepBitmapTaskScheduler *scheduler, uint8_t group_num,
				std::atomic<uint16_t> *bitmap, NestedRingTask **groups);
#endif /* linux */

/**
 * @brief Wake up the task scheduler.
 *
 * This function wakes up the task scheduler if it is currently in an idle state.
 *
 * @param[in] scheduler  A pointer to the NepBitmapTaskScheduler to wake up.
 *
 * @return None.
 */
static inline void NepBitmapTaskSchedulerSignal(NepBitmapTaskScheduler *scheduler)
{
#ifdef linux
	wake_up_interruptible(&scheduler->wait);
#else /* linux */
	scheduler->wait.release();
#endif /* linux */
}

/**
 * @brief Task scheduler main processing loop.
 *
 * This function implements the main processing loop for the task scheduler.
 * It continuously processes tasks related to stages. If there are no active
 * tasks, it enters an idle state to release CPU resources. When new tasks
 * are available, the scheduler is woken up via NepBitmapTaskSchedulerSignal().
 *
 * @param[in] context  A pointer to the NepBitmapTaskScheduler to run.
 *
 * @return  The result of the operation.
 * @retval 0  Success.
 * @retval <0  Error code indicating an issue during task processing.
 */
int32_t NepBitmapTaskSchedulerRunner(void *context);
#endif /* NOA_NETWORK_PIPELINE_FRAMEWORK_TASK_SCHEDULER_BITMAP_H */
