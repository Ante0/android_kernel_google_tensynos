/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NEP simualtor
 *
 * Copyright 2023 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
/*
 * Referred from Android main:packages/modules/Connectivity/bpf_progs/offload.h
 */
#ifndef OFFLOAD_H
#define OFFLOAD_H

#ifdef linux
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <common/map_def.h>
#include <uapi/linux/bpf.h>
#include "common/map_def.h"
#else
#include "nep_helpers.h"
#include "common/core.h"
#include "linux_port/spinlock.h"
#include "net/map_def.h"
#include "net/nep_helpers.h"
#endif

// forward declaration of noa_simulator (recursive include)
struct noa_simulator; // defined in nep.h
struct network_ext_txd;

#define NET_ENGINE_ACT_DROP		1
#define NET_ENGINE_ACT_FORWARD		2
#define NET_ENGINE_ACT_PUNT		3

#define BPF_TETHER_ERRORS	  \
	ERR(INVALID_IPV4_VERSION) \
	ERR(INVALID_IPV6_VERSION) \
	ERR(LOW_TTL)		  \
	ERR(INVALID_TCP_HEADER)   \
	ERR(TCPV4_CONTROL_PACKET) \
	ERR(TCPV6_CONTROL_PACKET) \
	ERR(NON_GLOBAL_SRC)	  \
	ERR(NON_GLOBAL_DST)	  \
	ERR(LOCAL_SRC_DST)	  \
	ERR(NO_FLOWID_ENTRY)	  \
	ERR(NO_CLAT_ENTRY)	  \
	ERR(NO_STATS_ENTRY)	  \
	ERR(NO_LIMIT_ENTRY)	  \
	ERR(BELOW_IPV4_MTU)	  \
	ERR(BELOW_IPV6_MTU)	  \
	ERR(LIMIT_REACHED)	  \
	ERR(CHANGE_HEAD_FAILED)   \
	ERR(TOO_SHORT)		  \
	ERR(HAS_IP_OPTIONS)	  \
	ERR(IS_IP_FRAG)		  \
	ERR(CHECKSUM)		  \
	ERR(NON_TCP_UDP)	  \
	ERR(NON_TCP)		  \
	ERR(SHORT_L4_HEADER)	  \
	ERR(SHORT_TCP_HEADER)	  \
	ERR(SHORT_UDP_HEADER)	  \
	ERR(UDP_CSUM_ZERO)	  \
	ERR(TRUNCATED_IPV4)	  \
	ERR(_MAX)

#define ERR(x) BPF_TETHER_ERR_ ##x,
enum {
	BPF_TETHER_ERRORS
};
#undef ERR

#define ERR(x) #x,
static const char *bpf_tether_errors[] = {
	BPF_TETHER_ERRORS
};
#undef ERR

struct noa_netengine_stat {
	unsigned long upstream_src_v4[NOA_PORT_MAX];
	unsigned long upstream_dst_v4[NOA_PORT_MAX];
	unsigned long downstream_src_v4[NOA_PORT_MAX];
	unsigned long downstream_dst_v4[NOA_PORT_MAX];
	unsigned long upstream_src_v6[NOA_PORT_MAX];
	unsigned long upstream_dst_v6[NOA_PORT_MAX];
	unsigned long downstream_src_v6[NOA_PORT_MAX];
	unsigned long downstream_dst_v6[NOA_PORT_MAX];
	unsigned long ipv4_packets;
	unsigned long ipv4_bytes;
	unsigned long ipv6_packets;
	unsigned long ipv6_bytes;
};

struct neteng_callback_ops {
	void (*write)(uint32_t type, const void *data);
};

struct offload_info {
	uint32_t upstreamIif;
	uint32_t downstreamIif;
	uint8_t downstream_if_Mac[ETH_ALEN];
	uint8_t upstream_if_Mac[ETH_ALEN];
	uint16_t pmtu;
	uint64_t limit_bytes;
	TetherStatsValue stats;
	spinlock_t stats_lock;
	struct noa_netengine_stat netengine_stat;
	const struct neteng_callback_ops *cb_ops;
	bool ever_notify_limit_reach;
};

extern struct offload_info* get_tethering_offload_info(void);

extern int do_process_pkt(void **data, u32 *len, bool is_ethernet, bool downstream,
						int fallback_port, nep_forward_info *nw_info);
extern void offload_init(void);
extern void get_nep_stats(TetherStats *stats);
int32_t get_nep_error_counters(int32_t error);
uint8_t nep_tos2priority(uint8_t tos);

#endif // OFFLOAD_H
