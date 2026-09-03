/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common GBMS IRQ API driver
 */

#ifndef _GBMS_IRQ_H_
#define _GBMS_IRQ_H_

#include <linux/types.h>

#define gbms_irq_t uint16_t

#define GBMS_IRQ_VIN_UV_SHIFT	(0)
#define GBMS_IRQ_VIN_UV_MASK	BIT(0)
#define GBMS_IRQ_VIN_UV_CLEAR	~BIT(0)
#define GBMS_IRQ_VIN_OV_SHIFT	(1)
#define GBMS_IRQ_VIN_OV_MASK	BIT(1)
#define GBMS_IRQ_VIN_OV_CLEAR	~BIT(1)
#define GBMS_IRQ_VOUT_UV_SHIFT	(2)
#define GBMS_IRQ_VOUT_UV_MASK	BIT(2)
#define GBMS_IRQ_VOUT_UV_CLEAR	~BIT(2)
#define GBMS_IRQ_VOUT_OV_SHIFT	(3)
#define GBMS_IRQ_VOUT_OV_MASK	BIT(3)
#define GBMS_IRQ_VOUT_OV_CLEAR	~BIT(3)
#define GBMS_IRQ_IIN_UC_SHIFT	(4)
#define GBMS_IRQ_IIN_UC_MASK	BIT(4)
#define GBMS_IRQ_IIN_UC_CLEAR	~BIT(4)
#define GBMS_IRQ_IIN_OC_SHIFT	(5)
#define GBMS_IRQ_IIN_OC_MASK	BIT(5)
#define GBMS_IRQ_IIN_OC_CLEAR	~BIT(5)
#define GBMS_IRQ_IOUT_UC_SHIFT	(6)
#define GBMS_IRQ_IOUT_UC_MASK	BIT(6)
#define GBMS_IRQ_IOUT_UC_CLEAR	~BIT(6)
#define GBMS_IRQ_IOUT_OC_SHIFT	(7)
#define GBMS_IRQ_IOUT_OC_MASK	BIT(7)
#define GBMS_IRQ_IOUT_OC_CLEAR	~BIT(7)
#define GBMS_IRQ_IC_OCP_SHIFT	(8)
#define GBMS_IRQ_IC_OCP_MASK	BIT(8)
#define GBMS_IRQ_IC_OCP_CLEAR	~BIT(8)

typedef int (*gbms_irq_read_fn)(int irq, gbms_irq_t *val);

struct gbms_irq_config_t {
	int irq;
	gbms_irq_read_fn read_fn;

	/* do not add after */
	struct hlist_node hnode;
};

int gbms_irq_register(struct device *dev, int irq, gbms_irq_read_fn func);
int gbms_irq_read(int irq, gbms_irq_t *val);

#endif /* GBMS_IRQ */
