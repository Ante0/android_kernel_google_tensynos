// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of NEP Pipeline Stage
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifdef linux
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/printk.h>

#include "stage.h"

#include "common/inttypes.h"
#include "common/ring.h"
#include "packet_table.h"
#else /* linux */
#include "network_pipeline_framework/stage.h"

#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstring>

#include "common/ring.h"
#include "linux_port/container_of.h"
#include "linux_port/list.h"
#include "linux_port/log.h"
#include "network_pipeline_framework/packet_table.h"
#endif /* linux */

static inline int32_t RouteToNextStage(struct NepStage *next_stage, uint16_t pkt_id,
				       struct NepStage **busy_stage)
{
	int32_t ret;

	if (!next_stage || !next_stage->container || !next_stage->engine) {
		pr_err("next stage is invalid\n");
		return -EINVAL;
	}

	ret = next_stage->container->input_packet(next_stage->container, pkt_id);
	if (ret) {
		if (ret == -EAGAIN && busy_stage) {
			*busy_stage = next_stage;
		}
		return ret;
	}

	return NepStageHookProcessingEngine(next_stage);
}

int32_t NepStageDirectRouting(struct NepStageRouter *basic, const struct NepPacketContext *packet,
			      uint16_t pkt_id, struct NepStage **busy_stage)
{
	struct NepStageDirectRouter *router =
		container_of(basic, struct NepStageDirectRouter, basic);
	(void)packet;

	if (!basic) {
		pr_err("Invalid router\n");
		return -EINVAL;
	}
	return RouteToNextStage(router->next_stage, pkt_id, busy_stage);
}

void NepStageDirectRouterConstructor(struct NepStageDirectRouter *router,
				     struct NepStage *next_stage)
{
	router->next_stage = next_stage;
	router->basic.routing = NepStageDirectRouting;
}

int32_t NepStageDecisionChainRouting(struct NepStageRouter *basic,
				     const struct NepPacketContext *packet, uint16_t pkt_id,

				     struct NepStage **busy_stage)
{
	struct NepStageDecisionChainRouter *router =
		container_of(basic, struct NepStageDecisionChainRouter, basic);
	struct NepStageRoutingRule *rule = NULL;

	if (!basic) {
		pr_err("Invalid decision chain router\n");
		return -EINVAL;
	}

	for (rule = router->rules; rule; rule = rule->next_rule) {
		if (rule->is_matched(packet)) {
			return RouteToNextStage(rule->next_stage, pkt_id, busy_stage);
		}
	}

	return -EINVAL;
}

void NepStageRoutingRuleConstructor(struct NepStageRoutingRule *rule,
				    bool (*is_matched)(const struct NepPacketContext *packet),
				    struct NepStage *next_stage,
				    struct NepStageRoutingRule *next_rule)
{
	rule->is_matched = is_matched;
	rule->next_stage = next_stage;
	rule->next_rule = next_rule;
}

void NepStageDecisionChainRouterConstructor(struct NepStageDecisionChainRouter *router,
					    struct NepStageRoutingRule *rules)
{
	router->rules = rules;
	router->basic.routing = NepStageDecisionChainRouting;
}

/************************** Single Packet Container ***************************/

uint32_t NepSinglePktContainerSpaceCount(const struct PktContainer *basic)
{
	struct SinglePktContainer *container =
		container_of(basic, struct SinglePktContainer, basic);
	if (!basic) {
		pr_err("Invalid packet container\n");
		return 0;
	} else if (container->pkt_id) {
		return 0;
	}
	return 1;
}

int32_t NepSinglePktContainerInput(struct PktContainer *basic, uint16_t pkt_id)
{
	struct SinglePktContainer *container =
		container_of(basic, struct SinglePktContainer, basic);
	if (!basic) {
		pr_err("Invalid packet container\n");
		return -EINVAL;
	} else if (container->pkt_id) {
		return -EAGAIN;
	}
	container->pkt_id = pkt_id;
	MarkNepStageAwaiting(&container->status);
	return 0;
}

