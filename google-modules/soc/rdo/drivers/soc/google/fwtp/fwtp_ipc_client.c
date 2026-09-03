// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Copyright 2024 Google LLC.
 *
 * Google firmware tracepoint IPC client services source.
 *
 * This file is copied from the Pixel firmware sources to the Linux kernel
 * sources, so it's written to be compiled under both Linux and the firmware,
 * and it's licensed under GPL or MIT licenses.
 */

#ifdef __KERNEL__
// clang-format off: Self include should come first.
#include "fwtp_ipc_client.h"
// clang-format on

#include "fwtp_protocol.h"
#else
// clang-format off: Self include should come first.
#include "lib/fwtp/fwtp_ipc_client.h"
// clang-format on

#include <interfaces/protocols/mba/fwtp/fwtp_protocol.h>
#include <string.h>

#include "lib/utils/array.h"
#endif

/*******************************************************************************
 * Internal FWTP interface services.
 ******************************************************************************/

/**
 * fwtp_ipc_client_get_string_table - Gets the FWTP string table.
 *
 * @fwtp_ipc_client: The FWTP IPC client for which to get the string table.
 *
 * Return: kFwtpOk on success, non-zero error code on error.
 */
static fwtp_error_code_t
fwtp_ipc_client_get_string_table(struct fwtp_ipc_client *fwtp_ipc_client)
{
	struct fwtp_msg_get_strings *msg_get_strings = NULL;
	char *string_table = NULL;
	uint16_t chunk_offset = 0;
	uint16_t chunk_size;
	uint32_t table_size;
	uint16_t rx_msg_data_size;
	fwtp_error_code_t err;

	/* Check if peer supports string table numbers. */
	if (fwtp_ipc_client->fwtp_if.peer_protocol_version <
	    FWTP_PROTOCOL_VERSION_2) {
		err = kFwtpErrUnsupported;
		goto out;
	}

	/* Allocate an FWTP get strings table message buffer. */
	msg_get_strings = FWTP_MALLOC(FWTP_IPC_CLIENT_LARGE_MSG_SIZE);
	if (!msg_get_strings) {
		err = kFwtpErrMemAlloc;
		goto out;
	}

	/*
	 * Get the string table in chunks until the entire table is retrieved.
	 */
	do {
		/* Get the next chunk. */
		memset(msg_get_strings, 0, sizeof(*msg_get_strings));
		msg_get_strings->base.type = kFwtpMsgTypeGetStrings;
		msg_get_strings->table_num = fwtp_ipc_client->string_table_num;
		msg_get_strings->chunk_offset = chunk_offset;
		msg_get_strings->chunk_size =
			FWTP_IPC_CLIENT_LARGE_MSG_SIZE -
			sizeof(struct fwtp_msg_get_strings);
		err = fwtp_ipc_client->fwtp_if.send_message(
			&(fwtp_ipc_client->fwtp_if), msg_get_strings,
			FWTP_IPC_CLIENT_LARGE_MSG_SIZE,
			sizeof(struct fwtp_msg_get_strings), &rx_msg_data_size);
		if (err == kFwtpOk)
			err = msg_get_strings->base.error;
		if (err != kFwtpOk) {
			FWTP_IPC_CLIENT_LOG_ERR(
				fwtp_ipc_client,
				"Get string table request failed with error %d.\n",
				err);
			goto out;
		}

		/* Get the string table offset. */
		fwtp_ipc_client->string_table_offset =
			msg_get_strings->string_table_offset;

		/* Allocate the string table if needed. */
		if (!string_table) {
			table_size = msg_get_strings->table_size;
			if (table_size == 0) {
				FWTP_IPC_CLIENT_LOG_ERR(
					fwtp_ipc_client,
					"String table is empty.\n");
				err = kFwtpErrNotFound;
				goto out;
			}
			if (table_size > FWTP_MAX_STRING_TABLE_SIZE) {
				FWTP_IPC_CLIENT_LOG_ERR(
					fwtp_ipc_client,
					"Table size %d too big.\n", table_size);
				err = kFwtpErrTooBig;
				goto out;
			}
			string_table = FWTP_MALLOC(table_size);
			if (!string_table) {
				err = kFwtpErrMemAlloc;
				goto out;
			}
		}

		/* Add the next chunk to the table. */
		chunk_size = msg_get_strings->chunk_size;
		if (chunk_size > (table_size - chunk_offset)) {
			FWTP_IPC_CLIENT_LOG_WARN(
				fwtp_ipc_client,
				"Last chunk ran past end of string table.\n");
			chunk_size = table_size - chunk_offset;
		}
		memcpy(string_table + chunk_offset, msg_get_strings->chunk,
		       chunk_size);
		chunk_offset += chunk_size;
	} while (chunk_offset < table_size);

out:
	/* On success, move string table to FWTP IPC client. */
	if (err == kFwtpOk) {
		fwtp_ipc_client->string_table = string_table;
		fwtp_ipc_client->string_table_size = table_size;
	} else {
		FWTP_FREE(string_table);
	}

	/* Clean up. */
	FWTP_FREE(msg_get_strings);

	return err;
}

