/* SPDX-License-Identifier: GPL-2.0-only */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM irq_gia

#if !defined(_TRACE_IRQ_GIA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IRQ_GIA_H

#include <linux/trace.h>
#include <linux/tracepoint.h>
#include <linux/api-compat.h>

#include "irq-gia-lib.h"

#define GIA_DEV_NAME(gdd) (gdd->dev->of_node->full_name)

TRACE_EVENT(gia_error,
	TP_PROTO(struct gia_device_data *gdd, const char *msg),
	TP_ARGS(gdd, msg),
	TP_STRUCT__entry(
		__string(dev_name, GIA_DEV_NAME(gdd))
		__string(msg, msg)
	),
	TP_fast_assign(
		assign_str_wrp(dev_name, GIA_DEV_NAME(gdd));
		assign_str_wrp(msg, msg);
	),
	TP_printk("dev_name=%s, %s", __get_str(dev_name), __get_str(msg))
);

TRACE_EVENT(gia_pending_irq_error,
	TP_PROTO(struct gia_device_data *gdd, int chip_number, u32 pending, const char *msg),
	TP_ARGS(gdd, chip_number, pending, msg),
	TP_STRUCT__entry(
		__string(dev_name, GIA_DEV_NAME(gdd))
		__field(int, chip_number)
		__field(u32, pending)
		__string(msg, msg)
	),
	TP_fast_assign(
		assign_str_wrp(dev_name, GIA_DEV_NAME(gdd));
		__entry->chip_number = chip_number;
		__entry->pending = pending;
		assign_str_wrp(msg, msg);
	),
	TP_printk("dev_name=%s, chip_number=%d, pending=0x%x, %s",
		  __get_str(dev_name), __entry->chip_number, __entry->pending, __get_str(msg))
);

DECLARE_EVENT_CLASS(gia_irq_events,
	TP_PROTO(struct gia_device_data *gdd, struct irq_data *d),
	TP_ARGS(gdd, d),
	TP_STRUCT__entry(
		__string(dev_name, GIA_DEV_NAME(gdd))
		__field(unsigned int, virq)
		__field(unsigned int, hwirq)
	),
	TP_fast_assign(
		assign_str_wrp(dev_name, GIA_DEV_NAME(gdd));
		__entry->virq = d->irq;
		__entry->hwirq = d->hwirq;
	),
	TP_printk("dev_name=%s, hwirq=%u, virq=%u",
		  __get_str(dev_name), __entry->hwirq, __entry->virq)
);

DEFINE_EVENT(gia_irq_events, gia_irq_handler_entry,
	TP_PROTO(struct gia_device_data *gdd, struct irq_data *d),
	TP_ARGS(gdd, d)
);

DEFINE_EVENT(gia_irq_events, gia_irq_handler_exit,
	TP_PROTO(struct gia_device_data *gdd, struct irq_data *d),
	TP_ARGS(gdd, d)
);

DEFINE_EVENT(gia_irq_events, gia_irq_mask,
	TP_PROTO(struct gia_device_data *gdd, struct irq_data *d),
	TP_ARGS(gdd, d)
);

DEFINE_EVENT(gia_irq_events, gia_irq_unmask,
	TP_PROTO(struct gia_device_data *gdd, struct irq_data *d),
	TP_ARGS(gdd, d)
);

DEFINE_EVENT(gia_irq_events, gia_irq_bus_lock,
	TP_PROTO(struct gia_device_data *gdd, struct irq_data *d),
	TP_ARGS(gdd, d)
);

DEFINE_EVENT(gia_irq_events, gia_irq_bus_sync_unlock,
	TP_PROTO(struct gia_device_data *gdd, struct irq_data *d),
	TP_ARGS(gdd, d)
);

DEFINE_EVENT(gia_irq_events, gia_irq_ack,
	TP_PROTO(struct gia_device_data *gdd, struct irq_data *d),
	TP_ARGS(gdd, d)
);

DEFINE_EVENT(gia_irq_events, gia_irq_eoi,
	TP_PROTO(struct gia_device_data *gdd, struct irq_data *d),
	TP_ARGS(gdd, d)
);

DECLARE_EVENT_CLASS(gia_device_events,
	TP_PROTO(struct gia_device_data *gdd),
	TP_ARGS(gdd),
	TP_STRUCT__entry(
		__string(dev_name, GIA_DEV_NAME(gdd))
	),
	TP_fast_assign(
		assign_str_wrp(dev_name, GIA_DEV_NAME(gdd));
	),
	TP_printk("dev_name=%s", __get_str(dev_name))
);

DEFINE_EVENT(gia_device_events, gia_restore_state,
	TP_PROTO(struct gia_device_data *gdd),
	TP_ARGS(gdd)
);

DEFINE_EVENT(gia_device_events, gia_runtime_suspend,
	TP_PROTO(struct gia_device_data *gdd),
	TP_ARGS(gdd)
);

DEFINE_EVENT(gia_device_events, gia_runtime_resume,
	TP_PROTO(struct gia_device_data *gdd),
	TP_ARGS(gdd)
);

DEFINE_EVENT(gia_device_events, gia_put_power,
	TP_PROTO(struct gia_device_data *gdd),
	TP_ARGS(gdd)
);

DEFINE_EVENT(gia_device_events, gia_init,
	TP_PROTO(struct gia_device_data *gdd),
	TP_ARGS(gdd)
);

DEFINE_EVENT(gia_device_events, gia_exit,
	TP_PROTO(struct gia_device_data *gdd),
	TP_ARGS(gdd)
);

