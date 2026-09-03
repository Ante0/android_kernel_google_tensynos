// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 */
#include <dt-bindings/lcm/google,rdo.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/reset-controller.h>

/* TODO(b/252966027): Figure out actual timeout */
#define LCM_STATUS_INTERVAL_US 5
#define NUM_RETRIES 200

#define GATE_SIZE 0x20
#define GATE_CLK_EN_OFFSET 0x04
#define GATE_QCH_MODE_OFFSET 0x08
#define GATE_RST_VAL_OFFSET 0x0c
#define GATE_STATUS_BIT BIT(8)
#define GATE_R_STATUS_OFFSET 0x1c
#define GATE_NR_STATUS_OFFSET 0x14

#define MUX_SIZE 0x10
#define MUX_SEL_OFFSET 0x04
#define MUX_STATUS_OFFSET 0x08

#define DIV_SIZE 0x10
#define DIV_RATIO_OFFSET 0x04
#define DIV_STATUS_OFFSET 0x08

#define RST_SIZE 0x20
#define RST_VAL_OFFSET 0x0c
#define RST_STATUS_OFFSET 0x1c
#define RST_VAL_BIT BIT(0)
#define RST_STATUS_BIT BIT(12)

#define CTRL_MODE_OFFSET 0x00
#define CTRL_MODE_SW 0x0
#define CTRL_MODE_HW 0x1

/* Check if we have two resets */
#define COMBINED_RST(x) (((x) >> 16) == 0xff)
/* Get index of first reset */
#define DEFAULT_RST(x) ((x) & 0xff)
/* Index for second reset */
#define EXTRA_RST(x) (((x) >> 8) & 0xff)

struct google_lcm_data {
	const char *name;
	int hw_size;
};

static const struct google_lcm_data lcm_data[LCM_TYPE_MAX] = {
	[LCM_GATE_R] = {
		.name = "gate_r", /* with reset */
		.hw_size = GATE_SIZE,
	},
	[LCM_GATE_NR] = {
		.name = "gate_nr", /* without reset */
		.hw_size = GATE_SIZE,
	},
	[LCM_MUX] = {
		.name = "mux",
		.hw_size = MUX_SIZE,
	},
	[LCM_DIV] = {
		.name = "div",
		.hw_size = DIV_SIZE,
	},
};

struct google_clk_hw {
	void __iomem *base;
	int hwtype;
	u32 hw_val;
	struct device *dev;
	struct clk_hw hw;
};

struct lcm_container {
	u32 num;
	struct google_clk_hw *hws;
};

struct lcm_driverdata {
	struct lcm_container containers[LCM_TYPE_MAX];
	struct device *dev;
};

static inline struct google_clk_hw *to_google_clk_hw(struct clk_hw *hw)
{
	return container_of(hw, struct google_clk_hw, hw);
}

struct clk_hw *google_of_clk_hw_get(struct of_phandle_args *clkspec, void *data)
{
	u32 hwtype, idx;
	struct google_clk_hw *clk_hw;
	struct lcm_driverdata *drvdata = data;
	struct device *dev = drvdata->dev;

	if (clkspec->args_count != 3) {
		dev_err(dev, "invalid no. of args in clock specifier\n");
		return ERR_PTR(-EINVAL);
	}

	hwtype = clkspec->args[0];
	if (hwtype >= LCM_TYPE_MAX) {
		dev_err(dev, "invalid clock type\n");
		return ERR_PTR(-EINVAL);
	}

	idx = clkspec->args[1];
	if (idx >= drvdata->containers[hwtype].num) {
		dev_err(dev, "invalid clock index for %s\n",
			lcm_data[hwtype].name);
		return ERR_PTR(-EINVAL);
	}

	clk_hw = &drvdata->containers[hwtype].hws[idx];
	clk_hw->hw_val = clkspec->args[2];
	return &clk_hw->hw;
}

/*
 * Return: 0 if clock is disabled, 1 if enabled.
 */
static int lcm_check_gate(struct google_clk_hw *gate, u32 status_offset)
{
	u32 val = readl(gate->base + status_offset);

	return (val & GATE_STATUS_BIT) ? 1 : 0;
}

