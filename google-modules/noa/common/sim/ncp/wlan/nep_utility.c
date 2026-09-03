// SPDX-License-Identifier: GPL-2.0-only
/*
 * NEP utility for NCP WiFi
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */

#include <nep/ring_manager.h>
#include <common/inttypes.h>
#include "ncp_wlan_fw.h"
#include "device.h"
#include "system_utility.h"
#include "buffer_manager.h"
#include "nep_utility.h"

static ssize_t nep_input_desc_write(void *d, size_t buf_len, const void *data, size_t data_len)
{
	const uint8_t neteng_path = NoaRingPathIdConvert(
		kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel, kNoaNetengineRingData);
	struct noa_input_act *input_act = (struct noa_input_act *)data;
	struct noa_wlan_fw *fw = input_act->fw;
	struct noa_desc *desc = (struct noa_desc *)d;
	struct noa_bm_map *buf = input_act->buf;

	desc->ver = 0;
	desc->ddone = 0;
	desc->dst = input_act->dst;
	desc->dp_low = (u32)((unsigned long)buf->pa);
	desc->dp_high = (u32)((uint64_t)buf->pa >> 32 & 0xFFFFFFFF);
	desc->dv = buf->va;
	desc->tkid = buf->pktid;
	desc->mode = NOAD_MODE_DATA;
	desc->desc_type = NOA_DESC_BASIC;
	/* define the 802.3 header offset */
	desc->head_offset = input_act->head_offset;
	/* extend data len to include header offset */
	desc->dl = input_act->data_len;
	/* write ring manager control fields depends on dest */
	if (desc->dst == neteng_path) {
#if IS_ENABLED(CONFIG_XFRM_OFFLOAD)
	// For VPN use case, set NO_COPY for NetEngine to decrypt the packet.
	// This is workaround and will effect the tethering path, need to fix it.
		desc->cp = NOAD_NO_COPY;
#else
		desc->cp = NOAD_COPY_DATA;
#endif
		/* request to send feedback event */
		desc->fk = 1;
		desc->reason = FWD_REASON_NETENGINE;
	} else {
		desc->cp = NOAD_NO_COPY;
		desc->fk = 0;
		desc->reason = FWD_REASON_FEEDTHROUGH;
	}

	if (fw->rxdbg) {
		u8 *pkt = (u8 *)desc->dv + desc->head_offset;
		dev_err(&fw->dev, "dv %#lx, data %#lx, len %d, tkid %d\n", desc->dv,
			(unsigned long)pkt, desc->dl, desc->tkid);
		hexdump("noa_rx_pkt:", pkt, desc->dl);
	}
	return NOA_DESC_BASIC_BYTE;
}

static void nep_trigger_doorbell(struct noa_ring_wrapper *ring)
{
	struct noa_wlan_fw *fw = (struct noa_wlan_fw *)ring->owner;
	/* Trigger NOA DMA scheduler to handle input ring */
	if (noa_ring_pos_is_moved(ring)) {
		writel(1, (void *)fw->noa_hw_doorbell_addr);
		noa_sim_trig_rx();
	}
}

const static struct noa_ring_ops nep_ring_rx_ops = {
	.payload_len = NULL,
	.read_payload = noa_generic_read_raw_pointer,
	.fill_noop = NULL,
	.write_payload = NULL,
	.begin_hook = NULL,
	.complete_prehook = NULL,
	.complete_hook = NULL,
};

const static struct noa_ring_ops nep_ring_tx_ops = {
	.payload_len = NULL,
	.read_payload = NULL,
	.fill_noop = NULL,
	.write_payload = nep_input_desc_write,
	.begin_hook = NULL,
	.complete_prehook = NULL,
	.complete_hook = nep_trigger_doorbell,
};

static int nep_free_rxbuf(struct noa_wlan_fw *fw, u16 pktid)
{
	struct noa_bm_map *map = rxbm_get_buffer_by_id(fw, pktid);
	struct noa_bm_buf buf;
	int ret;

	if (!map)
		goto err;

	if (fw->rxdbg)
		dev_info(&fw->dev, "free rx pkt id %d\n", pktid);

	buf.pktid = map->pktid;
	buf.len = map->len;
	buf.pa = map->pa;
	buf.dpa_va = map->va;
	ret = wdev_rx_post(fw, 1, &buf);
	if (ret)
		goto err;
	fw->stat.rx_free++;
	return 0;
err:
	dev_err(&fw->dev, "%s(): err pktid %d\n", __func__, pktid);
	fw->stat.rx_free_err++;
	return -EINVAL;
}

