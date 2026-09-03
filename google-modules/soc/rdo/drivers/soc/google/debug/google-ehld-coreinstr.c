// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC.
 *
 * google-ehld-coreinstr driver.
 */

#include <linux/sched/clock.h>
#include <linux/coresight.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/io.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/time.h>
#include <soc/google/google_gtc.h>
#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_gdmc_service_ids.h>
#include <soc/google/google-cdd.h>
#include <soc/google/google_gtc.h>

#include <linux/workqueue.h>

#define NUM_TRACE			(4)
#define EHLD_STAT_HANDLED_FLAG		(0x80)
#define CHAR_ARRAY_SIZE			(128)
#define CPU_OFFSET (4)
#define COREINSTRUN_BIT (0x2)

#define PMUPCSR         (0x200)
#define PMUPCSR_HI      (0x204)
#define DBGLAR		(0xfb0)
#define EHLD_PCSR_RES0_MAGIC	(0x5cULL << 56)
#define EHLD_PCSR_SELF		(EHLD_PCSR_RES0_MAGIC | 0xbb0ce1a1d05e1fULL)
#define MSB_MASKING		(0x0000FF0000000000)
#define MSB_PADDING		(0xFFFFFF0000000000)
#define DBG_UNLOCK(base)	\
	do { isb(); __raw_writel(CORESIGHT_UNLOCK, base + DBGLAR); } while (0)
#define DBG_LOCK(base)		\
	do { __raw_writel(0x1, base + DBGLAR); isb(); } while (0)

#if IS_ENABLED(CONFIG_HARDLOCKUP_WATCHDOG)
extern struct atomic_notifier_head hardlockup_notifier_list;
#endif

#define COREINSTR_OFFSET(cpu) (((cpu) <= 4) ? (cpu) : (cpu) + 1)
#define COREINSTR_REGSZ COREINSTR_OFFSET(num_possible_cpus())

struct google_ehld_main {
	bool				gdmc_enabled;
	u32				interval;
	u32				warn_count;
	u32				cpu_hp_state;
	bool				suspending;
	void __iomem			*coreinstret;
	void __iomem			*coreinstr;
	void __iomem			**sgi_base;
	struct clk			*tss_clk;
	raw_spinlock_t			lock;
	struct work_struct		work;
};

static struct google_ehld_main ehld_main = {
	.lock = __RAW_SPIN_LOCK_UNLOCKED(ehld_main.lock),
};

struct google_ehld_data {
	u64			time[NUM_TRACE];
	u64			alive_time[NUM_TRACE];
	u64			event[NUM_TRACE];
	u64			pmpcsr[NUM_TRACE];
	unsigned long		data_ptr;
};

struct google_ehld_ctrl {
	struct hrtimer			hrtimer;
	int				ehld_running;  // CPUHP state
	struct google_ehld_data		data;
	void __iomem			*pmu_base;
	raw_spinlock_t			lock;
};

static DEFINE_PER_CPU(struct google_ehld_ctrl, ehld_ctrl) = {
	.lock = __RAW_SPIN_LOCK_UNLOCKED(ehld_ctrl.lock),
};

struct google_ehld_dev {
	struct device *dev;
	struct gdmc_iface *gdmc_iface;
};

struct google_ehld_dev ehld_dev;

static void __iomem *get_coreinstr_address(u32 cpu)
{
	// Account for gap in coreinstr registers after CPU4
	return ehld_main.coreinstr + (COREINSTR_OFFSET(cpu) * sizeof(u32));
}

static bool is_core_idle(u32 cpu)
{
	u32 val = __raw_readl(get_coreinstr_address(cpu)) & COREINSTRUN_BIT;

	return val == 0;
}

static void print_gicr_regs(void)
{
	u32 cpu;

	for_each_possible_cpu(cpu)
		dev_err(ehld_dev.dev, "cpu%u: gicr: is-enabled:%08x, is-pending:%08x\n",
			 cpu,
			 __raw_readl(ehld_main.sgi_base[cpu] + GICR_ISENABLER0),
			 __raw_readl(ehld_main.sgi_base[cpu] + GICR_ISPENDR0));
}

