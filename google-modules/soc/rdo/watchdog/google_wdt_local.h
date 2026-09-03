/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Google wdt local header file.
 *
 * Copyright (C) 2023 Google LLC.
 */

#ifndef _GOOGLE_WDT_LOCAL_H
#define _GOOGLE_WDT_LOCAL_H

#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/watchdog.h>

#define GOOGLE_WDT_VALUE_MAX 0xFFFFFFFF

#define GOOGLE_WDT_ID_PART_NUM 0x0000
#define GOOGLE_WDT_ID_PART_NUM_VALUE 0x574454 //ASCII of "WDT"

#define GOOGLE_WDT_ID_VERSION 0x0004
#define GOOGLE_WDT_ID_VERSION_FIELD_MAJOR GENMASK(31, 24)
#define GOOGLE_WDT_ID_VERSION_FIELD_MINOR GENMASK(23, 16)
#define GOOGLE_WDT_ID_VERSION_FIELD_INCREMENTAL GENMASK(15, 0)

#define GOOGLE_WDT_WDT_CONTROL 0x0008
#define GOOGLE_WDT_WDT_CONTROL_FIELD_INT_CLEAR BIT(5)
#define GOOGLE_WDT_WDT_CONTROL_FIELD_DEBUG_ENABLE BIT(4)
#define GOOGLE_WDT_WDT_CONTROL_FIELD_EXPIRY_ACTION BIT(3)
#define GOOGLE_WDT_WDT_CONTROL_FIELD_KEY_ENABLE BIT(2)
#define GOOGLE_WDT_WDT_CONTROL_FIELD_LOW_POWER_ENABLE BIT(1)
#define GOOGLE_WDT_WDT_CONTROL_FIELD_ENABLE BIT(0)

#define GOOGLE_WDT_WDT_VALUE 0x000C
#define GOOGLE_WDT_WDT_KEY 0x0010

struct google_wdt {
	// Field(s) initialize by platform probe.
	struct list_head node;
	struct device *dev;
	void __iomem *base;
	int irq;
	u8 expiry_action;
	bool use_syscore_pm;
	u32 key_unlock;
	u32 key_lock;
	u32 control;
	u32 (*read)(struct google_wdt *wdt, ptrdiff_t offset);
	void (*write)(struct google_wdt *wdt, u32 val, ptrdiff_t offset);

	// Internal field(s) below.

	/*
	 * Protects I/O access to wdt, so that IRQ will not happen during
	 * watchdog_op_start and watchdog_op_ping.
	 */
	spinlock_t lock;
	struct clk *tclk;
	unsigned long tclk_rate_hz;
	u32 ping_value;
	struct watchdog_device wdd;
};

int google_wdt_validate_id_part_num(struct google_wdt *wdt);

irqreturn_t google_wdt_isr(int irq, void *data);

int google_wdt_watchdog_op_start(struct watchdog_device *wdd);

int google_wdt_watchdog_op_stop(struct watchdog_device *wdd);

#endif // _GOOGLE_WDT_LOCAL_H
