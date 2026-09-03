/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NEP utility for NCP WiFi
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __NCP_WLAN_NEP_UTILITY_H__
#define __NCP_WLAN_NEP_UTILITY_H__

extern int nep_init(struct noa_wlan_fw *fw);
extern void nep_exit(struct noa_wlan_fw *fw);
extern int nep_flowid_table_add(struct noa_wlan_fw *fw, struct wlan_sta_info *sta, u8 pri);
extern int nep_flowid_table_remove_all(struct noa_wlan_fw *fw, struct wlan_sta_info *sta);
extern void nep_receiver(unsigned long data);
extern int nep_tx_packet(struct noa_wlan_fw *fw, struct noa_input_act *in_act);
extern int nep_replenish_tx_buffer(struct noa_wlan_fw *fw, u16 pktid);

#endif /* __NCP_WLAN_NEP_UTILITY_H__ */
