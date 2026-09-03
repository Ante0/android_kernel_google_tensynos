// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2025 Google LLC */

#include <linux/kernel.h>
#include "cpm_tracepoint_decoder.h"
#include "power_tracepoints.h"

#define CURR_STATE_STR_LEN 64

static enum tracepoint_handle power_payload_handler(const char *tp_string, u32 payload,
						u64 timestamp)
{
	char clock_name[CURR_STATE_STR_LEN];

	scnprintf(clock_name, sizeof(clock_name), "%s", tp_string);
	add_cpm_param_trace(clock_name, payload, timestamp);

	return CLIENT_TP_HANDLING_COMPLETE;
}

#define DEFINE_POWER_TRACEPOINT(name, tp_str, function) \
	struct client_tracepoint name = { \
		.enabled = true, \
		.tp_string = tp_str, \
		.init = NULL, \
		.handler = function, \
		.exit = NULL \
	};

POWER_TRACEPOINTS_LIST(DEFINE_POWER_TRACEPOINT)

#undef DEFINE_POWER_TRACEPOINT
