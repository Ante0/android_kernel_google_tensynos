/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020-2021 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare MIPI DSI 2 host - Core registers
 *
 * Author: Marcelo Borges <marcelob@synopsys.com>
 * Author: Pedro Correia <correia@synopsys.com>
 * Author: Nuno Cardoso <cardoso@synopsys.com>
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#ifndef _DSI2H_H
#define _DSI2H_H

#include <linux/regmap.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_bridge.h>
#include <linux/phy/phy.h>
#include <gs_drm/gs_drm_connector.h>

#define MASK(msb, lsb)		\
	((((u32)1 << ((msb) - (lsb) + 1)) - 1) << (lsb))

/* Parameters for the waiting for status flags */
#define POOLING_USLEEP_MIN			50
#define POOLING_USLEEP_MAX			(POOLING_USLEEP_MIN + 25)

/*
 * REGISTERS
 */

/*  Main registers */
#define DW_DSI2H_CORE_ID			0x0
#define DW_DSI2H_VERSION			0x4
#define DW_DSI2H_PWR_UP				0xc
#define DW_DSI2H_SOFT_RESET			0x10
#define DW_DSI2H_INT_ST_MAIN			0x400
#define DW_DSI2H_MODE_CTRL			0x18
#define DW_DSI2H_MODE_STATUS			0x1c
#define DW_DSI2H_CORE_STATUS			0x20
#define CRI_BUSY				BIT(16)
#define CRI_RD_DATA_AVAIL			BIT(18)
#define PRI_BUSY				BIT(24)
#define PRI_RX_DATA_AVAIL			BIT(26)
#define DW_DSI2H_MANUAL_MODE_CFG		0x24
#define DW_DSI2H_OBS_FSM_SEL			0x28
#define DW_DSI2H_OBS_FSM_STATUS			0x2c
#define DW_DSI2H_OBS_FSM_CTRL			0x30
#define DW_DSI2H_OBS_FIFO_SEL			0x34
#define DW_DSI2H_OBS_FIFO_STATUS		0x38
#define DW_DSI2H_OBS_FIFO_CTRL			0x3c
#define DW_DSI2H_TO_HSTX_CFG			0x48
#define DW_DSI2H_TO_HSTXRDY_CFG			0x4c
#define DW_DSI2H_TO_LPRX_CFG			0x50
#define DW_DSI2H_TO_LPTXRDY_CFG			0x54
#define DW_DSI2H_TO_LPTXTRIG_CFG		0x58
#define DW_DSI2H_TO_LPTXULPS_CFG		0x5c
#define DW_DSI2H_TO_BTA_CFG			0x60
/*  PHY registers */
#define DW_DSI2H_PHY_MODE_CFG			0x100
#define DW_DSI2H_PHY_CLK_CFG_LPTX_CLK_DIV_MIN	0x1U
#define DW_DSI2H_PHY_CLK_CFG_LPTX_CLK_DIV_MAX	0x3FU
#define DW_DSI2H_PHY_CLK_CFG			0x104
#define DW_DSI2H_PHY_STATUS			0x108
#define DW_DSI2H_PHY_LP2HS_MAN_CFG		0x10c
#define DW_DSI2H_PHY_LP2HS_AUTO			0x110
#define DW_DSI2H_PHY_HS2LP_MAN_CFG		0x114
#define DW_DSI2H_PHY_HS2LP_AUTO			0x118
#define DW_DSI2H_PHY_MAX_RD_T_MAN_CFG		0x11c
#define DW_DSI2H_PHY_MAX_RD_T_AUTO		0x120
#define DW_DSI2H_PHY_ESC_CMD_T_MAN_CFG		0x124
#define DW_DSI2H_PHY_ESC_CMD_T_AUTO		0x128
#define DW_DSI2H_PHY_ESC_BYTE_T_MAN_CFG		0x12c
#define DW_DSI2H_PHY_ESC_BYTE_T_AUTO		0x130
#define DW_DSI2H_PHY_IPI_RATIO_MAN_CFG		0x134
#define DW_DSI2H_PHY_IPI_RATIO_AUTO		0x138
#define DW_DSI2H_PHY_SYS_RATIO_MAN_CFG		0x13c
#define DW_DSI2H_PHY_SYS_RATIO_AUTO		0x140
#define DW_DSI2H_PRI_TX_CMD			0x1c0
#define PHY_TX_TRIGGER_0			BIT(0)
#define PHY_TX_TRIGGER_1			BIT(1)
#define PHY_TX_TRIGGER_2			BIT(2)
#define PHY_TX_TRIGGER_3			BIT(3)
#define DW_DSI2H_PRI_RX_CMD			0x1c4
#define PHY_RX_TRIGGER_0			BIT(0)
#define PHY_RX_TRIGGER_1			BIT(1)
#define PHY_RX_TRIGGER_2			BIT(2)
#define PHY_RX_TRIGGER_3			BIT(3)
#define DW_DSI2H_PRI_CAL_CTRL			0x1c8
#define	PHY_DESKEWCAL				BIT(8)
#define	PHY_ALTERNATECAL			BIT(9)
#define PHY_ULPS_ENTRY				BIT(12)
#define PHY_ULPS_EXIT				BIT(13)
#define PHY_BTA					BIT(16)
#define DW_DSI2H_PRI_ULPS_CTRL			0x1cc
/* DSI registers */
#define DW_DSI2H_DSI_GENERAL_CFG		0x200
#define DW_DSI2H_DSI_VCID_CFG			0x204
#define DW_DSI2H_DSI_SCRAMBLING_CFG		0x208
#define DW_DSI2H_DSI_VID_TX_CFG			0x20c
#define DW_DSI2H_DSI_MAX_RPS_CFG		0x210
#define DW_DSI2H_DSI_TEAR_EFFECT_CFG		0x214
#define DW_DSI2H_DSI_DS_TX_CFG			0x218
#define DW_DSI2H_CRI_TX_HDR			0x2c0
#define CMD_TX_MODE				BIT(24)
#define CMD_HDR_RD				BIT(28)
#define CMD_HDR_LONG				BIT(29)
#define DW_DSI2H_CRI_TX_PLD			0x2c4
#define DW_DSI2H_CRI_RX_HDR			0x2c8
#define RX_HDR_DATA_TYPE			MASK(5, 0)
#define RX_HDR_HEADER				MASK(7, 0)
#define RX_HDR_WC_LSB				MASK(15, 8)
#define RX_HDR_WC_MSB				MASK(23, 16)
#define DW_DSI2H_CRI_RX_PLD			0x2cc
#define DW_DSI2H_CRI_TX_CTRL			0x2d0
#define CRI_HOLD				BIT(0)
#define CRI_MODE				BIT(8)
/* IPI registers */
#define DW_DSI2H_IPI_COLOR_MAN_CFG		0x300
#define DW_DSI2H_IPI_VID_HSA_MAN_CFG		0x304
#define DW_DSI2H_IPI_VID_HSA_AUTO		0x308
#define DW_DSI2H_IPI_VID_HBP_MAN_CFG		0x30c
#define DW_DSI2H_IPI_VID_HBP_AUTO		0x310
#define DW_DSI2H_IPI_VID_HACT_MAN_CFG		0x314
#define DW_DSI2H_IPI_VID_HACT_AUTO		0x318
#define DW_DSI2H_IPI_VID_HLINE_MAN_CFG		0x31c
#define DW_DSI2H_IPI_VID_HLINE_AUTO		0x320
#define DW_DSI2H_IPI_VID_VSA_MAN_CFG		0x324
#define DW_DSI2H_IPI_VID_VSA_AUTO		0x328
#define DW_DSI2H_IPI_VID_VBP_MAN_CFG		0x32c
#define DW_DSI2H_IPI_VID_VBP_AUTO		0x330
#define DW_DSI2H_IPI_VID_VACT_MAN_CFG		0x334
#define DW_DSI2H_IPI_VID_VACT_AUTO		0x338
#define DW_DSI2H_IPI_VID_VFP_MAN_CFG		0x33c
#define DW_DSI2H_IPI_VID_VFP_AUTO		0x340
#define DW_DSI2H_IPI_PIX_PKT_CFG		0x344
#define DW_DSI2H_IPI_HIBERNATE_CFG		0x348
/* INT registers */
#define DW_DSI2H_INT_ST_PHY			0x420
#define DW_DSI2H_INT_MASK_PHY			0x460
#define DW_DSI2H_INT_FORCE_PHY			0x4a0
#define DW_DSI2H_INT_ST_TO			0x424
#define DW_DSI2H_INT_MASK_TO			0x464
#define DW_DSI2H_INT_FORCE_TO			0x4a4
#define DW_DSI2H_INT_ST_ACK			0x428
#define DW_DSI2H_INT_MASK_ACK			0x468
#define DW_DSI2H_INT_FORCE_ACK			0x4a8
#define DW_DSI2H_INT_ST_IPI			0x42c
#define DW_DSI2H_INT_MASK_IPI			0x46c
#define DW_DSI2H_INT_FORCE_IPI			0x4ac
#define DW_DSI2H_INT_ST_PRI			0x430
#define DW_DSI2H_INT_MASK_PRI			0x470
#define DW_DSI2H_INT_FORCE_PRI			0x4b0
#define DW_DSI2H_INT_ST_CRI			0x434
#define DW_DSI2H_INT_MASK_CRI			0x474
#define DW_DSI2H_INT_FORCE_CRI			0x4b4

