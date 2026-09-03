/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM uart

#if !defined(_TRACE_UART_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_UART_H

#include <linux/trace.h>
#include <linux/tracepoint.h>

#define MAX_DEV_NAME_SIZE 32

TRACE_EVENT(dw8250_function_entry,
	TP_PROTO(struct device *dev, const char *func),
	TP_ARGS(dev, func),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
		__string(func, func)
	),
	TP_fast_assign(
		scnprintf(__entry->dev_name, MAX_DEV_NAME_SIZE, "%s", dev_name(dev));
		__assign_str(func);
	),
	TP_printk("%s: %s", __entry->dev_name, __get_str(func))
);

TRACE_EVENT(dw8250_do_pm,
	TP_PROTO(struct device *dev, u32 state, u32 old),
	TP_ARGS(dev, state, old),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
		__field(u32, state)
		__field(u32, old)
	),
	TP_fast_assign(
		scnprintf(__entry->dev_name, MAX_DEV_NAME_SIZE, "%s", dev_name(dev));
		__entry->state = state;
		__entry->old = old;
	),
	TP_printk("%s: state %d, old %d",
		__entry->dev_name, __entry->state, __entry->old)
	);

TRACE_EVENT(dw8250_handle_irq,
	TP_PROTO(struct device *dev, u32 iir, u32 lsr),
	TP_ARGS(dev, iir, lsr),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
		__field(u32, iir)
		__field(u32, lsr)
	),
	TP_fast_assign(
		scnprintf(__entry->dev_name, MAX_DEV_NAME_SIZE, "%s", dev_name(dev));
		__entry->iir = iir;
		__entry->lsr = lsr;
	),
	TP_printk("%s: iir %#08x, lsr %#08x",
		__entry->dev_name, __entry->iir, __entry->lsr)
	);

void google_8250_trace_init(struct platform_device *pdev);

#endif /* _TRACE_UART_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/tty/serial/8250

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE google_8250_trace

#include <trace/define_trace.h>
