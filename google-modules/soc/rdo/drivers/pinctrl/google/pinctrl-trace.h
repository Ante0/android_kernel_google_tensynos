/* SPDX-License-Identifier: GPL-2.0-only */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM pinctrl

#if !defined(_TRACE_PINCTRL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PINCTRL_H

#include <linux/trace.h>
#include <linux/tracepoint.h>
#include "common.h"

#define MAX_LABEL_SIZE 32

TRACE_EVENT(google_gpio_irq_handler,
	TP_PROTO(struct google_pinctrl *gctl, u32 irq, u32 hwirq, u32 pad,
		 bool suspended, bool rpm_suspended),
	TP_ARGS(gctl, irq, hwirq, pad, suspended, rpm_suspended),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u32, irq)
		__field(u32, hwirq)
		__field(u32, pad)
		__field(bool, suspended)
		__field(bool, rpm_suspended)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
		__entry->pad = pad;
		__entry->irq = irq;
		__entry->hwirq = hwirq;
		__entry->suspended = suspended;
		__entry->rpm_suspended = rpm_suspended;
	),
	TP_printk("%s: irq %u, hwirq %u, pad %u, suspended %d, rpm_suspended %d",
		__entry->label,
		__entry->irq, __entry->hwirq,
		__entry->pad, __entry->suspended, __entry->rpm_suspended)
);

TRACE_EVENT(google_gpio_irq_set_affinity,
	TP_PROTO(struct google_pinctrl *gctl, u32 cpumask, u32 pad, u32 virq),
	TP_ARGS(gctl, cpumask, pad, virq),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u32, cpumask)
		__field(u32, pad)
		__field(u32, virq)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
		__entry->cpumask = cpumask;
		__entry->pad = pad;
		__entry->virq = virq;
	),
	TP_printk("%s: cpumask 0x%08x, pad %d, virq %d",
		__entry->label, __entry->cpumask, __entry->pad, __entry->virq)
);

TRACE_EVENT(google_gpio_irq_affinity_notifier_callback,
	TP_PROTO(struct google_pinctrl *gctl, u32 cpumask, u32 pad, u32 virq),
	TP_ARGS(gctl, cpumask, pad, virq),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u32, cpumask)
		__field(u32, pad)
		__field(u32, virq)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
		__entry->cpumask = cpumask;
		__entry->pad = pad;
		__entry->virq = virq;
	),
	TP_printk("%s: cpumask 0x%08x, pad %d, virq %d",
		__entry->label, __entry->cpumask, __entry->pad, __entry->virq)
);

TRACE_EVENT(google_pinconf_group_set,
	TP_PROTO(struct google_pinctrl *gctl, u32 param, u32 txdata, u32 pad),
	TP_ARGS(gctl, param, txdata, pad),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u32, param)
		__field(u32, txdata)
		__field(u32, pad)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
		__entry->param = param;
		__entry->txdata = txdata;
		__entry->pad = pad;
	),
	TP_printk("%s: param 0x%08x, txdata 0x%08x, pad %u",
		__entry->label, __entry->param, __entry->txdata, __entry->pad)
);

TRACE_EVENT(google_pinconf_group_get,
	TP_PROTO(struct google_pinctrl *gctl, u32 param, u32 pad),
	TP_ARGS(gctl, param, pad),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u32, param)
		__field(u32, pad)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
		__entry->param = param;
		__entry->pad = pad;
	),
	TP_printk("%s: param 0x%08x, pad %u",
		__entry->label, __entry->param, __entry->pad)
);

TRACE_EVENT(google_pinmux_set_mux,
	TP_PROTO(struct google_pinctrl *gctl, u32 pad, u32 func),
	TP_ARGS(gctl, pad, func),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u32, pad)
		__field(u32, func)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
		__entry->pad = pad;
		__entry->func = func;
	),
	TP_printk("%s: func %d, pad %d",
		__entry->label, __entry->func, __entry->pad)
);

