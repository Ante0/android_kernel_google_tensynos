// SPDX-License-Identifier: GPL-2.0-only
/*
 * NCP WiFi QCA Device Utility
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <common/core.h>
#include <common/noa_hw_ring.h>
#include <common/wlan/noa_wlan_qca.h>
#include "ncp_wlan_fw.h"
#include "buffer_manager.h"
#include "device.h"
#include "system_utility.h"

static struct qca_irq_ctrl{
	struct tasklet_struct rx_task;
	u32 status;
	spinlock_t lock;
	u32 irq_base;
} irq_ctrl;

#define DW_SIZE 4
static inline u16 noa_dma_get_write_count_q(struct noa_hw_ring *ring)
{
	u16 cnt;
	u16 dws_len = ring->desc_sz / DW_SIZE;

	if (!ring->regs.read || !ring->regs.write)
		return 0;

	ring->read = sys_io_read((u32 *)ring->regs.read);
	cnt = _noa_dma_get_write_count(ring->read / dws_len,
		ring->write / dws_len, ring->ndesc);
	return cnt > ring->ndesc ? 0 : cnt;
}

static inline u16 noa_dma_get_read_count_q(struct noa_hw_ring *ring)
{
	u16 cnt;
	u32 dw_len = ring->desc_sz / DW_SIZE;

	if (!ring->regs.read || !ring->regs.write)
		return 0;

	ring->write = sys_io_read((u32 *)ring->regs.write);
	ring->read = sys_io_read((u32 *)ring->regs.read);
	cnt = _noa_dma_get_read_count(ring->read / dw_len, ring->write / dw_len, ring->ndesc);
	return cnt > ring->ndesc ? 0 : cnt;
}

static inline void noa_dma_update_sw_write_q(struct noa_hw_ring *ring)
{
	u32 dw_len = ring->desc_sz / DW_SIZE;
	ring->write = (ring->write + dw_len) % (ring->ndesc * dw_len);
}

static inline void noa_dma_update_sw_read_q(struct noa_hw_ring *ring)
{
	u32 dw_len = ring->desc_sz / DW_SIZE;
	ring->read = (ring->read + dw_len) % (ring->ndesc * dw_len);
}

static inline u8 *noa_dma_get_write_base_q(struct noa_hw_ring *ring)
{
	return (u8 *)ring->desc + ring->write * DW_SIZE;
}

static inline u8 *noa_dma_get_read_base_q(struct noa_hw_ring *ring)
{
	return (u8 *)ring->desc + ring->read * DW_SIZE;
}

static void dump_txd_qca(struct noa_wlan_fw *fw, char *str, struct tcl_data_cmd *txd)
{
	dev_info(&fw->dev, "%s: buf_l: %x, buf_h: %x, bm_id: %d, cook: %d\n",
		str,
		txd->buf_addr_info.buffer_addr_31_0,
		txd->buf_addr_info.buffer_addr_39_32,
		txd->buf_addr_info.return_buffer_manager,
		txd->buf_addr_info.sw_buffer_cookie);
	dev_info(&fw->dev,
		"%s: buf_or_ext_desc_type: %d, epd: %d, encap_type: %d, encrypt_type: %d\n",
		str,
		txd->buf_or_ext_desc_type,
		txd->epd,
		txd->encap_type,
		txd->encrypt_type);
	dev_info(&fw->dev,
		"%s: src_buffer_swap: %d, link_meta_swap: %d, tqm_no_drop: %d, reserved_2a: %d\n",
		str,
		txd->src_buffer_swap,
		txd->link_meta_swap,
		txd->tqm_no_drop,
		txd->reserved_2a);
	dev_info(&fw->dev,
		"%s: search_type: %d, addrx_en: %d, addry_en: %d, tcl_cmd_number: %d\n",
		str,
		txd->search_type,
		txd->addrx_en,
		txd->addry_en,
		txd->tcl_cmd_number);
	dev_info(&fw->dev,
		"%s: data_length: %d, ipv4_checksum_en: %d, udp_over_ipv4_checksum_en: %d "
		"udp_over_ipv6_checksum_en: %d\n",
		str,
		txd->data_length,
		txd->ipv4_checksum_en,
		txd->udp_over_ipv4_checksum_en,
		txd->udp_over_ipv6_checksum_en);
	dev_info(&fw->dev,
		"%s: tcp_over_ipv4_checksum_en: %d, tcp_over_ipv6_checksum_en: %d, to_fw: %d, "
		"reserved_3a: %d\n",
		str,
		txd->tcp_over_ipv4_checksum_en,
		txd->tcp_over_ipv6_checksum_en,
		txd->to_fw,
		txd->reserved_3a);
	dev_info(&fw->dev,
		"%s: packet_offset: %d, buffer_timestamp: %d, buffer_timestamp_valid: %d, "
		"reserved_4a: %d\n",
		str,
		txd->packet_offset,
		txd->buffer_timestamp,
		txd->buffer_timestamp_valid,
		txd->reserved_4a);
	dev_info(&fw->dev,
		"%s: hlos_tid_overwrite: %d, hlos_tid: %d, lmac_id: %d, reserved_4b: %d\n",
		str,
		txd->hlos_tid_overwrite,
		txd->hlos_tid,
		txd->lmac_id,
		txd->reserved_4b);
	dev_info(&fw->dev,
		"%s: dscp_tid_table_num: %d, search_index: %d, cache_set_num: %d, mesh_enable: %d\n",
		str,
		txd->dscp_tid_table_num,
		txd->search_index,
		txd->cache_set_num,
		txd->mesh_enable);
	dev_info(&fw->dev, "%s: reserved_6a: %d, ring_id: %d, looping_count: %d\n",
		str,
		txd->reserved_6a,
		txd->ring_id,
		txd->looping_count);
}


static void dump_rx_mpdu_desc_info(struct noa_wlan_fw *fw, char *str,
	struct rx_mpdu_desc_info *desc)
{
	dev_info(&fw->dev,
	"%s: msdu_count: %d, mpdu_sequence_number: %d, fragment_flag: %d\n",
	str,
	desc->msdu_count,
	desc->mpdu_sequence_number,
	desc->fragment_flag);

	dev_info(&fw->dev,
	"%s: mpdu_retry_bit: %d, ampdu_flag: %d, bar_frame: %d\n",
	str,
	desc->mpdu_retry_bit,
	desc->ampdu_flag,
	desc->bar_frame);

	dev_info(&fw->dev,
	"%s: pn_fields_contain_valid_info: %d, sa_is_valid: %d, sa_idx_timeout: %d\n",
	str,
	desc->pn_fields_contain_valid_info,
	desc->sa_is_valid,
	desc->sa_idx_timeout);

	dev_info(&fw->dev,
	"%s: da_is_valid: %d, da_is_mcbc: %d, da_idx_timeout: %d\n",
	str,
	desc->da_is_valid,
	desc->da_is_mcbc,
	desc->da_idx_timeout);

	dev_info(&fw->dev,
	"%s: raw_mpdu: %d, more_fragment_flag: %d, peer_meta_data: %x\n",
	str,
	desc->raw_mpdu,
	desc->more_fragment_flag,
	desc->peer_meta_data);
}

static void dump_rx_msdu_desc_info(struct noa_wlan_fw *fw, char *str,
	struct rx_msdu_desc_info *desc)
{
	dev_info(&fw->dev,
	"%s: first_msdu_in_mpdu_flag: %d, last_msdu_in_mpdu_flag: %d, msdu_continuation: %d\n",
	str,
	desc->first_msdu_in_mpdu_flag,
	desc->last_msdu_in_mpdu_flag,
	desc->msdu_continuation);

	dev_info(&fw->dev,
	"%s: msdu_length: %d, reo_destination_indication: %d, msdu_drop: %d\n",
	str,
	desc->msdu_length,
	desc->reo_destination_indication,
	desc->msdu_drop);

	dev_info(&fw->dev,
	"%s: sa_is_valid: %d, sa_idx_timeout: %d\n",
	str,
	desc->sa_is_valid,
	desc->sa_idx_timeout);

	dev_info(&fw->dev,
	"%s: da_is_valid: %d, da_is_mcbc: %d, da_idx_timeout: %d\n",
	str,
	desc->da_is_valid,
	desc->da_is_mcbc,
	desc->da_idx_timeout);
}

static void dump_rx_desc_qca(struct noa_wlan_fw *fw, char *str, struct reo_destination_ring *desc)
{
	dev_info(&fw->dev, "%s: buf_l: %x, buf_h: %x, bm_id: %d, cookie: %d\n",
		str,
		desc->buf_or_link_desc_addr_info.buffer_addr_31_0,
		desc->buf_or_link_desc_addr_info.buffer_addr_39_32,
		desc->buf_or_link_desc_addr_info.return_buffer_manager,
		desc->buf_or_link_desc_addr_info.sw_buffer_cookie);
	dump_rx_mpdu_desc_info(fw, str, &desc->rx_mpdu_desc_info_details);
	dump_rx_msdu_desc_info(fw, str, &desc->rx_msdu_desc_info_details);
	dev_info(&fw->dev,
		"%s: rx_reo_queue_desc_addr_31_0: %x, rx_reo_queue_desc_addr_39_32: %x\n",
		str,
		desc->rx_reo_queue_desc_addr_31_0,
		desc->rx_reo_queue_desc_addr_39_32);
	dev_info(&fw->dev,
		"%s: reo_dest_buffer_type: %d, reo_push_reason: %d, reo_error_code: %d\n",
		str,
		desc->reo_dest_buffer_type,
		desc->reo_push_reason,
		desc->reo_error_code);
	dev_info(&fw->dev,
		"%s: receive_queue_number: %d, soft_reorder_info_valid: %d, reorder_opcode: %d\n",
		str,
		desc->receive_queue_number,
		desc->soft_reorder_info_valid,
		desc->reorder_opcode);
	dev_info(&fw->dev,
		"%s: reorder_slot_index: %d, mpdu_fragment_number: %d,"
		"captured_msdu_data_size: %d\n",
		str,
		desc->reorder_slot_index,
		desc->mpdu_fragment_number,
		desc->captured_msdu_data_size);
	dev_info(&fw->dev,
		"%s: sw_exception: %d, reo_destination_struct_signature: %d\n",
		str,
		desc->sw_exception,
		desc->reo_destination_struct_signature);
	dev_info(&fw->dev, "%s: ring_id: %d, looping_count: %d\n",
		str,
		desc->ring_id,
		desc->looping_count);
}

static void dump_txcpl_desc_qca(struct noa_wlan_fw *fw, char *str, struct wbm_release_ring *desc)
{
	dev_info(&fw->dev, "%s: buf_l: %x, buf_h: %x, bm_id: %d, cook: %d\n",
		str,
		desc->released_buff_or_desc_addr_info.buffer_addr_31_0,
		desc->released_buff_or_desc_addr_info.buffer_addr_39_32,
		desc->released_buff_or_desc_addr_info.return_buffer_manager,
		desc->released_buff_or_desc_addr_info.sw_buffer_cookie);
	dev_info(&fw->dev, "%s: release_source_module: %d, bm_action: %d\n",
		str,
		desc->release_source_module,
		desc->bm_action);
	dev_info(&fw->dev, "%s: buffer_or_desc_type: %d, first_msdu_index: %d\n",
		str,
		desc->buffer_or_desc_type,
		desc->first_msdu_index);
	dev_info(&fw->dev, "%s: tqm_release_reason: %d, rxdma_push_reason: %d\n",
		str,
		desc->tqm_release_reason,
		desc->rxdma_push_reason);
	dev_info(&fw->dev, "%s: rxdma_error_code: %d, reo_push_reason: %d\n",
		str,
		desc->rxdma_error_code,
		desc->reo_push_reason);
	dev_info(&fw->dev, "%s: reo_error_code: %d, wbm_internal_error: %d\n",
		str,
		desc->reo_error_code,
		desc->wbm_internal_error);
	dev_info(&fw->dev, "%s: wbm_internal_error: %d, tqm_status_number: %d\n",
		str,
		desc->wbm_internal_error,
		desc->tqm_status_number);
	dev_info(&fw->dev, "%s: transmit_count: %d, msdu_continuation: %d\n",
		str,
		desc->transmit_count,
		desc->msdu_continuation);
	dev_info(&fw->dev, "%s: ack_frame_rssi: %d, sw_release_details_valid: %d\n",
		str,
		desc->ack_frame_rssi,
		desc->sw_release_details_valid);
	dev_info(&fw->dev, "%s: first_msdu: %d, last_msdu: %d\n",
		str,
		desc->first_msdu,
		desc->last_msdu);
	dev_info(&fw->dev, "%s: msdu_part_of_amsdu: %d, fw_tx_notify_frame: %d\n",
		str,
		desc->msdu_part_of_amsdu,
		desc->last_msdu);
	dev_info(&fw->dev, "%s: buffer_timestamp: %d, sw_peer_id: %d\n",
		str,
		desc->buffer_timestamp,
		desc->sw_peer_id);
	dev_info(&fw->dev, "%s: tid: %d, ring_id: %d, looping_count: %d\n",
		str,
		desc->tid,
		desc->ring_id,
		desc->looping_count);
}

static int fill_txd_qca(struct noa_wlan_fw *fw, struct noa_desc *desc,
	struct noa_qca_txd *ext, u8 *tx_desc)
{
	struct tcl_data_cmd cmd = {
		.buf_addr_info = {
			.buffer_addr_31_0 = desc->dp_low,
			.buffer_addr_39_32 = (desc->dp_high & 0xff),
			.return_buffer_manager = ext->bmid,
			.sw_buffer_cookie = desc->tkid,
		},
		.buf_or_ext_desc_type = ext->frag,
		.epd = 0,
		.encap_type = ext->encap_type,
		.encrypt_type = ext->encrypt_type,
		.src_buffer_swap = 0,
		.link_meta_swap = 0,
		.tqm_no_drop = 0,
		.reserved_2a = 0,
		.search_type = 0,
		.addrx_en = ext->addrx_en,
		.addry_en = ext->addry_en,
		.tcl_cmd_number = 1025,
		.data_length = desc->dl,
		.ipv4_checksum_en = ext->l3_checksum_en,
		.udp_over_ipv4_checksum_en = ext->l4_checksum_en,
		.udp_over_ipv6_checksum_en = ext->l4_checksum_en,
		.tcp_over_ipv4_checksum_en = ext->l4_checksum_en,
		.tcp_over_ipv6_checksum_en = ext->l4_checksum_en,
		.to_fw = ext->to_fw,
		.reserved_3a = 0,
		.packet_offset = 0,
		.buffer_timestamp = 0,
		.buffer_timestamp_valid = 0,
		.reserved_4a = 0,
		.hlos_tid_overwrite = 1,
		.hlos_tid = ext->set_hlos_tid,
		.lmac_id = 0,
		.reserved_4b = 0,
		.dscp_tid_table_num = 0,
		.search_index = ext->search_index & 0x3f,
		.cache_set_num = 0,
		.mesh_enable = 0,
		.reserved_6a = 0,
		.ring_id = ext->ring_id,
		.looping_count = 0,
	};
	unsigned long pa = desc->dp_high;
	pa = pa << 32 | desc->dp_low;
	dma_sync_single_for_device(&fw->dev, pa, desc->dl, DMA_TO_DEVICE);
	*((struct tcl_data_cmd *)tx_desc) = cmd;
	return 0;
}

#define TLV_32_HDR_SZ 4
static int tx_packet_process_qca(struct noa_wlan_fw *fw, struct noa_desc *desc,
	struct noa_qca_txd *qca_txd, unsigned long long *flags)
{
	int cnt;
	u8 *tlv, *tx_desc;
	struct noa_hw_ring *ring;

	ring = &fw->wlan_info.tx_rings[qca_txd->ring_id];
	/* check tx flow ring remaining count */
	cnt = noa_dma_get_write_count_q(ring);
	if (!cnt) {
		dev_err(&fw->dev,"%s(): %s %d, hw res %d (%d,%d) is not enough\n",
			__func__, ring->name, ring->hw_idx, cnt, ring->read, ring->write);
		goto end;
	}
	/* tx descriptor address */
	tlv = noa_dma_get_write_base_q(ring);
	tx_desc = tlv + TLV_32_HDR_SZ;
	fill_txd_qca(fw, desc, qca_txd, tx_desc);
	if (fw->txdbg) {
		dump_txd_qca(fw, "noa_fw", (struct tcl_data_cmd *)tx_desc);
		hexdump("noa_qca_txd:", tlv, sizeof(struct tcl_data_cmd) + TLV_32_HDR_SZ);
	}
	noa_dma_update_sw_write_q(ring);
	/*add ring to flags*/
	*flags |= BIT(qca_txd->ring_id);
	/* update counter */
	fw->stat.tx++;
	return 0;
