// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NEP simulator
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/ktime.h>
#if IS_ENABLED(CONFIG_NOA_MD_SAMSUNG_SUPPORT)
#include <sim/ncp/md/samsung/ncp_md_fw.h>
#endif
#include "netengine.h"
#include "netengine_utils.h"
#include "util/ring_util.h"
#include "nep_helpers.h"
#include "nep_tables.h"
#include "offload.h"
#include <common/noatrace.h>
#include <common/ring.h>
#include <common/wlan_ring_id.h>
#include <common/modem_ring_id.h>
#include <common/buffer_manager.h>
#include <common/wlan/noa_wlan.h>
#include "nep.h"
#include "ring_descriptor.h"
#include "ring_manager.h"
#include "if_ether.h"

#if IS_ENABLED(CONFIG_XFRM_OFFLOAD)
#include "noa_ipsec_crypto.h"
#include "noa_ipsec_utils.h"
#include "noa_ipsec.h"
#endif

#if IS_ENABLED(CONFIG_NOA_PPF_SUPPORT)
#include "apf_interpreter.h"
#endif

#define NET_ENGINE_BUF_SIZE (2048U)
#define NET_ENGINE_RING_SIZE (1024U)
#define NET_ENGINE_BUF_NUM (NET_ENGINE_RING_SIZE * 2)

#if !IS_ENABLED(CONFIG_XFRM_OFFLOAD)
enum packet_dir {
	PACKET_IN = 1, // XFRM_DEV_OFFLOAD_IN
	PACKET_OUT = 2, // XFRM_DEV_OFFLOAD_OUT
};

#define IP_HEADER_SIZE 20  // (sizeof(struct iphdr))
#endif

/**
 * The buffer size should be twice the size of the ring buffer.
 * Because there are two rings, one is rx ring, we should prepare
 * one buffer for each slot in this rx ring. Another is tx ring,
 * we will move the descriptor including the buffer from rx ring to
 * this tx ring. In the worst case, the rx ring is empty and it
 * needs `RING_SIZE` number of buffer; The tx ring is full so it
 * contains `RING_SIZE` number of buffer. So we need twice the
 * size of the ring buffer.
 */
#define NET_ENGINE_BUFFER_POOL_SIZE ((NET_ENGINE_RING_SIZE << 1U))
static char neteng_ping_pong_buffers[NET_ENGINE_BUFFER_POOL_SIZE][NET_ENGINE_BUF_SIZE] = { 0 };
static bool neteng_ping_pong_index[NET_ENGINE_RING_SIZE] = { 0 };
static struct noa_session session_entries[SESSION_ENTRY_MAX] = { 0 };
static struct device *dev_netengine;

struct netengine_ring {
	struct noa_ring_wrapper ring;
	void *buffer;
	char static_wrap_buf[NOA_DESC_MAX_BYTE];
};

static noa_buffer_pool_desc g_pool_ring_buf[NET_ENGINE_RING_SIZE];

/**
 * TODO: we should put everything related to netengine into this structure,
 * including, dev_netengine, session_entries.
 */
struct netengine_info {
	struct noa_port *port;
	struct netengine_ring tx_ring;
	struct netengine_ring rx_ring;
	noa_ring_producer pool_ring;
};

struct netengine_info g_netengine;

static inline u16 get_ping_pong_buffer_tkid(u16 i)
{
	u16 buf_idx = (i << 1U | (neteng_ping_pong_index[i] & 0x1));
	neteng_ping_pong_index[i] ^= 1;
	return buf_idx;
}

static struct noa_session *get_free_session(void)
{
	int i;
	for (i = 0 ; i < SESSION_ENTRY_MAX; i++) {
		if (session_entries[i].common.state == SESSION_STATE_UNUSED) {
			return &session_entries[i];
		}
	}
	return NULL;
}

static bool is_same_session(struct noa_session *s1, struct noa_session *s2)
{
	return !memcmp(s1, s2, sizeof(struct noa_session));
}

static struct noa_session *find_session(struct noa_session *session)
{
	struct noa_session *target;
	int i;

	for (i = 0 ; i < SESSION_ENTRY_MAX; i++) {
		target = &session_entries[i];
		if (target->common.state == SESSION_STATE_UNUSED)
			continue;
		if (is_same_session(target, session))
			return target;
	}
	return NULL;
}

static bool match_ipv4_session(struct noa_session_ipv4 *ipv4, struct iphdr *iphdr)
{
	return ipv4->priority == iphdr->tos >> 5;
}

static bool match_ipv6_session(struct noa_session_ipv6 *ipv6, struct ipv6hdr *ipv6hdr)
{
	return ipv6->priority == ipv6hdr->priority;
}

