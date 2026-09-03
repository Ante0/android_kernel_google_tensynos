// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024-2025 Google LLC
 */

#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>

#include "irq-gia-lib.h"

#define CREATE_TRACE_POINTS
#include "irq-gia-lib-trace.h"

#define GIA_AUTOSUSPEND_DELAY_MS 10

/* List head linking all the gdd */
static LIST_HEAD(gia_device_list);

/* Global lock for synchronizing access to gia_device_list */
static DEFINE_MUTEX(gia_global_mutex);

/* Due to cyclic dependency, forward declaration is unavoidable here */
static struct irq_chip gia_irq_chip;

/* Window time of default 1 sec in ns */
u32 window_time = NSEC_PER_SEC;

/*
 * gia_lock/unlock APIs are simple wrapper function to take appropriate locks according to the
 * context of the function.
 *
 * All these variants have implicit memory barrier in them. They ensure the transactions happened
 * before gets comepleted before the lock is released or acquired. So, if the register accesses
 * proteced by these lock APIs, no need for explicit memory barrier calls.
 */
static inline void gia_lock_irq_context(raw_spinlock_t *lock)
{
	/*
	 * Why raw_spin_lock() variant here? Because this is already interrupt context and
	 * interrupts to local core are disabled already. So, no need to do extra by saving context
	 * again. Use efficient variant here.
	 */
	raw_spin_lock(lock);
}

static inline void gia_unlock_irq_context(raw_spinlock_t *lock)
{
	raw_spin_unlock(lock);
}

static inline void gia_lock_process_context(raw_spinlock_t *lock, unsigned long *flags)
{
	unsigned long local_flags;

	/*
	 * Why *_irqsave* version of the spinlock? Because, *_irqsave() variant guarantees that
	 * other CPU or interrupt will not be able to preempt the flow. This is very important for
	 * a function which is running in process context, but its critical section is accessed from
	 * interrupt context too (i.e. trigger & mask register). If interrupts are allowed, then
	 * the same lock can be re-acquired by the handler on the same CPU, a classic deadlock.
	 */
	raw_spin_lock_irqsave(lock, local_flags);
	*flags = local_flags;
}

static inline void gia_unlock_process_context(raw_spinlock_t *lock, unsigned long *flags)
{
	raw_spin_unlock_irqrestore(lock, *flags);
}

static inline int gia_get_power(struct gia_device_data *gdd)
{
	int ret;

	if (!gdd->dev->pm_domain)
		return 0;

	trace_gia_get_power(gdd);

	ret = pm_runtime_get_sync(gdd->dev);
	if (ret < 0) {
		dev_warn(gdd->dev, "Error turning on power: %d\n", ret);
		trace_gia_error(gdd, "pm_runtime_get_sync() failed.");
		return ret;
	}

	return 0;
}

static inline void gia_put_power(struct gia_device_data *gdd)
{
	if (!gdd->dev->pm_domain)
		return;

	trace_gia_put_power(gdd);
	pm_runtime_mark_last_busy(gdd->dev);
	pm_runtime_put_autosuspend(gdd->dev);
}

static u32 gia_adjusted_offset(struct gia_device_data *gdd, u32 offset, u32 chip_number)
{
	u32 adjusted_offset;

	/*
	 * gdd->next_bank_base_offset says where exactly the next chip is starting. There are 2
	 * such variants of this. Let's say we've 2 chips. Possible 2 register arrangements can be:
	 *
	 * Non interleaved case: isr0, ier0, imr0, itr0, isr1, ier1, imr1, itr1
	 * Interleaved case: isr0, isr1, ier0, ier1, imr0, imr1, itr0, itr1
	 *
	 * So, gdd->next_bank_base_offset for non interleaved case is 0x10, because that's the
	 * offset of isr1. For the interleaved case, the isr1 offset is 0x4. This info helps locate
	 * all registers from any specific chip.
	 */
	if (gdd->next_bank_base_offset == OFFSET_FOR_INTERLEAVED_PATTERN)
		adjusted_offset = (gdd->next_bank_base_offset * chip_number) +
				  (gdd->nr_irq_chips * offset);
	else
		adjusted_offset = (gdd->next_bank_base_offset * chip_number) + offset;

	return adjusted_offset;
}

static void __iomem *gia_reg_addr(struct gia_device_data *gdd, u32 offset, u32 chip_number)
{
	u32 adjusted_offset = offset;

	if (!gdd->type_data->wide_gia)
		goto out;

	adjusted_offset = gia_adjusted_offset(gdd, offset, chip_number);

out:
	return gdd->io_base + adjusted_offset;
}

static inline bool any_wake_irqs_enabled(struct gia_device_data *gdd)
{
	for (int i = 0; i < gdd->nr_irq_chips; i++) {
		if (gdd->wake_cache[i])
			return true;
	}
	return false;
}

/* Root sysfs directory object */
struct kobject *gia_sysfs_kobj;

const char *null_name = "(null)";
const char *unclaimed_name = "unclaimed";
static bool gia_telemetry_on = true;

static int gia_telemetry_memory_alloc(struct gia_device_data *gdd)
{
	gdd->telemetry.concurrency = devm_kmalloc(gdd->dev, sizeof(*(gdd->telemetry.concurrency)) *
						  MAX_CONCURRENCY, GFP_KERNEL);
	if (!gdd->telemetry.concurrency)
		return -ENOMEM;

	gdd->telemetry.irq_latencies = devm_kzalloc(gdd->dev, sizeof(struct gia_irq_latency *) *
						    gdd->nr_irq_chips * IRQS_PER_REG, GFP_KERNEL);
	if (!gdd->telemetry.irq_latencies)
		return -ENOMEM;

	return 0;
}

static void gia_telemetry_reset(struct gia_device_data *gdd)
{
	unsigned long flags;
	struct gia_irq_latency *entry;
	u32 total_hwirq = gdd->nr_irq_chips * IRQS_PER_REG;

	gia_lock_process_context(&gdd->lock, &flags);

	gdd->telemetry.handler_count = 0;
	gdd->telemetry.gia_handler_latency_min = INT_MAX;
	gdd->telemetry.gia_handler_latency_max = 0;
	gdd->telemetry.gia_handler_latency_total = 0;

	for (int i = 0; i < MAX_CONCURRENCY; i++)
		gdd->telemetry.concurrency[i] = 0;

	for (u32 hwirq = 0; hwirq < total_hwirq; hwirq++) {
		entry = gdd->telemetry.irq_latencies[hwirq];
		if (!entry)
			continue;

		entry->count = 0;
		entry->sw_wait_time_min = INT_MAX;
		entry->sw_wait_time_max = 0;
		entry->sw_wait_time_total = 0;
		entry->handler_time_min = INT_MAX;
		entry->handler_time_max = 0;
		entry->handler_time_total = 0;
		entry->max_freq = 0;
		entry->curr_freq = 0;
		entry->hwirq = hwirq;
	}
	gia_unlock_process_context(&gdd->lock, &flags);
}

