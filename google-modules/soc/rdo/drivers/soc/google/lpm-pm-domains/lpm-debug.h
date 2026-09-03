/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 */

#ifndef __POWER_CONTROLLER_LPM_PM_DOMAINS_DEBUG_H__
#define __POWER_CONTROLLER_LPM_PM_DOMAINS_DEBUG_H__

#ifdef CONFIG_DEBUG_FS
void genpd_debugfs_init(void *lpm_pm_domains);
void genpd_debugfs_remove(void *lpm_pm_domains);
#else
static inline void genpd_debugfs_init(void *lpm_pm_domains) {}
static inline void genpd_debugfs_remove(void *lpm_pm_domains) {}
#endif

#endif /* __POWER_CONTROLLER_LPM_PM_DOMAINS_DEBUG_H__ */
