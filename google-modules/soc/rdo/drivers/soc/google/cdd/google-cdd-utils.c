// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC.
 *
 */

#include <linux/init_task.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/kdebug.h>
#include <linux/nmi.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/rtc.h>
#include <linux/sched/debug.h>
#include <soc/google/google-cdd.h>
#include <soc/google/google-smc.h>
#include <soc/google/google_wdt.h>
#include <trace/hooks/debug.h>
#include <uapi/linux/psci.h>

#include "google-cdd-local.h"

/*
 * SYSTEM_RESET2 macros
 */
#define PSCI_RESET2_TYPE_VENDOR					BIT(31)
#define PSCI_RESET2_VENDOR_SYSTEM_WARM_RESET			(PSCI_RESET2_TYPE_VENDOR | (0U))
#define PSCI_RESET2_VENDOR_SYSTEM_WARM_RESET_WITH_REASON	(PSCI_RESET2_TYPE_VENDOR | (1U))

/* TF-A PSCI crumbs indicating that CPU successfully resumed to NS-EL1. */
#define APC_BOOT_CRUMBS_PHASE_TYPE_MASK 0xFU
#define APC_BOOT_CRUMBS_PHASE_TYPE_SHIFT 12
#define APC_BOOT_CRUMBS_PHASE_KERNEL 7

struct google_cdd_mmu_reg {
	long SCTLR_EL1;
	long TTBR0_EL1;
	long TTBR1_EL1;
	long TCR_EL1;
	long ESR_EL1;
	long FAR_EL1;
	long CONTEXTIDR_EL1;
	long TPIDR_EL0;
	long TPIDRRO_EL0;
	long TPIDR_EL1;
	long MAIR_EL1;
	long ELR_EL1;
	long SP_EL0;
};

static struct pt_regs __percpu **cdd_core_reg;
static struct google_cdd_mmu_reg __percpu **cdd_mmu_reg;
static struct google_cdd_helper_ops cdd_soc_ops;

static bool google_cdd_psci_system_reset2_supported;

static struct watchdog_device *cdd_wdd;

static void google_cdd_dump_panic(char *str, size_t len)
{
	/*  This function is only one which runs in panic function */
	if (str && len && len < CDD_PANIC_STRING_SZ)
		memcpy(google_cdd_get_header_vaddr() + CDD_OFFSET_PANIC_STRING,
				str, len);
}

static void google_cdd_set_core_power_stat(unsigned int val, unsigned int cpu)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	if (header)
		__raw_writel(val, header + CDD_OFFSET_CORE_POWER_STAT + cpu * 4);
}

static unsigned int google_cdd_get_core_panic_stat(unsigned int cpu)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	return header ?  __raw_readl(header + CDD_OFFSET_PANIC_STAT + cpu * 4) : 0;
}

static void google_cdd_set_core_panic_stat(unsigned int val, unsigned int cpu)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	if (header)
		__raw_writel(val, header + CDD_OFFSET_PANIC_STAT + cpu * 4);
}

static void google_cdd_set_wdt_pet_time_stat(u64 mono_raw_ns, u64 gtc_ns)
{
	static atomic_t cdd_wdt_pet_time_idx = ATOMIC_INIT(0);
	void __iomem *header = google_cdd_get_header_vaddr();
	int i;

	if (!header)
		return;

	i = atomic_fetch_inc(&cdd_wdt_pet_time_idx) % CDD_WDT_PET_TIME_STAT_NUM;
	__raw_writeq(mono_raw_ns, header + CDD_OFFSET_WDT_PET_TIME_STAT(i));
	__raw_writeq(gtc_ns, header + CDD_OFFSET_WDT_PET_TIME_STAT(i) + sizeof(mono_raw_ns));
	__raw_writel(i, header + CDD_OFFSET_WDT_PET_TIME_IDX);
}

void google_cdd_set_core_cflush_stat(unsigned int val)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	if (header)
		__raw_writel(val, header + CDD_OFFSET_CFLUSH_STAT);
}
EXPORT_SYMBOL_GPL(google_cdd_set_core_cflush_stat);

