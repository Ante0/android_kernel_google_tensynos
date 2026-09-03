/* SPDX-License-Identifier: GPL-2.0-only */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM lpm_pm_domains

#if !defined(LPM_PM_DOMAINS_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define LPM_PM_DOMAINS_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(lpm_pm_latency_event,
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

#define LPM_PM_TRACE_BEGIN(_name, _pd_name) \
	trace_lpm_pm_latency_event('B', _name, _pd_name)
#define LPM_PM_TRACE_END(_name, _pd_name) \
	trace_lpm_pm_latency_event('E', _name, _pd_name)

#endif /* LPM_PM_DOMAINS_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/soc/google/lpm-pm-domains

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE lpm-pm-domains-trace

#include <trace/define_trace.h>
