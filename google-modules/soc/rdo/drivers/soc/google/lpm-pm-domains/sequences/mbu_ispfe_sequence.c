// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/device.h>
#include <linux/io.h>

#include "sequence.h"

#define LPCM_REGION_IDX 0

#define ISPFE_LPM_CONTROL_OFFSET 0x1004
#define SSWRP_TEARDOWN_TRIGGER_OFFSET 0x1000

/*
 * NOTES:
 *  - ISPFE_CSIS_DVDD0P75 belongs to CSIS_TOP PSM
 *  - ISPFE_TOP should not be managed, its status is a derivative of other PSMs
 *  - Control bits in structure are PG (power_gating), ie 0==turn on,
 *    1==turn_off corresponding domain
 */

// Status
#define NS_PSM_ISPFE_TOP_FSM_STATUS 0x1010
#define NS_PSM_ISPFE_CORE0_FSM_STATUS 0x1014
#define NS_PSM_ISPFE_CORE1_FSM_STATUS 0x1018
#define NS_PSM_ISPFE_CORE2_FSM_STATUS 0x101c
#define NS_PSM_ISPFE_CSIS_TOP_FSM_STATUS 0x1020
#define NS_PSM_ISPFE_CSIS_AVDD075_FSM_STATUS 0x1024

// PSM power states
#define CORE_ON 0
#define CORE_OFF 2
#define CSIS_DVDD0P75_ON 0
#define CSIS_DVDD0P75_OFF 1
#define CSIS_TOP_ON 1
#define CSIS_TOP_OFF 2
#define CSIS_AVDD075_ON 0
#define CSIS_AVDD075_OFF 2

struct ispfe_control {
	union {
		struct {
			u8 core0_pg_req : 1;
			u8 core1_pg_req : 1;
			u8 core2_pg_req : 1;
			u8 csis_csi2_pg_req : 1;
			u8 csis_pg_req : 1;
			u8 csis_avdd075_pg_req : 1;
			u32 reserved : 26;
		};
		u32 reg_val;
	};
};


static struct mutex ispfe_ctrl_lock = __MUTEX_INITIALIZER(ispfe_ctrl_lock);

static int core0_turn_on(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *status_addr = lpcm_addr + NS_PSM_ISPFE_CORE0_FSM_STATUS;

	mutex_lock(&ispfe_ctrl_lock);
	struct ispfe_control ispfe_ctrl = {
		.reg_val = readl(lpcm_addr + ISPFE_LPM_CONTROL_OFFSET)
	};

	ispfe_ctrl.core0_pg_req = 0;
	writel(ispfe_ctrl.reg_val, lpcm_addr + ISPFE_LPM_CONTROL_OFFSET);
	mutex_unlock(&ispfe_ctrl_lock);

	return poll_for_psm_state(pd, status_addr, CORE_ON);
}

static int core0_turn_off(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *status_addr = lpcm_addr + NS_PSM_ISPFE_CORE0_FSM_STATUS;

	mutex_lock(&ispfe_ctrl_lock);
	struct ispfe_control ispfe_ctrl = {
		.reg_val = readl(lpcm_addr + ISPFE_LPM_CONTROL_OFFSET)
	};

	ispfe_ctrl.core0_pg_req = 1;
	writel(ispfe_ctrl.reg_val, lpcm_addr + ISPFE_LPM_CONTROL_OFFSET);
	mutex_unlock(&ispfe_ctrl_lock);

	return poll_for_psm_state(pd, status_addr, CORE_OFF);
}

static int core1_turn_on(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *status_addr = lpcm_addr + NS_PSM_ISPFE_CORE1_FSM_STATUS;

	mutex_lock(&ispfe_ctrl_lock);
	struct ispfe_control ispfe_ctrl = {
		.reg_val = readl(lpcm_addr + ISPFE_LPM_CONTROL_OFFSET)
	};

	ispfe_ctrl.core1_pg_req = 0;
	writel(ispfe_ctrl.reg_val, lpcm_addr + ISPFE_LPM_CONTROL_OFFSET);
	mutex_unlock(&ispfe_ctrl_lock);

	return poll_for_psm_state(pd, status_addr, CORE_ON);
}

static int core1_turn_off(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *status_addr = lpcm_addr + NS_PSM_ISPFE_CORE1_FSM_STATUS;

	mutex_lock(&ispfe_ctrl_lock);
	struct ispfe_control ispfe_ctrl = {
		.reg_val = readl(lpcm_addr + ISPFE_LPM_CONTROL_OFFSET)
	};

	ispfe_ctrl.core1_pg_req = 1;
	writel(ispfe_ctrl.reg_val, lpcm_addr + ISPFE_LPM_CONTROL_OFFSET);
	mutex_unlock(&ispfe_ctrl_lock);

	return poll_for_psm_state(pd, status_addr, CORE_OFF);
}

static int core2_turn_on(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *status_addr = lpcm_addr + NS_PSM_ISPFE_CORE2_FSM_STATUS;

	mutex_lock(&ispfe_ctrl_lock);
	struct ispfe_control ispfe_ctrl = {
		.reg_val = readl(lpcm_addr + ISPFE_LPM_CONTROL_OFFSET)
	};

	ispfe_ctrl.core2_pg_req = 0;
	writel(ispfe_ctrl.reg_val, lpcm_addr + ISPFE_LPM_CONTROL_OFFSET);
	mutex_unlock(&ispfe_ctrl_lock);

	return poll_for_psm_state(pd, status_addr, CORE_ON);
}

