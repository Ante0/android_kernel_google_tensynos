// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of NEP Pipeline Packet Initiator
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>

#include "common/inttypes.h"
#include "packet_initiator.h"
#else /* linux */
#include "network_pipeline_framework/packet_initiator.h"

#include <cerrno>
#include <cinttypes>
#include <cstdint>

#include "linux_port/container_of.h"
#include "linux_port/log.h"
#endif /* linux */

void NepPacketInitiatorConstructor(struct NepPacketInitiator *initiator, struct NepStage *stage,
				   struct NepPacketTable *table,
				   bool (*has_packet)(struct NepPacketInitiator *initiator),
				   int32_t (*format_packet)(struct NepPacketInitiator *initiator,
							    struct NepPacketContext *context),
				   struct NepTaskScheduler *scheduler)
{
	initiator->stage = stage;
	initiator->table = table;
	initiator->has_packet = has_packet;
	initiator->format_packet = format_packet;
	INIT_LIST_HEAD(&initiator->waiting_hook);
	initiator->scheduler = scheduler;
	NepRunnableTaskConstructor(&initiator->irq_task, NepPacketInitiatorIrqHandler, NULL);
	INIT_LIST_HEAD(&initiator->tracking_hook);
}

int32_t NepPacketIntitatorProcessing(struct NepPacketInitiator *initiator)
{
	uint32_t space;
	struct PktContainer *container = NULL;

	if (!initiator || !initiator->table || !initiator->stage || !initiator->stage->container) {
		pr_err("Invalid argument when process initiator\n");
		return -EINVAL;
	}
	container = initiator->stage->container;
	space = container->space_count(container);

	for (; space; --space) {
		int32_t err;
		uint16_t pkt_id;
		struct NepPacketContext *context;

		if (!initiator->has_packet(initiator)) {
			break;
		}

		err = NepPacketTableFreePacketAcquire(initiator->table, &pkt_id);
		if (err) {
			if (err != -EAGAIN) {
				pr_err("Failed to get packet id %" PRIu16 "\n", pkt_id);
			}
			return err;
		}

		err = NepPacketTablePacketGet(initiator->table, pkt_id, &context);
		if (err) {
			pr_err("Failed to get packet id %" PRIu16 "\n", pkt_id);
			return err;
		}

		err = initiator->format_packet(initiator, context);
		NepPacketTablePacketRelease(context);
		if (err) {
			if (err != -EAGAIN) {
				pr_err("Failed to format packet %" PRIu8 "\n", err);
			}
			NepPacketTablePacketFree(initiator->table, pkt_id);
			return err;
		}

		err = container->input_packet(container, pkt_id);
		if (err) {
			pr_err("Failed to input packet %" PRIu16 " into initiator\n", pkt_id);
			NepPacketTablePacketFree(initiator->table, pkt_id);
			return err;
		}
		NepStageHookProcessingEngine(initiator->stage);
	}

	if (!space && initiator->has_packet(initiator)) {
		list_del(&initiator->waiting_hook);
		list_add_tail(&initiator->waiting_hook, &initiator->stage->waiting_list_head);
	} else {
		list_del_init(&initiator->waiting_hook);
	}

	return 0;
}

void NepPacketInitiatorSchedule(struct NepPacketInitiator *initiator)
{
	if (!initiator || !initiator->scheduler) {
		pr_err("Invalid argument when initator schedule\n");
		return;
	}
	NepRunnableTaskSchedule(&initiator->irq_task, initiator->scheduler);
	NepTaskSchedulerSignal(initiator->scheduler);
}

int32_t NepPacketInitiatorIrqHandler(struct NepRunnableTask *task, void *data)
{
	struct NepPacketInitiator *initiator =
		container_of(task, struct NepPacketInitiator, irq_task);
	struct NepTaskScheduler *scheduler = initiator->scheduler;
	(void) data;

	if (!scheduler) {
		return -EINVAL;
	} else if (list_empty(&initiator->waiting_hook)) {
		list_add_tail(&initiator->waiting_hook, &scheduler->pkt_initiator_tasks);
	}
	return 0;
}