end:
	return -ENOMEM;
}

static int tx_packet_process_from_neteng(struct noa_wlan_fw *fw, struct noa_desc *desc,
	unsigned long long *flags)
{
	struct network_ext_txd *nw_txd = (struct network_ext_txd *)desc->ext_data;
	struct wlan_sta_info *sta = lookup_sta_by_da(fw, nw_txd->dest, nw_txd->info.oif);
	struct noa_qca_txd qca_txd = {
		.encrypt_type = sta->encrypt_type,
		.encap_type = sta->encap_type,
		.l3_checksum_en = 0,
		.l4_checksum_en = 0,
		.set_hlos_tid = nw_txd->info.priority,
		.to_fw = 0,
		.ring_id = sta->qos_txq_map[nw_txd->info.priority],
		.bmid = sta->bmid,
		.frag = 0,
		.search_index = sta->search_idx,
		.addry_en = 0,
		.addrx_en = 1,
	};

	return tx_packet_process_qca(fw, desc, &qca_txd, flags);
}

static int wlan_tx_qca(struct noa_wlan_fw *fw, struct noa_desc *desc,
	unsigned long long *flags)
{
	struct noa_qca_txd *qca_txd = (struct noa_qca_txd *)desc->ext_data;

	switch(desc->desc_type) {
	case NOA_DESC_NETENG_PKT_FLOW:
		return tx_packet_process_from_neteng(fw, desc, flags);
	case NOA_DESC_WLAN_TX_QCA:
		return tx_packet_process_qca(fw, desc, qca_txd, flags);
	default:
		dev_err(&fw->dev, "unsupported desc_type %d.\n", desc->desc_type);
		break;
	}
	return -EINVAL;
}