static int core2_turn_off(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *status_addr = lpcm_addr + NS_PSM_ISPFE_CORE2_FSM_STATUS;

	mutex_lock(&ispfe_ctrl_lock);
	struct ispfe_control ispfe_ctrl = {
		.reg_val = readl(lpcm_addr + ISPFE_LPM_CONTROL_OFFSET)
	};

	ispfe_ctrl.core2_pg_req = 1;
	writel(ispfe_ctrl.reg_val, lpcm_addr + ISPFE_LPM_CONTROL_OFFSET);
	mutex_unlock(&ispfe_ctrl_lock);

	return poll_for_psm_state(pd, status_addr, CORE_OFF);
}

// CSIS top and subdomains are controlled together
static int csis_turn_on(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *top_status_addr =
		lpcm_addr + NS_PSM_ISPFE_CSIS_TOP_FSM_STATUS;
	void __iomem *avdd_status_addr =
		lpcm_addr + NS_PSM_ISPFE_CSIS_AVDD075_FSM_STATUS;
	int ret;

	// Turn on AVDD first
	mutex_lock(&ispfe_ctrl_lock);
	struct ispfe_control ispfe_ctrl = {
		.reg_val = readl(lpcm_addr + ISPFE_LPM_CONTROL_OFFSET)
	};
	ispfe_ctrl.csis_avdd075_pg_req = 0;
	writel(ispfe_ctrl.reg_val, lpcm_addr + ISPFE_LPM_CONTROL_OFFSET);
	mutex_unlock(&ispfe_ctrl_lock);

	ret = poll_for_psm_state(pd, avdd_status_addr, CSIS_AVDD075_ON);
	if (ret)
		return ret;

	// Turn on TOP and DVDD together, it will be sequenced by LPCM
	mutex_lock(&ispfe_ctrl_lock);
	ispfe_ctrl.reg_val = readl(lpcm_addr + ISPFE_LPM_CONTROL_OFFSET);
	ispfe_ctrl.csis_pg_req = 0;
	ispfe_ctrl.csis_csi2_pg_req = 0;
	writel(ispfe_ctrl.reg_val, lpcm_addr + ISPFE_LPM_CONTROL_OFFSET);
	mutex_unlock(&ispfe_ctrl_lock);

	return poll_for_psm_state(pd, top_status_addr, CSIS_DVDD0P75_ON);
}

static int csis_turn_off(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	void __iomem *top_status_addr =
		lpcm_addr + NS_PSM_ISPFE_CSIS_TOP_FSM_STATUS;
	void __iomem *avdd_status_addr =
		lpcm_addr + NS_PSM_ISPFE_CSIS_AVDD075_FSM_STATUS;
	int ret;

	// Turn off TOP and DVDD together, it will be sequenced by LPCM
	mutex_lock(&ispfe_ctrl_lock);
	struct ispfe_control ispfe_ctrl = {
		.reg_val = readl(lpcm_addr + ISPFE_LPM_CONTROL_OFFSET)
	};
	ispfe_ctrl.csis_pg_req = 1;
	ispfe_ctrl.csis_csi2_pg_req = 1;
	writel(ispfe_ctrl.reg_val, lpcm_addr + ISPFE_LPM_CONTROL_OFFSET);
	mutex_unlock(&ispfe_ctrl_lock);

	ret = poll_for_psm_state(pd, top_status_addr, CSIS_TOP_OFF);
	if (ret)
		return ret;

	// Turn off AVDD
	mutex_lock(&ispfe_ctrl_lock);
	ispfe_ctrl.reg_val = readl(lpcm_addr + ISPFE_LPM_CONTROL_OFFSET);
	ispfe_ctrl.csis_avdd075_pg_req = 1;
	writel(ispfe_ctrl.reg_val, lpcm_addr + ISPFE_LPM_CONTROL_OFFSET);
	mutex_unlock(&ispfe_ctrl_lock);

	return poll_for_psm_state(pd, avdd_status_addr, CSIS_AVDD075_OFF);
}

static struct power_ops mbu_ispfe_power_ops[] = {
	[GENPD_MBU_ISPFE_CORE0_ID] = {
		.power_on = core0_turn_on,
		.power_off = core0_turn_off,
	},
	[GENPD_MBU_ISPFE_CORE1_ID] = {
		.power_on = core1_turn_on,
		.power_off = core1_turn_off,
	},
	[GENPD_MBU_ISPFE_CORE2_ID] = {
		.power_on = core2_turn_on,
		.power_off = core2_turn_off,
	},
	[GENPD_MBU_ISPFE_CSIS_ID] = {
		.power_on = csis_turn_on,
		.power_off = csis_turn_off,
	},
};

static const char *const reg_names[] = {
	[LPCM_REGION_IDX] = "lpcm_ispfe",
};

const struct sswrp_power_desc mbu_ispfe_power_desc_table = {
	.reg_names = reg_names,
	.region_count = ARRAY_SIZE(reg_names),
	.power_ops_table = mbu_ispfe_power_ops,
	.power_ops_table_size = ARRAY_SIZE(mbu_ispfe_power_ops),
};
