/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Internal API for NEP task.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_NETWORK_PIPELINE_SERVICE_TASK_MANAGER_INTERNAL_H
#define NOA_NETWORK_PIPELINE_SERVICE_TASK_MANAGER_INTERNAL_H

#ifdef linux
#include <linux/types.h>

#include "network_pipeline_framework/nested_ring_task.h"
#else /* linux */
#include <cstdint>
#include <atomic>

#include "network_pipeline_framework/nested_ring_task.h"
#endif /* linux */

/**
 * @brief Initializes all NEP (Network Pipeline Service) task groups and their tasks.
 *
 * This function prepares the NEP task system for operation. It iterates over all
 * predefined task groups.
 *
 */
void NepTaskInitAll(void);

/**
 * @brief Retrieves a pointer to the array of group bitmaps.
 *
 * This function provides access to the underlying bitmaps used for managing task
 * states within different NEP task groups. Each element in the returned array
 * corresponds to a task group and holds a bitmap where each bit represents the
 * state of a task in that group.
 *
 * @return A pointer to the first element of the global array `g_group_bitmap`.
 */
#ifdef linux
unsigned long *NepTaskGroupBitmapGet(void);
#else /* linux */
std::atomic<uint16_t> *NepTaskGroupBitmapGet(void);
#endif /* linux */

/**
 * @brief Retrieves a pointer to the array of task group pointers.
 *
 * This function returns a pointer to an array that holds pointers to the actual
 * task structures for each NEP task group. The main scheduler uses this to access
 * the tasks themselves when they are scheduled to run. Each element in the
 * `g_groups` array points to the first task of a specific group.
 *
 * @return A pointer to the first element of the global array `g_groups`. This array
 * contains pointers to the task arrays for each group.
 */
NestedRingTask **NepTaskGroupsGet(void);

/**
 * @brief Retrieves a pointer to the array of tasks for a specific group.
 *
 * This function allows retrieval of the task array for a particular NEP task
 * group, identified by its group ID. It maps the group ID to a statically
 * allocated array of tasks (e.g., system tasks, cache data tasks).
 *
 * @param[in] group  The identifier of the task group.
 * @return A pointer to the array of `NestedRingTask` structures for the specified group,
 * or `NULL` if the group ID is not valid.
 */
NestedRingTask *NepTaskGroupGet(uint8_t group);

#endif /* NOA_NETWORK_PIPELINE_SERVICE_TASK_MANAGER_INTERNAL_H */