static int lcm_enable_gate(struct google_clk_hw *gate, u32 status_offset,
			   bool clk_en)
{
	void __iomem *base = gate->base;
	u32 val;
	int ret = 0;

	writel(CTRL_MODE_SW, base + CTRL_MODE_OFFSET);

	/* Disable Qch handshaking */
	writel((u32)0, base + GATE_QCH_MODE_OFFSET);

	writel((u32)clk_en, base + GATE_CLK_EN_OFFSET);

	ret = readl_poll_timeout_atomic(base + status_offset, val,
					((bool)(val & GATE_STATUS_BIT) == clk_en),
					LCM_STATUS_INTERVAL_US,
					NUM_RETRIES * LCM_STATUS_INTERVAL_US);

	writel(CTRL_MODE_HW, base + CTRL_MODE_OFFSET);

	return ret;
}

static int lcm_set_mux(struct google_clk_hw *mux)
{
	void __iomem *base = mux->base;
	u32 val, mux_sel = mux->hw_val;
	int ret = 0;

	writel(CTRL_MODE_SW, base + CTRL_MODE_OFFSET);

	writel(mux_sel, base + MUX_SEL_OFFSET);

	ret = readl_poll_timeout_atomic(base + MUX_STATUS_OFFSET, val, val != 0,
					LCM_STATUS_INTERVAL_US,
					NUM_RETRIES * LCM_STATUS_INTERVAL_US);

	writel(CTRL_MODE_HW, base + CTRL_MODE_OFFSET);
	return ret;
}

static int lcm_set_div(struct google_clk_hw *div)
{
	void __iomem *base = div->base;
	u32 val, div_ratio = div->hw_val;
	int ret = 0;

	writel(CTRL_MODE_SW, base + CTRL_MODE_OFFSET);

	writel(div_ratio, base + DIV_RATIO_OFFSET);

	ret = readl_poll_timeout_atomic(base + DIV_RATIO_OFFSET, val, val != 0,
					LCM_STATUS_INTERVAL_US,
					NUM_RETRIES * LCM_STATUS_INTERVAL_US);

	writel(CTRL_MODE_HW, base + CTRL_MODE_OFFSET);

	return ret;
}

static int lcm_clk_enable(struct clk_hw *hw)
{
	struct google_clk_hw *lcm_clk = to_google_clk_hw(hw);

	switch (lcm_clk->hwtype) {
	case LCM_GATE_R:
		return lcm_enable_gate(lcm_clk, GATE_R_STATUS_OFFSET, true);
	case LCM_GATE_NR:
		return lcm_enable_gate(lcm_clk, GATE_NR_STATUS_OFFSET, true);
	case LCM_MUX:
		return lcm_set_mux(lcm_clk);
	case LCM_DIV:
		return lcm_set_div(lcm_clk);
	}

	return 0;
}

static void lcm_clk_disable(struct clk_hw *hw)
{
	struct google_clk_hw *lcm_clk = to_google_clk_hw(hw);

	switch (lcm_clk->hwtype) {
	case LCM_GATE_R:
		lcm_enable_gate(lcm_clk, GATE_R_STATUS_OFFSET, false);
		break;
	case LCM_GATE_NR:
		lcm_enable_gate(lcm_clk, GATE_NR_STATUS_OFFSET, false);
		break;
	default:
		dev_err(lcm_clk->dev, "disable not supported for %s\n",
			lcm_data[lcm_clk->hwtype].name);
		break;
	}
}

static int lcm_clk_is_enabled(struct clk_hw *hw)
{
	struct google_clk_hw *lcm_clk = to_google_clk_hw(hw);

	switch (lcm_clk->hwtype) {
	case LCM_GATE_R:
		return lcm_check_gate(lcm_clk, GATE_R_STATUS_OFFSET);
	case LCM_GATE_NR:
		return lcm_check_gate(lcm_clk, GATE_NR_STATUS_OFFSET);
	default:
		return -EOPNOTSUPP;
	}
}

static unsigned long lcm_clk_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct google_clk_hw *lcm_clk = to_google_clk_hw(hw);

	switch (lcm_clk->hwtype) {
	case LCM_GATE_R:
		return lcm_clk->hw_val;
	case LCM_GATE_NR:
		return lcm_clk->hw_val;
	default:
		return 0;
	}

	return 0;
}

