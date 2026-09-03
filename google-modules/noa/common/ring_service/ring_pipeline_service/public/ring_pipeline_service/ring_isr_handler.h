/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header of ring ISR handler component.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_RING_PIPELINE_SERVICE_RING_ISR_HANDLER_H
#define NOA_RING_PIPELINE_SERVICE_RING_ISR_HANDLER_H

#ifdef linux
#include <linux/types.h>

#include "network_pipeline_framework/nested_ring_task.h"
#include "network_pipeline_framework/task_scheduler_bitmap.h"
#else /* linux */
#include <cstdint>

#include "network_pipeline_framework/nested_ring_task.h"
#include "network_pipeline_framework/task_scheduler_bitmap.h"
#include "notifier/notifier_mailbox.h"
#endif /* linux */

#define MAX_NUM_OF_TASK_PER_RING_SERVICE_ISR (8U)

typedef struct {
	uint32_t task_bitmask;
	NestedRingTask *tasks[MAX_NUM_OF_TASK_PER_RING_SERVICE_ISR];
#ifndef linux
	::noa::module::notifier::NotifierMailbox notifier;
#endif /* linux */
	NepBitmapTaskScheduler *scheduler;
	const char *name;
} RingServiceIsrContext;

/**
* @brief Handles a ring service interrupt.
*
* This function serves as the interrupt service routine handler for a ring service.
* Upon invocation, it iterates through its registered tasks. Each active task is
* queued to the associated scheduler, and finally, the scheduler is signaled to
* process the newly queued tasks.
*
* @param[in] context A pointer to the RingServiceIsrContext structure which
*  contains information about the tasks to be queued and the scheduler to be
*  signaled.
* @return 0 on success, or a negative error code on failure (e.g., -EINVAL if
*context is NULL).
*/
int32_t RingServiceIsrHandler(void *context);

/**
* @brief Initializes all ring service ISR handlers.
*
* @param[in] scheduler A pointer to the NepBitmapTaskScheduler that will be used
*by the ISR handlers to schedule tasks.
*/
void RingServiceIsrHandlerInitAll(NepBitmapTaskScheduler *scheduler);

/**
* @brief Enables all ring service ISR handlers.
*
* This function enables all configured ring service ISR (Interrupt Service
* Routine) handlers. For each ISR, it sets up the mechanism to invoke
* RingServiceIsrHandler upon an interrupt.
*
* @return 0 on success, or a negative error code if enabling any handler fails.
*/
int32_t RingServiceIsrHandlerEnableAll(void);

/**
* @brief Registers a task with a specific ring service ISR.
*
* This function registers a NestedRingTask with a specific ring service Interrupt
* Service Routine (ISR) handler. It associates the task with a unique ring_id
* within the ISR identified by interface_id. The registered task is queued for
* execution when the corresponding ring signals an interrupt.
*
* @param[in] interface_id The identifier of the network interface for which the ISR
*  handler is configured and to which this task will be registered.
* @param[in] ring_id The identifier for the specific ring within the ISR. This ID
*  determines which ring's interrupt will trigger this task.
* @param[in] task A pointer to the NestedRingTask that will be registered. This task
*  is queued for execution when the specified ring signals an interrupt.
*
* @return 0 on success. Returns a negative error code on failure, such as when
* an invalid ID is provided or if the specified ring_id is already registered
* with another task for the given interface_id.
*/
int32_t RingServiceRegisterIsr(uint8_t interface_id, uint8_t ring_id, NestedRingTask *task);

#endif /* NOA_RING_PIPELINE_SERVICE_RING_ISR_HANDLER_H */
