/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dw_i3c_xfer

#if !defined(_DW_I3C_XFER_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _DW_I3C_XFER_TRACE_H

#include <linux/trace.h>
#include <linux/tracepoint.h>

TRACE_EVENT(dw_i3c_xfer_irq,
	TP_PROTO(u32 status),
	TP_ARGS(status),
	TP_STRUCT__entry(
		__field(u32, status)
	),
	TP_fast_assign(
		__entry->status = status;
	),
	TP_printk("status=0x%08x", __entry->status)
);

TRACE_EVENT(dw_i3c_xfer_cmd_push,
	TP_PROTO(u32 cmd_hi, u32 cmd_lo),
	TP_ARGS(cmd_hi, cmd_lo),
	TP_STRUCT__entry(
		__field(u32, cmd_hi)
		__field(u32, cmd_lo)
	),
	TP_fast_assign(
		__entry->cmd_hi = cmd_hi;
		__entry->cmd_lo = cmd_lo;
	),
	TP_printk("cmd_hi=0x%08x cmd_lo=0x%08x", __entry->cmd_hi, __entry->cmd_lo)
);

TRACE_EVENT(dw_i3c_xfer_resp_pull,
	TP_PROTO(u32 resp),
	TP_ARGS(resp),
	TP_STRUCT__entry(
		__field(u32, resp)
	),
	TP_fast_assign(
		__entry->resp = resp;
	),
	TP_printk("resp=0x%08x", __entry->resp)
);

TRACE_EVENT(dw_i3c_xfer_exec_start,
	TP_PROTO(int type, unsigned int ncmds),
	TP_ARGS(type, ncmds),
	TP_STRUCT__entry(
		__field(int, type)
		__field(unsigned int, ncmds)
	),
	TP_fast_assign(
		__entry->type = type;
		__entry->ncmds = ncmds;
	),
	TP_printk("type=%d ncmds=%u", __entry->type, __entry->ncmds)
);

TRACE_EVENT(dw_i3c_xfer_exec_done,
	TP_PROTO(int ret),
	TP_ARGS(ret),
	TP_STRUCT__entry(
		__field(int, ret)
	),
	TP_fast_assign(
		__entry->ret = ret;
	),
	TP_printk("ret=%d", __entry->ret)
);

TRACE_EVENT(dw_i3c_xfer_tx_data,
	TP_PROTO(unsigned int len, u32 buf_lvl),
	TP_ARGS(len, buf_lvl),
	TP_STRUCT__entry(
		__field(unsigned int, len)
		__field(u32, buf_lvl)
	),
	TP_fast_assign(
		__entry->len = len;
		__entry->buf_lvl = buf_lvl;
	),
	TP_printk("wrote_bytes=%u tx_buf_lvl=%u", __entry->len, __entry->buf_lvl)
);

TRACE_EVENT(dw_i3c_xfer_rx_data,
	TP_PROTO(unsigned int len, u32 buf_lvl),
	TP_ARGS(len, buf_lvl),
	TP_STRUCT__entry(
		__field(unsigned int, len)
		__field(u32, buf_lvl)
	),
	TP_fast_assign(
		__entry->len = len;
		__entry->buf_lvl = buf_lvl;
	),
	TP_printk("read_bytes=%u rx_buf_lvl=%u", __entry->len, __entry->buf_lvl)
);

TRACE_EVENT(dw_i3c_xfer_thld_update,
	TP_PROTO(u32 q_thld, u32 d_thld, u32 sig_en),
	TP_ARGS(q_thld, d_thld, sig_en),
	TP_STRUCT__entry(
		__field(u32, q_thld)
		__field(u32, d_thld)
		__field(u32, sig_en)
	),
	TP_fast_assign(
		__entry->q_thld = q_thld;
		__entry->d_thld = d_thld;
		__entry->sig_en = sig_en;
	),
	TP_printk("queue_thld=0x%08x data_thld=0x%08x signal_en=0x%08x",
		  __entry->q_thld, __entry->d_thld, __entry->sig_en)
);

void dw_i3c_xfer_host_trace_init(struct platform_device *dev);

#endif /* _DW_I3C_XFER_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/i3c/master_thi

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE dw-i3c-xfer-host-trace

#include <trace/define_trace.h>
