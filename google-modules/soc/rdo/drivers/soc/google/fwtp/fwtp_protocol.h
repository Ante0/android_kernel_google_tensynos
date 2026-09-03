/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Copyright 2024 Google LLC.
 *
 * Google firmware tracepoint protocol header.
 *
 * This header is copied from the Pixel firmware sources to the Linux kernel
 * sources, so it's written to be compiled under both Linux and the firmware,
 * and it's licensed under GPL or MIT licenses.
 */

#ifndef __FWTP_PROTOCOL_H
#define __FWTP_PROTOCOL_H

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>
#elif LK
#include <lk/compiler.h>
#include <sys/types.h>
#define __packed __PACKED
#else
#include <stdint.h>
#define ATTRIBUTE(x) __attribute__((x)) /* Work around checkpatch warning. */
#define __packed ATTRIBUTE(packed)
#endif

/**
 * DOC: FWTP protocol
 *
 * FWTP implements a message protocol for communication between FWTP drivers and
 * firmware. Each FWTP message starts with &struct fwtp_msg_base and has a
 * message type from &enum fwtp_msg_type that specifies how to interpret and
 * handle the remainder of the message.
 */

/**
 * define FWTP_PROTOCOL_VERSION - FWTP protocol version.
 *
 * Version 1: Initial version.
 * Version 2: Add table_num to fwtp_msg_get_strings.
 */
#define FWTP_PROTOCOL_VERSION FWTP_PROTOCOL_VERSION_2
#define FWTP_PROTOCOL_VERSION_1 1
#define FWTP_PROTOCOL_VERSION_2 2

/**
 * enum fwtp_msg_type - Set of FWTP message types.
 *
 * @kFwtpMsgTypeInvalid: Invalid message.
 * @kFwtpMsgTypeExchangeVersion: Exchange protocol version message. Uses
 *                               &struct fwtp_msg_version.
 * @kFwtpMsgTypeGetStrings: Get tracepoint string table message. Uses
 *                          &struct fwtp_msg_get_strings.
 * @kFwtpMsgTypeGetTracepoints: Get tracepoints message. Uses
 *                              &struct fwtp_msg_get_tracepoints.
 * @kFwtpMsgTypeRingNotify: Tracepoint ring notification message. Uses
 *                          &struct fwtp_msg_ring_notify.
 * @kFwtpMsgTypeRingSubscribe: Tracepoint ring subscribe message. Uses
 *                             &struct fwtp_msg_ring_subscribe.
 * @kFwtpMsgTypeGetRingInfo: Get tracepoint ring info message. Uses
 *                           &struct fwtp_msg_get_ring_info.
 * @kFwtpMsgTypeGetFilterInfo: Get tracepoint filter info message. Uses
 *                             &struct fwtp_msg_get_filter_info.
 * @kFwtpMsgTypeSetFilterConfig: Set tracepoint filter configuration message.
 *                               Uses &struct fwtp_msg_set_filter_config.
 * @kFwtpMsgTypeAddTracepoint: Add tracepoint message. Uses
 *                             &struct fwtp_msg_add_tracepoint.
 */
enum fwtp_msg_type {
	kFwtpMsgTypeInvalid = 0,
	kFwtpMsgTypeExchangeVersion = 1,
	kFwtpMsgTypeGetStrings = 2,
	kFwtpMsgTypeGetTracepoints = 3,
	kFwtpMsgTypeRingNotify = 4,
	kFwtpMsgTypeRingSubscribe = 5,
	kFwtpMsgTypeGetRingInfo = 6,
	kFwtpMsgTypeGetFilterInfo = 7,
	kFwtpMsgTypeSetFilterConfig = 8,
	kFwtpMsgTypeAddTracepoint = 9,
};

/**
 * struct fwtp_msg_base - Base FWTP message.
 *
 * @type: Message type. Set to a value from &enum fwtp_msg_type.
 * @error: Message error code. A value of 0 means no error; otherwise, an error
 *         occurred.
 * @reserved: Reserved. Set to 0 when writing and ignore when reading.
 *
 * This structure defines the base FWTP message. All FWTP messages start with
 * this structure.
 */
struct fwtp_msg_base {
	uint8_t type;
	uint8_t error;
	uint16_t reserved;
} __packed;

