// SPDX-License-Identifier: GPL-2.0-only

#include <linux/platform_device.h>
#include <linux/trace.h>
#include <linux/trace_events.h>

#include "common.h"

static const char * const pinctrl_trace_events[] = {
	"google_gpio_direction_input",
	"google_gpio_direction_output",
	"google_gpio_get",
	"google_gpio_get_direction",
	"google_gpio_irq_affinity_notifier_callback",
	"google_gpio_irq_disable",
	"google_gpio_irq_enable",
	"google_gpio_irq_handler",
	"google_gpio_irq_mask",
	"google_gpio_irq_set_affinity",
	"google_gpio_irq_unmask",
	"google_gpio_irq_startup",
	"google_gpio_irq_shutdown",
	"google_gpio_set",
	"google_pinconf_group_get",
	"google_pinconf_group_set",
	"google_pinctrl_suspend",
	"google_pinctrl_resume",
	"google_pinctrl_runtime_suspend",
	"google_pinctrl_runtime_resume",
	"google_pinmux_set_mux",
	"generic_handle_domain_irq",
	"google_pinctrl_detach_power_domain",
	"google_pinconf_group_set_enter",
	"google_gpio_irq_bus_lock",
	"google_gpio_irq_bus_unlock",
	"google_gpio_irq_set_type",
};

void google_pinctrl_trace_init(struct platform_device *pdev)
{
	struct trace_array *trace_instance;

	trace_instance = trace_array_get_by_name("pinctrl_google", "pinctrl");
	if (!trace_instance)
		dev_err(&pdev->dev, "Pinctrl trace instance creation/retrieve did not succeed\n");

	for (int i = 0; i < ARRAY_SIZE(pinctrl_trace_events); i++)
		trace_array_set_clr_event(trace_instance, NULL, pinctrl_trace_events[i], true);

	trace_array_put(trace_instance);
}