static void print_ehld_header(void)
{
	dev_err(ehld_dev.dev, "---------------------------------------------------------------\n");
	dev_err(ehld_dev.dev, "  Google Early Lockup Detector Information\n\n");
	dev_err(ehld_dev.dev, "  CPU  NUM   TIME:sys(alive)              Value            PC\n\n");
}

static void google_ehld_event_raw_dump(cpumask_t cpu_mask)
{
	struct google_ehld_ctrl *ctrl;
	struct google_ehld_data data;
	unsigned long flags, count;
	u32 cpu;
	char buf[CHAR_ARRAY_SIZE];

	print_gicr_regs();
	print_ehld_header();

	for_each_cpu(cpu, &cpu_mask) {
		ctrl = per_cpu_ptr(&ehld_ctrl, cpu);

		raw_spin_lock_irqsave(&ctrl->lock, flags);
		data = ctrl->data;
		raw_spin_unlock_irqrestore(&ctrl->lock, flags);

		for (int i = 0; i < NUM_TRACE; i++) {
			count = ++data.data_ptr % NUM_TRACE;

			if (data.pmpcsr[count] == EHLD_PCSR_SELF) {
				strscpy(buf, "(self)", sizeof(buf));
			} else {
				snprintf(buf, sizeof(buf), "%#016llx(%pS)",
					data.pmpcsr[count], (void *)data.pmpcsr[count]);
			}

			dev_err(ehld_dev.dev, "  %03u  %03d   %015llu(%010llu)  %#015llx  %s\n",
						cpu, i + 1, data.time[count],
						data.alive_time[count],
						data.event[count], buf);
		}
		dev_err(ehld_dev.dev, "---------------------------------------------------------------\n");
	}
}

static void google_ehld_event_raw_update(cpumask_t cpu_mask)
{
	struct google_ehld_ctrl *ctrl;
	struct google_ehld_data *data;
	u64 val;
	unsigned long flags, count;
	u32 cpu;
	bool tss_clk_enable = false;
	u32 ret = clk_enable(ehld_main.tss_clk);

	if (ret < 0)
		dev_err(ehld_dev.dev, "Failed tss_clk enable: %d\n", ret);
	else
		tss_clk_enable = true;

	for_each_cpu(cpu, &cpu_mask) {
		ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
		raw_spin_lock_irqsave(&ctrl->lock, flags);
		data = &ctrl->data;
		count = ++data->data_ptr & (NUM_TRACE - 1);
		data->time[count] = cpu_clock(cpu);
		data->alive_time[count] =  goog_gtc_get_time_ns() / NSEC_PER_MSEC;
		data->event[count] = __raw_readl(ehld_main.coreinstret + cpu * sizeof(cpu));
		if (cpu_is_offline(cpu) || !ctrl->ehld_running ||
		    is_core_idle(cpu) || !tss_clk_enable) {
			data->pmpcsr[count] = 0;
		} else {
			DBG_UNLOCK(ctrl->pmu_base);
			/*
			 * Workaround: Need to read PMUPCSR twice to get valid
			 * PC values. The first read keeps returning 0xffffffff.
			 */
			val = __raw_readq(ctrl->pmu_base + PMUPCSR);
			val = __raw_readq(ctrl->pmu_base + PMUPCSR);
			if (MSB_MASKING == (MSB_MASKING & val))
				val |= MSB_PADDING;
			if (cpu == raw_smp_processor_id())
				val = EHLD_PCSR_SELF;
			data->pmpcsr[count] = val;
			DBG_LOCK(ctrl->pmu_base);
		}

		dev_info(ehld_dev.dev, "@%s: cpu%u: running:%d, offline:%ld\n",
				__func__, cpu,
				ctrl->ehld_running,
				cpu_is_offline(cpu));

		raw_spin_unlock_irqrestore(&ctrl->lock, flags);
		dev_info(ehld_dev.dev, "@%s: cpu%u - time:%llu(%llu), event:%#llx\n",
			__func__, cpu, data->time[count],
			data->alive_time[count],
			data->event[count]);
	}

	if (tss_clk_enable)
		clk_disable(ehld_main.tss_clk);
}

