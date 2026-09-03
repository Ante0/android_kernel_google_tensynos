// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of ring data routing component.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "ring_pipeline_service/ring_data_route.h"

#include <linux/printk.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#include "common/core.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "common/inttypes.h"
#include "common/compiler.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_service/task_manager.h"
#include "ring_manager_instance.h"
#include "dma_simulator.h"
#include "netengine_utils.h"
#else /* linux */
#include "ring_pipeline_service/ring_data_route.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "common/core.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "common/compiler.h"
#include "dma/templated_dma.h"
#include "device_mgmt/manager.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_service/task_manager.h"
#include "net/netengine_utils.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "pw_assert/assert.h"
#endif /* linux */

#ifdef linux
#define NepDtcmAddressMap(x) (x)
#else /* linux */
using std::min;
using dma_route_data_request = ::noa::driver::dma::RouteDescriptorProgram::Request;
using dma_drop_data_request = ::noa::driver::dma::DropDescriptorProgram::Request;
using dma_route_forward_pkt_request = ::noa::driver::dma::RouteForwardPacketProgram::Request;
using ::noa::driver::dma::TemplatedDma;
using ProgramType = TemplatedDma::ProgramType;
using noa::module::device_mgmt::DeviceManager;
constexpr auto &NepDtcmAddressMap = ::noa::driver::dma::DmaControllerBase::NepDtcmAddressMap;
#endif /* linux */

#define MAX_ROUTE_FEEDTHROUGH_DESC_NUM (8U)
#define MAX_ROUTE_FEEDBACK_DESC_NUM (MAX_ROUTE_FEEDTHROUGH_DESC_NUM)
#define MAX_ROUTE_VPN_DESC_NUM (MAX_ROUTE_FEEDTHROUGH_DESC_NUM)
/*
 * Each forward descriptor carries a corresponding feedback descriptor.
 * Therefore, the maximum number of forward descriptors must be less than the maximum number of feedback descriptors.
 */
#define MAX_ROUTE_FORWARD_DESC_NUM (2U)
static_assert(MAX_ROUTE_FORWARD_DESC_NUM <= MAX_ROUTE_FEEDBACK_DESC_NUM);

// Enabled by default for Gem5 integration tests.
static bool g_ring_route_forwarding_enabled = true;

void NestedRingRouteSetForwardingEnabled(bool enable)
{
	g_ring_route_forwarding_enabled = enable;
}

enum RouteDataType {
	kDmaDropDesc,
	kDmaRouteDesc,
	kDmaRouteForwardPacket,
};

typedef struct {
	enum RouteDataType type;
	struct ring_manager_instance *src_ring_instance;
	RingServiceDoorbellContext *dst_notifier;
	RingServiceDoorbellContext *feedback_notifier;
	struct noa_desc feedback[MAX_ROUTE_FEEDBACK_DESC_NUM];
	uint8_t forward[MAX_ROUTE_FORWARD_DESC_NUM * NOA_DESC_MAX_BYTE];
	uint8_t vpn[MAX_ROUTE_VPN_DESC_NUM * NOA_DESC_MAX_BYTE];
	uint16_t completed_buffer_cached_ring_idx;
	NepBufferPoolContext *buffer_pool_context;
} RouteDataRequestContext;

static void SetupRingDataRouteStats(const NestedRingRouteStageSetupParams *params, NestedRingRouteContext *context);

SEC_FAST static inline struct noa_desc *GetForwardDescriptor(RouteDataRequestContext *context,
							     uint32_t idx)
{
	return (struct noa_desc *)&(context->forward[idx * NOA_DESC_MAX_BYTE]);
}

SEC_FAST static inline struct noa_desc *GetVpnRxDescriptor(RouteDataRequestContext *context,
							   uint32_t idx)
{
	return (struct noa_desc *)&(context->vpn[idx * NOA_DESC_MAX_BYTE]);
}

SEC_FAST static void CompleteRouteData(NestedRingRequest *request)
{
	NestedRingRequest *req = (NestedRingRequest *)request;
	RouteDataRequestContext *req_context = (RouteDataRequestContext *)req->context;
	struct ring_manager_instance *instance =
		(struct ring_manager_instance *)req_context->src_ring_instance;

	switch (req_context->type) {
	case kDmaRouteDesc:
		RingServiceDoorbellTrigger(req_context->dst_notifier);
		break;
	case kDmaRouteForwardPacket:
		RingServiceDoorbellTrigger(req_context->dst_notifier);
		RingServiceDoorbellTrigger(req_context->feedback_notifier);
		NepBufferPoolCompleted(req_context->buffer_pool_context,
				       req_context->completed_buffer_cached_ring_idx);
		break;
	default:
		break;
	}

	if (instance->is_stopping && noa_ring_is_empty(&instance->ring)) {
		instance->stopped(instance);
	}
}

SEC_FAST static uintptr_t FormatFeedbackDesc(NestedRingRouteContext *route_context,
					     RouteDataRequestContext *req_context, void *desc,
					     void *entry, uint32_t idx)
{
	struct noa_desc *feedback = &req_context->feedback[idx];
	struct noa_desc *noa_desc = (struct noa_desc *)desc;
	(void)route_context;
	(void)entry;
	feedback->dp_low = noa_desc->dp_low;
	feedback->dp_high = noa_desc->dp_high;
	feedback->tkid = noa_desc->tkid;
	feedback->dv = noa_desc->dv;
	feedback->dst = NoaFeedbckPathIdFromDataPath(noa_desc->src);
	return (uintptr_t)feedback;
}

SEC_FAST static int32_t FormatForwardToWlanRequest(dma_route_forward_pkt_request *req,
						   RouteDataRequestContext *context,
						   struct noa_desc *desc, nep_device_entry *entry,
						   uint32_t idx)
{
	int32_t ret;
	struct noa_desc *forward = GetForwardDescriptor(context, idx);
	noa_buffer_pool_desc buffer_desc;

	ret = NepBufferPoolGetBuffer(context->buffer_pool_context, &buffer_desc);
	if (ret) {
		if (ret != -EAGAIN) {
			pr_err("Failed to get WLAN buffer, ret: %" PRId32 "\n", ret);
		}
		return ret;
	}
	forward->dst = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice,
					    kNoaWlanRingTxData);
	forward->dv = buffer_desc.dv;
	forward->dp_low = buffer_desc.dp_low;
	forward->dp_high = buffer_desc.dp_high;
	forward->tkid = buffer_desc.tkid;
	forward->desc_type = NOA_DESC_NETENG_PKT_FLOW;
	forward->dl = entry->pkt_info.header_length + entry->pkt_info.unchanged_payload_length;
	forward->head_offset = 0;
	NetEngineFormatWlanForwardDescriptor(&forward->ext_data[0], &entry->pkt_info,
					     &entry->forward_info);
	req->forward_unit[idx].header_len = entry->pkt_info.header_length;
	req->forward_unit[idx].payload_len = entry->pkt_info.unchanged_payload_length;
	req->forward_unit[idx].header_src = NepDtcmAddressMap(entry->pkt_info.header_address);
	req->forward_unit[idx].payload_src = entry->pkt_info.unchanged_payload_address;
	req->forward_unit[idx].pkt_dst = (uintptr_t)buffer_desc.dv;
	req->forward_unit[idx].forward_desc = NepDtcmAddressMap((uintptr_t)forward);
	req->forward_unit[idx].feedback_desc =
		NepDtcmAddressMap(FormatFeedbackDesc(NULL, context, desc, entry, idx));
	return 0;
}