static void gia_telemetry_global_reset(void)
{
	struct gia_device_data *gdd;

	mutex_lock(&gia_global_mutex);
	list_for_each_entry(gdd, &gia_device_list, list)
		gia_telemetry_reset(gdd);
	mutex_unlock(&gia_global_mutex);
}

static inline void gia_telemetry_start(struct gia_device_data *gdd)
{
	if (!gia_telemetry_on)
		return;

	gdd->telemetry.start_time = ktime_get();
}

static inline void gia_telemetry_end(struct gia_device_data *gdd, u32 nr_irqs_handled)
{
	u32 gia_handler_latency;
	ktime_t gia_handler_end;

	if (!gia_telemetry_on)
		return;

	gia_handler_end = ktime_get();
	gia_handler_latency = ktime_to_ns(ktime_sub(gia_handler_end, gdd->telemetry.start_time));

	gdd->telemetry.gia_handler_latency_min = min(gia_handler_latency,
						     gdd->telemetry.gia_handler_latency_min);
	gdd->telemetry.gia_handler_latency_max = max(gia_handler_latency,
						     gdd->telemetry.gia_handler_latency_max);

	/*
	 * Can it overflow? Assuming 10 million lifetime interrupts, and each taking 1ms. So, total
	 * would be 10k seconds ~ 10^13 ns. That needs 44 bits. Having u64, we have enough space.
	 */
	gdd->telemetry.gia_handler_latency_total += gia_handler_latency;
	gdd->telemetry.handler_count += 1;

	if (nr_irqs_handled > MAX_CONCURRENCY) {
		/*
		 * Cap nr_irqs_handled as we are not really interested to know how many IRQs
		 * we handled, due to memory constraints.
		 */
		nr_irqs_handled = MAX_CONCURRENCY;
	} else if (nr_irqs_handled == 0) {
		trace_gia_error(gdd, "Handler called but no IRQ handled!!");
		return;
	}

	gdd->telemetry.concurrency[nr_irqs_handled - 1] += 1;
}

static void gia_telemetry_per_irq_start(ktime_t *handler_start)
{
	if (!gia_telemetry_on)
		return;

	*handler_start = ktime_get();
}

static void gia_telemetry_per_irq_end(struct gia_device_data *gdd, u32 hwirq, u32 chip_number,
				      ktime_t start)
{
	struct gia_irq_latency *entry;
	u32 wait_time_in_sw;
	u32 handler_latency;
	u32 window_index;
	ktime_t handler_end;

	handler_end = ktime_get();

	/*
	 * It is a conscious choice that spinlock not taken while doing telemetry write. Because,
	 * the only concurrently occurring scenario is IRQ path and sysfs action happening together.
	 * 2 IRQ handlers on same GIA cannot run together. So, at runtime, in most production
	 * scenarios, not taking spinlock is a great perf win. During sysfs based interaction,
	 * the most racy scenario is during read path. That again, won't be a big problem.
	 */
	if (!gia_telemetry_on)
		return;

	entry = gdd->telemetry.irq_latencies[(chip_number * IRQS_PER_REG) + hwirq];
	if (!entry)
		return;

	wait_time_in_sw = ktime_to_ns(ktime_sub(start, gdd->telemetry.start_time));
	handler_latency = ktime_to_ns(ktime_sub(handler_end, start));

	window_index = ktime_to_ns(start) / window_time;
	if (window_index == entry->last_window_index) {
		entry->curr_freq += 1;
	} else {
		entry->last_window_index = window_index;
		entry->curr_freq = 1;
	}

	entry->max_freq = max(entry->max_freq, entry->curr_freq);

	entry->count += 1;
	entry->sw_wait_time_total += wait_time_in_sw;
	entry->handler_time_total += handler_latency;

	entry->sw_wait_time_min = min(wait_time_in_sw, entry->sw_wait_time_min);
	entry->sw_wait_time_max = max(wait_time_in_sw, entry->sw_wait_time_max);

	entry->handler_time_min = min(handler_latency, entry->handler_time_min);
	entry->handler_time_max = max(handler_latency, entry->handler_time_max);
}

static inline u32 real_min(u32 val)
{
	return val == INT_MAX ? 0 : val;
}

static u32 get_virq_from_kobj(struct kobject *kobj)
{
	struct gia_device_data *gdd;
	struct gia_irq_latency *gil;
	struct kobject *gia_kobj;
	u32 virq;

	gia_kobj = kobj->parent;
	gdd = container_of(gia_kobj, struct gia_device_data, gia_kobj);
	gil = container_of(kobj, struct gia_irq_latency, irq_kobj);
	virq = irq_find_mapping(gdd->irq_domain, gil->hwirq);

	return virq;
}

static ssize_t irq_virq_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	u32 virq;

	virq = get_virq_from_kobj(kobj);
	return sysfs_emit(buf, "%u\n", virq);
}

static ssize_t irq_name_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct gia_device_data *gdd_child;
	struct irq_desc *desc;
	struct irq_data *d;
	const char *name;
	u32 virq;

	virq = get_virq_from_kobj(kobj);
	if (!virq) {
		name = unclaimed_name;
		goto out;
	}

	d = irq_get_irq_data(virq);
	desc = irq_to_desc(virq);
	if (d->chip_data) {
		gdd_child = (struct gia_device_data *) d->chip_data;
		name = gdd_child->dev->of_node->full_name;
	} else if (!desc || !desc->action || !desc->action->name) {
		name = null_name;
	} else {
		name = desc->action->name;
	}

out:
	return sysfs_emit(buf, "%s\n", name);
}

static ssize_t irq_sw_status_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct irq_desc *desc;
	u32 virq;
	int depth;

	virq = get_virq_from_kobj(kobj);
	if (!virq) {
		depth = -EINVAL;
		goto out;
	}

	desc = irq_to_desc(virq);
	if (!desc || !desc->action)
		depth = -EINVAL;
	else
		depth = desc->depth;

out:
	return sysfs_emit(buf, "%s (depth %d)\n", depth == 0 ? "ENABLED" : "DISABLED", depth);
}

static inline bool check_hwirq_reg_status(struct gia_device_data *gdd, int offset, u32 hwirq)
{
	u32 chip_number;
	void __iomem *addr;

	chip_number = hwirq / IRQS_PER_REG;
	addr = gia_reg_addr(gdd, offset, chip_number);

	return (readl_relaxed(addr) & (1UL << (hwirq % IRQS_PER_REG))) != 0;
}

