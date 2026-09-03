/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_UTILITY_H__
#define __MTK_UTILITY_H__

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/preempt.h>
#include <net/netlink.h>
#include <net/sock.h>
#include "mtk_dev.h"

#define MTK_UEVENT_INFO_LEN 128

static inline void mtk_drv_delay(unsigned int n_us)
{
	if (in_interrupt())
		udelay(n_us);
	else
		usleep_range(n_us, n_us + 5);
}

/* MTK uevent */
enum mtk_uevent_id {
	MTK_UEVENT_UNDEF = 0,
	MTK_UEVENT_FSM = 1,
	MTK_UEVENT_MINIDUMP = 2,
	MTK_UEVENT_LOWPOWER = 3,
	MTK_UEVENT_MAX
};

static inline void mtk_uevent_notify(struct device *dev, enum mtk_uevent_id id, const char *info)
{
	char buf[MTK_UEVENT_INFO_LEN];
	char *ext[2] = {NULL, NULL};

	snprintf(buf, MTK_UEVENT_INFO_LEN, "%s:event_id=%d, info=%s",
		 dev->kobj.name, id, info);
	ext[0] = buf;
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, ext);
}

/* MTK netlink */
extern struct sock *mtk_netlink_sock;
#define MTK_NETLINK_SOCK			(NETLINK_USERSOCK)
#define MTK_NETLINK_GROUP			(22)

static inline int mtk_netlink_send_msg(int grp, const char *buf, int len)
{
	struct sk_buff *nl_skb;
	struct nlmsghdr *nlh;
	int err;

	if (!mtk_netlink_sock) {
		pr_err("Invalid mtk_netlink_sock\n");
		return -1;
	}

	nl_skb = nlmsg_new(len, GFP_KERNEL);
	if (!nl_skb) {
		pr_err("Failed to alloc netlink msg\n");
		return -1;
	}

	nlh = nlmsg_put(nl_skb, 0, 1, NLMSG_DONE, len, 0);
	if (!nlh) {
		pr_err("Failed to release netlink\n");
		nlmsg_free(nl_skb);
		return -1;
	}

	memcpy(nlmsg_data(nlh), buf, len);
	err = netlink_broadcast(mtk_netlink_sock, nl_skb,
				0, grp, GFP_KERNEL);

	return err;
}

static inline void mtk_netlink_init(void)
{
	mtk_netlink_sock = netlink_kernel_create(&init_net, MTK_NETLINK_SOCK, NULL);
	if (!mtk_netlink_sock)
		pr_err("Failed to create netlink socket\n");
}

static inline void mtk_netlink_uninit(void)
{
	if (mtk_netlink_sock) {
		netlink_kernel_release(mtk_netlink_sock);
		mtk_netlink_sock = NULL;
	}
}
#endif /* __MTK_UTILITY_H__ */
