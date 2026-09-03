/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trace module for NOA WLAN
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Jason Lin <wwlin@google.com>>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM noa_wlan

#if !defined(__NOA_WLAN_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __NOA_WLAN_TRACE_H__

#include <linux/tracepoint.h>
#include <common/core.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/ipv6.h>

DECLARE_EVENT_CLASS(wlan_ipv4_class,
	TP_PROTO(struct iphdr *ip),
	TP_ARGS(ip),
	TP_STRUCT__entry(
		__field(u8,  protocol)
		__field(u8,  version)
		__field(u8,  tos)
		__field(u32, saddr)
		__field(u32, daddr)
	),
	TP_fast_assign(
		__entry->protocol = ip->protocol;
		__entry->version  = ip->version;
		__entry->tos      = ip->tos;
		__entry->saddr    = ip->saddr;
		__entry->daddr    = ip->daddr;
	),
	TP_printk("IPv4 Trace in WLAN: \n"
		"proto(%d), ver(%d), tos(%d)\n"
		"src: %d.%d.%d.%d(0x%08x), dst: %d.%d.%d.%d(0x%08x)",
		__entry->protocol,
		__entry->version,
		__entry->tos,
		(__entry->saddr >> 0) & 0xff,
		(__entry->saddr >> 8) & 0xff,
		(__entry->saddr >> 16) & 0xff,
		(__entry->saddr >> 24) & 0xff,
		__entry->saddr,
		(__entry->daddr >> 0) & 0xff,
		(__entry->daddr >> 8) & 0xff,
		(__entry->daddr >> 16) & 0xff,
		(__entry->daddr >> 24) & 0xff,
		__entry->daddr
		)
);
#define DEFINE_WLAN_IPV4_EVENT(name)	\
DEFINE_EVENT(wlan_ipv4_class, name,	\
	TP_PROTO(struct iphdr *ip), 		\
	TP_ARGS(ip))
DEFINE_WLAN_IPV4_EVENT(wlan_ipv4);