static bool nep_feedback_handle(struct noa_wlan_fw *fw,
	struct noa_desc *desc)
{
	if (desc->reason == FWD_REASON_NETENGINE)
		nep_free_rxbuf(fw, desc->tkid);
	if (fw->rxdbg)
		dev_err(&fw->dev, "Receiving feedback event tkid %d, reason %d\n",
			desc->tkid, desc->reason);
	fw->stat.feedback++;
	return false;
}

static bool nep_data_handle(struct noa_wlan_fw *fw,
	struct noa_desc *desc, unsigned long long *flags)
{
	bool flush = false;
	u16 pktid = desc->tkid;

	/* reason handle */
	switch(desc->reason) {
	case FWD_REASON_VPN:
		fallthrough;
	case FWD_REASON_FEEDTHROUGH:
		if (!txbm_pktid_txsync(fw, pktid))
			break;
		wdev_tx(fw, desc, flags);
		flush = true;
		break;
	case FWD_REASON_NETENGINE:
		wdev_tx(fw, desc, flags);
		flush = true;
		break;
	default:
		break;
	}
	return flush;
}

static bool nep_rx_packet(struct noa_wlan_fw *fw,
	struct noa_desc *desc, unsigned long long *flags)
{
	bool flush = false;

	/* mode handle */
	switch(desc->mode) {
	case NOAD_MODE_FEEDBACK:
		flush = nep_feedback_handle(fw, desc);
		break;
	case NOAD_MODE_DATA:
		flush = nep_data_handle(fw, desc, flags);
		break;
	default:
		break;
	}

	return flush;
}

/* NOA output ring handle */
static bool nep_rx_handle(struct noa_wlan_fw *fw)
{
	int ret;
	noa_ring_consumer *ring = &fw->nep_rx_ring.ring;
	unsigned long long flags = 0;
	bool flush = false;

	ret = noa_ring_begin_processing(ring);
	if (ret <= 0) {
		if (!ret) {
			dev_err(&fw->dev,
				"Ring %s is already in processing state, something may went wrong, "
				"because it is only allowed one thread to operate this ring.\n",
				ring->name);
			ret = -EINVAL;
		}
		goto out;
	}
	/* currently we didn't limite the number of batch count */
	while (true) {
		struct noa_desc *desc;
		unsigned long data_addr = 0;
		ret = noa_ring_read(ring, &data_addr, sizeof(data_addr));
		if (!ret) {
			break;
		} else if (ret < 0 || !data_addr) {
			dev_err(&fw->dev,
				"Failed to read data from wifi fw ring, drop this item, err: %d\n",
				ret);
			noa_ring_tail_inc(ring);
			continue;
		}
		desc = (struct noa_desc *)data_addr;
		flush |= nep_rx_packet(fw, desc, &flags);
	}
	/* update WiFi TX ring*/
	if (flush)
		wdev_tx_ringbell(fw, flags);
	/* update NOA RX ring */
	noa_ring_complete_processing(ring);
out:
	return !noa_ring_is_empty(ring);
}

/* This ISR is used for handled interrupt from NOA */
static irqreturn_t nep_isr(int32_t id, void *data)
{
	struct noa_wlan_fw *fw = (struct noa_wlan_fw *)data;

	/* clear ints first, then schedule a button-helf task */
	writel(0, (void *) fw->noa_hw_ints_addr);
	if (NOA_FW_DIRECT_TX)
		nep_receiver((unsigned long)fw);
	else
		tasklet_schedule(&fw->wlan_tx_task);
	return IRQ_HANDLED;
}

