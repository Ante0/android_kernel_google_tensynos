// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of DMA Engine
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifdef linux
#include "network_pipeline_module/dma_engine.h"
#include "network_pipeline_module/dma_engine_internal.h"

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>

#include "common/compiler.h"
#include "dma_simulator.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/task_scheduler.h"
#else /* linux */
#include "network_pipeline_module/dma_engine.h"
#include "network_pipeline_module_private/dma_engine_internal.h"

#include <cerrno>
#include <cstdint>

#include "common/compiler.h"
#include "dma/dma.h"
#include "linux_port/container_of.h"
#include "linux_port/log.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "pw_status/status.h"
#endif /* linux */

static inline int32_t GetIdleChannel(NepDmaEngine *engine, uint8_t *channel)
{
	if (!engine->free_channel) {
		return -EAGAIN;
	}

	*channel = ffs(engine->free_channel) - 1;
	return 0;
}

static inline void MarkDmaChannelAsBusy(NepDmaEngine *engine, uint8_t channel)
{
	engine->free_channel &= ~(uint8_t)(1U << channel);
}

static inline void MarkDmaChannelAsIdle(NepDmaEngine *engine, uint8_t channel)
{
	engine->free_channel |= (uint8_t)(1U << channel);
}

static int32_t ConfigureDma(void *dma_driver, int32_t channel);
static int32_t SubmitDmaRequest(void *dma_driver, uintptr_t dst, uintptr_t src, uint32_t size,
				uint16_t channel, void *context);

static void DmaCompleteWrapper(void *context, int32_t ret)
{
	struct NepAsyncProcessingRequest *async_req = (struct NepAsyncProcessingRequest *)context;
	struct NepStage *stage = (struct NepStage *)async_req->pkt_owner;
	struct NepAsyncEngine *async_engine =
		container_of(stage->engine, struct NepAsyncEngine, basic);
	NepDmaEngine *dma = container_of(async_engine, NepDmaEngine, engine);

	MarkDmaChannelAsIdle(dma, (uint8_t)((uintptr_t)async_req->data));
	NepProcessingRequestCallback(&async_req->basic, ret);
}

int32_t DmaEngineProcessing(struct NepEngine *engine, struct NepProcessingRequest *request)
{
	int32_t ret;
	uint8_t channel;
	struct NepAsyncProcessingRequest *async_req =
		container_of(request, struct NepAsyncProcessingRequest, basic);
	NepDmaGenericStage *dma_stage =
		container_of(((struct NepStage *)async_req->pkt_owner), NepDmaGenericStage, stage);
	struct NepAsyncEngine *async_engine = container_of(engine, struct NepAsyncEngine, basic);
	NepDmaEngine *dma = container_of(async_engine, NepDmaEngine, engine);
	NepDmaRequest dma_req;

	ret = GetIdleChannel(dma, &channel);
	if (ret) {
		return ret;
	}

	MarkDmaChannelAsBusy(dma, channel);
	ret = dma_stage->prepare_request(&dma_req, request);
	if (ret) {
		pr_err("Failed to prepare dma request for packet %" PRIu16 "\n",
		       request->item.pkt_id);
		goto out;
	}

	async_req->data = (void *)((uintptr_t)channel);
	ret = SubmitDmaRequest(dma->driver, dma_req.dst, dma_req.src, dma_req.size, channel,
			       async_req);
	if (ret) {
		goto out;
	}
	ret = 0;

out:
	if (ret) {
		MarkDmaChannelAsIdle(dma, channel);
	}

	return ret;
}

#define NUM_OF_DMA_ENGINE_REQUEST (8U)
SEC_FAST_DATA static struct NepAsyncProcessingRequest dma_engine_reqs[NUM_OF_DMA_ENGINE_REQUEST];

struct NepAsyncEngine *NepDmaEngineSingletonGet(void)
{
	SEC_FAST_DATA static NepDmaEngine dma_engine;
	return &dma_engine.engine;
}

int32_t NepDmaEngineSetup(struct NepAsyncEngine *engine, uint8_t channel_num, void *dma_driver,
			  struct NepTaskScheduler *scheduler)
{
	NepDmaEngine *dma = container_of(engine, NepDmaEngine, engine);
	uint8_t i = 0;

	if (!engine) {
		return -EINVAL;
	}
	dma->driver = dma_driver;
	dma->free_channel = 0;

	for (i = 0; i < channel_num; ++i) {
		int32_t ret;
		MarkDmaChannelAsIdle(dma, i);
		ret = ConfigureDma(dma->driver, i);
		if (ret) {
			return ret;
		}
	}
	NepAsyncEngineConstructor(&dma->engine, NUM_OF_DMA_ENGINE_REQUEST, &dma_engine_reqs[0],
				  scheduler, DmaEngineProcessing);
	NepAsyncEngineInit(&dma->engine);
	NepAsyncEngineAddToScheduler(&dma->engine, scheduler);
	return 0;
}

static int32_t PrepareIpHeaderFetchingRequest(NepDmaRequest *dma_req,
					      struct NepProcessingRequest *req)
{
#ifndef linux
	using std::min;
#endif /* linux */

