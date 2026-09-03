// SPDX-License-Identifier: GPL-2.0-only
/*
 * NCP WiFi Firmware
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include "ncp_wlan_fw.h"
#include "buffer_manager.h"
#include "nep_utility.h"
#include "system_utility.h"
#include "device.h"

struct noa_wlan_fw wlan_fw;

static int ncp_wlan_fw_rings_init(struct noa_wlan_fw *fw, void *msg)
{
	struct noa_wlan_cmd_ring_info *rings = (struct noa_wlan_cmd_ring_info *)msg;
	switch(rings->ring_type) {
	case RING_TYPE_TX_DATA:
		fw->wlan_info.tx_ring_max = rings->count;
		return wdev_ring_init(fw, rings, fw->wlan_info.tx_rings);
	case RING_TYPE_RX_DATA:
		fw->wlan_info.rx_ring_max = rings->count;
		return wdev_ring_init(fw, rings, fw->wlan_info.rx_rings);
	case RING_TYPE_TX_CPL:
		fw->wlan_info.tx_cpl_ring_max = rings->count;
		return wdev_ring_init(fw, rings, fw->wlan_info.tx_cpl_rings);
	case RING_TYPE_RX_POST:
		fw->wlan_info.rx_post_ring_max = rings->count;
		return wdev_ring_init(fw, rings, fw->wlan_info.rx_post_rings);
	default:
		break;
	}
	return -EINVAL;
}

static int ncp_wlan_fw_rings_exit(struct noa_wlan_fw *fw)
{
	struct noa_wlan_info *wlan_info = &fw->wlan_info;

	/* free rx ring */
	wdev_ring_exit(fw, wlan_info->rx_rings, wlan_info->rx_ring_max);
	/* free tx ring */
	wdev_ring_exit(fw, wlan_info->tx_rings, wlan_info->tx_ring_max);
	/* free tx cpl ring */
	wdev_ring_exit(fw, wlan_info->tx_cpl_rings, wlan_info->tx_cpl_ring_max);
	return 0;
}

static int ncp_wlan_fw_start(struct noa_wlan_fw *fw, void *msg)
{
	struct noa_wlan_info *info = &fw->wlan_info;
	struct noa_wlan_cmd_fw_start *cmd = (struct noa_wlan_cmd_fw_start *)msg;
	/* TODO: FW mode will map to physical address */
	if (cmd->reg_size)
		fw->reg_base = ioremap(cmd->reg_addr,cmd->reg_size);
	if (cmd->share_size)
		fw->share_base = ioremap(cmd->share_addr,cmd->share_size);

	info->ints_addr = cmd->ints_addr;
	info->intm_addr = cmd->intm_addr;
	info->reg_addr = cmd->reg_addr;
	info->share_addr = cmd->share_addr;
	info->reg_size = cmd->reg_size;
	info->share_size = cmd->share_size;

	return nep_init(fw);
}

static int ncp_wlan_fw_stop(struct noa_wlan_fw *fw)
{
	fw->flags &= ~BIT(WLAN_FW_FLAG_START);
	nep_exit(fw);
	ncp_wlan_fw_rings_exit(fw);
	iounmap(fw->reg_base);
	iounmap(fw->share_base);
	return 0;
}

static int ncp_wlan_fw_init(struct noa_wlan_fw *fw, void *msg)
{
	struct noa_wlan_info *info = &fw->wlan_info;
	struct noa_wlan_cmd_fw_init *cmd = (struct noa_wlan_cmd_fw_init *)msg;
	static u64 dmamask = DMA_BIT_MASK(PCIE_DMA_MASK);
	int ret;

	/* test only */
	device_initialize(&fw->dev);
	dev_set_name(&fw->dev, "noa_fw");
	fw->dev.dma_mask = (u64 *)&dmamask;
	fw->dev.coherent_dma_mask = DMA_BIT_MASK(PCIE_DMA_MASK);
	/* hook chip ops by chip type */
	wdev_chip_register(fw, cmd->type);
	ret = device_add(&fw->dev);
	if (ret) {
		pr_err("wlan_fw add device fail. err: %d", ret);
		return ret;
	}
	/* resource */
	info->rx_pkt_max = cmd->rx_pkt_max;
	info->tx_pkt_max = cmd->tx_pkt_max;
	info->rx_buf_sz = cmd->rx_buf_sz;
	info->tx_bm_sz = cmd->tx_bm_sz;
	rxbm_init(fw, info->rx_pkt_max);
	txbm_init(fw, info->tx_pkt_max);
	/* fw debug initial */
	fw->rxdbg = false;
	fw->txdbg = false;
	memset(&fw->stat, 0, sizeof(struct noa_wlan_stat));
	/* data path initial */
	spin_lock_init(&fw->rx_lock);
	tasklet_init(&fw->wlan_tx_task, nep_receiver, (unsigned long)fw);
	fw->flags |= BIT(WLAN_FW_FLAG_INIT);
	return 0;
}

