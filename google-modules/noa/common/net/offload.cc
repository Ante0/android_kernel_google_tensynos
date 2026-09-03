// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NEP simualtor
 *
 * Copyright 2023 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
/*
 * Referred from Android main:packages/modules/Connectivity/bpf_progs/offload.c
 */
#ifdef linux
#include <linux/if.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/ktime.h>
#include <linux/pkt_cls.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/checksum.h>
#include <common/inttypes.h>
#include <common/ring_id.h>
#include <common/wlan_ring_id.h>
#include <common/modem_ring_id.h>
#include "common/compiler.h"
#include "nep.h"
#include "offload.h"
#include "nep_helpers.h"
#include "nep_tables.h"
#include "nep_service.h"
#include "netengine_utils.h"
#else
#include "net/offload.h"
#include "net/nep_service.h"

#include <chrono>
#include <cinttypes>
#include <errno.h>

#include "common/compiler.h"
#include "common/core.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "linux_port/memory-alloc.h"
#include "linux_port/mutex.h"
#include "linux_port/log.h"
#include "linux_port/types.h"
#include "net/ip.h"
#include "net/ipv6.h"
#include "net/nep_helpers.h"
#include "net/nep_tables.h"
#include "net/netengine_utils.h"
#include "net/tcp.h"
#include "net/udp.h"
#include "pw_chrono/system_clock.h"
#include "pw_chrono/system_timer.h"
#endif

#include "checksum.h"
#include "bpf.h"

// From kernel:include/net/ip.h
#define IP_DF 0x4000  // Flag: "Don't Fragment"

#define EXTEND_TIMEOUT_CALLBACK_NS 60000000000

#define PACKET_HOST 0

// ----- Helper functions for offsets to fields -----

// They all assume simple IP packets:
//   - no VLAN ethernet tags
//   - no IPv4 options (see IPV4_HLEN/TCP4_OFFSET/UDP4_OFFSET)
//   - no IPv6 extension headers
//   - no TCP options (see TCP_HLEN)

//#define ETH_HLEN sizeof(struct ethhdr)
#define IP4_HLEN sizeof(struct iphdr)
#define IP6_HLEN sizeof(struct ipv6hdr)
#define TCP_HLEN sizeof(struct tcphdr)
#define UDP_HLEN sizeof(struct udphdr)

// Offsets from beginning of L4 (TCP/UDP) header
#define TCP_OFFSET(field) offsetof(struct tcphdr, field)
#define UDP_OFFSET(field) offsetof(struct udphdr, field)

// Offsets from beginning of L3 (IPv4) header
#define IP4_OFFSET(field) offsetof(struct iphdr, field)
#define IP4_TCP_OFFSET(field) (IP4_HLEN + TCP_OFFSET(field))
#define IP4_UDP_OFFSET(field) (IP4_HLEN + UDP_OFFSET(field))

// Offsets from beginning of L3 (IPv6) header
#define IP6_OFFSET(field) offsetof(struct ipv6hdr, field)
#define IP6_TCP_OFFSET(field) (IP6_HLEN + TCP_OFFSET(field))
#define IP6_UDP_OFFSET(field) (IP6_HLEN + UDP_OFFSET(field))

// Offsets from beginning of L2 (ie. Ethernet) header (which must be present)
#define ETH_IP4_OFFSET(field) (ETH_HLEN + IP4_OFFSET(field))
#define ETH_IP4_TCP_OFFSET(field) (ETH_HLEN + IP4_TCP_OFFSET(field))
#define ETH_IP4_UDP_OFFSET(field) (ETH_HLEN + IP4_UDP_OFFSET(field))
#define ETH_IP6_OFFSET(field) (ETH_HLEN + IP6_OFFSET(field))
#define ETH_IP6_TCP_OFFSET(field) (ETH_HLEN + IP6_TCP_OFFSET(field))
#define ETH_IP6_UDP_OFFSET(field) (ETH_HLEN + IP6_UDP_OFFSET(field))

#ifdef linux
// ----- NEP Error Counters -----
#define COUNT_AND_RETURN(counter, ret) do {         \
	uint32_t code = BPF_TETHER_ERR_ ## counter; \
	noa_sim_get()->nep_error_counters[code]++;            \
	return ret;                                 \
} while(0)
#else

int32_t nep_error_counters[BPF_TETHER_ERR__MAX];

