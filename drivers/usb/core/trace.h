/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Google LLC
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM usbcore

#if !defined(_USB_CORE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _USB_CORE_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>

#define show_usb_speed_name(speed)		\
	__print_symbolic(speed,			\
		{ 0, "unknown" },		\
		{ 1, "low-speed" },		\
		{ 2, "full-speed" },		\
		{ 3, "high-speed" },		\
		{ 4, "wireless" },		\
		{ 5, "super-speed" },		\
		{ 6, "super-speed-plus" })

#define show_usb_state_name(state)		\
	__print_symbolic(state,			\
		{ 0, "not attached" },		\
		{ 1, "attached" },		\
		{ 2, "powered" },		\
		{ 3, "reconnecting" },		\
		{ 4, "unauthenticated" },	\
		{ 5, "default" },		\
		{ 6, "address" },		\
		{ 7, "configured" },		\
		{ 8, "suspended" })

DECLARE_EVENT_CLASS(usb_core_log_usb_device,
	TP_PROTO(const char *devname, int speed, int state, u16 bus_mA, bool authorized),
	TP_ARGS(devname, speed, state, bus_mA, authorized),
	TP_STRUCT__entry(
		__string(name, devname)
		__field(int, speed)
		__field(int, state)
		__field(u16, bus_mA)
		__field(bool, authorized)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->speed = speed;
		__entry->state = state;
		__entry->bus_mA = bus_mA;
		__entry->authorized = authorized;
	),
	TP_printk("usb %s speed %s state %s %dmA [%s]",
		__get_str(name),
		show_usb_speed_name(__entry->speed),
		show_usb_state_name(__entry->state),
		(int)__entry->bus_mA,
		__entry->authorized ? "authorized" : "unauthorized")
);

DEFINE_EVENT(usb_core_log_usb_device, usb_set_device_state,
	TP_PROTO(const char *devname, int speed, int state, u16 bus_mA, bool authorized),
	TP_ARGS(devname, speed, state, bus_mA, authorized)
);

DEFINE_EVENT(usb_core_log_usb_device, usb_alloc_dev,
	TP_PROTO(const char *devname, int speed, int state, u16 bus_mA, bool authorized),
	TP_ARGS(devname, speed, state, bus_mA, authorized)
);


#endif /* _USB_CORE_TRACE_H */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
