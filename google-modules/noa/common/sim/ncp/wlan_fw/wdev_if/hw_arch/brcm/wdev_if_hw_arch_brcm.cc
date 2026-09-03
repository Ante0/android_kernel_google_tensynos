#include "wdev_if_hw_arch_brcm.h"

#include "sys_if/types/types.h"
#include "sys_if/common.h"
#include "sys_if/memory/sys_if_memory.h"
#include "wlan_cast.h"
#include "wdev_if/wdev_if.h"
#include "noa_desc.h"
#include "brcm_msgbuf.h"
#include "wlan_log/wlan_log.h"
#include "wlan_debug_controller/wlan_packet_sniffer/wlan_debug_packet_sniffer.h"
#include "modules/wlan_buffer_management/wlan_buffer_manager.h"

#define BRCM_FLOW_ID_RING_ID_OFFSET 0
#ifndef WLAN_PKT_PAD
#define WLAN_PKT_PAD (60U)
#endif

#if IS_ENABLED(CONFIG_NOA_WLAN_PACKET_SNIFFER_SUPPORT)
static void WdevDumpTxPostDesc(void *desc)
{
	HostTxbufPostV1 *tx_post = WLAN_STATIC_CAST(HostTxbufPostV1 *, desc);
	WLAN_LOG_INFO(Wdev, "txhdr: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
		      tx_post->txhdr[0], tx_post->txhdr[1], tx_post->txhdr[2], tx_post->txhdr[3],
		      tx_post->txhdr[4], tx_post->txhdr[5], tx_post->txhdr[6], tx_post->txhdr[7],
		      tx_post->txhdr[8], tx_post->txhdr[9], tx_post->txhdr[10], tx_post->txhdr[11]);
	WLAN_LOG_INFO(Wdev, "flags: %d", tx_post->flags);
	WLAN_LOG_INFO(Wdev, "data_buf_addr: 0x%llx, data_len: %d", tx_post->data_buf_addr,
		      tx_post->data_len);
	WLAN_LOG_INFO(Wdev, "cmn_hdr request_id: %d, flags: %d, if_id: %d",
		      tx_post->cmn_hdr.request_id, tx_post->cmn_hdr.flags, tx_post->cmn_hdr.if_id);
}
#endif /* CONFIG_NOA_WLAN_PACKET_SNIFFER_SUPPORT */

int32_t WdevArchGetTxFlowRingIdBrcm(WdevIf *const wdev_if, const void *const wlan_ext_txd,
				    uint32_t *ring_id)
{
	const NoaBrcmTxD *brcm_txd = WLAN_REINTERPRET_CAST(const NoaBrcmTxD *, wlan_ext_txd);

	if (!wlan_ext_txd || !wdev_if || !ring_id) {
		return -EINVAL;
	}

	*ring_id = brcm_txd->ring_id;

	return 0;
}

static bool IsCfoSupported(WdevIf *const wdev_if)
{
	return wdev_if->arch_type == kWlanDeviceArchTypeBrcmV1;
}