struct noa_session *noa_sim_lookup_session(uint8_t *pkt)
{
	struct noa_session *target;
	struct noa_session_common *common;
	struct ethhdr *eth = (struct ethhdr *)pkt;
	struct iphdr *iphdr = (struct iphdr *)(pkt + sizeof(struct ethhdr));
	struct ipv6hdr *ipv6hdr = (struct ipv6hdr *)iphdr;
	int i;

	for (i = 0 ; i < SESSION_ENTRY_MAX; i++) {
		target = &session_entries[i];
		common = &target->common;
		/* match common layer */
		if (common->state == SESSION_STATE_UNUSED)
			continue;
		if (common->eth_type != eth->h_proto)
			continue;
		if (memcmp(&eth->h_dest[0], &common->dst_mac1[0], 2))
			continue;
		if (memcmp(&eth->h_dest[2], &common->dst_mac2[0], 4))
			continue;
		/* match ip layer is not required, this function is for L2 only */
		if (0) {
			if (common->eth_type == htons(ETH_P_IP)) {
				if (!match_ipv4_session(&target->noa_session_ip.ipv4, iphdr))
					continue;
			} else if (common->eth_type == htons(ETH_P_IPV6)) {
				if (!match_ipv6_session(&target->noa_session_ip.ipv6, ipv6hdr))
					continue;
			} else {
				continue;
			}
		}
		return target;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(noa_sim_lookup_session);

static void dump_session(struct noa_session *session)
{
	struct noa_session_common *entry = &session->common;
	dev_err(dev_netengine, "DW0::ver: %d, sport: %d, dport: %d, rsv1: %d, state: %d, ageout: %d\n",
		entry->ver, entry->sport, entry->dport, entry->rsv1, entry->state, entry->ageout);
	dev_err(dev_netengine, "DW1::ethtype: %d, src_mac1: %02X:%02X\n",
		entry->eth_type, entry->src_mac1[0], entry->src_mac1[1]);
	dev_err(dev_netengine, "DW2::src_mac2: %02X:%02X:%02X:%02X\n",
		entry->src_mac2[0], entry->src_mac2[1], entry->src_mac2[2], entry->src_mac2[3]);
	dev_err(dev_netengine, "DW3::vlan_act: %d, sec_act: %d, rsv2: %d, dst_mac1: %02X:%02X\n",
		entry->vlan_act, entry->sec_act, entry->rsv2, entry->dst_mac1[0],
			entry->dst_mac1[1]);
	dev_err(dev_netengine, "DW4::dst_mac2: %02X:%02X:%02X:%02X\n",
		entry->dst_mac2[0], entry->dst_mac2[1], entry->dst_mac2[2], entry->dst_mac2[3]);
	dev_err(dev_netengine, "DW5::vlan_info: %08X\n", entry->vlan_info);
	dev_err(dev_netengine, "DW6::sec_info: %08X\n", entry->sec_info);
	dev_err(dev_netengine, "DW7::src_ifidx: %d, dst_ifidx: %d, flow_id: %d, clat: %d, related_id %d\n",
		entry->src_ifidx, entry->dst_ifidx, entry->flow_id, entry->clat, entry->related_id);
}

static int net_engine_fallback_route(int src)
{
	return src;
}

void net_engine_offload_util_cnt_inc(bool is_to_apc, bool is_downstream)
{
	struct noa_simulator *sim = noa_sim_get();
	uint64_t boot_time_ms = ktime_to_ms(ktime_get_boottime());
	utilization_monitor_add(&sim->no_offload_util, boot_time_ms);
	if (is_to_apc) {
		utilization_monitor_add(&sim->offload_util, boot_time_ms);
	}
}
EXPORT_SYMBOL_GPL(net_engine_offload_util_cnt_inc);

void update_wifi_tx_l2_header_for_vpn(unsigned char *l2_pkt, int l3_pkt_len)
{
	struct ethhdr *eth = (struct ethhdr *)l2_pkt;
	struct dot11_llc_snap_header *llc_hdr = (struct dot11_llc_snap_header *)(eth + 1);
	struct iphdr *ip_hdr = (struct iphdr *)(llc_hdr + 1);

	// The ethernet format for brcm is 802.3 header.
	eth->h_proto = htons(l3_pkt_len + DOT11_LLC_SNAP_HDR_LEN);
	llc_hdr->type = (ip_hdr->version == 4) ? htons(ETH_P_IP) : htons(ETH_P_IPV6);
}
EXPORT_SYMBOL_GPL(update_wifi_tx_l2_header_for_vpn);

void update_wifi_rx_l2_header_for_vpn(unsigned char *l2_pkt)
{
	struct ethhdr *eth = (struct ethhdr *)l2_pkt;
	struct iphdr *ip_hdr = (struct iphdr *)(eth + 1);

	eth->h_proto = (ip_hdr->version == 4) ? htons(ETH_P_IP) : htons(ETH_P_IPV6);
}
EXPORT_SYMBOL_GPL(update_wifi_rx_l2_header_for_vpn);

/**
 * The main function that handles packet encryption and decryption.
 * The caller must passes an IPv4 or IPv6 packet to `pkt`.
 */
int process_pkt_for_vpn(unsigned char *pkt, int *pkt_len, int direction, u32 *ipsec_handle,
						u32 if_id)
{
#if IS_ENABLED(CONFIG_NOA_SIM_VPN_OFFLOAD_SUPPORT)
	int oseq;
	int output_pkt_len;
	noa_xfrm_state noa_sa;
	struct xfrm_state *sa;

	if (is_pkt_ipv4_keep_alive(pkt) || is_pkt_ike(pkt))
		return IPSEC_METADATA_STATUS_SKIP;

	noa_sa = lookup_sa(pkt, direction, &oseq, if_id);
	sa = noa_sa.state;
	if (!sa)
		return IPSEC_METADATA_STATUS_SKIP;

	if (direction == PACKET_OUT) {
		// The buffer must be allocated in heap, or the encryption will fail with the
		// following error:
		//   "Unable to handle kernel paging request at virtual address".
		// It's possibly due to some stack protection mechanism in Android kernel.
		// TODO: See if it's feasible to allocate the memory size based on the MTU of
		// underlying network.
		unsigned char *output_pkt = kmalloc(DEFAULT_MTU, GFP_KERNEL);
		const bool is_packet_offload = (sa->xso.type == XFRM_DEV_OFFLOAD_PACKET);

		if (is_packet_offload) {
			output_pkt_len = encrypt_packet_for_packet_offload(sa, pkt, *pkt_len,
				output_pkt, oseq);
		} else {
			memcpy(output_pkt, pkt, *pkt_len);
			output_pkt_len = encrypt_packet_for_crypto_offload(sa, output_pkt,
				*pkt_len);
		}

		if (output_pkt_len <= 0) {
			pr_err("encryption failed with error %d\n", output_pkt_len);
			kfree(output_pkt);
			return output_pkt_len == -EBADMSG ? IPSEC_METADATA_STATUS_AUTH_FAILED :
							    IPSEC_METADATA_STATUS_GENERIC_ERROR;
		}

		*pkt_len = output_pkt_len;
		memcpy(pkt, output_pkt, output_pkt_len);
		kfree(output_pkt);

		return IPSEC_METADATA_STATUS_SUCCESS;
	} else if (direction == PACKET_IN) {
		*ipsec_handle = noa_sa.seq;
		output_pkt_len = decrypt_packet(sa, pkt, *pkt_len, pkt);
		if (output_pkt_len <= 0) {
			pr_err("decryption failed with error %d\n", output_pkt_len);
			return output_pkt_len == -EBADMSG ? IPSEC_METADATA_STATUS_AUTH_FAILED :
							    IPSEC_METADATA_STATUS_GENERIC_ERROR;
		}
		*pkt_len = output_pkt_len;

		return IPSEC_METADATA_STATUS_SUCCESS;
	}
#endif

	return IPSEC_METADATA_STATUS_SKIP;
}
EXPORT_SYMBOL_GPL(process_pkt_for_vpn);

static int net_engine_wifi_pkt_handle(const struct noa_desc *in, struct noa_desc *desc)
{
	int32_t ret;
	struct noa_simulator *sim = noa_sim_get();
	struct ethhdr *eth;
	bool is_downstream = false;
	int pkt_len;
	nep_pkt_info pkt_info;
	nep_forward_info nw_info;

	/* va is used in simulator only. */
	eth = (struct ethhdr *)((u8 *)in->dv + in->head_offset);

	pkt_len = in->dl - in->head_offset;

#if IS_ENABLED(CONFIG_NOA_PPF_SUPPORT)
	if (sim->nep_apf_info.is_apf_filter_enabled && sim->nep_apf_info.apf_program_len > 0) {
		uint64_t boot_time_ms = ktime_to_ms(ktime_get_boottime());
		/*
		 * TODO: If the return value is zero, the packet should be filtered.
		 *       Currently, the ring manager does not support dropping packets.
		 */
		accept_packet(sim->nep_apf_info.apf_program, sim->nep_apf_info.apf_program_len,
			      NEP_MAX_APF_PROGRAM_LEN, (uint8_t*) eth, pkt_len,
			      (boot_time_ms - sim->nep_apf_info.apf_program_time_ms) / 1000);
	}
#endif

	ParseNoaDescToNepPktInfo(in, &pkt_info);
	ret = NetEngineTetheringHandleWlanPacket(&pkt_info, &nw_info, &is_downstream);
	if (!ret) {
		NetEngineForwardDescriptorFormat(desc, &pkt_info, &nw_info);
		desc->cp = NOAD_COPY_DATA_AND_RENEW_TKID;
		net_engine_offload_util_cnt_inc(false, is_downstream);
	} else {
		const int original_pkt_len = pkt_len;
		u32 ipsec_handle = 0;
		unsigned char *pkt = (unsigned char *)(eth + 1);
		const bool unwanted_packet =
			eth->h_proto != htons(ETH_P_IPV6) && eth->h_proto != htons(ETH_P_IP);

		pkt_len -= sizeof(struct ethhdr);

		/* TODO: Don't process the packet if the `desc->desc_type` is not valid
		 * for NOA VPN, because it's unsafe to write the ipsec metadata to a
		 * descriptor that is invalid to NOA VPN.
		 */
		if (!unwanted_packet) {
			const int status =
				process_pkt_for_vpn(pkt, &pkt_len, PACKET_IN, &ipsec_handle, 0);

			if (status != IPSEC_METADATA_STATUS_SKIP) {
				struct noa_rx_ipsec_metadata *metadata =
					(struct noa_rx_ipsec_metadata *)desc->ext_data;

				metadata->status = status;
				metadata->ipsec_handle = ipsec_handle;
				pkt_len += sizeof(struct ethhdr);
				desc->dl += (pkt_len - original_pkt_len);
				desc->desc_type = NOA_DESC_VPN_RX;
				update_wifi_rx_l2_header_for_vpn((unsigned char *)eth);
			}
		}
		/* fallback path reuse the original rx buffer, remark cp as 0 */
		desc->mode = NOAD_MODE_DATA;
		desc->reason = FWD_REASON_FALLBACK;
		desc->cp = NOAD_NO_COPY;
		desc->dst = net_engine_fallback_route(in->src);
		desc->fk = NOAD_FEEDBACK_DISABLE;
		net_engine_offload_util_cnt_inc(true, is_downstream);
	}
	/*
	 * desc src should not be replaced to net engine due to the src port
	 * will be used for sending feedback event.
	 */
	desc->ddone = 0;
	return 0;
}

static int net_engine_md_pkt_handle(const struct noa_desc *src, struct noa_desc *dst)
{
	int32_t ret;
	void *pkt;
	int pkt_len;
	nep_pkt_info pkt_info;
	nep_forward_info nw_info;

	pkt = (void *)((u8 *)src->dv + src->head_offset);
	pkt_len = src->dl - src->head_offset;

	ParseNoaDescToNepPktInfo(src, &pkt_info);
	ret = NetEngineTetheringHandleModemPacket(&pkt_info, &nw_info);
	if (!ret) {
		NetEngineForwardDescriptorFormat(dst, &pkt_info, &nw_info);
		dst->cp = NOAD_COPY_DATA_AND_RENEW_TKID;
		net_engine_offload_util_cnt_inc(false, true);
	} else {
		const int original_pkt_len = pkt_len;
		u32 ipsec_handle = 0;
		int status;

		/* TODO: Don't process the packet if the `desc->desc_type` is not valid
		 * for NOA VPN, because it's unsafe to write the ipsec metadata to a
		 * descriptor that is invalid to NOA VPN.
		 */
		status = process_pkt_for_vpn(pkt, &pkt_len, PACKET_IN, &ipsec_handle, 0);
		if (status != IPSEC_METADATA_STATUS_SKIP) {
#if IS_ENABLED(CONFIG_NOA_MD_SAMSUNG_SUPPORT)
			struct mr_lassen_ext_rxd *rxd = (struct mr_lassen_ext_rxd *)dst->ext_data;

			rxd->ipsec_metadata.status = status;
			rxd->ipsec_metadata.ipsec_handle = ipsec_handle;
			dst->dl += (pkt_len - original_pkt_len);
			/* Do not change the output descriptor type to NOA_DESC_VPN_RX because
			 * the descriptor type is NOA_DESC_MODEM_LASSEN. The associated ext_data
			 * type already supports ipsec metadata.
			 */
#endif
		}

		// FIXME:
		dst->reason = FWD_REASON_FALLBACK;
		dst->cp = NOAD_NO_COPY;
		dst->dst = net_engine_fallback_route(src->src);
		// disable fk to MD to mitigate ring operations
		dst->fk = NOAD_FEEDBACK_DISABLE;
		net_engine_offload_util_cnt_inc(true, true);
	}
	// Pseudo Code for Tethering:
	// FIXME: when doing routing algo.
	/*
	 * if (routing decision to APC) {
	 *   dst->reason = FWD_REASON_FALLBACK;
	 *   dst->cp = NOAD_NO_COPY;
	 *   dst->dst = NOA_PORT_MODEM_SW;
	 *   // or dst->dst = net_engine_fallback_route(src->src);
	 * } else (routing decision to Wi-Fi) { // tethering path
	 *   dst->reason = FWD_REASON_NETENGINE;
	 *   dst->cp = NOAD_COPY_DATA_AND_RENEW_TKID;
	 *   dst->fk = NOAD_FEEDBACK_ENABLE;
	 *   dst->dst = NOA_PORT_WLAN_FW;
	 * }
	 */
	/*
	 * desc src should not be replaced to net engine due to the src port
	 * will be used for sending feedback event.
	 */
	dst->ddone = 0;
	noa_sim_get()->nep_stat.reason[dst->reason]++;
	return 0;
}

// This value is sync'ed from cpif/link_tx_pktproc.h.
// TODO(b/338502042): Remove it if there's a better way to know the padding size of packets
// from modem_sw.
#define CP_PADDING 76

#define BRCM_TX_L2_HEADER_SIZE (ETH_HLEN + DOT11_LLC_SNAP_HDR_LEN)

static int net_engine_apc_pkt_handle(const struct noa_desc *src, struct noa_desc *dst,
				     uint8_t interface)
{
	struct noa_simulator *sim = noa_sim_get();
	unsigned char *l2_pkt;
	unsigned char *pkt;
	int pkt_len;
	int original_pkt_len;
	u32 ipsec_handle = 0;
	bool unwanted_packet = false;
	u32 if_id = 0;

	l2_pkt = (unsigned char *)src->dv + src->head_offset;
	pkt = l2_pkt;
	pkt_len = src->dl - src->head_offset;

	if (interface == kNoaNetworkInterfaceWlan) {
		uint16_t *h_proto;

		pkt += BRCM_TX_L2_HEADER_SIZE;
		pkt_len -= BRCM_TX_L2_HEADER_SIZE;

		h_proto = (uint16_t *)(pkt - 2);
		unwanted_packet = (*h_proto) != htons(ETH_P_IPV6) && (*h_proto) != htons(ETH_P_IP);
		if_id = ((struct noa_bcm_txd *)src->ext_data)->ipsec_metadata.xfrm_interface_id;
	}

	if (interface == kNoaNetworkInterfaceModem) {
		// TODO: Find a better way to know the padding size.
		pkt_len -= CP_PADDING;
		if_id = ((struct mr_lassen_ext_txd *)src->ext_data)->
			ipsec_metadata.xfrm_interface_id;
	}
	original_pkt_len = pkt_len;

	if (pkt_len < (int)IP_HEADER_SIZE) {
		// The data carried by the noa_desc is too short.
		// Let it bypass VPN engine without modification.
		pr_warn("%s: Received packet too short. Ignored.", __func__);
		return 0;
	}

	if (sim->dbg)
		hexdump("packet(out):", pkt, pkt_len);

	if (!unwanted_packet) {
		const int status =
			process_pkt_for_vpn(pkt, &pkt_len, PACKET_OUT, &ipsec_handle, if_id);
		if (status != IPSEC_METADATA_STATUS_SKIP) {
			// Update the dl field based on the packet size difference between the
			// unencrypted/encrypted packet.
			dst->dl += (pkt_len - original_pkt_len);

			if (interface == kNoaNetworkInterfaceWlan)
				update_wifi_tx_l2_header_for_vpn(l2_pkt, pkt_len);
		}
	}
	dst->reason = FWD_REASON_VPN;

	switch (interface) {
	case kNoaNetworkInterfaceWlan:
		dst->dst = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
						kNoaNetworkFlowHostToDevice, kNoaWlanRingTxData);
		break;
	case kNoaNetworkInterfaceModem:
		dst->dst = NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
						kNoaNetworkFlowHostToDevice, kNoaModemRingTxData);
		break;
	default:
		return -EINVAL;
	}

