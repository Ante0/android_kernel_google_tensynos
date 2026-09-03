/* SPDX-License-Identifier: MIT */

#ifndef __DW_MIPI_CDPHY_G301_CLOCK_CALC_
#define __DW_MIPI_CDPHY_G301_CLOCK_CALC_

#include <linux/types.h>

struct cphy_hs_regs {
	u32 core_dig_clane_n_rw_hs_tx_13_hs_tx_13_tlptxoverlap_reg;
	u32 core_dig_clane_n_rw_hs_tx_12_hs_tx_12_tlp11init_dco_reg;
	u32 core_dig_clane_n_rw_hs_tx_11_hs_tx_11_tlpx_dco_reg;
	u32 core_dig_clane_n_rw_hs_tx_7_hs_tx_7_t3prpr_dco_reg;
	u32 core_dig_clane_n_rw_hs_tx_10_hs_tx_10_tprebegin_reg;
	u32 core_dig_clane_n_rw_hs_tx_1_hs_tx_1_tpost_reg;
	u32 core_dig_clane_n_rw_hs_tx_2_hs_tx_2_tcalpreamble_reg;
	u32 core_dig_clane_n_rw_hs_tx_8_hs_tx_8_tlp11end_dco_reg;
	u32 core_dig_clane_n_rw_hs_tx_9_hs_tx_9_t3post_dco_reg;
	u32 core_dig_clane_n_rw_hs_tx_0_hs_tx_0_thsexit_dco_reg;
};

struct dphy_hs_regs {
	u32 core_dig_dlane_clk_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg;
	u32 core_dig_dlane_clk_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg;
	u32 core_dig_dlane_clk_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg;
	u32 core_dig_dlane_clk_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg;
	u32 core_dig_dlane_clk_rw_hs_tx_1_hs_tx_1_thszero_reg;
	u32 core_dig_dlane_clk_rw_hs_tx_2_hs_tx_2_tclkpre_reg;
	u32 core_dig_dlane_clk_rw_hs_tx_0_hs_tx_0_thstrail_reg;
	u32 core_dig_dlane_clk_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg;
	u32 core_dig_dlane_clk_rw_hs_tx_8_hs_tx_8_tclkpost_reg;
	u32 core_dig_dlane_clk_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg;
	u32 core_dig_dlane_clk_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg;
	u32 core_dig_dlane_n_rw_hs_tx_3_hs_tx_3_tlptxoverlap_reg;
	u32 core_dig_dlane_n_rw_hs_tx_10_hs_tx_10_tlp11init_dco_reg;
	u32 core_dig_dlane_n_rw_hs_tx_4_hs_tx_4_tlpx_dco_reg;
	u32 core_dig_dlane_n_rw_hs_tx_9_hs_tx_9_thsprpr_dco_reg;
	u32 core_dig_dlane_n_rw_hs_tx_1_hs_tx_1_thszero_reg;
	u32 core_dig_dlane_n_rw_hs_tx_0_hs_tx_0_thstrail_reg;
	u32 core_dig_dlane_n_rw_hs_tx_5_hs_tx_5_thstrail_dco_reg;
	u32 core_dig_dlane_n_rw_hs_tx_6_hs_tx_6_tlp11end_dco_reg;
	u32 core_dig_dlane_n_rw_hs_tx_12_hs_tx_12_thsexit_dco_reg;
};

struct pll_charge_pump_cfg {
	u32 pll_vco_cntrl_2_0;
	u32 cpbias_cntrl;
	u32 gmp_cntrl;
	u32 int_cntrl;
	u32 prop_cntrl;
	u32 cb_vref_mpll_reg_rel_rw;
};

struct pll_config {
	u32 pll_n;
	u32 pll_m;
	u32 pll_vco_cntrl_5_3;

	u32 pll_ssc_frac_en;
	u32 pll_mint;
	u32 pll_frac_quote;
	u32 pll_frac_rem;
	u32 pll_frac_den;

	u32 pll_ssc_en;
	u32 pll_ssc_peak;
	u32 pll_ssc_stepsize;

	const struct pll_charge_pump_cfg *charge_pump_cfg;
};

int32_t cphy_regs_calc(u32 datarate_mbps, struct cphy_hs_regs *regs);
int32_t dphy_regs_calc(u32 datarate_mbps, struct dphy_hs_regs *regs);
int32_t pll_calc(u32 datarate_mbps, u32 f_clkin_khz, bool ssc, struct pll_config *pll_cfg);
int32_t pll_calc_v2(u32 datarate_mbps, u32 f_clkin_khz, bool ssc, struct pll_config *pll_cfg);

#endif
