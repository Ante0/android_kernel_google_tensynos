/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024-2025 Google LLC.
 */

#ifndef __POWER_CONTROLLER_LPM_PM_DOMAINS_H__
#define __POWER_CONTROLLER_LPM_PM_DOMAINS_H__

#include <linux/pm_domain.h>

#include "lpm-residency.h"

struct regmap;

struct lpm_pm_domains {
	struct device *dev;
	struct power_domain *pds;
	u32 pd_count;

	struct dentry *debugfs_root;
	struct kobject *residency_root;
};

struct power_domain {
	struct device *dev;
	struct generic_pm_domain genpd;
	struct power_ops *ops;
	struct lpm_residency_desc *residency;
	void __iomem **regions;
	u32 subdomain_id;
	struct regmap **syscons;

	struct lpm_pm_domains *lpm_pm_domains;
};

int power_on(struct generic_pm_domain *domain);
int power_off(struct generic_pm_domain *domain);

#endif /* __POWER_CONTROLLER_LPM_PM_DOMAINS_H__ */
