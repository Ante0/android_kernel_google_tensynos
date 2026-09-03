// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of Ring Pipeline Service Receiver
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "ring_pipeline_service/service.h"
#include "ring_pipeline_service/receiver.h"

#include <linux/printk.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#include <common/core.h>
#include <common/ring.h>
#include <common/ring_id.h>
#include <common/inttypes.h>

#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/packet_initiator.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "nep.h"
#include "port.h"
#include "ring_manager.h"
#include "ring_manager_instance.h"
#include "ring_service/ring_buffer_pool.h"
#else /* linux */
#include "ring_pipeline_service/service.h"
#include "ring_pipeline_service_private/receiver.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "common/core.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "linux_port/container_of.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/packet_initiator.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "noa_configs/noa_configs.h"
#include "notifier/notifier_mailbox.h"
#include "pw_status/status.h"
#include "ring_mgmt/ring_manager.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "ring_mgmt/ring_buffer_pool.h"
#endif /* linux */

#define SIZE_BIT_FOR_RING_RECEIVER_INTIATOR (7U) // 2^7 = 128 packets

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

int32_t RingReceiverIsr(void *context)
{
	unsigned long flags = 0;
	struct NetworkInterfaceRingGroup *group = (struct NetworkInterfaceRingGroup *)context;
	if (!group) {
		return -EINVAL;
	}

	spin_lock_irqsave(&group->doorbell_lock, flags);
	group->doorbell_status_register |= (1 << group->num_of_ring) - 1;
	spin_unlock_irqrestore(&group->doorbell_lock, flags);
	NepPacketInitiatorSchedule(group->initiator);
	return 0;
}

#ifdef linux
static void RingReceiverIsrWrapper(unsigned long context)
{
	RingReceiverIsr((void *)context);
}

#endif /* linux */

static int32_t RegisterRingReceiverIsr(struct NetworkInterfaceRingGroup *group)
{
#ifdef linux
	struct noa_port *port = noa_sim_get_port(group->id);
	if (!port) {
		pr_err("Failed to get port with group id %" PRIu16 "\n", group->id);
		return -EINVAL;
	}
	tasklet_init(&port->input_task, RingReceiverIsrWrapper, (unsigned long)group);
#else /* linux */
	using ::noa::module::notifier::kReceiver;
	using ::noa::module::notifier::NotificationHandler;
	using ::noa::module::notifier::NotifierMailbox;

	int32_t ret;

	auto mailbox_id = noa::module::noa_configs::NoaConfigs::Instance().MailboxId(group->id);
	if (!mailbox_id.has_value()) {
		pr_err("Failed to get mailbox with group id %" PRIu16 "\n", group->id);
		return -EINVAL;
	}
	ret = group->notifier.Init("ring receiver", kReceiver, *mailbox_id);
	if (ret) {
		pr_err("Failed to init notifier on ring group %" PRIu16 ", err: %" PRId32 "\n",
		       group->id, ret);
		return ret;
	}
	ret = group->notifier.RegisterNotificationHandler(RingReceiverIsr, group);
	if (ret) {
		pr_err("Failed to register notifier on ring group %" PRIu16 ", err: %" PRId32 "\n",
		       group->id, ret);
		return ret;
	}
#endif /* linux */
	return 0;
}

static int32_t BypassEngine(struct NepEngine *engine, struct NepProcessingRequest *request)
{
	(void)engine;
	(void)request;
	return 0;
}

static ssize_t ReadNoaDescriptor(const void *data, size_t data_len, struct noa_iovec *iov)
{
	memcpy(iov->base, data, data_len);
	return data_len;
}

static void SwitchToNextRing(struct NepRingReceiver *receiver,
			     struct NetworkInterfaceRingGroup *group)
{
	group->curr_ring = (group->curr_ring + 1) % group->num_of_ring;
	receiver->curr_group = (receiver->curr_group + 1) % receiver->num_of_group;
}

