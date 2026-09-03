/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __MAX77759_BCL_IRQ_H
#define __MAX77759_BCL_IRQ_H

#include <linux/platform_device.h>

#define MAX_IRQ_CTX 3

#if IS_ENABLED(CONFIG_KUNIT)
int max77759_get_irq(struct device *ifpmic_irq_dev, int *idx);
int max77759_clr_irq(struct device *ifpmic_irq_dev, int idx);
int max77759_vimon_read(struct device *ifpmic_irq_dev);
int max77759_external_reg_read_helper(struct device *dev, uint8_t reg,
				      uint8_t *val);
int max77759_external_reg_write_helper(struct device *dev, uint8_t reg,
				       uint8_t val);
int max77759_set_oilo(struct device *ifpmic_irq_dev, int val);
int max77759_get_oilo(struct device *ifpmic_irq_dev, int *val);
int max77759_set_uvlo1(struct device *ifpmic_irq_dev, int val);
int max77759_get_uvlo1(struct device *ifpmic_irq_dev, int *val);
int max77759_set_uvlo2(struct device *ifpmic_irq_dev, int val);
int max77759_get_uvlo2(struct device *ifpmic_irq_dev, int *val);
int max77759_set_uvlo1_hyst(struct device *ifpmic_irq_dev, int val);
int max77759_get_uvlo1_hyst(struct device *ifpmic_irq_dev, int *val);
int max77759_set_uvlo2_hyst(struct device *ifpmic_irq_dev, int val);
int max77759_get_uvlo2_hyst(struct device *ifpmic_irq_dev, int *val);
#endif

struct max77759_irq_context;

struct max77759_bcl_irq_data {
	struct device *dev;
	struct bcl_device *bcl_dev;
	struct device *pmic_dev;
	struct gpio_desc *vd1_gpio;
	struct gpio_desc *vd2_gpio;
	struct max77759_irq_context *irq_ctx[MAX_IRQ_CTX];
};

struct max77759_irq_context {
	int idx;
	struct max77759_bcl_irq_data *parent;
};


#endif /* __MAX77759_BCL_IRQ_H */