static void google_ehld_do_action(cpumask_t cpu_mask, u32 lockup_level)
{
	u32 val;
	int i, err;

	switch (lockup_level) {
	case EHLD_STATE_WARN:
	case EHLD_STATE_LOCKUP_SW:
		google_ehld_event_raw_dump(cpu_mask);
		break;
	case EHLD_STATE_LOCKUP_HW:
		google_ehld_event_raw_dump(cpu_mask);
		for_each_possible_cpu(i) {
			err = google_cdd_get_core_pmu_val(i, &val);
			if (err)
				dev_err(ehld_dev.dev, "@%s: cpu%u: Unable to get pmu val from DRAM\n",
							__func__, i);
			else
				dev_info(ehld_dev.dev, "@%s: cpu%u: pmu_val:%#x\n",
							__func__, i, val);
		}
		panic("Watchdog detected hard HANG on cpu %*pbl by EHLD",
			cpumask_pr_args(&cpu_mask));
		break;
	}
}

static void google_ehld_handle_cpu_state(struct cpumask mask, const char *message, int ehld_state)
{
	if (!cpumask_empty(&mask)) {
		google_ehld_event_raw_update(mask);
		dev_info(ehld_dev.dev, "@%s: cpu %*pbl %s", __func__, cpumask_pr_args(&mask), message);
		google_ehld_do_action(mask, ehld_state);
	}
}

static void google_ehld_do_policy(void)
{
	unsigned long flags;
	u32 cpu;
	cpumask_t warn = CPU_MASK_NONE, lockup_hw = CPU_MASK_NONE, lockup_sw = CPU_MASK_NONE;
	int err;

	raw_spin_lock_irqsave(&ehld_main.lock, flags);
	for_each_possible_cpu(cpu) {
		u32 val;

		err = google_cdd_get_core_ehld_stat(cpu, &val);
		if (err) {
			dev_err(ehld_dev.dev, "@%s: cpu%u: Unable to get cpu state from DRAM\n",
							__func__, cpu);
			return;
		}
		if (val & EHLD_STAT_HANDLED_FLAG)
			continue;

		if (val == EHLD_STATE_NORMAL)
			continue;

		dev_info(ehld_dev.dev, "@%s: cpu%u: val:%x timer:%llx",
				__func__, cpu, val, arch_timer_read_counter());

		switch (val) {
		case EHLD_STATE_WARN:
			cpumask_set_cpu(cpu, &warn);
			break;
		case EHLD_STATE_LOCKUP_SW:
			cpumask_set_cpu(cpu, &lockup_sw);
			break;
		case EHLD_STATE_LOCKUP_HW:
			cpumask_set_cpu(cpu, &lockup_hw);
			break;
		default:
			break;
		}
		val |= EHLD_STAT_HANDLED_FLAG;
		google_cdd_set_core_ehld_stat(cpu, val);
	}
	raw_spin_unlock_irqrestore(&ehld_main.lock, flags);

	google_ehld_handle_cpu_state(warn, "hardlockup warning", EHLD_STATE_WARN);
	google_ehld_handle_cpu_state(lockup_sw, "hardlockup by software", EHLD_STATE_LOCKUP_SW);
	google_ehld_handle_cpu_state(lockup_hw, "hardlockup by hardware", EHLD_STATE_LOCKUP_HW);
}

