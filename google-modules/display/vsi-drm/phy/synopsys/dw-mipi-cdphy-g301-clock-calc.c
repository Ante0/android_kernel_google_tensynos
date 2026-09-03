// SPDX-License-Identifier: MIT

/*
 * Copyright (c) 2023 Google Inc.
 */

#include <linux/module.h>

#include "dw-mipi-cdphy-g301-clock-calc.h"

#define MAX_DATA_RATE_Mbps		4500
#define MIN_DATA_RATE_Mbps		80

#define F_CLKIN_KHZ			38400
#define F_LOOP_COMP_MIN_KHZ		19200
#define F_LOOP_COMP_MAX_KHZ		64000
#define MAX_N				16
#define F_VCO_OSC_MIN_KHZ		2000000
#define F_VCO_OSC_MAX_KHZ		4500000
#define MIN_LOG2_P			1
#define MAX_LOG2_P			6
#define F_OUT_MIN_KHZ			40000
#define F_OUT_MAX_KHZ			2250000
#define F_SSC_MOD_HZ			31500
#define F_SSC_MOD_DEPTH_PPM		5000

#define LP_DCO_CLOCK_PERIOD_PS		4770
#define LPTX_EN_DELAY_PS		5000
#define LPX_IO_SR0_FALL_DELAY_PS	12500
#define LPTX_IO_RESET_DELAY_PS		35000
#define FIFO_DELTA_ADDR			4
#define TXREADY_WORDCLKHS		2
#define TXREADY_SYMBOLCLKHS		5
#define DATATRAILDONE_DELTA		5
#define DPHY_D2A_HS_TX_DELAY		3
#define CPHY_D2A_HS_TX_DELAY		4

#define T_ESC_PS			50000
#define T_DCO_PS			4770
#define T_LPX_PS			55000
#define T_3_PREPARE			73150
#define T_HS_EXIT_PS			110000
#define T_CLK_PREPARE_PS		73150
#define T_CLK_TRAIL_PS			60000
#define T_CLK_PREPARE_PLUS_CLK_ZERO_PS	300000

#define HS_TX_13_TLPTXOVERLAP_REG	2
#define HS_TX_12_TLP11INIT_DCO_REG	52
#define HS_TX_11_TLPX_DCO_REG		11
#define HS_TX_7_T3PRPR_DCO_REG		17
#define HS_TX_1_TPOST_REG		20
#define HS_TX_8_TLP11END_DCO_REG	52
#define HS_TX_0_THSEXIT_DCO_REG		23

#define HS_TX_3_TLPTXOVERLAP_REG	2
#define HS_TX_10_TLP11INIT_DCO_REG	52
#define HS_TX_4_TLPX_DCO_REG		11
#define HS_TX_9_THSPRPR_DCO_REG		17
#define HS_TX_2_TCLKPRE_REG		3
#define HS_TX_6_TLP11END_DCO_REG	52
#define HS_TX_12_THSEXIT_DCO_REG	23

#define PS_IN_US	1000000
#define DIV_ROUND DIV_ROUND_CLOSEST

static int debug_log_calc;
module_param(debug_log_calc, int, 0660);

struct pll_charge_pump_key {
	u32 vco_min_khz;
	u32 vco_max_khz;
	u32 pll_vco_cntrl_5_3;
};

struct pll_charge_pump_table_item {
	struct pll_charge_pump_key key;
	struct pll_charge_pump_cfg item;
};

/* UI dependent params, unit ps */
struct cphy_global_params {
	u32 t_word_clk_ps;

	u32 t_3_prebegin;
	u32 t_3_post;
	u32 t_3_calpreamble;
};

/* UI dependent params, unit ps */
struct dphy_global_params {
	u32 t_word_clk_ps;

	u32 t_hs_prepare_ps;
	u32 t_clk_post_ps;
	u32 t_hs_zero_ps;
	u32 t_hs_trail_ps;
	u32 t_clk_zero_ps;
	u32 t_eot_ps;
	u32 t_clk_trail_adj_ps;
	u32 t_hs_prepare_plus_hszero_ps;
	u32 t_hs_trail_adj_ps;
};