int32_t NepSinglePktContainerGetAwaitingPacket(struct PktContainer *basic,
					       struct PktContainerItem *pkt_item)
{
	int32_t ret;
	struct SinglePktContainer *container =
		container_of(basic, struct SinglePktContainer, basic);

	if (!basic || !pkt_item) {
		pr_err("Invalid argument when getting awaiting packet from single container\n");
		return -EINVAL;
	} else if (!container->pkt_id) {
		pr_err("No awaiting packet in this container\n");
		return -EAGAIN;
	} else if (!IsNepStageAwaiting(container->status)) {
		pr_err("Not an awaiting packet, its status: %u\n", container->status);
		ret = -EFAULT;
		goto out;
	}
	ret = 0;

out:
	pkt_item->index = 0;
	pkt_item->pkt_id = container->pkt_id;
	return ret;
}

int32_t NepSinglePktContainerProcessing(struct PktContainer *basic,
					const struct PktContainerItem pkt_item)
{
	struct SinglePktContainer *container =
		container_of(basic, struct SinglePktContainer, basic);
	(void)pkt_item;

	if (!basic) {
		pr_err("Invalid argument when marking in processing on single container\n");
		return -EINVAL;
	}
	MarkNepStageInProcessing(&container->status);
	return 0;
}

bool NepSinglePktContainerHasAwaitingPacket(const struct PktContainer *basic)
{
	struct SinglePktContainer *container =
		container_of(basic, struct SinglePktContainer, basic);
	if (!basic) {
		return false;
	}

	return IsNepStageAwaiting(container->status);
}

void NepSinglePktContainerComplete(struct PktContainer *basic, struct NepStage *stage,
				   const struct PktContainerItem pkt_item, int32_t pkt_state)
{
	struct SinglePktContainer *container =
		container_of(basic, struct SinglePktContainer, basic);

	if (!basic || !stage || !stage->scheduler) {
		pr_err("Invalid arguments when completing pkt on single container\n");
		return;
	} else if (pkt_state || container->pkt_id != pkt_item.pkt_id) {
		struct NepPacketTable *table = stage->scheduler->pkt_table;
		if (container->pkt_id == pkt_item.pkt_id) {
			pr_err("The pkt %" PRIu16 " is in error state %" PRId32
			       ", we drop the packet\n",
			       pkt_item.pkt_id, pkt_state);
			NepPacketTablePacketFree(table, container->pkt_id);
		} else {
			pr_err("The completed pkt %" PRIu16 " and the one being held %" PRIu16
			       " are different"
			       ", we drop the packet\n",
			       pkt_item.pkt_id, container->pkt_id);
			NepPacketTablePacketFree(table, container->pkt_id);
			NepPacketTablePacketFree(table, pkt_item.pkt_id);
		}
		container->pkt_id = 0;
		MarkNepStageError(&container->status);
		return;
	}

	MarkNepStageCompleted(&container->status);

	if (list_empty(&stage->waiting_hook)) {
		list_add_tail(&stage->waiting_hook, &stage->scheduler->stage_tasks);
	}
}

int32_t NepSinglePktContaineGetCompletedPacket(struct PktContainer *basic, uint16_t *pkt_id)
{
	struct SinglePktContainer *container =
		container_of(basic, struct SinglePktContainer, basic);

	if (!basic || !pkt_id) {
		pr_err("Invalid argument when get completed packet in single pkt container\n");
		return -EINVAL;
	} else if (!container->pkt_id) {
		return 0;
	} else if (!IsNepStageCompleted(container->status)) {
		pr_err("Packet %" PRIu16 " is not completed status %" PRIu16 "\n",
		       container->pkt_id, container->status);
		return -EIO;
	}
	*pkt_id = container->pkt_id;
	return 1;
}

void NepSinglePktContaineDequeuePacket(struct PktContainer *basic)
{
	struct SinglePktContainer *container =
		container_of(basic, struct SinglePktContainer, basic);

	if (!basic) {
		return;
	}
	container->pkt_id = 0;
	MarkNepStageIdle(&container->status);
}

