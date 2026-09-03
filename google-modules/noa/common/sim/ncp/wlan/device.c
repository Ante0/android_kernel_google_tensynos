// SPDX-License-Identifier: GPL-2.0-only
/*
 * NCP WiFi generic device utility
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */

#include "ncp_wlan_fw.h"
#include "system_utility.h"
#include "buffer_manager.h"
#include "device.h"
#include "nep_utility.h"

/* chip ops only be used internally */
static int wdev_chip_tx(struct noa_wlan_fw *fw, struct noa_desc *desc,
	unsigned long long *flags)
{
	if (fw->chip_ops && fw->chip_ops->tx)
		return fw->chip_ops->tx(fw, desc, flags);
	return -ENODEV;
}

static int wdev_chip_rx(struct noa_wlan_fw *fw,
	struct noa_hw_ring *ring, void *desc, struct noa_input_act *input_act)
{
	if (fw->chip_ops && fw->chip_ops->rx)
		return fw->chip_ops->rx(fw, ring, desc, input_act);
	return -ENODEV;
}

static int wdev_chip_tx_cpl(struct noa_wlan_fw *fw,
	struct noa_hw_ring *ring, void *desc, u32 *pktid)
{
	if (fw->chip_ops && fw->chip_ops->tx_cpl)
		return fw->chip_ops->tx_cpl(fw, ring, desc, pktid);
	return -ENODEV;
}

static int wdev_chip_rx_post(struct noa_wlan_fw *fw, u32 count, struct noa_bm_buf *bufs)
{
	if (fw->chip_ops && fw->chip_ops->rx_post)
		return fw->chip_ops->rx_post(fw, count, bufs);
	return -ENODEV;
}

static int wdev_chip_request_irqs(struct noa_wlan_fw *fw)
{
	if (fw->chip_ops && fw->chip_ops->request_irqs)
		return fw->chip_ops->request_irqs(fw);
	return -ENODEV;
}

static void wdev_chip_release_irqs(struct noa_wlan_fw *fw)
{
	if (fw->chip_ops && fw->chip_ops->release_irqs)
		fw->chip_ops->release_irqs(fw);
}

static int wdev_rx_packet_cb(struct noa_wlan_fw *fw,
	struct noa_hw_ring *ring, void *d)
{
	int ret;
	struct noa_input_act input_act = {
		.fw = fw,
	};

	ret = wdev_chip_rx(fw, ring, d, &input_act);
	if (ret || !rxbm_pktid_rxsync(fw, input_act.buf->pktid)) {
		fw->stat.rx_err++;
		return ret;
	}

	fw->stat.rx++;
	/* forward to nep */
	return nep_tx_packet(fw, &input_act);
}

static int wdev_txcpl_cb(struct noa_wlan_fw *fw, struct noa_hw_ring *ring, void *d)
{
	int ret;
	u32 pktid;

	ret = wdev_chip_tx_cpl(fw, ring, d, &pktid);
	if (ret) {
		dev_err(&fw->dev, "%s(): txcpl handle coherence issue %d\n", __func__, ret);
		return ret;
	}

	ret = txbm_sync(fw, pktid);
	/* TODO: aggregation to reduce time of wake up APC */
	if (ret)
		ncp_send_event_to_apc(fw, NOA_WLAN_EVENT_TXCPL_SYNC, (void *)d);
	return 0;
}

/* This function array is used control the order in vendor specific ISR */
wdev_rx_fun *wdev_rx_cb[RX_TYPE_MAX] = {
	wdev_rx_packet_cb,
	wdev_txcpl_cb,
};

/* ringbell wifi device to handle the device rings*/
static void _wdev_tx_ringbell(struct noa_wlan_fw *fw, struct noa_hw_ring *ring)
{
	u32 value = (0xFF000000 | ring->hw_idx << 16 | ring->write);

	ncp_send_event_to_apc(fw, NOA_WLAN_EVENT_DOORBELL, (void *)&value);
}

/* wake up pcie bus and wifi device firmware */
/* TODO: change to control PCIe bus directly */
static inline void wdev_hw_wakeup(struct noa_wlan_fw *fw, bool lock)
{
	ncp_send_event_to_apc(fw, NOA_WLAN_EVENT_WAKEUP, &lock);
}

void wdev_tx_ringbell(struct noa_wlan_fw *fw, unsigned long long flags)
{
	struct noa_hw_ring *ring;
	int i;

	for (i = 0 ; i < fw->wlan_info.tx_ring_max; i++) {
		if (!(BIT(i) & flags))
			continue;
		ring = &fw->wlan_info.tx_rings[i];
		/* update write index of WiFi TX flow ring */
		wdev_hw_wakeup(fw, true);
		noa_dma_update_hw_write(ring);
		wdev_hw_wakeup(fw, false);
		/* ringbell*/
		_wdev_tx_ringbell(fw, ring);
	}
}

#define INITIAL_SN 1
int wdev_ring_init(struct noa_wlan_fw *fw,
	struct noa_wlan_cmd_ring_info *rings, struct noa_hw_ring *hw_rings)
{
	int i;
	struct noa_wlan_ring_info *info;
	struct noa_hw_ring *ring;

