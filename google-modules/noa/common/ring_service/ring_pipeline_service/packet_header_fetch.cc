// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of packet header fetching component.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "ring_pipeline_service/packet_header_fetch.h"

#include <linux/printk.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#include "common/core.h"
#include "common/compiler.h"
#include "common/ring.h"
#include "common/inttypes.h"
#include "dma_simulator.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_service/task_manager.h"
#else /* linux */
#include "ring_pipeline_service/packet_header_fetch.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "common/core.h"
#include "common/compiler.h"
#include "common/ring.h"
#include "dma/templated_dma.h"
#include "device_mgmt/manager.h"
#include "linux_port/bitops.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_service/task_manager.h"
#include "pw_assert/assert.h"
#endif /* linux */

#define NEP_PACKET_HEADER_ALIGNMENT ((16U))
#define NEP_PACKET_HEADER_ALIGNMENT_MASK ((NEP_PACKET_HEADER_ALIGNMENT - 1))

int32_t NestedRingFetchHeader(struct NestedRingStage *stage, NestedRingRequest *req)
{
#ifdef linux
	int32_t ret;
#define NepDtcmAddressMap(x) (x)
#else /* linux */
#define MAX_FETCH_HEADER_NUM (::noa::driver::dma::FetchHeaderProgram::kMaxHeaderNum)
	using ::noa::driver::dma::FetchHeaderProgram;
	using ::noa::driver::dma::TemplatedDma;
	using dma_fetch_header_request = FetchHeaderProgram::Request;
	using ProgramType = TemplatedDma::ProgramType;
	constexpr auto &NepDtcmAddressMap =
		::noa::driver::dma::DmaControllerBase::NepDtcmAddressMap;
#endif /* linux */
	NestedRingFetchHeaderContext *context = (NestedRingFetchHeaderContext *)stage->context;
	NestedShadowRing *shadow_ring = &stage->shadow_ring;
	dma_fetch_header_request dma_req;
	nep_device_entry *entry;
	uint32_t idx = 0;
	uintptr_t end;
	const uint32_t header_idx = (req->start_shadow_ring_addr & shadow_ring->end_mask) >>
				    shadow_ring->item_len_bitshift;
	uintptr_t header_buffer_address =
		context->header_buffer_ring_base | (header_idx << NEP_HEADER_BUFFER_SIZE_BITSHIFT);

	NESTED_RING_FOR_EACH_ENTRY(stage, req, end, entry)
	{
		const uint32_t header_offset =
			entry->pkt_info.packet_address & NEP_PACKET_HEADER_ALIGNMENT_MASK;
		const uintptr_t header_address = header_buffer_address + NEP_HEADER_BUFFER_HEADROOM;
		if (idx >= MAX_FETCH_HEADER_NUM) {
			break;
		} else if (entry->pkt_info.action != PKT_ACTION_UNDECIDED) {
			goto next;
		}
		dma_req.unit[idx].src =
			entry->pkt_info.packet_address & (~NEP_PACKET_HEADER_ALIGNMENT_MASK);
		dma_req.unit[idx].dst = NepDtcmAddressMap(header_address);
		entry->pkt_info.header_address = header_address + header_offset;
		entry->pkt_info.header_length = NEP_HEADER_FETCH_SIZE - header_offset;
		idx++;
	next:
		header_buffer_address = context->header_buffer_ring_base |
					((header_buffer_address + NEP_HEADER_BUFFER_SIZE) &
					 context->header_buffer_ring_end_mask);
	}

	if (!idx) {
		NestedRingRequestCompleteCallback(req);
		return 0;
	}
	dma_req.num = idx;
	dma_req.context = (void *)req;
#ifdef linux
	dma_req.callback = NestedRingRequestCompleteCallback;
	ret = queue_dma_fetch_header_request(&dma_req);
	if (ret) {
		return -EAGAIN;
	}
#else /* linux */
	dma_req.callback = [](pw::Status, void *context) {
		NestedRingRequestCompleteCallback(context);
	};
	auto ret = reinterpret_cast<TemplatedDma *>(context->dma)
			   ->InitiateTransfer(
				   TemplatedDma::ProgramToChannel(ProgramType::kFetchHeader),
				   ProgramType::kFetchHeader, dma_req);
	if (!ret.ok()) {
		PW_LOG_ERROR("Failed to init DMA fetch heaer for stage %s with ret %s", stage->name,
			     ret.str());
		return -EAGAIN;
	}
#endif /* linux */
	return 0;
}

NestedRingEngine *NestedRingFetchHeaderEngineSingletonGet(void)
{
	SEC_FAST_DATA static NestedRingEngine engine;
	return &engine;
}

#define FETCH_HEADER_ENGINE_REQUEST_NUM (2U)

int32_t NestedRingFetchHeaderEngineInit(NestedRingEngine *engine, NepBitmapTaskScheduler *scheduler,
					NestedRingTask *post_complete_task)
{
	SEC_FAST_DATA static NestedRingRequestPool req_pool;
	SEC_FAST_DATA static NestedRingRequest req_arr[FETCH_HEADER_ENGINE_REQUEST_NUM];

	NestedRingRequestPoolInit(&req_pool, FETCH_HEADER_ENGINE_REQUEST_NUM, req_arr);
	NestedRingAsyncEngineInit(engine, NestedRingFetchHeader, post_complete_task, NULL,
				  &req_pool, scheduler);
	return 0;
};

int32_t NestedRingFetchHeaderStageSetup(const NestedRingFetchHeaderStageSetupParams *params)
{
	NestedRingStage *stage = params->stage;
	NestedRingEngine *engine = params->engine;
	NestedRingFetchHeaderContext *context = params->fetch_header_context;

	context->dma = params->dma;
	context->header_buffer_ring_base = params->header_buffer_ring_base;
	if (!is_power_of_2(params->header_buffer_ring_size)) {
		pr_err("Ring size %" PRIu32 " is not power of 2\n",
		       params->header_buffer_ring_size);
		return -EINVAL;
	}
	context->header_buffer_ring_end_mask =
		(params->header_buffer_ring_size << NEP_HEADER_BUFFER_SIZE_BITSHIFT) - 1U;
	if (context->header_buffer_ring_base & context->header_buffer_ring_end_mask) {
		pr_err("Header buffer 0x%" PRIxPTR " is not aligned\n",
		       context->header_buffer_ring_base);
		return -EINVAL;
	}

	NestedRingStageInit(stage, true, *params->shadow_ring_info, *params->cached_ring_info,
			    context, engine, params->next_stage, params->task, params->name, NULL);
	return 0;
}
