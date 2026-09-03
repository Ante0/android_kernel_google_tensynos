/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Handles XFRM packet offload.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Mike Yu <yumike@google.com>
 */
#ifndef NOA_IPSEC_UTILS_H_
#define NOA_IPSEC_UTILS_H_

#include <net/xfrm.h>
#include <net/esp.h>

#define DEFAULT_MTU 1500
#define IKE_PORT 500
#define UDP_ENCAP_PORT 4500
#define IP_HEADER_SIZE (sizeof(struct iphdr))
#define IP6_HEADER_SIZE (sizeof(struct ipv6hdr))
#define UDP_HEADER_SIZE (sizeof(struct udphdr))
#define ESP_HEADER_SIZE (sizeof(struct ip_esp_hdr))  // 8 bytes for spi and seq.

enum packet_dir {
	PACKET_IN = XFRM_DEV_OFFLOAD_IN,
	PACKET_OUT = XFRM_DEV_OFFLOAD_OUT,
};

static inline bool is_ipv4(const struct sk_buff *skb)
{
	return skb->protocol == htons(ETH_P_IP);
}

static inline bool is_ipv6(const struct sk_buff *skb)
{
	return skb->protocol == htons(ETH_P_IPV6);
}

static inline bool is_udp(const struct sk_buff *skb)
{
	if (is_ipv4(skb))
		return ip_hdr(skb)->protocol == IPPROTO_UDP;

	// TODO: Go through all nexthdr field until ESP is found, because the first nexthdr
	// field may not be UDP.
	if (is_ipv6(skb))
		return ipv6_hdr(skb)->nexthdr == NEXTHDR_UDP;

	return false;
}

static inline bool is_pkt_ipv4(const unsigned char *packet)
{
	return ((struct iphdr *) packet)->version == 4;
}

static inline bool is_pkt_ipv6(const unsigned char *packet)
{
	return ((struct iphdr *) packet)->version == 6;
}

static inline struct udphdr *get_udp_header(const unsigned char *packet)
{
	if (is_pkt_ipv4(packet) && ((struct iphdr *) packet)->protocol == IPPROTO_UDP)
		return (struct udphdr *)(packet + IP_HEADER_SIZE);

	if (is_pkt_ipv6(packet) && ((struct ipv6hdr *) packet)->nexthdr == NEXTHDR_UDP) {
		// TODO: Go through all nexthdr field until NEXTHDR_UDP is found.
		return (struct udphdr *)(packet + 40);
	}
	return NULL;
}

static inline bool is_esp(const struct sk_buff *skb)
{
	if (is_ipv4(skb))
		return ip_hdr(skb)->protocol == IPPROTO_ESP;

	// TODO: Go through all nexthdr field until ESP is found, because the first nexthdr
	// field may not be ESP.
	if (is_ipv6(skb))
		return ipv6_hdr(skb)->nexthdr == NEXTHDR_ESP;

	return false;
}

static inline bool is_ipv4_udp(const struct sk_buff *skb)
{
	return is_ipv4(skb) && is_udp(skb);
}

// TODO: Support checking IPv6 packets.
static inline bool is_ipv4_udp_encap(const struct sk_buff *skb, enum packet_dir dir)
{
	struct udphdr *uh;

	if (!is_ipv4_udp(skb))
		return false;

	uh = udp_hdr(skb);
	return (dir == PACKET_IN) ? ntohs(uh->source) == UDP_ENCAP_PORT
		: ntohs(uh->dest) == UDP_ENCAP_PORT;
}

// TODO: Support checking IPv6 packets.
static inline bool is_ipv4_keep_alive(const struct sk_buff *skb)
{
	int udp_data_len;
	u8 *udp_data;

	if (!is_ipv4_udp(skb))
		return false;

	udp_data_len = skb->len - sizeof(struct iphdr) - sizeof(struct udphdr);
	udp_data = (u8 *)udp_hdr(skb) + sizeof(struct udphdr);
	return udp_data_len == 1 && udp_data[0] == 0xff;
}

// TODO: Support checking IPv6 packets.
static inline bool is_pkt_ipv4_keep_alive(const unsigned char *packet)
{
	const struct iphdr *ip_hdr = (struct iphdr *)packet;
	const unsigned char *udp_data;

	if (ip_hdr->version != 4 || ip_hdr->protocol != IPPROTO_UDP
		|| ip_hdr->tot_len != htons(IP_HEADER_SIZE + UDP_HEADER_SIZE + 1))
		return false;

	udp_data = packet + IP_HEADER_SIZE + UDP_HEADER_SIZE;
	return udp_data[0] == 0xff;
}

