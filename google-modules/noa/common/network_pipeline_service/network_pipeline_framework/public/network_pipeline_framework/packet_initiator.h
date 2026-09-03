/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for NEP Pipeline Packet Initiator
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifndef NOA_NETWORK_PIPELINE_FRAMEWORK_PACKET_INITIATOR_H
#define NOA_NETWORK_PIPELINE_FRAMEWORK_PACKET_INITIATOR_H

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>

#include "common/core.h"
#include "packet_table.h"
#include "stage.h"
#include "task_scheduler.h"
#else /* linux */
#include <cerrno>
#include <cstdint>

#include "linux_port/list.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#endif /* linux */

struct NepPacketInitiator {
	struct NepStage *stage;
	struct NepPacketTable *table;
	bool (*has_packet)(struct NepPacketInitiator *initiator);
	int32_t (*format_packet)(struct NepPacketInitiator *initiator,
				 struct NepPacketContext *context);
	struct list_head waiting_hook;
	struct NepTaskScheduler *scheduler;
	struct NepRunnableTask irq_task;
	struct list_head tracking_hook;
};

/**
 * @brief Add a Packet Initiator to the Task Scheduler.
 *
 * This function adds a Packet Initiator to the initiator task list
 * of the Pipeline Task Scheduler. It is used by NepRunnableTask to
 * handle this operation.
 *
 * @param[in] task  The NepRunnableTask instance.
 * @param[in] data  A pointer to the NepPacketInitiator to be added to
 * the scheduler.
 *
 * @return  The result of the operation.
 * @retval 0  Success.
 * @retval <0  Error code indicating the reason for
 * failure.
 */
int32_t NepPacketInitiatorIrqHandler(struct NepRunnableTask *task, void *data);

#define __INIT_NEP_PACKET_INITIATOR(name, stg, tbl, haspkt, form, sche) \
	(struct NepPacketInitiator) { 				\
		.stage = stg, 					\
		.table = tbl, 					\
		.has_packet = haspkt, 				\
		.format_packet = form, 				\
		.waiting_hook = { &(name.waiting_hook), &(name.waiting_hook) }, 	\
		.scheduler = sche, 							\
		.irq_task = __INIT_NEP_RUNNABLE_TASK(name.irq_task, 			\
						     NepPacketInitiatorIrqHandler, 	\
						     NULL), 				\
		.tracking_hook = { &(name.tracking_hook), &(name.tracking_hook) }, 	\
	}

#define INIT_NEP_PACKET_INITIATOR(name, stg, tbl, haspkt, form, sche) 	\
	struct NepPacketInitiator name = 				\
		__INIT_NEP_PACKET_INITIATOR(name, stg, tbl, haspkt, form, sche)

/**
 * @brief Construct a NepPacketInitiator object.
 *
 * This function initializes a NepPacketInitiator object with the provided parameters.
 * It behaves the same way as INIT_NEP_PACKET_INITIATOR().
 *
 * @param[in] initiator  Pointer to the NepPacketInitiator object to be initialized.
 * @param[in] stage  Pointer to the NepStage object that this initiator belongs to.
 * @param[in] table  Pointer to the NepPacketTable object used for packet management.
 * @param[in] has_packet  Pointer to a function that checks if there are packets to be
 * processed.
 * @param[in] format_packet  Pointer to a function that formats the packet data for
 * processing.
 * @param[in] scheduler  Pointer to the NepTaskScheduler object used for scheduling tasks.
 */
void NepPacketInitiatorConstructor(struct NepPacketInitiator *initiator, struct NepStage *stage,
				   struct NepPacketTable *table,
				   bool (*has_packet)(struct NepPacketInitiator *initiator),
				   int32_t (*format_packet)(struct NepPacketInitiator *initiator,
							    struct NepPacketContext *context),
				   struct NepTaskScheduler *scheduler);

static inline void NepInitiatorStagePostSend(struct NepStage *stage)
{
	struct NepPacketInitiator *waiting_initiator;
	struct NepPacketInitiator *next;
	struct NepTaskScheduler *scheduler = stage->scheduler;
	list_for_each_entry_safe (waiting_initiator, next, &stage->waiting_list_head,
				  waiting_hook) {
		list_del(&waiting_initiator->waiting_hook);
		list_add_tail(&waiting_initiator->waiting_hook, &scheduler->pkt_initiator_tasks);
	}
}

#define INIT_NEP_PACKET_INITIATOR_STAGE(name, r, c, e, s)                                          \
	INIT_NEP_STAGE_HELPER(name, r, c, e, s, NepInitiatorStagePostSend)

static inline void NepInitiatorStageConstructor(struct NepStage *stage,
						struct NepStageRouter *router,
						struct PktContainer *container,
						struct NepEngine *engine,
						struct NepTaskScheduler *scheduler)
{
	NepStageConstructHelper(stage, router, container, engine, scheduler,
				NepInitiatorStagePostSend);
}

/**
 * @brief Generation a packet from a packet initiator.
 *
 * This function generates a packet from a  `NepPacketInitiator`. It checks if the
 * associated stage's container has space for new packets and if the initiator has
 * more packets to generate. If both conditions are met, it acquires a free
 * `PacketContext` from the `NepPacketTable`, populates it with data  using the
 * initiator's `format_packet` function, and adds it to the stage's container.
 *
 * @param[in] initiator  A pointer to the NepPacketInitiator to process.
 *
 * @return  The result of the operation.
 * @retval 0  Success, packets were processed.
 * @retval -EAGAIN  No free packet contexts available or the initiator has no
 * resources to process data.
 * @retval <0  Error code indicating an issue during packet processing.
 */
int32_t NepPacketIntitatorProcessing(struct NepPacketInitiator *initiator);

/**
 * @brief Schedule a packet initiator for processing.
 *
 * This function schedules a `NepPacketInitiator` for processing by adding it
 * to the task scheduler's irq_tasks list. This function can be safely executed
 * within an interrupt handler without causing any race condition issues.
 *
 * @param[in] initiator  A pointer to the NepPacketInitiator to schedule.
 *
 * @return  None.
 */
void NepPacketInitiatorSchedule(struct NepPacketInitiator *initiator);

static inline void NepPacketInitiatorReset(struct NepPacketInitiator *initiator)
{
	if (!initiator) {
		return;
	}
	NepStageReset(initiator->stage);
	list_del_init(&initiator->waiting_hook);
}

static inline void NepPacketInitiatorAddToScheduler(struct NepPacketInitiator *initiator,
						    struct NepTaskScheduler *scheduler)
{
	if (!initiator || !scheduler) {
		return;
	}
	initiator->scheduler = scheduler;
	initiator->table = scheduler->pkt_table;
	list_add_tail(&initiator->tracking_hook, &scheduler->all_pkt_initiators);
}

#endif /* NOA_NETWORK_PIPELINE_FRAMEWORK_PACKET_INITIATOR_H */