static int nep_ring_init(struct noa_wlan_fw *fw, struct nep_ring *nep_ring, int wrapper_type,
			 int size, int item_len, const struct noa_ring_ops *ops,
			 const uint8_t flow_of_ring, const uint8_t type_of_ring,
			 const uint8_t direction_of_ring, const char *name)
{
	int ret;
	struct noa_ring_regs regs = { 0 };
	struct noa_ring_info info = {
		.head = 0,
		.tail = 0,
		.size = size,
		.item_len = item_len,
	};

	ret = NoaRingSharedRegsGet(&regs, kNoaNetworkInterfaceWlan, flow_of_ring, type_of_ring,
				   direction_of_ring);
	if (ret) {
		dev_err(&fw->dev,
			"Failed to get wlan fw noa ring regs with "
			"flow %" PRIu16 ", type %" PRIu16 ", direction %" PRIu16 "\n",
			flow_of_ring, type_of_ring, direction_of_ring);
		return -ENODEV;
	}

	noa_ring_regs_wrapper_init(&nep_ring->ring, wrapper_type, ops, &regs, fw, name, 0);

	/*ring sw initial*/
	nep_ring->desc = dmam_alloc_coherent(&fw->dev, info.size * info.item_len,
					     &nep_ring->desc_dma, GFP_KERNEL);
	if (!nep_ring->desc) {
		dev_err(&fw->dev,"%s(): dma alloc fail!\n", __func__);
		return -ENOMEM;
	}
	info.base = nep_ring->desc;
	info.dpa_base = info.base;
	spin_lock_init(&nep_ring->lock);
	noa_ring_info_setup(&nep_ring->ring, &info);
	return 0;
}

static void nep_ring_exit(struct noa_wlan_fw *fw, struct nep_ring *nep_ring)
{
	struct noa_ring_wrapper *ring = &nep_ring->ring;

	noa_ring_deactivate(ring);

	spin_lock(&nep_ring->lock);
	if (nep_ring->desc) {
		dma_free_coherent(&fw->dev, ring->basic.size * ring->basic.item_len, nep_ring->desc,
				nep_ring->desc_dma);
	}
	nep_ring->desc = NULL;
	nep_ring->desc_dma = 0;
	noa_ring_info_clean(ring);
	spin_unlock(&nep_ring->lock);
}

static int nep_rings_init(struct noa_wlan_fw *fw)
{
	struct noa_port *port = noa_sim_get_port(NOA_PORT_WLAN_FW);
	int ret;

	ret = nep_ring_init(fw, &fw->nep_tx_ring, NOA_RING_TYPE_PRODUCER, WLAN_FW_INPUT_SIZE,
			    NOA_DESC_BASIC_BYTE, &nep_ring_tx_ops, kNoaNetworkFlowDeviceToHost,
			    kNoaWlanRingRxData, kNoaRingNepInput, "noa_input_ring2");
	if (ret) {
		goto end;
	}
	ret = nep_ring_init(fw, &fw->nep_rx_ring, NOA_RING_TYPE_CONSUMER, WLAN_FW_OUTPUT_SIZE,
			    NOA_DESC_MAX_BYTE, &nep_ring_rx_ops, kNoaNetworkFlowHostToDevice,
			    kNoaWlanRingTxData, kNoaRingNepOutput, "noa_output_ring2");
	if (ret) {
		goto end;
	}
	noa_ring_activate(&fw->nep_tx_ring.ring);
	noa_ring_activate(&fw->nep_rx_ring.ring);
	fw->noa_hw_doorbell_addr = (unsigned long)&port->doorbell;
	fw->noa_hw_ints_addr = (unsigned long)&port->ints;
	return 0;
end:
	nep_ring_exit(fw, &fw->nep_tx_ring);
	nep_ring_exit(fw, &fw->nep_rx_ring);
	return ret;
}

/* Handle packets from NOA */
void nep_receiver(unsigned long data)
{
	struct noa_wlan_fw *fw = (struct noa_wlan_fw *)data;
	bool more = false;

	/* rx handler */
	more |= nep_rx_handle(fw);
	if (more)
		tasklet_schedule(&fw->wlan_tx_task);
}

