// SPDX-License-Identifier: GPL-2.0-only
/*
 * NCP WiFi buffer manager
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */

#include "buffer_manager.h"
#include "system_utility.h"
#include "nep_utility.h"

bool txbm_pktid_rxsync(struct noa_wlan_fw *fw, u16 pktid)
{
	if (!fw->tx_pktid_checker[pktid]) {
		dev_err(&fw->dev, "freed tx pktid %d error\n", pktid);
		return false;
	}
	fw->tx_pktid_checker[pktid] = 0;
	return true;
}

bool txbm_pktid_txsync(struct noa_wlan_fw *fw, u16 pktid)
{
	if (pktid > fw->wlan_info.tx_pkt_max || fw->tx_pktid_checker[pktid]) {
		dev_err(&fw->dev, "used tx pktid %d error\n", pktid);
		return false;
	}
	fw->tx_pktid_checker[pktid] = 1;
	return true;
}

bool rxbm_pktid_rxsync(struct noa_wlan_fw *fw, u16 pktid)
{
	if (!fw->rx_pktid_checker[pktid]) {
		dev_err(&fw->dev, "freed rx pktid %d error\n", pktid);
		return false;
	}
	fw->rx_pktid_checker[pktid] = 0;
	return true;
}

bool rxbm_pktid_txsync(struct noa_wlan_fw *fw, u16 pktid)
{
	if (fw->rxbm_tx_check && fw->rx_pktid_checker[pktid]) {
		dev_err(&fw->dev, "used rx pktid %d error\n", pktid);
		return false;
	}
	fw->rx_pktid_checker[pktid] = 1;
	return true;
}

void txbm_set_bufs(struct noa_wlan_fw *fw, int count, struct noa_bm_buf *src_bufs)
{
	int i;
	struct noa_bm_map *entry;
	struct noa_bm_buf *buf;

	fw->wlan_info.tx_bm_sz = count;
	/* allocate buffers */
	for (i = 0; i < count; i++) {
		buf = &src_bufs[i];
		entry = &fw->txbm[i];
		entry->va = buf->dpa_va;
		entry->len = buf->len;
		entry->pa = buf->pa;
	}
}

static void txbm_free_bufs(struct noa_wlan_fw *fw)
{
	int i;
	struct noa_bm_map *entry;

	/* free buffers */
	for (i = 0; i < fw->wlan_info.tx_bm_sz ; i++) {
		entry = &fw->txbm[i];
		entry->valid = false;
		entry->len = 0;
	}
}

int txbm_init(struct noa_wlan_fw *fw, u32 tx_pkt_max)
{
	int sz, i;
	u32 tx_bm_sz = fw->wlan_info.tx_bm_sz;
	struct noa_bm_map *entry;
	/* Start token id is tx_pkt_max */
	sz = sizeof(struct noa_bm_map) * tx_bm_sz;
	fw->txbm = (struct noa_bm_map *) kzalloc(sz, GFP_KERNEL);
	if (!fw->txbm)
		return -ENOMEM;

	/* initial entries */
	for (i = 0; i < tx_bm_sz; i++) {
		entry = &fw->txbm[i];
		entry->pktid = i + tx_pkt_max;
		entry->valid = false;
	}

	/* alloc tx_pktid checker */
	sz = sizeof(u8) * tx_pkt_max;
	fw->tx_pktid_checker = (u8 *)kzalloc(sz, GFP_KERNEL);
	if (!fw->tx_pktid_checker)
		goto fail;
	return 0;
fail:
	kfree(fw->txbm);
	return -ENOMEM;
}

void txbm_exit(struct noa_wlan_fw *fw)
{
	txbm_free_bufs(fw);
	kfree(fw->txbm);
	kfree(fw->tx_pktid_checker);
}

