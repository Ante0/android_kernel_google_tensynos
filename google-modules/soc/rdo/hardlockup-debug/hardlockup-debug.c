// SPDX-License-Identifier: GPL-2.0-only

#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/panic_notifier.h>
#include <linux/percpu.h>
#include <linux/platform_device.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/oom.h>

#include <linux/suspend.h>
#include <linux/sched/debug.h>
#include <trace/hooks/cpuidle.h>

#include <asm/debug-monitors.h>
#include <asm/stacktrace.h>
#include <soc/google/google-cdd.h>
#include <soc/google/google-smc.h>
#include <soc/google/meminfo.h>

#include "hardlockup-debug.h"

#define BUG_BRK_IMM_HARDLOCKUP (0x801)

#define SMC_CMD_LOCKUP_NOTICE (0xC3000004)
#define SMC_CMD_KERNEL_PANIC_NOTICE (0xC3000005)

#define CPU_MASK_VALID (0x4F4E << 16)
#define CPU_MASK_VALID_BITS (0xFFFF0000)
#define CPU_MASK_BITS (0xFFFF)
#define RAMDUMP_MODE_DISABLE 0

static raw_spinlock_t hardlockup_seq_lock;
static raw_spinlock_t hardlockup_log_lock;
static int watchdog_fiq;
static int allcorelockup_detected;
static unsigned long hardlockup_core_mask;
static unsigned long hardlockup_core_handled_mask;
static bool ramdump_is_enabled;

enum hardlockup_magic {
	HARDLOCKUP_WDT_SPI_BEGIN = 0x44544152,
	HARDLOCKUP_WDT_SGI_ONLINE,
	HARDLOCKUP_WDT_SPI_NOT_KERNEL,
	HARDLOCKUP_WDT_FLUSH_TIMEOUT,
	HARDLOCKUP_WDT_SPI_KERNEL,
	HARDLOCKUP_WDT_SPI_C2,
	HARDLOCKUP_WDT_SGI_BEGIN,
	HARDLOCKUP_WDT_SGI_NOT_KERNEL,
	HARDLOCKUP_WDT_SGI_KERNEL,
	HARDLOCKUP_WDT_SGI_C2,
};

static struct task_struct *pm_suspend_task;
static DEFINE_SPINLOCK(pm_suspend_task_lock);
static unsigned long pm_panic_once;

static void hardlockup_debug_bug_func(void)
{
	do {
		asm volatile(__stringify(__BUG_ENTRY(0);
					 brk BUG_BRK_IMM_HARDLOCKUP));
		unreachable();
	} while (0);
}

static unsigned int get_fiq_pending_cpu_id(void)
{
	unsigned int val = google_cdd_get_fiq_pending_core();

	if ((val & CPU_MASK_VALID_BITS) == CPU_MASK_VALID)
		return val & CPU_MASK_BITS;
	else
		return -1;
}

static void vh_bug_on_wdt_fiq_pending(void *data, int state, struct cpuidle_device *dev)
{
	unsigned int cpu = raw_smp_processor_id();

	if (cpu == get_fiq_pending_cpu_id()) {
		pr_emerg("CPU %d woken up from C2 due to WDT FIQ\n", cpu);
		hardlockup_debug_bug_func();
	}
}

static void hardlockup_debug_disable_fiq(void)
{
	asm volatile("msr	daifset, #0x1");
}

static void hardlockup_debug_spin_func(void)
{
	do {
		wfi();
	} while (1);
}

static inline int hardlockup_debug_try_lock_timeout(raw_spinlock_t *lock,
						    long timeout_us)
{
	int ret;
	ktime_t timeout = ktime_add_us(ktime_get(), timeout_us);

	for (;;) {
		ret = raw_spin_trylock(lock);
		if (ret)
			break;
		if (timeout_us && ktime_compare(ktime_get(), timeout) > 0) {
			ret = raw_spin_trylock(lock);
			break;
		}
		udelay(1);
	}

	return ret;
}

