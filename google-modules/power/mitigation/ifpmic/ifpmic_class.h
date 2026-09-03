/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __IFPMIC_CLASS_H
#define __IFPMIC_CLASS_H

#include <linux/sysfs.h>
#include <linux/types.h>

int ifpmic_class_create(void);
void ifpmic_class_destroy(void);
struct device *ifpmic_device_create_with_groups(struct device *parent, void *drvdata,
						const struct attribute_group **groups,
						const char *fmt);
void ifpmic_device_destroy(dev_t devt);

#endif /* __IFPMIC_CLASS_H */