#define DRM_FORMAT_RGB121212 fourcc_code('R', 'G', '1', '2')
#define DRM_FORMAT_YCC422_24BIT	fourcc_code('Y', '2', '0', '8')
#define DRM_FORMAT_YCC422_20BIT	fourcc_code('Y', '2', '1', '0')
#define DRM_FORMAT_YCC422_16BIT	fourcc_code('Y', '2', '1', '2')
#define DRM_FORMAT_YCC420_12BIT	fourcc_code('Y', '0', '1', '2')

#define DSI2H_DPHY_8_BITS 0
#define DSI2H_DPHY_16_BITS 1
#define DSI2H_DPHY_32_BITS 2

#define MIPI_FIFO_HDR_SIZE (32)
#define MIPI_FIFO_PLD_SIZE (104)
#define MIPI_FIFO_HDR_SIZE_ALMOST_FULL (24) // MIPI_FIFO_HDR_SIZE * 0.75
#define MIPI_FIFO_PLD_SIZE_ALMOST_FULL (75) // MIPI_FIFO_PLD_SIZE * 0.75

struct sscd_funcs;

enum pri_cal_request {
	PHY_DESKEW_CAL,
	PHY_ALTERNATE_CAL,
};

enum dsi2h_operation_mode {
	IDLE_MODE,
	AUTO_CALC_MODE,
	COMMAND_MODE,
	VIDEO_MODE,
	DATA_STREAM_MODE,
};

enum dsi2h_ipi_depth {
	D_565_BIT = 2,
	D_6_BIT,
	D_8_BIT = 5,
	D_10_BIT,
	D_12_BIT,
};

enum dsi2h_ipi_format {
	F_RGB,
	F_YCC_422,
	F_YCC_420 = 3,
	F_YCC_422_L,
	F_RGB_L,
	F_COMPRESSED = 0xB,
};

/**
 * enum dsi2h_rstn - recommended behavior as a result of dsi errors read
 *
 * These parallel @enum gs_dsi_err values, but are designed to be OR'd
 * together directly in relation to individual error bit reads.
 */
enum dsi2h_rstn {
	/** @NO_RSTN: no reset is recommended */
	NO_RSTN = 0,
	/** @SYS_RSTN: System reset needed */
	SYS_RSTN = BIT(GS_DSI_ERR_SYS_RSTN),
	/** @PHY_RSTN: PHY reset needed */
	PHY_RSTN = BIT(GS_DSI_ERR_PHY_RSTN),
	/** @IPI_RSTN: IPI reset needed */
	IPI_RSTN = BIT(GS_DSI_ERR_IPI_RSTN),
	/** @HARD_RSTN: Hard reset needed */
	HARD_RSTN = BIT(GS_DSI_ERR_HARD_RSTN),
};

struct dsi2h_color_format {
	enum dsi2h_ipi_format format;
	enum dsi2h_ipi_depth depth;
};