static const struct pll_charge_pump_table_item pll_charge_pump_table[] = {
	{ .key = { 1706250, 2250000, 0 }, .item = {0,  0, 1, 8, 16, 2}, },
	{ .key = { 1413750, 1793750, 0 }, .item = {0,  0, 1, 8, 16, 2}, },
	{ .key = { 1170000, 1486260, 0 }, .item = {3,  0, 1, 8, 16, 2}, },
	{ .key = {  975000, 1230000, 0 }, .item = {7, 16, 1, 8, 16, 2}, },
	{ .key = {  853125, 1025000, 1 }, .item = {0,  0, 1, 8, 16, 2}, },
	{ .key = {  706875,  896875, 1 }, .item = {0,  0, 1, 8, 16, 2}, },
	{ .key = {  585000,  743125, 1 }, .item = {3,  0, 1, 8, 16, 2}, },
	{ .key = {  487500,  615000, 1 }, .item = {7, 16, 1, 8, 16, 2}, },
	{ .key = {  426560,  512500, 2 }, .item = {0,  0, 1, 8, 16, 2}, },
	{ .key = {  353400,  484400, 2 }, .item = {0,  0, 1, 8, 16, 2}, },
	{ .key = {  292500,  371500, 2 }, .item = {3,  0, 1, 8, 16, 2}, },
	{ .key = {  243750,  307500, 2 }, .item = {7, 16, 1, 8, 16, 2}, },
	{ .key = {  213300,  256250, 3 }, .item = {0,  0, 1, 8, 16, 2}, },
	{ .key = {  176720,  224200, 3 }, .item = {0,  0, 1, 8, 16, 2}, },
	{ .key = {  146250,  185780, 3 }, .item = {3,  0, 1, 8, 16, 2}, },
	{ .key = {  121880,  153750, 3 }, .item = {7, 16, 1, 8, 16, 2}, },
	{ .key = {  106640,  125120, 4 }, .item = {0,  0, 1, 8, 16, 2}, },
	{ .key = {   88360,  112100, 4 }, .item = {0,  0, 1, 8, 16, 2}, },
	{ .key = {   73130,   92900, 4 }, .item = {3,  0, 1, 8, 16, 2}, },
	{ .key = {   60930,   76870, 4 }, .item = {7, 16, 1, 8, 16, 2}, },
	{ .key = {   53320,   64000, 5 }, .item = {0,  0, 1, 8, 16, 2}, },
	{ .key = {   44180,   56000, 5 }, .item = {0,  0, 1, 8, 16, 2}, },
	{ .key = {   40000,   46440, 5 }, .item = {3,  0, 1, 8, 16, 2}, },
};

static void dump_cphy_global_params(const struct cphy_global_params *params)
{
	if (!params)
		return;

	pr_info("t_word_clk_ps   = %d\n", params->t_word_clk_ps);
	pr_info("t_3_prebegin    = %d\n", params->t_3_prebegin);
	pr_info("t_3_post        = %d\n", params->t_3_post);
	pr_info("t_3_calpreamble = %d\n", params->t_3_calpreamble);
}

static void dump_dphy_global_params(const struct dphy_global_params *params)
{
	if (!params)
		return;

	pr_info("t_word_clk_ps               = %d\n", params->t_word_clk_ps);
	pr_info("t_hs_prepare_ps             = %d\n", params->t_hs_prepare_ps);
	pr_info("t_clk_post_ps               = %d\n", params->t_clk_post_ps);
	pr_info("t_hs_zero_ps                = %d\n", params->t_hs_zero_ps);
	pr_info("t_hs_trail_ps               = %d\n", params->t_hs_trail_ps);
	pr_info("t_clk_zero_ps               = %d\n", params->t_clk_zero_ps);
	pr_info("t_eot_ps                    = %d\n", params->t_eot_ps);
	pr_info("t_clk_trail_adj_ps          = %d\n", params->t_clk_trail_adj_ps);
	pr_info("t_hs_prepare_plus_hszero_ps = %d\n", params->t_hs_prepare_plus_hszero_ps);
	pr_info("t_hs_trail_adj_ps           = %d\n", params->t_hs_trail_adj_ps);
}

