/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Handles VPN offload related information.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Mike Yu <yumike@google.com>
 */
#ifndef NOA_VPN_MANAGER_H_
#define NOA_VPN_MANAGER_H_

#include <linux/netdevice.h>

#include <common/core.h>

void noa_vpn_manager_init(void);

struct xfrm_state *lookup_sa_by_ipsec_handle(u32 ipsec_handle);

// A function exposed to HW drivers for setting xfrmdev_ops on their net_device.
void noa_vpn_manager_setup_xfrmdev_ops(struct net_device *net);

// In RX data path, when modem/wifi drivers receive a packet, they need to know whether the packet
// has been handled by HW. The information is available from `metadata` written in HW.
void noa_vpn_manager_handle_ipsec_offload_rx_skb(struct sk_buff *skb,
						 const struct noa_rx_ipsec_metadata *metadata);

// In TX data path, when modem/wifi drivers are about to send a packet to NOA, the ipsec metadata
// should be written to the NOA descriptor.
void noa_vpn_manager_handle_ipsec_offload_tx_skb(const struct sk_buff *skb,
						 struct noa_tx_ipsec_metadata *metadata);

// This functions checks if the provided packet is vpn-offloaded.
bool is_ipsec_offload_tx_packet(const struct sk_buff *skb);

// A function exposed for dynamic switch to switch offload from enabled to disabled.
void do_switch_migrate(void);
#endif  // NOA_VPN_MANAGER_H_
