// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NetEngine Utility
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Mark Chien <markchien@google.com>, KH Shi <kenghua@google.com>
 */
#ifdef linux
#include "netengine_utils.h"

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/byteorder/generic.h>
#include <net/ndisc.h>

#include "common/core.h"
#include "common/memory.h"
#include "common/inttypes.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "if_ether.h"
#include "offload.h"
#include "ra_proxy.h"
#include "nep.h"
#include "netengine.h"
#include "nep_helpers.h"
#include "nep_tables.h"
#else /* linux */
#include "net/netengine_utils.h"

#include <cstdint>
#include <cerrno>
#include <cinttypes>

#include "arch/memory.h"
#include "common/core.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include "net/if_ether.h"
#include "net/offload.h"
#include "net/nep_tables.h"
#include "net/nep_helpers.h"
#include "net/ra_proxy.h"
#endif /* linux */

static inline bool IsDebugMode(void)
{
#ifdef linux
	return noa_sim_get()->dbg;
#else /* linux */
	return false;
#endif /* linux */
}

static inline uint8_t *GetWlanUpstreamMac(void)
{
	return &get_tethering_offload_info()->upstream_if_Mac[0];
}

static inline bool IsForceFallback(void)
{
#ifdef linux
	return is_sim_force_fallback();
#else /* linux */
	return false;
#endif /* linux */
}

#if defined(NOA_MODEM_USE_ALCEDO_SOLUTION)
#include "common/md/mediatek/noa_md_mtk_common.h"

#ifdef linux
typedef struct noa_modem_ext_txd noa_modem_ext_txd;
#endif /* linux */

#elif defined(NOA_MODEM_USE_LASSEN_SOLUTION)

#ifdef linux
#include "common/md/samsung/mr_md.h"
#include "sim/ncp/md/samsung/ncp_md_fw.h"
typedef struct mr_lassen_ext_txd mr_lassen_ext_txd;

#else /* linux */
#include "modem/modem_ring_noa_descriptor.h"
using mr_lassen_ext_txd = ::noa::driver::modem::ModemRingLassenTxExtension;
#define MR_TO_MD_FR_WIFI (::noa::driver::modem::ModemRingToModemSrc::kWifi)

#endif /* linux */

#else
#error "A proper modem solution has not yet been defined"

#endif

void NetEngineFormatWlanForwardDescriptor(void *txd, const nep_pkt_info *pkt_info,
					  const nep_forward_info *nw_info)
{
	struct network_ext_txd *nw_txd = (struct network_ext_txd *)txd;
	struct ethhdr *eth;

	eth = (struct ethhdr *)pkt_info->header_address;
	memcpy(nw_txd->dest, eth->h_dest, sizeof(eth->h_dest));
	memcpy(&nw_txd->info, nw_info, sizeof(*nw_info));
}

void NetEngineFormatModemForwardDescriptor(void *txd_ext __attribute__((__unused__)),
					   const nep_pkt_info *pkt_info __attribute__((__unused__)),
					   const nep_forward_info *nw_info
					   __attribute__((__unused__)))
{
#if defined(NOA_MODEM_USE_ALCEDO_SOLUTION)
	noa_modem_ext_txd *txd = (noa_modem_ext_txd *)txd_ext;
	memset(txd, 0, sizeof(noa_modem_ext_txd));
	txd->pkt_info.ifindex = nw_info->oif;
	txd->pkt_info.drb_cnt = 2;
#elif defined(NOA_MODEM_USE_LASSEN_SOLUTION)
	mr_lassen_ext_txd *txd = (mr_lassen_ext_txd *)txd_ext;
	memset(txd, 0, sizeof(mr_lassen_ext_txd));
	txd->channel_id = 182;
	txd->src = MR_TO_MD_FR_WIFI; // From Wi-Fi
#endif
}