static void dump_cphy_regs(const struct cphy_hs_regs *regs)
{
	pr_info("core_dig_clane_n_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg  = %d\n",
		regs->core_dig_clane_n_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg);
	pr_info("core_dig_clane_n_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg = %d\n",
		regs->core_dig_clane_n_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg);
	pr_info("core_dig_clane_n_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg      = %d\n",
		regs->core_dig_clane_n_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg);
	pr_info("core_dig_clane_n_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg      = %d\n",
		regs->core_dig_clane_n_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg);
	pr_info("core_dig_clane_n_rw_hs_tx_10_hs_tx_10_tprebegin_reg     = %d\n",
		regs->core_dig_clane_n_rw_hs_tx_10_hs_tx_10_tprebegin_reg);
	pr_info("core_dig_clane_n_rw_hs_tx_1_hs_tx_1_tpost_reg           = %d\n",
		regs->core_dig_clane_n_rw_hs_tx_1_hs_tx_1_tpost_reg);
	pr_info("core_dig_clane_n_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg    = %d\n",
		regs->core_dig_clane_n_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg);
	pr_info("core_dig_clane_n_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg    = %d\n",
		regs->core_dig_clane_n_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg);
	pr_info("core_dig_clane_n_rw_hs_tx_9_hs_tx_9_t3post_dco_reg      = %d\n",
		regs->core_dig_clane_n_rw_hs_tx_9_hs_tx_9_t3post_dco_reg);
	pr_info("core_dig_clane_n_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg     = %d\n",
		regs->core_dig_clane_n_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg);
}

static void dump_dphy_regs(const struct dphy_hs_regs *regs)
{
	if (!regs)
		return;

	pr_info("core_dig_dlane_clk_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg   = %d\n",
		regs->core_dig_dlane_clk_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg);
	pr_info("core_dig_dlane_clk_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg = %d\n",
		regs->core_dig_dlane_clk_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg);
	pr_info("core_dig_dlane_clk_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg       = %d\n",
		regs->core_dig_dlane_clk_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg);
	pr_info("core_dig_dlane_clk_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg    = %d\n",
		regs->core_dig_dlane_clk_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg);
	pr_info("core_dig_dlane_clk_rw_hs_tx_1_hs_tx_1_thszero_reg        = %d\n",
		regs->core_dig_dlane_clk_rw_hs_tx_1_hs_tx_1_thszero_reg);
	pr_info("core_dig_dlane_clk_rw_hs_tx_2_hs_tx_2_tclkpre_reg        = %d\n",
		regs->core_dig_dlane_clk_rw_hs_tx_2_hs_tx_2_tclkpre_reg);
	pr_info("core_dig_dlane_clk_rw_hs_tx_0_hs_tx_0_thstrail_reg       = %d\n",
		regs->core_dig_dlane_clk_rw_hs_tx_0_hs_tx_0_thstrail_reg);
	pr_info("core_dig_dlane_clk_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg   = %d\n",
		regs->core_dig_dlane_clk_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg);
	pr_info("core_dig_dlane_clk_rw_hs_tx_8_hs_tx_8_tclkpost_reg       = %d\n",
		regs->core_dig_dlane_clk_rw_hs_tx_8_hs_tx_8_tclkpost_reg);
	pr_info("core_dig_dlane_clk_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg   = %d\n",
		regs->core_dig_dlane_clk_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg);
	pr_info("core_dig_dlane_clk_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg  = %d\n",
		regs->core_dig_dlane_clk_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg);
	pr_info("core_dig_dlane_n_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg     = %d\n",
		regs->core_dig_dlane_n_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg);
	pr_info("core_dig_dlane_n_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg  = %d\n",
		regs->core_dig_dlane_n_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg);
	pr_info("core_dig_dlane_n_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg         = %d\n",
		regs->core_dig_dlane_n_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg);
	pr_info("core_dig_dlane_n_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg      = %d\n",
		regs->core_dig_dlane_n_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg);
	pr_info("core_dig_dlane_n_rw_hs_tx_1_hs_tx_1_thszero_reg          = %d\n",
		regs->core_dig_dlane_n_rw_hs_tx_1_hs_tx_1_thszero_reg);
	pr_info("core_dig_dlane_n_rw_hs_tx_0_hs_tx_0_thstrail_reg         = %d\n",
		regs->core_dig_dlane_n_rw_hs_tx_0_hs_tx_0_thstrail_reg);
	pr_info("core_dig_dlane_n_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg     = %d\n",
		regs->core_dig_dlane_n_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg);
	pr_info("core_dig_dlane_n_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg     = %d\n",
		regs->core_dig_dlane_n_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg);
	pr_info("core_dig_dlane_n_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg    = %d\n",
		regs->core_dig_dlane_n_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg);
}