static bool is_valid_hardlockup_magic(int val)
{
	return val == HARDLOCKUP_WDT_SPI_KERNEL ||
	       val == HARDLOCKUP_WDT_SGI_KERNEL ||
	       val == HARDLOCKUP_WDT_SPI_C2 || val == HARDLOCKUP_WDT_SGI_C2;
}

static bool is_valid_hardlockup_mask(void)
{
	u32 mask = google_cdd_get_hardlockup_mask() & CPU_MASK_VALID_BITS;

	return mask == CPU_MASK_VALID;
}

static unsigned long hardlockup_debug_get_locked_cpu_mask(void)
{
	return google_cdd_get_hardlockup_mask() & CPU_MASK_BITS;
}

static int hardlockup_debug_bug_handler(struct pt_regs *regs, unsigned long esr)
{
	static atomic_t dump_tasks_once = ATOMIC_INIT(1);
	int cpu = raw_smp_processor_id();
	unsigned int val;
	unsigned long last_pc;
	int log_lock_acquired;
	bool pm_panic = false;

	hardlockup_debug_disable_fiq();

	/*
	 * We enter this handler in two cases:
	 * Case 1: All cores locked up and watchdog expired. In that case
	 * hardlockup_core_mask is not set. Kernel reads the information
	 * provided by EL3 SW to set the hardlockup_core_mask.
	 * Case 2: Kernel hits a panic, on panic kernel sends IPI_CPU_STOP to
	 * all online cores. If cores cannot handle IPI_CPU_STOP then they are
	 * locked up. The panic handler sets the hardlockup_core_mask to point
	 * to all online cores.
	 */
	log_lock_acquired = hardlockup_debug_try_lock_timeout(&hardlockup_seq_lock,
			 500 * USEC_PER_MSEC);
	if (log_lock_acquired && !hardlockup_core_mask) {
		if (watchdog_fiq && !allcorelockup_detected) {
			/* 1st WDT FIQ trigger */
			val = google_cdd_get_hardlockup_magic(cpu);
			/* Magic to ensure debug handler is triggered by watchdog fiq. */
			if (is_valid_hardlockup_magic(val) && is_valid_hardlockup_mask()) {
				allcorelockup_detected = 1;
				hardlockup_core_mask = hardlockup_debug_get_locked_cpu_mask();
			} else {
				pr_emerg("%s: invalid magic (%d) or mask (%d) from el3 fiq handler\n",
					__func__,
					val,
					google_cdd_get_hardlockup_mask() & CPU_MASK_VALID_BITS);
				raw_spin_unlock(&hardlockup_seq_lock);
				return DBG_HOOK_ERROR;
			}
		}
	}
	if (log_lock_acquired)
		raw_spin_unlock(&hardlockup_seq_lock);
	else
		pr_debug("%s: fail to get seq lock\n", __func__);

	/* Replace real pc value even if it is invalid */
	last_pc = google_cdd_get_last_pc(cpu);
	if (get_fiq_pending_cpu_id() != cpu)
		regs->pc = last_pc;

	log_lock_acquired = hardlockup_debug_try_lock_timeout(&hardlockup_log_lock,
						5 * USEC_PER_SEC);
	if (!log_lock_acquired)
		pr_emerg("%s: fail to get log lock\n", __func__);

	/*
	 * We expect this bug to be executed only on cores that are part of the
	 * hardlockup_core_mask. If the current core is not part of the mask, it
	 * means that the FIQ was triggered by another reason (e.g. APC WDT).
	 */
	if (!(hardlockup_core_mask & BIT(cpu)))
		pr_emerg("Unintended core%d fiq handling, likely due to APC WDT\n", cpu);

	pr_emerg("%s - Debugging Information for Hardlockup core(%d) - locked CPUs mask (0x%lx)\n",
			allcorelockup_detected ? "WDT expired" : "Core", cpu,
			hardlockup_core_mask);

	google_cdd_save_context(regs, false);
	dump_backtrace(regs, NULL, KERN_DEFAULT);

	if (atomic_cmpxchg(&dump_tasks_once, 1, 0)) {
		show_mem();
		dump_pixel_meminfo();
		/*
		 * Dump task info only when ramdump mode is enabled (userdebug/eng builds)
		 * to avoid excessive logging to last_kmsg in user builds with limited
		 * buffers.
		 */
		if (ramdump_is_enabled) {
			/* dummy struct to fulfill dump_tasks interface */
			struct oom_control oc = { 0 };

			dump_tasks(&oc);
		}
	}

	if (pm_suspend_task && !test_and_set_bit(0, &pm_panic_once)) {
		if (spin_trylock(&pm_suspend_task_lock)) {
			pr_emerg("pm_suspend_task '%s' %d hung (state=%d)\n",
					pm_suspend_task->comm, pm_suspend_task->pid,
					pm_suspend_task->__state);
			sched_show_task(pm_suspend_task);
			spin_unlock(&pm_suspend_task_lock);
		} else {
			pr_warn("Failed to get pm_suspend_task_lock\n");
		}

		pm_panic = true;
	}

	set_bit(cpu, &hardlockup_core_handled_mask);

	if (log_lock_acquired)
		raw_spin_unlock(&hardlockup_log_lock);

	if ((READ_ONCE(hardlockup_core_handled_mask) & hardlockup_core_mask) ==
	    hardlockup_core_mask) {
		pr_emerg("***** All locked up cores dumped the call stack *****\n");
		//TODO(b/205354981): If we can request warm reset from here
	}

	if (pm_panic) {
		/*
		 * Wait 1 second max for all CPUs to complete their logging
		 * A 1-second ceiling is pragmatic to allow slow UART consoles to drain
		 * backtraces from all CPUs
		 */
		int wait = 1 * USEC_PER_SEC;
		const int poll_interval_us = 10;

		while (hardlockup_core_mask !=
		       (READ_ONCE(hardlockup_core_handled_mask) & hardlockup_core_mask) &&
		       wait > 0) {
			udelay(poll_interval_us);
			wait -= poll_interval_us;
		}

		if (wait <= 0)
			pr_emerg("Timeout waiting for cores: expected_mask=%#lx, handled_mask=%#lx\n",
				 hardlockup_core_mask, READ_ONCE(hardlockup_core_handled_mask));

		/* Panic here also sets the string as Ramdump Reason */
		panic("PM suspend timeout");
	}
	/* If cpu is locked, wait for WDT reset without executing code anymore. */
	hardlockup_debug_spin_func();

	return DBG_HOOK_ERROR;
}

