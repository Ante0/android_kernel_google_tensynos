/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  Types and definitions for AF_INET6
 *  Linux INET6 implementation
 *
 *  Authors:
 *  Pedro Roque <roque@di.fc.ul.pt>
 *
 *  Sources:
 *  IPv6 Program Interfaces for BSD Systems
 *  <draft-ietf-ipngwg-bsd-api-05.txt>
 *
 *  Advanced Sockets API for IPv6
 *  <draft-stevens-advanced-api-00.txt>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
// NOLINTBEGIN
#pragma once

struct in6_addr {
  union {
    uint8_t __s6_addr[16];
    uint16_t __s6_addr16[8];
    uint32_t __s6_addr32[4];
  } __in6_union;
};

#define s6_addr __in6_union.__s6_addr
#define s6_addr16 __in6_union.__s6_addr16
#define s6_addr32 __in6_union.__s6_addr32

/* Copied from kernel(include/uapi/linux/in6.h) */
#define IPPROTO_HOPOPTS 0 /* IPv6 hop-by-hop options */
#define IPPROTO_ROUTING 43 /* IPv6 routing header */
#define IPPROTO_FRAGMENT 44 /* IPv6 fragmentation header */
#define IPPROTO_ICMPV6 58 /* ICMPv6 */
#define IPPROTO_NONE 59 /* IPv6 no next header */
#define IPPROTO_DSTOPTS 60 /* IPv6 destination options */
#define IPPROTO_MH 135 /* IPv6 mobility header */
//NOLINTEND
