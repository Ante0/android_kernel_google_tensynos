
// SPDX-License-Identifier: GPL-2.0-only
/* sched_netlink.c
 *
 * Android Vendor Hook Sched Generic Netlink Subsystem
 *
 * Copyright 2026 Google LLC
 */
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/sched/task.h>
#include <linux/cred.h>
#include <net/genetlink.h>

#include <kernel/sched/sched.h>
#include "sched_priv.h"

/* === Generic Netlink Definitions for VENDOR_SCHED === */
enum {
	VENDOR_SCHED_ATTR_UNSPEC,
	VENDOR_SCHED_ATTR_TGID,
	VENDOR_SCHED_ATTR_UID,
	VENDOR_SCHED_ATTR_CMDLINE,
	VENDOR_SCHED_ATTR_OLD_GROUP,
	VENDOR_SCHED_ATTR_NEW_GROUP,
	VENDOR_SCHED_ATTR_TID,         /* Thread TID */
	__VENDOR_SCHED_ATTR_MAX,
};
#define VENDOR_SCHED_ATTR_MAX (__VENDOR_SCHED_ATTR_MAX - 1)

enum vendor_sched_multicast_groups {
	VENDOR_SCHED_MCGRP_GROUP_MIGRATION, /* Thread Vendor Group Migration Notification Multicast Group */
	VENDOR_SCHED_MCGRP_TASK_FORK,  /* Thread Fork Notification Multicast Group */
	VENDOR_SCHED_MCGRP_TASK_EXIT,  /* Thread Exit Notification Multicast Group */
	VENDOR_SCHED_MCGRP_TASK_RENAME, /* Thread Rename Notification Multicast Group */
};


/* 1. Netlink Attribute Policy and Multicast Group Definitions */
const struct nla_policy vendor_sched_genl_policy[VENDOR_SCHED_ATTR_MAX + 1] = {
	[VENDOR_SCHED_ATTR_TGID] = { .type = NLA_U32 },
	[VENDOR_SCHED_ATTR_UID] = { .type = NLA_U32 },
	[VENDOR_SCHED_ATTR_CMDLINE] = { .type = NLA_NUL_STRING },
	[VENDOR_SCHED_ATTR_OLD_GROUP] = { .type = NLA_U32 },
	[VENDOR_SCHED_ATTR_NEW_GROUP] = { .type = NLA_U32 },
	[VENDOR_SCHED_ATTR_TID] = { .type = NLA_U32 },
};

const struct genl_multicast_group vendor_sched_mcgrps[] = {
	[VENDOR_SCHED_MCGRP_GROUP_MIGRATION] = { .name = "group_migration", },
	[VENDOR_SCHED_MCGRP_TASK_FORK] = { .name = "task_fork", },
	[VENDOR_SCHED_MCGRP_TASK_EXIT] = { .name = "task_exit", },
	[VENDOR_SCHED_MCGRP_TASK_RENAME] = { .name = "task_rename", },
};

static struct genl_family vendor_sched_gnl_family = {
	.name = "VENDOR_SCHED",
	.version = 1,
	.maxattr = VENDOR_SCHED_ATTR_MAX,
	.policy = vendor_sched_genl_policy,
	.module = THIS_MODULE,
	.mcgrps = vendor_sched_mcgrps,
	.n_mcgrps = ARRAY_SIZE(vendor_sched_mcgrps),
};

int vh_sched_netlink_init(void)
{
	return genl_register_family(&vendor_sched_gnl_family);
}

struct vendor_sched_notification_work {
	struct delayed_work dwork;
	struct task_struct *task;
	enum vendor_sched_cmd cmd;
	int old_group;
	int new_group;
};

void send_netlink_notification(struct task_struct *p, struct work_struct *work,
			       enum vendor_sched_cmd cmd)
{
	struct sk_buff *skb;
	void *msg_head;
	int ret = 0, mcgrp, old_group = -1, new_group = -1;
	gfp_t flags = work ? GFP_KERNEL : GFP_ATOMIC;
	const struct cred *cred;
	uid_t uid = -1;
	char cmdline[VENDOR_CMDLINE_LEN] = {0};

