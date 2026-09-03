// SPDX-License-Identifier: GPL-2.0-only
#include <linux/platform_device.h>
#include <linux/trace.h>
#include <linux/trace_events.h>

#include "spi-dw-mmio-trace.h"

static const char * const spi_dw_mmio_trace_events[] = {
	"spi_dw_mmio_probe",
	"spi_dw_mmio_remove",
	"spi_dw_mmio_runtime_resume",
	"spi_dw_mmio_runtime_suspend",
	"spi_dw_mmio_set_cs",
};

void spi_dw_mmio_trace_init(struct platform_device *pdev)
{
	struct trace_array *trace_instance;
	int i;

	trace_instance = trace_array_get_by_name("spi_dw_mmio", "spi_dw");
	if (!trace_instance)
		dev_err(&pdev->dev, "Failed to create/retrieve spi_dw_mmio trace instance\n");
	for (i = 0; i < ARRAY_SIZE(spi_dw_mmio_trace_events); i++)
		trace_array_set_clr_event(trace_instance, NULL, spi_dw_mmio_trace_events[i], true);

	trace_array_put(trace_instance);
}
