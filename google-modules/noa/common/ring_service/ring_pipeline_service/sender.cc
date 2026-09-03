// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of Ring Pipeline Service Sender
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifdef linux
#include "ring_pipeline_service/service.h"
#include "ring_pipeline_service/sender.h"

#include <linux/printk.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#include <common/core.h>
#include <common/ring.h>
#include <common/ring_id.h>
#include <common/inttypes.h>

#include "nep.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "port.h"
#include "ring_manager.h"
#include "ring_manager_instance.h"
#else /* linux */
#include "ring_pipeline_service/service.h"
#include "ring_pipeline_service_private/sender.h"

#include <cerrno>
#include <cstdint>
#include <cinttypes>

#include "arch/memory.h"
#include "common/core.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "linux_port/container_of.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "noa_configs/noa_configs.h"
#include "notifier/notifier_mailbox.h"
#include "pw_status/status.h"
#include "ring_mgmt/ring_manager.h"
#include "ring_mgmt/ring_manager_instance.h"
#endif /* linux */
static int32_t SendDescriptorHelper(struct NepRingSender *sender, struct NepPacketContext *packet);

static inline void SendDoorbell(struct DoorbellRingGroup *group)
{
#ifdef linux
	group->port->ints |= 1;
	noa_sim_trig_tx();
#else /* linux */
	group->notifier.Notify();
#endif /* linux */
}

int32_t NepSenderFlushRingTask(struct NepRunnableTask *task, void *data)
{
	uint8_t g;
	struct NepRingSender *sender = container_of(task, struct NepRingSender, task);

	for (g = 0; g < sender->num_of_group; ++g) {
		uint8_t i;
		struct DoorbellRingGroup *group = &sender->ring_group[g];
		if (!group->ring_dirty_bitmap) {
			continue;
		}
		for (i = 0; i < group->num_of_ring; ++i) {
			const uint32_t bit = 1U << i;
			struct noa_ring_wrapper *ring =
				&group->rings->entries[i].entries[kNoaRingNepOutput].ring;
			if (!(group->ring_dirty_bitmap & bit)) {
				continue;
			}
#ifndef linux
			if (ring->basic.tail < ring->basic.head) {
				CleanDCache(noa_ring_curr_tail_pos(&ring->basic),
					    (ring->basic.head - ring->basic.tail) *
						    ring->basic.item_len);
			} else {
				CleanDCache(noa_ring_curr_tail_pos(&ring->basic),
					    (ring->basic.size - ring->basic.tail) *
						    ring->basic.item_len);
				CleanDCache(ring->basic.base, ring->basic.head * ring->basic.item_len);
			}
#endif /* linux */
			noa_ring_head_write_once(ring, ring->basic.head);
		}
		group->ring_dirty_bitmap = 0U;
		SendDoorbell(group);
	}
	return 0;
}

static struct ring_manager_instance *GetRingInstance(struct NoaRingManagerInfoRoot *root,
						     uint8_t interface, uint8_t flow, uint8_t type)
{
	if (!root || interface >= root->num || flow >= root->entries[interface]->num ||
	    type >= root->entries[interface]->entries[flow]->num) {
		pr_err("Failed to get ring instance with interface %" PRIu16 ", flow %" PRIu16
		       ", type %" PRIu16 "\n",
		       interface, flow, type);
		return NULL;
	}
	return &root->entries[interface]->entries[flow]->entries[type].entries[kNoaRingNepOutput];
}

static void RecycleBuffer(struct NepRingSender *sender, struct NepPacketContext *packet)
{
	int32_t ret;

	if (!NepHasBufferToRecycled(packet, kUseOriginalPacket)) {
		return;
	}

	OriginalPacketRecycleDescriptorFormat(packet);

	ret = SendDescriptorHelper(sender, packet);
	if (ret) {
		return;
	}
	NepPacketContextResetBuffer(packet, kUseOriginalPacket);
}

static void CheckOutputRingIsStopping(struct NepRingSender *sender, uint8_t dst)
{
	uint8_t interface;
	uint8_t flow;
	uint8_t type;
	struct noa_ring_wrapper *ring;
	struct DoorbellRingGroup *group;
	struct ring_manager_instance *instance;

	NoaRingPathIdParse(dst, &interface, &flow, &type);
	instance = GetRingInstance(sender->ring_root, interface, flow, type);
	if (!instance || !instance->ring.owner) {
		pr_err("Failed to get output ring with interface %" PRIu16 ", flow %" PRIu16
		       ", category %" PRIu16 "\n",
		       interface, flow, type);
		return;
	} else if (!is_noa_ring_activate(&instance->ring)) {
		return;
	}

	ring = &instance->ring;
	group = (struct DoorbellRingGroup *)ring->owner;

	if (instance->is_stopping &&
	    NoPacketSendingToDestination(sender->scheduler->pkt_table, dst)) {
		noa_ring_head_write_once(ring, ring->basic.head);
		SendDoorbell(group);
		instance->stopped(instance);
	}
}

