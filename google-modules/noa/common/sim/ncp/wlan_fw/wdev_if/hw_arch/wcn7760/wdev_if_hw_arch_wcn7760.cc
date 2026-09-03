#include "wdev_if_hw_arch_wcn7760.h"

#include "wlan_cast.h"
#include "wdev_if/wdev_if.h"
#include "noa_desc.h"
#include "wcn7760_msgbuf.h"
#include "wlan_log/wlan_log.h"
#include "wlan_debug_controller/wlan_packet_sniffer/wlan_debug_packet_sniffer.h"
#include "modules/wlan_buffer_management/wlan_buffer_manager.h"

#ifndef WLAN_PKT_PAD
#define WLAN_PKT_PAD (60U)
#endif

#define QCA_BE_SW_COOKIE_MASK      0xfffff  /* 20 bits for packet index */
#define QCA_BE_RBM_SW0             5        /* TCL 0 Release Ring */
#define QCA_BE_RBM_SW1             6        /* RXDMA/RX Refill Release Ring */
#define QCA_BE_TCL_DATA_CMD_NUM    1025     /* Standard Data Command */
#define QCA_BE_TCL_CMD_TYPE_DATA   0
#define QCA_BE_TX_NOTIFY_NONE      0

#if IS_ENABLED(CONFIG_NOA_WLAN_PACKET_SNIFFER_SUPPORT)
static void WdevDumpTxPostDescQca(void *desc)
{
	struct tcl_data_cmd *cmd = WLAN_STATIC_CAST(struct tcl_data_cmd *, desc);
	WLAN_LOG_INFO(Wdev, "WCN TX: addr=0x%x_%x, len=%d, tid=%d, vdev=%d, rbm=%d, bank_id=%d",
		      cmd->buf_addr_info.buffer_addr_39_32,
		      cmd->buf_addr_info.buffer_addr_31_0,
		      cmd->data_length, cmd->hlos_tid, cmd->vdev_id,
		      cmd->buf_addr_info.return_buffer_manager, cmd->bank_id);
}
#endif

int32_t WdevArchGetTxFlowRingIdWcn7760(WdevIf *const wdev_if, const void *const wlan_ext_txd,
				    uint32_t *ring_id)
{
	if (!wlan_ext_txd || !wdev_if || !ring_id) {
		return -EINVAL;
	}

	*ring_id = 0; //Currently only tx ring 0 is mapped
	return 0;
}

static int32_t ComposeWcnTxPostDesc(WdevIf *const wdev_if, const NoaDesc *noa_desc,
				     const NoaQcaTxD *qca_txd, uint32_t wdev_desc_buf_size,
				     void *const wdev_desc)
{
	struct tcl_data_cmd *cmd = WLAN_STATIC_CAST(struct tcl_data_cmd *, wdev_desc);

	if (wdev_desc_buf_size < sizeof(struct tcl_data_cmd)) {
		return -EINVAL;
	}

	memset(cmd, 0, sizeof(struct tcl_data_cmd));

	/*  Map Data Buffer Address (64-bit) */
	cmd->buf_addr_info.buffer_addr_31_0 = noa_desc->dp_low + noa_desc->head_offset;
	cmd->buf_addr_info.buffer_addr_39_32 = (uint32_t)(noa_desc->dp_high & 0xFF);

	/* Map Buffer Metadata (Cookie/Tkid) */
	cmd->buf_addr_info.return_buffer_manager = QCA_BE_RBM_SW0;
	cmd->buf_addr_info.sw_buffer_cookie = noa_desc->tkid;

	/* Map Transmission Control Flags */
	cmd->buf_or_ext_desc_type = qca_txd->frag & 0x01;
	cmd->bank_id = qca_txd->bank_id;
	cmd->tx_notify_frame = qca_txd->tx_notify_frame;

	/* Map Command Metadata */
	cmd->tcl_cmd_number = qca_txd->fw_metadata;

	/* Map Length and Checksum Offload */
	cmd->data_length = (uint16_t)noa_desc->dl - noa_desc->head_offset;
	cmd->ipv4_checksum_en = qca_txd->l3_checksum_en;
	if (qca_txd->l4_checksum_en) {
		cmd->udp_over_ipv4_checksum_en = 1;
		cmd->udp_over_ipv6_checksum_en = 1;
		cmd->tcp_over_ipv4_checksum_en = 1;
		cmd->tcp_over_ipv6_checksum_en = 1;
	}

	cmd->to_fw = qca_txd->to_fw;

	/* Map Priority and Routing */
	cmd->hlos_tid_overwrite = 1;
	cmd->hlos_tid = qca_txd->set_hlos_tid;
	cmd->vdev_id = qca_txd->vdev_id;
	cmd->pmac_id = qca_txd->pmac_id;

	/* Map Peer Lookup Context */
	cmd->search_index = qca_txd->search_index;
	cmd->cache_set_num = 0;
	cmd->cache_set_num = qca_txd->cache_set_num;

	/* Ring Identification */
	cmd->ring_id = 0;

	WLAN_PACKET_SNIFF(noa_desc->dv + noa_desc->head_offset, noa_desc->dl,
			  WLAN_REINTERPRET_CAST(void *, cmd), WdevDumpTxPostDescQca, NoaHwTx);

	return 0;
}

