/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common shared offload map structure
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Mark Chien <markchien@google.com>
 */
#ifndef MAP_DEF_H
#define MAP_DEF_H

#ifdef linux
#include <linux/if.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#else
#include <type_traits>

#include "if_ether.h"
#include "in6.h"
#include "net/in.h"
#endif

#define SIZE_BE16	2
#define SIZE_UINT8 1
#define SIZE_UINT16 2
#define SIZE_UINT32 4
#define SIZE_UINT64 8
#define SIZE_MAC_ADDR 6
#define SIZE_ETH_HDR 14
#define SIZE_IPV4_ADDR 4
#define SIZE_IPV6_ADDR 16

#define NOA_CALLBACK_DATA_LEN 88

// If changed, please update //network_offload/noa_def.h
#define CMD_PUT_UPSTREAM_4MAP              0
#define CMD_PUT_DOWNSTREAM_4MAP            1
#define CMD_REMOVE_UPSTREAM_4MAP           2
#define CMD_REMOVE_DOWNSTREAM_4MAP         3
#define CMD_PUT_UPSTREAM_6MAP              4
#define CMD_REMOVE_UPSTREAM_6MAP           5
#define CMD_PUT_DOWNSTREAM_6MAP            6
#define CMD_REMOVE_DOWNSTREAM_6MAP         7
#define CMD_NO_USED_8                      8
#define CMD_NO_USED_9                      9
#define CMD_NO_USED_10                     10
#define CMD_NO_USED_11                     11
#define CMD_NO_USED_12                     12
#define CMD_NO_USED_13                     13
#define CMD_SET_CONFIG                     14
#define CMD_REMOVE_CONFIG                  15
#define CMD_GET_STATS                      16
#define CMD_CALLBACK_EXTEND_TIMEOUT        17
#define CMD_CALLBACK_LIMIT_REACH           18
#define CMD_TETHERING_TABLE_SYNC           19
#define CMD_SET_APF_PROGRAM                20
#define CMD_PUT_FLOWID_MAP                 21
#define CMD_REMOVE_FLOWID_MAP              22
#define CMD_PUT_DSCP2UP_MAP                23
#define CMD_STA_CONNECT                    24
#define CMD_STA_DISCONNECT                 25
#define CMD_NO_USED_26                     26
#define CMD_CALLBACK_BASE_ADDR             27
#define CMD_CALLBACK_SEND_RA_TO_KERNEL     28
#define CMD_REQUEST_BASE_ADDR              29
#define CMD_VPN_ADD_SA                     101
#define CMD_VPN_DEL_SA                     102
#define CMD_VPN_FREE_SA                    103
#define CMD_PUT_NETLINK_CONF               201

// explicitly pad all relevant structures and assert that their size
// is the sum of the sizes of their fields.
#define STRUCT_SIZE(name, size) \
  static_assert(sizeof(name) == (size), "Incorrect struct size.")

typedef struct {
  uint64_t rxPackets;
  uint64_t rxBytes;
  uint64_t rxErrors;
  uint64_t txPackets;
  uint64_t txBytes;
  uint64_t txErrors;
} TetherStatsValue;

STRUCT_SIZE(TetherStatsValue, 6 * SIZE_UINT64);  // 48

// For now tethering offload only needs to support downstreams that use 6-byte MAC addresses,
// because all downstream types that are currently supported (WiFi, USB, Bluetooth and
// Ethernet) have 6-byte MAC addresses.

typedef struct {
  uint32_t iif;  // The input interface index
  uint8_t
      dstMac[ETH_ALEN];  // destination ethernet mac address (zeroed iff rawip)
  uint8_t zero[2];       // zero pad for 8 byte alignment
  struct in6_addr neigh6;  // The destination IPv6 address
} TetherDownstream6Key;

STRUCT_SIZE(TetherDownstream6Key,
            SIZE_UINT32 + SIZE_MAC_ADDR + 2 + SIZE_IPV6_ADDR);  // 28

typedef struct {
  uint32_t oif;  // The output interface to redirect to
  struct ethhdr
      macHeader;  // includes dst/src mac and ethertype (zeroed iff rawip)
  uint16_t pmtu;  // The maximum L3 output path/route mtu
} Tether6Value;

STRUCT_SIZE(Tether6Value, SIZE_UINT32 + SIZE_ETH_HDR + SIZE_UINT16);  // 20

typedef struct {
  uint32_t iif;  // The input interface index
  uint8_t
      dstMac[ETH_ALEN];  // destination ethernet mac address (zeroed iff rawip)
  uint8_t zero[6];       // zero pad for 8 byte alignment
  uint64_t src64;        // Top 64-bits of the src ip
} TetherUpstream6Key;

STRUCT_SIZE(TetherUpstream6Key,
            SIZE_UINT32 + SIZE_MAC_ADDR + 6 + SIZE_UINT64);  // 24

typedef struct {
  uint16_t l4Proto;      // IPPROTO_TCP/UDP/...
  uint8_t zero[2];       // zero pad for 8 byte alignment
  struct in_addr src4;	 // source &
  struct in_addr dst4;	 // destination IPv4 addresses
  uint16_t srcPort;      // source &
  uint16_t dstPort;      // destination TCP/UDP/... ports
} Tether4Key;

STRUCT_SIZE(Tether4Key, SIZE_UINT16 + 2 + 2 * SIZE_IPV4_ADDR + 2 * SIZE_UINT16); // 16

typedef struct {
  uint8_t dstMac[ETH_ALEN]; // destination ethernet mac address
  uint8_t zero[2];          // zero pad for 8 byte alignment
  struct in6_addr mangle46; // to be mangled (always IPv4 mapped for downstream & maybe
                            // IPv4 mapped or IPv6 for upstream)
  uint16_t manglePort;
  uint8_t zero2[6];         // zero pad for 8 byte alignment
  uint64_t last_used;       // Kernel updates on each use with bpf_ktime_get_boot_ns()
} Tether4Value;