/**
 * fwtp_ipc_client_get_string - Returns the string corresponding to a string ID.
 *
 * @printer_ctx: Printer context.
 * @string_id: ID of string to get.
 *
 * Return: String corresponding to the string ID.
 */
static const char *
fwtp_ipc_client_get_string(struct fwtp_printer_ctx *printer_ctx,
			   uint32_t string_id)
{
	struct fwtp_ipc_client *fwtp_ipc_client = printer_ctx->get_string_ctx;

	return fwtp_lookup_string(fwtp_ipc_client->string_table,
				  fwtp_ipc_client->string_table_size,
				  fwtp_ipc_client->string_table_offset,
				  string_id);
}

/**
 * fwtp_ipc_client_clear_filter_list - Clears the IPC client filter list.
 *
 * @fwtp_ipc_client: FWTP IPC client for which to clear filter list.
 */
static void
fwtp_ipc_client_clear_filter_list(struct fwtp_ipc_client *fwtp_ipc_client)
{
	fwtp_ipc_client->filter_count = 0;
	FWTP_FREE(fwtp_ipc_client->filter_name_table);
	fwtp_ipc_client->filter_name_table = NULL;
	FWTP_FREE(fwtp_ipc_client->filter_name_strings);
	fwtp_ipc_client->filter_name_strings = NULL;
}

/*******************************************************************************
 * External FWTP interface services.
 ******************************************************************************/

/**
 * fwtp_ipc_client_register - Registers an FWTP IPC client.
 *
 * @fwtp_ipc_client: FWTP IPC client to register.
 *
 * Return: kFwtpOk on success, non-zero error code on error.
 */
fwtp_error_code_t
fwtp_ipc_client_register(struct fwtp_ipc_client *fwtp_ipc_client)
{
	struct fwtp_msg_version msg_version = { 0 };
	uint16_t rx_msg_data_size;
	fwtp_error_code_t err;

	/* Exchange protocol versions. */
	msg_version.base.type = kFwtpMsgTypeExchangeVersion;
	msg_version.version = FWTP_PROTOCOL_VERSION;
	err = fwtp_ipc_client->fwtp_if.send_message(
		&(fwtp_ipc_client->fwtp_if), &msg_version,
		sizeof(struct fwtp_msg_version),
		sizeof(struct fwtp_msg_version), &rx_msg_data_size);
	if (err == kFwtpOk)
		err = msg_version.base.error;
	if (err != kFwtpOk) {
		FWTP_IPC_CLIENT_LOG_ERR(
			fwtp_ipc_client,
			"Exchange protocol versions request failed with error %d.\n",
			err);
		goto out;
	}
	fwtp_ipc_client->fwtp_if.peer_protocol_version = msg_version.version;

	/* Get the FWTP string table. */
	err = fwtp_ipc_client_get_string_table(fwtp_ipc_client);
	if (err != kFwtpOk) {
		FWTP_IPC_CLIENT_LOG_ERR(
			fwtp_ipc_client,
			"Failed to get string table with error %d.\n", err);
		goto out;
	}

out:
	/* Clean up on error. */
	if (err != kFwtpOk)
		fwtp_ipc_client_unregister(fwtp_ipc_client);

	return err;
}
EXPORT_SYMBOL_GPL(fwtp_ipc_client_register);

/**
 * fwtp_ipc_client_unregister - Unregisters an FWTP IPC client.
 *
 * @fwtp_ipc_client: FWTP IPC client to unregister.
 */
