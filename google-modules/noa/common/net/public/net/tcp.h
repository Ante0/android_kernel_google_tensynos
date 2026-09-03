/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * INET         An implementation of the TCP/IP protocol suite for the LINUX
 *              operating system.  INET is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              Definitions for the TCP protocol.
 *
 * Version:     @(#)tcp.h       1.0.2   04/28/93
 *
 * Author:      Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 */
// NOLINTBEGIN
#pragma once

#include <cstdint>

#ifndef __LITTLE_ENDIAN_BITFIELD
#define __LITTLE_ENDIAN_BITFIELD
#endif

struct tcphdr {
  uint16_t source;
  uint16_t dest;
  uint32_t seq;
  uint32_t ack_seq;
#if defined(__LITTLE_ENDIAN_BITFIELD)
  uint16_t res1 : 4, doff : 4, fin : 1, syn : 1, rst : 1, psh : 1, ack : 1,
      urg : 1, ece : 1, cwr : 1;
#elif defined(__BIG_ENDIAN_BITFIELD)
  uint16_t doff : 4, res1 : 4, cwr : 1, ece : 1, urg : 1, ack : 1, psh : 1,
      rst : 1, syn : 1, fin : 1;
#else
#error "Adjust your <asm/byteorder.h> defines"
#endif
  uint16_t window;
  uint16_t check;
  uint16_t urg_ptr;
} __attribute__((__packed__));

// NOLINTEND