TRACE_EVENT(google_gpio_irq_set_type,
	TP_PROTO(struct google_pinctrl *gctl, u32 pad, u32 irq, u32 trigger_type),
	TP_ARGS(gctl, pad, irq, trigger_type),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u32, pad)
		__field(u32, irq)
		__field(u32, trigger_type)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
		__entry->pad = pad;
		__entry->irq = irq;
		__entry->trigger_type = trigger_type;
	),
	TP_printk("%s: irq %d, pad %d, trigger_type 0x%08x",
		__entry->label,
		__entry->irq, __entry->pad, __entry->trigger_type)
);

TRACE_EVENT(google_gpio_irq_mask_update,
	TP_PROTO(struct google_pinctrl *gctl, u32 pad_idx, u32 imr_reg),
	TP_ARGS(gctl, pad_idx, imr_reg),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u32, pad_idx)
		__field(u32, imr_reg)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
		__entry->pad_idx = pad_idx;
		__entry->imr_reg = imr_reg;
	),
	TP_printk("%s: pad_idx %d, imr_reg 0x%08x",
		__entry->label, __entry->pad_idx, __entry->imr_reg)
);

TRACE_EVENT(generic_handle_domain_irq,
	TP_PROTO(struct google_pinctrl *gctl, struct irq_domain *domain, u32 pad),
	TP_ARGS(gctl, domain, pad),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(struct irq_domain*, domain)
		__field(u32, pad)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
		__entry->domain = domain;
		__entry->pad = pad;
	),
	TP_printk("%s: domain %p, pad %d",
		__entry->label, __entry->domain, __entry->pad)
);

TRACE_EVENT(google_gpio_irq_bus_lock,
	TP_PROTO(struct google_pinctrl *gctl, u32 irq),
	TP_ARGS(gctl, irq),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u32, irq)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
		__entry->irq = irq;
	),
	TP_printk("%s: irq %d", __entry->label, __entry->irq)
);

TRACE_EVENT(google_gpio_irq_bus_unlock,
	TP_PROTO(struct google_pinctrl *gctl, u32 irq),
	TP_ARGS(gctl, irq),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u32, irq)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
		__entry->irq = irq;
	),
	TP_printk("%s: irq %d", __entry->label, __entry->irq)
);

DECLARE_EVENT_CLASS(pinctrl_gpio_group_event,
	TP_PROTO(struct google_pinctrl *gctl, u32 pad, u32 value),
	TP_ARGS(gctl, pad, value),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u32, pad)
		__field(u32, value)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
		__entry->pad = pad;
		__entry->value = value;
	),
	TP_printk("%s: val %d, pad %d",
		__entry->label, __entry->value, __entry->pad)
);

DEFINE_EVENT(pinctrl_gpio_group_event, google_gpio_set,
	TP_PROTO(struct google_pinctrl *gctl, u32 pad, u32 value),
	TP_ARGS(gctl, pad, value)
);

DEFINE_EVENT(pinctrl_gpio_group_event, google_gpio_get,
	TP_PROTO(struct google_pinctrl *gctl, u32 pad, u32 value),
	TP_ARGS(gctl, pad, value)
);

DEFINE_EVENT(pinctrl_gpio_group_event, google_gpio_get_direction,
	TP_PROTO(struct google_pinctrl *gctl, u32 pad, u32 value),
	TP_ARGS(gctl, pad, value)
);

DEFINE_EVENT(pinctrl_gpio_group_event, google_gpio_direction_output,
	TP_PROTO(struct google_pinctrl *gctl, u32 pad, u32 value),
	TP_ARGS(gctl, pad, value)
);

DEFINE_EVENT(pinctrl_gpio_group_event, google_gpio_direction_input,
	TP_PROTO(struct google_pinctrl *gctl, u32 pad, u32 value),
	TP_ARGS(gctl, pad, value)
);