#define HAL_REO_ERROR_DETECTED 0
#define RX_DESC_COOKIE_INDEX_SHIFT		0
#define RX_DESC_COOKIE_INDEX_MASK		0x3ffff /* 18 bits */
#define RX_DESC_COOKIE_POOL_ID_SHIFT		18
#define RX_DESC_COOKIE_POOL_ID_MASK		0x1c0000

#define RX_MSDU_END_TAG_WORD 1
static void fill_input_act_qca(struct reo_destination_ring *desc,
	struct noa_bm_map *buf, struct noa_input_act *input_act)
{
	const uint8_t neteng_path = NoaRingPathIdConvert(
		kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel, kNoaNetengineRingData);
	const uint8_t feedthrough_rx_path = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost, kNoaWlanRingRxData);

	u32 *pkt_tlv = (u32 *)buf->va;
	u32 l2_hdr_offset;

	/* header padding is located at position after tags (4byte) */
	pkt_tlv += (RX_MSDU_END_10_L3_HEADER_PADDING_OFFSET >> 2) + RX_MSDU_END_TAG_WORD;
	l2_hdr_offset = *pkt_tlv;
	l2_hdr_offset = l2_hdr_offset & RX_MSDU_END_10_L3_HEADER_PADDING_MASK;
	l2_hdr_offset = l2_hdr_offset >> RX_MSDU_END_10_L3_HEADER_PADDING_LSB;
	/* assign input action value */
	if (desc->rx_mpdu_desc_info_details.sa_is_valid) {
		input_act->dst = neteng_path;
		input_act->reason = FWD_REASON_NETENGINE;
	} else {
		input_act->dst = feedthrough_rx_path;
		input_act->reason = FWD_REASON_FEEDTHROUGH;
	}
	input_act->head_offset = l2_hdr_offset  + RX_PKT_TLV_LEN;
	input_act->data_len = desc->rx_msdu_desc_info_details.msdu_length + input_act->head_offset;
	input_act->buf = buf;
}