	// TODO(b/321875336): Leave dst->reason unchanged for now because none of
	// FWD_REASON_* is suitable for this case. Consider defining a new reason.
	// Besides, all other fields don't seem to be functional-critical, leave them
	// unchanged for now.

	return 0;
}

static int net_engine_pkt_handle(const struct noa_desc *in, struct noa_desc *out)
{
	uint8_t interface;
	uint8_t flow;
	uint8_t category;

	NoaRingPathIdParse(in->src, &interface, &flow, &category);
	/* copy src to desc first */
	memcpy(out, in, noa_desc_bytes(noa_desc_type_parse(in)));

	switch (flow) {
	case kNoaNetworkFlowDeviceToHost:
		if (interface == kNoaNetworkInterfaceWlan) {
			return net_engine_wifi_pkt_handle(in, out);
		} else if (interface == kNoaNetworkInterfaceModem) {
			return net_engine_md_pkt_handle(in, out);
		}
		break;
	case kNoaNetworkFlowHostToDevice:
		return net_engine_apc_pkt_handle(in, out, interface);
	default:
		break;
	}
	pr_err("Unsupported port(%d)\n", in->src);
	return -EINVAL;
}

static void trigger_doorbell(struct noa_ring_wrapper *ring)
{
	/* Doorbell DMA scheduler */
	if (noa_ring_pos_is_moved(ring))
		notify_ring_manager(NOA_PORT_NETENGINE);
}