static void dump_pll_charge_pump_cfg(const struct pll_charge_pump_cfg *cfg)
{
	if (!cfg)
		return;

	pr_info("pll_vco_cntrl_2_0	= %d\n", cfg->pll_vco_cntrl_2_0);
	pr_info("cpbias_cntrl		= %d\n", cfg->cpbias_cntrl);
	pr_info("gmp_cntrl		= %d\n", cfg->gmp_cntrl);
	pr_info("int_cntrl		= %d\n", cfg->int_cntrl);
	pr_info("prop_cntrl		= %d\n", cfg->prop_cntrl);
	pr_info("cb_vref_mpll_reg_rel_rw	= %d\n", cfg->cb_vref_mpll_reg_rel_rw);
}

static void dump_pll_cfg(const struct pll_config *pll_cfg)
{
	if (!pll_cfg)
		return;

	pr_info("pll_n			= %d\n", pll_cfg->pll_n);
	pr_info("pll_m			= %d\n", pll_cfg->pll_m);
	pr_info("pll_vco_cntrl_5_3	= %d\n", pll_cfg->pll_vco_cntrl_5_3);
	pr_info("pll_ssc_frac_en	= %d\n", pll_cfg->pll_ssc_frac_en);
	pr_info("pll_mint		= %d\n", pll_cfg->pll_mint);
	pr_info("pll_frac_quote		= %d\n", pll_cfg->pll_frac_quote);
	pr_info("pll_frac_rem		= %d\n", pll_cfg->pll_frac_rem);
	pr_info("pll_frac_den		= %d\n", pll_cfg->pll_frac_den);
	pr_info("pll_ssc_en		= %d\n", pll_cfg->pll_ssc_en);
	pr_info("pll_ssc_peak		= %d\n", pll_cfg->pll_ssc_peak);
	pr_info("pll_ssc_stepsize	= %d\n", pll_cfg->pll_ssc_stepsize);

	dump_pll_charge_pump_cfg(pll_cfg->charge_pump_cfg);
}

static void cphy_param_calc(u32 datarate_mbps, struct cphy_global_params *params)
{
	u32 ui_ps = DIV_ROUND_CLOSEST(PS_IN_US, datarate_mbps);

	if (!params)
		return;

	memset(params, 0, sizeof(*params));
	params->t_word_clk_ps = 7 * ui_ps;

	params->t_3_prebegin = 224 * ui_ps;
	params->t_3_post = 119 * ui_ps;
	params->t_3_calpreamble = DIV_ROUND(1799 * ui_ps, 2);
}

static void dphy_param_calc(u32 datarate_mbps, struct dphy_global_params *params)
{
	u32 ui_ps = DIV_ROUND_CLOSEST(PS_IN_US, datarate_mbps);

	if (!params)
		return;

	memset(params, 0, sizeof(*params));
	params->t_word_clk_ps = 8 * ui_ps;

	params->t_hs_prepare_ps = DIV_ROUND((40000 + 4 * ui_ps + 85000 + 6 * ui_ps) * 11, 20);
	params->t_clk_post_ps = DIV_ROUND((60000 + 52 * ui_ps) * 11, 10);
	params->t_hs_prepare_plus_hszero_ps = 145000 + 10 * ui_ps;
	params->t_hs_zero_ps = params->t_hs_prepare_plus_hszero_ps - params->t_hs_prepare_ps;
	params->t_hs_trail_ps = MAX(8 * ui_ps, 60000 + 4 * ui_ps);
	params->t_clk_zero_ps =
		DIV_ROUND((T_CLK_PREPARE_PLUS_CLK_ZERO_PS - T_CLK_PREPARE_PS) * 11, 10);
	params->t_eot_ps = 105000 + 12 * ui_ps;
	params->t_clk_trail_adj_ps = DIV_ROUND(T_CLK_TRAIL_PS + params->t_eot_ps, 2);
	params->t_hs_trail_adj_ps = DIV_ROUND(params->t_hs_trail_ps + params->t_eot_ps, 2);
}