/* Callback function when messages are received from GDMC */
static void google_mba_apc_critical_ehld_service_handler(void *resp_buf, void *prv_data)
{
	u32 *buf = resp_buf;
	u32 state, cpu, count;

	state = buf[1];
	cpu = buf[2];
	count = buf[3];

	switch (state) {
	case EHLD_STATE_WARN:
		dev_err(ehld_dev.dev, "EHLD Warning for CPU%d - count: 0x%x\n",
				cpu, count);
		break;
	case EHLD_STATE_LOCKUP_SW:
		dev_err(ehld_dev.dev, "EHLD on CPU%d caused by SW - count: 0x%x\n",
				cpu, count);
		break;
	case EHLD_STATE_LOCKUP_HW:
		dev_err(ehld_dev.dev, "EHLD on CPU%d caused by HW - count: 0x%x\n",
				cpu, count);
		break;
	}

	google_ehld_do_policy();
}

static int gdmc_ehld_timer_param_config(u32 interval, u32 warn_count)
{
	u32 msg[2];

	msg[0] = interval;
	msg[1] = warn_count;

	return gdmc_ehld_config(ehld_dev.gdmc_iface, GDMC_MBA_EHLD_CMD_TIMER_PARAM,
					msg, ehld_dev.dev);
}

static int gdmc_ehld_timer_enable(void)
{
	return gdmc_ehld_config(ehld_dev.gdmc_iface, GDMC_MBA_EHLD_CMD_TIMER_ENABLE,
					NULL, ehld_dev.dev);
}

static int gdmc_ehld_timer_disable(void)
{
	return gdmc_ehld_config(ehld_dev.gdmc_iface, GDMC_MBA_EHLD_CMD_TIMER_DISABLE,
					NULL, ehld_dev.dev);
}

static int google_ehld_value_raw_update(u32 cpu)
{
	u32 val = __raw_readl(ehld_main.coreinstret + cpu * sizeof(val));

	return google_cdd_set_core_pmu_val(cpu, val);
}

static enum hrtimer_restart ehld_value_raw_hrtimer_fn(struct hrtimer *hrtimer)
{
	u32 cpu = raw_smp_processor_id();

	/* Update instruciton retired count in DRAM */
	if (google_ehld_value_raw_update(cpu)) {
		dev_err(ehld_dev.dev, "@%s: cpu%u DRAM updated failed\n",
			__func__, cpu);

		return HRTIMER_NORESTART;
	}

	google_ehld_do_policy();

	/* Restart HR timer with same interval*/
	if (ehld_main.interval > 0) {
		hrtimer_forward_now(hrtimer,
			ns_to_ktime(ehld_main.interval * NSEC_PER_MSEC));
	} else {
		dev_info(ehld_dev.dev, "@%s: cpu%u hrtimer interval is abnormal: %u\n",
			__func__, cpu, ehld_main.interval);
		return HRTIMER_NORESTART;
	}

	return HRTIMER_RESTART;
}

static void google_ehld_start_cpu_hrtimer(void *data)
{
	struct hrtimer *hrtimer = (struct hrtimer *)data;
	u64 interval = ehld_main.interval * NSEC_PER_MSEC;

	hrtimer_start(hrtimer, ns_to_ktime(interval), HRTIMER_MODE_REL_PINNED);
}

static int google_ehld_start_cpu(u32 cpu)
{
	struct google_ehld_ctrl *ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
	struct hrtimer *hrtimer = &ctrl->hrtimer;

	/*
	 * Originally all CPUs are controlled by cpuhp framework.
	 * But CPU0 is not controlled by cpuhp framework during suspend.
	 * Hence we need to handle cpu 0 here from cpu 1 context
	 */
	if (ehld_main.suspending && cpu == 1)
		google_ehld_start_cpu(0);

	ctrl->ehld_running = 1;

	/* Start an HR timer to perdically update DRAM with instructions retired count */
	u64 interval = ehld_main.interval * NSEC_PER_MSEC;

	dev_info(ehld_dev.dev, "@%s: cpu%u ehld running with hrtimer\n", __func__, cpu);
	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = ehld_value_raw_hrtimer_fn;
	if (google_ehld_value_raw_update(cpu))
		dev_err(ehld_dev.dev, "cpu%u DRAM updated failed when cpu is onlined\n", cpu);

	if (ehld_main.suspending && cpu == 0)
		smp_call_function_single(0, google_ehld_start_cpu_hrtimer, hrtimer, 0);
	else
		hrtimer_start(hrtimer, ns_to_ktime(interval), HRTIMER_MODE_REL_PINNED);

	return 0;
}

