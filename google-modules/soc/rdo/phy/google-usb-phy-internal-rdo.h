/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright 2023 Google LLC
 * Google's SoC-specific Glue Driver for the dwc3 USB controller
 */

/* HSIO_N_USB_CFG_CSR block */
#define USBCS_PHY_CFG1_OFFSET  0x18
#define USB2PHY_CFG19_OFFSET   0x60
#define USB2PHY_CFG20_OFFSET   0x64
#define USB2PHY_CFG21_OFFSET   0x68
#define USBCS_TOP_CFG1_OFFSET  0x74

/* HSIO_N_DPTX_TCA_CSR block */
#define TCA_INTR_STS_OFFSET     0x8
#define TCA_TCPC_OFFSET         0x14
#define TCPC_MUX_NO_CONN		0x0
#define TCPC_MUX_USB_ONLY		0x1
#define TCPC_MUX_DP_ONLY		0x2
#define TCPC_MUX_USB_DP			0x3

/* HSIO_N_DPTX_PHY_CSR block */
#define SUP_DIG_ANA_BG_OVRD_OUT_OFFSET 0x128U

/* HSIO_N_DP_TOP_CSR block */
#define DP_PHY_CFG3_OFFSET		0x24
#define DP_USBCS_PHY_CFG1_OFFSET	0x34
#define TCA_REG1_OFFSET			0x84
#define TCA_OVERRIDE_REG1_OFFSET	0x88
#define TCA_OVERRIDE_SEL_REG1_OFFSET	0x8c

// TODO(mnkumar): This is only for LGA, refactor this
#define USBDP_TOP_CFG_REG_OFFSET 0x44

/* HSIO_N_DWC_DPTX block */
#define PHYIF_CTRL_OFFSET	0xA00
#define TYPEC_CTRL_OFFSET	0xC08

/* GP_SS_CSR block */
#define GP_SS_CLK_EN_OFFSET 0x8

/* HSIO_N_USB_CFG_CSR block */
typedef union {
	/* NB: A register with same name is also in DP_TOP_CSR region */
	struct {
		uint32_t phy0_mplla_force_en : 1; // Force MPLLA power up (ignore txX_mpll_en input)
		uint32_t phy0_mplla_ssc_en : 1; // Enable spread-spectrum clock gen on mplla_div_clk
		uint32_t phy0_mpllb_force_en: 1; // Force MPLLB power up (ignore txX_mpll_en input)
		uint32_t phy0_mpllb_ssc_en: 1; // Enable spread-spectrum clock gen on mpllb_div_clk
		uint32_t phy0_ref_use_pad : 1;
		uint32_t phy0_sram_bypass : 1;
		uint32_t phy0_sram_ext_ld_done : 1;
		uint32_t phy_ext_ctrl_sel : 1;
		uint32_t phy_lane0_rx2tx_par_lb_en : 1;
		uint32_t phy_res_req_in : 1;
		uint32_t phy_rtune_req : 1;
		uint32_t pipe_lane0_tx2rx_loopbk : 1;
		uint32_t pipe_rx0_cmn_refclk_mode : 1;
		uint32_t pipe_rx_cdr_legacy_en : 1;
		uint32_t pipe_rx_recal_cont_en : 1;
		uint32_t reserved_31_15 : 17;
	} bf;
	uint32_t reg_value;
} USBCS_PHY_CFG1;

typedef union {
	struct {
		uint32_t volatile phy_cfg_cr_clk_sel : 1;
		uint32_t volatile phy_cfg_pll_cpbias_cntrl : 7;
		uint32_t volatile phy_cfg_pll_fb_div : 12;
		uint32_t volatile phy_cfg_pll_gmp_cntrl : 2;
		uint32_t volatile phy_cfg_pll_int_cntrl : 6;
		uint32_t volatile phy_cfg_por_in_lx : 1;
		uint32_t volatile phy_cfg_rcal_bypass : 1;
		uint32_t volatile phy_cfg_rptr_mode : 1;
		uint32_t volatile phy_cfg_rx_hs_term_en : 1;
	} bf;
	uint32_t reg_value;
} USB2PHY_CFG19;

/* USBCS_USB2PHY_CFG20 DESCRIPTION :USB 2.0 PHY Pin configurations */
typedef union {
	struct {
		uint32_t volatile phy_cfg_pll_prop_cntrl : 6;
		uint32_t volatile phy_cfg_pll_ref_div : 4;
		uint32_t volatile phy_cfg_pll_vco_cntrl : 3;
		uint32_t volatile phy_cfg_pll_vref_tune : 2;
		uint32_t volatile phy_cfg_rcal_code : 4;
		uint32_t volatile phy_cfg_rcal_offset : 4;
		uint32_t volatile phy_cfg_rx_eq_ctle : 2;
		uint32_t volatile phy_cfg_rx_hs_tune : 3;
		uint32_t volatile phy_cfg_tx_fsls_slew_rate_tune : 1;
		uint32_t volatile phy_cfg_tx_fsls_vref_tune : 2;
		uint32_t volatile phy_cfg_tx_fsls_vreg_bypass : 1;
	} bf;
	uint32_t reg_value;
} USB2PHY_CFG20;

