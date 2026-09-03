/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Panel command trace support
 *
 * Copyright (C) 2022 Google, Inc.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM panel

#if !defined(_PANEL_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _PANEL_TRACE_H_

#include <linux/bitmap.h>
#include <linux/tracepoint.h>

#include "gs_drm/gs_drm_connector.h"

TRACE_EVENT_CONDITION(dsi_tx,
	TP_PROTO(u8 idx, u8 type, const u8 *tx_buf, size_t length, bool last, u32 delay_ms),
	TP_ARGS(idx, type, tx_buf, length, last, delay_ms),
	TP_CONDITION(length > 0),
	TP_STRUCT__entry(
			__field(u8, idx)
			__field(u8, type)
			__dynamic_array(u8, tx_buf, length)
			__field(bool, last)
			__field(u32, delay_ms)
		),
	TP_fast_assign(
			__entry->idx = idx;
			__entry->type = type;
			memcpy(__get_dynamic_array(tx_buf), tx_buf, length);
			__entry->last = last;
			__entry->delay_ms = delay_ms;
		),
	TP_printk("idx=%d type=0x%02x length=%u last=%d delay=%d tx=[%s]", __entry->idx,
			  __entry->type, __get_dynamic_array_len(tx_buf), __entry->last,
			  __entry->delay_ms, __print_hex(__get_dynamic_array(tx_buf),
							 __get_dynamic_array_len(tx_buf)))
);
TRACE_EVENT_CONDITION(dsi_rx,
	TP_PROTO(u8 idx, u8 cmd, const u8 *rx_buf, size_t length),
	TP_ARGS(idx, cmd, rx_buf, length),
	TP_CONDITION(length > 0),
	TP_STRUCT__entry(
			__field(u8, idx)
			__field(u8, cmd)
			__dynamic_array(u8, rx_buf, length)
		),
	TP_fast_assign(
			__entry->idx = idx;
			__entry->cmd = cmd;
			memcpy(__get_dynamic_array(rx_buf), rx_buf, length);
		),
	TP_printk("idx=%d cmd=0x%02x length=%u rx=[%s]", __entry->idx, __entry->cmd,
			  __get_dynamic_array_len(rx_buf),
			  __print_hex(__get_dynamic_array(rx_buf),
				      __get_dynamic_array_len(rx_buf)))
);

TRACE_EVENT(dsi_cmd_fifo_status,
	TP_PROTO(u8 header, u16 payload),
	TP_ARGS(header, payload),
	TP_STRUCT__entry(
			__field(u8, header)
			__field(u16, payload)
		),
	TP_fast_assign(
			__entry->header   = header;
			__entry->payload  = payload;
		),
	TP_printk("header=%d payload=%d", __entry->header, __entry->payload)
);

