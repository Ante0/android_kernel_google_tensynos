// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 Google LLC
 */

#define pr_fmt(fmt) "ACFW debug: " fmt

#include <linux/arm-smccc.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/panic_notifier.h>

#include <asm/esr.h>

#define SMC_FC_OEM_DUMP_SMMU_EVT	0x8300000D

static struct dentry *debugfs_dentry;

static void acfw_log_dump_smmu(bool log_smc_call)
{
	struct arm_smccc_res res;

	/*
	 * Don't log smc calls if called from the panic notifier to minimize
	 * logging during panics.
	 */
	if (log_smc_call)
		pr_info("Calling SMC_FC_OEM_DUMP_SMMU_EVT\n");

	arm_smccc_smc(SMC_FC_OEM_DUMP_SMMU_EVT, 0, 0, 0, 0, 0, 0, 0, &res);

	if (log_smc_call)
		pr_info("SMC_FC_OEM_DUMP_SMMU_EVT call returned a0=%lx a1=%lx a2=%lx a3=%lx\n",
			res.a0, res.a1, res.a2, res.a3);
}

static bool active_serror(void)
{
	unsigned long esr = read_sysreg(esr_el1);

	return (ESR_ELx_EC(esr) == ESR_ELx_EC_SERROR);
}

static int acfw_debug_panic_notify(struct notifier_block *nb,
				   unsigned long action, void *data)
{
	/*
	 * Calls into ACFW go through TF-A which panics if there is an active
	 * SError. Therefore, let's only call into ACFW if there is no active
	 * SError.
	 */
	if (!active_serror())
		acfw_log_dump_smmu(false);

	return NOTIFY_OK;
}

static ssize_t log_dump_write(struct file *f, const char __user *buf,
			      size_t count, loff_t *off)
{
	acfw_log_dump_smmu(true);

	return count;
}

static const struct file_operations log_dump_fops = {
	.owner		= THIS_MODULE,
	.write		= log_dump_write,
};

static struct notifier_block panic_notifier = {
	.notifier_call = acfw_debug_panic_notify,
	/* We need to be notified before trusty_log */
	.priority = 1,
};

static int __init acfw_debug_init(void)
{
	int result;

	result = atomic_notifier_chain_register(&panic_notifier_list, &panic_notifier);
	if (result < 0) {
		pr_err("failed to register panic notifier: %d\n", result);
		return result;
	}

	debugfs_dentry = debugfs_create_file("acfw_log_dump", 0220, NULL, NULL, &log_dump_fops);
	if (IS_ERR(debugfs_dentry)) {
		pr_err("failed to create debugfs entry: %pe\n", debugfs_dentry);
		result = PTR_ERR(debugfs_dentry);
		goto out_unregister_handler;
	}

	return 0;

out_unregister_handler:
	atomic_notifier_chain_unregister(&panic_notifier_list, &panic_notifier);
	return result;
}

static void __exit acfw_debug_exit(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list, &panic_notifier);
	debugfs_remove(debugfs_dentry);
}

module_init(acfw_debug_init);
module_exit(acfw_debug_exit);

MODULE_DESCRIPTION("ACFW debug");
MODULE_AUTHOR("Daniel Mentz <danielmentz@google.com>");
MODULE_LICENSE("GPL");
