/* SPDX-License-Identifier: GPL-2.0-only */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM pd_latency

#if !defined(PD_LATENCY_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define PD_LATENCY_TRACE_H

#include <linux/trace.h>
#include <linux/tracepoint.h>
#include <linux/api-compat.h>
#include <linux/types.h>

TRACE_EVENT(pd_latency_event,
	TP_PROTO(
		/* Slicing definition */
		char track_event_type,
		const char *slice_name,
		/* Rest */
		const char *pd_name
	),
	TP_ARGS(track_event_type, slice_name, pd_name),
	TP_STRUCT__entry(
		__field(char, track_event_type)
		__array(char, slice_name, 64)
	),
	TP_fast_assign(
		__entry->track_event_type = track_event_type;
		snprintf(__entry->slice_name, 64, "%s_%s", slice_name, pd_name);
	),
	TP_printk("%c|%s",
		  __entry->track_event_type,
		  __entry->slice_name)
);

TRACE_EVENT(pd_latency_store,
	TP_PROTO(
		/* Slicing definition */
		const char *slice_name,
		/* Rest */
		const char *pd_name,
		u32 stored_time
	),
	TP_ARGS(slice_name, pd_name, stored_time),
	TP_STRUCT__entry(
		__field(char, track_event_type)
		__array(char, slice_name, 64)
		__field(u32, stored_time)
	),
	TP_fast_assign(
		__entry->track_event_type = 'I';
		snprintf(__entry->slice_name, 64, "%s_%s", slice_name, pd_name);
		__entry->stored_time = stored_time;
	),
	TP_printk("%c|%s|%u",
		  __entry->track_event_type,
		  __entry->slice_name,
		  __entry->stored_time)
);

#define PD_LATENCY_TRACE_BEGIN(_pd, _name) \
	trace_pd_latency_event('B', _name, _pd->name)
#define PD_LATENCY_TRACE_END(_pd, _name) \
	trace_pd_latency_event('E', _name, _pd->name)
#define PD_LATENCY_TRACE_INSTANT(_pd, _name, _time_stored) \
	trace_pd_latency_store(_name, _pd->name, _time_stored)

#endif /* PD_LATENCY_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../power-controller/latency

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE pd_latency_trace

#include <trace/define_trace.h>
