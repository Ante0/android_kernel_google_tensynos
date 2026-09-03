/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cdev_helper.h Thermal cooling device helper.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */
#ifndef _CDEV_HELPER_H_
#define _CDEV_HELPER_H_

#include "thermal_cpm_mbox.h"

struct cdev_opp_table {
	unsigned int power;
	unsigned int freq;
	unsigned int voltage;
	unsigned int *children_power;
};

int cdev_dev_pm_opp_get_opp_count(struct device *dev);
struct dev_pm_opp *cdev_dev_pm_opp_find_freq_ceil(struct device *dev,
						  unsigned long *freq);
void cdev_dev_pm_opp_put(struct dev_pm_opp *opp);
int cdev_msg_tmu_get_power_table(enum hw_dev_type cdev_id, u8 idx,
				 int *val, int *max_state_idx);

#endif  // _CDEV_HELPER_H_