SEC_FAST static int32_t FormatForwardToModemRequest(dma_route_forward_pkt_request *req,
						    RouteDataRequestContext *context,
						    struct noa_desc *desc, nep_device_entry *entry,
						    uint32_t idx)
{
	int32_t ret;
	struct noa_desc *forward = GetForwardDescriptor(context, idx);
	noa_buffer_pool_desc buffer_desc;

	ret = NepBufferPoolGetBuffer(context->buffer_pool_context, &buffer_desc);
	if (ret) {
		if (ret != -EAGAIN) {
			pr_err("Failed to get modem buffer, ret: %" PRId32 "\n", ret);
		}
		return ret;
	}
	forward->dst = NoaRingPathIdConvert(kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice,
					    kNoaModemRingTxData);
	forward->dv = buffer_desc.dv;
	forward->dp_low = buffer_desc.dp_low;
	forward->dp_high = buffer_desc.dp_high;
	forward->tkid = buffer_desc.tkid;
	forward->desc_type = NOA_DESC_MODEM_TX;
	forward->dl = entry->pkt_info.header_length + entry->pkt_info.unchanged_payload_length;
	forward->head_offset = 0;
	NetEngineFormatModemForwardDescriptor(&forward->ext_data[0], &entry->pkt_info,
					      &entry->forward_info);
	req->forward_unit[idx].header_len = entry->pkt_info.header_length;
	req->forward_unit[idx].payload_len = entry->pkt_info.unchanged_payload_length;
	req->forward_unit[idx].header_src = NepDtcmAddressMap(entry->pkt_info.header_address);
	req->forward_unit[idx].payload_src = entry->pkt_info.unchanged_payload_address;
	req->forward_unit[idx].pkt_dst = (uintptr_t)buffer_desc.dv;
	req->forward_unit[idx].forward_desc = NepDtcmAddressMap((uintptr_t)forward);
	req->forward_unit[idx].feedback_desc =
		NepDtcmAddressMap(FormatFeedbackDesc(NULL, context, desc, entry, idx));
	return 0;
}

SEC_FAST static int32_t RouteForwardPacket(
	struct NestedRingStage *stage, NestedRingRouteContext *context, NestedRingRequest *req,
	noa_ring_producer *dst_ring, noa_ring_producer *feedback_ring,
	RingServiceDoorbellContext *dst_notifier, RingServiceDoorbellContext *feedback_notifier,
	NepBufferPoolContext *buffer_pool_context, bool (*is_type_matched)(const void *),
	uint16_t forward_desc_len,
	int32_t (*format_request)(dma_route_forward_pkt_request *, RouteDataRequestContext *,
				  struct noa_desc *desc, nep_device_entry *, uint32_t),
	uint32_t *counter)
{
	int32_t ret;
	uint32_t idx;
	noa_ring_consumer *src_ring = context->src_ring;
	uint32_t src_ring_tail = context->processed_src_ring_tail;
	const uint32_t dst_ring_head = dst_ring->basic.head;
	const uint32_t dst_ring_tail = noa_ring_tail_read_once(dst_ring);
	const uint32_t dst_ring_size = dst_ring->basic.size;
	const uint32_t feedback_ring_head = feedback_ring->basic.head;
	const uint32_t feedback_ring_tail = noa_ring_tail_read_once(feedback_ring);
	const uint32_t feedback_ring_size = feedback_ring->basic.size;
	uint32_t count;
	uintptr_t shadow_ring_end_addr = noa_readptr(stage->shadow_ring.head_addr_ptr);
	RouteDataRequestContext *req_context = (RouteDataRequestContext *)req->context;
	dma_route_forward_pkt_request dma_req;
	struct noa_desc *desc;
	nep_device_entry *entry;
	uint16_t saved_buffer_src_got = NepBufferPoolSrcRingConsumerIdxGet(buffer_pool_context);

	if (!is_noa_ring_activate(dst_ring) || !is_noa_ring_activate(feedback_ring)) {
		return -EBUSY;
	}

	count = min(noa_ring_free_items_count(dst_ring_head, dst_ring_tail, dst_ring_size),
		    noa_ring_free_items_count(feedback_ring_head, feedback_ring_tail,
					      feedback_ring_size));
	// When a forward_desc is transmitted, a feedback_desc is also sent. Therefore, if
	// the dst_ring and feedback_ring are the same, the free_count must be divided by 2.
	if (dst_ring == feedback_ring) {
		count >>= 1;
	}
	count = min(count, MAX_ROUTE_FORWARD_DESC_NUM);
	if (!count) {
		return 0;
	}

	ret = idx = 0;
	req_context->buffer_pool_context = buffer_pool_context;
	NESTED_RING_FOR_EACH_DESC_AND_ENTRY(stage, &req->cached_ring_idx, &req->shadow_ring_addr,
					    shadow_ring_end_addr, desc, entry)
	{
		if (idx >= count || !is_type_matched(entry)) {
			break;
		}
		ret = format_request(&dma_req, req_context, desc, entry, idx);
		if (ret) {
			break;
		}
		dma_req.forward_unit[idx].forward_dst = (uintptr_t)noa_ring_buf_pos(
			dst_ring->basic.base, dst_ring->basic.head, dst_ring->basic.item_len);
		dst_ring->basic.head =
			noa_ring_move_pos(dst_ring->basic.head, 1, dst_ring->basic.size);

		dma_req.forward_unit[idx].feedback_dst =
			(uintptr_t)noa_ring_buf_pos(feedback_ring->basic.base,
						    feedback_ring->basic.head,
						    feedback_ring->basic.item_len);
		feedback_ring->basic.head =
			noa_ring_move_pos(feedback_ring->basic.head, 1, feedback_ring->basic.size);
		idx++;
	}
	count = idx;
	if (!count) {
		return ret;
	}
	src_ring_tail = noa_ring_move_pos(src_ring_tail, count, src_ring->basic.size);

	req_context->src_ring_instance = (struct ring_manager_instance *)src_ring->owner;
	req_context->dst_notifier = dst_notifier;
	req_context->feedback_notifier = feedback_notifier;
	req_context->type = kDmaRouteForwardPacket;

	dma_req.forward_desc_len = forward_desc_len;
	dma_req.num = count;
	dma_req.context = (void *)req;
	dma_req.src_ring_addr = (uintptr_t)src_ring->regs.read;
	dma_req.src_ring_tail = src_ring_tail;
	dma_req.dst_ring_addr = (uintptr_t)dst_ring->regs.write;
	dma_req.dst_ring_head = dst_ring->basic.head;
	dma_req.feedback_ring_addr = (uintptr_t)feedback_ring->regs.write;
	dma_req.feedback_ring_head = feedback_ring->basic.head;
	dma_req.buffer_ring_addr = NepBufferPoolSrcRingTailAddressGet(buffer_pool_context);
	dma_req.buffer_ring_tail = NepBufferPoolSrcRingConsumerIdxGet(buffer_pool_context);
	req_context->completed_buffer_cached_ring_idx =
		NepBufferPoolCachedRingConsumerIdxGet(buffer_pool_context);
#ifdef linux
	dma_req.callback = NestedRingRequestCompleteCallback;
	ret = queue_dma_route_forward_pkt_request(&dma_req);
	if (ret) {
		pr_err("Failed to route ring %s forwarding data, ret %" PRId32 "\n", src_ring->name,
		       ret);
		ret = -EAGAIN;
		goto err;
	}
#else /* linux */
	dma_req.callback = [](pw::Status, void *context) {
		NestedRingRequestCompleteCallback(context);
	};
	auto err = reinterpret_cast<TemplatedDma *>(context->dma)
			   ->InitiateTransfer(
				   TemplatedDma::ProgramToChannel(ProgramType::kRouteForwardPacket),
				   ProgramType::kRouteForwardPacket, dma_req);
	if (!err.ok()) {
		PW_LOG_ERROR(
			"Failed to init DMA transfer to route forwarding packet from %s to %s with ret %s",
			src_ring->name, dst_ring->name, err.str());
		ret = -EAGAIN;
		goto err;
	}
#endif /* linux */
  *counter = *counter + count;
	context->processed_src_ring_tail = src_ring_tail;
	return 0;

err:
	dst_ring->basic.head = dst_ring_head;
	feedback_ring->basic.head = feedback_ring_head;
	NepBufferPoolUpdateAllRingConsumerIdx(buffer_pool_context, saved_buffer_src_got);
	return ret;
}

