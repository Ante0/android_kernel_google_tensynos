// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA WLAN RPC Call Entry
 *
 * Copyright (c) 2025 Google LLC.
 */

#include <linux/slab.h>
#include <linux/list.h>
#include "pw_rpc_c/pw_status.h"
#include "wlan_rpc_call_entry.h"

PwStatus wlan_rpc_call_entry_add_callbacks(wlan_rpc_call_entry *entry,
					   wlan_cmd_rpc_callback callback, void *context)
{
	wlan_rpc_callback_context *callback_context;
	callback_context = kmalloc(sizeof(*callback_context), GFP_KERNEL);

	if (callback_context == NULL) {
		return kPwStatusResourceExhausted;
	}

	callback_context->callback = callback;
	callback_context->context = context;
	INIT_LIST_HEAD(&callback_context->callback_context_list);

	list_add(&callback_context->callback_context_list, &entry->callback_context_list);

	return kPwStatusOk;
}

void wlan_rpc_call_entry_invoke_callbacks(wlan_rpc_call_entry *entry, const PwStatus status,
					  const void *response, size_t response_size)
{
	struct list_head *pos, *tmp;
	wlan_rpc_callback_context *callback_context;

	list_for_each_safe (pos, tmp, &entry->callback_context_list) {
		callback_context =
			list_entry(pos, struct wlan_rpc_callback_context, callback_context_list);
		if (callback_context->callback != NULL) {
			callback_context->callback(callback_context->context, status, response,
						   response_size);
		}
	}
}

void wlan_rpc_call_entry_init(wlan_rpc_call_entry *entry)
{
	INIT_LIST_HEAD(&entry->rpc_call_list);
	INIT_LIST_HEAD(&entry->callback_context_list);
}

void wlan_rpc_call_entry_deinit(wlan_rpc_call_entry *entry)
{
	struct list_head *pos, *tmp;
	wlan_rpc_callback_context *callback_context;

	list_for_each_safe (pos, tmp, &entry->callback_context_list) {
		callback_context =
			list_entry(pos, struct wlan_rpc_callback_context, callback_context_list);
		kfree(callback_context);
	}
}