/**
 * DOC: FWTP protocol versioning
 *
 * The FWTP protocol versions in use by FWTP drivers and firmware may be
 * exchanged using &struct fwtp_msg_version messages of type
 * &fwtp_msg_type.kFwtpMsgTypeExchangeVersion. The FWTP driver sends a version
 * message with the &fwtp_msg_version.version field set to its protocol version
 * number. Firmware then responds with a version message with the
 * &fwtp_msg_version.version field set to the firmware's protocol version.
 *
 * In case of a version mismatch, one side or the other may downgrade to a lower
 * version and send an updated version message. If downgrading is not possible,
 * an error should be logged, and FWTP should not be used.
 */

/**
 * struct fwtp_msg_version - FWTP version message.
 *
 * @base: Message base.
 * @version: Protocol version.
 *
 * The FWTP version message is used to exchange protocol versions.
 */
struct fwtp_msg_version {
	struct fwtp_msg_base base;
	uint32_t version;
} __packed;

/**
 * DOC: FWTP string tables
 *
 * Strings in tracepoints are represented by an offset within a string table,
 * so tracepoint decoding requires the string table. The string table may be
 * read using &struct fwtp_msg_get_strings messages of type
 * &fwtp_msg_type.kFwtpMsgTypeGetStrings.
 *
 * Some firmwares may use multiple string tables (e.g., CPM may include string
 * tables for both CPM and CAP tracepoints). The string table to read is
 * specified by the &fwtp_msg_get_strings.table_num field.
 *
 * The offset of a string in a tracepoint may include an offset of the start of
 * the string table itself. When looking up a string by its offset within the
 * string table, the string table offset must first be subtracted from the
 * string offset. The string table offset is specified by the
 * &fwtp_msg_get_strings.string_table_offset field.
 *
 * The string table may be read in chunks using multiple messages. The
 * &fwtp_msg_get_strings.chunk_size field in the request message specifies the
 * size of the requested chunk, and the &fwtp_msg_get_strings.chunk_offset field
 * specifies the offset of the start of the requested chunk in the strings
 * table.
 *
 * The requested chunk will be appended to the response message in the
 * &fwtp_msg_get_strings.chunk field. The actual size of the returned chunk is
 * set in the &fwtp_msg_get_strings.chunk_size field, and the size of the entire
 * table is set in the &fwtp_msg_get_strings.table_size field.
 */

/**
 * struct fwtp_msg_get_strings - FWTP get string table message.
 *
 * @base: Message base.
 * @table_num: String table number to read.
 * @reserved: Reserved. Set to zero.
 * @table_size: Size of the string table.
 * @string_table_offset: Offset of start of string table.
 * @chunk_offset: Offset to the start of the chunk in the string table.
 * @chunk_size: Size of the string table chunk.
 * @chunk: String table chunk data.
 */
struct fwtp_msg_get_strings {
	struct fwtp_msg_base base;
	uint8_t table_num;
	uint8_t reserved[3];
	uint32_t table_size;
	uint32_t string_table_offset;
	uint32_t chunk_offset;
	uint32_t chunk_size;
	uint8_t chunk[];
} __packed;

