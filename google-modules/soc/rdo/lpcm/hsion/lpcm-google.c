// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022 Google LLC
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/module.h>

#define LPCM_STATUS_TIMEOUT_US	10000
#define LPCM_STATUS_INTERVAL_US	10
#define PSM_STATUS_NUM_RETRIES	30
#define STABLE_POWER_DELAY_US	10000

#define LPM_OFFSET		0x0
#define TOP_PWR_EN_OFFSET	0x404
#define DBG_PCS_PWR_EN_OFFSET	0x40c
#define PWR_EN			BIT(0)

#define PSM_1_OFFSET		0x2000
#define PSM_1_STATUS_OFFSET	0x260
#define CURR_STATE		0xf
#define PS0			0
#define STATE_VALID		BIT(4)

#define LCM_OFFSET		0x10000
#define PCIE0_PERST_OFFSET	0x7a0
#define APP_HOLD_PHY_OFFSET	0x7c0
#define APP_INIT_OFFSET		0x7e0
#define PCIE0_BUTTON_OFFSET	0x800
#define PCIE0_POWER_UP_OFFSET	0x820
#define CG_REG_FALSE_OFFSET	0x960
#define PCIE0_SLV_ACLK_OFFSET	0x9a0
#define PCIE0_DBI_ACLK_OFFSET	0x9e0
#define PCIE0_APB_CLK_OFFSET	0xa20
#define TOP_CSR_CLK_OFFSET	0xa60
#define CG_AUX_CLK_OFFSET	0xa80

#define CTRL_MODE_OFFSET	0x0
#define CTRL_MODE_HW		BIT(0)

#define CLK_EN_OFFSET		0x4
#define CLK_EN			BIT(0)

#define RST_VAL_OFFSET		0xc
#define RST_DEASSERT		BIT(0)
#define RET_RST_DEASSERT	BIT(8)

#define GATE_STATUS_OFFSET	0x1c
#define CLK_STATUS_ON		BIT(8)
#define RST_STATUS_DEASSERTED	BIT(12)

enum hsion_clks {
	PCIE_CLK,
	MAX_CLKS,
};

static const char * const clk_names[] = {
	[PCIE_CLK] = "pcie_clk",
};

struct google_lpcm {
	void __iomem *base;
	struct clk_hw pcie_clk_hw;
	struct clk_hw usb_clk_hw;
};

static int lcm_configure_cg(struct google_lpcm *g_lpcm, u64 cg_offset)
{
	u64 offset = LCM_OFFSET + cg_offset;
	int ret = 0;
	u32 val;

	val = readl(g_lpcm->base + offset + CTRL_MODE_OFFSET);
	val &= ~CTRL_MODE_HW;
	writel(val, g_lpcm->base + offset + CTRL_MODE_OFFSET);

	val = readl(g_lpcm->base + offset + CLK_EN_OFFSET);
	val |= CLK_EN;
	writel(val, g_lpcm->base + offset + CLK_EN_OFFSET);

	ret = readl_poll_timeout_atomic(g_lpcm->base + offset + GATE_STATUS_OFFSET,
					val, val & CLK_STATUS_ON,
					LPCM_STATUS_INTERVAL_US, LPCM_STATUS_TIMEOUT_US);

	val = readl(g_lpcm->base + offset + CTRL_MODE_OFFSET);
	val |= CTRL_MODE_HW;
	writel(val, g_lpcm->base + offset + CTRL_MODE_OFFSET);

	return ret;
}

static int lcm_configure_rst(struct google_lpcm *g_lpcm, u64 rst_offset)
{
	u64 offset = LCM_OFFSET + rst_offset;
	int ret = 0;
	u32 val;

	val = readl(g_lpcm->base + offset + CTRL_MODE_OFFSET);
	val &= ~CTRL_MODE_HW;
	writel(val, g_lpcm->base + offset + CTRL_MODE_OFFSET);

	val = readl(g_lpcm->base + offset + RST_VAL_OFFSET);
	val = (val | RST_DEASSERT) & ~RET_RST_DEASSERT;
	writel(val, g_lpcm->base + offset + RST_VAL_OFFSET);

	ret = readl_poll_timeout_atomic(g_lpcm->base + offset + GATE_STATUS_OFFSET,
					val, val & RST_STATUS_DEASSERTED,
					LPCM_STATUS_INTERVAL_US, LPCM_STATUS_TIMEOUT_US);

	val = readl(g_lpcm->base + offset + CTRL_MODE_OFFSET);
	val |= CTRL_MODE_HW;
	writel(val, g_lpcm->base + offset + CTRL_MODE_OFFSET);

	return ret;
}

