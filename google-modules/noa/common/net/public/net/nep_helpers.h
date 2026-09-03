/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NEP simualtor
 *
 * Copyright 2023 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
/*
 * Referred from Android main:packages/modules/Connectivity/bpf_progs/bpf_net_helpers.h
 */
#ifndef NEP_HELPERS_H
#define NEP_HELPERS_H

#include "common/core.h"

#ifdef linux
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/byteorder/generic.h>

#else
#include "common/debug.h"
#include "linux_port/types.h"
#endif

#include "if_ether.h"

#ifdef linux
#define NET_DBG_ENABLE noa_sim_get()->dbg
#define LOCAL_DBG(fmt, ...)                                                                        \
	if (NET_DBG_ENABLE)                                                                        \
		pr_info(fmt, ##__VA_ARGS__);
#else
#define NET_DBG_ENABLE GetDebugFlag() & DBGF_MOD_NETENIGNE
// definition needed by WiFi
#ifndef ETH_ALEN
#define ETH_ALEN ETHER_ADDR_LEN
#endif

#define LOCAL_DBG(fmt, ...)                                                                        \
	NOA_LOG_VERBOSE(DBGF_MOD_NETENIGNE, "%s:%" PRId32 " " fmt, __FILE__, __LINE__, __VA_ARGS__);

//TODO: b/328944140 - Use little endian here as GEM5 system is, but better to check underlying machine.
// Network/Host byte order conversion : referenced by WiFi
#define htons(n) (((((uint16_t)(n) & 0xFF)) << 8) | (((uint16_t)(n) & 0xFF00) >> 8))
#define ntohs(n) htons(n)
#define htonl(n)                                                                                   \
	(((((uint32_t)(n) & 0xFF)) << 24) | ((((uint32_t)(n) & 0xFF00)) << 8) |                    \
	 ((((uint32_t)(n) & 0xFF0000)) >> 8) | ((((uint32_t)(n) & 0xFF000000)) >> 24))
#define ntohl(n) htonl(n)
#endif
#define htonll(n)                                                                                  \
	(((((uint64_t)(n) & 0xFF)) << 56) | ((((uint64_t)(n) & 0xFF00)) << 40) |                   \
	 ((((uint64_t)(n) & 0xFF0000)) << 24) | ((((uint64_t)(n) & 0xFF000000)) << 8) |            \
	 ((((uint64_t)(n) & 0xFF00000000)) >> 8) | ((((uint64_t)(n) & 0xFF0000000000)) >> 24) |    \
	 ((((uint64_t)(n) & 0xFF000000000000)) >> 40) |                                            \
	 ((((uint64_t)(n) & 0xFF00000000000000)) >> 56))
#define ntohll(n) htonll(n)

// Offsets from beginning of L4 (TCP/UDP) header
#define TCP_OFFSET(field) offsetof(struct tcphdr, field)
#define UDP_OFFSET(field) offsetof(struct udphdr, field)

// Offsets from beginning of L3 (IPv4/IPv6) header
#define IP4_OFFSET(field) offsetof(struct iphdr, field)

#endif // NEP_HELPERS_H