void NetEngineForwardDescriptorFormat(struct noa_desc *desc, const nep_pkt_info *pkt_info,
				      const nep_forward_info *nw_info)
{
	const uint8_t modem_tx_path = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice, kNoaModemRingTxData);
	const uint8_t wlan_tx_path = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice, kNoaWlanRingTxData);

	switch (pkt_info->action) {
	case PKT_ACTION_FORWARD_TO_WLAN:
		desc->dst = wlan_tx_path;
		desc->desc_type = NOA_DESC_NETENG_PKT_FLOW;
		NetEngineFormatWlanForwardDescriptor((struct network_ext_txd *)&desc->ext_data[0],
						     pkt_info, nw_info);
		break;
	case PKT_ACTION_FORWARD_TO_MODEM:
		desc->dst = modem_tx_path;
		desc->desc_type = NOA_DESC_MODEM_TX;
		NetEngineFormatModemForwardDescriptor((void *)&desc->ext_data[0], pkt_info,
						      nw_info);
		break;
	default:
		pr_err("Failed to format forward desc with action %" PRIu32 "\n", pkt_info->action);
		desc->dst = desc->src;
		desc->reason = FWD_REASON_FALLBACK;
		return;
	}

	desc->reason = FWD_REASON_NETENGINE;
	desc->mode = NOAD_MODE_DATA;
	desc->dl = pkt_info->header_length;
	desc->dv = pkt_info->header_address;
}

int32_t NetEngineTetheringHandleWlanPacket(nep_pkt_info *pkt_info, nep_forward_info *nw_info,
					   bool *is_downstream)
{
	struct ethhdr *eth;
	uint32_t pkt_len;
	int32_t net_act;
	uint8_t *pkt;

#ifdef linux
	if (get_monitor_packet_type() == MONITOR_PACKET_TYPE_RS) {
		struct ipv6hdr *ip6h;
		struct icmp6hdr *icmp6h;
		pkt = (uint8_t *)pkt_info->header_address;
		pkt_len = pkt_info->packet_length;
		eth = (struct ethhdr *)(pkt);
		ip6h = (struct ipv6hdr*)(eth + 1);
		icmp6h = (struct icmp6hdr *)(ip6h + 1);
		if (ntohs(eth->h_proto) == ETH_P_IPV6 &&
		    ip6h->nexthdr == IPPROTO_ICMPV6 &&
		    icmp6h->icmp6_type == NDISC_ROUTER_SOLICITATION) {
			pr_info("Receive Router Solicitation packet:\n");
			hexdump("Packet data: ", pkt, pkt_len);
		}
	}
#endif

	if (IsForceFallback()) {
		return -ENOENT;
	}

	pkt = (uint8_t *)pkt_info->header_address;

	eth = (struct ethhdr *)(pkt);
	if (IsDebugMode()) {
		pr_debug("src: %02X:%02X:%02X:%02X:%02X:%02X, ethertype %x\n", eth->h_source[0],
			 eth->h_source[1], eth->h_source[2], eth->h_source[3], eth->h_source[4],
			 eth->h_source[5], eth->h_proto);
		pr_debug("dst: %02X:%02X:%02X:%02X:%02X:%02X\n", eth->h_dest[0], eth->h_dest[1],
			 eth->h_dest[2], eth->h_dest[3], eth->h_dest[4], eth->h_dest[5]);
	}

	pkt_len = pkt_info->packet_length;

	if (memcmp(GetWlanUpstreamMac(), eth->h_dest, ETH_ALEN) == 0) {
		*is_downstream = true;
	}

	// Provide the logical fallback port for statistics purposes if the packet is punted.
	net_act = do_process_pkt((void **)&pkt, &pkt_len, true, *is_downstream, (int)NOA_PORT_WLAN_SW, nw_info);
	if (net_act == NET_ENGINE_ACT_FORWARD) {
		pkt_info->header_address = (uintptr_t) pkt;
		// The forwarding decision (WLAN vs. Modem) is communicated via a flag.
		if (!nw_info->is_wlan_forward) {
			if (pkt_len <= MODEM_HEADER_ALI_LEN) {
				pkt_info->header_length = pkt_len;
				pkt_info->unchanged_payload_length = 0;
			} else {
				pkt_info->header_length = MODEM_HEADER_ALI_LEN;
				pkt_info->unchanged_payload_address = pkt_info->packet_address + WIFI_TO_MODEM_OFFSET;
				pkt_info->unchanged_payload_length = pkt_info->packet_length - WIFI_TO_MODEM_OFFSET;
			}
			pkt_info->action = PKT_ACTION_FORWARD_TO_MODEM;
		} else {
			if (pkt_len <= WIFI_HEADER_ALI_LEN) {
				pkt_info->header_length = pkt_len;
				pkt_info->unchanged_payload_length = 0;
			} else {
				pkt_info->header_length = WIFI_HEADER_ALI_LEN;
				pkt_info->unchanged_payload_address = pkt_info->packet_address + WIFI_TO_WIFI_OFFSET;
				pkt_info->unchanged_payload_length = pkt_info->packet_length - WIFI_TO_WIFI_OFFSET;
			}
			pkt_info->action = PKT_ACTION_FORWARD_TO_WLAN;
		}

		return 0;
	}

	return -ENOENT;
}

