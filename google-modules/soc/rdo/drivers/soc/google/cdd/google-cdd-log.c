// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/ktime.h>
#include <linux/clk-provider.h>
#include <linux/sched/clock.h>
#include <linux/ftrace.h>
#include <linux/kernel_stat.h>
#include <linux/irqnr.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>

#include <asm/stacktrace.h>
#include <soc/google/google-cdd.h>
#include "google-cdd-local.h"

#include <trace/events/sched.h>
#include <trace/events/workqueue.h>
#include <trace/events/irq.h>

struct google_cdd_log_item cdd_log_items[] = {
	[CDD_LOG_TASK_ID]	= {CDD_LOG_TASK,	{0, 0, 0, true}, },
	[CDD_LOG_WORK_ID]	= {CDD_LOG_WORK,	{0, 0, 0, true}, },
	[CDD_LOG_CPUIDLE_ID]	= {CDD_LOG_CPUIDLE,	{0, 0, 0, true}, },
	[CDD_LOG_SUSPEND_ID]	= {CDD_LOG_SUSPEND,	{0, 0, 0, true}, },
	[CDD_LOG_IRQ_ID]	= {CDD_LOG_IRQ,		{0, 0, 0, false}, },
	[CDD_LOG_FREQ_ID]	= {CDD_LOG_FREQ,	{0, 0, 0, true}, },
};

/*  Internal interface variable */
struct google_cdd_log *cdd_log;
struct google_cdd_log_misc cdd_log_misc;
static char cdd_freq_name[CDD_DOMAIN_NUM][CDD_FREQ_MAX_NAME_SIZE];
static unsigned int cdd_freq_size;

