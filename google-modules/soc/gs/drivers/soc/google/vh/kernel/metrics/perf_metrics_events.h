/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM pixel_metrics

#if !defined(_PERF_METRICS_EVENTS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _PERF_METRICS_EVENTS_H

#include <linux/tracepoint.h>

TRACE_EVENT(long_irq,

	TP_PROTO(int irq, unsigned int latency, unsigned int count),

	TP_ARGS(irq, latency, count),

	TP_STRUCT__entry(
		__field(int, irq)
		__field(unsigned int, latency)
		__field(unsigned int, count)
	),

	TP_fast_assign(
		__entry->irq = irq;
		__entry->latency = latency;
		__entry->count = count;
	),

	TP_printk("irq=%d latency=%u count=%u",
		  __entry->irq, __entry->latency, __entry->count)
);

TRACE_EVENT(long_softirq,

	TP_PROTO(int vec_nr, unsigned int latency, unsigned int count),

	TP_ARGS(vec_nr, latency, count),

	TP_STRUCT__entry(
		__field(int, vec_nr)
		__field(unsigned int, latency)
		__field(unsigned int, count)
	),

	TP_fast_assign(
		__entry->vec_nr = vec_nr;
		__entry->latency = latency;
		__entry->count = count;
	),

	TP_printk("vec_nr=%d latency=%u count=%u",
		  __entry->vec_nr, __entry->latency, __entry->count)
);

#endif /* _PERF_METRICS_EVENTS_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../private/google-modules/soc/gs/drivers/soc/google/vh/kernel/metrics
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE perf_metrics_events
#include <trace/define_trace.h>