void NepSinglePktContainerReset(struct PktContainer *basic)
{
	NepSinglePktContaineDequeuePacket(basic);
}

void NepSinglePktContainerConstructor(struct SinglePktContainer *container)
{
	container->status = 0;
	container->pkt_id = 0;
	container->basic.space_count = NepSinglePktContainerSpaceCount;
	container->basic.input_packet = NepSinglePktContainerInput;
	container->basic.get_awaiting_packet = NepSinglePktContainerGetAwaitingPacket;
	container->basic.processing = NepSinglePktContainerProcessing;
	container->basic.has_awaiting_packet = NepSinglePktContainerHasAwaitingPacket;
	container->basic.complete = NepSinglePktContainerComplete;
	container->basic.get_completed_packet = NepSinglePktContaineGetCompletedPacket;
	container->basic.dequeue_packet = NepSinglePktContaineDequeuePacket;
	container->basic.reset = NepSinglePktContainerReset;
}

/************************* Multiple Packet Container **************************/

uint32_t NepMultiPktContainerSpaceCount(const struct PktContainer *basic)
{
	struct MultiPktContainer *container = container_of(basic, struct MultiPktContainer, basic);

	if (!basic) {
		pr_err("Invalid packet container\n");
		return 0;
	}
	return noa_bitring_space(container->head, container->tail, container->size_mask);
}

int32_t NepMultiPktContainerInput(struct PktContainer *basic, uint16_t pkt_id)
{
	struct MultiPktContainer *container = container_of(basic, struct MultiPktContainer, basic);

	if (!basic) {
		return -EINVAL;
	} else if (noa_bitring_is_full(container->head, container->tail, container->size_mask)) {
		return -EAGAIN;
	}

	container->arr[container->head].pkt_id = pkt_id;
	MarkNepStageAwaiting(&container->arr[container->head].status);
	container->head = noa_bitring_move_pos(container->head, 1, container->size_mask);
	return 0;
}

int32_t NepMultiPktContainerGetAwaitingPacket(struct PktContainer *basic,
					      struct PktContainerItem *pkt_item)
{
	int32_t ret;
	struct MultiPktContainer *container = container_of(basic, struct MultiPktContainer, basic);

	if (!basic || !pkt_item) {
		pr_err("Invalid argument when getting awaiting packet from multi container\n");
		return -EINVAL;
	} else if (noa_bitring_is_empty(container->head, container->processing, container->size_mask)) {
		pr_err("No awaiting packet in this container\n");
		return -EAGAIN;
	} else if (!IsNepStageAwaiting(container->arr[container->processing].status)) {
		pr_err("Not an awaiting packet, its status: %u\n",
		       container->arr[container->processing].status);
		ret = -EFAULT;
		goto out;
	}
	ret = 0;

out:
	pkt_item->index = container->processing;
	pkt_item->pkt_id = container->arr[container->processing].pkt_id;
	return ret;
}
int32_t NepMultiPktContainerProcessing(struct PktContainer *basic,
				       const struct PktContainerItem pkt_id)
{
	struct MultiPktContainer *container = container_of(basic, struct MultiPktContainer, basic);

	if (!basic) {
		pr_err("Invalid argument when marking packet in processing from multi container\n");
		return -EINVAL;
	}

	MarkNepStageInProcessing(&container->arr[pkt_id.index].status);
	container->processing =
		noa_bitring_move_pos(container->processing, 1, container->size_mask);
	return 0;
}

bool NepMultiPktContainerHasAwaitingPacket(const struct PktContainer *basic)
{
	struct MultiPktContainer *container = container_of(basic, struct MultiPktContainer, basic);

	if (!basic) {
		return false;
	}

	return !noa_bitring_is_empty(container->head, container->processing, container->size_mask);
}

