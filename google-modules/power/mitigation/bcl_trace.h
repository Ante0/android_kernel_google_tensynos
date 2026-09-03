/* SPDX-License-Identifier: GPL-2.0 only */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM bcl

#if !defined(_BCL_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _BCL_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(bcl_ifpmic_irq,
	TP_PROTO(int idx, const char *devname),
	TP_ARGS(idx, devname),
	TP_STRUCT__entry(
		__field(int, idx)
		__string(devname, devname)
	),
	TP_fast_assign(
		__entry->idx = idx;
		__assign_str(devname);
	),
	TP_printk("idx=%d devname=%s", __entry->idx, __get_str(devname))
);

TRACE_EVENT(bcl_ifpmic_serviced,
	TP_PROTO(int idx, const char *devname),
	TP_ARGS(idx, devname),
	TP_STRUCT__entry(
		__field(int, idx)
		__string(devname, devname)
	),
	TP_fast_assign(
		__entry->idx = idx;
		__assign_str(devname);
	),
	TP_printk("idx=%d devname=%s", __entry->idx, __get_str(devname))
);

TRACE_EVENT(bcl_ifpmic_mitigation,
	TP_PROTO(int idx, const char *devname, int throttle_lvl),
	TP_ARGS(idx, devname, throttle_lvl),
	TP_STRUCT__entry(
		__field(int, idx)
		__string(devname, devname)
		__field(int, throttle_lvl)
	),
	TP_fast_assign(
		__entry->idx = idx;
		__assign_str(devname);
		__entry->throttle_lvl = throttle_lvl;
	),
	TP_printk("idx=%d devname=%s throttle_lvl=%d",
		  __entry->idx, __get_str(devname), __entry->throttle_lvl)
);

TRACE_EVENT(bcl_qos_mitigation,
	TP_PROTO(int idx, const char *devname, int freq),
	TP_ARGS(idx, devname, freq),
	TP_STRUCT__entry(
		__field(int, idx)
		__string(devname, devname)
		__field(int, freq)
	),
	TP_fast_assign(
		__entry->idx = idx;
		__assign_str(devname);
		__entry->freq = freq;
	),
	TP_printk("idx=%d devname=%s freq=%d",
		  __entry->idx, __get_str(devname), __entry->freq)
);

#endif /* _BCL_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE bcl_trace
#include <trace/define_trace.h>
