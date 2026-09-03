/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
// NOLINTBEGIN
#pragma once

#include "in6.h"

#include <cstdint>

#ifndef __LITTLE_ENDIAN_BITFIELD
#define __LITTLE_ENDIAN_BITFIELD
#endif

#define IPV6_MIN_MTU 1280

struct ipv6hdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
  uint8_t priority : 4, version : 4;
#elif defined(__BIG_ENDIAN_BITFIELD)
  uint8_t version : 4, priority : 4;
#else
#error "Please fix <asm/byteorder.h>"
#endif
  uint8_t flow_lbl[3];

  uint16_t payload_len;
  uint8_t nexthdr;
  uint8_t hop_limit;

  struct in6_addr saddr;
  struct in6_addr daddr;
} __attribute__((__packed__));

// NOLINTEND