/* TX packet to NOA input ring */
int nep_tx_packet(struct noa_wlan_fw *fw, struct noa_input_act *in_act)
{
	int ret;
	noa_ring_producer *ring = &fw->nep_tx_ring.ring;

	ret = noa_ring_begin_processing(ring);
	if (ret < 0) {
		return ret;
	} else if (!ret) {
		dev_err(&fw->dev,
			"Ring %s is already in processing state, something may went wrong, "
			"because it is only allowed one thread to operate this ring.\n",
			ring->name);
		return -EINVAL;
	}

	// the actual payload size is struct noa_desc
	ret = noa_ring_write(ring, in_act, NOA_DESC_BASIC_BYTE);
	if (ret < 0 && ret != -EAGAIN)
		dev_err(&fw->dev, "Failed to write tx packet to NOA wlan fw input ring\n");

	noa_ring_complete_processing(ring);
	return ret;
}

int nep_flowid_table_add(struct noa_wlan_fw *fw, struct wlan_sta_info *sta, u8 pri)
{
	struct {
		char dst[ETH_ALEN];
		u8 priority;
		u8 enable;
		u32 flowid;
	} cmd = {
		.priority = pri,
		.enable = true,
		.flowid = sta->qos_txq_map[pri],
	};
	memcpy(cmd.dst, sta->addr, ETH_ALEN);
	return nep_request_send(NEP_CMD_NETENGINE_FLOWID_UPDATE, (void *)&cmd);
}

int nep_flowid_table_remove_all(struct noa_wlan_fw *fw, struct wlan_sta_info *sta)
{
	int i = 0;
	for (i = 0 ; i < PRIORITY_CLASS; i++) {
		struct {
			char dst[ETH_ALEN];
			u8 priority;
			u8 enable;
			u32 flowid;
		} cmd = {
			.priority = i,
			.enable = false,
			.flowid = 0,
		};
		memcpy(cmd.dst, sta->addr, ETH_ALEN);
		nep_request_send(NEP_CMD_NETENGINE_FLOWID_UPDATE, (void *)&cmd);
	}
	return 0;
}

static void nep_buffer_pool_exit(struct noa_wlan_fw *fw)
{
	const u8 buffer_pool_ring_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost, kNoaWlanNepBufferPool);
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, false, buffer_pool_ring_id,
				      kNoaRingNepInput);
	noa_ring_deactivate(&fw->tx_buffer_pool.ring);
	noa_ring_info_clean(&fw->tx_buffer_pool.ring);
	kfree(fw->tx_buffer_pool.ring_buf);
	fw->tx_buffer_pool.ring_buf = NULL;
}

static ssize_t refill_buffer(void *buf, size_t buf_len, const void *data, size_t data_len)
{
	*((noa_buffer_pool_desc *)buf) = *(noa_buffer_pool_desc *)data;
	return sizeof(noa_buffer_pool_desc);
}

const static struct noa_ring_ops pool_ring_ops = {
	.write_payload = refill_buffer,
};

static int nep_buffer_pool_init(struct noa_wlan_fw *fw)
{
	int ret;
	int i;
	int tx_bm_sz = fw->wlan_info.tx_bm_sz;
	const u8 buffer_pool_ring_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost, kNoaWlanNepBufferPool);
	struct noa_ring_regs ring_regs = { 0 };
	struct noa_ring_info ring_info = {
		.head = tx_bm_sz,
		.tail = 0,
		.size = tx_bm_sz + 1,
		.item_len = sizeof(noa_buffer_pool_desc),
	};

	fw->tx_buffer_pool.ring_buf =
		(char *)kzalloc(ring_info.item_len * ring_info.size, GFP_KERNEL);
	if (!fw->tx_buffer_pool.ring_buf) {
		ret = -ENOMEM;
		goto out;
	}
	ring_info.base = fw->tx_buffer_pool.ring_buf;
	ring_info.dpa_base = ring_info.base;

	for (i = 0; i < tx_bm_sz; i++) {
		struct noa_bm_map *entry = &fw->txbm[i];
		noa_buffer_pool_desc *item =
			noa_ring_buf_pos(ring_info.base, i, ring_info.item_len);
		item->tkid = entry->pktid;
		item->dp_high = (entry->pa >> 32U) & 0xFFFFFFFF;
		item->dp_low = entry->pa & 0xFFFFFFFF;
		item->dv = entry->va;
	}

	ret = NoaRingSharedRegsGet(&ring_regs, kNoaNetworkInterfaceWlan,
				   kNoaNetworkFlowDeviceToHost, kNoaWlanNepBufferPool,
				   kNoaRingNepInput);
	if (ret) {
		dev_err(&fw->dev, "Failed to get buffer pool regs, ret %d\n", ret);
		goto out;
	}

	ret = noa_ring_regs_wrapper_init(&fw->tx_buffer_pool.ring, NOA_RING_TYPE_PRODUCER,
					 &pool_ring_ops, &ring_regs, fw, "wlan buf", 0);
	if (ret) {
		dev_err(&fw->dev, "Failed to init wlan buf ring, ret %d\n", ret);
		goto out;
	}
	noa_ring_info_setup(&fw->tx_buffer_pool.ring, &ring_info);
	noa_ring_activate(&fw->tx_buffer_pool.ring);

	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true, buffer_pool_ring_id,
				      kNoaRingNepInput);
	ret = 0;

