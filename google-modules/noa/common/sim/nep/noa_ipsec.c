// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handles XFRM packet offload.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Mike Yu <yumike@google.com>
 */

#include "noa_ipsec.h"

#include <net/xfrm.h>
#include <net/esp.h>
#include <net/ip6_route.h>

#include "noa_ipsec_utils.h"

// Align with IPsec Engine 1.1 Arch Spec Section 3.
#define NOA_VPN_SA_TABLE_SIZE 32
#define NOA_VPN_SP_TABLE_SIZE 32

struct noa_sa_info {
	// The SA shared from the kernel.
	struct xfrm_state *state;
	// A flag to check whether `state` is valid to access.
	// TODO(b/321875336): Remove this field and replace it with state != NULL.
	bool occupied;
	// The ESP seq that will be used for the next ESP packet during encryption.
	int oseq;
	// An unique sequence number for this SA.
	int seq;
};

struct noa_sp_info {
	// The SP shared from the kernel.
	struct xfrm_policy *policy;
	// A flag to check whether `policy` is valid to access.
	// TODO(b/321875336): Remove this field and replace it with state != NULL.
	bool occupied;
};

static struct mutex noa_vpn_mutex;
static struct noa_sa_info sa_table[NOA_VPN_SA_TABLE_SIZE];
static struct noa_sp_info sp_table[NOA_VPN_SP_TABLE_SIZE];

// Looks up the SA table for a given `state`, and returns the corresponding entry. A NULL is
// returned if not found.
static struct noa_sa_info *sa_table_find_locked(struct xfrm_state *state)
{
	for (int i = 0; i < NOA_VPN_SA_TABLE_SIZE; i++) {
		struct noa_sa_info *entry = &sa_table[i];

		if (entry->occupied && entry->state == state)
			return entry;
	}
	return NULL;
}

// Returns true if `state` is inserted to the SA table.
// Returns false if the insertion fails. The possible reasons are:
//   1) `state` already exists in the table.
//   2) The table is full.
static bool sa_table_insert_locked(struct xfrm_state *state, int seq)
{
	struct noa_sa_info *first_available_entry = NULL;

	for (int i = 0; i < NOA_VPN_SA_TABLE_SIZE; i++) {
		struct noa_sa_info *entry = &sa_table[i];

		if (!entry->occupied && first_available_entry == NULL)
			first_available_entry = entry;

		if (entry->occupied && entry->state == state)
			return false;
	}

	if (!first_available_entry)
		return false;

	first_available_entry->state = state;
	first_available_entry->occupied = true;
	first_available_entry->seq = seq;

	// Set the initial value to 1, according to rfc4303#section-3.3.3.
	first_available_entry->oseq = 1;
	return true;
}

// Returns true if `state` is deleted from the SA table.
// Returns false if the deletion fails, which indicates `state` doesn't exist in the SA table.
static bool sa_table_delete_locked(struct xfrm_state *state)
{
	struct noa_sa_info *entry = sa_table_find_locked(state);

	if (!entry)
		return false;

	entry->state = NULL;
	entry->occupied = false;
	return true;
}

// Looks up the SP table for a given `policy`, and returns the corresponding entry. A NULL is
// returned if not found.
static struct noa_sp_info *sp_table_find_locked(struct xfrm_policy *policy)
{
	for (int i = 0; i < NOA_VPN_SP_TABLE_SIZE; i++) {
		struct noa_sp_info *entry = &sp_table[i];

		if (entry->occupied && entry->policy == policy)
			return entry;
	}
	return NULL;
}

// Returns true if `policy` is inserted to the SP table.
// Returns false if the insertion fails. The possible reasons are:
//   1) `policy` already exists in the table.
//   2) The table is full.
static bool sp_table_insert_locked(struct xfrm_policy *policy)
{
	struct noa_sp_info *first_available_entry = NULL;

	for (int i = 0; i < NOA_VPN_SP_TABLE_SIZE; i++) {
		struct noa_sp_info *entry = &sp_table[i];

		if (!entry->occupied && first_available_entry == NULL)
			first_available_entry = entry;

		if (entry->occupied && entry->policy == policy)
			return false;
	}

	if (!first_available_entry)
		return false;

	first_available_entry->policy = policy;
	first_available_entry->occupied = true;
	return true;
}