// A return of 0 means the packet should be forwarded, while
// -ENOENT indicates that the packet cannot find an entry in
// the tethering table and should be fallback to APC.
int32_t NetEngineTetheringHandleModemPacket(nep_pkt_info *pkt_info, nep_forward_info *nw_info)
{
	void *pkt;
	uint32_t pkt_len;
	int32_t net_act;

	if (IsForceFallback()) {
		return -ENOENT;
	}

	pkt = (void *)((u8 *)pkt_info->header_address);
	pkt_len = pkt_info->packet_length;

	net_act = do_process_pkt((void **)&pkt, &pkt_len, false, true, (int)NOA_PORT_MODEM_SW, nw_info);
	if (net_act == NET_ENGINE_ACT_FORWARD) {
		pkt_info->header_address = (uintptr_t) pkt;
		if (pkt_len <= WIFI_HEADER_ALI_LEN) {
				pkt_info->header_length = pkt_len;
				pkt_info->unchanged_payload_length = 0;
		} else {
			pkt_info->header_length = WIFI_HEADER_ALI_LEN;
			pkt_info->unchanged_payload_address = pkt_info->packet_address + MODEM_TO_WIFI_OFFSET;
			pkt_info->unchanged_payload_length = pkt_info->packet_length - MODEM_TO_WIFI_OFFSET;
		}
		pkt_info->action = PKT_ACTION_FORWARD_TO_WLAN;
	} else {
		return -ENOENT;
	}

	return 0;
}

// A return of 0 means the packet should be forwarded (i.e. fallback to APC),
// while -1 indicates that the packet doesn't pass the PPF filter and should
// be dropped.
int32_t NetEnginePpfHandleWlanPacket(const nep_pkt_info *pkt_info, const nep_ppf_info *ppf_info)
{
	uint8_t *pkt;
	uint32_t pkt_len;
	uint8_t *eth;
	uint32_t ifindex = 0;

	pkt = (uint8_t *)pkt_info->header_address;
	eth = pkt;
	pkt_len = pkt_info->packet_length;
	if (ppf_info) {
		ifindex = ppf_info->iif;
	}

	if (IsDebugMode()) {
		pr_debug("packet: %p, ifindex = %u, length = %u\n", eth, ifindex, pkt_len);
	}

	if (ra_proxy_accept_packet(eth, pkt_len, ifindex) != 0) {
		pr_debug("Drop RA packet");
		return -1;
	};

	return 0;
}

/**
 * @brief Logs the key-value pair of a Tether4 entry.
 * @param key The Tether4Key to log.
 * @param value The Tether4Value to log.
 */
static void NetEngineDumpTether4KeyValue(Tether4Key *key, Tether4Value *value)
{
	pr_info("Key  : l4Proto: %" PRId32 ", src: " NIPQUAD_FMT ":%" PRId32 ", dst: " NIPQUAD_FMT ":%" PRId32 "",
		ntohs(key->l4Proto), NIPQUAD(key->src4), ntohs(key->srcPort), NIPQUAD(key->dst4), ntohs(key->dstPort));
	pr_info("Value: dstMac: " NMACADDR_FMT ", mangle46: " NIP6_FMT ", manglePort: %" PRIu32 ", last_used: %" PRIx64 "",
		NMACADDR(value->dstMac), NIP6(value->mangle46), ntohs(value->manglePort), ntohll(value->last_used));
}

/**
 * @brief Logs the key-value pair of a TetherDownstream6 entry.
 * @param key The TetherDownstream6Key to log.
 * @param value The Tether6Value to log.
 */
static void NetEngineDumpDownstream6KeyValue(TetherDownstream6Key *key, Tether6Value *value)
{
	pr_info("Key  : iif: %" PRId32 ", dstMac: " NMACADDR_FMT ", neigh6: " NIP6_FMT "",
		key->iif, NMACADDR(key->dstMac), NIP6(key->neigh6));
	pr_info("Value: oif: %" PRId32 ", pmtu: %" PRIu32 ", MAC Header [dst: " NMACADDR_FMT ", src: " NMACADDR_FMT ", proto: 0x%" PRIx16 "]",
		value->oif, value->pmtu, NMACADDR(value->macHeader.h_dest),
		NMACADDR(value->macHeader.h_source), ntohs(value->macHeader.h_proto));
}

