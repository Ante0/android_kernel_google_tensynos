/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Handles WiFi-Calling offload related information.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Ken Chen <cken@google.com>
 */
#ifndef NOA_VPN_EGRESS_HANDLER_H_
#define NOA_VPN_EGRESS_HANDLER_H_

#include <linux/kconfig.h>  // for IS_ENABLED macro
#include <linux/netdevice.h>

// WFC packect offload's' control path is still depends on the XFRM offload,
// which is handled by the VPN offload functionality.
#if IS_ENABLED(CONFIG_NOA_VPN_OFFLOAD_SUPPORT) && IS_ENABLED(CONFIG_NOA_WFC_OFFLOAD_SUPPORT)

// Real implementation
void noa_vpn_egress_handler_init(void);
int noa_vpn_egress_hook_add(struct net *net, struct net_device *dev);
int noa_vpn_egress_hook_delete(struct net *net, struct net_device *dev);

#else

// Provide stub function(s) so that we don't need to wrap caller side with
// #ifdef everywhere.
static inline void noa_vpn_egress_handler_init(void)
{
}

static int noa_vpn_egress_hook_add(struct net *net, struct net_device *dev)
{
	return 0;
}

static int noa_vpn_egress_hook_delete(struct net *net, struct net_device *dev)
{
	return 0;
}

#endif

#endif  // NOA_VPN_EGRESS_HANDLER_H_
