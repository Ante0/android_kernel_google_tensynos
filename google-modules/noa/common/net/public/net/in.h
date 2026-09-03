/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
// Disable linting for non-standard identifier names (Linux kernel style)
// NOLINTBEGIN(readability-identifier-naming)
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions of the Internet Protocol.
 *
 * Version:	@(#)in.h	1.0.1	04/21/93
 *
 * Authors:	Original taken from the GNU Project <netinet/in.h> file.
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#pragma once

// An integral type representing an IPv4 address.
typedef uint32_t in_addr_t;

// A structure representing an IPv4 address.
struct in_addr {
  in_addr_t s_addr;
};

/* Copied from kernel(include/uapi/linux/in.h) */
/* Standard well-defined IP protocols.  */
enum {
  IPPROTO_IP = 0, /* Base protocol */

#define IPPROTO_IP IPPROTO_IP
  IPPROTO_TCP = 6, /* Transmission Control Protocol	*/
#define IPPROTO_TCP IPPROTO_TCP
  IPPROTO_UDP = 17, /* User Datagram Protocol */
#define IPPROTO_UDP IPPROTO_UDP
  IPPROTO_ESP = 50, /* Encapsulation Security Payload protocol */
#define IPPROTO_ESP IPPROTO_ESP
  IPPROTO_AH = 51, /* Authentication Header protocol */
#define IPPROTO_AH IPPROTO_AH
  IPPROTO_MAX
};
// NOLINTEND(readability-identifier-naming)
