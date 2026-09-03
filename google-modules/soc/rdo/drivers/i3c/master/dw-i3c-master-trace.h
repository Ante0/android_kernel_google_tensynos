// SPDX-License-Identifier: GPL-2.0-only

#undef TRACE_SYSTEM
#define TRACE_SYSTEM i3c_dw

#if !defined(_TRACE_I3C_MASTER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_I3C_MASTER_H

#include <linux/trace.h>
#include <linux/tracepoint.h>
#include "dw-i3c-core.h"

#define show_xfer_type(type) \
	__print_symbolic(type, \
		{ XFER_TYPE_I2C, "I2C" }, \
		{ XFER_TYPE_I3C, "I3C" }, \
		{ XFER_TYPE_CCC, "CCC" }, \
		{ XFER_TYPE_DAA, "DAA" })

#define MAX_DEV_NAME_SIZE 32

TRACE_EVENT(dw_i3c_xfers_start,
	TP_PROTO(struct dw_i3c_master *master, int nxfers, int total_bytes),
	TP_ARGS(master, nxfers, total_bytes),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
		__field(int, nxfers)
		__field(int, total_bytes)
	),
	TP_fast_assign(
		strscpy(__entry->dev_name, dev_name(master->dev), MAX_DEV_NAME_SIZE);
		__entry->nxfers = nxfers;
		__entry->total_bytes = total_bytes;
	),
	TP_printk("[%s]: nxfers=%d total_bytes=%d",
		__entry->dev_name,
		__entry->nxfers,
		__entry->total_bytes
	)
);

TRACE_EVENT(dw_i3c_xfer_queue,
	TP_PROTO(struct dw_i3c_master *master, struct dw_i3c_xfer *xfer),
	TP_ARGS(master, xfer),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
		__field(void *, xfer)
		__field(enum dw_i3c_xfer_type, type)
		__field(unsigned int, ncmds)
	),
	TP_fast_assign(
		strscpy(__entry->dev_name, dev_name(master->dev), MAX_DEV_NAME_SIZE);
		__entry->xfer = xfer;
		__entry->type = xfer->type;
		__entry->ncmds = xfer->ncmds;
	),
	TP_printk("[%s]: xfer=%p type=%s ncmds=%u",
		__entry->dev_name,
		__entry->xfer,
		show_xfer_type(__entry->type),
		__entry->ncmds
	)
);


TRACE_EVENT(dw_i3c_xfer_dequeue,
	TP_PROTO(struct dw_i3c_master *master, struct dw_i3c_xfer *xfer),
	TP_ARGS(master, xfer),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
		__field(void *, xfer)
		__field(int, ret)
	),
	TP_fast_assign(
		strscpy(__entry->dev_name, dev_name(master->dev), MAX_DEV_NAME_SIZE);
		__entry->xfer = xfer;
		__entry->ret = xfer->ret;
	),
	TP_printk("[%s]: xfer=%p ret=%d",
		__entry->dev_name,
		__entry->xfer,
		__entry->ret
	)
);

TRACE_EVENT(dw_i3c_xfer_done,
	TP_PROTO(struct dw_i3c_master *master, struct dw_i3c_xfer *xfer),
	TP_ARGS(master, xfer),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
		__field(void *, xfer)
		__field(int, ret)
	),
	TP_fast_assign(
		strscpy(__entry->dev_name, dev_name(master->dev), MAX_DEV_NAME_SIZE);
		__entry->xfer = xfer;
		__entry->ret = xfer->ret;
	),
	TP_printk("[%s]: xfer=%p ret=%d",
		__entry->dev_name,
		__entry->xfer,
		__entry->ret
	)
);

TRACE_EVENT(dw_i3c_irq,
	TP_PROTO(struct dw_i3c_master *master, u32 status),
	TP_ARGS(master, status),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
		__field(u32, status)
	),
	TP_fast_assign(
		strscpy(__entry->dev_name, dev_name(master->dev), MAX_DEV_NAME_SIZE);
		__entry->status = status;
	),
	TP_printk("[%s]: status=0x%x",
		__entry->dev_name,
		__entry->status
	)
);

