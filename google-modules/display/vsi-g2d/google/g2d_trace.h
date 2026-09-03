/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM g2d

#if !defined(_G2D_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _G2D_TRACE_H_

#include <linux/tracepoint.h>
#include <linux/version.h>

TRACE_EVENT(g2d_slice_instant,
	TP_PROTO(char type, const char *track_name, const struct va_format *vaf),
	TP_ARGS(type, track_name, vaf),
	TP_STRUCT__entry(
		__field(char, track_event_type)
		__string(track_name, track_name)
		__vstring(slice_name, vaf->fmt, vaf->va)
	),
	TP_fast_assign(
		__entry->track_event_type = type;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
		__assign_str(track_name, track_name);
#else
		__assign_str(track_name);
#endif
		__assign_vstr(slice_name, vaf->fmt, vaf->va);
	),
	TP_printk("%c|%s|%s",
		  __entry->track_event_type, __get_str(track_name), __get_str(slice_name))
);

TRACE_EVENT(g2d_counter,
	TP_PROTO(
		int counter_value,
		int scope_id,
		const struct va_format *vaf
	),
	TP_ARGS(counter_value, scope_id, vaf),
	TP_STRUCT__entry(
		__field(int, counter_value)
		__field(int, scope_g2d)
		__vstring(track_name, vaf->fmt, vaf->va)
	),
	TP_fast_assign(
		__entry->counter_value = counter_value;
		__entry->scope_g2d = scope_id;
		__assign_vstr(track_name, vaf->fmt, vaf->va);
	),
	TP_printk(
		"%s|%d|%d",
		__get_str(track_name),
		__entry->scope_g2d,
		__entry->counter_value
	)
);

#ifndef __G2D_ATRACE_API_DEF_
#define __G2D_ATRACE_API_DEF_

/* utility function for variadic arguments */
static inline void _g2d_slice_instant(char type, const char *fmt, ...)
{

	va_list args = {0};
	struct va_format vaf = {
		.fmt = fmt,
	};

	va_start(args, fmt);
	vaf.va = &args;
	trace_g2d_slice_instant(type, current->comm, &vaf);
	va_end(args);
}

static inline void _g2d_counter(int value, const char *fmt, ...)
{

	va_list args = {0};
	struct va_format vaf = {
		.fmt = fmt,
	};

	va_start(args, fmt);
	vaf.va = &args;
	trace_g2d_counter(value, 0, &vaf);
	va_end(args);
}


#define G2D_ATRACE_BEGIN(...) _g2d_slice_instant('B', __VA_ARGS__)
#define G2D_ATRACE_END(...) _g2d_slice_instant('E', __VA_ARGS__)
#define G2D_ATRACE_INSTANT(...) _g2d_slice_instant('I', __VA_ARGS__)
#define G2D_ATRACE_INT(value, ...) _g2d_counter(value, __VA_ARGS__)

#endif /* __G2D_ATRACE_API_DEF_ */

#endif /* _G2D_TRACE_H_ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH google

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE g2d_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