SEC_FAST static int32_t
RouteDescriptor(struct NestedRingStage *stage, NestedRingRouteContext *context,
		NestedRingRequest *req, noa_ring_producer *dst_ring,
		RingServiceDoorbellContext *dst_notifier, const uint32_t max_count,
		bool (*is_type_matched)(const void *),
		uintptr_t (*format_src_data)(NestedRingRouteContext *, RouteDataRequestContext *,
					     void *, void *, uint32_t), uint32_t *counter)
{
#ifdef linux
	int32_t ret;
#endif /* linux */
	uint32_t idx;
	noa_ring_consumer *src_ring = context->src_ring;
	const uint16_t ring_item_len = dst_ring->basic.item_len;
	uint32_t src_ring_tail = context->processed_src_ring_tail;
	uint32_t dst_ring_head = dst_ring->basic.head;
	const uint32_t dst_ring_tail = noa_ring_tail_read_once(dst_ring);
	const uint32_t dst_ring_size = dst_ring->basic.size;
	uint32_t count = min(noa_ring_free_items_count(dst_ring_head, dst_ring_tail, dst_ring_size),
			     max_count);
	uintptr_t shadow_ring_end_addr = noa_readptr(stage->shadow_ring.head_addr_ptr);
	RouteDataRequestContext *req_context = (RouteDataRequestContext *)req->context;
	dma_route_data_request dma_req;
	struct noa_desc *desc;
	void *entry;

	if (!is_noa_ring_activate(dst_ring)) {
		return -EBUSY;
	}

	idx = 0;
	NESTED_RING_FOR_EACH_DESC_AND_ENTRY(stage, &req->cached_ring_idx, &req->shadow_ring_addr,
					    shadow_ring_end_addr, desc, entry)
	{
		if (idx >= count || !is_type_matched(entry)) {
			break;
		}
		dma_req.unit[idx].src =
			NepDtcmAddressMap(format_src_data(context, req_context, desc, entry, idx));
		dma_req.unit[idx].dst = (uintptr_t)noa_ring_buf_pos(
			dst_ring->basic.base, dst_ring_head, dst_ring->basic.item_len);
		dst_ring_head = noa_ring_move_pos(dst_ring_head, 1, dst_ring->basic.size);
		idx++;
	}
	count = idx;
	if (!count) {
		return 0;
	}
	src_ring_tail = noa_ring_move_pos(src_ring_tail, count, src_ring->basic.size);

	req_context->src_ring_instance = (struct ring_manager_instance *)src_ring->owner;
	req_context->dst_notifier = dst_notifier;
	req_context->type = kDmaRouteDesc;

	dma_req.num = count;
	dma_req.desc_len = ring_item_len;
	dma_req.context = (void *)req;
	dma_req.src_ring_addr = (uintptr_t)src_ring->regs.read;
	dma_req.src_ring_tail = src_ring_tail;
	dma_req.dst_ring_addr = (uintptr_t)dst_ring->regs.write;
	dma_req.dst_ring_head = dst_ring_head;
#ifdef linux
	dma_req.callback = NestedRingRequestCompleteCallback;
	ret = queue_dma_route_data_request(&dma_req);
	if (ret) {
		pr_err("Failed to route ring %s feedthrough data, ret %" PRId32 "\n",
		       src_ring->name, ret);
		return -EAGAIN;
	}
#else /* linux */
	dma_req.callback = [](pw::Status, void *context) {
		NestedRingRequestCompleteCallback(context);
	};
	auto ret = reinterpret_cast<TemplatedDma *>(context->dma)
			   ->InitiateTransfer(
				   TemplatedDma::ProgramToChannel(ProgramType::kRouteDescriptor),
				   ProgramType::kRouteDescriptor, dma_req);
	if (!ret.ok()) {
		PW_LOG_ERROR(
			"Failed to init DMA transfer to route descriptor from %s to %s with ret %s",
			src_ring->name, dst_ring->name, ret.str());
		return -EAGAIN;
	}
#endif /* linux */
  *counter = *counter + count;
	dst_ring->basic.head = dst_ring_head;
	context->processed_src_ring_tail = src_ring_tail;
	return 0;
}

