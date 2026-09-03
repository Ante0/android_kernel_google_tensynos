/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for Ring Pipeline Service Receiver
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_RING_PIPELINE_SERVICE_RECEIVER_H
#define NOA_RING_PIPELINE_SERVICE_RECEIVER_H

#ifdef linux
#include <linux/spinlock.h>
#include <linux/types.h>

#include "ring_manager_instance.h"
#include "network_pipeline_framework/packet_initiator.h"
#else /* linux */
#include <cstdint>

#include "linux_port/spinlock.h"
#include "ring_mgmt/ring_manager_instance.h"
#include "network_pipeline_framework/packet_initiator.h"
#include "notifier/notifier_mailbox.h"
#endif /* linux */

struct NetworkInterfaceRingGroup {
	uint8_t id;
	uint8_t curr_ring;
	uint8_t num_of_ring;
	uint32_t ring_data_bitmap;
	struct NoaRingManagerInfoFlow *rings;
	struct NepPacketInitiator *initiator;
	// TODO(b/378846675) - Integrate doorbell status mechanism.
	uint32_t doorbell_status_register;
	spinlock_t doorbell_lock;
#ifndef linux
	::noa::module::notifier::NotifierMailbox notifier;
#endif /* linux */
};

struct NepRingReceiver {
	uint8_t curr_group;
	uint8_t num_of_group;
	struct NepPacketInitiator initiator;
	struct NetworkInterfaceRingGroup *ring_group;
};

/**
 * @brief Handles the Ring Receiver interrupt.
 *
 * This function is invoked upon the reception of a Ring Receiver interrupt.
 * It adds the Packet Initiator to the task scheduler, effectively triggering
 * the processing of incoming data on the associated ring.
 *
 * @param context  A pointer to the Ring Receiver context.
 *
 * @return 0 on success, a negative error code on failure.
 */
int32_t RingReceiverIsr(void *context);

/**
 * @brief Checks if the Ring Receiver has data available.
 *
 * This function determines if the Ring Receiver has any data pending
 * processing. It checks the rings associated with the receiver,
 * starting with the current ring indicated by the  `curr_group` and
 * `curr_ring` indices. If no data is found on the current ring, it
 * iterates through other rings within the same NetworkInterfaceRingGroup.
 * If all rings within that group are empty, the function proceeds to
 * examine rings in other NetworkInterfaceRingGroups.
 *
 * @param[in] initiator A pointer to the NepPacketInitiator associated with the
 * Ring Receiver.
 *
 * @return true if data is available, false otherwise.
 */
bool RingReceiverHasData(struct NepPacketInitiator *initiator);

/**
 * @brief Transfers data from the ring buffer to a PacketContext.
 *
 * It copies data from the ring buffer to the noa_desc field of a
 * PacketContext object. Upon completing data transfer from a ring,
 * the Receiver switches to the next NetworkInterfaceRingGroup to
 * continue transferring.
 *
 * @param[in] initiator  The PacketInitiator instance.
 * @param[in] context   The PacketContext to populate with data.
 *
 * @return 0 on success, a negative error code otherwise.
 */
int32_t RingReceiverFormatPacket(struct NepPacketInitiator *initiator,
                                 struct NepPacketContext *context);

/**
 * @brief Obtain the NepRingReceiver singleton instance.
 *
 * This singleton is not yet initialized. You must call RingReceiverInit()
 * before using it.
 *
 * @return The NepRingReceiver singleton object pointer.
 */
struct NepRingReceiver *RingReceiverSingletonGet(void);

/**
 * @brief Initializes a RingReceiver.
 *
 * This function initializes a RingReceiver by adding its packet instantiator to the
 * provided task scheduler. It binds the Network Interface Ring Group from the
 * NoaRingManagerInfoRoot to the "ring_group" and initializes each ring within
 * the group.
 *
 * @param[in] receiver The RingReceiver to initialize.
 * @param[in] scheduler The task scheduler to add the packet instantiator to.
 * @param[in] ring_root The NoaRingManagerInfoRoot containing the Network Interface Ring Group.
 *
 * @return  0 on success, a negative error code on failure.
 */
int32_t RingReceiverInit(struct NepRingReceiver *receiver, struct NepTaskScheduler *scheduler,
			 struct NoaRingManagerInfoRoot *ring_root);

#endif /* NOA_RING_PIPELINE_SERVICE_RECEIVER_H */
