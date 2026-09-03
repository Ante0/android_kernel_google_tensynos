// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare MIPI D-PHY G301 Tx driver
 *
 * Author: Marcelo Borges <marcelob@synopsys.com>
 * Author: Pedro Correia <correia@synopsys.com>
 * Author: Nuno Cardoso <cardoso@synopsys.com>
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <drm/drm_print.h>

#include <gs_drm/gs_reg_dump.h>
#include <gs_drm/gs_sscd.h>
#include <trace/dpu_trace.h>

#include <drm/phy/dw_mipi_cdphy_op.h>
#include <drm/phy/dw_mipi_cdphy_sscd.h>
#include <drm/sscd/gs_sscd_dpu.h>

#include "dw-mipi-cdphy-g301.h"

/* Cached MIPI calibration value from MIPI PHY 0 */
static u32 shared_cb_ctrl_val;

struct debugfs_entries {
	const char		*name;
	struct regmap_field	*reg;
};

/* TODO NC: Delete */
struct regmap_field {
	struct regmap *regmap;
	unsigned int mask;
	/* lsb */
	unsigned int shift;
	unsigned int reg;

	unsigned int id_size;
	unsigned int id_offset;
};

#define regmap_range_sized(addr, size) regmap_reg_range((addr), (addr) + (size) - 1)
static const struct regmap_range cdphy_dump_reg_allowed[] = {
	regmap_range_sized(0x3000, 0x6820), /*cdphy_mem_map: PPI_STARTUP_RW_COMMON_DPHY_0*/
	regmap_range_sized(0xC000, 0x2470), /*cdphy_mem_map: CORE_DIG_DLANE_0_RW_CFG_0*/
	regmap_range_sized(0x10000, 0x1020), /*cdphy_mem_map: PPI_RW_CPHY_TRIO0_LBERT_0*/
	regmap_range_sized(0x14000, 0x1470), /*cdphy_mem_map: CORE_DIG_CLANE_0_RW_CFG_0*/
};
static const struct regmap_access_table cdphy_dump_reg_access_table = {
	.yes_ranges = cdphy_dump_reg_allowed,
	.n_yes_ranges = ARRAY_SIZE(cdphy_dump_reg_allowed),
};
static const struct regmap_range apb_dump_reg_allowed[] = {
	regmap_range_sized(0x0, 0x50), /*apb_regbank: PLL_CFG0*/
};
static const struct regmap_access_table apb_dump_reg_access_table = {
	.yes_ranges = apb_dump_reg_allowed,
	.n_yes_ranges = ARRAY_SIZE(apb_dump_reg_allowed),
};

#define REGISTER(a) \
	{ .name = #a, .reg = cdphy_g301->field_##a }
static inline __maybe_unused void dw_cdphy_write_base(u32 val, void __iomem *mem, u32 reg)
{
	//pr_info("$> %s: reg=0x%x, val=%u\n", __func__, reg, val);
	writel(val, mem + reg);
}

static inline __maybe_unused void dw_cdphy_write_u0_apb_regbank(u32 val, void __iomem *mem, u32 reg)
{
	//pr_info("$> %s: reg=0x%x, val=%u\n", __func__, reg, val);
	writel(val, mem + reg);
}

static inline u32 dw_cdphy_read_reg(struct regmap *regm, u32 reg)
{
	u32 val = 0;

	regmap_read(regm, reg, &val);
	//pr_info("$> %s: reg=0x%x, val=%u\n", __func__, reg, val);
	return val;
}

static inline __maybe_unused void dw_cdphy_write_reg(struct regmap *regm, u32 reg, u32 val)
{
	//pr_info("$> %s: reg=0x%x, val=%u\n", __func__, reg, val);
	regmap_write(regm, reg, val);
}

static inline void dw_cdphy_write_field(struct regmap_field *reg_field, u32 val)
{
	//pr_info("$> %s: reg=0x%x, mask=0x%x, shift=%u, val=%u\n",
	//	__func__, reg_field->reg, reg_field->mask, reg_field->shift, val);
	regmap_field_write(reg_field, val);
}

static int dw_mipi_debugfs_u32_get(void *data, u64 *val)
{
	u32 tmp = 0;
	struct regmap_field *reg_field = (struct regmap_field *)data;
	struct device *dev;

	if (!reg_field)
		return -ENODEV;

	dev = regmap_get_device(reg_field->regmap);
	if (!dev)
		return -ENODEV;

	if (pm_runtime_get_if_in_use(dev) <= 0)
		return -EPERM;

	regmap_field_read(reg_field, &tmp);
	*val = (u64)tmp;

	pm_runtime_put_sync(dev);

	return 0;
}

static int dw_mipi_debugfs_u32_set(void *data, u64 val)
{
	struct regmap_field *reg_field = (struct regmap_field *)data;
	struct device *dev;

	if (!reg_field)
		return -ENODEV;

	dev = regmap_get_device(reg_field->regmap);
	if (!dev)
		return -ENODEV;

	if (pm_runtime_get_if_in_use(dev) <= 0)
		return -ENODEV;

	regmap_field_write(reg_field, val);

	pm_runtime_put_sync(dev);

	return 0;
}

static int dw_mipi_debugfs_u32_local_get(void *data, u64 *val)
{
	struct dw_mipi_cdphy_g301 *cdphy_g301 = (struct dw_mipi_cdphy_g301 *)data;
	struct dw_mipi_cdphy *cdphy = cdphy_g301->cdphy;

	*val = (u64)cdphy->datarate;

	return 0;
}

static int dw_mipi_debugfs_u32_local_set(void *data, u64 val)
{
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_x32, dw_mipi_debugfs_u32_get, dw_mipi_debugfs_u32_set, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_x32_local, dw_mipi_debugfs_u32_local_get,
			 dw_mipi_debugfs_u32_local_set, "%llu\n");

static void dw_mipi_debugfs_create_x32(const struct debugfs_entries entries[],
				       int nr_entries, struct dentry *dir)
{
	int i;

	for (i = 0; i < nr_entries; i++) {
		if (!debugfs_create_file_unsafe(entries[i].name, 0444, dir,
						entries[i].reg, &fops_x32))
			break;
	}
}

#define INIT_FIELD(f, regs) INIT_FIELD_CFG(field_##f, cfg_##f, regs)
#define INIT_FIELD_CFG(f, conf, regs) ({					\
		cdphy_g301->f = devm_regmap_field_alloc(dev, cdphy_g301->regs,\
							variant->conf);	\
		if (IS_ERR(cdphy_g301->f)) {					\
			dev_err(dev, "Failed alloc regmap field"#f "\n");\
			return PTR_ERR(cdphy_g301->f);				\
		}})

static const struct regmap_config dw_mipi_cdphy_base_regmap_cfg = {
	.name = "dw_mipi_cdphy_base",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
};

static const struct regmap_config dw_mipi_cdphy_u0_apb_regbank_regmap_cfg = {
	.name = "dw_mipi_cdphy_u0_apb_regbank",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
};