enum dsi_err_event_stats {
	/* error event in driver */
	ST_CRI_BUSY = 0,
	ST_PRI_BUSY,
	ST_ULPS_EXIT_ERR,
	ST_ULPS_ENTER_ERR,
	ST_DCS_READ_ACK_ERR,
	ST_DCS_READ_EXTRA_DATA,
	ST_DCS_READ_DATA_NOT_ARRIVED,
	/* Total Count */
	STATS_EVENT_COUNT,
};

struct dw_dsi2h_int_cntrs {
	u64	cntr_int_phy_l0_erresc;
	u64	cntr_int_phy_l0_errsyncesc;
	u64	cntr_int_phy_l0_errcontrol;
	u64	cntr_int_phy_l0_errcontentionlp0;
	u64	cntr_int_phy_l0_errcontentionlp1;
	u64	cntr_int_txhs_fifo_over;
	u64	cntr_int_txhs_fifo_under;

	u64	cntr_int_err_to_hstx;
	u64	cntr_int_err_to_hstxrdy;
	u64	cntr_int_err_to_lprx;
	u64	cntr_int_err_to_lptxrdy;
	u64	cntr_int_err_to_lptxtrig;
	u64	cntr_int_err_to_lptxulps;
	u64	cntr_int_err_to_bta;

	u64	cntr_int_err_ack_rpt_0;
	u64	cntr_int_err_ack_rpt_1;
	u64	cntr_int_err_ack_rpt_2;
	u64	cntr_int_err_ack_rpt_3;
	u64	cntr_int_err_ack_rpt_4;
	u64	cntr_int_err_ack_rpt_5;
	u64	cntr_int_err_ack_rpt_6;
	u64	cntr_int_err_ack_rpt_7;
	u64	cntr_int_err_ack_rpt_8;
	u64	cntr_int_err_ack_rpt_9;
	u64	cntr_int_err_ack_rpt_10;
	u64	cntr_int_err_ack_rpt_11;
	u64	cntr_int_err_ack_rpt_12;
	u64	cntr_int_err_ack_rpt_13;
	u64	cntr_int_err_ack_rpt_14;
	u64	cntr_int_err_ack_rpt_15;

	u64	cntr_int_err_display_cmd_time;
	u64	cntr_int_err_ipi_dtype;
	u64	cntr_int_err_vid_bandwidth;
	u64	cntr_int_err_ipi_cmd;
	u64	cntr_int_err_display_cmd_ovfl;
	u64	cntr_int_ipi_event_fifo_over;
	u64	cntr_int_ipi_event_fifo_under;
	u64	cntr_int_ipi_pixel_fifo_over;
	u64	cntr_int_ipi_pixel_fifo_under;

	u64	cntr_int_err_pri_tx_time;
	u64	cntr_int_err_pri_tx_cmd;

	u64	cntr_int_err_cri_cmd_time;
	u64	cntr_int_err_cri_dtype;
	u64	cntr_int_err_cri_vchannel;
	u64	cntr_int_err_cri_rx_length;
	u64	cntr_int_err_cri_ecc;
	u64	cntr_int_err_cri_ecc_fatal;
	u64	cntr_int_err_cri_crc;
	u64	cntr_int_cmd_rd_pld_fifo_over;
	u64	cntr_int_cmd_rd_pld_fifo_under;
	u64	cntr_int_cmd_wr_pld_fifo_over;
	u64	cntr_int_cmd_wr_pld_fifo_under;
	u64	cntr_int_cmd_wr_hdr_fifo_over;
	u64	cntr_int_cmd_wr_hdr_fifo_under;
};

enum dsi2h_host_state {
	DSI2H_STATE_SUSPEND,
	DSI2H_STATE_HS_EN,
	DSI2H_STATE_ULPS,
	DSI2H_STATE_PENDING_ULPS, /* HW state is HS_EN, but exist delay work to switch back ULPS */
};

struct dw_mipi_dsi2h_config {
	struct dsi2h_color_format color_format;
	u32 channel;
	u32 lanes;
	unsigned long mode_flags;
	union phy_configure_opts phy_opts;
};

struct dsi2h_bridge_state {
	struct drm_bridge_state base;
	bool is_seamless_modeset;
	bool is_active_modeset;
	struct dw_mipi_dsi2h_config cfg;
};

#define to_dsi2h_bridge_state(state) \
	container_of(state, struct dsi2h_bridge_state, base)

struct dw_mipi_dsi2h {
	struct drm_bridge bridge;
	struct mipi_dsi_host dsi_host;
	struct mipi_dsi_device *dsi_device;
	struct drm_bridge *panel_bridge;
	struct drm_display_mode mode;

	bool enabled;
	bool suppress_dsi_errors;
	enum dsi2h_host_state state;

	/* should dsi be fully powered off during panel self refresh, otherwise only enter ulps */
	bool disable_psr_ulps;
	/* indicates runtime pm was left on while entering psr, need to deal with extra vote */
	bool psr_rpm_on;

	struct phy *phy;
	/* this device has driver data vs_mipi_dsi2h */
	struct device *dev;
	/* this device has driver data dw_mipi_dsi2h */
	struct device *vdev;
	void __iomem *base;
	u32 reg_size;
	u64 phys_addr;
	struct regmap *regs;
	u32 hw_version;
	u32 hw_core_id;
	int irq;
	bool packet_stack_mode;
	int mipi_fifo_hdr_used;
	int mipi_fifo_pld_used;

	spinlock_t spinlock_dsi; /* lock to protect interrupts */
	struct mutex dsi2h_lock; /* protect state and data */

	/** @coredump_funcs: Sub-System Core Dump (SSCD) callback functions */
	struct sscd_funcs *coredump_funcs;

	const struct dw_mipi_dsi2h_plat_data *plat_data;
	struct kthread_worker dsi2h_worker;
	struct task_struct *thread;
	struct kthread_delayed_work ulps_dwork;
	u32 pm_ver;
	u32 *stat_data;

