// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handles VPN offload related information.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Mike Yu <yumike@google.com>
 */

#include "noa_vpn_ingress_handler.h"
#include "noa_vpn_manager.h"

#include <linux/netfilter.h>
#include <net/xfrm.h>

static bool is_clat_dev(const struct net_device *dev)
{
	return strncmp("v4-", dev->name, 3) == 0;
}

static int xfrm_dev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_REGISTER:
		if (is_clat_dev(dev))
			noa_vpn_manager_setup_xfrmdev_ops(dev);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block xfrm_dev_notifier = {
	.notifier_call = xfrm_dev_event,
};

static unsigned int ingress_hook_clat(void *priv, struct sk_buff *skb,
				      const struct nf_hook_state *state)
{
	struct xfrm_state *sa;
	struct sec_path *sp;
	struct xfrm_offload *xo;
	const u64 ipsec_handle = skb_shinfo(skb)->android_oem_data1[0];

	if (ipsec_handle == 0)
		return NF_ACCEPT;

	// When the VPN net_device just gets down, the VPN packets would re-enter this ingress
	// hook function. Since xfrmi[SA->if_id] is deleted, skb->dev in the XFRM stack can't be
	// set to the VPN net_device. skb->dev remains unchanged after the XFRM stack, which leads
	// to the re-entrance. The re-entrance causes xfrm_offload() to return null because sp->olen
	// is 0 while sp->len is not 0. This would last until the SA deletion event is delivered to
	// NOA.
	// In this case, although it's fine to simply ignore the packet, it makes more sense to
	// drop the packet to avoid the packet being received on the clat network interface.
	if (secpath_exists(skb))
		return NF_DROP;

	sa = lookup_sa_by_ipsec_handle(ipsec_handle);
	if (!sa)
		return NF_ACCEPT;

	xfrm_state_hold(sa);

	sp = secpath_set(skb);
	sp->xvec[sp->len++] = sa;
	sp->olen++;

	xo = xfrm_offload(skb);
	xo->flags = CRYPTO_DONE;
	xo->status = CRYPTO_SUCCESS;

	return NF_ACCEPT;
}

static struct nf_hook_ops noa_vpn_clat_nfops = {
	.hook     = ingress_hook_clat,
	.pf       = NFPROTO_NETDEV,
	.hooknum  = NF_NETDEV_INGRESS,
	.priority = INT_MAX,
};

int noa_vpn_ingress_hook_add(struct net *net, struct net_device *dev)
{
	// The VPN ingress hook is used for clat network dev only.
	if (!is_clat_dev(dev))
		return 0;

	// Avoid adding duplicate hook on the same device.
	// Current implementation doesn't support adding the hook on multiple devices. If
	// that happens, an error ENOSPC is returned to the caller.
	if (noa_vpn_clat_nfops.dev)
		return (noa_vpn_clat_nfops.dev == dev) ? 0 : -ENOSPC;

	noa_vpn_clat_nfops.dev = dev;
	return nf_register_net_hook(net, &noa_vpn_clat_nfops);
}

int noa_vpn_ingress_hook_delete(struct net *net, struct net_device *dev)
{
	// The VPN ingress hook is used for clat network dev only.
	if (!is_clat_dev(dev))
		return 0;

	if (!noa_vpn_clat_nfops.dev)
		return 0;

	nf_unregister_net_hook(net, &noa_vpn_clat_nfops);
	noa_vpn_clat_nfops.dev = NULL;

	return 0;
}

void noa_vpn_ingress_handler_init(void)
{
	register_netdevice_notifier(&xfrm_dev_notifier);
}
