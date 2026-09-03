// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "sequence.h"

#define LPCM_REGION_IDX				0

#define PCIE_LPM_OFFSET				0x0
#define PCIE_LCM_OFFSET				0x10000

/* PCIe Common Top region offsets */
#define APP_CONTROL				0x0
#define PORT_ENABLE				BIT(3)

#define PERST_RESET				0x4

// Triggers
#define TOP_PWR_EN_OFFSET			(PCIE_LPM_OFFSET + 0x1004)
#define OLS_0P75_DISABLE_OFFSET			(PCIE_LPM_OFFSET + 0x438)
#define OLS_1P2_DISABLE_OFFSET			(PCIE_LPM_OFFSET + 0x43c)
#define OLS_0P75_READY_OFFSET			(PCIE_LPM_OFFSET + 0x440)
#define OLS_1P2_READY_OFFSET			(PCIE_LPM_OFFSET + 0x444)
#define CTRL0_SW_ON_OFFSET			(PCIE_LPM_OFFSET + 0x100C)
#define CTRL1_SW_ON_OFFSET			(PCIE_LPM_OFFSET + 0x1010)
#define TOP_SS_PGM_FINISH_OFFSET		(PCIE_LPM_OFFSET + 0x1024)
#define PLL_OFF_EN				(PCIE_LPM_OFFSET + 0x1028)
#define FORCE_DPA_OFF_CSR_OFFSET		(PCIE_LPM_OFFSET + 0x102c)
#define CSR_DPA_ON_OFFSET			(PCIE_LPM_OFFSET + 0xC000)

// Status
#define NS_PSM_PCIE_TOP_PSM_STS_OFFSET		(PCIE_LPM_OFFSET + 0x103C)
#define NS_PSM_PCIE_CTRL0_PSM_STS_OFFSET	(PCIE_LPM_OFFSET + 0x1034)
#define NS_PSM_PCIE_CTRL1_PSM_STS_OFFSET	(PCIE_LPM_OFFSET + 0x1038)
#define NS_PSM_DPA_PSM_STS_OFFSET		(PCIE_LPM_OFFSET + 0x1040)

// Clocks
#define QCH_MODE_OFFSET(clk_base)		((clk_base) + 0x8)
#define CG_DPA_IPSEC_CLK_BASE			(PCIE_LCM_OFFSET + 0x600)
#define CG_DPA_CLK_BASE				(PCIE_LCM_OFFSET + 0x20C0)
#define CG_SSWRP_AUX_DPA_AON_CLK_BASE		(PCIE_LCM_OFFSET + 0x2140)
#define CG_SSWRP_AUX_DPA_CFG_CLK_BASE		(PCIE_LCM_OFFSET + 0x2160)

static int pcie_power_on(struct power_domain *pd, u32 controller_idx,
			 u32 trigger_offset, u32 status_offset)
{
	u32 reg;

	/* Trigger the PSM transition */
	writel(1, pd->regions[LPCM_REGION_IDX] + trigger_offset);
	/* Set PORT_ENABLE */
	regmap_read(pd->syscons[controller_idx], APP_CONTROL, &reg);
	reg |= PORT_ENABLE;
	regmap_write(pd->syscons[controller_idx], APP_CONTROL, reg);
	/*
	 * Wait for Controller PSW Domain to be turned on. There's no register
	 * to poll, so we use a fixed delay.
	 */
	udelay(7);

	/* De-Assert perst */
	regmap_write(pd->syscons[controller_idx], PERST_RESET, 1);
	/* Wait for PSM to enter PS0 */
	return poll_for_psm_state(pd, pd->regions[LPCM_REGION_IDX] + status_offset, 0);
}

static int pcie_power_off(struct power_domain *pd, u32 controller_idx,
			  u32 trigger_offset, u32 status_offset)
{
	u32 reg;

	/* Assert perst */
	regmap_write(pd->syscons[controller_idx], PERST_RESET, 0);
	/* Clear PORT_EN */
	regmap_read(pd->syscons[controller_idx], APP_CONTROL, &reg);
	reg &= ~PORT_ENABLE;
	regmap_write(pd->syscons[controller_idx], APP_CONTROL, reg);
	/* Trigger a PSM transition to PS2 */
	writel(0, pd->regions[LPCM_REGION_IDX] + trigger_offset);
	return poll_for_psm_state(pd, pd->regions[LPCM_REGION_IDX] + status_offset, 2);
}

static int pcie_top_power_on(struct power_domain *pd)
{
	int ret;
	void __iomem *lpcm_addr;

	lpcm_addr = pd->regions[LPCM_REGION_IDX];
	writel(0, lpcm_addr + OLS_0P75_DISABLE_OFFSET);
	writel(0, lpcm_addr + OLS_1P2_DISABLE_OFFSET);
	writel(1, lpcm_addr + OLS_0P75_READY_OFFSET);
	writel(1, lpcm_addr + OLS_1P2_READY_OFFSET);
	writel(1, lpcm_addr + TOP_PWR_EN_OFFSET);
	writel(1, lpcm_addr + TOP_SS_PGM_FINISH_OFFSET);

	ret = poll_for_psm_state(pd, lpcm_addr + NS_PSM_PCIE_TOP_PSM_STS_OFFSET, 0);
	if (ret)
		return ret;

	writel(1, lpcm_addr + PLL_OFF_EN);
	return 0;
}

