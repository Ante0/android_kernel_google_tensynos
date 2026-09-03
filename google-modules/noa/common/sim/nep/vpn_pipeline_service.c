// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of VPN Pipeline Service
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Mike Yu <yumike@google.com>
 */
#include "vpn_pipeline_service.h"

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>

#if IS_ENABLED(CONFIG_NOA_MD_SAMSUNG_SUPPORT)
#include "sim/ncp/md/samsung/ncp_md_fw.h"
#endif /* CONFIG_NOA_MD_SAMSUNG_SUPPORT */

#include "common/compiler.h"
#include "common/core.h"
#include "common/ring_id.h"
#include "common/wlan_ring_id.h"
#include "common/modem_ring_id.h"
#include <common/wlan/noa_wlan.h>
#include "if_ether.h"
#include "netengine_utils.h"
#include "network_pipeline_framework/engine.h"
#include "network_pipeline_framework/nested_ring_stage.h"
#include "network_pipeline_framework/task_scheduler.h"

#define VPN_IPSEC_HANDLED 1
#define VPN_IPSEC_NOT_HANDLED 2

#if IS_ENABLED(CONFIG_XFRM_OFFLOAD)
#include "noa_ipsec_crypto.h"
#include "noa_ipsec_utils.h"
#include "noa_ipsec.h"
#else /* CONFIG_XFRM_OFFLOAD */
enum packet_dir {
	PACKET_IN = 1, // XFRM_DEV_OFFLOAD_IN
	PACKET_OUT = 2, // XFRM_DEV_OFFLOAD_OUT
};

#define IP_HEADER_SIZE 20  // (sizeof(struct iphdr))
#endif

static int32_t HandleWlanRxPacket(nep_pkt_info *pkt_info, struct noa_rx_ipsec_metadata *metadata);
static int32_t HandleModemRxPacket(nep_pkt_info *pkt_info, struct noa_rx_ipsec_metadata *metadata);
static int32_t HandleTxPacket(nep_pkt_info *pkt_info, struct noa_tx_ipsec_metadata *metadata,
			      const bool is_wlan);

static inline int32_t HandleWlanTxPacket(nep_pkt_info *pkt_info,
					 struct noa_tx_ipsec_metadata *metadata)
{
	return HandleTxPacket(pkt_info, metadata, true);
}

static inline int32_t HandleModemTxPacket(nep_pkt_info *pkt_info,
					  struct noa_tx_ipsec_metadata *metadata)
{
	return HandleTxPacket(pkt_info, metadata, false);
}

#define MAX_VPN_PROCESSING_COUNT (1U)

#define VPN_PROCESSING_FUNCS(interface, flow, entry_type)                                          \
	static int32_t Vpn##interface##flow##Processing(struct NestedRingStage *stage,             \
							NestedRingRequest *req)                    \
	{                                                                                          \
		int32_t ret;                                                                       \
		uintptr_t end;                                                                     \
		entry_type *entry;                                                                 \
		uint32_t count = MAX_VPN_PROCESSING_COUNT;                                         \
                                                                                                   \
		NESTED_RING_FOR_EACH_ENTRY_N(stage, req, end, entry, count)                        \
		{                                                                                  \
			if (entry->pkt_info.action != PKT_ACTION_UNDECIDED) {                      \
				continue;                                                          \
			}                                                                          \
                                                                                                   \
			ret = Handle##interface##flow##Packet(&entry->pkt_info,                    \
							      &entry->ipsec_metadata);             \
			if (!ret) {                                                                \
				entry->pkt_info.action = PKT_ACTION_PROCESS_AS_VPN;                \
			}                                                                          \
		}                                                                                  \
		return 0;                                                                          \
	}

VPN_PROCESSING_FUNCS(Wlan, Rx, nep_device_entry);
VPN_PROCESSING_FUNCS(Modem, Rx, nep_device_entry);
VPN_PROCESSING_FUNCS(Wlan, Tx, nep_host_entry);
VPN_PROCESSING_FUNCS(Modem, Tx, nep_host_entry);

int32_t NestedRingVpnStageSetup(const NestedRingVpnStageSetupParams *params)
{
	NestedRingStage *stage = params->stage;
	NestedRingEngine *engine = params->engine;

	if (params->is_tx_path) {
		NestedRingEngineInit(engine,
				     params->is_wlan ? VpnWlanTxProcessing : VpnModemTxProcessing);
	} else {
		NestedRingEngineInit(engine,
				     params->is_wlan ? VpnWlanRxProcessing : VpnModemRxProcessing);
	}
	NestedRingStageInit(stage, false, *params->shadow_ring_info, *params->cached_ring_info,
			    NULL, engine, params->next_stage, params->task, params->name, NULL);
	return 0;
}