static int32_t DropData(struct NestedRingStage *stage, NestedRingRouteContext *context,
			NestedRingRequest *req)
{
#ifdef linux
	int32_t ret;
#endif /* linux */
	noa_ring_consumer *src_ring = context->src_ring;
	NestedShadowRing *shadow_ring = &stage->shadow_ring;
	NestedCachedRing *cached_ring = &stage->cached_ring;
	uint32_t src_ring_tail = context->processed_src_ring_tail;
	uint16_t cached_ring_head = readw(cached_ring->head_idx_ptr);
	uintptr_t shadow_ring_head = noa_readptr(shadow_ring->head_addr_ptr);
	uint32_t count = noa_ring_items_count(cached_ring_head, req->start_cached_ring_idx,
					      cached_ring->size);
	RouteDataRequestContext *req_context = (RouteDataRequestContext *)req->context;
	dma_drop_data_request dma_req;

	src_ring_tail = noa_ring_move_pos(src_ring_tail, count, src_ring->basic.size);

	req->cached_ring_idx = cached_ring_head;
	req->shadow_ring_addr = shadow_ring_head;

	req_context->src_ring_instance = (struct ring_manager_instance *)src_ring->owner;
	req_context->type = kDmaDropDesc;

	dma_req.context = (void *)req;
	dma_req.src_ring_addr = (uintptr_t)src_ring->regs.read;
	dma_req.src_ring_tail = src_ring_tail;
#ifdef linux
	dma_req.callback = NestedRingRequestCompleteCallback;
	ret = queue_dma_drop_data_request(&dma_req);
	if (ret) {
		pr_err("Failed to drop ring %s data, ret %" PRId32 "\n", src_ring->name, ret);
		return -EAGAIN;
	}
#else /* linux */
	dma_req.callback = [](pw::Status, void *context) {
		NestedRingRequestCompleteCallback(context);
	};
	auto ret = reinterpret_cast<TemplatedDma *>(context->dma)
			   ->InitiateTransfer(
				   TemplatedDma::ProgramToChannel(ProgramType::kDropDescriptor),
				   ProgramType::kDropDescriptor, dma_req);
	if (!ret.ok()) {
		PW_LOG_ERROR("Failed to init DMA transfer to drop descriptor from %s with ret %s",
			     src_ring->name, ret.str());
		return -EAGAIN;
	}
#endif /* linux */
	context->processed_src_ring_tail = src_ring_tail;
	return 0;
}

SEC_FAST static uintptr_t FormatFeedthroughDesc(NestedRingRouteContext *route_context,
						RouteDataRequestContext *req_context, void *desc,
						void *entry, uint32_t idx)
{
	(void)route_context;
	(void)req_context;
	(void)entry;
	(void)idx;
	return (uintptr_t)desc;
}

SEC_FAST static uintptr_t FormatFallbackDesc(NestedRingRouteContext *route_context,
					     RouteDataRequestContext *req_context, void *desc,
					     void *entry, uint32_t idx)
{
	(void)route_context;
	(void)req_context;
	(void)entry;
	(void)idx;
	((struct noa_desc *)desc)->reason = FWD_REASON_FALLBACK;
	return (uintptr_t)desc;
}

SEC_FAST static uintptr_t FormatVpnRxDesc(NestedRingRouteContext *route_context,
					  RouteDataRequestContext *req_context, void *desc,
					  void *entry, uint32_t idx)
{
	struct noa_desc *vpn = GetVpnRxDescriptor(req_context, idx);
	nep_device_entry *device_entry = (nep_device_entry *)entry;
	route_context->format_vpn_rx_desc(desc, device_entry, vpn);
	return (uintptr_t)vpn;
}

SEC_FAST static int32_t RouteDataFromDevice(NestedRingStage *stage, NestedRingRouteContext *context, NestedRingRequest *req)
{
	nep_device_entry *entry = (nep_device_entry *)req->shadow_ring_addr;

	switch (entry->pkt_info.action) {
	case PKT_ACTION_UNDECIDED:
		return RouteDescriptor(stage, context, req, context->feedthrough_ring,
				       context->feedthrough_path_notifier,
				       MAX_ROUTE_FEEDTHROUGH_DESC_NUM,
				       NEP_DEVICE_ENTRY_IS_UNDECIDED, FormatFallbackDesc, context->fallback_counter);
	case PKT_ACTION_FEEDTHROUGH:
		return RouteDescriptor(stage, context, req, context->feedthrough_ring,
				       context->feedthrough_path_notifier,
				       MAX_ROUTE_FEEDTHROUGH_DESC_NUM,
				       NEP_DEVICE_ENTRY_IS_FEEDTHROUGH, FormatFeedthroughDesc, context->feedthrough_counter);
	case PKT_ACTION_FILTER_PACKET:
		return RouteDescriptor(stage, context, req, context->feedback_ring,
				       context->feedback_path_notifier, MAX_ROUTE_FEEDBACK_DESC_NUM,
				       NEP_DEVICE_ENTRY_IS_FILTER_PACKET, FormatFeedbackDesc, context->filter_counter);
	case PKT_ACTION_FALLBACK:
		return RouteDescriptor(stage, context, req, context->feedthrough_ring,
				       context->feedthrough_path_notifier,
				       MAX_ROUTE_FEEDTHROUGH_DESC_NUM, NEP_DEVICE_ENTRY_IS_FALLBACK,
				       FormatFallbackDesc, context->fallback_counter);
	case PKT_ACTION_FORWARD_TO_WLAN:
		if (!context->forward_wlan_ring) {
			pr_err("Src ring %s didn't support forwarding wlan\n",
			       context->src_ring->name);
			return -EINVAL;
		} else if (!g_ring_route_forwarding_enabled) {
			entry->pkt_info.action = PKT_ACTION_FALLBACK;
			return RouteDescriptor(stage, context, req, context->feedthrough_ring,
					       context->feedthrough_path_notifier,
					       MAX_ROUTE_FEEDTHROUGH_DESC_NUM,
					       NEP_DEVICE_ENTRY_IS_FALLBACK, FormatFallbackDesc,
					       context->fallback_counter);
		}
		return RouteForwardPacket(
			stage, context, req, context->forward_wlan_ring, context->feedback_ring,
			context->forward_wlan_path_notifier, context->feedback_path_notifier,
			context->wlan_buffer_pool_context, NEP_DEVICE_ENTRY_IS_FORWARD_TO_WLAN,
			NOA_DESC_NETENG_PKT_FLOW_BYTE, FormatForwardToWlanRequest,
			context->forward_to_wlan_counter);
	case PKT_ACTION_FORWARD_TO_MODEM:
		if (!context->forward_modem_ring) {
			pr_err("Src ring %s didn't support forwarding modem\n",
			       context->src_ring->name);
			return -EINVAL;
		} else if (!g_ring_route_forwarding_enabled) {
			entry->pkt_info.action = PKT_ACTION_FALLBACK;
			return RouteDescriptor(stage, context, req, context->feedthrough_ring,
					       context->feedthrough_path_notifier,
					       MAX_ROUTE_FEEDTHROUGH_DESC_NUM,
					       NEP_DEVICE_ENTRY_IS_FALLBACK, FormatFallbackDesc,
					       context->fallback_counter);
		}
		return RouteForwardPacket(
			stage, context, req, context->forward_modem_ring, context->feedback_ring,
			context->forward_modem_path_notifier, context->feedback_path_notifier,
			context->modem_buffer_pool_context, NEP_DEVICE_ENTRY_IS_FORWARD_TO_MODEM,
			NOA_DESC_MODEM_TX_BYTE, FormatForwardToModemRequest,
			context->forward_to_modem_counter);
	case PKT_ACTION_PROCESS_AS_VPN:
		return RouteDescriptor(stage, context, req, context->vpn_ring,
				       context->vpn_path_notifier, MAX_ROUTE_VPN_DESC_NUM,
				       NEP_DEVICE_ENTRY_IS_PROCESS_AS_VPN, FormatVpnRxDesc, context->vpn_counter);
	default:
		pr_err("Invalid nep device desc action %" PRIu16 "\n", entry->pkt_info.action);
		return -EINVAL;
	}
	return -EINVAL;
}