int32_t cphy_regs_calc(u32 datarate_mbps, struct cphy_hs_regs *regs)
{
	struct cphy_global_params params;

	if (!regs)
		return -EINVAL;
	if (datarate_mbps < MIN_DATA_RATE_Mbps || datarate_mbps > MAX_DATA_RATE_Mbps)
		return -EINVAL;

	cphy_param_calc(datarate_mbps, &params);

	regs->core_dig_clane_n_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg = HS_TX_13_TLPTXOVERLAP_REG;
	regs->core_dig_clane_n_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg = HS_TX_12_TLP11INIT_DCO_REG;
	regs->core_dig_clane_n_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg = HS_TX_11_TLPX_DCO_REG;
	regs->core_dig_clane_n_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg = HS_TX_7_T3PRPR_DCO_REG;
	regs->core_dig_clane_n_rw_hs_tx_10_hs_tx_10_tprebegin_reg =
		DIV_ROUND_UP(T_LPX_PS + T_3_PREPARE + params.t_3_prebegin +
				5 * LP_DCO_CLOCK_PERIOD_PS - 3 * params.t_word_clk_ps,
				params.t_word_clk_ps) - 1;
	regs->core_dig_clane_n_rw_hs_tx_1_hs_tx_1_tpost_reg = HS_TX_1_TPOST_REG;
	regs->core_dig_clane_n_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg = DIV_ROUND_UP(T_LPX_PS +
			T_3_PREPARE + params.t_3_calpreamble + 5 * LP_DCO_CLOCK_PERIOD_PS -
			3 * params.t_word_clk_ps, params.t_word_clk_ps) - 1;
	regs->core_dig_clane_n_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg = HS_TX_8_TLP11END_DCO_REG;
	regs->core_dig_clane_n_rw_hs_tx_9_hs_tx_9_t3post_dco_reg = MAX(
			DIV_ROUND((HS_TX_1_TPOST_REG + 1) * params.t_word_clk_ps -
				params.t_word_clk_ps - 3 * LP_DCO_CLOCK_PERIOD_PS,
				LP_DCO_CLOCK_PERIOD_PS),
			DIV_ROUND(LPTX_IO_RESET_DELAY_PS, LP_DCO_CLOCK_PERIOD_PS));
	regs->core_dig_clane_n_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg = HS_TX_0_THSEXIT_DCO_REG;

	if (debug_log_calc) {
		dump_cphy_global_params(&params);
		dump_cphy_regs(regs);
	}

	return 0;
}