// ----- NEP Error Counters -----
#define COUNT_AND_RETURN(counter, ret)        \
  do {                                        \
    uint32_t code = BPF_TETHER_ERR_##counter; \
    nep_error_counters[code]++;               \
    LOCAL_DBG("%s", #counter);                \
    return ret;                               \
  } while (0)

#endif

#define LOG_AND_RETURN(act) \
  do {                      \
    LOCAL_DBG("%s", #act);  \
    return act;             \
  } while (0)

struct offload_info g_offload_info;

#define TC_DROP(counter) COUNT_AND_RETURN(counter, NET_ENGINE_ACT_DROP)
#define TC_PUNT(counter) COUNT_AND_RETURN(counter, NET_ENGINE_ACT_PUNT)

// Referred from Linux kernel: include/uapi/linux/ip.h
#define IPTOS_TOS_MASK		0x1E
#define IPTOS_TOS(tos)		((tos)&IPTOS_TOS_MASK)

// Referred from Linux kernel: include/uapi/linux/pkt_sched.h
#define TC_PRIO_BESTEFFORT		0
#define TC_PRIO_FILLER			1
#define TC_PRIO_BULK			2
#define TC_PRIO_INTERACTIVE_BULK	4
#define TC_PRIO_INTERACTIVE		6
#define TC_PRIO_CONTROL			7
#define TC_PRIO_MAX			15

// Referred from Linux kernel: net/ipv4/route.c
#define ECN_OR_COST(class)	TC_PRIO_##class
const __u8 ip_tos2prio[16] = {
	TC_PRIO_BESTEFFORT,
	ECN_OR_COST(BESTEFFORT),
	TC_PRIO_BESTEFFORT,
	ECN_OR_COST(BESTEFFORT),
	TC_PRIO_BULK,
	ECN_OR_COST(BULK),
	TC_PRIO_BULK,
	ECN_OR_COST(BULK),
	TC_PRIO_INTERACTIVE,
	ECN_OR_COST(INTERACTIVE),
	TC_PRIO_INTERACTIVE,
	ECN_OR_COST(INTERACTIVE),
	TC_PRIO_INTERACTIVE_BULK,
	ECN_OR_COST(INTERACTIVE_BULK),
	TC_PRIO_INTERACTIVE_BULK,
	ECN_OR_COST(INTERACTIVE_BULK)
};

#ifdef NOA_WLAN_REQUIRES_LLC_HEADER
static void TransEtherTo8023Hdr(struct ethhdr *eth, struct dot11_llc_snap_header *llc_hdr, u32 len)
{
	llc_hdr->oui[0] = 0;
	llc_hdr->oui[1] = 0;
	llc_hdr->oui[2] = 0;
	llc_hdr->ctl = 0x3;
	llc_hdr->dsap = 0xAA;
	llc_hdr->ssap = 0xAA;
	llc_hdr->type = eth->h_proto;
	eth->h_proto = htons(len - ETH_HLEN);
}
#endif

#define INCREMENT_COUNTER_LOCK(counter, offset) \
do {                                            \
	spin_lock(&g_offload_info.stats_lock);      \
	(*(counter) += offset);                     \
	spin_unlock(&g_offload_info.stats_lock);    \
} while(0)

uint64_t timer = 0;

static uint8_t *GetZeroMac(void)
{
	SEC_FAST_DATA static u8 zero_mac[ETH_ALEN] = { 0 };
	return &zero_mac[0];
}

uint8_t nep_tos2priority(uint8_t tos)
{
	return ip_tos2prio[IPTOS_TOS(tos)>>1];
}

static bool nep_flowid_lookup(u8 priority, u8 *dest_mac, nep_forward_info *nw_info)
{
#ifdef NOA_FLOW_TABLE_LOOKUP_BYPASS
	(void)priority;
	(void)dest_mac;
	(void)nw_info;

	return true;
#else /* NOA_FLOW_TABLE_LOOKUP_BYPASS */
	TetherFlowIdKey flow_id_k;
	TetherFlowIdValue flow_id_v;

	nw_info->priority = 0;
	nw_info->flowid = 0;
	if (memcmp(dest_mac, GetZeroMac(), ETH_ALEN)) {
		memcpy(flow_id_k.dstMac, dest_mac, ETH_ALEN);
		memset(flow_id_k.zero, 0, sizeof(flow_id_k.zero));
		flow_id_k.priority = priority;
		if (nep_tables_flowid_map_lookup(&flow_id_k, &flow_id_v) != NEP_MAP_TABLE_ERR_NONE)
			return false;
		nw_info->priority = flow_id_k.priority;
		nw_info->flowid = flow_id_v;
	}
	return true;
#endif /* NOA_FLOW_TABLE_LOOKUP_BYPASS */
}

static void build_eth_header(struct ethhdr *eth, uint8_t *src,
							 uint8_t *dst, uint16_t proto_host)
{
	memcpy(eth->h_source, src, ETH_ALEN);
	memcpy(eth->h_dest, dst, ETH_ALEN);
	eth->h_proto = htons(proto_host);
}

static __always_inline int do_forward6(struct nep_sk_buff *skb, const bool is_ethernet,
				       const bool downstream, nep_forward_info *nw_info)
{
	const int l2_header_size = is_ethernet ? sizeof(struct ethhdr) : 0;
	void *data;
	void *data_end;
	struct ethhdr *eth;
#ifdef NOA_WLAN_REQUIRES_LLC_HEADER
	struct dot11_llc_snap_header *llc_hdr = NULL;
#endif
	struct ipv6hdr *ip6;
	__u32 src32;
	__u32 dst32;
	TetherDownstream6Key kd;
	TetherUpstream6Key ku;
	Tether6Value v;
	TetherStatsValue *stat_v;
	uint64_t *limit_v;
	uint64_t packets;
	uint64_t L3_bytes;
	bool v_exist = false;

	// Since the program never writes via DPA (direct packet access) auto-pull/unclone logic
	// does not trigger and thus we need to manually make sure we can read packet headers via
	// DPA.
	// Note: this is a blind best effort pull, which may fail or pull less - this doesn't
	//       matter.
	// It has to be done early cause it will invalidate any skb->data/data_end derived pointers.
	//skb_try_make_writable(skb, l2_header_size + IP6_HLEN + TCP_HLEN);

	data = (void *)(long)skb->data;
	data_end = (void *)(long)skb->data_end;
	eth = is_ethernet ? (struct ethhdr*)data : NULL;  // used iff is_ethernet
	ip6 = is_ethernet ? (struct ipv6hdr*)(eth + 1) : (struct ipv6hdr*)data;

	// Must have (ethernet and) ipv6 header
	if ((void*)((long)data + l2_header_size + sizeof(*ip6)) > data_end)
		LOG_AND_RETURN(NET_ENGINE_ACT_PUNT);

	// Cannot decrement during forward if already zero or would be zero,
	// Let the kernel's stack handle these cases and generate appropriate ICMP errors.
	if (ip6->hop_limit <= 1)
		TC_PUNT(LOW_TTL);

	// If hardware offload is running and programming flows based on conntrack entries,
	// try not to interfere with it.
	nw_info->ipv6 = 1;
	nw_info->l3_len = sizeof(*ip6);
	if (ip6->nexthdr == IPPROTO_TCP) {
		struct tcphdr *tcph = (struct tcphdr*)(ip6 + 1);

		// Make sure we can get at the tcp header
		if ((void*)((long)data + l2_header_size + sizeof(*ip6) + sizeof(*tcph)) > data_end)
			TC_PUNT(INVALID_TCP_HEADER);

		nw_info->tcp = 1;
		nw_info->l4_len = tcph->doff << 2u;
	} else if (ip6->nexthdr == IPPROTO_UDP) {
		struct udphdr *udph = (struct udphdr *)(ip6 + 1);
		// Make sure we can get at the udp header
		if ((void*)((long)data + l2_header_size + sizeof(*ip6) + sizeof(*udph)) > data_end)
			TC_PUNT(SHORT_UDP_HEADER);
		nw_info->udp = 1;
		nw_info->l4_len = sizeof(*udph);
	}

	// Protect against forwarding packets sourced from ::1 or fe80::/64 or other weirdness.
	src32 = ip6->saddr.s6_addr32[0];
	if (src32 != htonl(0x0064ff9b) &&			  // 64:ff9b:/32 incl. XLAT464 WKP
		(src32 & htonl(0xe0000000)) != htonl(0x20000000)) // 2000::/3 Global Unicast
		TC_PUNT(NON_GLOBAL_SRC);

	// Protect against forwarding packets destined to ::1 or fe80::/64 or other weirdness.
	dst32 = ip6->daddr.s6_addr32[0];
	if (dst32 != htonl(0x0064ff9b) &&			  // 64:ff9b:/32 incl. XLAT464 WKP
		(dst32 & htonl(0xe0000000)) != htonl(0x20000000)) // 2000::/3 Global Unicast
		TC_PUNT(NON_GLOBAL_DST);

	// In the upstream direction do not forward traffic within the same /64 subnet.
	if (!downstream && (src32 == dst32) && (ip6->saddr.s6_addr32[1] == ip6->daddr.s6_addr32[1]))
		TC_PUNT(LOCAL_SRC_DST);

	if (downstream) {
		memset(&kd, 0, sizeof(TetherDownstream6Key));
		kd.neigh6 = ip6->daddr;
		if (is_ethernet) {
			__builtin_memcpy(kd.dstMac, eth->h_dest, ETH_ALEN);
		}

		v_exist = nep_tables_downstream6_map_lookup(&kd, &v) == NEP_MAP_TABLE_ERR_NONE;
	} else {
		memset(&ku, 0, sizeof(TetherUpstream6Key));
		if (is_ethernet) {
			__builtin_memcpy(ku.dstMac, eth->h_dest, ETH_ALEN);
		}
		memcpy(&ku.src64, ip6->saddr.s6_addr, sizeof(uint64_t));

		v_exist = nep_tables_upstream6_map_lookup(&ku, &v) == NEP_MAP_TABLE_ERR_NONE;
	}

	// If we don't find any offload information then simply let the core stack handle it...
	if (!v_exist)
		LOG_AND_RETURN(NET_ENGINE_ACT_PUNT);

	// If we can't find the flow id information for wifi TX then simply let
	// the core stack handle it (it will create a new one when being forwarded).
	// Currently, we assume priority = 0 for all IPv6 packets from modem.
	if (!nep_flowid_lookup(nw_info->priority, v.macHeader.h_dest, nw_info))
		TC_PUNT(NO_FLOWID_ENTRY);

	limit_v = &g_offload_info.limit_bytes;
	stat_v = &g_offload_info.stats;

	// Required IPv6 minimum mtu is 1280, below that not clear what we should do, abort...
	if (v.pmtu < IPV6_MIN_MTU)
		TC_PUNT(BELOW_IPV6_MTU);

	// Approximate handling of TCP/IPv6 overhead for incoming LRO/GRO packets: default
	// outbound path mtu of 1500 is not necessarily correct, but worst case we simply
	// undercount, which is still better then not accounting for this overhead at all.
	// Note: this really shouldn't be device/path mtu at all, but rather should be
	// derived from this particular connection's mss (ie. from gro segment size).
	// This would require a much newer kernel with newer ebpf accessors.
	// (This is also blindly assuming 12 bytes of tcp timestamp option in tcp header)
	packets = 1;
	L3_bytes = skb->len - l2_header_size;
	if (L3_bytes > v.pmtu) {
		const int tcp6_overhead = sizeof(struct ipv6hdr) + sizeof(struct tcphdr) + 12;
		const int mss = v.pmtu - tcp6_overhead;
		const uint64_t payload = L3_bytes - tcp6_overhead;
		packets = (payload + mss - 1) / mss;
		L3_bytes = tcp6_overhead * packets + payload;
	}

	// Are we past the limit?  If so, then abort...
	// Note: will not overflow since u64 is 936 years even at 5Gbps.
	// Do not drop here.  Offload is just that, whenever we fail to handle
	// a packet we let the core stack deal with things.
	// (The core stack needs to handle limits correctly anyway,
	// since we don't offload all traffic in both directions)
	if (stat_v->rxBytes + stat_v->txBytes + L3_bytes > *limit_v) {
		TC_PUNT(LIMIT_REACHED);
	}

	if (!is_ethernet) {
		// The rest of this function works on a standard L2 frame, pointing
		// it to the start of the L2 header.
		skb->data -= ETH_HLEN;
		skb->len += ETH_HLEN;
		data = (void *)(long)skb->data;
		eth = (struct ethhdr *)data;
		ip6 = (struct ipv6hdr *)(eth + 1);
	}

	// At this point we always have an ethernet header - which will get stripped by the
	// kernel during transmit through a rawip interface.  ie. 'eth' pointer is valid.
	// Additionally note that 'is_ethernet' and 'l2_header_size' are no longer correct.

	--ip6->hop_limit;

	INCREMENT_COUNTER_LOCK(downstream ? &stat_v->rxPackets : &stat_v->txPackets, packets);
	INCREMENT_COUNTER_LOCK(downstream ? &stat_v->rxBytes : &stat_v->txBytes, L3_bytes);

	nw_info->is_wlan_forward = !!memcmp(v.macHeader.h_dest, GetZeroMac(), ETH_ALEN);

	if (nw_info->is_wlan_forward) {
#ifdef NOA_WLAN_REQUIRES_LLC_HEADER
		skb->data -= DOT11_LLC_SNAP_HDR_LEN;
		skb->len += DOT11_LLC_SNAP_HDR_LEN;
		eth = (struct ethhdr *)skb->data;
		llc_hdr = (struct dot11_llc_snap_header *)(eth +1);
		// Overwrite any mac header with the new one
		// For a rawip tx interface it will simply be a bunch of zeroes and later stripped.
		*eth = v.macHeader;
		TransEtherTo8023Hdr(eth, llc_hdr, skb->len);
#else
		*eth = v.macHeader;
#endif
	} else {
		skb->data += ETH_HLEN;
		skb->len -= ETH_HLEN;
	}

	LOG_AND_RETURN(NET_ENGINE_ACT_FORWARD);
}

// ----- IPv4 Support -----

static __always_inline int do_forward4_bottom(struct nep_sk_buff *skb, const int l2_header_size,
					      void *data, const void *data_end, struct ethhdr *eth,
					      struct iphdr *ip, const bool is_ethernet,
					      const bool downstream,
					      const bool is_tcp, nep_forward_info *nw_info)
{
#ifdef NOA_WLAN_REQUIRES_LLC_HEADER
	struct dot11_llc_snap_header *llc_hdr = NULL;
#endif
	struct tcphdr *tcph = is_tcp ? (struct tcphdr*)(ip + 1) : NULL;
	struct udphdr *udph = is_tcp ? NULL : (struct udphdr*)(ip + 1);
	Tether4Key k;
	Tether4Value v;
	TetherStatsValue *stat_v;
	uint64_t *limit_v;
	uint64_t packets;
	uint64_t L3_bytes;
	uint64_t last_used;
	int sz2 = sizeof(__u16);
	__u16 old_ttl_proto;
	__u16 new_ttl_proto;
	int l4_offs_csum;
	int sz4 = sizeof(__u32);
	// UDP 0 is special and stored as FFFF (this flag also causes a csum of 0 to be unmodified)
	int l4_flags;
	bool v_exist = false;
	__u32 old_daddr;
	__u32 old_saddr;
	__u32 new_daddr;
	__u32 new_saddr;

	nw_info->l3_len = sizeof(*ip);
	if (is_tcp) {
		// Make sure we can get at the tcp header
		if ((void*)((long)data + l2_header_size + sizeof(*ip) + sizeof(*tcph)) > data_end)
			TC_PUNT(SHORT_TCP_HEADER);

#ifdef NOA_TCP_CONTROL_NO_FORWARDING_SEARCH
		// If hardware offload is running and programming flows based on conntrack entries,
		// try not to interfere with it, so do not offload TCP packets with any one of the
		// SYN/FIN/RST flags
		if (tcph->syn || tcph->fin || tcph->rst)
			TC_PUNT(TCPV4_CONTROL_PACKET);
#endif /* NOA_TCP_CONTROL_NO_FORWARDING_SEARCH */
		nw_info->tcp = 1;
		nw_info->l4_len = tcph->doff << 2u;
	} else { // UDP
		// Make sure we can get at the udp header
		if ((void*)((long)data + l2_header_size + sizeof(*ip) + sizeof(*udph)) > data_end)
			TC_PUNT(SHORT_UDP_HEADER);

		// Since we do not support UDP checksum calculation, we will punt packets with a
		// zero UDP checksum to the APC for processing.
		if (!udph->check)
			TC_PUNT(UDP_CSUM_ZERO);

		nw_info->udp = 1;
		nw_info->l4_len = sizeof(*udph);
	}

	memset(&k, 0, sizeof(Tether4Key));
	k.l4Proto = ip->protocol;
	k.src4.s_addr = ip->saddr;
	k.dst4.s_addr = ip->daddr;
	k.srcPort = is_tcp ? tcph->source : udph->source;
	k.dstPort = is_tcp ? tcph->dest : udph->dest;

	// TODO: protect the iif and dest mac of rx packet here.

#ifdef linux
	last_used = ktime_to_ns(ktime_get_boottime());
#else
	last_used = uint64_t(pw::chrono::SystemClock::now().time_since_epoch().count());
#endif

	if (downstream) {
		v_exist = nep_tables_downstream4_map_lookup(&k, &v, last_used) == NEP_MAP_TABLE_ERR_NONE;
	}
	else {
		v_exist = nep_tables_upstream4_map_lookup(&k, &v, last_used) == NEP_MAP_TABLE_ERR_NONE;
	}

	// If we don't find any offload information then simply let the core stack handle it...
	if (!v_exist) {
		LOG_AND_RETURN(NET_ENGINE_ACT_PUNT);
	}

	// If we can't find the flow id information for wifi TX then simply let
	// the core stack handle it (it will create a new one when being forwarded).
	if (!nep_flowid_lookup(nw_info->priority, v.dstMac, nw_info))
		TC_PUNT(NO_FLOWID_ENTRY);

	limit_v = &g_offload_info.limit_bytes;
	stat_v = &g_offload_info.stats;

	// Required IPv4 minimum mtu is 68, below that not clear what we should do, abort...
	if (g_offload_info.pmtu < 68)
		TC_PUNT(BELOW_IPV4_MTU);

	// Approximate handling of TCP/IPv4 overhead for incoming LRO/GRO packets: default
	// outbound path mtu of 1500 is not necessarily correct, but worst case we simply
	// undercount, which is still better then not accounting for this overhead at all.
	// Note: this really shouldn't be device/path mtu at all, but rather should be
	// derived from this particular connection's mss (ie. from gro segment size).
	// This would require a much newer kernel with newer ebpf accessors.
	// (This is also blindly assuming 12 bytes of tcp timestamp option in tcp header)
	packets = 1;
	L3_bytes = skb->len - l2_header_size;
	if (L3_bytes > g_offload_info.pmtu) {
		const int tcp4_overhead = sizeof(struct iphdr) + sizeof(struct tcphdr) + 12;
		const int mss = g_offload_info.pmtu - tcp4_overhead;
		const uint64_t payload = L3_bytes - tcp4_overhead;
		packets = (payload + mss - 1) / mss;
		L3_bytes = tcp4_overhead * packets + payload;
	}

	// Are we past the limit?  If so, then abort...
	// Note: will not overflow since u64 is 936 years even at 5Gbps.
	// Do not drop here.  Offload is just that, whenever we fail to handle
	// a packet we let the core stack deal with things.
	// (The core stack needs to handle limits correctly anyway,
	// since we don't offload all traffic in both directions)
	if (stat_v->rxBytes + stat_v->txBytes + L3_bytes > *limit_v) {
		TC_PUNT(LIMIT_REACHED);
	}

	if (!is_ethernet) {
		// The rest of this function works on a standard L2 frame. Adjust the
		// skb->data pointer to skip past the reserved LLC headroom, pointing
		// it to the start of the L2 header.
		skb->data -= ETH_HLEN;
		skb->len += ETH_HLEN;
		// skb invalidates all pointers - reload them
		data = (void *)(long)skb->data;
		eth = (struct ethhdr *)data;
		ip = (struct iphdr*)(eth + 1);
		tcph = is_tcp ? (struct tcphdr*)(ip + 1) : NULL;
		udph = is_tcp ? NULL : (struct udphdr*)(ip + 1);
	};

	// At this point we always have an reserved ethernet header - which will get stripped by the
	// kernel during transmit through a rawip interface.  ie. 'eth' pointer is valid.
	// Additionally note that 'is_ethernet' and 'l2_header_size' are no longer correct.

	// Decrement the IPv4 TTL, we already know it's greater than 1.
	// u8 TTL field is followed by u8 protocol to make a u16 for ipv4 header checksum update.
	// Since we're keeping the ipv4 checksum valid (which means the checksum of the entire
	// ipv4 header remains 0), the overall checksum of the entire packet does not change.
	old_ttl_proto = *(__u16 *)&ip->ttl;
	new_ttl_proto = old_ttl_proto - htons(0x0100);
	nep_l3_csum_replace(skb, ETH_IP4_OFFSET(check), old_ttl_proto, new_ttl_proto, sz2);
	nep_skb_store_bytes(skb, ETH_IP4_OFFSET(ttl), &new_ttl_proto, sz2);

	l4_offs_csum = is_tcp ? ETH_IP4_TCP_OFFSET(check) : ETH_IP4_UDP_OFFSET(check);

	// UDP 0 is special and stored as FFFF (this flag also causes a csum of 0 to be unmodified)
	l4_flags = is_tcp ? 0 : BPF_F_MARK_MANGLED_0;

	// The offsets for TCP and UDP ports: source (u16 @ L4 offset 0) & dest (u16 @ L4 offset 2)
	// are actually the same, so the compiler should just optimize them both down to a constant.
	if (!downstream) {
		old_saddr = k.src4.s_addr;
		new_saddr = v.mangle46.s6_addr32[3];
		nep_l4_csum_replace(skb, l4_offs_csum, old_saddr, new_saddr,
				    sz4 | BPF_F_PSEUDO_HDR | l4_flags);
		nep_l3_csum_replace(skb, ETH_IP4_OFFSET(check), old_saddr, new_saddr, sz4);
		nep_skb_store_bytes(skb, ETH_IP4_OFFSET(saddr), &new_saddr, sz4);

		nep_l4_csum_replace(skb, l4_offs_csum, k.srcPort, v.manglePort, sz2 | l4_flags);
		nep_skb_store_bytes(skb, is_tcp ? ETH_IP4_TCP_OFFSET(source) : ETH_IP4_UDP_OFFSET(source),
					&v.manglePort, sz2);
	} else {
		old_daddr = k.dst4.s_addr;
		new_daddr = v.mangle46.s6_addr32[3];
		nep_l4_csum_replace(skb,
							l4_offs_csum, old_daddr, new_daddr, sz4 | BPF_F_PSEUDO_HDR | l4_flags);
		nep_l3_csum_replace(skb, ETH_IP4_OFFSET(check), old_daddr, new_daddr, sz4);
		nep_skb_store_bytes(skb, ETH_IP4_OFFSET(daddr), &new_daddr, sz4);

		nep_l4_csum_replace(skb, l4_offs_csum, k.dstPort, v.manglePort, sz2 | l4_flags);
		nep_skb_store_bytes(skb, is_tcp ? ETH_IP4_TCP_OFFSET(dest) : ETH_IP4_UDP_OFFSET(dest),
			&v.manglePort, sz2);
	}

#ifdef linux
#else
	// Initialize timer
	if (timer == 0) {
		timer = last_used;
	}

	if (timer + EXTEND_TIMEOUT_CALLBACK_NS <= last_used) {
		NepRpcService* nep_rpc_service = GetNepService();

		if (nep_rpc_service) {
			nep_rpc_service->send_event_to_apc_from_nep(CMD_CALLBACK_EXTEND_TIMEOUT,
														&timer, sizeof(uint64_t));
		}

		// Reschedule timer
		timer = last_used;
	}
#endif

	INCREMENT_COUNTER_LOCK(downstream ? &stat_v->rxPackets : &stat_v->txPackets, packets);
	INCREMENT_COUNTER_LOCK(downstream ? &stat_v->rxBytes : &stat_v->txBytes, L3_bytes);

	// Set destination MAC address.
	nw_info->is_wlan_forward = !!memcmp(v.dstMac, GetZeroMac(), ETH_ALEN);

	if (nw_info->is_wlan_forward) {
#ifdef NOA_WLAN_REQUIRES_LLC_HEADER
		skb->data -= DOT11_LLC_SNAP_HDR_LEN;
		skb->len += DOT11_LLC_SNAP_HDR_LEN;
		eth = (struct ethhdr *)skb->data;
		llc_hdr = (struct dot11_llc_snap_header *)(eth +1);
		build_eth_header(eth,
					 downstream ? g_offload_info.downstream_if_Mac : g_offload_info.upstream_if_Mac,
					 v.dstMac,
					 ETH_P_IP);
		TransEtherTo8023Hdr(eth, llc_hdr, skb->len);
#else
		build_eth_header(eth,
					 downstream ? g_offload_info.downstream_if_Mac : g_offload_info.upstream_if_Mac,
					 v.dstMac,
					 ETH_P_IP);
#endif
	} else {
		skb->data += ETH_HLEN;
		skb->len -= ETH_HLEN;
	}

	LOG_AND_RETURN(NET_ENGINE_ACT_FORWARD);
}

static __always_inline int do_forward4(struct nep_sk_buff *skb, const bool is_ethernet,
				       const bool downstream,
				       nep_forward_info *nw_info)
{
	const int l2_header_size = is_ethernet ? sizeof(struct ethhdr) : 0;
	void *data;
	const void *data_end;
	struct ethhdr *eth;  // used iff is_ethernet
	struct iphdr *ip;
	bool is_tcp;

	// Since the program never writes via DPA (direct packet access) auto-pull/unclone logic
	// does not trigger and thus we need to manually make sure we can read packet headers via
	// DPA.
	// Note: this is a blind best effort pull, which may fail or pull less - this doesn't
	//       matter.
	// It has to be done early cause it will invalidate any skb->data/data_end derived pointers.
	//skb_try_make_writable(skb, l2_header_size + IP4_HLEN + TCP_HLEN);

	data = (void *)(long)skb->data;
	data_end = (void *)(long)skb->data_end;
	eth = is_ethernet ? (struct ethhdr*)data : NULL;  // used iff is_ethernet
	ip = is_ethernet ? (struct iphdr*)(eth + 1) : (struct iphdr*)data;

	// Must have (ethernet and) ipv4 header
	if ((void*)((long)data + l2_header_size + sizeof(*ip)) > data_end)
		LOG_AND_RETURN(NET_ENGINE_ACT_PUNT);

	// We cannot handle IP options, just standard 20 byte == 5 dword minimal IPv4 header
	if (ip->ihl != 5)
		TC_PUNT(HAS_IP_OPTIONS);

	// Minimum IPv4 total length is the size of the header
	if ((uint32_t)ntohs(ip->tot_len) < sizeof(*ip))
		TC_PUNT(TRUNCATED_IPV4);

	// We are incapable of dealing with IPv4 fragments
	if (ip->frag_off & ~htons(IP_DF))
		TC_PUNT(IS_IP_FRAG);

	// Cannot decrement during forward if already zero or would be zero,
	// Let the kernel's stack handle these cases and generate appropriate ICMP errors.
	if (ip->ttl <= 1)
		TC_PUNT(LOW_TTL);

	is_tcp = (ip->protocol == IPPROTO_TCP);

	// We do not support offloading anything besides IPv4 TCP and UDP, due to need for NAT.
	if (!is_tcp && (ip->protocol != IPPROTO_UDP))
		TC_PUNT(NON_TCP_UDP);

	// This is a bit of a hack to make things easier on the bpf verifier.
	// (In particular I believe the Linux 4.14 kernel's verifier can get confused later on about
	// what offsets into the packet are valid and can spuriously reject the program, this is
	// because it fails to realize that is_tcp && !is_tcp is impossible)
	//
	// For both TCP & UDP we'll need to read and modify the src/dst ports, which so happen to
	// always be in the first 4 bytes of the L4 header.  Additionally for UDP we'll need access
	// to the checksum field which is in bytes 7 and 8.  While for TCP we'll need to read the
	// TCP flags (at offset 13) and access to the checksum field (2 bytes at offset 16).
	// As such we *always* need access to at least 8 bytes.
	if ((void*)((long)data + l2_header_size + sizeof(*ip) + 8) > data_end)
		TC_PUNT(SHORT_L4_HEADER);

	// We're forcing the compiler to emit two copies of the following code, optimized
	// separately for is_tcp being true or false.  This simplifies the resulting bpf
	// byte code sufficiently that the 4.14 bpf verifier is able to keep track of things.
	// Without this (updatetime == true) case would fail to bpf verify on 4.14 even
	// if the underlying requisite kernel support (ktime_get_boottime) was backported.
	nw_info->ipv4 = 1;
	if (is_tcp) {
		return do_forward4_bottom(skb, l2_header_size, data, data_end, eth, ip, is_ethernet,
					  downstream, /* is_tcp */ true, nw_info);
	} else {
		return do_forward4_bottom(skb, l2_header_size, data, data_end, eth, ip, is_ethernet,
					  downstream, /* is_tcp */ false, nw_info);
	}
}

int do_process_pkt(void **data, u32 *len, bool is_ethernet, bool downstream, int fallback_port,
		   nep_forward_info *nw_info)
{
	uint8_t output_port_id;
	uint8_t input_port_id;

	void *data_end = (void *)((long)*data + *len);
	const struct ethhdr *eth = (struct ethhdr*)*data;
	const struct iphdr* iphdr =
			(struct iphdr*)(is_ethernet ? (void*)((long)*data + sizeof(struct ethhdr))
					: *data);
	const u16 protocol = iphdr->version == 6 ? htons(ETH_P_IPV6) : htons(ETH_P_IP);
	int ret = NET_ENGINE_ACT_PUNT;
	struct nep_sk_buff skb = {
		.data = (__u8*)*data,
		.data_end = (__u8*)data_end,
		.len = *len,
		.protocol = protocol,
		.ifindex = 0,
	};
	struct noa_netengine_stat *netengine_stat = &g_offload_info.netengine_stat;

	if (NET_DBG_ENABLE) hexdump("packet(in):", (uint8_t *) *data, *len);

	// Make sure we actually have an ethernet header
	if (is_ethernet && (void*)(eth + 1) > data_end)
		LOG_AND_RETURN(NET_ENGINE_ACT_PUNT);

	if (protocol == htons(ETH_P_IPV6)) {
		netengine_stat->ipv6_packets++;
		netengine_stat->ipv6_bytes += *len;
		nw_info->ethertype = ETH_P_IPV6;
		nw_info->priority = 0;
		ret = do_forward6(&skb, is_ethernet, downstream, nw_info);
	} else if (protocol == htons(ETH_P_IP)) {
		netengine_stat->ipv4_packets++;
		netengine_stat->ipv4_bytes += *len;
		nw_info->ethertype = ETH_P_IP;
		nw_info->priority = nep_tos2priority(iphdr->tos);
		ret = do_forward4(&skb, is_ethernet, downstream, nw_info);
	} else {
		LOCAL_DBG("Unsupported protocol = %" PRIu16 "", protocol);
	}

	if (ret == NET_ENGINE_ACT_FORWARD) {
		*len = skb.len;
		*data = skb.data;
		nw_info->oif = downstream ? g_offload_info.downstreamIif : g_offload_info.upstreamIif;
		output_port_id = nw_info->is_wlan_forward ? NOA_PORT_WLAN_FW : NOA_PORT_MODEM_FW;
	} else {
		output_port_id = fallback_port;
	}

	LOCAL_DBG("%s protocol = %u, ret = %d, output_port = %d\n",
		downstream? "downstream" : "upstream", protocol, ret, output_port_id);
	if (NET_DBG_ENABLE) hexdump("packet(out):", (uint8_t *) *data, *len);

	input_port_id = is_ethernet ? NOA_PORT_WLAN_FW : NOA_PORT_MODEM_FW;
	if (output_port_id < NOA_PORT_MAX && input_port_id < NOA_PORT_MAX) {
		if (downstream) {
			if (protocol == htons(ETH_P_IPV6)) {
				netengine_stat->downstream_src_v6[input_port_id]++;
				netengine_stat->downstream_dst_v6[output_port_id]++;
			} else {
				netengine_stat->downstream_src_v4[input_port_id]++;
				netengine_stat->downstream_dst_v4[output_port_id]++;
			}
		} else {
			if (protocol == htons(ETH_P_IPV6)) {
				netengine_stat->upstream_src_v6[input_port_id]++;
				netengine_stat->upstream_dst_v6[output_port_id]++;
			} else {
				netengine_stat->upstream_src_v4[input_port_id]++;
				netengine_stat->upstream_dst_v4[output_port_id]++;
			}
		}
	} else {
		pr_err("Failed to update netengine stat with "
		       "output path %" PRIx8 ", input path %" PRIx8 "\n",
		       output_port_id, input_port_id);
	}

	// Anything else we don't know how to handle...
	return ret;
}

struct offload_info* get_tethering_offload_info(void) {
	return &g_offload_info;
}

void offload_init(void) {
	spin_lock_init(&g_offload_info.stats_lock);
}

void get_nep_stats(TetherStats *stats) {
	spin_lock(&g_offload_info.stats_lock);
	memcpy(&stats->value, &g_offload_info.stats, sizeof(TetherStatsValue));
	spin_unlock(&g_offload_info.stats_lock);
}

#ifdef linux
EXPORT_SYMBOL_GPL(do_process_pkt);
EXPORT_SYMBOL_GPL(get_tethering_offload_info);
EXPORT_SYMBOL_GPL(offload_init);
EXPORT_SYMBOL_GPL(get_nep_stats);
#else
int32_t get_nep_error_counters(int32_t error) {
  return error < BPF_TETHER_ERR__MAX ? nep_error_counters[error] : 0;
}
#endif