static int pcie_top_power_off(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];

	writel(0, lpcm_addr + TOP_SS_PGM_FINISH_OFFSET);
	writel(0, lpcm_addr + TOP_PWR_EN_OFFSET);
	writel(0, lpcm_addr + OLS_1P2_READY_OFFSET);
	writel(0, lpcm_addr + OLS_0P75_READY_OFFSET);

	return poll_for_psm_state(pd, lpcm_addr + NS_PSM_PCIE_TOP_PSM_STS_OFFSET, 2);
}

static int pcie_ctrl0_power_on(struct power_domain *pd)
{
	return pcie_power_on(pd, 0, CTRL0_SW_ON_OFFSET, NS_PSM_PCIE_CTRL0_PSM_STS_OFFSET);
}

static int pcie_ctrl0_power_off(struct power_domain *pd)
{
	return pcie_power_off(pd, 0, CTRL0_SW_ON_OFFSET, NS_PSM_PCIE_CTRL0_PSM_STS_OFFSET);
}

static int pcie_ctrl1_power_on(struct power_domain *pd)
{
	return pcie_power_on(pd, 1, CTRL1_SW_ON_OFFSET, NS_PSM_PCIE_CTRL1_PSM_STS_OFFSET);
}

static int pcie_ctrl1_power_off(struct power_domain *pd)
{
	return pcie_power_off(pd, 1, CTRL1_SW_ON_OFFSET, NS_PSM_PCIE_CTRL1_PSM_STS_OFFSET);
}

static int pcie_dpa_power_on(struct power_domain *pd)
{
	/* TODO(b/369258778): Implemenat DPA power_on in power-controller driver */

	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	int ret;
	u32 ps_val;

	ps_val = readl(lpcm_addr + NS_PSM_DPA_PSM_STS_OFFSET) & 0xf;
	if (ps_val != 0x2) {
		/*
		 * Program LPM when DPA is OFF(PS2). The power_off function of
		 * DPA does not actually turn off DPA.
		 * DPA is turned off when synced_poweroff is requested.
		 **/
		return 0;
	}

	writel(1, lpcm_addr + CSR_DPA_ON_OFFSET);
	ret = poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPA_PSM_STS_OFFSET, 1);
	writel(0, lpcm_addr + CSR_DPA_ON_OFFSET);

	return ret;
}

static int pcie_dpa_force_power_off(struct power_domain *pd)
{
	void __iomem *lpcm_addr = pd->regions[LPCM_REGION_IDX];
	int ret;

	writel(0, lpcm_addr + QCH_MODE_OFFSET(CG_DPA_CLK_BASE));
	writel(0, lpcm_addr + QCH_MODE_OFFSET(CG_DPA_IPSEC_CLK_BASE));
	writel(0, lpcm_addr + QCH_MODE_OFFSET(CG_SSWRP_AUX_DPA_AON_CLK_BASE));
	writel(0, lpcm_addr + QCH_MODE_OFFSET(CG_SSWRP_AUX_DPA_CFG_CLK_BASE));

	writel(1, lpcm_addr + FORCE_DPA_OFF_CSR_OFFSET);
	ret = poll_for_psm_state(pd, lpcm_addr + NS_PSM_DPA_PSM_STS_OFFSET, 2);
	writel(0, lpcm_addr + FORCE_DPA_OFF_CSR_OFFSET);

	writel(1, lpcm_addr + QCH_MODE_OFFSET(CG_DPA_CLK_BASE));
	writel(1, lpcm_addr + QCH_MODE_OFFSET(CG_DPA_IPSEC_CLK_BASE));
	writel(1, lpcm_addr + QCH_MODE_OFFSET(CG_SSWRP_AUX_DPA_AON_CLK_BASE));
	writel(1, lpcm_addr + QCH_MODE_OFFSET(CG_SSWRP_AUX_DPA_CFG_CLK_BASE));

	return ret;
}

static int pcie_dpa_power_off(struct power_domain *pd)
{
	/* TODO(b/369258778): Implemenat DPA power_off in power-controller driver */
	if (pd->genpd.synced_poweroff) {
		/*
		 * Force power off DPA. This is used when Linux want to
		 * - shutdown DPA
		 * - reboot DPA when DPA firmware crashes
		 **/
		return pcie_dpa_force_power_off(pd);
	}
	/*
	 * The normal power off means the power will be eventually
	 * powered off. DPA itself decides when to go to the low power
	 * state. Do nothing because Linux does not have control over
	 * it without CPM's help.
	 **/
	return 0;
}

static struct power_ops mbu_pcie_power_ops[] = {
	[GENPD_MBU_PCIE_CTRL0_ID] = {
		.power_on = pcie_ctrl0_power_on,
		.power_off = pcie_ctrl0_power_off,
	},
	[GENPD_MBU_PCIE_CTRL1_ID] = {
		.power_on = pcie_ctrl1_power_on,
		.power_off = pcie_ctrl1_power_off,
	},
	[GENPD_MBU_PCIE_TOP_ID] = {
		.power_on = pcie_top_power_on,
		.power_off = pcie_top_power_off,
	},
	[GENPD_MBU_PCIE_DPA_ID] = {
		.power_on = pcie_dpa_power_on,
		.power_off = pcie_dpa_power_off,
	},
};

static const char *const reg_names[] = {
	[LPCM_REGION_IDX] = "lpcm_pcie",
};

const struct sswrp_power_desc mbu_pcie_power_desc_table = {
	.reg_names = reg_names,
	.region_count = ARRAY_SIZE(reg_names),
	.power_ops_table = mbu_pcie_power_ops,
	.power_ops_table_size = ARRAY_SIZE(mbu_pcie_power_ops),
	.syscon_name = "pcie-regs",
	.syscon_count = 2,
};