typedef union {
	struct {
		uint32_t phy_cfg_tx_hs_vref_tune : 3;
		uint32_t phy_cfg_tx_hs_xv_tune : 2;
		uint32_t phy_cfg_tx_preemp_tune : 3;
		uint32_t phy_cfg_tx_res_tune : 2;
		uint32_t phy_cfg_tx_rise_tune : 2;
		uint32_t phy_enable : 1;
		uint32_t ref_freq_sel : 3;
		uint32_t retenable_n : 1;
		uint32_t utmi_clk_force_en : 1;
		uint32_t vbus_valid_ext : 1;
		uint32_t reserved_31_19 : 13;
	} bf;
	uint32_t reg_value;
} USB2PHY_CFG21;

typedef union {
	struct {
		uint32_t xa_ack_evt: 1; // XBar_Assist ack event status
		uint32_t xa_timeout_evt : 1; // XBar_Assist timeout event status
		uint32_t reserved_7_2 : 6;
		uint32_t sys_vbusvalid_evt : 1; // sys_vbusvalid change event status
		uint32_t sys_vbusvalid : 1; // sys_vbusvalid status/value as seen on TCA i/p
		uint32_t tca_vbusvalid_evt : 1; // tca_vbusvalid change event status
		uint32_t tca_vbusvalid : 1; // tca_vbusvalid status/value as seen on TCA o/p
		uint32_t tca_drv_host_vbus_evt : 1; // tca_drv_host_vbus change event status
		uint32_t tca_drv_host_vbus : 1; // tca_drv_host_vbus status/value as seen on TCA o/p
		uint32_t reserved_31_14 : 18;
	} bf;
	uint32_t reg_value;
} TCA_INTR_STS;

typedef union {
	struct {
		uint32_t tcpc_mux_control : 2; // TCPM Mux setting for TypeC/DPAlt_Xbar and TCA sync
		uint32_t tcpc_connector_orientation : 1; // Connector orientation from TCPM
		uint32_t tcpc_low_power_en : 1; // Control from TCPM to put PHY in LP state
		uint32_t tcpc_valid : 1; // Indicates to Bar_Assist that fields are valid
		uint32_t RESERVED_31_5 : 27;
	} bf;
	uint32_t reg_value;
} TCA_TCPC;

/* HSIO_N_DPTX_PHY_CSR block */
typedef union {
	struct {
		uint16_t BG_EN : 1; //Overrides the bg_ana_en signal
		uint16_t BG_FAST_START : 1; //Overrides the bg_ana_fast_start signal
		uint16_t BG_KICK_START : 1; //Overrides the bg_ana_kick_start_en signal
		uint16_t BG_OVRD_EN : 1; //Override bit for bandgap outputs
		uint16_t ANA_ASYNC_RST : 1; //Override for reset regs for analog latches
		uint16_t ANA_ASYNC_RST_OVRD_EN : 1; //Override enable for ana_async_rst
		uint16_t REF_VREG_FAST_START : 1; //Override for ref_ana_vreg_fast_start
		uint16_t REF_VREG_FAST_START_OVRD_EN : 1; //Override for ref_ana_vreg_fast_start
		uint16_t REF_VREG_REF_SEL : 2; // Override value for ref_ana_vreg_ref_sel
		uint16_t REF_VREG_REF_SEL_OVRD_EN : 1; // Override for ref_ana_vreg_ref_sel
		uint16_t RESERVED_15_11 : 5; // Reserved for future use.
	} bf;
	uint16_t reg_value;
} SUP_DIG_ANA_BG_OVRD_OUT;

/* HSIO_N_DP_TOP_CSR block */
/* Layout of TCA_REG1, TCA_OVERRIDE_REG1 & TCA_OVERRIDE_SEL_REG1 registers */
typedef union {
	struct {
		uint32_t dpalt_disable_ack : 1;
		uint32_t ss_rxdet_disable_ack : 1;
		uint32_t sys_vbusvalid : 1;
		uint32_t tca_dp4_por : 1;
		uint32_t tca_usb_dev_por : 1;
		uint32_t typec_flip_invert : 1;
	} bf;
	uint32_t reg_value;
} TCA_X_REG1;