static int ncp_wlan_fw_exit(struct noa_wlan_fw *fw)
{
	fw->flags &= ~BIT(WLAN_FW_FLAG_INIT);
	fw->flags &= ~BIT(WLAN_FW_FLAG_IRQ);
	wdev_release_irqs(fw);
	rxbm_exit(fw);
	txbm_exit(fw);
	tasklet_kill(&fw->wlan_tx_task);
	device_del(&fw->dev);
	memset(fw, 0, sizeof(struct noa_wlan_fw));
	return 0;
}

static int ncp_wlan_register_irqs(struct noa_wlan_fw *fw, void *msg)
{
	struct noa_wlan_cmd_irq_request *cmd = (
		struct noa_wlan_cmd_irq_request *)msg;
	struct noa_wlan_info *info = &fw->wlan_info;
	int i;

	if (fw->flags & BIT(WLAN_FW_FLAG_IRQ))
		return 0;
	/* Register PCIe ISR */
	info->irq_nums = cmd->irq_nums;
	for (i = 0 ; i < cmd->irq_nums; i++)
		info->irqs[i] = cmd->irqs[i];

	wdev_request_irqs(fw);
	fw->flags |= BIT(WLAN_FW_FLAG_IRQ);
	return 0;
}

static int ncp_wlan_txq_active(struct noa_wlan_fw *fw, void *msg)
{
	struct txq_cmd {
		u16 ring_id;
		bool enable;
	} *cmd = (struct txq_cmd *)msg;
	struct noa_hw_ring *tx_ring = &fw->wlan_info.tx_rings[cmd->ring_id];

	bool cur_state = !!(tx_ring->flags & BIT(TX_RING_FLAG_ACTIVE));

	if (cmd->enable == cur_state) {
		dev_err(&fw->dev, "%s(): current txq state is wrong %d, %d, %d\n",
			__func__, cmd->ring_id, cur_state, cmd->enable);
	}
	dev_info(&fw->dev, "%s(): active txq %d, %d\n", __func__, cmd->ring_id, cmd->enable);
	return wdev_txq_active(fw, tx_ring, cmd->enable);
}