static int32_t TransferLassenModemPacketsToNewBuffer(struct noa_desc *desc)
{
	int32_t ret;
	noa_buffer_pool_desc buffer_desc;

	ret = noa_ring_service_buffer_get(NOA_RING_SERVICE_BUFFER_POOL_MODEM, &buffer_desc);
	if (ret) {
		if (ret != -EAGAIN) {
			pr_err("Failed to get Lassen Modem buffer ret: %" PRId32 "\n", ret);
		}
		goto out;
	}
	memcpy((void *)buffer_desc.dv, (void *)desc->dv, desc->dl);
	desc->dv = buffer_desc.dv;
	desc->dp_low = buffer_desc.dp_low;
	desc->dp_high = buffer_desc.dp_high;
	desc->tkid = buffer_desc.tkid;
	ret = 0;

out:
	return ret;
}

int32_t RingReceiverFormatPacket(struct NepPacketInitiator *initiator,
				 struct NepPacketContext *context)
{
	int32_t size;
	int32_t ret;
	struct NepRingReceiver *receiver =
		container_of(initiator, struct NepRingReceiver, initiator);
	struct NetworkInterfaceRingGroup *group = &receiver->ring_group[receiver->curr_group];
	struct ring_manager_instance *instance =
		&group->rings->entries[group->curr_ring].entries[kNoaRingNepInput];
	noa_ring_consumer *ring = &instance->ring;
	struct noa_desc *desc;

	ret = noa_ring_begin_processing(ring);
	if (ret <= 0) {
		pr_err("Invalid ring %s status, ret %" PRId32 "\n", ring->name, ret);
		return -EINVAL;
	}
	size = noa_ring_read(ring, &context->noa_desc[0], sizeof(context->noa_desc));
	if (!size) {
		ret = -EAGAIN;
		goto out;
	} else if (ret < 0) {
		pr_err("Failed to read descriptor from ring %s , ret %" PRId32 "\n", ring->name,
		       ret);
		noa_ring_tail_inc(ring);
		ret = size;
		goto out;
	}
	desc = (struct noa_desc *)&context->noa_desc[0];
	context->src = context->dst = group->rings->entries[group->curr_ring].path_id;
	// This is a special case for Lassen modem drivers. The Lassen driver on the AP
	// side reclaims buffers from NOA the moment the ring's read index changes.
	// Therefore, all packets intended for the ring service must be copied into new
	// buffers right away to prevent use-after-free problems.
	if (group->id == NOA_PORT_MODEM_SW && desc->desc_type == NOA_DESC_MODEM_LASSEN) {
		ret = TransferLassenModemPacketsToNewBuffer(desc);
		if (ret) {
			noa_ring_rollback(ring, 1);
			ret = EAGAIN;
			goto out;
		}
	}

	get_nep_stat()->src[group->id]++;

	context->packet_buffer[kUseOriginalPacket].dl = desc->dl;
	context->packet_buffer[kUseOriginalPacket].head_offset = desc->head_offset;
	context->packet_buffer[kUseOriginalPacket].tkid = desc->tkid;
	context->packet_buffer[kUseOriginalPacket].dp_low = desc->dp_low;
	context->packet_buffer[kUseOriginalPacket].dp_high = desc->dp_high;
	context->packet_buffer[kUseOriginalPacket].dv = desc->dv;
	context->use_packet_type = kUseOriginalPacket;
	context->should_recycle_original_buffer = false;

	ret = 0;
out:
	noa_ring_complete_processing(ring);
	if (!ret && instance->is_stopping && noa_ring_is_empty(ring)) {
		instance->stopped(instance);
	}
	SwitchToNextRing(receiver, group);
	return ret;
}

static bool RingHasData(struct NetworkInterfaceRingGroup *group, uint8_t idx,
			noa_ring_consumer *ring)
{
	unsigned long flags = 0;
	const uint32_t bit = 1U << idx;
	bool has_data = false;

	if (!is_noa_ring_activate(ring)) {
		return false;
	}

	has_data = !__noa_ring_is_empty(ring->basic.head, ring->basic.tail);
	if (has_data) {
		goto out;
	}
	has_data = group->doorbell_status_register & bit;
	if (!has_data) {
		goto out;
	}

	// Re-check the ring index before unmasking the doorbell status register to
	// ensure it can remain in polling mode if needed.
	ring->basic.head = noa_ring_head_read_once(ring);
	has_data = !__noa_ring_is_empty(ring->basic.head, ring->basic.tail);
	if (has_data) {
		goto out;
	}

	// To avoid potential race conditions, it need to re-check the ring index after
	// unmasking the doorbell status register.
	spin_lock_irqsave(&group->doorbell_lock, flags);
	group->doorbell_status_register &= ~(bit);
	spin_unlock_irqrestore(&group->doorbell_lock, flags);
	ring->basic.head = noa_ring_head_read_once(ring);
	has_data = !__noa_ring_is_empty(ring->basic.head, ring->basic.tail);
	if (has_data) {
		goto out;
	}

out:
	if (has_data) {
		group->ring_data_bitmap |= bit;
	} else {
		group->ring_data_bitmap &= ~(bit);
	}
	return has_data;
}