static int wlan_rx_qca(struct noa_wlan_fw *fw,
	struct noa_hw_ring *ring, void *wlan_desc, struct noa_input_act *input_act)
{
	struct reo_destination_ring *desc = wlan_desc;
	struct buffer_addr_info *addr_info = &desc->buf_or_link_desc_addr_info;
	struct noa_bm_map *buf;
	u32 pktid = addr_info->sw_buffer_cookie & RX_DESC_COOKIE_INDEX_MASK;
	u8 *ptr;

	if (fw->rxdbg)
		dump_rx_desc_qca(fw, "qca_rxd", desc);

	buf = rxbm_get_buffer_and_invalid_by_id(fw, pktid);
	if (!buf) {
		dev_err(&fw->dev, "Err packet buffer (%d)\n", pktid);
		return -EINVAL;
	}

	/* copy wifi desc to head of buffer*/
	if (!buf->va) {
		dev_err(&fw->dev, "Err va (%lu)\n", buf->va);
		return -EINVAL;
	}

	if (unlikely(desc->reo_push_reason == HAL_REO_ERROR_DETECTED)) {
		dev_err(&fw->dev, "%s(): RX error detected!\n", __func__);
		return -EINVAL;
	}
	/* put rxd to header room which is prepared by the WiFi kernel driver */
	dma_sync_single_for_cpu(&fw->dev, buf->pa, buf->len, DMA_FROM_DEVICE);
	/* TODO: Move headroom to extend rxd */
	ptr = ((u8 *)buf->va) - WLAN_PKT_PAD;
	memcpy(ptr, desc, ring->desc_sz);
	fill_input_act_qca(desc, buf, input_act);
	return 0;
}

