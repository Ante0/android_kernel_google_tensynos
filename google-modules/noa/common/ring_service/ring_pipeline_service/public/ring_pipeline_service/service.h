/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for Ring Pipeline Service
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_RING_PIPELINE_SERVICE_SERVICE_H
#define NOA_RING_PIPELINE_SERVICE_SERVICE_H

#ifdef linux
#include <linux/types.h>

#include "ring_manager_instance.h"
#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/task_scheduler.h"
#else /* linux */
#include <cstdint>

#include "network_pipeline_framework/stage.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/task_scheduler.h"
#include "ring_mgmt/ring_manager_instance.h"
#endif /* linux */

struct RingServiceObsoletePacketHandler {
	uint16_t size;
	uint16_t tracking_index;
	unsigned long *recycle_bitmap;
	struct NepStage recycle_stage;
	struct NepStage fallback_packet_stage;
	struct NepRunnableTask task;
	struct NepTaskScheduler *scheduler;
};

/**
 * @brief Sets up the Ring Service Receiver.
 *
 * This function sets up the Ring Service Receiver, adding it to the specified
 * task scheduler and associating it with the provided ring instances. It also
 * configures the routing process for received data. It is crucial to note that
 * this function is exclusively designed for invocation during the NEP bootup
 * sequence and should be called only once.
 *
 * @param[in] scheduler The task scheduler to which the Ring Service Receiver
 * will be added.
 * @param[in] ring_root The root structure containing information about the ring
 * instances.
 * @param[in] router The NepStageRouter used to define the routing process for
 * received data.
 *
 * @return 0 on success, negative error code otherwise.
 */
int32_t RingServiceReceiverSetup(struct NepTaskScheduler *scheduler,
				 struct NoaRingManagerInfoRoot *ring_root,
				 struct NepStageRouter *router);

/**
 * @brief Initializes the Ring Service Sender.
 *
 * Initializes the Ring Service Sender, adding it to the specified task scheduler
 * and associating it with the provided ring instances. It is crucial to note that
 * this function is exclusively designed for invocation during the NEP bootup
 * sequence and should be called only once.
 *
 * @param[in] scheduler The task scheduler to which the Ring Service Sender will
 * be added.
 * @param[in] ring_root The root structure containing information about the ring
 * instances.
 *
 * @return 0 on success, negative error code otherwise.
 */
int32_t RingServiceSenderSetup(struct NepTaskScheduler *scheduler,
			       struct NoaRingManagerInfoRoot *ring_root);

/**
 * @brief Returns the Ring Service Sender Engine.
 *
 * Returns the Sender Engine, allowing Network services to use this engine to
 * send their processed data.
 *
 * @return A pointer to the NepEngine. This function won't be failed.
 */
struct NepEngine *RingServiceSenderEngineGet(void);

/**
 * @brief Set up the Ring Service Feedthrough Stage.
 *
 * This function sets up the Feedthrough Stage. This stage is responsible for
 * sending non-NOA data directly to the NOA output rings.
 *
 * @param[in] sender_engine The NepEngine instance used to send descriptors to
 * the output ring.
 * @param[in] scheduler The task scheduler used for managing the feedthrough
 * operations.
 *
 * @return 0 on success, negative value for error code.
 */
int32_t RingServiceFeedthroughStageSetup(struct NepEngine *sender_engine,
					 struct NepTaskScheduler *scheduler);
/**
 * @brief Get the singleton instance of the Ring Service Feedthrough Stage.
 *
 * This function retrieves the singleton instance of the Ring Service
 * Feedthrough Stage. This function is guaranteed to return a valid
 * pointer to the singleton object.
 *
 * @return A valid pointer to the Ring Service Feedthrough Stage singleton.
 */
struct NepStage *RingServiceFeedthroughStageSingletonGet(void);
/**
 * @brief Setup the Fallback Stage path.
 *
 * This function establishes the Fallback Stage path, including both RX path
 * and TX path fallback stages. The setup is performed using the provided
 * sender_engine.
 *
 * @param[in] sender_engine The NepEngine structure used for setup.
 * @param[in] scheduler The NepTaskScheduler used for scheduling fallback tasks.
 *
 * @return 0 on success, negative value on error.
 */
int32_t RingServiceFallbackStageSetup(struct NepEngine *sender_engine,
				      struct NepTaskScheduler *scheduler);
/**
 * @brief Get the singleton instance of the Ring Service TX Fallback Stage.
 *
 * This function retrieves the singleton instance of the Ring Service TX
 * Fallback Stage. This function is guaranteed to return a valid pointer
 * to the singleton object.
 *
 * @return A valid pointer to the Ring Service TX Fallback Stage singleton.
 */
struct NepStage *RingServiceTxFallbackStageSingletonGet(void);
/**
 * @brief Get the singleton instance of the Ring Service RX Fallback Stage.
 *
 * This function retrieves the singleton instance of the Ring Service RX
 * Fallback Stage. This function is guaranteed to return a valid pointer
 * to the singleton object.
 *
 * @return A valid pointer to the Ring Service RX Fallback Stage singleton.
 */
struct NepStage *RingServiceRxFallbackStageSingletonGet(void);

/**
 * @brief Sets up the ObsoletePacketHandler for the Ring Service.
 *
 * This function creates and initializes an ObsoletePacketHandler
 * specifically designed to handle packets discarded by the Network
 * Pipeline Service. These discarded packets may have encountered
 * errors during processing. This handler ensures that these
 * discarded packets are properly managed, preventing issues such as
 * buffer memory leaks.
 *
 * @param[in] handler The handler to be set up.
 * @param[in] scheduler The task scheduler to be used by the handler.
 * @param[in] size The number of bits in the recycle_bitmap.
 * @param[in] recycle_bitmap The bitmap used for tracking recycled buffers.
 *
 * @return 0 on success; negative error code on failure.
 */
int32_t RingServiceObsoletePacketHandlerSetup(struct RingServiceObsoletePacketHandler *handler,
					      struct NepTaskScheduler *scheduler, uint16_t size,
					      unsigned long *recycle_bitmap);
/**
 * @brief Recycles or forwards an obsolete packet based on its state.
 *
 * This function triggers the recycling or forwarding of an obsolete
 * packet identified by its ID. Based on the packet's current state,
 * the function either recycles the associated buffer or forwards
 * the original packet to the appropriate driver for further
 * processing. The return value indicates whether the packet has
 * been successfully submitted to a processing stage.
 *
 * @param[in] table The packet table containing the packet information.
 * @param[in] pkt_id The ID of the packet to be recycled or forwarded.
 * @param[in] data The RingServiceObsoletePacketHandler structure.
 *
 * @return 1 if the packet has been submitted to a processing stage;
 * @return 0 if no buffer requires processing;
 * @return negative value if the packet could not be submitted to a
 * processing stage and will be retried by a subsequent task.
 */
int32_t RingServiceObsoletePacketRecycle(struct NepPacketTable *table, uint16_t pkt_id, void *data);

#endif /* NOA_RING_PIPELINE_SERVICE_SERVICE_H */