DECLARE_EVENT_CLASS(wlan_ipv6_class,
	TP_PROTO(struct ipv6hdr *ip),
	TP_ARGS(ip),
	TP_STRUCT__entry(
		__field(u8,  priority)
		__field(u8,  version)
		__field(u8,  nexthdr)
		__field(u8,  saddr_0)
		__field(u8,  saddr_1)
		__field(u8,  saddr_2)
		__field(u8,  saddr_3)
		__field(u8,  saddr_4)
		__field(u8,  saddr_5)
		__field(u8,  saddr_6)
		__field(u8,  saddr_7)
		__field(u8,  saddr_8)
		__field(u8,  saddr_9)
		__field(u8,  saddr_10)
		__field(u8,  saddr_11)
		__field(u8,  saddr_12)
		__field(u8,  saddr_13)
		__field(u8,  saddr_14)
		__field(u8,  saddr_15)
		__field(u8,  daddr_0)
		__field(u8,  daddr_1)
		__field(u8,  daddr_2)
		__field(u8,  daddr_3)
		__field(u8,  daddr_4)
		__field(u8,  daddr_5)
		__field(u8,  daddr_6)
		__field(u8,  daddr_7)
		__field(u8,  daddr_8)
		__field(u8,  daddr_9)
		__field(u8,  daddr_10)
		__field(u8,  daddr_11)
		__field(u8,  daddr_12)
		__field(u8,  daddr_13)
		__field(u8,  daddr_14)
		__field(u8,  daddr_15)
	),
	TP_fast_assign(
		__entry->priority = ip->priority;
		__entry->version  = ip->version;
		__entry->nexthdr  = ip->nexthdr;
		__entry->saddr_0  = ip->saddr.in6_u.u6_addr8[0];
		__entry->saddr_1  = ip->saddr.in6_u.u6_addr8[1];
		__entry->saddr_2  = ip->saddr.in6_u.u6_addr8[2];
		__entry->saddr_3  = ip->saddr.in6_u.u6_addr8[3];
		__entry->saddr_4  = ip->saddr.in6_u.u6_addr8[4];
		__entry->saddr_5  = ip->saddr.in6_u.u6_addr8[5];
		__entry->saddr_6  = ip->saddr.in6_u.u6_addr8[6];
		__entry->saddr_7  = ip->saddr.in6_u.u6_addr8[7];
		__entry->saddr_8  = ip->saddr.in6_u.u6_addr8[8];
		__entry->saddr_9  = ip->saddr.in6_u.u6_addr8[9];
		__entry->saddr_10 = ip->saddr.in6_u.u6_addr8[10];
		__entry->saddr_11 = ip->saddr.in6_u.u6_addr8[11];
		__entry->saddr_12 = ip->saddr.in6_u.u6_addr8[12];
		__entry->saddr_13 = ip->saddr.in6_u.u6_addr8[13];
		__entry->saddr_14 = ip->saddr.in6_u.u6_addr8[14];
		__entry->saddr_15 = ip->saddr.in6_u.u6_addr8[15];
		__entry->daddr_0  = ip->daddr.in6_u.u6_addr8[0];
		__entry->daddr_1  = ip->daddr.in6_u.u6_addr8[1];
		__entry->daddr_2  = ip->daddr.in6_u.u6_addr8[2];
		__entry->daddr_3  = ip->daddr.in6_u.u6_addr8[3];
		__entry->daddr_4  = ip->daddr.in6_u.u6_addr8[4];
		__entry->daddr_5  = ip->daddr.in6_u.u6_addr8[5];
		__entry->daddr_6  = ip->daddr.in6_u.u6_addr8[6];
		__entry->daddr_7  = ip->daddr.in6_u.u6_addr8[7];
		__entry->daddr_8  = ip->daddr.in6_u.u6_addr8[8];
		__entry->daddr_9  = ip->daddr.in6_u.u6_addr8[9];
		__entry->daddr_10 = ip->daddr.in6_u.u6_addr8[10];
		__entry->daddr_11 = ip->daddr.in6_u.u6_addr8[11];
		__entry->daddr_12 = ip->daddr.in6_u.u6_addr8[12];
		__entry->daddr_13 = ip->daddr.in6_u.u6_addr8[13];
		__entry->daddr_14 = ip->daddr.in6_u.u6_addr8[14];
		__entry->daddr_15 = ip->daddr.in6_u.u6_addr8[15];
	),
	TP_printk("IPv6 Trace in WLAN: \n"
		"priority(%d), ver(%d), nexthdr(0x%x)\n"
		"src:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x\n"
		"dst:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x:%x%x",
		__entry->priority,
		__entry->version,
		__entry->nexthdr,
		__entry->saddr_0,
		__entry->saddr_1,
		__entry->saddr_2,
		__entry->saddr_3,
		__entry->saddr_4,
		__entry->saddr_5,
		__entry->saddr_6,
		__entry->saddr_7,
		__entry->saddr_8,
		__entry->saddr_9,
		__entry->saddr_10,
		__entry->saddr_11,
		__entry->saddr_12,
		__entry->saddr_13,
		__entry->saddr_14,
		__entry->saddr_15,
		__entry->daddr_0,
		__entry->daddr_1,
		__entry->daddr_2,
		__entry->daddr_3,
		__entry->daddr_4,
		__entry->daddr_5,
		__entry->daddr_6,
		__entry->daddr_7,
		__entry->daddr_8,
		__entry->daddr_9,
		__entry->daddr_10,
		__entry->daddr_11,
		__entry->daddr_12,
		__entry->daddr_13,
		__entry->daddr_14,
		__entry->daddr_15
		)
);
#define DEFINE_WLAN_IPV6_EVENT(name)	\
DEFINE_EVENT(wlan_ipv6_class, name,	\
	TP_PROTO(struct ipv6hdr *ip), 		\
	TP_ARGS(ip))
DEFINE_WLAN_IPV6_EVENT(wlan_ipv6);

#endif /* __NOA_WLAN_TRACE_H__ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH wlan
#define TRACE_INCLUDE_FILE wlan_trace
/* This part must be outside protection */
#include <trace/define_trace.h>