int32_t dphy_regs_calc(u32 datarate_mbps, struct dphy_hs_regs *regs)
{
	struct dphy_global_params params;

	if (!regs)
		return -EINVAL;
	if (datarate_mbps < MIN_DATA_RATE_Mbps || datarate_mbps > MAX_DATA_RATE_Mbps)
		return -EINVAL;

	dphy_param_calc(datarate_mbps, &params);

	regs->core_dig_dlane_clk_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg = HS_TX_3_TLPTXOVERLAP_REG;
	regs->core_dig_dlane_clk_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg =
		HS_TX_10_TLP11INIT_DCO_REG;
	regs->core_dig_dlane_clk_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg = HS_TX_4_TLPX_DCO_REG;
	regs->core_dig_dlane_clk_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg = HS_TX_9_THSPRPR_DCO_REG;
	regs->core_dig_dlane_clk_rw_hs_tx_2_hs_tx_2_tclkpre_reg = DPHY_D2A_HS_TX_DELAY;

	regs->core_dig_dlane_clk_rw_hs_tx_1_hs_tx_1_thszero_reg =
		DIV_ROUND_UP(T_LPX_PS + T_CLK_PREPARE_PS + params.t_clk_zero_ps +
				5 * LP_DCO_CLOCK_PERIOD_PS - 3 * params.t_word_clk_ps,
				params.t_word_clk_ps) - 1;
	regs->core_dig_dlane_clk_rw_hs_tx_0_hs_tx_0_thstrail_reg =
		DIV_ROUND_UP(params.t_clk_trail_adj_ps, params.t_word_clk_ps) - 1
		+ DPHY_D2A_HS_TX_DELAY;
	regs->core_dig_dlane_clk_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg =
		DIV_ROUND(DPHY_D2A_HS_TX_DELAY * params.t_word_clk_ps + params.t_clk_trail_adj_ps -
				params.t_word_clk_ps - 4 * LP_DCO_CLOCK_PERIOD_PS,
				LP_DCO_CLOCK_PERIOD_PS) - 1;
	regs->core_dig_dlane_clk_rw_hs_tx_8_hs_tx_8_tclkpost_reg =
		DIV_ROUND_UP(params.t_clk_post_ps, params.t_word_clk_ps) - 3;

	regs->core_dig_dlane_clk_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg = HS_TX_6_TLP11END_DCO_REG;
	regs->core_dig_dlane_clk_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg = HS_TX_12_THSEXIT_DCO_REG;

	regs->core_dig_dlane_n_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg = HS_TX_3_TLPTXOVERLAP_REG;
	regs->core_dig_dlane_n_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg = HS_TX_10_TLP11INIT_DCO_REG;
	regs->core_dig_dlane_n_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg = HS_TX_4_TLPX_DCO_REG;

	regs->core_dig_dlane_n_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg =
		DIV_ROUND_UP(LPX_IO_SR0_FALL_DELAY_PS + params.t_hs_prepare_ps,
				LP_DCO_CLOCK_PERIOD_PS) - 1;
	regs->core_dig_dlane_n_rw_hs_tx_1_hs_tx_1_thszero_reg =
		MAX(DIV_ROUND_UP(T_LPX_PS + params.t_hs_prepare_ps + params.t_hs_zero_ps +
					5 * LP_DCO_CLOCK_PERIOD_PS -
					3 * params.t_word_clk_ps, params.t_word_clk_ps) - 1, 0);
	regs->core_dig_dlane_n_rw_hs_tx_0_hs_tx_0_thstrail_reg =
		DIV_ROUND_UP(params.t_hs_trail_adj_ps, params.t_word_clk_ps) - 1 +
		DPHY_D2A_HS_TX_DELAY;
	regs->core_dig_dlane_n_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg =
		DIV_ROUND(DPHY_D2A_HS_TX_DELAY * params.t_word_clk_ps +
			params.t_hs_trail_adj_ps - params.t_word_clk_ps - 4 *
			LP_DCO_CLOCK_PERIOD_PS, LP_DCO_CLOCK_PERIOD_PS) - 1;

	regs->core_dig_dlane_n_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg = HS_TX_6_TLP11END_DCO_REG;
	regs->core_dig_dlane_n_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg = HS_TX_12_THSEXIT_DCO_REG;

	if (debug_log_calc) {
		dump_dphy_global_params(&params);
		dump_dphy_regs(regs);
	}

	return 0;
}

static int32_t search_charge_pump_cfg(u32 f_vco_out_khz, u32 vco_cntrl_5_3)
{
	/* search the charge pump config table */
	for (int ix = 0; ix < ARRAY_SIZE(pll_charge_pump_table); ++ix) {
		if (pll_charge_pump_table[ix].key.vco_min_khz <= f_vco_out_khz &&
				pll_charge_pump_table[ix].key.vco_max_khz >= f_vco_out_khz &&
				pll_charge_pump_table[ix].key.pll_vco_cntrl_5_3 == vco_cntrl_5_3) {
			return ix;
		}
	}

	return -1;
}