static int wlan_tx_cpl_qca(struct noa_wlan_fw *fw,
	struct noa_hw_ring *ring, void *wlan_desc, u32 *pktid)
{
	struct wbm_release_ring *desc = wlan_desc;
	struct buffer_addr_info *addr_info = &desc->released_buff_or_desc_addr_info;

	*pktid = addr_info->sw_buffer_cookie;
	*pktid = *pktid & RX_DESC_COOKIE_INDEX_MASK;

	if (fw->rxdbg)
		dump_txcpl_desc_qca(fw, "qca_txcpl_desc", desc);
	return 0;
}

/* This ISR is used for handle event from WiFi device */
static irqreturn_t fw_isr_qca(int32_t irq, void *arg)
{
	u32 irq_idx = irq - irq_ctrl.irq_base;
	unsigned long flags;

	disable_irq_nosync(irq);
	if (irq_idx < 0 || irq_idx > 31)
		goto end;
	spin_lock_irqsave(&irq_ctrl.lock, flags);
	irq_ctrl.status |= BIT(irq_idx);
	spin_unlock_irqrestore(&irq_ctrl.lock, flags);
	tasklet_schedule(&irq_ctrl.rx_task);
end:
	return IRQ_HANDLED;
}

/* offset only need to add IRQ_BASE */
enum {
	QCA_TXCPL_IRQ0 = 0,
	QCA_TXCPL_IRQ1 = 4,
	QCA_RXDATA_IRQ0 = 1,
	QCA_RXDATA_IRQ1 = 2,
	QCA_RXDATA_IRQ2 = 3,
};

