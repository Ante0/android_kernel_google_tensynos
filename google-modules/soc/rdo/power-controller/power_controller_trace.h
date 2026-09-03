/* SPDX-License-Identifier: GPL-2.0-only */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM power_controller

#if !defined(_TRACE_POWER_CONTROLLER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_POWER_CONTROLLER_H

#include <linux/trace.h>
#include <linux/tracepoint.h>
#include <linux/api-compat.h>

#include "power_controller.h"

DECLARE_EVENT_CLASS(power_controller_send_mba_events,
	TP_PROTO(struct power_domain *pd, bool on),
	TP_ARGS(pd, on),
	TP_STRUCT__entry(
		__string(name, pd->name)
		__field(unsigned char, pm_resource_id)
		__field(unsigned char, subdomain_id)
		__field(bool, on)
	),
	TP_fast_assign(
		assign_str_wrp(name, pd->name);
		__entry->pm_resource_id = pd->pm_resource_id;
		__entry->subdomain_id = pd->cpm_lpcm_subdomain_id;
		__entry->on = on;
	),
	TP_printk("\t%#04x.%02x:%s\ten:%d",
		  __entry->pm_resource_id, __entry->subdomain_id,
		  __get_str(name), __entry->on)
);
DEFINE_EVENT(power_controller_send_mba_events, send_mail_lpb,
	TP_PROTO(struct power_domain *pd, bool on),
	TP_ARGS(pd, on)
);
DEFINE_EVENT(power_controller_send_mba_events, send_mail_lpcm,
	TP_PROTO(struct power_domain *pd, bool on),
	TP_ARGS(pd, on)
);

DECLARE_EVENT_CLASS(power_controller_mba_result_events,
	TP_PROTO(struct power_domain *pd, bool on, int result),
	TP_ARGS(pd, on, result),
	TP_STRUCT__entry(
		__string(name, pd->name)
		__field(unsigned char, pm_resource_id)
		__field(unsigned char, subdomain_id)
		__field(bool, on)
		__field(int, result)
	),
	TP_fast_assign(
		assign_str_wrp(name, pd->name);
		__entry->pm_resource_id = pd->pm_resource_id;
		__entry->subdomain_id = pd->cpm_lpcm_subdomain_id;
		__entry->on = on;
		__entry->result = result;
	),
	TP_printk("\t%#04x.%02x:%s\ten:%d (res:%d)",
		  __entry->pm_resource_id, __entry->subdomain_id,
		  __get_str(name), __entry->on, __entry->result)
);
DEFINE_EVENT(power_controller_mba_result_events, recv_result_lpb,
	TP_PROTO(struct power_domain *pd, bool on, int result),
	TP_ARGS(pd, on, result)
);
DEFINE_EVENT(power_controller_mba_result_events, recv_result_lpcm,
	TP_PROTO(struct power_domain *pd, bool on, int result),
	TP_ARGS(pd, on, result)
);

TRACE_EVENT(send_mail_mbfs,
	TP_PROTO(struct power_domain *pd, bool on),
	TP_ARGS(pd, on),
	TP_STRUCT__entry(
		__string(name, pd->name)
		__field(u32, pm_resource_id)
		__field(bool, on)
	),
	TP_fast_assign(
		assign_str_wrp(name, pd->name);
		__entry->pm_resource_id = pd->pm_resource_id;
		__entry->on = on;
	),
	TP_printk("\t%#06x:%s\ten:%d",
		  __entry->pm_resource_id, __get_str(name), __entry->on)
);

TRACE_EVENT(recv_result_mbfs,
	TP_PROTO(struct power_domain *pd, bool on, int result),
	TP_ARGS(pd, on, result),
	TP_STRUCT__entry(
		__string(name, pd->name)
		__field(u32, pm_resource_id)
		__field(bool, on)
		__field(int, result)
	),
	TP_fast_assign(
		assign_str_wrp(name, pd->name);
		__entry->pm_resource_id = pd->pm_resource_id;
		__entry->on = on;
		__entry->result = result;
	),
	TP_printk("\t%#06x:%s\ten:%d (res:%d)",
		  __entry->pm_resource_id, __get_str(name), __entry->on,
		  __entry->result)
);

#endif /* _TRACE_POWER_CONTROLLER_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../power-controller/

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE power_controller_trace

#include <trace/define_trace.h>