SEC_FAST static uintptr_t FormatVpnTxDesc(NestedRingRouteContext *route_context,
					  RouteDataRequestContext *req_context, void *desc,
					  void *entry, uint32_t idx)
{
	struct noa_desc *noa_desc = (struct noa_desc *)desc;
	nep_host_entry *host_entry = (nep_host_entry *)entry;
	(void)route_context;
	(void)req_context;
	(void)idx;
	// Since VPN processes packets in-place, the packet length must include the original head_offset.
	noa_desc->dl = host_entry->pkt_info.packet_length + noa_desc->head_offset;
	noa_desc->reason = FWD_REASON_VPN;
	return (uintptr_t)desc;
}

SEC_FAST static int32_t RouteDataFromHost(NestedRingStage *stage, NestedRingRouteContext *context,
					  NestedRingRequest *req)
{
	nep_host_entry *entry = (nep_host_entry *)req->shadow_ring_addr;

	switch (entry->pkt_info.action) {
	case PKT_ACTION_UNDECIDED:
		return RouteDescriptor(stage, context, req, context->feedthrough_ring,
				       context->feedthrough_path_notifier,
				       MAX_ROUTE_FEEDTHROUGH_DESC_NUM, NEP_HOST_ENTRY_IS_UNDECIDED,
				       FormatFallbackDesc, context->fallback_counter);
	case PKT_ACTION_FEEDTHROUGH:
		return RouteDescriptor(stage, context, req, context->feedthrough_ring,
				       context->feedthrough_path_notifier,
				       MAX_ROUTE_FEEDTHROUGH_DESC_NUM,
				       NEP_HOST_ENTRY_IS_FEEDTHROUGH, FormatFeedthroughDesc, context->feedthrough_counter);
	case PKT_ACTION_FILTER_PACKET:
		return RouteDescriptor(stage, context, req, context->feedback_ring,
				       context->feedback_path_notifier, MAX_ROUTE_FEEDBACK_DESC_NUM,
				       NEP_HOST_ENTRY_IS_FILTER_PACKET, FormatFeedbackDesc, context->filter_counter);
	case PKT_ACTION_FALLBACK:
		return RouteDescriptor(stage, context, req, context->feedthrough_ring,
				       context->feedthrough_path_notifier,
				       MAX_ROUTE_FEEDTHROUGH_DESC_NUM, NEP_HOST_ENTRY_IS_FALLBACK,
				       FormatFallbackDesc, context->fallback_counter);
	case PKT_ACTION_PROCESS_AS_VPN:
		return RouteDescriptor(stage, context, req, context->vpn_ring,
				       context->vpn_path_notifier, MAX_ROUTE_VPN_DESC_NUM,
				       NEP_HOST_ENTRY_IS_PROCESS_AS_VPN, FormatVpnTxDesc, context->vpn_counter);
	default:
		pr_err("Invalid nep host desc action %" PRIu16 "\n", entry->pkt_info.action);
		return -EINVAL;
	}
	return -EINVAL;
}