static int32_t ComposeBrcmTxPostDesc(WdevIf *const wdev_if, const NoaDesc *noa_desc,
				     const NoaBrcmTxD *brcm_txd, uint32_t wdev_desc_buf_size,
				     void *const wdev_desc)
{
	HostTxbufPostV1 *tx_post = WLAN_STATIC_CAST(HostTxbufPostV1 *, wdev_desc);

	if (IsCfoSupported(wdev_if) && wdev_desc_buf_size < sizeof(HostTxbufPostV2)) {
		return -EINVAL;
	} else if (wdev_desc_buf_size < sizeof(HostTxbufPostV1)) {
		return -EINVAL;
	}

	memset(tx_post, 0, sizeof(HostTxbufPostV1));
	tx_post->cmn_hdr.msg_type = kBrcmPcieMsgTypeTxPost;
	tx_post->seg_cnt = 1;
	tx_post->flags = brcm_txd->flags;
	// remove brcm_txd from dp and dl
	tx_post->data_buf_addr = noa_desc->dp_high;
	tx_post->data_buf_addr <<= 32;
	tx_post->data_buf_addr += noa_desc->dp_low;
	tx_post->data_buf_addr += noa_desc->head_offset;
	tx_post->data_len = noa_desc->dl - noa_desc->head_offset;
	tx_post->ext_flags = brcm_txd->ext_flags;
	tx_post->metadata_buf_len = 0;
	tx_post->metadata_buf_addr = 0;
	tx_post->cmn_hdr.request_id = noa_desc->tkid;
	tx_post->cmn_hdr.flags = brcm_txd->current_phase;
	tx_post->cmn_hdr.if_id = brcm_txd->ifidx;
	SysIfInvalidDCache(WLAN_STATIC_CAST(PhyAddr, noa_desc->dv), noa_desc->dl);
	memcpy(tx_post->txhdr,
	       WLAN_REINTERPRET_CAST(uint8_t *, noa_desc->dv + noa_desc->head_offset),
	       BCM_ETHER_HDR_LEN);
	*(WLAN_REINTERPRET_CAST(uint16_t *, &tx_post->txhdr[12])) = brcm_txd->ethertype;
	// CFO supported
	if (IsCfoSupported(wdev_if)) {
		HostTxbufPostV2 *tx_post_v2 = WLAN_STATIC_CAST(HostTxbufPostV2 *, wdev_desc);
		tx_post_v2->v2_ext.pkt_info_cso.ver = brcm_txd->ext_tag;
		tx_post_v2->v2_ext.pkt_info_cso.pkt_csum_type = brcm_txd->pkt_csum_type;
		tx_post_v2->v2_ext.pkt_info_cso.nwk_hdr_len = brcm_txd->l3_hdr_len;
		tx_post_v2->v2_ext.pkt_info_cso.trans_hdr_len = brcm_txd->l4_hdr_len;
	}

	if (SysIfIsDriverMode()) {
		// In driver simulator mode, we need to flush the data cache for
		// the WiFi device on the NCP.
		SysIfDmaSyncForDevice(WLAN_STATIC_CAST(PhyAddr, tx_post->data_buf_addr),
				      tx_post->data_len);
	}

#if defined(CONFIG_NOA_WLAN_FORECE_FLUSH_TX_PAYLOAD)
	SysIfFlushDCache(WLAN_STATIC_CAST(PhyAddr, noa_desc->dv), tx_post->data_len);
#endif

	WLAN_PACKET_SNIFF(noa_desc->dv + noa_desc->head_offset, tx_post->data_len,
			  WLAN_REINTERPRET_CAST(void *, tx_post), WdevDumpTxPostDesc, NoaHwTx);

	return 0;
}

int32_t WdevIfPrepareNoaWlanExtendTxDBrcm(struct WdevIf *const wdev_if,
					  const NoaDesc *const noa_desc,
					  const StaInfo *const sta_info, NoaWlanExtendTxD *ext_txd)
{
	if (!wdev_if || !noa_desc || !sta_info || !ext_txd) {
		return -EINVAL;
	}

	if (noa_desc->desc_type == NOA_DESC_NETENG_PKT_FLOW) {
		const NoaNetworkTxD *nwk_txd =
			WLAN_REINTERPRET_CAST(const NoaNetworkTxD *, noa_desc->ext_data);

		ext_txd->brcm_txd.flags = 0x11;
		ext_txd->brcm_txd.ext_flags = 0;
		ext_txd->brcm_txd.current_phase = BCMPCIE_FLOWRING_PHASE_NIBBLE_INIT;
		ext_txd->brcm_txd.ring_id = nwk_txd->info.flowid;
		ext_txd->brcm_txd.ethertype = htons(nwk_txd->info.ethertype);
		ext_txd->brcm_txd.l3_hdr_len = nwk_txd->info.l3_len;
		ext_txd->brcm_txd.l4_hdr_len = nwk_txd->info.l4_len;
		ext_txd->brcm_txd.ifidx = sta_info->bss_idx;

		if (nwk_txd->info.ipv6) {
			ext_txd->brcm_txd.pkt_csum_type |= kPktCsumTypeIpV6Mask;
		}
		if (nwk_txd->info.ipv4) {
			ext_txd->brcm_txd.pkt_csum_type |= kPktCsumTypeIpV4Mask;
		}
		if (nwk_txd->info.tcp) {
			ext_txd->brcm_txd.pkt_csum_type |= kPktCsumTypeTcpMask;
		}
		if (nwk_txd->info.udp) {
			ext_txd->brcm_txd.pkt_csum_type |= kPktCsumTypeUdpMask;
		}
		if (nwk_txd->info.tcp || nwk_txd->info.udp) {
			ext_txd->brcm_txd.pkt_csum_type |= kPktCsumTypeTransCsumMask;
		}
	} else if (noa_desc->desc_type == NOA_DESC_WLAN_TX_BRCM) {
		const NoaBrcmTxD *noa_brcm_txd =
			WLAN_REINTERPRET_CAST(const NoaBrcmTxD *, noa_desc->ext_data);
		memcpy(ext_txd, noa_brcm_txd, sizeof(NoaBrcmTxD));
	} else {
		WLAN_LOG_WARN(Wdev, "%s(): unknown desc_type: %" PRIu32, __func__,
			      noa_desc->desc_type);
		return -EINVAL;
	}

	return 0;
}