typedef union {
	struct {
		uint32_t phy0_mplla_force_en : 1; // Power up MPLL irresp. of txX_mpll_en
		uint32_t phy0_mplla_ssc_en : 1; // Enable SS clock gen on mplla_div_clk o/p
		uint32_t phy0_ref_alt_clk_lp_sel : 1; // select between alt_clk and alt_lp_clk
		uint32_t phy0_ref_use_pad : 1; // Use clock from Pad as ref_clk
		uint32_t phy_ext_ref_range : 3; // Ref clock frequency range 001 - 26.1 to 52Mhz
		uint32_t phy0_ref_repeat_clk_en : 1;
		uint32_t phy_ext_rx_vref_ctrl : 3;
		uint32_t phy_ext_ctrl_sel : 1;
		uint32_t phy_ext_tx_vswing_lvl : 3;
		uint32_t phy_res_req_in : 1;
		uint32_t phy_res_ack_out : 1;
		uint32_t phy_rtune_req : 1;
		uint32_t phy_rtune_ack : 1;
		uint32_t phy0_sup_pre_hp : 1;
		uint32_t phy0_sup_rx_vco_vref_sel : 3;
		uint32_t phy_ss_lane0_rx2tx_par_lb_en : 1;
		uint32_t phy_ss_lane0_tx2rx_ser_lb_en : 1;
		uint32_t phy_ext_tx_dcc_byp_ac_cap : 2;
		uint32_t phy0_sram_bypass : 1;
		uint32_t phy0_sram_ext_ld_done : 1;
		uint32_t phy0_sram_init_done : 1;
		uint32_t reserved_31_30 : 2;
	} bf;
	uint32_t reg_value;
} DP_USBCS_PHY_CFG1;

typedef union {
	struct {
	uint32_t volatile dp_tx0_dcc_byp_ac_cap : 1; /* 0 SW=rw HW=ro 0x0 */
	uint32_t volatile dp_tx1_dcc_byp_ac_cap : 1; /* 1 SW=rw HW=ro 0x0 */
	uint32_t volatile dp_tx2_dcc_byp_ac_cap : 1; /* 2 SW=rw HW=ro 0x0 */
	uint32_t volatile dp_tx3_dcc_byp_ac_cap : 1; /* 3 SW=rw HW=ro 0x0 */
	uint32_t volatile dp_tx0_iboost_en : 1; /* 4 SW=rw HW=ro 0x1 */
	uint32_t volatile dp_tx1_iboost_en : 1; /* 5 SW=rw HW=ro 0x1 */
	uint32_t volatile dp_tx2_iboost_en : 1; /* 6 SW=rw HW=ro 0x1 */
	uint32_t volatile dp_tx3_iboost_en : 1; /* 7 SW=rw HW=ro 0x1 */
	uint32_t resv8 : 24;
} bf;
uint32_t reg_value;
} DP_PHY_CFG3;

/* HSIO_N_DWC_DPTX block */
typedef union {
	struct {
		uint32_t disable_ack : 1; // Controls tca_disable_ack_o o/p pin
		uint32_t disable_status : 1; // Status of the primary input pin tca_disable_i
		uint32_t interrupt_status : 1; // Interrupt status pin for TYPE-C
		uint32_t reserved : 29;
	} bf;
	uint32_t reg_value;
} TYPEC_CTRL;

typedef union {
	struct {
		uint32_t tps_sel : 4; // Select PHY training pattern
		uint32_t phyrate : 2; // Rate setting for the PHY
		uint32_t phy_lanes : 2; // Number of lanes active
		uint32_t xmit_enable : 4; // Per-lane PHY xmitter enable
		uint32_t phy_busy : 4; // Per-lane PHY Status
		uint32_t ssc_dis : 1; //Disable SSC on the PHY.
		uint32_t phy_powerdown : 4;// Per-lane PHY power-down control
		uint32_t reserved_24_21 : 4;
		uint32_t phy_width : 1; // Data width of main link interface to PHY
		uint32_t edp_phy_rate : 3; //Support for the eDP rates
		uint32_t reserved_31_29 : 3;
	} bf;
	uint32_t reg_value;
} PHYIF_CTRL;

// TODO(mnkumar): This is only for LGA, refactor this after separating the headers
typedef union {
	struct {
		uint32_t pmgt_ref_clk_req_n : 1;               /* 0 SW=rw HW=ro 0x0 */
		uint32_t ss_lane0_active : 1;               /* 1 SW=rw HW=ro 0x0 */
		uint32_t ss_lane1_active : 1;               /* 2 SW=rw HW=ro 0x0 */
		uint32_t usb32_pipe_lane0_disable : 1;               /* 3 SW=rw HW=ro 0x0 */
		uint32_t usb32_pipe_lane0_tx2rx_loopbk : 1;               /* 4 SW=rw HW=ro 0x0 */
		uint32_t usb32_pipe_rx0_es_mode : 1;               /* 5 SW=rw HW=ro 0x0 */
		uint32_t resv6 : 26;
	} bf;
	uint32_t reg_value;
} USBDP_TOP_CFG_REG;

union USBCS_TOP_CFG1 {
	struct {
		uint32_t awuservc : 3;  // 2:0 default=0x0 sw=rw hw=ro
		uint32_t aruservc : 3;  // 5:3 default=0x0 sw=rw hw=ro
		uint32_t awqos : 2;  // 7:6 default=0x0 sw=rw hw=ro
		uint32_t arqos : 2;  // 9:8 default=0x0 sw=rw hw=ro
		uint32_t spare_cfg : 22;  // 31:10 default=0x0 sw=rw hw=ro
	} bf;
	u32 reg_value;  // default=0x0
};

union GP_SS_CSR_CLK_EN {
	struct {
		uint32_t usb_force_clk_en : 1;
		uint32_t pcie_force_clk_en : 1;
	} bf;
	u32 reg_value;
};
