/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024, Google LLC
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM google_icc_trace

#if !defined(_TRACE_GOOGLE_ICC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GOOGLE_ICC_H

#include <linux/tracepoint.h>

#define TPS(x)  tracepoint_string(x)

TRACE_EVENT(google_icc_event,
	    TP_PROTO(const char *s, u64 timestamp),
	    TP_ARGS(s, timestamp),
	    TP_STRUCT__entry(__field(const char *, s)
			     __field(u64, timestamp)),
	    TP_fast_assign(__entry->s = s;
			   __entry->timestamp = timestamp;),
	    TP_printk("event: %s, timestamp: %llu\n", __entry->s, __entry->timestamp));

TRACE_EVENT(google_icc_event_with_ret,
	    TP_PROTO(const char *s, int ret, u64 timestamp),
	    TP_ARGS(s, ret, timestamp),
	    TP_STRUCT__entry(__field(const char *, s)
			     __field(int, ret)
			     __field(u64, timestamp)),
	    TP_fast_assign(__entry->s = s;
			   __entry->ret = ret;
			   __entry->timestamp = timestamp;),
	    TP_printk("event: %s, ret = %d, timestamp: %llu\n",
		      __entry->s, __entry->ret, __entry->timestamp));

#endif /* _TRACE_GOOGLE_ICC_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE google_icc_trace
#include <trace/define_trace.h>
