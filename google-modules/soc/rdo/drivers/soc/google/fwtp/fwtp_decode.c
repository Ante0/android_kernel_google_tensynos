// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2026 Google LLC.
 *
 * Google firmware tracepoint decoding services source.
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "fwtp.h"
#include "fwtp_decode.h"
#include "fwtp_ipc_client.h"

/*******************************************************************************
 * Internal FWTP decoding structures.
 ******************************************************************************/

/**
 * struct client_tracepoint_node - A node in the list of decodable tracepoints.
 * @tp_id: The unique identifier for the tracepoint, corresponding to its
 *         offset in the firmware's tracepoint string table. The list is
 *         kept sorted by this ID to allow for efficient lookups.
 * @tp: A pointer to the client_tracepoint struct which contains the handler
 *      and other metadata for this specific tracepoint.
 * @list: The list_head struct for linking this node into the decoder's
 *        client_tracepoints_node list.
 */
struct client_tracepoint_node {
	u32 tp_id;
	struct client_tracepoint *tp;
	struct list_head list;
};

/*******************************************************************************
 * Internal FWTP decoding services.
 ******************************************************************************/

/**
 * fwtp_decoder_populate_clients - Populates the decoder handler list.
 *
 * @decoder: Tracepoint decoder.
 * @fwtp_dev: FWTP device.
 *
 * Searches the firmware string table for matching tracepoint strings and
 * populates the handler list with their corresponding tracepoint IDs. Populated
 * list is sorted by clients' tp_id.
 *
 * Note: Must be called with decoder->list_mutex locked.
 */
static void fwtp_decoder_populate_clients(struct fwtp_decoder *decoder,
					  struct fwtp_dev *fwtp_dev)
{
	struct fwtp_ipc_client *ipc_client = &(fwtp_dev->fwtp_ipc_client);
	size_t clients_tp_string_size[DECODE_MAX_CLIENTS];
	const char *string_table = ipc_client->string_table;
	int string_table_size = ipc_client->string_table_size;
	int string_table_offset = ipc_client->string_table_offset;

	for (int i = 0; i < decoder->clients_size; i++)
		clients_tp_string_size[i] =
			strlen(decoder->clients[i]->tp_string);

	/* TODO(b/535976618): This can be simplified and optimized. */
	for (int pos = 0; pos < string_table_size; pos++) {
		for (int i = 0; i < decoder->clients_size; i++) {
			/* Skip if tp_string is too long. */
			if (pos + clients_tp_string_size[i] >=
			    string_table_size)
				continue;

			/* Check for match.
			 * String table is not guaranteed to contain only null-terminated
			 * tracepoint strings concatenated one after the other. In particular,
			 * string table can contain non-string data.
			 * Note: Currently there is no easy way to map tp_string to its intended
			 * tp_id - tp_string literal may match in firmware's string table in
			 * multiple places (i.e. as a substring or the same literal may be
			 * declared many times).
			 * To address this issue, client_tracepoint_node is dynamically allocated
			 * for each match in the string table with corresponding tp_id.
			 */
			if (string_table[pos + clients_tp_string_size[i]] ==
				    '\0' &&
			    strncmp(string_table + pos,
				    decoder->clients[i]->tp_string,
				    clients_tp_string_size[i]) == 0) {
				struct client_tracepoint_node *node =
					kzalloc(sizeof(*node), GFP_KERNEL);

				if (!node) {
					struct client_tracepoint_node *n, *tmp;

					dev_err(fwtp_dev->dev,
						"Failed to allocate client tracepoint node.\n");
					list_for_each_entry_safe(
						n, tmp, &decoder->handler_list,
						list) {
						list_del(&n->list);
						kfree(n);
					}
					decoder->list_populated = false;
					return;
				}

				node->tp_id = pos + string_table_offset;
				node->tp = decoder->clients[i];
				list_add_tail(&node->list,
					      &decoder->handler_list);
				break;
			}
		}
	}

	decoder->list_populated = true;
}

/**
 * fwtp_decoder_init_clients - Initializes all tracepoint clients.
 *
 * @decoder: Tracepoint decoder.
 * @fwtp_dev: FWTP device.
 *
 * Invokes the init callback of all enabled client tracepoint handlers.
 */
static void fwtp_decoder_init_clients(struct fwtp_decoder *decoder,
				      struct fwtp_dev *fwtp_dev)
{
	struct client_tracepoint_node *node;

	mutex_lock(&decoder->list_mutex);
	if (!decoder->list_populated)
		fwtp_decoder_populate_clients(decoder, fwtp_dev);

