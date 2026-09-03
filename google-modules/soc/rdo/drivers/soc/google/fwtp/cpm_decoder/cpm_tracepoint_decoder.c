// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2024 Google LLC */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#define CREATE_TRACE_POINTS
#include "cpm_decoder/cpm_trace.h"
#include "cpm_tracepoint_decoder.h"
#include "soc/google/google_timestamp_sync.h"
#include "thermal_tracepoints.h"
#include "perf_tracepoints.h"
#include "power_tracepoints.h"

static DEFINE_MUTEX(client_list_mutex);
bool clients_initialized;
bool clients_id_list_initialized;
static const char *cpm_tracepoint_string_table;
static int cpm_tracepoint_string_table_size;
static int cpm_tracepoint_string_table_offset;
static cpm_tracepoint_decoder_enable_cb_t cpm_tracepoint_decoder_enable_cb;
static void *cpm_tracepoint_decoder_enable_cb_ctx;

#define GET_POWER_TRACEPOINT_POINTER(name, ...) \
	(&(name)),

#define MAX_CLIENTS 100
struct client_tracepoint *all_clients[] = {
	&thermal_tj_pid_curr_state,
#if IS_ENABLED(CONFIG_SOC_LGA)
	&domain_freq_tp,
#endif
	&thermal_tj_sensors,
	&freq_agg_update_tp,
	&dvfsmon_tp,
	POWER_TRACEPOINTS_LIST(GET_POWER_TRACEPOINT_POINTER)
};
static_assert(ARRAY_SIZE(all_clients) <= MAX_CLIENTS);

#undef GET_POWER_TRACEPOINT_POINTER

/**
 * struct client_tracepoint_node - A node in the list of decodable tracepoints.
 * @tp_id: The unique identifier for the tracepoint, corresponding to its
 * offset in the firmware's tracepoint string table. The list is
 * kept sorted by this ID to allow for efficient lookups.
 * @tp:    A pointer to the client_tracepoint struct which contains the handler
 * and other metadata for this specific tracepoint.
 * @list:  The list_head struct for linking this node into the global
 * client_tracepoints list.
 */
struct client_tracepoint_node {
	u32 tp_id;
	struct client_tracepoint *tp;
	struct list_head list;
};
static LIST_HEAD(client_tracepoints);

/**
 * cpm_tracepoint_decode() - Finds and executes a handler for a given tracepoint ID.
 * @tp_id:     The unique id (memory address) of the incoming tracepoint event.
 * @payload:   The 32-bit payload data associated with the tracepoint.
 * @timestamp: The 64-bit timestamp of the event.
 *
 * This function serves as the primary dispatcher for incoming CPM tracepoint
 * events. It is called from a performance-critical context (the FWTP workqueue)
 * for each event received from the firmware.
 *
 * It performs a lookup in the sorted client_tracepoints list to find the
 * matching handler for the given tp_id. If a handler is found, it is executed
 * with the tracepoint's payload and a boot-time adjusted timestamp.
 *
 * Return: An enum tracepoint_handle indicating the status of the decoding,
 * such as CLIENT_TP_HANDLING_COMPLETE or CLIENT_TP_HANDLING_NOT_COMPLETE.
 */
