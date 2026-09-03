/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __EXYNOS_CORESIGHT_H__
#define __EXYNOS_CORESIGHT_H__

#include <linux/kernel.h>

#define CORESIGHT_CPUPM_PRIORITY	(INT_MAX / 2)

#ifdef CONFIG_EXYNOS_CORESIGHT_ETR
int gs_coresight_etm_external_etr_on(u64 buf_addr, u32 buf_size);
int gs_coresight_etm_external_etr_off(void);
#else
static inline int gs_coresight_etm_external_etr_on(u64 buf_addr, u32 buf_size)
{
	return -EINVAL;
}

static inline int gs_coresight_etm_external_etr_off(void)
{
	return -EINVAL;
}
#endif

#endif /* __EXYNOS_CORESIGHT_H__ */
