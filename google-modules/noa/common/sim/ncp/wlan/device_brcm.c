// SPDX-License-Identifier: GPL-2.0-only
/*
 * NCP WiFi BRCM Device Utility
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <common/core.h>
#include <common/noa_hw_ring.h>
#include <common/wlan/noa_wlan_brcm.h>
#include "ncp_wlan_fw.h"
#include "device.h"
#include "trace.h"
#include "buffer_manager.h"
#include "system_utility.h"
#include <nep/ring_manager_instance.h>

static struct tasklet_struct brcm_rx_task;

#define IS_4390(fw) (fw->wlan_info.chip_id == CHIP_ID_BRCM4390)
#define IS_4389(fw) (fw->wlan_info.chip_id == CHIP_ID_BRCM4389)

/* TX related Section */
static void dump_txd_brcm(struct noa_wlan_fw *fw, struct noa_desc *d, struct noa_bcm_txd *txd)
{
	u8 *pkt_va;
	pkt_va = ((u8 *)d->dv) + d->head_offset;
	dev_info(&fw->dev, "FW TX data dump from NOA output ring.\n");
	dev_info(&fw->dev, "va %p, %#lx\n", pkt_va, (unsigned long)pkt_va);
	dev_info(&fw->dev, "low addr %#x, high addr %#x\n", d->dp_low, d->dp_high);
	dev_info(&fw->dev, "pktid %x, dst %d, pkt len %d\n", d->tkid, d->dst, d->dl);
	dev_info(&fw->dev, "txd %p, %#lx\n", txd, (unsigned long)txd);
	dev_info(&fw->dev, "current_phase %d, ext_flags %#x\n", txd->current_phase, txd->ext_flags);
	dev_info(&fw->dev, "ring_id %d, flags %#x, ifidx %d\n", txd->ring_id, txd->flags,
		 txd->ifidx);
	dev_info(&fw->dev, "ext_tag %d, pkt_csum_type %x, l3_hdr_len %d, l4_hdr_len: %d\n",
		 txd->ext_tag, txd->pkt_csum_type, txd->l3_hdr_len, txd->l4_hdr_len);
	dev_info(&fw->dev, "ethertype %04x\n", ntohs(txd->ethertype));
}

static void fill_brcm_txd(struct noa_wlan_fw *fw, struct noa_desc *input, struct noa_bcm_txd *txd,
			  u8 *desc, u8 *pkt)
{
	struct host_txbuf_post *tx_desc = (struct host_txbuf_post *)desc;
	struct pkt_info_cso *cso_info = &tx_desc->pktinfo;
	size_t size =
		IS_4390(fw) ? sizeof(struct host_txbuf_post) : (sizeof(struct host_txbuf_post) - 8);

	memset(tx_desc, 0, size);
	tx_desc->cmn_hdr.msg_type = 0xF;
	tx_desc->seg_cnt = 1;
	tx_desc->flags = txd->flags;
	/* remove brcm_txd from dp and dl */
	tx_desc->data_buf_addr = input->dp_low | (((uint64_t)input->dp_high) << 32);
	tx_desc->data_buf_addr += input->head_offset;
	tx_desc->data_len = input->dl - input->head_offset;
	tx_desc->ext_flags = txd->ext_flags;
	tx_desc->metadata_buf_len = 0;
	tx_desc->metadata_buf_addr = 0;
	tx_desc->cmn_hdr.request_id = input->tkid;
	tx_desc->cmn_hdr.flags = txd->current_phase;
	tx_desc->cmn_hdr.if_id = txd->ifidx;
	/* brcm4390 supported */
	if (IS_4390(fw)) {
		cso_info->ver = txd->ext_tag;
		cso_info->pkt_csum_type = txd->pkt_csum_type;
		cso_info->nwk_hdr_len = txd->l3_hdr_len;
		cso_info->trans_hdr_len = txd->l4_hdr_len;
	}
	memcpy(tx_desc->txhdr, pkt, sizeof(tx_desc->txhdr));
	*((u16 *)&tx_desc->txhdr[12]) = txd->ethertype;
	if (fw->txdbg) {
		hexdump("noa_txd:", (unsigned char *)tx_desc, sizeof(struct host_txbuf_post));
		hexdump("noa_tx_pkt:", pkt, input->dl);
		hexdump("noa_bcm_txd:", (u8 *)txd, sizeof(struct noa_bcm_txd));
	}
	dma_sync_single_for_device(fw->client_dev ? fw->client_dev : &fw->dev,
				   (dma_addr_t)tx_desc->data_buf_addr, tx_desc->data_len,
				   DMA_TO_DEVICE);
}

