/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DPU trace support
 *
 * Copyright (C) 2020 Google, Inc.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dpu

#if !defined(_DPU_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _DPU_TRACE_H_

#include <linux/tracepoint.h>

TRACE_EVENT(reg_dump_header, TP_PROTO(const char *desc, u32 offset, u32 size),
	TP_ARGS(desc, offset, size),
	TP_STRUCT__entry(
			__string(desc, desc)
			__field(u32, offset)
			__field(u32, size)
		),
	TP_fast_assign(
			__assign_str(desc);
			__entry->offset = offset;
			__entry->size = size;
		),
	TP_printk("%s offset:%u size:%u", __get_str(desc), __entry->offset, __entry->size)
);

TRACE_EVENT(reg_dump_line,
	TP_PROTO(u32 offset, const char *line_buf),
	TP_ARGS(offset, line_buf),
	TP_STRUCT__entry(
		__field(u32, offset)
		__string(line_buf, line_buf)
	),
	TP_fast_assign(
		__entry->offset = offset;
		__assign_str(line_buf);
	),
	TP_printk("%08X: %s", __entry->offset, __get_str(line_buf))
);

TRACE_EVENT(tracing_mark_write,
	TP_PROTO(char type, int pid, struct va_format *vaf, int value),
	TP_ARGS(type, pid, vaf, value),
	TP_STRUCT__entry(
		__field(char, type)
		__field(int, pid)
		__vstring(name, vaf->fmt, vaf->va)
		__field(int, value)
	),
	TP_fast_assign(
		__entry->type = type;
		__entry->pid = pid;
		__assign_vstr(name, vaf->fmt, vaf->va);
		__entry->value = value;
	),
	TP_printk("%c|%d|%s|%d",
		__entry->type, __entry->pid, __get_str(name), __entry->value)
);

DECLARE_EVENT_CLASS(dpu_underrun,
	TP_PROTO(int id, int frames_pending, int vsync_count),
	TP_ARGS(id, frames_pending, vsync_count),
	TP_STRUCT__entry(
		__field(int, id)
		__field(int, frames_pending)
		__field(int, vsync_count)
	),
	TP_fast_assign(
		__entry->id = id;
		__entry->frames_pending = frames_pending;
		__entry->vsync_count = vsync_count;
	),
	TP_printk("id: %d frames_pending: %d vsync_count: %d",
		__entry->id, __entry->frames_pending, __entry->vsync_count)
);

/* frame-based underrun */
DEFINE_EVENT(dpu_underrun, disp_dpu_underrun,
	TP_PROTO(int id, int frames_pending, int vsync_count),
	TP_ARGS(id, frames_pending, vsync_count)
);

/* scanline-based underrun */
DEFINE_EVENT(dpu_underrun, disp_dpu_line_underrun,
	TP_PROTO(int id, int frames_pending, int vsync_count),
	TP_ARGS(id, frames_pending, vsync_count)
);

TRACE_EVENT(disp_vblank_irq_enable,
	TP_PROTO(int id, int output_id, bool enable),
	TP_ARGS(id, output_id, enable),
	TP_STRUCT__entry(
		__field(int, id)
		__field(int, output_id)
		__field(int, enable)
	),
	TP_fast_assign(
		__entry->id = id;
		__entry->output_id = output_id;
		__entry->enable = enable;
	),
	TP_printk("id: %d output_id: %d %s",
		__entry->id, __entry->output_id, __entry->enable ? "enable" : "disable")
);

DECLARE_EVENT_CLASS(display,
	TP_PROTO(int display_id, u32 output_id, int frames_pending, int te_count),
	TP_ARGS(display_id, output_id, frames_pending, te_count),
	TP_STRUCT__entry(
		__field(int, display_id)
		__field(u32, output_id)
		__field(int, frames_pending)
		__field(int, te_count)
	),
	TP_fast_assign(
		__entry->display_id = display_id;
		__entry->output_id = output_id;
		__entry->frames_pending = frames_pending;
		__entry->te_count = te_count;
	),
	TP_printk("display_id: %d output_id: %u frames_pending: %d te_count: %d",
		__entry->display_id,  __entry->output_id, __entry->frames_pending,
		__entry->te_count)
);

DEFINE_EVENT(display, disp_frame_start_timeout,
	TP_PROTO(int display_id, u32 output_id, int frames_pending, int te_count),
	TP_ARGS(display_id, output_id, frames_pending, te_count)
);
DEFINE_EVENT(display, disp_frame_start_missing,
	TP_PROTO(int display_id, u32 output_id, int frames_pending, int te_count),
	TP_ARGS(display_id, output_id, frames_pending, te_count)
);
DEFINE_EVENT(display, disp_frame_done_missing,
	TP_PROTO(int display_id, u32 output_id, int frames_pending, int te_count),
	TP_ARGS(display_id, output_id, frames_pending, te_count)
);