	if (!decoder->list_initialized) {
		list_for_each_entry(node, &decoder->handler_list, list) {
			if (node->tp->enabled && node->tp->init)
				node->tp->init();
		}
		decoder->list_initialized = true;
	}
	mutex_unlock(&decoder->list_mutex);
}

/**
 * fwtp_decoder_exit_clients - Exits all tracepoint clients.
 *
 * @decoder: Tracepoint decoder.
 *
 * Invokes the exit callback of all enabled client tracepoint handlers.
 */
static void fwtp_decoder_exit_clients(struct fwtp_decoder *decoder)
{
	struct client_tracepoint_node *node, *temp_node;

	mutex_lock(&decoder->list_mutex);
	if (decoder->list_initialized) {
		list_for_each_entry(node, &decoder->handler_list, list) {
			if (node->tp->enabled && node->tp->exit)
				node->tp->exit();
		}
		decoder->list_initialized = false;
	}

	if (decoder->list_populated) {
		list_for_each_entry_safe(node, temp_node,
					 &decoder->handler_list, list) {
			list_del(&node->list);
			kfree(node);
		}
		decoder->list_populated = false;
	}
	mutex_unlock(&decoder->list_mutex);
}

/*******************************************************************************
 * External FWTP decoding services.
 ******************************************************************************/

/**
 * fwtp_decoder_init - Initializes a tracepoint decoder.
 *
 * @decoder: Tracepoint decoder to initialize.
 * @clients: Array of client tracepoints.
 * @clients_size: Number of client tracepoints.
 *
 * Note: decoder takes ownership of the provided array of clients.
 *
 * Return: 0 on success, non-zero error code on error.
 */
int fwtp_decoder_init(struct fwtp_decoder *decoder,
		      struct client_tracepoint **clients, size_t clients_size)
{
	if (clients_size > DECODE_MAX_CLIENTS)
		return -EINVAL;

	decoder->clients = clients;
	decoder->clients_size = clients_size;
	INIT_LIST_HEAD(&decoder->handler_list);
	mutex_init(&decoder->list_mutex);
	decoder->list_populated = false;
	decoder->list_initialized = false;

	return 0;
}
EXPORT_SYMBOL_GPL(fwtp_decoder_init);

/**
 * fwtp_decoder_enable - Enables or disables tracepoint decoder.
 *
 * @decoder: Tracepoint decoder.
 * @fwtp_dev: FWTP device.
 * @enable: True to enable, false to disable.
 */
void fwtp_decoder_enable(struct fwtp_decoder *decoder,
			 struct fwtp_dev *fwtp_dev, bool enable)
{
	if (enable)
		fwtp_decoder_init_clients(decoder, fwtp_dev);
	else
		fwtp_decoder_exit_clients(decoder);
}
EXPORT_SYMBOL_GPL(fwtp_decoder_enable);

/**
 * fwtp_decode_tracepoint - Finds and executes a handler for a given tracepoint ID.
 *
 * @fwtp_dev: FWTP device.
 * @tp_id: The unique id of the incoming tracepoint event.
 * @payload: The 32-bit payload data associated with the tracepoint.
 * @timestamp: The 64-bit timestamp of the event.
 *
 * This function serves as the primary dispatcher for incoming tracepoint
 * events. It is called from a performance-critical context (the FWTP workqueue)
 * for each event received from the firmware.
 *
 * It performs a lookup in the sorted decoder handler list to find the
 * matching handler for the given tp_id. If a handler is found, it is executed
 * with the tracepoint's payload and a boot-time adjusted timestamp.
 *
 * Return: An enum tracepoint_handle indicating the status of the decoding,
 * such as CLIENT_TP_HANDLING_COMPLETE or CLIENT_TP_HANDLING_NOT_COMPLETE.
 */
enum tracepoint_handle fwtp_decode_tracepoint(struct fwtp_dev *fwtp_dev,
					      u32 tp_id, u32 payload,
					      u64 timestamp)
{
	struct fwtp_decoder *decoder = fwtp_dev->decoder;
	struct client_tracepoint_node *node;
	int ret = CLIENT_TP_HANDLING_NOT_COMPLETE;

	mutex_lock(&decoder->list_mutex);

	if (!decoder->list_initialized)
		goto out;

	list_for_each_entry(node, &decoder->handler_list, list) {
		if (!node->tp || !node->tp->handler) {
			dev_err(fwtp_dev->dev,
				"FATAL: Corrupted client tracepoint node!\n");
			break;
		}
		if (node->tp_id == tp_id) {
			ret = node->tp->handler(node->tp->tp_string, payload,
						timestamp);
			break;
		} else if (node->tp_id > tp_id) {
			/* Early exit as nodes are kept sorted by their tp_id. */
			break;
		}
	}

out:
	mutex_unlock(&decoder->list_mutex);

	return ret;
}