static void wlan_dump_all_rings(struct noa_wlan_fw *fw)
{
	struct noa_port *port;
	int i;
	int direction;

	dev_info(&fw->dev, "======== Ring Address ========\n");
	for (i = 0; i < NOA_PORT_MAX; i++) {
		port = noa_sim_get_port(i);
		if (!port->priv)
			continue;

		dev_info(&fw->dev, "======== Port %d: %s ========\n", i, port->name);

		dev_info(&fw->dev, "irq: %d, doorbell %p (%d), intm %p (%d), ints %p (%d)\n",
			 port->irq, &port->doorbell, port->doorbell, &port->intm, port->intm,
			 &port->ints, port->ints);
	}
	dev_info(&fw->dev, "======== Ring Values ========\n");
	NoaRingManagerInstancePrintAll();
}

#define FLOWID_TO_RINGID(_flowid) (_flowid - 2)
static int tx_packet_process_brcm(struct noa_wlan_fw *fw, struct noa_desc *desc,
				  struct noa_bcm_txd *bcm_txd, unsigned long long *flags)
{
	u8 *pkt_va;
	u8 *tx_desc;
	struct noa_hw_ring *ring;
	int cnt, ring_id;

	if (fw->txdbg)
		dump_txd_brcm(fw, desc, bcm_txd);

	if (!desc->dv) {
		dev_err(&fw->dev, "dv in tx packet desc is null, fw state: 0x%x.\n", fw->flags);
		dump_txd_brcm(fw, desc, bcm_txd);
		wlan_dump_all_rings(fw);
		fw->stat.tx_err++;
		goto end;
	}

	/* va is used in simulator only */
	pkt_va = (u8 *)desc->dv + desc->head_offset;
	ring_id = bcm_txd->ring_id;
	if (fw->txdbg) {
		dev_info(&fw->dev, "pkt_va %lx, bcm_txd %lx, ring_id %d\n", desc->dv,
			 (unsigned long)bcm_txd, ring_id);
	}
	if (ring_id < 0) {
		dev_err(&fw->dev, "txd ring id %d is not expected.\n", ring_id);
		fw->stat.tx_err++;
		goto end;
	}
	ring = &fw->wlan_info.tx_rings[ring_id];
	if (!(ring->flags & BIT(TX_RING_FLAG_ACTIVE))) {
		dev_err(&fw->dev, "txd ring id %d is not active.\n", ring_id);
		fw->stat.tx_err++;
		goto end;
	}
	/* check tx flow ring remaining count */
	cnt = noa_dma_get_write_count(ring);
	if (!cnt)
		goto end;

	/* tx descriptor address */
	tx_desc = noa_dma_get_write_base(ring);
	fill_brcm_txd(fw, desc, bcm_txd, tx_desc, pkt_va);
#ifdef linux
	trace_wlan_noa_dump_vendor_tx(bcm_txd->ring_id, sizeof(struct host_txbuf_post),
				      (char *)tx_desc, desc->dl - desc->head_offset,
				      (char *)(pkt_va + desc->head_offset));
#endif
	noa_dma_update_sw_write(ring);
	/*add ring to flags*/
	*flags |= BIT(ring_id);
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
	struct noa_bcm_txd bcm_txd = {
		.flags = 0x11,
		.ext_flags = 0,
		.current_phase = 160,
		// TODO(b/377615978): Align the way ring_id and flow_id are used for
		// both network TXD and WLAN TXD.
		.ring_id = FLOWID_TO_RINGID(nw_txd->info.flowid),
		.ethertype = htons(nw_txd->info.ethertype),
		.l3_hdr_len = nw_txd->info.l3_len,
		.l4_hdr_len = nw_txd->info.l4_len,
	};
	if (!sta || !sta->enable) {
		return -ENODEV;
	}
	if (nw_txd->info.ipv6)
		bcm_txd.pkt_csum_type |= BIT(PKT_CSUM_TYPE_IPV6_SHIFT);
	if (nw_txd->info.ipv4)
		bcm_txd.pkt_csum_type |= BIT(PKT_CSUM_TYPE_IPV4_SHIFT);
	if (nw_txd->info.tcp)
		bcm_txd.pkt_csum_type |= BIT(PKT_CSUM_TYPE_TCP_SHIFT);
	if (nw_txd->info.udp)
		bcm_txd.pkt_csum_type |= BIT(PKT_CSUM_TYPE_UDP_SHIFT);
	if (nw_txd->info.tcp || nw_txd->info.udp)
		bcm_txd.pkt_csum_type |= BIT(PKT_CSUM_TYPE_TRANS_CSUM_SHIFT);
	bcm_txd.ifidx = sta->bss_idx;
	return tx_packet_process_brcm(fw, desc, &bcm_txd, flags);
}