SEC_FAST static int32_t
RouteAlcedoVendorDescriptor(struct NestedRingStage *stage, NestedRingRouteContext *context,
			    NestedRingRequest *req, noa_ring_producer *dst_ring,
			    RingServiceDoorbellContext *dst_notifier, const uint32_t max_count,
			    bool (*is_type_matched)(const void *), uint32_t *counter)
{
#ifdef linux
	int32_t ret;
#endif /* linux */
	uint32_t idx = 0;
	uint32_t src_ring_moved_cnt = 0;
	noa_ring_consumer *src_ring = context->src_ring;
	const uint16_t ring_item_len = dst_ring->basic.item_len;
	uint32_t src_ring_tail = context->processed_src_ring_tail;
	uint32_t dst_ring_head = dst_ring->basic.head;
	const uint32_t dst_ring_tail = noa_ring_tail_read_once(dst_ring);
	const uint32_t dst_ring_size = dst_ring->basic.size;
	uint32_t count = min(noa_ring_free_items_count(dst_ring_head, dst_ring_tail, dst_ring_size),
			     max_count);
	uintptr_t shadow_ring_end_addr = noa_readptr(stage->shadow_ring.head_addr_ptr);
	RouteDataRequestContext *req_context = (RouteDataRequestContext *)req->context;
	dma_route_data_request dma_req;
	struct noa_desc *desc;
	void *entry;

	if (!is_noa_ring_activate(dst_ring)) {
		return -EBUSY;
	}

	NESTED_RING_FOR_EACH_DESC_AND_ENTRY(stage, &req->cached_ring_idx, &req->shadow_ring_addr,
					    shadow_ring_end_addr, desc, entry)
	{
		uintptr_t vendor_desc_addr =
			NepDtcmAddressMap((uintptr_t)desc + sizeof(struct noa_desc));
		if ((idx >= count) && !is_type_matched(entry)) {
			break;
		}
		if (desc->desc_type == NOA_DESC_MODEM_RX_MTK_MSG_PD) {
			if ((idx + 1) >= count) {
				break;
			}
			dma_req.unit[idx].src = vendor_desc_addr;
			dma_req.unit[idx].dst = (uintptr_t)noa_ring_buf_pos(
				dst_ring->basic.base, dst_ring_head, dst_ring->basic.item_len);
			dst_ring_head = noa_ring_move_pos(dst_ring_head, 1, dst_ring->basic.size);
			idx++;
			vendor_desc_addr += ring_item_len;
		}
		dma_req.unit[idx].src = vendor_desc_addr;
		dma_req.unit[idx].dst = (uintptr_t)noa_ring_buf_pos(
			dst_ring->basic.base, dst_ring_head, dst_ring->basic.item_len);
		dst_ring_head = noa_ring_move_pos(dst_ring_head, 1, dst_ring->basic.size);
		idx++;
		src_ring_moved_cnt++;
	}
	if (!src_ring_moved_cnt) {
		return 0;
	}
	src_ring_tail = noa_ring_move_pos(src_ring_tail, src_ring_moved_cnt, src_ring->basic.size);

	req_context->src_ring_instance = (struct ring_manager_instance *)src_ring->owner;
	req_context->dst_notifier = dst_notifier;
	req_context->type = kDmaRouteDesc;

	dma_req.num = idx;
	dma_req.desc_len = ring_item_len;
	dma_req.context = (void *)req;
	dma_req.src_ring_addr = (uintptr_t)src_ring->regs.read;
	dma_req.src_ring_tail = src_ring_tail;
	dma_req.dst_ring_addr = (uintptr_t)dst_ring->regs.write;
	dma_req.dst_ring_head = dst_ring_head;
#ifdef linux
	dma_req.callback = NestedRingRequestCompleteCallback;
	ret = queue_dma_route_data_request(&dma_req);
	if (ret) {
		pr_err("Failed to route ring %s feedthrough data, ret %" PRId32 "\n",
		       src_ring->name, ret);
		return -EAGAIN;
	}
#else /* linux */
	dma_req.callback = [](pw::Status, void *context) {
		NestedRingRequestCompleteCallback(context);
	};
	auto ret = reinterpret_cast<TemplatedDma *>(context->dma)
			   ->InitiateTransfer(
				   TemplatedDma::ProgramToChannel(ProgramType::kRouteDescriptor),
				   ProgramType::kRouteDescriptor, dma_req);
	if (!ret.ok()) {
		PW_LOG_ERROR(
			"Failed to init DMA transfer to route descriptor from %s to %s with ret %s",
			src_ring->name, dst_ring->name, ret.str());
		return -EAGAIN;
	}
#endif /* linux */
  *counter = *counter + src_ring_moved_cnt;
	dst_ring->basic.head = dst_ring_head;
	context->processed_src_ring_tail = src_ring_tail;
	return 0;
}

SEC_FAST static bool IsEmbeddedPath(const void *entry)
{
	switch (((const nep_device_entry *)entry)->pkt_info.action) {
	case PKT_ACTION_UNDECIDED:
	case PKT_ACTION_FEEDTHROUGH:
	case PKT_ACTION_FALLBACK:
		return true;
	default:
		break;
	}
	return false;
}

SEC_FAST static int32_t RouteDataFromAlcedoDevice(NestedRingStage *stage,
					  NestedRingRouteContext *context,
					  NestedRingRequest *req)
{
	nep_host_entry *entry = (nep_host_entry *)req->shadow_ring_addr;

	switch (entry->pkt_info.action) {
	case PKT_ACTION_UNDECIDED:
	case PKT_ACTION_FEEDTHROUGH:
	case PKT_ACTION_FALLBACK:
		return RouteAlcedoVendorDescriptor(stage, context, req, context->feedthrough_ring,
					   context->feedthrough_path_notifier,
					   MAX_ROUTE_FEEDTHROUGH_DESC_NUM, IsEmbeddedPath, context->feedthrough_counter);
	case PKT_ACTION_FILTER_PACKET:
		return RouteDescriptor(stage, context, req, context->feedback_ring,
				       context->feedback_path_notifier, MAX_ROUTE_FEEDBACK_DESC_NUM,
				       NEP_HOST_ENTRY_IS_FILTER_PACKET, FormatFeedbackDesc, context->filter_counter);
	case PKT_ACTION_FORWARD_TO_WLAN:
		if (!context->forward_wlan_ring) {
			pr_err("Src ring %s didn't support forwarding wlan\n",
			       context->src_ring->name);
			return -EINVAL;
		} else if (!g_ring_route_forwarding_enabled) {
			entry->pkt_info.action = PKT_ACTION_FALLBACK;
			return RouteAlcedoVendorDescriptor(
				stage, context, req, context->feedthrough_ring,
				context->feedthrough_path_notifier, MAX_ROUTE_FEEDTHROUGH_DESC_NUM,
				IsEmbeddedPath, context->feedthrough_counter);
		}

		return RouteForwardPacket(
			stage, context, req, context->forward_wlan_ring, context->feedback_ring,
			context->forward_wlan_path_notifier, context->feedback_path_notifier,
			context->wlan_buffer_pool_context, NEP_DEVICE_ENTRY_IS_FORWARD_TO_WLAN,
			NOA_DESC_NETENG_PKT_FLOW_BYTE, FormatForwardToWlanRequest,
			context->forward_to_wlan_counter);
	default:
		pr_err("Invalid nep alcedo device desc action %" PRIu16 "\n",
		       entry->pkt_info.action);
		return -EINVAL;
	}
	return -EINVAL;
}

static bool AlwaysTrue(const void *desc)
{
	(void)desc;
	return true;
}

static int32_t RouteDataErrorHandler(struct NestedRingStage *stage, NestedRingRouteContext *context,
				     NestedRingRequest *req)
{
	const uint32_t count = 1;
	int32_t ret;
	// First, we attempt to transmit the packet using the fallback mechanism.
	pr_warn("Try to send invalid data to fallback ring %s\n", context->feedthrough_ring->name);
	req->cached_ring_idx = req->start_cached_ring_idx;
	req->shadow_ring_addr = req->start_shadow_ring_addr;
	ret = RouteDescriptor(stage, context, req, context->feedthrough_ring,
			      context->feedthrough_path_notifier, count, AlwaysTrue,
			      FormatFallbackDesc, context->fallback_counter);
	if (!ret) {
		return 0;
	}
	// Next, we try to recycle the buffer via the feedback mechanism
	pr_warn("Try to recycle invalid data to feedback ring %s\n", context->feedback_ring->name);
	req->cached_ring_idx = req->start_cached_ring_idx;
	req->shadow_ring_addr = req->start_shadow_ring_addr;
	ret = RouteDescriptor(stage, context, req, context->feedback_ring,
			      context->feedback_path_notifier, count, AlwaysTrue,
			      FormatFeedbackDesc, context->filter_counter);
	if (!ret) {
		return 0;
	}
	// Finally, if both fail, the packet is dropped entirely.
	pr_warn("Drop data from src ring %s\n", context->src_ring->name);
	return DropData(stage, context, req);
}

