/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trace module for NOA wlan driver
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Wade Shih <wadeshih@google.com>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dhd_noa_wlan

#if !defined(__DHD_CUSTOM_GOOGLE_NOA_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __DHD_CUSTOM_GOOGLE_NOA_TRACE_H__

#include <linux/tracepoint.h>

/* Wlan packet capture */
DECLARE_EVENT_CLASS(
	wlan_pkt_cap_class,
	TP_PROTO(u32 flow_id, u32 pkt_len, const char *pkt, u32 data_len, const char *data),
	TP_ARGS(flow_id, pkt_len, pkt, data_len, data),
	TP_STRUCT__entry(__field(u32, flow_id) __field(u32, pkt_len) __field(u32, data_len)
				 __dynamic_array(char, pkt_buff, pkt_len)
					 __dynamic_array(char, data_buff, data_len)),
	TP_fast_assign(__entry->flow_id = flow_id; __entry->pkt_len = pkt_len;
		       __entry->data_len = data_len;
		       memcpy(__get_dynamic_array(pkt_buff), pkt, pkt_len);
		       memcpy(__get_dynamic_array(data_buff), data, data_len);),
	TP_printk("flow_id: %u, pkt_len: %u, pkt_buff: %s, data_len: %u, data_buff: %s",
		  __entry->flow_id, __entry->pkt_len,
		  __print_array(__get_dynamic_array(pkt_buff),
				__get_dynamic_array_len(pkt_buff) / sizeof(char), sizeof(char)),
		  __entry->data_len,
		  __print_array(__get_dynamic_array(data_buff),
				__get_dynamic_array_len(data_buff) / sizeof(char), sizeof(char))));
#define DEFINE_WLAN_PKT_CAP_EVENT(name)                                                            \
	DEFINE_EVENT(wlan_pkt_cap_class, name,                                                     \
		     TP_PROTO(u32 flow_id, u32 pkt_len, const char *pkt, u32 data_len,             \
			      const char *data),                                                   \
		     TP_ARGS(flow_id, pkt_len, pkt, data_len, data))
DEFINE_WLAN_PKT_CAP_EVENT(wlan_dump_noa_tx);
DEFINE_WLAN_PKT_CAP_EVENT(wlan_dump_noa_rx);

#endif /* __DHD_CUSTOM_GOOGLE_NOA_TRACE_H__ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH wlan
#define TRACE_INCLUDE_FILE dhd_custom_google_noa_trace
/* This part must be outside protection */
#include <trace/define_trace.h>
