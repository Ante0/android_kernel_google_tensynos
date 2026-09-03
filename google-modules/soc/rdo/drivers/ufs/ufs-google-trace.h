/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */

// #include "ufs-google.h"
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ufs_google

#if !defined(_TRACE_UFS_GOOGLE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_UFS_GOOGLE_TRACE_H

#include <linux/tracepoint.h>

/* Perfetto track event types */
#define PERFETTO_EVENT_INSTANT 'I'
#define PERFETTO_EVENT_SLICE_OPEN 'B'
#define PERFETTO_EVENT_SLICE_END 'E'

TRACE_EVENT(ufs_google_event,
	TP_PROTO(
		char track_event_type,
		const char *slice_name,
		u32 value
	),
	TP_ARGS(track_event_type, slice_name, value),
	TP_STRUCT__entry(
		__field(char, track_event_type)
		__string(slice_name, slice_name)
		__field(u32, value)
	),
	TP_fast_assign(
		__entry->track_event_type = track_event_type;
		/* kernels before v6.10: __assign_str(slice_name, slice_name) */
		__assign_str(slice_name);
		__entry->value = value;
	),
	TP_printk(
		"type=%c slice_name=%s value=0x%x",
		__entry->track_event_type,
		__get_str(slice_name),
		__entry->value
	)
);

TRACE_EVENT(ufs_google_read_boost,
	TP_PROTO(
		char type,
		bool enabled,
		int result
	),
	TP_ARGS(type, enabled, result),
	TP_STRUCT__entry(
		__field(char, type)
		__field(bool, enabled)
		__field(int, result)
	),
	TP_fast_assign(
		__entry->type = type;
		__entry->enabled = enabled;
		__entry->result = result;
	),
	TP_printk
		("type=%c enabled=%d result=%d",
		__entry->type,
		__entry->enabled,
		__entry->result
	)
);

#endif /* if !defined(_TRACE_UFS_GOOGLE_TRACE_H) */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/ufs
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE ufs-google-trace

/* This part must be outside protection */
#include <trace/define_trace.h>