static int net_engine_fifo_in_process(struct netengine_info *neteng, const void *buf,
				      size_t buf_len)
{
	noa_ring_producer *ring = &neteng->tx_ring.ring;
	int ret;

	ret = noa_ring_begin_processing(ring);
	if (ret < 0) {
		return ret;
	} else if (!ret) {
		dev_err(dev_netengine,
			"Ring %s is already in processing state, something may went wrong, "
			"because it is only allowed one thread to operate this ring.\n",
			ring->name);
		return -EINVAL;
	}

	ret = noa_ring_write_variable_length(ring, buf, buf_len);
	if (ret < 0 && ret != -EAGAIN)
		dev_err(dev_netengine, "Failed to write tx packet to NOA wlan fw input ring\n");

	noa_ring_complete_processing(ring);
	return 0;
}

#define NETENGINE_THROTTLING_THRESHOLD ((8U))

static bool should_throttling(struct netengine_info *neteng)
{
	struct noa_ring_wrapper *output = &neteng->tx_ring.ring;
	int free_items = noa_ring_free_items_count(
		output->basic.head, noa_ring_tail_read_once(output), output->basic.size);
	if (free_items <= NETENGINE_THROTTLING_THRESHOLD) {
		return true;
	}
	return false;
}

static int net_engine_fifo_out_process(struct netengine_info *neteng)
{
	int ret;
	unsigned long data_addr = 0;
	noa_ring_consumer *ring = &neteng->rx_ring.ring;
	char buffer[NOA_DESC_MAX_BYTE] = { 0 };

	if (should_throttling(neteng)) {
		ret = 0;
		goto out;
	}

	ret = noa_ring_begin_processing(ring);
	if (ret < 0) {
		dev_err(dev_netengine, "Ring %s is in invalid status, err: %d\n", ring->name, ret);
		goto out;
	} else if (!ret) {
		dev_err(dev_netengine,
			"Ring %s is already in processing state, something may went wrong, "
			"because it is only allowed one thread to operate this ring.\n",
			ring->name);
		ret = -EINVAL;
		goto out;
	}

	ret = noa_ring_read_variable_length(ring, &data_addr, sizeof(data_addr), true);
	if (!ret) {
		goto complete;
	} else if (ret < 0 || !data_addr) {
		dev_err(dev_netengine,
			"Failed to read data from net engine ring, drop this item, err: %d\n", ret);
		noa_ring_tail_inc(ring);
		goto complete;
	}

	ret = net_engine_pkt_handle((struct noa_desc *)data_addr, (struct noa_desc *)buffer);
	if (ret) {
		dev_err(dev_netengine,
			"Failed to handle data in netengine, drop this data, err: %d\n", ret);
		noa_ring_tail_inc(ring);
		goto complete;
	}

	ret = net_engine_fifo_in_process(
		neteng, buffer, noa_desc_bytes(noa_desc_type_parse((struct noa_desc *)buffer)));
	if (ret)
		dev_err(dev_netengine, "Failed to handle read data from net engine ring, err: %d\n",
			ret);

complete:
	noa_ring_complete_processing(ring);

out:
	/* reschedule */
	if (!noa_ring_is_empty(ring))
		notify_port(NOA_PORT_NETENGINE);
	return ret;
}