int32_t WdevArchPrepareTxPostDescBrcm(WdevIf *const wdev_if, const NoaDesc *const noa_desc,
				      const void *const wlan_ext_txd, uint32_t wdev_desc_buf_size,
				      WdevPostDescCoherenceInfo *const UNUSED(coherence_info),
				      void *const wdev_desc)
{
	const NoaBrcmTxD *brcm_txd = WLAN_REINTERPRET_CAST(const NoaBrcmTxD *, wlan_ext_txd);

	if (!wdev_if || !noa_desc || !wlan_ext_txd || !wdev_desc) {
		return -EINVAL;
	}

	if (noa_desc->desc_type != NOA_DESC_WLAN_TX_BRCM &&
	    noa_desc->desc_type != NOA_DESC_NETENG_PKT_FLOW) {
		WLAN_LOG_WARN(Tx, "%s(): unsupported desc_type %" PRIi32, __func__,
			      noa_desc->desc_type);
		return -EINVAL;
	}

	return ComposeBrcmTxPostDesc(wdev_if, noa_desc, brcm_txd, wdev_desc_buf_size, wdev_desc);
}

int32_t WdevArchPrepareRxPostDescBrcm(WdevIf *const wdev_if, uint16_t tkid,
				      uint32_t wdev_desc_buf_size,
				      WdevPostDescCoherenceInfo *const coherence_info,
				      void *const wdev_desc)
{
	HostRxbufPost *rxbuf_post = WLAN_STATIC_CAST(HostRxbufPost *, wdev_desc);
	const BmTkidItem *tkid_item;

	if (wdev_desc_buf_size < sizeof(HostRxbufPost) || !wdev_if || !coherence_info ||
	    !wdev_desc) {
		WLAN_LOG_WARN(Tx, "%s(): invalid input parameters.", __func__);
		return -EINVAL;
	}

	if (BmFind(kVendorRxBufferManager, tkid, &tkid_item) != 0) {
		return -EINVAL;
	}

	memset(rxbuf_post, 0, wdev_desc_buf_size);
	rxbuf_post->cmn_hdr.msg_type = kBrcmPcieMsgTypeRxPost;
	rxbuf_post->cmn_hdr.request_id = tkid_item->tkid;
	rxbuf_post->cmn_hdr.if_id = 0;
	rxbuf_post->cmn_hdr.flags = BCMPCIE_FLOWRING_PHASE_NIBBLE_INIT;
	rxbuf_post->data_buf_len = tkid_item->size - WLAN_PKT_PAD;
	rxbuf_post->data_buf_addr = tkid_item->pa + WLAN_PKT_PAD;

	if (wdev_if->post_desc_val_method == kWdevPostDescCoherenceValidationMethodBrcmSn) {
		rxbuf_post->cmn_hdr.epoch = coherence_info->brcm_sn_cks.current_sn;
		coherence_info->brcm_sn_cks.next_sn =
			(coherence_info->brcm_sn_cks.current_sn + 1) % H2D_EPOCH_MODULO;
	}

	return 0;
}

static uint32_t ComputeXor32(const uint32_t *val, int32_t nwords)
{
	int32_t idx;
	uint32_t xor32 = 0;

	for (idx = 0; idx < nwords; idx++) {
		xor32 ^= *(val + idx);
	}

	return xor32;
}

