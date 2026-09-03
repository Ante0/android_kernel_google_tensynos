// SPDX-License-Identifier: GPL-2.0-only
#undef TRACE_SYSTEM
#define TRACE_SYSTEM spi_dw

#if !defined(_TRACE_SPI_DW_MMIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SPI_DW_MMIO_H

#include <linux/spi/spi.h>
#include <linux/tracepoint.h>

#define MAX_DEV_NAME_SIZE 32

TRACE_EVENT(spi_dw_mmio_set_cs,
	TP_PROTO(struct spi_device *spi, bool enable),
	TP_ARGS(spi, enable),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
		__field(u16, bus_num)
		__field(u16, cs_index)
		__field(bool, enable)
	),
	TP_fast_assign(
		strscpy(__entry->dev_name, dev_name(&spi->dev), MAX_DEV_NAME_SIZE);
		__entry->bus_num = spi->controller->bus_num;
		__entry->cs_index = spi_get_chipselect(spi, 0);
		__entry->enable = enable;
	),
	TP_printk("[%s]: bus=%u cs=%u %s",
		__entry->dev_name,
		__entry->bus_num,
		__entry->cs_index,
		__entry->enable ? "assert" : "deassert")
);

DECLARE_EVENT_CLASS(spi_dw_mmio,
	TP_PROTO(struct device *dev),
	TP_ARGS(dev),
	TP_STRUCT__entry(
		__array(char, dev_name, MAX_DEV_NAME_SIZE)
	),
	TP_fast_assign(
		strscpy(__entry->dev_name, dev_name(dev), MAX_DEV_NAME_SIZE);
	),
	TP_printk("%s", __entry->dev_name)
);

DEFINE_EVENT(spi_dw_mmio, spi_dw_mmio_runtime_suspend,
	TP_PROTO(struct device *dev),
	TP_ARGS(dev)
);

DEFINE_EVENT(spi_dw_mmio, spi_dw_mmio_runtime_resume,
	TP_PROTO(struct device *dev),
	TP_ARGS(dev)
);

DEFINE_EVENT(spi_dw_mmio, spi_dw_mmio_probe,
	TP_PROTO(struct device *dev),
	TP_ARGS(dev)
);

DEFINE_EVENT(spi_dw_mmio, spi_dw_mmio_remove,
	TP_PROTO(struct device *dev),
	TP_ARGS(dev)
);

void spi_dw_mmio_trace_init(struct platform_device *pdev);

#endif /* _TRACE_SPI_DW_MMIO_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../drivers/spi

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE spi-dw-mmio-trace

#include <trace/define_trace.h>