static irqreturn_t net_engine_isr(int irq, void *data)
{
	struct netengine_info *neteng = (struct netengine_info *)data;
	/* clear ints */
	neteng->port->ints = 0;
	net_engine_fifo_out_process(neteng);
	return IRQ_HANDLED;
}

static void unregister_neteng_ring(struct netengine_ring *net_ring)
{
	struct noa_ring_wrapper *ring = &net_ring->ring;

	noa_ring_deactivate(ring);
	noa_ring_info_clean(ring);
	kfree(net_ring->buffer);
	net_ring->buffer = NULL;
}

static int register_neteng_ring(struct netengine_info *neteng, struct netengine_ring *net_ring,
				int ring_type, const uint8_t direction_of_ring,
				const struct noa_ring_ops *ops, const char *name)
{
	int ret;
	struct noa_ring_regs regs = { 0 };
	struct noa_ring_info info = {
		.head = 0,
		.tail = 0,
		.size = NET_ENGINE_RING_SIZE,
		.item_len = NOA_DESC_MAX_BYTE,
	};

	ret = NoaRingSharedRegsGet(&regs, kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel,
				   kNoaNetengineRingData, direction_of_ring);
	if (ret)
		goto out;

	ret = noa_ring_regs_wrapper_init(&net_ring->ring, ring_type, ops, &regs, neteng, name, 0);
	if (ret)
		goto out;

	net_ring->buffer = nmalloc(info.item_len * info.size, GFB_DRAM_COHERENCE);
	if (!net_ring->buffer) {
		ret = -ENOMEM;
		goto out;
	}
	net_ring->ring.wrap_buf = &net_ring->static_wrap_buf[0];
	net_ring->ring.wrap_buf_sz = sizeof(net_ring->static_wrap_buf);
	info.base = net_ring->buffer;
	info.dpa_base = info.base;
	noa_ring_info_setup(&net_ring->ring, &info);
	noa_ring_activate(&net_ring->ring);
	ret = 0;

out:
	if (ret)
		unregister_neteng_ring(net_ring);
	return ret;
}

