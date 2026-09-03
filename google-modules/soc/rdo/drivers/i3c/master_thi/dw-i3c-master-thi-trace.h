/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dw_i3c_master_thi

#if !defined(_DW_I3C_MASTER_THI_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _DW_I3C_MASTER_THI_TRACE_H

#include <linux/trace.h>
#include <linux/tracepoint.h>

TRACE_EVENT(dw_i3c_thi_ccc,
	TP_PROTO(u8 id, bool rnw, unsigned int len),
	TP_ARGS(id, rnw, len),
	TP_STRUCT__entry(
		__field(u8, id)
		__field(bool, rnw)
		__field(unsigned int, len)
	),
	TP_fast_assign(
		__entry->id = id;
		__entry->rnw = rnw;
		__entry->len = len;
	),
	TP_printk("id=0x%02x rnw=%d len=%u", __entry->id, __entry->rnw, __entry->len)
);

TRACE_EVENT(dw_i3c_thi_daa,
	TP_PROTO(const char *stage, int result),
	TP_ARGS(stage, result),
	TP_STRUCT__entry(
		__string(stage, stage)
		__field(int, result)
	),
	TP_fast_assign(
		__assign_str(stage);
		__entry->result = result;
	),
	TP_printk("stage=%s result=%d", __get_str(stage), __entry->result)
);

TRACE_EVENT(dw_i3c_thi_xfer,
	TP_PROTO(u8 addr, bool is_i3c, int nxfers),
	TP_ARGS(addr, is_i3c, nxfers),
	TP_STRUCT__entry(
		__field(u8, addr)
		__field(bool, is_i3c)
		__field(int, nxfers)
	),
	TP_fast_assign(
		__entry->addr = addr;
		__entry->is_i3c = is_i3c;
		__entry->nxfers = nxfers;
	),
	TP_printk("dev_addr=0x%02x type=%s nxfers=%d",
		  __entry->addr, __entry->is_i3c ? "I3C" : "I2C", __entry->nxfers)
);

void dw_i3c_thi_trace_init(struct platform_device *dev);

#endif /* _DW_I3C_MASTER_THI_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/i3c/master_thi

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE dw-i3c-master-thi-trace

#include <trace/define_trace.h>
