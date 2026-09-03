/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Router Advertisement packet parser
 *
 * Copyright 2025 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifndef RA_PACKET_H
#define RA_PACKET_H

#ifdef linux
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/types.h>
#else
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include "linux_port/types.h"
#include "linux_port/timer.h"
#endif

#define ETH_HEADER_LEN			14
#define IPV6_HEADER_OFFSET		ETH_HEADER_LEN
#define IPV6_HEADER_LEN			40
#define ICMP6_HEADER_OFFSET		(ETH_HEADER_LEN + IPV6_HEADER_LEN)
#define IPV6_ADDR_LEN			16
#define IPV6_DEST_ADDR_OFFSET		(ETH_HEADER_LEN + 24)
#define IPV6_FLOW_LABEL_OFFSET		(ETH_HEADER_LEN + 1)
#define IPV6_FLOW_LABEL_LEN		3
#define ICMP6_RA_HEADER_LEN		16
#define ICMP6_RA_CHECKSUM_OFFSET	(ETH_HEADER_LEN + IPV6_HEADER_LEN + 2)
#define ICMP6_RA_CHECKSUM_LEN		2
#define ICMP6_RA_OPTION_OFFSET		(ETH_HEADER_LEN + IPV6_HEADER_LEN + ICMP6_RA_HEADER_LEN)
#define ICMP6_RA_ROUTER_LIFETIME_OFFSET	(ETH_HEADER_LEN + IPV6_HEADER_LEN + 6)
#define ICMP6_RA_ROUTER_LIFETIME_LEN	2
#define ETH_ETHERTYPE_OFFSET		12
#define IPV6_NEXT_HEADER_OFFSET		(ETH_HEADER_LEN + 6)
#define ICMP6_TYPE_OFFSET		(ETH_HEADER_LEN + IPV6_HEADER_LEN)
#define ICMP6_4_BYTE_LIFETIME_OFFSET	4
#define ICMP6_4_BYTE_LIFETIME_LEN	4
#define ICMPV6_ROUTER_ADVERTISEMENT	134
#define ICMP6_SOURCE_LL_ADDRESS_OPTION_TYPE		1
#define ICMP6_PREFIX_OPTION_TYPE			3
#define ICMP6_MTU_OPTION_TYPE				5
#define ICMP6_ROUTE_INFO_OPTION_TYPE			24
#define ICMP6_RDNSS_OPTION_TYPE				25
#define ICMP6_RA_FLAGS_EXTENSION_OPTION_TYPE		26
#define ICMP6_DNSSL_OPTION_TYPE				31
#define ICMP6_CAPTIVE_PORTAL_OPTION_TYPE		37
#define ICMP6_PREF64_OPTION_TYPE			38
#define ICMP6_PREFIX_OPTION_VALID_LIFETIME_OFFSET	4
#define ICMP6_PREFIX_OPTION_VALID_LIFETIME_LEN		4
#define ICMP6_PREFIX_OPTION_PREFERRED_LIFETIME_LEN	4

#define MAX_PACKET_SECTIONS		30
// Theoretically, the maximum size for an RA packet is constrained by the IPv6 minimum MTU of 1280
// bytes, but in practice, it is much smaller (less than 200 bytes).
#define MAX_RA_PACKET_SIZE		256
#define MAX_RA_ROUTER_LIFETIME		9000

#define RA_PACKET_FLAG_SOLICITED	0x1

typedef enum section_type {
	SECTION_TYPE_IGNORE,
	SECTION_TYPE_MATCH,
	SECTION_TYPE_LIFETIME,
} section_type_t;

typedef struct packet_section {
	section_type_t type;
	uint32_t start;
	uint32_t length;
	uint32_t lifetime;
} __attribute__((packed, aligned(4))) packet_section_t;

typedef struct ra_packet {
	int num_sections;
	int router_lifetime_idx;
	int pio_valid_lifetime_idx;
	int pio_preferred_lifetime_idx;
	int rdnss_lifetime_idx;
	int rio_route_lifetime_idx;
	int mtu_option_idx;
	int slla_option_idx;
	uint32_t router_lifetime;
	uint32_t min_pio_valid_lifetime;
	uint32_t min_rio_route_lifetime;
	uint32_t min_rdnss_lifetime;
	uint32_t packet_length;
	uint32_t flags;
	packet_section_t packet_sections[MAX_PACKET_SECTIONS];
	uint8_t packet_buffer[MAX_RA_PACKET_SIZE];
	uint8_t proxied_packet[MAX_RA_PACKET_SIZE];
} __attribute__((packed, aligned(4))) ra_packet_t;

int parse_ra_packet(const uint8_t *packet, size_t packet_len, ra_packet_t *ra_packet);
int generate_proxied_packet(ra_packet_t *ra_packet);
int send_rs_packet(const int ifindex);
bool is_same_ra(const ra_packet_t *ra1, const ra_packet_t *ra2);

#endif // RA_PACKET_H