static int google_ehld_stop_cpu(u32 cpu)
{
	struct google_ehld_ctrl *ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
	struct hrtimer *hrtimer = &ctrl->hrtimer;
	/*
	 * Originally all CPUs are controlled by cpuhp framework.
	 * But CPU0 is not controlled by cpuhp framework during suspend.
	 * Hence we need to handle cpu 0 here from cpu 1 context
	 */
	if (ehld_main.suspending && cpu == 1)
		google_ehld_stop_cpu(0);

	ctrl->ehld_running = 0;

	/* Cancel the HR Timer */
	dev_info(ehld_dev.dev, "@%s: cpu%u hrtimer cancel\n", __func__, cpu);
	if (google_ehld_value_raw_update(cpu))
		dev_err(ehld_dev.dev, "cpu%u DRAM updated failed when cpu is offlined\n", cpu);
	hrtimer_cancel(hrtimer);

	return 0;
}

static void ehld_work_handler(struct work_struct *work)
{
	int ret;

	if (ehld_main.suspending)
		clk_unprepare(ehld_main.tss_clk);
	else {
		ret = clk_prepare(ehld_main.tss_clk);
		if (ret)
			dev_err(ehld_dev.dev, "Failed to prepare tss_clk: %d\n", ret);
	}
}

static int google_ehld_pm_notifier(struct notifier_block *notifier, unsigned long pm_event,
						void *v)
{
	unsigned long flags;
	u32 cpu;

	/*
	 * We should control re-init / exit for all CPUs
	 * Originally all CPUs are controlled by cpuhp framework.
	 * But CPU0 is not controlled by cpuhp framework in exynos BSP.
	 * So mainline code of perf(kernel/cpu.c) for CPU0 is not called by cpuhp framework.
	 * As a result, it's OK to not control CPU0.
	 * CPU0 will be controlled by CPU_PM notifier call.
	 */

	switch (pm_event) {

	case PM_SUSPEND_PREPARE:
		raw_spin_lock_irqsave(&ehld_main.lock, flags);

		ehld_main.suspending = 1;
		cpu = raw_smp_processor_id();
		if (google_ehld_value_raw_update(cpu))
			dev_err(ehld_dev.dev, "cpu%u DRAM updated failed during suspend\n", cpu);

		if (ehld_main.gdmc_enabled) {
			gdmc_ehld_config(ehld_dev.gdmc_iface, GDMC_MBA_EHLD_CMD_TIMER_DISABLE,
						NULL, ehld_dev.dev);
			ehld_main.gdmc_enabled = false;
		}

		schedule_work(&ehld_main.work);

		raw_spin_unlock_irqrestore(&ehld_main.lock, flags);
		break;

	case PM_POST_SUSPEND:
		raw_spin_lock_irqsave(&ehld_main.lock, flags);

		ehld_main.suspending = 0;
		cpu = raw_smp_processor_id();
		if (google_ehld_value_raw_update(cpu))
			dev_err(ehld_dev.dev, "cpu%u DRAM updated failed during resume\n", cpu);

		if (!ehld_main.gdmc_enabled) {
			gdmc_ehld_config(ehld_dev.gdmc_iface, GDMC_MBA_EHLD_CMD_TIMER_ENABLE,
						NULL, ehld_dev.dev);
			ehld_main.gdmc_enabled = true;
		}

		schedule_work(&ehld_main.work);

		raw_spin_unlock_irqrestore(&ehld_main.lock, flags);
		break;

	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block google_ehld_pm_notifier_block = {
	.notifier_call = google_ehld_pm_notifier,
};

#if IS_ENABLED(CONFIG_HARDLOCKUP_WATCHDOG)
static int google_ehld_hardlockup_handler(struct notifier_block *nb,
					   unsigned long l, void *p)
{
	cpumask_t cpu_mask;

	cpumask_setall(&cpu_mask);
	google_ehld_event_raw_dump(cpu_mask);

	return 0;
}

static struct notifier_block google_ehld_hardlockup_block = {
	.notifier_call = google_ehld_hardlockup_handler,
};

static void google_ehld_register_hardlockup_notifier_list(void)
{
	atomic_notifier_chain_register(&hardlockup_notifier_list,
					&google_ehld_hardlockup_block);
}

static void google_ehld_unregister_hardlockup_notifier_list(void)
{
	atomic_notifier_chain_unregister(&hardlockup_notifier_list,
					&google_ehld_hardlockup_block);
}
#else
static void google_ehld_register_hardlockup_notifier_list(void) {}
static void google_ehld_unregister_hardlockup_notifier_list(void) {}
#endif

static int google_ehld_reboot_handler(struct notifier_block *nb,
				unsigned long l, void *p)
{
	return gdmc_ehld_config(ehld_dev.gdmc_iface, GDMC_MBA_EHLD_CMD_TIMER_DISABLE,
				NULL, ehld_dev.dev);
}

static struct notifier_block google_ehld_reboot_block = {
	.notifier_call = google_ehld_reboot_handler,
};

static int google_ehld_init_dt(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *child;
	int ret;
	const int num_cpus = num_possible_cpus();
	u32 base, cpu;
	u64 val;

	ehld_main.sgi_base = devm_kcalloc(dev, num_possible_cpus(),
			sizeof(void *), GFP_KERNEL);
	if (!ehld_main.sgi_base) {
		dev_err(ehld_dev.dev, "ehld: cannot allocate memory for sgi_base\n");
		return -ENOMEM;
	}

	child = of_get_child_by_name(np, "sgi_base");
	if (!child) {
		dev_err(ehld_dev.dev, "ehld: sgi_base node not found\n");
		return -EINVAL;
	}

	for_each_possible_cpu(cpu) {
		ret = of_property_read_reg(child, cpu, &val, NULL);
		if (ret < 0) {
			dev_err(ehld_dev.dev, "ehld: cpu%u's sgi base not found\n", cpu);
			of_node_put(child);
			return -EINVAL;
		}

		ehld_main.sgi_base[cpu] = devm_ioremap(dev, val, SZ_4K);
		if (!ehld_main.sgi_base[cpu]) {
			dev_err(ehld_dev.dev, "ehld: not enough memory for sgi_base\n");
			of_node_put(child);
			return -ENOMEM;
		}
	}
	of_node_put(child);

	ret = of_property_read_u32(np, "interval", &base);
	if (ret) {
		dev_err(ehld_dev.dev,
			"ehld: no timer interval in device tree\n");
		return -EINVAL;
	}
	ehld_main.interval = base;

	ret = of_property_read_u32(np, "warn-count", &base);
	if (ret) {
		dev_err(ehld_dev.dev,
			"ehld: no warn count in device tree\n");
		return -EINVAL;
	}
	ehld_main.warn_count = base;

	child = of_get_child_by_name(np, "coreinstret_base");
	if (!child) {
		dev_err(ehld_dev.dev, "ehld: coreinstret_base node not found\n");
		return -EINVAL;
	}

	ret = of_property_read_reg(child, 0, &val, NULL);
	if (ret) {
		dev_err(ehld_dev.dev, "ehld: Failed to read coreinstret_base\n");
		return -EINVAL;
	}
	of_node_put(child);

	ehld_main.coreinstret = devm_ioremap(dev, val, num_cpus * sizeof(u32));
	if (!ehld_main.coreinstret) {
		dev_err(ehld_dev.dev, "ehld: not enough memory for coreinstret\n");
		return -ENOMEM;
	}

	child = of_get_child_by_name(np, "coreinstr_base");
	if (!child) {
		dev_err(ehld_dev.dev, "ehld: coreinstr_base node not found\n");
		return -EINVAL;
	}

	ret = of_property_read_reg(child, 0, &val, NULL);
	if (ret) {
		dev_err(ehld_dev.dev, "ehld: Failed to read coreinstr_base\n");
		return -EINVAL;
	}
	of_node_put(child);

	ehld_main.coreinstr = devm_ioremap(dev, val, COREINSTR_REGSZ * sizeof(u32));
	if (!ehld_main.coreinstr) {
		dev_err(ehld_dev.dev, "ehld: not enough memory for coreinstrun\n");
		return -ENOMEM;
	}

	child = of_get_child_by_name(np, "pmu_base");
	if (!child) {
		dev_err(ehld_dev.dev, "ehld: pmu_base node not found\n");
		return -EINVAL;
	}

	for_each_possible_cpu(cpu) {
		struct google_ehld_ctrl *ctrl;

		ret = of_property_read_reg(child, cpu, &val, NULL);
		if (ret) {
			dev_err(ehld_dev.dev, "ehld: cpu%u's pmu base not found\n", cpu);
			of_node_put(child);
			return -EINVAL;
		}

		ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
		ctrl->pmu_base = devm_ioremap(dev, val, SZ_4K);
		if (!ctrl->pmu_base) {
			dev_err(ehld_dev.dev, "ehld: not enough memory for pmu_base\n");
			of_node_put(child);
			return -ENOMEM;
		}

		dev_info(ehld_dev.dev, "ehld: cpu%u, pmu_base:%#llx\n",
					cpu, val);
	}
	of_node_put(child);

	ehld_main.tss_clk = devm_clk_get(dev, "tss_clk");
	if (IS_ERR(ehld_main.tss_clk)) {
		dev_err(ehld_dev.dev, "failed to acquire tss_clk (%ld)\n",
			PTR_ERR(ehld_main.tss_clk));
		ehld_main.tss_clk = NULL;
		return -EINVAL;
	}

	ret = clk_prepare(ehld_main.tss_clk);
	if (ret)
		dev_err(ehld_dev.dev, "Failed to prepare tss_clk %d\n", ret);

	return ret;
}

static int google_ehld_setup(void)
{
	int ret;

	register_pm_notifier(&google_ehld_pm_notifier_block);
	google_ehld_register_hardlockup_notifier_list();
	register_reboot_notifier(&google_ehld_reboot_block);

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "google-ehld:online",
			google_ehld_start_cpu, google_ehld_stop_cpu);
	if (ret >= 0) {
		ehld_main.cpu_hp_state = ret;
		ret = 0;
	}
	return ret;
}

static unsigned long noehld;
module_param(noehld, ulong, 0664);
MODULE_PARM_DESC(noehld, "disable EHLD by setting noehld=1");

static int ehld_probe(struct platform_device *pdev)
{
	int err;
	struct device *dev = &pdev->dev;
	struct gdmc_iface *gdmc_iface;

	if (noehld == 1) {
		dev_dbg(ehld_dev.dev, "ehld: disabled by module parameter: noehld=1\n");
		return -EPERM;
	}

	platform_set_drvdata(pdev, &ehld_dev);
	ehld_dev.dev = dev;

	/* Get a GDMC mailbox interface handle */
	gdmc_iface = gdmc_iface_get(dev);
	if (IS_ERR(gdmc_iface)) {
		dev_err(ehld_dev.dev, "failed to get gdmc interface %ld\n",
			PTR_ERR(gdmc_iface));
		return PTR_ERR(gdmc_iface);
	}
	ehld_dev.gdmc_iface = gdmc_iface;

	err = google_ehld_init_dt(&pdev->dev);
	if (err) {
		dev_err(ehld_dev.dev, "ehld: fail device tree for ehld:%d\n", err);
		gdmc_iface_put(ehld_dev.gdmc_iface);
		return err;
	}

	INIT_WORK(&ehld_main.work, ehld_work_handler);

	/* Register Call back function to receive messages from GDMC */
	err = gdmc_register_host_cb(ehld_dev.gdmc_iface, APC_CRITICAL_GDMC_EHLD_SERVICE,
				google_mba_apc_critical_ehld_service_handler, &ehld_dev);
	if (err) {
		dev_err(ehld_dev.dev, "ehld: failed to register mba callback function:%d\n", err);
		gdmc_iface_put(ehld_dev.gdmc_iface);
		return err;
	}

	/* Send Mailbox message to GDMC to setup ehld parameters */
	err = gdmc_ehld_timer_param_config(ehld_main.interval, ehld_main.warn_count);
	if (err) {
		dev_err(ehld_dev.dev, "ehld: failed to enable GDMC periodic timer:%d\n", err);
		gdmc_unregister_host_cb(ehld_dev.gdmc_iface, APC_CRITICAL_GDMC_EHLD_SERVICE);
		gdmc_iface_put(ehld_dev.gdmc_iface);
		return err;
	}

	/* Send Mailbox message to GDMC to enable periodic timer */
	err = gdmc_ehld_timer_enable();
	if (err) {
		dev_err(ehld_dev.dev, "ehld: failed to enable GDMC periodic timer:%d\n", err);
		gdmc_unregister_host_cb(ehld_dev.gdmc_iface, APC_CRITICAL_GDMC_EHLD_SERVICE);
		gdmc_iface_put(ehld_dev.gdmc_iface);
		return err;
	}
	ehld_main.gdmc_enabled = true;

	err = google_ehld_setup();
	if (err) {
		dev_err(ehld_dev.dev, "ehld: fail setup for ehld:%d\n", err);
		gdmc_ehld_timer_disable();
		ehld_main.gdmc_enabled = false;
		gdmc_unregister_host_cb(ehld_dev.gdmc_iface, APC_CRITICAL_GDMC_EHLD_SERVICE);
		gdmc_iface_put(ehld_dev.gdmc_iface);
		unregister_reboot_notifier(&google_ehld_reboot_block);
		google_ehld_unregister_hardlockup_notifier_list();
		unregister_pm_notifier(&google_ehld_pm_notifier_block);
		return err;
	}

	return 0;
}

static void ehld_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);

	if (ehld_main.gdmc_enabled) {
		gdmc_ehld_timer_disable();
		ehld_main.gdmc_enabled = true;
	}

	if (ehld_dev.gdmc_iface) {
		gdmc_unregister_host_cb(ehld_dev.gdmc_iface, APC_CRITICAL_GDMC_EHLD_SERVICE);
		gdmc_iface_put(ehld_dev.gdmc_iface);
	}

	cpuhp_remove_state(ehld_main.cpu_hp_state);
	unregister_reboot_notifier(&google_ehld_reboot_block);
	google_ehld_unregister_hardlockup_notifier_list();
	unregister_pm_notifier(&google_ehld_pm_notifier_block);

	dev_dbg(ehld_dev.dev, "Removed Google EHLD device.\n");
}

static const struct of_device_id ehld_dt_match[] = {
	{
		.compatible = "google,ehld-coreinstr",
	},
	{},
};
MODULE_DEVICE_TABLE(of, ehld_dt_match);

static struct platform_driver ehld_driver = {
	.probe = ehld_probe,
	.remove = ehld_remove,
	.driver = {
		.name = "ehld_coreinstr",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ehld_dt_match),
	},
};
module_platform_driver(ehld_driver);

MODULE_DESCRIPTION("Module for Detecting HW lockup at an early time");
MODULE_AUTHOR("Mayank Rungta <mrungta@google.com");
MODULE_LICENSE("GPL");
