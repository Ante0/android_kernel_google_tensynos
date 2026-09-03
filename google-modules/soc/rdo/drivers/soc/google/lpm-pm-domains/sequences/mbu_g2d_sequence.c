// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include "sequence.h"

#define LPCM_REGION_IDX			0

#define NS_G2D_CONTROL 0x1000
#define NS_G2D_PS_STATUS 0x1004

static int g2d_core_turn_on(struct power_domain *pd)
{
	int ret;
	int val;
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];

	writel(0x1, lpcm_addr + NS_G2D_CONTROL);

	ret = readl_poll_timeout(lpcm_addr + NS_G2D_PS_STATUS, val, val == 0x1,
		0, 1000);

	return ret;
}

static int g2d_core_turn_off(struct power_domain *pd)
{
	int ret;
	int val;
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];

	writel(0x0, lpcm_addr + NS_G2D_CONTROL);

	ret = readl_poll_timeout(lpcm_addr + NS_G2D_PS_STATUS, val, val == 0x0,
		0, 1000);

	return ret;
}

static struct power_ops g2d_power_ops[] = {
	[GENPD_MBU_G2D_CORE_ID] = {
		.power_on = g2d_core_turn_on,
		.power_off = g2d_core_turn_off,
	},
};

static const char *const reg_names[] = {
	[LPCM_REGION_IDX] = "lpcm_g2d",
};

const struct sswrp_power_desc mbu_g2d_power_desc_table = {
	.reg_names = reg_names,
	.region_count = ARRAY_SIZE(reg_names),
	.power_ops_table = g2d_power_ops,
	.power_ops_table_size = ARRAY_SIZE(g2d_power_ops),
};