// A return of 0 means the packet is VPN packet, while
// -ENOENT indicates that the packet cannot find an entry in
// the VPN table and should be fallback to APC.
static int32_t HandleWlanRxPacket(nep_pkt_info *pkt_info, struct noa_rx_ipsec_metadata *metadata)
{
	struct ethhdr *eth = (struct ethhdr *)((u8 *)pkt_info->packet_address);
	void *pkt = (void *)((u8 *)eth + sizeof(struct ethhdr));
	int32_t pkt_len = pkt_info->packet_length - sizeof(struct ethhdr);
	int32_t original_pkt_len = pkt_len;
	uint32_t ipsec_handle = 0;
	const bool unwanted_packet =
		eth->h_proto != htons(ETH_P_IPV6) && eth->h_proto != htons(ETH_P_IP);

	if (!unwanted_packet) {
		const int status = process_pkt_for_vpn(pkt, &pkt_len, PACKET_IN, &ipsec_handle, 0);

		if (status != IPSEC_METADATA_STATUS_SKIP) {
			metadata->status = status;
			metadata->ipsec_handle = ipsec_handle;
			pkt_info->packet_length +=
				(pkt_len + sizeof(struct ethhdr) - original_pkt_len);
			update_wifi_rx_l2_header_for_vpn((unsigned char *)eth);
			return 0;
		}
	}
	return -ENOENT;
}

// A return of 0 means the packet is VPN packet, while
// -ENOENT indicates that the packet cannot find an entry in
// the VPN table and should be fallback to APC.
static int32_t HandleModemRxPacket(nep_pkt_info *pkt_info, struct noa_rx_ipsec_metadata *metadata)
{
	void *pkt = (void *)((u8 *)pkt_info->packet_address);
	int32_t pkt_len = pkt_info->packet_length;
	int32_t original_pkt_len = pkt_len;
	uint32_t ipsec_handle = 0;
	int status;

	status = process_pkt_for_vpn(pkt, &pkt_len, PACKET_IN, &ipsec_handle, 0);
	if (status != IPSEC_METADATA_STATUS_SKIP) {
		metadata->status = status;
		metadata->ipsec_handle = ipsec_handle;
		pkt_info->packet_length += (pkt_len - original_pkt_len);
		return 0;
	}
	return -ENOENT;
}

static int32_t VpnRxPacketEngine(struct NepEngine *engine, struct NepProcessingRequest *request)
{
	uint8_t interface;
	uint8_t flow;
	uint8_t category;
	int32_t ret;
	struct NepPacketContext *packet = request->packet;
	struct noa_desc *desc = (struct noa_desc *)&packet->noa_desc[0];
	nep_pkt_info pkt_info;
	struct noa_rx_ipsec_metadata *metadata = NULL;

	if (desc->reason == FWD_REASON_FEEDTHROUGH) {
		return 0;
	}

	ParseNoaDescToNepPktInfo(desc, &pkt_info);
	NoaRingPathIdParse(packet->src, &interface, &flow, &category);
	switch (interface) {
	case kNoaNetworkInterfaceWlan:
		metadata = (struct noa_rx_ipsec_metadata *)desc->ext_data;
		ret = HandleWlanRxPacket(&pkt_info, metadata);
		break;
	case kNoaNetworkInterfaceModem:
#if IS_ENABLED(CONFIG_NOA_MD_SAMSUNG_SUPPORT)
		metadata = &((struct mr_lassen_ext_rxd *)desc->ext_data)->ipsec_metadata;
#endif /* CONFIG_NOA_MD_SAMSUNG_SUPPORT */
		ret = HandleModemRxPacket(&pkt_info, metadata);
		break;
	default:
		pr_err("Unexpected interface %u for VPN Rx Processing", interface);
		return -EINVAL;
		break;
	}

	if (!ret) {
		// Since VPN processes packets in-place, the packet length must include the original head_offset.
		desc->dl = pkt_info.packet_length + desc->head_offset;
		if (interface == kNoaNetworkInterfaceWlan) {
			desc->desc_type = NOA_DESC_VPN_RX;
		} else {
			/* Do not change the output descriptor type to NOA_DESC_VPN_RX because
			 * the descriptor type is NOA_DESC_MODEM_LASSEN. The associated ext_data
			 * type already supports ipsec metadata.
			 */
		}
		desc->reason = FWD_REASON_VPN;
		packet->use_packet_type = kUseOriginalPacket;
		packet->packet_buffer[packet->use_packet_type].dl = desc->dl;
	} else {
		desc->reason = FWD_REASON_FALLBACK;
	}
	return 0;
}