	u32 max_data_lanes;
	u32 is_cphy;
	u32 clk_type;
	u32 mode_ctrl;
	bool auto_calc_off;
	bool hs_link_enable;
	u32 ppi_width;
	u32 high_speed;
	u32 bta;
	u32 eotp;
	u32 tear_effect;
	u32 scrambling;
	u32 hib_type;
	u32 vid_mode_type;
	u32 vfp_hs_en;
	u32 vbp_hs_en;
	u32 vsa_hs_en;
	u32 hfp_hs_en;
	u32 hbp_hs_en;
	u32 hsa_hs_en;
	u32 datarate;
	u32 pending_datarate;
	bool datarate_changed;
	// Only values in allowed_hs_clks can be set through hs_clock sysfs node.
	struct gs_mipi_clks allowed_hs_clks;
	// Set true to enable setting values outside of allowed_hs_clks.
	bool force_set_datarate;
	// Whether enable dynamic hs_clk switch or not
	bool dynamic_hs_clk_en;
	u32 reconf;
	u32 lp2hs_time;
	u32 hs2lp_time;
	u32 ulps_wakeup_time;

	struct dw_mipi_dsi2h_config cfg;
	u32 sys_clk;
	u32 ipi_clk;
	u32 phy_hstx_clk;
	u32 lptx_clk;

	pid_t trace_pid;

	/** @rstn: Bitmask of reset behavior recommended */
	u64 rstn;

	const struct regmap_access_table *reg_table;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *debugfs;
	struct dw_debugfs_hwv *debugfs_hwv;
	u32 reg_dump_offset;
	u32 reg_dump_size;
#endif /* CONFIG_DEBUG_FS */
	struct dw_dsi2h_int_cntrs int_cntrs;

