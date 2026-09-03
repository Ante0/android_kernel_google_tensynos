#undef TRACE_SYSTEM
#define TRACE_SYSTEM noa_wlan_debug

#if !defined(__NOA_WLAN_DEBUG_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __NOA_WLAN_DEBUG_TRACE_H__

#include <linux/version.h>
#include <linux/tracepoint.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)
#define __general_assign_str(a, b) __assign_str(a, b)
#else
#define __general_assign_str(a, b) __assign_str(a)
#endif

/* NOA Log System Capture */
TRACE_EVENT(wlan_noa_log, TP_PROTO(char *log_message), TP_ARGS(log_message),
	    TP_STRUCT__entry(__string(log_message, log_message)),
	    TP_fast_assign(__general_assign_str(log_message, log_message);),
	    TP_printk("wlan_noa_log: {%s}", __get_str(log_message)));

/* NOA Packet Sniffer Dump */
TRACE_EVENT(wlan_noa_packet_sniffer, TP_PROTO(char *packet_contents), TP_ARGS(packet_contents),
	    TP_STRUCT__entry(__string(packet_contents, packet_contents)),
	    TP_fast_assign(__general_assign_str(packet_contents, packet_contents);),
	    TP_printk("wlan_noa_packet_sniffer: {%s}", __get_str(packet_contents)));

/* NOA Shared Info Dump */
TRACE_EVENT(wlan_noa_shared_info, TP_PROTO(char *shared_info), TP_ARGS(shared_info),
	    TP_STRUCT__entry(__field(char *, shared_info)),
	    TP_fast_assign(__entry->shared_info = shared_info;),
	    TP_printk("wlan_noa_shared_info: {%s}", __entry->shared_info));

#endif /*__NOA_WLAN_DEBUG_TRACE_H__*/

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH wlan/wlan_debug_controller
#define TRACE_INCLUDE_FILE wlan_debug_trace
/* This part must be outside protection */
#include <trace/define_trace.h>