static ssize_t irq_hw_status_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct gia_device_data *gdd;
	struct gia_irq_latency *gil;
	struct kobject *gia_kobj;
	bool pending, enabled, unmasked;
	unsigned long flags;

	gia_kobj = kobj->parent;
	gdd = container_of(gia_kobj, struct gia_device_data, gia_kobj);
	gil = container_of(kobj, struct gia_irq_latency, irq_kobj);

	/*
	 * gia_get_power can fail when the system is collecting a bugreport which includes hw_stats
	 * If it fails, we must not touch the GIA registers since that will cause an SError
	 */
	if (gia_get_power(gdd)) {
		gia_put_power(gdd);
		return sysfs_emit(buf, "NO POWER\n");
	}

	gia_lock_process_context(&gdd->lock, &flags);

	enabled = check_hwirq_reg_status(gdd, gdd->type_data->enable_offset, gil->hwirq);
	unmasked = check_hwirq_reg_status(gdd, gdd->type_data->mask_offset, gil->hwirq);
	pending = check_hwirq_reg_status(gdd, gdd->type_data->status_offset, gil->hwirq);
	if (gdd->type_data->check_overflow_reg)
		pending |= check_hwirq_reg_status(gdd, gdd->type_data->status_overflow_offset,
						  gil->hwirq);

	gia_unlock_process_context(&gdd->lock, &flags);
	gia_put_power(gdd);

	return sysfs_emit(buf, "%s, %s, %s\n",
			  enabled ? "ENABLED" : "DISABLED",
			  unmasked ? "UNMASKED" : "MASKED",
			  pending ? "PENDING" : "CLEARED");
}

#define DEFINE_IRQ_SHOW(name, value_expr, format, type)					\
	static ssize_t irq_##name##_show(struct kobject *kobj,				\
					 struct kobj_attribute *attr, char *buf)	\
	{										\
		struct gia_irq_latency *gil;						\
		gil = container_of(kobj, struct gia_irq_latency, irq_kobj);		\
		return sysfs_emit(buf, format, (type)(value_expr));			\
	}

/* Macro Invocations (with value expressions) */
DEFINE_IRQ_SHOW(count, gil->count, "%d\n", int)
DEFINE_IRQ_SHOW(max_freq, gil->max_freq, "%d\n", int)
DEFINE_IRQ_SHOW(handler_time_min, real_min(gil->handler_time_min), "%u\n", unsigned int)
DEFINE_IRQ_SHOW(handler_time_max, gil->handler_time_max, "%u\n", unsigned int)
DEFINE_IRQ_SHOW(handler_time_avg, (gil->count == 0) ? 0 :
		(gil->handler_time_total / gil->count), "%llu\n", unsigned long long)
DEFINE_IRQ_SHOW(sw_wait_time_min, real_min(gil->sw_wait_time_min), "%u\n", unsigned int)
DEFINE_IRQ_SHOW(sw_wait_time_max, gil->sw_wait_time_max, "%u\n", unsigned int)
DEFINE_IRQ_SHOW(sw_wait_time_avg, (gil->count == 0) ? 0 :
		(gil->sw_wait_time_total / gil->count), "%llu\n", unsigned long long)

static struct kobj_attribute irq_virq_attr = __ATTR_RO(irq_virq);
static struct kobj_attribute irq_name_attr = __ATTR_RO(irq_name);
static struct kobj_attribute irq_sw_status_attr = __ATTR_RO(irq_sw_status);
static struct kobj_attribute irq_hw_status_attr = __ATTR_RO(irq_hw_status);
static struct kobj_attribute irq_count_attr = __ATTR_RO(irq_count);
static struct kobj_attribute irq_max_freq_attr = __ATTR_RO(irq_max_freq);
static struct kobj_attribute irq_handler_time_min_attr = __ATTR_RO(irq_handler_time_min);
static struct kobj_attribute irq_handler_time_max_attr = __ATTR_RO(irq_handler_time_max);
static struct kobj_attribute irq_handler_time_avg_attr = __ATTR_RO(irq_handler_time_avg);
static struct kobj_attribute irq_sw_wait_time_min_attr = __ATTR_RO(irq_sw_wait_time_min);
static struct kobj_attribute irq_sw_wait_time_max_attr = __ATTR_RO(irq_sw_wait_time_max);
static struct kobj_attribute irq_sw_wait_time_avg_attr = __ATTR_RO(irq_sw_wait_time_avg);

static struct attribute *per_irq_attrs[] = {
	&irq_virq_attr.attr,
	&irq_name_attr.attr,
	&irq_sw_status_attr.attr,
	&irq_hw_status_attr.attr,
	&irq_count_attr.attr,
	&irq_max_freq_attr.attr,
	&irq_handler_time_min_attr.attr,
	&irq_handler_time_max_attr.attr,
	&irq_handler_time_avg_attr.attr,
	&irq_sw_wait_time_min_attr.attr,
	&irq_sw_wait_time_max_attr.attr,
	&irq_sw_wait_time_avg_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(per_irq);

static const struct kobj_type per_irq_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = per_irq_groups,
};


#define DEFINE_GIA_SHOW(name, value_expr, format, type)					\
	static ssize_t gia_##name##_show(struct kobject *kobj,				\
					 struct kobj_attribute *attr, char *buf)	\
	{										\
		struct gia_device_data *gdd;						\
		gdd = container_of(kobj, struct gia_device_data, gia_kobj);		\
		return sysfs_emit(buf, format, (type)(value_expr));			\
	}

/* Macro Invocations (with value expressions) */
DEFINE_GIA_SHOW(count, gdd->telemetry.handler_count, "%d\n", int)
DEFINE_GIA_SHOW(handler_latency_min, real_min(gdd->telemetry.gia_handler_latency_min), "%u\n",
					      unsigned int)
DEFINE_GIA_SHOW(handler_latency_max, gdd->telemetry.gia_handler_latency_max, "%u\n", unsigned int)
DEFINE_GIA_SHOW(handler_latency_avg, (gdd->telemetry.handler_count == 0) ? 0 :
		(gdd->telemetry.gia_handler_latency_total / gdd->telemetry.handler_count), "%llu\n",
		unsigned long long)
DEFINE_GIA_SHOW(concurrency_1, gdd->telemetry.concurrency[0], "%u\n", unsigned int)
DEFINE_GIA_SHOW(concurrency_2, gdd->telemetry.concurrency[1], "%u\n", unsigned int)
DEFINE_GIA_SHOW(concurrency_3, gdd->telemetry.concurrency[2], "%u\n", unsigned int)
DEFINE_GIA_SHOW(concurrency_4, gdd->telemetry.concurrency[3], "%u\n", unsigned int)