static const struct dw_mipi_phy_g301_variant dw_mipi_phy_g301_lca01_variant = {
	/* dwc_mipi_cdphy_tx_4l3t_ns */
	.cfg_ppi_startup_rw_common_dphy_2_rcal_addr = REG_FIELD(PPI_STARTUP_RW_COMMON_DPHY_2, 0, 7),
	.cfg_ppi_startup_rw_common_dphy_3_pll_start_addr =
		REG_FIELD(PPI_STARTUP_RW_COMMON_DPHY_3, 0, 7),
	.cfg_ppi_startup_rw_common_dphy_6_lp_dco_cal_addr =
		REG_FIELD(PPI_STARTUP_RW_COMMON_DPHY_6, 0, 7),
	.cfg_ppi_startup_rw_common_dphy_a_hibernate_addr =
		REG_FIELD(PPI_STARTUP_RW_COMMON_DPHY_A, 0, 7),
	.cfg_ppi_startup_rw_common_dphy_10_phy_ready_addr =
		REG_FIELD(PPI_STARTUP_RW_COMMON_DPHY_10, 0, 7),
	.cfg_ppi_startup_rw_common_startup_1_1_phy_ready_dly =
		REG_FIELD(PPI_STARTUP_RW_COMMON_STARTUP_1_1, 0, 11),
	.cfg_ppi_calibctrl_rw_common_bg_0_bg_max_counter =
		REG_FIELD(PPI_CALIBCTRL_RW_COMMON_BG_0, 0, 8),
	.cfg_ppi_rw_lpdcocal_timebase_lpcdcocal_timebase =
		REG_FIELD(PPI_RW_LPDCOCAL_TIMEBASE, 0, 9),
	.cfg_ppi_rw_lpdcocal_nref_lpcdcocal_nref = REG_FIELD(PPI_RW_LPDCOCAL_NREF, 0, 10),
	.cfg_ppi_rw_lpdcocal_nref_range_lpcdcocal_nref_range =
		REG_FIELD(PPI_RW_LPDCOCAL_NREF_RANGE, 0, 4),
	.cfg_ppi_rw_lpdcocal_twait_config_lpcdcocal_twait_coarse =
		REG_FIELD(PPI_RW_LPDCOCAL_TWAIT_CONFIG, 0, 8),
	.cfg_ppi_rw_lpdcocal_twait_config_lpcdcocal_twait_pon =
		REG_FIELD(PPI_RW_LPDCOCAL_TWAIT_CONFIG, 9, 15),
	.cfg_ppi_rw_lpdcocal_vt_config_lpcdcocal_vt_tracking_en =
		REG_FIELD(PPI_RW_LPDCOCAL_VT_CONFIG, 0, 0),
	.cfg_ppi_rw_lpdcocal_vt_config_lpcdcocal_use_ideal_nref =
		REG_FIELD(PPI_RW_LPDCOCAL_VT_CONFIG, 1, 1),
	.cfg_ppi_rw_lpdcocal_vt_config_lpcdcocal_vt_nref_range =
		REG_FIELD(PPI_RW_LPDCOCAL_VT_CONFIG, 2, 6),
	.cfg_ppi_rw_lpdcocal_vt_config_lpcdcocal_twait_fine =
		REG_FIELD(PPI_RW_LPDCOCAL_VT_CONFIG, 7, 15),
	.cfg_ppi_rw_lpdcocal_coarse_cfg_ncoarse_start = REG_FIELD(PPI_RW_LPDCOCAL_COARSE_CFG, 0, 1),
	.cfg_ppi_rw_common_cfg_cfg_clk_div_factor = REG_FIELD(PPI_RW_COMMON_CFG, 0, 1),
	.cfg_ppi_rw_termcal_cfg_0_termcal_timer = REG_FIELD(PPI_RW_TERMCAL_CFG_0, 0, 6),
	.cfg_ppi_rw_pll_startup_cfg_0_pll_rst_time = REG_FIELD(PPI_RW_PLL_STARTUP_CFG_0, 0, 9),
	.cfg_ppi_rw_pll_startup_cfg_1_pll_gear_shift_time =
		REG_FIELD(PPI_RW_PLL_STARTUP_CFG_1, 0, 9),
	.cfg_ppi_rw_pll_startup_cfg_2_pll_lock_det_time = REG_FIELD(PPI_RW_PLL_STARTUP_CFG_2, 0, 9),
	.cfg_ppi_rw_pll_startup_cfg_4_ssc_reset_n = REG_FIELD(PPI_RW_PLL_STARTUP_CFG_4, 2, 2),
	.cfg_ppi_rw_tx_hibernate_cfg_0_hibernate_exit = REG_FIELD(PPI_RW_TX_HIBERNATE_CFG_0, 0, 0),
	.cfg_core_dig_ioctrl_rw_afe_lane0_ctrl_2_2_oa_lane0_sel_lane_cfg =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_2, 0, 0),
	.cfg_core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_hstx_sel_phase0 =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_3, 4, 4),
	.cfg_core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_hstx_eqa =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_3, 5, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_hstx_sel_clklb =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_3, 8, 8),
	.cfg_core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_lptx_din_dn_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_3, 9, 9),
	.cfg_core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_lptx_din_dp_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_3, 10, 10),
	.cfg_core_dig_ioctrl_rw_afe_lane0_ctrl_2_4_oa_lane0_hstx_eqb =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_4, 2, 4),
	.cfg_core_dig_ioctrl_rw_afe_lane0_ctrl_2_4_oa_lane0_lptx_din_dn_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_4, 6, 6),
	.cfg_core_dig_ioctrl_rw_afe_lane0_ctrl_2_4_oa_lane0_lptx_din_dp_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_4, 7, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane0_ctrl_2_7_oa_lane0_lptx_en_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_7, 0, 1),
	.cfg_core_dig_ioctrl_rw_afe_lane0_ctrl_2_7_oa_lane0_lptx_pon_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_7, 2, 3),
	.cfg_core_dig_ioctrl_rw_afe_lane0_ctrl_2_8_oa_lane0_lptx_en_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_8, 6, 6),
	.cfg_core_dig_ioctrl_rw_afe_lane0_ctrl_2_8_oa_lane0_lptx_pon_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_8, 7, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane1_ctrl_2_2_oa_lane1_sel_lane_cfg =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_2, 0, 0),
	.cfg_core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_hstx_sel_phase0 =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_3, 4, 4),
	.cfg_core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_hstx_eqa =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_3, 5, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_hstx_sel_clklb =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_3, 8, 8),
	.cfg_core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_lptx_din_dn_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_3, 9, 9),
	.cfg_core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_lptx_din_dp_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_3, 10, 10),
	.cfg_core_dig_ioctrl_rw_afe_lane1_ctrl_2_4_oa_lane1_hstx_eqb =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_4, 2, 4),
	.cfg_core_dig_ioctrl_rw_afe_lane1_ctrl_2_4_oa_lane1_lptx_din_dn_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_4, 6, 6),
	.cfg_core_dig_ioctrl_rw_afe_lane1_ctrl_2_4_oa_lane1_lptx_din_dp_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_4, 7, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane1_ctrl_2_7_oa_lane1_lptx_en_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_7, 0, 1),
	.cfg_core_dig_ioctrl_rw_afe_lane1_ctrl_2_7_oa_lane1_lptx_pon_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_7, 2, 3),
	.cfg_core_dig_ioctrl_rw_afe_lane1_ctrl_2_8_oa_lane1_lptx_en_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_8, 6, 6),
	.cfg_core_dig_ioctrl_rw_afe_lane1_ctrl_2_8_oa_lane1_lptx_pon_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_8, 7, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_2_oa_lane2_sel_lane_cfg =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_2, 0, 0),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_hstx_sel_phase0 =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_3, 4, 4),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_hstx_eqa =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_3, 5, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_hstx_sel_clklb =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_3, 8, 8),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_lptx_din_dn_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_3, 9, 9),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_lptx_din_dp_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_3, 10, 10),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_4_oa_lane2_hstx_eqb =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_4, 2, 4),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_4_oa_lane2_lptx_din_dn_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_4, 6, 6),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_4_oa_lane2_lptx_din_dp_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_4, 7, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lptx_en_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_7, 0, 1),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lptx_pon_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_7, 2, 3),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lprx_lp_pon_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_7, 6, 6),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lprx_cd_pon_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_7, 7, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lprx_lp_pon_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_8, 0, 1),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lprx_cd_pon_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_8, 2, 3),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lptx_en_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_8, 6, 6),
	.cfg_core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lptx_pon_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_8, 7, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane3_ctrl_2_2_oa_lane3_sel_lane_cfg =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_2, 0, 0),
	.cfg_core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_hstx_sel_phase0 =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_3, 4, 4),
	.cfg_core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_hstx_eqa =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_3, 5, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_hstx_sel_clklb =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_3, 8, 8),
	.cfg_core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_lptx_din_dn_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_3, 9, 9),
	.cfg_core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_lptx_din_dp_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_3, 10, 10),
	.cfg_core_dig_ioctrl_rw_afe_lane3_ctrl_2_4_oa_lane3_hstx_eqb =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_4, 2, 4),
	.cfg_core_dig_ioctrl_rw_afe_lane3_ctrl_2_4_oa_lane3_lptx_din_dn_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_4, 6, 6),
	.cfg_core_dig_ioctrl_rw_afe_lane3_ctrl_2_4_oa_lane3_lptx_din_dp_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_4, 7, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane3_ctrl_2_7_oa_lane3_lptx_en_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_7, 0, 1),
	.cfg_core_dig_ioctrl_rw_afe_lane3_ctrl_2_7_oa_lane3_lptx_pon_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_7, 2, 3),
	.cfg_core_dig_ioctrl_rw_afe_lane3_ctrl_2_8_oa_lane3_lptx_en_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_8, 6, 6),
	.cfg_core_dig_ioctrl_rw_afe_lane3_ctrl_2_8_oa_lane3_lptx_pon_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE3_CTRL_2_8, 7, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane4_ctrl_2_2_oa_lane4_sel_lane_cfg =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_2, 0, 0),
	.cfg_core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_hstx_sel_phase0 =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_3, 4, 4),
	.cfg_core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_hstx_eqa =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_3, 5, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_hstx_sel_clklb =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_3, 8, 8),
	.cfg_core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_lptx_din_dn_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_3, 9, 9),
	.cfg_core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_lptx_din_dp_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_3, 10, 10),
	.cfg_core_dig_ioctrl_rw_afe_lane4_ctrl_2_4_oa_lane4_hstx_eqb =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_4, 2, 4),
	.cfg_core_dig_ioctrl_rw_afe_lane4_ctrl_2_4_oa_lane4_lptx_din_dn_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_4, 6, 6),
	.cfg_core_dig_ioctrl_rw_afe_lane4_ctrl_2_4_oa_lane4_lptx_din_dp_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_4, 7, 7),
	.cfg_core_dig_ioctrl_rw_afe_lane4_ctrl_2_7_oa_lane4_lptx_en_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_7, 0, 1),
	.cfg_core_dig_ioctrl_rw_afe_lane4_ctrl_2_7_oa_lane4_lptx_pon_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_7, 2, 3),
	.cfg_core_dig_ioctrl_rw_afe_lane4_ctrl_2_8_oa_lane4_lptx_en_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_8, 6, 6),
	.cfg_core_dig_ioctrl_rw_afe_lane4_ctrl_2_8_oa_lane4_lptx_pon_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_LANE4_CTRL_2_8, 7, 7),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_0_oa_cb_hstxlb_dco_clk90_en_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_0, 15, 15),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_1_oa_cb_hstxlb_dco_clk0_en_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_1, 15, 15),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_2_oa_cb_pll_bustiez =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_2, 15, 15),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_3_oa_cb_hstxlb_dco_clk0_en_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_3, 8, 8),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_3_oa_cb_hstxlb_dco_clk90_en_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_3, 9, 9),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_4_oa_cb_sel_vcomm_prog =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_4, 11, 13),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_4_oa_cb_cal_sink_en_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_4, 15, 15),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_5_oa_cb_sel_45ohm_50ohm =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_5, 8, 8),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_5_oa_cb_cal_sink_en_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_5, 15, 15),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_6_oa_cb_hstxlb_dco_pon_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_6, 12, 12),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_6_oa_cb_hstxlb_dco_en_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_6, 13, 13),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_6_oa_cb_hstxlb_dco_tune_clkdig_en_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_6, 14, 14),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_cb_hstxlb_dco_en_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7, 9, 9),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_cb_hstxlb_dco_tune_clkdig_en_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7, 10, 10),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_setr_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7, 11, 11),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_setr_calib_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7, 12, 12),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_setra_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7, 13, 13),
	.cfg_core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_setrb_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7, 14, 14),
	.cfg_core_dig_anactrl_rw_common_anactrl_0_cb_lp_dco_en_dly =
		REG_FIELD(CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_0, 2, 7),
	.cfg_core_dig_anactrl_rw_common_anactrl_2_global_ulps_ovr_en =
		REG_FIELD(CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_2, 12, 12),
	.cfg_core_dig_anactrl_rw_common_anactrl_2_global_ulps_ovr_val =
		REG_FIELD(CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_2, 13, 13),
	.cfg_core_dig_dlane_clk_rw_cfg_0_cfg_0_lp_pin_swap_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_CFG_0, 0, 0),
	.cfg_core_dig_dlane_clk_rw_cfg_0_cfg_0_hs_pin_swap_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_CFG_0, 1, 1),
	.cfg_core_dig_dlane_clk_rw_lp_0_lp_0_itminrx_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_LP_0, 12, 15),
	.cfg_core_dig_dlane_clk_rw_lp_2_lp_2_filter_input_sampling_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_LP_2, 0, 0),
	.cfg_core_dig_dlane_clk_rw_hs_tx_3_hs_tx_3_pin_swap_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_HS_TX_3, 15, 15),
	.cfg_core_dig_dlane_0_rw_lp_0_lp_0_ttago_reg = REG_FIELD(CORE_DIG_DLANE_0_RW_LP_0, 8, 11),
	.cfg_core_dig_dlane_0_rw_lp_0_lp_0_itminrx_reg =
		REG_FIELD(CORE_DIG_DLANE_0_RW_LP_0, 12, 15),
	.cfg_core_dig_dlane_0_rw_lp_2_lp_2_filter_input_sampling_reg =
		REG_FIELD(CORE_DIG_DLANE_0_RW_LP_2, 0, 0),
	.cfg_core_dig_dlane_0_rw_hs_tx_0_hs_tx_0_thstrail_reg =
		REG_FIELD(CORE_DIG_DLANE_0_RW_HS_TX_0, 0, 15),
	.cfg_core_dig_dlane_0_rw_hs_tx_1_hs_tx_1_thszero_reg =
		REG_FIELD(CORE_DIG_DLANE_0_RW_HS_TX_1, 0, 15),
	.cfg_core_dig_dlane_0_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg =
		REG_FIELD(CORE_DIG_DLANE_0_RW_HS_TX_3, 0, 7),
	.cfg_core_dig_dlane_0_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_0_RW_HS_TX_4, 0, 15),
	.cfg_core_dig_dlane_0_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_0_RW_HS_TX_5, 0, 15),
	.cfg_core_dig_dlane_0_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_0_RW_HS_TX_6, 0, 15),
	.cfg_core_dig_dlane_0_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_0_RW_HS_TX_9, 0, 15),
	.cfg_core_dig_dlane_0_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_0_RW_HS_TX_10, 0, 15),
	.cfg_core_dig_dlane_0_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_0_RW_HS_TX_12, 0, 15),
	.cfg_core_dig_dlane_0_rw_hs_tx_sh_11_sh_sel_reg =
		REG_FIELD(CORE_DIG_DLANE_0_RW_HS_TX_SHADOW_11, 0, 0),
	.cfg_core_dig_dlane_1_rw_lp_0_lp_0_ttago_reg = REG_FIELD(CORE_DIG_DLANE_1_RW_LP_0, 8, 11),
	.cfg_core_dig_dlane_1_rw_lp_0_lp_0_itminrx_reg =
		REG_FIELD(CORE_DIG_DLANE_1_RW_LP_0, 12, 15),
	.cfg_core_dig_dlane_1_rw_lp_2_lp_2_filter_input_sampling_reg =
		REG_FIELD(CORE_DIG_DLANE_1_RW_LP_2, 0, 0),
	.cfg_core_dig_dlane_1_rw_hs_tx_0_hs_tx_0_thstrail_reg =
		REG_FIELD(CORE_DIG_DLANE_1_RW_HS_TX_0, 0, 15),
	.cfg_core_dig_dlane_1_rw_hs_tx_1_hs_tx_1_thszero_reg =
		REG_FIELD(CORE_DIG_DLANE_1_RW_HS_TX_1, 0, 15),
	.cfg_core_dig_dlane_1_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg =
		REG_FIELD(CORE_DIG_DLANE_1_RW_HS_TX_3, 0, 7),
	.cfg_core_dig_dlane_1_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_1_RW_HS_TX_4, 0, 15),
	.cfg_core_dig_dlane_1_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_1_RW_HS_TX_5, 0, 15),
	.cfg_core_dig_dlane_1_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_1_RW_HS_TX_6, 0, 15),
	.cfg_core_dig_dlane_1_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_1_RW_HS_TX_9, 0, 15),
	.cfg_core_dig_dlane_1_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_1_RW_HS_TX_10, 0, 15),
	.cfg_core_dig_dlane_1_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_1_RW_HS_TX_12, 0, 15),
	.cfg_core_dig_dlane_1_rw_hs_tx_sh_11_sh_sel_reg =
		REG_FIELD(CORE_DIG_DLANE_1_RW_HS_TX_SHADOW_11, 0, 0),
	.cfg_core_dig_dlane_2_rw_lp_0_lp_0_ttago_reg = REG_FIELD(CORE_DIG_DLANE_2_RW_LP_0, 8, 11),
	.cfg_core_dig_dlane_2_rw_lp_0_lp_0_itminrx_reg =
		REG_FIELD(CORE_DIG_DLANE_2_RW_LP_0, 12, 15),
	.cfg_core_dig_dlane_2_rw_lp_2_lp_2_filter_input_sampling_reg =
		REG_FIELD(CORE_DIG_DLANE_2_RW_LP_2, 0, 0),
	.cfg_core_dig_dlane_2_rw_hs_tx_0_hs_tx_0_thstrail_reg =
		REG_FIELD(CORE_DIG_DLANE_2_RW_HS_TX_0, 0, 15),
	.cfg_core_dig_dlane_2_rw_hs_tx_1_hs_tx_1_thszero_reg =
		REG_FIELD(CORE_DIG_DLANE_2_RW_HS_TX_1, 0, 15),
	.cfg_core_dig_dlane_2_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg =
		REG_FIELD(CORE_DIG_DLANE_2_RW_HS_TX_3, 0, 7),
	.cfg_core_dig_dlane_2_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_2_RW_HS_TX_4, 0, 15),
	.cfg_core_dig_dlane_2_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_2_RW_HS_TX_5, 0, 15),
	.cfg_core_dig_dlane_2_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_2_RW_HS_TX_6, 0, 15),
	.cfg_core_dig_dlane_2_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_2_RW_HS_TX_9, 0, 15),
	.cfg_core_dig_dlane_2_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_2_RW_HS_TX_10, 0, 15),
	.cfg_core_dig_dlane_2_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_2_RW_HS_TX_12, 0, 15),
	.cfg_core_dig_dlane_2_rw_hs_tx_sh_11_sh_sel_reg =
		REG_FIELD(CORE_DIG_DLANE_2_RW_HS_TX_SHADOW_11, 0, 0),
	.cfg_core_dig_dlane_3_rw_lp_0_lp_0_ttago_reg = REG_FIELD(CORE_DIG_DLANE_3_RW_LP_0, 8, 11),
	.cfg_core_dig_dlane_3_rw_lp_0_lp_0_itminrx_reg =
		REG_FIELD(CORE_DIG_DLANE_3_RW_LP_0, 12, 15),
	.cfg_core_dig_dlane_3_rw_lp_2_lp_2_filter_input_sampling_reg =
		REG_FIELD(CORE_DIG_DLANE_3_RW_LP_2, 0, 0),
	.cfg_core_dig_dlane_3_rw_hs_tx_0_hs_tx_0_thstrail_reg =
		REG_FIELD(CORE_DIG_DLANE_3_RW_HS_TX_0, 0, 15),
	.cfg_core_dig_dlane_3_rw_hs_tx_1_hs_tx_1_thszero_reg =
		REG_FIELD(CORE_DIG_DLANE_3_RW_HS_TX_1, 0, 15),
	.cfg_core_dig_dlane_3_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg =
		REG_FIELD(CORE_DIG_DLANE_3_RW_HS_TX_3, 0, 7),
	.cfg_core_dig_dlane_3_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_3_RW_HS_TX_4, 0, 15),
	.cfg_core_dig_dlane_3_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_3_RW_HS_TX_5, 0, 15),
	.cfg_core_dig_dlane_3_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_3_RW_HS_TX_6, 0, 15),
	.cfg_core_dig_dlane_3_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_3_RW_HS_TX_9, 0, 15),
	.cfg_core_dig_dlane_3_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_3_RW_HS_TX_10, 0, 15),
	.cfg_core_dig_dlane_3_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_3_RW_HS_TX_12, 0, 15),
	.cfg_core_dig_dlane_3_rw_hs_tx_sh_11_sh_sel_reg =
		REG_FIELD(CORE_DIG_DLANE_3_RW_HS_TX_SHADOW_11, 0, 0),
	.cfg_core_dig_dlane_clk_rw_hs_tx_0_hs_tx_0_thstrail_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_HS_TX_0, 0, 15),
	.cfg_core_dig_dlane_clk_rw_hs_tx_1_hs_tx_1_thszero_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_HS_TX_1, 0, 15),
	.cfg_core_dig_dlane_clk_rw_hs_tx_2_hs_tx_2_tclkpre_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_HS_TX_2, 0, 15),
	.cfg_core_dig_dlane_clk_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_HS_TX_3, 0, 7),
	.cfg_core_dig_dlane_clk_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_HS_TX_4, 0, 15),
	.cfg_core_dig_dlane_clk_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_HS_TX_5, 0, 15),
	.cfg_core_dig_dlane_clk_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_HS_TX_6, 0, 15),
	.cfg_core_dig_dlane_clk_rw_hs_tx_8_hs_tx_8_tclkpost_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_HS_TX_8, 0, 15),
	.cfg_core_dig_dlane_clk_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_HS_TX_9, 0, 15),
	.cfg_core_dig_dlane_clk_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_HS_TX_10, 0, 15),
	.cfg_core_dig_dlane_clk_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_HS_TX_12, 0, 15),
	.cfg_core_dig_dlane_clk_rw_hs_tx_sh_11_sh_sel_reg =
		REG_FIELD(CORE_DIG_DLANE_CLK_RW_HS_TX_SHADOW_11, 0, 0),
	.cfg_core_dig_clane_0_rw_lp_0_lp_0_ttago_reg = REG_FIELD(CORE_DIG_CLANE_0_RW_LP_0, 8, 11),
	.cfg_core_dig_clane_0_rw_lp_0_lp_0_itminrx_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_LP_0, 12, 15),
	.cfg_core_dig_clane_0_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_0, 0, 15),
	.cfg_core_dig_clane_0_rw_hs_tx_1_hs_tx_1_tpost_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_1, 0, 15),
	.cfg_core_dig_clane_0_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_2, 0, 15),
	.cfg_core_dig_clane_0_rw_hs_tx_3_hs_tx_3_burst_type_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_3, 5, 7),
	.cfg_core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb0_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_4, 0, 2),
	.cfg_core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb1_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_4, 3, 5),
	.cfg_core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb2_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_4, 6, 8),
	.cfg_core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb3_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_4, 9, 11),
	.cfg_core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb4_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_4, 12, 14),
	.cfg_core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb5_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_5, 0, 2),
	.cfg_core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb6_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_5, 3, 5),
	.cfg_core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb7_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_5, 6, 8),
	.cfg_core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb8_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_5, 9, 11),
	.cfg_core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb9_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_5, 12, 14),
	.cfg_core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb10_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_6, 0, 2),
	.cfg_core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb11_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_6, 3, 5),
	.cfg_core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb12_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_6, 6, 8),
	.cfg_core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb13_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_6, 9, 11),
	.cfg_core_dig_clane_0_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_7, 0, 15),
	.cfg_core_dig_clane_0_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_8, 0, 15),
	.cfg_core_dig_clane_0_rw_hs_tx_9_hs_tx_9_t3post_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_9, 0, 15),
	.cfg_core_dig_clane_0_rw_hs_tx_10_hs_tx_10_tprebegin_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_10, 0, 15),
	.cfg_core_dig_clane_0_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_11, 0, 15),
	.cfg_core_dig_clane_0_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_12, 0, 15),
	.cfg_core_dig_clane_0_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg =
		REG_FIELD(CORE_DIG_CLANE_0_RW_HS_TX_13, 0, 7),
	.cfg_core_dig_clane_1_rw_cfg_0_cfg_0_lp_pin_swap_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_CFG_0, 0, 2),
	.cfg_core_dig_clane_1_rw_hs_tx_6_hs_tx_6_pin_swap_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_6, 12, 14),
	.cfg_core_dig_clane_1_rw_lp_0_lp_0_ttago_reg = REG_FIELD(CORE_DIG_CLANE_1_RW_LP_0, 8, 11),
	.cfg_core_dig_clane_1_rw_lp_0_lp_0_itminrx_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_LP_0, 12, 15),
	.cfg_core_dig_clane_1_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_0, 0, 15),
	.cfg_core_dig_clane_1_rw_hs_tx_1_hs_tx_1_tpost_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_1, 0, 15),
	.cfg_core_dig_clane_1_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_2, 0, 15),
	.cfg_core_dig_clane_1_rw_hs_tx_3_hs_tx_3_burst_type_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_3, 5, 7),
	.cfg_core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb0_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_4, 0, 2),
	.cfg_core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb1_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_4, 3, 5),
	.cfg_core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb2_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_4, 6, 8),
	.cfg_core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb3_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_4, 9, 11),
	.cfg_core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb4_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_4, 12, 14),
	.cfg_core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb5_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_5, 0, 2),
	.cfg_core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb6_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_5, 3, 5),
	.cfg_core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb7_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_5, 6, 8),
	.cfg_core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb8_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_5, 9, 11),
	.cfg_core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb9_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_5, 12, 14),
	.cfg_core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb10_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_6, 0, 2),
	.cfg_core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb11_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_6, 3, 5),
	.cfg_core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb12_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_6, 6, 8),
	.cfg_core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb13_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_6, 9, 11),
	.cfg_core_dig_clane_1_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_7, 0, 15),
	.cfg_core_dig_clane_1_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_8, 0, 15),
	.cfg_core_dig_clane_1_rw_hs_tx_9_hs_tx_9_t3post_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_9, 0, 15),
	.cfg_core_dig_clane_1_rw_hs_tx_10_hs_tx_10_tprebegin_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_10, 0, 15),
	.cfg_core_dig_clane_1_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_11, 0, 15),
	.cfg_core_dig_clane_1_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_12, 0, 15),
	.cfg_core_dig_clane_1_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg =
		REG_FIELD(CORE_DIG_CLANE_1_RW_HS_TX_13, 0, 7),
	.cfg_core_dig_clane_2_rw_lp_0_lp_0_ttago_reg = REG_FIELD(CORE_DIG_CLANE_2_RW_LP_0, 8, 11),
	.cfg_core_dig_clane_2_rw_lp_0_lp_0_itminrx_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_LP_0, 12, 15),
	.cfg_core_dig_clane_2_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_0, 0, 15),
	.cfg_core_dig_clane_2_rw_hs_tx_1_hs_tx_1_tpost_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_1, 0, 15),
	.cfg_core_dig_clane_2_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_2, 0, 15),
	.cfg_core_dig_clane_2_rw_hs_tx_3_hs_tx_3_burst_type_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_3, 5, 7),
	.cfg_core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb0_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_4, 0, 2),
	.cfg_core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb1_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_4, 3, 5),
	.cfg_core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb2_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_4, 6, 8),
	.cfg_core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb3_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_4, 9, 11),
	.cfg_core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb4_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_4, 12, 14),
	.cfg_core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb5_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_5, 0, 2),
	.cfg_core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb6_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_5, 3, 5),
	.cfg_core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb7_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_5, 6, 8),
	.cfg_core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb8_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_5, 9, 11),
	.cfg_core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb9_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_5, 12, 14),
	.cfg_core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb10_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_6, 0, 2),
	.cfg_core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb11_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_6, 3, 5),
	.cfg_core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb12_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_6, 6, 8),
	.cfg_core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb13_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_6, 9, 11),
	.cfg_core_dig_clane_2_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_7, 0, 15),
	.cfg_core_dig_clane_2_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_8, 0, 15),
	.cfg_core_dig_clane_2_rw_hs_tx_9_hs_tx_9_t3post_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_9, 0, 15),
	.cfg_core_dig_clane_2_rw_hs_tx_10_hs_tx_10_tprebegin_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_10, 0, 15),
	.cfg_core_dig_clane_2_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_11, 0, 15),
	.cfg_core_dig_clane_2_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_12, 0, 15),
	.cfg_core_dig_clane_2_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg =
		REG_FIELD(CORE_DIG_CLANE_2_RW_HS_TX_13, 0, 7),

	.cfg_core_dig_ioctrl_r_common_ppi_ovr_0_12_phy_state =
		REG_FIELD(CORE_DIG_IOCTRL_R_COMMON_PPI_OVR_0_12, 0, 4),

	/* Telmo */
	.cfg_core_dig_rw_common_1_ocla_data_sel = REG_FIELD(CORE_DIG_RW_COMMON_1, 0, 8),
	.cfg_core_dig_rw_common_3_ocla_clk_sel = REG_FIELD(CORE_DIG_RW_COMMON_3, 0, 8),
	.cfg_ppi_rw_hstx_fifo_cfg_txdatatransferenhs_sel = REG_FIELD(PPI_RW_HSTX_FIFO_CFG, 0, 0),

	/* Unexpected LP data transmission on lane 2 fix */
	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane1_ovr_0_3_i_txrequestesc_d1_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE1_OVR_0_3, 0, 0),
	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane1_ovr_0_4_i_txrequestesc_d1_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE1_OVR_0_4, 0, 0),
	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane2_ovr_0_3_i_txrequestesc_d2_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE2_OVR_0_3, 0, 0),
	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane2_ovr_0_4_i_txrequestesc_d2_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE2_OVR_0_4, 0, 0),
	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane3_ovr_0_3_i_txrequestesc_d3_ovr_val =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE3_OVR_0_3, 0, 0),
	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane3_ovr_0_4_i_txrequestesc_d3_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE3_OVR_0_4, 0, 0),

	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane0_ovr_0_8_i_turnrequest_d0_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE0_OVR_0_8, 3, 3),
	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane0_ovr_0_8_i_turndisable_d0_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE0_OVR_0_8, 4, 4),
	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane1_ovr_0_8_i_turnrequest_d1_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE1_OVR_0_8, 3, 3),
	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane1_ovr_0_8_i_turndisable_d1_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE1_OVR_0_8, 4, 4),
	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane2_ovr_0_8_i_turnrequest_d2_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE2_OVR_0_8, 3, 3),
	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane2_ovr_0_8_i_turndisable_d2_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE2_OVR_0_8, 4, 4),
	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane3_ovr_0_8_i_turnrequest_d3_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE3_OVR_0_8, 3, 3),
	.cfg_core_dig_ioctrl_rw_dphy_ppi_lane3_ovr_0_8_i_turndisable_d3_ovr_en =
		REG_FIELD(CORE_DIG_IOCTRL_RW_DPHY_PPI_LANE3_OVR_0_8, 4, 4),

	/* ds_ss_dsi2host_abp_regbank */
	.cfg_pll_cfg0_rw_pll_atb_sense_sel = REG_FIELD(PLL_CFG0, 0, 0),
	.cfg_pll_cfg0_rw_pll_clksel = REG_FIELD(PLL_CFG0, 1, 2),
	.cfg_pll_cfg0_rw_pll_gp_clk_en = REG_FIELD(PLL_CFG0, 3, 3),
	.cfg_pll_cfg0_rw_pll_opmode = REG_FIELD(PLL_CFG0, 4, 8),
	.cfg_pll_cfg0_rw_pll_meas_iv = REG_FIELD(PLL_CFG0, 9, 26),
	.cfg_pll_cfg0_rw_pll_clkouten_left = REG_FIELD(PLL_CFG0, 27, 27),

	.cfg_pll_cfg1_rw_pll_prg_31_0 = REG_FIELD(PLL_CFG1, 0, 31),

	.cfg_pll_cfg2_rw_pll_prg_32 = REG_FIELD(PLL_CFG2, 0, 0),
	.cfg_pll_cfg2_rw_pll_th1 = REG_FIELD(PLL_CFG2, 4, 13),
	.cfg_pll_cfg2_rw_pll_th2 = REG_FIELD(PLL_CFG2, 14, 21),
	.cfg_pll_cfg2_rw_pll_th3 = REG_FIELD(PLL_CFG2, 22, 29),

	.cfg_pll_cfg3_rw_pll_mint = REG_FIELD(PLL_CFG3, 0, 11),
	.cfg_pll_cfg3_rw_pll_m = REG_FIELD(PLL_CFG3, 12, 23),
	.cfg_pll_cfg3_rw_pll_n = REG_FIELD(PLL_CFG3, 24, 29),
	.cfg_pll_cfg3_rw_pll_fracn_cfg_update_en = REG_FIELD(PLL_CFG3, 30, 30),
	.cfg_pll_cfg3_rw_pll_fracn_en = REG_FIELD(PLL_CFG3, 31, 31),

	.cfg_pll_cfg4_rw_pll_frac_den = REG_FIELD(PLL_CFG4, 0, 15),
	.cfg_pll_cfg5_rw_pll_stepsize = REG_FIELD(PLL_CFG5, 0, 20),

	.cfg_pll_cfg6_rw_pll_ssc_en = REG_FIELD(PLL_CFG6, 0, 0),
	.cfg_pll_cfg6_rw_pll_spread_type = REG_FIELD(PLL_CFG6, 1, 2),
	.cfg_pll_cfg6_rw_pll_ssc_peak = REG_FIELD(PLL_CFG6, 3, 22),

	.cfg_pll_cfg7_rw_pll_frac_quot = REG_FIELD(PLL_CFG7, 0, 15),
	.cfg_pll_cfg7_rw_pll_frac_rem = REG_FIELD(PLL_CFG7, 16, 31),

	/* PLL Charge Pump configuration registers */
	.cfg_pll_cp_rw_pll_vco_cntrl_0_2 = REG_FIELD(PLL_CP, 0, 2),
	.cfg_pll_cp_rw_pll_vco_cntrl_3_5 = REG_FIELD(PLL_CP, 3, 5),
	.cfg_pll_cp_rw_pll_prop_cntrl = REG_FIELD(PLL_CP, 6, 11),
	.cfg_pll_cp_rw_pll_int_cntrl = REG_FIELD(PLL_CP, 12, 17),
	.cfg_pll_cp_rw_pll_gmp_cntrl = REG_FIELD(PLL_CP, 18, 19),
	.cfg_pll_cp_rw_pll_cpbias_cntrl = REG_FIELD(PLL_CP, 20, 26),

	/* PHY status */
	.cfg_phy_sts_r_phy_ready = REG_FIELD(PHY_STS, 0, 0),
	.cfg_phy_sts_r_pll_lock = REG_FIELD(PHY_STS, 1, 1),
	.cfg_phy_sts_r_pll_vpl_det = REG_FIELD(PHY_STS, 2, 2),

	.cfg_phy_ctrl0_rw_phy_rst_n = REG_FIELD(PHY_CTRL0, 0, 0),
	.cfg_phy_ctrl0_rw_shutdown_n = REG_FIELD(PHY_CTRL0, 1, 1),
	.cfg_phy_ctrl0_rw_phy_mode = REG_FIELD(PHY_CTRL0, 8, 8),
	.cfg_phy_ctrl0_rw_test_stop_clk_en = REG_FIELD(PHY_CTRL0, 16, 16),
	.cfg_phy_ctrl0_rw_bist_en = REG_FIELD(PHY_CTRL0, 17, 17),

	.cfg_phy_extended_ctr0_rw_forcerxmode_dck = REG_FIELD(PHY_EXTENDED_CTRL0, 0, 0),
	.cfg_phy_extended_ctr0_rw_forcerxmode_0 = REG_FIELD(PHY_EXTENDED_CTRL0, 1, 1),
	.cfg_phy_extended_ctr0_rw_forcerxmode_1 = REG_FIELD(PHY_EXTENDED_CTRL0, 2, 2),
	.cfg_phy_extended_ctr0_rw_forcerxmode_2 = REG_FIELD(PHY_EXTENDED_CTRL0, 3, 3),
	.cfg_phy_extended_ctr0_rw_forcerxmode_3 = REG_FIELD(PHY_EXTENDED_CTRL0, 4, 4),
	.cfg_phy_extended_ctr0_rw_turndisable_0 = REG_FIELD(PHY_EXTENDED_CTRL0, 5, 5),
	.cfg_phy_extended_ctr0_rw_turndisable_1 = REG_FIELD(PHY_EXTENDED_CTRL0, 6, 6),
	.cfg_phy_extended_ctr0_rw_turndisable_2 = REG_FIELD(PHY_EXTENDED_CTRL0, 7, 7),
	.cfg_phy_extended_ctr0_rw_turndisable_3 = REG_FIELD(PHY_EXTENDED_CTRL0, 8, 8),

	.cfg_phy_extended_ctr1_rw_enable0 = REG_FIELD(PHY_EXTENDED_CTRL1, 0, 0),
	.cfg_phy_extended_ctr1_rw_enable1 = REG_FIELD(PHY_EXTENDED_CTRL1, 1, 1),
	.cfg_phy_extended_ctr1_rw_enable2 = REG_FIELD(PHY_EXTENDED_CTRL1, 2, 2),
	.cfg_phy_extended_ctr1_rw_enable3 = REG_FIELD(PHY_EXTENDED_CTRL1, 3, 3),
	.cfg_phy_extended_ctr1_rw_enable_dck = REG_FIELD(PHY_EXTENDED_CTRL1, 4, 4),
	.cfg_phy_extended_ctr1_rw_enable_ov_en = REG_FIELD(PHY_EXTENDED_CTRL1, 5, 5),
	.cfg_phy_extended_ctr1_rw_forcetxstopmode_dck = REG_FIELD(PHY_EXTENDED_CTRL1, 7, 7),
	.cfg_phy_extended_ctr1_rw_forcetxstopmode_0 = REG_FIELD(PHY_EXTENDED_CTRL1, 8, 8),
	.cfg_phy_extended_ctr1_rw_forcetxstopmode_1 = REG_FIELD(PHY_EXTENDED_CTRL1, 9, 9),
	.cfg_phy_extended_ctr1_rw_forcetxstopmode_2 = REG_FIELD(PHY_EXTENDED_CTRL1, 10, 10),
	.cfg_phy_extended_ctr1_rw_forcetxstopmode_3 = REG_FIELD(PHY_EXTENDED_CTRL1, 11, 11),
	.cfg_phy_extended_ctr1_rw_txdatawidthhs_0 = REG_FIELD(PHY_EXTENDED_CTRL1, 16, 17),
	.cfg_phy_extended_ctr1_rw_txdatawidthhs_1 = REG_FIELD(PHY_EXTENDED_CTRL1, 18, 19),
	.cfg_phy_extended_ctr1_rw_txdatawidthhs_2 = REG_FIELD(PHY_EXTENDED_CTRL1, 20, 21),
	.cfg_phy_extended_ctr1_rw_txdatawidthhs_3 = REG_FIELD(PHY_EXTENDED_CTRL1, 22, 23),
	.cfg_phy_extended_ctr1_rw_cont_en = REG_FIELD(PHY_EXTENDED_CTRL1, 28, 28),
	.cfg_phy_extended_ctr1_rw_cont_en_ov_en = REG_FIELD(PHY_EXTENDED_CTRL1, 31, 31),

	.cfg_phy_common9_reg_lp_dco_clk_dyn_gating = REG_FIELD(CORE_DIG_RW_COMMON_9, 8, 8),
	.cfg_phy_startup_cfg3_reg = REG_FIELD(PPI_RW_PLL_STARTUP_CFG_3, 0, 31),
};