SEC_FAST int32_t NestedRingRouteData(NestedRingStage *stage, NestedRingRequest *req)
{
	int32_t ret;
	NestedRingRouteContext *context = (NestedRingRouteContext *)stage->context;

	ret = context->route_data(stage, context, req);

	if (ret) {
		if (ret == -EAGAIN) {
			return ret;
		}
		return RouteDataErrorHandler(stage, context, req);
	}

	return 0;
}

NestedRingEngine *NestedRingRouteEngineSingletonGet(void)
{
	SEC_FAST_DATA static NestedRingEngine engine;
	return &engine;
}

#ifdef linux
#define ROUTE_ENGINE_REQUEST_NUM (8U)
#else /* linux */
#define ROUTE_ENGINE_REQUEST_NUM (2U)
#endif /* linux */

int32_t NestedRingRouteEngineInit(NestedRingEngine *engine, NepBitmapTaskScheduler *scheduler,
				  NestedRingTask *post_complete_task)
{
	SEC_FAST_DATA static NestedRingRequestPool req_pool;
	SEC_FAST_DATA static NestedRingRequest req_arr[ROUTE_ENGINE_REQUEST_NUM];
	SEC_FAST_DATA static RouteDataRequestContext context_arr[ROUTE_ENGINE_REQUEST_NUM];
	RingDataRouteStats *stats = RingRouteGetStats();
	uint32_t i = 0;
	uint32_t j = 0;

	memset(&stats, 0, sizeof(stats));
	NestedRingRequestPoolInit(&req_pool, ROUTE_ENGINE_REQUEST_NUM, req_arr);
	memset(context_arr, 0, sizeof(context_arr));
	for (i = 0; i < ROUTE_ENGINE_REQUEST_NUM; i++) {
		RouteDataRequestContext *context = &context_arr[i];
		for (j = 0; j < MAX_ROUTE_FEEDBACK_DESC_NUM; j++) {
			struct noa_desc *desc = &context->feedback[j];
			desc->mode = NOAD_MODE_FEEDBACK;
			desc->ddone = 1;
			desc->src =
				NoaRingPathIdConvert(kNoaNetworkInterfaceNetengine,
						     kNoaNetengineTunnel, kNoaNetengineRingData);
			desc->desc_type = NOA_DESC_BASIC;
			desc->reason = FWD_REASON_NETENGINE;
		}
		for (j = 0; j < MAX_ROUTE_FORWARD_DESC_NUM; j++) {
			struct noa_desc *desc = GetForwardDescriptor(context, j);
			desc->mode = NOAD_MODE_DATA;
			desc->ddone = 1;
			desc->src =
				NoaRingPathIdConvert(kNoaNetworkInterfaceNetengine,
						     kNoaNetengineTunnel, kNoaNetengineRingData);
			desc->reason = FWD_REASON_NETENGINE;
		}
		for (j = 0; j < MAX_ROUTE_VPN_DESC_NUM; j++) {
			struct noa_desc *desc = GetVpnRxDescriptor(context, j);
			desc->mode = NOAD_MODE_DATA;
			desc->ddone = 1;
			desc->src =
				NoaRingPathIdConvert(kNoaNetworkInterfaceNetengine,
						     kNoaNetengineTunnel, kNoaNetengineRingData);
			desc->reason = FWD_REASON_VPN;
		}
		req_arr[i].context = (void *)context;
	}
	NestedRingAsyncEngineInit(engine, NestedRingRouteData, post_complete_task,
				  CompleteRouteData, &req_pool, scheduler);
	return 0;
};

static void ResetRouteContext(void *data)
{
	NestedRingRouteContext *context = (NestedRingRouteContext *)data;
	context->processed_src_ring_tail = noa_ring_tail_read_once(context->src_ring);
}

#ifdef linux
static int32_t StoppingInputRing(struct ring_manager_instance *instance, uint8_t path_id)
{
	int32_t ret;
	(void)path_id;
	if (noa_ring_is_empty(&instance->ring)) {
		noa_ring_deactivate(&instance->ring);
		return 0;
	}
	instance->is_stopping = true;
	wait_event_interruptible_timeout(instance->wait_stopping,
					 noa_ring_is_empty(&instance->ring),
					 msecs_to_jiffies(5000));
	if (!noa_ring_is_empty(&instance->ring)) {
		pr_err("Input Ring %s is not empty it might drop packet head %u tail %u\n",
		       instance->ring.name, noa_ring_head_read_once(&instance->ring),
		       instance->ring.basic.tail);
		ret = -ETIME;
		goto out;
	}
	ret = 0;
	out:
	noa_ring_deactivate(&instance->ring);
	instance->is_stopping = false;
	return ret;
}

static void InputRingStopped(struct ring_manager_instance *instance)
{
	wake_up_interruptible(&instance->wait_stopping);
}
#else /* linux */
static int32_t StoppingInputRing(struct ring_manager_instance *instance, uint8_t)
{
	if (noa_ring_is_empty(&instance->ring)) {
		noa_ring_deactivate(&instance->ring);
		if (instance->stopped_callback) {
			instance->stopped_callback(pw::OkStatus());
		}
		return 0;
	}
	instance->is_stopping = true;
	return 0;
}

static void InputRingStopped(struct ring_manager_instance *instance)
{
	pw::Status ret;

	if (!noa_ring_is_empty(&instance->ring)) {
		pr_err("Input Ring %s is not empty it might drop packet head %u tail %u\n",
		       instance->ring.name, noa_ring_head_read_once(&instance->ring),
		       instance->ring.basic.tail);
		ret = pw::Status::DeadlineExceeded();
		goto out;
	}
	ret = pw::OkStatus();

out:
	noa_ring_deactivate(&instance->ring);
	instance->is_stopping = false;
	if (instance->stopped_callback) {
		instance->stopped_callback(ret);
	}
}
#endif /* linux */

static int32_t RegisterStopHandlers(noa_ring_consumer *src_ring)
{
	struct ring_manager_instance *instance;

	instance = (struct ring_manager_instance *)src_ring->owner;
	if (!instance) {
		return -EINVAL;
	}

	instance->stopping = StoppingInputRing;
	instance->stopped = InputRingStopped;
#ifdef linux
	init_waitqueue_head(&instance->wait_stopping);
#endif /* linux */

	return 0;
}

