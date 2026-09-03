/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 */

#ifndef _GOOGLE_DPA_LOG_PROXY_H
#define _GOOGLE_DPA_LOG_PROXY_H

#include "pw_rpc_c/pw_rpc_client.h"

#include "google_dpa_internal.h"
#include "google_dpa_log_cdev.h"

/*
 * log_info is used as an interface to MCUs (1 source)
 * It is log_handle's job to dispatch the logs (multi sinks)
 */
struct google_dpa_log_info {
	/* log sources */
	/* RPC client, NULL if not listening */
	PwRpcClient *rpc_client;
	/* call id for listen RPC. */
	uint32_t call_id;
	/* log source name */
	const char *name;

	/* log sinks */
	struct google_dpa_log_cdev textlog_cdev;
	struct google_dpa_log_cdev tracepoint_cdev;

	/* callback for pw log streaming */
	void (*pwlog_listen_on_next)(PwRpcCall *call, const uint8_t *payload, size_t payload_size);

	struct device *dev;
};

/*
 * init -> (listen/cancel) -> deinit
 * So that we can guaranteed the life span of cdev is
 * always longer than rpc_clients
 */
// probed
int google_dpa_log_proxy_init(struct google_dpa *dpa);

// on DPA boot, poweroff, debugfs write
int google_dpa_log_proxy_listen(struct google_dpa *dpa);
int google_dpa_log_proxy_cancel(struct google_dpa *dpa);

// remove
int google_dpa_log_proxy_deinit(struct google_dpa *dpa);

#endif /* _GOOGLE_DPA_LOG_PROXY_H */
