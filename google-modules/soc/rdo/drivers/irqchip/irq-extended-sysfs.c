// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

static int irqs_populated;
static void irq_sysfs_populate_attrs(void);

static ssize_t hw_state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct irq_desc *desc;
	int ret_masked, ret_active, ret_pending;
	bool masked, active, pending;
	ssize_t ret;

	desc = container_of(kobj, struct irq_desc, kobj);
	if (!desc) {
		pr_warn("Invalid desc from hw_state\n");
		return -ENOENT;
	}

	ret_pending = irq_get_irqchip_state(desc->irq_data.irq, IRQCHIP_STATE_PENDING, &pending);
	ret_active = irq_get_irqchip_state(desc->irq_data.irq, IRQCHIP_STATE_ACTIVE, &active);
	ret_masked = irq_get_irqchip_state(desc->irq_data.irq, IRQCHIP_STATE_MASKED, &masked);

	ret = sysfs_emit(buf, "%s, %s, %s\n",
			 ret_masked ? "NA" : (masked ? "MASKED" : "UNMASKED"),
			 ret_active ? "NA" : (active ? "ACTIVE" : "INACTIVE"),
			 ret_pending ? "NA" : (pending ? "PENDING" : "CLEARED"));

	return ret;
}

static ssize_t sw_state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct irq_desc *desc;
	unsigned long flags;
	unsigned int depth = 0;
	ssize_t ret;

	desc = container_of(kobj, struct irq_desc, kobj);
	if (!desc) {
		pr_warn("Invalid desc from sw_state\n");
		return -ENOENT;
	}

	raw_spin_lock_irqsave(&desc->lock, flags);
	depth = desc->depth;
	raw_spin_unlock_irqrestore(&desc->lock, flags);

	ret = sysfs_emit(buf, "%s (depth %d)\n", depth == 0 ? "ENABLED" : "DISABLED",
			 depth);
	return ret;
}

static ssize_t repopulate_irq_extended_store(struct kobject *kobj, struct kobj_attribute *attr,
					     const char *buf, size_t count)
{
	irq_sysfs_populate_attrs();

	return count;
}

static struct kobj_attribute hw_state_attr		  = __ATTR_RO(hw_state);
static struct kobj_attribute sw_state_attr		  = __ATTR_RO(sw_state);
static struct kobj_attribute repopulate_irq_extended_attr = __ATTR_WO(repopulate_irq_extended);

static void irq_sysfs_populate_attrs(void)
{
	struct irq_desc *desc;
	int irq;
	int ret;

	for_each_irq_desc(irq, desc) {
		/* Skip attempting to create files for IRQs which already got this done, once. */
		if (irq <= irqs_populated)
			continue;

		ret = sysfs_create_file(&desc->kobj, &hw_state_attr.attr);
		if (ret && ret != EEXIST)
			pr_err("Failed to create hw_state file for irq %d\n", irq);

		ret = sysfs_create_file(&desc->kobj, &sw_state_attr.attr);
		if (ret && ret != EEXIST)
			pr_err("Failed to create sw_state file for irq %d\n", irq);

		irqs_populated = irq;
	}
}

static void irq_sysfs_depopulate_attrs(void)
{
	struct irq_desc *desc;
	int irq;

	for_each_irq_desc(irq, desc) {
		sysfs_remove_file(&desc->kobj, &hw_state_attr.attr);
		sysfs_remove_file(&desc->kobj, &sw_state_attr.attr);
	}
}

static int __init irq_sysfs_extended_init(void)
{
	int ret;

	ret = sysfs_create_file(kernel_kobj, &repopulate_irq_extended_attr.attr);
	irq_sysfs_populate_attrs();

	return ret;
}
module_init(irq_sysfs_extended_init);

static void __exit irq_sysfs_extended_exit(void)
{
	sysfs_remove_file(kernel_kobj, &repopulate_irq_extended_attr.attr);
	irq_sysfs_depopulate_attrs();
}
module_exit(irq_sysfs_extended_exit);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("IRQ sysfs extended module");
MODULE_LICENSE("GPL");
