/*
 * Router Advertisement Packet Parser
 *
 * Copyright 2025 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/icmpv6.h>
#include <linux/etherdevice.h>
#include <net/addrconf.h>
#include <net/ipv6.h>
#include <net/ndisc.h>
#include "checksum.h"
#include "ra_packet.h"
#else
#include "linux_port/types.h"
#include "net/icmpv6.h"
#include "net/ipv6.h"
#include "net/if_ether.h"
#include "net/nep_helpers.h"
#include "net/ra_packet.h"
#include "checksum.h"
#include "in6.h"
#endif

#ifdef linux
#define FALLTHROUGH fallthrough;
#else
#define FALLTHROUGH [[fallthrough]];
#endif

// Define the all-hosts multicast address (FF02::1).
struct in6_addr all_hosts_addr = {
	{
		{
			0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
		}
	}
};

static uint8_t get_uint8(const uint8_t *packet, int position) {
	return packet[position];
}

static uint16_t get_uint16(const uint8_t *packet, int position) {
	return ntohs(*((const uint16_t*)(packet + position)));
}

static uint32_t get_uint32(const uint8_t *packet, int position) {
	uint32_t value;
	memcpy(&value, packet + position, sizeof(uint32_t));
	return ntohl(value);
}

static uint32_t get_min_for_positive_uint32(uint32_t old_min, uint32_t value) {
	if (value == 0) {
		return old_min;
	}
	return (old_min < value) ? old_min : value;
}

static void add_section(ra_packet_t *ra_packet, section_type_t type, uint32_t start,
			uint32_t length, uint32_t lifetime) {
	packet_section_t *section;
	if (ra_packet->num_sections >= MAX_PACKET_SECTIONS) {
		return;
	}
	section = &ra_packet->packet_sections[ra_packet->num_sections];
	section->type = type;
	section->start = start;
	section->length = length;
	section->lifetime = lifetime;
	ra_packet->num_sections++;
}

static void add_lifetime_section(ra_packet_t *ra_packet, uint32_t *start, uint32_t length,
				 uint64_t value) {
	add_section(ra_packet, SECTION_TYPE_LIFETIME, *start, length, value);
	*start += length;
}

static void add_match_section(ra_packet_t *ra_packet, uint32_t *start, uint32_t length) {
	add_section(ra_packet, SECTION_TYPE_MATCH, *start, length, 0);
	*start += length;
}

static void add_match_until(ra_packet_t *ra_packet, uint32_t *start, uint32_t end) {
	add_match_section(ra_packet, start, end - *start);
}

static void add_ignore_section(ra_packet_t *ra_packet, uint32_t *start, uint32_t length) {
	add_section(ra_packet, SECTION_TYPE_IGNORE, *start, length, 0);
	*start += length;
}

static long add_4byte_lifetime_option(ra_packet_t *ra_packet, uint32_t *start, int option_length,
				      bool is_rdnss) {
	uint32_t lifetime;
	if (is_rdnss) {
		add_match_section(ra_packet, start, ICMP6_4_BYTE_LIFETIME_OFFSET - 2);
		add_ignore_section(ra_packet, start, 2);  // reserved, but observed non-zero
		ra_packet->rdnss_lifetime_idx = ra_packet->num_sections;
	} else {
		add_match_section(ra_packet, start, ICMP6_4_BYTE_LIFETIME_OFFSET);
		ra_packet->rio_route_lifetime_idx = ra_packet->num_sections;
	}
	lifetime = get_uint32(ra_packet->packet_buffer, *start);
	add_lifetime_section(ra_packet, start, ICMP6_4_BYTE_LIFETIME_LEN, lifetime);
	add_match_section(ra_packet, start, option_length - ICMP6_4_BYTE_LIFETIME_OFFSET
			  - ICMP6_4_BYTE_LIFETIME_LEN);
	return lifetime;
}

#ifdef linux
static __sum16 calculate_icmpv6_checksum(const struct ipv6hdr *ip6h,
				  const unsigned char *payload,
				  int payload_len) {
	return csum_ipv6_magic(&ip6h->saddr, // Source IPv6 address
			       &ip6h->daddr, // Destination IPv6 address
			       payload_len,  // Length of the ICMPv6 message
			       IPPROTO_ICMPV6, // Protocol (ICMPv6)
			       csum_partial(payload, payload_len, 0)); // Payload's partial checksum
}
#endif

/**
 * @brief Parses a Router Advertisement packet from raw byte data.
 *
 * This function takes raw byte data of a Router Advertisement packet and parses
 * it according to RFC 4861. It extracts the ICMPv6 header, Router Advertisement
 * message fields, and any included options, storing the parsed information in
 * an 'ra_packet_t' structure.
 *
 * @param[in]   packet      Pointer to the raw packet data (uint8_t array).
 * @param[in]   packet_len  Length of the packet data in bytes.
 * @param[out]  ra_packet   Pointer to an 'ra_packet_t' structure to store the parsed
 *                          information upon success.
 * @return      The result of the operation.
 * @retval 0    Success: the packet was successfully parsed.
 * @retval -1   Failure (e.g., not an RA packet, invalid packet).
 */
