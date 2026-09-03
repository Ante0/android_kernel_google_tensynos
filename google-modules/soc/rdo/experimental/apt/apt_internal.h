/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _GOOGLE_APT_INTERNAL_H
#define _GOOGLE_APT_INTERNAL_H

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/types.h>

#define GOOGLE_APT_NUM_GT_COMP 4
#define GOOGLE_APT_NUM_LT 4

#define GOOGLE_APT_CLOCKSOURCE_RATING 300
#define GOOGLE_APT_GT_COMP_CLKEVT_RATING 300
// Prefer use local timer since 32-bit with 24.576 MHz is enough for 4ms
// counter.
#define GOOGLE_APT_LT_CLKEVT_RATING 301

#define GOOGLE_APT_GT_COMP_MIN_DELTA 0x1ul
#define GOOGLE_APT_GT_COMP_MAX_DELTA 0xFFFFFFFFul
#define GOOGLE_APT_LT_MIN_DELTA 0x1ul
#define GOOGLE_APT_LT_MAX_DELTA 0xFFFFFFFFul

struct google_apt_gt_comp {
	// Field(s) initialized by platform driver.
	int irq;

	// Field(s) initialized by google_apt driver.
	int idx;

	// Internal field(s) below.
	const char *name;
	struct dentry *debugfs;
	u32 debug_irqcount;
	struct clock_event_device clkevt;
};

#define clkevt_to_google_apt_gt_comp(evt)                                      \
	container_of(evt, struct google_apt_gt_comp, clkevt)

struct google_apt_lt {
	// Field(s) initialized by platform driver.
	int irq;

	// Field(s) initialized by google_apt driver.
	int idx;

	// Internal field(s) below.
	const char *name;
	struct dentry *debugfs;
	u32 debug_irqcount;
	struct clock_event_device clkevt;
};

#define clkevt_to_google_apt_lt(evt)                                           \
	container_of(evt, struct google_apt_lt, clkevt)

struct google_apt {
	// Field(s) initialized by platform driver.
	struct device *dev;
	void __iomem *base;
	// Desired prescale ratio. Driver should convert it into corresponding
	// raw value before writing down to config registers.
	u32 prescaler;

	// Internal field(s) below.
	struct dentry *debugfs;

	struct clk *tclk;
	struct clocksource clocksource;
	struct google_apt_gt_comp gt_comp[GOOGLE_APT_NUM_GT_COMP];
	struct google_apt_lt lt[GOOGLE_APT_NUM_LT];
};

#define clocksource_to_google_apt(cs)                                          \
	container_of(cs, struct google_apt, clocksource)
#define gt_comp_to_google_apt(g)                                               \
	({                                                                     \
		struct google_apt_gt_comp *__gt_comp = (g);                    \
		container_of(__gt_comp, struct google_apt,                     \
			     gt_comp[__gt_comp->idx]);                         \
	})
#define lt_to_google_apt(l)                                                    \
	({                                                                     \
		struct google_apt_lt *__lt = (l);                              \
		container_of(__lt, struct google_apt, lt[__lt->idx]);          \
	})

// All interrupt registers (ISR/ISR_OVF/IER/IMR/ITR) have only one field on the
// same bit. Refer to the register mapping document for the usage of these
// interrupt registers.
#define GOOGLE_APT_INTERRUPT_FIELD BIT(0)

#define GOOGLE_APT_CONFIG_CLK 0x0000
#define GOOGLE_APT_CONFIG_CLK_FIELD_PRESCALER GENMASK(31, 16)
#define GOOGLE_APT_CONFIG_CLK_FIELD_PRESCALER_EN BIT(0)
#define GOOGLE_APT_CONFIG_CLK_PRESCALER_EN_FALSE 0
#define GOOGLE_APT_CONFIG_CLK_PRESCALER_EN_TRUE 1

#define GOOGLE_APT_ID_PART_NUM 0x0008

#define GOOGLE_APT_ID_CONFIG 0x000C
#define GOOGLE_APT_ID_CONFIG_FIELD_GT_COMP GENMASK(9, 5)
#define GOOGLE_APT_ID_CONFIG_FIELD_LT GENMASK(4, 0)

#define GOOGLE_APT_ID_VERSION 0x0010
#define GOOGLE_APT_ID_VERSION_FIELD_MAJOR GENMASK(31, 24)
#define GOOGLE_APT_ID_VERSION_FIELD_MINOR GENMASK(23, 16)
#define GOOGLE_APT_ID_VERSION_FIELD_INCREMENTAL GENMASK(15, 0)

#define GOOGLE_APT_GT_TIM_CONFIG 0x0014
#define GOOGLE_APT_GT_TIM_CONFIG_VALUE_DISABLE 0
#define GOOGLE_APT_GT_TIM_CONFIG_VALUE_ENABLE 1