/* From WiFi device to NOA input ring */
static bool wlan_rx_process_qca(struct noa_wlan_fw *fw, struct noa_hw_ring *ring, u32 rx_type)
{
	void *d;
	bool more = false;
	int cnt, i;
	int ret;

	cnt = noa_dma_get_read_count_q(ring);
	if (!cnt)
		goto end;

	for (i = 0 ; i < cnt; i++) {
		d = noa_dma_get_read_base_q(ring);
		ret = wdev_rx_cb[rx_type](fw, ring, d);
		if (ret < 0) {
			dev_err(&fw->dev, "rx error %d\n", ret);
		}
		noa_dma_update_sw_read_q(ring);
	}
	/* Update WiFi RX ring */
	noa_dma_update_hw_read(ring);
end:
	return more;
}

static bool wlan_tx_cpl_handler_qca(struct noa_wlan_fw *fw)
{
	int i;
	bool more = false;

	for (i = 0; i < fw->wlan_info.tx_cpl_ring_max; i++) {
		if (!fw->wlan_info.tx_cpl_rings[i].desc)
			continue;
		more |= wlan_rx_process_qca(fw, &fw->wlan_info.tx_cpl_rings[i], RX_TYPE_TXCPL);
	}
	return more;
}

static bool wlan_rx_handler(struct noa_wlan_fw *fw, u32 irq)
{
	bool more = false;
	u32 irq_offset = irq - irq_ctrl.irq_base;

	switch(irq_offset) {
	case QCA_RXDATA_IRQ0:
		more |= wlan_rx_process_qca(fw, &fw->wlan_info.rx_rings[0],
			RX_TYPE_DATA);
		break;
	case QCA_RXDATA_IRQ1:
		more |= wlan_rx_process_qca(fw, &fw->wlan_info.rx_rings[1],
			RX_TYPE_DATA);
		break;
	case QCA_RXDATA_IRQ2:
		more |= wlan_rx_process_qca(fw, &fw->wlan_info.rx_rings[2],
			RX_TYPE_DATA);
		more |= wlan_rx_process_qca(fw, &fw->wlan_info.rx_rings[3],
			RX_TYPE_DATA);
		break;
	case QCA_TXCPL_IRQ0:
	case QCA_TXCPL_IRQ1:
		more |= wlan_tx_cpl_handler_qca(fw);
		break;
	default:
		dev_info(&fw->dev, "%s(): the irq %d is not offloaded in NoA.\n", __func__, irq);
		break;
	}
	/* request APC to handle remaining interrupts if it was needed */
	ncp_doorbell_to_apc(irq);
	return more;
}

