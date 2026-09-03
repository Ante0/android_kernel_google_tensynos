// SPDX-License-Identifier: GPL-2.0-only
/* fork.c
 *
 * Android Vendor Hook Sched Fork Support
 *
 * Copyright 2026 Google LLC
 */
#include <linux/sched.h>
#include <linux/module.h>
#include <trace/hooks/dtask.h>

#include <kernel/sched/sched.h>
#include "sched_priv.h"

void vh_lock_task_fork_pixel_mod(void *data, struct task_struct *p)
{
	if (unlikely(!p))
		return;

	queue_delayed_notification(p, VENDOR_SCHED_CMD_TASK_FORK, 0, get_vendor_group(p));
}