static int dw_cdphy_regmap_fields_init(struct dw_mipi_cdphy_g301 *cdphy_g301)
{
	struct dw_mipi_cdphy *cdphy = cdphy_g301->cdphy;
	struct platform_device *pdev = cdphy->pdev;
	struct device *dev = &pdev->dev;

	const struct dw_mipi_phy_g301_variant *variant = &dw_mipi_phy_g301_lca01_variant;

	/* dwc_mipi_cdphy_tx_4l3t_ns */
	INIT_FIELD(ppi_startup_rw_common_dphy_2_rcal_addr, base_regs);
	INIT_FIELD(ppi_startup_rw_common_dphy_3_pll_start_addr, base_regs);
	INIT_FIELD(ppi_startup_rw_common_dphy_6_lp_dco_cal_addr, base_regs);
	INIT_FIELD(ppi_startup_rw_common_dphy_a_hibernate_addr, base_regs);
	INIT_FIELD(ppi_startup_rw_common_dphy_10_phy_ready_addr, base_regs);
	INIT_FIELD(ppi_startup_rw_common_startup_1_1_phy_ready_dly, base_regs);
	INIT_FIELD(ppi_calibctrl_rw_common_bg_0_bg_max_counter, base_regs);
	INIT_FIELD(ppi_rw_lpdcocal_timebase_lpcdcocal_timebase, base_regs);
	INIT_FIELD(ppi_rw_lpdcocal_nref_lpcdcocal_nref, base_regs);
	INIT_FIELD(ppi_rw_lpdcocal_nref_range_lpcdcocal_nref_range, base_regs);
	INIT_FIELD(ppi_rw_lpdcocal_twait_config_lpcdcocal_twait_coarse, base_regs);
	INIT_FIELD(ppi_rw_lpdcocal_twait_config_lpcdcocal_twait_pon, base_regs);
	INIT_FIELD(ppi_rw_lpdcocal_vt_config_lpcdcocal_vt_tracking_en, base_regs);
	INIT_FIELD(ppi_rw_lpdcocal_vt_config_lpcdcocal_use_ideal_nref, base_regs);
	INIT_FIELD(ppi_rw_lpdcocal_vt_config_lpcdcocal_vt_nref_range, base_regs);
	INIT_FIELD(ppi_rw_lpdcocal_vt_config_lpcdcocal_twait_fine, base_regs);
	INIT_FIELD(ppi_rw_lpdcocal_coarse_cfg_ncoarse_start, base_regs);
	INIT_FIELD(ppi_rw_common_cfg_cfg_clk_div_factor, base_regs);
	INIT_FIELD(ppi_rw_termcal_cfg_0_termcal_timer, base_regs);
	INIT_FIELD(ppi_rw_pll_startup_cfg_0_pll_rst_time, base_regs);
	INIT_FIELD(ppi_rw_pll_startup_cfg_1_pll_gear_shift_time, base_regs);
	INIT_FIELD(ppi_rw_pll_startup_cfg_2_pll_lock_det_time, base_regs);
	INIT_FIELD(ppi_rw_pll_startup_cfg_4_ssc_reset_n, base_regs);
	INIT_FIELD(ppi_rw_tx_hibernate_cfg_0_hibernate_exit, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane0_ctrl_2_2_oa_lane0_sel_lane_cfg, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_hstx_sel_phase0, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_hstx_eqa, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_hstx_sel_clklb, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_lptx_din_dn_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_lptx_din_dp_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane0_ctrl_2_4_oa_lane0_hstx_eqb, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane0_ctrl_2_4_oa_lane0_lptx_din_dn_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane0_ctrl_2_4_oa_lane0_lptx_din_dp_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane0_ctrl_2_7_oa_lane0_lptx_en_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane0_ctrl_2_7_oa_lane0_lptx_pon_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane0_ctrl_2_8_oa_lane0_lptx_en_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane0_ctrl_2_8_oa_lane0_lptx_pon_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane1_ctrl_2_2_oa_lane1_sel_lane_cfg, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_hstx_sel_phase0, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_hstx_eqa, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_hstx_sel_clklb, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_lptx_din_dn_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_lptx_din_dp_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane1_ctrl_2_4_oa_lane1_hstx_eqb, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane1_ctrl_2_4_oa_lane1_lptx_din_dn_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane1_ctrl_2_4_oa_lane1_lptx_din_dp_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane1_ctrl_2_7_oa_lane1_lptx_en_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane1_ctrl_2_7_oa_lane1_lptx_pon_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane1_ctrl_2_8_oa_lane1_lptx_en_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane1_ctrl_2_8_oa_lane1_lptx_pon_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_2_oa_lane2_sel_lane_cfg, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_hstx_sel_phase0, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_hstx_eqa, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_hstx_sel_clklb, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_lptx_din_dn_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_lptx_din_dp_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_4_oa_lane2_hstx_eqb, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_4_oa_lane2_lptx_din_dn_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_4_oa_lane2_lptx_din_dp_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lptx_en_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lptx_pon_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lprx_lp_pon_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lprx_cd_pon_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lprx_lp_pon_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lprx_cd_pon_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lptx_en_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lptx_pon_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane3_ctrl_2_2_oa_lane3_sel_lane_cfg, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_hstx_sel_phase0, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_hstx_eqa, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_hstx_sel_clklb, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_lptx_din_dn_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_lptx_din_dp_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane3_ctrl_2_4_oa_lane3_hstx_eqb, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane3_ctrl_2_4_oa_lane3_lptx_din_dn_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane3_ctrl_2_4_oa_lane3_lptx_din_dp_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane3_ctrl_2_7_oa_lane3_lptx_en_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane3_ctrl_2_7_oa_lane3_lptx_pon_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane3_ctrl_2_8_oa_lane3_lptx_en_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane3_ctrl_2_8_oa_lane3_lptx_pon_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane4_ctrl_2_2_oa_lane4_sel_lane_cfg, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_hstx_sel_phase0, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_hstx_eqa, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_hstx_sel_clklb, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_lptx_din_dn_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_lptx_din_dp_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane4_ctrl_2_4_oa_lane4_hstx_eqb, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane4_ctrl_2_4_oa_lane4_lptx_din_dn_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane4_ctrl_2_4_oa_lane4_lptx_din_dp_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane4_ctrl_2_7_oa_lane4_lptx_en_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane4_ctrl_2_7_oa_lane4_lptx_pon_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane4_ctrl_2_8_oa_lane4_lptx_en_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_lane4_ctrl_2_8_oa_lane4_lptx_pon_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_0_oa_cb_hstxlb_dco_clk90_en_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_1_oa_cb_hstxlb_dco_clk0_en_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_2_oa_cb_pll_bustiez, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_3_oa_cb_hstxlb_dco_clk0_en_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_3_oa_cb_hstxlb_dco_clk90_en_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_4_oa_cb_sel_vcomm_prog, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_4_oa_cb_cal_sink_en_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_5_oa_cb_sel_45ohm_50ohm, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_5_oa_cb_cal_sink_en_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_6_oa_cb_hstxlb_dco_pon_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_6_oa_cb_hstxlb_dco_en_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_6_oa_cb_hstxlb_dco_tune_clkdig_en_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_cb_hstxlb_dco_en_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_cb_hstxlb_dco_tune_clkdig_en_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_setr_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_setr_calib_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_setra_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_setrb_ovr_en, base_regs);
	INIT_FIELD(core_dig_anactrl_rw_common_anactrl_0_cb_lp_dco_en_dly, base_regs);
	INIT_FIELD(core_dig_anactrl_rw_common_anactrl_2_global_ulps_ovr_en, base_regs);
	INIT_FIELD(core_dig_anactrl_rw_common_anactrl_2_global_ulps_ovr_val, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_cfg_0_cfg_0_lp_pin_swap_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_cfg_0_cfg_0_hs_pin_swap_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_lp_0_lp_0_itminrx_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_lp_2_lp_2_filter_input_sampling_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_hs_tx_3_hs_tx_3_pin_swap_reg, base_regs);
	INIT_FIELD(core_dig_dlane_0_rw_lp_0_lp_0_ttago_reg, base_regs);
	INIT_FIELD(core_dig_dlane_0_rw_lp_0_lp_0_itminrx_reg, base_regs);
	INIT_FIELD(core_dig_dlane_0_rw_lp_2_lp_2_filter_input_sampling_reg, base_regs);
	INIT_FIELD(core_dig_dlane_0_rw_hs_tx_0_hs_tx_0_thstrail_reg, base_regs);
	INIT_FIELD(core_dig_dlane_0_rw_hs_tx_1_hs_tx_1_thszero_reg, base_regs);
	INIT_FIELD(core_dig_dlane_0_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg, base_regs);
	INIT_FIELD(core_dig_dlane_0_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_0_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_0_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_0_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_0_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_0_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_0_rw_hs_tx_sh_11_sh_sel_reg, base_regs);
	INIT_FIELD(core_dig_dlane_1_rw_lp_0_lp_0_ttago_reg, base_regs);
	INIT_FIELD(core_dig_dlane_1_rw_lp_0_lp_0_itminrx_reg, base_regs);
	INIT_FIELD(core_dig_dlane_1_rw_lp_2_lp_2_filter_input_sampling_reg, base_regs);
	INIT_FIELD(core_dig_dlane_1_rw_hs_tx_0_hs_tx_0_thstrail_reg, base_regs);
	INIT_FIELD(core_dig_dlane_1_rw_hs_tx_1_hs_tx_1_thszero_reg, base_regs);
	INIT_FIELD(core_dig_dlane_1_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg, base_regs);
	INIT_FIELD(core_dig_dlane_1_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_1_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_1_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_1_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_1_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_1_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_1_rw_hs_tx_sh_11_sh_sel_reg, base_regs);
	INIT_FIELD(core_dig_dlane_2_rw_lp_0_lp_0_ttago_reg, base_regs);
	INIT_FIELD(core_dig_dlane_2_rw_lp_0_lp_0_itminrx_reg, base_regs);
	INIT_FIELD(core_dig_dlane_2_rw_lp_2_lp_2_filter_input_sampling_reg, base_regs);
	INIT_FIELD(core_dig_dlane_2_rw_hs_tx_0_hs_tx_0_thstrail_reg, base_regs);
	INIT_FIELD(core_dig_dlane_2_rw_hs_tx_1_hs_tx_1_thszero_reg, base_regs);
	INIT_FIELD(core_dig_dlane_2_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg, base_regs);
	INIT_FIELD(core_dig_dlane_2_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_2_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_2_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_2_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_2_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_2_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_2_rw_hs_tx_sh_11_sh_sel_reg, base_regs);
	INIT_FIELD(core_dig_dlane_3_rw_lp_0_lp_0_ttago_reg, base_regs);
	INIT_FIELD(core_dig_dlane_3_rw_lp_0_lp_0_itminrx_reg, base_regs);
	INIT_FIELD(core_dig_dlane_3_rw_lp_2_lp_2_filter_input_sampling_reg, base_regs);
	INIT_FIELD(core_dig_dlane_3_rw_hs_tx_0_hs_tx_0_thstrail_reg, base_regs);
	INIT_FIELD(core_dig_dlane_3_rw_hs_tx_1_hs_tx_1_thszero_reg, base_regs);
	INIT_FIELD(core_dig_dlane_3_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg, base_regs);
	INIT_FIELD(core_dig_dlane_3_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_3_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_3_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_3_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_3_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_3_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_3_rw_hs_tx_sh_11_sh_sel_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_hs_tx_0_hs_tx_0_thstrail_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_hs_tx_1_hs_tx_1_thszero_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_hs_tx_2_hs_tx_2_tclkpre_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_hs_tx_8_hs_tx_8_tclkpost_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg, base_regs);
	INIT_FIELD(core_dig_dlane_clk_rw_hs_tx_sh_11_sh_sel_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_lp_0_lp_0_ttago_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_lp_0_lp_0_itminrx_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_1_hs_tx_1_tpost_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_3_hs_tx_3_burst_type_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb0_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb1_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb2_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb3_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb4_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb5_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb6_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb7_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb8_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb9_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb10_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb11_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb12_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb13_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_9_hs_tx_9_t3post_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_10_hs_tx_10_tprebegin_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_0_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_cfg_0_cfg_0_lp_pin_swap_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_6_hs_tx_6_pin_swap_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_lp_0_lp_0_ttago_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_lp_0_lp_0_itminrx_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_1_hs_tx_1_tpost_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_3_hs_tx_3_burst_type_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb0_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb1_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb2_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb3_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb4_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb5_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb6_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb7_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb8_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb9_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb10_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb11_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb12_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb13_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_9_hs_tx_9_t3post_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_10_hs_tx_10_tprebegin_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_1_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_lp_0_lp_0_ttago_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_lp_0_lp_0_itminrx_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_1_hs_tx_1_tpost_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_3_hs_tx_3_burst_type_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb0_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb1_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb2_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb3_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb4_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb5_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb6_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb7_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb8_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb9_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb10_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb11_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb12_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb13_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_9_hs_tx_9_t3post_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_10_hs_tx_10_tprebegin_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg, base_regs);
	INIT_FIELD(core_dig_clane_2_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg, base_regs);

	INIT_FIELD(core_dig_ioctrl_r_common_ppi_ovr_0_12_phy_state, base_regs);

	/* Telmo */
	INIT_FIELD(core_dig_rw_common_1_ocla_data_sel, base_regs);
	INIT_FIELD(core_dig_rw_common_3_ocla_clk_sel, base_regs);
	INIT_FIELD(ppi_rw_hstx_fifo_cfg_txdatatransferenhs_sel, base_regs);

	/* Unexpected LP data transmission on lane 2 fix */
	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane1_ovr_0_3_i_txrequestesc_d1_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane1_ovr_0_4_i_txrequestesc_d1_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane2_ovr_0_3_i_txrequestesc_d2_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane2_ovr_0_4_i_txrequestesc_d2_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane3_ovr_0_3_i_txrequestesc_d3_ovr_val, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane3_ovr_0_4_i_txrequestesc_d3_ovr_en, base_regs);

	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane0_ovr_0_8_i_turnrequest_d0_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane0_ovr_0_8_i_turndisable_d0_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane1_ovr_0_8_i_turnrequest_d1_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane1_ovr_0_8_i_turndisable_d1_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane2_ovr_0_8_i_turnrequest_d2_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane2_ovr_0_8_i_turndisable_d2_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane3_ovr_0_8_i_turnrequest_d3_ovr_en, base_regs);
	INIT_FIELD(core_dig_ioctrl_rw_dphy_ppi_lane3_ovr_0_8_i_turndisable_d3_ovr_en, base_regs);

	INIT_FIELD(pll_cfg0_rw_pll_atb_sense_sel, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg0_rw_pll_clksel, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg0_rw_pll_gp_clk_en, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg0_rw_pll_opmode, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg0_rw_pll_meas_iv, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg0_rw_pll_clkouten_left, u0_apb_regbank_regs);

	INIT_FIELD(pll_cfg1_rw_pll_prg_31_0, u0_apb_regbank_regs);

	INIT_FIELD(pll_cfg2_rw_pll_prg_32, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg2_rw_pll_th1, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg2_rw_pll_th2, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg2_rw_pll_th3, u0_apb_regbank_regs);

	INIT_FIELD(pll_cfg3_rw_pll_mint, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg3_rw_pll_m, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg3_rw_pll_n, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg3_rw_pll_fracn_cfg_update_en, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg3_rw_pll_fracn_en, u0_apb_regbank_regs);

	INIT_FIELD(pll_cfg4_rw_pll_frac_den, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg5_rw_pll_stepsize, u0_apb_regbank_regs);

	INIT_FIELD(pll_cfg6_rw_pll_ssc_en, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg6_rw_pll_spread_type, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg6_rw_pll_ssc_peak, u0_apb_regbank_regs);

	INIT_FIELD(pll_cfg7_rw_pll_frac_quot, u0_apb_regbank_regs);
	INIT_FIELD(pll_cfg7_rw_pll_frac_rem, u0_apb_regbank_regs);

	INIT_FIELD(pll_cp_rw_pll_vco_cntrl_0_2, u0_apb_regbank_regs);
	INIT_FIELD(pll_cp_rw_pll_vco_cntrl_3_5, u0_apb_regbank_regs);
	INIT_FIELD(pll_cp_rw_pll_prop_cntrl, u0_apb_regbank_regs);
	INIT_FIELD(pll_cp_rw_pll_int_cntrl, u0_apb_regbank_regs);
	INIT_FIELD(pll_cp_rw_pll_gmp_cntrl, u0_apb_regbank_regs);
	INIT_FIELD(pll_cp_rw_pll_cpbias_cntrl, u0_apb_regbank_regs);

	INIT_FIELD(phy_sts_r_phy_ready, u0_apb_regbank_regs);
	INIT_FIELD(phy_sts_r_pll_lock, u0_apb_regbank_regs);
	INIT_FIELD(phy_sts_r_pll_vpl_det, u0_apb_regbank_regs);

	INIT_FIELD(phy_ctrl0_rw_phy_rst_n, u0_apb_regbank_regs);
	INIT_FIELD(phy_ctrl0_rw_shutdown_n, u0_apb_regbank_regs);
	INIT_FIELD(phy_ctrl0_rw_phy_mode, u0_apb_regbank_regs);
	INIT_FIELD(phy_ctrl0_rw_test_stop_clk_en, u0_apb_regbank_regs);
	INIT_FIELD(phy_ctrl0_rw_bist_en, u0_apb_regbank_regs);

	INIT_FIELD(phy_extended_ctr0_rw_forcerxmode_dck, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr0_rw_forcerxmode_0, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr0_rw_forcerxmode_1, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr0_rw_forcerxmode_2, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr0_rw_forcerxmode_3, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr0_rw_turndisable_0, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr0_rw_turndisable_1, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr0_rw_turndisable_2, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr0_rw_turndisable_3, u0_apb_regbank_regs);

	INIT_FIELD(phy_extended_ctr1_rw_enable0, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_enable1, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_enable2, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_enable3, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_enable_dck, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_enable_ov_en, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_forcetxstopmode_dck, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_forcetxstopmode_0, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_forcetxstopmode_1, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_forcetxstopmode_2, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_forcetxstopmode_3, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_txdatawidthhs_0, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_txdatawidthhs_1, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_txdatawidthhs_2, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_txdatawidthhs_3, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_cont_en, u0_apb_regbank_regs);
	INIT_FIELD(phy_extended_ctr1_rw_cont_en_ov_en, u0_apb_regbank_regs);

	INIT_FIELD(phy_common9_reg_lp_dco_clk_dyn_gating, base_regs);
	INIT_FIELD(phy_startup_cfg3_reg, base_regs);

	return 0;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)

