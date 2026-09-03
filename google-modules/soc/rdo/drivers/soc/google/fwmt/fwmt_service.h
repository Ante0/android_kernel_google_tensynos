/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Copyright 2025 Google LLC.
 *
 * Google firmware metrics (FWMT) protocol header.
 *
 * This header is copied from the Pixel firmware sources to the Linux kernel
 * sources, so it's written to be compiled under both Linux and the firmware,
 * and it's licensed under GPL or MIT licenses.
 */

#ifndef __FWMT_SERVICE_H
#define __FWMT_SERVICE_H

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#ifndef static_assert
#define static_assert _Static_assert
#endif

/**
 * struct fwmt_mba_msg - FWMT mailbox message.
 *
 * @msg_phys_addr_lo: Lower 32 bits of the shared buffer physical address.
 * @msg_phys_addr_hi: Upper 32 bits of the shared buffer physical address.
 * @msg_buffer_size: Total allocated size of the shared memory buffer. This is
 *                   needed to ensure the firmware does not read or write past
 *                   the end of the allocated memory.
 * @msg_data_size: Size of the actual message payload data currently residing in
 *                 the buffer. This tells the firmware exactly how many bytes
 *                 to read for the request, and allows the firmware to return
 *                 the exact size of the response payload.
 */
struct fwmt_mba_msg {
	uint32_t msg_phys_addr_lo;
	uint32_t msg_phys_addr_hi;
	uint16_t msg_buffer_size;
	uint16_t msg_data_size;
};
static_assert(sizeof(struct fwmt_mba_msg) == 12, "fwmt_mba_msg size mismatch");

/**
 * enum fwmt_msg_type - Set of FWMT message types.
 *
 * @kFwmtMsgTypeRetrieveString: Retrieve string table message.
 * @kFwmtMsgTypeRetrieveMetric: Retrieve metric table message.
 */
enum fwmt_msg_type {
	kFwmtMsgTypeRetrieveString = 0,
	kFwmtMsgTypeRetrieveMetric = 1,
};

/**
 * struct fwmt_msg_base - Base FWMT message.
 *
 * @type: Message type.
 * @error: Message error code. A value of 0 means no error; otherwise, an error
 *         occurred.
 * @reserved: Reserved. Set to 0 when writing and ignore when reading.
 *
 * This structure defines the base FWMT message. All FWMT messages in the shared
 * buffer start with this structure.
 */
struct fwmt_msg_base {
	uint8_t type;
	uint8_t error;
	uint16_t reserved;
};
static_assert(sizeof(struct fwmt_msg_base) == 4, "fwmt_msg_base size mismatch");

/**
 * struct fwmt_msg_retrieve_request - FWMT retrieve resource request message.
 *
 * @base: Message base.
 * @resource_offset: Offset of the requested data within the resource.
 */
struct fwmt_msg_retrieve_request {
	struct fwmt_msg_base base;
	uint32_t resource_offset;
};
static_assert(sizeof(struct fwmt_msg_retrieve_request) == 8,
	      "fwmt_msg_retrieve_request size mismatch");

/**
 * struct fwmt_msg_retrieve_response - FWMT retrieve resource response message.
 *
 * @base: Message base.
 * @size: Size of the resource data returned in this chunk.
 * @total_size: Total size of the requested resource.
 * @data: Resource chunk data.
 */
struct fwmt_msg_retrieve_response {
	struct fwmt_msg_base base;
	uint32_t size;
	uint32_t total_size;
	uint8_t data[];
};
static_assert(sizeof(struct fwmt_msg_retrieve_response) == 12,
	      "fwmt_msg_retrieve_response size mismatch");

#endif /* __FWMT_SERVICE_H */
