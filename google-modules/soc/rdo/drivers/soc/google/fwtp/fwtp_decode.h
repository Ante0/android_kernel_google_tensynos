/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2026 Google LLC.
 *
 * Google firmware tracepoint decoding services header.
 */

#ifndef FWTP_DECODE_H_
#define FWTP_DECODE_H_

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "fwtp.h"
#include "fwtp_ipc_client.h"

#define DECODE_MAX_CLIENTS 100

enum tracepoint_handle {
	CLIENT_TP_HANDLING_COMPLETE,
	CLIENT_TP_HANDLING_NOT_COMPLETE,
	CLIENT_TP_HANDLING_ERROR,
};

/**
 * struct client_tracepoint - A handler for single tracepoint decoding.
 * @enabled: Flag to turn on/off tracepoint decode.
 * @tp_string: String to match for decode.
 * @handler: Decode handler to be called for a string match.
 * @init: Optional callback to initialize internal state of the tracepoint
 *        decode functionality.
 * @exit: Optional callback to clean-up internal state of the tracepoint decode
 *        functionality.
 */
struct client_tracepoint {
	bool enabled;
	const char *tp_string;
	enum tracepoint_handle (*handler)(const char *tp_string, u32 payload,
					  u64 timestamp);
	void (*init)(void);
	void (*exit)(void);
};

/**
 * struct fwtp_decoder - Decoder context for a specific tracepoint subdevice.
 * @clients: Array of client_tracepoint registered to this decode context.
 * @clients_size: Size of clients array.
 * @handler_list: Ordered (by tp_id) list of client_tracepoint_node.
 * @list_mutex: Mutex protecting the handler_list.
 * @list_populated: True if the handler_list has been populated.
 * @list_initialized: True if the handler_list entries are initialized.
 */
struct fwtp_decoder {
	struct client_tracepoint **clients;
	size_t clients_size;
	struct list_head handler_list;
	struct mutex list_mutex;
	bool list_populated;
	bool list_initialized;
};

int fwtp_decoder_init(struct fwtp_decoder *decoder,
		      struct client_tracepoint **clients, size_t clients_size);

void fwtp_decoder_enable(struct fwtp_decoder *decoder,
			 struct fwtp_dev *fwtp_dev, bool enable);

enum tracepoint_handle fwtp_decode_tracepoint(struct fwtp_dev *fwtp_dev,
					      u32 tracepoint_id, u32 payload,
					      u64 timestamp);

#endif /* FWTP_DECODE_H_ */