static struct kobj_attribute gia_count_attr = __ATTR_RO(gia_count);
static struct kobj_attribute gia_handler_latency_min_attr = __ATTR_RO(gia_handler_latency_min);
static struct kobj_attribute gia_handler_latency_max_attr = __ATTR_RO(gia_handler_latency_max);
static struct kobj_attribute gia_handler_latency_avg_attr = __ATTR_RO(gia_handler_latency_avg);
static struct kobj_attribute gia_concurrency_1_attr = __ATTR_RO(gia_concurrency_1);
static struct kobj_attribute gia_concurrency_2_attr = __ATTR_RO(gia_concurrency_2);
static struct kobj_attribute gia_concurrency_3_attr = __ATTR_RO(gia_concurrency_3);
static struct kobj_attribute gia_concurrency_4_attr = __ATTR_RO(gia_concurrency_4);

static struct attribute *per_gia_attrs[] = {
	&gia_count_attr.attr,
	&gia_handler_latency_min_attr.attr,
	&gia_handler_latency_max_attr.attr,
	&gia_handler_latency_avg_attr.attr,
	&gia_concurrency_1_attr.attr,
	&gia_concurrency_2_attr.attr,
	&gia_concurrency_3_attr.attr,
	&gia_concurrency_4_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(per_gia);

static const struct kobj_type per_gia_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = per_gia_groups,
};

static int gia_sysfs_create_per_gia_dir(struct gia_device_data *gdd)
{
	if (!gia_sysfs_kobj)
		return -EIO;

	if (kobject_init_and_add(&gdd->gia_kobj,
				 &per_gia_ktype, gia_sysfs_kobj, "%s",
				 gdd->dev->of_node->full_name)) {
		dev_err(gdd->dev, "Failed to create sysfs folder for this device\n");
		return -EIO;
	}

	return 0;
}

static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", gia_telemetry_on ? 1 : 0);
}

static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
			    size_t count)
{
	int enable_val;
	int ret;

	ret = kstrtoint(buf, 0, &enable_val);
	if (ret < 0)
		return ret;

	if (enable_val != 0 && enable_val != 1)
		return -EINVAL;

	if (enable_val == 1)
		gia_telemetry_on = true;
	else
		gia_telemetry_on = false;

	return count;
}

static ssize_t reset_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	int reset_val;
	int ret;

	ret = kstrtoint(buf, 0, &reset_val);
	if (ret < 0)
		return ret;

	if (reset_val == 1)
		gia_telemetry_global_reset();
	else
		return -EINVAL;

	return count;
}

static struct kobj_attribute enable_attr = __ATTR_RW(enable);

static struct kobj_attribute reset_attr = __ATTR_WO(reset);

static int __init gia_sysfs_init(void)
{
	int ret;

	gia_sysfs_kobj = kobject_create_and_add("gia", kernel_kobj);
	if (!gia_sysfs_kobj) {
		pr_err("gia: Failed to create gia root directory in sysfs\n");
		return -EIO;
	}

	ret = sysfs_create_file(gia_sysfs_kobj, &enable_attr.attr);
	if (ret) {
		pr_err("gia: Failed to create 'enable' file in sysfs\n");
		goto error_enable;
	}

	ret = sysfs_create_file(gia_sysfs_kobj, &reset_attr.attr);
	if (ret) {
		pr_err("gia: Failed to create 'reset' file in sysfs\n");
		goto error_reset;
	}

	return 0;

error_reset:
	sysfs_remove_file(gia_sysfs_kobj, &enable_attr.attr);

error_enable:
	kobject_put(gia_sysfs_kobj);
	return ret;
}

static inline u32 gia_process_each_bit(struct gia_device_data *gdd, u32 status_offset,
				       u32 chip_number)
{
	unsigned long isr;
	u32 imr;
	u32 hwirq;
	void __iomem *isr_addr;
	u32 nr_irqs_handled = 0;
	ktime_t handler_start;

	gia_lock_irq_context(&gdd->lock);

	isr_addr = gia_reg_addr(gdd, status_offset, chip_number);
	imr = gdd->mask_cache[chip_number];
	isr = readl_relaxed(isr_addr) & imr;

	if (!isr) {
		gia_unlock_irq_context(&gdd->lock);
		goto out;
	}

	/*
	 * Some GIAs require ISR to be cleared explicitly, some doesn't. Generally, the pulse
	 * aggregator keep the local ISR copy as input signal is going to get dropped. That's not
	 * the case with level. So, ISR *may* get reset when the line de-asserts.
	 */
	if (gdd->type_data->clear_status_reg)
		writel_relaxed(isr, isr_addr);

	gia_unlock_irq_context(&gdd->lock);

	for_each_set_bit(hwirq, &isr, IRQS_PER_REG) {
		gia_telemetry_per_irq_start(&handler_start);
		generic_handle_domain_irq(gdd->irq_domain, (chip_number * IRQS_PER_REG) + hwirq);
		gia_telemetry_per_irq_end(gdd, hwirq, chip_number, handler_start);
		nr_irqs_handled += 1;
	}

out:
	return nr_irqs_handled;
}

static u32 gia_process_each_status_reg(struct gia_device_data *gdd, u32 offset)
{
	u32 nr_irqs_handled = 0;

	for (int i = 0; i < gdd->nr_irq_chips; i++)
		nr_irqs_handled += gia_process_each_bit(gdd, offset, i);

	return nr_irqs_handled;
}

static void gia_irq_handler_chained(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_data *d = irq_desc_get_irq_data(desc);
	struct gia_device_data *gdd = irq_desc_get_handler_data(desc);
	u32 nr_irqs_handled = 0;

	trace_gia_irq_handler_entry(gdd, d);
	gia_telemetry_start(gdd);

	chained_irq_enter(chip, desc);

	nr_irqs_handled += gia_process_each_status_reg(gdd, gdd->type_data->status_offset);

	/*
	 * Some GIAs have 2nd interrupt registered into overflow register. This generally gets set
	 * when ISR is not cleared and another interrupt was fired. If GIA has overflow reg, better
	 * check it too.
	 */
	if (gdd->type_data->check_overflow_reg)
		nr_irqs_handled +=
			gia_process_each_status_reg(gdd, gdd->type_data->status_overflow_offset);

	chained_irq_exit(chip, desc);

	gia_telemetry_end(gdd, nr_irqs_handled);
	trace_gia_irq_handler_exit(gdd, d);
}

static void gia_irq_mask_update(struct irq_data *d, bool set, bool wake)
{
	struct gia_device_data *gdd = d->domain->host_data;
	unsigned int chip_number = d->hwirq / IRQS_PER_REG;
	unsigned long flags;
	void __iomem *addr;
	u32 imr;
	u32 bit = (1U << (d->hwirq % IRQS_PER_REG));

	gia_lock_process_context(&gdd->lock, &flags);
	addr = gia_reg_addr(gdd, gdd->type_data->mask_offset, chip_number);

	if (wake) {
		if (set) {
			gdd->wake_cache[chip_number] |= bit;
			gdd->has_wake_irqs = true;
		} else {
			gdd->wake_cache[chip_number] &= ~bit;
			gdd->has_wake_irqs = any_wake_irqs_enabled(gdd);
		}
	} else {
		if (set)
			gdd->mask_cache[chip_number] |= bit;
		else
			gdd->mask_cache[chip_number] &= ~bit;
	}

	imr = gdd->mask_cache[chip_number];
	writel_relaxed(imr, addr);
	gia_unlock_process_context(&gdd->lock, &flags);
	trace_gia_cache_update(gdd, chip_number, imr, gdd->has_wake_irqs);
}

