// SPDX-License-Identifier: GPL-2.0-only
/* init.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2021 Google LLC
 */

#include <linux/module.h>
#include <trace/hooks/cgroup.h>

#include "cgroup.h"

static int vh_cgroup_init(void)
{
	int ret;

	ret = register_trace_android_rvh_cgroup_force_kthread_migration(
		rvh_cgroup_force_kthread_migration_pixel_mod, NULL);
	if (ret)
		return ret;

	return 0;
}

module_init(vh_cgroup_init);
MODULE_LICENSE("GPL v2");