// Returns true if `policy` is deleted from the SP table.
// Returns false if the deletion fails, which indicates `policy` doesn't exist in the SP table.
static bool sp_table_delete_locked(struct xfrm_policy *policy)
{
	struct noa_sp_info *entry = sp_table_find_locked(policy);

	if (!entry)
		return false;

	entry->policy = NULL;
	entry->occupied = false;
	return true;
}

static int noa_ipsec_add_state(struct xfrm_state *state, int seq)
{
	mutex_lock(&noa_vpn_mutex);
	sa_table_insert_locked(state, seq);
	mutex_unlock(&noa_vpn_mutex);
	return 0;
}

static void noa_ipsec_del_state(struct xfrm_state *state)
{
	mutex_lock(&noa_vpn_mutex);
	sa_table_delete_locked(state);
	mutex_unlock(&noa_vpn_mutex);
}

static void noa_ipsec_free_state(struct xfrm_state *state)
{
	mutex_lock(&noa_vpn_mutex);
	sa_table_delete_locked(state);
	mutex_unlock(&noa_vpn_mutex);
}

static int noa_ipsec_add_policy(struct xfrm_policy *policy)
{
	mutex_lock(&noa_vpn_mutex);
	sp_table_insert_locked(policy);
	mutex_unlock(&noa_vpn_mutex);
	return 0;
}

static void noa_ipsec_del_policy(struct xfrm_policy *policy)
{
	mutex_lock(&noa_vpn_mutex);
	sp_table_delete_locked(policy);
	mutex_unlock(&noa_vpn_mutex);
}

static void noa_ipsec_free_policy(struct xfrm_policy *policy)
{
	mutex_lock(&noa_vpn_mutex);
	sp_table_delete_locked(policy);
	mutex_unlock(&noa_vpn_mutex);
}

int noa_vpn_add_state(struct xfrm_state *state, int seq)
{
	return noa_ipsec_add_state(state, seq);
}
EXPORT_SYMBOL_GPL(noa_vpn_add_state);

void noa_vpn_del_state(struct xfrm_state *state)
{
	return noa_ipsec_del_state(state);
}
EXPORT_SYMBOL_GPL(noa_vpn_del_state);

void noa_vpn_free_state(struct xfrm_state *state)
{
	return noa_ipsec_free_state(state);
}
EXPORT_SYMBOL_GPL(noa_vpn_free_state);

int noa_vpn_add_policy(struct xfrm_policy *policy)
{
	return noa_ipsec_add_policy(policy);
}
EXPORT_SYMBOL_GPL(noa_vpn_add_policy);

void noa_vpn_del_policy(struct xfrm_policy *policy)
{
	return noa_ipsec_del_policy(policy);
}
EXPORT_SYMBOL_GPL(noa_vpn_del_policy);

void noa_vpn_free_policy(struct xfrm_policy *policy)
{
	return noa_ipsec_free_policy(policy);
}
EXPORT_SYMBOL_GPL(noa_vpn_free_policy);

#if 0
// In Android IPsec, SP is configured with wildcard IP addresses. This makes the simulator
// unable determine whether a packet comes from ipsec interface by checking source/destination
// IP address. This method uses a workaround to check if the skb needs HW offload. The workaround
// is unreliable
bool is_encryption_needed(struct sk_buff *skb)
{
	int flags;

	if (is_ipv4(skb)) {
		flags = IPCB(skb)->flags;
		return  (flags & IPSKB_XFRM_TRANSFORMED) && !(flags & IPSKB_XFRM_TUNNEL_SIZE);
	}
	if (is_ipv6(skb)) {
		flags = IP6CB(skb)->flags;
		return flags & IP6SKB_XFRM_TRANSFORMED;
	}

	return false;
}
EXPORT_SYMBOL_GPL(is_encryption_needed);

