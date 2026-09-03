// SPDX-License-Identifier: GPL-2.0-only
/*
 * google_powercap_devfreq.c driver to register CPU powercap.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "google_powercap_devfreq.h"

static int gpc_devfreq_setup(struct gpowercap *gpowercap, struct device_node *np,
			     enum hw_dev_type cdev_id, const char *odpm_rail_name,
			     u64 polling_interval_ms)
{
	return __gpc_devfreq_setup(gpowercap, np, cdev_id,
					odpm_rail_name, polling_interval_ms);
}

struct gpowercap_subsys_ops gpc_devfreq_dev_ops = {
	.name = KBUILD_MODNAME,
	.setup = gpc_devfreq_setup,
};