static ssize_t refill_buffer(void *buf, size_t buf_len, const void *data, size_t data_len)
{
	*((noa_buffer_pool_desc *)buf) = *(noa_buffer_pool_desc *)data;
	return sizeof(u16);
}

const static struct noa_ring_ops pool_ring_ops = {
	.write_payload = refill_buffer,
};

static void deinit_buffer_pool(struct netengine_info *neteng)
{
	const u8 buffer_pool_ring_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel, kNoaNetengineBufferPool);
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, false, buffer_pool_ring_id,
				      kNoaRingNepInput);
	noa_ring_deactivate(&neteng->pool_ring);
	noa_ring_info_clean(&neteng->pool_ring);
}

static int init_buffer_pool(struct netengine_info *neteng)
{
	const u8 buffer_pool_ring_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel, kNoaNetengineBufferPool);
	int i = 0;
	int ret = -1;
	u16 tkid = 0;
	struct noa_ring_regs ring_regs = { 0 };
	struct noa_ring_info info = {
		.head = NET_ENGINE_RING_SIZE - 1,
		.tail = 0,
		.base = (char *)&g_pool_ring_buf[0],
		.size = NET_ENGINE_RING_SIZE,
		.item_len = sizeof(noa_buffer_pool_desc),
		.dpa_base = info.base,
	};

	for (i = 0; i < NET_ENGINE_RING_SIZE - 1; i++) {
		unsigned long addr;
		noa_buffer_pool_desc *item =
			(noa_buffer_pool_desc *)noa_ring_buf_pos(info.base, i, info.item_len);
		tkid = get_ping_pong_buffer_tkid(i);
		addr = (unsigned long)&neteng_ping_pong_buffers[tkid][0];
		item->tkid = tkid;
		item->dp_high = (addr >> 32U) & 0xFFFFFFFF;
		item->dp_low = addr & 0xFFFFFFFF;
		item->dv = addr;
	}

	ret = NoaRingSharedRegsGet(&ring_regs, kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel,
				   kNoaNetengineBufferPool, kNoaRingNepInput);
	if (ret) {
		dev_err(dev_netengine, "Failed to get buffer pool regs, ret %d\n", ret);
		goto out;
	}

	ret = noa_ring_regs_wrapper_init(&neteng->pool_ring, NOA_RING_TYPE_PRODUCER, &pool_ring_ops,
					 &ring_regs, neteng, "neteng buf", 0);
	if (ret) {
		dev_err(dev_netengine, "Failed to init neteng buf ring, ret %d\n", ret);
		goto out;
	}

	noa_ring_info_setup(&neteng->pool_ring, &info);
	noa_ring_activate(&neteng->pool_ring);
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true, buffer_pool_ring_id,
				      kNoaRingNepInput);
	ret = 0;
