/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for Ring Pipeline Service Sender
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_RING_PIPELINE_SERVICE_SENDER_H
#define NOA_RING_PIPELINE_SERVICE_SENDER_H

#ifdef linux
#include <linux/types.h>

#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "port.h"
#include "ring_manager_instance.h"
#else /* linux */
#include <cstdint>

#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "notifier/notifier_mailbox.h"
#endif /* linux */

struct DoorbellRingGroup {
	uint8_t id;
	uint8_t num_of_ring;
	uint32_t ring_dirty_bitmap;
	struct NoaRingManagerInfoFlow *rings;
#ifdef linux
	struct noa_port *port;
#else /* linux */
	::noa::module::notifier::NotifierMailbox notifier;
#endif /* linux */
};

struct NepRingSender {
	uint8_t num_of_group;
	struct NepEngine engine;
	struct DoorbellRingGroup *ring_group;
	struct NoaRingManagerInfoRoot *ring_root;
	struct NepRunnableTask task;
	struct NepTaskScheduler *scheduler;
};

/**
 * @brief Flush dirty rings and update head index.
 *
 * This function flushes dirty rings in the Ring Service, updates their head indices
 * and triggers doorbell to notify the destination.
 *
 * @param[in] task  The NepRunnableTask instance.
 * @param[in] data  The data associated with the task
 *                  (unused in this function).
 *
 * @return  The result of the operation.
 * @retval 0  Success.
 * @retval <0  Error code indicating the reason for
 *              failure.
 */
int32_t NepSenderFlushRingTask(struct NepRunnableTask *task, void *data);

/**
 * @brief Send a descriptor to the output ring.
 *
 * This function sends the given descriptor to the output ring and marks the
 * corresponding dirty bit in the ring_dirty_bitmap. It does not perform any
 * data flushing operations. A separate task is responsible for
 * flushing dirty data to the output ring.
 *
 * @param[in] engine  The NepEngine instance.
 * @param[in] req  The NepProcessingRequest containing the
 * descriptor to send.
 *
 * @return  The result of the operation.
 * @retval 0  Success.
 * @retval -EAGAIN  The output ring is full.
 * @retval <0  Error code indicating the reason for
 *              failure.
 */
int32_t SendDescriptor(struct NepEngine *engine, struct NepProcessingRequest *req);

/**
 * @brief Obtain the NepRingSender singleton instance.
 *
 * This singleton is not yet initialized. You must call RingSenderInit()
 * before using it.
 *
 * @return The NepRingSender singleton object pointer.
 */
struct NepRingSender *RingSenderSingletonGet(void);

/**
 * @brief Initializes a NepRingSender.
 *
 *
 * @param[in] sender The NepRingSender to initialize.
 * @param[in] scheduler The task scheduler to add the engine to.
 * @param[in] ring_root The NoaRingManagerInfoRoot containing the Network Interface Ring Group.
 *
 * @return  0 on success, a negative error code on failure.
 */
int32_t RingSenderInit(struct NepRingSender *sender, struct NepTaskScheduler *scheduler,
		       struct NoaRingManagerInfoRoot *ring_root);

#endif /* NOA_RING_PIPELINE_SERVICE_SENDER_H */
