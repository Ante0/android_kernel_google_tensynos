// SPDX-License-Identifier: GPL-2.0 only
/*
 * common.c Google bcl sysfs driver library functions
 *
 * Copyright (c) 2026 Google LLC
 *
 */
#include <sysfs/batoilo_policy_lvl.h>

int check_batoilo_threshold_range(struct bcl_device *bcl_dev, int idx,
				  int value)
{
	int batoilo_lower_limit, batoilo_upper_limit;

	if (idx != BATOILO1 && idx != BATOILO2)
		return -EINVAL;

	if (!bcl_dev->zone[idx])
		return -EIO;

	if (idx == BATOILO1) {
		batoilo_lower_limit =
			bcl_dev->batt_irq_conf1.batoilo_lower_limit;
		batoilo_upper_limit =
			bcl_dev->batt_irq_conf1.batoilo_upper_limit;
	} else {
		batoilo_lower_limit =
			bcl_dev->batt_irq_conf2.batoilo_lower_limit;
		batoilo_upper_limit =
			bcl_dev->batt_irq_conf2.batoilo_upper_limit;
	}

	if (value < batoilo_lower_limit || value > batoilo_upper_limit) {
		dev_err(bcl_dev->device,
			"zone %d: %d outside of range %d - %d mA.", idx, value,
			batoilo_lower_limit, batoilo_upper_limit);
		return -EINVAL;
	}

	return 0;
}
