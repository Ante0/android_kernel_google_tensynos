// SPDX-License-Identifier: GPL-2.0-only
/*
 * goog-cpu-idle-info.c - record psci crumbs module
 *
 * Copyright 2024 Google LLC
 */
#include "linux/cpu_pm.h"
#include "linux/notifier.h"
#include "soc/google/google-cdd.h"
#include <linux/module.h>

static int cpu_pm_notifier(struct notifier_block *nb, unsigned long action,
			   void *data)
{
	if (action == CPU_PM_EXIT)
		google_cdd_update_psci_crumbs();

	return NOTIFY_DONE;
}

static struct notifier_block cpu_pm_nb = {
	.notifier_call = cpu_pm_notifier
};

static int cpu_idle_logs_probe(void)
{
	int err;

	err = cpu_pm_register_notifier(&cpu_pm_nb);
	if (err)
		pr_warn("Failed to register CPU PM notifier\n");

	return err;
}

static void cpu_idle_logs_remove(void)
{
	int err;

	err = cpu_pm_unregister_notifier(&cpu_pm_nb);
	if (err)
		pr_warn("Failed to unregister CPU PM notifier\n");
}

module_init(cpu_idle_logs_probe);
module_exit(cpu_idle_logs_remove);

MODULE_AUTHOR("Mariano Marciello <mmarciello@google.com>");
MODULE_DESCRIPTION("record psci crumbs module");
MODULE_LICENSE("GPL");