TRACE_EVENT(gia_irq_set_wake,
	TP_PROTO(struct gia_device_data *gdd, struct irq_data *d, unsigned int set),
	TP_ARGS(gdd, d, set),
	TP_STRUCT__entry(
		__string(dev_name, GIA_DEV_NAME(gdd))
		__field(unsigned int, virq)
		__field(unsigned int, hwirq)
		__field(unsigned int, set)
	),
	TP_fast_assign(
		assign_str_wrp(dev_name, GIA_DEV_NAME(gdd));
		__entry->virq = d->irq;
		__entry->hwirq = d->hwirq;
		__entry->set = set;
	),
	TP_printk("dev_name=%s, hwirq=%u, virq=%u, set=%u",
		  __get_str(dev_name), __entry->hwirq, __entry->virq, __entry->set)
);

TRACE_EVENT(gia_get_power,
	TP_PROTO(struct gia_device_data *gdd),
	TP_ARGS(gdd),
	TP_STRUCT__entry(
		__string(dev_name, GIA_DEV_NAME(gdd))
	),
	TP_fast_assign(
		assign_str_wrp(dev_name, GIA_DEV_NAME(gdd));
	),
	TP_printk("dev_name=%s", __get_str(dev_name))
);

TRACE_EVENT(gia_irq_update_effective_affinity,
	TP_PROTO(struct gia_device_data *gdd, unsigned int virq, const struct cpumask *cpumask),
	TP_ARGS(gdd, virq, cpumask),
	TP_STRUCT__entry(
		__string(dev_name, GIA_DEV_NAME(gdd))
		__field(unsigned int, virq)
		__field(unsigned long, cpu_mask)
	),
	TP_fast_assign(
		assign_str_wrp(dev_name, GIA_DEV_NAME(gdd));
		__entry->virq = virq;
		__entry->cpu_mask = *cpumask_bits(cpumask);
	),
	TP_printk("dev_name=%s, virq=%u, effective_affinity=%#lx",
		  __get_str(dev_name), __entry->virq, __entry->cpu_mask)
);

TRACE_EVENT(gia_irq_set_affinity,
	TP_PROTO(struct gia_device_data *gdd, struct irq_data *d, const struct cpumask *cpu_mask,
		 bool force),
	TP_ARGS(gdd, d, cpu_mask, force),
	TP_STRUCT__entry(
		__string(dev_name, GIA_DEV_NAME(gdd))
		__field(unsigned int, virq)
		__field(unsigned long, cpu_mask)
		__field(bool, force)
	),
	TP_fast_assign(
		assign_str_wrp(dev_name, GIA_DEV_NAME(gdd));
		__entry->virq = d->irq;
		__entry->cpu_mask = *cpumask_bits(cpu_mask);
		__entry->force = force;
	),
	TP_printk("dev_name=%s, virq=%u, cpumask=%#lx, force=%d",
		  __get_str(dev_name), __entry->virq, __entry->cpu_mask, __entry->force)
);

TRACE_EVENT(gia_domain_map,
	TP_PROTO(struct gia_device_data *gdd, unsigned int virq, irq_hw_number_t hwirq),
	TP_ARGS(gdd, virq, hwirq),
	TP_STRUCT__entry(
		__string(dev_name, GIA_DEV_NAME(gdd))
		__field(unsigned int, virq)
		__field(irq_hw_number_t, hwirq)
	),
	TP_fast_assign(
		assign_str_wrp(dev_name, GIA_DEV_NAME(gdd));
		__entry->virq = virq;
		__entry->hwirq = hwirq;
	),
	TP_printk("dev_name=%s, virq=%u, hwirq=%lu",
		  __get_str(dev_name), __entry->virq, __entry->hwirq)
);

TRACE_EVENT(gia_set_clear_trigger_reg,
	TP_PROTO(struct gia_device_data *gdd, irq_hw_number_t hwirq, bool set),
	TP_ARGS(gdd, hwirq, set),
	TP_STRUCT__entry(
		__string(dev_name, GIA_DEV_NAME(gdd))
		__field(irq_hw_number_t, hwirq)
		__field(bool, set)
	),
	TP_fast_assign(
		assign_str_wrp(dev_name, GIA_DEV_NAME(gdd));
		__entry->hwirq = hwirq;
		__entry->set = set;
	),
	TP_printk("dev_name=%s, hwirq=%lu, set=%d",
		  __get_str(dev_name), __entry->hwirq, __entry->set)
);

TRACE_EVENT(gia_cache_update,
	TP_PROTO(struct gia_device_data *gdd, int chip_number, u32 imr, bool has_wake_irqs),
	TP_ARGS(gdd, chip_number, imr, has_wake_irqs),
	TP_STRUCT__entry(
		__string(dev_name, GIA_DEV_NAME(gdd))
		__field(int, chip_number)
		__field(u32, imr)
		__field(bool, has_wake_irqs)
	),
	TP_fast_assign(
		assign_str_wrp(dev_name, GIA_DEV_NAME(gdd));
		__entry->chip_number = chip_number;
		__entry->imr = imr;
		__entry->has_wake_irqs = has_wake_irqs;
	),
	TP_printk("dev_name=%s, chip_number=%d, imr=0x%x, has_wake_irqs=%d",
		  __get_str(dev_name), __entry->chip_number, __entry->imr, __entry->has_wake_irqs)
);

#endif /* _TRACE_IRQ_GIA_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/irqchip/

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE irq-gia-lib-trace

#include <trace/define_trace.h>