void NepMultiPktContainerComplete(struct PktContainer *basic, struct NepStage *stage,
				  const struct PktContainerItem pkt_item, int32_t pkt_state)
{
	bool has_moved_completed_index = false;
	struct MultiPktContainer *container = container_of(basic, struct MultiPktContainer, basic);
	uint16_t index = pkt_item.index;
	uint16_t pkt_id = pkt_item.pkt_id;

	if (!basic || !stage || !stage->scheduler) {
		pr_err("Invalid arguments when completing pkt on multi container\n");
		return;
	} else if (pkt_state || container->arr[index].pkt_id != pkt_id) {
		struct NepPacketTable *table = stage->scheduler->pkt_table;
		if (container->arr[index].pkt_id == pkt_id) {
			pr_err("The pkt %" PRIu16 " is in error state %" PRId32
			       ", we drop the packet\n",
			       pkt_id, pkt_state);
			NepPacketTablePacketFree(table, pkt_id);
		} else {
			pr_err("The completed pkt %" PRIu16 " and the one being held %" PRIu16
			       " are different"
			       ", we drop the packet\n",
			       pkt_id, container->arr[index].pkt_id);
			NepPacketTablePacketFree(table, container->arr[index].pkt_id);
			NepPacketTablePacketFree(table, pkt_id);
		}
		container->arr[index].pkt_id = 0;
		MarkNepStageError(&container->arr[index].status);
	} else {
		MarkNepStageCompleted(&container->arr[index].status);
	}

	while (!noa_bitring_is_empty(container->processing, container->completed,
				     container->size_mask)) {
		if (IsNepStageInProcessing(container->arr[container->completed].status)) {
			break;
		}
		container->completed =
			noa_bitring_move_pos(container->completed, 1, container->size_mask);
		has_moved_completed_index = true;
	}

	if (has_moved_completed_index && list_empty(&stage->waiting_hook)) {
		list_add_tail(&stage->waiting_hook, &stage->scheduler->stage_tasks);
	}
}

int32_t NepMultiPktContaineGetCompletedPacket(struct PktContainer *basic, uint16_t *pkt_id)
{
	struct MultiPktContainer *container = container_of(basic, struct MultiPktContainer, basic);

	if (!basic || !pkt_id) {
		pr_err("Invalid argument when get completed packet in single pkt container\n");
		return -EINVAL;
	} else if (noa_bitring_is_empty(container->completed, container->tail,
					container->size_mask)) {
		return 0;
	} else if (!IsNepStageCompleted(container->arr[container->tail].status)) {
		pr_err("Packet %" PRIu16 " is not completed status %" PRIu16 "\n",
		       container->arr[container->tail].pkt_id,
		       container->arr[container->tail].status);
		return -EIO;
	}
	*pkt_id = container->arr[container->tail].pkt_id;
	return 1;
}

void NepMultiPktContaineDequeuePacket(struct PktContainer *basic)
{
	struct MultiPktContainer *container = container_of(basic, struct MultiPktContainer, basic);

	if (!basic) {
		return;
	} else if (noa_bitring_is_empty(container->completed, container->tail,
					container->size_mask)) {
		return;
	}
	container->arr[container->tail].pkt_id = 0;
	MarkNepStageIdle(&container->arr[container->tail].status);
	container->tail = noa_bitring_move_pos(container->tail, 1, container->size_mask);
}

void NepMultiPktContainerReset(struct PktContainer *basic)
{
	struct MultiPktContainer *container = container_of(basic, struct MultiPktContainer, basic);
	uint16_t size = 0;

	if (!basic) {
		return;
	}
	container->head = container->tail = container->processing = container->completed = 0;
	size = (uint16_t)container->size_mask + 1U;
	memset(container->arr, 0, size * sizeof(struct PktContainerArrayEntry));
}

