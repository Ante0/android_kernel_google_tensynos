// SPDX-License-Identifier: GPL-2.0-only

#include <linux/platform_device.h>
#include <linux/trace.h>
#include <linux/trace_events.h>

#include "dw-i3c-master-trace.h"

static const char * const i3c_master_trace_events[] = {
	"dw_i3c_xfers_start",
	"dw_i3c_xfer_queue",
	"dw_i3c_xfer_dequeue",
	"dw_i3c_xfer_done",
	"dw_i3c_irq",
	"dw_i3c_push_cmd",
	"dw_i3c_pull_resp",
	"dw_i3c_ibi",
	"dw_i3c_suspend",
	"dw_i3c_resume",
	"dw_i3c_runtime_suspend",
	"dw_i3c_runtime_resume",
};

void i3c_master_trace_init(struct platform_device *pdev)
{
	struct trace_array *trace_instance;
	int i;

	trace_instance = trace_array_get_by_name("i3c_master", "i3c_dw");
	if (!trace_instance)
		return;

	for (i = 0; i < ARRAY_SIZE(i3c_master_trace_events); i++)
		trace_array_set_clr_event(trace_instance, NULL, i3c_master_trace_events[i], true);

	trace_array_put(trace_instance);
}
