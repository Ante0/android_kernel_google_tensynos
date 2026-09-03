// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <net/sock.h>

#include "google_dpa_netlink.h"

#define GOOGLE_DPA_NETLINK_FAMILY 30

struct google_dpa_netlink {
	struct sock *sk;
	u32 pid;
};

static void google_dpa_netlink_recv_msg(struct sk_buff *skb)
{
	struct google_dpa *dpa = skb->sk->sk_user_data;
	struct nlmsghdr *nlh = (struct nlmsghdr *)skb->data;

	if (dpa && nlh)
		dpa->netlink->pid = nlh->nlmsg_pid;
}

int google_dpa_netlink_init(struct google_dpa *dpa)
{
	struct google_dpa_netlink *netlink =
		devm_kzalloc(dpa->dev, sizeof(struct google_dpa_netlink), GFP_KERNEL);
	struct netlink_kernel_cfg cfg = {
		.input = google_dpa_netlink_recv_msg,
	};

	if (!netlink) {
		dev_err(dpa->dev, "Failed to allocate netlink.");
		return -ENOMEM;
	}

	netlink->sk = netlink_kernel_create(&init_net, GOOGLE_DPA_NETLINK_FAMILY, &cfg);
	if (!netlink->sk) {
		dev_err(dpa->dev, "Failed to create Netlink socket.");
		return -ENOMEM;
	}
	netlink->sk->sk_user_data = dpa;
	dpa->netlink = netlink;

	return 0;
}

void google_dpa_netlink_deinit(struct google_dpa *dpa)
{
	if (dpa->netlink && dpa->netlink->sk) {
		netlink_kernel_release(dpa->netlink->sk);
		dpa->netlink->sk = NULL;
		dpa->netlink->pid = 0;
	}
}

int google_dpa_netlink_send_data(struct google_dpa *dpa, void *data, size_t size)
{
	struct sk_buff *skb_out;
	struct nlmsghdr *nlh;
	int res;

	if (!dpa->netlink || !dpa->netlink->pid)
		return -EINVAL;

	skb_out = nlmsg_new(size, 0);
	if (!skb_out) {
		dev_err(dpa->dev, "Failed to allocate sk_buff.");
		return -ENOMEM;
	}

	nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, size, 0);
	if (!nlh) {
		dev_err(dpa->dev, "Failed to put Netlink header.");
		res = -ENOMEM;
		goto err_free_skb_out;
	}

	memcpy(nlmsg_data(nlh), data, size);

	res = nlmsg_unicast(dpa->netlink->sk, skb_out, dpa->netlink->pid);
	if (res < 0) {
		dev_err(dpa->dev, "Error sending Netlink message: %d", res);
		goto err_free_skb_out;
	}

err_free_skb_out:
	nlmsg_free(skb_out);
	return res;
}
