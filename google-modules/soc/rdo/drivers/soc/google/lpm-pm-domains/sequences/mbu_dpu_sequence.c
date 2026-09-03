// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 *
 * Unlike most other SSWRPs, DPU has dependencies between PSM<n> and PSM0.
 * When SW writes to the SW trigger, that lets PSM<n> transition to a p-state,
 * but as a result of that, PSM0 might also transition into a different p-state.
 * Furthermore, depending on other power domains' status, PSM0 might transition
 * into different p-states. Therefore, to address this nature, DPU power-on/off
 * sequences poll from PSM<n>'s status CSR first, and then polls from PSM0's
 * status CSR only if applicable.
 */

#include <asm-generic/errno.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include "sequence.h"

#define LPCM_REGION_IDX			0

#define DPU_LPM_OFFSET			0x0
#define NS_DPU_CONTROL_OFFSET		(DPU_LPM_OFFSET + 0x1000)
#define NS_PSM_DPU_TOP_FSM_STATUS	(DPU_LPM_OFFSET + 0x100C)
#define NS_PSM_DPU_BE_FSM_STATUS	(DPU_LPM_OFFSET + 0x1010)
#define NS_PSM_DPU_FE_FSM_STATUS	(DPU_LPM_OFFSET + 0x1014)
#define NS_PSM_DPU_DSI_FSM_STATUS	(DPU_LPM_OFFSET + 0x1018)
#define NS_PSM_DPU_DP_FSM_STATUS	(DPU_LPM_OFFSET + 0x1018)

/*
 * The table below describes the underlying state the PSM supports
 *	  | DSI0 | DSI1
 *  State |-------------
 *	0 | On   | On
 *	1 | On   | Off
 *	2 | Off  | On
 *	3 | Off  | Off
 */
#define DPU_DSI_BOTH_ON_STATE (0x0)
#define DPU_ONLY_DSI0_ON_STATE (0x1)
#define DPU_ONLY_DSI1_ON_STATE (0x2)
#define DPU_DSI_BOTH_OFF_STATE (0x3)

struct ns_dpu_control {
	union {
		struct {
			u8 cfg_dpu_fe0_en : 1;
			u8 cfg_dpu_fe1_en : 1;
			u8 cfg_dpu_be_en : 1;
			u8 cfg_dpu_dsi0_en : 1;
			u8 cfg_dpu_dsi1_en : 1;
			u8 cfg_dpu_dp0_en : 1;
			u32 reserved : 26;
		};
		u32 reg_val;
	};
};

static struct mutex dpu_ctrl_lock = __MUTEX_INITIALIZER(dpu_ctrl_lock);

static void dpu_turn_on(struct power_domain *pd, struct ns_dpu_control *ctrl)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	struct ns_dpu_control ns_dpu_ctrl;

	mutex_lock(&dpu_ctrl_lock);
	ns_dpu_ctrl.reg_val = readl(lpcm_addr + NS_DPU_CONTROL_OFFSET);
	ns_dpu_ctrl.reg_val |= ctrl->reg_val;
	writel(ns_dpu_ctrl.reg_val, lpcm_addr + NS_DPU_CONTROL_OFFSET);
	mutex_unlock(&dpu_ctrl_lock);
}

static void dpu_turn_off(struct power_domain *pd, struct ns_dpu_control *ctrl)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	struct ns_dpu_control ns_dpu_ctrl;

	mutex_lock(&dpu_ctrl_lock);
	ns_dpu_ctrl.reg_val = readl(lpcm_addr + NS_DPU_CONTROL_OFFSET);
	ns_dpu_ctrl.reg_val &= ~ctrl->reg_val;
	writel(ns_dpu_ctrl.reg_val, lpcm_addr + NS_DPU_CONTROL_OFFSET);
	mutex_unlock(&dpu_ctrl_lock);
}

static int dpu_turn_on_be(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	struct ns_dpu_control ns_dpu_ctrl = { .cfg_dpu_be_en = 1 };
	int ret;

	dpu_turn_on(pd, &ns_dpu_ctrl);
	ret = poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPU_BE_FSM_STATUS, 3);
	if (ret < 0)
		return ret;
	return poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPU_TOP_FSM_STATUS, 5);
}

static int dpu_turn_off_be(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	struct ns_dpu_control ns_dpu_ctrl = { .cfg_dpu_be_en = 1 };
	int ret;

	dpu_turn_off(pd, &ns_dpu_ctrl);
	ret = poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPU_BE_FSM_STATUS, 4);
	if (ret < 0)
		return ret;
	return poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPU_TOP_FSM_STATUS, 4);
}

static int dpu_turn_on_fe0(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	struct ns_dpu_control ns_dpu_ctrl = { .cfg_dpu_fe0_en = 1 };
	int ret;

	dpu_turn_on(pd, &ns_dpu_ctrl);
	ret = poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPU_FE_FSM_STATUS, 1);
	if (ret < 0)
		return ret;
	return poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPU_TOP_FSM_STATUS, 6);
}

static int dpu_turn_off_fe0(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	struct ns_dpu_control ns_dpu_ctrl = { .cfg_dpu_fe0_en = 1 };

	dpu_turn_off(pd, &ns_dpu_ctrl);
	return poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPU_FE_FSM_STATUS, 2);
}

static int dpu_turn_on_fe1(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	struct ns_dpu_control ns_dpu_ctrl = { .cfg_dpu_fe1_en = 1 };

	dpu_turn_on(pd, &ns_dpu_ctrl);
	return poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPU_FE_FSM_STATUS, 0);
}