int32_t NestedRingRouteStageSetup(const NestedRingRouteStageSetupParams *params)
{
	int32_t ret;
	NestedRingStage *stage = params->stage;
	NestedRingEngine *engine = params->engine;
	NestedRingRouteContext *context = params->route_context;
	noa_ring_consumer *src_ring;
	noa_ring_producer *feedthrough_ring;
	noa_ring_producer *feedback_ring;
	noa_ring_producer *forward_wlan_ring = NULL;
	noa_ring_producer *forward_modem_ring = NULL;
	noa_ring_producer *vpn_ring = NULL;

	src_ring = NoaRingManagerRingGetById(params->src_path_id, kNoaRingNepInput);
	if (!src_ring) {
		pr_err("Invalid src ring %" PRIu16 " when setup route data stage\n",
		       params->src_path_id);
		return -EINVAL;
	}

	ret = RegisterStopHandlers(src_ring);
	if (ret) {
		pr_err("Failed to register stop handlers on src ring %s\n", src_ring->name);
		return ret;
	}

	feedthrough_ring =
		NoaRingManagerRingGetById(params->feedthrough_path_id, kNoaRingNepOutput);
	if (!feedthrough_ring) {
		pr_err("Invalid AP ring %" PRIu16 " when setup route data stage\n",
		       params->feedthrough_path_id);
		return -EINVAL;
	}

	feedback_ring = NoaRingManagerRingGetById(params->feedback_path_id, kNoaRingNepOutput);
	if (!feedback_ring) {
		pr_err("Invalid feedback ring %" PRIu16 " when setup route data stage\n",
		       params->feedback_path_id);
		return -EINVAL;
	}

	if (params->support_forward_wlan) {
		forward_wlan_ring =
			NoaRingManagerRingGetById(params->forward_wlan_path_id, kNoaRingNepOutput);
		if (!forward_wlan_ring) {
			pr_err("Invalid forward wlan ring %" PRIu16
			       " when setup route data stage\n",
			       params->forward_wlan_path_id);
			return -EINVAL;
		}
		context->wlan_buffer_pool_context = params->wlan_buffer_pool_context;
	}

	if (params->support_forward_modem) {
		forward_modem_ring =
			NoaRingManagerRingGetById(params->forward_modem_path_id, kNoaRingNepOutput);
		if (!forward_modem_ring) {
			pr_err("Invalid forward modem ring %" PRIu16
			       " when setup route data stage\n",
			       params->forward_modem_path_id);
			return -EINVAL;
		}
		context->modem_buffer_pool_context = params->modem_buffer_pool_context;
	}

	if (params->from_device) {
		if (params->use_vendor_ring) {
			context->route_data = RouteDataFromAlcedoDevice;
		} else {
			context->route_data = RouteDataFromDevice;
		}
	} else {
		context->route_data = RouteDataFromHost;
	}
	context->src_ring = src_ring;
	context->feedthrough_ring = feedthrough_ring;
	context->feedback_ring = feedback_ring;
	context->forward_wlan_ring = forward_wlan_ring;
	context->forward_modem_ring = forward_modem_ring;
	context->feedthrough_path_notifier = params->feedthrough_path_notifier;
	context->dma = params->dma;
#ifndef linux
	PW_CHECK_NOTNULL(context->dma);
#endif /* linux */
	context->feedback_path_notifier = params->feedback_path_notifier;
	context->forward_wlan_path_notifier = params->forward_wlan_path_notifier;
	context->forward_modem_path_notifier = params->forward_modem_path_notifier;
	if (params->support_vpn) {
		vpn_ring = NoaRingManagerRingGetById(params->vpn_path_id, kNoaRingNepOutput);
		if (!vpn_ring) {
			pr_err("Invalid VPN ring %" PRIu16 " when setup route data stage\n",
			       params->vpn_path_id);
			return -EINVAL;
		}

		context->vpn_ring = vpn_ring;
		context->vpn_path_notifier = params->vpn_path_notifier;
		context->format_vpn_rx_desc = params->format_vpn_rx_desc;
		if (params->from_device && !context->format_vpn_rx_desc) {
			pr_err("Tx path should setup the VPN formator\n");
			return -EAGAIN;
		}
	}
  SetupRingDataRouteStats(params, context);

	NestedRingStageInit(stage, true, *params->shadow_ring_info, *params->cached_ring_info,
			    context, engine, params->next_stage, params->task, params->name,
			    ResetRouteContext);
	return 0;
}

static void SetupRingDataRouteStats(const NestedRingRouteStageSetupParams *params, NestedRingRouteContext *context)
{
  RingDataRouteStats *stats = RingRouteGetStats();

  switch (params->network_type) {
    case kNoaNetworkInterfaceWlan:
      if (params->from_device) {
        context->feedthrough_counter = &(stats->feedthrough_to_wlan_host);
        context->fallback_counter = &(stats->fallback_to_wlan_host);
        context->filter_counter = &(stats->wlan_filter_packet);
        context->vpn_counter = &(stats->vpn_to_wlan_host);
        context->forward_to_wlan_counter = &(stats->forward_to_wlan);
        context->forward_to_modem_counter = &(stats->forward_to_modem);
      } else {
        context->feedthrough_counter = &(stats->feedthrough_to_wlan_device);
        context->fallback_counter = &(stats->feedthrough_to_wlan_device);
        context->filter_counter = &(stats->wlan_filter_packet);
        context->vpn_counter = &(stats->vpn_to_wlan_device);
        context->forward_to_wlan_counter = NULL;
        context->forward_to_modem_counter = NULL;
      }
      break;
    case kNoaNetworkInterfaceModem:
      if (params->from_device) {
        context->feedthrough_counter = &(stats->feedthrough_to_modem_host);
        context->fallback_counter = &(stats->fallback_to_modem_host);
        context->filter_counter = &(stats->modem_filter_packet);
        context->vpn_counter = &(stats->vpn_to_modem_host);
        context->forward_to_wlan_counter = &(stats->forward_to_wlan);
        context->forward_to_modem_counter = &(stats->forward_to_modem);
      } else {
        context->feedthrough_counter = &(stats->feedthrough_to_modem_device);
        context->fallback_counter = &(stats->feedthrough_to_modem_device);
        context->filter_counter = &(stats->modem_filter_packet);
        context->vpn_counter = &(stats->vpn_to_modem_device);
        context->forward_to_wlan_counter = NULL;
        context->forward_to_modem_counter = NULL;
      }
      break;
    default:
      WARN_ON(1);
  }
}

RingDataRouteStats *RingRouteGetStats(void)
{
	SEC_FAST_DATA static RingDataRouteStats stats;
	return &stats;
}