// This value is sync'ed from cpif/link_tx_pktproc.h.
// TODO(b/338502042): Remove it if there's a better way to know the padding size of packets
// from modem_sw.
#define CP_PADDING 76

#define BRCM_TX_L2_HEADER_SIZE (ETH_HLEN + DOT11_LLC_SNAP_HDR_LEN)

// A return of 0 means the packet is VPN packet, while
// -ENOENT indicates that the packet cannot find an entry in
// the VPN table and should be fallback to APC.
static int32_t HandleTxPacket(nep_pkt_info *pkt_info, struct noa_tx_ipsec_metadata *metadata,
			      const bool is_wlan)
{
	unsigned char *l2_pkt;
	unsigned char *pkt;
	int32_t pkt_len;
	int32_t original_pkt_len;
	uint32_t ipsec_handle = 0;
	bool unwanted_packet = false;
	uint32_t if_id = metadata->xfrm_interface_id;

	l2_pkt = (unsigned char *)pkt_info->packet_address;
	pkt = l2_pkt;
	pkt_len = pkt_info->packet_length;

	if (is_wlan) {
		uint16_t *h_proto;

		pkt += BRCM_TX_L2_HEADER_SIZE;
		pkt_len -= BRCM_TX_L2_HEADER_SIZE;

		h_proto = (uint16_t *)(pkt - 2);
		unwanted_packet = (*h_proto) != htons(ETH_P_IPV6) && (*h_proto) != htons(ETH_P_IP);
	} else {
		// TODO: Find a better way to know the padding size.
		pkt_len -= CP_PADDING;
	}
	original_pkt_len = pkt_len;

	if (pkt_len < (int)IP_HEADER_SIZE) {
		// The data carried by the noa_desc is too short.
		// Let it bypass VPN engine without modification.
		pr_warn("%s: Received packet too short. Ignored.", __func__);
		return -ENOENT;
	}

	if (noa_sim_get()->dbg) {
		hexdump("packet(out):", pkt, pkt_len);
	}

	if (!unwanted_packet) {
		const int status =
			process_pkt_for_vpn(pkt, &pkt_len, PACKET_OUT, &ipsec_handle, if_id);

		if (status != IPSEC_METADATA_STATUS_SKIP) {
			// Update the dl field based on the packet size difference between the
			// unencrypted/encrypted packet.
			pkt_info->packet_length += (pkt_len - original_pkt_len);

			if (is_wlan)
				update_wifi_tx_l2_header_for_vpn(l2_pkt, pkt_len);
			return 0;
		}
	}

	return -ENOENT;
}

static int32_t VpnTxPacketEngine(struct NepEngine *engine, struct NepProcessingRequest *request)
{
	int32_t ret;
	uint8_t interface;
	uint8_t flow;
	uint8_t category;
	struct NepPacketContext *packet = request->packet;
	struct noa_desc *desc = (struct noa_desc *)&packet->noa_desc[0];
	nep_pkt_info pkt_info;
	struct noa_tx_ipsec_metadata *metadata;
	bool is_wlan;

	if (desc->reason == FWD_REASON_FEEDTHROUGH) {
		return 0;
	}

	NoaRingPathIdParse(packet->src, &interface, &flow, &category);

	// TODO(b/394748911): Dump the data for debugging. Remove the code after the issue
	// is clarified.
	if (noa_sim_get()->dbg) {
		pr_warn("%s: dl=%d, head_offset=%d, interface=%d", __func__, desc->dl,
			desc->head_offset, interface);
		print_hex_dump(KERN_INFO, "dump dv:", DUMP_PREFIX_OFFSET, 32, 1,
			       (unsigned char *)desc->dv, desc->dl, false);
	}

	if (interface == kNoaNetworkInterfaceWlan) {
		is_wlan = true;
		metadata = &(((struct noa_bcm_txd *)desc->ext_data)->ipsec_metadata);
	} else {
		is_wlan = false;
		metadata = &(((struct mr_lassen_ext_txd *)desc->ext_data)->ipsec_metadata);
	}
	ParseNoaDescToNepPktInfo(desc, &pkt_info);
	ret = HandleTxPacket(&pkt_info, metadata, is_wlan);

	if (!ret) {
		// Since VPN processes packets in-place, the packet length must include the original head_offset.
		desc->dl = pkt_info.packet_length + desc->head_offset;
		desc->reason = FWD_REASON_VPN;
		packet->use_packet_type = kUseOriginalPacket;
		packet->packet_buffer[packet->use_packet_type].dl = desc->dl;
	} else {
		desc->reason = FWD_REASON_FALLBACK;
	}

	return 0;
}

