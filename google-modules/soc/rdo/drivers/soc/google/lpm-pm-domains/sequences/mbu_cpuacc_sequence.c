// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/device.h>
#include <linux/io.h>

#include "sequence.h"

#define LPCM_REGION_IDX			0

#define CPUACC_LPM_OFFSET			0x0
#define CPUACC_LCM_OFFSET			0x10000
#define CPUACC_GPDMA_ACTIVE_SW_TRIGGER_OFFSET	(CPUACC_LPM_OFFSET + 0x1008)
#define CPUACC_SSU_ACTIVE_SW_TRIGGER_OFFSET	(CPUACC_LPM_OFFSET + 0x1010)
#define NS_PSM_GPDMA_PSM_STATUS_OFFSET	(CPUACC_LPM_OFFSET + 0x101c)
#define CG_CPUACC_GPDMA_MISC_CLK_OFFSET	(CPUACC_LCM_OFFSET + 0x840)
#define NS_PSM_SSU_PSM_STATUS_OFFSET	(CPUACC_LPM_OFFSET + 0x1024)

#define SUBDOMAIN_ON 0
#define SUBDOMAIN_OFF 2

static int cpuacc_gpdma_turn_on(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *addr = lpcm_addr + CPUACC_GPDMA_ACTIVE_SW_TRIGGER_OFFSET;
	void __iomem *status_addr = lpcm_addr + NS_PSM_GPDMA_PSM_STATUS_OFFSET;
	int ret;

	writel(1, addr);
	ret = poll_for_psm_state(pd, status_addr, SUBDOMAIN_ON);
	if (ret)
		return ret;

	return lcm_clk_en(pd, lpcm_addr + CG_CPUACC_GPDMA_MISC_CLK_OFFSET, true);
}

static int cpuacc_gpdma_turn_off(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *addr = lpcm_addr + CPUACC_GPDMA_ACTIVE_SW_TRIGGER_OFFSET;
	void __iomem *status_addr = lpcm_addr + NS_PSM_GPDMA_PSM_STATUS_OFFSET;
	int ret;

	ret = lcm_clk_en(pd, lpcm_addr + CG_CPUACC_GPDMA_MISC_CLK_OFFSET, false);
	if (ret)
		return ret;

	writel(0, addr);

	return poll_for_psm_state(pd, status_addr, SUBDOMAIN_OFF);
}

static int cpuacc_ssu_turn_on(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *status_addr = lpcm_addr + NS_PSM_SSU_PSM_STATUS_OFFSET;

	writel(1, lpcm_addr + CPUACC_SSU_ACTIVE_SW_TRIGGER_OFFSET);

	return poll_for_psm_state(pd, status_addr, SUBDOMAIN_ON);
}

static int cpuacc_ssu_turn_off(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *status_addr = lpcm_addr + NS_PSM_SSU_PSM_STATUS_OFFSET;

	writel(0, lpcm_addr + CPUACC_SSU_ACTIVE_SW_TRIGGER_OFFSET);

	return poll_for_psm_state(pd, status_addr, SUBDOMAIN_OFF);
}

static struct power_ops mbu_cpuacc_power_ops[] = {
	[GENPD_MBU_CPUACC_GPDMA_ID] = {
		.power_on = cpuacc_gpdma_turn_on,
		.power_off = cpuacc_gpdma_turn_off,
	},
	[GENPD_MBU_CPUACC_SSU_ID] = {
		.power_on = cpuacc_ssu_turn_on,
		.power_off = cpuacc_ssu_turn_off,
	},
};

static const char *const reg_names[] = {
	[LPCM_REGION_IDX] = "lpcm_cpuacc",
};

const struct sswrp_power_desc mbu_cpuacc_power_desc_table = {
	.reg_names = reg_names,
	.region_count = ARRAY_SIZE(reg_names),
	.power_ops_table = mbu_cpuacc_power_ops,
	.power_ops_table_size = ARRAY_SIZE(mbu_cpuacc_power_ops),
};