int parse_ra_packet(const uint8_t *packet, size_t packet_len, ra_packet_t *ra_packet) {
	uint32_t position = 0;
	struct ipv6hdr *ip6h;

	if (packet_len < ICMP6_RA_OPTION_OFFSET || packet_len > MAX_RA_PACKET_SIZE) {
		return -1;
	}

	// Check whether it is an RA packet.
	if (get_uint16(packet, ETH_ETHERTYPE_OFFSET) != ETH_P_IPV6 ||
	    get_uint8(packet, IPV6_NEXT_HEADER_OFFSET) != IPPROTO_ICMPV6 ||
	    get_uint8(packet, ICMP6_TYPE_OFFSET) != ICMPV6_ROUTER_ADVERTISEMENT) {
		return -1;
	}

	memset(ra_packet, 0, sizeof(ra_packet_t));
	memcpy(ra_packet->packet_buffer, packet, packet_len);
	ra_packet->packet_length = packet_len;
	ra_packet->pio_valid_lifetime_idx = -1;
	ra_packet->pio_preferred_lifetime_idx = -1;
	ra_packet->rdnss_lifetime_idx = -1;
	ra_packet->rio_route_lifetime_idx = -1;
	ra_packet->mtu_option_idx = -1;
	ra_packet->slla_option_idx = -1;
	ra_packet->num_sections = 0;

	ip6h = (struct ipv6hdr *) (ra_packet->packet_buffer + IPV6_HEADER_OFFSET);
	if (memcmp(&ip6h->daddr, &all_hosts_addr, sizeof(struct in6_addr)) != 0) {
		ra_packet->flags |= RA_PACKET_FLAG_SOLICITED;
	}

	// Ignore destination MAC address.
	add_ignore_section(ra_packet, &position, 6);

	// Ignore the flow label and low 4 bits of traffic class.
	add_match_until(ra_packet, &position, IPV6_FLOW_LABEL_OFFSET);
	add_ignore_section(ra_packet, &position, IPV6_FLOW_LABEL_LEN);

	// Ignore IPv6 destination address.
	add_match_until(ra_packet, &position, IPV6_DEST_ADDR_OFFSET);
	add_ignore_section(ra_packet, &position, IPV6_ADDR_LEN);

	// Ignore checksum.
	add_match_until(ra_packet, &position, ICMP6_RA_CHECKSUM_OFFSET);
	add_ignore_section(ra_packet, &position, ICMP6_RA_CHECKSUM_LEN);

	// Parse router lifetime
	add_match_until(ra_packet, &position, ICMP6_RA_ROUTER_LIFETIME_OFFSET);
	ra_packet->router_lifetime = get_uint16(packet, ICMP6_RA_ROUTER_LIFETIME_OFFSET);
	ra_packet->router_lifetime_idx = ra_packet->num_sections;
	add_lifetime_section(ra_packet, &position, ICMP6_RA_ROUTER_LIFETIME_LEN,
			     ra_packet->router_lifetime);

	// Add remaining fields (reachable time and retransmission timer) to match section.
	add_match_until(ra_packet, &position, ICMP6_RA_OPTION_OFFSET);

	ra_packet->min_pio_valid_lifetime = 0xffffffff;
	ra_packet->min_rio_route_lifetime = 0xffffffff;
	ra_packet->min_rdnss_lifetime = 0xffffffff;

	while (position < packet_len) {
		const uint32_t tmp_position = position;
		uint8_t option_type;
		uint16_t option_length;
		uint32_t lifetime;

		// The minimum option length is 8 bytes.
		if (position + 8 > packet_len) {
			return -1;
		}

		option_type = get_uint8(packet, tmp_position);
		option_length = get_uint8(packet, tmp_position + 1) * 8;

		// Check if the option length is valid and the option fits in the packet.
		if (option_length <= 0 || tmp_position + option_length > packet_len) {
			return -1;
		}

		switch (option_type) {
		case ICMP6_PREFIX_OPTION_TYPE:
			// Parse valid lifetime
			add_match_section(ra_packet, &position,
					  ICMP6_PREFIX_OPTION_VALID_LIFETIME_OFFSET);
			ra_packet->pio_valid_lifetime_idx = ra_packet->num_sections;
			lifetime = get_uint32(packet, position);
			add_lifetime_section(ra_packet, &position,
					     ICMP6_PREFIX_OPTION_VALID_LIFETIME_LEN, lifetime);
			ra_packet->min_pio_valid_lifetime = get_min_for_positive_uint32(
					ra_packet->min_pio_valid_lifetime, lifetime);

			// Parse preferred lifetime
			ra_packet->pio_preferred_lifetime_idx = ra_packet->num_sections;
			lifetime = get_uint32(packet, position);
			add_lifetime_section(ra_packet, &position,
					     ICMP6_PREFIX_OPTION_PREFERRED_LIFETIME_LEN, lifetime);

			add_match_section(ra_packet, &position, 4);       // Reserved bytes
			add_match_section(ra_packet, &position, IPV6_ADDR_LEN);  // The prefix itself
			break;
		// These two options have the same lifetime offset and size, and
		// are processed with the same specialized add4ByteLifetimeOption:
		case ICMP6_RDNSS_OPTION_TYPE:
			lifetime = add_4byte_lifetime_option(ra_packet, &position, option_length, true);
			ra_packet->min_rdnss_lifetime = get_min_for_positive_uint32(
					ra_packet->min_rdnss_lifetime, lifetime);
			break;
		case ICMP6_ROUTE_INFO_OPTION_TYPE:
			lifetime = add_4byte_lifetime_option(ra_packet, &position, option_length, false);
			ra_packet->min_rio_route_lifetime = get_min_for_positive_uint32(
					ra_packet->min_rio_route_lifetime, lifetime);
			break;
		case ICMP6_SOURCE_LL_ADDRESS_OPTION_TYPE:
			ra_packet->slla_option_idx = ra_packet->num_sections;
			FALLTHROUGH
		case ICMP6_MTU_OPTION_TYPE:
			ra_packet->mtu_option_idx = ra_packet->num_sections;
			FALLTHROUGH
		case ICMP6_PREF64_OPTION_TYPE:
			FALLTHROUGH
		case ICMP6_RA_FLAGS_EXTENSION_OPTION_TYPE:
			add_match_section(ra_packet, &position, option_length);
			break;
		case ICMP6_CAPTIVE_PORTAL_OPTION_TYPE: // unlikely to ever change.
			FALLTHROUGH
		case ICMP6_DNSSL_OPTION_TYPE: // currently unsupported in userspace.
			FALLTHROUGH
		default:
			// RFC4861 section 4.2 dictates we ignore unknown options for forwards
			// compatibility.
			// However, make sure the option's type and length match.
			add_match_section(ra_packet, &position, 2); // option type & length
			// optionLength is guaranteed to be >= 8.
			add_ignore_section(ra_packet, &position, option_length - 2);
			break;
		}
	}

	return 0;
}