out:
	if (ret) {
		deinit_buffer_pool(neteng);
	}
	return ret;
}

/*
 * Due to our current inability to recycle buffers in the network
 * engine (since feedback is only valid for other drivers), using
 * "ping-pong" buffers is one of viable options. So, We have sized
 * the buffer pool to exactly match the combined size of the input
 * and output rings. This allows us to handle the worst-case scenario
 * where both rings are full.  Each time the network engine receives
 * descriptors from the input ring, it consumes buffers from the pool.
 * To ensure the pool is replenished, we immediately add "ping-pong"
 * buffers until the pool ring is full.
 */
static void replenish_ping_pong_buffer(struct noa_ring_wrapper *ring)
{
	int ret = -1;
	struct netengine_info *neteng = (struct netengine_info *)ring->owner;
	struct noa_ring_wrapper *pool_ring = &neteng->pool_ring;
	u32 curr = 0;
	ret = noa_ring_begin_processing(pool_ring);
	if (ret <= 0) {
		dev_err(dev_netengine, "Ring %s is not ready, ret %d\n", pool_ring->name, ret);
		return;
	}
	curr = noa_ring_head_read_once(pool_ring);
	while (!noa_ring_is_full(pool_ring)) {
		unsigned long addr;
		noa_buffer_pool_desc item;
		u16 tkid = get_ping_pong_buffer_tkid((u16)curr);
		addr = (unsigned long)&neteng_ping_pong_buffers[tkid][0];
		item.tkid = tkid;
		item.dp_high = (addr >> 32U) & 0xFFFFFFFF;
		item.dp_low = addr & 0xFFFFFFFF;
		item.dv = addr;

		ret = noa_ring_write(pool_ring, &item, sizeof(item));
		if (ret < 0) {
			dev_err(dev_netengine, "Failed to replenish netengine pool, ret %d\n", ret);
			break;
		}
		curr = (curr + 1) % pool_ring->basic.size;
	}
	noa_ring_complete_processing(pool_ring);
}