TRACE_EVENT(disp_frame_done_timeout,
	TP_PROTO(int display_id, u32 output_id, int frames_pending, int te_count,
		bool during_disable),
	TP_ARGS(display_id, output_id, frames_pending, te_count, during_disable),
	TP_STRUCT__entry(
		__field(int, display_id)
		__field(u32, output_id)
		__field(int, frames_pending)
		__field(int, te_count)
		__field(bool, during_disable)
	),
	TP_fast_assign(
		__entry->display_id = display_id;
		__entry->output_id = output_id;
		__entry->frames_pending = frames_pending;
		__entry->te_count = te_count;
		__entry->during_disable = during_disable;
	),
	TP_printk("display_id: %d output_id: %d frames_pending: %d te_count: %d%s",
		__entry->display_id, __entry->output_id, __entry->frames_pending,
		__entry->te_count, __entry->during_disable ? " during disable" : "")
);


/* extra define flag for the case TRACE_HEAD_MULTI_READ & _DPU_TRACE_H both set */
#ifndef __DPU_ATRACE_API_DEF_
#define __DPU_ATRACE_API_DEF_

/* utility function for variadic arguments */
static inline void _tracing_mark_write(char type, int pid, int value, const char *fmt, ...)
{
	va_list args = {0};
	struct va_format vaf = {
		.fmt = fmt,
	};

	va_start(args, fmt);
	vaf.va = &args;
	trace_tracing_mark_write(type, pid, &vaf, value);
	va_end(args);
}

/**
 * DPU_ATRACE_INT_PID_FMT() - used to trace an integer value for a formatted variable
 * @value: Value of variable to trace
 * @pid: Attach trace log to specific process ID
 *
 * Used to trace a formatted variable or counter with an integer value
 */
#define DPU_ATRACE_INT_PID_FMT(value, pid, ...) \
	_tracing_mark_write('C', pid, value, __VA_ARGS__)

/**
 * DPU_ATRACE_INT_PID() - used to trace an integer value
 * @name: Name of variable to trace; does not support format string
 * @value: Value of variable to trace
 * @pid: Attach trace log to specific process ID
 *
 * Used to trace a variable or counter with an integer value
 */
#define DPU_ATRACE_INT_PID(name, value, pid) \
	DPU_ATRACE_INT_PID_FMT(value, pid, name)

/**
 * DPU_ATRACE_INT() - used to trace an integer value
 * @name: Name of variable to trace; does not support format string
 * @value: Value of variable to trace
 *
 * Used to trace a variable or counter with an integer value
 */
#define DPU_ATRACE_INT(name, value) \
	DPU_ATRACE_INT_PID(name, value, current->tgid)

/**
 * DPU_ATRACE_BEGIN() - used to trace beginning of a scope
 * @...: Name of scope to trace; supports format string
 *
 * Used to trace a scope of time. Often used for function duration,
 * but may be used to keep track of the duration of more high-level operations.
 */
#define DPU_ATRACE_BEGIN(...) \
	_tracing_mark_write('B', current->tgid, 0, __VA_ARGS__)

/**
 * DPU_ATRACE_END() - used to trace end of a scope
 * @...: Name of scope to trace; supports format string
 *
 * Used to trace a scope of time. Often used for function duration,
 * but may be used to keep track of the duration of more high-level operations.
 */
#define DPU_ATRACE_END(...) \
	_tracing_mark_write('E', current->tgid, 0, "")

/**
 * DPU_ATRACE_INSTANT_PID_FMT() - used to trace an instantaneous formatted event
 * @pid: Attach trace log to specific process ID
 *
 * Used to trace an instantaneous formatted event with a string value
 */
#define DPU_ATRACE_INSTANT_PID_FMT(pid, ...) \
	_tracing_mark_write('I', pid, 0, __VA_ARGS__)

/**
 * DPU_ATRACE_INSTANT_PID() - used to trace an instantaneous event
 * @name: Name of variable to trace; does not support format string
 * @value: Value of variable to trace
 * @pid: Attach trace log to specific process ID
 *
 * Used to trace an instantaneous event with a string value
 */
#define DPU_ATRACE_INSTANT_PID(name, pid) \
	DPU_ATRACE_INSTANT_PID_FMT(pid, name)

/**
 * DPU_ATRACE_INSTANT() - used to trace an instantaneous event
 * @name: Name of variable to trace; does not support format string
 * @value: Value of variable to trace
 *
 * Used to trace an instantaneous event with a string value
 */
#define DPU_ATRACE_INSTANT(name) \
	DPU_ATRACE_INSTANT_PID(name, current->tgid)

#endif /* __DPU_ATRACE_API_DEF_ */
#endif /* _DPU_TRACE_H_ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/.

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE dpu_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