int32_t pll_calc(u32 datarate_mbps, u32 f_clkin_khz, bool ssc, struct pll_config *pll_cfg)
{
	/*
	 *          1/n            *m_int.m_frac             1/p             *2
	 * f_clkin ----> f_vco_in --------------> f_vco_osc ----> f_vco_out ----> datarate
	 */
	u32 n;
	u32 f_vco_in_khz;
	u32 f_vco_out_khz;
	u32 f_vco_osc_khz;
	u32 m_int;
	u32 m_frac;
	u32 p;
	u32 log2_p;
	int32_t idx_charge_pump;

	if (!pll_cfg)
		return -EINVAL;
	if (datarate_mbps < MIN_DATA_RATE_Mbps || datarate_mbps > MAX_DATA_RATE_Mbps)
		return -EINVAL;
	/* find the N */
	if (f_clkin_khz < F_LOOP_COMP_MIN_KHZ) {
		pr_info("%s: f_clkin %d is too small\n", __func__, f_clkin_khz);
		return -EINVAL;
	}
	n = 1;
	while (f_clkin_khz % n == 0 && f_clkin_khz / n > F_LOOP_COMP_MAX_KHZ && n <= MAX_N)
		++n;

	if (f_clkin_khz % n != 0 || f_clkin_khz / n < F_LOOP_COMP_MIN_KHZ || n > MAX_N) {
		pr_info("%s: no suitable N for f_clkin %d\n", __func__, f_clkin_khz);
		return -EINVAL;
	}

	f_vco_in_khz = f_clkin_khz / n;
	f_vco_out_khz = datarate_mbps * 1000 / 2;

	/* find the P. Note not all candidates have a valid charge pump config */
	idx_charge_pump = -1;
	log2_p = MIN_LOG2_P;
	p = 1 << log2_p;
	while (log2_p <= MAX_LOG2_P) {
		f_vco_osc_khz = f_vco_out_khz * p;
		if (f_vco_osc_khz >= F_VCO_OSC_MIN_KHZ && f_vco_osc_khz <= F_VCO_OSC_MAX_KHZ) {
			idx_charge_pump = search_charge_pump_cfg(f_vco_out_khz, log2_p - 1);
			if (idx_charge_pump >= 0)
				break;
		}
		++log2_p;
		p <<= 1;
	}

	if (idx_charge_pump < 0) {
		pr_err("%s: failed to config datarate %d, no charge pump config found\n", __func__,
				datarate_mbps);
		return -EINVAL;
	}

	memset(pll_cfg, 0, sizeof(*pll_cfg));

	if (!ssc && (f_vco_osc_khz % f_vco_in_khz == 0)) {
		/* integral mode */
		m_int = f_vco_osc_khz / f_vco_in_khz;
		m_frac = 0;
	} else {
		/* fractional mode, resolution is 0.5 */
		pll_cfg->pll_ssc_frac_en = 1;
		m_int = f_vco_osc_khz * 2 / f_vco_in_khz;
		m_frac = (f_vco_osc_khz * 2 - f_vco_in_khz * m_int) * (1 << 16) / f_vco_in_khz;
	}

	/* collect pll config */
	pll_cfg->pll_n = n - 1;
	pll_cfg->pll_vco_cntrl_5_3 = log2_p - 1;
	pll_cfg->charge_pump_cfg = &pll_charge_pump_table[idx_charge_pump].item;
	if (!pll_cfg->pll_ssc_frac_en) {
		pll_cfg->pll_m = m_int * 2 - 32;
	} else {
		/* it's fine to leave pll_cfg->pll_m as 0 in factional mode */
		pll_cfg->pll_mint = m_int - 32;
		pll_cfg->pll_frac_quote = m_frac;
		pll_cfg->pll_frac_rem = 0;
		pll_cfg->pll_frac_den = 1;
		if (ssc) {
			pll_cfg->pll_ssc_en = 1;
			pll_cfg->pll_ssc_peak = DIV_ROUND(m_int * F_SSC_MOD_DEPTH_PPM * 65536ULL,
					2 * 1000000);
			pll_cfg->pll_ssc_stepsize = DIV_ROUND((u64)F_SSC_MOD_HZ * m_int *
				F_SSC_MOD_DEPTH_PPM * (1 << 25), (u64)1000000000ULL * f_vco_in_khz);
		}
	}

	if (debug_log_calc)
		dump_pll_cfg(pll_cfg);

	return 0;
}

