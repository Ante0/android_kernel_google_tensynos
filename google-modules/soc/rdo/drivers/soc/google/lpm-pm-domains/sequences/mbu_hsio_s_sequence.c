// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/device.h>
#include <linux/io.h>

#include "sequence.h"

#define LPCM_REGION_IDX				0

#define HSIO_S_LPM_OFFSET			0x0

// Triggers
#define UFS_TOP_PWR_OFF_OFFSET			(HSIO_S_LPM_OFFSET + 0x1004)
#define UFS_FSM_EN_OFFSET			(HSIO_S_LPM_OFFSET + 0x1014)
#define SD_TOP_PWR_EN_OFFSET			(HSIO_S_LPM_OFFSET + 0x1018)

// Status
#define NS_PSM_UFS_TOP_HC_PSM_STATUS_OFFSET	(HSIO_S_LPM_OFFSET + 0x1020)
#define NS_PSM_SD_PSM_STATUS_OFFSET		(HSIO_S_LPM_OFFSET + 0x1028)

// PSM power states
#define UFS_TOP_ON 0
#define UFS_TOP_OFF 3
#define SD_ON 0
#define SD_OFF 1

static int hsio_s_ufs_top_turn_on(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *status_addr = lpcm_addr + NS_PSM_UFS_TOP_HC_PSM_STATUS_OFFSET;

	writel(0, lpcm_addr + UFS_TOP_PWR_OFF_OFFSET);
	writel(1, lpcm_addr + UFS_FSM_EN_OFFSET);

	return poll_for_psm_state(pd, status_addr, UFS_TOP_ON);
}

static int hsio_s_ufs_top_turn_off(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *status_addr = lpcm_addr + NS_PSM_UFS_TOP_HC_PSM_STATUS_OFFSET;

	writel(1, lpcm_addr + UFS_TOP_PWR_OFF_OFFSET);

	return poll_for_psm_state(pd, status_addr, UFS_TOP_OFF);
}

static int hsio_s_sd_turn_on(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *status_addr = lpcm_addr + NS_PSM_SD_PSM_STATUS_OFFSET;

	writel(1, lpcm_addr + SD_TOP_PWR_EN_OFFSET);

	return poll_for_psm_state(pd, status_addr, SD_ON);
}

static int hsio_s_sd_turn_off(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *status_addr = lpcm_addr + NS_PSM_SD_PSM_STATUS_OFFSET;

	writel(0, lpcm_addr + SD_TOP_PWR_EN_OFFSET);

	return poll_for_psm_state(pd, status_addr, SD_OFF);
}

static struct power_ops mbu_hsio_s_power_ops[] = {
	[GENPD_MBU_HSIO_S_UFS_ID] = {
		.power_on = hsio_s_ufs_top_turn_on,
		.power_off = hsio_s_ufs_top_turn_off,
	},
	[GENPD_MBU_HSIO_S_SD_ID] = {
		.power_on = hsio_s_sd_turn_on,
		.power_off = hsio_s_sd_turn_off,
	},
};

static const char *const reg_names[] = {
	[LPCM_REGION_IDX] = "lpcm_hsio_s",
};

const struct sswrp_power_desc mbu_hsio_s_power_desc_table = {
	.reg_names = reg_names,
	.region_count = ARRAY_SIZE(reg_names),
	.power_ops_table = mbu_hsio_s_power_ops,
	.power_ops_table_size = ARRAY_SIZE(mbu_hsio_s_power_ops),
};