int32_t WdevIfPrepareNoaWlanExtendTxDWcn7760(struct WdevIf *const wdev_if,
					const NoaDesc *const noa_desc,
					const StaInfo *const sta_info, NoaWlanExtendTxD *ext_txd)
{
	NoaQcaTxD *qca_txd = WLAN_REINTERPRET_CAST(NoaQcaTxD *, ext_txd);

	if (!wdev_if || !noa_desc || !sta_info || !ext_txd) {
		return -EINVAL;
	}

	if (noa_desc->desc_type == NOA_DESC_NETENG_PKT_FLOW) {
		const NoaNetworkTxD *nwk_txd =
			WLAN_REINTERPRET_CAST(const NoaNetworkTxD *, noa_desc->ext_data);

		qca_txd->l3_checksum_en = nwk_txd->info.ipv4 || nwk_txd->info.ipv6;
		qca_txd->l4_checksum_en = nwk_txd->info.tcp || nwk_txd->info.udp;
		qca_txd->set_hlos_tid = nwk_txd->info.priority;
		qca_txd->to_fw = 0;
		qca_txd->ring_id = 0;
		qca_txd->bmid = sta_info->bmid; //Not required if using single TX ring, else use it for return_buffer_manager
		qca_txd->frag = 0;
		qca_txd->search_index = sta_info->search_idx;
		qca_txd->cache_set_num = 0;  //TODO: to be filled with ast_hash
		qca_txd->bank_id = (uint8_t)sta_info->dscp_tid_map_id;
		qca_txd->vdev_id = (uint8_t)sta_info->bss_idx;
		qca_txd->tx_notify_frame = 0;
		qca_txd->fw_metadata = (uint16_t)sta_info->fw_metadata;

	} else if (noa_desc->desc_type == NOA_DESC_WLAN_TX_QCA) {
		memcpy(qca_txd, noa_desc->ext_data, sizeof(NoaQcaTxD));
	} else {
		return -EINVAL;
	}

	return 0;
}

int32_t WdevArchPrepareTxPostDescWcn7760(WdevIf *const wdev_if, const NoaDesc *const noa_desc,
				      const void *const wlan_ext_txd, uint32_t wdev_desc_buf_size,
				      WdevPostDescCoherenceInfo *const UNUSED(coherence_info),
				      void *const wdev_desc)
{
	const NoaQcaTxD *qca_txd = WLAN_REINTERPRET_CAST(const NoaQcaTxD *, wlan_ext_txd);

	if (!wdev_if || !noa_desc || !wlan_ext_txd || !wdev_desc) {
		return -EINVAL;
	}

	if (noa_desc->desc_type != NOA_DESC_WLAN_TX_QCA &&
	    noa_desc->desc_type != NOA_DESC_NETENG_PKT_FLOW) {
		WLAN_LOG_WARN(Tx, "%s(): unsupported desc_type %" PRIi32, __func__,
			      noa_desc->desc_type);
		return -EINVAL;
	}

	return ComposeWcnTxPostDesc(wdev_if, noa_desc, qca_txd, wdev_desc_buf_size, wdev_desc);

}

int32_t WdevArchPrepareRxPostDescWcn7760(WdevIf *const wdev_if, uint16_t tkid,
				      uint32_t wdev_desc_buf_size,
				      WdevPostDescCoherenceInfo *const UNUSED(coherence_info),
				      void *const wdev_desc)
{
	struct buffer_addr_info *addr_info = WLAN_STATIC_CAST(struct buffer_addr_info *, wdev_desc);
	const BmTkidItem *tkid_item;
	uint32_t ppt_index;
	uint32_t spt_index;
	uint32_t cookie;
	uint64_t pa;

	if (wdev_desc_buf_size < sizeof(struct buffer_addr_info) || !wdev_if || !wdev_desc) {
		return -EINVAL;
	}

	if (BmFind(kVendorRxBufferManager, tkid, &tkid_item) != 0) {
		return -EINVAL;
	}

	ppt_index = tkid_item->tkid >> 9;
	spt_index = tkid_item->tkid & 0x1FF;
	cookie = ((ppt_index + wdev_if->cookie_base_addr) << 9) | spt_index;
	memset(addr_info, 0, sizeof(struct buffer_addr_info));
	pa = tkid_item->pa + WLAN_PKT_PAD;
	addr_info->buffer_addr_31_0 = (uint32_t)(pa & 0xffffffff);
	addr_info->buffer_addr_39_32 = (uint32_t)((pa >> 32) & 0xff);
	addr_info->return_buffer_manager = QCA_BE_RBM_SW1;
	addr_info->sw_buffer_cookie = (uint32_t)(cookie & QCA_BE_SW_COOKIE_MASK);

	return 0;
}