/* From WiFi device to NOA input ring */
static void wlan_rx_task_qca(unsigned long data)
{
	struct noa_wlan_fw *fw = (struct noa_wlan_fw *)data;
	unsigned long flags;
	u32 irq_mask;
	int i;
	bool more = false, cur;
	u32 status = 0;

	/* clear ints first */
	spin_lock_irqsave(&irq_ctrl.lock, flags);
	irq_mask = irq_ctrl.status;
	irq_ctrl.status = 0;
	spin_unlock_irqrestore(&irq_ctrl.lock, flags);
	if (!irq_mask)
		return;

	/* TODO: handle IRQ depend on priority */
	for (i = 0 ; i < 32; i++) {
		if (!(BIT(i) & irq_mask))
			continue;
		cur = wlan_rx_handler(fw, irq_ctrl.irq_base + i);
		more |= cur;
		if (cur)
			status |= BIT(i);
		else
			enable_irq(irq_ctrl.irq_base + i);
	}
	/* reschedule task if the more flag is set */
	if (more) {
		spin_lock_irqsave(&irq_ctrl.lock, flags);
		irq_ctrl.status |= status;
		spin_unlock_irqrestore(&irq_ctrl.lock, flags);
		tasklet_schedule(&irq_ctrl.rx_task);
	}
}

static void qca_irq_ctrl_init(struct noa_wlan_fw *fw, u32 irq_base)
{
	spin_lock_init(&irq_ctrl.lock);
	tasklet_init(&irq_ctrl.rx_task, wlan_rx_task_qca, (unsigned long)fw);
	/* initial ints */
	irq_ctrl.status = 0;
	irq_ctrl.irq_base = irq_base;
}