#define ADD_PRESET_ITEM(item)                                                                 \
	do {                                                                                  \
		if (dw_cdphy_preset_add_item(&cdphy_g301->preset_list, dev, #item,            \
					     cdphy_g301->field_##item, apply_timing)) {       \
			dev_err(dev, "%s: failed to add preset item %s\n", __func__, #item);  \
			return -ENOMEM;                                                       \
		}                                                                             \
	} while (0)

static int dw_cdphy_preset_add_item(struct list_head *preset_list, struct device *dev,
				    const char *name, struct regmap_field *regmap_field,
				    enum dw_cdphy_preset_apply_timing apply_timing)
{
	struct dw_cdphy_preset_item *item;

	item = devm_kzalloc(dev, sizeof(struct dw_cdphy_preset_item), GFP_KERNEL);
	if (!item)
		return -ENOMEM;

	item->name = name;
	item->regmap_field = regmap_field;
	item->val = INVALID_PRESET_VAL;
	item->apply_timing = apply_timing;

	list_add_tail(&item->list, preset_list);

	return 0;
}

static int dw_cdphy_preset_config_init(struct dw_mipi_cdphy_g301 *cdphy_g301)
{
	struct dentry *preset_dir;
	struct dw_cdphy_preset_item *preset_item;
	struct device *dev;
	enum dw_cdphy_preset_apply_timing apply_timing;

	if (!cdphy_g301 || !cdphy_g301->cdphy || !cdphy_g301->cdphy->pdev)
		return -ENODEV;
	dev = &cdphy_g301->cdphy->pdev->dev;

	preset_dir = debugfs_create_dir("preset", cdphy_g301->cdphy->debugfs);
	if (IS_ERR_OR_NULL(preset_dir)) {
		pr_err("%s failed to create preset folder\n", __func__);
		return -EIO;
	}

	apply_timing = DPHY_PRESET_APPLY_BEFORE_RST;
	ADD_PRESET_ITEM(core_dig_ioctrl_rw_afe_cb_ctrl_2_4_oa_cb_sel_vcomm_prog);

	apply_timing = DPHY_PRESET_APPLY_AFTER_READY;
	ADD_PRESET_ITEM(core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_hstx_eqa);
	ADD_PRESET_ITEM(core_dig_ioctrl_rw_afe_lane0_ctrl_2_4_oa_lane0_hstx_eqb);
	ADD_PRESET_ITEM(core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_hstx_eqa);
	ADD_PRESET_ITEM(core_dig_ioctrl_rw_afe_lane1_ctrl_2_4_oa_lane1_hstx_eqb);
	ADD_PRESET_ITEM(core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_hstx_eqa);
	ADD_PRESET_ITEM(core_dig_ioctrl_rw_afe_lane2_ctrl_2_4_oa_lane2_hstx_eqb);
	ADD_PRESET_ITEM(core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_hstx_eqa);
	ADD_PRESET_ITEM(core_dig_ioctrl_rw_afe_lane3_ctrl_2_4_oa_lane3_hstx_eqb);
	ADD_PRESET_ITEM(core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_hstx_eqa);
	ADD_PRESET_ITEM(core_dig_ioctrl_rw_afe_lane4_ctrl_2_4_oa_lane4_hstx_eqb);

	list_for_each_entry(preset_item, &cdphy_g301->preset_list, list) {
		debugfs_create_u32(preset_item->name, 0664, preset_dir, &preset_item->val);
	}

	return 0;
}

static int dw_cdphy_preset_config_load(struct dw_mipi_cdphy_g301 *cdphy_g301,
				       enum dw_cdphy_preset_apply_timing timing)
{
	struct dw_cdphy_preset_item *preset_item;
	struct device *dev;

	if (!cdphy_g301 || !cdphy_g301->cdphy || !cdphy_g301->cdphy->pdev)
		return -ENODEV;
	dev = &cdphy_g301->cdphy->pdev->dev;

	list_for_each_entry(preset_item, &cdphy_g301->preset_list, list) {
		if (preset_item->val == INVALID_PRESET_VAL || !preset_item->regmap_field)
			continue;
		if (preset_item->apply_timing != timing)
			continue;
		dev_dbg(dev, "%s applying %s with %u at timing %u\n", __func__, preset_item->name,
			preset_item->val, timing);
		dw_cdphy_write_field(preset_item->regmap_field, preset_item->val);
	}

	return 0;
}

static __maybe_unused void dw_cdphy_lptx_en_pon_override_val(struct dw_mipi_cdphy_g301 *cdphy_g301,
							     u32 lane_mask, u32 val)
{
	if (lane_mask & BIT(0)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane0_ctrl_2_7_oa_lane0_lptx_en_ovr_val, val);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane0_ctrl_2_7_oa_lane0_lptx_pon_ovr_val, val);
	}
	if (lane_mask & BIT(1)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane1_ctrl_2_7_oa_lane1_lptx_en_ovr_val, val);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane1_ctrl_2_7_oa_lane1_lptx_pon_ovr_val, val);
	}
	if (lane_mask & BIT(2)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lptx_en_ovr_val, val);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lptx_pon_ovr_val, val);
	}
	if (lane_mask & BIT(3)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane3_ctrl_2_7_oa_lane3_lptx_en_ovr_val, val);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane3_ctrl_2_7_oa_lane3_lptx_pon_ovr_val, val);
	}
	if (lane_mask & BIT(4)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane4_ctrl_2_7_oa_lane4_lptx_en_ovr_val, val);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane4_ctrl_2_7_oa_lane4_lptx_pon_ovr_val, val);
	}
}

static __maybe_unused void dw_cdphy_lptx_en_pon_override_en(struct dw_mipi_cdphy_g301 *cdphy_g301,
							    u32 lane_mask, u32 en)
{
	if (lane_mask & BIT(0)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane0_ctrl_2_8_oa_lane0_lptx_en_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane0_ctrl_2_8_oa_lane0_lptx_pon_ovr_en, en);
	}
	if (lane_mask & BIT(1)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane1_ctrl_2_8_oa_lane1_lptx_en_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane1_ctrl_2_8_oa_lane1_lptx_pon_ovr_en, en);
	}
	if (lane_mask & BIT(2)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lptx_en_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lptx_pon_ovr_en, en);
	}
	if (lane_mask & BIT(3)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane3_ctrl_2_8_oa_lane3_lptx_en_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane3_ctrl_2_8_oa_lane3_lptx_pon_ovr_en, en);
	}
	if (lane_mask & BIT(4)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane4_ctrl_2_8_oa_lane4_lptx_en_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane4_ctrl_2_8_oa_lane4_lptx_pon_ovr_en, en);
	}
}

static __maybe_unused void dw_cdphy_lptx_din_override_val(struct dw_mipi_cdphy_g301 *cdphy_g301,
							  u32 lane_mask, u32 dp_val, u32 dn_val)
{
	if (lane_mask & BIT(0)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_lptx_din_dp_ovr_val, dp_val);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_lptx_din_dn_ovr_val, dn_val);
	}
	if (lane_mask & BIT(1)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_lptx_din_dp_ovr_val, dp_val);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_lptx_din_dn_ovr_val, dn_val);
	}
	if (lane_mask & BIT(2)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_lptx_din_dp_ovr_val, dp_val);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_lptx_din_dn_ovr_val, dn_val);
	}
	if (lane_mask & BIT(3)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_lptx_din_dp_ovr_val, dp_val);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_lptx_din_dn_ovr_val, dn_val);
	}
	if (lane_mask & BIT(4)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_lptx_din_dp_ovr_val, dp_val);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_lptx_din_dn_ovr_val, dn_val);
	}
}

static __maybe_unused void dw_cdphy_lptx_din_override_en(struct dw_mipi_cdphy_g301 *cdphy_g301,
							 u32 lane_mask, u32 en)
{
	if (lane_mask & BIT(0)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane0_ctrl_2_4_oa_lane0_lptx_din_dp_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane0_ctrl_2_4_oa_lane0_lptx_din_dn_ovr_en, en);
	}
	if (lane_mask & BIT(1)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane1_ctrl_2_4_oa_lane1_lptx_din_dp_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane1_ctrl_2_4_oa_lane1_lptx_din_dn_ovr_en, en);
	}
	if (lane_mask & BIT(2)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_4_oa_lane2_lptx_din_dp_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_4_oa_lane2_lptx_din_dn_ovr_en, en);
	}
	if (lane_mask & BIT(3)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane3_ctrl_2_4_oa_lane3_lptx_din_dp_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane3_ctrl_2_4_oa_lane3_lptx_din_dn_ovr_en, en);
	}
	if (lane_mask & BIT(4)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane4_ctrl_2_4_oa_lane4_lptx_din_dp_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane4_ctrl_2_4_oa_lane4_lptx_din_dn_ovr_en, en);
	}
}

static __maybe_unused void dw_cdphy_forcetxstopmode(struct dw_mipi_cdphy_g301 *cdphy_g301,
						    u32 lane_mask, u32 en)
{
	if (lane_mask & BIT(0))
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_0, en);
	if (lane_mask & BIT(1))
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_1, en);
	if (lane_mask & BIT(2))
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_2, en);
	if (lane_mask & BIT(3))
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_3, en);
	if (lane_mask & BIT(4))
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_dck, en);
}

static __maybe_unused void dw_cdphy_turndisable_ovr(struct dw_mipi_cdphy_g301 *cdphy_g301,
						    u32 lane_mask, u32 en)
{
	if (lane_mask & BIT(0)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_dphy_ppi_lane0_ovr_0_8_i_turnrequest_d0_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_dphy_ppi_lane0_ovr_0_8_i_turndisable_d0_ovr_en, en);
	}
	if (lane_mask & BIT(1)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_dphy_ppi_lane1_ovr_0_8_i_turnrequest_d1_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_dphy_ppi_lane1_ovr_0_8_i_turndisable_d1_ovr_en, en);
	}
	if (lane_mask & BIT(2)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_dphy_ppi_lane2_ovr_0_8_i_turnrequest_d2_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_dphy_ppi_lane2_ovr_0_8_i_turndisable_d2_ovr_en, en);
	}
	if (lane_mask & BIT(3)) {
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_dphy_ppi_lane3_ovr_0_8_i_turnrequest_d3_ovr_en, en);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_dphy_ppi_lane3_ovr_0_8_i_turndisable_d3_ovr_en, en);
	}
}
#endif

/* Use doing
 *
 *
 *	init -----> configure ----> power_on
 *
 *	init: performs baisc phy initialization
 *	configure: prepare all data for configure the phy
 *	power_on: configure phy and start sending data on lanes
 *
 */

/* for cfg_clk dependent CSRs, cfg_clk is 38.4 MHz */
static void dw_cdphy_common_config(struct dw_mipi_cdphy_g301 *cdphy_g301)
{
	/* CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_0 -> CB_LP_DCO_EN_DLY */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_anactrl_rw_common_anactrl_0_cb_lp_dco_en_dly, 63);

	/* PPI_STARTUP_RW_COMMON_STARTUP_1_1 -> PHY_READY_DLY */
	dw_cdphy_write_field(cdphy_g301->field_ppi_startup_rw_common_startup_1_1_phy_ready_dly,
			     563);

	/* PPI_STARTUP_RW_COMMON_DPHY_2 -> RCAL_ADDR */
	dw_cdphy_write_field(cdphy_g301->field_ppi_startup_rw_common_dphy_2_rcal_addr, 3);

	/* PPI_STARTUP_RW_COMMON_DPHY_3 -> PLL_START_ADDR */
	dw_cdphy_write_field(cdphy_g301->field_ppi_startup_rw_common_dphy_3_pll_start_addr, 38);

	/* PPI_STARTUP_RW_COMMON_DPHY_6 -> LP_DCO_CAL_ADDR */
	dw_cdphy_write_field(cdphy_g301->field_ppi_startup_rw_common_dphy_6_lp_dco_cal_addr, 16);

	/* PPI_STARTUP_RW_COMMON_DPHY_A -> HIBERNATE_ADDR */
	dw_cdphy_write_field(cdphy_g301->field_ppi_startup_rw_common_dphy_a_hibernate_addr, 33);

	/* CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_2 -> GLOBAL_ULPS_OVR_EN */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_anactrl_rw_common_anactrl_2_global_ulps_ovr_en, 0);

	/* PPI_CALIBCTRL_RW_COMMON_BG_0 -> BG_MAX_COUNTER */
	dw_cdphy_write_field(cdphy_g301->field_ppi_calibctrl_rw_common_bg_0_bg_max_counter, 500);

	/* PPI_RW_TERMCAL_CFG_0 -> TERMCAL_TIMER */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_termcal_cfg_0_termcal_timer, 38);

	/* PPI_RW_LPDCOCAL_TIMEBASE -> LPCDCOCAL_TIMEBASE */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_lpdcocal_timebase_lpcdcocal_timebase, 153);

	/* PPI_RW_LPDCOCAL_NREF -> LPCDCOCAL_NREF */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_lpdcocal_nref_lpcdcocal_nref, 800);

	/* PPI_RW_LPDCOCAL_NREF_RANGE -> LPCDCOCAL_NREF_RANGE */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_lpdcocal_nref_range_lpcdcocal_nref_range, 27);

	/* PPI_RW_LPDCOCAL_TWAIT_CONFIG -> LPCDCOCAL_TWAIT_PON */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_lpdcocal_twait_config_lpcdcocal_twait_pon,
			     127);

	/* PPI_RW_LPDCOCAL_TWAIT_CONFIG -> LPCDCOCAL_TWAIT_COARSE */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_lpdcocal_twait_config_lpcdcocal_twait_coarse,
			     47);

	/* PPI_RW_LPDCOCAL_VT_CONFIG -> LPCDCOCAL_TWAIT_FINE */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_lpdcocal_vt_config_lpcdcocal_twait_fine, 47);

	/* PPI_RW_LPDCOCAL_VT_CONFIG -> LPCDCOCAL_VT_NREF_RANGE */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_lpdcocal_vt_config_lpcdcocal_vt_nref_range,
			     27);

	/* PPI_RW_LPDCOCAL_VT_CONFIG -> LPCDCOCAL_USE_IDEAL_NREF */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_lpdcocal_vt_config_lpcdcocal_use_ideal_nref,
			     1);

	/* PPI_RW_LPDCOCAL_VT_CONFIG -> LPCDCOCAL_VT_TRACKING_EN */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_lpdcocal_vt_config_lpcdcocal_vt_tracking_en,
			     0);

	/* PPI_RW_LPDCOCAL_COARSE_CFG -> NCOARSE_START */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_lpdcocal_coarse_cfg_ncoarse_start, 1);

	/* PPI_RW_PLL_STARTUP_CFG_0 -> PLL_RST_TIME */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_pll_startup_cfg_0_pll_rst_time, 383);

	/* PPI_RW_PLL_STARTUP_CFG_1 -> PLL_GEAR_SHIFT_TIME */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_pll_startup_cfg_1_pll_gear_shift_time, 191);

	/* PPI_RW_PLL_STARTUP_CFG_2 -> PLL_LOCK_DET_TIME */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_pll_startup_cfg_2_pll_lock_det_time, 0);

	/* CORE_DIG_IOCTRL_RW_AFE_LANE?_CTRL_2_3 -> OA_LANE?_HSTX_SEL_CLKLB */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_hstx_sel_clklb, 0);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_hstx_sel_clklb, 0);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_hstx_sel_clklb, 0);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_hstx_sel_clklb, 0);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_hstx_sel_clklb, 0);

	/* PPI_RW_COMMON_CFG -> CFG_CLK_DIV_FACTOR: 8 */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_common_cfg_cfg_clk_div_factor, 3);
}

