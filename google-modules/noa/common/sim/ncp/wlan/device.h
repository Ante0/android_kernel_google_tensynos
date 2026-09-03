/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NCP WiFi generic device utility
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __NCP_WLAN_DEVICE_GENERIC_H__
#define __NCP_WLAN_DEVICE_GENERIC_H__

enum {
	CHIP_ID_BRCM4389 = 0x4389,
	CHIP_ID_BRCM4390 = 0x4390,
};

extern void wdev_tx_ringbell(struct noa_wlan_fw *fw, unsigned long long flags);
extern int wdev_ring_init(struct noa_wlan_fw *fw,
	struct noa_wlan_cmd_ring_info *rings, struct noa_hw_ring *hw_rings);
extern void wdev_ring_exit(struct noa_wlan_fw *fw,
	struct noa_hw_ring *rings, u8 ring_num);
extern void wdev_release_irqs(struct noa_wlan_fw *fw);
extern int wdev_request_irqs(struct noa_wlan_fw *fw);
extern int wdev_tx(struct noa_wlan_fw *fw, struct noa_desc *desc,
	unsigned long long *flags);
extern int wdev_txq_active(struct noa_wlan_fw *fw, struct noa_hw_ring *tx_ring, bool enable);
extern int wdev_chip_register(struct noa_wlan_fw *fw, u32 type);
extern struct wlan_sta_info *lookup_sta_by_da(struct noa_wlan_fw *fw, char *da, const u32 oif);
extern int wdev_rx_post(struct noa_wlan_fw *fw, u32 count, struct noa_bm_buf *bufs);

typedef int wdev_rx_fun(struct noa_wlan_fw *fw, struct noa_hw_ring *ring, void *d);

extern wdev_rx_fun *wdev_rx_cb[RX_TYPE_MAX];

#if IS_ENABLED(CONFIG_NOA_WLAN_BRCM_SUPPORT)
extern void device_brcm_init(struct noa_wlan_fw *fw, u32 type);
#endif /* CONFIG_NOA_WLAN_BRCM_SUPPORT */
#if IS_ENABLED(CONFIG_NOA_WLAN_QCA_SUPPORT)
extern void device_qca_init(struct noa_wlan_fw *fw, u32 type);
#endif /* CONFIG_NOA_WLAN_QCA_SUPPORT */
#endif /* __NCP_WLAN_DEVICE_GENERIC_H__ */