static bool RingGroupHasData(struct NetworkInterfaceRingGroup *group)
{
	uint8_t i;
	if (!group->ring_data_bitmap && !group->doorbell_status_register) {
		return false;
	}
	for (i = 0; i < group->num_of_ring; i++) {
		bool has_data = RingHasData(
			group, group->curr_ring,
			&group->rings->entries[group->curr_ring].entries[kNoaRingNepInput].ring);
		if (has_data) {
			return true;
		}
		group->curr_ring = (group->curr_ring + 1) % group->num_of_ring;
	}
	return false;
}

bool RingReceiverHasData(struct NepPacketInitiator *initiator)
{
	uint8_t i;
	struct NepRingReceiver *receiver =
		container_of(initiator, struct NepRingReceiver, initiator);
	for (i = 0; i < receiver->num_of_group; i++) {
		bool has_data = RingGroupHasData(&receiver->ring_group[receiver->curr_group]);
		if (has_data) {
			return true;
		}
		receiver->curr_group = (receiver->curr_group + 1) % receiver->num_of_group;
	}
	return false;
}

static void GroupIdToNetworkFlowPath(const uint8_t group_id, uint8_t *interface, uint8_t *flow)
{
	switch (group_id) {
	case NOA_PORT_WLAN_FW:
		*interface = kNoaNetworkInterfaceWlan;
		*flow = kNoaNetworkFlowDeviceToHost;
		break;
	case NOA_PORT_WLAN_SW:
		*interface = kNoaNetworkInterfaceWlan;
		*flow = kNoaNetworkFlowHostToDevice;
		break;
	case NOA_PORT_MODEM_FW:
		*interface = kNoaNetworkInterfaceModem;
		*flow = kNoaNetworkFlowDeviceToHost;
		break;
	case NOA_PORT_MODEM_SW:
		*interface = kNoaNetworkInterfaceModem;
		*flow = kNoaNetworkFlowHostToDevice;
		break;
	case NOA_PORT_NETENGINE:
		*interface = kNoaNetworkInterfaceNetengine;
		*flow = kNoaNetengineTunnel;
		break;
	default:
		break;
	}
}

static const struct noa_ring_ops ring_receiver_ops = {
	.payload_len = NULL,
	.read_payload = ReadNoaDescriptor,
	.fill_noop = NULL,
	.write_payload = NULL,
	.begin_hook = NULL,
	.complete_prehook = NULL,
	.complete_hook = NULL,
};

static int32_t InitInputRingInstance(struct ring_manager_instance *instance, const uint8_t group_id,
				     const uint8_t interface, const uint8_t flow,
				     const uint8_t type)
{
	int32_t ret;
	struct noa_ring_regs regs = { 0 };

	ret = NoaRingSharedRegsGet(&regs, interface, flow, type, kNoaRingNepInput);
	if (ret) {
		pr_err("Failed to get regs with interface %" PRIu16 ", flow %" PRIu16
		       ", type %" PRIu16 ", ret %" PRId32 "\n",
		       interface, flow, type, ret);
		return ret;
	}
	ret = noa_ring_regs_wrapper_init(&instance->ring, NOA_RING_TYPE_CONSUMER,
					 &ring_receiver_ops, &regs, instance, instance->name, 0);
	if (ret) {
		pr_err("Failed to init ring wrapper with interface %" PRIu16 ", flow %" PRIu16
		       ", type %" PRIu16 ", ret %" PRId32 "\n",
		       interface, flow, type, ret);
		return ret;
	}
	instance->is_stopping = false;
	instance->stopping = StoppingInputRing;
	instance->stopped = InputRingStopped;
#ifdef linux
	init_waitqueue_head(&instance->wait_stopping);
#endif /* linux */

	return 0;
}