int google_cdd_get_system_dev_stat(uint32_t dev, uint32_t *val)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	if (header && dev < CDD_SYSTEM_DEVICE_DEV_MAX)
		*val = __raw_readl(header + CDD_OFFSET_SYSTEM_STATUS + dev * 4);

	return 0;
}
EXPORT_SYMBOL_GPL(google_cdd_get_system_dev_stat);

void google_cdd_set_system_dev_stat(uint32_t dev, uint32_t val)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	if (header && dev < CDD_SYSTEM_DEVICE_DEV_MAX)
		__raw_writel(val, header + CDD_OFFSET_SYSTEM_STATUS + dev * 4);
}
EXPORT_SYMBOL_GPL(google_cdd_set_system_dev_stat);

void google_cdd_update_psci_crumbs(void)
{
	void __iomem *header = google_cdd_get_header_vaddr();
	unsigned int cpu = raw_smp_processor_id();
	unsigned int psci_crumbs;

	if (!header)
		return;

	psci_crumbs = __raw_readl(header + CDD_OFFSET_BL31_PSCI_CRUMBS(cpu));

	/*
	 * Overwrite only PHASE bits of PSCI crumbs
	 * to not lose information from TF-A.
	 */
	psci_crumbs &= ~(APC_BOOT_CRUMBS_PHASE_TYPE_MASK
			 << APC_BOOT_CRUMBS_PHASE_TYPE_SHIFT);
	psci_crumbs |= (APC_BOOT_CRUMBS_PHASE_KERNEL
			<< APC_BOOT_CRUMBS_PHASE_TYPE_SHIFT);
	__raw_writel(psci_crumbs, header + CDD_OFFSET_BL31_PSCI_CRUMBS(cpu));
}
EXPORT_SYMBOL_GPL(google_cdd_update_psci_crumbs);

int google_cdd_set_core_pmu_val(uint32_t cpu, uint32_t val)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	if (!header)
		return -EPERM;

	if (cpu * sizeof(cpu) > CDD_EHLD_CARVEOUT_SIZE)
		return -ENOMEM;

	__raw_writel(val, header + CDD_OFFSET_CORE_PMU_VAL + (cpu * sizeof(cpu)));

	return 0;
}
EXPORT_SYMBOL_GPL(google_cdd_set_core_pmu_val);

int google_cdd_get_core_pmu_val(uint32_t cpu, uint32_t *val)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	if (!header)
		return -EPERM;

	if (cpu * sizeof(cpu) > CDD_EHLD_CARVEOUT_SIZE)
		return -ENOMEM;

	*val = __raw_readl(header + CDD_OFFSET_CORE_PMU_VAL + (cpu * sizeof(cpu)));

	return 0;
}
EXPORT_SYMBOL_GPL(google_cdd_get_core_pmu_val);

int google_cdd_set_core_ehld_stat(uint32_t cpu, uint32_t val)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	if (!header)
		return -EPERM;

	if (cpu * sizeof(cpu) > CDD_EHLD_CARVEOUT_SIZE)
		return -ENOMEM;

	__raw_writel(val, header + CDD_OFFSET_EHLD_STAT + (cpu * sizeof(cpu)));

	return 0;
}
EXPORT_SYMBOL_GPL(google_cdd_set_core_ehld_stat);

int google_cdd_get_core_ehld_stat(uint32_t cpu, uint32_t *val)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	if (!header)
		return -EPERM;

	if (cpu * sizeof(cpu) > CDD_EHLD_CARVEOUT_SIZE)
		return -ENOMEM;

	*val = __raw_readl(header + CDD_OFFSET_EHLD_STAT + (cpu * sizeof(cpu)));

	return 0;
}
EXPORT_SYMBOL_GPL(google_cdd_get_core_ehld_stat);

static void google_cdd_report_reason(unsigned int val)
{
	cdd_ctx.reset_reason = val;
}

static unsigned int google_cdd_get_reason(void)
{
	return cdd_ctx.reset_reason;
}

static void google_cdd_set_apc_wdt_sub_reason(struct device *dev, unsigned int val)
{
	if (!cdd_ctx.apc_wdt_sub_reason) {
		dev_err(dev, "cdd_ctx.apc_wdt_sub_reason is null\n");
		return;
	}

	__raw_writel(val, cdd_ctx.apc_wdt_sub_reason);
}