int32_t WdevArchProcessTxCplDescWcn7760(WdevIf *const wdev_if, const void *const wdev_desc,
				     WdevCmplDescCoherenceInfo *const coherence_info,
				     WdevTxCmplDescriptorInfo *desc_info)
{
	const struct wbm_release_ring *desc = WLAN_STATIC_CAST(const struct wbm_release_ring *, wdev_desc);

	if (!wdev_if || !desc_info || !wdev_desc) {
		return -EINVAL;
	}

	desc_info->pktid = desc->released_buff_or_desc_addr_info.sw_buffer_cookie & QCA_BE_SW_COOKIE_MASK;

	if (desc->release_source_module > 3) {
		return -EFAULT;
	}

	return 0;
}

int32_t WdevArchProcessRxCplDescWcn7760(WdevIf *const wdev_if, const void *const wdev_desc,
				     WdevCmplDescCoherenceInfo *const coherence_info,
				     WdevRxCmplDescriptorInfo *const desc_info)
{
	const struct reo_destination_ring *desc = WLAN_STATIC_CAST(const struct reo_destination_ring *, wdev_desc);
	uint32_t ppt_index;
	uint32_t spt_index;

	if (!wdev_if || !wdev_desc || !desc_info) {
		return -EINVAL;
	}

	memset(desc_info, 0, sizeof(WdevRxCmplDescriptorInfo));

	/* Map Packet ID from Cookie (20 bits for Beryllium) */
	ppt_index = (desc->buf_or_link_desc_addr_info.sw_buffer_cookie & QCA_BE_SW_COOKIE_MASK) >> 9;
	spt_index = (desc->buf_or_link_desc_addr_info.sw_buffer_cookie & QCA_BE_SW_COOKIE_MASK) & 0x1FF;
	desc_info->pktid = ((ppt_index - wdev_if->cookie_base_addr) << 9) | spt_index;
	/* Determine Head Offset (Dynamic L2 padding logic) */
	const BmTkidItem *tkid_item;
	uint32_t l2_hdr_offset = 0;
	uint32_t vdev_id = 0xff;

	if (BmFind(kVendorRxBufferManager, desc_info->pktid, &tkid_item) == 0) {

		uint32_t *pkt_tlv = (uint32_t *)(tkid_item->va + WLAN_PKT_PAD);
		uint32_t word_idx = 4; //following rx_msdu_end_compact
		//uint32_t word_idx = (RX_MSDU_END_L3_HEADER_PADDING_OFFSET >> 2) + RX_MSDU_END_TAG_WORD;
		l2_hdr_offset = (pkt_tlv[word_idx] & RX_MSDU_END_L3_HEADER_PADDING_MASK) >> RX_MSDU_END_L3_HEADER_PADDING_LSB;
		//word_idx = (NUM_OF_DWORDS_RX_MSDU_END + RX_MSDU_END_TAG_WORD + RX_BE_PADDING0_BYTES + RX_MPDU_START_TAG_WORD) +
		//	   (RX_MPDU_INFO_VDEV_ID_OFFSET >> 2);
		//vdev_id = pkt_tlv[word_idx] & RX_MPDU_INFO_VDEV_ID_MASK;
		vdev_id = 0;
	}

	/* Runtime TLV size passing */
	desc_info->head_offset = l2_hdr_offset + wdev_if->rx_pkt_tlv_size;

	/* Map Total Length (Hardware MSDU length + the prepended metadata) */
	desc_info->data_len = desc->rx_msdu_desc_info_details.msdu_length + desc_info->head_offset;

	/* Dynamic BSS Index Mapping */
	desc_info->bss_idx = vdev_id;

	if (desc->rx_mpdu_desc_info_details.raw_mpdu ||
	    !(desc->rx_msdu_desc_info_details.sa_is_valid)) {
		desc_info->flags |= kWdevPacketFlag802dot11;
	}

	return 0;
}
