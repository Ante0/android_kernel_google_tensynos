/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC.
 *
 * Google firmware tracepoint ftrace services source.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM fwtp_ftrace

#if !defined(FWTP_FTRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define FWTP_FTRACE_H_

#include <linux/api-compat.h>
#include <linux/tracepoint.h>

/* Define FWTP ftrace event. */
TRACE_EVENT(fwtp, TP_PROTO(const char *fwtp_string), TP_ARGS(fwtp_string),
	    TP_STRUCT__entry(__string(fwtp_string, fwtp_string)),
	    TP_fast_assign(assign_str_wrp(fwtp_string, fwtp_string)),
	    TP_printk("%s", __get_str(fwtp_string)));

/* Define FWTP Perfetto counter ftrace event. */
TRACE_EVENT(fwtp_perfetto_counter,
	    TP_PROTO(unsigned long long timestamp, unsigned int track_id,
		     const char *category, const char *name,
		     unsigned int value),
	    TP_ARGS(timestamp, track_id, category, name, value),
	    TP_STRUCT__entry(__field(unsigned long long, timestamp) /* lint */
			     __field(unsigned int, track_id) /* lint */
			     __string(category, category) /* lint */
			     __string(name, name) /* lint */
			     __field(unsigned int, value)),
	    TP_fast_assign(__entry->timestamp = timestamp;
			   __entry->track_id = track_id;
			   assign_str_wrp(category, category);
			   assign_str_wrp(name, name); __entry->value = value;),
	    TP_printk("timestamp:%llu track_id:%u category:%s name:%s value:%u",
		      __entry->timestamp, __entry->track_id,
		      __get_str(category), __get_str(name), __entry->value));

/* Define FWTP Perfetto slice ftrace event. */
TRACE_EVENT(fwtp_perfetto_slice,
	    TP_PROTO(unsigned long long timestamp, unsigned int track_id,
		     const char *category, const char *name, bool begin),
	    TP_ARGS(timestamp, track_id, category, name, begin),
	    TP_STRUCT__entry(__field(unsigned long long, timestamp) /* lint */
			     __field(unsigned int, track_id) /* lint */
			     __string(category, category) /* lint */
			     __string(name, name) /* lint */
			     __field(bool, begin)),
	    TP_fast_assign(__entry->timestamp = timestamp;
			   __entry->track_id = track_id;
			   assign_str_wrp(category, category);
			   assign_str_wrp(name, name); __entry->begin = begin;),
	    TP_printk("timestamp:%llu track_id:%u category:%s name:%s begin:%u",
		      __entry->timestamp, __entry->track_id,
		      __get_str(category), __get_str(name), __entry->begin));

#endif /* FWTP_FTRACE_H_ */

/* This part must be outside protection. */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE fwtp_ftrace
#include <trace/define_trace.h>