/**
 * @brief Generates a new Router Advertisement packet with extended lifetimes.
 *
 * This function takes a 'ra_packet_t' and generates a new Router Advertisement
 * packet that is identical except that the lifetime fields are all extended to
 * their maximum values. The generated packet is stored in the 'proxied_packet'
 * field. The MTU option is also moved to the end of the options.
 *
 * @param ra_packet Pointer to the 'ra_packet_t' structure.
 * @return Returns 0 on success, and -1 on failure.
 */
int generate_proxied_packet(ra_packet_t *ra_packet) {
	int i;
	struct icmp6hdr *icmp6h;

	memcpy(ra_packet->proxied_packet, ra_packet->packet_buffer, ra_packet->packet_length);
	icmp6h = (struct icmp6hdr *) (ra_packet->proxied_packet + ICMP6_HEADER_OFFSET);
	// Generate a new RA packet with each lifetimes set its maximum value
	for (i = 0; i < ra_packet->num_sections; ++i) {
		packet_section_t *section = &ra_packet->packet_sections[i];
		if (section->type == SECTION_TYPE_LIFETIME) {
			if (section->length == 2) {
				__u16 old_value = *((__u16 *)(ra_packet->packet_buffer + section->start));
				__u16 new_value = htons(MAX_RA_ROUTER_LIFETIME);
				*((__u16 *)(ra_packet->proxied_packet + section->start)) = new_value;
				// Update the ICMPv6 checksum
				icmp6h->icmp6_cksum = nep_csum16_update(
						icmp6h->icmp6_cksum, old_value, new_value);
			} else {
				__u32 old_value;
				__u32 new_value = htonl(MAX_RA_ROUTER_LIFETIME);
				memcpy(&old_value, ra_packet->packet_buffer + section->start, sizeof(__u32));
				memcpy(ra_packet->proxied_packet + section->start, &new_value, sizeof(__u32));
				// Update the ICMPv6 checksum
				icmp6h->icmp6_cksum = nep_csum32_update(
						icmp6h->icmp6_cksum, old_value, new_value);
			}
		}
	}
	// Move MTU option to the end
	if (ra_packet->mtu_option_idx >= 0) {
		packet_section_t *section = &ra_packet->packet_sections[ra_packet->mtu_option_idx];
		int start = section->start;
		int length = section->length;
		uint8_t *buffer = ra_packet->proxied_packet;
		// Move options follow mtu option to the front
		memmove(buffer + start, buffer + start + length, ra_packet->packet_length - (start + length));
		// Copy the MTU option from the original packet buffer
		memcpy(buffer + ra_packet->packet_length - length, ra_packet->packet_buffer + start, length);
	}
	return 0;
}

