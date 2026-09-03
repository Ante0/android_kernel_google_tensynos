/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Copyright 2024 Google LLC.
 *
 * Google firmware tracepoint IPC client services header.
 *
 * This file is copied from the Pixel firmware sources to the Linux kernel
 * sources, so it's written to be compiled under both Linux and the firmware,
 * and it's licensed under GPL or MIT licenses.
 */

#ifndef __FWTP_IPC_CLIENT_H
#define __FWTP_IPC_CLIENT_H

#ifdef __KERNEL__
#include "fwtp_if.h"
#include "fwtp_printer.h"
#include "fwtp_protocol.h"
#else
#include <interfaces/protocols/mba/fwtp/fwtp_protocol.h>

#include "lib/fwtp/fwtp_if.h"
#include "lib/fwtp/fwtp_printer.h"
#endif

__BEGIN_CDECLS

/* Size of large FWTP IPC client messages. */
#define FWTP_IPC_CLIENT_LARGE_MSG_SIZE 4096
/* Maximum size of an FWTP string table. */
#define FWTP_MAX_STRING_TABLE_SIZE (1024 * 1024)
/* Maximum number of tracepoint rings that a client can use. */
#define FWTP_IPC_CLIENT_MAX_RING_COUNT 2

/**
 * DOC: FWTP IPC clients
 *
 * FWTP provides a set of services that may be used by clients to make requests
 * using IPC. These services may be used to get the tracepoint string table or
 * print tracepoints.
 *
 * In order to use these services, an FWTP client initializes a
 * &struct fwtp_ipc_client record and registers it using
 * fwtp_ipc_client_register(). When an FWTP client is done using FWTP, it should
 * unregister using fwtp_ipc_client_unregister().
 */

/**
 * struct fwtp_ipc_client_ring - Structure used to manage client ring usage.
 *
 * This structure is used to manage use of a tracepoint ring by an FWTP IPC
 * client.
 *
 * @ring_num: Tracepoint ring number.
 * @head_offset: Current tracepoint ring head offset.
 */
struct fwtp_ipc_client_ring {
	uint16_t ring_num;
	uint32_t head_offset;
};

/**
 * struct fwtp_ipc_client - Structure representing an FWTP IPC client.
 *
 * @fwtp_if: FWTP IPC interface.
 * @string_table_num: String table number.
 * @string_table: Table of tracepoint strings.
 * @string_table_offset: Offset of start of string table.
 * @string_table_size: Size of string table.
 * @client_ring_list: List of tracepoint rings used by client.
 * @client_ring_count: Count of the number of tracepoint rings used by client.
 * @filter_count: Count of the number of tracepoint filters.
 * @filter_name_table: Table of filter names.
 * @filter_name_strings: Buffer containing the filter name strings.
 */
struct fwtp_ipc_client {
	struct fwtp_if fwtp_if;
	int string_table_num;
	char *string_table;
	uint32_t string_table_offset;
	uint32_t string_table_size;
	struct fwtp_ipc_client_ring
		client_ring_list[FWTP_IPC_CLIENT_MAX_RING_COUNT];
	int client_ring_count;
	int filter_count;
	char **filter_name_table;
	char *filter_name_strings;
};

fwtp_error_code_t
fwtp_ipc_client_register(struct fwtp_ipc_client *fwtp_ipc_client);
void fwtp_ipc_client_unregister(struct fwtp_ipc_client *fwtp_ipc_client);
fwtp_error_code_t
fwtp_ipc_client_print_tracepoints(struct fwtp_ipc_client *fwtp_ipc_client,
				  struct fwtp_printer_ctx *printer_ctx,
				  bool print_all);
void fwtp_ipc_client_printer_ctx_init(struct fwtp_ipc_client *fwtp_ipc_client,
				      struct fwtp_printer_ctx *printer_ctx);
fwtp_error_code_t
fwtp_ipc_client_get_filter_list(struct fwtp_ipc_client *fwtp_ipc_client);
int fwtp_ipc_client_get_filter_index(struct fwtp_ipc_client *fwtp_ipc_client,
				     const char *filter_name);
fwtp_error_code_t
fwtp_ipc_client_subscribe(struct fwtp_ipc_client *fwtp_ipc_client, bool start,
			  uint32_t notify_byte_count);

/**
 * FWTP_IPC_CLIENT_LOG_ERR - Logs an error message for an FWTP IPC client.
 *
 * @fwtp_ipc_client: FWTP IPC client for which to log message.
 * @msg: Message to log.
 * @...: Arguments for message.
 */
#define FWTP_IPC_CLIENT_LOG_ERR(fwtp_ipc_client, msg, ...) \
	FWTP_IF_LOG_ERR(&((fwtp_ipc_client)->fwtp_if), msg, ##__VA_ARGS__)

/**
 * FWTP_IPC_CLIENT_LOG_WARN - Logs a warning message for an FWTP IPC client.
 *
 * @fwtp_ipc_client: FWTP IPC client for which to log message.
 * @msg: Message to log.
 * @...: Arguments for message.
 */
#define FWTP_IPC_CLIENT_LOG_WARN(fwtp_ipc_client, msg, ...) \
	FWTP_IF_LOG_WARN(&((fwtp_ipc_client)->fwtp_if), msg, ##__VA_ARGS__)

__END_CDECLS

#endif /* __FWTP_IPC_CLIENT_H */
