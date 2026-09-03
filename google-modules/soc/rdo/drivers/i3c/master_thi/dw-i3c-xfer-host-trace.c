// SPDX-License-Identifier: GPL-2.0-only

#include <linux/platform_device.h>
#include <linux/trace.h>
#include <linux/trace_events.h>

#include "dw-i3c-xfer-host-trace.h"

static const char * const dw_i3c_xfer_trace_events[] = {
	"dw_i3c_xfer_irq",
	"dw_i3c_xfer_cmd_push",
	"dw_i3c_xfer_resp_pull",
	"dw_i3c_xfer_exec_start",
	"dw_i3c_xfer_exec_done",
	"dw_i3c_xfer_tx_data",
	"dw_i3c_xfer_rx_data",
	"dw_i3c_xfer_thld_update",
};

void dw_i3c_xfer_host_trace_init(struct platform_device *dev)
{
	struct trace_array *trace_instance;
	int i;

	trace_instance = trace_array_get_by_name("i3c_master_xfer", "dw_i3c_xfer");
	if (!trace_instance)
		return;

	for (i = 0; i < ARRAY_SIZE(dw_i3c_xfer_trace_events); i++) {
		trace_array_set_clr_event(trace_instance, NULL,
					  dw_i3c_xfer_trace_events[i], true);
	}

	trace_array_put(trace_instance);
}