enum tracepoint_handle cpm_tracepoint_decode(u32 tp_id, u32 payload,
					 u64 timestamp)
{

	struct client_tracepoint_node *node;
	int ret = CLIENT_TP_HANDLING_NOT_COMPLETE;

	mutex_lock(&client_list_mutex);
	list_for_each_entry(node, &client_tracepoints, list) {
		if (!node || !node->tp || !node->tp->handler) {
			pr_err("FATAL: Corrupted client tracepoint node!\n");
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
	mutex_unlock(&client_list_mutex);

	return ret;
}

/**
 * initialize_clients_id_list() - Populates the tracepoint list from CPM memory.
 * @buf: Pointer to the CPM's tracepoint string table buffer.
 * @buf_size: The size of the string table buffer.
 * @cpm_table_offset: The base offset of the string table in CPM memory.
 *
 * This function scans the provided string table buffer to find tracepoint
 * strings that match the clients defined in the all_clients array. For each
 * match, it allocates a new client_tracepoint_node, populates it with the
 * calculated tracepoint ID (buffer position + base offset) and a pointer
to the client's static data, and adds it to the global client_tracepoints
 * list.
 *
 * Note: This function must be called with client_list_mutex held.
 *
 * Return: 0 on success, or a negative error code (e.g., -ENOMEM) on failure.
 */
static int initialize_clients_id_list(const char *buf, int buf_size,
				      int cpm_table_offset)
{
	int32_t clients_tp_string_size[MAX_CLIENTS];
	size_t clients_cnt = ARRAY_SIZE(all_clients);

	for (int i = 0; i < clients_cnt; i++)
		clients_tp_string_size[i] = strlen(all_clients[i]->tp_string);

	for (int buf_pos = 0; buf_pos < buf_size; buf_pos++) {
		for (int i = 0; i < clients_cnt; i++) {
			if (buf_pos + clients_tp_string_size[i] >= buf_size)
				continue;
			if (buf[buf_pos + clients_tp_string_size[i]] == '\0' &&
			    strncmp(buf + buf_pos, all_clients[i]->tp_string,
				    clients_tp_string_size[i]) == 0) {
				struct client_tracepoint_node *node = kzalloc(
					sizeof(struct client_tracepoint_node),
					GFP_KERNEL);
				if (!node) {
					struct client_tracepoint_node *node,
						*temp_node;
					list_for_each_entry_safe(
						node, temp_node,
						&client_tracepoints, list) {
						list_del(&node->list);
						kfree(node);
					}
					return -ENOMEM;
				}
				node->tp_id = buf_pos + cpm_table_offset;
				node->tp = all_clients[i];
				list_add_tail(&node->list, &client_tracepoints);
				break;
			}
		}
	}
	clients_id_list_initialized = true;
	return 0;
}

/**
 * client_init_callbacks() - Initializes all tracepoint decoder clients.
 * @buf: Pointer to the firmware's tracepoint string table buffer.
 * @buf_size: The size of the string table buffer.
 * @cpm_table_offset: The base offset of the string table in firmware memory.
 *
 * This is the main entry point for enabling tracepoint decoding. It acquires
 * the mutex to ensure thread safety. If not already done, it calls
 * initialize_clients_id_list() to build the list of tracepoints. It then
 * iterates through the list and calls the init() function for each enabled
 * client. This function is typically called when CPM tracing is activated.
 */
void client_init_callbacks(const char *buf, int buf_size, int cpm_table_offset)
{
	int ret = 0;

	mutex_lock(&client_list_mutex);

	/* If clients already initialized, skip again initializing. */
	if (clients_initialized) {
		mutex_unlock(&client_list_mutex);
		return;
	}

	/* Initialize the list of clients, if not already initialized. */
	if (!clients_id_list_initialized) {
		ret = initialize_clients_id_list(buf, buf_size,
						 cpm_table_offset);

		/* If failed to initialize list of clients, skip the init process. */
		if (ret < 0) {
			mutex_unlock(&client_list_mutex);
			return;
		}
	}

	for (int i = 0; i < ARRAY_SIZE(all_clients); i++)
		if ((all_clients[i]->enabled) && (all_clients[i]->init))
			all_clients[i]->init();

	clients_initialized = true;
	mutex_unlock(&client_list_mutex);
}

/**
 * client_exit_callbacks() - De-initializes all clients and cleans up resources.
 *
 * This is the main exit point for disabling tracepoint decoding. It acquires
 * the mutex to ensure thread safety. It iterates through the list of clients
 * and calls their respective exit() functions to allow them to clean up.
 * Afterwards, it safely dismantles the entire client_tracepoints list,
 * freeing all allocated nodes. This function is typically called when CPM
 * tracing is deactivated.
 */
void client_exit_callbacks(void)
{
	struct client_tracepoint_node *node, *temp_node;

	mutex_lock(&client_list_mutex);
	if (clients_initialized) {
		for (int i = 0; i < ARRAY_SIZE(all_clients); i++)
			if ((all_clients[i]->enabled) && (all_clients[i]->exit))
				all_clients[i]->exit();
		clients_initialized = false;
	}

	if (clients_id_list_initialized) {
		list_for_each_entry_safe(node, temp_node, &client_tracepoints,
					 list) {
			list_del(&node->list);
			kfree(node);
		}
		clients_id_list_initialized = false;
	}
	mutex_unlock(&client_list_mutex);
}

void initialize_cpm_tracepoint_decoder(void)
{
	clients_id_list_initialized = false;
	clients_initialized = false;
	cpm_tracepoint_string_table = NULL;
	cpm_tracepoint_string_table_size = 0;
	cpm_tracepoint_string_table_offset = 0;
	cpm_tracepoint_decoder_enable_cb = NULL;
	cpm_tracepoint_decoder_enable_cb_ctx = NULL;
}

/**
 * cpm_tracepoint_decoder_set_string_table - Sets the string table.
 * @string_table: CPM tracepoint string table.
 * @string_table_size: Size of string table.
 * @string_table_offset: Offset of start of string table.
 *
 * Sets the string table to use for decoding CPM tracepoints.
 */
void cpm_tracepoint_decoder_set_string_table(const char *string_table,
					     int string_table_size,
					     int string_table_offset)
{
	cpm_tracepoint_string_table = string_table;
	cpm_tracepoint_string_table_size = string_table_size;
	cpm_tracepoint_string_table_offset = string_table_offset;
}

/**
 * cpm_tracepoint_decoder_set_enable_cb - Set the tracepoint enable callback.
 * @enable_cb: Callback function to enable or disable CPM tracepoints.
 * @cb_ctx: Context for callback function.
 *
 * Sets the callback to invoke to enable or disable CPM tracepoints.
 */
void cpm_tracepoint_decoder_set_enable_cb(cpm_tracepoint_decoder_enable_cb_t
						  enable_cb,
					  void *cb_ctx)
{
	cpm_tracepoint_decoder_enable_cb = enable_cb;
	cpm_tracepoint_decoder_enable_cb_ctx = cb_ctx;
}

void add_cpm_param_trace(const char *param_name, unsigned int value,
			 unsigned long timestamp)
{
	trace_param_set_value_cpm(param_name, value, timestamp);
}

/**
 * param_set_value_cpm_enable - Handles ftrace "param_set_value_cpm" enabled.
 */
int param_set_value_cpm_enable(void)
{
	int ret = 0;

	/* Initialize tracepoint decoder clients. */
	client_init_callbacks(cpm_tracepoint_string_table,
			      cpm_tracepoint_string_table_size,
			      cpm_tracepoint_string_table_offset);

	/* Enable CPM tracepoints. */
	if (cpm_tracepoint_decoder_enable_cb)
		ret = (*cpm_tracepoint_decoder_enable_cb)(
			cpm_tracepoint_decoder_enable_cb_ctx, true);

	return ret;
}

/**
 * param_set_value_cpm_disable - Handles ftrace "param_set_value_cpm" disabled.
 */
void param_set_value_cpm_disable(void)
{
	/* Disable CPM tracepoints. */
	if (cpm_tracepoint_decoder_enable_cb)
		(*cpm_tracepoint_decoder_enable_cb)(
			cpm_tracepoint_decoder_enable_cb_ctx, false);

	/* Deinitialize tracepoint decoder clients. */
	client_exit_callbacks();
}
MODULE_LICENSE("GPL");