/**
 * DOC: FWTP tracepoint rings
 *
 * FWTP tracepoints are written into and read from ring buffers. New tracepoints
 * are written to the tail of the ring, and the oldest tracepoint is read from
 * the head of the ring. The head and tail of the ring are maintained as byte
 * offsets from the start of the ring.
 *
 * FWTP tracepoint rings are initially filled with empty tracepoints. When a
 * tracepoint producer adds new tracepoints to the ring, they will overwrite the
 * oldest entry.
 *
 * FWTP rings have one tail offset, but may have multiple head offsets. Each
 * consumer of tracepoints has its own ring head offset. Since the producer
 * overwrites the oldest entry, it doesn't need to keep track of any head
 * offsets.
 *
 * If a consumer does not read from a ring often enough, the tail offset could
 * wrap around the ring and past the consumer's head offset, overrunning
 * tracepoints that the consumer has not yet read. The consumer must be able to
 * detect this condition.
 *
 * To detect overruns, FWTP uses the head and tail offsets. When the head and
 * tail offsets reach the end of the ring buffer, instead of being wrapped
 * around back to zero, they continue to be incremented. If the difference
 * between the tail and head offsets is larger than the size of the ring buffer,
 * then an overrun has occurred.
 *
 * When computing the location in the ring buffer of given a head or tail
 * offset, FWTP uses the offset modulo the size of the ring buffer.
 *
 * Information about a tracepoint ring may be obtained by using
 * &struct fwtp_msg_get_ring_info messages of type
 * &fwtp_msg_type.kFwtpMsgTypeGetRingInfo. The ring information to get is
 * specified by &fwtp_msg_get_ring_info.ring_num.
 *
 * Tracepoints may be read using &struct fwtp_msg_get_tracepoints messages of
 * type &fwtp_msg_type.kFwtpMsgTypeGetTracepoints. The ring to read from is
 * specified by &fwtp_msg_get_tracepoints.ring_num, and the consumer's head
 * offset is specified by the &fwtp_msg_get_tracepoints.head_offset field.
 *
 * In the response, the read tracepoint data will be set in the
 * &fwtp_msg_get_tracepoints.tracepoints field, and the size of the data will be
 * specified in the &fwtp_msg_get_tracepoints.tracepoints_size field. In
 * addition, the head offset of the next unread tracepoint will be specified in
 * the &fwtp_msg_get_tracepoints.head_offset field, and the current tail offset
 * is returned in the &fwtp_msg_get_tracepoints.tail_offset field.
 *
 * If &fwtp_msg_get_tracepoints.timestamp_hz is non-zero, it specifies the
 * frequency of the timestamps in Hz. This may be used to convert the tracepoint
 * timestamps to seconds.
 *
 * An overrun may be detected by comparing the returned tracepoints size with
 * the difference between the ending and starting head offsets. If the
 * difference is larger than the returned tracepoints size, an overrun has
 * occurred and some tracepoints have been lost.
 */

/**
 * struct fwtp_msg_get_ring_info - FWTP tracepoint ring get info message.
 *
 * @base: Message base.
 * @ring_num: Ring number for which to get ring info.
 * @version: Ring version.
 * @timestamp_hz: Timestamp frequency in Hz. If zero, timestamp frequency is
 *                unspecified.
 * @ring_size: Size in bytes of ring.
 * @ring_buffer_size: Size in bytes of ring buffer.
 * @tail_offset: Current tracepoint ring tail offset.
 * @buffer_soc_addr: Address of ring buffer from SoC point of view.
 */
struct fwtp_msg_get_ring_info {
	struct fwtp_msg_base base;
	uint16_t ring_num;
	uint16_t version;
	uint32_t timestamp_hz;
	uint32_t ring_size;
	uint32_t ring_buffer_size;
	uint32_t tail_offset;
	uint64_t buffer_soc_addr;
} __packed;

/**
 * struct fwtp_msg_get_tracepoints - FWTP get tracepoints message.
 *
 * @base: Message base.
 * @head_offset: Current tracepoint ring head offset.
 * @tail_offset: Current tracepoint ring tail offset.
 * @timestamp_hz: Timestamp frequency in Hz. If zero, timestamp frequency is
 *                unspecified.
 * @ring_num: Ring number to read from.
 * @tracepoints_size: Size of returned tracepoints.
 * @tracepoints: Returned tracepoints.
 */
struct fwtp_msg_get_tracepoints {
	struct fwtp_msg_base base;
	uint32_t head_offset;
	uint32_t tail_offset;
	uint32_t timestamp_hz;
	uint16_t ring_num;
	uint16_t tracepoints_size;
	uint8_t tracepoints[];
} __packed;

/**
 * DOC: FWTP tracepoint ring notifications
 *
 * FWTP clients may be notified when a certain number of bytes have been written
 * to a ring. Notification is delivered using &struct fwtp_msg_ring_notify
 * messages of type &fwtp_msg_type.kFwtpMsgTypeRingNotify.
 *
 * FWTP client may subscribe to ring notifications using
 * &struct fwtp_msg_ring_subscribe messages of type
 * &fwtp_msg_type.kFwtpMsgTypeRingSubscribe.
 */

/**
 * struct fwtp_msg_ring_notify - FWTP tracepoint ring notification message.
 *
 * @base: Message base.
 * @ring_num: Ring number for which notification is being given.
 * @reserved: Reserved. Set to zero.
 * @tail_offset: Tail offset of ring.
 */
struct fwtp_msg_ring_notify {
	struct fwtp_msg_base base;
	uint16_t ring_num;
	uint16_t reserved;
	uint32_t tail_offset;
} __packed;