static void gia_irq_mask(struct irq_data *d)
{
	struct gia_device_data *gdd = d->domain->host_data;

	trace_gia_irq_mask(gdd, d);
	gia_irq_mask_update(d, false, false);
}

static void gia_irq_unmask(struct irq_data *d)
{
	struct gia_device_data *gdd = d->domain->host_data;

	trace_gia_irq_unmask(gdd, d);
	gia_irq_mask_update(d, true, false);
}

/**
 * The driver calling irq_set_wake must make sure that the power domain stays active for the entire
 * time that wakeup interrupts can be generated. This means that GIA can safely serve those
 * interrupts even if it is pm_runtime suspended.
 *
 * This needs to be orthogonal to regular masking:
 * see comment above irq_set_irq_wake() in kernel/irq/manage.c
 *
 * This needs to propagate to all parents because otherwise they might not serve wakeup interrupts
 * if pm_runtime is off. Clients calling set_wake must ensure that the power domain stays on while
 * wakeup interrupts can generate.
 *
 * With current Pixel architecture, this will *not* change whether an interrupt can wake device from
 * system suspend. That is controlled entirely by CPM. There is no interface for kernel drivers
 * to dynamically choose which interrupts CPM uses to wake AP.
 */
static int gia_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct gia_device_data *gdd = d->domain->host_data;
	struct irq_chip *parent_chip = NULL;
	struct irq_data *parent_data = NULL;

	trace_gia_irq_set_wake(gdd, d, on);
	gia_irq_mask_update(d, on != 0, true);

	if (gdd->nr_out_irq > 0) {
		parent_chip = irq_get_chip(gdd->virqs[0]);
		parent_data = irq_get_irq_data(gdd->virqs[0]);
	}

	if (parent_chip && parent_chip->irq_set_wake && parent_data)
		parent_chip->irq_set_wake(parent_data, gdd->has_wake_irqs);

	return 0;
}

/*
 * irq_bus_lock & irq_bus_sync_unlock hooks are called by IRQ framework when some client calls
 * request_irq(). Well, these hooks are called under process context, a good time to make sleeping
 * calls to enable the power. The other time request_irq() calls into irq_chip hooks are from the
 * atomic context, and we can't really toggle power then.
 */
static void gia_irq_bus_lock(struct irq_data *d)
{
	struct gia_device_data *gdd = d->domain->host_data;

	trace_gia_irq_bus_lock(gdd, d);
	gia_get_power(gdd);
}

static void gia_irq_bus_sync_unlock(struct irq_data *d)
{
	struct gia_device_data *gdd = d->domain->host_data;

	trace_gia_irq_bus_sync_unlock(gdd, d);
	gia_put_power(gdd);
}

static void gia_irq_ack(struct irq_data *d)
{
	struct gia_device_data *gdd = d->domain->host_data;

	trace_gia_irq_ack(gdd, d);
}

static void gia_irq_eoi(struct irq_data *d)
{
	struct gia_device_data *gdd = d->domain->host_data;

	trace_gia_irq_eoi(gdd, d);
}

static int gia_irq_get_irqchip_state(struct irq_data *d,
				     enum irqchip_irq_state which, bool *val)
{
	struct gia_device_data *gdd = d->domain->host_data;