static void dw_cdphy_specific_dphy_cfg(struct dw_mipi_cdphy_g301 *cdphy_g301)
{
	u32 val = 6; /* Default value for LP_0_TTAGO_REG */
	struct dphy_hs_regs *hs_regs = &cdphy_g301->hs_config.dphy_regs;

	/* CORE_DIG_DLANE_?_RW_LP_0 -> LP_0_TTAGO_REG */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_0_rw_lp_0_lp_0_ttago_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_1_rw_lp_0_lp_0_ttago_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_2_rw_lp_0_lp_0_ttago_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_3_rw_lp_0_lp_0_ttago_reg, val);

	/* CORE_DIG_IOCTRL_RW_AFE_LANE?_CTRL_2_2 -> OA_LANE?_SEL_LANE_CFG */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane0_ctrl_2_2_oa_lane0_sel_lane_cfg, 0);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane1_ctrl_2_2_oa_lane1_sel_lane_cfg, 0);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_2_oa_lane2_sel_lane_cfg, 1);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane3_ctrl_2_2_oa_lane3_sel_lane_cfg, 0);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane4_ctrl_2_2_oa_lane4_sel_lane_cfg, 0);

	/* CORE_DIG_IOCTRL_RW_AFE_LANE?_CTRL_2_3 -> OA_LANE?_HSTX_SEL_PHASE0 */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_hstx_sel_phase0,
		1);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_hstx_sel_phase0,
		1);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_hstx_sel_phase0,
		0);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_hstx_sel_phase0,
		1);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_hstx_sel_phase0,
		1);

	/* CORE_DIG_DLANE_?_RW_HS_TX_0 -> HS_TX_0_THSTRAIL_REG */
	val = hs_regs->core_dig_dlane_n_rw_hs_tx_0_hs_tx_0_thstrail_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_0_rw_hs_tx_0_hs_tx_0_thstrail_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_1_rw_hs_tx_0_hs_tx_0_thstrail_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_2_rw_hs_tx_0_hs_tx_0_thstrail_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_3_rw_hs_tx_0_hs_tx_0_thstrail_reg,
			     val);
	/* CORE_DIG_DLANE_CLK_RW_HS_TX_0 -> HS_TX_0_THSTRAIL_REG */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_0_hs_tx_0_thstrail_reg,
		hs_regs->core_dig_dlane_clk_rw_hs_tx_0_hs_tx_0_thstrail_reg);

	/* CORE_DIG_DLANE_?_RW_HS_TX_1 -> HS_TX_1_THSZERO_REG */
	val = hs_regs->core_dig_dlane_n_rw_hs_tx_1_hs_tx_1_thszero_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_0_rw_hs_tx_1_hs_tx_1_thszero_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_1_rw_hs_tx_1_hs_tx_1_thszero_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_2_rw_hs_tx_1_hs_tx_1_thszero_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_3_rw_hs_tx_1_hs_tx_1_thszero_reg,
			     val);
	/* CORE_DIG_DLANE_CLK_RW_HS_TX_1 -> HS_TX_1_THSZERO_REG */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_1_hs_tx_1_thszero_reg,
		hs_regs->core_dig_dlane_clk_rw_hs_tx_1_hs_tx_1_thszero_reg);

	/* CORE_DIG_DLANE_CLK_RW_HS_TX_2 -> HS_TX_2_TCLKPRE_REG */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_2_hs_tx_2_tclkpre_reg,
		hs_regs->core_dig_dlane_clk_rw_hs_tx_2_hs_tx_2_tclkpre_reg);

	/* CORE_DIG_DLANE_?_RW_HS_TX_3 -> HS_TX_3_TLPTXOVERLAP_REG */
	val = hs_regs->core_dig_dlane_n_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_0_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_1_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_2_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_3_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg,
			     val);
	/* CORE_DIG_DLANE_CLK_RW_HS_TX_3 -> HS_TX_3_TLPTXOVERLAP_REG */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg,
		hs_regs->core_dig_dlane_clk_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg);

	/* CORE_DIG_DLANE_?_RW_HS_TX_4 -> HS_TX_4_TLPX_DCO_REG */
	val = hs_regs->core_dig_dlane_n_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_0_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_1_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_2_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_3_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg,
			     val);
	/* CORE_DIG_DLANE_CLK_RW_HS_TX_4 -> HS_TX_4_TLPX_DCO_REG */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg,
		hs_regs->core_dig_dlane_clk_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg);

	/* CORE_DIG_DLANE_?_RW_HS_TX_5 -> HS_TX_5_THSTRAIL_DCO_REG */
	val = hs_regs->core_dig_dlane_n_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_0_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_1_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_2_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_3_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg,
			     val);
	/* CORE_DIG_DLANE_CLK_RW_HS_TX_5 -> HS_TX_5_THSTRAIL_DCO_REG */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg,
		hs_regs->core_dig_dlane_clk_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg);

	/* CORE_DIG_DLANE_?_RW_HS_TX_6 -> HS_TX_6_TLP11END_DCO_REG */
	val = hs_regs->core_dig_dlane_n_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_0_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_1_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_2_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_3_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg,
			     val);
	/* CORE_DIG_DLANE_CLK_RW_HS_TX_6 -> HS_TX_6_TLP11END_DCO_REG */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg,
		hs_regs->core_dig_dlane_clk_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg);

	/* CORE_DIG_DLANE_CLK_RW_HS_TX_8 -> HS_TX_8_TCLKPOST_REG */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_8_hs_tx_8_tclkpost_reg,
			     hs_regs->core_dig_dlane_clk_rw_hs_tx_8_hs_tx_8_tclkpost_reg);

	/* CORE_DIG_DLANE_?_RW_HS_TX_9 -> HS_TX_9_THSPRPR_DCO_REG */
	val = hs_regs->core_dig_dlane_n_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_0_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_1_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_2_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg,
			     val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_3_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg,
			     val);
	/* CORE_DIG_DLANE_CLK_RW_HS_TX_9 -> HS_TX_9_THSPRPR_DCO_REG */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg,
		hs_regs->core_dig_dlane_clk_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg);

	/* CORE_DIG_DLANE_?_RW_HS_TX_10 -> HS_TX_10_TLP11INIT_DCO_REG */
	val = hs_regs->core_dig_dlane_n_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg;
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_0_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg, val);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_1_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg, val);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_2_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg, val);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_3_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg, val);
	/* CORE_DIG_DLANE_CLK_RW_HS_TX_10 -> HS_TX_10_TLP11INIT_DCO_REG */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg,
		hs_regs->core_dig_dlane_clk_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg);

	/* CORE_DIG_DLANE_?_RW_HS_TX_12 -> HS_TX_12_THSEXIT_DCO_REG */
	val = hs_regs->core_dig_dlane_n_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg;
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_0_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg, val);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_1_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg, val);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_2_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg, val);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_3_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg, val);
	/* CORE_DIG_DLANE_CLK_RW_HS_TX_12 -> HS_TX_12_THSEXIT_DCO_REG */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg,
			     hs_regs->core_dig_dlane_clk_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg);

	/* CORE_DIG_DLANE_?_RW_LP_2 -> LP_2_FILTER_INPUT_SAMPLING_REG */
	val = 0;
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_0_rw_lp_2_lp_2_filter_input_sampling_reg, val);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_1_rw_lp_2_lp_2_filter_input_sampling_reg, val);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_2_rw_lp_2_lp_2_filter_input_sampling_reg, val);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_3_rw_lp_2_lp_2_filter_input_sampling_reg, val);
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_dlane_clk_rw_lp_2_lp_2_filter_input_sampling_reg, val);
}

static void dw_cdphy_specific_cphy_cfg(struct dw_mipi_cdphy_g301 *cdphy_g301)
{
	u32 val = 6; /* Default value for LP_0_TTAGO_REG */
	struct cphy_hs_regs *hs_regs = &cdphy_g301->hs_config.cphy_regs;

	/* CORE_DIG_CLANE_?_RW_LP_0 -> LP_0_TTAGO_REG */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_lp_0_lp_0_ttago_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_lp_0_lp_0_ttago_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_lp_0_lp_0_ttago_reg, val);

	/* CORE_DIG_IOCTRL_RW_AFE_LANE?_CTRL_2_2 -> OA_LANE?_SEL_LANE_CFG */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane0_ctrl_2_2_oa_lane0_sel_lane_cfg, 1);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane1_ctrl_2_2_oa_lane1_sel_lane_cfg, 0);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_2_oa_lane2_sel_lane_cfg, 1);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane3_ctrl_2_2_oa_lane3_sel_lane_cfg, 1);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane4_ctrl_2_2_oa_lane4_sel_lane_cfg, 0);

	/* CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_4 -> OA_CB_SEL_VCOMM_PROG */
	val = 3;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_4_oa_cb_sel_vcomm_prog, val);

	/* CORE_DIG_IOCTRL_RW_AFE_LANE?_CTRL_2_3 -> OA_LANE?_HSTX_SEL_PHASE0 */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_hstx_sel_phase0, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_hstx_sel_phase0, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_hstx_sel_phase0, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_0 -> HS_TX_0_THSEXIT_DCO_REG */
	val = hs_regs->core_dig_clane_n_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_13 -> HS_TX_13_TLPTXOVERLAP_REG */
	val = hs_regs->core_dig_clane_n_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_12 -> HS_TX_12_TLP11INIT_DCO_REG */
	val = hs_regs->core_dig_clane_n_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_11 -> HS_TX_11_TLPX_DCO_REG */
	val = hs_regs->core_dig_clane_n_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_7 -> HS_TX_7_T3PRPR_DCO_REG */
	val = hs_regs->core_dig_clane_n_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_10 -> HS_TX_10_TPREBEGIN_REG */
	val = hs_regs->core_dig_clane_n_rw_hs_tx_10_hs_tx_10_tprebegin_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_10_hs_tx_10_tprebegin_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_10_hs_tx_10_tprebegin_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_10_hs_tx_10_tprebegin_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_1 -> HS_TX_1_TPOST_REG */
	val = hs_regs->core_dig_clane_n_rw_hs_tx_1_hs_tx_1_tpost_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_1_hs_tx_1_tpost_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_1_hs_tx_1_tpost_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_1_hs_tx_1_tpost_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_2 -> HS_TX_2_TCALPREAMBLE_REG */
	val = hs_regs->core_dig_clane_n_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_8 -> HS_TX_8_TLP11END_DCO_REG */
	val = hs_regs->core_dig_clane_n_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_9 -> HS_TX_9_T3POST_DCO_REG */
	val = hs_regs->core_dig_clane_n_rw_hs_tx_9_hs_tx_9_t3post_dco_reg;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_9_hs_tx_9_t3post_dco_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_9_hs_tx_9_t3post_dco_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_9_hs_tx_9_t3post_dco_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_3 -> HS_TX_3_BURST_TYPE_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_3_hs_tx_3_burst_type_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_3_hs_tx_3_burst_type_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_3_hs_tx_3_burst_type_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_4 -> HS_TX_4_PROGSEQSYMB0_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb0_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb0_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb0_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_4 -> HS_TX_4_PROGSEQSYMB1_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb1_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb1_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb1_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_4 -> HS_TX_4_PROGSEQSYMB2_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb2_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb2_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb2_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_4 -> HS_TX_4_PROGSEQSYMB3_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb3_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb3_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb3_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_4 -> HS_TX_4_PROGSEQSYMB4_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb4_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb4_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb4_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_5 -> HS_TX_5_PROGSEQSYMB5_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb5_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb5_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb5_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_5 -> HS_TX_5_PROGSEQSYMB6_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb6_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb6_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb6_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_5 -> HS_TX_5_PROGSEQSYMB7_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb7_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb7_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb7_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_5 -> HS_TX_5_PROGSEQSYMB8_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb8_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb8_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb8_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_5 -> HS_TX_5_PROGSEQSYMB9_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb9_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb9_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb9_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_6 -> HS_TX_6_PROGSEQSYMB10_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb10_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb10_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb10_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_6 -> HS_TX_6_PROGSEQSYMB11_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb11_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb11_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb11_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_6 -> HS_TX_6_PROGSEQSYMB12_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb12_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb12_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb12_reg, val);

	/* CORE_DIG_CLANE_?_RW_HS_TX_6 -> HS_TX_6_PROGSEQSYMB13_REG */
	val = 0;
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb13_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb13_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb13_reg, val);
}

static void dw_cdphy_extra_config(struct dw_mipi_cdphy_g301 *cdphy_g301)
{
	u32 val = 1; /* Default value for LP_0_ITMINRX_REG */

	/* CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_7 -> OA_LANE2_LPRX_LP_PON_OVR_EN */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lprx_lp_pon_ovr_en,
			     1);

	/* CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_8 -> OA_LANE2_LPRX_LP_PON_OVR_VAL */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lprx_lp_pon_ovr_val,
			     0);

	/* CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_7 -> OA_LANE2_LPRX_CD_PON_OVR_EN */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lprx_cd_pon_ovr_en,
			     1);

	/* CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_8 -> OA_LANE2_LPRX_CD_PON_OVR_VAL */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lprx_cd_pon_ovr_val,
			     0);

	/* CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_5 -> OA_CB_SEL_45OHM_50OHM */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_5_oa_cb_sel_45ohm_50ohm,
			     0);

	/* CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_3 -> OA_CB_HSTXLB_DCO_CLK0_EN_OVR_EN */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_3_oa_cb_hstxlb_dco_clk0_en_ovr_en,
			     1);

	/* CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_1 -> OA_CB_HSTXLB_DCO_CLK0_EN_OVR_VAL */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_1_oa_cb_hstxlb_dco_clk0_en_ovr_val,
			     0);

	/* CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_3 -> OA_CB_HSTXLB_DCO_CLK90_EN_OVR_EN */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_3_oa_cb_hstxlb_dco_clk90_en_ovr_en,
			     1);

	/* CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_0 -> OA_CB_HSTXLB_DCO_CLK90_EN_OVR_VAL */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_0_oa_cb_hstxlb_dco_clk90_en_ovr_val,
			     0);

	/* CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_6 -> OA_CB_HSTXLB_DCO_EN_OVR_EN */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_6_oa_cb_hstxlb_dco_en_ovr_en,
			     1);

	/* CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7 -> OA_CB_HSTXLB_DCO_EN_OVR_VAL */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_cb_hstxlb_dco_en_ovr_val,
			     0);

	/* CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_6 -> OA_CB_HSTXLB_DCO_TUNE_CLKDIG_EN_OVR_EN */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_6_oa_cb_hstxlb_dco_tune_clkdig_en_ovr_en,
			     1);

	/* CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7 -> OA_CB_HSTXLB_DCO_TUNE_CLKDIG_EN_OVR_VAL */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_cb_hstxlb_dco_tune_clkdig_en_ovr_val,
			     0);

	/* CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_4 -> OA_CB_CAL_SINK_EN_OVR_VAL */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_4_oa_cb_cal_sink_en_ovr_val,
			     0);

	/* CORE_DIG_DLANE_?_RW_LP_0 -> LP_0_ITMINRX_REG */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_0_rw_lp_0_lp_0_itminrx_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_1_rw_lp_0_lp_0_itminrx_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_2_rw_lp_0_lp_0_itminrx_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_3_rw_lp_0_lp_0_itminrx_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_clk_rw_lp_0_lp_0_itminrx_reg, val);

	/* CORE_DIG_CLANE_?_RW_LP_0 -> LP_0_ITMINRX_REG */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_0_rw_lp_0_lp_0_itminrx_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_1_rw_lp_0_lp_0_itminrx_reg, val);
	dw_cdphy_write_field(cdphy_g301->field_core_dig_clane_2_rw_lp_0_lp_0_itminrx_reg, val);

	/* PPI_STARTUP_RW_COMMON_DPHY_10 -> PHY_READY_ADDR, enables hibernate */
	dw_cdphy_write_field(cdphy_g301->field_ppi_startup_rw_common_dphy_10_phy_ready_addr, 47);

	/* CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_2 -> GLOBAL_ULPS_OVR_EN */
	dw_cdphy_write_field(
		cdphy_g301->field_core_dig_anactrl_rw_common_anactrl_2_global_ulps_ovr_en, 0);

	/* CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_2 -> GLOBAL_ULPS_OVR_VAL */
	dw_cdphy_write_field(cdphy_g301->field_core_dig_anactrl_rw_common_anactrl_2_global_ulps_ovr_val,
			     0);
}

static void dw_cdphy_pll_config(struct dw_mipi_cdphy_g301 *cdphy_g301)
{
	u32 val = 0;
	struct pll_config *pll_config = &cdphy_g301->hs_config.pll_config;

	/* TC_DIG_TC_REGISTERS_RW_PLL_ANA_CTRL_0  -> ATB_SENSE_SEL_R */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg0_rw_pll_atb_sense_sel, 0);

	/* TC_DIG_TC_REGISTERS_RW_PLL_ANA_CTRL_0  -> CLKSEL_R */
	/* set pll_clksel to 1 after PLL locked */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg0_rw_pll_clksel, 0);

	/* PLL_CFG0 -> PLL_CLKOUTEN_LEFT */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg0_rw_pll_clkouten_left, 1);

	/* PLL_CFG0 -> PLL_OPMODE */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg0_rw_pll_opmode, 16);

	/* PLL_CFG1 -> PLL_PRG_31_0 */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg1_rw_pll_prg_31_0, 3);

	/* PLL_CFG2 -> PLL_PRG_32 */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg2_rw_pll_prg_32, 0);

	/* PLL_CFG2 -> TH1 */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg2_rw_pll_th1, 1);

	/* PLL_CFG2 -> TH2 */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg2_rw_pll_th2, 255);

	/* PLL_CFG2 -> TH3 */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg2_rw_pll_th3, 3);

	/* PLL_CFG3 -> FRACN_EN */
	val = pll_config->pll_ssc_frac_en;
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg3_rw_pll_fracn_en, val);

	/* PLL_CFG3 -> PHY_M */
	val = pll_config->pll_m;
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg3_rw_pll_m, val);

	val = pll_config->pll_mint;
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg3_rw_pll_mint, val);

	/* PLL_CFG3 -> PHY_N */
	val = pll_config->pll_n;
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg3_rw_pll_n, val);

	/* PLL_CP -> VCO_CNTRL */
	val = pll_config->charge_pump_cfg->pll_vco_cntrl_2_0; // TODO: change to  0_2
	dw_cdphy_write_field(cdphy_g301->field_pll_cp_rw_pll_vco_cntrl_0_2, val);
	val = pll_config->pll_vco_cntrl_5_3;
	dw_cdphy_write_field(cdphy_g301->field_pll_cp_rw_pll_vco_cntrl_3_5, val);

	/* PLL_CP -> CPBIAS_CNTRL */
	val = pll_config->charge_pump_cfg->cpbias_cntrl;
	dw_cdphy_write_field(cdphy_g301->field_pll_cp_rw_pll_cpbias_cntrl, val);

	/* PLL_CP -> INT_CNTRL */
	val = pll_config->charge_pump_cfg->int_cntrl;
	dw_cdphy_write_field(cdphy_g301->field_pll_cp_rw_pll_int_cntrl, val);

	/* PLL_CP -> GMP_CNTRL */
	val = pll_config->charge_pump_cfg->gmp_cntrl;
	dw_cdphy_write_field(cdphy_g301->field_pll_cp_rw_pll_gmp_cntrl, val);

	/* PLL_CP -> PROP_CNTRL */
	val = pll_config->charge_pump_cfg->prop_cntrl;
	dw_cdphy_write_field(cdphy_g301->field_pll_cp_rw_pll_prop_cntrl, val);

	/* PLL_CFG7 -> PLL_FRAC_QUOT */
	val = pll_config->pll_frac_quote;
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg7_rw_pll_frac_quot, val);

	/* PLL_CFG7 -> PLL_FRAC_REM */
	val = pll_config->pll_frac_rem;
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg7_rw_pll_frac_rem, val);

	/* PLL_CFG4 -> PLL_FRAC_DEN */
	val = pll_config->pll_frac_den;
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg4_rw_pll_frac_den, val);

	/* PLL_CFG3 -> PLL_FRACN_EN */
	val = pll_config->pll_ssc_frac_en;
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg3_rw_pll_fracn_en, val);

	/* PLL_CFG6 -> PLL_SSC_EN */
	val = pll_config->pll_ssc_en;
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg6_rw_pll_ssc_en, val);

	/* PLL_CFG3 -> PLL_FRACN_CFG_UPDATE_EN */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg3_rw_pll_fracn_cfg_update_en, 1);

	/* PLL_CFG6 -> PLL_SSC_PEAK */
	val = pll_config->pll_ssc_peak;
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg6_rw_pll_ssc_peak, val);

	/* PLL_CFG6 -> PLL_SPREAD_TYPE */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg6_rw_pll_spread_type, 0);

	/* PLL_CFG5 --> PLL_STEP_SIZE */
	val = pll_config->pll_ssc_stepsize;
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg5_rw_pll_stepsize, val);
}