/**
 * struct fwtp_msg_ring_subscribe - FWTP tracepoint ring notification subscribe
 *                                  message.
 *
 * @base: Message base.
 * @ring_num: Ring number for which to subscribe to notifications.
 * @start: If non-zero, start subscription to notifications; otherwise, stop
 *         subscription.
 * @reserved: Reserved. Set to zero.
 * @notify_byte_count: Send notification after this number of bytes are written
 *                     to ring.
 */
struct fwtp_msg_ring_subscribe {
	struct fwtp_msg_base base;
	uint16_t ring_num;
	uint8_t start;
	uint8_t reserved;
	uint32_t notify_byte_count;
} __packed;

/**
 * DOC: FWTP filters
 *
 * FWTP filters may be used to block tracepoints or allow them to pass. In these
 * messages, filters are referenced using a filter index.
 *
 * Information about the filters may be obtained using
 * &struct fwtp_msg_get_filter_info messages of type
 * &fwtp_msg_type.kFwtpMsgTypeGetFilterInfo. This information includes the
 * number of filters and their names.
 *
 * Filters may be configured using &struct fwtp_msg_set_filter_config messages
 * of type &fwtp_msg_type.kFwtpMsgTypeSetFilterConfig.
 */

/**
 * struct fwtp_msg_get_filter_info - FWTP get tracepoint filter info message.
 *
 * This message may be used to obtain information about filters, including the
 * total number of filters in @filter_count and the filter names in
 * @filter_names. The first name returned will be for the filter with the index
 * specified by @starting_filter_index.
 *
 * In the request message, the maximum size of the returned filter names list is
 * specified by @filter_names_size. In the response, @filter_names_size
 * specifies the actual size of the filter names list, and @filter_names_count
 * specifies the number of returned filter names. The @filter_names field
 * contains the null-terminated filter names concatenated in order.
 *
 * The response message also specifies the total size of all null-terminated
 * filter names in @total_filter_names_size
 *
 * @base: Message base.
 * @filter_count: Total number of filters.
 * @total_filter_names_size: Total size of all filter names.
 * @filter_names_size: Size of data in filter names.
 * @filter_names_count: Number of returned filter names.
 * @starting_filter_index: Index of first filter info to get.
 * @filter_names: Table of filter names.
 */
struct fwtp_msg_get_filter_info {
	struct fwtp_msg_base base;
	uint16_t filter_count;
	uint16_t total_filter_names_size;
	uint16_t filter_names_size;
	uint16_t filter_names_count;
	uint16_t starting_filter_index;
	uint8_t filter_names[];
} __packed;

/**
 * struct fwtp_msg_filter_config_entry - FWTP tracepoint filter configuration
 *                                       entry.
 *
 * @filter_index: Index of filter to configure.
 * @blocking: If true, filter should block tracepoints; otherwise, it should not
 *            block them.
 */
struct fwtp_msg_filter_config_entry {
	uint16_t filter_index;
	uint8_t blocking;
} __packed;

/**
 * struct fwtp_msg_set_filter_config - FWTP set tracepoint filter configuration
 *                                     message.
 *
 * @base: Message base.
 * @config_count: Number of entries in the filter configuration list.
 * @config_list: List of filter configuration entries.
 */
struct fwtp_msg_set_filter_config {
	struct fwtp_msg_base base;
	uint16_t config_count;
	struct fwtp_msg_filter_config_entry config_list[];
} __packed;

/**
 * DOC: FWTP add tracepoint
 *
 * FWTP clients may request firmware to generate a tracepoint. This is useful
 * for verification of other FWTP services.
 *
 * Message &struct fwtp_msg_add_tracepoint of type
 * &fwtp_msg_type.kFwtpMsgTypeAddTracepoint can be used to add tracepoint to
 * a particular ring specified in &fwtp_msg_add_tracepoint.ring_num. Field
 * &fwtp_msg_add_tracepoint.payload provides value that firmware can use in
 * generated tracepoint.
 */

/**
 * struct fwtp_msg_add_tracepoint - FWTP add tracepoint message.
 *
 * @base: Message base.
 * @ring_num: Ring number to add tracepoint to.
 * @payload: Payload for tracepoint.
 */
struct fwtp_msg_add_tracepoint {
	struct fwtp_msg_base base;
	uint16_t ring_num;
	uint32_t payload;
} __packed;

#endif /* __FWTP_PROTOCOL_H */