	struct NepPacketContext *packet = req->packet;
	dma_req->src = (uintptr_t)packet->packet_buffer[kUseOriginalPacket].dv;
	if (!dma_req->src) {
		pr_err("Invalid src address in packet %" PRIu16 "\n", req->item.pkt_id);
		return -EINVAL;
	}
	dma_req->src = (uintptr_t)((uint8_t *)dma_req->src +
				   packet->packet_buffer[kUseOriginalPacket].head_offset);
	dma_req->dst = (uintptr_t)&packet->ip_header[0];
	dma_req->size = min((uint16_t)packet->packet_buffer[kUseOriginalPacket].dl,
			    (uint16_t)SIZE_OF_NEP_PACKET_HEADER);
	return 0;
}

int32_t NepIpHeaderFetchingStageConstructor(NepIpHeaderFetchingStage *ip_stage,
					    struct NepStageRouter *router,
					    struct PktContainer *container,
					    struct NepAsyncEngine *dma_engine,
					    struct NepTaskScheduler *scheduler)
{
	if (!ip_stage || !dma_engine || !scheduler) {
		return -EINVAL;
	}
	ip_stage->prepare_request = PrepareIpHeaderFetchingRequest;
	NepStageConstructor(&ip_stage->stage, router, container, &dma_engine->basic, scheduler);
	NepStageAddToScheduler(&ip_stage->stage, scheduler);
	return 0;
}

static int32_t PreparePacketMovementgRequest(NepDmaRequest *dma_req,
					     struct NepProcessingRequest *req)
{
#ifndef linux
	using std::min;
#endif /* linux */

	struct NepPacketContext *packet = req->packet;
	struct noa_desc *desc = (struct noa_desc *)&packet->noa_desc[0];
	dma_req->src = desc->dv;
	dma_req->dst = (uintptr_t)packet->packet_buffer[kOutputPacketBuffer].dv;
	if (!dma_req->src || !dma_req->dst) {
		pr_err("Invalid src or dst address in packet %" PRIu16 "\n", req->item.pkt_id);
		return -EINVAL;
	}
	dma_req->size = packet->packet_buffer[kOutputPacketBuffer].dl = desc->dl;
	packet->packet_buffer[kOutputPacketBuffer].head_offset = desc->head_offset;
	packet->use_packet_type = kOutputPacketBuffer;
	return 0;
}

int32_t NepPacketMovementStageConstructor(NepPacketMovementStage *movement_stage,
					  struct NepStageRouter *router,
					  struct PktContainer *container,
					  struct NepAsyncEngine *dma_engine,
					  struct NepTaskScheduler *scheduler)
{
	if (!movement_stage || !dma_engine || !scheduler) {
		return -EINVAL;
	}
	movement_stage->prepare_request = PreparePacketMovementgRequest;
	NepStageConstructor(&movement_stage->stage, router, container, &dma_engine->basic,
			    scheduler);
	NepStageAddToScheduler(&movement_stage->stage, scheduler);
	return 0;
}

#ifdef linux
void DmaComplete(void *context)
{
	return DmaCompleteWrapper(context, 0);
}

static int32_t SubmitDmaRequest(void *dma_driver, uintptr_t dst, uintptr_t src, uint32_t size,
				uint16_t channel, void *context)
{
	struct dma_request req = {
		.destination_address = dst,
		.source_address = src,
		.size = RoundUpDmaUnitSize(size),
		.channel = channel,
		.callback = DmaComplete,
		.context = context,
	};
	return queue_dma_request(req);
}

static int32_t ConfigureDma(void *dma_driver, int32_t channel)
{
	return 0;
}

#else /* linux */
namespace DmaDrv = ::noa::driver::dma;
void DmaComplete(pw::Status status, int32_t, void *context)
{
	int32_t ret;

	if (status.ok()) {
		ret = 0;
	} else {
		PW_LOG_ERROR(
			"Failed to perform dma request on packet %" PRIu16,
			reinterpret_cast<NepAsyncProcessingRequest *>(context)->basic.item.pkt_id);
		ret = -EIO;
	}
	DmaCompleteWrapper(context, ret);
}

static int32_t SubmitDmaRequest(void *dma_driver, uintptr_t dst, uintptr_t src, uint32_t size,
				uint16_t channel, void *context)
{
	noa::driver::dma::DmaTransferUnit unit = {
 		.destination_address = dst,
 		.source_address = src,
 		.size = RoundUpDmaUnitSize(size),
	};

	DmaDrv::DmaTransferRequest request = {
		.dma330_transfer_unit = { unit },
		.num_transfer_units = 1,
		.channel = channel,
		.callback = DmaComplete,
		.context = context,
		.peripheral = {.is_peripheral_transfer = 0, .peripheral_id = 0, .peripheral_bs_type = 0, .src_burst_type = 0, .dst_burst_type = 0},
	};
	auto id = reinterpret_cast<DmaDrv::Dma *>(dma_driver)->InitiateTransfer(request);
	if (id < 0) {
		return id;
	}
	return 0;
}

static int32_t ConfigureDma(void *dma_driver, int32_t channel)
{
	DmaDrv::DmaChannelConfiguration config = {
		.burst_size = {kBurstSize},
		.burst_length = {kBurstLength},
		.num_transfer_units = 1,
	};

	return reinterpret_cast<DmaDrv::Dma *>(dma_driver)
		->ConfigureChannel(channel, config);
}
#endif /* linux */
