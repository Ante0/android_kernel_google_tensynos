/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header File for WLAN RPC Call Entry
 *
 * Copyright (c) 2025 Google LLC.
 */

#ifndef __WLAN_RPC_CALL_ENTRY_H__
#define __WLAN_RPC_CALL_ENTRY_H__

#include <linux/types.h>
#include <linux/list.h>
#include "pw_rpc_c/pw_status.h"

typedef void (*wlan_cmd_rpc_callback)(void *callback_context, PwStatus status, const void *response,
				      size_t response_size);

typedef struct wlan_rpc_callback_context {
	wlan_cmd_rpc_callback callback;
	void *context;
	struct list_head callback_context_list;
} wlan_rpc_callback_context;

typedef struct wlan_rpc_call_entry {
	struct list_head callback_context_list;
	struct list_head rpc_call_list;
} wlan_rpc_call_entry;

PwStatus wlan_rpc_call_entry_add_callbacks(wlan_rpc_call_entry *entry,
					   wlan_cmd_rpc_callback callback, void *context);

void wlan_rpc_call_entry_invoke_callbacks(wlan_rpc_call_entry *entry, const PwStatus status,
					  const void *response, size_t response_size);

void wlan_rpc_call_entry_init(wlan_rpc_call_entry *entry);

void wlan_rpc_call_entry_deinit(wlan_rpc_call_entry *entry);

#endif /* __WLAN_RPC_CALL_ENTRY_H__ */