static void google_cdd_set_reboot_mode(enum reboot_mode mode)
{
	reboot_mode = mode;
}

static void google_cdd_set_wdt_caller(unsigned long addr)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	if (header)
		__raw_writeq(addr, header + CDD_OFFSET_WDT_CALLER);
}

int google_cdd_emergency_reboot_timeout(const char *str, unsigned int level, unsigned int timeout)
{
	void *addr;
	char *reboot_msg;

	reboot_msg = kmalloc(CDD_PANIC_STRING_SZ, GFP_ATOMIC);
	if (!reboot_msg) {
		dev_emerg(cdd_ctx.dev,
			  "Out of memory! Couldn't allocate reboot message\n");
		return -ENOMEM;
	}

	/*
	 * Set default "Emergency Reboot" message
	 */
	scnprintf(reboot_msg, CDD_PANIC_STRING_SZ, "%s", "Emergency Reboot");

	if (!cdd_wdd || !cdd_wdd->ops || !cdd_wdd->ops->set_timeout) {
		dev_emerg(cdd_ctx.dev, "There is no wdt functions!\n");
		return -ENODEV;
	}

	addr = return_address(level);
	google_cdd_set_wdt_caller((unsigned long)addr);
	if (str)
		scnprintf(reboot_msg, CDD_PANIC_STRING_SZ, "%s", str);

	dev_emerg(cdd_ctx.dev, "WDT Caller: %pS %s\n", addr, str ? str : "");

	google_cdd_dump_panic(reboot_msg, strlen(reboot_msg));
	dump_stack();

	cdd_wdd->ops->set_timeout(cdd_wdd, timeout < cdd_wdd->min_timeout ?
				  cdd_wdd->min_timeout : timeout);
	kfree(reboot_msg);
	return 0;
}
EXPORT_SYMBOL_GPL(google_cdd_emergency_reboot_timeout);

int google_cdd_emergency_reboot(const char *str)
{
	google_cdd_emergency_reboot_timeout(str, 1, cdd_wdd->min_timeout);
	while (1) {
		wfi();
	}

	return 0;
}
EXPORT_SYMBOL_GPL(google_cdd_emergency_reboot);

static void google_cdd_dump_one_task_info(struct task_struct *tsk)
{
	char state_array[] = {'R', 'S', 'D', 'T', 't', 'X',
			'Z', 'P', 'x', 'K', 'W', 'I', 'N'};
	unsigned char idx = 0;
	unsigned long state, pc = 0;

	if ((!tsk) || !try_get_task_stack(tsk) || (tsk->__state & TASK_FROZEN) ||
	    !(tsk->__state == TASK_RUNNING ||
	    tsk->__state == TASK_UNINTERRUPTIBLE ||
	    tsk->__state == TASK_KILLABLE))
		return;

	state = tsk->__state | tsk->exit_state;
	pc = KSTK_EIP(tsk);
	while (state) {
		idx++;
		state >>= 1;
	}

	/*
	 * kick watchdog to prevent unexpected reset during panic sequence
	 * and it prevents the hang during panic sequence by watchedog
	 */
	touch_softlockup_watchdog();

	pr_info("%8d %16llu %16llu %16llu %c(%u) %3d %16pK %16pK %c %16s\n",
		tsk->pid, tsk->utime, tsk->stime,
		tsk->se.exec_start, state_array[idx], (tsk->__state),
		task_cpu(tsk), (void *) pc, tsk,
		thread_group_leader(tsk) ? '*' : ' ', tsk->comm);

	sched_show_task(tsk);
}

static void google_cdd_dump_task_info(void)
{
	struct task_struct *p, *t;

	pr_info("\n");
	pr_info(" current proc : %d %s\n",
			current->pid, current->comm);
	pr_info("------------------------------------------------------------------------------\n");
	pr_info("%8s %8s %8s %16s %4s %3s %16s %16s  %16s\n",
			"pid", "uTime", "sTime", "exec(ns)", "stat", "cpu",
			"user_pc", "task_struct", "comm");
	pr_info("------------------------------------------------------------------------------\n");

	rcu_read_lock();
	for_each_process_thread(p, t)
		google_cdd_dump_one_task_info(t);
	rcu_read_unlock();
	pr_info("------------------------------------------------------------------------------\n");
}