	if (work) {
		struct vendor_sched_notification_work *vs_work =
			container_of(to_delayed_work(work),
				     struct vendor_sched_notification_work, dwork);
		new_group = vs_work->new_group;
		old_group = vs_work->old_group;
	} else {
		new_group = get_vendor_group(p);
	}

	skb = genlmsg_new(NLMSG_GOODSIZE, flags);
	if (!skb)
		return;

	msg_head = genlmsg_put(skb, 0, 0, &vendor_sched_gnl_family, 0, cmd);
	if (!msg_head)
		goto free_skb;

	switch (cmd) {
	case VENDOR_SCHED_CMD_GROUP_MIGRATION:
		if (nla_put_u32(skb, VENDOR_SCHED_ATTR_OLD_GROUP, old_group))
			goto nla_put_failure;
		fallthrough;
	case VENDOR_SCHED_CMD_TASK_FORK:
		if (nla_put_u32(skb, VENDOR_SCHED_ATTR_NEW_GROUP, new_group))
			goto nla_put_failure;
		fallthrough;
	case VENDOR_SCHED_CMD_TASK_RENAME:
		if (p->pid == p->tgid)
			get_cmdline(p, cmdline, sizeof(cmdline) - 1);
		else
			__get_task_comm(cmdline, sizeof(cmdline) - 1, p);

		rcu_read_lock();
		cred = __task_cred(p);
		if (cred)
			uid = cred->uid.val;
		rcu_read_unlock();

		if (nla_put_string(skb, VENDOR_SCHED_ATTR_CMDLINE, cmdline) ||
			nla_put_u32(skb, VENDOR_SCHED_ATTR_UID, uid))
			goto nla_put_failure;
		fallthrough;
	default:
		if (nla_put_u32(skb, VENDOR_SCHED_ATTR_TGID, p->tgid) ||
		    nla_put_u32(skb, VENDOR_SCHED_ATTR_TID, p->pid))
			goto nla_put_failure;
		break;
	}

	switch (cmd) {
	case VENDOR_SCHED_CMD_TASK_EXIT:
		mcgrp = VENDOR_SCHED_MCGRP_TASK_EXIT;
		break;
	case VENDOR_SCHED_CMD_TASK_FORK:
		mcgrp = VENDOR_SCHED_MCGRP_TASK_FORK;
		break;
	case VENDOR_SCHED_CMD_TASK_RENAME:
		mcgrp = VENDOR_SCHED_MCGRP_TASK_RENAME;
		break;
	case VENDOR_SCHED_CMD_GROUP_MIGRATION:
		mcgrp = VENDOR_SCHED_MCGRP_GROUP_MIGRATION;
		break;
	default:
		pr_warn("unknown sched netlink cmd: %d\n", cmd);
		return;
	}

	genlmsg_end(skb, msg_head);

	ret = genlmsg_multicast(&vendor_sched_gnl_family, skb, 0, mcgrp, flags);
	if (ret < 0 && ret != -ESRCH)
		pr_warn("failed to send netlink notification (%d): %d\n", cmd, ret);

	return;

nla_put_failure:
	genlmsg_cancel(skb, msg_head);
free_skb:
	nlmsg_free(skb);
}

static void vendor_sched_notification_work_handler(struct work_struct *work)
{
	struct vendor_sched_notification_work *vs_work =
		container_of(to_delayed_work(work), struct vendor_sched_notification_work, dwork);
	struct task_struct *p = vs_work->task;

	if (likely(!(p->flags & PF_EXITING)))
		send_netlink_notification(p, work, vs_work->cmd);

	put_task_struct(p);
	kfree(vs_work);
}

void queue_delayed_notification(struct task_struct *p, enum vendor_sched_cmd cmd,
			       int old_group, int new_group)
{
	struct vendor_sched_notification_work *vs_work;

	vs_work = kmalloc(sizeof(*vs_work), GFP_ATOMIC);
	if (!vs_work)
		return;

	get_task_struct(p);
	vs_work->task = p;
	vs_work->cmd = cmd;
	vs_work->old_group = old_group;
	vs_work->new_group = new_group;

	INIT_DELAYED_WORK(&vs_work->dwork, vendor_sched_notification_work_handler);
	queue_delayed_work(system_wq, &vs_work->dwork, msecs_to_jiffies(VENDOR_SCHED_NETLINK_DELAY_MS));
}

