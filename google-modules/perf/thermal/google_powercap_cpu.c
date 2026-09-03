// SPDX-License-Identifier: GPL-2.0-only
/*
 * google_powercap_cpu.c driver to register CPU powercap.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "google_powercap_cpu.h"

static int gpc_cpu_setup(struct gpowercap *parent, struct device_node *np,
			 enum hw_dev_type cdev_id, const char *odpm_rail_name,
			 u64 polling_interval_ms)
{
	int cpu;

	cpu = of_cpu_node_to_id(np);
	if (cpu < 0)
		return 0;
	return __gpc_cpu_setup(cpu, parent, cdev_id, odpm_rail_name, polling_interval_ms);
}

struct gpowercap_subsys_ops gpc_cpu_dev_ops = {
	.name = "google_powercap_cpus",
	.setup = gpc_cpu_setup,
};