	for (i = 0; i < rings->count; i++) {
		ring = &hw_rings[i];
		info = &rings->info[i];
		if (!info->ndesc || !info->desc_sz)
			continue;
		spin_lock_init(&ring->lock);
		ring->read = 0;
		ring->write = 0;
		ring->sn = INITIAL_SN;
		memcpy(&ring->regs, &info->regs, sizeof(struct noa_ring_regs));
		strncpy(ring->name, info->name, sizeof(ring->name));
		ring->buf_size = fw->wlan_info.rx_buf_sz;
		ring->hw_idx = info->hw_idx;
		ring->ndesc = info->ndesc;
		ring->desc_sz = info->desc_sz;
		ring->desc = (void *)info->dma_va;
		ring->stride = info->stride;
		ring->desc_dma = (dma_addr_t)info->dma_pa;
	}
	return 0;
}

void wdev_ring_exit(struct noa_wlan_fw *fw, struct noa_hw_ring *rings, u8 ring_num)
{
	struct noa_hw_ring *ring;
	int i;

	for (i = 0 ; i < ring_num; i++) {
		ring = &rings[i];
		spin_lock(&ring->lock);
		ring->read = 0;
		ring->write = 0;
		ring->sn = 0;
		ring->desc = NULL;
		spin_unlock(&ring->lock);
	}
}

void wdev_release_irqs(struct noa_wlan_fw *fw)
{
	wdev_chip_release_irqs(fw);
}

int wdev_request_irqs(struct noa_wlan_fw *fw)
{
	return wdev_chip_request_irqs(fw);
}

int wdev_tx(struct noa_wlan_fw *fw, struct noa_desc *desc,
	unsigned long long *flags)
{
	return wdev_chip_tx(fw, desc, flags);
}

int wdev_txq_active(struct noa_wlan_fw *fw, struct noa_hw_ring *tx_ring, bool enable)
{
	u32 read, write;
	if (enable) {
		/* re-sync read and write index when a txq active */
		read = sys_io_read((u32 *)tx_ring->regs.read);
		write = sys_io_read((u32 *)tx_ring->regs.write);
		dev_info(&fw->dev,"%s(): resync read/write index from (%d,%d) to (%d,%d)\n",
			__func__, tx_ring->read, tx_ring->write, read, write);
		tx_ring->read = read;
		tx_ring->write = write;
		tx_ring->flags |= BIT(TX_RING_FLAG_ACTIVE);
	} else {
		tx_ring->flags &= ~BIT(TX_RING_FLAG_ACTIVE);
	}
	return 0;
}

int wdev_rx_post(struct noa_wlan_fw *fw, u32 count, struct noa_bm_buf *bufs)
{
	int ret;
	ret = wdev_chip_rx_post(fw, count, bufs);
	if (ret) {
		dev_err(&fw->dev, "rx post handle err %d\n", ret);
		return ret;
	}
	return rxbm_sync(fw, count, bufs);
}

int wdev_chip_register(struct noa_wlan_fw *fw, u32 type)
{
	fw->type = type;
	switch(type) {
#if IS_ENABLED(CONFIG_NOA_WLAN_QCA_SUPPORT)
	case WLAN_FW_TYPE_QCA:
		device_qca_init(fw, type);
		break;
#endif /* CONFIG_NOA_WLAN_QCA_SUPPORT */
#if IS_ENABLED(CONFIG_NOA_WLAN_BRCM_SUPPORT)
	case WLAN_FW_TYPE_BRCM_4389:
		fallthrough;
	case WLAN_FW_TYPE_BRCM_4390:
		device_brcm_init(fw, type);
		break;
#endif /* CONFIG_NOA_WLAN_BRCM_SUPPORT */
	default:
		dev_err(&fw->dev, "Invalid chip_type %d.\n", type);
		return -EINVAL;
	}
	return 0;
}

#define STA_MODE_LMAC_ID 0
struct wlan_sta_info *lookup_sta_by_da(struct noa_wlan_fw *fw, char *da, const u32 oif)
{
	struct wlan_sta_info *sta_info;
	struct wlan_sta_info *free_sta = NULL;
	int i = 0, free_sta_idx;

	for (i = 0 ; i < MAX_STA_SUPPORT; i++) {
		sta_info = &fw->wlan_info.sta_info[i];
		if (!sta_info->enable) {
			free_sta = sta_info;
			free_sta_idx = i;
			continue;
		}

		if (oif == sta_info->oif && sta_info->lmac_id == STA_MODE_LMAC_ID) {
			return sta_info;
		}

		if (!memcmp(sta_info->addr, da, MAC_ADDR_LEN)) {
			return sta_info;
		}
	}
	dev_info(&fw->dev, "lookup fail next free sta_info idx %d\n", free_sta_idx);
	dev_info(&fw->dev, "da %02x:%02x:%02x:%02x:%02x:%02x\n",
				da[0], da[1], da[2], da[3], da[4], da[5]);
	return free_sta;
}
