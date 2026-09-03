/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare MIPI CDPHY
 */

#ifndef _DW_MIPI_CDPHY_H
#define _DW_MIPI_CDPHY_H

#include <linux/phy/phy.h>

enum cdphy_model {
	PHY_MODEL_G301_DPHY,
	PHY_MODEL_G301_CDPHY
};

struct dw_mipi_cdphy_data {
	bool lane_conn_fixup;
	bool pll_calc_old;
};

struct dw_mipi_cdphy {
	//struct device *dev;
	struct platform_device *pdev;
	struct phy *phy;
	const struct dw_mipi_cdphy_data *driver_data;
	union phy_configure_opts *cfg;
	struct clk *pllref_clk;
	u32 pll_ref_clk_khz;
	u32 lanes_config;
	u32 dphy_freq;
	/* 0 dphy, 1 cphy */
	u32 is_cphy;
	u32 dphy_gen;
	u32 dphy_te_len;
	u32 max_lanes;
	u32 lp_time;
	u32 datarate;
	bool pll_ssc;
	bool pll_enabled;
	bool phy_in_ulps;
	bool non24x_fix;
	u32 lane_ovr_en;

	const struct phy_ops *cdphy_ops;

	int (*cdphy_init)(struct dw_mipi_cdphy *cdphy);
	int (*cdphy_remove)(struct dw_mipi_cdphy *cdphy);
	void *cdphy_priv_data;

	u32 loop_div;
	u32 in_div;
	u32 range;

	#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *debugfs;
	#endif
};

struct dw_cdphy_plat_data {
	enum cdphy_model phy_model;
	/* 0 dphy, 1 cphy */
	u32 is_cphy;
	bool pll_ssc;
	bool non24x_fix;
};
#endif /* _DW_MIPI_CDPHY_H */