bool is_decryption_needed(struct sk_buff *skb)
{
	if (is_esp(skb))
		return true;

	if (is_ipv4_udp_encap(skb, PACKET_IN)) {
		// For keep alive packets, let kernel to eat it.
		// For IKE packets, is it ok to let kernel to process them?
		if (is_ipv4_keep_alive(skb) || is_ike_in_ipv4_udp_encap(skb))
			return false;

		return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(is_decryption_needed);

static struct xfrm_state *get_matched_sa(struct sk_buff *skb, bool rx)
{
	for (int i = 0; i < NOA_VPN_SA_TABLE_SIZE; i++) {
		struct noa_sa_info *tmp = &sa_table[i];

		if (tmp->occupied && tmp->state && tmp->state->encap) {
			struct xfrm_encap_tmpl *encap = tmp->state->encap;

			if (rx) {
				struct udphdr *uh = udp_hdr(skb);

				// TODO: Compare the source and destination IP addresses as well.
				if (tmp->state->xso.dir == XFRM_DEV_OFFLOAD_IN
					&& encap->encap_sport == uh->dest
					&& encap->encap_dport == uh->source) {
					return tmp->state;
				}
			} else {
				// TODO: There is no good solution to find the SA for TX.
				// Let's just find the first dir=OUT SA for now.
				if (tmp->state->xso.dir == XFRM_DEV_OFFLOAD_OUT)
					return tmp->state;
			}
		}
	}

	return NULL;
}
#endif

__maybe_unused
static bool xfrm_id_match(const struct xfrm_id *id1, const struct xfrm_id *id2)
{
	return !memcmp(id1, id2, sizeof(struct xfrm_id));
}

// This function comes from addr_match() with non-functional change.
static inline bool ipv6_addr_match(const __be32 *a1, const __be32 *a2, u8 prefixlen)
{
	const unsigned int pdw = prefixlen >> 5; /* num of whole u32 in prefix */
	const unsigned int pbi = prefixlen & 0x1f;  /* num of bits in incomplete u32 in prefix */

	if (pdw)
		if (memcmp(a1, a2, pdw << 2))
			return false;

	if (pbi) {
		const __be32 mask = htonl((0xffffffff) << (32 - pbi));

		if ((a1[pdw] ^ a2[pdw]) & mask)
			return false;
	}

	return true;
}

// This function comes from addr4_match() without the check for sizeof(long) == 4 && prefixlen == 0.
static bool ipv4_addr_match(__be32 a1, __be32 a2, u8 prefixlen)
{
	return !((a1 ^ a2) & htonl(~0UL << (32 - prefixlen)));
}

static bool xfrm_policy_selector_saddr_match(const struct xfrm_selector *selector,
					     const unsigned char *packet)
{
	if (selector->prefixlen_s == 0)
		return false;

	if (is_pkt_ipv4(packet))
		return ipv4_addr_match(selector->saddr.a4, ((struct iphdr *) packet)->saddr,
				       selector->prefixlen_s);

	return ipv6_addr_match(selector->saddr.a6, ((struct ipv6hdr *) packet)->saddr.s6_addr32,
			       selector->prefixlen_s);
}

__maybe_unused
static struct xfrm_policy *lookup_sp(unsigned char *packet)
{
	const int family = is_pkt_ipv4(packet) ? AF_INET : AF_INET6;

	for (int i = 0; i < NOA_VPN_SP_TABLE_SIZE; i++) {
		const struct noa_sp_info *tmp = &sp_table[i];
		struct xfrm_policy *policy = tmp->policy;

		if (!tmp->occupied || !policy || policy->family != family)
			continue;

		if (xfrm_policy_selector_saddr_match(&policy->selector, packet))
			return policy;
	}

	return NULL;
}

static noa_xfrm_state lookup_sa_by_spi(u32 spi, int *oseq)
{
	struct noa_sa_info *tmp;

	for (int i = 0; i < NOA_VPN_SA_TABLE_SIZE; i++) {
		tmp = &sa_table[i];

		if (!tmp->occupied || !tmp->state)
			continue;

		if (spi == tmp->state->id.spi) {
			if (oseq)
				*oseq = tmp->oseq++;
			return (noa_xfrm_state){ .state = tmp->state,
						 .seq = tmp->seq };
		}
	}

	return (noa_xfrm_state){ .state = NULL, .seq = 0 };
}

// Looks up the SA for an inbound packet.
// The supported packets are:
// - IPv4 + UDP-encap + ESP
// - IPv6 + ESP
// - IPv6 + UDP-encap + ESP
static noa_xfrm_state lookup_sa_for_inbound_packet(unsigned char *packet)
{
	// TODO: The lookup should have compared xfrm_id (daddr, spi, and protocol), see
	// xfrm_state_lookup() for reference. However, in consideration of XLAT, let's do
	// the lookup based on spi, udp sport, and udp dport.
	const u32 spi = get_pkt_esp_spi(packet);

	if (spi != 0)
		return lookup_sa_by_spi(spi, NULL);

	return (noa_xfrm_state){ .state = NULL, .seq = 0 };
}

__maybe_unused
static struct xfrm_state *lookup_sa_for_outbound_packet_by_sp(struct xfrm_policy *policy, int *oseq)
{
	struct noa_sa_info *tmp;

	// Assume that the policy has only one template.
	// TODO: Follow xfrm_bundle_create() to understand how
	// to get the final matched xfrm_state.
	if (policy->xfrm_nr != 1)
		return NULL;

	for (int i = 0; i < NOA_VPN_SA_TABLE_SIZE; i++) {
		tmp = &sa_table[i];

		if (!tmp->occupied || !tmp->state)
			continue;

		if (xfrm_id_match(&policy->xfrm_vec[0].id, &tmp->state->id)) {
			*oseq = tmp->oseq++;
			return tmp->state;
		}
	}

	return NULL;
}

static noa_xfrm_state lookup_sa_for_outbound_packet_by_if_id(u32 if_id,
							     int *oseq)
{
	struct noa_sa_info *tmp;

	if (if_id == 0)
		return (noa_xfrm_state){ .state = NULL, .seq = 0 };

	for (int i = 0; i < NOA_VPN_SA_TABLE_SIZE; i++) {
		tmp = &sa_table[i];

		if (!tmp->occupied || !tmp->state || tmp->state->xso.dir == XFRM_DEV_OFFLOAD_IN)
			continue;

		if (if_id == tmp->state->if_id) {
			*oseq = tmp->oseq++;
			return (noa_xfrm_state){ .state = tmp->state,
						 .seq = tmp->seq };
		}
	}

	return (noa_xfrm_state){ .state = NULL, .seq = 0 };
}

// Looks up the SA for an outbound packet.
static noa_xfrm_state lookup_sa_for_outbound_packet(unsigned char *packet,
						    u32 if_id, int *oseq)
{
	return lookup_sa_for_outbound_packet_by_if_id(if_id, oseq);
}

// Given an inbound/outbound IPv4 packet `packet` in wire format, find the matched SA.
noa_xfrm_state lookup_sa(unsigned char *packet, int direction, int *oseq,
			 u32 if_id)
{
	return direction == PACKET_IN ? lookup_sa_for_inbound_packet(packet)
				      : lookup_sa_for_outbound_packet(packet, if_id, oseq);
}

#if 0
// A custom version of xfrm_skb_check_space() for skb that doesn't contains dst_entry.
static int noa_xfrm_skb_check_space(struct sk_buff *skb, struct xfrm_state *sa)
{
	int nhead = sa->props.header_len + LL_RESERVED_SPACE(skb->dev)
		- skb_headroom(skb);
	int ntail = skb->dev->needed_tailroom - skb_tailroom(skb);

	if (nhead <= 0) {
		if (ntail <= 0)
			return 0;
		nhead = 0;
	} else if (ntail < 0)
		ntail = 0;

	return pskb_expand_head(skb, nhead, ntail, GFP_ATOMIC);
}

// Part of the code in xfrmi_xmit() to find the dst_entry.
// TODO: Check if the dst_entry created in this function are probably releases.
static void find_and_set_skb_dst(struct sk_buff *skb, struct xfrm_state *sa)
{
	struct flowi fl;
	struct dst_entry *dst;
	struct rtable *rt = NULL;
	// Should it be skb-dev??
	struct net_device *dev = skb->dev;

	memset(&fl, 0, sizeof(fl));
	switch (skb->protocol) {
	case htons(ETH_P_IPV6):
		memset(IP6CB(skb), 0, sizeof(*IP6CB(skb)));
		xfrm_decode_session(skb, &fl, AF_INET6);
		fl.u.ip6.flowi6_oif = dev->ifindex;
		fl.u.ip6.flowi6_flags |= FLOWI_FLAG_ANYSRC;

		dst = ip6_route_output(dev_net(dev), NULL, &fl.u.ip6);
		if (dst->error) {
			dst_release(dst);
			return;
		}
		skb_dst_set(skb, dst);
		break;
	case htons(ETH_P_IP):
		memset(IPCB(skb), 0, sizeof(*IPCB(skb)));
		xfrm_decode_session(skb, &fl, AF_INET);
		fl.u.ip4.flowi4_oif = dev->ifindex;
		fl.u.ip4.flowi4_flags |= FLOWI_FLAG_ANYSRC;

		rt = __ip_route_output_key(dev_net(dev), &fl.u.ip4);
		if (IS_ERR(rt))
			return;

		skb_dst_set(skb, &rt->dst);
		break;
	default:
		return;
	}

	dst = skb_dst(skb);

	// Initiate a new dst_entry that contains xfrm.
	// TODO: dst might be invalid if xfrm_lookup_with_ifid() can't find a dst for the skb, which
	// results in crash in dump_dst_entry(). How to fix it?
	dst = xfrm_lookup_with_ifid(dev_net(skb->dev), dst, &fl, NULL, 0, sa->if_id);
	skb_dst_set(skb, dst);
}

// Step 1: Find the SA and SP
// - In XFRM stack, kernel uses xfrm_lookup_with_ifid() to find a dst_entry that contains
//   SA and SP, and then set the dst_entry to the skb. Afterwards, In xfrm_output_one(),
//   kernel passes the SA derived from the dst_entry to esp_output().
//
// Next, follow xfrm_output_one().
// Step 2: Set skb->mark = xfrm_smark_get(skb->mark, x)  // TODO: This doesn't seem needed.
// Step 3: Add encapsulation header by xfrm_outer_mode_output()
// Step 4: Check if the xfrm_state expires by xfrm_state_check_expire()  // TODO: To be implemented.
// Step 5: Run xfrm_replay_overflow()  // This will set skb's cb so that ESP header seq can be
//                                     // set correctly.
// Step 6: Update xfrm_state's curlft.bytes, curlft.packets, and lastused.
// Step 7: Run esp_output().
int do_encryption(struct sk_buff *skb)
{
	int err;
	struct ethhdr eth;
	struct iphdr *new_iph;
	struct xfrm_state *matched_sa;
	const int ETH_HDR_SIZE = sizeof(struct ethhdr);

	// The packet should be from either wifi or modem. It can be determined by the mac
	// header length.
	bool is_from_wifi = skb_mac_header_len(skb) == ETH_HDR_SIZE;

	// Step 1.
	matched_sa = get_matched_sa(skb, false /* rx */);
	if (!matched_sa)
		return -1;

	if (is_from_wifi) {
		// The ethernet header will become inner mac header. Copy it so that we will set
		// it to outer mac header.
		memcpy(&eth, eth_hdr(skb), ETH_HDR_SIZE);

		// Remove the mac header because they should not be encrypted.
		__skb_pull(skb, ETH_HDR_SIZE);
		skb_reset_mac_header(skb);
		skb_reset_mac_len(skb);
	}

	// TODO: Correct the UDP header checksum before encryption. For now, set it to 0.
	//udp_hdr(skb)->check = 0;

	find_and_set_skb_dst(skb, matched_sa);
	err = noa_xfrm_skb_check_space(skb, matched_sa);

	// Step 3
	// We want to call xfrm_outer_mode_output() but it's not public.
	// pktgen_xfrm_outer_mode_output() just fits our needs.
	err = pktgen_xfrm_outer_mode_output(matched_sa, skb);

	// Step 5
	// This will set skb's cb so that ESP header seq can be set correctly.
	err = xfrm_replay_overflow(matched_sa, skb);

	// Step 6.
	// Update stats.
	// TODO: They should be updated when noa_ipsec_update_curlft() is invoked.
	matched_sa->curlft.bytes += skb->len;
	matched_sa->curlft.packets++;
	matched_sa->lastused = ktime_get_real_seconds();

	// Step 7.
	esp_output(matched_sa, skb);

	// Update IP header
	new_iph = ip_hdr(skb);
	new_iph->id = 0;
	new_iph->tot_len = htons(skb->len);
	ip_send_check(new_iph);

	udp_hdr(skb);

	// Avoid HW setting UDP checksum??
	skb->ip_summed = 0;

	if (is_from_wifi) {
		// TODO: Error handling for headroom size is too small.
		if (skb_headroom(skb) >= ETH_HDR_SIZE) {
			// Add the mac header back.
			__skb_push(skb, ETH_HDR_SIZE);
			skb_reset_mac_header(skb);
			if (is_ipv4(skb))
				eth.h_proto = htons(ETH_P_IP);
			else if (is_ipv6(skb))
				eth.h_proto = htons(ETH_P_IPV6);

			memcpy(eth_hdr(skb), &eth, ETH_HDR_SIZE);
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(do_encryption);

// Follows xfrm4_udp_encap_rcv()
// Step 1: If this is a paged skb, make sure we pull up
//         whatever data we need to look at.
//         TODO: see if we need to implement step 1.
// Step 2: Check if it's a keep alive packet. If yes, eat it.
// ( Now the packet is an ESPinUDP packet, remove 'len' bytes from the packet (the UDP
//   header and optional ESP marker bytes) and then modify the
//   protocol to ESP, and then call into the transform receiver.
// Step 3: Update and verify the packet length
// Step 4: Pull the data buffer up to the ESP header and set the
//         transport header to point to ESP.  Keep UDP on the stack
//         for later.
// ( Next Call: xfrm4_rcv_encap(skb, IPPROTO_ESP, 0, encap_type))
//
// Follows xfrm4_rcv_encap()
// Step 5: Set skb's cb  // TODO: This doesn't seem needed.
//   XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip4 = NULL;
//   XFRM_SPI_SKB_CB(skb)->family = AF_INET;
//   XFRM_SPI_SKB_CB(skb)->daddroff = offsetof(struct iphdr, daddr);
// ( Next Call: xfrm_input())
//
// Follows xfrm_input()
// Step 6: Call esp_input()
// Step 7: Call xfrm_inner_mode_input() to correct some skb's field, including protocol.
static void do_offload(struct sk_buff *skb, struct xfrm_state *sa)
{
	struct iphdr *iph;
	int iphlen, len;
	int nexthdr;
	struct sec_path *sp;

	// Step 1: Not implemented.
	// Step 2: Done in is_decryption_needed().

	// After step 2, the packet is an ESP-in-UDP packet. Set `len` to the size
	// of UDP header length to drop UDP-encapsulation.
	len = sizeof(struct udphdr);

	// Step 3.
	iph = ip_hdr(skb);
	iphlen = iph->ihl << 2;
	iph->tot_len = htons(ntohs(iph->tot_len) - len);
	if (skb->len < iphlen + len)
		return;

	// Step 4.
	__skb_pull(skb, skb_network_header_len(skb));
	__skb_pull(skb, len);
	skb_reset_transport_header(skb);

	// Step 5
	// TODO: Do we need to set the skb's control buffer as what xfrm4_rcv_encap does?
	//   XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip4 = NULL;
	//   XFRM_SPI_SKB_CB(skb)->family = AF_INET;
	//   XFRM_SPI_SKB_CB(skb)->daddroff = offsetof(struct iphdr, daddr);
	//ret = xfrm4_rcv_encap(skb, IPPROTO_ESP, 0, UDP_ENCAP_ESPINUDP);

	// Step 6
	sp = secpath_set(skb);
	sp->xvec[sp->len++] = sa;
	sp->olen++;
	xfrm_state_hold(sa);
	nexthdr = esp_input(sa, skb);
	// TODO(b/321875336): esp_input() can return a negative value. Refer to xfrm_input() to
	// see how kernel handles it, and try to do similar thing here.

	// Step 7
	// Partially implemented.
	if (nexthdr == IPPROTO_IPIP)
		skb->protocol = htons(ETH_P_IP);
	else if (nexthdr == IPPROTO_IPV6)
		skb->protocol = htons(ETH_P_IPV6);

	skb_reset_network_header(skb);
	skb_mac_header_rebuild(skb);

	// Set the decryption status so that rx path won't fail in xfrm4_policy_check().
	xfrm_offload(skb)->flags = CRYPTO_DONE;
	xfrm_offload(skb)->status = CRYPTO_SUCCESS;
}

// Decrypts the packet, and returns whether the decryption succeeds or not.
// This functions supports decrypting ESP-in-UDP packets now. Don't pass other packets, such as
// keep-alive packets or ESP-in-IKE packets, to it.
bool do_decryption(struct sk_buff *skb)
{
	struct list_head *next = skb->list.next;
	struct list_head *prev = skb->list.prev;
	struct xfrm_state *matched_sa = get_matched_sa(skb, true /* rx */);

	if (!matched_sa)
		return false;

	do_offload(skb, matched_sa);

	// Set the skb to point to the original next/prev list.
	skb->list.next = next;
	skb->list.prev = prev;

	return true;
}
EXPORT_SYMBOL_GPL(do_decryption);
#endif