static ssize_t copy_buffer(void *buf, size_t buf_len, const void *data, size_t data_len)
{
	if (buf_len < data_len)
		return -EINVAL;
	memcpy(buf, data, data_len);
	return data_len;
}

const static struct noa_ring_ops neteng_tx_ring_ops = {
	.write_payload = copy_buffer,
	.complete_hook = trigger_doorbell,
	.fill_noop = noa_ring_manager_noop_payload,
};

const static struct noa_ring_ops neteng_rx_ring_ops = {
	.read_payload = noa_generic_read_raw_pointer,
	.complete_prehook = replenish_ping_pong_buffer,
	.payload_len = noa_ring_manager_payload_parser,
};

int net_engine_init(void *data)
{
	int ret;
	struct noa_simulator *sim = (struct noa_simulator *)data;
	struct netengine_info *neteng = &g_netengine;

	neteng->port = &sim->ports[NOA_PORT_NETENGINE];
	sim->session_entries = session_entries;
	dev_netengine = &sim->dev;

	ret = register_neteng_ring(neteng, &neteng->rx_ring, NOA_RING_TYPE_CONSUMER,
				   kNoaRingNepOutput, &neteng_rx_ring_ops, "neteng rx");
	if (ret) {
		dev_err(dev_netengine, "Failed to init RX ring for netengine.\n");
		goto out;
	}

	ret = register_neteng_ring(neteng, &neteng->tx_ring, NOA_RING_TYPE_PRODUCER,
				   kNoaRingNepInput, &neteng_tx_ring_ops, "neteng tx");
	if (ret) {
		dev_err(dev_netengine, "Failed to init TX ring for netengine.\n");
		goto out;
	}

	ret = init_buffer_pool(neteng);
	if (ret) {
		goto out;
	}
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceNetengine,
							   kNoaNetengineTunnel,
							   kNoaNetengineRingData),
				      kNoaRingNepInput);
	;
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceNetengine,
							   kNoaNetengineTunnel,
							   kNoaNetengineRingData),
				      kNoaRingNepOutput);
	;

	ret = noa_interrupt_register(neteng->port->irq, net_engine_isr, (void *)neteng);
	if (ret) {
		dev_err(dev_netengine, "Failed to register interrupt for netengine.\n");
		goto out;
	}

	ret = 0;
out:
	if (ret)
		net_engine_exit();
	return ret;
}

void net_engine_exit(void)
{
	struct netengine_info *neteng = &g_netengine;
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, false,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceNetengine,
							   kNoaNetengineTunnel,
							   kNoaNetengineRingData),
				      kNoaRingNepInput);
	;
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, false,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceNetengine,
							   kNoaNetengineTunnel,
							   kNoaNetengineRingData),
				      kNoaRingNepOutput);
	;
	unregister_neteng_ring(&neteng->tx_ring);
	unregister_neteng_ring(&neteng->rx_ring);
	deinit_buffer_pool(neteng);
}

int noa_sim_add_session_entry(struct noa_session *entry)
{
	struct noa_session *session = find_session(entry);

	if (session) {
		trace_nep_add_session_entry(session);
		/* update ageout */
		session->common.ageout = 30;
		return 0;
	}
	session = get_free_session();
	if (!session) {
		dev_err(dev_netengine, "%s(): request a new entry fail.\n", __func__);
		return -ENOMEM;
	}
	memcpy(session, entry, sizeof(struct noa_session));
	dump_session(session);
	dev_err(dev_netengine, "%s(): request a new entry %p success.\n",
		__func__, session);
	return 0;
}
EXPORT_SYMBOL_GPL(noa_sim_add_session_entry);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Star Chang <starchang@google.com>");
MODULE_DESCRIPTION("NEP Firmware Simualtor Driver");