int32_t SendDescriptor(struct NepEngine *engine, struct NepProcessingRequest *request)
{
	int32_t ret;
	struct NepRingSender *sender = container_of(engine, struct NepRingSender, engine);
	struct NepPacketContext *packet = request->packet;

	ret = SendDescriptorHelper(sender, packet);
	if (ret) {
		return ret;
	}
	NepPacketContextResetBuffer(packet, packet->use_packet_type);
	if (packet->use_packet_type != kUseOriginalPacket) {
		packet->should_recycle_original_buffer = true;
		RecycleBuffer(sender, packet);
	}

	CheckOutputRingIsStopping(sender, packet->dst);

	return 0;
}

static inline bool IsVendorDescriptor(const uint8_t desc_type, uint32_t *offset, uint32_t *size)
{
	switch (desc_type) {
		case NOA_DESC_MODEM_RX_MTK_MSG_PD:
			*offset = sizeof(struct noa_desc);
			*size = NOA_DESC_MODEM_RX_MTK_MSG_PD_BYTE - sizeof(struct noa_desc);
			return true;
		case NOA_DESC_MODEM_RX_MTK_PD:
			*offset = sizeof(struct noa_desc);
			*size = NOA_DESC_MODEM_RX_MTK_PD_BYTE - sizeof(struct noa_desc);
			return true;
		default:
			break;
	}
	return false;
}

static int32_t SendDescriptorHelper(struct NepRingSender *sender, struct NepPacketContext *packet)
{
#ifndef linux
	using std::min;
#endif /* linux */
	uint8_t interface;
	uint8_t flow;
	uint8_t type;
	struct noa_ring_wrapper *ring;
	struct DoorbellRingGroup *group;
	uint8_t type_bit;
	struct noa_desc *desc;
	struct ring_manager_instance *instance;
	uint32_t vendor_desc_offset;
	uint32_t vendor_desc_size;

	NoaRingPathIdParse(packet->dst, &interface, &flow, &type);
	instance = GetRingInstance(sender->ring_root, interface, flow, type);

	if (!instance || !instance->ring.owner) {
		pr_err("Failed to get output ring with interface %" PRIu16 ", flow %" PRIu16
		       ", category %" PRIu16 "\n",
		       interface, flow, type);
		return -EINVAL;
	} else if (!is_noa_ring_activate(&instance->ring)) {
		// To prevent overwhelming the pipeline service, we should drop packets when
		// a ring is inactive instead of returning an EAGAIN error.  Since there's no
		// guarantee the ring will reactivate, continuously retrying to send the packet
		// could cause the service to hang.
		pr_err("Output ring %s is not activate\n", instance->ring.name);
		return -EINVAL;
	}

	ring = &instance->ring;
	type_bit = 1U << type;
	group = (struct DoorbellRingGroup *)ring->owner;
	if (!(group->ring_dirty_bitmap & type_bit)) {
		ring->basic.tail = noa_ring_tail_read_once(ring);
	}

	if (__noa_ring_is_full(ring->basic.head, ring->basic.tail, ring->basic.size)) {
		return -EAGAIN;
	}

	desc = (struct noa_desc *)&packet->noa_desc[0];
	desc->dl = packet->packet_buffer[packet->use_packet_type].dl;
	desc->head_offset = packet->packet_buffer[packet->use_packet_type].head_offset;
	desc->tkid = packet->packet_buffer[packet->use_packet_type].tkid;
	desc->dp_high = packet->packet_buffer[packet->use_packet_type].dp_high;
	desc->dp_low = packet->packet_buffer[packet->use_packet_type].dp_low;
#ifdef linux
	desc->dv = packet->packet_buffer[packet->use_packet_type].dv;
#endif /* linux */

	if (IsVendorDescriptor(desc->desc_type, &vendor_desc_offset, &vendor_desc_size)) {
		uint16_t free_item_to_end = noa_ring_free_items_to_end_count(ring->basic.head, ring->basic.tail, ring->basic.size);
		uint16_t free_item = noa_ring_free_items_count(ring->basic.head, ring->basic.tail, ring->basic.size);
		uint16_t total_count = vendor_desc_size / ring->basic.item_len;
		uint16_t count = min(free_item_to_end, total_count);
		char *vendor_desc = (char *)desc + vendor_desc_offset;
		// The vendor descriptor being written must be an integer multiple of `item_len`.
		WARN_ON(vendor_desc_size % ring->basic.item_len);

		if (total_count > free_item) {
			return -EAGAIN;
		}

		memcpy(noa_ring_curr_head_pos(&ring->basic), vendor_desc, count * ring->basic.item_len);
		vendor_desc += count * ring->basic.item_len;
		count = total_count - count;
		if (count) {
			memcpy(ring->basic.base, vendor_desc, count * ring->basic.item_len);
		}
		noa_ring_info_head_move(&ring->basic, total_count);
	} else {
		memcpy(noa_ring_curr_head_pos(&ring->basic), desc, ring->basic.item_len);
		noa_ring_info_head_move(&ring->basic, 1);
	}

	get_nep_stat()->reason[desc->reason]++;
	get_nep_stat()->mode[desc->mode]++;
	get_nep_stat()->dst[group->id]++;

	group->ring_dirty_bitmap |= type_bit;
	NepRunnableTaskSchedule(&sender->task, sender->scheduler);
	return 0;
}