static void google_cdd_save_system(void *unused)
{
	struct google_cdd_mmu_reg *mmu_reg;

	mmu_reg = *per_cpu_ptr(cdd_mmu_reg, raw_smp_processor_id());

	asm volatile ("mrs x1, SCTLR_EL1\n\t"	/* SCTLR_EL1 */
		"mrs x2, TTBR0_EL1\n\t"		/* TTBR0_EL1 */
		"stp x1, x2, [%0]\n\t"
		"mrs x1, TTBR1_EL1\n\t"		/* TTBR1_EL1 */
		"mrs x2, TCR_EL1\n\t"		/* TCR_EL1 */
		"stp x1, x2, [%0, #0x10]\n\t"
		"mrs x1, ESR_EL1\n\t"		/* ESR_EL1 */
		"mrs x2, FAR_EL1\n\t"		/* FAR_EL1 */
		"stp x1, x2, [%0, #0x20]\n\t"
		"mrs x1, CONTEXTIDR_EL1\n\t"	/* CONTEXTIDR_EL1 */
		"mrs x2, TPIDR_EL0\n\t"		/* TPIDR_EL0 */
		"stp x1, x2, [%0, #0x30]\n\t"
		"mrs x1, TPIDRRO_EL0\n\t"	/* TPIDRRO_EL0 */
		"mrs x2, TPIDR_EL1\n\t"		/* TPIDR_EL1 */
		"stp x1, x2, [%0, #0x40]\n\t"
		"mrs x1, MAIR_EL1\n\t"		/* MAIR_EL1 */
		"mrs x2, ELR_EL1\n\t"		/* ELR_EL1 */
		"stp x1, x2, [%0, #0x50]\n\t"
		"mrs x1, SP_EL0\n\t"		/* SP_EL0 */
		"str x1, [%0, 0x60]\n\t" :	/* output */
		: "r"(mmu_reg)			/* input */
		: "x1", "x2", "memory"		/* clobbered register */
	);
}

static inline void google_cdd_save_core(struct pt_regs *regs)
{
	unsigned int cpu = raw_smp_processor_id();
	struct pt_regs *core_reg = *per_cpu_ptr(cdd_core_reg, cpu);

	if (!core_reg) {
		pr_err("Core reg is null\n");
		return;
	}

	if (!regs) {
		asm volatile ("str x0, [%0, #0]\n\t"
				"mov x0, %0\n\t"
				"stp x1, x2, [x0, #0x8]\n\t"
				"stp x3, x4, [x0, #0x18]\n\t"
				"stp x5, x6, [x0, #0x28]\n\t"
				"stp x7, x8, [x0, #0x38]\n\t"
				"stp x9, x10, [x0, #0x48]\n\t"
				"stp x11, x12, [x0, #0x58]\n\t"
				"stp x13, x14, [x0, #0x68]\n\t"
				"stp x15, x16, [x0, #0x78]\n\t"
				"stp x17, x18, [x0, #0x88]\n\t"
				"stp x19, x20, [x0, #0x98]\n\t"
				"stp x21, x22, [x0, #0xa8]\n\t"
				"stp x23, x24, [x0, #0xb8]\n\t"
				"stp x25, x26, [x0, #0xc8]\n\t"
				"stp x27, x28, [x0, #0xd8]\n\t"
				"stp x29, x30, [x0, #0xe8]\n\t" :
				: "r"(core_reg));
		core_reg->sp = core_reg->regs[29];
		core_reg->pc =
			(unsigned long)(core_reg->regs[30] - sizeof(unsigned int));
		/* We don't know other bits but mode is definitely CurrentEL. */
		core_reg->pstate = read_sysreg(CurrentEL);
	} else {
		memcpy(core_reg, regs, sizeof(struct user_pt_regs));
	}
}

