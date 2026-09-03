/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Copyright 2025 Google LLC.
 *
 * Google firmware tracepoint interface services header.
 *
 * This file is copied from the Pixel firmware sources to the Linux kernel
 * sources, so it's written to be compiled under both Linux and the firmware,
 * and it's licensed under GPL or MIT licenses.
 */

#ifndef __FWTP_IF_H
#define __FWTP_IF_H

#ifndef __KERNEL__
#include <lib/utils/compiler.h>
#include <stdint.h>
#endif

#include "fwtp_err.h"
#include "fwtp_platform.h"

__BEGIN_CDECLS

/* Forward declarations. */
struct fwtp_if;

/**
 * typedef fwtp_if_send_message - Sends a message through the interface.
 *
 * @fwtp_if: The firmware tracepoint interface through which to send message.
 * @msg_buffer: Buffer containing the message.
 * @msg_buffer_size: Size of the message buffer.
 * @tx_msg_data_size: Size of the message data to transmit.
 * @rx_msg_data_size: Size of the received message data.
 *
 * Sends the message contained in the buffer @msg_buffer. The size of the
 * message data to transmit is specified by @tx_msg_data_size.
 *
 * Any received response message is placed in the message buffer @msg_buffer.
 * The size of the data in the received message is returned in
 * @rx_msg_data_size. This will not be larger than the buffer size
 * @msg_buffer_size.
 *
 * Return: kFwtpOk on success, non-zero error code on error.
 */
typedef fwtp_error_code_t(fwtp_if_send_message)(struct fwtp_if *fwtp_if,
						void *msg_buffer,
						uint16_t msg_buffer_size,
						uint16_t tx_msg_data_size,
						uint16_t *rx_msg_data_size);

/**
 * struct fwtp_if - Structure representing an FWTP IPC interface.
 *
 * @platform: FWTP interface platform record.
 * @send_message: Function used to send a message.
 * @send_message_ctx: Context for send message function.
 * @peer_protocol_version: FWTP protocol version of the IPC peer.
 */
struct fwtp_if {
	struct fwtp_if_platform platform;
	fwtp_if_send_message *send_message;
	void *send_message_ctx;
	int peer_protocol_version;
};

__END_CDECLS

#endif /* __FWTP_IPC_CLIENT_H */