static struct break_hook hardlockup_debug_break_hook = {
	.fn = hardlockup_debug_bug_handler,
	.imm = BUG_BRK_IMM_HARDLOCKUP,
};

static int hardlockup_debug_panic_handler(struct notifier_block *nb,
					  unsigned long l, void *buf)
{
	int cpu;
	unsigned long locked_up_mask = 0;
#ifdef SMC_CMD_KERNEL_PANIC_NOTICE
	int ret;
#endif

	if (allcorelockup_detected)
		return NOTIFY_OK;

	/* Assume that at this stage, CPUs that are still online
	 * (other than the panic-ing CPU) are locked up.
	 */
	for_each_possible_cpu(cpu) {
		if (cpu != raw_smp_processor_id() && cpu_online(cpu))
			locked_up_mask |= (1 << cpu);
	}

	pr_emerg("Hardlockup CPU mask: 0x%lx\n", locked_up_mask);
	if (!locked_up_mask)
		return NOTIFY_OK;

#ifdef SMC_CMD_KERNEL_PANIC_NOTICE
	hardlockup_core_mask = locked_up_mask;

	/* Setup for generating NMI interrupt to unstopped CPUs */
	ret = google_smc(SMC_CMD_KERNEL_PANIC_NOTICE, locked_up_mask,
				(unsigned long)hardlockup_debug_bug_func, 0);

	if (ret) {
		pr_emerg("Failed to generate NMI for hardlockup to dump information of core\n");
		locked_up_mask = 0;
	}

	/*  Wait up to 3 seconds for NMI interrupt */
	mdelay(MSEC_PER_SEC * 3);
#endif
	return NOTIFY_OK;
}

