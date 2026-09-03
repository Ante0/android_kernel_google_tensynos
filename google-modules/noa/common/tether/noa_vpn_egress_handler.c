// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handles WiFi-Calling offload related information.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Ken Chen <cken@google.com>
 */

 #include "noa_vpn_egress_handler.h"

#include <linux/netfilter.h>
#include <net/xfrm.h>

// `vpn_dev` points to the current VPN net_device. If there is another VPN net_device
// about to up, `vpn_dev` will still point to the original net_device and won't be
// reassigned to the new one.
// TODO: Support multi VPN net_devices.
static struct net_device *vpn_dev;

// TODO: Support multi net_devices.
static struct net_device *modem_dev;
static struct net_device *wifi_dev;

#define NOA_VPN_MODEM_DEV 1
#define NOA_VPN_WIFI_DEV 2

static bool is_ipsec_dev(const struct net_device *dev)
{
	// The rtnl_link identifier for ipsec net_device is "xfrm".
	return dev->rtnl_link_ops && strncmp("xfrm", dev->rtnl_link_ops->kind, 4) == 0;
}

static int xfrm_dev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_REGISTER:
		if (!vpn_dev && is_ipsec_dev(dev))
			vpn_dev = dev;
		break;
	case NETDEV_UNREGISTER:
		if (vpn_dev && vpn_dev == dev)
			vpn_dev = NULL;
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block xfrm_dev_notifier = {
	.notifier_call	= xfrm_dev_event,
};

void noa_vpn_egress_handler_init(void)
{
	// TODO: Register only once with noa_vpn_ingress_handler_init.
	register_netdevice_notifier(&xfrm_dev_notifier);
}

// For debugging purpose. Uncomment when need.
// static void print_l4_checksum(struct iphdr *iph) {
// 	void *l4_header;

// 	if (!iph) {
// 		return;
// 	}

// 	l4_header = (void *)iph + (iph->ihl * 4);
// 	if (iph->protocol == IPPROTO_TCP) {
// 		struct tcphdr *tcph = (struct tcphdr *)l4_header;
// 		pr_info("%s:  -> TCP checksum = 0x%04x\n", __func__, ntohs(tcph->check));
// 	} else if (iph->protocol == IPPROTO_UDP) {
// 		struct udphdr *udph = (struct udphdr *)l4_header;
// 		pr_info("%s:  -> UDP checksum = 0x%04x\n", __func__, ntohs(udph->check));
// 	} else {
// 		pr_info("%s:  -> Not TCP/UDP (Protocol: %u)\n", __func__, iph->protocol);
// 	}
// }

static unsigned int output_gso(const struct net_device_ops *ops,
			       struct net_device *dev, struct sk_buff *skb)
{
	struct sk_buff *segs, *nskb;

	BUILD_BUG_ON(sizeof(*IPCB(skb)) > SKB_GSO_CB_OFFSET);
	BUILD_BUG_ON(sizeof(*IP6CB(skb)) > SKB_GSO_CB_OFFSET);
	segs = skb_gso_segment(skb, 0);
	kfree_skb(skb);

	if (IS_ERR(segs)) {
		pr_err("%s: skb_gso_segment failed: %ld\n", __func__, PTR_ERR(segs));
		return PTR_ERR(segs);
	}

	if (segs == NULL) {
		return -EINVAL;
	}

	skb_list_walk_safe(segs, segs, nskb) {
		skb_mark_not_on_list(segs);
		// TODO: check return value and kfree_skb_list(nskb) on error.
		pr_info("%s: ndo_start_xmit\n", __func__);
		ops->ndo_start_xmit(segs, dev);
	}

	return 0;
}