static void qca_irq_ctrl_exit(struct noa_wlan_fw *fw)
{
	tasklet_kill(&irq_ctrl.rx_task);
	irq_ctrl.status = 0;
	spin_lock_init(&irq_ctrl.lock);
}

static int wlan_request_irqs_qca(struct noa_wlan_fw *fw)
{
	int i = 0;
	int ret;
	struct noa_wlan_info *info = &fw->wlan_info;

	qca_irq_ctrl_init(fw, info->irqs[0]);
	for (i = 0 ; i < info->irq_nums; i++) {
			ret = request_irq(info->irqs[i], fw_isr_qca,
				IRQF_SHARED | IRQF_NO_SUSPEND, "noa_wlan_qca", fw);
			dev_info(&fw->dev, "%s(): request irq %d, ret %d\n",
				__func__, info->irqs[i], ret);
	}
	return 0;
}

static void wlan_release_irqs_qca(struct noa_wlan_fw *fw)
{
	int i;
	struct noa_wlan_info *info = &fw->wlan_info;
	/* clear ints */
	qca_irq_ctrl_exit(fw);
	for (i = 0 ; i < info->irq_nums; i++) {
		irq_set_affinity_hint(info->irqs[i], NULL);
		free_irq(info->irqs[i], fw);
	}
}

static void dump_addr_info(struct noa_wlan_fw *fw, struct buffer_addr_info *addr_info, char *str)
{
		dev_info(&fw->dev, "%s: buf_l: %x, buf_h: %x, bm_id: %d, cook: %d, addr %lx\n",
		str,
		addr_info->buffer_addr_31_0,
		addr_info->buffer_addr_39_32,
		addr_info->return_buffer_manager,
		addr_info->sw_buffer_cookie,
		(unsigned long)addr_info);
}

static int wlan_rx_post_qca(struct noa_wlan_fw *fw, u32 count, struct noa_bm_buf *bufs)
{
	int i, cnt;
	struct buffer_addr_info *addr_info;
	struct noa_hw_ring *ring = &fw->wlan_info.rx_post_rings[0];
	struct noa_bm_buf *buf;

	cnt = noa_dma_get_write_count_q(ring);
	if (cnt < count) {
		dev_err(&fw->dev, "%s(): hw resource is not enough\n", __func__);
		goto end;
	}
	for (i = 0 ; i < count; i++) {
		buf = &bufs[i];
		addr_info = (struct buffer_addr_info *)noa_dma_get_write_base_q(ring);
		addr_info->buffer_addr_31_0 = buf->pa & 0xffffffff;
		addr_info->buffer_addr_39_32 = (buf->pa >> 32) & 0xffffffff;
		addr_info->return_buffer_manager = 4;
		addr_info->sw_buffer_cookie = buf->pktid;
		if (fw->rxdbg)
			dump_addr_info(fw, addr_info, "wlan_rx_post_qca");
		noa_dma_update_sw_write_q(ring);
	}
	/* Update WiFi TX refill ring */
	noa_dma_update_hw_write(ring);
end:
	return 0;
}


static_assert(NOA_DESC_WLAN_TX_QCA_BYTE == (
	sizeof(struct noa_desc) + sizeof(struct noa_qca_txd)));
static struct ncp_wlan_chip_ops qca_ops = {
	.tx = wlan_tx_qca,
	.rx = wlan_rx_qca,
	.tx_cpl = wlan_tx_cpl_qca,
	.rx_post = wlan_rx_post_qca,
	.request_irqs = wlan_request_irqs_qca,
	.release_irqs = wlan_release_irqs_qca,
};

void device_qca_init(struct noa_wlan_fw *fw, u32 type)
{
	/*
	* QCA solution skip rxbm tx check since some rx_err_ring is
	* not handled by the NCP WiFi firmware.
	*/
	fw->rxbm_tx_check = false;
	fw->chip_ops = &qca_ops;
}