#ifdef linux
static int generate_eui64_addr(struct net_device *dev, uint8_t *eui) {
	if (dev->addr_len != ETH_ALEN) {
		return -1;
	}
	memset(eui, 0, 16);
	eui[0] = 0xfe;
	eui[1] = 0x80;
	memcpy(eui + 8, dev->dev_addr, 3);
	eui[11] = 0xFF;
	eui[12] = 0xFE;
	memcpy(eui + 13, dev->dev_addr + 3, 3);
	if (dev->dev_id) {
		eui[11] = (dev->dev_id >> 8) & 0xFF;
		eui[12] = dev->dev_id & 0xFF;
	} else {
		eui[8] ^= 2;
	}
	return 0;
}
#endif

/**
 * @brief Sends Router Solicitation Packet from the interface.
 *
 * This function generates and sends a Router Solicitation Packet from the
 * specified interface.
 *
 * @param ifindex Interface index
 * @return Returns 0 on success, and -1 on failure.
 */
int send_rs_packet(const int ifindex __attribute__((__unused__))) {
#ifdef linux
	struct net_device *dev;
	struct sk_buff *skb;
	struct ethhdr *eth;
	struct ipv6hdr *ip6h;
	struct icmp6hdr *icmp6h;
	int option_len;
	int headers_len;
	static unsigned char dst_mac[] = {0x33, 0x33, 0x00, 0x00, 0x00, 0x02};
	static unsigned char src_mac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	int ret;

	dev = dev_get_by_index(&init_net, ifindex);
	if (!dev) {
		return -1;
	}
	memcpy(src_mac, dev->dev_addr, ETH_ALEN);

	option_len = 8; // Source Link-Layer Address Option
	headers_len = sizeof(struct ethhdr) + sizeof(struct ipv6hdr) + sizeof(struct icmp6hdr);
	skb = dev_alloc_skb(headers_len + option_len);
	if (!skb) {
		dev_put(dev);
		return -1;
	}

	skb_reserve(skb, headers_len);

	// Add Source Link-Layer Address Option
	__ndisc_fill_addr_option(skb, ND_OPT_SOURCE_LL_ADDR, src_mac, ETH_ALEN,
				 ndisc_addr_option_pad(dev->type));

	icmp6h = (struct icmp6hdr *)skb_push(skb, sizeof(struct icmp6hdr));
	icmp6h->icmp6_type = NDISC_ROUTER_SOLICITATION;
	icmp6h->icmp6_code = 0;
	icmp6h->icmp6_cksum = 0;
	icmp6h->icmp6_unused = 0;

	ip6h = (struct ipv6hdr *)skb_push(skb, sizeof(struct ipv6hdr));
	ip6h->version = 6;
	ip6h->priority = 0;
	memset(&(ip6h->flow_lbl), 0, sizeof(ip6h->flow_lbl));
	ip6h->payload_len = htons(sizeof(struct icmp6hdr) + option_len);
	ip6h->nexthdr = IPPROTO_ICMPV6;
	ip6h->hop_limit = 255;
	ipv6_addr_set(&ip6h->saddr, htonl(0xfe800000), 0, 0, htonl(0x1)); // Link-local address
	ipv6_addr_set(&ip6h->daddr, htonl(0xff020000), 0, 0, htonl(0x2)); // All-nodes mcast address

	// Use the EUI-64 link-local address on the interface.
	if (generate_eui64_addr(dev, (uint8_t*)&ip6h->saddr) != 0) {
		kfree_skb(skb);
		dev_put(dev);
		return -1;
	}

	// Calculate the ICMPv6 checksum
	icmp6h->icmp6_cksum = calculate_icmpv6_checksum(ip6h, (unsigned char *)icmp6h,
							sizeof(struct icmp6hdr) + option_len);

	// Fill in Ethernet Header
	eth = (struct ethhdr*)skb_push(skb, sizeof(struct ethhdr));
	ether_addr_copy(eth->h_dest, dst_mac);
	ether_addr_copy(eth->h_source, src_mac);
	eth->h_proto = htons(ETH_P_IPV6);

	// Set the device for the sk_buff
	skb->dev = dev;
	skb->pkt_type = PACKET_OUTGOING;
	skb->protocol = eth->h_proto;
	skb->ip_summed = CHECKSUM_NONE;
	skb->csum = 0;

	ret = dev_queue_xmit(skb);
	if (ret != NET_XMIT_SUCCESS && ret != NET_XMIT_CN) {
		kfree_skb(skb);
	}
	dev_put(dev);

	return (ret == NET_XMIT_SUCCESS || ret == NET_XMIT_CN) ? 0 : -1;
#else
	return 0;
#endif
}

