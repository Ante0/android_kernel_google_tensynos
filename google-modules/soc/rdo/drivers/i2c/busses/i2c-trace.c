// SPDX-License-Identifier: GPL-2.0-only

#include <linux/platform_device.h>
#include <linux/trace.h>
#include <linux/trace_events.h>

#include "i2c-trace.h"

static const char * const i2c_trace_events[] = {
	"i2c_dw_set_timings_master_cnts",
	"i2c_dw_set_timings_master_fast_mod_cnts",
	"i2c_dw_set_timings_master_hs_mod_cnts",
	"i2c_dw_set_timings_master_bus_speed",
	"i2c_dw_xfer",
	"i2c_dw_isr",
	"i2c_dw_set_sda_hold",
	"i2c_dw_suspend",
	"i2c_dw_resume",
	"i2c_dw_runtime_suspend",
	"i2c_dw_runtime_resume",
	"i2c_dw_xfer_init",
	"i2c_dw_xfer_msg",
};

void google_i2c_trace_init(struct platform_device *pdev)
{
	struct trace_array *trace_instance;

	trace_instance = trace_array_get_by_name("i2c_google", "i2c_dw");
	if (!trace_instance) {
		dev_err(&pdev->dev, "I2C trace instance creation/retrieve did not succeed\n");
		return;
	}

	for (int i = 0; i < ARRAY_SIZE(i2c_trace_events); i++)
		trace_array_set_clr_event(trace_instance, NULL, i2c_trace_events[i], true);

	trace_array_put(trace_instance);
}