static int dw_cdphy_frequency_hopping_exit_ulps(struct phy *phy)
{
	struct dw_mipi_cdphy *cdphy = phy_get_drvdata(phy);
	struct dw_mipi_cdphy_g301 *cdphy_g301 = cdphy->cdphy_priv_data;
	bool ssc = cdphy->pll_ssc;
	struct pll_config pll_config;
	int ret;
	u32 val;

	DPU_ATRACE_BEGIN("%s, %d", __func__, cdphy->datarate);
	dev_dbg(&cdphy->phy->dev, "%s: datarate = %d\n", __func__, cdphy->datarate);
	/* 7 - Set register */
	if (ssc)
		dw_cdphy_write_field(cdphy_g301->field_ppi_rw_pll_startup_cfg_4_ssc_reset_n, 1);

	/* 9 - Update PLL static inputs according to the frequency hopping */
	if (cdphy->driver_data && cdphy->driver_data->pll_calc_old)
		ret = pll_calc(cdphy->datarate, cdphy->pll_ref_clk_khz,
					cdphy->pll_ssc, &pll_config);
	else
		ret = pll_calc_v2(cdphy->datarate, cdphy->pll_ref_clk_khz,
					cdphy->pll_ssc, &pll_config);

	if (ret == 0) {
		val = pll_config.pll_ssc_peak;
		dw_cdphy_write_field(cdphy_g301->field_pll_cfg6_rw_pll_ssc_peak, val);
		val = pll_config.pll_ssc_stepsize;
		dw_cdphy_write_field(cdphy_g301->field_pll_cfg5_rw_pll_stepsize, val);
		val = pll_config.pll_mint;
		dw_cdphy_write_field(cdphy_g301->field_pll_cfg3_rw_pll_mint, val);
		val = pll_config.pll_frac_quote;
		dw_cdphy_write_field(cdphy_g301->field_pll_cfg7_rw_pll_frac_quot, val);
		cdphy_g301->hs_config.datarate = cdphy->datarate;
		memcpy(&cdphy_g301->hs_config.pll_config, &pll_config, sizeof(pll_config));
	}

	/* 10 - Set register */
	/* TODO: add shadow control logic for C-PHY */
	if (!cdphy->is_cphy) {
		/* DPHY Specific */
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_0_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_1_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_2_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_3_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
	}

	/* 11 */
	if (ssc)
		dw_cdphy_write_field(cdphy_g301->field_ppi_rw_pll_startup_cfg_4_ssc_reset_n, 1);

	DPU_ATRACE_END("%s, %d", __func__, cdphy->datarate);
	return ret;
}

static int dw_dphy_power_on_g301(struct phy *phy)
{
	struct dw_mipi_cdphy *cdphy = phy_get_drvdata(phy);
	struct dw_mipi_cdphy_g301 *cdphy_g301 = cdphy->cdphy_priv_data;
	u32 ovr_en = cdphy->lane_ovr_en;

	/* disable lane override setting */
	if (ovr_en && cdphy->phy_in_ulps == true) {
		dw_cdphy_lptx_din_override_en(cdphy_g301, 0x1F, 0);
		dw_cdphy_lptx_en_pon_override_en(cdphy_g301, 0x1F, 0);
		dw_cdphy_turndisable_ovr(cdphy_g301, 0x1F, 0);
	}
	/* Step 11.b */
	dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_0, 0);
	dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_1, 0);
	dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_2, 0);
	if (!cdphy->is_cphy) {
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_3, 0);
		/* Step 11.c */
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_dck,
				     0x0);
	}

	if (!shared_cb_ctrl_val) {
		u32 val = 0;

		val = dw_cdphy_read_reg(cdphy_g301->base_regs, CORE_DIG_IOCTRL_R_AFE_CB_CTRL_2_15);
		if (val != 0) {
			cdphy_g301->is_primary = true;
			shared_cb_ctrl_val = val;
			dev_dbg(&cdphy->phy->dev,
				"Primary PHY detected, cached calibration = 0x%x\n", val);
		}
	}

	/* PHY exit LP00 state */
	cdphy->phy_in_ulps = false;
	dev_info(&cdphy->phy->dev, "CD-PHY G301 Power-on\n");
	return 0;
}

static int dw_dphy_power_off_g301(struct phy *phy)
{
	struct dw_mipi_cdphy *cdphy = phy_get_drvdata(phy);
	struct dw_mipi_cdphy_g301 *cdphy_g301 = cdphy->cdphy_priv_data;

	dev_info(&cdphy->phy->dev, "CD-PHY G301 Power OFF\n");

	/* shutdown and reset PHY before power off */
	dw_cdphy_write_field(cdphy_g301->field_phy_ctrl0_rw_shutdown_n, 0);
	dw_cdphy_write_field(cdphy_g301->field_phy_ctrl0_rw_phy_rst_n, 0);

	return 0;
}

static int dw_cdphy_set_ulps(struct phy *phy, int enable)
{
	struct dw_mipi_cdphy *cdphy = phy_get_drvdata(phy);
	struct dw_mipi_cdphy_g301 *cdphy_g301 = cdphy->cdphy_priv_data;

	DPU_ATRACE_BEGIN("%s, %d", __func__, enable);
	if (unlikely(!cdphy->pll_enabled)) {
		dev_info(&cdphy->phy->dev, "CD-PHY G301 change ULPS state while PLL disabled\n");
		DPU_ATRACE_INSTANT("change ULPS state while PLL disabled");
		DPU_ATRACE_END("%s, %d", __func__, enable);
		return -EPERM;
	}

	dev_dbg(&phy->dev, "CD-PHY G301 Setting ulps to %d\n", enable);
	if (!enable) {
		if (unlikely(!cdphy->phy_in_ulps)) {
			dev_warn(&phy->dev, "CD-PHY G301 exit ULPS fail, PHY not in ULPS state\n");
			DPU_ATRACE_INSTANT("PHY not in ULPS state");
			DPU_ATRACE_END("%s, %d", __func__, enable);
			return -EPERM;
		}
		dw_cdphy_write_field(
			cdphy_g301->field_core_dig_anactrl_rw_common_anactrl_2_global_ulps_ovr_en,
			0);
		cdphy->phy_in_ulps = false;
	} else {
		if (unlikely(cdphy->phy_in_ulps)) {
			dev_warn(&phy->dev, "CD-PHY G301 enter ULPS fail, PHY in ULPS already\n");
			DPU_ATRACE_INSTANT("PHY in ULPS already");
			DPU_ATRACE_END("%s, %d", __func__, enable);
			return -EPERM;
		}
		cdphy->phy_in_ulps = true;
		dw_cdphy_write_field(cdphy_g301->field_pll_cfg3_rw_pll_fracn_cfg_update_en, 0);
	}
	DPU_ATRACE_END("%s, %d", __func__, enable);
	return 0;
}

static int dw_cdphy_set_pll(struct phy *phy, int enable)
{
	struct dw_mipi_cdphy *cdphy = phy_get_drvdata(phy);
	struct dw_mipi_cdphy_g301 *cdphy_g301 = cdphy->cdphy_priv_data;
	int val, ret = 0;

	DPU_ATRACE_BEGIN("%s, %d", __func__, enable);
	if (!enable) {
		if (unlikely(!cdphy->pll_enabled)) {
			dev_warn(&phy->dev, "CD-PHY G301 PLL disabled already\n");
			DPU_ATRACE_INSTANT("PLL disabled already");
			DPU_ATRACE_END("%s, %d", __func__, enable);
			return -EPERM;
		}
		cdphy->pll_enabled = false;
	} else {
		if (unlikely(cdphy->pll_enabled)) {
			dev_warn(&phy->dev, "CD-PHY G301 PLL enabled already\n");
			DPU_ATRACE_INSTANT("PLL enabled already");
			DPU_ATRACE_END("%s, %d", __func__, enable);
			return -EPERM;
		}

		dw_cdphy_write_field(cdphy_g301->field_pll_cfg3_rw_pll_fracn_cfg_update_en, 1);

		if (cdphy->datarate != cdphy_g301->hs_config.datarate)
			dw_cdphy_frequency_hopping_exit_ulps(phy);

		dw_cdphy_write_field(cdphy_g301->field_ppi_rw_tx_hibernate_cfg_0_hibernate_exit,
				     0x1);
		dw_cdphy_write_field(cdphy_g301->field_ppi_rw_tx_hibernate_cfg_0_hibernate_exit,
				     0x0);
		/* TODO: following configuration cause b/384404118, skip until problem solved */
		//dw_cdphy_write_field(cdphy_g301->field_phy_startup_cfg3_reg, 0x0);
		//dw_cdphy_write_field(cdphy_g301->field_phy_startup_cfg3_reg, 0x0);

		ret = readl_poll_timeout(cdphy_g301->u0_apb_regbank + PHY_STS, val, val & PLL_LOCK,
					 50, PHY_STATUS_TIMEOUT_US);
		if (ret) {
			/* TODO: dump corresponding CSR */
			dev_err(&cdphy->phy->dev, "%s: Failed to wait PHY Lock\n", __func__);
			DPU_ATRACE_INSTANT("fail to wait PHY Lock");
			DPU_ATRACE_END("%s, %d", __func__, enable);
			return ret;
		}

		cdphy->pll_enabled = true;
	}
	DPU_ATRACE_END("%s, %d", __func__, enable);

	return ret;
}

static int dw_cdphy_frequency_hopping(struct phy *phy)
{
	struct dw_mipi_cdphy *cdphy = phy_get_drvdata(phy);
	struct dw_mipi_cdphy_g301 *cdphy_g301 = cdphy->cdphy_priv_data;
	bool ssc = cdphy->pll_ssc;
	bool pll_calc_old = false;
	struct pll_config pll_config;
	int ret;
	u32 val;

	if (cdphy->driver_data && cdphy->driver_data->pll_calc_old)
		pll_calc_old = true;

	/* Check PLL reference clock */
	cdphy->pll_ref_clk_khz = clk_get_rate(cdphy->pllref_clk) / 1000;
	if (!cdphy->pll_ref_clk_khz) {
		dev_warn(&cdphy->phy->dev, "%s: invalid pll reference clock rate %u Khz\n",
			__func__, cdphy->pll_ref_clk_khz);
		return -EINVAL;
	}


	DPU_ATRACE_BEGIN("%s, %d", __func__, cdphy->datarate);
	/* 1 - Boot TX PHY and wait until phy_ready (CPHY or DPHY) */
	ret = readl_poll_timeout(cdphy_g301->u0_apb_regbank + PHY_STS, val, val & PHY_READY, 50,
				 PHY_STATUS_TIMEOUT_US);
	if (ret) {
		dev_err(&cdphy->phy->dev, "%s Failed to wait PHY_READY\n", __func__);
		DPU_ATRACE_END("%s, %d", __func__, cdphy->datarate);
		return ret;
	}

	/* 2 - Set pll_fracn_cfg_update_en to 1'b0 (Disable PLL and ssc_gen configuration update) */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg3_rw_pll_fracn_cfg_update_en, 0);
	/* 3 - Set register */
	/* TODO: add shadow control logic for C-PHY */
	if (!cdphy->is_cphy) {
		/* DPHY Specific */
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_0_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_1_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_2_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_3_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
	}
	/* 4. While PHY is in transmission state,
	 * update PLL ssc static configuration inputs
	 * and the following list of HS-TX related register according to the frequency hopping
	 */

	/* 5 - Command all lanes (including clock lane) or trios to enter ULP
	 * (wait until ulpsactivenot_* = 1'b0)
	 */
	ret = readl_poll_timeout(cdphy_g301->base + CORE_DIG_IOCTRL_R_DPHY_PPI_LANE0_OVR_0_14, val,
				 val & BIT(1), 50, PHY_STATUS_TIMEOUT_US);
	if (ret)
		dev_err(&cdphy->phy->dev, "%s Failed to wait STOP_STATE\n", __func__);


	/* 6 - TX PHY should enter hibernate automatically and disable both PLL and LP-DCO */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg0_rw_pll_clksel, 0);
	dw_cdphy_write_field(cdphy_g301->field_phy_common9_reg_lp_dco_clk_dyn_gating, 0x0);
	dw_cdphy_write_field(cdphy_g301->field_phy_startup_cfg3_reg, 0x0);
	dw_cdphy_write_field(cdphy_g301->field_phy_startup_cfg3_reg, 0x2);

	/* 7 - Set register */
	if (ssc)
		dw_cdphy_write_field(cdphy_g301->field_ppi_rw_pll_startup_cfg_4_ssc_reset_n, 1);

	/* 8 - Set pll_fracn_cfg_update_en to 1'b1 (Enable PLL and ssc_gen configuration update */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg3_rw_pll_fracn_cfg_update_en, 1);

	/* 9 - Update PLL static inputs according to the frequency hopping */
	if (pll_calc_old)
		ret = pll_calc(cdphy->datarate, cdphy->pll_ref_clk_khz,
					cdphy->pll_ssc, &pll_config);
	else
		ret = pll_calc_v2(cdphy->datarate, cdphy->pll_ref_clk_khz,
					cdphy->pll_ssc, &pll_config);

	if (ret == 0) {
		val = pll_config.pll_ssc_peak;
		dw_cdphy_write_field(cdphy_g301->field_pll_cfg6_rw_pll_ssc_peak, val);
		val = pll_config.pll_ssc_stepsize;
		dw_cdphy_write_field(cdphy_g301->field_pll_cfg5_rw_pll_stepsize, val);
		val = pll_config.pll_mint;
		dw_cdphy_write_field(cdphy_g301->field_pll_cfg3_rw_pll_mint, val);
		val = pll_config.pll_frac_quote;
		dw_cdphy_write_field(cdphy_g301->field_pll_cfg7_rw_pll_frac_quot, val);
		cdphy_g301->hs_config.datarate = cdphy->datarate;
		memcpy(&cdphy_g301->hs_config.pll_config, &pll_config, sizeof(pll_config));
	}

	/* 10 - Set register */
	/* TODO: add shadow control logic for C-PHY */
	if (!cdphy->is_cphy) {
		/* DPHY Specific */
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_0_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_1_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_2_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_3_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
		dw_cdphy_write_field(cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_sh_11_sh_sel_reg,
				     0);
	}

	/* 11 */
	if (ssc)
		dw_cdphy_write_field(cdphy_g301->field_ppi_rw_pll_startup_cfg_4_ssc_reset_n, 1);

	/* 12 */
	dw_cdphy_write_field(cdphy_g301->field_phy_common9_reg_lp_dco_clk_dyn_gating, 0x1);
	dw_cdphy_write_field(cdphy_g301->field_phy_startup_cfg3_reg, 0x0);
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg0_rw_pll_clksel, 0);

	/* 14 - wait for the pll_lock to lock */
	ret = readl_poll_timeout(cdphy_g301->u0_apb_regbank + PHY_STS, val, val & PLL_LOCK, 1000,
				 PHY_STATUS_TIMEOUT_US);
	if (ret) {
		/* TODO: dump corresponding CSR */
		dev_err(&cdphy->phy->dev, "%s: Failed to wait PHY Lock\n", __func__);
		DPU_ATRACE_INSTANT("fail to wait PHY Lock");
		DPU_ATRACE_END("%s, %d", __func__, cdphy->datarate);
		return ret;
	}
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg0_rw_pll_clksel, 1);
	/* 15 - Command all lanes (including clock lane) or trios to exit ULP
	 * (wait until ulpsactivenot_* = 1'b1 and stopstate_* = 1'b1)
	 */

	DPU_ATRACE_END("%s, %d", __func__, cdphy->datarate);
	return 0;
}

static void dw_cdphy_fixup_b269704700(struct dw_mipi_cdphy *cdphy)
{
	struct dw_mipi_cdphy_g301 *cdphy_g301 = cdphy->cdphy_priv_data;

	if (!cdphy->driver_data || !cdphy->driver_data->lane_conn_fixup)
		return;

	if (cdphy->is_cphy) {
		dw_cdphy_write_field(
			cdphy_g301->field_core_dig_clane_1_rw_cfg_0_cfg_0_lp_pin_swap_reg, 0x100);
		dw_cdphy_write_field(
			cdphy_g301->field_core_dig_clane_1_rw_hs_tx_6_hs_tx_6_pin_swap_reg, 0x100);
	} else {
		dw_cdphy_write_field(
			cdphy_g301->field_core_dig_dlane_clk_rw_cfg_0_cfg_0_hs_pin_swap_reg, 0x1);

		dw_cdphy_write_field(
			cdphy_g301->field_core_dig_dlane_clk_rw_cfg_0_cfg_0_lp_pin_swap_reg, 0x1);
		dw_cdphy_write_field(
			cdphy_g301->field_core_dig_dlane_clk_rw_hs_tx_3_hs_tx_3_pin_swap_reg, 0x1);
	}
}