TRACE_EVENT(dw_i3c_push_cmd,
	TP_PROTO(struct dw_i3c_master *master, struct dw_i3c_cmd *cmd),
	TP_ARGS(master, cmd),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
		__field(u32, cmd_hi)
		__field(u32, cmd_lo)
	),
	TP_fast_assign(
		strscpy(__entry->dev_name, dev_name(master->dev), MAX_DEV_NAME_SIZE);
		__entry->cmd_hi = cmd->cmd_hi;
		__entry->cmd_lo = cmd->cmd_lo;
	),
	TP_printk("[%s]: hi=0x%08x lo=0x%08x",
		__entry->dev_name,
		__entry->cmd_hi,
		__entry->cmd_lo
	)
);

TRACE_EVENT(dw_i3c_pull_resp,
	TP_PROTO(struct dw_i3c_master *master, u32 resp),
	TP_ARGS(master, resp),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
		__field(u32, resp)
	),
	TP_fast_assign(
		strscpy(__entry->dev_name, dev_name(master->dev), MAX_DEV_NAME_SIZE);
		__entry->resp = resp;
	),
	TP_printk("[%s]: resp=0x%08x (err=%lu tid=%lu len=%lu)",
		__entry->dev_name,
		__entry->resp,
		RESPONSE_PORT_ERR_STATUS(__entry->resp),
		RESPONSE_PORT_TID(__entry->resp),
		RESPONSE_PORT_DATA_LEN(__entry->resp)
	)
);


TRACE_EVENT(dw_i3c_ibi,
	TP_PROTO(struct dw_i3c_master *master, u32 status),
	TP_ARGS(master, status),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
		__field(u32, status)
		__field(u8, addr)
		__field(u8, len)
	),
	TP_fast_assign(
		strscpy(__entry->dev_name, dev_name(master->dev), MAX_DEV_NAME_SIZE);
		__entry->status = status;
		__entry->addr = IBI_QUEUE_IBI_ADDR(status);
		__entry->len = IBI_QUEUE_STATUS_DATA_LEN(status);
	),
	TP_printk("[%s]: status=0x%08x addr=0x%02x len=%d",
		__entry->dev_name,
		__entry->status,
		__entry->addr,
		__entry->len
	)
);

DECLARE_EVENT_CLASS(dw_i3c_resume_suspend_event,
	TP_PROTO(struct device *dev, int ret),
	TP_ARGS(dev, ret),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
		__field(int, ret)
	),
	TP_fast_assign(
		strscpy(__entry->dev_name, dev_name(dev), MAX_DEV_NAME_SIZE);
		__entry->ret = ret;
	),
	TP_printk("[%s]: ret=%d", __entry->dev_name, __entry->ret)
);

DEFINE_EVENT(dw_i3c_resume_suspend_event, dw_i3c_suspend,
	TP_PROTO(struct device *dev, int ret),
	TP_ARGS(dev, ret)
);

DEFINE_EVENT(dw_i3c_resume_suspend_event, dw_i3c_resume,
	TP_PROTO(struct device *dev, int ret),
	TP_ARGS(dev, ret)
);

DEFINE_EVENT(dw_i3c_resume_suspend_event, dw_i3c_runtime_suspend,
	TP_PROTO(struct device *dev, int ret),
	TP_ARGS(dev, ret)
);

DEFINE_EVENT(dw_i3c_resume_suspend_event, dw_i3c_runtime_resume,
	TP_PROTO(struct device *dev, int ret),
	TP_ARGS(dev, ret)
);

void i3c_master_trace_init(struct platform_device *pdev);

#endif /* !defined(_TRACE_I3C_MASTER_H) || defined(TRACE_HEADER_MULTI_READ) */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/i3c/master

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE dw-i3c-master-trace

#include <trace/define_trace.h>
