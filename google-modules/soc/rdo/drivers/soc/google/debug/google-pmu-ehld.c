// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/suspend.h>
#include <linux/perf_event.h>
#include <linux/of.h>
#include <linux/cpu_pm.h>
#include <linux/sched/clock.h>
#include <linux/notifier.h>
#include <linux/panic_notifier.h>
#include <linux/smpboot.h>
#include <linux/hrtimer.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/time64.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_gdmc_service_ids.h>
#include <soc/google/google-cdd.h>
#include <soc/google/google_gtc.h>
#include <linux/perf_event.h>
#include <linux/cpumask.h>
#include <linux/coresight.h>

#define NUM_TRACE			(4)

#define EHLD_STAT_HANDLED_FLAG		(0x80)

#define EHLD_VAL_INIT			(0xC0)
#define EHLD_VAL_PM_PREPARE		(0xC1)
#define EHLD_VAL_PM			(0xC2)
#define EHLD_VAL_PM_POST		(0xC3)
#define EHLD_VAL_NORMAL			(0xC4)

#define PMUPCSR         (0x200)
#define PMUPCSR_HI      (0x204)
#define DBGLAR		(0xfb0) /* WO */

#define EHLD_PCSR_RES0_MAGIC	(0x5cULL << 56)
#define EHLD_PCSR_SELF		(EHLD_PCSR_RES0_MAGIC | 0xbb0ce1a1d05e1fULL)
#define MSB_MASKING		(0x0000FF0000000000)
#define MSB_PADDING		(0xFFFFFF0000000000)
#define DBG_UNLOCK(base)	\
	do { isb(); __raw_writel(CORESIGHT_UNLOCK, base + DBGLAR); } while (0)
#define DBG_LOCK(base)		\
	do { __raw_writel(0x1, base + DBGLAR); isb(); } while (0)

#define GTC_TICKS_PER_MS (38400) /* 38.4 MHz crystal */

#if IS_ENABLED(CONFIG_HARDLOCKUP_WATCHDOG)
extern struct atomic_notifier_head hardlockup_notifier_list;
#endif

struct google_ehld_main {
	raw_spinlock_t			lock;
	bool				gdmc_enabled;
	unsigned int			interval;
	unsigned int			warn_count;
	bool				suspending;
	void __iomem			**sgi_base;
};

static struct google_ehld_main ehld_main = {
	.lock = __RAW_SPIN_LOCK_UNLOCKED(ehld_main.lock),
};

struct google_ehld_data {
	unsigned long long		time[NUM_TRACE];
	unsigned long long		alive_time[NUM_TRACE];
	unsigned long long		event[NUM_TRACE];
	unsigned long long		pmpcsr[NUM_TRACE];
	unsigned long			data_ptr;
};