static int wlan_tx_brcm(struct noa_wlan_fw *fw, struct noa_desc *desc, unsigned long long *flags)
{
	struct noa_bcm_txd *txd = (struct noa_bcm_txd *)desc->ext_data;

	switch (desc->desc_type) {
	case NOA_DESC_NETENG_PKT_FLOW:
		return tx_packet_process_from_neteng(fw, desc, flags);
	case NOA_DESC_WLAN_TX_BRCM:
		return tx_packet_process_brcm(fw, desc, txd, flags);
	default:
		dev_err(&fw->dev, "unsupported desc_type %d.\n", desc->desc_type);
		break;
	}
	return -EINVAL;
}

/* RX related Section */
static void dump_rx_desc_brcm(struct noa_wlan_fw *fw, struct wlan_rxbuf_cmpl *desc)
{
	struct cmn_msg_hdr *hdr = &desc->cmn_hdr;
	struct compl_msg_hdr *compl_hdr = &desc->compl_hdr;
	dev_err(&fw->dev, "data_len: %d, data_offset: %d, flags: %d, metadata_len: %d\n",
		desc->data_len, desc->data_offset, desc->flags, desc->metadata_len);
	dev_err(&fw->dev, "msg: %#lx, msg_type: %d, flags: %x, pkt_id: %d, if_id: %d\n",
		(unsigned long)desc, hdr->msg_type, hdr->flags, hdr->request_id, hdr->if_id);
	dev_err(&fw->dev, "status: %d, ring_id: %d\n", compl_hdr->status, compl_hdr->ring_id);
}

static u32 compute_xor32(u32 *val, int nwords)
{
	int idx;
	u32 xor32 = 0;
	for (idx = 0; idx < nwords; idx++)
		xor32 ^= *(val + idx);
	return xor32;
}

#define D2H_EPOCH_MODULO (253)
#define H2D_EPOCH_MODULO (253)
#define PCIE_D2H_SYNC_DELAY (100UL)
#define PCIE_D2H_SYNC_NUM_OF_STEPS (5U)
#define PCIE_D2H_SYNC_WAIT_TRIES (512U)
static u8 sync_xorcsum(struct noa_wlan_fw *fw, struct noa_hw_ring *ring, struct cmn_msg_hdr *msg)
{
	u32 retries;
	u32 checksum = 0;
	int nwords = ring->desc_sz / sizeof(u32);
	u32 step = 0;

	for (step = 1; step <= PCIE_D2H_SYNC_NUM_OF_STEPS; step++) {
		for (retries = 0; retries < PCIE_D2H_SYNC_WAIT_TRIES; retries++) {
			if (msg->epoch != ring->sn)
				continue;
			checksum = compute_xor32((u32 *)msg, nwords);
			if (checksum != 0)
				continue;
			ring->sn = (ring->sn + 1) % D2H_EPOCH_MODULO;
			goto end;
		}
	}
	dev_err(&fw->dev, "checksum error and updated fail! (%d,%d,%d) id: %d, name %s, type %x\n",
		msg->epoch, ring->sn, checksum, msg->request_id, ring->name, msg->msg_type);
	return 0;
end:
	return msg->msg_type;
}

static u8 route_decide_brcm(struct wlan_rxbuf_cmpl *desc)
{
	const uint8_t neteng_path = NoaRingPathIdConvert(
		kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel, kNoaNetengineRingData);
#if IS_ENABLED(CONFIG_XFRM_OFFLOAD)
	/**
	 * Route all packet to NetEngine because some of them are VPN packets
	 * that need to be further processed in NetEngine.
	 * TODO(b/355097058): Support dynamic switch for fast path, and remove the preprocessor.
	 */
	const uint8_t dst = neteng_path;
#else
	const uint8_t dst = NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
						 kNoaNetworkFlowDeviceToHost, kNoaWlanRingRxData);
#endif

	if (!NOA_WLAN_FORWARD_NET)
		return dst;

	/* STA mode is always using feed through path */
	if (desc->cmn_hdr.if_id == 0)
		return dst;

	/* 802.11 packet is always using feed through mode */
	if (desc->flags & BCMPCIE_PKT_FLAGS_FRAME_802_11)
		return dst;

	return neteng_path;
}