static int32_t InitNetworkInterfaceRingGroup(struct NetworkInterfaceRingGroup *group,
					     struct NepPacketInitiator *initiator, const uint8_t id,
					     const uint8_t interface, const uint8_t flow,
					     struct NoaRingManagerInfoFlow *rings)
{
	uint8_t i;
	int32_t ret;

	group->id = id;
	group->curr_ring = 0;
	group->num_of_ring = rings->num;
	group->doorbell_status_register = 0;
	group->ring_data_bitmap = 0;
	group->rings = rings;
	group->initiator = initiator;
	spin_lock_init(&group->doorbell_lock);

	for (i = 0; i < group->num_of_ring; ++i) {
		struct ring_manager_instance *instance =
			&group->rings->entries[i].entries[kNoaRingNepInput];
		ret = InitInputRingInstance(instance, id, interface, flow, i);
		if (ret) {
			pr_err("Failed to init ring receiver isr, err: %" PRId32 "\n", ret);
			goto out;
		}
	}

	ret = RegisterRingReceiverIsr(group);
	if (ret) {
		pr_err("Failed to register ring receiver isr, err: %" PRId32 "\n", ret);
		goto out;
	}

	ret = 0;
out:
	return ret;
}

struct NepRingReceiver *RingReceiverSingletonGet(void)
{
	static INIT_NEP_ENGINE(bypass_engine, NULL, BypassEngine);
	INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(static, receiver_container,
						       SIZE_BIT_FOR_RING_RECEIVER_INTIATOR);
	static INIT_NEP_PACKET_INITIATOR_STAGE(receiver_stage, NULL, &receiver_container.basic,
					       &bypass_engine, NULL);
	static struct NetworkInterfaceRingGroup group[NOA_PORT_MAX];
	static struct NepRingReceiver receiver = {
		.curr_group = 0,
		.num_of_group = sizeof(group) / sizeof(group[0]),
		.initiator = __INIT_NEP_PACKET_INITIATOR(receiver.initiator, &receiver_stage, NULL,
							 RingReceiverHasData,
							 RingReceiverFormatPacket, NULL),
		.ring_group = &group[0],
	};
	return &receiver;
}

static struct NoaRingManagerInfoFlow *GetRingGroup(struct NoaRingManagerInfoRoot *root,
						   uint8_t interface, uint8_t flow)
{
	if (interface >= root->num || flow >= root->entries[interface]->num) {
		pr_err("Failed to get ring flow group with interface %" PRIu16 ", flow %" PRIu16
		       "\n",
		       interface, flow);
		return NULL;
	}
	return root->entries[interface]->entries[flow];
}

int32_t RingReceiverInit(struct NepRingReceiver *receiver, struct NepTaskScheduler *scheduler,
			 struct NoaRingManagerInfoRoot *ring_root)
{
	uint8_t i;

	if (!scheduler || !receiver || !receiver->initiator.stage ||
	    !receiver->initiator.stage->engine) {
		return -EINVAL;
	}
	NepPacketInitiatorAddToScheduler(&receiver->initiator, scheduler);
	NepStageAddToScheduler(receiver->initiator.stage, scheduler);
	NepEngineAddToScheduler(receiver->initiator.stage->engine, scheduler);

	for (i = 0; i < receiver->num_of_group; ++i) {
		int32_t ret;
		uint8_t interface;
		uint8_t flow;
		struct NoaRingManagerInfoFlow *rings;

		if (i == NOA_PORT_NETENGINE) {
			continue;
		}

		GroupIdToNetworkFlowPath(i, &interface, &flow);
		rings = GetRingGroup(ring_root, interface, flow);
		if (!rings) {
			return -EINVAL;
		}
		ret = InitNetworkInterfaceRingGroup(&receiver->ring_group[i], &receiver->initiator,
						    i, interface, flow, rings);
		if (ret) {
			return ret;
		}
	}
	return 0;
}

int32_t RingServiceReceiverSetup(struct NepTaskScheduler *scheduler,
				 struct NoaRingManagerInfoRoot *ring_root,
				 struct NepStageRouter *router)
{
	struct NepRingReceiver *receiver = RingReceiverSingletonGet();
	receiver->initiator.stage->router = router;
	return RingReceiverInit(receiver, scheduler, ring_root);
}
