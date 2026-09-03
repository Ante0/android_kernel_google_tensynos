/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __IFPMIC_DEFS_H
#define __IFPMIC_DEFS_H

#include "bcl.h"

int batoilo_reg_read(struct device *dev, enum IFPMIC ifpmic, int oilo, unsigned int *val);
int batoilo_reg_write(struct device *dev, uint8_t val, enum IFPMIC ifpmic, int oilo);
int uvlo_reg_read(struct device *dev, enum IFPMIC ifpmic, int triggered, unsigned int *val);
int uvlo_reg_write(struct device *dev, uint8_t val, enum IFPMIC ifpmic, int triggered);
bool ifpmic_retrieve_batoilo_asserted(struct device *dev, enum IFPMIC ifpmic);
int ifpmic_setup(struct bcl_device *bcl_dev, struct platform_device *pdev);
int ifpmic_setup_dev(struct bcl_device *bcl_dev);
int ifpmic_init_fs(struct bcl_device *bcl_dev);
void ifpmic_destroy_fs(struct bcl_device *bcl_dev);
void ifpmic_teardown(struct bcl_device *bcl_dev);
extern int max77759_ifpmic_setup(struct bcl_device *bcl_dev, struct platform_device *pdev);
extern int max77779_ifpmic_setup(struct bcl_device *bcl_dev, struct platform_device *pdev);
extern const struct attribute_group *mitigation_mw_groups[];
extern const struct attribute_group *mitigation_sq_groups[];

#endif /* __IFPMIC_DEFS_H */