static const struct noa_ring_ops ring_sender_ops = {
	.payload_len = NULL,
	.read_payload = NULL,
	.fill_noop = NULL,
	.write_payload = NULL,
	.begin_hook = NULL,
	.complete_prehook = NULL,
	.complete_hook = NULL,
};

#ifdef linux
static int32_t StoppingOutputRing(struct ring_manager_instance *instance, uint8_t path_id)
{
	int32_t ret;
	struct NepPacketTable *pkt_table = (struct NepPacketTable *)instance->stop_context;
	if (NoPacketSendingToDestination(pkt_table, path_id)) {
		noa_ring_deactivate(&instance->ring);
		return 0;
	}
	instance->is_stopping = true;
	wait_event_interruptible_timeout(instance->wait_stopping, !instance->is_stopping,
					 msecs_to_jiffies(5000));
	if (!NoPacketSendingToDestination(pkt_table, path_id)) {
		pr_err("Output Ring %s is not empty it might drop packet head %u tail %u\n",
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

static void OutputRingStopped(struct ring_manager_instance *instance)
{
	instance->is_stopping = false;
	wake_up_interruptible(&instance->wait_stopping);
}
#else /* linux */
static int32_t StoppingOutputRing(struct ring_manager_instance *instance, uint8_t path_id)
{
	struct NepPacketTable *pkt_table = (struct NepPacketTable *)instance->stop_context;
	if (NoPacketSendingToDestination(pkt_table, path_id)) {
		noa_ring_deactivate(&instance->ring);
		if (instance->stopped_callback) {
			instance->stopped_callback(pw::OkStatus());
		}
		return 0;
	}
	instance->is_stopping = true;
	return 0;
}

static void OutputRingStopped(struct ring_manager_instance *instance)
{
	noa_ring_deactivate(&instance->ring);
	instance->is_stopping = false;
	if (instance->stopped_callback) {
		instance->stopped_callback(pw::OkStatus());
	}
}
#endif /* linux */

static int32_t InitOutputRingInstance(struct ring_manager_instance *instance,
				      const uint8_t group_id, const uint8_t interface,
				      const uint8_t flow, const uint8_t type, void *owner,
				      struct NepPacketTable *pkt_table)
{
	int32_t ret;
	struct noa_ring_regs regs = { 0 };

	ret = NoaRingSharedRegsGet(&regs, interface, flow, type, kNoaRingNepOutput);
	if (ret) {
		pr_err("Failed to get regs with interface %" PRIu16 ", flow %" PRIu16
		       ", type %" PRIu16 ", ret %" PRId32 "\n",
		       interface, flow, type, ret);
		return ret;
	}
	ret = noa_ring_regs_wrapper_init(&instance->ring, NOA_RING_TYPE_PRODUCER, &ring_sender_ops,
					 &regs, owner, instance->name, 0);
	if (ret) {
		pr_err("Failed to init ring wrapper with interface %" PRIu16 ", flow %" PRIu16
		       ", type %" PRIu16 ", ret %" PRId32 "\n",
		       interface, flow, type, ret);
		return ret;
	}
	instance->is_stopping = false;
	instance->stopping = StoppingOutputRing;
	instance->stopped = OutputRingStopped;
	instance->stop_context = (void *)pkt_table;
#ifdef linux
	init_waitqueue_head(&instance->wait_stopping);
#endif /* linux */

	return 0;
}

#ifdef linux
static void DoorbellTask(unsigned long data)
{
	struct noa_port *port = (struct noa_port *)data;

	if (port->isr) {
		port->isr(port->rcv_irq, port->priv);
	}
}
#endif /* linux */

static int32_t RegisterDoorbell(struct DoorbellRingGroup *group)
{
#ifdef linux
	struct noa_port *port = noa_sim_get_port(group->id);
	if (!port) {
		pr_err("Failed to get port with group id %" PRIu16 "\n", group->id);
		return -EINVAL;
	}
	group->port = port;
	tasklet_init(&port->doorbell_task, DoorbellTask, (unsigned long)port);
#else /* linux */
	using ::noa::module::notifier::kSender;
	using ::noa::module::notifier::NotificationHandler;
	using ::noa::module::notifier::NotifierMailbox;

	int32_t ret;

	auto mailbox_id = noa::module::noa_configs::NoaConfigs::Instance().MailboxId(group->id);
	if (!mailbox_id.has_value()) {
		pr_err("Failed to get mailbox with group id %" PRIu16 "\n", group->id);
		return -EINVAL;
	}
	ret = group->notifier.Init("ring sender", kSender, *mailbox_id);
	if (ret) {
		pr_err("Failed to init send notifier on ring group %" PRIu16 ", err: %" PRId32 "\n",
		       group->id, ret);
		return ret;
	}
#endif /* linux */
	return 0;
}

static int32_t InitDoorbellRingGroup(struct DoorbellRingGroup *group, const uint8_t id,
				     const uint8_t interface, const uint8_t flow,
				     struct NoaRingManagerInfoFlow *rings,
				     struct NepPacketTable *pkt_table)
{
	uint8_t i;
	int32_t ret;

	group->id = id;
	group->num_of_ring = rings->num;
	group->ring_dirty_bitmap = 0;
	group->rings = rings;

	for (i = 0; i < group->num_of_ring; ++i) {
		struct ring_manager_instance *instance =
			&group->rings->entries[i].entries[kNoaRingNepOutput];
		ret = InitOutputRingInstance(instance, id, interface, flow, i, group, pkt_table);
		if (ret) {
			pr_err("Failed to init ring receiver isr, err: %" PRId32 "\n", ret);
			goto out;
		}
	}

	ret = RegisterDoorbell(group);
	if (ret) {
		pr_err("Failed to register ring receiver isr, err: %" PRId32 "\n", ret);
		goto out;
	}

	ret = 0;
out:
	return ret;
}

struct NepRingSender *RingSenderSingletonGet(void)
{
	static struct DoorbellRingGroup group[NOA_PORT_MAX];
	static struct NepRingSender sender = {
		.num_of_group = sizeof(group) / sizeof(group[0]),
		.engine = __INIT_NEP_ENGINE(sender.engine, NULL, SendDescriptor),
		.ring_group = &group[0],
		.ring_root = NULL,
	};
	return &sender;
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

static void GroupIdToNetworkFlowPath(const uint8_t group_id, uint8_t *interface, uint8_t *flow)
{
	switch (group_id) {
	case NOA_PORT_WLAN_FW:
		*interface = kNoaNetworkInterfaceWlan;
		*flow = kNoaNetworkFlowHostToDevice;
		break;
	case NOA_PORT_WLAN_SW:
		*interface = kNoaNetworkInterfaceWlan;
		*flow = kNoaNetworkFlowDeviceToHost;
		break;
	case NOA_PORT_MODEM_FW:
		*interface = kNoaNetworkInterfaceModem;
		*flow = kNoaNetworkFlowHostToDevice;
		break;
	case NOA_PORT_MODEM_SW:
		*interface = kNoaNetworkInterfaceModem;
		*flow = kNoaNetworkFlowDeviceToHost;
		break;
	case NOA_PORT_NETENGINE:
		*interface = kNoaNetworkInterfaceNetengine;
		*flow = kNoaNetengineTunnel;
		break;
	default:
		break;
	}
}

int32_t RingSenderInit(struct NepRingSender *sender, struct NepTaskScheduler *scheduler,
		       struct NoaRingManagerInfoRoot *ring_root)
{
	uint8_t i;

	if (!sender || !ring_root) {
		return -EINVAL;
	}

	NepEngineAddToScheduler(&sender->engine, scheduler);
	NepRunnableTaskConstructor(&sender->task, NepSenderFlushRingTask, NULL);
	sender->scheduler = scheduler;
	sender->ring_root = ring_root;
	for (i = 0; i < sender->num_of_group; ++i) {
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
		ret = InitDoorbellRingGroup(&sender->ring_group[i], i, interface, flow, rings,
					    scheduler->pkt_table);
		if (ret) {
			return ret;
		}
	}
	return 0;
}

int32_t RingServiceSenderSetup(struct NepTaskScheduler *scheduler,
			       struct NoaRingManagerInfoRoot *ring_root)
{
	struct NepRingSender *sender = RingSenderSingletonGet();
	return RingSenderInit(sender, scheduler, ring_root);
}

struct NepEngine *RingServiceSenderEngineGet(void)
{
	return &RingSenderSingletonGet()->engine;
}
