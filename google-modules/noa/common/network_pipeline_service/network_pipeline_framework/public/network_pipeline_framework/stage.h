/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for NEP Pipeline Stage
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifndef NOA_NETWORK_PIPELINE_FRAMEWORK_STAGE_H
#define NOA_NETWORK_PIPELINE_FRAMEWORK_STAGE_H

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/printk.h>

#include "engine.h"
#include "packet_table.h"
#include "task_scheduler.h"
#else /* linux */
#include <cerrno>
#include <cstdint>

#include "linux_port/list.h"
#include "linux_port/log.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/packet_table.h"
#include "network_pipeline_framework/task_scheduler.h"
#endif /* linux */

#define NEP_STAGE_PKT_CONTEXT_STATUS_MAX_BIT (1U << 3U)
#define NEP_STAGE_PKT_CONTEXT_STATUS_MASK (NEP_STAGE_PKT_CONTEXT_STATUS_MAX_BIT - 1U)

enum NepStagePktContextStatus {
	kIdle = 0,
	kAwaiting,
	kCompleted,
	kInProcessing,
	kError,
	kStatusEnd,
};
static_assert(kStatusEnd <= NEP_STAGE_PKT_CONTEXT_STATUS_MAX_BIT);

#define DEFINE_NEP_STAGE_STATUS_FUNC(name) 			\
static inline void MarkNepStage##name(uint16_t *status) 	\
{ 								\
	*status = (uint16_t)(k##name); 				\
} 								\
static inline bool IsNepStage##name(uint16_t status) 		\
{ 								\
	return (status & NEP_STAGE_PKT_CONTEXT_STATUS_MASK) == (uint16_t)(k##name); \
}
DEFINE_NEP_STAGE_STATUS_FUNC(Idle)
DEFINE_NEP_STAGE_STATUS_FUNC(Awaiting)
DEFINE_NEP_STAGE_STATUS_FUNC(Completed)
DEFINE_NEP_STAGE_STATUS_FUNC(InProcessing)
DEFINE_NEP_STAGE_STATUS_FUNC(Error)

struct NepStage;

struct PktContainer {
	/**
	 * @brief Calculate the available space in the container.
	 *
	 * @param[in] container the PktContainer for which to
	 * calculate the available space.
	 *
	 * @return  The number of free slots in the container.
	 */
	uint32_t (*space_count)(const struct PktContainer *);
	/**
	 * @brief Add a PacketContext to the container.
	 *
	 * @param[in] container  The PktContainer that the
	 * PacketContext should be added.
	 * @param[in] pkt_id  The ID of the PacketContext to
	 * be added to the container.
	 *
	 * @return  The result of the operation.
	 * @retval 0  Success.
	 * @retval -EAGAIN  The container has no space available.
	 * @retval <0  Error code indicating the reason for failure.
	 */
	int32_t (*input_packet)(struct PktContainer *, uint16_t pkt_id);
	/**
	 * @brief Retrieve an awaiting packet from the container.
	 *
	 * @param[in] container  A pointer to the PktContainer structure
	 * from which to retrieve the awaiting packet.
	 * @param[out] pkt_item  A pointer to a PktContainerItem structure
	 * that will be filled with information about the retrieved packet,
	 * such as its ID and any associated data.
	 *
	 * @return  The result of the operation.
	 * @retval 0  Success.
	 * @retval <0  Error code indicating the reason for  failure (e.g.,
	 * no awaiting packets).
	 */
	int32_t (*get_awaiting_packet)(struct PktContainer *, struct PktContainerItem *pkt_item);
	/**
	 * @brief Mark a packet as being processed.
	 *
	 * @param[in] container  A pointer to the PktContainer structure
	 * containing the packet.
	 * @param[in] pkt_item   The PktContainerItem identifying the packe
	 * to be marked as in processing.
	 *
	 * @return  The result of the operation.
	 * @retval 0  Success.
	 * @retval <0  Error code indicating the reason for failure.
	 */
	int32_t (*processing)(struct PktContainer *, const struct PktContainerItem pkt_item);
	/**
	 * @brief Check if the container has any awaiting packets.
	 *
	 * @param[in] container  A pointer to the PktContainer structure
	 * to check for awaiting packets.
	 *
	 * @return  True if the container has awaiting packets, false otherwise.
	 */
	bool (*has_awaiting_packet)(const struct PktContainer *);
	/**
	 * @brief Mark a packet as complete or with an error.
	 *
	 * @param[in] container  A pointer to the PktContainer structure containing
	 * the packet.
	 * @param[in] stage  A pointer to the NepStage that processed the packet.
	 * @param[in] pkt_item  The PktContainerItem identifying the packet to be
	 * marked as complete or with an error.
	 * @param[in] pkt_state  An integer value indicating the state of the packet
	 * after processing. This value determines whether the packet is marked as
	 * complete or with an error.
	 */
	void (*complete)(struct PktContainer *, struct NepStage *stage,
			 const struct PktContainerItem pkt_item, int32_t pkt_state);
	/*
	 * @brief get one completed packet.
	 *
	 * @param[in] container: the container which stores poackets
	 * @param[out] pkt_id: the completed packet's id
	 *
	 * @return 1: get completed packet
	 *         0: no completed packet
	 *      -EIO: packet is in error state, should drop this packet.
	 *       < 0: errno
	 */
	int32_t (*get_completed_packet)(struct PktContainer *container, uint16_t *pkt_id);
	/*
	 * @brief dequeue the first packet.
	 *
	 * @param[in] container: the container which stores poackets
	 */
	void (*dequeue_packet)(struct PktContainer *container);
	/*
	 * @brief reset the container.
	 *
	 * @param[in] container: the container which stores poackets
	 */
	void (*reset)(struct PktContainer *container);
};

static inline void NepPktContainerReset(struct PktContainer *basic)
{
	if (basic) {
		basic->reset(basic);
	}
}

/************************** Single Packet Container ***************************/

struct SinglePktContainer {
	uint16_t status;
	uint16_t pkt_id;
	struct PktContainer basic;
};

uint32_t NepSinglePktContainerSpaceCount(const struct PktContainer *basic);
int32_t NepSinglePktContainerInput(struct PktContainer *basic, uint16_t pkt_id);
int32_t NepSinglePktContainerGetAwaitingPacket(struct PktContainer *basic,
					       struct PktContainerItem *pkt_item);
int32_t NepSinglePktContainerProcessing(struct PktContainer *basic,
					const struct PktContainerItem pkt_item);
bool NepSinglePktContainerHasAwaitingPacket(const struct PktContainer *basic);
void NepSinglePktContainerComplete(struct PktContainer *basic, struct NepStage *stage,
				   const struct PktContainerItem pkt_item, int32_t pkt_state);
int32_t NepSinglePktContaineGetCompletedPacket(struct PktContainer *basic, uint16_t *pkt_id);
void NepSinglePktContaineDequeuePacket(struct PktContainer *basic);
void NepSinglePktContainerReset(struct PktContainer *basic);

#define INIT_NEP_STAGE_SINGLE_PKT_CONTAINER(name)                                                  \
	struct SinglePktContainer name = { 		\
		.status = 0, 				\
		.pkt_id = 0, 				\
		.basic = { 				\
			.space_count = NepSinglePktContainerSpaceCount, 		\
			.input_packet = NepSinglePktContainerInput, 			\
			.get_awaiting_packet = NepSinglePktContainerGetAwaitingPacket, 	\
			.processing = NepSinglePktContainerProcessing, 			\
			.has_awaiting_packet = NepSinglePktContainerHasAwaitingPacket, 	\
			.complete = NepSinglePktContainerComplete, 			\
			.get_completed_packet = NepSinglePktContaineGetCompletedPacket, \
			.dequeue_packet = NepSinglePktContaineDequeuePacket, 		\
			.reset = NepSinglePktContainerReset, 				\
		}, 					\
	}

/**
 * @brief Construct a SinglePktContainer object.
 *
 * This function initializes a SinglePktContainer object. It
 * is equivalent to the INIT_NEP_STAGE_SINGLE_PKT_CONTAINER() macro.
 *
 * @param[in] container The SinglePktContainer object to
 * be initialized.
 */
void NepSinglePktContainerConstructor(struct SinglePktContainer *container);

/************************* Multiple Packet Container **************************/

struct PktContainerArrayEntry {
	uint16_t status;
	uint16_t pkt_id;
};

/*
 * The layout of MultiPktContainer's ring buffer array:
 *
 *  Head             Processing               Completed               Tail
 *   |-- awaiting pkts --|-- in processing pkts --|-- completed pkts --|
 */
struct MultiPktContainer {
	struct PktContainerArrayEntry *arr;
	uint8_t head;
	uint8_t tail;
	uint8_t processing;
	uint8_t completed;
	uint8_t size_mask;
	struct PktContainer basic;
};

uint32_t NepMultiPktContainerSpaceCount(const struct PktContainer *basic);
int32_t NepMultiPktContainerInput(struct PktContainer *basic, uint16_t pkt_id);
int32_t NepMultiPktContainerGetAwaitingPacket(struct PktContainer *basic,
					      struct PktContainerItem *pkt_id);
int32_t NepMultiPktContainerProcessing(struct PktContainer *basic,
				       const struct PktContainerItem pkt_id);
bool NepMultiPktContainerHasAwaitingPacket(const struct PktContainer *basic);
void NepMultiPktContainerComplete(struct PktContainer *basic, struct NepStage *stage,
				  const struct PktContainerItem pkt_id, int32_t pkt_state);
int32_t NepMultiPktContaineGetCompletedPacket(struct PktContainer *basic, uint16_t *pkt_id);
void NepMultiPktContaineDequeuePacket(struct PktContainer *basic);
void NepMultiPktContainerReset(struct PktContainer *basic);

#define NEP_STAGE_MULTI_PKT_CONTAINER_MAX_SIZE_BIT (8U)
#define NEP_STAGE_MULTI_PKT_CONTAINER_MAX_SIZE (1U << NEP_STAGE_MULTI_PKT_CONTAINER_MAX_SIZE_BIT)
#define INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(prefix, name, sz_bit)                       \
	static_assert(sz_bit <= NEP_STAGE_MULTI_PKT_CONTAINER_MAX_SIZE_BIT);                       \
	prefix struct PktContainerArrayEntry name##_arr[1 << sz_bit] = {                           \
		{ 0, 0 },                                                                          \
	};                                                                                         \
	prefix struct MultiPktContainer name = { 		\
		.arr = &name##_arr[0], 				\
		.head = 0, 					\
		.tail = 0, 					\
		.processing = 0, 				\
		.completed = 0, 				\
		.size_mask = (1U << sz_bit) - 1, 		\
		.basic = { 					\
			.space_count = NepMultiPktContainerSpaceCount, 			\
			.input_packet = NepMultiPktContainerInput, 			\
			.get_awaiting_packet = NepMultiPktContainerGetAwaitingPacket, 	\
			.processing = NepMultiPktContainerProcessing, 			\
			.has_awaiting_packet = NepMultiPktContainerHasAwaitingPacket, 	\
			.complete = NepMultiPktContainerComplete, 			\
			.get_completed_packet = NepMultiPktContaineGetCompletedPacket, 	\
			.dequeue_packet = NepMultiPktContaineDequeuePacket, 		\
			.reset = NepMultiPktContainerReset, 				\
		}, 						\
	}
#define INIT_NEP_STAGE_MULTI_PKT_CONTAINER(name, sz_bit) 	\
		INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(, name, sz_bit)

/**
 * @brief Construct a MultiPktContainer object.
 *
 * This function initializes a MultiPktContainer object. It behaves the same as the
 * INIT_NEP_STAGE_MULTI_PKT_CONTAINER() macro.
 *
 * @param[in] container Pointer to the MultiPktContainer structure
 * to be initialized.
 * @param[in] size_bit The bit width used to represent the size of
 * the container. This value determines the maximum number of
 * packets that can be stored in the container.
 * @param[in] array Pointer to the PktContainerArrayEntry array that holds
 * the packet containers.
 */
void NepMultiPktContainerConstructor(struct MultiPktContainer *container, uint8_t size_bit,
				     struct PktContainerArrayEntry *array);

struct NepStageRouter {
	/**
	 * @brief Route a packet to the next stage.
	 *
	 * @param[in] basic  A pointer to the NepStageRouter responsible for
	 * making routing decisions.
	 * @param[in] packet  A pointer to the NepPacketContext containing the
	 * packet to be routed.
	 * @param[in] pkt_id  The ID of the packet to be routed.
	 * @param[out] busy_stage  A pointer to a NepStage pointer that will be set to
	 * the next stage if it is currently busy, or NULL otherwise.
	 *
	 * @return  The result of the operation.
	 * @retval 0  Success.
	 * @retval -EAGAIN  The next stage is busy processing another packet.
	 * @retval <0  Error code indicating the reason for failure.
	 */
	int32_t (*routing)(struct NepStageRouter *basic, const struct NepPacketContext *packet,
			   uint16_t pkt_id, struct NepStage **busy_stage);
};

struct NepStageDirectRouter {
	struct NepStage *next_stage;
	struct NepStageRouter basic;
};

#define INIT_NEP_STAGE_ENDING_ROUTER(name)                                                         \
	struct NepStageRouter name = {                                                             \
		.routing = NULL,                                                                   \
	}

/**
 * @brief Route a packet directly to the next stage.
 *
 * This function routes the packet identified by `pkt_id` directly to the next stage
 * configured in the `NepStageDirectRouter`. If the next stage is full of packets,
 * the function will return an  error code and set the `busy_stage` parameter to point
 * to the busy stage.
 *
 * @param[in] basic  A pointer to the NepStageRouter responsible for making routing decisions.
 * @param[in] packet  A pointer to the NepPacketContext containing the packet to be routed.
 * @param[in] pkt_id  The ID of the packet to be routed.
 * @param[out] busy_stage  A pointer to a NepStage pointer that will be set to the next stage
 * if it is currently busy, or NULL otherwise.
 *
 * @return  The result of the operation.
 * @retval 0  Success, the packet was routed to the next stage.
 * @retval -EAGAIN  The next stage is busy processing another packet.
 * @retval <0  Error code indicating the reason for failure.
 */
int32_t NepStageDirectRouting(struct NepStageRouter *basic, const struct NepPacketContext *,
			      uint16_t pkt_id, struct NepStage **busy_stge);

#define INIT_NEP_STAGE_DIRECT_ROUTER(name, n) 	\
	struct NepStageDirectRouter name = { 	\
		.next_stage = n, 		\
		.basic = { 			\
			.routing = NepStageDirectRouting, 	\
		}, 				\
	}

/**
 * @brief Construct a NepStageDirectRouter object.
 *
 * This function initializes a NepStageDirectRouter object.
 * It behaves the same as the INIT_NEP_STAGE_DIRECT_ROUTER() macro.
 *
 * @param[in] router Pointer to the NepStageDirectRouter
 * structure to be initialized.
 * @param[in] next_stage Pointer to the next stage in the
 * pipeline.
 */
void NepStageDirectRouterConstructor(struct NepStageDirectRouter *router,
				     struct NepStage *next_stage);

struct NepStageRoutingRule {
	bool (*is_matched)(const struct NepPacketContext *packet);
	struct NepStage *next_stage;
	struct NepStageRoutingRule *next_rule;
};

struct NepStageDecisionChainRouter {
	struct NepStageRoutingRule *rules;
	struct NepStageRouter basic;
};

/**
 * @brief Route a packet based on a decision chain.
 *
 * This function routes the packet identified by `pkt_id` based on a chain of routing
 * decisions defined in the `NepStageDecisionChainRouter`. It iterates through a list
 * of routing rules, checking if the packet matches each rule's criteria. If a match
 * is found, the packe is routed to the corresponding next stage. If no matching rule
 * is found, the function returns an error.
 *
 * If the determined next stage is full of packets, the function will return an error
 * code and set the `busy_stage` parameter to point to the busy stage.
 *
 * @param[in] basic  A pointer to the NepStageRouter responsible for making routing decisions.
 * @param[in] packet  A pointer to the NepPacketContext containing the packet to be routed.
 * @param[in] pkt_id  The ID of the packet to be routed.
 * @param[out] busy_stage  A pointer to a NepStage pointer that will be set to the next stage
 * if it is currently busy, or NULL otherwise.
 *
 * @return  The result of the operation.
 * @retval 0  Success, the packet was routed to the next stage.
 * @retval -EAGAIN  The next stage is busy processing another packet.
 * @retval <0  Error code indicating the reason for failure.
 */
int32_t NepStageDecisionChainRouting(struct NepStageRouter *basic,
				     const struct NepPacketContext *packet, uint16_t pkt_id,
				     struct NepStage **busy_stge);
#define INIT_NEP_STAGE_DECISION_RULE(name, func, s, r)                                             \
	struct NepStageRoutingRule name = {                                                        \
		.is_matched = func,                                                                \
		.next_stage = s,                                                                   \
		.next_rule = r,                                                                    \
	}

/**
 * @brief Construct a NepStageRoutingRule object.
 *
 * This function initializes a NepStageRoutingRule object. It
 * behaves the same as the INIT_NEP_STAGE_DECISION_RULE() macro.
 *
 * @param[in] rule Pointer to the NepStageRoutingRule structure
 * to be initialized.
 * @param[in] is_matched Pointer to a function that checks whether
 * the rule matches a given packet.
 * @param[in] next_stage Pointer to the next stage in the
 * pipeline to be sent if the rule matches.
 * @param[in] next_rule Pointer to the next rule in the list to be
 * evaluated if the current rule does not match.
 */
void NepStageRoutingRuleConstructor(struct NepStageRoutingRule *rule,
				    bool (*is_matched)(const struct NepPacketContext *packet),
				    struct NepStage *next_stage,
				    struct NepStageRoutingRule *next_rule);

#define INIT_NEP_STAGE_DECISION_CHAIN_ROUTER(name, r) 			\
	struct NepStageDecisionChainRouter name = { 			\
		.rules = r, 						\
		.basic = { 						\
			.routing = NepStageDecisionChainRouting, 	\
		}, 							\
	}

/**
 * @brief Construct a NepStageDecisionChainRouter object.
 *
 * This function initializes a NepStageDecisionChainRouter object
 * with the provided routing rules. It behaves the same way as
 * INIT_NEP_STAGE_DECISION_CHAIN_ROUTER().
 *
 * @param[in] router  Pointer to the
 * NepStageDecisionChainRouter object to be initialized.
 * @param[in] rules  Pointer to an array of
 * NepStageRoutingRule structures containing the routing rules
 * for the router.
 */
void NepStageDecisionChainRouterConstructor(struct NepStageDecisionChainRouter *router,
					    struct NepStageRoutingRule *rules);

struct NepStage {
	struct NepStageRouter *router;
	struct PktContainer *container;
	struct NepEngine *engine;
	struct list_head eng_hook;
	/*
	 * If the next stage is busy and unable to receive the
	 * completed packet from this stage, this stage will
	 * queue itself on the next stage's waiting_list_head
	 * using the waiting_hook.
	 */
	struct list_head waiting_hook;
	struct list_head waiting_list_head;
	struct NepTaskScheduler *scheduler;
	struct list_head tracking_hook;
	void (*post_route)(struct NepStage *stage);
};

static inline void NepNormalStagePostSend(struct NepStage *stage)
{
	struct NepStage *waiting_stage;
	struct NepStage *next;
	struct NepTaskScheduler *scheduler = stage->scheduler;
	list_for_each_entry_safe (waiting_stage, next, &stage->waiting_list_head,
				  waiting_hook) {
		list_del(&waiting_stage->waiting_hook);
		list_add_tail(&waiting_stage->waiting_hook, &scheduler->stage_tasks);
	}
}

#define INIT_NEP_STAGE_HELPER(name, r, c, e, s, f)                                                 \
	struct NepStage name = {                                                                   \
		.router = r,                                                                       \
		.container = c,                                                                    \
		.engine = e,                                                                       \
		.eng_hook = { &(name.eng_hook), &(name.eng_hook) },                                \
		.waiting_hook = { &(name.waiting_hook), &(name.waiting_hook) },                    \
		.waiting_list_head = { &(name.waiting_list_head), &(name.waiting_list_head) },     \
		.scheduler = s,                                                                    \
		.tracking_hook = { &(name.tracking_hook), &(name.tracking_hook) },                 \
		.post_route = f,                                                                   \
	}

#define INIT_NEP_STAGE(name, r, c, e, s)                                                           \
	INIT_NEP_STAGE_HELPER(name, r, c, e, s, NepNormalStagePostSend)

/**
 * @brief Construct a NepStage object.
 *
 * This function initializes a NepStage object with the provided
 * parameters. It behaves the same way as INIT_NEP_STAGE_HELPER().
 *
 * @param[in] stage  Pointer to the NepStage object to be initialized.
 * @param[in] router  Pointer to the NepStageRouter object associated with this stage.
 * @param[in] container  Pointer to the PktContainer object used for packet processing
 * within this stage.
 * @param[in] engine  Pointer to the NepEngine object that this stage belongs to.
 * @param[in] scheduler  Pointer to the NepTaskScheduler object used for task scheduling
 * within this stage.
 * @param[in] post_route  Pointer to a function that is called after the routing decision
 * is made for a packet.
 */
void NepStageConstructHelper(struct NepStage *stage, struct NepStageRouter *router,
			     struct PktContainer *container, struct NepEngine *engine,
			     struct NepTaskScheduler *scheduler,
			     void (*post_route)(struct NepStage *stage));

static inline void NepStageConstructor(struct NepStage *stage, struct NepStageRouter *router,
				       struct PktContainer *container, struct NepEngine *engine,
				       struct NepTaskScheduler *scheduler)
{
	NepStageConstructHelper(stage, router, container, engine, scheduler,
				NepNormalStagePostSend);
}

int32_t NepStageHookProcessingEngine(struct NepStage *stage);
static inline int32_t NepStageGetAwaitingPacket(struct NepStage *stage,
						struct PktContainerItem *pkt_item)
{
	if (!stage || !stage->container || !pkt_item) {
		pr_err("Invalid arguments when get awaiting packet");
		return -EINVAL;
	}
	return stage->container->get_awaiting_packet(stage->container, pkt_item);
}

static inline void NepStageReset(struct NepStage *stage)
{
	if (!stage) {
		return;
	}
	NepPktContainerReset(stage->container);
	list_del_init(&stage->eng_hook);
	list_del_init(&stage->waiting_hook);
	list_del_init(&stage->waiting_list_head);
}

static inline void NepStageAddToScheduler(struct NepStage *stage,
					  struct NepTaskScheduler *scheduler)
{
	if (!stage || !scheduler) {
		return;
	}
	stage->scheduler = scheduler;
	list_add_tail(&stage->tracking_hook, &scheduler->all_stages);
}

/**
 * @brief Route completed packets to their next stage.
 *
 * This function is responsible for routing completed packets from a given stage to their
 * next stage in the processing pipeline. It retrieves completed packets from the stage's
 * container, determines the next stage using the stage's router, and handles potential busy
 * states in the destination stage.
 *
 * This function is typically called by the task scheduler to process completed packets from
 * stages that have indicated they have packets ready for routing.
 *
 * @param[in] stage  A pointer to the NepStage with completed packets to be routed.
 *
 * @return  The result of the operation.
 * @retval 0  Success, all completed packets were processed.
 * @retval <0  Error code indicating a problem occurred during routing.
 */
int32_t NepStageRouteCompletedPackets(struct NepStage *stage);

#endif /* NOA_NETWORK_PIPELINE_FRAMEWORK_STAGE_H */