static void fill_input_act_brcm(struct wlan_rxbuf_cmpl *desc, struct noa_bm_map *buf,
				struct noa_input_act *input_act)
{
	/* assign input action value */
	input_act->dst = route_decide_brcm(desc);
	input_act->head_offset = desc->data_offset;
	input_act->data_len = desc->data_len + desc->data_offset;
	input_act->buf = buf;
}

static int wlan_rx_brcm(struct noa_wlan_fw *fw, struct noa_hw_ring *ring, void *wlan_desc,
			struct noa_input_act *input_act)
{
	struct noa_bm_map *buf;
	struct wlan_rxbuf_cmpl *desc = (struct wlan_rxbuf_cmpl *)wlan_desc;
	u32 pktid = desc->cmn_hdr.request_id;
	bool sync = true;
	u8 *ptr;

	sync = sync_xorcsum(fw, ring, &desc->cmn_hdr);
	if (!sync) {
		dev_err(&fw->dev, "rx coherence issue!\n");
		return -EINVAL;
	}

	if (pktid == 0) {
		dev_err(&fw->dev, "rx invalid pktid is %d\n", pktid);
		return -EINVAL;
	}

	buf = rxbm_get_buffer_and_invalid_by_id(fw, pktid);
	if (!buf) {
		dev_err(&fw->dev, "Err packet buffer (%d)\n", pktid);
		return -EINVAL;
	}

	if (fw->rxdbg)
		dump_rx_desc_brcm(fw, desc);

	/* copy wifi desc to head of buffer*/
	if (!buf->va) {
		dev_err(&fw->dev, "Err va (%lu)\n", buf->va);
		return -EINVAL;
	}
#ifdef linux
	trace_wlan_noa_dump_vendor_rx(0, sizeof(struct wlan_rxbuf_cmpl), (char *)desc,
				      desc->data_len + desc->data_offset, (char *)(buf->va));
#endif
	/* put rxd to header room which is prepared by the WiFi kernel driver */
	dma_sync_single_for_cpu(fw->client_dev ? fw->client_dev : &fw->dev, buf->pa, buf->len,
				DMA_FROM_DEVICE);
	/* TODO: Move headroom to extend txd */
	ptr = ((u8 *)buf->va) - WLAN_PKT_PAD;
	memcpy(ptr, desc, ring->desc_sz);
	fill_input_act_brcm(desc, buf, input_act);
	return 0;
}

static int wlan_tx_cpl_brcm(struct noa_wlan_fw *fw, struct noa_hw_ring *ring, void *d, u32 *pktid)
{
	struct host_txbuf_cmpl *txstatus = (struct host_txbuf_cmpl *)d;
	bool sync = true;

	sync = sync_xorcsum(fw, ring, &txstatus->cmn_hdr);
	if (!sync) {
		dev_err(&fw->dev, "rx coherence issue!\n");
		return -EINVAL;
	}
	*pktid = txstatus->cmn_hdr.request_id;
	if (*pktid == 0) {
		dev_err(&fw->dev, "invalid pktid %d\n", *pktid);
		return -EINVAL;
	}
	return 0;
}

/* From WiFi device to NOA input ring */
static bool wlan_rx_process_brcm(struct noa_wlan_fw *fw, struct noa_hw_ring *ring, u32 rx_type)
{
	void *d;
	bool more = false;
	int cnt, i, ret;

	cnt = noa_dma_get_read_count(ring);
	if (!cnt)
		goto end;

	for (i = 0; i < cnt; i++) {
		d = noa_dma_get_read_base(ring);
		ret = wdev_rx_cb[rx_type](fw, ring, d);
		if (ret < 0) {
			dev_err(&fw->dev, "rx error: %d, type: %d\n", ret, rx_type);
		}
		noa_dma_update_sw_read(ring);
	}
	/* Update WiFi RX ring */
	noa_dma_update_hw_read(ring);
end:
	return more;
}

/* From WiFi device to NOA input ring */
static void wlan_rx_task_brcm(unsigned long data)
{
	struct noa_wlan_fw *fw = (struct noa_wlan_fw *)data;
	bool more = false;

	/* rx data handler */
	more |= wlan_rx_process_brcm(fw, &fw->wlan_info.rx_rings[0], RX_TYPE_DATA);
	/* rx txcpl handler */
	more |= wlan_rx_process_brcm(fw, &fw->wlan_info.tx_cpl_rings[0], RX_TYPE_TXCPL);
	/* request APC to handle remaining interrupts if it was needed */
	ncp_doorbell_to_apc(fw->wlan_info.irqs[0]);
	if (more)
		tasklet_schedule(&brcm_rx_task);
}