void NepMultiPktContainerConstructor(struct MultiPktContainer *container, uint8_t size_bit,
				     struct PktContainerArrayEntry *array)
{
	container->arr = array;
	container->head = 0;
	container->tail = 0;
	container->processing = 0;
	container->completed = 0;
	container->size_mask = (1U << size_bit) - 1;
	container->basic.space_count = NepMultiPktContainerSpaceCount;
	container->basic.input_packet = NepMultiPktContainerInput;
	container->basic.get_awaiting_packet = NepMultiPktContainerGetAwaitingPacket;
	container->basic.processing = NepMultiPktContainerProcessing;
	container->basic.has_awaiting_packet = NepMultiPktContainerHasAwaitingPacket;
	container->basic.complete = NepMultiPktContainerComplete;
	container->basic.get_completed_packet = NepMultiPktContaineGetCompletedPacket;
	container->basic.dequeue_packet = NepMultiPktContaineDequeuePacket;
	container->basic.reset = NepMultiPktContainerReset;
}

/**************************** NepStage Operations *****************************/

void NepStageConstructHelper(struct NepStage *stage, struct NepStageRouter *router,
			     struct PktContainer *container, struct NepEngine *engine,
			     struct NepTaskScheduler *scheduler,
			     void (*post_route)(struct NepStage *stage))
{
	stage->router = router;
	stage->container = container;
	stage->engine = engine;
	INIT_LIST_HEAD(&stage->eng_hook);
	INIT_LIST_HEAD(&stage->waiting_hook);
	INIT_LIST_HEAD(&stage->waiting_list_head);
	stage->scheduler = scheduler;
	INIT_LIST_HEAD(&stage->tracking_hook);
	stage->post_route = post_route;
}

int32_t NepStageHookProcessingEngine(struct NepStage *stage)
{
	struct NepEngine *engine = NULL;
	struct NepTaskScheduler *scheduler = NULL;

	if (!stage || !stage->engine || !stage->scheduler) {
		pr_err("Invalid stage\n");
		return -EINVAL;
	}
	engine = stage->engine;
	scheduler = stage->scheduler;

	if (!list_empty(&stage->eng_hook)) {
		return 0;
	}

	list_add_tail(&stage->eng_hook, &engine->stage_todo_list);

	if (list_empty(&engine->waiting_hook)) {
		list_add_tail(&engine->waiting_hook, &scheduler->engine_tasks);
	}
	return 0;
}

int32_t NepStageRouteCompletedPackets(struct NepStage *stage)
{
	bool has_route_once = false;
	struct NepStageRouter *router = NULL;
	struct PktContainer *container = NULL;
	struct NepPacketTable *table = NULL;

	if (!stage || !stage->router || !stage->container || !stage->scheduler) {
		pr_err("Invalid argument when perform stage routing\n");
		return -EINVAL;
	}
	router = stage->router;
	container = stage->container;
	table = stage->scheduler->pkt_table;

	while (true) {
		int32_t ret;
		struct NepStage *busy_stage = NULL;
		uint16_t completed_pkt_id = 0;
		struct NepPacketContext *packet = NULL;
		bool should_drop = true;

		ret = container->get_completed_packet(container, &completed_pkt_id);
		if (ret < 0) {
			if (ret == -EIO) {
				goto next;
			}
			return ret;
		} else if (!ret) {
			break;
		}

		if (!router->routing) {
			// In the final stage of the service, the packet is dequeued directly.
			goto next;
		}

		ret = NepPacketTablePacketGet(table, completed_pkt_id, &packet);
		if (ret) {
			goto next;
		}

		ret = router->routing(router, packet, completed_pkt_id, &busy_stage);
		NepPacketTablePacketRelease(packet);
		if (ret) {
			if (ret == -EAGAIN && busy_stage) {
				list_del(&stage->waiting_hook);
				list_add_tail(&stage->waiting_hook, &busy_stage->waiting_list_head);
				goto out;
			}
			goto next;
		}
		should_drop = false;

	next:
		if (unlikely(should_drop) && completed_pkt_id) {
			NepPacketTablePacketFree(table, completed_pkt_id);
		}
		container->dequeue_packet(container);
		has_route_once = true;
	}

	list_del_init(&stage->waiting_hook);

out:
	if (has_route_once) {
		stage->post_route(stage);
	}
	return 0;
}