	struct regmap_field	*field_core_id;
	struct regmap_field	*field_ver_number;
	struct regmap_field	*field_type_num;
	struct regmap_field	*field_pkg_num;
	struct regmap_field	*field_type_enum;
	struct regmap_field	*field_pwr_up;
	struct regmap_field	*field_mode_ctrl;
	struct regmap_field	*field_mode_status;
	struct regmap_field	*field_core_busy;
	struct regmap_field	*field_core_fifos_not_empty;
	struct regmap_field	*field_ipi_busy;
	struct regmap_field	*field_ipi_fifos_not_empty;
	struct regmap_field	*field_cri_busy;
	struct regmap_field	*field_cri_wr_fifos_not_empty;
	struct regmap_field	*field_cri_rd_data_avail;
	struct regmap_field	*field_pri_busy;
	struct regmap_field	*field_pri_tx_fifos_not_empty;
	struct regmap_field	*field_pri_rx_data_avail;
	struct regmap_field	*field_manual_mode_en;
	struct regmap_field	*field_fsm_selector;
	struct regmap_field	*field_current_state;
	struct regmap_field	*field_stuck;
	struct regmap_field	*field_previous_state;
	struct regmap_field	*field_current_state_cnt;
	struct regmap_field	*field_fsm_manual_init;
	struct regmap_field	*field_fifo_selector;
	struct regmap_field	*field_empty;
	struct regmap_field	*field_almost_empty;
	struct regmap_field	*field_half_full;
	struct regmap_field	*field_almost_full;
	struct regmap_field	*field_full;
	struct regmap_field	*field_current_word_count;
	struct regmap_field	*field_fifo_manual_init;
	struct regmap_field	*field_to_hstx_value;
	struct regmap_field	*field_to_hstxrdy_value;
	struct regmap_field	*field_to_lprx_value;
	struct regmap_field	*field_to_lptxrdy_value;
	struct regmap_field	*field_to_lptxtrig_value;
	struct regmap_field	*field_to_lptxulps_value;
	struct regmap_field	*field_to_bta_value;
	struct regmap_field	*field_phy_type;
	struct regmap_field	*field_phy_lanes;
	struct regmap_field	*field_ppi_width;
	struct regmap_field	*field_hs_transferen_en;
	struct regmap_field	*field_clk_type;
	struct regmap_field	*field_phy_lptx_clk_div;
	struct regmap_field	*field_phy_direction;
	struct regmap_field	*field_phy_clk_stopstate;
	struct regmap_field	*field_phy_l0_stopstate;
	struct regmap_field	*field_phy_l1_stopstate;
	struct regmap_field	*field_phy_l2_stopstate;
	struct regmap_field	*field_phy_l3_stopstate;
	struct regmap_field	*field_phy_clk_ulpsactivenot;
	struct regmap_field	*field_phy_l0_ulpsactivenot;
	struct regmap_field	*field_phy_l1_ulpsactivenot;
	struct regmap_field	*field_phy_l2_ulpsactivenot;
	struct regmap_field	*field_phy_l3_ulpsactivenot;
	struct regmap_field	*field_phy_lp2hs_time;
	struct regmap_field	*field_phy_lp2hs_time_auto;
	struct regmap_field	*field_phy_hs2lp_time;
	struct regmap_field	*field_phy_hs2lp_time_auto;
	struct regmap_field	*field_phy_max_rd_time;
	struct regmap_field	*field_phy_max_rd_time_auto;
	struct regmap_field	*field_phy_esc_cmd_time;
	struct regmap_field	*field_phy_esc_cmd_time_auto;
	struct regmap_field	*field_phy_esc_byte_time;
	struct regmap_field	*field_phy_esc_byte_time_auto;
	struct regmap_field	*field_phy_ipi_ratio;
	struct regmap_field	*field_phy_ipi_ratio_auto;
	struct regmap_field	*field_phy_sys_ratio;
	struct regmap_field	*field_phy_sys_ratio_auto;
	struct regmap_field	*field_phy_tx_trigger_0;
	struct regmap_field	*field_phy_tx_trigger_1;
	struct regmap_field	*field_phy_tx_trigger_2;
	struct regmap_field	*field_phy_tx_trigger_3;
	struct regmap_field	*field_phy_deskewcal;
	struct regmap_field	*field_phy_alternatecal;
	struct regmap_field	*field_phy_ulps_entry;
	struct regmap_field	*field_phy_ulps_exit;
	struct regmap_field	*field_phy_bta;
	struct regmap_field	*field_phy_cal_time;
	struct regmap_field	*field_phy_ulps_data_lanes;
	struct regmap_field	*field_phy_ulps_clk_lane;
	struct regmap_field	*field_phy_wakeup_time;
	struct regmap_field	*field_eotp_tx_en;
	struct regmap_field	*field_bta_en;
	struct regmap_field	*field_tx_vcid;
	struct regmap_field	*field_scrambling_en;
	struct regmap_field	*field_scrambling_seed;
	struct regmap_field	*field_vid_mode_type;
	struct regmap_field	*field_blk_hsa_hs_en;
	struct regmap_field	*field_blk_hbp_hs_en;
	struct regmap_field	*field_blk_hfp_hs_en;
	struct regmap_field	*field_blk_vsa_hs_en;
	struct regmap_field	*field_blk_vbp_hs_en;
	struct regmap_field	*field_blk_vfp_hs_en;
	struct regmap_field	*field_lpdt_display_cmd_en;
	struct regmap_field	*field_max_rt_pkt_sz;
	struct regmap_field	*field_auto_tear_bta_disable;
	struct regmap_field	*field_te_type_hw;
	struct regmap_field	*field_set_tear_on_args_hw;
	struct regmap_field	*field_set_tear_scanline_args_hw;
	struct regmap_field	*field_ipi_format;
	struct regmap_field	*field_ipi_depth;
	struct regmap_field	*field_vid_hsa_time;
	struct regmap_field	*field_vid_hsa_time_auto;
	struct regmap_field	*field_vid_hbp_time;
	struct regmap_field	*field_vid_hbp_time_auto;
	struct regmap_field	*field_vid_hact_time;
	struct regmap_field	*field_vid_hact_time_auto;
	struct regmap_field	*field_vid_hline_time;
	struct regmap_field	*field_vid_hline_time_auto;
	struct regmap_field	*field_vid_vsa_lines;
	struct regmap_field	*field_vid_vsa_lines_auto;
	struct regmap_field	*field_vid_vbp_lines;
	struct regmap_field	*field_vid_vbp_lines_auto;
	struct regmap_field	*field_vid_vact_lines;
	struct regmap_field	*field_vid_vact_lines_auto;
	struct regmap_field	*field_vid_vfp_lines;
	struct regmap_field	*field_vid_vfp_lines_auto;
	struct regmap_field	*field_max_pix_pkt;
	struct regmap_field	*field_hib_type;
	struct regmap_field	*field_hib_ulps_wakeup_time;
	struct regmap_field	*field_mask_phy_l0_erresc;
	struct regmap_field	*field_mask_phy_l0_errsyncesc;
	struct regmap_field	*field_mask_phy_l0_errcontrol;
	struct regmap_field	*field_mask_phy_l0_errcontentionlp0;
	struct regmap_field	*field_mask_phy_l0_errcontentionlp1;
	struct regmap_field	*field_mask_txhs_fifo_over;
	struct regmap_field	*field_mask_txhs_fifo_under;
	struct regmap_field	*field_force_phy_l0_erresc;
	struct regmap_field	*field_force_phy_l0_errsyncesc;
	struct regmap_field	*field_force_phy_l0_errcontrol;
	struct regmap_field	*field_force_phy_l0_errcontentionlp0;
	struct regmap_field	*field_force_phy_l0_errcontentionlp1;
	struct regmap_field	*field_force_txhs_fifo_over;
	struct regmap_field	*field_force_txhs_fifo_under;
	struct regmap_field	*field_mask_err_to_hstx;
	struct regmap_field	*field_mask_err_to_hstxrdy;
	struct regmap_field	*field_mask_err_to_lprx;
	struct regmap_field	*field_mask_err_to_lptxrdy;
	struct regmap_field	*field_mask_err_to_lptxtrig;
	struct regmap_field	*field_mask_err_to_lptxulps;
	struct regmap_field	*field_mask_err_to_bta;
	struct regmap_field	*field_force_err_to_hstx;
	struct regmap_field	*field_force_err_to_hstxrdy;
	struct regmap_field	*field_force_err_to_lprx;
	struct regmap_field	*field_force_err_to_lptxrdy;
	struct regmap_field	*field_force_err_to_lptxtrig;
	struct regmap_field	*field_force_err_to_lptxulps;
	struct regmap_field	*field_force_err_to_bta;
	struct regmap_field	*field_mask_err_ack_rpt_0;
	struct regmap_field	*field_mask_err_ack_rpt_1;
	struct regmap_field	*field_mask_err_ack_rpt_2;
	struct regmap_field	*field_mask_err_ack_rpt_3;
	struct regmap_field	*field_mask_err_ack_rpt_4;
	struct regmap_field	*field_mask_err_ack_rpt_5;
	struct regmap_field	*field_mask_err_ack_rpt_6;
	struct regmap_field	*field_mask_err_ack_rpt_7;
	struct regmap_field	*field_mask_err_ack_rpt_8;
	struct regmap_field	*field_mask_err_ack_rpt_9;
	struct regmap_field	*field_mask_err_ack_rpt_10;
	struct regmap_field	*field_mask_err_ack_rpt_11;
	struct regmap_field	*field_mask_err_ack_rpt_12;
	struct regmap_field	*field_mask_err_ack_rpt_13;
	struct regmap_field	*field_mask_err_ack_rpt_14;
	struct regmap_field	*field_mask_err_ack_rpt_15;
	struct regmap_field	*field_force_err_ack_rpt_0;
	struct regmap_field	*field_force_err_ack_rpt_1;
	struct regmap_field	*field_force_err_ack_rpt_2;
	struct regmap_field	*field_force_err_ack_rpt_3;
	struct regmap_field	*field_force_err_ack_rpt_4;
	struct regmap_field	*field_force_err_ack_rpt_5;
	struct regmap_field	*field_force_err_ack_rpt_6;
	struct regmap_field	*field_force_err_ack_rpt_7;
	struct regmap_field	*field_force_err_ack_rpt_8;
	struct regmap_field	*field_force_err_ack_rpt_9;
	struct regmap_field	*field_force_err_ack_rpt_10;
	struct regmap_field	*field_force_err_ack_rpt_11;
	struct regmap_field	*field_force_err_ack_rpt_12;
	struct regmap_field	*field_force_err_ack_rpt_13;
	struct regmap_field	*field_force_err_ack_rpt_14;
	struct regmap_field	*field_force_err_ack_rpt_15;
	struct regmap_field	*field_mask_err_display_cmd_time;
	struct regmap_field	*field_mask_err_ipi_dtype;
	struct regmap_field	*field_mask_err_vid_bandwidth;
	struct regmap_field	*field_mask_err_ipi_cmd;
	struct regmap_field	*field_mask_err_display_cmd_ovfl;
	struct regmap_field	*field_mask_ipi_event_fifo_over;
	struct regmap_field	*field_mask_ipi_event_fifo_under;
	struct regmap_field	*field_mask_ipi_pixel_fifo_over;
	struct regmap_field	*field_mask_ipi_pixel_fifo_under;
	struct regmap_field	*field_force_err_display_cmd_time;
	struct regmap_field	*field_force_err_ipi_dtype;
	struct regmap_field	*field_force_err_vid_bandwidth;
	struct regmap_field	*field_force_err_ipi_cmd;
	struct regmap_field	*field_force_err_display_cmd_ovfl;
	struct regmap_field	*field_force_ipi_event_fifo_over;
	struct regmap_field	*field_force_ipi_event_fifo_under;
	struct regmap_field	*field_force_ipi_pixel_fifo_over;
	struct regmap_field	*field_force_ipi_pixel_fifo_under;
	struct regmap_field	*field_mask_err_pri_tx_time;
	struct regmap_field	*field_mask_err_pri_tx_cmd;
	struct regmap_field	*field_force_err_pri_tx_time;
	struct regmap_field	*field_force_err_pri_tx_cmd;
	struct regmap_field	*field_mask_err_cri_cmd_time;
	struct regmap_field	*field_mask_err_cri_dtype;
	struct regmap_field	*field_mask_err_cri_vchannel;
	struct regmap_field	*field_mask_err_cri_rx_length;
	struct regmap_field	*field_mask_err_cri_ecc;
	struct regmap_field	*field_mask_err_cri_ecc_fatal;
	struct regmap_field	*field_mask_err_cri_crc;
	struct regmap_field	*field_mask_cmd_rd_pld_fifo_over;
	struct regmap_field	*field_mask_cmd_rd_pld_fifo_under;
	struct regmap_field	*field_mask_cmd_wr_pld_fifo_over;
	struct regmap_field	*field_mask_cmd_wr_pld_fifo_under;
	struct regmap_field	*field_mask_cmd_wr_hdr_fifo_over;
	struct regmap_field	*field_mask_cmd_wr_hdr_fifo_under;
	struct regmap_field	*field_force_err_cri_cmd_time;
	struct regmap_field	*field_force_err_cri_dtype;
	struct regmap_field	*field_force_err_cri_vchannel;
	struct regmap_field	*field_force_err_cri_rx_length;
	struct regmap_field	*field_force_err_cri_ecc;
	struct regmap_field	*field_force_err_cri_ecc_fatal;
	struct regmap_field	*field_force_err_cri_crc;
	struct regmap_field	*field_force_cmd_rd_pld_fifo_over;
	struct regmap_field	*field_force_cmd_rd_pld_fifo_under;
	struct regmap_field	*field_force_cmd_wr_pld_fifo_over;
	struct regmap_field	*field_force_cmd_wr_pld_fifo_under;
	struct regmap_field	*field_force_cmd_wr_hdr_fifo_over;
	struct regmap_field	*field_force_cmd_wr_hdr_fifo_under;
};

