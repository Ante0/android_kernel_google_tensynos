/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef __POWER_CONTROLLER_LPM_PM_DOMAINS_RESIDENCY_H__
#define __POWER_CONTROLLER_LPM_PM_DOMAINS_RESIDENCY_H__

#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/kobject.h>

#define LPM_OFF 0
#define LPM_ON 1
#define LPM_STATE_COUNT 2

#define LPM_UNTRACKED 3

struct lpm_residency_time_state {
	u64 entry_count;
	u64 time_in_state;
	u64 last_entry;
	u64 last_exit;
	u64 last_updated;

	struct kobject residency_kobj;
};

struct lpm_residency_desc {
	struct lpm_residency_time_state residency[LPM_STATE_COUNT];
	int curr_state;

	struct kobject lpm_pm_domain_kobj;
};

int lpm_update_residency(struct lpm_residency_desc *desc, int new_state);

const struct lpm_residency_time_state *lpm_get_residency(struct lpm_residency_desc *desc);

void lpm_residency_init(struct lpm_residency_desc *desc);

int lpm_start_residency_tracking(struct lpm_residency_desc *desc, int init_state, bool from_zero);

void lpm_stop_residency_tracking(struct lpm_residency_desc *desc);

/* Pre: drvdata lpm_pm_domains has all pds initialized */
int lpm_residency_sysfs_init(struct platform_device *pdev);

void lpm_residency_sysfs_remove(struct platform_device *pdev);

#endif /* __POWER_CONTROLLER_LPM_PM_DOMAINS_RESIDENCY_H__ */