#define GOOGLE_APT_GT_TIM_LOWER 0x0018
#define GOOGLE_APT_GT_TIM_UPPER 0x001C
#define GOOGLE_APT_GT_TIM_UPPER_LATCH 0x0020
#define GOOGLE_APT_GT_TIM_LOAD_LOWER 0x0024
#define GOOGLE_APT_GT_TIM_LOAD_UPPER 0x0028

#define GOOGLE_APT_GT_COMPX_BASE(n) (0x1000 + 0x1000 * (n))

#define GOOGLE_APT_GT_COMPX_CONFIG(n) (GOOGLE_APT_GT_COMPX_BASE(n))
#define GOOGLE_APT_GT_COMPX_CONFIG_FIELD_MODE BIT(1)
#define GOOGLE_APT_GT_COMPX_CONFIG_MODE_ONESHOT 0
#define GOOGLE_APT_GT_COMPX_CONFIG_MODE_AUTO_INCREMENT 1
#define GOOGLE_APT_GT_COMPX_CONFIG_FIELD_ENABLE BIT(0)

#define GOOGLE_APT_GT_COMPX_ADD_INCR(n) (GOOGLE_APT_GT_COMPX_BASE(n) + 0x04)
#define GOOGLE_APT_GT_COMPX_LOWER(n) (GOOGLE_APT_GT_COMPX_BASE(n) + 0x08)
#define GOOGLE_APT_GT_COMPX_UPPER_LATCH(n) (GOOGLE_APT_GT_COMPX_BASE(n) + 0x10)
#define GOOGLE_APT_GT_COMPX_INTERRUPT_ISR(n)                                   \
	(GOOGLE_APT_GT_COMPX_BASE(n) + 0x14)
#define GOOGLE_APT_GT_COMPX_INTERRUPT_ISR_OVF(n)                               \
	(GOOGLE_APT_GT_COMPX_BASE(n) + 0x18)
#define GOOGLE_APT_GT_COMPX_INTERRUPT_IER(n)                                   \
	(GOOGLE_APT_GT_COMPX_BASE(n) + 0x1C)
#define GOOGLE_APT_GT_COMPX_INTERRUPT_IMR(n)                                   \
	(GOOGLE_APT_GT_COMPX_BASE(n) + 0x20)
#define GOOGLE_APT_GT_COMPX_INTERRUPT_ITR(n)                                   \
	(GOOGLE_APT_GT_COMPX_BASE(n) + 0x24)

#define GOOGLE_APT_LTX_BASE(n) (0x5000 + 0x1000 * (n))

#define GOOGLE_APT_LTX_CONFIG(n) (GOOGLE_APT_LTX_BASE(n))
#define GOOGLE_APT_LTX_CONFIG_FIELD_MODE BIT(1)
#define GOOGLE_APT_LTX_CONFIG_MODE_ONESHOT 0
#define GOOGLE_APT_LTX_CONFIG_MODE_AUTORELOAD 1
#define GOOGLE_APT_LTX_CONFIG_FIELD_ENABLE BIT(0)

#define GOOGLE_APT_LTX_CNT_LOAD(n) (GOOGLE_APT_LTX_BASE(n) + 0x04)
#define GOOGLE_APT_LTX_CNT(n) (GOOGLE_APT_LTX_BASE(n) + 0x08)
#define GOOGLE_APT_LTX_INTERRUPT_ISR(n) (GOOGLE_APT_LTX_BASE(n) + 0x0C)
#define GOOGLE_APT_LTX_INTERRUPT_ISR_OVF(n) (GOOGLE_APT_LTX_BASE(n) + 0x10)
#define GOOGLE_APT_LTX_INTERRUPT_IER(n) (GOOGLE_APT_LTX_BASE(n) + 0x14)
#define GOOGLE_APT_LTX_INTERRUPT_IMR(n) (GOOGLE_APT_LTX_BASE(n) + 0x18)
#define GOOGLE_APT_LTX_INTERRUPT_ITR(n) (GOOGLE_APT_LTX_BASE(n) + 0x1C)

static inline u32 google_apt_readl(struct google_apt *apt, ptrdiff_t offset)
{
	return readl(apt->base + offset);
}

static inline void google_apt_writel(struct google_apt *apt, u32 val,
				     ptrdiff_t offset)
{
	writel(val, apt->base + offset);
}

int google_apt_debugfs_init(void);

int google_apt_init(struct google_apt *apt);
int google_apt_exit(struct google_apt *apt);

unsigned long google_apt_get_prescaled_tclk_rate(const struct google_apt *apt);

#endif /* _GOOGLE_APT_INTERNAL_H */