#define cdd_get_log(item)						\
static __maybe_unused							\
long cdd_get_len_##item##_log(void) {					\
	return ARRAY_SIZE(cdd_log->item);				\
}									\
static __maybe_unused							\
long cdd_get_last_##item##_log_idx(void) {				\
	return (atomic_read(&cdd_log_misc.item##_log_idx) - 1) &	\
			(cdd_get_len_##item##_log() - 1);		\
}									\
static __maybe_unused							\
long cdd_get_first_##item##_log_idx(void) {				\
	return atomic_read(&cdd_log_misc.item##_log_idx) &		\
			(cdd_get_len_##item##_log() - 1);		\
}									\
static __maybe_unused							\
struct item##_log *cdd_get_last_##item##_log(void) {			\
	return &cdd_log->item[cdd_get_last_##item##_log_idx()];		\
}									\
static __maybe_unused							\
struct item##_log *cdd_get_first_##item##_log(void) {			\
	return &cdd_log->item[cdd_get_first_##item##_log_idx()];	\
}									\
static __maybe_unused							\
struct item##_log *cdd_get_##item##_log_by_idx(int idx) {		\
	if (idx < 0 || idx >= cdd_get_len_##item##_log())		\
		return NULL;						\
	return &cdd_log->item[idx];					\
}									\
static __maybe_unused							\
struct item##_log *cdd_get_##item##_log_iter(int idx) {			\
	if (idx < 0)							\
		idx = cdd_get_len_##item##_log() - abs(idx);		\
	if (idx >= cdd_get_len_##item##_log())				\
		idx -= cdd_get_len_##item##_log();			\
	return &cdd_log->item[idx];					\
}									\
static __maybe_unused							\
unsigned long cdd_get_vaddr_##item##_log(void) {			\
	return (unsigned long)cdd_log->item;				\
}									\

#define cdd_get_log_by_cpu(item)					\
static __maybe_unused							\
long cdd_get_len_##item##_log(void) {					\
	return ARRAY_SIZE(cdd_log->item);				\
}									\
static __maybe_unused							\
long cdd_get_len_##item##_log_by_cpu(int cpu) {				\
	if (cpu < 0 || cpu >= cdd_get_len_##item##_log())		\
		return -EINVAL;						\
	return ARRAY_SIZE(cdd_log->item[cpu]);				\
}									\
static __maybe_unused							\
long cdd_get_last_##item##_log_idx(int cpu) {				\
	if (cpu < 0 || cpu >= cdd_get_len_##item##_log())		\
		return -EINVAL;						\
	return (atomic_read(&cdd_log_misc.item##_log_idx[cpu]) - 1) &	\
			(cdd_get_len_##item##_log_by_cpu(cpu) - 1);	\
}									\
static __maybe_unused							\
long cdd_get_first_##item##_log_idx(int cpu) {				\
	if (cpu < 0 || cpu >= cdd_get_len_##item##_log())		\
		return -EINVAL;						\
	return atomic_read(&cdd_log_misc.item##_log_idx[cpu]) &		\
			(cdd_get_len_##item##_log_by_cpu(cpu) - 1);	\
}									\
static __maybe_unused							\
struct item##_log *cdd_get_last_##item##_log(int cpu) {			\
	if (cpu < 0 || cpu >= cdd_get_len_##item##_log())		\
		return NULL;						\
	return &cdd_log->item[cpu][cdd_get_last_##item##_log_idx(cpu)];	\
}									\
static __maybe_unused							\
struct item##_log *cdd_get_first_##item##_log(int cpu) {		\
	if (cpu < 0 || cpu >= cdd_get_len_##item##_log())		\
		return NULL;						\
	return &cdd_log->item[cpu][cdd_get_first_##item##_log_idx(cpu)];\
}									\
static __maybe_unused							\
struct item##_log *cdd_get_##item##_log_by_idx(int cpu, int idx) {	\
	if (cpu < 0 || cpu >= cdd_get_len_##item##_log())		\
		return NULL;						\
	if (idx < 0 || idx >= cdd_get_len_##item##_log_by_cpu(cpu))	\
		return NULL;						\
	return &cdd_log->item[cpu][idx];				\
}									\
static __maybe_unused							\
struct item##_log *cdd_get_##item##_log_by_cpu_iter(int cpu, int idx) {	\
	if (cpu < 0 || cpu >= cdd_get_len_##item##_log())		\
		return NULL;						\
	if (idx < 0)							\
		idx = cdd_get_len_##item##_log_by_cpu(cpu) - abs(idx);	\
	if (idx >= cdd_get_len_##item##_log_by_cpu(cpu))		\
		idx -= cdd_get_len_##item##_log_by_cpu(cpu);		\
	return &cdd_log->item[cpu][idx];				\
}									\
static __maybe_unused							\
unsigned long cdd_get_vaddr_##item##_log_by_cpu(int cpu) {		\
	if (cpu < 0 || cpu >= cdd_get_len_##item##_log())		\
		return 0;						\
	return (unsigned long)cdd_log->item[cpu];			\
}

cdd_get_log_by_cpu(task);
cdd_get_log_by_cpu(work);
cdd_get_log_by_cpu(cpuidle);
cdd_get_log(suspend);
cdd_get_log_by_cpu(irq);
cdd_get_log(freq);

#define log_item_set_filed(id, log_name)				\
		log_item = &cdd_log_items[CDD_LOG_##id##_ID];		\
		log_item->entry.vaddr = (size_t)(&cdd_log->log_name[0]);\
		log_item->entry.paddr = item->entry.paddr +		\
					((size_t)&cdd_log->log_name[0] -\
					(size_t)&cdd_log->task[0]);	\
		log_item->entry.size = sizeof(cdd_log->log_name);	\
		if (!log_item->entry.paddr || !log_item->entry.vaddr	\
				|| !log_item->entry.size)		\
			log_item->entry.enabled = false

static inline bool google_cdd_is_log_item_enabled(int id)
{
	bool item_enabled = cdd_items[CDD_ITEM_KEVENTS_ID].entry.enabled;
	bool log_enabled = cdd_log_items[id].entry.enabled;

	return google_cdd_get_enable() && item_enabled && log_enabled;
}

void google_cdd_log_output(void)
{
	unsigned long i;

	pr_info("google-cdd-log physical / virtual memory layout:\n");
	for (i = 0; i < ARRAY_SIZE(cdd_log_items); i++) {
		if (cdd_log_items[i].entry.enabled)
			pr_info("%-12s: phys:%pa / virt:%pK / size:0x%zx / en:%d\n",
				cdd_log_items[i].name,
				&cdd_log_items[i].entry.paddr,
				(void *) cdd_log_items[i].entry.vaddr,
				cdd_log_items[i].entry.size,
				cdd_log_items[i].entry.enabled);
	}
}

static void google_cdd_task(int cpu, struct task_struct *v_task)
{
	unsigned long i;

	if (!google_cdd_is_log_item_enabled(CDD_LOG_TASK_ID))
		return;

	i = atomic_fetch_inc(&cdd_log_misc.task_log_idx[cpu]) % ARRAY_SIZE(cdd_log->task[0]);
	cdd_log->task[cpu][i].time = cpu_clock(cpu);
	cdd_log->task[cpu][i].task = v_task;
	cdd_log->task[cpu][i].pid = v_task->pid;
	cdd_log->task[cpu][i].se_exec_start = v_task->se.exec_start;
	strncpy(cdd_log->task[cpu][i].task_comm, v_task->comm, TASK_COMM_LEN - 1);
}

static void google_cdd_sched_switch(void *ignore, bool preempt, struct task_struct *prev,
				    struct task_struct *next, unsigned int prev_state)
{
	google_cdd_task(raw_smp_processor_id(), next);
}

static void google_cdd_work(work_func_t fn, int en)
{
	int cpu = raw_smp_processor_id();
	unsigned long i;

	if (!google_cdd_is_log_item_enabled(CDD_LOG_WORK_ID))
		return;

	i = atomic_fetch_inc(&cdd_log_misc.work_log_idx[cpu]) % ARRAY_SIZE(cdd_log->work[0]);
	cdd_log->work[cpu][i].time = cpu_clock(cpu);
	cdd_log->work[cpu][i].fn = fn;
	cdd_log->work[cpu][i].en = en;
}

static void google_cdd_wq_start(void *ignore, struct work_struct *work)
{
	google_cdd_work(work->func, CDD_FLAG_IN);
}

static void google_cdd_wq_end(void *ignore, struct work_struct *work, work_func_t func)
{
	google_cdd_work(func, CDD_FLAG_OUT);
}

void google_cdd_cpuidle_mod_by_id(int cpu, char *modes, unsigned int state, s64
				  diff, int en)
{
	unsigned int i;

	if (!cdd_log)
		return;

	if (!google_cdd_is_log_item_enabled(CDD_LOG_CPUIDLE_ID))
		return;

	i = atomic_fetch_inc(&cdd_log_misc.cpuidle_log_idx[cpu]) %
		ARRAY_SIZE(cdd_log->cpuidle[0]);
	cdd_log->cpuidle[cpu][i].time = local_clock();
	cdd_log->cpuidle[cpu][i].modes = modes;
	cdd_log->cpuidle[cpu][i].state = state;
	cdd_log->cpuidle[cpu][i].num_online_cpus = num_online_cpus();
	cdd_log->cpuidle[cpu][i].delta = (int)diff;
	cdd_log->cpuidle[cpu][i].en = en;
}
EXPORT_SYMBOL_GPL(google_cdd_cpuidle_mod_by_id);

static void google_cdd_irq(int irq, void *fn, int en)
{
	unsigned long flags, i;
	int cpu = raw_smp_processor_id();

	if (!google_cdd_is_log_item_enabled(CDD_LOG_IRQ_ID))
		return;

	i = atomic_fetch_inc(&cdd_log_misc.irq_log_idx[cpu]) % ARRAY_SIZE(cdd_log->irq[0]);
	flags = arch_local_irq_save();
	cdd_log->irq[cpu][i].time = cpu_clock(cpu);
	cdd_log->irq[cpu][i].irq = irq;
	cdd_log->irq[cpu][i].fn = fn;
	cdd_log->irq[cpu][i].desc = irq_to_desc(irq);
	cdd_log->irq[cpu][i].en = en;
	arch_local_irq_restore(flags);
}

static void google_cdd_irq_entry(void *ignore, int irq, struct irqaction *action)
{
	google_cdd_irq(irq, action->handler, CDD_FLAG_IN);
}

static void google_cdd_irq_exit(void *ignore, int irq, struct irqaction *action, int ret)
{
	google_cdd_irq(irq, action->handler, CDD_FLAG_OUT);
}

void google_cdd_freq(enum cdd_log_freq_domain domain, unsigned int *old_freq,
		     unsigned int *target_freq)
{
	if (domain >= CDD_DOMAIN_NUM) {
		pr_err("Invalid cdd log freq domain(%u)\n", domain);
		return;
	}

	google_cdd_freq_by_name(cdd_freq_name[domain], old_freq, target_freq);
}
EXPORT_SYMBOL_GPL(google_cdd_freq);

void google_cdd_freq_by_name(const char *name, unsigned int *old_freq,
		     unsigned int *target_freq)
{
	unsigned long i;

	if (!name)
		return;

	if (!cdd_log)
		return;

	if (!google_cdd_is_log_item_enabled(CDD_LOG_FREQ_ID))
		return;

	i = atomic_read(&cdd_log_misc.freq_log_idx);
	if (i >= CDD_DOMAIN_NUM) {
		pr_err("Invalid cdd log index(%lu)\n", i);
		return;
	}
	i = atomic_fetch_inc(&cdd_log_misc.freq_log_idx);

	cdd_log->freq[i].freq_name = name;
	cdd_log->freq[i].old_freq = old_freq;
	cdd_log->freq[i].target_freq = target_freq;
}
EXPORT_SYMBOL_GPL(google_cdd_freq_by_name);

static inline void google_cdd_get_sec(unsigned long long ts, unsigned long *sec,
				      unsigned long *msec)
{
	*sec = ts / NSEC_PER_SEC;
	*msec = (ts % NSEC_PER_SEC) / USEC_PER_MSEC;
}

static void google_cdd_print_last_task(int cpu)
{
	struct google_cdd_log_item *log_item = &cdd_log_items[CDD_LOG_TASK_ID];
	unsigned long idx, sec, msec;
	struct task_struct *task;

	if (!cdd_log)
		return;
	if (!log_item->entry.enabled)
		return;

	idx = cdd_get_last_task_log_idx(cpu);
	google_cdd_get_sec(cdd_log->task[cpu][idx].time, &sec, &msec);
	task = cdd_log->task[cpu][idx].task;

	pr_info("%-12s: [%4lu] %10lu.%06lu sec, %10s: %-16s, %8s: 0x%-16px, %10s: %16llu\n",
		">>> task", idx, sec, msec,
		"task_comm", (task) ? task->comm : "NULL",
		"task", task,
		"exec_start", (task) ? task->se.exec_start : 0);
}

static void google_cdd_print_last_work(int cpu)
{
	struct google_cdd_log_item *log_item = &cdd_log_items[CDD_LOG_WORK_ID];
	unsigned long idx, sec, msec;

	if (!cdd_log)
		return;
	if (!log_item->entry.enabled)
		return;

	idx = cdd_get_last_work_log_idx(cpu);
	google_cdd_get_sec(cdd_log->work[cpu][idx].time, &sec, &msec);

	pr_info("%-12s: [%4lu] %10lu.%06lu sec, %10s: %pS, %3s: %3d %s\n",
		">>> work", idx, sec, msec,
		"work_fn", cdd_log->work[cpu][idx].fn,
		"en", cdd_log->work[cpu][idx].en,
		(cdd_log->work[cpu][idx].en == 1) ? "[Mismatch]" : "");
}

static void google_cdd_print_last_cpuidle(int cpu)
{
	struct google_cdd_log_item *log_item = &cdd_log_items[CDD_LOG_CPUIDLE_ID];
	unsigned long idx, sec, msec;

	if (!cdd_log)
		return;

	if (!log_item->entry.enabled)
		return;

	idx = cdd_get_last_cpuidle_log_idx(cpu);
	google_cdd_get_sec(cdd_log->cpuidle[cpu][idx].time, &sec, &msec);

	pr_info("%-12s: [%4lu] %10lu.%06lu sec, %10s: %s, %s: %d, %s: %d, %s: %d, %s: %d %s\n",
			">>> cpuidle", idx, sec, msec,
			"modes", cdd_log->cpuidle[cpu][idx].modes,
			"state", cdd_log->cpuidle[cpu][idx].state,
			"stay time", cdd_log->cpuidle[cpu][idx].delta,
			"online_cpus", cdd_log->cpuidle[cpu][idx].num_online_cpus,
			"en", cdd_log->cpuidle[cpu][idx].en,
			(cdd_log->cpuidle[cpu][idx].en == 1) ? "[Mismatch]" : "");
}

static void google_cdd_print_last_irq(int cpu)
{
	struct google_cdd_log_item *log_item = &cdd_log_items[CDD_LOG_IRQ_ID];
	unsigned long idx, sec, msec;

	if (!cdd_log)
		return;

	if (!log_item->entry.enabled)
		return;

	idx = cdd_get_last_irq_log_idx(cpu);
	google_cdd_get_sec(cdd_log->irq[cpu][idx].time, &sec, &msec);

	pr_info("%-12s: [%4ld] %10lu.%06lu sec, %10s: %pS, %8s: %8d, %10s: %2d, %s\n",
			">>> irq", idx, sec, msec,
			"handler", cdd_log->irq[cpu][idx].fn,
			"irq", cdd_log->irq[cpu][idx].irq,
			"en", cdd_log->irq[cpu][idx].en,
			(cdd_log->irq[cpu][idx].en == 1) ? "[Mismatch]" : "");
}

static void google_cdd_print_lastinfo(void)
{
	int cpu;
	unsigned int nr_cpu = google_cdd_get_max_core_num();

	if (!cdd_log)
		return;

	if (cdd_log_items[CDD_LOG_TASK_ID].entry.enabled ||
	    cdd_log_items[CDD_LOG_WORK_ID].entry.enabled ||
	    cdd_log_items[CDD_LOG_CPUIDLE_ID].entry.enabled ||
	    cdd_log_items[CDD_LOG_IRQ_ID].entry.enabled)
		goto print_last_info;

	return;

print_last_info:
	pr_info("<last info>\n");
	for (cpu = 0; cpu < nr_cpu; cpu++) {
		pr_info("CPU ID: %d ----------------------------------\n", cpu);
		google_cdd_print_last_task(cpu);
		google_cdd_print_last_work(cpu);
		google_cdd_print_last_cpuidle(cpu);
		google_cdd_print_last_irq(cpu);
	}
}

static void google_cdd_print_freqinfo(void)
{
	struct google_cdd_log_item *log_item = &cdd_log_items[CDD_LOG_FREQ_ID];
	unsigned int *old_freq, *target_freq;
	unsigned int i, idx;

	if (!cdd_log)
		return;

	if (!log_item->entry.enabled)
		return;

	pr_info("\n<last freq info>\n");
	idx = atomic_read(&cdd_log_misc.freq_log_idx);
	for (i = 0; i < idx; i++) {
		if (!cdd_log->freq[i].freq_name) {
			pr_info("no information\n");
			continue;
		}

		old_freq = cdd_log->freq[i].old_freq;
		target_freq = cdd_log->freq[i].target_freq;
		pr_info("%10s: %9s: %8u Khz, %12s: %8u Khz\n",
			cdd_log->freq[i].freq_name ?: "UNKNOWN",
			"old_freq", old_freq ? *old_freq : 0x0,
			"target_freq", target_freq ? *target_freq : 0x0);
	}
}

#ifndef arch_irq_stat
#define arch_irq_stat() 0
#endif

#define IRQ_THRESHOLD 50

static void google_cdd_print_irq(void)
{
	int i, cpu;
	u64 sum = 0;
	struct timespec64 tp;
	unsigned long long irq_filter = 0;
	ktime_get_boottime_ts64(&tp);
	irq_filter = tp.tv_sec * IRQ_THRESHOLD;

	for_each_possible_cpu(cpu)
		sum += kstat_cpu_irqs_sum(cpu);

	sum += arch_irq_stat();

	pr_info("<irq info>\n");
	pr_info("----------------------------------------------------------\n");
	pr_info("sum irq : %llu", sum);
	pr_info("----------------------------------------------------------\n");

	for_each_irq_nr(i) {
		struct irq_data *data;
		struct irq_desc *desc;
		unsigned int irq_stat = 0;
		const char *name;

		data = irq_get_irq_data(i);
		if (!data)
			continue;

		desc = irq_data_to_desc(data);

		for_each_possible_cpu(cpu)
			irq_stat += per_cpu(desc->kstat_irqs->cnt, cpu);

		if (!irq_stat || irq_stat < irq_filter)
			continue;

		if (desc->action && desc->action->name)
			name = desc->action->name;
		else
			name = "???";
		pr_info("irq-%-4d(hwirq-%-3d) : %8u %s\n",
			i, (int)desc->irq_data.hwirq, irq_stat, name);
	}
}

static void google_cdd_print_wdt_pet_time(void)
{
	void __iomem *header;
	int i, src_idx;

	header = google_cdd_get_header_vaddr();
	if (!header)
		return;

	pr_info("\n<last wdt pet time>\n");
	src_idx = __raw_readl(header + CDD_OFFSET_WDT_PET_TIME_IDX);
	if (src_idx >= CDD_WDT_PET_TIME_STAT_NUM) {
		pr_err("wdt pet time carveout is invalid\n");
		return;
	}

	for (i = 0; i < CDD_WDT_PET_TIME_STAT_NUM; ++i) {
		u64 mono_raw_ns, mono_raw_sec, gtc_ns, gtc_sec;

		src_idx = (src_idx == CDD_WDT_PET_TIME_STAT_NUM - 1) ? 0 : src_idx + 1;
		mono_raw_ns = __raw_readq(header + CDD_OFFSET_WDT_PET_TIME_STAT(src_idx));
		gtc_ns = __raw_readq(header + CDD_OFFSET_WDT_PET_TIME_STAT(src_idx) +
				     sizeof(mono_raw_ns));

		mono_raw_sec = mono_raw_ns;
		mono_raw_ns = do_div(mono_raw_sec, NSEC_PER_SEC);

		gtc_sec = gtc_ns;
		gtc_ns = do_div(gtc_sec, NSEC_PER_SEC);

		pr_info("kernel: %llu.%09llu s, GTC: %llu.%09llu s\n",
			mono_raw_sec, mono_raw_ns, gtc_sec, gtc_ns);
	}
}

void google_cdd_print_log_report(void)
{

	if (unlikely(!google_cdd_get_enable()))
		return;

	pr_info("==========================================================\n");
	pr_info("Panic Report\n");
	pr_info("==========================================================\n");
	google_cdd_print_lastinfo();
	google_cdd_print_freqinfo();
	google_cdd_print_irq();
	google_cdd_print_wdt_pet_time();
	pr_info("==========================================================\n");
}

void google_cdd_init_log(void)
{
	struct google_cdd_item *item = &cdd_items[CDD_ITEM_KEVENTS_ID];
	struct google_cdd_log_item *log_item;
	int i;

	if (!item->entry.enabled) {
		for (i = 0; i < ARRAY_SIZE(cdd_log_items); i++)
			cdd_log_items[i].entry.enabled = false;
		return;
	}

	log_item_set_filed(TASK, task);
	log_item_set_filed(WORK, work);
	log_item_set_filed(CPUIDLE, cpuidle);
	log_item_set_filed(SUSPEND, suspend);
	log_item_set_filed(IRQ, irq);
	log_item_set_filed(FREQ, freq);
}

void google_cdd_register_vh_log(void)
{
	if (cdd_log_items[CDD_LOG_TASK_ID].entry.enabled) {
		if (register_trace_sched_switch(google_cdd_sched_switch, NULL))
			pr_err("cdd task log VH register failed\n");
	}

	if (cdd_log_items[CDD_LOG_WORK_ID].entry.enabled) {
		if (register_trace_workqueue_execute_start(google_cdd_wq_start, NULL))
			pr_err("cdd wq start log VH register failed\n");

		if (register_trace_workqueue_execute_end(google_cdd_wq_end, NULL))
			pr_err("cdd wq end log VH register failed\n");
	}

	if (cdd_log_items[CDD_LOG_IRQ_ID].entry.enabled) {
		if (register_trace_irq_handler_entry(google_cdd_irq_entry, NULL))
			pr_err("cdd irq handler start log VH register failed\n");
		if (register_trace_irq_handler_exit(google_cdd_irq_exit, NULL))
			pr_err("cdd irq handler end log VH register failed\n");
	}
}

void google_cdd_start_log(struct device *dev)
{
	struct property *prop;
	const char *str;
	unsigned int i = 0;
	struct device_node *np = dev->of_node;
	int count;

	if (google_cdd_is_log_item_enabled(CDD_LOG_SUSPEND_ID)) {
		google_cdd_suspend_init();
	}

	count = of_property_count_strings(np, "freq-names");
	if (count > 0)
		cdd_freq_size = count;
	of_property_for_each_string(np, "freq-names", prop, str) {
		if (i >= ARRAY_SIZE(cdd_freq_name)) {
			dev_warn(dev,
				 "DT specified more 'freq-names' than we can handle (%u vs %zu)\n",
				 cdd_freq_size, ARRAY_SIZE(cdd_freq_name));
			break;
		}
		strscpy(cdd_freq_name[i], str, sizeof(cdd_freq_name[i]));
		++i;
	}

	google_cdd_log_output();
}
