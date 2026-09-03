/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for NEP Pipeline Task Scheduler
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifndef NOA_NETWORK_PIPELINE_FRAMEWORK_TASK_SCHEDULER_H
#define NOA_NETWORK_PIPELINE_FRAMEWORK_TASK_SCHEDULER_H

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/spinlock.h>

#include "packet_table.h"
#else /* linux */
#include <cerrno>
#include <cstdint>

#include "linux_port/list.h"
#include "linux_port/spinlock.h"
#include "network_pipeline_framework/packet_table.h"
#include "pw_sync/thread_notification.h"
#endif /* linux */

struct NepRunnableTask {
	int32_t (*func)(struct NepRunnableTask *task, void *data);
	void *data;
	struct list_head list;
};

#define __INIT_NEP_RUNNABLE_TASK(name, f, d)                                                       \
	{                                                                                          \
		.func = f,                                                                         \
		.data = d,                                                                         \
		.list = { &(name.list), &(name.list) },                                            \
	}

/**
 * @brief Construct a NepRunnableTask object.
 *
 * This function initializes a NepRunnableTask object with the provided function and data.
 * It behaves the same way as __INIT_NEP_RUNNABLE_TASK().
 *
 * @param[in] task  Pointer to the NepRunnableTask object to be initialized.
 * @param[in] func  Pointer to the function that will be executed by this task.
 * @param[in] data  Pointer to the data that will be passed to the function.
 */
static inline void
NepRunnableTaskConstructor(struct NepRunnableTask *task,
			   int32_t (*func)(struct NepRunnableTask *task, void *data), void *data)
{
	task->func = func;
	task->data = data;
	INIT_LIST_HEAD(&task->list);
}

struct NepTaskScheduler {
	bool should_stop;
	struct NepPacketTable *pkt_table;
	struct list_head stage_tasks;
	struct list_head engine_tasks;
	struct list_head pkt_initiator_tasks;
	spinlock_t runnable_tasks_lock;
	struct list_head runnable_tasks;
#ifdef linux
	wait_queue_head_t wait;
#else /* linux */
	pw::sync::ThreadNotification wait;
#endif /* linux */
	struct list_head all_stages;
	struct list_head all_engines;
	struct list_head all_pkt_initiators;
};

static inline void InitNepTaskScheduler(struct NepTaskScheduler *scheduler,
					struct NepPacketTable *pkt_table)
{
	scheduler->should_stop = false;
	scheduler->pkt_table = pkt_table;
	INIT_LIST_HEAD(&scheduler->runnable_tasks);
	INIT_LIST_HEAD(&scheduler->stage_tasks);
	INIT_LIST_HEAD(&scheduler->engine_tasks);
	INIT_LIST_HEAD(&scheduler->pkt_initiator_tasks);
	spin_lock_init(&scheduler->runnable_tasks_lock);
#ifdef linux
	init_waitqueue_head(&scheduler->wait);
#endif /* linux */
	INIT_LIST_HEAD(&scheduler->all_stages);
	INIT_LIST_HEAD(&scheduler->all_engines);
	INIT_LIST_HEAD(&scheduler->all_pkt_initiators);
}

/**
 * @brief Wake up the task scheduler.
 *
 * This function wakes up the task scheduler if it is currently in an idle state.
 *
 * @param[in] scheduler  A pointer to the NepTaskScheduler to wake up.
 *
 * @return None.
 */
static inline void NepTaskSchedulerSignal(struct NepTaskScheduler *scheduler)
{
#ifdef linux
	wake_up_interruptible(&scheduler->wait);
#else /* linux */
	scheduler->wait.release();
#endif /* linux */
}

/**
 * @brief Schedule a runnable task.
 *
 * This function schedules a NepRunnableTask to be executed by the
 * Pipeline Service's Task Scheduler. This operation is safe to be
 * called during interrupt context.
 *
 * @param[in] task  The NepRunnableTask to be scheduled.
 * @param[in] scheduler  The NepTaskScheduler instance
 * to handle the task.
 */
static inline void NepRunnableTaskSchedule(struct NepRunnableTask *task,
					   struct NepTaskScheduler *scheduler)
{
	unsigned long flags = 0;
	spin_lock_irqsave(&scheduler->runnable_tasks_lock, flags);
	if (list_empty(&task->list)) {
		list_add(&task->list, &scheduler->runnable_tasks);
		NepTaskSchedulerSignal(scheduler);
	}
	spin_unlock_irqrestore(&scheduler->runnable_tasks_lock, flags);
}

/**
 * @brief Task scheduler main processing loop.
 *
 * This function implements the main processing loop for the task scheduler.
 * It continuously processes tasks related to packet initiators, engines,
 * and stages. If there are no active tasks, it enters an idle state to
 * release CPU resources. When new tasks are available, the scheduler is
 * woken up via NepTaskSchedulerSignal().
 *
 * @param[in] context  A pointer to the NepTaskScheduler to run.
 *
 * @return  The result of the operation.
 * @retval 0  Success.
 * @retval <0  Error code indicating an issue during task processing.
 */
int32_t NepTaskSchedulerRunner(void *context);

/**
 * @brief Reset the task scheduler and its components.
 *
 * This function resets the task scheduler and all its associated components,
 * including engines, stages, initiators, and the packet table.
 *
 * @param[in] scheduler  A pointer to the NepTaskScheduler to reset.
 *
 * @return None.
 */
void NepTaskSchedulerReset(struct NepTaskScheduler *scheduler);

#endif /* NOA_NETWORK_PIPELINE_FRAMEWORK_TASK_SCHEDULER_H */