struct dw_mipi_dsi2h_variant {
	struct reg_field	cfg_core_id;
	struct reg_field	cfg_ver_number;
	struct reg_field	cfg_type_num;
	struct reg_field	cfg_pkg_num;
	struct reg_field	cfg_type_enum;
	struct reg_field	cfg_pwr_up;
	struct reg_field	cfg_mode_ctrl;
	struct reg_field	cfg_mode_status;
	struct reg_field	cfg_core_busy;
	struct reg_field	cfg_core_fifos_not_empty;
	struct reg_field	cfg_ipi_busy;
	struct reg_field	cfg_ipi_fifos_not_empty;
	struct reg_field	cfg_cri_busy;
	struct reg_field	cfg_cri_wr_fifos_not_empty;
	struct reg_field	cfg_cri_rd_data_avail;
	struct reg_field	cfg_pri_busy;
	struct reg_field	cfg_pri_tx_fifos_not_empty;
	struct reg_field	cfg_pri_rx_data_avail;
	struct reg_field	cfg_manual_mode_en;
	struct reg_field	cfg_fsm_selector;
	struct reg_field	cfg_current_state;
	struct reg_field	cfg_stuck;
	struct reg_field	cfg_previous_state;
	struct reg_field	cfg_current_state_cnt;
	struct reg_field	cfg_fsm_manual_init;
	struct reg_field	cfg_fifo_selector;
	struct reg_field	cfg_empty;
	struct reg_field	cfg_almost_empty;
	struct reg_field	cfg_half_full;
	struct reg_field	cfg_almost_full;
	struct reg_field	cfg_full;
	struct reg_field	cfg_current_word_count;
	struct reg_field	cfg_fifo_manual_init;
	struct reg_field	cfg_to_hstx_value;
	struct reg_field	cfg_to_hstxrdy_value;
	struct reg_field	cfg_to_lprx_value;
	struct reg_field	cfg_to_lptxrdy_value;
	struct reg_field	cfg_to_lptxtrig_value;
	struct reg_field	cfg_to_lptxulps_value;
	struct reg_field	cfg_to_bta_value;
	struct reg_field	cfg_phy_type;
	struct reg_field	cfg_phy_lanes;
	struct reg_field	cfg_ppi_width;
	struct reg_field	cfg_hs_transferen_en;
	struct reg_field	cfg_clk_type;
	struct reg_field	cfg_phy_lptx_clk_div;
	struct reg_field	cfg_phy_direction;
	struct reg_field	cfg_phy_clk_stopstate;
	struct reg_field	cfg_phy_l0_stopstate;
	struct reg_field	cfg_phy_l1_stopstate;
	struct reg_field	cfg_phy_l2_stopstate;
	struct reg_field	cfg_phy_l3_stopstate;
	struct reg_field	cfg_phy_clk_ulpsactivenot;
	struct reg_field	cfg_phy_l0_ulpsactivenot;
	struct reg_field	cfg_phy_l1_ulpsactivenot;
	struct reg_field	cfg_phy_l2_ulpsactivenot;
	struct reg_field	cfg_phy_l3_ulpsactivenot;
	struct reg_field	cfg_phy_lp2hs_time;
	struct reg_field	cfg_phy_lp2hs_time_auto;
	struct reg_field	cfg_phy_hs2lp_time;
	struct reg_field	cfg_phy_hs2lp_time_auto;
	struct reg_field	cfg_phy_max_rd_time;
	struct reg_field	cfg_phy_max_rd_time_auto;
	struct reg_field	cfg_phy_esc_cmd_time;
	struct reg_field	cfg_phy_esc_cmd_time_auto;
	struct reg_field	cfg_phy_esc_byte_time;
	struct reg_field	cfg_phy_esc_byte_time_auto;
	struct reg_field	cfg_phy_ipi_ratio;
	struct reg_field	cfg_phy_ipi_ratio_auto;
	struct reg_field	cfg_phy_sys_ratio;
	struct reg_field	cfg_phy_sys_ratio_auto;
	struct reg_field	cfg_phy_tx_trigger_0;
	struct reg_field	cfg_phy_tx_trigger_1;
	struct reg_field	cfg_phy_tx_trigger_2;
	struct reg_field	cfg_phy_tx_trigger_3;
	struct reg_field	cfg_phy_deskewcal;
	struct reg_field	cfg_phy_alternatecal;
	struct reg_field	cfg_phy_ulps_entry;
	struct reg_field	cfg_phy_ulps_exit;
	struct reg_field	cfg_phy_bta;
	struct reg_field	cfg_phy_cal_time;
	struct reg_field	cfg_phy_ulps_data_lanes;
	struct reg_field	cfg_phy_ulps_clk_lane;
	struct reg_field	cfg_phy_wakeup_time;
	struct reg_field	cfg_eotp_tx_en;
	struct reg_field	cfg_bta_en;
	struct reg_field	cfg_tx_vcid;
	struct reg_field	cfg_scrambling_en;
	struct reg_field	cfg_scrambling_seed;
	struct reg_field	cfg_vid_mode_type;
	struct reg_field	cfg_blk_hsa_hs_en;
	struct reg_field	cfg_blk_hbp_hs_en;
	struct reg_field	cfg_blk_hfp_hs_en;
	struct reg_field	cfg_blk_vsa_hs_en;
	struct reg_field	cfg_blk_vbp_hs_en;
	struct reg_field	cfg_blk_vfp_hs_en;
	struct reg_field	cfg_lpdt_display_cmd_en;
	struct reg_field	cfg_max_rt_pkt_sz;
	struct reg_field	cfg_auto_tear_bta_disable;
	struct reg_field	cfg_te_type_hw;
	struct reg_field	cfg_set_tear_on_args_hw;
	struct reg_field	cfg_set_tear_scanline_args_hw;
	struct reg_field	cfg_ipi_format;
	struct reg_field	cfg_ipi_depth;
	struct reg_field	cfg_vid_hsa_time;
	struct reg_field	cfg_vid_hsa_time_auto;
	struct reg_field	cfg_vid_hbp_time;
	struct reg_field	cfg_vid_hbp_time_auto;
	struct reg_field	cfg_vid_hact_time;
	struct reg_field	cfg_vid_hact_time_auto;
	struct reg_field	cfg_vid_hline_time;
	struct reg_field	cfg_vid_hline_time_auto;
	struct reg_field	cfg_vid_vsa_lines;
	struct reg_field	cfg_vid_vsa_lines_auto;
	struct reg_field	cfg_vid_vbp_lines;
	struct reg_field	cfg_vid_vbp_lines_auto;
	struct reg_field	cfg_vid_vact_lines;
	struct reg_field	cfg_vid_vact_lines_auto;
	struct reg_field	cfg_vid_vfp_lines;
	struct reg_field	cfg_vid_vfp_lines_auto;
	struct reg_field	cfg_max_pix_pkt;
	struct reg_field	cfg_hib_type;
	struct reg_field	cfg_hib_ulps_wakeup_time;
	struct reg_field	cfg_mask_phy_l0_erresc;
	struct reg_field	cfg_mask_phy_l0_errsyncesc;
	struct reg_field	cfg_mask_phy_l0_errcontrol;
	struct reg_field	cfg_mask_phy_l0_errcontentionlp0;
	struct reg_field	cfg_mask_phy_l0_errcontentionlp1;
	struct reg_field	cfg_mask_txhs_fifo_over;
	struct reg_field	cfg_mask_txhs_fifo_under;
	struct reg_field	cfg_force_phy_l0_erresc;
	struct reg_field	cfg_force_phy_l0_errsyncesc;
	struct reg_field	cfg_force_phy_l0_errcontrol;
	struct reg_field	cfg_force_phy_l0_errcontentionlp0;
	struct reg_field	cfg_force_phy_l0_errcontentionlp1;
	struct reg_field	cfg_force_txhs_fifo_over;
	struct reg_field	cfg_force_txhs_fifo_under;
	struct reg_field	cfg_mask_err_to_hstx;
	struct reg_field	cfg_mask_err_to_hstxrdy;
	struct reg_field	cfg_mask_err_to_lprx;
	struct reg_field	cfg_mask_err_to_lptxrdy;
	struct reg_field	cfg_mask_err_to_lptxtrig;
	struct reg_field	cfg_mask_err_to_lptxulps;
	struct reg_field	cfg_mask_err_to_bta;
	struct reg_field	cfg_force_err_to_hstx;
	struct reg_field	cfg_force_err_to_hstxrdy;
	struct reg_field	cfg_force_err_to_lprx;
	struct reg_field	cfg_force_err_to_lptxrdy;
	struct reg_field	cfg_force_err_to_lptxtrig;
	struct reg_field	cfg_force_err_to_lptxulps;
	struct reg_field	cfg_force_err_to_bta;
	struct reg_field	cfg_mask_err_ack_rpt_0;
	struct reg_field	cfg_mask_err_ack_rpt_1;
	struct reg_field	cfg_mask_err_ack_rpt_2;
	struct reg_field	cfg_mask_err_ack_rpt_3;
	struct reg_field	cfg_mask_err_ack_rpt_4;
	struct reg_field	cfg_mask_err_ack_rpt_5;
	struct reg_field	cfg_mask_err_ack_rpt_6;
	struct reg_field	cfg_mask_err_ack_rpt_7;
	struct reg_field	cfg_mask_err_ack_rpt_8;
	struct reg_field	cfg_mask_err_ack_rpt_9;
	struct reg_field	cfg_mask_err_ack_rpt_10;
	struct reg_field	cfg_mask_err_ack_rpt_11;
	struct reg_field	cfg_mask_err_ack_rpt_12;
	struct reg_field	cfg_mask_err_ack_rpt_13;
	struct reg_field	cfg_mask_err_ack_rpt_14;
	struct reg_field	cfg_mask_err_ack_rpt_15;
	struct reg_field	cfg_force_err_ack_rpt_0;
	struct reg_field	cfg_force_err_ack_rpt_1;
	struct reg_field	cfg_force_err_ack_rpt_2;
	struct reg_field	cfg_force_err_ack_rpt_3;
	struct reg_field	cfg_force_err_ack_rpt_4;
	struct reg_field	cfg_force_err_ack_rpt_5;
	struct reg_field	cfg_force_err_ack_rpt_6;
	struct reg_field	cfg_force_err_ack_rpt_7;
	struct reg_field	cfg_force_err_ack_rpt_8;
	struct reg_field	cfg_force_err_ack_rpt_9;
	struct reg_field	cfg_force_err_ack_rpt_10;
	struct reg_field	cfg_force_err_ack_rpt_11;
	struct reg_field	cfg_force_err_ack_rpt_12;
	struct reg_field	cfg_force_err_ack_rpt_13;
	struct reg_field	cfg_force_err_ack_rpt_14;
	struct reg_field	cfg_force_err_ack_rpt_15;
	struct reg_field	cfg_mask_err_display_cmd_time;
	struct reg_field	cfg_mask_err_ipi_dtype;
	struct reg_field	cfg_mask_err_vid_bandwidth;
	struct reg_field	cfg_mask_err_ipi_cmd;
	struct reg_field	cfg_mask_err_display_cmd_ovfl;
	struct reg_field	cfg_mask_ipi_event_fifo_over;
	struct reg_field	cfg_mask_ipi_event_fifo_under;
	struct reg_field	cfg_mask_ipi_pixel_fifo_over;
	struct reg_field	cfg_mask_ipi_pixel_fifo_under;
	struct reg_field	cfg_force_err_display_cmd_time;
	struct reg_field	cfg_force_err_ipi_dtype;
	struct reg_field	cfg_force_err_vid_bandwidth;
	struct reg_field	cfg_force_err_ipi_cmd;
	struct reg_field	cfg_force_err_display_cmd_ovfl;
	struct reg_field	cfg_force_ipi_event_fifo_over;
	struct reg_field	cfg_force_ipi_event_fifo_under;
	struct reg_field	cfg_force_ipi_pixel_fifo_over;
	struct reg_field	cfg_force_ipi_pixel_fifo_under;
	struct reg_field	cfg_mask_err_pri_tx_time;
	struct reg_field	cfg_mask_err_pri_tx_cmd;
	struct reg_field	cfg_force_err_pri_tx_time;
	struct reg_field	cfg_force_err_pri_tx_cmd;
	struct reg_field	cfg_mask_err_cri_cmd_time;
	struct reg_field	cfg_mask_err_cri_dtype;
	struct reg_field	cfg_mask_err_cri_vchannel;
	struct reg_field	cfg_mask_err_cri_rx_length;
	struct reg_field	cfg_mask_err_cri_ecc;
	struct reg_field	cfg_mask_err_cri_ecc_fatal;
	struct reg_field	cfg_mask_err_cri_crc;
	struct reg_field	cfg_mask_cmd_rd_pld_fifo_over;
	struct reg_field	cfg_mask_cmd_rd_pld_fifo_under;
	struct reg_field	cfg_mask_cmd_wr_pld_fifo_over;
	struct reg_field	cfg_mask_cmd_wr_pld_fifo_under;
	struct reg_field	cfg_mask_cmd_wr_hdr_fifo_over;
	struct reg_field	cfg_mask_cmd_wr_hdr_fifo_under;
	struct reg_field	cfg_force_err_cri_cmd_time;
	struct reg_field	cfg_force_err_cri_dtype;
	struct reg_field	cfg_force_err_cri_vchannel;
	struct reg_field	cfg_force_err_cri_rx_length;
	struct reg_field	cfg_force_err_cri_ecc;
	struct reg_field	cfg_force_err_cri_ecc_fatal;
	struct reg_field	cfg_force_err_cri_crc;
	struct reg_field	cfg_force_cmd_rd_pld_fifo_over;
	struct reg_field	cfg_force_cmd_rd_pld_fifo_under;
	struct reg_field	cfg_force_cmd_wr_pld_fifo_over;
	struct reg_field	cfg_force_cmd_wr_pld_fifo_under;
	struct reg_field	cfg_force_cmd_wr_hdr_fifo_over;
	struct reg_field	cfg_force_cmd_wr_hdr_fifo_under;
};