static struct notifier_block hardlockup_debug_panic_nb = {
	.notifier_call = hardlockup_debug_panic_handler,
};

static int hardlockup_debugger_pm_notifier(struct notifier_block *notifier,
				  unsigned long pm_event, void *v)
{
	unsigned long flags;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		spin_lock_irqsave(&pm_suspend_task_lock, flags);
		pm_suspend_task = get_task_struct(current);
		spin_unlock_irqrestore(&pm_suspend_task_lock, flags);
		break;
	case PM_POST_SUSPEND:
		spin_lock_irqsave(&pm_suspend_task_lock, flags);
		put_task_struct(pm_suspend_task);
		pm_suspend_task = NULL;
		spin_unlock_irqrestore(&pm_suspend_task_lock, flags);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block hardlockup_debugger_pm_nb = {
	.notifier_call = hardlockup_debugger_pm_notifier,
	.priority = 0,
};

static bool is_ramdump_mode_enabled(struct device *dev)
{
	struct device_node *dpm_node = NULL;
	struct device_node *abl_node = NULL;
	unsigned int ret = RAMDUMP_MODE_DISABLE;

	dpm_node = of_find_node_by_name(NULL, "dpm");
	if (!dpm_node) {
		dev_info(dev, "'dpm' node not found in DT.\n");
		goto out;
	}

	abl_node = of_find_node_by_name(dpm_node, "abl");
	if (!abl_node) {
		dev_info(dev, "'abl' node not found in DT.\n");
		goto out;
	}

	if (of_property_read_u32(abl_node, "ramdump_mode", &ret))
		dev_info(dev, "'ramdump_mode' property not found or invalid.\n");

	dev_info(dev, "ramdump mode is %s\n", ret ? "enabled" : "disabled");
out:
	of_node_put(abl_node);
	of_node_put(dpm_node);
	return !!ret;
}

static int hardlockup_debugger_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *node = pdev->dev.of_node;

	ret = of_property_read_u32(node, "use_multistage_wdt_irq",
				   &watchdog_fiq);
	if (ret) {
		dev_info(&pdev->dev, "Multistage watchdog is not supported\n");
		return ret;
	}

#ifdef SMC_CMD_LOCKUP_NOTICE
	ret = google_smc(SMC_CMD_LOCKUP_NOTICE,
			 (unsigned long)hardlockup_debug_bug_func, watchdog_fiq, 0);

	dev_info(&pdev->dev,
		 "%s to register all-core lockup detector - ret: %d\n",
		 (ret == 0) ? "success" : "failed", ret);

	if (ret != 0)
		goto error;

	raw_spin_lock_init(&hardlockup_seq_lock);
	raw_spin_lock_init(&hardlockup_log_lock);
	atomic_notifier_chain_register(&panic_notifier_list,
				       &hardlockup_debug_panic_nb);
	register_kernel_break_hook(&hardlockup_debug_break_hook);

	register_pm_notifier(&hardlockup_debugger_pm_nb);

	WARN_ON(register_trace_android_vh_cpu_idle_exit(
				vh_bug_on_wdt_fiq_pending, NULL));

#else
		ret = -EINVAL;
		goto error;
#endif

	ramdump_is_enabled = is_ramdump_mode_enabled(&pdev->dev);

	dev_info(&pdev->dev,
		 "Initialized hardlockup debug dump successfully.\n");
error:
	return ret;
}

static const struct of_device_id hardlockup_debug_dt_match[] = {
	{
		.compatible = "google,hardlockup-debug",
		.data = NULL,
	},
	{},
};
MODULE_DEVICE_TABLE(of, hardlockup_debug_dt_match);

static struct platform_driver hardlockup_debug_driver = {
	.probe = hardlockup_debugger_probe,
	.driver = {
			.name = "hardlockup-debug-driver",
			.of_match_table = hardlockup_debug_dt_match,
		},
};
module_platform_driver(hardlockup_debug_driver);

MODULE_DESCRIPTION("Module for Debugging Hardlockups via FIQ");
MODULE_LICENSE("GPL");
