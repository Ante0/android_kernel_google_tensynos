/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header for NEP Pipeline Packet Table
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifndef NOA_NETWORK_PIPELINE_FRAMEWORK_PACKET_TABLE_H
#define NOA_NETWORK_PIPELINE_FRAMEWORK_PACKET_TABLE_H

#ifdef linux
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/printk.h>
#include <linux/bitops.h>

#include "common/core.h"
#include "common/ring_id.h"
#include "common/inttypes.h"
#else /* linux */
#include <cerrno>
#include <cinttypes>
#include <cstdint>

#include "common/core.h"
#include "common/ring_id.h"
#include "linux_port/log.h"
#include "linux_port/bitops.h"
#endif /* linux */

#define SIZE_OF_NEP_PACKET_HEADER ((128U))

enum {
	kUseOriginalPacket,
	kUseProcessedPacket,
	kOutputPacketBuffer = kUseProcessedPacket,
	kUsePacketTypeMax,
};

struct NepPacketContext {
	union {
		uint8_t flags;
		struct {
			uint8_t is_in_processing : 1;
			uint8_t use_packet_type : 1;
			uint8_t should_recycle_original_buffer : 1;
		};
	};
	uint8_t src;
	uint8_t dst;
	uint8_t resv;
	uint8_t noa_desc[NOA_DESC_MAX_BYTE];
	uint8_t ip_header[SIZE_OF_NEP_PACKET_HEADER];
	struct {
		uint16_t dl;
		uint16_t head_offset;
		uint16_t tkid;
		uint16_t dp_high;
		uint32_t dp_low;
		uint64_t dv;
	} packet_buffer[kUsePacketTypeMax];
};

/**
 * @brief Resets buffer information fields in the packet context.
 *
 * This function cleans up the buffer information fields within the provided
 * packet context. This cleanup prevents the associated buffer from being
 * recycled when the packet is subsequently freed.
 *
 * @param[in] packet The packet context whose buffer information needs
 * to be reset.
 * @param[in] reset_type Specifies the type of reset operation to be
 * performed on the buffer.
 */
static inline void NepPacketContextResetBuffer(struct NepPacketContext *packet, uint8_t reset_type)
{
	packet->packet_buffer[reset_type].dv = 0;
}

/**
 * @brief Checks if there are unused buffers in the packet.
 *
 * This function checks the provided packet context to determine if
 * there are any unused buffers of the specified type available.
 *
 * @param[in] packet The packet context to check for unused buffers.
 * @param[in] buffer_type The type of buffer to check for availability.
 *
 * @return True if unused buffers of the specified type exist;
 * otherwise, false.
 */
static inline bool NepHasBufferToRecycled(struct NepPacketContext *packet, uint8_t buffer_type)
{
	return packet->packet_buffer[buffer_type].dv != 0;
}

/**
 * @brief Formats the noa_descriptor in the packet context as a feedback descriptor.
 *
 * This function takes a packet context and formats its internal
 * `noa_descriptor` into a feedback descriptor. This process prepares
 * the descriptor for use in feedback mechanisms, allowing for proper
 * buffer recycling related to this packet.
 *
 * @param[in] packet The packet context containing the noa_descriptor
 * to be formatted.
 */
static inline void OriginalPacketRecycleDescriptorFormat(struct NepPacketContext *packet)
{
	struct noa_desc *desc;
	if (!packet) {
		return;
	}

	desc = (struct noa_desc *)&packet->noa_desc[0];
	desc->dst = packet->dst = NoaFeedbckPathIdFromDataPath(packet->src);
	desc->mode = NOAD_MODE_FEEDBACK;
	desc->desc_type = NOA_DESC_BASIC;
	packet->use_packet_type = kUseOriginalPacket;
}

/**
 * @brief Formats the noa_descriptor in the packet context as a fallback descriptor.
 *
 * This function takes a packet context and formats its internal
 * `noa_descriptor` into a fallback descriptor.
 *
 * @param[in] packet The packet context containing the noa_descriptor
 * to be formatted.
 */
static inline void FallbackPacketDescriptorFormat(struct NepPacketContext *packet)
{
	struct noa_desc *desc;
	if (!packet) {
		return;
	}

	desc = (struct noa_desc *)&packet->noa_desc[0];
	desc->dst = packet->dst = packet->src;
	desc->reason = FWD_REASON_FALLBACK;
	packet->use_packet_type = kUseOriginalPacket;
}