static int dw_dphy_configure_g301(struct phy *phy, union phy_configure_opts *opts)
{
	struct dw_mipi_cdphy *cdphy = phy_get_drvdata(phy);
	struct dw_mipi_cdphy_g301 *cdphy_g301 = cdphy->cdphy_priv_data;
	struct dw_cdphy_hs_configuration *hs_config = &cdphy_g301->hs_config;
	struct phy_configure_opts_mipi_dphy *cdphy_opts = &opts->mipi_dphy;
	u32 val = 0 ;
	u32 ovr_en = cdphy->lane_ovr_en;
	int ret;

	dev_info(&cdphy->phy->dev, "Configuring CD-PHY G301\n");
	pr_info("datarate = %d\n", cdphy->datarate);

	if (cdphy->datarate != 0) {
		bool pll_calc_old = false;

		if (cdphy->driver_data && cdphy->driver_data->pll_calc_old)
			pll_calc_old = true;

		cdphy->pll_ref_clk_khz = clk_get_rate(cdphy->pllref_clk) / 1000;
		if (!cdphy->pll_ref_clk_khz) {
			dev_warn(&cdphy->phy->dev, "%s: invalid pll reference clock rate %u Khz\n",
				__func__, cdphy->pll_ref_clk_khz);
			return -EINVAL;
		}

		dev_dbg(&cdphy->phy->dev, "%s: pll_ref_clk_khz %u Khz\n", __func__,
			cdphy->pll_ref_clk_khz);

		if (pll_calc_old)
			ret = pll_calc(cdphy->datarate, cdphy->pll_ref_clk_khz,
						cdphy->pll_ssc, &hs_config->pll_config);
		else
			ret = pll_calc_v2(cdphy->datarate, cdphy->pll_ref_clk_khz,
						cdphy->pll_ssc, &hs_config->pll_config);

		if (ret == 0) {
			if (cdphy->is_cphy)
				ret = cphy_regs_calc(cdphy->datarate, &hs_config->cphy_regs);
			else
				ret = dphy_regs_calc(cdphy->datarate, &hs_config->dphy_regs);
		}

		if (ret != 0) {
			dev_warn(&cdphy->phy->dev, "%s: failed to apply datarate %d Mbps\n",
				 __func__, cdphy->datarate);
			if (hs_config->datarate != 0)
				dev_warn(&cdphy->phy->dev, "%s:  use %d Mbps\n", __func__,
					 hs_config->datarate);
			else
				return ret;
		} else {
			hs_config->datarate = cdphy->datarate;
		}
	}

	dw_cdphy_write_field(cdphy_g301->field_phy_ctrl0_rw_shutdown_n, 0);
	dw_cdphy_write_field(cdphy_g301->field_phy_ctrl0_rw_phy_rst_n, 0);

	/* Step 6 - Controller - Done */

	/* Telmo */
	dw_cdphy_write_field(cdphy_g301->field_ppi_rw_hstx_fifo_cfg_txdatatransferenhs_sel, 0x0);

	/* Telmo */
	// OCLA: On-Chip Logic Analyzer
	// dw_cdphy_write_field(cdphy_g301->field_core_dig_rw_common_1_ocla_data_sel, 0x2);
	// dw_cdphy_write_field(cdphy_g301->field_core_dig_rw_common_3_ocla_clk_sel, 0x2);

	/* Step 7.b */
	if (cdphy->is_cphy) {
		if (!cdphy_opts->lanes || cdphy_opts->lanes > 3) {
			pr_err("[%s] Failed to get trios config\n", __func__);
			return -ENODEV;
		}
	} else {
		/* DPHY */
		if (!cdphy_opts->lanes || cdphy_opts->lanes > 4) {
			pr_err("[%s] Failed to get lanes config\n", __func__);
			return -ENODEV;
		}
	}
	dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_0, 1);
	if (cdphy_opts->lanes > 1)
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_1, 1);
	if (cdphy_opts->lanes > 2)
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_2, 1);
	if (!cdphy->is_cphy && cdphy_opts->lanes > 3)
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_3, 1);

	/* 5.12 Limitation of panel width */
	if (cdphy->non24x_fix)
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_cont_en, 1);

	/* Step 7.c */
	if (!cdphy->is_cphy)
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_forcetxstopmode_dck, 1);

	/* workaround for force set_ulps exit */
	val = cdphy->phy_in_ulps;
	cdphy->phy_in_ulps = true;
	dw_cdphy_set_ulps(phy, false);
	cdphy->phy_in_ulps = val;

	/* Step 8.a */
	dw_cdphy_common_config(cdphy_g301);
	/* Step 8.b */
	if (cdphy->is_cphy)
		dw_cdphy_specific_cphy_cfg(cdphy_g301); /* CPHY */
	else
		dw_cdphy_specific_dphy_cfg(cdphy_g301); /* DPHY */
	/* Step 8.c */
	dw_cdphy_extra_config(cdphy_g301);
	dw_cdphy_fixup_b269704700(cdphy);

	/* Step 8.d */
	dw_cdphy_pll_config(cdphy_g301);

	/* 16bit PPI data width */
	dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_txdatawidthhs_0, 1);
	if (cdphy_opts->lanes > 1)
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_txdatawidthhs_1, 1);
	if (cdphy_opts->lanes > 2)
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_txdatawidthhs_2, 1);
	if (cdphy_opts->lanes > 3)
		dw_cdphy_write_field(cdphy_g301->field_phy_extended_ctr1_rw_txdatawidthhs_3, 1);

	/* Pass value from primary PHY to secondary PHY if applicable */
	if (!cdphy_g301->is_primary && (shared_cb_ctrl_val != 0)) {
		dev_dbg(&cdphy->phy->dev,
			"Applying cached value (0x%x) to CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_8\n",
			shared_cb_ctrl_val);
		dw_cdphy_write_reg(cdphy_g301->base_regs, CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_8,
			shared_cb_ctrl_val);
		dw_cdphy_write_field(
			cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_setr_ovr_en, 1);
		dw_cdphy_write_field(
			cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_setr_calib_ovr_en,
			1);
		dw_cdphy_write_field(
			cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_setra_ovr_en, 1);
		dw_cdphy_write_field(
			cdphy_g301->field_core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_setrb_ovr_en, 1);
	}

	/* Step 9.a */
#if IS_ENABLED(CONFIG_DEBUG_FS)
	dw_cdphy_preset_config_load(cdphy_g301, DPHY_PRESET_APPLY_BEFORE_RST);
#endif

	/* lane override setting, start from LP00 */
	if (ovr_en && cdphy->phy_in_ulps == true) {
		dw_cdphy_forcetxstopmode(cdphy_g301, 0x1F, 0);
		dw_cdphy_lptx_en_pon_override_val(cdphy_g301, 0x1F, 0x3);
		dw_cdphy_lptx_en_pon_override_en(cdphy_g301, 0x1F, 1);
		dw_cdphy_lptx_din_override_val(cdphy_g301, 0x1F, 0, 0);
		dw_cdphy_lptx_din_override_en(cdphy_g301, 0x1F, 1);
		dw_cdphy_turndisable_ovr(cdphy_g301, 0x1F, 1);
		udelay(100);
	}
	/* Step 9.b */
	dw_cdphy_write_field(cdphy_g301->field_phy_ctrl0_rw_phy_mode, cdphy->is_cphy);
	dw_cdphy_write_field(cdphy_g301->field_phy_ctrl0_rw_shutdown_n, 1);
	dw_cdphy_write_field(cdphy_g301->field_phy_ctrl0_rw_phy_rst_n, 1);

	/* lane override to LP10 and LP11 */
	if (ovr_en && cdphy->phy_in_ulps == true) {
		dw_cdphy_lptx_din_override_val(cdphy_g301, 0x1F, 1, 0);
		udelay(100);
		dw_cdphy_lptx_din_override_val(cdphy_g301, 0x1F, 1, 1);
		udelay(50);

		dw_cdphy_forcetxstopmode(cdphy_g301, 0x1F, 1);
		udelay(100);
	}

	/* Step 10 */
	ret = readl_poll_timeout(cdphy_g301->u0_apb_regbank + PHY_STS, val,
				 val & PHY_READY, 50, PHY_STATUS_TIMEOUT_US);
	if (ret) {
		dev_err(&cdphy->phy->dev, "Failed to wait PHY Lock, step 10.b\n");
		return ret;
	}

	ret = readl_poll_timeout(cdphy_g301->u0_apb_regbank + PHY_STS, val, val & PLL_LOCK, 50,
				 PHY_STATUS_TIMEOUT_US);
	if (ret) {
		/* TODO: dump corresponding CSR */
		dev_err(&cdphy->phy->dev, "%s: Failed to wait PHY Lock\n", __func__);
		DPU_ATRACE_INSTANT("fail to wait PHY Lock");
		return ret;
	}

	/* set pll_clksel to 1 after PLL locked */
	dw_cdphy_write_field(cdphy_g301->field_pll_cfg0_rw_pll_clksel, 1);
#if IS_ENABLED(CONFIG_DEBUG_FS)
	dw_cdphy_preset_config_load(cdphy_g301, DPHY_PRESET_APPLY_AFTER_READY);
#endif

	// Temporary
	val = dw_cdphy_read_reg(cdphy_g301->base_regs, CORE_DIG_IOCTRL_R_COMMON_PPI_OVR_0_12);
	val &= 0x1f;
	dev_info(&cdphy->phy->dev, "Phy state %u\n", val);
	// Temporary

	return 0;
}

static int dw_dphy_reset_g301(struct phy *phy)
{
	struct dw_mipi_cdphy *dphy = phy_get_drvdata(phy);

	dev_info(&dphy->phy->dev, "Reset DPHY\n");

	return 0;
}

static int dw_dphy_init_g301(struct phy *phy)
{
	struct dw_mipi_cdphy *cdphy = phy_get_drvdata(phy);
	struct dw_mipi_cdphy_g301 *cdphy_g301 = cdphy->cdphy_priv_data;
	u32 val = 0;

	dev_info(&cdphy->phy->dev, "Init CD-PHY G301\n");

	/* check pll state (LP-DCO), pll might be enabled at bootloader */
	val = dw_cdphy_read_reg(cdphy_g301->base_regs, CORE_DIG_RW_COMMON_9);
	if (val & BIT(8))
		cdphy->pll_enabled = true;
	else
		cdphy->pll_enabled = false;

	return 0;
}

static int dw_dphy_set_mode_g301(struct phy *phy, enum phy_mode mode, int submode)
{
	struct dw_mipi_cdphy *cdphy = phy_get_drvdata(phy);
	struct dw_mipi_cdphy_g301 *cdphy_g301 = cdphy->cdphy_priv_data;
	int ret;

	dev_dbg(&cdphy->phy->dev, "CD-PHY G301 Setting phy type to %d submode %d\n",
		 mode, submode);

	/* phy core does not support CPHY type. This driver reads the phy type
	 * from dts  */
	if (mode != PHY_MODE_MIPI_DPHY)
		return -EINVAL;
	cdphy->is_cphy = 0;

	switch (submode) {
	case DW_MIPI_CDPHY_OP_PLL_DISABLE:
		ret = dw_cdphy_set_pll(phy, 0);
		break;
	case DW_MIPI_CDPHY_OP_PLL_ENABLE:
		ret = dw_cdphy_set_pll(phy, 1);
		break;
	case DW_MIPI_CDPHY_OP_OVR_ULPS_EXIT:
		ret = dw_cdphy_set_ulps(phy, 0);
		break;
	case DW_MIPI_CDPHY_OP_OVR_ULPS_ENTER:
		ret = dw_cdphy_set_ulps(phy, 1);
		break;
	case DW_MIPI_CDPHY_OP_FREQUENCY_HOPPING:
		ret = dw_cdphy_frequency_hopping(phy);
		break;
	case DW_MIPI_CDPHY_OP_OVR_LANE_LP00:
		dev_info(&cdphy->phy->dev, "CD-PHY G301 OVR LP00, LP1 Contention is expected\n");
		dw_cdphy_lptx_din_override_val(cdphy_g301, 0x1F, 0, 0);
		dw_cdphy_lptx_din_override_en(cdphy_g301, 0x1F, 1);
		ret = 0;
		break;
	case DW_MIPI_CDPHY_OP_OVR_PHY_ULPS_STATE:
		cdphy->phy_in_ulps = true;
		ret = 0;
		break;
	case DW_MIPI_CDPHY_OP_OVR_LANE_ENABLE:
		cdphy->lane_ovr_en = 1;
		ret = 0;
		break;
	case DW_MIPI_CDPHY_OP_OVR_LANE_DISABLE:
		cdphy->lane_ovr_en = 0;
		ret = 0;
		break;
	default:
		dev_err(&cdphy->phy->dev, "%s invalid submode %d\n", __func__, submode);
		ret = -EINVAL;
	}

	return ret;
}

static int dw_dphy_set_speed_g301(struct phy *phy, int speed)
{
	struct dw_mipi_cdphy *cdphy = phy_get_drvdata(phy);

	dev_info(&cdphy->phy->dev, "CD-PHY G301 Setting speed to %d\n", speed);

	cdphy->datarate = speed;

	return 0;
}

/* set of function pointers for performing CD-PHY G301 operations */
const struct phy_ops dw_dphy_ops_g301 = {
	.init = dw_dphy_init_g301,
	.reset = dw_dphy_reset_g301,
	.power_on = dw_dphy_power_on_g301,
	.power_off = dw_dphy_power_off_g301,
	.configure = dw_dphy_configure_g301,
	.set_mode = dw_dphy_set_mode_g301,
	.set_speed = dw_dphy_set_speed_g301,
	.owner = THIS_MODULE,
};

#if IS_ENABLED(CONFIG_DEBUG_FS)

static noinline void dw_create_debugfs_hwv_files_1(struct dw_mipi_cdphy_g301 *cdphy_g301)
{
	int nr_entries;

	const struct debugfs_entries debugfs_regs[] = {
		/* dwc_mipi_cdphy_tx_4l3t_ns */
		REGISTER(ppi_startup_rw_common_dphy_2_rcal_addr),
		REGISTER(ppi_startup_rw_common_dphy_3_pll_start_addr),
		REGISTER(ppi_startup_rw_common_dphy_6_lp_dco_cal_addr),
		REGISTER(ppi_startup_rw_common_dphy_a_hibernate_addr),
		REGISTER(ppi_startup_rw_common_dphy_10_phy_ready_addr),
		REGISTER(ppi_startup_rw_common_startup_1_1_phy_ready_dly),
		REGISTER(ppi_calibctrl_rw_common_bg_0_bg_max_counter),
		REGISTER(ppi_rw_lpdcocal_timebase_lpcdcocal_timebase),
		REGISTER(ppi_rw_lpdcocal_nref_lpcdcocal_nref),
		REGISTER(ppi_rw_lpdcocal_nref_range_lpcdcocal_nref_range),
		REGISTER(ppi_rw_lpdcocal_twait_config_lpcdcocal_twait_coarse),
		REGISTER(ppi_rw_lpdcocal_twait_config_lpcdcocal_twait_pon),
		REGISTER(ppi_rw_lpdcocal_vt_config_lpcdcocal_vt_tracking_en),
		REGISTER(ppi_rw_lpdcocal_vt_config_lpcdcocal_use_ideal_nref),
		REGISTER(ppi_rw_lpdcocal_vt_config_lpcdcocal_vt_nref_range),
		REGISTER(ppi_rw_lpdcocal_vt_config_lpcdcocal_twait_fine),
		REGISTER(ppi_rw_lpdcocal_coarse_cfg_ncoarse_start),
		REGISTER(ppi_rw_common_cfg_cfg_clk_div_factor),
		REGISTER(ppi_rw_termcal_cfg_0_termcal_timer),
		REGISTER(ppi_rw_pll_startup_cfg_0_pll_rst_time),
		REGISTER(ppi_rw_pll_startup_cfg_1_pll_gear_shift_time),
		REGISTER(ppi_rw_pll_startup_cfg_2_pll_lock_det_time),
		REGISTER(core_dig_ioctrl_rw_afe_lane0_ctrl_2_2_oa_lane0_sel_lane_cfg),
		REGISTER(core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_hstx_sel_phase0),
		REGISTER(core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_hstx_eqa),
		REGISTER(core_dig_ioctrl_rw_afe_lane0_ctrl_2_3_oa_lane0_hstx_sel_clklb),
		REGISTER(core_dig_ioctrl_rw_afe_lane0_ctrl_2_4_oa_lane0_hstx_eqb),
		REGISTER(core_dig_ioctrl_rw_afe_lane1_ctrl_2_2_oa_lane1_sel_lane_cfg),
		REGISTER(core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_hstx_sel_phase0),
		REGISTER(core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_hstx_eqa),
		REGISTER(core_dig_ioctrl_rw_afe_lane1_ctrl_2_3_oa_lane1_hstx_sel_clklb),
		REGISTER(core_dig_ioctrl_rw_afe_lane1_ctrl_2_4_oa_lane1_hstx_eqb),
		REGISTER(core_dig_ioctrl_rw_afe_lane2_ctrl_2_2_oa_lane2_sel_lane_cfg),
		REGISTER(core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_hstx_sel_phase0),
		REGISTER(core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_hstx_eqa),
		REGISTER(core_dig_ioctrl_rw_afe_lane2_ctrl_2_3_oa_lane2_hstx_sel_clklb),
		REGISTER(core_dig_ioctrl_rw_afe_lane2_ctrl_2_4_oa_lane2_hstx_eqb),
		REGISTER(core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lprx_lp_pon_ovr_en),
		REGISTER(core_dig_ioctrl_rw_afe_lane2_ctrl_2_7_oa_lane2_lprx_cd_pon_ovr_en),
		REGISTER(core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lprx_lp_pon_ovr_val),
		REGISTER(core_dig_ioctrl_rw_afe_lane2_ctrl_2_8_oa_lane2_lprx_cd_pon_ovr_val),
		REGISTER(core_dig_ioctrl_rw_afe_lane3_ctrl_2_2_oa_lane3_sel_lane_cfg),
		REGISTER(core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_hstx_sel_phase0),
		REGISTER(core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_hstx_eqa),
		REGISTER(core_dig_ioctrl_rw_afe_lane3_ctrl_2_3_oa_lane3_hstx_sel_clklb),
		REGISTER(core_dig_ioctrl_rw_afe_lane3_ctrl_2_4_oa_lane3_hstx_eqb),
		REGISTER(core_dig_ioctrl_rw_afe_lane4_ctrl_2_2_oa_lane4_sel_lane_cfg),
		REGISTER(core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_hstx_sel_phase0),
		REGISTER(core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_hstx_eqa),
		REGISTER(core_dig_ioctrl_rw_afe_lane4_ctrl_2_3_oa_lane4_hstx_sel_clklb),
		REGISTER(core_dig_ioctrl_rw_afe_lane4_ctrl_2_4_oa_lane4_hstx_eqb),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_0_oa_cb_hstxlb_dco_clk90_en_ovr_val),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_1_oa_cb_hstxlb_dco_clk0_en_ovr_val),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_2_oa_cb_pll_bustiez),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_3_oa_cb_hstxlb_dco_clk0_en_ovr_en),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_3_oa_cb_hstxlb_dco_clk90_en_ovr_en),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_4_oa_cb_sel_vcomm_prog),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_4_oa_cb_cal_sink_en_ovr_val),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_5_oa_cb_sel_45ohm_50ohm),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_5_oa_cb_cal_sink_en_ovr_en),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_6_oa_cb_hstxlb_dco_pon_ovr_en),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_6_oa_cb_hstxlb_dco_en_ovr_en),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_6_oa_cb_hstxlb_dco_tune_clkdig_en_ovr_en),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_cb_hstxlb_dco_en_ovr_val),
		REGISTER(core_dig_ioctrl_rw_afe_cb_ctrl_2_7_oa_cb_hstxlb_dco_tune_clkdig_en_ovr_val),
		REGISTER(core_dig_anactrl_rw_common_anactrl_0_cb_lp_dco_en_dly),
		REGISTER(core_dig_anactrl_rw_common_anactrl_2_global_ulps_ovr_en),
		REGISTER(core_dig_anactrl_rw_common_anactrl_2_global_ulps_ovr_val),
		REGISTER(core_dig_dlane_0_rw_lp_0_lp_0_ttago_reg),
		REGISTER(core_dig_dlane_0_rw_lp_0_lp_0_itminrx_reg),
		REGISTER(core_dig_dlane_0_rw_hs_tx_0_hs_tx_0_thstrail_reg),
		REGISTER(core_dig_dlane_0_rw_hs_tx_1_hs_tx_1_thszero_reg),
		REGISTER(core_dig_dlane_0_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg),
		REGISTER(core_dig_dlane_0_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg),
		REGISTER(core_dig_dlane_0_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg),
		REGISTER(core_dig_dlane_0_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg),
		REGISTER(core_dig_dlane_0_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg),
		REGISTER(core_dig_dlane_0_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg),
		REGISTER(core_dig_dlane_0_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg),
		REGISTER(core_dig_dlane_1_rw_lp_0_lp_0_ttago_reg),
		REGISTER(core_dig_dlane_1_rw_lp_0_lp_0_itminrx_reg),
		REGISTER(core_dig_dlane_1_rw_hs_tx_0_hs_tx_0_thstrail_reg),
		REGISTER(core_dig_dlane_1_rw_hs_tx_1_hs_tx_1_thszero_reg),
		REGISTER(core_dig_dlane_1_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg),
		REGISTER(core_dig_dlane_1_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg),
		REGISTER(core_dig_dlane_1_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg),
		REGISTER(core_dig_dlane_1_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg),
		REGISTER(core_dig_dlane_1_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg),
		REGISTER(core_dig_dlane_1_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg),
		REGISTER(core_dig_dlane_1_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg),
		REGISTER(core_dig_dlane_2_rw_lp_0_lp_0_ttago_reg),
		REGISTER(core_dig_dlane_2_rw_lp_0_lp_0_itminrx_reg),
		REGISTER(core_dig_dlane_2_rw_hs_tx_0_hs_tx_0_thstrail_reg),
		REGISTER(core_dig_dlane_2_rw_hs_tx_1_hs_tx_1_thszero_reg),
		REGISTER(core_dig_dlane_2_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg),
		REGISTER(core_dig_dlane_2_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg),
		REGISTER(core_dig_dlane_2_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg),
		REGISTER(core_dig_dlane_2_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg),
		REGISTER(core_dig_dlane_2_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg),
		REGISTER(core_dig_dlane_2_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg),
		REGISTER(core_dig_dlane_2_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg),
		REGISTER(core_dig_dlane_3_rw_lp_0_lp_0_ttago_reg),
		REGISTER(core_dig_dlane_3_rw_lp_0_lp_0_itminrx_reg),
		REGISTER(core_dig_dlane_3_rw_hs_tx_0_hs_tx_0_thstrail_reg),
		REGISTER(core_dig_dlane_3_rw_hs_tx_1_hs_tx_1_thszero_reg),
		REGISTER(core_dig_dlane_3_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg),
		REGISTER(core_dig_dlane_3_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg),
		REGISTER(core_dig_dlane_3_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg),
		REGISTER(core_dig_dlane_3_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg),
		REGISTER(core_dig_dlane_3_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg),
		REGISTER(core_dig_dlane_3_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg),
		REGISTER(core_dig_dlane_3_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg),
		REGISTER(core_dig_dlane_clk_rw_hs_tx_0_hs_tx_0_thstrail_reg),
		REGISTER(core_dig_dlane_clk_rw_hs_tx_1_hs_tx_1_thszero_reg),
		REGISTER(core_dig_dlane_clk_rw_hs_tx_2_hs_tx_2_tclkpre_reg),
		REGISTER(core_dig_dlane_clk_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg),
		REGISTER(core_dig_dlane_clk_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg),
		REGISTER(core_dig_dlane_clk_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg),
		REGISTER(core_dig_dlane_clk_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg),
		REGISTER(core_dig_dlane_clk_rw_hs_tx_8_hs_tx_8_tclkpost_reg),
		REGISTER(core_dig_dlane_clk_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg),
		REGISTER(core_dig_dlane_clk_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg),
		REGISTER(core_dig_dlane_clk_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg),
	};

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_mipi_debugfs_create_x32(debugfs_regs, nr_entries, cdphy_g301->cdphy->debugfs);
}