static int dpu_turn_off_fe1(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	struct ns_dpu_control ns_dpu_ctrl = { .cfg_dpu_fe1_en = 1 };
	int ret;

	dpu_turn_off(pd, &ns_dpu_ctrl);
	ret = poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPU_FE_FSM_STATUS, 1);
	if (ret < 0)
		return ret;
	return poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPU_TOP_FSM_STATUS, 6);
}

static int poll_for_dpu_dsi_state(struct power_domain *pd, u32 target1,
				  u32 target2)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	struct csr_psm_status val;
	int ret;
	u32 timeout_us = 1000;

	if (get_emulation_flag())
		timeout_us *= EMULATION_TIMEOUT_MULTIPLIER;

	ret = readl_poll_timeout(lpcm_addr + NS_PSM_DPU_DSI_FSM_STATUS,
				 val.reg_val,
				 (val.curr_state == target1) || (val.curr_state == target2),
				 0, timeout_us);

	if (ret != 0) {
		panic("%s: Poll timeout (val: %#x, psm_sts: %#x, target1: %#x target2: %#x)\n",
		      pd->genpd.name, val.curr_state, val.psm_sts, target1,
		      target2);
	}

	return 0;
}

static int dpu_dsi_populate_ctrl(u32 subdomain_id,
				 struct ns_dpu_control *ns_dpu_ctrl)
{
	int ret = 0;

	if (subdomain_id == GENPD_MBU_DPU_DSI0_ID)
		ns_dpu_ctrl->cfg_dpu_dsi0_en = 1;
	else if (subdomain_id == GENPD_MBU_DPU_DSI1_ID)
		ns_dpu_ctrl->cfg_dpu_dsi1_en = 1;
	else
		ret = -ENODEV;

	return ret;
}

static int dpu_dsi_get_target_state(u32 subdomain_id, bool power_on,
				    u32 *target)
{
	int ret = 0;

	if (subdomain_id == GENPD_MBU_DPU_DSI0_ID)
		*target = power_on ? DPU_ONLY_DSI0_ON_STATE : DPU_ONLY_DSI1_ON_STATE;
	else if (subdomain_id == GENPD_MBU_DPU_DSI1_ID)
		*target = power_on ? DPU_ONLY_DSI1_ON_STATE : DPU_ONLY_DSI0_ON_STATE;
	else
		ret = -ENODEV;

	return ret;
}

static int dpu_turn_on_dsi(struct power_domain *pd)
{
	u32 subdomain_id = pd->subdomain_id;
	u32 target;
	int ret;
	struct ns_dpu_control ns_dpu_ctrl = { 0 };

	ret = dpu_dsi_populate_ctrl(subdomain_id, &ns_dpu_ctrl);
	if (ret)
		return ret;

	ret = dpu_dsi_get_target_state(subdomain_id, true, &target);
	if (ret)
		return ret;

	dpu_turn_on(pd, &ns_dpu_ctrl);

	return poll_for_dpu_dsi_state(pd, target, DPU_DSI_BOTH_ON_STATE);
}

static int dpu_turn_off_dsi(struct power_domain *pd)
{
	u32 subdomain_id = pd->subdomain_id;
	u32 target;
	int ret;
	struct ns_dpu_control ns_dpu_ctrl = { 0 };

	ret = dpu_dsi_populate_ctrl(subdomain_id, &ns_dpu_ctrl);
	if (ret)
		return ret;

	ret = dpu_dsi_get_target_state(subdomain_id, false, &target);
	if (ret)
		return ret;

	dpu_turn_off(pd, &ns_dpu_ctrl);

	return poll_for_dpu_dsi_state(pd, target, DPU_DSI_BOTH_OFF_STATE);
}

static int dpu_turn_on_dp(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	struct ns_dpu_control ns_dpu_ctrl = { .cfg_dpu_dp0_en = 1 };

	dpu_turn_on(pd, &ns_dpu_ctrl);
	return poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPU_BE_FSM_STATUS, 0);
}

static int dpu_turn_off_dp(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	struct ns_dpu_control ns_dpu_ctrl = { .cfg_dpu_dp0_en = 1 };

	dpu_turn_off(pd, &ns_dpu_ctrl);
	return poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPU_BE_FSM_STATUS, 3);
}

static struct power_ops mbu_dpu_power_ops[] = {
	[GENPD_MBU_DPU_BE_ID] = {
		.power_on = dpu_turn_on_be,
		.power_off = dpu_turn_off_be,
	},
	[GENPD_MBU_DPU_FE0_ID] = {
		.power_on = dpu_turn_on_fe0,
		.power_off = dpu_turn_off_fe0,
	},
	[GENPD_MBU_DPU_FE1_ID] = {
		.power_on = dpu_turn_on_fe1,
		.power_off = dpu_turn_off_fe1,
	},
	[GENPD_MBU_DPU_DSI0_ID] = {
		.power_on = dpu_turn_on_dsi,
		.power_off = dpu_turn_off_dsi,
	},
	[GENPD_MBU_DPU_DSI1_ID] = {
		.power_on = dpu_turn_on_dsi,
		.power_off = dpu_turn_off_dsi,
	},
	[GENPD_MBU_DPU_DP0_ID] = {
		.power_on = dpu_turn_on_dp,
		.power_off = dpu_turn_off_dp,
	},
};

static const char *const reg_names[] = {
	[LPCM_REGION_IDX] = "lpcm_dpu",
};

const struct sswrp_power_desc mbu_dpu_power_desc_table = {
	.reg_names = reg_names,
	.region_count = ARRAY_SIZE(reg_names),
	.power_ops_table = mbu_dpu_power_ops,
	.power_ops_table_size = ARRAY_SIZE(mbu_dpu_power_ops),
};