void fwtp_ipc_client_unregister(struct fwtp_ipc_client *fwtp_ipc_client)
{
	FWTP_FREE(fwtp_ipc_client->string_table);
	fwtp_ipc_client->string_table = NULL;
	fwtp_ipc_client_clear_filter_list(fwtp_ipc_client);
}
EXPORT_SYMBOL_GPL(fwtp_ipc_client_unregister);

/**
 * fwtp_ipc_client_print_tracepoints - Prints the FWTP tracepoints.
 *
 * Prints FWTP tracepoints from all rings in the FWTP IPC client specified by
 * fwtp_ipc_client using the printer context specified by printer_ctx. If
 * print_all is true, prints all of the tracepoints. Otherwise, prints
 * tracepoints starting from the head offset in each FWTP IPC client ring and
 * updates the head offset accordingly.
 *
 * Sets the get_string and get_string_ctx fields in the printer context and
 * updates the timestamp_hz field.
 *
 * @fwtp_ipc_client: The FWTP IPC client for which to print the tracepoints.
 * @printer_ctx: Context to use for printing.
 * @print_all: If true, print all tracepoints; otherwise, print tracepoints from
 *             head.
 *
 * Return: kFwtpOk on success, non-zero error code on error.
 */
fwtp_error_code_t
fwtp_ipc_client_print_tracepoints(struct fwtp_ipc_client *fwtp_ipc_client,
				  struct fwtp_printer_ctx *printer_ctx,
				  bool print_all)
{
	struct fwtp_msg_get_tracepoints *msg_get_tracepoints;
	int client_ring_count;
	int max_client_ring_count;
	fwtp_error_code_t err = kFwtpOk;

	/* Set up printer context. */
	fwtp_ipc_client_printer_ctx_init(fwtp_ipc_client, printer_ctx);

	/* Allocate an FWTP get tracepoints message buffer. */
	msg_get_tracepoints = FWTP_MALLOC(FWTP_IPC_CLIENT_LARGE_MSG_SIZE);
	if (!msg_get_tracepoints) {
		err = kFwtpErrMemAlloc;
		goto out;
	}

	/* Print all tracepoints from all client rings. */
	max_client_ring_count = ARRAY_SIZE(fwtp_ipc_client->client_ring_list);
	client_ring_count = fwtp_ipc_client->client_ring_count <
					    max_client_ring_count ?
				    fwtp_ipc_client->client_ring_count :
				    max_client_ring_count;
	for (int client_ring_index = 0; client_ring_index < client_ring_count;
	     ++client_ring_index) {
		/* Get the client ring. */
		struct fwtp_ipc_client_ring *client_ring =
			&(fwtp_ipc_client->client_ring_list[client_ring_index]);

		/* Get the head offset from which to start printing. */
		uint32_t head_offset =
			(print_all ? 0 : client_ring->head_offset);

		/* Print tracepoints in chunks. */
		do {
			uint16_t rx_msg_data_size;

			/* Get the tracepoints. */
			memset(msg_get_tracepoints, 0,
			       sizeof(*msg_get_tracepoints));
			msg_get_tracepoints->base.type =
				kFwtpMsgTypeGetTracepoints;
			msg_get_tracepoints->head_offset = head_offset;
			msg_get_tracepoints->ring_num = client_ring->ring_num;
			err = fwtp_ipc_client->fwtp_if.send_message(
				&(fwtp_ipc_client->fwtp_if),
				msg_get_tracepoints,
				FWTP_IPC_CLIENT_LARGE_MSG_SIZE,
				sizeof(struct fwtp_msg_get_tracepoints),
				&rx_msg_data_size);
			if (err == kFwtpOk)
				err = msg_get_tracepoints->base.error;
			if (err != kFwtpOk) {
				FWTP_IPC_CLIENT_LOG_ERR(
					fwtp_ipc_client,
					"Get tracepoints request failed with error %d.\n",
					err);
				goto out;
			}
			if (rx_msg_data_size <
			    (sizeof(struct fwtp_msg_get_tracepoints) +
			     msg_get_tracepoints->tracepoints_size)) {
				FWTP_IPC_CLIENT_LOG_ERR(
					fwtp_ipc_client,
					"Received message size %d too short for tracepoints of size %d.\n",
					rx_msg_data_size,
					msg_get_tracepoints->tracepoints_size);
				err = kFwtpErrBadMsg;
				goto out;
			}
			printer_ctx->timestamp_hz =
				msg_get_tracepoints->timestamp_hz;
			if (msg_get_tracepoints->tracepoints_size == 0)
				break;

			/* Print tracepoints. */
			fwtp_print_entries(
				printer_ctx,
				(uint64_t *)msg_get_tracepoints->tracepoints,
				msg_get_tracepoints->tracepoints_size /
					sizeof(uint64_t));

			/* Update the head offset for the next chunk. */
			head_offset = msg_get_tracepoints->head_offset;
		} while (msg_get_tracepoints->head_offset !=
			 msg_get_tracepoints->tail_offset);

		/* Update the client ring head offset if not printing all tracepoints. */
		if (!print_all)
			client_ring->head_offset = head_offset;
	}

out:
	/* Clean up. */
	FWTP_FREE(msg_get_tracepoints);

	return err;
}
EXPORT_SYMBOL_GPL(fwtp_ipc_client_print_tracepoints);

