// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare MIPI CD-PHY Tx driver
 *
 * Author: Marcelo Borges <marcelob@synopsys.com>
 * Author: Pedro Correia <correia@synopsys.com>
 * Author: Nuno Cardoso <cardoso@synopsys.com>
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/slab.h>

#include "dw-mipi-cdphy.h"
#include "dw-mipi-cdphy-g301.h"

static int dw_mipi_cdphy_of_parse_pdata(struct device *dev,
					struct dw_cdphy_plat_data *pdata)
{
	struct device_node *np = dev->of_node;

	if (!pdata)
		return -ENOMEM;

	if (of_property_read_u32(np, "is_cphy", &pdata->is_cphy)) {
		/* default as dphy if not specified */
		pdata->is_cphy = 0;
	}

	pdata->pll_ssc = of_property_read_bool(np, "pll_ssc");
	pdata->non24x_fix = of_property_read_bool(np, "non24x_fix");

	return 0;
}

static struct phy *dw_mipi_phy_of_xlate(struct device *dev, const struct of_phandle_args *args)
{
	struct dw_mipi_cdphy *dw_cdphy = dev_get_drvdata(dev);

	if (WARN_ON(args->args[0] >= 1))
		return ERR_PTR(-ENODEV);

	return dw_cdphy->phy;
}

static int dw_mipi_cdphy_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct dw_cdphy_plat_data *pdata = dev->platform_data;
	struct dw_mipi_cdphy *dw_cdphy;
	struct phy_provider *phy_provider;

	printk("DEBUG: Called from platform device %s", pdev->name);
	if (!pdata) {
		pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
		ret = dw_mipi_cdphy_of_parse_pdata(dev, pdata);
		if (ret)
			return ret;
	}

	dw_cdphy = devm_kzalloc(dev, sizeof(*dw_cdphy), GFP_KERNEL);
	if (!dw_cdphy)
		return -ENOMEM;

	dw_cdphy->driver_data = of_device_get_match_data(dev);
	//dw_cdphy->dev = &pdev->dev;
	dw_cdphy->pdev = pdev;
	/* Add to the phy struct */
	dw_cdphy->is_cphy = pdata->is_cphy;
	dw_cdphy->non24x_fix = pdata->non24x_fix;
	dw_cdphy->pllref_clk = devm_clk_get(dev, "pllref");
	if (IS_ERR(dw_cdphy->pllref_clk))
		return PTR_ERR(dw_cdphy->pllref_clk);

	// TODO NC - change callback based on PHY Type
	if (pdata->phy_model == PHY_MODEL_G301_DPHY) {
		dw_cdphy->cdphy_init = cdphy_init_g301;
		dw_cdphy->cdphy_remove = cdphy_remove_g301;
	} else {
		dev_err(dev, "unsupported phy model %d\n", pdata->phy_model);
		return -EINVAL;
	}

	// call CD-PHY init callback
	ret = dw_cdphy->cdphy_init(dw_cdphy);
	if (ret) {
		dev_err(dev, "Failed to init PHY\n");
		return ret;
	}

	/* Create CD-PHY */
	dw_cdphy->phy = devm_phy_create(dev, NULL, dw_cdphy->cdphy_ops);
	if (IS_ERR(dw_cdphy->phy)) {
		dev_err(dev, "Failed to create PHY\n");
		return PTR_ERR(dw_cdphy->phy);
	}

	platform_set_drvdata(pdev, dw_cdphy);
	phy_set_drvdata(dw_cdphy->phy, dw_cdphy);

	phy_provider = devm_of_phy_provider_register(dev, dw_mipi_phy_of_xlate);
	if (IS_ERR(phy_provider))
		dev_err(dev, "failed to create dw mipi-cdphy\n");
	else
		dev_info(dev, "creating dw-mipi-cdphy\n");

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		dev_err(dev, "runtime pm enabled but failed to add disable action: %d\n", ret);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static void dw_mipi_cdphy_remove(struct platform_device *pdev)
{
	struct dw_mipi_cdphy *dw_cdphy = platform_get_drvdata(pdev);

	dw_cdphy->cdphy_remove(dw_cdphy);

	pr_info("Removed dw_mipi_cdphy\n");
}

const struct dw_mipi_cdphy_data dw_mipi_cdphy_data_v0 = {
	.lane_conn_fixup = true,
};

const struct dw_mipi_cdphy_data dw_mipi_cdphy_data_v1 = {
	.pll_calc_old = true,
};

static const struct of_device_id dw_mipi_cdphy_dt_match[] = {
	{.compatible = "synopsys,dw-mipi-cdphy", .data = &dw_mipi_cdphy_data_v0},
	{.compatible = "synopsys,dw-mipi-cdphy-v1", .data = &dw_mipi_cdphy_data_v1},
	{.compatible = "synopsys,dw-mipi-cdphy-v2"},
	{},
};
MODULE_DEVICE_TABLE(of, dw_mipi_cdphy_dt_match);

struct platform_driver dw_mipi_cdphy_driver = {
	.probe		= dw_mipi_cdphy_probe,
	.remove		= dw_mipi_cdphy_remove,
	.driver		= {
		.name	= "dw-mipi-cdphy",
		.of_match_table = of_match_ptr(dw_mipi_cdphy_dt_match),
	}
};
module_platform_driver(dw_mipi_cdphy_driver);

MODULE_DESCRIPTION("DW MIPI CDPHY");
MODULE_LICENSE("GPL");
MODULE_ALIAS("dw-mipi-cdphy");
MODULE_AUTHOR("Marcelo Borges <marcelob@synopsys.com>");
MODULE_AUTHOR("Pedro Correia <correia@synopsys.com>");
MODULE_AUTHOR("Nuno Cardoso <cardoso@synopsys.com>");
MODULE_AUTHOR("Gustavo Pimentel <gustavo.pimentel@synopsys.com>");
