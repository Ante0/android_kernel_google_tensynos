// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for GBMS IRQ driver
 */

#include <linux/device.h>
#include <linux/hashtable.h>
#include <linux/module.h>

#include "gbms_irq.h"

/* 1 << 3 = 8 entries */
#define GBMS_IRQ_HASHTABLE_SIZE	3
DECLARE_HASHTABLE(gbms_irq_table, GBMS_IRQ_HASHTABLE_SIZE);

static struct gbms_irq_config_t *gbms_irq_get_cnfg(int irq)
{
	struct gbms_irq_config_t *cnfg;

	hash_for_each_possible(gbms_irq_table, cnfg, hnode, irq) {
		if (cnfg->irq == irq)
			return cnfg;
	}

	return NULL;
}

int gbms_irq_register(struct device *dev, int irq, gbms_irq_read_fn func)
{
	struct gbms_irq_config_t *cnfg;

	if (gbms_irq_get_cnfg(irq))
		return 0;

	cnfg = devm_kzalloc(dev, sizeof(*cnfg), GFP_KERNEL);
	if (!cnfg) {
		dev_err(dev, "No memory for gbms irq\n");
		return - ENOMEM;
	}

	dev_err(dev, "Registering gbms irq:%d\n", irq);

	cnfg->irq = irq;
	cnfg->read_fn = func;

	hash_add(gbms_irq_table, &cnfg->hnode, irq);

	return 0;
}
EXPORT_SYMBOL_GPL(gbms_irq_register);

int gbms_irq_read(int irq, gbms_irq_t *val)
{
	struct gbms_irq_config_t *cnfg = gbms_irq_get_cnfg(irq);

	if (!cnfg)
		return -EINVAL;

	return cnfg->read_fn(irq, val);
}
EXPORT_SYMBOL_GPL(gbms_irq_read);