	switch (which) {
	case IRQCHIP_STATE_PENDING:
		*val = check_hwirq_reg_status(gdd, gdd->type_data->status_offset, d->hwirq);
		if (gdd->type_data->check_overflow_reg)
			*val |= check_hwirq_reg_status(gdd, gdd->type_data->status_overflow_offset,
						       d->hwirq);
		break;
	case IRQCHIP_STATE_ACTIVE:
		/* GIA doesn't have ACTIVE state */
		return -EINVAL;
	case IRQCHIP_STATE_MASKED:
		*val = !check_hwirq_reg_status(gdd, gdd->type_data->mask_offset, d->hwirq);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * gia_irq_update_affinity_downstream: This function updates 'effective_affinity' for all child
 *				     irq sources under the GIA.
 *
 * This function works from GIA-1, towards downstream path and update effective_affinity for
 * GIAs [1, 3], interrupt sources S [1, 5] and GPIOs [1, 2]. This can be invoked when any source
 * S [1, 7] wants to set affinity for itself.
 *
 * Note that, GPIO being a separate interrupt controller, GIA driver would not venture into their
 * territory and handle its children. GIA driver would invoke notifier attached with its leaf
 * children and other irq controllers can implement callback, so it can update effective_affinity
 * further downstream.
 *
 *
 *			       ---------
 *			       | GICv3 |
 *			       ---------
 *				   |
 *				   |
 *			       ---------
 *			       | GIA-1 |
 *			       ---------
 *			       |   |   |
 *			---------  |  ---------
 *			| GIA-2 |  |  | GIA-3 |
 *			---------  |  ---------
 *			|   |   |  |  |   |   |
 *			S1  S2  S3 S4 S5  |   |
 *				  ---------- ----------
 *				  | GPIO-1 | | GPIO-2 |
 *				  ---------- ----------
 *				       |	 |
 *				       S6	S7
 */
static void gia_irq_update_affinity_downstream(struct gia_device_data *gdd,
					       const struct cpumask *cpumask)
{
	int max_hwirq, virq;
	struct irq_desc *desc_child;
	struct irq_data *d_child;

	max_hwirq = gdd->nr_irq_chips * IRQS_PER_REG;
	for (int i = 0; i < max_hwirq; i++) {
		virq = irq_find_mapping(gdd->irq_domain, i);
		if (!virq)
			continue;

		d_child = irq_get_irq_data(virq);
		if (!d_child)
			continue;

		/*
		 * It's safe to stop here and don't do further downstream update as any parent or
		 * upstream IRQ's affinity is anyways guaranteed to have propagated downstream
		 * previously.
		 */
		if (*cpumask_bits(irq_data_get_effective_affinity_mask(d_child)) ==
				*cpumask_bits(cpumask))
			continue;

		irq_data_update_effective_affinity(d_child, cpumask);
		trace_gia_irq_update_effective_affinity(gdd, virq, cpumask);

		desc_child = irq_data_to_desc(d_child);

		/* if child is not gia, then they will be notified */
		if (desc_child->affinity_notify) {
			/*
			 * Take a ref count so that desc->affinity_notify struct doesn't get into
			 * racy situation. This struct is allocated by GIA users, and GIA driver
			 * doesn't aware of its lifetime. So, take ref counts to avoid use after
			 * free, etc.
			 */
			kref_get(&desc_child->affinity_notify->kref);
			if (!schedule_work(&desc_child->affinity_notify->work)) {
				/* Work was already scheduled, drop our extra ref */
				kref_put(&desc_child->affinity_notify->kref,
					 desc_child->affinity_notify->release);
			}
		} else if (d_child->chip_data) { /* This confirms child is GIA */
			gia_irq_update_affinity_downstream(d_child->chip_data, cpumask);
		}
	}
}

/*
 * When a device driver (behind GIA) make a request for setting affinity for its interrupts, it
 * can't be really honored truly. That is because, this device's interrupt is getting aggregated at
 * the GIA, and possibly some more GIAs along the path before summary line reaches to GIC. CPU
 * affinity plays role at GIC level. So, client's request need to be passed to parent controller,
 * until it reaches GIC.
 *
 * TODO: At present, the same CPU mask gets transparently passed to parent controller. That means,
 * if device A and B sets the different mask, the one which comes later will remain effective. This
 * can be improved 2 ways:
 *
 * 1) Don't transparently pass cpu_mask up. Do some processing before.
 * 2) _OR_ do something implicitly for all GIAs that they get distributed for best performance o/p
 *
 */
static int gia_irq_set_affinity(struct irq_data *d, const struct cpumask *cpu_mask, bool force)
{
	struct gia_device_data *gdd = d->domain->host_data;
	struct irq_chip *parent_chip = NULL;
	struct irq_data *parent_data = NULL;
	int ret;
	unsigned long flags;

	trace_gia_irq_set_affinity(gdd, d, cpu_mask, force);
	if (gdd->nr_out_irq > 0) {
		parent_chip = irq_get_chip(gdd->virqs[0]);
		parent_data = irq_get_irq_data(gdd->virqs[0]);
	}

	if (!parent_chip || !parent_chip->irq_set_affinity || !parent_data) {
		dev_err(gdd->dev, "NULL parent_chip %pK or irq_set_affinity, or 0 output irqs\n",
				 parent_chip);
		return -EINVAL;
	}

	ret = parent_chip->irq_set_affinity(parent_data, cpu_mask, force);
	if (ret < IRQ_SET_MASK_OK) {
		dev_err(gdd->dev, "parent_chip->irq_set_affinity returned with %d\n", ret);
		return ret;
	}

	/* If parent is GIA, downstream propagation has already happenned. Skip re-doing it.*/
	if (parent_chip == &gia_irq_chip)
		goto done_here;
	/*
	 * This will be a process context call, and only the SW structures getting impacted. Should
	 * be okay to use spinlock here.
	 */
	gia_lock_process_context(&gdd->lock, &flags);
	gia_irq_update_affinity_downstream(gdd, irq_data_get_effective_affinity_mask(parent_data));
	gia_unlock_process_context(&gdd->lock, &flags);

done_here:
	return IRQ_SET_MASK_OK_DONE;
}

static struct irq_chip gia_irq_chip = {
	.name = "GIA",
	.irq_mask = gia_irq_mask,
	.irq_unmask = gia_irq_unmask,
	.irq_ack = gia_irq_ack,
	.irq_eoi = gia_irq_eoi,
	.irq_get_irqchip_state  = gia_irq_get_irqchip_state,
	.irq_bus_lock = gia_irq_bus_lock,
	.irq_bus_sync_unlock = gia_irq_bus_sync_unlock,
	.irq_set_affinity = gia_irq_set_affinity,
	.irq_set_wake = gia_irq_set_wake,
};

static int gia_domain_map(struct irq_domain *irq_domain, unsigned int virq, irq_hw_number_t hwirq)
{
	struct gia_device_data *gdd = irq_domain->host_data;
	struct gia_irq_latency *gil;

	trace_gia_domain_map(gdd, virq, hwirq);

	if (!gdd->type_data->edge_detection)
		irq_set_status_flags(virq, IRQ_LEVEL);

	irq_set_chip_and_handler_name(virq, &gia_irq_chip, handle_fasteoi_irq,
				      gdd->dev->of_node->full_name);

	/*
	 * Telemetry allocations happen just one per line, even if the line is shared between
	 * multiple clients. And, this is not error case.
	 */
	if (gdd->telemetry.irq_latencies[hwirq])
		return 0;

	gil = devm_kzalloc(gdd->dev, sizeof(*gil), GFP_KERNEL);
	if (!gil)
		return -ENOMEM;

	gil->hwirq = hwirq;
	gil->sw_wait_time_min = INT_MAX;
	gil->handler_time_min = INT_MAX;
	gdd->telemetry.irq_latencies[hwirq] = gil;

	if (kobject_init_and_add(&gil->irq_kobj,
				 &per_irq_ktype, &gdd->gia_kobj, "hwirq_%03u", (u32)hwirq)) {
		dev_err(gdd->dev, "Failed to create sysfs folder for hwirq %u\n", (u32)hwirq);
		return -EIO;
	}

	return 0;
}

static const struct irq_domain_ops gia_irq_domain_ops = {
	.map = gia_domain_map,
	.xlate = irq_domain_xlate_onecell,
};

static void gia_restore_state(struct gia_device_data *gdd)
{
	unsigned long flags;
	u32 imr;

	trace_gia_restore_state(gdd);
	gia_lock_process_context(&gdd->lock, &flags);
	for (int i = 0; i < gdd->nr_irq_chips; i++) {
		writel_relaxed(~0x0, gia_reg_addr(gdd, gdd->type_data->enable_offset, i));
		imr = gdd->mask_cache[i];
		trace_gia_cache_update(gdd, i, imr, false);
		writel_relaxed(imr, gia_reg_addr(gdd, gdd->type_data->mask_offset, i));
	}
	gia_unlock_process_context(&gdd->lock, &flags);
}

#ifdef CONFIG_PM
static void gia_log_pending_irqs(struct gia_device_data *gdd, bool suspend)
{
	unsigned long flags;
	void __iomem *isr_addr;
	u32 isr, imr, pending;

	gia_lock_process_context(&gdd->lock, &flags);
	for (int i = 0; i < gdd->nr_irq_chips; i++) {
		isr_addr = gia_reg_addr(gdd, gdd->type_data->status_offset, i);
		isr = readl_relaxed(isr_addr);
		if (gdd->type_data->check_overflow_reg) {
			isr_addr = gia_reg_addr(gdd, gdd->type_data->status_overflow_offset, i);
			isr |= readl_relaxed(isr_addr);
		}

		imr = gdd->mask_cache[i];

		/*
		 * Log only relevant irqs. Filter out ones who are actually disabled by their
		 * clients. That's definitely one way for clients to disable their irqs. And also,
		 * filter out wake up irqs. They indeed are legit outliers.
		 */
		pending = (isr & imr) & ~(gdd->wake_cache[i]);

		if (!pending)
			continue;

		/*
		 * Logs printed here do not necessarily suggests an underlying issue. It varies
		 * case to case. Some clients okay to drop their IRQ and GIAs with wake up IRQs
		 * may still serve some interrupts, if clients have not unmasked. So, they won't be
		 * dropped.
		 */
		if (suspend) {
			trace_gia_pending_irq_error(gdd, i, pending,
						    "IRQs may be dropped in suspend!!");
			dev_warn(gdd->dev,
				 "chip %d, pending irq_status %#x at suspend may be dropped\n",
				 i, pending);
		} else {
			trace_gia_pending_irq_error(gdd, i, pending,
						    "Unexpected pending IRQs at resume!!");
			dev_warn(gdd->dev,
				 "chip %d, pending irq_status %#x before resume is unexpected\n",
				 i, pending);
		}
	}
	gia_unlock_process_context(&gdd->lock, &flags);
}

static int gia_runtime_suspend(struct device *dev)
{
	struct gia_device_data *gdd = dev_get_drvdata(dev);

	trace_gia_runtime_suspend(gdd);

	/*
	 * GIAs without power domains should not be treated on their RPM suspend case. Historically,
	 * they have been RPM disabled, and used to work. Now, they are RPM enabled. So, we will
	 * observe their suspend/resume hooks invoked. But to maintain legacy behavior, better to
	 * bail out early.
	 */
	if (!gdd->dev->pm_domain)
		return 0;

	/*
	 * Make sure we don't receive any interrupts while we're runtime suspended. When we're
	 * runtime suspended our power domain might be turned off and we can't read registers.
	 *
	 * NOTES:
	 * - If we have any wakeup IRQs then it _must_ be safe to still get interrupts, so don't
	 *   disable them in this case.
	 * - We can safely look at `has_wake_irqs` without any locking and also guarantee that the
	 *   value of `has_wake_irqs` will stay the same for gia_runtime_resume() (so we don't get
	 *   unbalanced). Why? Before the core ever updates things like wake masks, it should lock
	 *   the bus. Locking the bus forces us to stay runtime resumed. So if we are suspended then
	 *   the bus is unlocked and values can't change.
	 * - Well, in N:N aggr cases, we should ideally disable non-wake up out-going IRQs. But, not
	 *   blocking that one case here because we understand that underlying power is ON as far
	 *   as we have at least one wake up source.
	 */
	if (!gdd->has_wake_irqs) {
		for (int i = 0; i < gdd->nr_out_irq; i++)
			/*
			 * We can just wait for ACTIVE interrupts today. No upstream method to wait
			 * on, PENDING but not yet ACTIVE interrupts. This helps clients who have no
			 * means to know if their IRQ is raised or not, and hence can't wait for
			 * it to be handled before going RPM SUSPENDED. We'd drop them today.
			 */
			disable_irq(gdd->virqs[i]);
	}

	/*
	 * Pending IRQs at this time may be dropped. Idea is to be explicit about it. If clients
	 * really care not dropping their IRQs, they may wait to get all their pending IRQs handled
	 * and then go to RPM SUSPENDED state.
	 */
	gia_log_pending_irqs(gdd, true);

	return 0;
}

static int gia_runtime_resume(struct device *dev)
{
	struct gia_device_data *gdd = dev_get_drvdata(dev);

	trace_gia_runtime_resume(gdd);

	/*
	 * On P26, some GIAs don't define power domain, and go off in system suspend. They need
	 * their GIA IMR restoration.
	 */
	gia_restore_state(gdd);

	/*
	 * GIAs without power domain don't disable their interrutps in suspend, so don't re-enable
	 * them in resume too.
	 */
	if (!gdd->dev->pm_domain)
		return 0;

	/*
	 * Any pending IRQ at this time, means clients raised IRQs while they were RPM
	 * SUSPENDED. Something, clients need to fix, ideally.
	 */
	gia_log_pending_irqs(gdd, false);

	if (!gdd->has_wake_irqs) {
		for (int i = 0; i < gdd->nr_out_irq; i++)
			enable_irq(gdd->virqs[i]);
	}

	return 0;
}

const struct dev_pm_ops gia_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(gia_runtime_suspend, gia_runtime_resume, NULL)
};

static void gia_setup_pm_ops(struct gia_device_data *gdd)
{
	gdd->dev->driver->pm = &gia_pm_ops;
}
#else
static void gia_setup_pm_ops(struct gia_device_data *gdd)
{}
#endif

static void gia_pm_init(struct gia_device_data *gdd)
{
	gia_setup_pm_ops(gdd);
	pm_runtime_use_autosuspend(gdd->dev);
	pm_runtime_set_autosuspend_delay(gdd->dev, GIA_AUTOSUSPEND_DELAY_MS);
	pm_runtime_set_active(gdd->dev);
	devm_pm_runtime_enable(gdd->dev);
	pm_runtime_mark_last_busy(gdd->dev);
}

/**
 * gia_set_clear_trigger_reg - Manually set a register bit on a specified GIA line
 *
 * @pdev: A pointer to the platform_device struct representing the GIA device
 *
 * @hwirq: The bit of the register to set. Corresponds to the interrupt hardware line
 *
 * @set: true to set bit to 1, false to set bit to 0
 *
 * Description:
 * This function sets bits in the trigger register for a given GIA line, to simulate a device
 * generating an interrupt. It is designed for the following purposes:
 *
 * - Device Validation and Testing: Verifying the correct functionality of the GIA device, its
 *   configuration, and associated software.
 * - Interrupt Handler Testing:  Simulating interrupts to test interrupt handler routines.
 *
 * Behavior:
 * The function directly sets the corresponding bit in the GIA's Interrupt Trigger Register (ITR)
 * This mimics the behavior of a downstream device on that line signaling an interrupt
 *
 * Power must be explicitly turned on before calling this API.
 * This is not intended for use in normal system operation, but as a testing tool.
 *
 * Return 0 on success, or a negative error code on failure.
 */
int gia_set_clear_trigger_reg(struct platform_device *pdev, u32 hwirq, bool set)
{
	void __iomem *reg_addr;
	unsigned long flags;
	u32 reg;
	struct gia_device_data *gdd = platform_get_drvdata(pdev);

	if (!gdd) {
		dev_err(&pdev->dev, "no gia device data associated with this pdev\n");
		return -EINVAL;
	}

	if (hwirq > IRQS_PER_REG * gdd->nr_irq_chips) {
		dev_err(&pdev->dev, "hwirq %d too large. Expected less than %d\n",
			hwirq, IRQS_PER_REG * gdd->nr_irq_chips);
		return -EINVAL;
	}

	trace_gia_set_clear_trigger_reg(gdd, hwirq, set);
	reg_addr = gia_reg_addr(gdd, gdd->type_data->trigger_offset, hwirq / IRQS_PER_REG);

	gia_lock_process_context(&gdd->lock, &flags);
	reg = readl_relaxed(reg_addr);

	if (set)
		reg |= 1 << (hwirq % IRQS_PER_REG);
	else
		reg &= ~(1 << (hwirq % IRQS_PER_REG));

	writel_relaxed(reg, reg_addr);
	gia_unlock_process_context(&gdd->lock, &flags);

	return 0;
}
EXPORT_SYMBOL_GPL(gia_set_clear_trigger_reg);

/**
 * gia_init - Initialize the GIA device.
 *
 * @gdd: A pointer to the gia_device_data struct containing GIA device specific information. This
 *   struct holds all possible GIA features, info, offset etc which is required by this lib.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int gia_init(struct gia_device_data *gdd)
{
	struct device_node *node;
	int ret;

	trace_gia_init(gdd);
	node = gdd->dev->of_node;

	/*
	 * Why *raw_* version of the spinlock? Because, that makes this lock guaranteed to be a
	 * spinning lock even if the kernel has PREEPT_RT enabled.
	 */
	raw_spin_lock_init(&gdd->lock);

