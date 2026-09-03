/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cpufreq_trace

#if !defined(_TRACE_CPUFREQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CPUFREQ_H

#include <linux/tracepoint.h>

TRACE_EVENT(cpufreq_fastswitch,
	    TP_PROTO(unsigned int cpu, unsigned int target_freq),
	    TP_ARGS(cpu, target_freq),
	    TP_STRUCT__entry(__field(unsigned int, cpu)
			     __field(unsigned int, target_freq)),
	    TP_fast_assign(__entry->cpu = cpu,
			   __entry->target_freq = target_freq;),
	    TP_printk("cpu: %u, freq: %u", __entry->cpu, __entry->target_freq));

#endif /* _TRACE_CPUFREQ_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE cpufreq_trace
#include <trace/define_trace.h>