struct NepPacketTable {
	uint16_t free_count;
	uint16_t size;
	struct NepPacketContext *arr;
	unsigned long *free_bitmap;
	/**
	 * @brief Triggers the buffer recycling process for a specific packet.
	 *
	 * This function triggers the buffer recycling process associated
	 * with a given packet ID. It informs the relevant handler to
	 * initiate the recycling of the buffer associated with the
	 * provided packet. The return value indicates the status of the
	 * recycling operation and whether the packet can be safely freed.
	 *
	 * @param[in] table The packet table containing the packet information.
	 * @param[in] pkt_id The ID of the packet for which to trigger buffer
	 * recycling.
	 * @param[in] handler The handler responsible for performing the buffer
	 * recycling.
	 *
	 * @return 1 if recycling is in progress (packet should not be freed);
	 * @return 0 if no buffer needs recycling (packet can be freed);
	 * @return negative value if recycling failed (packet should not be freed).
	 */
	int32_t (*buffer_recycle)(struct NepPacketTable *table, uint16_t pkt_id, void *handler);
	void *buffer_recycle_handler;
};
#define INIT_NEP_PACKET_TABLE_WITH_PREFIX(prefix, name, sz) 	\
	prefix struct NepPacketContext name##_arr[sz]; 		\
	prefix unsigned long name##_bitmap[BITS_TO_LONGS(sz)]; 	\
	prefix struct NepPacketTable name = { 			\
		.free_count = 0, 				\
		.size = sz, 					\
		.arr = &(name##_arr[0]), 			\
		.free_bitmap = &(name##_bitmap[0]), 		\
		.buffer_recycle = NULL, 			\
		.buffer_recycle_handler = NULL, 		\
	}

#define INIT_NEP_PACKET_TABLE(name, sz) 			\
		INIT_NEP_PACKET_TABLE_WITH_PREFIX(, name, sz)

struct PktContainerItem {
	uint16_t index;
	uint16_t pkt_id;
	// This comparator is designed for googlemock, so it requires a piece of C++ code.
#ifdef __cplusplus
	bool operator==(const PktContainerItem &rhs) const
	{
		return index == rhs.index && pkt_id == rhs.pkt_id;
	}
#endif /* __cplusplus */
};

struct NepProcessingRequest {
	struct PktContainerItem item;
	struct NepPacketContext *packet;
};

/**
 * @brief Initialize the packet table.
 *
 * @param[in] table, the packet table data structure.
 */
void NepPacketTableInit(struct NepPacketTable *table);

/**
 * @brief Are there any available packet contexts in the table.
 *
 * @param[in] table, the packet table data structure.
 *
 * @return The result.
 * @retval true is empty.
 * @retval false not empty.
 */
static inline bool NepPacketTableIsEmpty(struct NepPacketTable *table)
{
	return !table || !table->free_count;
}

/**
 * @brief Acquire the ID of free packet context.
 *
 * This API retrieves the free packet context based on the bitmap status in
 * NepPacketTable. It then returns the corresponding ID, which has a base
 * index of one.
 *
 * @param[in] table, the packet table data structure.
 * @param[out] pkt_id, the ID of the free packet that this API acquires.
 *
 * @return The acquisition result.
 * @retval 0 Success.
 * @retval -EINVAL Invalide arguments.
 * @retval -EAGAIN There are no free packets in the packet table.
 * @retval -EFAULT Failed to get free packet.
 */
int32_t NepPacketTableFreePacketAcquire(struct NepPacketTable *table, uint16_t *pkt_id);

/**
 * @brief Get the packet context by ID.
 *
 * This API retrieves the packet context using its ID and marks it as being processed.
 *
 * @param[in] table, the packet table data structure.
 * @param[in] pkt_id, the ID of the packet context.
 *
 * @return The getting result.
 * @retval 0 Success.
 * @retval -EINVAL Invalide arguments.
 */
static inline int32_t NepPacketTablePacketGet(struct NepPacketTable *table, uint16_t pkt_id,
					      struct NepPacketContext **context)
{
	uint16_t index = pkt_id - 1;
	if (index >= table->size) {
		pr_err("The pkt %" PRIu16 " has exceeded the table size %" PRIu32 "\n", pkt_id,
		       table->size);
		return -EINVAL;
	}
	*context = &(table->arr[index]);
	if ((*context)->is_in_processing) {
		pr_err("The pkt %" PRIu16 " is already in processing\n", pkt_id);
	}
	(*context)->is_in_processing = true;
	return 0;
}

/**
 * @brief Reset the packet processing.
 *
 * @param[in] context, the packet context that the caller want to clear.
 */
static inline void NepPacketTablePacketRelease(struct NepPacketContext *context)
{
	context->is_in_processing = false;
}

/**
 * @brief Free the unused packet context.
 *
 * This API frees the unused packet context and returns it to the NepPacketTable.
 * The NepPacketTable then marks the corresponding bit in the bitmap to indicate
 * that the packet context is available.
 *
 * @param[in] table, the packet table data structure.
 * @param[in] pkt_id, the ID of the unused packet.
 */
void NepPacketTablePacketFree(struct NepPacketTable *table, uint16_t pkt_id);

/**
 * @brief Reset the packet table.
 *
 * @param[in] table, the packet table data structure.
 */
void NepPacketTableReset(struct NepPacketTable *table);

bool NoPacketSendingToDestination(struct NepPacketTable *table, uint8_t dst);

#endif /* NOA_NETWORK_PIPELINE_FRAMEWORK_PACKET_TABLE_H */
