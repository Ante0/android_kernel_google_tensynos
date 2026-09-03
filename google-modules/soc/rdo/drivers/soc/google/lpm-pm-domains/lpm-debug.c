// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/container_of.h>
#include <linux/debugfs.h>
#include <linux/pm_domain.h>

#include "lpm-debug.h"
#include "lpm-pm-domains.h"
#include "sequences/sequence.h"

#define ON_OFF_STR(x) ((x) ? "power_on" : "power_off")

static inline struct power_domain *to_power_domain(struct generic_pm_domain *d)
{
	return container_of(d, struct power_domain, genpd);
}

static int debugfs_power_domain_ctrl_set(void *domain, u64 val)
{
	struct generic_pm_domain *genpd = (struct generic_pm_domain *)(domain);
	int ret = 0;

	dev_dbg(&genpd->dev, "%s\n", ON_OFF_STR(val));

	mutex_lock(&genpd->mlock);
	if (val)
		ret = power_on(genpd);
	else
		ret = power_off(genpd);
	mutex_unlock(&genpd->mlock);

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(debugfs_power_domain_ctrl_fops, NULL,
			 debugfs_power_domain_ctrl_set, "%llu\n");

static void genpd_debugfs_add(struct generic_pm_domain *domain)
{
	struct power_domain *pd = to_power_domain(domain);
	struct dentry *d =
		debugfs_create_dir(domain->name,
				   pd->lpm_pm_domains->debugfs_root);

	debugfs_create_file("state", 0220, d, domain,
			    &debugfs_power_domain_ctrl_fops);
}

void genpd_debugfs_init(void *lpm_pm_domains)
{
	struct lpm_pm_domains *domains = lpm_pm_domains;

	domains->debugfs_root =
		debugfs_create_dir(dev_name(domains->dev), NULL);

	for (int i = 0; i < domains->pd_count; ++i)
		genpd_debugfs_add(&domains->pds[i].genpd);
}

void genpd_debugfs_remove(void *ptr)
{
	struct lpm_pm_domains *lpm_pm_domains = ptr;

	debugfs_remove_recursive(lpm_pm_domains->debugfs_root);
}