out:
	if (ret) {
		nep_buffer_pool_exit(fw);
	}
	return ret;
}

int nep_replenish_tx_buffer(struct noa_wlan_fw *fw, u16 pktid)
{
	int ret = -1;
	const int offset = fw->wlan_info.tx_pkt_max;
	noa_ring_producer *ring = &fw->tx_buffer_pool.ring;
	int idx = pktid - offset;
	noa_buffer_pool_desc item;
	struct noa_bm_map *entry = NULL;

	if (idx < 0 || idx >= fw->wlan_info.tx_bm_sz) {
		dev_err(&fw->dev, "Replenish invalid tkid %u to nep buffer pool", pktid);
		return -EINVAL;
	}

	ret = noa_ring_begin_processing(ring);
	if (ret <= 0) {
		dev_err(&fw->dev, "Ring %s is not ready, ret %d\n", ring->name, ret);
		return ret;
	}

	entry = &fw->txbm[idx];
	item.tkid = entry->pktid;
	item.dp_high = (entry->pa >> 32U) & 0xFFFFFFFF;
	item.dp_low = entry->pa & 0xFFFFFFFF;
	item.dv = entry->va;

	ret = noa_ring_write(ring, &item, sizeof(item));
	if (ret < 0) {
		dev_err(&fw->dev, "Failed to replenish wlan pool, head %u, tail %u, ret %d\n",
			ring->basic.head, noa_ring_tail_read_once(ring), ret);
		goto out;
	}

	ret = 0;
out:
	noa_ring_complete_processing(ring);
	return ret;
}

static inline bool is_wlan_ready(struct noa_wlan_fw *fw)
{
	return !!(fw->flags & BIT(WLAN_FW_FLAG_INIT));
}

int nep_init(struct noa_wlan_fw *fw)
{
	int ret;
	int32_t irq_id = get_nep_wlan_irq();

	if (!is_wlan_ready(fw)) {
		return -EINVAL;
	}

	ret = nep_buffer_pool_init(fw);
	if (ret) {
		dev_err(&fw->dev, "ring service buffer pool init fail.\n");
		return ret;
	}
	/* Activate the own ring wrappers */
	ret = nep_rings_init(fw);
	if (ret) {
		dev_err(&fw->dev, "noa_fw_ring init fail.\n");
		return ret;
	}
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
							   kNoaNetworkFlowDeviceToHost,
							   kNoaWlanRingRxData),
				      kNoaRingNepInput);
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
							   kNoaNetworkFlowHostToDevice,
							   kNoaWlanRingTxData),
				      kNoaRingNepOutput);
	noa_interrupt_register(irq_id, nep_isr, fw);
	fw->flags |= BIT(WLAN_FW_FLAG_START);
	return ret;
}

void nep_exit(struct noa_wlan_fw *fw)
{
	if (!is_wlan_ready(fw)) {
		return;
	}
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, false,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
							   kNoaNetworkFlowDeviceToHost,
							   kNoaWlanRingRxData),
				      kNoaRingNepInput);
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, false,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceWlan,
							   kNoaNetworkFlowHostToDevice,
							   kNoaWlanRingTxData),
				      kNoaRingNepOutput);
	nep_ring_exit(fw, &fw->nep_tx_ring);
	nep_ring_exit(fw, &fw->nep_rx_ring);
	nep_buffer_pool_exit(fw);
}