/**
 * fwtp_ipc_client_printer_ctx_init - Initializes printer context.
 *
 * Initializes the printer context specified by printer_ctx for use with the
 * FWTP IPC client specified by fwtp_ipc_client.
 *
 * @fwtp_ipc_client: FWTP IPC client to use with printer context.
 * @printer_ctx: Context ot use for printing with client.
 */
void fwtp_ipc_client_printer_ctx_init(struct fwtp_ipc_client *fwtp_ipc_client,
				      struct fwtp_printer_ctx *printer_ctx)
{
	printer_ctx->get_string = fwtp_ipc_client_get_string;
	printer_ctx->get_string_ctx = fwtp_ipc_client;
}
EXPORT_SYMBOL_GPL(fwtp_ipc_client_printer_ctx_init);

/**
 * fwtp_ipc_client_get_filter_list - Gets the FWTP filter list.
 *
 * @fwtp_ipc_client: FWTP IPC client for which to get the filter list.
 *
 * Return: kFwtpOk on success, non-zero error code on error.
 */
fwtp_error_code_t
fwtp_ipc_client_get_filter_list(struct fwtp_ipc_client *fwtp_ipc_client)
{
	const size_t msg_get_filter_info_size = FWTP_IPC_CLIENT_LARGE_MSG_SIZE;
	struct fwtp_msg_get_filter_info *msg_get_filter_info = NULL;
	char **filter_name_table = NULL;
	char **p_filter_name_table;
	char *filter_name_strings = NULL;
	char *p_filter_name_strings;
	char *p_filter_name_strings_end;
	uint16_t total_filter_names_size;
	uint16_t filter_names_size;
	uint16_t starting_filter_index;
	uint16_t rx_msg_data_size;
	int filter_count;
	int chunk_filter_count;
	fwtp_error_code_t err;

	/* Clear the filter list in case it's previously been set. */
	fwtp_ipc_client_clear_filter_list(fwtp_ipc_client);

	/* Allocate an FWTP get filter info message buffer. */
	msg_get_filter_info = FWTP_MALLOC(msg_get_filter_info_size);
	if (!msg_get_filter_info) {
		err = kFwtpErrMemAlloc;
		goto out;
	}
	filter_names_size = msg_get_filter_info_size -
			    sizeof(struct fwtp_msg_get_filter_info);

	/* Get the filter info in chunks until it's completely retrieved. */
	starting_filter_index = 0;
	do {
		/* Get the next chunk of filter info. */
		memset(msg_get_filter_info, 0,
		       sizeof(struct fwtp_msg_get_filter_info));
		msg_get_filter_info->base.type = kFwtpMsgTypeGetFilterInfo;
		msg_get_filter_info->filter_names_size = filter_names_size;
		msg_get_filter_info->starting_filter_index =
			starting_filter_index;
		err = fwtp_ipc_client->fwtp_if.send_message(
			&(fwtp_ipc_client->fwtp_if), msg_get_filter_info,
			msg_get_filter_info_size,
			sizeof(struct fwtp_msg_get_filter_info),
			&rx_msg_data_size);
		if (err == kFwtpOk)
			err = msg_get_filter_info->base.error;
		if (err != kFwtpOk) {
			FWTP_IPC_CLIENT_LOG_ERR(
				fwtp_ipc_client,
				"Get filter info request failed with error %d.\n",
				err);
			goto out;
		}

		/*
		 * Validate that the received message data size fits within the message
		 * buffer.
		 */
		if (rx_msg_data_size > msg_get_filter_info_size) {
			FWTP_IPC_CLIENT_LOG_ERR(
				fwtp_ipc_client,
				"Received message data size %u too big for buffer.\n",
				rx_msg_data_size);
			err = kFwtpErrBadMsg;
			goto out;
		}

		/*
		 * Validate that the filter names size doesn't extend past the end of the
		 * message.
		 */
		if ((sizeof(struct fwtp_msg_get_filter_info) +
		     msg_get_filter_info->filter_names_size) >
		    rx_msg_data_size) {
			FWTP_IPC_CLIENT_LOG_ERR(
				fwtp_ipc_client,
				"Filter names size %d too big.\n",
				msg_get_filter_info->filter_names_size);
			err = kFwtpErrBadMsg;
			goto out;
		}

		/* Allocate the filter name table if needed. */
		if (!filter_name_table) {
			/* Get the number of filters and total filter names size. */
			filter_count = msg_get_filter_info->filter_count;
			total_filter_names_size =
				msg_get_filter_info->total_filter_names_size;

			/* Allocate table of filter names and filter name string buffer. */
			if (filter_count > 0) {
				filter_name_table = FWTP_MALLOC(filter_count *
								sizeof(char *));
				if (!filter_name_table) {
					err = kFwtpErrMemAlloc;
					goto out;
				}
				filter_name_strings =
					FWTP_MALLOC(total_filter_names_size);
				if (!filter_name_strings) {
					err = kFwtpErrMemAlloc;
					goto out;
				}
			}
			p_filter_name_table = filter_name_table;
			p_filter_name_strings = filter_name_strings;
			p_filter_name_strings_end =
				p_filter_name_strings + total_filter_names_size;
		}

		/* Add the next chunk of filter names to the table. */
		char *p_msg_filter_names =
			(char *)msg_get_filter_info->filter_names;
		char *p_msg_filter_names_end =
			p_msg_filter_names +
			msg_get_filter_info->filter_names_size;
		chunk_filter_count = 0;
		while (p_msg_filter_names < p_msg_filter_names_end) {
			/*
			 * Check if the number of returned filter names exceeds the filter count.
			 */
			if ((starting_filter_index + chunk_filter_count) >=
			    filter_count) {
				FWTP_IPC_CLIENT_LOG_ERR(
					fwtp_ipc_client,
					"Returned filter names exceed filter count.\n");
				err = kFwtpErrBadMsg;
				goto out;
			}

			/* Get the size of the next filter name. */
			size_t max_filter_name_size =
				p_msg_filter_names_end - p_msg_filter_names;
			size_t filter_name_size =
				strnlen(p_msg_filter_names,
					max_filter_name_size) +
				1;
			if (filter_name_size > max_filter_name_size) {
				FWTP_IPC_CLIENT_LOG_ERR(
					fwtp_ipc_client,
					"Filter name not null-terminated.\n");
				err = kFwtpErrBadMsg;
				goto out;
			}

			/* Add the filter name to the client table. */
			if ((p_filter_name_strings + filter_name_size) >
			    p_filter_name_strings_end) {
				FWTP_IPC_CLIENT_LOG_ERR(
					fwtp_ipc_client,
					"Returned filter names exceed total_filter_names_size.\n");
				err = kFwtpErrBadMsg;
				goto out;
			}
			memcpy(p_filter_name_strings, p_msg_filter_names,
			       filter_name_size);
			*p_filter_name_table++ = p_filter_name_strings;
			p_msg_filter_names += filter_name_size;
			p_filter_name_strings += filter_name_size;
			chunk_filter_count++;
		}

		/* Advance to next chunk of filter names. */
		starting_filter_index += chunk_filter_count;
	} while ((starting_filter_index < filter_count) &&
		 (msg_get_filter_info->filter_names_size > 0));

	/* Check if all filter names were returned. */
	if (starting_filter_index < filter_count) {
		FWTP_IPC_CLIENT_LOG_ERR(
			fwtp_ipc_client,
			"Expecting %d filter names, but only received %d.\n",
			filter_count, starting_filter_index);
		err = kFwtpErrBadMsg;
		goto out;
	}

	/* Move filter name table to FWTP IPC client. */
	fwtp_ipc_client->filter_count = filter_count;
	fwtp_ipc_client->filter_name_table = filter_name_table;
	filter_name_table = NULL;
	fwtp_ipc_client->filter_name_strings = filter_name_strings;
	filter_name_strings = NULL;

out:
	/* Clean up. */
	FWTP_FREE(msg_get_filter_info);
	FWTP_FREE(filter_name_table);
	FWTP_FREE(filter_name_strings);

	return err;
}
EXPORT_SYMBOL_GPL(fwtp_ipc_client_get_filter_list);