	gia_pm_init(gdd);

	/* Allocate memory for register cache (SW copy of registers) */
	gdd->mask_cache = devm_kzalloc(gdd->dev, sizeof(u32) * (gdd->nr_irq_chips), GFP_KERNEL);
	if (!gdd->mask_cache)
		return -ENOMEM;

	gdd->wake_cache = devm_kzalloc(gdd->dev, sizeof(u32) * (gdd->nr_irq_chips), GFP_KERNEL);
	if (!gdd->wake_cache)
		return -ENOMEM;

	/* Allocate memory for telemetry */
	if (gia_telemetry_memory_alloc(gdd))
		return -ENOMEM;

	gia_telemetry_reset(gdd);

	gia_restore_state(gdd);

	ret = gia_sysfs_create_per_gia_dir(gdd);
	if (ret)
		return ret;

	/*
	 * Create IRQ domain for this interrupt controller. This domain allows this GIA's consumers
	 * to claim particular hwirq and attach their handler. Domain also hosts chip specific
	 * low-level function callback to perform enable, mask, clear operations.
	 *
	 * Pass max number of IRQs this domain can handle, pointer to domain ops and data, that we
	 * want to get into every ops.
	 */
	gdd->irq_domain = irq_domain_create_linear(dev_fwnode(gdd->dev),
						   gdd->nr_irq_chips * IRQS_PER_REG,
						   &gia_irq_domain_ops, gdd);

