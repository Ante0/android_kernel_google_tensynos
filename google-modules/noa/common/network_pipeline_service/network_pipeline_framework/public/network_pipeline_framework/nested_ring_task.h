/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for Nested Ring Task
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_NETWORK_PIPELINE_FRAMEWORK_NESTED_RING_TASK_H
#define NOA_NETWORK_PIPELINE_FRAMEWORK_NESTED_RING_TASK_H

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#else /* linux */
#include <cerrno>
#include <cstdint>
#include <atomic>
#endif /* linux */

typedef struct {
	uint8_t id;
#ifdef linux
	unsigned long id_bit;
	unsigned long *group_bitmap;
#else /* linux */
	uint16_t id_bit;
	std::atomic<uint16_t> *group_bitmap;
#endif /* linux */
	void *context;
	int32_t (*run)(void *context);
	bool (*should_defer)(void *context);
} NestedRingTask;

/**
 * @brief Removes the specified task from the scheduler.
 *
 * This function removes the specified task from its associated scheduler group. It
 * clears the bit corresponding to the task's ID within the group's bitmap.
 *
 * @param[in] task A pointer to the NestedRingTask structure to be removed from the
 * scheduler.
 *
 * @return None.
 */
static inline void NestedRingTaskRemoveFromScheduler(NestedRingTask *task)
{
#ifdef linux
	clear_bit(task->id, task->group_bitmap);
#else /* linux */
	task->group_bitmap->fetch_and(static_cast<uint16_t>(~(task->id_bit)));
#endif /* linux */
}

/**
 * @brief Queues the specified task onto the scheduler.
 *
 * This function queues the specified task onto its associated scheduler group. It
 * sets the bit corresponding to the task's ID within the group's bitmap.
 *
 * @param[in] task A pointer to the NestedRingTask structure to be queued onto the
 * scheduler.
 *
 * @return None.
 */
static inline void NestedRingTaskQueueToScheduler(NestedRingTask *task)
{
#ifdef linux
	set_bit(task->id, task->group_bitmap);
#else /* linux */
	task->group_bitmap->fetch_or(task->id_bit);
#endif /* linux */
}

/**
 * @brief Checks if the specified task is scheduled.
 *
 * This function determines whether a given task is currently scheduled for
 * execution.
 *
 * @param task A pointer to the NestedRingTask structure to be checked.
 *
 * @return True if the task is scheduled, false otherwise.
 */
static inline bool NestedRingTaskIsScheduled(NestedRingTask *task)
{
#ifdef linux
	return test_bit(task->id, task->group_bitmap);
#else /* linux */
	return task->group_bitmap->load() & task->id_bit;
#endif /* linux */
}

/**
 * @brief Executes the specified task.
 *
 * This function executes the specified task by calling its associated `run` function.
 * The task's context is passed as an argument to the `run` function.
 *
 * @param[in] task A pointer to the NestedRingTask structure to be executed.
 *
 * @return The value returned by the task's run function. A return value of 0
 * typically indicates success, while a negative value signifies an error code.
 */
static inline int32_t NestedRingTaskRun(NestedRingTask *task)
{
	NestedRingTaskRemoveFromScheduler(task);
	return task->run(task->context);
}

static inline bool NestedRingTaskShouldDefer(NestedRingTask *task)
{
	if (!task->should_defer) {
		return false;
	}
	return task->should_defer(task->context);
}

/**
 * @brief Configures the run function and context for a task.
 *
 * Prepares a `NestedRingTask` for execution by associating it with its
 * operational logic (the `run` function) and the necessary data (`context`). This
 * setup assumes the task's core identity and group placement were previously
 * established.
 *
 * @param[in] task A pointer to the NestedRingTask structure to be configured.
 * @param[in] context A pointer to the data context for this task.
 * @param[in] run A function pointer to the task's execution routine.
 *
 * @return None.
 */
void NestedRingTaskSetup(NestedRingTask *task, void *context, int32_t (*run)(void *context));

#endif /* NOA_NETWORK_PIPELINE_FRAMEWORK_NESTED_RING_TASK_H */
