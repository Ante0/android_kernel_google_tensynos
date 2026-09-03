/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */
#ifndef __GOOGLE_POWERDASHBOARD_HELPER_H__
#define __GOOGLE_POWERDASHBOARD_HELPER_H__

#include "google_powerdashboard_iface.h"
#include <linux/platform_device.h>

#define SYSFS_PWRBLK_NODE(_id, name, _psm_num)                             \
	static ssize_t pwrblk_##name##_show(struct device *dev,                \
					    struct device_attribute *devattr,  \
					    char *buf)                         \
	{                                                                      \
		struct platform_device *pdev = to_platform_device(dev);        \
		struct google_powerdashboard *pd = platform_get_drvdata(pdev); \
		typeof(_psm_num)(psm_num) = (_psm_num);                        \
		if (google_read_section(pd, PD_PWRBLK))                        \
			return -EBUSY;                                             \
		return google_pwrblk_show(buf, pd, _id, psm_num);              \
	}

#define SYSFS_LPCM_NODE(_id, name, _lpcm_num)                              \
	static ssize_t lpcm_##name##_show(struct device *dev,                  \
					  struct device_attribute *devattr,    \
					  char *buf)                           \
	{                                                                      \
		struct platform_device *pdev = to_platform_device(dev);        \
		struct google_powerdashboard *pd = platform_get_drvdata(pdev); \
		typeof(_lpcm_num)(lpcm_num) = (_lpcm_num);                     \
		if (google_read_section(pd, PD_LPCM))                          \
			return -EBUSY;                                             \
		return google_lpcm_show(pd, buf, _id, lpcm_num);               \
	}

#define POWER_STATE_BLOCKERS_NODE(name, _state)                             \
	static ssize_t blockers_##name##_show(struct device *dev,               \
					      struct device_attribute *devattr, \
					      char *buf)                        \
	{                                                                       \
		struct platform_device *pdev = to_platform_device(dev);         \
		struct google_powerdashboard *pd = platform_get_drvdata(pdev);  \
		typeof(_state)(state) = (_state);                               \
		if (google_read_section(pd, PD_POWER_STATE_BLOCKERS))           \
			return -EBUSY;                                              \
		return google_power_state_blockers_show(pd, buf, state);        \
	}

#define POWER_STATE_SIMPLIFIED_BLOCKERS_NODE(name, _state)                             \
	static ssize_t simplified_blockers_##name##_show(struct device *dev,               \
					      struct device_attribute *devattr, \
					      char *buf)                        \
	{                                                                       \
		struct platform_device *pdev = to_platform_device(dev);         \
		struct google_powerdashboard *pd = platform_get_drvdata(pdev);  \
		typeof(_state)(state) = (_state);                               \
		if (google_read_section(pd, PD_POWER_STATE_BLOCKERS))           \
			return -EBUSY;                                              \
		return google_power_state_simplified_blockers_show(pd, buf, state);        \
	}

#define FABRIC_ACG_APG_RES_NODE(name)						\
static ssize_t csr_##name##_show(struct device *dev,			\
				struct device_attribute *devattr, char *buf)	\
{											\
	struct platform_device *pdev = to_platform_device(dev);			\
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);		\
	if (google_read_section(pd, PD_ACG_APG_CSR_RES))	\
		return -EBUSY;			\
	struct pd_acg_apg_csr_res_section *acg_apg_csr_res_section =	\
		(struct pd_acg_apg_csr_res_section *)	\
		powerdashboard_iface.sections.acg_apg_csr_res_section;	\
	return sysfs_emit(buf, "%u\n",	\
				acg_apg_csr_res_section->fabric_acg_apg_res->name);	\
}

#define GMC_ACG_APG_RES_NODE(name)						\
static ssize_t csr_##name##_show(struct device *dev,			\
				struct device_attribute *devattr, char *buf)	\
{											\
	struct platform_device *pdev = to_platform_device(dev);			\
	struct google_powerdashboard *pd = platform_get_drvdata(pdev);		\
	if (google_read_section(pd, PD_ACG_APG_CSR_RES))	\
		return -EBUSY;							\
	struct pd_acg_apg_csr_res_section *acg_apg_csr_res_section =	\
		(struct pd_acg_apg_csr_res_section *)	\
		powerdashboard_iface.sections.acg_apg_csr_res_section;	\
	return sysfs_emit(buf, "%u\n",	\
				acg_apg_csr_res_section->gmc_acg_apg_res->name);	\
}

uint64_t get_ms_from_ticks(uint64_t ticks);

int google_read_section(struct google_powerdashboard *pd,
			enum pd_section section);

ssize_t google_pwrblk_show(char *buf, struct google_powerdashboard *pd,
			int id, int psm_num);

ssize_t google_lpcm_show(struct google_powerdashboard *pd, char *buf,
			int id, int lpcm_num);

bool plat_dvfs_helper_ready(void);

ssize_t google_power_state_blockers_show(struct google_powerdashboard *pd, char *buf,
			unsigned int state);

#endif /* __GOOGLE_POWERDASHBOARD_HELPER_H__ */