	if (unlikely(!gdd->irq_domain)) {
		dev_err(gdd->dev, "Cannot get IRQ domain\n");
		return -EINVAL;
	}

	/*
	 * Each GIA node sends 1 (or more) interrupts out to upstream interrupt controller. For
	 * each of the out-going interrupt lines, it requires to register a handler so that it can
	 * work on the source of the interrupt.
	 *
	 * For registering the handler, it requires Linux virq. So, GIA lib users are expected to
	 * map the interrupt and get virq (more than 1 virqs if it is non-aggr GIA) info passed
	 * through gdd.
	 *
	 * Note:
	 * Non-aggr type GIA sends more than 1 interrupt line out. If these lines are going directly
	 * to CPU (say GIC), it *may* invoke parallel handler execution on the same GIA. In such
	 * scenarios, we would want each bit to be serviced only when its destined handler invokes.
	 * If a single handler services all the interrupts, accounting might go wrong. Currently,
	 * there is no such non-aggr available.
	 *
	 * The only available non-aggr GIAs are getting aggregated at some point before going to
	 * CPU. And that makes their multiple sources gets similar serialized treatement as any
	 * other aggregating GIAs.
	 */
	for (int i = 0; i < gdd->nr_out_irq; i++) {
		/*
		 * Default, disable_irq() does IRQ masking at SW layer. So, when an IRQ fires next,
		 * IRQ handling flow would reach the upstream interrupt controller handler and
		 * interrupt core would mask calling the device handler (GIA handler in this case).
		 * We prefer "masking" at HW layer. That guarantees, IRQ won't propagate till CPU.
		 * This is now a hard requirement, because we exactly want this behavior in GIA's
		 * runtime suspend callback. IRQ_DISABLE_UNLAZY flag achieves that.
		 */
		irq_set_status_flags(gdd->virqs[i], IRQ_DISABLE_UNLAZY);
		irq_set_chained_handler_and_data(gdd->virqs[i], gia_irq_handler_chained, gdd);
	}

	/* Attach gdd in global linked list */
	mutex_lock(&gia_global_mutex);
	list_add_tail(&gdd->list, &gia_device_list);
	mutex_unlock(&gia_global_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(gia_init);

/**
 * gia_exit - Remove the GIA device.
 *
 * @gdd: A pointer to the gia_device_data struct
 */
void gia_exit(struct gia_device_data *gdd)
{
	trace_gia_exit(gdd);
	for (int i = 0; i < gdd->nr_out_irq; i++)
		irq_set_chained_handler_and_data(gdd->virqs[i], NULL, NULL);

	irq_domain_remove(gdd->irq_domain);
}
EXPORT_SYMBOL_GPL(gia_exit);

static const char * const gia_trace_events[] __initconst = {
	"gia_get_power",
	"gia_put_power",
	"gia_irq_handler_entry",
	"gia_irq_handler_exit",
	"gia_irq_mask",
	"gia_irq_unmask",
	"gia_irq_bus_lock",
	"gia_irq_bus_sync_unlock",
	"gia_irq_ack",
	"gia_irq_eoi",
	"gia_irq_update_effective_affinity",
	"gia_irq_set_affinity",
	"gia_domain_map",
	"gia_restore_state",
	"gia_cache_update",
	"gia_runtime_suspend",
	"gia_runtime_resume",
	"gia_set_clear_trigger_reg",
	"gia_init",
	"gia_exit",
	"gia_error",
	"gia_pending_irq_error",
};

static struct trace_array * __init gia_trace_init(void)
{
	struct trace_array *trace_instance;

	trace_instance = trace_array_get_by_name("irq_gia_google", "irq_gia");
	if (!trace_instance) {
		pr_err("Interrupts trace instance creation/retrieve did not succeed\n");
		return NULL;
	}

	for (int i = 0; i < ARRAY_SIZE(gia_trace_events); i++)
		trace_array_set_clr_event(trace_instance, NULL, gia_trace_events[i], true);

	return trace_instance;
}

static int __init gia_lib_init(void)
{
	gia_trace_init();
	gia_sysfs_init();

	return 0;
}
module_init(gia_lib_init);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Generic Interrupt Aggregator Library");
MODULE_LICENSE("GPL");