static noinline void dw_create_debugfs_hwv_files_2(struct dw_mipi_cdphy_g301 *cdphy_g301)
{
	int nr_entries;

	const struct debugfs_entries debugfs_regs[] = {
		REGISTER(core_dig_clane_0_rw_lp_0_lp_0_ttago_reg),
		REGISTER(core_dig_clane_0_rw_lp_0_lp_0_itminrx_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_1_hs_tx_1_tpost_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_3_hs_tx_3_burst_type_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb0_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb1_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb2_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb3_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_4_hs_tx_4_progseqsymb4_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb5_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb6_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb7_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb8_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_5_hs_tx_5_progseqsymb9_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb10_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb11_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb12_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_6_hs_tx_6_progseqsymb13_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_9_hs_tx_9_t3post_dco_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_10_hs_tx_10_tprebegin_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg),
		REGISTER(core_dig_clane_0_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg),
		REGISTER(core_dig_clane_1_rw_cfg_0_cfg_0_lp_pin_swap_reg),
		REGISTER(core_dig_clane_1_rw_lp_0_lp_0_ttago_reg),
		REGISTER(core_dig_clane_1_rw_lp_0_lp_0_itminrx_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_1_hs_tx_1_tpost_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_3_hs_tx_3_burst_type_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb0_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb1_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb2_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb3_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_4_hs_tx_4_progseqsymb4_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb5_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb6_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb7_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb8_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_5_hs_tx_5_progseqsymb9_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb10_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb11_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb12_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_6_hs_tx_6_progseqsymb13_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_9_hs_tx_9_t3post_dco_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_10_hs_tx_10_tprebegin_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg),
		REGISTER(core_dig_clane_1_rw_hs_tx_6_hs_tx_6_pin_swap_reg),
		REGISTER(core_dig_clane_2_rw_lp_0_lp_0_ttago_reg),
		REGISTER(core_dig_clane_2_rw_lp_0_lp_0_itminrx_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_1_hs_tx_1_tpost_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_3_hs_tx_3_burst_type_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb0_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb1_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb2_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb3_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_4_hs_tx_4_progseqsymb4_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb5_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb6_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb7_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb8_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_5_hs_tx_5_progseqsymb9_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb10_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb11_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb12_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_6_hs_tx_6_progseqsymb13_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_9_hs_tx_9_t3post_dco_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_10_hs_tx_10_tprebegin_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg),
		REGISTER(core_dig_clane_2_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg),

		REGISTER(core_dig_ioctrl_r_common_ppi_ovr_0_12_phy_state),

		/* Telmo */
		REGISTER(core_dig_rw_common_1_ocla_data_sel),
		REGISTER(core_dig_rw_common_3_ocla_clk_sel),
		REGISTER(ppi_rw_hstx_fifo_cfg_txdatatransferenhs_sel),

		/* Unexpected LP data transmission on lane 2 fix */
		REGISTER(core_dig_ioctrl_rw_dphy_ppi_lane1_ovr_0_3_i_txrequestesc_d1_ovr_val),
		REGISTER(core_dig_ioctrl_rw_dphy_ppi_lane1_ovr_0_4_i_txrequestesc_d1_ovr_en),
		REGISTER(core_dig_ioctrl_rw_dphy_ppi_lane2_ovr_0_3_i_txrequestesc_d2_ovr_val),
		REGISTER(core_dig_ioctrl_rw_dphy_ppi_lane2_ovr_0_4_i_txrequestesc_d2_ovr_en),
		REGISTER(core_dig_ioctrl_rw_dphy_ppi_lane3_ovr_0_3_i_txrequestesc_d3_ovr_val),
		REGISTER(core_dig_ioctrl_rw_dphy_ppi_lane3_ovr_0_4_i_txrequestesc_d3_ovr_en),
	};

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_mipi_debugfs_create_x32(debugfs_regs, nr_entries, cdphy_g301->cdphy->debugfs);
}

/* local variables that are not stored on registers */
static void dw_create_debugfs_hwv_files_3(struct dw_mipi_cdphy_g301 *cdphy_g301)
{
	debugfs_create_file_unsafe("datarate", 0444, cdphy_g301->cdphy->debugfs,
				   cdphy_g301, &fops_x32_local);
}

static void dw_cdphy_g301_debugfs_remove(struct dw_mipi_cdphy_g301 *cdphy_g301)
{
	debugfs_remove_recursive(cdphy_g301->cdphy->debugfs);
}

static void dw_create_debugfs_hwv_files_4(struct dw_mipi_cdphy_g301 *cdphy_g301)
{
	int nr_entries;

	const struct debugfs_entries debugfs_regs[] = {
		REGISTER(pll_cfg0_rw_pll_atb_sense_sel),
		REGISTER(pll_cfg0_rw_pll_clksel),
		REGISTER(pll_cfg0_rw_pll_gp_clk_en),
		REGISTER(pll_cfg0_rw_pll_opmode),
		REGISTER(pll_cfg0_rw_pll_meas_iv),
		REGISTER(pll_cfg0_rw_pll_clkouten_left),

		REGISTER(pll_cfg1_rw_pll_prg_31_0),

		REGISTER(pll_cfg2_rw_pll_prg_32),
		REGISTER(pll_cfg2_rw_pll_th1),
		REGISTER(pll_cfg2_rw_pll_th2),
		REGISTER(pll_cfg2_rw_pll_th3),

		REGISTER(pll_cfg3_rw_pll_mint),
		REGISTER(pll_cfg3_rw_pll_m),
		REGISTER(pll_cfg3_rw_pll_n),
		REGISTER(pll_cfg3_rw_pll_fracn_cfg_update_en),
		REGISTER(pll_cfg3_rw_pll_fracn_en),

		REGISTER(pll_cfg4_rw_pll_frac_den),
		REGISTER(pll_cfg5_rw_pll_stepsize),

		REGISTER(pll_cfg6_rw_pll_ssc_en),
		REGISTER(pll_cfg6_rw_pll_spread_type),
		REGISTER(pll_cfg6_rw_pll_ssc_peak),

		REGISTER(pll_cfg7_rw_pll_frac_quot),
		REGISTER(pll_cfg7_rw_pll_frac_rem),

		REGISTER(pll_cp_rw_pll_vco_cntrl_0_2),
		REGISTER(pll_cp_rw_pll_vco_cntrl_3_5),
		REGISTER(pll_cp_rw_pll_prop_cntrl),
		REGISTER(pll_cp_rw_pll_int_cntrl),
		REGISTER(pll_cp_rw_pll_gmp_cntrl),
		REGISTER(pll_cp_rw_pll_cpbias_cntrl),

		REGISTER(phy_sts_r_phy_ready),
		REGISTER(phy_sts_r_pll_lock),
		REGISTER(phy_sts_r_pll_vpl_det),

		REGISTER(phy_ctrl0_rw_phy_rst_n),
		REGISTER(phy_ctrl0_rw_shutdown_n),
		REGISTER(phy_ctrl0_rw_phy_mode),
		REGISTER(phy_ctrl0_rw_test_stop_clk_en),
		REGISTER(phy_ctrl0_rw_bist_en),

		REGISTER(phy_extended_ctr0_rw_forcerxmode_dck),
		REGISTER(phy_extended_ctr0_rw_forcerxmode_0),
		REGISTER(phy_extended_ctr0_rw_forcerxmode_1),
		REGISTER(phy_extended_ctr0_rw_forcerxmode_2),
		REGISTER(phy_extended_ctr0_rw_forcerxmode_3),
		REGISTER(phy_extended_ctr0_rw_turndisable_0),
		REGISTER(phy_extended_ctr0_rw_turndisable_1),
		REGISTER(phy_extended_ctr0_rw_turndisable_2),
		REGISTER(phy_extended_ctr0_rw_turndisable_3),

		REGISTER(phy_extended_ctr1_rw_enable0),
		REGISTER(phy_extended_ctr1_rw_enable1),
		REGISTER(phy_extended_ctr1_rw_enable2),
		REGISTER(phy_extended_ctr1_rw_enable3),
		REGISTER(phy_extended_ctr1_rw_enable_dck),
		REGISTER(phy_extended_ctr1_rw_enable_ov_en),
		REGISTER(phy_extended_ctr1_rw_forcetxstopmode_dck),
		REGISTER(phy_extended_ctr1_rw_forcetxstopmode_0),
		REGISTER(phy_extended_ctr1_rw_forcetxstopmode_1),
		REGISTER(phy_extended_ctr1_rw_forcetxstopmode_2),
		REGISTER(phy_extended_ctr1_rw_forcetxstopmode_3),
		REGISTER(phy_extended_ctr1_rw_txdatawidthhs_0),
		REGISTER(phy_extended_ctr1_rw_txdatawidthhs_1),
		REGISTER(phy_extended_ctr1_rw_txdatawidthhs_2),
		REGISTER(phy_extended_ctr1_rw_txdatawidthhs_3),
		REGISTER(phy_extended_ctr1_rw_cont_en),
		REGISTER(phy_extended_ctr1_rw_cont_en_ov_en),
	};

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_mipi_debugfs_create_x32(debugfs_regs, nr_entries, cdphy_g301->cdphy->debugfs);
}

static int reg_dump_show(struct seq_file *s, void *data)
{
	int ret, ret_dump;
	struct dw_mipi_cdphy_g301 *cdphy_g301 = s->private;
	struct drm_printer p = drm_seq_file_printer(s);
	struct device *dev;

	if (!cdphy_g301 || !cdphy_g301->cdphy || !cdphy_g301->cdphy->pdev)
		return -ENODEV;

	dev = &cdphy_g301->cdphy->pdev->dev;
	ret = pm_runtime_get_if_in_use(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to power ON, ret %d\n", ret);
		return ret;
	}

	if (!ret) {
		dev_dbg(dev, "Device is powered OFF\n");
		return ret;
	}

	ret_dump = gs_reg_dump_with_skips("CDPHY-TX", cdphy_g301->base, 0, cdphy_g301->tx_reg_size,
					  &p, &cdphy_dump_reg_access_table);
	if (ret_dump)
		goto out;
	ret_dump = gs_reg_dump_with_skips("U0-APB", cdphy_g301->u0_apb_regbank, 0,
					  cdphy_g301->u0_apb_reg_size, &p,
					  &apb_dump_reg_access_table);

out:
	ret = pm_runtime_put_sync(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to power OFF, ret %d\n", ret);
		return ret;
	}

	return ret_dump;
}

DEFINE_SHOW_ATTRIBUTE(reg_dump);

#else
static void dw_cdphy_g301_debugfs_remove(struct dw_mipi_cdphy_g301 *cdphy_g301)
{
}
#endif

int cdphy_init_g301(struct dw_mipi_cdphy *cdphy)
{
	struct platform_device *pdev = cdphy->pdev;
	struct device *dev = &pdev->dev;
	struct dw_mipi_cdphy_g301 *cdphy_g301;
	struct resource *mem;
	int ret;

	cdphy_g301 = devm_kzalloc(dev, sizeof(*cdphy_g301), GFP_KERNEL);
	if (!cdphy_g301)
		return -ENOMEM;

	cdphy_g301->cdphy = cdphy;
	cdphy->cdphy_priv_data = cdphy_g301;
	INIT_LIST_HEAD(&cdphy_g301->preset_list);

	/* Get CD-PHY MEM resources */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		pr_err("%s: Failed to get memory resources.\n", __func__);
		return -ENXIO;
	}

	cdphy_g301->tx_reg_size = resource_size(mem);
	cdphy_g301->tx_phys_addr = mem->start;
	cdphy_g301->base = devm_ioremap_resource(dev, mem);
	if (IS_ERR(cdphy_g301->base)) {
		pr_err("%s: Failed to remap the memory.\n", __func__);
		return PTR_ERR(cdphy_g301->base);
	}

	cdphy_g301->base_regs = devm_regmap_init_mmio(dev, cdphy_g301->base,
						      &dw_mipi_cdphy_base_regmap_cfg);
	if (IS_ERR(cdphy_g301->base_regs)) {
		ret = PTR_ERR(cdphy_g301->base_regs);
		pr_err("%s: Failed to initialise managed register map: %d\n", __func__, ret);
		return ret;
	}

	/* Get CD-PHY U0-APB-REGBANK MEM resources */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!mem) {
		pr_err("%s: Failed to get memory resources.\n", __func__);
		return -ENXIO;
	}

	cdphy_g301->u0_apb_reg_size = resource_size(mem);
	cdphy_g301->u0_apb_phys_addr = mem->start;
	cdphy_g301->u0_apb_regbank = devm_ioremap_resource(dev, mem);
	if (IS_ERR(cdphy_g301->u0_apb_regbank)) {
		pr_err("%s: Failed to remap the memory.\n", __func__);
		return PTR_ERR(cdphy_g301->u0_apb_regbank);
	}

	cdphy_g301->u0_apb_regbank_regs = devm_regmap_init_mmio(dev, cdphy_g301->u0_apb_regbank,
							  &dw_mipi_cdphy_u0_apb_regbank_regmap_cfg);
	if (IS_ERR(cdphy_g301->u0_apb_regbank_regs)) {
		ret = PTR_ERR(cdphy_g301->u0_apb_regbank_regs);
		pr_err("%s: Failed to initialise managed register map: %d\n", __func__, ret);
		return ret;
	}

	ret = dw_cdphy_regmap_fields_init(cdphy_g301);
	if (ret) {
		dev_err(dev, "%s: Failed to init cd-phy regmap fields: %d\n", __func__, ret);
		return ret;
	}

	// set CD-PHY ops
	cdphy->cdphy_ops = &dw_dphy_ops_g301;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	cdphy->debugfs = debugfs_create_dir(dev_name(&cdphy->pdev->dev), NULL);
	if (IS_ERR(cdphy->debugfs)) {
		dev_err(&cdphy->pdev->dev, "failed to create cd-phy debugfs root\n");
		return PTR_ERR(cdphy->debugfs);
	}
	dw_create_debugfs_hwv_files_1(cdphy_g301);
	dw_create_debugfs_hwv_files_2(cdphy_g301);
	/* local variables that are not stored on registers */
	dw_create_debugfs_hwv_files_3(cdphy_g301);

	debugfs_create_file("reg_dump", 0444, cdphy->debugfs, cdphy_g301, &reg_dump_fops);

	dw_cdphy_preset_config_init(cdphy_g301);
#endif

	return 0;
}

int cdphy_remove_g301(struct dw_mipi_cdphy *cdphy)
{
	struct dw_mipi_cdphy_g301 *cdphy_g301 = cdphy->cdphy_priv_data;
	struct platform_device *pdev = cdphy->pdev;
	struct device *dev = &pdev->dev;

	dw_cdphy_g301_debugfs_remove(cdphy_g301);

	dev_info(dev, "Removed cdphy_g301\n");

	dw_create_debugfs_hwv_files_4(cdphy_g301);

	return 0;
}

static ssize_t _get_cdphy_sscd_regdump(const char *name, void __iomem *base, u32 size,
				       const struct regmap_access_table *access_table, char *buffer,
				       ssize_t count)
{
	struct drm_print_iterator iter;
	struct drm_printer p;

	iter.data = buffer;
	iter.start = 0;
	iter.remain = count;

	p = drm_coredump_printer(&iter);

	gs_reg_dump_with_skips(name, base, 0, size, &p, access_table);

	return count - iter.remain;
}

static ssize_t get_cdphy_sscd_regdump(const char *name, void __iomem *base, u32 size,
				      const struct regmap_access_table *access_table,
				      char **regdump_buf)
{
	ssize_t count;

	count = _get_cdphy_sscd_regdump(name, base, size, access_table, NULL, INT_MAX);
	if (count <= 0) {
		*regdump_buf = NULL;
		return count;
	}

	*regdump_buf = vmalloc(count);
	if (!*regdump_buf)
		return -ENOMEM;

	_get_cdphy_sscd_regdump(name, base, size, access_table, *regdump_buf, count);

	return count;
}

static int cdphy_append_regdump_section(struct sscd_payload *payload, const char *name,
					const char *dump_name, void __iomem *base, u32 size,
					u64 phys_addr, const struct regmap_access_table *table)
{
	char *buf = NULL;
	ssize_t dump_size;

	dump_size = get_cdphy_sscd_regdump(name, base, size, table, &buf);
	if (dump_size > 0) {
		display_sscd_configure_phys_hexdump_section(&payload->cfgs[payload->num_cfgs++],
							    dump_name, buf, phys_addr, dump_size);
		return 0;
	}

	kvfree(buf);
	return dump_size < 0 ? dump_size : 0;
}

int dw_mipi_cdphy_sscd_prepare(struct phy *phy, struct sscd_payload *payload)
{
	struct dw_mipi_cdphy *cdphy = phy_get_drvdata(phy);
	struct dw_mipi_cdphy_g301 *cdphy_g301;
	struct device *dev;
	int ret = 0;
	int i;

	if (!cdphy || !payload)
		return -EINVAL;

	cdphy_g301 = cdphy->cdphy_priv_data;
	if (!cdphy_g301)
		return -ENODEV;

	dev = &cdphy->pdev->dev;

	ret = pm_runtime_get_if_in_use(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to check power status for PHY SSCD, ret %d\n", ret);
		goto out;
	} else if (ret == 0) {
		dev_dbg(dev, "Device is powered OFF, skipping PHY SSCD\n");
		payload->cfgs = NULL;
		payload->num_cfgs = 0;
		goto out;
	}

	payload->cfgs = kcalloc(2, sizeof(*payload->cfgs), GFP_KERNEL);
	if (!payload->cfgs) {
		ret = -ENOMEM;
		goto out_put;
	}

	ret = cdphy_append_regdump_section(payload, "CDPHY-TX", "CDPHY-TX reg dump",
					   cdphy_g301->base, cdphy_g301->tx_reg_size,
					   cdphy_g301->tx_phys_addr, &cdphy_dump_reg_access_table);
	if (ret < 0)
		goto out_free;

	ret = cdphy_append_regdump_section(payload, "U0-APB", "U0-APB reg dump",
					   cdphy_g301->u0_apb_regbank, cdphy_g301->u0_apb_reg_size,
					   cdphy_g301->u0_apb_phys_addr,
					   &apb_dump_reg_access_table);
	if (ret < 0)
		goto out_free;

	if (payload->num_cfgs == 0) {
		kfree(payload->cfgs);
		payload->cfgs = NULL;
	}

	goto out_put;

out_free:
	for (i = 0; i < payload->num_cfgs; i++)
		kvfree(payload->cfgs[i].virt_addr);
	kfree(payload->cfgs);
	payload->cfgs = NULL;
	payload->num_cfgs = 0;
out_put:
	pm_runtime_put(dev);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(dw_mipi_cdphy_sscd_prepare);

int dw_mipi_cdphy_sscd_destroy(struct phy *phy, struct sscd_payload *payload)
{
	int i;

	if (!payload || !payload->cfgs)
		return 0;

	for (i = 0; i < payload->num_cfgs; i++)
		kvfree(payload->cfgs[i].virt_addr);

	kfree(payload->cfgs);
	payload->cfgs = NULL;
	payload->num_cfgs = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(dw_mipi_cdphy_sscd_destroy);