const struct clk_ops lcm_clk_ops = {
	.enable = lcm_clk_enable,
	.disable = lcm_clk_disable,
	.is_enabled = lcm_clk_is_enabled,
	.recalc_rate = lcm_clk_recalc_rate,
};

struct lcm_reset {
	void __iomem *base;
	struct reset_controller_dev rcdev;
};

static int lcm_reset_of_xlate(struct reset_controller_dev *rcdev,
			      const struct of_phandle_args *reset_spec)
{
	u32 rst1, rst2;

	rst1 = DEFAULT_RST(reset_spec->args[0]);
	rst2 = EXTRA_RST(reset_spec->args[0]);
	if (rst1 >= rcdev->nr_resets || rst2 >= rcdev->nr_resets)
		return -EINVAL;

	return reset_spec->args[0];
}

static inline struct lcm_reset *to_lcm_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct lcm_reset, rcdev);
}

static int lcm_set_reset_val(struct reset_controller_dev *rcdev,
			     unsigned long id, bool deassert)
{
	struct lcm_reset *rst = to_lcm_reset(rcdev);
	void __iomem *base = rst->base + (id * RST_SIZE);
	u32 rst_val, val;
	int ret = 0;

	writel(CTRL_MODE_SW, base + CTRL_MODE_OFFSET);

	rst_val = readl(base + RST_VAL_OFFSET);
	rst_val &= ~RST_VAL_BIT;
	if (deassert) /* 0:reset asserted, 1:reset deasserted */
		rst_val |= RST_VAL_BIT;
	writel(rst_val, base + RST_VAL_OFFSET);

	ret = readl_poll_timeout_atomic(base + RST_STATUS_OFFSET, val,
					((bool)(val & RST_STATUS_BIT) == deassert),
						LCM_STATUS_INTERVAL_US,
						NUM_RETRIES * LCM_STATUS_INTERVAL_US);

	writel(CTRL_MODE_HW, base + CTRL_MODE_OFFSET);

	return ret;
}

static int lcm_reset_assert(struct reset_controller_dev *rcdev,
			    unsigned long id)
{
	int ret = 0;

	ret = lcm_set_reset_val(rcdev, DEFAULT_RST(id), false);
	if (ret < 0)
		return ret;
	if (COMBINED_RST(id))
		ret = lcm_set_reset_val(rcdev, EXTRA_RST(id), false);
	return ret;
}

static int lcm_reset_deassert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	int ret = 0;

	ret = lcm_set_reset_val(rcdev, DEFAULT_RST(id), true);
	if (ret < 0)
		return ret;
	if (COMBINED_RST(id))
		ret = lcm_set_reset_val(rcdev, EXTRA_RST(id), true);
	return ret;
}

/*
 * Return: 1 if reset is asserted, 0 if deasserted
 */
static int lcm_reset_status(struct reset_controller_dev *rcdev,
			    unsigned long id)
{
	struct lcm_reset *rst = to_lcm_reset(rcdev);
	void __iomem *base = rst->base + (id * RST_SIZE);
	u32 rst_val = readl(base + RST_STATUS_OFFSET);

	return (rst_val & RST_STATUS_BIT) ? 0 : 1;
}

static int lcm_reset_reset(struct reset_controller_dev *rcdev, unsigned long id)
{
	int ret;

	ret = lcm_reset_assert(rcdev, id);
	if (ret < 0)
		return ret;
	udelay(2); /*TODO(b/264513162): Figure out the delay*/
	return lcm_reset_deassert(rcdev, id);
}

static const struct reset_control_ops lcm_reset_ops = {
	.assert = lcm_reset_assert,
	.deassert = lcm_reset_deassert,
	.status = lcm_reset_status,
	.reset = lcm_reset_reset,
};