STRUCT_SIZE(Tether4Value, SIZE_MAC_ADDR + 2 + SIZE_IPV6_ADDR
            + SIZE_BE16 + 6 + SIZE_UINT64); // 40

typedef struct __attribute__((__packed__)) {
  uint8_t valid;
  uint16_t next_link;
  uint8_t unused;
} NetengineTether4Header;
STRUCT_SIZE(NetengineTether4Header, 4);

typedef struct __attribute__((__packed__)) {
  uint8_t source_device_port;
  uint16_t protocol;
  uint8_t source_ip_address[SIZE_IPV4_ADDR];
  uint8_t destination_ip_address[SIZE_IPV4_ADDR];
  uint16_t source_port;
  uint16_t destination_port;
} NetengineTether4Key;
STRUCT_SIZE(NetengineTether4Key, 15);

typedef struct __attribute__((__packed__)) {
  uint8_t destination_device_port;
  uint8_t source_mac_address[ETH_ALEN];
  uint8_t destination_mac_address[ETH_ALEN];
  uint16_t ethertype;
  uint16_t pmtu;
  uint8_t source_ip_address[SIZE_IPV4_ADDR];
  uint8_t destination_ip_address[SIZE_IPV4_ADDR];
  uint16_t source_port;
  uint16_t destination_port;
  uint8_t unused[16];
} NetengineTether4Value;
STRUCT_SIZE(NetengineTether4Value, 45);

typedef struct {
  uint8_t dstMac[ETH_ALEN];  // destination ethernet mac address
  uint8_t zero[2];           // zero pad for 4 byte alignment
  uint32_t priority;         // priority
} TetherFlowIdKey;

STRUCT_SIZE(TetherFlowIdKey, SIZE_MAC_ADDR + 2 + SIZE_UINT32);  // 12

typedef struct {
  uint32_t type;
  uint8_t data[NOA_CALLBACK_DATA_LEN];
} NoaCallbackCommand;
STRUCT_SIZE(NoaCallbackCommand, SIZE_UINT32 + NOA_CALLBACK_DATA_LEN);  // 92

typedef uint32_t TetherFlowIdValue;  // flow id value

typedef struct {
  uint32_t iif;
  struct in6_addr pfx96;
  struct in6_addr local6;
} ClatIngress6Key;
STRUCT_SIZE(ClatIngress6Key, SIZE_UINT32 + 2 * SIZE_IPV6_ADDR); // 36

typedef struct {
  struct in_addr local4;
} ClatIngress6Value;
STRUCT_SIZE(ClatIngress6Value, SIZE_IPV4_ADDR); // 4

typedef struct {
  uint32_t iif;
  struct in_addr local4;
} ClatEgress4Key;
STRUCT_SIZE(ClatEgress4Key, SIZE_UINT32 + SIZE_IPV4_ADDR); // 8

typedef struct {
  struct in6_addr local6;
  struct in6_addr pfx96;
} ClatEgress4Value;
STRUCT_SIZE(ClatEgress4Value, 2 * SIZE_IPV6_ADDR); // 32

typedef struct __attribute__((__packed__)) {
  uint32_t ifindex;  // wifi interface index
  uint16_t type;  // netlink message type
  uint16_t state;  // wifi operation state or arp cache state
  uint8_t mac[ETH_ALEN];  // mac address
  uint8_t family;  // ip address family
  uint8_t prefixlen;  // ip mask prefix length
  union {
    struct in_addr ip4addr;  // ipv4 address
    struct in6_addr ip6addr;  // ipv6 address
  } addr;
} NetlinkConfig;

STRUCT_SIZE(NetlinkConfig, SIZE_UINT32 + 2 * SIZE_UINT16 + SIZE_MAC_ADDR
            + 2 * SIZE_UINT8 + SIZE_IPV6_ADDR); // 32

typedef struct {
  int32_t upstream;
  uint8_t zero[4];
  TetherStatsValue value;
} TetherStats;

typedef struct {
  Tether4Key key;
  Tether4Value value;
} Tether4Entry;

typedef union {
  uint8_t raw[64];
  struct __attribute__((__packed__)) {
    NetengineTether4Header header;
    NetengineTether4Key key;
    NetengineTether4Value value;
  };
} NetengineTether4Entry;
STRUCT_SIZE(NetengineTether4Entry, 64);
typedef struct {
  TetherUpstream6Key key;
  uint8_t zero[4];
  Tether6Value value;
} TetherUpstream6Entry;

typedef struct {
  TetherDownstream6Key key;
  Tether6Value value;
} TetherDownstream6Entry;

typedef struct {
  TetherFlowIdKey key;
  TetherFlowIdValue value;
} TetherFlowIdEntry;

typedef struct {
  ClatIngress6Key key;
  ClatIngress6Value value;
} ClatIngress6Entry;

typedef struct {
  ClatEgress4Key key;
  ClatEgress4Value value;
} ClatEgress4Entry;

typedef struct {
  uint32_t upstreamIif;
  uint32_t downstreamIif;
  uint8_t upstream_if_Mac[ETH_ALEN];
  uint8_t zero[2];
  uint8_t downstream_if_Mac[ETH_ALEN];
  uint16_t pmtu;
  uint64_t limit;     // in bytes
} TetherConfig;
STRUCT_SIZE(TetherConfig,
            2 * SIZE_UINT32 + SIZE_MAC_ADDR + 2 + SIZE_MAC_ADDR + SIZE_UINT16 + SIZE_UINT64);  // 40

#undef STRUCT_SIZE
#endif //MAP_DEF_H