#define BIT_SIZE_OF_VPN_STAGE (4U) // 2^4 = 16 packets

SEC_FAST_DATA static INIT_NEP_STAGE_ENDING_ROUTER(ending_router);
INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(SEC_FAST_DATA static,
					       vpn_rx_completed_stage_container,
					       BIT_SIZE_OF_VPN_STAGE);
SEC_FAST_DATA static INIT_NEP_STAGE(vpn_rx_completed_stage, &ending_router,
				    &vpn_rx_completed_stage_container.basic, NULL, NULL);

static bool IsFallbackPath(const struct NepPacketContext *packet)
{
	(void)packet;
	return true;
}
SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_RULE(vpn_rx_fallback_path, IsFallbackPath, NULL, NULL);

static bool IsVpnPath(const struct NepPacketContext *packet)
{
	return ((const struct noa_desc *)&packet->noa_desc[0])->reason == FWD_REASON_VPN;
}
SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_RULE(vpn_rx_path, IsVpnPath, &vpn_rx_completed_stage,
						  &vpn_rx_fallback_path);

INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(SEC_FAST_DATA static, vpn_rx_stage_container,
					       BIT_SIZE_OF_VPN_STAGE);
SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_CHAIN_ROUTER(vpn_rx_router, &vpn_rx_path);
SEC_FAST_DATA static INIT_NEP_ENGINE(vpn_rx_engine, NULL, VpnRxPacketEngine);
SEC_FAST_DATA static INIT_NEP_STAGE(vpn_rx_stage, &vpn_rx_router.basic,
				    &vpn_rx_stage_container.basic, &vpn_rx_engine, NULL);

int32_t NepVpnRxPathSetup(struct NepEngine *sender_engine, struct NepTaskScheduler *scheduler,
			  struct NepStage ***vpn_fallback_stage)
{
	vpn_rx_completed_stage.engine = sender_engine;
	vpn_rx_completed_stage.scheduler = scheduler;
	NepStageAddToScheduler(&vpn_rx_completed_stage, scheduler);

	vpn_rx_engine.scheduler = scheduler;
	NepEngineAddToScheduler(&vpn_rx_engine, scheduler);

	vpn_rx_stage.scheduler = scheduler;
	NepStageAddToScheduler(&vpn_rx_stage, scheduler);
	*vpn_fallback_stage = &vpn_rx_fallback_path.next_stage;

	return 0;
}

struct NepStage *NepVpnRxStageSingletonGet(void)
{
	return &vpn_rx_stage;
}

INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(SEC_FAST_DATA static,
					       vpn_tx_completed_stage_container,
					       BIT_SIZE_OF_VPN_STAGE);
SEC_FAST_DATA static INIT_NEP_STAGE(vpn_tx_completed_stage, &ending_router,
				    &vpn_tx_completed_stage_container.basic, NULL, NULL);

SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_RULE(vpn_tx_fallback_path, IsFallbackPath, NULL, NULL);
SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_RULE(vpn_tx_path, IsVpnPath, &vpn_tx_completed_stage,
						  &vpn_tx_fallback_path);

INIT_NEP_STAGE_MULTI_PKT_CONTAINER_WITH_PREFIX(SEC_FAST_DATA static, vpn_tx_stage_container,
					       BIT_SIZE_OF_VPN_STAGE);
SEC_FAST_DATA static INIT_NEP_STAGE_DECISION_CHAIN_ROUTER(vpn_tx_router, &vpn_tx_path);
SEC_FAST_DATA static INIT_NEP_ENGINE(vpn_tx_engine, NULL, VpnTxPacketEngine);
SEC_FAST_DATA static INIT_NEP_STAGE(vpn_tx_stage, &vpn_tx_router.basic,
				    &vpn_tx_stage_container.basic, &vpn_tx_engine, NULL);

int32_t NepVpnTxPathSetup(struct NepEngine *sender_engine, struct NepTaskScheduler *scheduler,
			  struct NepStage ***vpn_fallback_stage)
{
	vpn_tx_completed_stage.engine = sender_engine;
	vpn_tx_completed_stage.scheduler = scheduler;
	NepStageAddToScheduler(&vpn_tx_completed_stage, scheduler);

	vpn_tx_engine.scheduler = scheduler;
	NepEngineAddToScheduler(&vpn_tx_engine, scheduler);

	vpn_tx_stage.scheduler = scheduler;
	NepStageAddToScheduler(&vpn_tx_stage, scheduler);
	*vpn_fallback_stage = &vpn_tx_fallback_path.next_stage;

	return 0;
}

struct NepStage *NepVpnTxStageSingletonGet(void)
{
	return &vpn_tx_stage;
}
