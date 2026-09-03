/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cpm_trace

#if !defined(_TRACE_CPM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CPM_H

#include <linux/tracepoint.h>
#include <linux/api-compat.h>

/* clang-format off */
TRACE_EVENT(cpm_int,
	    TP_PROTO(long long timestamp, const char *tracepoint_body,
		     unsigned int tracepoint_payload),
	    TP_ARGS(timestamp, tracepoint_body, tracepoint_payload),
	    TP_STRUCT__entry(__field(long long, timestamp)
			     __string(body, tracepoint_body)
			     __field(unsigned int, tracepoint_payload)),
	    TP_fast_assign(__entry->timestamp = timestamp;
			   assign_str_wrp(body, tracepoint_body);
			   __entry->tracepoint_payload = tracepoint_payload;),

	    TP_printk("%lld: %s %d", __entry->timestamp, __get_str(body),
		      __entry->tracepoint_payload));

TRACE_EVENT(cpm_string,
	    TP_PROTO(long long timestamp, const char *tracepoint_body,
		     const char *tracepoint_payload),
	    TP_ARGS(timestamp, tracepoint_body, tracepoint_payload),
	    TP_STRUCT__entry(__field(long long, timestamp)
			     __string(body, tracepoint_body)
			     __string(payload, tracepoint_payload)),
	    TP_fast_assign(__entry->timestamp = timestamp;
			   assign_str_wrp(body, tracepoint_body);
			   assign_str_wrp(payload, tracepoint_payload);),

	    TP_printk("%lld: %s %s", __entry->timestamp, __get_str(body),
		      __get_str(payload)));
/* clang-format on */

/* Function to be called before trace_event "param_set_value_cpm" enabled. */
int param_set_value_cpm_enable(void);

/* Function to be called after trace_event "param_set_value_cpm" disabled. */
void param_set_value_cpm_disable(void);

/*
 * Trace "param_set_value_cpm" will be parsed by perfetto to extract the
 * timestamp field from the trace string associated with the trace.
 * This timestamp will be used to replace the kernel ftrace framework provided
 * timestamp for accurate visualization of the event this trace represents.
 */
/* clang-format off */
TRACE_EVENT_FN(param_set_value_cpm,
	    TP_PROTO(const char *param_name, unsigned int value,
		     long long timestamp),
	    TP_ARGS(param_name, value, timestamp),
	    TP_STRUCT__entry(__string(body, param_name)
			     __field(unsigned int, value)
			     __field(long long, timestamp)),
	    TP_fast_assign(assign_str_wrp(body, param_name);
			  __entry->value = value;
			  __entry->timestamp = timestamp;),

	    TP_printk("%s state=%u timestamp=%lld", __get_str(body),
		      __entry->value, __entry->timestamp),
	    param_set_value_cpm_enable, param_set_value_cpm_disable);
/* clang-format on */

#endif /* _TRACE_CPM_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH cpm_decoder
#define TRACE_INCLUDE_FILE cpm_trace
#include <trace/define_trace.h>