int32_t pll_calc_v2(u32 datarate_mbps, u32 f_clkin_khz, bool ssc, struct pll_config *pll_cfg)
{
	/*
	 *          1/n            *m_int.m_frac             1/p             *2
	 * f_clkin ----> f_vco_in --------------> f_vco_osc ----> f_vco_out ----> datarate
	 */
	u32 n;
	u32 f_vco_in_khz;
	u32 f_vco_out_khz;
	u32 f_vco_osc_khz;
	u32 m_int;
	u32 m_frac;
	u32 p;
	u32 log2_p;
	int32_t idx_charge_pump;

	if (!pll_cfg)
		return -EINVAL;
	if (datarate_mbps < MIN_DATA_RATE_Mbps || datarate_mbps > MAX_DATA_RATE_Mbps)
		return -EINVAL;
	/* find the N */
	if (f_clkin_khz < F_LOOP_COMP_MIN_KHZ) {
		pr_info("%s: f_clkin %d is too small\n", __func__, f_clkin_khz);
		return -EINVAL;
	}
	n = 1;
	while (f_clkin_khz % n == 0 && f_clkin_khz / n > F_LOOP_COMP_MAX_KHZ && n <= MAX_N)
		++n;

	if (f_clkin_khz % n != 0 || f_clkin_khz / n < F_LOOP_COMP_MIN_KHZ || n > MAX_N) {
		pr_info("%s: no suitable N for f_clkin %d\n", __func__, f_clkin_khz);
		return -EINVAL;
	}

	f_vco_in_khz = f_clkin_khz / n;
	f_vco_out_khz = datarate_mbps * 1000 / 2;

	/* find the P. Note not all candidates have a valid charge pump config */
	idx_charge_pump = -1;
	log2_p = MIN_LOG2_P;
	p = 1 << log2_p;
	while (log2_p <= MAX_LOG2_P) {
		f_vco_osc_khz = f_vco_out_khz * p;
		if (f_vco_osc_khz >= F_VCO_OSC_MIN_KHZ && f_vco_osc_khz <= F_VCO_OSC_MAX_KHZ) {
			idx_charge_pump = search_charge_pump_cfg(f_vco_out_khz, log2_p - 1);
			if (idx_charge_pump >= 0)
				break;
		}
		++log2_p;
		p <<= 1;
	}

	if (idx_charge_pump < 0) {
		pr_err("%s: failed to config datarate %d, no charge pump config found\n", __func__,
				datarate_mbps);
		return -EINVAL;
	}

	memset(pll_cfg, 0, sizeof(*pll_cfg));

	if (!ssc && (f_vco_osc_khz % f_vco_in_khz == 0)) {
		/* integral mode */
		m_int = f_vco_osc_khz / f_vco_in_khz;
		m_frac = 0;
	} else {
		/* fractional mode, resolution is 0.5 */
		pll_cfg->pll_ssc_frac_en = 1;
		m_int = f_vco_osc_khz * 2 / f_vco_in_khz;
		/* */
		m_frac = (f_vco_osc_khz * 2 - f_vco_in_khz * m_int) * (1 << 16) / f_vco_in_khz / 2;
	}

	/* collect pll config */
	pll_cfg->pll_n = n - 1;
	pll_cfg->pll_vco_cntrl_5_3 = log2_p - 1;
	pll_cfg->charge_pump_cfg = &pll_charge_pump_table[idx_charge_pump].item;
	if (!pll_cfg->pll_ssc_frac_en) {
		pll_cfg->pll_m = m_int * 2 - 32;
	} else {
		/* it's fine to leave pll_cfg->pll_m as 0 in factional mode */
		pll_cfg->pll_mint = m_int - 32;
		pll_cfg->pll_frac_quote = m_frac;
		pll_cfg->pll_frac_rem = 0;
		pll_cfg->pll_frac_den = 1;
		if (ssc) {
			pll_cfg->pll_ssc_en = 1;
			pll_cfg->pll_ssc_peak = DIV_ROUND(m_int * F_SSC_MOD_DEPTH_PPM * 65536ULL,
					2 * 1000000);
			pll_cfg->pll_ssc_stepsize = DIV_ROUND((u64)F_SSC_MOD_HZ * m_int *
				F_SSC_MOD_DEPTH_PPM * (1 << 25), (u64)1000000000ULL * f_vco_in_khz);
		}
	}

	if (debug_log_calc)
		dump_pll_cfg(pll_cfg);

	return 0;
}
