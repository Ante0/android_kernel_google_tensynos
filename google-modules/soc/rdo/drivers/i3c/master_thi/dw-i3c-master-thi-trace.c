// SPDX-License-Identifier: GPL-2.0-only

#include <linux/platform_device.h>
#include <linux/trace.h>
#include <linux/trace_events.h>

#include "dw-i3c-master-thi-trace.h"

static const char * const dw_i3c_thi_trace_events[] = {
	"dw_i3c_thi_ccc",
	"dw_i3c_thi_daa",
	"dw_i3c_thi_xfer",
};

void dw_i3c_thi_trace_init(struct platform_device *dev)
{
	struct trace_array *trace_instance;
	int i;

	trace_instance = trace_array_get_by_name("i3c_master_thi", "dw_i3c_master_thi");
	if (!trace_instance)
		return;

	for (i = 0; i < ARRAY_SIZE(dw_i3c_thi_trace_events); i++) {
		trace_array_set_clr_event(trace_instance, NULL,
					  dw_i3c_thi_trace_events[i], true);
	}

	trace_array_put(trace_instance);
}
