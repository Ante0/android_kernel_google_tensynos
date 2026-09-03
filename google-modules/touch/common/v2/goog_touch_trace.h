/* SPDX-License-Identifier: GPL */
/*
 * Google Touch Trace for Pixel.
 *
 * Copyright 2025 Google LLC.
 */

#ifndef __GOOG_TOUCH_TRACE_H__
#define __GOOG_TOUCH_TRACE_H__

#include <trace/hooks/systrace.h>
#include <linux/spi/spi.h>
#include <linux/list.h>

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
/* Perfetto trace for spi_sync() */
static inline int gti_spi_sync(struct spi_device *spi, struct spi_message *message)
{
	int ret;
	struct spi_transfer *t;
	char trace_tag[128];
	int trace_tag_len = 0;

	trace_tag_len = scnprintf(trace_tag, sizeof(trace_tag), "gti: spi_sync: ");
	list_for_each_entry(t, &(message->transfers), transfer_list) {
		if (trace_tag_len < sizeof(trace_tag)) {
			const char *dir = (t->tx_buf && t->rx_buf) ?
						  "WR" :
						  (t->tx_buf ? "W" : (t->rx_buf ? "R" : "?"));
			trace_tag_len += scnprintf(trace_tag + trace_tag_len,
						   sizeof(trace_tag) - trace_tag_len, "len=%u(%s) ",
						   t->len, dir);
		}
	}
	ATRACE_BEGIN(trace_tag);
	ret = spi_sync(spi, message);
	if (ret == -ESHUTDOWN) {
		pr_err("gti: SPI is currently suspended. Waiting for it to resume.\n");
		msleep_interruptible(500);
	}
	ATRACE_END();
	return ret;
}
#else
static inline int gti_spi_sync(struct spi_device *spi, struct spi_message *message)
{
	return spi_sync(spi, message);
}
#endif

#endif /* __GOOG_TOUCH_TRACE_H__ */