int rxbm_init(struct noa_wlan_fw *fw, u32 rx_pkt_max)
{
	int sz;
	/* Start token id is 1 */
	sz = sizeof(struct noa_bm_map) * (rx_pkt_max + 1);
	fw->rxbm = (struct noa_bm_map *)kzalloc(sz, GFP_KERNEL);
	if (!fw->rxbm)
		return -ENOMEM;
	/* alloc tx_pktid checker */
	sz = sizeof(u8) * rx_pkt_max;
	fw->rx_pktid_checker = (u8 *)kzalloc(sz, GFP_KERNEL);
	if (!fw->rx_pktid_checker)
		goto fail;
	return 0;
fail:
	kfree(fw->rxbm);
	return -ENOMEM;
}

void rxbm_exit(struct noa_wlan_fw *fw)
{
	kfree(fw->rxbm);
	kfree(fw->rx_pktid_checker);
}

int txbm_sync(struct noa_wlan_fw *fw, u16 pktid)
{
	int ret;
	u16 idx;

	/* packet is owned by driver */
	if (pktid < fw->wlan_info.tx_pkt_max) {
		/* tx_pktid for APC is invalid drop it.*/
		if (!txbm_pktid_rxsync(fw, pktid)) {
			fw->stat.tx_cpl_err++;
			return 0;
		}
		return -EINVAL;
	}
	idx = pktid - fw->wlan_info.tx_pkt_max;
	if (idx > fw->wlan_info.tx_bm_sz) {
		dev_err(&fw->dev, "%s(): idx %d is out of range\n", __func__, pktid);
		fw->stat.tx_cpl_err++;
		return 0;
	}

	ret = nep_replenish_tx_buffer(fw, pktid);
	if (ret) {
		dev_err(&fw->dev, "%s(): idx %d is not valid\n", __func__, pktid);
		fw->stat.tx_cpl_err++;
		return 0;
	}

	if (fw->txdbg)
		dev_err(&fw->dev, "%s(): free pktid %d succeeded.\n", __func__, pktid);
	fw->stat.tx_cpl++;
	return 0;
}

int rxbm_sync(struct noa_wlan_fw *fw, int count, struct noa_bm_buf *src_bufs)
{
	int i;
	struct noa_bm_buf *src;
	struct noa_bm_map *bufs = fw->rxbm, *dst;

	spin_lock(&fw->rx_lock);
	for (i = 0 ; i < count; i++) {
		src = &src_bufs[i];
		if (src->pktid > fw->wlan_info.rx_pkt_max) {
			dev_err(&fw->dev, "%s(): pktid %d > max %d\n", __func__,
				src->pktid, fw->wlan_info.rx_pkt_max);
			fw->stat.rxbm_sync_err++;
			continue;
		}
		if (!rxbm_pktid_txsync(fw, src->pktid)) {
			fw->stat.rxbm_sync_err++;
			continue;
		}
		dst = &bufs[src->pktid];
		dst->pktid = src->pktid;
		dst->len = src->len;
		dst->pa = src->pa;
		dst->va = src->dpa_va;
		dst->valid = true;
		fw->stat.rxbm_sync++;
	}
	spin_unlock(&fw->rx_lock);
	return 0;
}

struct noa_bm_map *rxbm_get_buffer_by_id(struct noa_wlan_fw *fw,
	u16 pktid)
{
	struct noa_bm_map *rxbm = fw->rxbm, *buf;

	if (pktid > fw->wlan_info.rx_pkt_max) {
		dev_err(&fw->dev, "Err packet id (%d)\n", pktid);
		return NULL;
	}

	spin_lock(&fw->rx_lock);
	buf = &rxbm[pktid];
	spin_unlock(&fw->rx_lock);
	return buf;
}

struct noa_bm_map *rxbm_get_buffer_and_invalid_by_id(struct noa_wlan_fw *fw,
	u16 pktid)
{
	struct noa_bm_map *buf;

	buf = rxbm_get_buffer_by_id(fw, pktid);
	if (!buf->valid)
		return NULL;
	buf->valid = false;
	return buf;
}

