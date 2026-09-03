// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */

#include "ifpmic_class.h"

#if IS_ENABLED(CONFIG_DRV_SAMSUNG_PMIC)

#include <linux/regulator/pmic_class.h>

int ifpmic_class_create(void)
{
	/* Create by pmic_class.ko */
	return 0;
}

void ifpmic_class_destroy(void)
{
	/* Destroy by pmic_class.ko */
}

struct device *ifpmic_device_create_with_groups(struct device *parent, void *drvdata,
						const struct attribute_group **groups,
						const char *fmt)
{
	return pmic_subdevice_create(parent, groups, drvdata, fmt);
}

void ifpmic_device_destroy(dev_t devt)
{
	pmic_device_destroy(devt);
}

#else /* CONFIG_DRV_SAMSUNG_PMIC */

#include <linux/device.h>

static struct class *ifpmic_class;
static atomic_t ifpmic_dev;

int ifpmic_class_create(void)
{
	ifpmic_class = class_create("pmic");

	if (IS_ERR(ifpmic_class)) {
		return PTR_ERR(ifpmic_class);
	}

	return 0;
}

void ifpmic_class_destroy(void)
{
	class_destroy(ifpmic_class);
}

struct device *ifpmic_device_create_with_groups(struct device *parent, void *drvdata,
						const struct attribute_group **groups,
						const char *fmt)
{
	return device_create_with_groups(ifpmic_class, parent, atomic_inc_return(&ifpmic_dev),
					 drvdata, groups, fmt);
}

void ifpmic_device_destroy(dev_t devt)
{
	device_destroy(ifpmic_class, devt);
}

#endif /* CONFIG_DRV_SAMSUNG_PMIC */
