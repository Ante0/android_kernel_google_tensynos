/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __MAX77779_BCL_IRQ_H
#define __MAX77779_BCL_IRQ_H

#include <linux/platform_device.h>

#define MAX77779_VIMON_NV_PRE_LSB 78122
#define MAX77779_VIMON_NA_PRE_LSB 781250
#define MAX_IRQ_CTX 4
#define VIMON_BUF_SIZE 256

struct max77779_irq_context;

struct max77779_bcl_irq_data {
	struct device *dev;
	struct bcl_device *bcl_dev;
	struct device *pmic_dev;
	struct device *chg_dev;
	struct device *vimon_dev;
	struct gpio_desc *vd1_gpio;
	struct gpio_desc *vd2_gpio;
	struct max77779_irq_context *irq_ctx[MAX_IRQ_CTX];

	/* VIMON */
	uint16_t vimon_data[VIMON_BUF_SIZE];
	size_t vimon_count;
};

struct max77779_irq_context {
	int idx;
	struct max77779_bcl_irq_data *parent;
};

int max77779_get_irq(struct device *ifpmic_irq_dev, int *idx);
int max77779_clr_irq(struct device *ifpmic_irq_dev, int idx);

/*
 * The function prototypes and data declarations below are only made visible
 * when KUnit is enabled. This allows the KUnit test module to link against
 * the static functions and data in the driver for testing purposes.
 */
#if IS_ENABLED(CONFIG_KUNIT)

int max77779_external_pmic_reg_read_helper(struct device *dev, uint8_t reg,
					   uint8_t *val);
int max77779_external_pmic_reg_write_helper(struct device *dev, uint8_t reg,
					    uint8_t val);
int max77779_external_chg_reg_read_helper(struct device *dev, uint8_t reg,
					  uint8_t *val);
int max77779_external_chg_reg_write_helper(struct device *dev, uint8_t reg,
					   uint8_t val);
int max77779_set_oilo1(struct device *ifpmic_irq_dev, int val);
int max77779_get_oilo1(struct device *ifpmic_irq_dev, int *val);
int max77779_set_oilo2(struct device *ifpmic_irq_dev, int val);
int max77779_get_oilo2(struct device *ifpmic_irq_dev, int *val);
int max77779_set_uvlo1(struct device *ifpmic_irq_dev, int val);
int max77779_get_uvlo1(struct device *ifpmic_irq_dev, int *val);
int max77779_set_uvlo2(struct device *ifpmic_irq_dev, int val);
int max77779_get_uvlo2(struct device *ifpmic_irq_dev, int *val);
int max77779_set_uvlo1_hyst(struct device *ifpmic_irq_dev, int val);
int max77779_get_uvlo1_hyst(struct device *ifpmic_irq_dev, int *val);
int max77779_set_uvlo2_hyst(struct device *ifpmic_irq_dev, int val);
int max77779_get_uvlo2_hyst(struct device *ifpmic_irq_dev, int *val);

int max77779_set_uvlo_vdroop(struct device *ifpmic_irq_dev, int uvlo_type,
			     int vdroop_type, bool enable);
int max77779_set_oilo_vdroop(struct device *ifpmic_irq_dev, int oilo_type,
			     int vdroop_type, bool enable);
int max77779_external_vimon_read_buffer_helper(struct device *dev, uint16_t *buff,
					       size_t *count, size_t buff_max);
int max77779_vimon_read(struct device *dev);
#endif /* IS_ENABLED(CONFIG_KUNIT) */

#endif /* __MAX77779_BCL_IRQ_H */