struct google_ehld_ctrl {
	struct hrtimer			hrtimer;
	struct perf_event		*event;
	struct google_ehld_data		data;
	void __iomem			*pmu_base;
	bool				ehld_running;  // CPUHP state
	bool				ehld_cpu_stopped;    // CPUPM states
	u32				cntr_shift;
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

/* PMU Configuration to setup a counter to count instrcutions retired */
static struct perf_event_attr google_ehld_attr = {
	.type           = PERF_TYPE_HARDWARE,
	.config         = PERF_COUNT_HW_INSTRUCTIONS,
	.size           = sizeof(struct perf_event_attr),
	.sample_period  = U32_MAX,
	.pinned         = 1,
	.disabled       = 1,
};

static void google_ehld_callback(struct perf_event *event,
			       struct perf_sample_data *data,
			       struct pt_regs *regs)
{
	event->hw.interrupts = 0;       // don't throttle interrupts
}

static void print_gicr_regs(void)
{
	unsigned int cpu;

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
	struct google_ehld_data *data;
	unsigned long flags, count;
	int i;
	unsigned int cpu;
	char *buf;
	size_t buf_size = 128;

	buf = kmalloc(buf_size, GFP_ATOMIC);
	if (!buf) {
		dev_err(ehld_dev.dev, "No heap memory, unable to dump EHLD events");
		return;
	}
/*
 *	if (sjtag_is_locked()) {
 *		ehld_err(1, "EHLD trace requires SJTAG authentication\n");
 *		return;
 *	}
 */
	print_gicr_regs();
	print_ehld_header();

	for_each_cpu(cpu, &cpu_mask) {
		ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
		raw_spin_lock_irqsave(&ctrl->lock, flags);
		data = &ctrl->data;
		for (i = 0; i < NUM_TRACE; i++) {
			count = ++data->data_ptr % NUM_TRACE;
			if (data->pmpcsr[count] == EHLD_PCSR_SELF)
				strscpy(buf, "(self)", buf_size);
			else
				snprintf(buf, buf_size, "%#016llx(%pS)",
					data->pmpcsr[count], (void *)data->pmpcsr[count]);
			dev_err(ehld_dev.dev, "  %03u  %03d   %015llu(%010llu)  %#015llx  %s\n",
						cpu, i + 1, data->time[count],
						data->alive_time[count],
						data->event[count], buf);
		}
		raw_spin_unlock_irqrestore(&ctrl->lock, flags);
		dev_err(ehld_dev.dev, "---------------------------------------------------------------\n");
	}
	kfree(buf);
}

static void google_ehld_event_raw_update(cpumask_t cpu_mask)
{
	struct google_ehld_ctrl *ctrl;
	struct google_ehld_data *data;
	unsigned long long val;
	unsigned long flags, count;
	unsigned int cpu;

	for_each_cpu(cpu, &cpu_mask) {
		ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
		raw_spin_lock_irqsave(&ctrl->lock, flags);
		data = &ctrl->data;
		count = ++data->data_ptr & (NUM_TRACE - 1);
		data->time[count] = cpu_clock(cpu);
		data->alive_time[count] = goog_gtc_get_counter() * MSEC_PER_SEC / GTC_TICKS_PER_MS;
		if (cpu_is_offline(cpu) || !ctrl->ehld_running || ctrl->ehld_cpu_stopped) {
			val = EHLD_VAL_PM;
			data->event[count] = val;
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
			val = __raw_readl(ctrl->pmu_base);
			data->event[count] = val;
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
}

static void google_ehld_do_action(cpumask_t cpu_mask, unsigned int lockup_level)
{
	unsigned int val;
	int i, err;

	switch (lockup_level) {
	case EHLD_STATE_WARN:
	case EHLD_STATE_LOCKUP_SW:
		google_ehld_event_raw_dump(cpu_mask);
		break;
	case EHLD_STATE_LOCKUP_HW:
		google_ehld_event_raw_dump(cpu_mask);
#if IS_ENABLED(CONFIG_HARDLOCKUP_WATCHDOG)
		atomic_notifier_call_chain(&hardlockup_notifier_list,
							0, &cpu_mask);
#endif
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

static void google_ehld_do_policy(void)
{
	unsigned long flags;
	unsigned int cpu;
	cpumask_t warn = CPU_MASK_NONE, lockup_hw = CPU_MASK_NONE, lockup_sw = CPU_MASK_NONE;
	int err;

	raw_spin_lock_irqsave(&ehld_main.lock, flags);
	for_each_possible_cpu(cpu) {
		unsigned int val;

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

	if (!cpumask_empty(&warn)) {
		google_ehld_event_raw_update(warn);
		dev_info(ehld_dev.dev, "@%s: cpu %*pbl hardlockup warning",
							__func__, cpumask_pr_args(&warn));
		google_ehld_do_action(warn, EHLD_STATE_WARN);
	}

	if (!cpumask_empty(&lockup_sw)) {
		google_ehld_event_raw_update(lockup_sw);
		dev_info(ehld_dev.dev, "@%s: cpu %*pbl hardlockup by software",
							__func__, cpumask_pr_args(&lockup_sw));
		google_ehld_do_action(lockup_sw, EHLD_STATE_LOCKUP_SW);
	}

	if (!cpumask_empty(&lockup_hw)) {
		google_ehld_event_raw_update(lockup_hw);
		dev_info(ehld_dev.dev, "@%s: cpu %*pbl hardlockup by hardware",
							__func__, cpumask_pr_args(&lockup_hw));
		google_ehld_do_action(lockup_hw, EHLD_STATE_LOCKUP_HW);
	}
}

/* Callback function when messages are received from GDMC */
static void google_mba_apc_critical_ehld_service_handler(void *resp_buf, void *prv_data)
{
	uint32_t *buf = resp_buf;
	uint32_t state, cpu, count;

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
static int gdmc_ehld_set_pmu_cntr(u32 cpu, u32 cntr)
{
	u32 msg[2];

	msg[0] = cpu;
	msg[1] = cntr;

	return gdmc_ehld_config(ehld_dev.gdmc_iface, GDMC_MBA_EHLD_CMD_PMU_COUNTER_ID,
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

static u32 google_ehld_read_pmu_counter(void)
{
	unsigned int cpu = raw_smp_processor_id();
	struct google_ehld_ctrl *ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
	struct perf_event *event = ctrl->event;

	write_sysreg(event->hw.idx, pmselr_el0);
	isb();
	return read_sysreg(pmxevcntr_el0);
}

static int google_ehld_value_raw_update(unsigned int cpu)
{
	struct google_ehld_ctrl *ctrl = per_cpu_ptr(&ehld_ctrl, cpu);

	u32 val = google_ehld_read_pmu_counter();

	return google_cdd_set_core_pmu_val(cpu, val + ctrl->cntr_shift);
}

static enum hrtimer_restart ehld_value_raw_hrtimer_fn(struct hrtimer *hrtimer)
{
	unsigned int cpu = raw_smp_processor_id();
	struct google_ehld_ctrl *ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
	struct perf_event *event = ctrl->event;

	if (!event) {
		dev_err(ehld_dev.dev, "@%s: cpu%u, HRTIMER is cancel\n", __func__, cpu);
		return HRTIMER_NORESTART;
	}

	if (!ehld_main.gdmc_enabled) {
		dev_err(ehld_dev.dev, "@%s: cpu%u, gdmc is not enabled, re-start\n",
								__func__, cpu);
		hrtimer_forward_now(hrtimer, ns_to_ktime(NSEC_PER_SEC / 2));
		return HRTIMER_RESTART;
	}

	/* Check if event is active */
	if (event->state != PERF_EVENT_STATE_ACTIVE) {
		google_cdd_set_core_pmu_val(cpu, EHLD_VAL_PM);
		dev_err(ehld_dev.dev, "@%s: cpu%u, event state is not active: %d\n",
			__func__, cpu, event->state);
		return HRTIMER_NORESTART;
	}

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

static int google_ehld_start_cpu(unsigned int cpu)
{
	struct google_ehld_ctrl *ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
	struct perf_event *event = ctrl->event;
	struct hrtimer *hrtimer = &ctrl->hrtimer;

	// during resume, need to handle cpu 0 here from cpu 1 context
	if (ehld_main.suspending && cpu == 1)
		google_ehld_start_cpu(0);

	/* Create a PMU event to count instructions retired */
	if (!event) {
		int err;

		event = perf_event_create_kernel_counter(&google_ehld_attr,
							 cpu,
							 NULL,
							 google_ehld_callback,
							 NULL);
		if (IS_ERR(event)) {
			dev_err(ehld_dev.dev, "@%s: cpu%u event make failed err: %ld\n",
				__func__, cpu, PTR_ERR(event));
			return PTR_ERR(event);
		}

		dev_info(ehld_dev.dev, "@%s: cpu%u event make success\n", __func__, cpu);
		ctrl->event = event;
		perf_event_enable(event);

		err = gdmc_ehld_set_pmu_cntr(cpu, event->hw.idx);

		if (err) {
			dev_err(ehld_dev.dev, "@%s: cpu%u set_pmu_cntr_id failed: %d\n", __func__,
					cpu, err);
			ctrl->event = NULL;
			perf_event_disable(event);
			perf_event_release_kernel(event);
			return err;
		}
	}

	ctrl->ehld_running = 1;

	/* Start an HR timer to perdically update DRAM with instructions retired count */
	u64 interval = ehld_main.interval * NSEC_PER_MSEC;

	dev_dbg(ehld_dev.dev, "@%s: cpu%u ehld running with hrtimer\n", __func__, cpu);
	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = ehld_value_raw_hrtimer_fn;

	if (ehld_main.suspending && cpu == 0)
		smp_call_function_single(0, google_ehld_start_cpu_hrtimer, hrtimer, 0);
	else
		hrtimer_start(hrtimer, ns_to_ktime(interval), HRTIMER_MODE_REL_PINNED);

	return 0;
}

static int google_ehld_stop_cpu(unsigned int cpu)
{
	struct google_ehld_ctrl *ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
	struct perf_event *event;
	struct hrtimer *hrtimer = &ctrl->hrtimer;

	// during suspend, need to handle cpu 0 here from cpu 1 context
	if (ehld_main.suspending && cpu == 1)
		google_ehld_stop_cpu(0);

	ctrl->ehld_running = 0;

	dev_info(ehld_dev.dev, "@%s: cpu%u ehld stopping\n", __func__, cpu);

	/* Cancel the PMU event and free up the counters */
	event = ctrl->event;
	if (event) {
		ctrl->event = NULL;
		perf_event_disable(event);
		perf_event_release_kernel(event);
	}

	/* Cancel the HR Timer */
	dev_info(ehld_dev.dev, "@%s: cpu%u hrtimer cancel\n", __func__, cpu);
	hrtimer_cancel(hrtimer);

	/* Update core state as PM in DRAM to notify GDMC that core is idle/offline */
	google_cdd_set_core_pmu_val(cpu, EHLD_VAL_PM);
	return 0;
}

static int google_ehld_cpu_pm_exit(unsigned int cpu)
{
	struct google_ehld_ctrl *ctrl;
	unsigned long flags;

	ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
	raw_spin_lock_irqsave(&ctrl->lock, flags);
	if (!ctrl->ehld_running)
		goto out;

	/*
	 * WAR for b/233011236
	 * Do not touch hrtimers in EHLD CPUPM callbacks
	 *
	 * if (ehld_main.dbgc.support && !ehld_main.dbgc.use_tick_timer &&
	 *     !ehld_main.suspending && !hrtimer_active(&ctrl->hrtimer)) {
	 *     hrtimer_start(&ctrl->hrtimer,
	 *                   ns_to_ktime(ehld_main.dbgc.interval * 1000 * 1000),
	 *                   HRTIMER_MODE_REL_PINNED);
	 * }
	 */

	/*
	 * When a CPU core exits PM, we cannot use a constant value
	 * (EHLD_VAL_PM_POST or zero) here as the wake-up value.
	 * This is because CPU enters and exits PM frequently, and
	 * debug core keeps observing the core PMU value frequently.
	 * A single constant value will look like the core is stuck.
	 *
	 * Instead, need to show progress. In the optimal world, we
	 * would just pull the PMU retired instruction counter here,
	 * just like the hrtimer callback does. But experimentation
	 * shows that PMU returns zero rather frequently here.
	 *
	 * Thus, we will need to use CPU clock as the initial value.
	 */
	google_cdd_set_core_pmu_val(cpu, cpu_clock(cpu));
	ctrl->cntr_shift = cpu_clock(cpu);
	ctrl->ehld_cpu_stopped = 0;
out:
	raw_spin_unlock_irqrestore(&ctrl->lock, flags);
	return 0;
}

static int google_ehld_cpu_pm_enter(unsigned int cpu)
{
	struct google_ehld_ctrl *ctrl;
	unsigned long flags;

	ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
	raw_spin_lock_irqsave(&ctrl->lock, flags);
	if (!ctrl->ehld_running)
		goto out;

	/*
	 * WAR for b/233011236
	 * Do not touch hrtimers in EHLD CPUPM callbacks
	 *
	 * if (ehld_main.dbgc.support && !ehld_main.dbgc.use_tick_timer &&
	 *     !ehld_main.suspending && hrtimer_active(&ctrl->hrtimer)) {
	 *     hrtimer_cancel(&ctrl->hrtimer);
	 * }
	 */

	google_cdd_set_core_pmu_val(cpu, EHLD_VAL_PM_PREPARE);
	ctrl->ehld_cpu_stopped = 1;
out:
	raw_spin_unlock_irqrestore(&ctrl->lock, flags);
	return 0;
}

#if IS_ENABLED(CONFIG_HARDLOCKUP_WATCHDOG)
static int google_ehld_hardlockup_handler(struct notifier_block *nb,
					   unsigned long l, void *p)
{
	unsigned int cpu, ehld_stat, pmu_val;
	int err;
	cpumask_t cpu_mask;

	cpumask_setall(&cpu_mask);
	google_ehld_event_raw_update(cpu_mask);
	google_ehld_event_raw_dump(cpu_mask);

	for_each_possible_cpu(cpu) {
		err = google_cdd_get_core_ehld_stat(cpu, &ehld_stat);
		if (err) {
			dev_err(ehld_dev.dev, "ehld: Unable to get ehld core stat: %d\n", err);

			return 0;
		}

		err = google_cdd_get_core_pmu_val(cpu, &pmu_val);
		if (err) {
			dev_err(ehld_dev.dev, "ehld: Unable to get core pmu val: %d\n", err);

			return 0;
		}

		dev_dbg(ehld_dev.dev, "@%s: cpu%u: pmu_val:%#x, ehld_stat:%#x\n",
			__func__, cpu,
			pmu_val,
			ehld_stat);
	}

	return 0;
}

static struct notifier_block google_ehld_hardlockup_block = {
	.notifier_call = google_ehld_hardlockup_handler,
};

static void register_hardlockup_notifier_list(void)
{
	atomic_notifier_chain_register(&hardlockup_notifier_list,
					&google_ehld_hardlockup_block);
}
#else
static void register_hardlockup_notifier_list(void) {}
#endif

static int google_ehld_reboot_handler(struct notifier_block *nb,
					   unsigned long l, void *p)
{
	return gdmc_ehld_timer_disable();
}

static struct notifier_block google_ehld_reboot_block = {
	.notifier_call = google_ehld_reboot_handler,
};


static int google_ehld_c2_pm_notifier(struct notifier_block *self,
						unsigned long action, void *v)
{
	unsigned int cpu = raw_smp_processor_id();

	switch (action) {
	case CPU_PM_ENTER:
		google_ehld_cpu_pm_enter(cpu);
		break;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		google_ehld_cpu_pm_exit(cpu);
		break;
	case CPU_CLUSTER_PM_ENTER:
		break;
	case CPU_CLUSTER_PM_ENTER_FAILED:
	case CPU_CLUSTER_PM_EXIT:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block google_ehld_c2_pm_nb = {
	.notifier_call = google_ehld_c2_pm_notifier,
	.priority = (INT_MAX / 2),
};

static int google_ehld_pm_notifier(struct notifier_block *notifier, unsigned long pm_event,
						void *v)
{
	unsigned int cpu;
	unsigned long flags;

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

		if (ehld_main.gdmc_enabled) {
			gdmc_ehld_timer_disable();
			ehld_main.gdmc_enabled = false;
		}

		raw_spin_unlock_irqrestore(&ehld_main.lock, flags);
		break;

	case PM_POST_SUSPEND:
		raw_spin_lock_irqsave(&ehld_main.lock, flags);

		ehld_main.suspending = 0;

		for_each_possible_cpu(cpu) {
			google_cdd_set_core_pmu_val(cpu, EHLD_VAL_PM_POST);
		}
		if (!ehld_main.gdmc_enabled) {
			gdmc_ehld_timer_enable();
			ehld_main.gdmc_enabled = true;
		}

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

static int google_ehld_init_dt(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *child;
	int ret = 0, num_cpus = num_possible_cpus(), i;
	unsigned int base, cpu;
	resource_size_t val, *val_array;

	ehld_main.sgi_base = devm_kcalloc(dev, num_possible_cpus(),
			sizeof(void *), GFP_KERNEL);
	if (!ehld_main.sgi_base)
		return -ENOMEM;

	val_array = kmalloc_array(num_cpus, sizeof(resource_size_t), GFP_KERNEL);
	if (!val_array)
		return -ENOMEM;

	ret = of_property_read_u64_array(np, "sgi_base", val_array, num_cpus);
	if (ret < 0) {
		dev_err(ehld_dev.dev, "ehld: Failed to read sgi_base\n");
		kfree(val_array);
		return -EINVAL;
	}

	for (i = 0; i < num_cpus; i++) {
		ehld_main.sgi_base[i] = devm_ioremap(dev, val_array[i], SZ_4K);
		if (!ehld_main.sgi_base[i]) {
			kfree(val_array);
			return -ENOMEM;
		}
	}
	kfree(val_array);

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

	struct google_ehld_ctrl *ctrl;

	for_each_possible_cpu(cpu) {
		char name[SZ_16];

		snprintf(name, sizeof(name), "cpu%u", cpu);
		child = of_get_child_by_name(np, name);

		if (!child) {
			dev_err(ehld_dev.dev, "ehld: cpu%u's node not found\n", cpu);
			return -EINVAL;
		}

		ret = of_property_read_u64(child, "pmu_base", &val);
		if (ret) {
			of_node_put(child);
			return -EINVAL;
		}

		ctrl = per_cpu_ptr(&ehld_ctrl, cpu);
		ctrl->pmu_base = devm_ioremap(dev, val, SZ_4K);

		if (!ctrl->pmu_base) {
			dev_err(ehld_dev.dev, "ehld: cpu%u's pmu base not found\n", cpu);
			of_node_put(child);
			return -ENOMEM;
		}

		dev_info(ehld_dev.dev, "ehld: cpu%u, pmu_base:%#llx\n",
					cpu, val);

		of_node_put(child);
	}

	return ret;
}

static int google_ehld_setup(void)
{
	int ret;

	register_pm_notifier(&google_ehld_pm_notifier_block);

	register_hardlockup_notifier_list();

	register_reboot_notifier(&google_ehld_reboot_block);

	// register cpu pm notifier for C2
	cpu_pm_register_notifier(&google_ehld_c2_pm_nb);

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "google-ehld:online",
			google_ehld_start_cpu, google_ehld_stop_cpu);
	if (ret > 0)
		ret = 0;

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
		return 0;
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

	err = google_ehld_setup();
	if (err) {
		dev_err(ehld_dev.dev, "ehld: fail setup for ehld:%d\n", err);
		gdmc_iface_put(ehld_dev.gdmc_iface);
		return err;
	}

	/* Register Call back function to receive messages from GDMC */
	err = gdmc_register_host_cb(ehld_dev.gdmc_iface, APC_CRITICAL_GDMC_EHLD_SERVICE,
				google_mba_apc_critical_ehld_service_handler, &ehld_dev);
	if (err) {
		dev_err(ehld_dev.dev, "ehld: failed to register mba callback function:%d\n", err);
		gdmc_iface_put(ehld_dev.gdmc_iface);
		return err;
	}

	/* Send Mailbox message to GDMC to setup periodic timer parameters */
	err = gdmc_ehld_timer_param_config(ehld_main.interval, ehld_main.warn_count);
	if (err) {
		dev_err(ehld_dev.dev, "ehld: failed to enable GDMC periodic timer:%d\n", err);
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

	return 0;
}

static void ehld_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);

	if (ehld_dev.gdmc_iface) {
		gdmc_unregister_host_cb(ehld_dev.gdmc_iface, APC_CRITICAL_GDMC_EHLD_SERVICE);
		gdmc_iface_put(ehld_dev.gdmc_iface);
	}

	dev_dbg(ehld_dev.dev, "Removed Google EHLD device.\n");
}

static const struct of_device_id ehld_dt_match[] = {
	{
		.compatible = "google,pmu-ehld",
	},
	{},
};
MODULE_DEVICE_TABLE(of, ehld_dt_match);

static struct platform_driver ehld_driver = {
	.probe = ehld_probe,
	.remove = ehld_remove,
	.driver = {
		.name = "pmu_ehld",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ehld_dt_match),
	},
};
module_platform_driver(ehld_driver);

MODULE_DESCRIPTION("Module for Detecting HW lockup at an early time");
MODULE_AUTHOR("Hosung Kim <hosung0.kim@samsung.com");
MODULE_LICENSE("GPL v2");
