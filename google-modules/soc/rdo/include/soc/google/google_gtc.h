/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Google LLC
 */

#ifndef _GOOGLE_GTC_H
#define _GOOGLE_GTC_H

#include <linux/dev_printk.h>

#if IS_ENABLED(CONFIG_GOOGLE_GTC)

/*
 * goog_gtc_get_counter - Read the current GTC counter/tick value.
 * Return:			The current GTC counter/tick value.
 */
u64 goog_gtc_get_counter(void);

/*
 * goog_gtc_ticks_to_ns - Convert a GTC counter/tick value into the timestamp in nanosecond.
 * @gtc_tick:			Global Timestamp Counter (GTC) counter/tick value.
 * Return:			The equivalent GTC timestamp in nanosecond.
 */
u64 goog_gtc_ticks_to_ns(u64 gtc_tick);

/*
 * goog_gtc_get_time_ns - Read the current nanosecond-format GTC timestamp.
 * Return:			The current GTC timestamp in nanosecond.
 */
u64 goog_gtc_get_time_ns(void);

/*
 * goog_gtc_ticks_to_us - Convert a GTC counter/tick value into the timestamp in microsecond.
 * @gtc_tick:			Global Timestamp Counter (GTC) counter/tick value.
 * Return:			The equivalent GTC timestamp in microsecond.
 */
u64 goog_gtc_ticks_to_us(u64 gtc_tick);

/*
 * goog_gtc_get_time_us - Read the current microsecond-format GTC timestamp.
 * Return:			The current GTC timestamp in microsecond.
 */
u64 goog_gtc_get_time_us(void);

/*
 * goog_gtc_get_freq_hz - Returns the GTC frequency in Hz.
 * Return:			The GTC tick frequency in Hz.
 */
u64 goog_gtc_get_freq_hz(void);

#else /* IS_ENABLED(CONFIG_GOOGLE_GTC) */

static inline u64 goog_gtc_get_counter(void)
{
	return 0;
}

static inline u64 goog_gtc_ticks_to_ns(u64 gtc_tick)
{
	return 0;
}

static inline u64 goog_gtc_get_time_ns(void)
{
	return 0;
}

static inline u64 goog_gtc_ticks_to_us(u64 gtc_tick)
{
	return 0;
}

static inline u64 goog_gtc_get_time_us(void)
{
	return 0;
}

static inline u64 goog_gtc_get_freq_hz(void)
{
	return 0;
}

#endif /* IS_ENABLED(CONFIG_GOOGLE_GTC) */


#define GTC_DEV_DBG(dev, fmt, args...) \
	dev_dbg(dev, "[%llu] " fmt, goog_gtc_get_counter(), ##args)

#define GTC_DEV_INFO(dev, fmt, args...) \
	dev_info(dev, "[%llu] " fmt, goog_gtc_get_counter(), ##args)

#define GTC_DEV_WARN(dev, fmt, args...) \
	dev_warn(dev, "[%llu] " fmt, goog_gtc_get_counter(), ##args)

#define GTC_DEV_ERR(dev, fmt, args...) \
	dev_err(dev, "[%llu] " fmt, goog_gtc_get_counter(), ##args)

#endif /* _GOOGLE_GTC_H */