#define INVALID_TXQ_ID 0xFFFF
static int ncp_wlan_sta_active(struct noa_wlan_fw *fw, void *msg)
{
	struct wlan_sta_info *cmd = msg;
	struct wlan_sta_info *sta_info;
	int i;

	dev_info(&fw->dev, "%s(): active sta %d, %d, %d\n",
		__func__, cmd->oif, cmd->bss_idx, cmd->enable);
	dev_info(&fw->dev, "%s(): qos_map (%d, %d, %d, %d, %d, %d, %d, %d)\n", __func__,
		cmd->qos_txq_map[0], cmd->qos_txq_map[1], cmd->qos_txq_map[2], cmd->qos_txq_map[3],
		cmd->qos_txq_map[4], cmd->qos_txq_map[5], cmd->qos_txq_map[6], cmd->qos_txq_map[7]);
	dev_info(&fw->dev, "%s(): mac %02x:%02x:%02x:%02x:%02x:%02x\n", __func__,
		cmd->addr[0], cmd->addr[1], cmd->addr[2], cmd->addr[3], cmd->addr[4], cmd->addr[5]);
	dev_info(&fw->dev, "%s(): encrypt_type %d, encap_type %d, lmac_id %d\n",
		__func__, cmd->encrypt_type, cmd->encap_type, cmd->lmac_id);
	dev_info(&fw->dev, "%s(): bmid %d, search_idx %d, search_type %d\n",
		__func__, cmd->bmid, cmd->search_idx, cmd->search_type);
	dev_info(&fw->dev, "%s(): dscp_tid_map_id %d, addry_en %d, addrx_en %d\n",
		__func__, cmd->dscp_tid_map_id, cmd->addry_en, cmd->addrx_en);
	sta_info = lookup_sta_by_da(fw, cmd->addr, cmd->oif);

	if (!sta_info)
		return -ENODEV;

	/* case of deactive sta, clear and then enable only */
	if (!cmd->enable) {
		sta_info->enable = false;
		sta_info->oif = cmd->oif;
		sta_info->bss_idx = cmd->bss_idx;
		nep_flowid_table_remove_all(fw, sta_info);
		return 0;
	}
	/* case of active sta */
	/* sta is not exist yet */
	if (!sta_info->enable) {
		sta_info->enable = true;
		sta_info->oif = cmd->oif;
		sta_info->bss_idx = cmd->bss_idx;
		sta_info->encrypt_type = cmd->encrypt_type;
		sta_info->encap_type = cmd->encap_type;
		sta_info->lmac_id = cmd->lmac_id;
		sta_info->bmid = cmd->bmid;
		sta_info->search_idx = cmd->search_idx;
		sta_info->search_type = cmd->search_type;
		sta_info->dscp_tid_map_id = cmd->dscp_tid_map_id;
		sta_info->addry_en = cmd->addry_en;
		sta_info->addrx_en = cmd->addrx_en;
		memcpy(sta_info->addr, cmd->addr, ETH_ALEN);
	}
	/* update sta pri to flowid mapping */
	for (i = 0 ; i < PRIORITY_CLASS; i++) {
		/* only update valid flow id */
		if (cmd->qos_txq_map[i] == INVALID_TXQ_ID)
			continue;
		sta_info->qos_txq_map[i] = cmd->qos_txq_map[i];
		/* add table to netengine of nep */
		nep_flowid_table_add(fw, sta_info, i);
	}
	return 0;
}

static int ncp_wlan_rxbm_sync(struct noa_wlan_fw *fw, void *msg)
{
	struct noa_wlan_cmd_bm *req = (struct noa_wlan_cmd_bm  *) msg;
	/* update to rx post ring */
	return wdev_rx_post(fw, req->count, req->bufs);
}

static int ncp_wlan_txbm_sync(struct noa_wlan_fw *fw, void *msg)
{
	struct noa_wlan_cmd_bm *req = (struct noa_wlan_cmd_bm  *) msg;
	txbm_set_bufs(fw, req->count, req->bufs);
	return 0;
}

/* To be removed, driver mode only */
static int ncp_wlan_client_dev(struct noa_wlan_fw *fw, void *msg)
{
	fw->client_dev = (struct device *)msg;
	return 0;
}

int __noa_wlan_fw_request_send_sim(int cmd, void *msg)
{
	struct noa_wlan_fw *fw = &wlan_fw;
	int ret = 0;

	switch(cmd) {
	case NOA_WLAN_CMD_RXBM_SYNC:
		ret = ncp_wlan_rxbm_sync(fw, msg);
		break;
	case NOA_WLAN_CMD_TXBM_SYNC:
		ret = ncp_wlan_txbm_sync(fw, msg);
		break;
	case NOA_WLAN_CMD_FW_INIT:
		ret = ncp_wlan_fw_init(fw, msg);
		break;
	case NOA_WLAN_CMD_FW_START:
		ret = ncp_wlan_fw_start(fw, msg);
		break;
	case NOA_WLAN_CMD_FW_EXIT:
		ret = ncp_wlan_fw_exit(fw);
		break;
	case NOA_WLAN_CMD_FW_STOP:
		ret = ncp_wlan_fw_stop(fw);
		break;
	case NOA_WLAN_CMD_RING_UPDATE:
		ret = ncp_wlan_fw_rings_init(fw, msg);
		break;
	case NOA_WLAN_CMD_REG_RECEIVER:
		ret = ncp_register_event_receiver(fw, msg);
		break;
	case NOA_WLAN_CMD_ISR_REGISTER:
		ret = ncp_wlan_register_irqs(fw, msg);
		break;
	case NOA_WLAN_CMD_CLIENT_DEV:
		ret = ncp_wlan_client_dev(fw, msg);
		break;
	case NOA_WLAN_CMD_TX_RING_ACTIVE:
		ret = ncp_wlan_txq_active(fw, msg);
		break;
	case NOA_WLAN_CMD_STA_ACTIVE:
		ret = ncp_wlan_sta_active(fw, msg);
		break;
	default:
		break;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(__noa_wlan_fw_request_send_sim);