static int google_lcm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct clk_init_data init = {};
	void __iomem *base, __iomem *rst_base;
	struct resource *res;
	struct google_clk_hw *hws;
	struct lcm_driverdata *drvdata;
	struct of_phandle_args clkspec;
	struct lcm_reset *rst;
	struct reset_control *rstc;
	struct clk *clk;
	char cur_name[20];
	int ret = 0, hwtype, inst, num_hw, num_rst;

	/* clock driver */
	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = dev;
	for (hwtype = 0; hwtype < LCM_TYPE_MAX; hwtype++) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   lcm_data[hwtype].name);
		if (!res)
			continue;

		base = devm_ioremap_resource(dev, res);
		if (IS_ERR(base)) {
			dev_err(dev, "ioremap failed for %s in %s\n",
				lcm_data[hwtype].name, node->name);
			return PTR_ERR(base);
		}

		num_hw = resource_size(res) / lcm_data[hwtype].hw_size;
		drvdata->containers[hwtype].num = num_hw;
		hws = devm_kzalloc(dev, num_hw * sizeof(struct google_clk_hw),
				   GFP_KERNEL);
		if (!hws)
			return -ENOMEM;
		drvdata->containers[hwtype].hws = hws;

		for (inst = 0; inst < num_hw; inst++, hws++) {
			snprintf(cur_name, 20, "%s_%s_%d", node->name,
				 lcm_data[hwtype].name, inst);

			init.name = cur_name;
			init.ops = &lcm_clk_ops;
			init.flags = CLK_GET_RATE_NOCACHE;
			hws->hw.init = &init;
			hws->base = base + (inst * lcm_data[hwtype].hw_size);
			hws->hwtype = hwtype;
			hws->dev = dev;

			ret = devm_clk_hw_register(dev, &hws->hw);
			if (ret < 0) {
				dev_err(dev, "clk_hw_register failed\n");
				return ret;
			}
		}

		if (hwtype == LCM_GATE_R) {
			num_rst = num_hw;
			rst_base = base;
		}
	}

	ret = devm_of_clk_add_hw_provider(dev, google_of_clk_hw_get, drvdata);
	if (ret < 0) {
		dev_err(dev, "clk_add_hw_provider failed\n");
		return ret;
	}

	num_hw = of_count_phandle_with_args(node, "clocks", "#clock-cells");
	for (inst = 0; inst < num_hw; ++inst) {
		ret = of_parse_phandle_with_args(node, "clocks", "#clock-cells",
						 inst, &clkspec);
		if (ret < 0) {
			dev_err(dev, "can't get clkspec\n");
			return ret;
		}

		clk = of_clk_get_from_provider(&clkspec);
		if (IS_ERR(clk)) {
			dev_err(dev, "clk_get failed\n");
			return PTR_ERR(clk);
		}

		ret = clk_prepare_enable(clk);
		clk_put(clk);

		if (ret < 0) {
			dev_err(dev, "clk_enable failed\n");
			return ret;
		}
	}

	/* reset driver */
	rst = devm_kzalloc(&pdev->dev, sizeof(*rst), GFP_KERNEL);
	if (!rst)
		return -ENOMEM;

	rst->rcdev.ops = &lcm_reset_ops;
	rst->rcdev.owner = THIS_MODULE;
	rst->rcdev.of_node = pdev->dev.of_node;
	rst->rcdev.nr_resets = num_rst;
	rst->rcdev.of_reset_n_cells = 1;
	rst->rcdev.of_xlate = lcm_reset_of_xlate;
	rst->base = rst_base;

	ret = devm_reset_controller_register(dev, &rst->rcdev);

	if (ret < 0) {
		dev_err(dev, "reset_register failed\n");
		return ret;
	}

	num_rst = of_count_phandle_with_args(node, "resets", "#reset-cells");
	for (inst = 0; inst < num_rst; ++inst) {
		rstc = __of_reset_control_get(node, NULL, inst, false, false,
					      true);
		if (IS_ERR(rstc)) {
			dev_err(dev, "reset_get failed\n");
			return PTR_ERR(rstc);
		}

		ret = reset_control_reset(rstc);
		reset_control_put(rstc);

		if (ret < 0) {
			dev_err(dev, "reset control failed\n");
			return ret;
		}
	}

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return ret;
}

static const struct of_device_id google_lcm_of_match[] = {
	{
		.compatible = "google,google-lcm",
	},
	{},
};

static struct platform_driver google_lcm_driver = {
	.driver = {
		.name = "google-lcm",
		.of_match_table = google_lcm_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = google_lcm_probe,
};

module_platform_driver(google_lcm_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google LCM Driver");
MODULE_LICENSE("GPL");
