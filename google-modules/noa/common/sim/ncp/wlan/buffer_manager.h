/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NCP WiFi buffer manager
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __NCP_WLAN_BUFFER_MANAGER_H__
#define __NCP_WLAN_BUFFER_MANAGER_H__

#include "ncp_wlan_fw.h"

extern int txbm_init(struct noa_wlan_fw *fw, u32 tx_pkt_max);
extern void txbm_exit(struct noa_wlan_fw *fw);
extern int txbm_sync(struct noa_wlan_fw *fw, u16 pktid);
extern bool txbm_pktid_rxsync(struct noa_wlan_fw *fw, u16 pktid);
extern bool txbm_pktid_txsync(struct noa_wlan_fw *fw, u16 pktid);

extern int rxbm_init(struct noa_wlan_fw *fw, u32 rx_pkt_max);
extern void rxbm_exit(struct noa_wlan_fw *fw);
extern int rxbm_sync(struct noa_wlan_fw *fw, int count, struct noa_bm_buf *src_bufs);
extern bool rxbm_pktid_rxsync(struct noa_wlan_fw *fw, u16 pktid);
extern bool rxbm_pktid_txsync(struct noa_wlan_fw *fw, u16 pktid);
extern struct noa_bm_map *rxbm_get_buffer_and_invalid_by_id(struct noa_wlan_fw *fw,
	u16 pktid);
extern struct noa_bm_map *rxbm_get_buffer_by_id(struct noa_wlan_fw *fw,
	u16 pktid);
extern void txbm_set_bufs(struct noa_wlan_fw *fw, int count, struct noa_bm_buf *src_bufs);
#endif /* __NCP_WLAN_BUFFER_MANAGER_H__ */