void google_cdd_save_context(struct pt_regs *regs, bool stack_dump)
{
	int cpu;
	unsigned long flags;

	if (!google_cdd_get_enable())
		return;

	cpu = raw_smp_processor_id();
	raw_spin_lock_irqsave(&cdd_ctx.ctrl_lock, flags);

	/* If it was already saved the context information, it should be skipped */
	if (google_cdd_get_core_panic_stat(cpu) !=  CDD_SIGN_PANIC) {
		google_cdd_set_core_panic_stat(CDD_SIGN_PANIC, cpu);
		google_cdd_save_system(NULL);
		google_cdd_save_core(regs);
		// google_cdd_ecc_dump(false);
		dev_emerg(cdd_ctx.dev, "context saved(CPU:%d)\n", cpu);
	} else
		dev_emerg(cdd_ctx.dev, "skip context saved(CPU:%d)\n", cpu);

	if (stack_dump)
		dump_stack();
	raw_spin_unlock_irqrestore(&cdd_ctx.ctrl_lock, flags);
}
EXPORT_SYMBOL_GPL(google_cdd_save_context);

void google_cdd_do_dpm_policy(unsigned int policy, const char *str)
{
	switch (policy) {
	case GO_DEFAULT_ID:
		pr_emerg("%s: %s\n", __func__, str);
		pr_emerg("no-op\n");
		break;
	case GO_PANIC_ID:
		panic("%s: %s", __func__, str);
		break;
	case GO_WATCHDOG_ID:
	case GO_S2M_ID:
		google_cdd_emergency_reboot(str);
		break;
	case GO_CACHEDUMP_ID:
		pr_emerg("%s: %s\n", __func__, str);
		pr_emerg("Entering Cachedump Mode!\n");
		if (cdd_soc_ops.run_cachedump)
			cdd_soc_ops.run_cachedump();
		else
			pr_emerg("no-op\n");
		break;
	case GO_SCANDUMP_ID:
		pr_emerg("%s: %s\n", __func__, str);
		pr_emerg("Entering Scandump Mode!\n");
		if (cdd_soc_ops.run_scandump_mode)
			cdd_soc_ops.run_scandump_mode();
		else
			pr_emerg("no-op\n");
		break;
	case GO_HALT_ID:
		pr_emerg("%s: %s\n", __func__, str);
		pr_emerg("Entering Halt Mode!\n");
		if (cdd_soc_ops.stop_all_cpus)
			cdd_soc_ops.stop_all_cpus();
		else
			pr_emerg("no-op\n");
		break;
	default:
		pr_emerg("no-op\n");
		break;
	}
}
EXPORT_SYMBOL_GPL(google_cdd_do_dpm_policy);

static int google_cdd_reboot_handler(struct notifier_block *nb,
				    unsigned long mode, void *cmd)
{
	cdd_ctx.in_reboot = true;

	if (mode == SYS_POWER_OFF)
		google_cdd_report_reason(CDD_SIGN_NORMAL_REBOOT);

	return NOTIFY_DONE;
}