/**
 * @brief Logs the key-value pair of a TetherUpstream6 entry.
 * @param key The TetherUpstream6Key to log.
 * @param value The Tether6Value to log.
 */
static void NetEngineDumpUpstream6KeyValue(TetherUpstream6Key *key, Tether6Value *value)
{
	pr_info("Key  : iif: %" PRId32 ", dstMac: " NMACADDR_FMT ", src64: 0x%" PRIx64 "",
			key->iif, NMACADDR(key->dstMac), ntohll(key->src64));
	pr_info("Value: oif: %" PRId32 ", pmtu: %" PRIu32 ", MAC Header [dst: " NMACADDR_FMT ", src: " NMACADDR_FMT ", proto: 0x%" PRIx16 "]",
		value->oif, value->pmtu, NMACADDR(value->macHeader.h_dest),
		NMACADDR(value->macHeader.h_source), ntohs(value->macHeader.h_proto));
}

/**
 * @brief Dumps the content of the downstream IPv4 tethering table via pr_info.
 */
void NetEngineDumpDownstream4Table(void)
{
	struct offload_info *ol_info = get_tethering_offload_info();
	int32_t i = 0;
	u32 bucket = 0, last = 0;
	Tether4Key key;
	Tether4Value value;

	pr_info("==== NetEngine Dump Downstream4 Table ====");
	pr_info("RX iif %" PRId32 " -> TX iif %" PRId32 "", ol_info->upstreamIif, ol_info->downstreamIif);
	pr_info("srcMac(gw) dstMac(" NMACADDR_FMT ") -> srcMac(" NMACADDR_FMT ") dstMac(client)",
		    NMACADDR(ol_info->upstream_if_Mac), NMACADDR(ol_info->downstream_if_Mac));

	while (nep_tables_downstream4_map_dump_next(&bucket, &last, &key, &value) == 0) {
		pr_info("---- Entry %" PRId32 " ----", i++);
		NetEngineDumpTether4KeyValue(&key, &value);
	}
}

/**
 * @brief Dumps the content of the upstream IPv4 tethering table via pr_info.
 */
void NetEngineDumpUpstream4Table(void)
{
	struct offload_info *ol_info = get_tethering_offload_info();
	int32_t i = 0;
	u32 bucket = 0, last = 0;
	Tether4Key key;
	Tether4Value value;

	pr_info("==== NetEngine Dump Upstream4 Table ====");
	pr_info("RX iif %" PRId32 " -> TX iif %" PRId32 "", ol_info->downstreamIif, ol_info->upstreamIif);
	pr_info("srcMac(client) dstMac(" NMACADDR_FMT ") -> srcMac(" NMACADDR_FMT ") dstMac(gw)",
		    NMACADDR(ol_info->downstream_if_Mac), NMACADDR(ol_info->upstream_if_Mac));

	while (nep_tables_upstream4_map_dump_next(&bucket, &last, &key, &value) == 0) {
		pr_info("---- Entry %" PRId32 " ----", i++);
		NetEngineDumpTether4KeyValue(&key, &value);
	}
}

/**
 * @brief Dumps the content of the downstream IPv6 tethering table via pr_info.
 */
void NetEngineDumpDownstream6Table(void)
{
	int32_t i = 0;
	u32 bucket = 0, last = 0;
	TetherDownstream6Key key;
	Tether6Value value;

	pr_info("==== NetEngine Dump Downstream6 Table ====");
	while (nep_tables_downstream6_map_dump_next(&bucket, &last, &key, &value) == 0) {
		pr_info("---- Entry %" PRId32 " ----", i++);
		NetEngineDumpDownstream6KeyValue(&key, &value);
	}
}

/**
 * @brief Dumps the content of the upstream IPv6 tethering table via pr_info.
 */
void NetEngineDumpUpstream6Table(void)
{
	int32_t i = 0;
	u32 bucket = 0, last = 0;
	TetherUpstream6Key key;
	Tether6Value value;

	pr_info("==== NetEngine Dump Upstream6 Table ====");
	while (nep_tables_upstream6_map_dump_next(&bucket, &last, &key, &value) == 0) {
		pr_info("---- Entry %" PRId32 " ----", i++);
		NetEngineDumpUpstream6KeyValue(&key, &value);
	}
}
