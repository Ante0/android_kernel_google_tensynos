// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of Ring Pipeline Service Obsolete Packet Handler
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "ring_pipeline_service/service.h"
#include "ring_pipeline_service/obsolete_packet_handler.h"

#include <linux/printk.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#include "common/inttypes.h"
#include "common/compiler.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "ring_pipeline_service/sender.h"
#else /* linux */
#include "ring_pipeline_service/service.h"
#include "ring_pipeline_service_private/obsolete_packet_handler.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "common/compiler.h"
#include "linux_port/container_of.h"
#include "linux_port/log.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "ring_pipeline_service_private/sender.h"
#endif /* linux */

#define SIZE_BIT_FOR_BUFFER_RECYCLE_STAGE_CONTAINER (5U)
#define SIZE_BIT_FOR_FALLBACK_PACKET_STAGE_CONTAINER (3U)

SEC_FAST_DATA static INIT_NEP_STAGE_ENDING_ROUTER(ending_router);

int32_t RingServiceObsoletePacketHandlerSetup(struct RingServiceObsoletePacketHandler *handler,
					      struct NepTaskScheduler *scheduler, uint16_t size,
					      unsigned long *recycle_bitmap)
{
	INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(SEC_FAST_DATA static,
						       buffer_recycle_container,
						       SIZE_BIT_FOR_BUFFER_RECYCLE_STAGE_CONTAINER);
	INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(
		SEC_FAST_DATA static, fallback_packet_container,
		SIZE_BIT_FOR_FALLBACK_PACKET_STAGE_CONTAINER);
	if (!handler) {
		return -EINVAL;
	}

	handler->size = size;
	handler->recycle_bitmap = recycle_bitmap;
	bitmap_zero(handler->recycle_bitmap, handler->size);
	handler->scheduler = scheduler;
	NepRunnableTaskConstructor(&handler->task, ObsoletePacketHandleTask, scheduler->pkt_table);
	ObsoletePacketHandlerStageSeup(&handler->recycle_stage, scheduler,
				       &buffer_recycle_container.basic,
				       RingServiceSenderEngineGet());
	ObsoletePacketHandlerStageSeup(&handler->fallback_packet_stage, scheduler,
				       &fallback_packet_container.basic,
				       RingServiceSenderEngineGet());
	return 0;
}

void ObsoletePacketHandlerStageSeup(struct NepStage *stage, struct NepTaskScheduler *scheduler,
				    struct PktContainer *container, struct NepEngine *engine)
{
	NepStageConstructor(stage, &ending_router, container, engine, scheduler);
	NepStageAddToScheduler(stage, scheduler);
}

int32_t RingServiceObsoletePacketRecycle(struct NepPacketTable *table, uint16_t pkt_id, void *data)
{
	int32_t ret;
	struct RingServiceObsoletePacketHandler *handler =
		(struct RingServiceObsoletePacketHandler *)data;
	struct NepPacketContext *packet;

	ret = NepPacketTablePacketGet(table, pkt_id, &packet);
	if (ret) {
		return ret;
	}
	NepPacketTablePacketRelease(packet);

	if (NepHasBufferToRecycled(packet, kUseOriginalPacket)) {
		struct NepStage *stage;
		if (packet->should_recycle_original_buffer) {
			stage = &handler->recycle_stage;
			OriginalPacketRecycleDescriptorFormat(packet);
		} else {
			stage = &handler->fallback_packet_stage;
			FallbackPacketDescriptorFormat(packet);
		}
		ret = stage->container->input_packet(stage->container, pkt_id);
		if (!ret) {
			NepStageHookProcessingEngine(stage);
			return 1;
		} else if (ret == -EAGAIN) {
			set_bit(pkt_id - 1, handler->recycle_bitmap);
			NepRunnableTaskSchedule(&handler->task, handler->scheduler);
			return ret;
		}
		pr_err("Failed to recycle packet buffer %" PRIu16 ", ret %" PRId32
		       ", drop original buffer\n",
		       pkt_id, ret);
		NepPacketContextResetBuffer(packet, kUseOriginalPacket);
	}

	if (NepHasBufferToRecycled(packet, kOutputPacketBuffer)) {
		pr_err("Not support recycle output packet buffer %" PRIu16 ", drop output buffer\n",
		       pkt_id);
		NepPacketContextResetBuffer(packet, kOutputPacketBuffer);
	}
	return 0;
}

static uint16_t FindRecyclableBuffer(struct RingServiceObsoletePacketHandler *handler)
{
	uint16_t index;
	uint16_t pkt_id;

	index = find_next_bit(handler->recycle_bitmap, handler->size, handler->tracking_index);
	if (index == handler->size) {
		index = find_next_bit(handler->recycle_bitmap, handler->size, 0);
	}
	if (index == handler->size) {
		handler->tracking_index = 0;
		return 0;
	}
	handler->tracking_index = index;
	pkt_id = index + 1;
	return pkt_id;
}

int32_t ObsoletePacketHandleTask(struct NepRunnableTask *task, void *data)
{
	uint16_t recycle_pkt_id;
	struct RingServiceObsoletePacketHandler *handler =
		container_of(task, struct RingServiceObsoletePacketHandler, task);
	struct NepPacketTable *table = (struct NepPacketTable *)data;

	recycle_pkt_id = FindRecyclableBuffer(handler);
	if (recycle_pkt_id == 0) {
		return 0;
	}

	clear_bit(recycle_pkt_id - 1, handler->recycle_bitmap);
	RingServiceObsoletePacketRecycle(table, recycle_pkt_id, (void *)handler);
	return 0;
}