/**
 * fwtp_ipc_client_get_filter_index - Get the index for a given filter name.
 *
 * @fwtp_ipc_client: FWTP IPC client for which to get filter index.
 * @filter_name: Name of filter for which to get index.
 *
 * Returns the index for the specified filter name. Returns -1 if the filter
 * name is not in the filter list.
 */
int fwtp_ipc_client_get_filter_index(struct fwtp_ipc_client *fwtp_ipc_client,
				     const char *filter_name)
{
	/* Search for the filter name in the filter list and return its index. */
	if (fwtp_ipc_client && filter_name) {
		const int filter_count = fwtp_ipc_client->filter_count;
		char **filter_name_table = fwtp_ipc_client->filter_name_table;

		for (int filter_index = 0; filter_index < filter_count;
		     ++filter_index) {
			if (strcmp(filter_name,
				   filter_name_table[filter_index]) == 0) {
				return filter_index;
			}
		}
	}

	/* Filter name is not in the list. */
	return -1;
}
EXPORT_SYMBOL_GPL(fwtp_ipc_client_get_filter_index);

/**
 * fwtp_ipc_client_subscribe - Subscribes to tracepoint rings used by client.
 *
 * Subscribes to or unsubscribes from all tracepoint rings used by the client
 * specified by fwtp_ipc_client. If start is true, subscribes to tracepoint
 * rings; otherwise, unsubscribes from all tracepoint rings. The subscription
 * notification byte count is specified by notify_byte_count.
 *
 * @fwtp_ipc_client: FWTP IPC client for which to make subscriptions.
 * @start: If true, start subscribing; otherwise, stop subscribing.
 * @notify_byte_count: Send notification after this number of bytes are written
 *                     to ring.
 */