static int pcie_clk_enable(struct clk_hw *hw)
{
	struct google_lpcm *g_lpcm = container_of(hw, struct google_lpcm, pcie_clk_hw);
	int ret;

	ret = lcm_configure_cg(g_lpcm, PCIE0_BUTTON_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_rst(g_lpcm, PCIE0_BUTTON_OFFSET);
	if (ret)
		return ret;

	udelay(STABLE_POWER_DELAY_US);
	ret = lcm_configure_cg(g_lpcm, PCIE0_PERST_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_rst(g_lpcm, PCIE0_PERST_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_cg(g_lpcm, PCIE0_POWER_UP_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_rst(g_lpcm, PCIE0_POWER_UP_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_cg(g_lpcm, APP_HOLD_PHY_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_rst(g_lpcm, APP_HOLD_PHY_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_cg(g_lpcm, APP_INIT_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_rst(g_lpcm, APP_INIT_OFFSET);
	if (ret)
		return ret;

	return ret;
}

static int pcie_clk_prepare(struct clk_hw *hw)
{
	struct google_lpcm *g_lpcm = container_of(hw, struct google_lpcm, pcie_clk_hw);
	int ret;
	u32 val;

	val = readl(g_lpcm->base + LPM_OFFSET + DBG_PCS_PWR_EN_OFFSET);
	val |= PWR_EN;
	writel(val, g_lpcm->base + LPM_OFFSET + DBG_PCS_PWR_EN_OFFSET);

	val = readl(g_lpcm->base + LPM_OFFSET + TOP_PWR_EN_OFFSET);
	val |= PWR_EN;
	writel(val, g_lpcm->base + LPM_OFFSET + TOP_PWR_EN_OFFSET);

	ret = readl_poll_timeout_atomic(g_lpcm->base + LPM_OFFSET +
					PSM_1_OFFSET + PSM_1_STATUS_OFFSET,
					val, (val & STATE_VALID) && ((val & CURR_STATE) == PS0),
					PSM_STATUS_NUM_RETRIES * LPCM_STATUS_INTERVAL_US,
					LPCM_STATUS_TIMEOUT_US);
	if (ret)
		return ret;

	ret = lcm_configure_cg(g_lpcm, PCIE0_DBI_ACLK_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_rst(g_lpcm, PCIE0_DBI_ACLK_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_cg(g_lpcm, CG_REG_FALSE_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_rst(g_lpcm, CG_REG_FALSE_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_cg(g_lpcm, PCIE0_SLV_ACLK_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_rst(g_lpcm, PCIE0_SLV_ACLK_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_cg(g_lpcm, PCIE0_APB_CLK_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_rst(g_lpcm, PCIE0_APB_CLK_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_cg(g_lpcm, CG_AUX_CLK_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_rst(g_lpcm, CG_AUX_CLK_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_cg(g_lpcm, TOP_CSR_CLK_OFFSET);
	if (ret)
		return ret;

	ret = lcm_configure_rst(g_lpcm, TOP_CSR_CLK_OFFSET);
	if (ret)
		return ret;

	return ret;
};

static const struct clk_ops pcie_clk_ops = {
	.prepare = pcie_clk_prepare,
	.enable = pcie_clk_enable,
};

static int google_lpcm_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *data;
	struct device *dev = &pdev->dev;
	struct google_lpcm *g_lpcm;
	void __iomem *regs;
	struct clk_init_data init = { };
	int ret;

	g_lpcm = devm_kzalloc(dev, sizeof(*g_lpcm), GFP_KERNEL);
	if (!g_lpcm)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	g_lpcm->base = regs;
	platform_set_drvdata(pdev, g_lpcm);

	init.name = clk_names[PCIE_CLK];
	init.ops = &pcie_clk_ops;
	g_lpcm->pcie_clk_hw.init = &init;
	ret = devm_clk_hw_register(dev, &g_lpcm->pcie_clk_hw);
	if (ret)
		return ret;

	data = devm_kzalloc(dev, struct_size(data, hws, MAX_CLKS), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->hws[PCIE_CLK] = &g_lpcm->pcie_clk_hw;
	data->num = MAX_CLKS;

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, data);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id google_lpcm_of_match[] = {
	{
		.compatible = "google,hsion-lpcm",
	},
	{},
};

static struct platform_driver google_lpcm_driver = {
	.driver = {
		.name	= "google-lpcm",
		.of_match_table = google_lpcm_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = google_lpcm_probe,
};

module_platform_driver(google_lpcm_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google LPCM Driver");
MODULE_LICENSE("GPL");
