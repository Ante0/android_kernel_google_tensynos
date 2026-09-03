/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Handles VPN offload related information.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Mike Yu <yumike@google.com>
 */
#ifndef NOA_VPN_INGRESS_HANDLER_H_
#define NOA_VPN_INGRESS_HANDLER_H_

#include <linux/netdevice.h>

#if 0 // VPN CLAT
void noa_vpn_ingress_handler_init(void);
int noa_vpn_ingress_hook_add(struct net *net, struct net_device *dev);
int noa_vpn_ingress_hook_delete(struct net *net, struct net_device *dev);
#else
static inline void noa_vpn_ingress_handler_init(void)
{
}

static inline int noa_vpn_ingress_hook_add(struct net *net,
					   struct net_device *dev)
{
	return 0;
}

static inline int noa_vpn_ingress_hook_delete(struct net *net,
					      struct net_device *dev)
{
	return 0;
}
#endif

#endif  // NOA_VPN_INGRESS_HANDLER_H_