/* This ISR is used for handle event from WiFi device */
static irqreturn_t wdev_isr_brcm(int32_t irq, void *arg)
{
	struct noa_wlan_fw *fw = (struct noa_wlan_fw *)arg;
	u32 ints = 0;

	if ((fw->flags & BIT(WLAN_FW_FLAG_START))) {
		ints = hw_io_read((char *)fw->reg_base, fw->wlan_info.ints_addr);
	}

	if (1 || ints) {
		tasklet_schedule(&brcm_rx_task);
	} else {
		ncp_doorbell_to_apc(irq);
	}
	return IRQ_HANDLED;
}

static int wlan_request_irqs_brcm(struct noa_wlan_fw *fw)
{
	int i = 0;
	int ret;
	struct noa_wlan_info *info = &fw->wlan_info;

	for (i = 0; i < info->irq_nums; i++) {
		ret = request_irq(info->irqs[i], wdev_isr_brcm, IRQF_SHARED, "noa_wlan_brcm", fw);
		dev_info(&fw->dev, "%s(): request irq %d, ret %d\n", __func__, info->irqs[i], ret);
	}
	tasklet_init(&brcm_rx_task, wlan_rx_task_brcm, (unsigned long)fw);
	return 0;
}

static void wlan_release_irqs_brcm(struct noa_wlan_fw *fw)
{
	int i;
	struct noa_wlan_info *info = &fw->wlan_info;

	tasklet_kill(&brcm_rx_task);
	for (i = 0; i < info->irq_nums; i++) {
		irq_set_affinity_hint(info->irqs[i], NULL);
		free_irq(info->irqs[i], fw);
	}
}

static void fill_rxpost_brcm(struct host_rxbuf_post *rxbuf_post, struct noa_bm_buf *buf, u8 sn)
{
	rxbuf_post->cmn_hdr.msg_type = MSG_TYPE_RXBUF_POST;
	rxbuf_post->cmn_hdr.request_id = buf->pktid;
	rxbuf_post->cmn_hdr.if_id = 0;
	rxbuf_post->cmn_hdr.epoch = sn;
	rxbuf_post->cmn_hdr.flags = 160;
	rxbuf_post->data_buf_len = buf->len;
	rxbuf_post->high_addr = buf->pa >> 32 & 0xffffffff;
	rxbuf_post->low_addr = buf->pa & 0xffffffff;
}

static int wlan_rx_post_brcm(struct noa_wlan_fw *fw, u32 count, struct noa_bm_buf *bufs)
{
	int i;
	struct host_rxbuf_post *rxbuf_post;
	struct noa_hw_ring *ring = &fw->wlan_info.rx_post_rings[0];
	int res = noa_dma_get_write_count(ring);

	if (res < count)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		rxbuf_post = (struct host_rxbuf_post *)noa_dma_get_write_base(ring);
		fill_rxpost_brcm(rxbuf_post, &bufs[i], ring->sn);
		ring->sn = (ring->sn + 1) % H2D_EPOCH_MODULO;
		noa_dma_update_sw_write(ring);
	}
	noa_dma_update_hw_write(ring);
	return 0;
}

static_assert(NOA_DESC_WLAN_TX_BRCM_BYTE == (sizeof(struct noa_desc) + sizeof(struct noa_bcm_txd)));
static struct ncp_wlan_chip_ops brcm_ops = {
	.tx = wlan_tx_brcm,
	.rx = wlan_rx_brcm,
	.tx_cpl = wlan_tx_cpl_brcm,
	.rx_post = wlan_rx_post_brcm,
	.request_irqs = wlan_request_irqs_brcm,
	.release_irqs = wlan_release_irqs_brcm,
};

void device_brcm_init(struct noa_wlan_fw *fw, u32 type)
{
	switch (type) {
	case WLAN_FW_TYPE_BRCM_4389:
		fw->chip_ops = &brcm_ops;
		fw->wlan_info.chip_id = CHIP_ID_BRCM4389;
		fw->rxbm_tx_check = true;
		break;
	case WLAN_FW_TYPE_BRCM_4390:
		fw->chip_ops = &brcm_ops;
		fw->wlan_info.chip_id = CHIP_ID_BRCM4390;
		fw->rxbm_tx_check = true;
		break;
	default:
		dev_err(&fw->dev, "device type %d is not supported\n", type);
		break;
	}
}