#define PANEL_ERROR_FLAGS_NAME(name) \
	{ (1UL << GS_PANEL_ERR_##name), #name }
TRACE_EVENT(panel_errors,
	TP_PROTO(int panel_index, unsigned long *panel_errors),
	TP_ARGS(panel_index, panel_errors),
	TP_STRUCT__entry(
		__field(int, panel_index)
		__field(u64, panel_errors)
	),
	TP_fast_assign(
		__entry->panel_index = panel_index;
		bitmap_to_arr64(&__entry->panel_errors, panel_errors, GS_PANEL_ERR_MAX);
	),
	TP_printk("idx:%d panel_errors:[%s]", __entry->panel_index,
		__print_flags(__entry->panel_errors, "|",
			PANEL_ERROR_FLAGS_NAME(DSI_SOT),
			PANEL_ERROR_FLAGS_NAME(DSI_SOT_SYNC),
			PANEL_ERROR_FLAGS_NAME(DSI_EOT_SYNC),
			PANEL_ERROR_FLAGS_NAME(DSI_ESCAPE_MODE_ENTRY),
			PANEL_ERROR_FLAGS_NAME(DSI_LP_XMIT_SYNC),
			PANEL_ERROR_FLAGS_NAME(DSI_HS_RX_TIMEOUT),
			PANEL_ERROR_FLAGS_NAME(DSI_FALSE_CONTROL),
			PANEL_ERROR_FLAGS_NAME(DSI_DATA_LANE_CONTENTION),
			PANEL_ERROR_FLAGS_NAME(DSI_ECC_SINGLE),
			PANEL_ERROR_FLAGS_NAME(DSI_ECC_MULTI),
			PANEL_ERROR_FLAGS_NAME(DSI_CHECKSUM),
			PANEL_ERROR_FLAGS_NAME(DSI_DATA_TYPE),
			PANEL_ERROR_FLAGS_NAME(DSI_VC_ID_INVALID),
			PANEL_ERROR_FLAGS_NAME(DSI_XMIT_LEN),
			PANEL_ERROR_FLAGS_NAME(DSI_RESERVED),
			PANEL_ERROR_FLAGS_NAME(DSI_PROTOCOL_VIOLATION),
			PANEL_ERROR_FLAGS_NAME(DSI_READ_FAILURE),
			PANEL_ERROR_FLAGS_NAME(VLIN1),
			PANEL_ERROR_FLAGS_NAME(PPS),
			PANEL_ERROR_FLAGS_NAME(CHECKSUM),
			PANEL_ERROR_FLAGS_NAME(ESD),
			PANEL_ERROR_FLAGS_NAME(DISP_INVALID),
			PANEL_ERROR_FLAGS_NAME(VGH),
			PANEL_ERROR_FLAGS_NAME(GRAM_COLLISION)))
);

TRACE_EVENT(gram_collision,
	TP_PROTO(int panel_index, u32 collision_cnt),
	TP_ARGS(panel_index, collision_cnt),
	TP_STRUCT__entry(
		__field(int, panel_index)
		__field(u32, collision_cnt)
	),
	TP_fast_assign(
		__entry->panel_index = panel_index;
		__entry->collision_cnt = collision_cnt;
	),
	TP_printk("idx:%d GRAM collision found (underrun), total:%u", __entry->panel_index,
		  __entry->collision_cnt)
);

TRACE_EVENT(panel_settings_full,
	TP_PROTO(int panel_index, bool hbm, u32 irc, bool h_pwm, bool fi_auto, bool fi_manual,
		 bool early_exit, u32 min_rr, u32 max_rr, u32 te_freq),
	TP_ARGS(panel_index, hbm, irc, h_pwm, fi_auto, fi_manual, early_exit, min_rr, max_rr,
		te_freq),
	TP_STRUCT__entry(
			__field(int, panel_index)
			__field(bool, hbm)
			__field(u32, irc)
			__field(bool, h_pwm)
			__field(bool, fi_auto)
			__field(bool, fi_manual)
			__field(bool, early_exit)
			__field(u32, min_rr)
			__field(u32, max_rr)
			__field(u32, te_freq)
		),
	TP_fast_assign(
			__entry->panel_index = panel_index;
			__entry->hbm = hbm;
			__entry->irc = irc;
			__entry->h_pwm = h_pwm;
			__entry->fi_auto = fi_auto;
			__entry->fi_manual = fi_manual;
			__entry->early_exit = early_exit;
			__entry->min_rr = min_rr;
			__entry->max_rr = max_rr;
			__entry->te_freq = te_freq;
		),
	TP_printk("idx:%d feat: hbm=%d irc=%u h_pwm=%d fi=%d@a,%d@m ee=%d rr=%3u-%3u@%3u",
		  __entry->panel_index, __entry->hbm, __entry->irc, __entry->h_pwm,
		  __entry->fi_auto, __entry->fi_manual, __entry->early_exit, __entry->min_rr,
		  __entry->max_rr, __entry->te_freq)
);

TRACE_EVENT(panel_settings_lite,
	TP_PROTO(int panel_index, bool vrr, u32 min_rr, u32 max_rr, u32 te_freq),
	TP_ARGS(panel_index, vrr, min_rr, max_rr, te_freq),
	TP_STRUCT__entry(
			__field(int, panel_index)
			__field(bool, vrr)
			__field(u32, min_rr)
			__field(u32, max_rr)
			__field(u32, te_freq)
		),
	TP_fast_assign(
			__entry->panel_index = panel_index;
			__entry->vrr = vrr;
			__entry->min_rr = min_rr;
			__entry->max_rr = max_rr;
			__entry->te_freq = te_freq;
		),
	TP_printk("idx:%d vrr=%d rr=%3u-%3u@%3u",
		  __entry->panel_index, __entry->vrr, __entry->min_rr, __entry->max_rr,
		  __entry->te_freq)
);

TRACE_EVENT(msleep,
	TP_PROTO(u32 delay_ms),
	TP_ARGS(delay_ms),
	TP_STRUCT__entry(__field(u32, delay_ms)),
	TP_fast_assign(__entry->delay_ms = delay_ms;),
	TP_printk("delay=%d", __entry->delay_ms)
);

TRACE_EVENT(dsi_label_scope,
	TP_PROTO(const char *name, bool begin),
	TP_ARGS(name, begin),
	TP_STRUCT__entry(
			__string(name, name)
			__field(bool, begin)
		),
	TP_fast_assign(
			__assign_str(name);
			__entry->begin = begin;
		),
	TP_printk("%s %s", __get_str(name), __entry->begin ? "begin" : "end")
);
#define PANEL_SEQ_LABEL_BEGIN(name) trace_dsi_label_scope(name, true)
#define PANEL_SEQ_LABEL_END(name) trace_dsi_label_scope(name, false)

TRACE_EVENT(te2_update_settings,
	TP_PROTO(int rising_us, int falling_us, bool is_changeable, bool is_idle),
	TP_ARGS(rising_us, falling_us, is_changeable, is_idle),
	TP_STRUCT__entry(
			__field(int, rising_us)
			__field(int, falling_us)
			__field(bool, is_changeable)
			__field(bool, is_idle)
		),
	TP_fast_assign(
			__entry->rising_us = rising_us;
			__entry->falling_us = falling_us;
			__entry->is_changeable = is_changeable;
			__entry->is_idle = is_idle;
		),
	TP_printk("TE2 updated: rising %dus falling %dus, option %s, idle %s",
		  __entry->rising_us, __entry->falling_us,
		  __entry->is_changeable ? "changeable" : "fixed",
		  __entry->is_idle ? "active" : "inactive")
);

TRACE_EVENT(panel_write_generic,
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
#ifndef __PANEL_ATRACE_API_DEF_
#define __PANEL_ATRACE_API_DEF_

/* utility function for variadic arguments */
static inline void _panel_write_generic(char type, int pid, int value, const char *fmt, ...)
{
	va_list args = {0};
	struct va_format vaf = {
		.fmt = fmt,
	};

	va_start(args, fmt);
	vaf.va = &args;
	trace_panel_write_generic(type, pid, &vaf, value);
	va_end(args);
}

/**
 * PANEL_ATRACE_BEGIN() - used to trace beginning of a scope
 * @...: Name of scope to trace; supports format string
 *
 * Used to trace a scope of time. Often used for function duration,
 * but may be used to keep track of the duration of more high-level operations.
 */
#define PANEL_ATRACE_BEGIN(...) \
	_panel_write_generic('B', current->tgid, 0, __VA_ARGS__)

/**
 * PANEL_ATRACE_END() - used to trace end of a scope
 * @...: Name of scope to trace; supports format string
 *
 * Used to trace a scope of time. Often used for function duration,
 * but may be used to keep track of the duration of more high-level operations.
 */
#define PANEL_ATRACE_END(...) \
	_panel_write_generic('E', current->tgid, 0, "")

/**
 * PANEL_ATRACE_INSTANT() - used to trace an instantaneous event
 * @...: Name of event to trace; supports format string
 *
 * Used to trace a named event without a duration attached.
 */
#define PANEL_ATRACE_INSTANT(...) \
	_panel_write_generic('I', current->tgid, 0, __VA_ARGS__)

/**
 * PANEL_ATRACE_INT_PID() - used to trace an integer value
 * @name: Name of variable to trace; does not support format string
 * @value: Value of variable to trace
 * @pid: Attach trace log to specific process ID
 *
 * Used to trace a variable or counter with an integer value
 */
#define PANEL_ATRACE_INT_PID(name, value, pid) \
	_panel_write_generic('C', pid, value, name)

/**
 * PANEL_ATRACE_INT_PID_FMT() - used to trace an integer value for a formatted variable
 * @value: Value of variable to trace
 * @pid: Attach trace log to specific process ID
 *
 * Used to trace a formatted variable or counter with an integer value
 */
#define PANEL_ATRACE_INT_PID_FMT(value, pid, ...) \
	_panel_write_generic('C', pid, value, __VA_ARGS__)

/**
 * PANEL_ATRACE_INT() - used to trace an integer value
 * @name: Name of variable to trace; does not support format string
 * @value: Value of variable to trace
 *
 * Used to trace a variable or counter with an integer value
 */
#define PANEL_ATRACE_INT(name, value) \
	PANEL_ATRACE_INT_PID(name, value, current->tgid)

#endif /* __PANEL_ATRACE_API_DEF_ */

#endif /* _PANEL_TRACE_H_ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/.

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE panel_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
