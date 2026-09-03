/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __BCL_SYSFS_BATOILO_LVL_H
#define __BCL_SYSFS_BATOILO_LVL_H

#include <linux/kernel.h>
#include "bcl.h"

/**
 * @brief - Generates sysfs read/write functions and device attribute for BATOILO trigger level.
 * @param [in] num The index of the BATOILO zone (e.g., 1 or 2) used to select
 * the corresponding interrupt configuration.
 * @param [in] type The trigger source type (e.g., usb) used to construct function
 * names and access the specific threshold member.
 * @return None. (Defines static sysfs functions).
 */
#define BATOILO_TYPE_FUNC(num, type)                                          \
	static ssize_t batoilo##num##_##type##_lvl_store(                     \
		struct device *dev, struct device_attribute *attr,            \
		const char *buf, size_t size)                                 \
	{                                                                     \
		struct platform_device *pdev =                                \
			container_of(dev, struct platform_device, dev);       \
		struct bcl_device *bcl_dev = platform_get_drvdata(pdev);      \
		unsigned int value;                                           \
		int ret;                                                      \
\
		ret = kstrtou32(buf, 10, &value);                             \
		if (ret)                                                      \
			return ret;                                           \
\
		ret = check_batoilo_threshold_range(bcl_dev, BATOILO##num,    \
						    value);                   \
		if (ret)                                                      \
			return ret;                                           \
\
		bcl_dev->batt_irq_conf##num.batoilo_##type##_trig_lvl =       \
			(value -                                              \
			 bcl_dev->batt_irq_conf##num.batoilo_lower_limit) /   \
			BO_STEP;                                              \
\
		return size;                                                  \
	}                                                                     \
\
	static ssize_t batoilo##num##_##type##_lvl_show(                      \
		struct device *dev, struct device_attribute *attr, char *buf) \
	{                                                                     \
		struct platform_device *pdev =                                \
			container_of(dev, struct platform_device, dev);       \
		struct bcl_device *bcl_dev = platform_get_drvdata(pdev);      \
		unsigned int lvl_mA;                                          \
\
		if (!bcl_dev->zone[BATOILO##num])                             \
			return -EIO;                                          \
		if (!bcl_dev->intf_pmic_dev)                                  \
			return -EBUSY;                                        \
\
		lvl_mA = BO_STEP * bcl_dev->batt_irq_conf##num                \
					   .batoilo_##type##_trig_lvl +       \
			 bcl_dev->batt_irq_conf##num.batoilo_lower_limit;     \
\
		return sysfs_emit(buf, "%umA\n", lvl_mA);                     \
	}                                                                     \
\
	static DEVICE_ATTR_RW(batoilo##num##_##type##_lvl)

/**
 * @brief Checks if the provided BATOILO threshold value is within the valid range.
 *
 * @param[in] bcl_dev Pointer to the BCL device structure.
 * @param[in] idx     Index of the BATOILO zone.
 * @param[in] value   The threshold value (in mA) to check.
 *
 * @return 0 if the value is within the valid range.
 * @return -EINVAL if the index is invalid or the value is outside the allowed limits.
 * @return -EIO if the specified zone is not initialized.
 */
int check_batoilo_threshold_range(struct bcl_device *bcl_dev, int idx,
				  int value);

#endif /* __BCL_SYSFS_BATOILO_LVL_H */