struct dw_debugfs_hwv {
	struct dw_mipi_dsi2h	*dsi2h;
	const char		*name;
	u32			reg;
	u32			mask;
	u32			offset;
	void (*write)(void *data, u64 val);
};

void dw_dsi2h_import_device(struct dw_mipi_dsi2h *dsi2h,
			    struct mipi_dsi_device *d_device);
#if IS_ENABLED(CONFIG_DEBUG_FS)
/* Functions that need to be exposed for HWV*/
int dw_mipi_dsi2h_disable(struct dw_mipi_dsi2h *dsi2h);

void dw_create_debugfs_hwv_files(struct dw_mipi_dsi2h *dsi2h);
u32 dw_ctrl_read_dbgfs(struct dw_mipi_dsi2h *dsi2h, int offset);
struct drm_display_mode *dw_dsi2h_export_display(struct dw_mipi_dsi2h *dsi2h);
struct mipi_dsi_device *dw_dsi2h_export_device(struct dw_mipi_dsi2h *dsi2h);
union phy_configure_opts
*dw_dsi2h_export_phy_opts(struct dw_mipi_dsi2h *dsi2h);
int dw_dsi2h_reconfigure(struct dw_mipi_dsi2h *dsi2h);
void dw_dsi2h_update(struct dw_mipi_dsi2h *dsi2h);
#endif /* CONFIG_DEBUG_FS */

int dw_mipi_dsi2h_sysfs_init(struct dw_mipi_dsi2h *dsi2h);
void dw_mipi_dsi2h_sysfs_remove(struct dw_mipi_dsi2h *dsi2h);

#endif /* DW_DSI2H_H */