DECLARE_EVENT_CLASS(pinctrl_irq_group_event,
	TP_PROTO(struct google_pinctrl *gctl, u32 irq, u32 pad),
	TP_ARGS(gctl, irq, pad),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
		__field(u32, pad)
		__field(u32, irq)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
		__entry->pad = pad;
		__entry->irq = irq;
	),
	TP_printk("%s: irq %d, pad %d",
		__entry->label, __entry->irq, __entry->pad)
);

DEFINE_EVENT(pinctrl_irq_group_event, google_gpio_irq_enable,
	TP_PROTO(struct google_pinctrl *gctl, u32 irq, u32 pad),
	TP_ARGS(gctl, irq, pad)
);

DEFINE_EVENT(pinctrl_irq_group_event, google_gpio_irq_disable,
	TP_PROTO(struct google_pinctrl *gctl, u32 irq, u32 pad),
	TP_ARGS(gctl, irq, pad)
);

DEFINE_EVENT(pinctrl_irq_group_event, google_gpio_irq_mask,
	TP_PROTO(struct google_pinctrl *gctl, u32 irq, u32 pad),
	TP_ARGS(gctl, irq, pad)
);

DEFINE_EVENT(pinctrl_irq_group_event, google_gpio_irq_unmask,
	TP_PROTO(struct google_pinctrl *gctl, u32 irq, u32 pad),
	TP_ARGS(gctl, irq, pad)
);

DEFINE_EVENT(pinctrl_irq_group_event, google_gpio_irq_startup,
	TP_PROTO(struct google_pinctrl *gctl, u32 irq, u32 pad),
	TP_ARGS(gctl, irq, pad)
);

DEFINE_EVENT(pinctrl_irq_group_event, google_gpio_irq_shutdown,
	TP_PROTO(struct google_pinctrl *gctl, u32 irq, u32 pad),
	TP_ARGS(gctl, irq, pad)
);

DECLARE_EVENT_CLASS(pinctrl_power_group_event,
	TP_PROTO(struct google_pinctrl *gctl),
	TP_ARGS(gctl),
	TP_STRUCT__entry(
		__array(char, label, MAX_LABEL_SIZE)
	),
	TP_fast_assign(
		scnprintf(__entry->label, MAX_LABEL_SIZE, "%s", gctl->info->label);
	),
	TP_printk("%s", __entry->label)
);

DEFINE_EVENT(pinctrl_power_group_event, google_pinctrl_suspend,
	TP_PROTO(struct google_pinctrl *gctl),
	TP_ARGS(gctl)
);

DEFINE_EVENT(pinctrl_power_group_event, google_pinctrl_resume,
	TP_PROTO(struct google_pinctrl *gctl),
	TP_ARGS(gctl)
);

DEFINE_EVENT(pinctrl_power_group_event, google_pinctrl_runtime_suspend,
	TP_PROTO(struct google_pinctrl *gctl),
	TP_ARGS(gctl)
);

DEFINE_EVENT(pinctrl_power_group_event, google_pinctrl_runtime_resume,
	TP_PROTO(struct google_pinctrl *gctl),
	TP_ARGS(gctl)
);

DEFINE_EVENT(pinctrl_power_group_event, google_pinctrl_get_csr_pd,
	TP_PROTO(struct google_pinctrl *gctl),
	TP_ARGS(gctl)
);

DEFINE_EVENT(pinctrl_power_group_event, google_pinctrl_put_csr_pd,
	TP_PROTO(struct google_pinctrl *gctl),
	TP_ARGS(gctl)
);

DEFINE_EVENT(pinctrl_power_group_event, google_pinctrl_detach_power_domain,
	TP_PROTO(struct google_pinctrl *gctl),
	TP_ARGS(gctl)
);

DEFINE_EVENT(pinctrl_power_group_event, google_pinconf_group_set_enter,
	TP_PROTO(struct google_pinctrl *gctl),
	TP_ARGS(gctl)
);

#endif /* _TRACE_PINCTRL_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/pinctrl/google

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE pinctrl-trace

#include <trace/define_trace.h>
