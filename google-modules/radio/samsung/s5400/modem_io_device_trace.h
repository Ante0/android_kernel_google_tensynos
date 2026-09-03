/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM s5400

#if !defined(_MODEM_IO_DEVICE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _MODEM_IO_DEVICE_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(
	s5400_log_watermark,

	TP_PROTO(const char *name, unsigned int queue_len),
	TP_ARGS(name, queue_len),

	TP_STRUCT__entry(
		__string(name, name)
		__field(int, queue_len)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->queue_len = queue_len;
	),
	TP_printk("%s|%u",
		__get_str(name),
		__entry->queue_len
	)
);

#endif /* _MODEM_IO_DEVICE_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE modem_io_device_trace
#include <trace/define_trace.h>
