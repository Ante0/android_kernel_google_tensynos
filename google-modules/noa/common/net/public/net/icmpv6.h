/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
// NOLINTBEGIN
#pragma once

#ifndef __LITTLE_ENDIAN_BITFIELD
#define __LITTLE_ENDIAN_BITFIELD
#endif

struct icmp6hdr {
	uint8_t		icmp6_type;
	uint8_t		icmp6_code;
	uint16_t	icmp6_cksum;
	union {
		uint32_t		un_data32[1];
		uint16_t		un_data16[2];
		uint8_t			un_data8[4];
		struct icmpv6_echo {
			uint16_t	identifier;
			uint16_t	sequence;
		} u_echo;
		struct icmpv6_nd_advt {
#if defined(__LITTLE_ENDIAN_BITFIELD)
			uint32_t	reserved:5,
					override:1,
					solicited:1,
					router:1,
					reserved2:24;
#elif defined(__BIG_ENDIAN_BITFIELD)
			uint32_t	router:1,
					solicited:1,
					override:1,
					reserved:29;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
		} u_nd_advt;
		struct icmpv6_nd_ra {
			uint8_t		hop_limit;
#if defined(__LITTLE_ENDIAN_BITFIELD)
			uint8_t		reserved:3,
					router_pref:2,
					home_agent:1,
					other:1,
					managed:1;
#elif defined(__BIG_ENDIAN_BITFIELD)
			uint8_t		managed:1,
					other:1,
					home_agent:1,
					router_pref:2,
					reserved:3;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
			uint16_t	rt_lifetime;
		} u_nd_ra;
	} icmp6_dataun;
} __attribute__((__packed__));

// NOLINTEND