// TODO: Support checking IPv6 packets.
static inline bool is_ike_in_ipv4_udp_encap(const struct sk_buff *skb)
{
	int udp_data_len;
	u8 *udp_data;
	u32 *udp_data_32;

	if (!is_ipv4_udp(skb))
		return false;

	udp_data_len = skb->len - sizeof(struct iphdr) - sizeof(struct udphdr);
	udp_data = (u8 *)udp_hdr(skb) + sizeof(struct udphdr);
	udp_data_32 = (u32 *)udp_data;

	if (udp_data_len > sizeof(struct ip_esp_hdr) && udp_data_32[0] != 0) {
		// ESP in UDP packet.
		return false;
	}

	if (is_ipv4_keep_alive(skb))
		return false;

	return true;
}

static inline bool contains_ike_port(const struct udphdr *udp_hdr)
{
	return ntohs(udp_hdr->source) == UDP_ENCAP_PORT
		|| ntohs(udp_hdr->source) == IKE_PORT
		|| ntohs(udp_hdr->dest) == UDP_ENCAP_PORT
		|| ntohs(udp_hdr->dest) == IKE_PORT;
}

static inline bool is_pkt4_ike(const unsigned char *packet)
{
	const struct iphdr *ip_hdr = (struct iphdr *)packet;
	// 4 bytes for non-ESP marker.
	const int minimal_total_len = IP_HEADER_SIZE + UDP_HEADER_SIZE + 4;
	u32 *non_esp_marker;

	if (ip_hdr->version != 4 || ip_hdr->protocol != IPPROTO_UDP
		|| ip_hdr->tot_len < htons(minimal_total_len))
		return false;

	if (!contains_ike_port((struct udphdr *)(ip_hdr + 1)))
		return false;

	non_esp_marker = (u32 *)(packet + IP_HEADER_SIZE + UDP_HEADER_SIZE);

	return *non_esp_marker == 0;
}



static inline bool is_pkt6_ike(const unsigned char *packet)
{
	const struct ipv6hdr *ipv6_hdr = (struct ipv6hdr *)packet;
	// 4 bytes for non-ESP marker.
	const int minimal_payload_len = UDP_HEADER_SIZE + 4;
	u32 *non_esp_marker;

	if (ipv6_hdr->version != 6 || ipv6_hdr->nexthdr != IPPROTO_UDP
		|| ipv6_hdr->payload_len < htons(minimal_payload_len))
		return false;

	if (!contains_ike_port((struct udphdr *)(ipv6_hdr + 1)))
		return false;

	non_esp_marker = (u32 *)(packet + IP6_HEADER_SIZE + UDP_HEADER_SIZE);

	return *non_esp_marker == 0;
}

static inline bool is_pkt_ike(const unsigned char *packet)
{
	return is_pkt_ipv4(packet) ? is_pkt4_ike(packet) : is_pkt6_ike(packet);
}

static inline u32 get_pkt4_esp_spi(const unsigned char *packet)
{
	const struct iphdr *ip_hdr = (struct iphdr *)packet;
	const int minimal_tot_len = IP_HEADER_SIZE + UDP_HEADER_SIZE + sizeof(struct ip_esp_hdr);
	struct ip_esp_hdr *esp_hdr;

	// Check if the packet is a UDP packet with required payload for ESP header.
	if (ip_hdr->version != 4 || ip_hdr->protocol != IPPROTO_UDP
		|| ip_hdr->tot_len < htons(minimal_tot_len))
		return 0;

	// Check if the UDP packet is for UDP-encap.
	if (!contains_ike_port((struct udphdr *)(ip_hdr + 1)))
		return 0;

	esp_hdr = (struct ip_esp_hdr *)(packet + IP_HEADER_SIZE + UDP_HEADER_SIZE);
	return esp_hdr->spi;
}

static inline u32 get_pkt6_esp_spi(const unsigned char *packet)
{
	const struct ipv6hdr *ipv6_hdr = (struct ipv6hdr *)packet;
	struct ip_esp_hdr *esp_hdr;

	if (ipv6_hdr->version != 6)
		return 0;

	switch (ipv6_hdr->nexthdr) {
	case NEXTHDR_UDP:
		if (!contains_ike_port((struct udphdr *)(ipv6_hdr + 1)))
			return 0;

		if (ipv6_hdr->payload_len < htons(UDP_HEADER_SIZE + ESP_HEADER_SIZE))
			return 0;

		esp_hdr = (struct ip_esp_hdr *)(packet + IP6_HEADER_SIZE + UDP_HEADER_SIZE);
		break;
	case NEXTHDR_ESP:
		if (ipv6_hdr->payload_len < htons(ESP_HEADER_SIZE))
			return 0;

		esp_hdr = (struct ip_esp_hdr *)(packet + IP6_HEADER_SIZE);
		break;
	default:
		return 0;
	}

	return esp_hdr->spi;
}

// Return the SPI in network-byte order if the packet is an ESP packet.
// If the return value is 0, simply treat the packet as not an ESP packet, refer to
// RFC 4303 section 2.1.
static inline u32 get_pkt_esp_spi(const unsigned char *packet)
{
	return is_pkt_ipv4(packet) ? get_pkt4_esp_spi(packet) : get_pkt6_esp_spi(packet);
}

#endif  // NOA_IPSEC_UTILS_H_