static void google_cdd_print_timestamp(void)
{
	struct timespec64 ts;
	struct rtc_time tm;

	ktime_get_real_ts64(&ts);
	rtc_time64_to_tm(ts.tv_sec - (sys_tz.tz_minuteswest * 60), &tm);
	pr_info("GCDD Timestamp (%02d-%02d %02d:%02d:%02d)\n",
		tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static int google_cdd_restart_handler(struct notifier_block *nb,
				    unsigned long mode, void *cmd)
{
	int cpu;

	if (!google_cdd_get_enable())
		goto exit;

	if (cdd_ctx.in_panic)
		goto exit;

	if (cdd_ctx.in_warm) {
		dev_emerg(cdd_ctx.dev, "warm reset\n");
		google_cdd_report_reason(CDD_SIGN_WARM_REBOOT);
		google_cdd_set_reboot_mode(REBOOT_WARM);
		google_cdd_dump_task_info();
	} else if (cdd_ctx.in_reboot) {
		dev_emerg(cdd_ctx.dev, "normal reboot starting\n");
		google_cdd_report_reason(CDD_SIGN_NORMAL_REBOOT);
	} else if (cdd_ctx.long_press_power) {
		dev_emerg(cdd_ctx.dev, "Power key been hold for 18s : Do restart\n");
		google_cdd_report_reason(CDD_SIGN_REBOOT_LONGKEY_POWER_WARM);
		google_cdd_set_reboot_mode(REBOOT_WARM);
		google_cdd_dump_task_info();
	} else {
		dev_emerg(cdd_ctx.dev, "emergency restart\n");
		google_cdd_report_reason(CDD_SIGN_EMERGENCY_REBOOT);
		google_cdd_set_reboot_mode(REBOOT_WARM);
		google_cdd_dump_task_info();
	}

	/* clear CDD_SIGN_PANIC when normal reboot */
	for_each_possible_cpu(cpu) {
		google_cdd_set_core_panic_stat(CDD_SIGN_RESET, cpu);
	}

exit:
	google_cdd_print_timestamp();
	dev_info(cdd_ctx.dev, "ready to do restart.\n");
	if ((reboot_mode == REBOOT_WARM || reboot_mode == REBOOT_SOFT) &&
		google_cdd_psci_system_reset2_supported) {
		google_cdd_save_context(NULL, false);
		google_smc(PSCI_1_1_FN64_SYSTEM_RESET2,
			   PSCI_RESET2_VENDOR_SYSTEM_WARM_RESET_WITH_REASON,
			   google_cdd_get_reason(), 0);
	} else {
		google_smc(PSCI_0_2_FN_SYSTEM_RESET, 0, 0, 0);
	}

	dev_err(cdd_ctx.dev, "Should not reach here\n");

	return NOTIFY_DONE;
}

static struct die_args *tombstone;

static int google_cdd_panic_handler(struct notifier_block *nb, unsigned long l, void *buf)
{
	char *kernel_panic_msg;
	void *pc_tombstone;
	void *lr_tombstone;
	unsigned long cpu;

	if (!google_cdd_get_enable())
		return 0;

	cdd_ctx.in_panic = true;

	kernel_panic_msg = kmalloc(CDD_PANIC_STRING_SZ, GFP_ATOMIC);
	if (!kernel_panic_msg) {
		dev_emerg(cdd_ctx.dev, "Out of memory! Couldn't allocate reboot message\n");
		return -ENOMEM;
	}



	if (tombstone) { /* Add tombstones to the panic message for Oops */
#if IS_ENABLED(CONFIG_ARM)
		pc_tombstone = (void *)tombstone->regs->ARM_pc;
		lr_tombstone = (void *)tombstone->regs->ARM_lr;
#elif IS_ENABLED(CONFIG_ARM64)
		pc_tombstone = (void *)tombstone->regs->pc;
		lr_tombstone = (void *)tombstone->regs->regs[30];
#endif

		scnprintf(kernel_panic_msg, CDD_PANIC_STRING_SZ,
			  "KP: %s: comm:%s PC:%pSR LR:%pSR", (char *)buf,
			  current->comm, pc_tombstone, lr_tombstone);
	} else {
		scnprintf(kernel_panic_msg, CDD_PANIC_STRING_SZ, "KP: %s",
			  (char *)buf);
	}

	/* Again disable log_kevents */
	google_cdd_set_item_enable("log_kevents", false);
	google_cdd_dump_panic(kernel_panic_msg, strlen(kernel_panic_msg));
	google_cdd_report_reason(CDD_SIGN_PANIC);
	google_cdd_set_reboot_mode(REBOOT_WARM);
	for_each_possible_cpu(cpu) {
		if (cpu_is_offline(cpu))
			google_cdd_set_core_power_stat(CDD_SIGN_DEAD, cpu);
		else
			google_cdd_set_core_power_stat(CDD_SIGN_ALIVE, cpu);
	}

	google_cdd_dump_task_info();
	google_cdd_output();
	google_cdd_log_output();
	google_cdd_print_timestamp();
	google_cdd_print_log_report();

	google_cdd_do_dpm_policy(cdd_ctx.panic_action, kernel_panic_msg);

	if (num_online_cpus() > 1)
		google_cdd_emergency_reboot(kernel_panic_msg);

	kfree(kernel_panic_msg);
	return 0;
}

static int google_cdd_die_handler(struct notifier_block *nb,
				   unsigned long l, void *data)
{
	static struct die_args args;

	memcpy(&args, data, sizeof(args));
	tombstone = &args;

	if (user_mode(tombstone->regs))
		return NOTIFY_DONE;

	google_cdd_save_context(tombstone->regs, false);
	google_cdd_set_item_enable("log_kevents", false);

	return NOTIFY_DONE;
}

static struct notifier_block nb_reboot_block = {
	.notifier_call = google_cdd_reboot_handler,
	.priority = INT_MAX,
};

/*
 * We must set priority 130 to be higher than psci_sys_reset(129) and lower than other
 * handlers. Thus we can make sure pixel_restart_handler is run in the end of system
 * restart.
 */
static struct notifier_block nb_restart_block = {
	.notifier_call = google_cdd_restart_handler,
	.priority = 130,
};

static struct notifier_block nb_panic_block = {
	.notifier_call = google_cdd_panic_handler,
	.priority = INT_MIN,
};

static struct notifier_block nb_die_block = {
	.notifier_call = google_cdd_die_handler,
	.priority = INT_MAX,
};

void google_cdd_register_debug_ops(int (*halt)(void),
				    int (*cachedump)(void),
				    int (*scandump)(void))
{
	if (halt)
		cdd_soc_ops.stop_all_cpus = halt;
	if (cachedump)
		cdd_soc_ops.run_cachedump = cachedump;
	if (scandump)
		cdd_soc_ops.run_scandump_mode = scandump;

	dev_info(cdd_ctx.dev, "Add %s%s%s functions from %pS\n",
		 halt ? "halt, " : "",
		 cachedump ? "cachedump, " : "",
		 scandump ? "scandump mode" : "",
		 return_address(0));
}
EXPORT_SYMBOL_GPL(google_cdd_register_debug_ops);

static void google_cdd_ipi_stop(void *ignore, struct pt_regs *regs)
{
	if (!cdd_ctx.in_reboot)
		google_cdd_save_context(regs, false);
}

static void google_cdd_psci_init_system_reset2(void)
{
	int ret;

	ret = google_smc(PSCI_1_0_FN_PSCI_FEATURES, PSCI_1_1_FN64_SYSTEM_RESET2, 0, 0);

	if (ret != PSCI_RET_NOT_SUPPORTED) {
		google_cdd_psci_system_reset2_supported = true;
		return;
	}

	dev_err(cdd_ctx.dev, "failed to support psci system reset2 in tf-a %d\n", ret);
}

void google_cdd_init_utils(struct device *dev)
{
	size_t vaddr;
	uintptr_t i;

	vaddr = cdd_items[CDD_ITEM_HEADER_ID].entry.vaddr;

	cdd_mmu_reg = alloc_percpu(struct google_cdd_mmu_reg *);
	cdd_core_reg = alloc_percpu(struct pt_regs *);
	for_each_possible_cpu(i) {
		*per_cpu_ptr(cdd_mmu_reg, i) = (struct google_cdd_mmu_reg *)
					  (vaddr + CDD_HDR_INFO_TOTAL_SZ +
					   i * CDD_SYSREG_PER_CORE_SZ);
		*per_cpu_ptr(cdd_core_reg, i) = (struct pt_regs *)
					   (vaddr + CDD_HDR_INFO_TOTAL_SZ + CDD_HDR_SYSREG_SZ +
					    i * CDD_COREREG_PER_CORE_SZ);
	}

	/* Sign it from CDD_SIGN_WATCHDOG_APC_EARLY to CDD_SIGN_WATCHDOG_APC */
	google_cdd_set_apc_wdt_sub_reason(dev, CDD_SIGN_WATCHDOG_APC);

	google_wdt_register_pet_timestamp_callback(google_cdd_set_wdt_pet_time_stat);

	cdd_wdd = google_wdt_wdd_get(dev);
	if (IS_ERR(cdd_wdd)) {
		dev_err(dev, "fail to get google wdt wdd, err: %ld\n", PTR_ERR(cdd_wdd));
		cdd_wdd = NULL;
	}

	register_die_notifier(&nb_die_block);
	register_restart_handler(&nb_restart_block);
	google_cdd_psci_init_system_reset2();
	register_reboot_notifier(&nb_reboot_block);
	atomic_notifier_chain_register(&panic_notifier_list, &nb_panic_block);
	register_trace_android_vh_ipi_stop(google_cdd_ipi_stop, NULL);

	smp_call_function(google_cdd_save_system, NULL, 1);
	google_cdd_save_system(NULL);
}
