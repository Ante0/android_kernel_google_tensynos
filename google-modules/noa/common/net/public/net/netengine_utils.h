/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header of NetEngine Utility
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Mark Chien <markchien@google.com>, KH Shi <kenghua@google.com>
 */
#ifndef NOA_NET_NETENGINE_UTILS_H
#define NOA_NET_NETENGINE_UTILS_H

#ifdef linux
#include <linux/kernel.h>
#include <linux/types.h>
#else /* linux */
#include <cstdint>
#endif /* linux */

#include "common/core.h"

#ifdef NOA_IS_CI_TESTING_FIRMWARE

/*
 * NOA_FLOW_TABLE_LOOKUP_BYPASS:
 * All the packets should bypass the standard
 * Tethering Flow Table lookup mechanism.
 */
#define NOA_FLOW_TABLE_LOOKUP_BYPASS

#else /* NOA_IS_CI_TESTING_FIRMWARE */

/*
 * NOA_WLAN_REQUIRES_LLC_HEADER:
 * Packets transmitted via the NOA WLAN data path
 * MUST be prefixed with an 802.2 Logical Link Control (LLC) header
 */
#define NOA_WLAN_REQUIRES_LLC_HEADER

/*
 * NOA_TCP_CONTROL_NO_FORWARDING_SEARCH:
 * Ensures that TCP SYN packets bypass the main tethering
 * forwarding table lookup because they are guaranteed to use
 * the fallback route. This is an optimization to save CPU cycles.
 */
#define NOA_TCP_CONTROL_NO_FORWARDING_SEARCH
#endif /* NOA_IS_CI_TESTING_FIRMWARE */

#define DOT11_LLC_SNAP_HDR_LEN 8
#define DOT11_OUI_LEN 3

// 62 (14B Ether + 8B LLC SNAP + 20B IPv4 + 20 L3 Hdr) + alignment = 64
#define WIFI_HEADER_ALI_LEN (64U)
// 40 (20B IPv4  + 20B L3 Hdr) + alignment = 48
#define MODEM_HEADER_ALI_LEN (48U)

#ifdef NOA_WLAN_REQUIRES_LLC_HEADER
#define REQUIRES_LLC_HEADER_FLAG (1)
#define MODEM_RESERVE_HEADROOM_SIZE (DOT11_LLC_SNAP_HDR_LEN + ETH_HLEN)
#define WIFI_RESERVE_HEADROOM_SIZE (DOT11_LLC_SNAP_HDR_LEN)
#else /* NOA_WLAN_REQUIRES_LLC_HEADER */
#define REQUIRES_LLC_HEADER_FLAG (0)
#define MODEM_RESERVE_HEADROOM_SIZE (ETH_HLEN)
#define WIFI_RESERVE_HEADROOM_SIZE (0U)
#endif /* NOA_WLAN_REQUIRES_LLC_HEADER */

#define MODEM_TO_WIFI_OFFSET (WIFI_HEADER_ALI_LEN - MODEM_RESERVE_HEADROOM_SIZE)
#define WIFI_TO_MODEM_OFFSET (ETH_HLEN + MODEM_HEADER_ALI_LEN)
#define WIFI_TO_WIFI_OFFSET (WIFI_HEADER_ALI_LEN - WIFI_RESERVE_HEADROOM_SIZE)

#define NIP6_FMT "%04" PRIx32 ":%04" PRIx32 ":%04" PRIx32 ":%04" PRIx32 ":%04" PRIx32 ":%04" PRIx32 ":%04" PRIx32 ":%04" PRIx32 ""
#define NIP6(addr) \
	ntohs((addr).s6_addr16[0]), \
	ntohs((addr).s6_addr16[1]), \
	ntohs((addr).s6_addr16[2]), \
	ntohs((addr).s6_addr16[3]), \
	ntohs((addr).s6_addr16[4]), \
	ntohs((addr).s6_addr16[5]), \
	ntohs((addr).s6_addr16[6]), \
	ntohs((addr).s6_addr16[7])
#define NIPQUAD_FMT "%" PRIu32 ".%" PRIu32 ".%" PRIu32 ".%" PRIu32 ""
#define NIPQUAD(addr)                                     \
  ((const uint8_t*)&addr)[0], ((const uint8_t*)&addr)[1], \
      ((const uint8_t*)&addr)[2], ((const uint8_t*)&addr)[3]
#define NMACADDR_FMT                                                     \
	"%02" PRIx16 ":%02" PRIx16 ":%02" PRIx16 ":%02" PRIx16 ":%02" PRIx16 ":%02" PRIx16 ""
#define NMACADDR(mac) mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]

struct dot11_llc_snap_header {
	uint8_t dsap; /* always 0xAA */
	uint8_t ssap; /* always 0xAA */
	uint8_t ctl; /* always 0x03 */
	uint8_t oui[DOT11_OUI_LEN]; /* RFC1042: 0x00 0x00 0x00 */
	uint16_t type; /* ethertype */
};

void NetEngineFormatWlanForwardDescriptor(void *txd_ext, const nep_pkt_info *pkt_info,
					  const nep_forward_info *nw_info);
void NetEngineFormatModemForwardDescriptor(void *txd_ext, const nep_pkt_info *pkt_info,
					   const nep_forward_info *nw_info);
void NetEngineForwardDescriptorFormat(struct noa_desc *desc, const nep_pkt_info *pkt_info,
				      const nep_forward_info *nw_info);
// A return of 0 means the packet should be forwarded, while
// -ENOENT indicates that the packet cannot find an entry in
// the tethering table and should be fallback to APC.
int32_t NetEngineTetheringHandleWlanPacket(nep_pkt_info *pkt_info, nep_forward_info *nw_info,
					   bool *is_downstream);
// A return of 0 means the packet should be forwarded, while
// -ENOENT indicates that the packet cannot find an entry in
// the tethering table and should be fallback to APC.
int32_t NetEngineTetheringHandleModemPacket(nep_pkt_info *pkt_info, nep_forward_info *nw_info);
// A return of 0 means the packet should be forwarded (i.e. fallback to APC),
// while -1 indicates that the packet doesn't pass the PPF filter and should
// be dropped.
int32_t NetEnginePpfHandleWlanPacket(const nep_pkt_info *pkt_info, const nep_ppf_info *ppf_info);
// Dumps the content of the downstream IPv4 tethering table.
void NetEngineDumpDownstream4Table(void);
// Dumps the content of the upstream IPv4 tethering table.
void NetEngineDumpUpstream4Table(void);
// Dumps the content of the downstream IPv6 tethering table.
void NetEngineDumpDownstream6Table(void);
// Dumps the content of the upstream IPv6 tethering table.
void NetEngineDumpUpstream6Table(void);
#endif /* NOA_NET_NETENGINE_UTILS_H */