fwtp_error_code_t
fwtp_ipc_client_subscribe(struct fwtp_ipc_client *fwtp_ipc_client, bool start,
			  uint32_t notify_byte_count)
{
	uint16_t rx_msg_data_size;
	int client_ring_count;
	int max_client_ring_count;
	fwtp_error_code_t err;

	/* Subscribe to all tracepoint rings used by client. */
	max_client_ring_count = ARRAY_SIZE(fwtp_ipc_client->client_ring_list);
	client_ring_count = fwtp_ipc_client->client_ring_count <
					    max_client_ring_count ?
				    fwtp_ipc_client->client_ring_count :
				    max_client_ring_count;
	for (int client_ring_index = 0; client_ring_index < client_ring_count;
	     ++client_ring_index) {
		/* Set up the subscription message. */
		struct fwtp_msg_ring_subscribe msg_subscribe = {
			.base.type = kFwtpMsgTypeRingSubscribe,
			.ring_num =
				fwtp_ipc_client
					->client_ring_list[client_ring_index]
					.ring_num,
			.start = start,
			.notify_byte_count = notify_byte_count,
		};

		/* Send the subscription message. */
		err = fwtp_ipc_client->fwtp_if.send_message(
			&(fwtp_ipc_client->fwtp_if), &msg_subscribe,
			sizeof(msg_subscribe), sizeof(msg_subscribe),
			&rx_msg_data_size);
		if (err == kFwtpOk) {
			if (rx_msg_data_size < sizeof(msg_subscribe.base)) {
				FWTP_IPC_CLIENT_LOG_ERR(
					fwtp_ipc_client,
					"Received message size %d too short.\n",
					rx_msg_data_size);
				return kFwtpErrBadMsg;
			}
			err = msg_subscribe.base.error;
		}
		if (err != kFwtpOk) {
			FWTP_IPC_CLIENT_LOG_ERR(
				fwtp_ipc_client,
				"Ring subscription failed for client ring index %d with error %d.\n",
				client_ring_index, err);
			return err;
		}
	}

	return kFwtpOk;
}
EXPORT_SYMBOL_GPL(fwtp_ipc_client_subscribe);