static unsigned int maybe_redirect_egress_packet(void *priv, struct sk_buff *skb,
					  const struct nf_hook_state *state)
{
	const struct net_device_ops *ops = NULL;
	struct iphdr *iph;

	if (!skb) {
		pr_err("%s: sbk is null\n", __func__);
		return NF_ACCEPT;
	}

	// TODO: Look up SP/SA and select the corresponding underlying network device
	// rather than redirect to modem unconditionally.
	if (modem_dev) {
		ops = modem_dev->netdev_ops;
		if (!ops) {
			pr_err("%s: Underlying netdev not found.\n", __func__);
			return NF_ACCEPT;
		}

		// Segment GSO packet and send. The checksum calculateion is handled in the
		// segmentation procedures.
		if (skb_is_gso(skb)) {
			pr_info("%s: skb_is_gso\n", __func__);
			if (output_gso(ops, modem_dev, skb) == 0) {
				return NF_STOLEN;
			}
			// segmentation failed.
			return NF_ACCEPT;
		}

		// For non-GSO packets, update checksum if needed.
		// The full L4 checksum is calculated by partial checksum plus UDP/TCP
		// header and UDP payload. When checksum offload is set, the kernel fills
		// only partial checksum (based on information from IP header). The full
		// checksum is expected to be handled by the network device hardware.
		// However, in the IPSec case, the inner header checksum needs to be done
		// prior to the encryption. Otherwise, there is no way to retrieve the inner
		// IP and UDP/TCP headers for checksum calculation. XFRM does the related
		// handling in the `xfrm_output`. For the netfilter egress hook ipsec packet
		// offload solution, the equivalent needs to be done here as the NOA IPsec
		// engine does not support L4 checksum offloading.
		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			iph = ip_hdr(skb);
			if (!iph) {
				pr_err("%s: IP header not found.\n", __func__);
				return NF_ACCEPT;
			}

			// print_l4_checksum(iph);
			if (skb_checksum_help(skb)) {
				pr_err("%s: checksum fail.\n", __func__);
			}
			// print_l4_checksum(iph);
		}

		ops->ndo_start_xmit(skb, modem_dev);
		return NF_STOLEN;
	}

	return NF_ACCEPT;
}

static struct nf_hook_ops noa_vpn_modem_nfops = {
	.hook     = maybe_redirect_egress_packet,
	.pf       = NFPROTO_NETDEV,
	.hooknum  = NF_NETDEV_EGRESS,
	.priority = INT_MAX,
};

static struct nf_hook_ops noa_vpn_wifi_nfops = {
	.hook     = maybe_redirect_egress_packet,
	.pf       = NFPROTO_NETDEV,
	.hooknum  = NF_NETDEV_EGRESS,
	.priority = INT_MAX,
};

static int dev_hw_type(const struct net_device *dev)
{
	if (!strncmp("rmnet", dev->name, 5))
		return NOA_VPN_MODEM_DEV;

	if (!strncmp("wlan", dev->name, 4))
		return NOA_VPN_WIFI_DEV;

	return -1;
}

int noa_vpn_egress_hook_add(struct net *net, struct net_device *dev)
{
	struct net_device **hw_dev;
	struct nf_hook_ops *hw_nfops;

	switch (dev_hw_type(dev)) {
	case NOA_VPN_MODEM_DEV:
		hw_dev = &modem_dev;
		hw_nfops = &noa_vpn_modem_nfops;
		break;
	case NOA_VPN_WIFI_DEV:
		hw_dev = &wifi_dev;
		hw_nfops = &noa_vpn_wifi_nfops;
		break;
	default:
		return -EOPNOTSUPP;
	}

	// Current implementation supports single wifi/modem net_device for now.
	// Multiple IPsec ipsec on the same HW type of net_device is not supported.
	if (*hw_dev)
		return (*hw_dev == dev) ? 0 : -ENOSPC;

	*hw_dev = dev;
	hw_nfops->dev = dev;

	return nf_register_net_hook(net, hw_nfops);
}

int noa_vpn_egress_hook_delete(struct net *net, struct net_device *dev)
{
	struct net_device **hw_dev;
	struct nf_hook_ops *hw_nfops;

	switch (dev_hw_type(dev)) {
	case NOA_VPN_MODEM_DEV:
		hw_dev = &modem_dev;
		hw_nfops = &noa_vpn_modem_nfops;
		break;
	case NOA_VPN_WIFI_DEV:
		hw_dev = &wifi_dev;
		hw_nfops = &noa_vpn_wifi_nfops;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (*hw_dev != dev)
		return -ESRCH;

	nf_unregister_net_hook(net, hw_nfops);

	*hw_dev = NULL;
	hw_nfops->dev = NULL;

	return 0;
}