static int32_t BrcmCoherenceCheckSnCsum(const void *const wdev_desc,
					WdevCmplDescCoherenceInfo *const coherence_info)
{
	uint32_t retries;
	uint32_t checksum = 0;
	int32_t nwords;
	uint32_t step = 0;
	const volatile CmnMsgHdr *msg = WLAN_STATIC_CAST(const CmnMsgHdr *, wdev_desc);

	if (!coherence_info || !wdev_desc) {
		return -EINVAL;
	}

	nwords = coherence_info->brcm_sn_cks.ring_desc_size / sizeof(uint32_t);

	for (step = 1; step <= PCIE_D2H_SYNC_NUM_OF_STEPS; step++) {
		for (retries = 0; retries < PCIE_D2H_SYNC_WAIT_TRIES; retries++) {
			SysIfInvalidDCache(WLAN_REINTERPRET_CAST(PhyAddr, wdev_desc),
					   coherence_info->brcm_sn_cks.ring_desc_size);
			if (msg->epoch != coherence_info->brcm_sn_cks.current_sn)
				continue;
			checksum =
				ComputeXor32(WLAN_STATIC_CAST(const uint32_t *, wdev_desc), nwords);
			if (checksum != 0)
				continue;
			coherence_info->brcm_sn_cks.next_sn =
				(coherence_info->brcm_sn_cks.current_sn + 1) % D2H_EPOCH_MODULO;
			return 0;
		}
	}

	WLAN_LOG_ERROR(Rx,
		       "Checksum error and updated fail! (epoch: %" PRIi32 ", current_sn: %" PRIi32
		       ", checksum: %" PRIu32 ") pktid: %" PRIi32 ", desc_type %" PRIu32,
		       msg->epoch, coherence_info->brcm_sn_cks.current_sn, checksum,
		       msg->request_id, msg->msg_type);

	return -EINVAL;
}

static int32_t BrcmCmplDescCoherenceValidation(WdevIf *const wdev_if, const void *const wdev_desc,
					       WdevCmplDescCoherenceInfo *const coherence_info)
{
	switch (wdev_if->cmpl_desc_val_method) {
	case kWdevCmplDescCoherenceValidationMethodNone:
		return 0;
	case kWdevCmplDescCoherenceValidationMethodBrcmSnCsum:
		return BrcmCoherenceCheckSnCsum(wdev_desc, coherence_info);
	default:
		return -EINVAL;
	}
}

int32_t WdevArchProcessTxCplDescBrcm(WdevIf *const wdev_if, const void *const wdev_desc,
				     WdevCmplDescCoherenceInfo *const coherence_info,
				     WdevTxCmplDescriptorInfo *desc_info)
{
	const HostTxbufCmpl *tx_cmpl;

	if (!wdev_if || !desc_info || !wdev_desc) {
		WLAN_LOG_WARN(Rx, "%s(): invalid input parameters.", __func__);
		return -EINVAL;
	}

	if (BrcmCmplDescCoherenceValidation(wdev_if, wdev_desc, coherence_info) != 0) {
		WLAN_LOG_WARN(Rx, "%s(): rx coherence issue.", __func__);
		return -EINVAL;
	}

	tx_cmpl = WLAN_STATIC_CAST(const HostTxbufCmpl *, wdev_desc);
	desc_info->pktid = tx_cmpl->cmn_hdr.request_id;
	if (desc_info->pktid == 0) {
		WLAN_LOG_WARN(Rx, "%s(): invalid pktid %" PRIu16, __func__, desc_info->pktid);
		return -EINVAL;
	}

	return 0;
}

int32_t WdevArchProcessRxCplDescBrcm(WdevIf *const wdev_if, const void *const wdev_desc,
				     WdevCmplDescCoherenceInfo *const coherence_info,
				     WdevRxCmplDescriptorInfo *const desc_info)
{
	const HostRxbufCmpl *rx_cmpl;

	if (!wdev_if || !wdev_desc || !desc_info) {
		WLAN_LOG_WARN(Rx, "%s(): invalid input parameters.", __func__);
		return -EINVAL;
	}

	if (BrcmCmplDescCoherenceValidation(wdev_if, wdev_desc, coherence_info) != 0) {
		WLAN_LOG_WARN(Rx, "%s(): encounter rx coherence issue.", __func__);
		return -EINVAL;
	}

	rx_cmpl = WLAN_STATIC_CAST(const HostRxbufCmpl *, wdev_desc);

	memset(desc_info, 0, sizeof(WdevRxCmplDescriptorInfo));
	desc_info->pktid = rx_cmpl->cmn_hdr.request_id;
	desc_info->head_offset = rx_cmpl->data_offset;
	desc_info->data_len = rx_cmpl->data_len + rx_cmpl->data_offset;
	desc_info->bss_idx = rx_cmpl->cmn_hdr.if_id;
	// STA mode is always using feedthrough path.
	if (rx_cmpl->cmn_hdr.if_id == 0) {
		desc_info->flags |= kWdevPacketFlagStationMode;
	}
	// 802.11 packet is always using feedthrough mode.
	if (rx_cmpl->flags & BCMPCIE_PKT_FLAGS_FRAME_802_11) {
		desc_info->flags |= kWdevPacketFlag802dot11;
	}

	return 0;
}