/**
 * @brief Compares whether two RAs are identical.
 *
 * This function takes two 'ra_packet_t' pointers and compares whether they are identical.
 * The definition of 'identical' means all sections with SECTION_TYPE_MATCH must be
 * identical.
 *
 * @param ra1 Pointer to the first 'ra_packet_t' structure.
 * @param ra2 Pointer to the second 'ra_packet_t' structure.
 * @return Returns true if the two RAs are identical, and false otherwise.
 */
bool is_same_ra(const ra_packet_t *ra1, const ra_packet_t *ra2) {
	int i;

	if (ra1->packet_length != ra2->packet_length ||
	    ra1->num_sections != ra2->num_sections) {
		return false;
	}

	for (i = 0; i < ra1->num_sections; ++i) {
		if (ra1->packet_sections[i].start != ra2->packet_sections[i].start ||
		    ra1->packet_sections[i].length != ra2->packet_sections[i].length ||
		    ra1->packet_sections[i].type != ra2->packet_sections[i].type) {
			return false;
		}
		if (ra1->packet_sections[i].type != SECTION_TYPE_MATCH) {
			continue;
		}
		if (memcmp(ra1->packet_buffer + ra1->packet_sections[i].start,
			   ra2->packet_buffer + ra1->packet_sections[i].start,
			   ra1->packet_sections[i].length) != 0) {
			return false;
		}
	}

	return true;
}
