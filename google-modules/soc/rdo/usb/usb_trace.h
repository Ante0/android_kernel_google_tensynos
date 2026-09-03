/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM usb_trace

#if !defined(_TRACE_USB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_USB_H

#include <linux/tracepoint.h>
#include <linux/api-compat.h>

TRACE_EVENT(platform_usb_suspend_start,
	    TP_PROTO(const char *name),
	    TP_ARGS(name),
	    TP_STRUCT__entry(__string(name, name)),
	    TP_fast_assign(assign_str_wrp(name, name);),
	    TP_printk("[%s]", __get_str(name)));

TRACE_EVENT(platform_usb_suspend_end,
	    TP_PROTO(const char *name),
	    TP_ARGS(name),
	    TP_STRUCT__entry(__string(name, name)),
	    TP_fast_assign(assign_str_wrp(name, name);),
	    TP_printk("[%s]", __get_str(name)));

TRACE_EVENT(platform_usb_resume_start,
	    TP_PROTO(const char *name),
	    TP_ARGS(name),
	    TP_STRUCT__entry(__string(name, name)),
	    TP_fast_assign(assign_str_wrp(name, name);),
	    TP_printk("[%s]", __get_str(name)));

TRACE_EVENT(platform_usb_resume_end,
	    TP_PROTO(const char *name),
	    TP_ARGS(name),
	    TP_STRUCT__entry(__string(name, name)),
	    TP_fast_assign(assign_str_wrp(name, name);),
	    TP_printk("[%s]", __get_str(name)));

TRACE_EVENT(platform_usb_pmu_set_state,
	    TP_PROTO(int state),
	    TP_ARGS(state),
	    TP_STRUCT__entry(__field(int, state)),
	    TP_fast_assign(__entry->state = state;),
	    TP_printk("state=D%d", __entry->state));

TRACE_EVENT(platform_usb_resume_interrupt,
	    TP_PROTO(u32 irq_status),
	    TP_ARGS(irq_status),
	    TP_STRUCT__entry(__field(u32, irq_status)),
	    TP_fast_assign(__entry->irq_status = irq_status;),
	    TP_printk("irq_status=0x%x", __entry->irq_status));

TRACE_EVENT(platform_usb_set_role,
	    TP_PROTO(const char *dr_role, const char *curr_role),
	    TP_ARGS(dr_role, curr_role),
	    TP_STRUCT__entry(__string(dr_role, dr_role)
			     __string(curr_role, curr_role)),
	    TP_fast_assign(assign_str_wrp(dr_role, dr_role);
			   assign_str_wrp(curr_role, curr_role);),
	    TP_printk("desired_role=%s current_role=%s", __get_str(dr_role),
		      __get_str(curr_role)));

#endif /* _TRACE_USB_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE usb_trace
