// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020-2021 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare MIPI DSI2 Host driver
 *
 * Author: Marcelo Borges <marcelob@synopsys.com>
 * Author: Pedro Correia <correia@synopsys.com>
 * Author: Nuno Cardoso <cardoso@synopsys.com>
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/debugfs.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <video/mipi_display.h>
#include <video/videomode.h>

/* internal */
#include <drm/bridge/dw_mipi_dsi2h.h>
#include <drm/phy/dw_mipi_cdphy_op.h>
#include <drm/sscd/gs_sscd_dpu.h>
/* kernel common */
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#include <gs_drm/gs_drm_connector.h>
#include <gs_drm/gs_fault_event.h>
#include <gs_drm/gs_reg_dump.h>
#include <gs_drm/gs_sscd.h>
#include <trace/dpu_trace.h>

#include "dw-mipi-dsi2h.h"
#include "dw-mipi-dsi2h-sscd.h"
#include "dw-mipi-dsi2h-regmap-limits.h"
#include "gs_panel/dcs_helper.h"

#define DSI2H_AUTOSUPEND_DELAY_MS 20
/*
 * Due to ULPS works need to be done before module disable,
 * ULPS delay time must be less than autosuspend delay time.
 */
#define DSI2H_ULPS_DWORK_MS 10

#define VERSION_01_00 0x100
#define VERSION_01_10 0x110
#define CORE_ID 0x3130302A

#define DW_PRI_TRIGGER_REQUEST		1
#define DW_PRI_DESKEW_CAL		2
#define DW_PRI_ALTERNATE_CAL		3
#define DW_PRI_ULPS_ENTRY_REQUEST	4
#define DW_PRI_ULPS_EXIT_REQUEST	5
#define DW_PRI_BTA_REQUEST		6
#define DW_PRI_TRIGGERS_READ		7

#define DW_DSI2H_CRI_BUSY_INTERVAL_US 50
#define DW_DSI2H_CRI_BUSY_TIMEOUT_US 25000
#define DW_DSI2H_PRI_BUSY_INTERVAL_US 50
#define DW_DSI2H_PRI_BUSY_TIMEOUT_US 25000
#define DW_DSI2H_PRI_RECEIVE_INTERVAL_US 50
#define DW_DSI2H_PRI_RECEIVE_TIMEOUT_US 1000
#define DW_DSI2H_REQUESTED_MODE_INTERVAL_US 50
#define DW_DSI2H_REQUESTED_MODE_TIMEOUT_US 1000
#define DW_DSI2H_AUTO_CALC_INTERVAL_US 50
#define DW_DSI2H_AUTO_CALC_TIMEOUT_US (500 * 1000)
#define DW_DSI2H_CRI_DATA_AVAIL_INTERVAL_US 50
#define DW_DSI2H_CRI_DATA_AVAIL_TIMEOUT_US 50000
#define DW_DSI2H_CORE_STATUS_INTERVAL_US 500
#define DW_DSI2H_CORE_STATUS_TIMEOUT_US 25000

#define INIT_FIELD(f) INIT_FIELD_CFG(field_##f, cfg_##f)
#define INIT_FIELD_CFG(f, conf) ({						\
		dsi2h->f = devm_regmap_field_alloc(dsi2h->dev, dsi2h->regs,	\
							variant->conf);		\
		if (IS_ERR(dsi2h->f)) {						\
			dev_err(dsi2h->dev, "Failed alloc regmap field"#f "\n");\
			return PTR_ERR(dsi2h->f);				\
		}})

#define INT_BIT_CHECK(_dsi2h, _level, _val, _int, _bit, _rstn, _err_str) \
	INT_BIT_CHECK_CFG(_dsi2h, _level, _val, dsi2h->int_cntrs.cntr_##_int, _bit, _rstn, _err_str)

#define INT_BIT_CHECK_CFG(_dsi2h, _level, _val, _int_ctr, _bit, _rstn, _err_str) \
	do {                                                                     \
		if ((_val) & BIT(_bit)) {                                        \
			dev_##_level(dev, _err_str);                             \
			INC_INT_CNT(_int_ctr);                                   \
			(_dsi2h)->rstn |= (_rstn);                               \
		}                                                                \
	} while (0)

#define INC_INT_CNT(int_cntr) (++(int_cntr))

static u32 pm_runtime_suspend_version = 1;
/*
 * version 0 : original flow.
 * version 1 : override MIPI output to LP00 upon pm_runtime suspend,
 *	       override MIPI output to generate ULPS exit sequence during initialization.
 */
module_param(pm_runtime_suspend_version, uint, 0644);
MODULE_PARM_DESC(pm_runtime_suspend_version,
		"dsi2h pm_runtime_suspend control flow version");

static const struct regmap_config dw_mipi_dsi2h_regmap_cfg = {
	.name = "dw-mipi-dsi2h",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
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

static inline u32 dw_dsi2h_read_reg(struct regmap *regm, u32 reg)
{
	u32 val = 0;

	regmap_read(regm, reg, &val);

	return val;
}

static inline u32 dw_dsi2h_read_field(struct regmap_field *reg_field)
{
	u32 val = 0;

	regmap_field_read(reg_field, &val);

	return val;
}

static inline void dw_dsi2h_write_reg(struct regmap *regm, u32 reg, u32 val)
{
	regmap_write(regm, reg, val);
}

static inline void dw_dsi2h_write_field(struct regmap_field *reg_field, u32 val)
{
	regmap_field_write(reg_field, val);
}

static const struct dw_mipi_dsi2h_variant dw_mipi_dsi2h_lca01_layout = {
	.cfg_core_id = REG_FIELD(DW_DSI2H_CORE_ID, 0, 31),
	.cfg_ver_number = REG_FIELD(DW_DSI2H_VERSION, 0, 15),
	.cfg_type_num = REG_FIELD(DW_DSI2H_VERSION, 16, 23),
	.cfg_pkg_num = REG_FIELD(DW_DSI2H_VERSION, 24, 27),
	.cfg_type_enum = REG_FIELD(DW_DSI2H_VERSION, 28, 31),
	.cfg_pwr_up = REG_FIELD(DW_DSI2H_PWR_UP, 0, 0),
	.cfg_mode_ctrl = REG_FIELD(DW_DSI2H_MODE_CTRL, 0, 2),
	.cfg_mode_status = REG_FIELD(DW_DSI2H_MODE_STATUS, 0, 2),
	.cfg_core_busy = REG_FIELD(DW_DSI2H_CORE_STATUS, 0, 0),
	.cfg_core_fifos_not_empty = REG_FIELD(DW_DSI2H_CORE_STATUS, 1, 1),
	.cfg_ipi_busy = REG_FIELD(DW_DSI2H_CORE_STATUS, 8, 8),
	.cfg_ipi_fifos_not_empty = REG_FIELD(DW_DSI2H_CORE_STATUS, 9, 9),
	.cfg_cri_busy = REG_FIELD(DW_DSI2H_CORE_STATUS, 16, 16),
	.cfg_cri_wr_fifos_not_empty = REG_FIELD(DW_DSI2H_CORE_STATUS, 17, 17),
	.cfg_cri_rd_data_avail = REG_FIELD(DW_DSI2H_CORE_STATUS, 18, 18),
	.cfg_pri_busy = REG_FIELD(DW_DSI2H_CORE_STATUS, 24, 24),
	.cfg_pri_tx_fifos_not_empty = REG_FIELD(DW_DSI2H_CORE_STATUS, 25, 25),
	.cfg_pri_rx_data_avail = REG_FIELD(DW_DSI2H_CORE_STATUS, 26, 26),
	.cfg_manual_mode_en = REG_FIELD(DW_DSI2H_MANUAL_MODE_CFG, 0, 0),
	.cfg_fsm_selector = REG_FIELD(DW_DSI2H_OBS_FSM_SEL, 0, 3),
	.cfg_current_state = REG_FIELD(DW_DSI2H_OBS_FSM_STATUS, 0, 4),
	.cfg_stuck = REG_FIELD(DW_DSI2H_OBS_FSM_STATUS, 5, 5),
	.cfg_previous_state = REG_FIELD(DW_DSI2H_OBS_FSM_STATUS, 8, 12),
	.cfg_current_state_cnt = REG_FIELD(DW_DSI2H_OBS_FSM_STATUS, 16, 31),
	.cfg_fsm_manual_init = REG_FIELD(DW_DSI2H_OBS_FSM_CTRL, 0, 0),
	.cfg_fifo_selector = REG_FIELD(DW_DSI2H_OBS_FIFO_SEL, 0, 3),
	.cfg_empty = REG_FIELD(DW_DSI2H_OBS_FIFO_STATUS, 0, 0),
	.cfg_almost_empty = REG_FIELD(DW_DSI2H_OBS_FIFO_STATUS, 1, 1),
	.cfg_half_full = REG_FIELD(DW_DSI2H_OBS_FIFO_STATUS, 2, 2),
	.cfg_almost_full = REG_FIELD(DW_DSI2H_OBS_FIFO_STATUS, 3, 3),
	.cfg_full = REG_FIELD(DW_DSI2H_OBS_FIFO_STATUS, 4, 4),
	.cfg_current_word_count = REG_FIELD(DW_DSI2H_OBS_FIFO_STATUS, 16, 31),
	.cfg_fifo_manual_init = REG_FIELD(DW_DSI2H_OBS_FIFO_CTRL, 0, 0),
	.cfg_to_hstx_value = REG_FIELD(DW_DSI2H_TO_HSTX_CFG, 0, 15),
	.cfg_to_hstxrdy_value = REG_FIELD(DW_DSI2H_TO_HSTXRDY_CFG, 0, 15),
	.cfg_to_lprx_value = REG_FIELD(DW_DSI2H_TO_LPRX_CFG, 0, 15),
	.cfg_to_lptxrdy_value = REG_FIELD(DW_DSI2H_TO_LPTXRDY_CFG, 0, 15),
	.cfg_to_lptxtrig_value = REG_FIELD(DW_DSI2H_TO_LPTXTRIG_CFG, 0, 15),
	.cfg_to_lptxulps_value = REG_FIELD(DW_DSI2H_TO_LPTXULPS_CFG, 0, 15),
	.cfg_to_bta_value = REG_FIELD(DW_DSI2H_TO_BTA_CFG, 0, 15),
	.cfg_phy_type = REG_FIELD(DW_DSI2H_PHY_MODE_CFG, 0, 0),
	.cfg_phy_lanes = REG_FIELD(DW_DSI2H_PHY_MODE_CFG, 4, 5),
	.cfg_ppi_width = REG_FIELD(DW_DSI2H_PHY_MODE_CFG, 8, 9),
	.cfg_hs_transferen_en = REG_FIELD(DW_DSI2H_PHY_MODE_CFG, 12, 12),
	.cfg_clk_type = REG_FIELD(DW_DSI2H_PHY_CLK_CFG, 0, 0),
	.cfg_phy_lptx_clk_div = REG_FIELD(DW_DSI2H_PHY_CLK_CFG, 8, 13),
	.cfg_phy_direction = REG_FIELD(DW_DSI2H_PHY_STATUS, 0, 0),
	.cfg_phy_clk_stopstate = REG_FIELD(DW_DSI2H_PHY_STATUS, 8, 8),
	.cfg_phy_l0_stopstate = REG_FIELD(DW_DSI2H_PHY_STATUS, 9, 9),
	.cfg_phy_l1_stopstate = REG_FIELD(DW_DSI2H_PHY_STATUS, 10, 10),
	.cfg_phy_l2_stopstate = REG_FIELD(DW_DSI2H_PHY_STATUS, 11, 11),
	.cfg_phy_l3_stopstate = REG_FIELD(DW_DSI2H_PHY_STATUS, 12, 12),
	.cfg_phy_clk_ulpsactivenot = REG_FIELD(DW_DSI2H_PHY_STATUS, 16, 16),
	.cfg_phy_l0_ulpsactivenot = REG_FIELD(DW_DSI2H_PHY_STATUS, 17, 17),
	.cfg_phy_l1_ulpsactivenot = REG_FIELD(DW_DSI2H_PHY_STATUS, 18, 18),
	.cfg_phy_l2_ulpsactivenot = REG_FIELD(DW_DSI2H_PHY_STATUS, 19, 19),
	.cfg_phy_l3_ulpsactivenot = REG_FIELD(DW_DSI2H_PHY_STATUS, 20, 20),
	.cfg_phy_lp2hs_time = REG_FIELD(DW_DSI2H_PHY_LP2HS_MAN_CFG, 0, 28),
	.cfg_phy_lp2hs_time_auto = REG_FIELD(DW_DSI2H_PHY_LP2HS_AUTO, 0, 28),
	.cfg_phy_hs2lp_time = REG_FIELD(DW_DSI2H_PHY_HS2LP_MAN_CFG, 0, 28),
	.cfg_phy_hs2lp_time_auto = REG_FIELD(DW_DSI2H_PHY_HS2LP_AUTO, 0, 28),
	.cfg_phy_max_rd_time = REG_FIELD(DW_DSI2H_PHY_MAX_RD_T_MAN_CFG, 0, 26),
	.cfg_phy_max_rd_time_auto = REG_FIELD(DW_DSI2H_PHY_MAX_RD_T_AUTO, 0, 26),
	.cfg_phy_esc_cmd_time = REG_FIELD(DW_DSI2H_PHY_ESC_CMD_T_MAN_CFG, 0, 28),
	.cfg_phy_esc_cmd_time_auto = REG_FIELD(DW_DSI2H_PHY_ESC_CMD_T_AUTO, 0, 28),
	.cfg_phy_esc_byte_time = REG_FIELD(DW_DSI2H_PHY_ESC_BYTE_T_MAN_CFG, 0, 28),
	.cfg_phy_esc_byte_time_auto = REG_FIELD(DW_DSI2H_PHY_ESC_BYTE_T_AUTO, 0, 28),
	.cfg_phy_ipi_ratio = REG_FIELD(DW_DSI2H_PHY_IPI_RATIO_MAN_CFG, 0, 21),
	.cfg_phy_ipi_ratio_auto = REG_FIELD(DW_DSI2H_PHY_IPI_RATIO_AUTO, 0, 21),
	.cfg_phy_sys_ratio = REG_FIELD(DW_DSI2H_PHY_SYS_RATIO_MAN_CFG, 0, 16),
	.cfg_phy_sys_ratio_auto = REG_FIELD(DW_DSI2H_PHY_SYS_RATIO_AUTO, 0, 16),
	.cfg_phy_tx_trigger_0 = REG_FIELD(DW_DSI2H_PRI_TX_CMD, 0, 0),
	.cfg_phy_tx_trigger_1 = REG_FIELD(DW_DSI2H_PRI_TX_CMD, 1, 1),
	.cfg_phy_tx_trigger_2 = REG_FIELD(DW_DSI2H_PRI_TX_CMD, 2, 2),
	.cfg_phy_tx_trigger_3 = REG_FIELD(DW_DSI2H_PRI_TX_CMD, 3, 3),
	.cfg_phy_deskewcal = REG_FIELD(DW_DSI2H_PRI_TX_CMD, 8, 8),
	.cfg_phy_alternatecal = REG_FIELD(DW_DSI2H_PRI_TX_CMD, 9, 9),
	.cfg_phy_ulps_entry = REG_FIELD(DW_DSI2H_PRI_TX_CMD, 12, 12),
	.cfg_phy_ulps_exit = REG_FIELD(DW_DSI2H_PRI_TX_CMD, 13, 13),
	.cfg_phy_bta = REG_FIELD(DW_DSI2H_PRI_TX_CMD, 16, 16),
	.cfg_phy_cal_time = REG_FIELD(DW_DSI2H_PRI_CAL_CTRL, 0, 17),
	.cfg_phy_ulps_data_lanes = REG_FIELD(DW_DSI2H_PRI_ULPS_CTRL, 0, 0),
	.cfg_phy_ulps_clk_lane = REG_FIELD(DW_DSI2H_PRI_ULPS_CTRL, 4, 4),
	.cfg_phy_wakeup_time = REG_FIELD(DW_DSI2H_PRI_ULPS_CTRL, 16, 31),
	.cfg_eotp_tx_en = REG_FIELD(DW_DSI2H_DSI_GENERAL_CFG, 0, 0),
	.cfg_bta_en = REG_FIELD(DW_DSI2H_DSI_GENERAL_CFG, 1, 1),
	.cfg_tx_vcid = REG_FIELD(DW_DSI2H_DSI_VCID_CFG, 0, 1),
	.cfg_scrambling_en = REG_FIELD(DW_DSI2H_DSI_SCRAMBLING_CFG, 0, 0),
	.cfg_scrambling_seed = REG_FIELD(DW_DSI2H_DSI_SCRAMBLING_CFG, 16, 31),
	.cfg_vid_mode_type = REG_FIELD(DW_DSI2H_DSI_VID_TX_CFG, 0, 1),
	.cfg_blk_hsa_hs_en = REG_FIELD(DW_DSI2H_DSI_VID_TX_CFG, 4, 4),
	.cfg_blk_hbp_hs_en = REG_FIELD(DW_DSI2H_DSI_VID_TX_CFG, 5, 5),
	.cfg_blk_hfp_hs_en = REG_FIELD(DW_DSI2H_DSI_VID_TX_CFG, 6, 6),
	.cfg_blk_vsa_hs_en = REG_FIELD(DW_DSI2H_DSI_VID_TX_CFG, 12, 12),
	.cfg_blk_vbp_hs_en = REG_FIELD(DW_DSI2H_DSI_VID_TX_CFG, 13, 13),
	.cfg_blk_vfp_hs_en = REG_FIELD(DW_DSI2H_DSI_VID_TX_CFG, 14, 14),
	.cfg_lpdt_display_cmd_en = REG_FIELD(DW_DSI2H_DSI_VID_TX_CFG, 20, 20),
	.cfg_max_rt_pkt_sz = REG_FIELD(DW_DSI2H_DSI_MAX_RPS_CFG, 0, 15),
	.cfg_auto_tear_bta_disable = REG_FIELD(DW_DSI2H_DSI_TEAR_EFFECT_CFG, 0, 0),
	.cfg_te_type_hw = REG_FIELD(DW_DSI2H_DSI_TEAR_EFFECT_CFG, 1, 1),
	.cfg_set_tear_on_args_hw = REG_FIELD(DW_DSI2H_DSI_TEAR_EFFECT_CFG, 8, 15),
	.cfg_set_tear_scanline_args_hw = REG_FIELD(DW_DSI2H_DSI_TEAR_EFFECT_CFG, 16, 31),
	.cfg_ipi_format = REG_FIELD(DW_DSI2H_IPI_COLOR_MAN_CFG, 0, 3),
	.cfg_ipi_depth = REG_FIELD(DW_DSI2H_IPI_COLOR_MAN_CFG, 4, 7),
	.cfg_vid_hsa_time = REG_FIELD(DW_DSI2H_IPI_VID_HSA_MAN_CFG, 0, 29),
	.cfg_vid_hsa_time_auto = REG_FIELD(DW_DSI2H_IPI_VID_HSA_AUTO, 0, 29),
	.cfg_vid_hbp_time = REG_FIELD(DW_DSI2H_IPI_VID_HBP_MAN_CFG, 0, 29),
	.cfg_vid_hbp_time_auto = REG_FIELD(DW_DSI2H_IPI_VID_HBP_AUTO, 0, 29),
	.cfg_vid_hact_time = REG_FIELD(DW_DSI2H_IPI_VID_HACT_MAN_CFG, 0, 29),
	.cfg_vid_hact_time_auto = REG_FIELD(DW_DSI2H_IPI_VID_HACT_AUTO, 0, 29),
	.cfg_vid_hline_time = REG_FIELD(DW_DSI2H_IPI_VID_HLINE_MAN_CFG, 0, 31),
	.cfg_vid_hline_time_auto = REG_FIELD(DW_DSI2H_IPI_VID_HLINE_AUTO, 0, 31),
	.cfg_vid_vsa_lines = REG_FIELD(DW_DSI2H_IPI_VID_VSA_MAN_CFG, 0, 9),
	.cfg_vid_vsa_lines_auto = REG_FIELD(DW_DSI2H_IPI_VID_VSA_AUTO, 0, 9),
	.cfg_vid_vbp_lines = REG_FIELD(DW_DSI2H_IPI_VID_VBP_MAN_CFG, 0, 9),
	.cfg_vid_vbp_lines_auto = REG_FIELD(DW_DSI2H_IPI_VID_VBP_AUTO, 0, 9),
	.cfg_vid_vact_lines = REG_FIELD(DW_DSI2H_IPI_VID_VACT_MAN_CFG, 0, 13),
	.cfg_vid_vact_lines_auto = REG_FIELD(DW_DSI2H_IPI_VID_VACT_AUTO, 0, 13),
	.cfg_vid_vfp_lines = REG_FIELD(DW_DSI2H_IPI_VID_VFP_MAN_CFG, 0, 9),
	.cfg_vid_vfp_lines_auto = REG_FIELD(DW_DSI2H_IPI_VID_VFP_AUTO, 0, 9),
	.cfg_max_pix_pkt = REG_FIELD(DW_DSI2H_IPI_PIX_PKT_CFG, 0, 15),
	.cfg_hib_type = REG_FIELD(DW_DSI2H_IPI_HIBERNATE_CFG, 0, 0),
	.cfg_hib_ulps_wakeup_time = REG_FIELD(DW_DSI2H_IPI_HIBERNATE_CFG, 16, 31),
	.cfg_mask_phy_l0_erresc = REG_FIELD(DW_DSI2H_INT_MASK_PHY, 0, 0),
	.cfg_mask_phy_l0_errsyncesc = REG_FIELD(DW_DSI2H_INT_MASK_PHY, 1, 1),
	.cfg_mask_phy_l0_errcontrol = REG_FIELD(DW_DSI2H_INT_MASK_PHY, 2, 2),
	.cfg_mask_phy_l0_errcontentionlp0 = REG_FIELD(DW_DSI2H_INT_MASK_PHY, 3, 3),
	.cfg_mask_phy_l0_errcontentionlp1 = REG_FIELD(DW_DSI2H_INT_MASK_PHY, 4, 4),
	.cfg_mask_txhs_fifo_over = REG_FIELD(DW_DSI2H_INT_MASK_PHY, 16, 16),
	.cfg_mask_txhs_fifo_under = REG_FIELD(DW_DSI2H_INT_MASK_PHY, 17, 17),
	.cfg_force_phy_l0_erresc = REG_FIELD(DW_DSI2H_INT_FORCE_PHY, 0, 0),
	.cfg_force_phy_l0_errsyncesc = REG_FIELD(DW_DSI2H_INT_FORCE_PHY, 1, 1),
	.cfg_force_phy_l0_errcontrol = REG_FIELD(DW_DSI2H_INT_FORCE_PHY, 2, 2),
	.cfg_force_phy_l0_errcontentionlp0 = REG_FIELD(DW_DSI2H_INT_FORCE_PHY, 3, 3),
	.cfg_force_phy_l0_errcontentionlp1 = REG_FIELD(DW_DSI2H_INT_FORCE_PHY, 4, 4),
	.cfg_force_txhs_fifo_over = REG_FIELD(DW_DSI2H_INT_FORCE_PHY, 16, 16),
	.cfg_force_txhs_fifo_under = REG_FIELD(DW_DSI2H_INT_FORCE_PHY, 17, 17),
	.cfg_mask_err_to_hstx = REG_FIELD(DW_DSI2H_INT_MASK_TO, 0, 0),
	.cfg_mask_err_to_hstxrdy = REG_FIELD(DW_DSI2H_INT_MASK_TO, 1, 1),
	.cfg_mask_err_to_lprx = REG_FIELD(DW_DSI2H_INT_MASK_TO, 2, 2),
	.cfg_mask_err_to_lptxrdy = REG_FIELD(DW_DSI2H_INT_MASK_TO, 3, 3),
	.cfg_mask_err_to_lptxtrig = REG_FIELD(DW_DSI2H_INT_MASK_TO, 4, 4),
	.cfg_mask_err_to_lptxulps = REG_FIELD(DW_DSI2H_INT_MASK_TO, 5, 5),
	.cfg_mask_err_to_bta = REG_FIELD(DW_DSI2H_INT_MASK_TO, 6, 6),
	.cfg_force_err_to_hstx = REG_FIELD(DW_DSI2H_INT_FORCE_TO, 0, 0),
	.cfg_force_err_to_hstxrdy = REG_FIELD(DW_DSI2H_INT_FORCE_TO, 1, 1),
	.cfg_force_err_to_lprx = REG_FIELD(DW_DSI2H_INT_FORCE_TO, 2, 2),
	.cfg_force_err_to_lptxrdy = REG_FIELD(DW_DSI2H_INT_FORCE_TO, 3, 3),
	.cfg_force_err_to_lptxtrig = REG_FIELD(DW_DSI2H_INT_FORCE_TO, 4, 4),
	.cfg_force_err_to_lptxulps = REG_FIELD(DW_DSI2H_INT_FORCE_TO, 5, 5),
	.cfg_force_err_to_bta = REG_FIELD(DW_DSI2H_INT_FORCE_TO, 6, 6),
	.cfg_mask_err_ack_rpt_0 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 0, 0),
	.cfg_mask_err_ack_rpt_1 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 1, 1),
	.cfg_mask_err_ack_rpt_2 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 2, 2),
	.cfg_mask_err_ack_rpt_3 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 3, 3),
	.cfg_mask_err_ack_rpt_4 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 4, 4),
	.cfg_mask_err_ack_rpt_5 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 5, 5),
	.cfg_mask_err_ack_rpt_6 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 6, 6),
	.cfg_mask_err_ack_rpt_7 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 7, 7),
	.cfg_mask_err_ack_rpt_8 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 8, 8),
	.cfg_mask_err_ack_rpt_9 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 9, 9),
	.cfg_mask_err_ack_rpt_10 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 10, 10),
	.cfg_mask_err_ack_rpt_11 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 11, 11),
	.cfg_mask_err_ack_rpt_12 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 12, 12),
	.cfg_mask_err_ack_rpt_13 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 13, 13),
	.cfg_mask_err_ack_rpt_14 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 14, 14),
	.cfg_mask_err_ack_rpt_15 = REG_FIELD(DW_DSI2H_INT_MASK_ACK, 15, 15),
	.cfg_force_err_ack_rpt_0 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 0, 0),
	.cfg_force_err_ack_rpt_1 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 1, 1),
	.cfg_force_err_ack_rpt_2 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 2, 2),
	.cfg_force_err_ack_rpt_3 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 3, 3),
	.cfg_force_err_ack_rpt_4 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 4, 4),
	.cfg_force_err_ack_rpt_5 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 5, 5),
	.cfg_force_err_ack_rpt_6 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 6, 6),
	.cfg_force_err_ack_rpt_7 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 7, 7),
	.cfg_force_err_ack_rpt_8 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 8, 8),
	.cfg_force_err_ack_rpt_9 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 9, 9),
	.cfg_force_err_ack_rpt_10 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 10, 10),
	.cfg_force_err_ack_rpt_11 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 11, 11),
	.cfg_force_err_ack_rpt_12 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 12, 12),
	.cfg_force_err_ack_rpt_13 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 13, 13),
	.cfg_force_err_ack_rpt_14 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 14, 14),
	.cfg_force_err_ack_rpt_15 = REG_FIELD(DW_DSI2H_INT_FORCE_ACK, 15, 15),
	.cfg_mask_err_display_cmd_time = REG_FIELD(DW_DSI2H_INT_MASK_IPI, 0, 0),
	.cfg_mask_err_ipi_dtype = REG_FIELD(DW_DSI2H_INT_MASK_IPI, 1, 1),
	.cfg_mask_err_vid_bandwidth = REG_FIELD(DW_DSI2H_INT_MASK_IPI, 2, 2),
	.cfg_mask_err_ipi_cmd = REG_FIELD(DW_DSI2H_INT_MASK_IPI, 3, 3),
	.cfg_mask_err_display_cmd_ovfl = REG_FIELD(DW_DSI2H_INT_MASK_IPI, 8, 8),
	.cfg_mask_ipi_event_fifo_over = REG_FIELD(DW_DSI2H_INT_MASK_IPI, 16, 16),
	.cfg_mask_ipi_event_fifo_under = REG_FIELD(DW_DSI2H_INT_MASK_IPI, 17, 17),
	.cfg_mask_ipi_pixel_fifo_over = REG_FIELD(DW_DSI2H_INT_MASK_IPI, 18, 18),
	.cfg_mask_ipi_pixel_fifo_under = REG_FIELD(DW_DSI2H_INT_MASK_IPI, 19, 19),
	.cfg_force_err_display_cmd_time = REG_FIELD(DW_DSI2H_INT_FORCE_IPI, 0, 0),
	.cfg_force_err_ipi_dtype = REG_FIELD(DW_DSI2H_INT_FORCE_IPI, 1, 1),
	.cfg_force_err_vid_bandwidth = REG_FIELD(DW_DSI2H_INT_FORCE_IPI, 2, 2),
	.cfg_force_err_ipi_cmd = REG_FIELD(DW_DSI2H_INT_FORCE_IPI, 3, 3),
	.cfg_force_err_display_cmd_ovfl = REG_FIELD(DW_DSI2H_INT_FORCE_IPI, 8, 8),
	.cfg_force_ipi_event_fifo_over = REG_FIELD(DW_DSI2H_INT_FORCE_IPI, 16, 16),
	.cfg_force_ipi_event_fifo_under = REG_FIELD(DW_DSI2H_INT_FORCE_IPI, 17, 17),
	.cfg_force_ipi_pixel_fifo_over = REG_FIELD(DW_DSI2H_INT_FORCE_IPI, 18, 18),
	.cfg_force_ipi_pixel_fifo_under = REG_FIELD(DW_DSI2H_INT_FORCE_IPI, 19, 19),
	.cfg_mask_err_pri_tx_time = REG_FIELD(DW_DSI2H_INT_MASK_PRI, 0, 0),
	.cfg_mask_err_pri_tx_cmd = REG_FIELD(DW_DSI2H_INT_MASK_PRI, 1, 1),
	.cfg_force_err_pri_tx_time = REG_FIELD(DW_DSI2H_INT_FORCE_PRI, 0, 0),
	.cfg_force_err_pri_tx_cmd = REG_FIELD(DW_DSI2H_INT_FORCE_PRI, 1, 1),
	.cfg_mask_err_cri_cmd_time = REG_FIELD(DW_DSI2H_INT_MASK_CRI, 0, 0),
	.cfg_mask_err_cri_dtype = REG_FIELD(DW_DSI2H_INT_MASK_CRI, 1, 1),
	.cfg_mask_err_cri_vchannel = REG_FIELD(DW_DSI2H_INT_MASK_CRI, 2, 2),
	.cfg_mask_err_cri_rx_length = REG_FIELD(DW_DSI2H_INT_MASK_CRI, 3, 3),
	.cfg_mask_err_cri_ecc = REG_FIELD(DW_DSI2H_INT_MASK_CRI, 4, 4),
	.cfg_mask_err_cri_ecc_fatal = REG_FIELD(DW_DSI2H_INT_MASK_CRI, 5, 5),
	.cfg_mask_err_cri_crc = REG_FIELD(DW_DSI2H_INT_MASK_CRI, 6, 6),
	.cfg_mask_cmd_rd_pld_fifo_over = REG_FIELD(DW_DSI2H_INT_MASK_CRI, 16, 16),
	.cfg_mask_cmd_rd_pld_fifo_under = REG_FIELD(DW_DSI2H_INT_MASK_CRI, 17, 17),
	.cfg_mask_cmd_wr_pld_fifo_over = REG_FIELD(DW_DSI2H_INT_MASK_CRI, 18, 18),
	.cfg_mask_cmd_wr_pld_fifo_under = REG_FIELD(DW_DSI2H_INT_MASK_CRI, 19, 19),
	.cfg_mask_cmd_wr_hdr_fifo_over = REG_FIELD(DW_DSI2H_INT_MASK_CRI, 20, 20),
	.cfg_mask_cmd_wr_hdr_fifo_under = REG_FIELD(DW_DSI2H_INT_MASK_CRI, 21, 21),
	.cfg_force_err_cri_cmd_time = REG_FIELD(DW_DSI2H_INT_FORCE_CRI, 0, 0),
	.cfg_force_err_cri_dtype = REG_FIELD(DW_DSI2H_INT_FORCE_CRI, 1, 1),
	.cfg_force_err_cri_vchannel = REG_FIELD(DW_DSI2H_INT_FORCE_CRI, 2, 2),
	.cfg_force_err_cri_rx_length = REG_FIELD(DW_DSI2H_INT_FORCE_CRI, 3, 3),
	.cfg_force_err_cri_ecc = REG_FIELD(DW_DSI2H_INT_FORCE_CRI, 4, 4),
	.cfg_force_err_cri_ecc_fatal = REG_FIELD(DW_DSI2H_INT_FORCE_CRI, 5, 5),
	.cfg_force_err_cri_crc = REG_FIELD(DW_DSI2H_INT_FORCE_CRI, 6, 6),
	.cfg_force_cmd_rd_pld_fifo_over = REG_FIELD(DW_DSI2H_INT_FORCE_CRI, 16, 16),
	.cfg_force_cmd_rd_pld_fifo_under = REG_FIELD(DW_DSI2H_INT_FORCE_CRI, 17, 17),
	.cfg_force_cmd_wr_pld_fifo_over = REG_FIELD(DW_DSI2H_INT_FORCE_CRI, 18, 18),
	.cfg_force_cmd_wr_pld_fifo_under = REG_FIELD(DW_DSI2H_INT_FORCE_CRI, 19, 19),
	.cfg_force_cmd_wr_hdr_fifo_over = REG_FIELD(DW_DSI2H_INT_FORCE_CRI, 20, 20),
	.cfg_force_cmd_wr_hdr_fifo_under = REG_FIELD(DW_DSI2H_INT_FORCE_CRI, 21, 21),
};

#define host_to_dsi2h(host) container_of(host, struct dw_mipi_dsi2h, dsi_host)
static int dw_mipi_dsi2h_enable(struct dw_mipi_dsi2h *dsi2h);

static inline struct dw_mipi_dsi2h *bridge_to_dsi(struct drm_bridge *bridge)
{
	return container_of(bridge, struct dw_mipi_dsi2h, bridge);
}

/**
 * dw_dsi2h_wait_core_idle() - Wait for the DSI2H core to be idle.
 * @dsi2h: The DSI2H device structure.
 *
 * This function polls the DW_DSI2H_CORE_STATUS register until all status
 * bits are clear, indicating that the core and its sub-modules (IPI, CRI, PRI)
 * are idle and all FIFOs are empty.
 *
 * Return: 0 if the core becomes idle within the timeout, or the CORE_STATUS register content.
 */
static u32 dw_dsi2h_wait_core_idle(struct dw_mipi_dsi2h *dsi2h)
{
	u32 val = 0;

	DPU_ATRACE_BEGIN("Polling Core Busy");
	regmap_read_poll_timeout(dsi2h->regs, DW_DSI2H_CORE_STATUS, val, !val,
				 DW_DSI2H_CORE_STATUS_INTERVAL_US, DW_DSI2H_CORE_STATUS_TIMEOUT_US);
	DPU_ATRACE_END("Polling Core Busy");

	return val;
}

/* PRI functions */

/**
 * dw_dsi2h_wait_pri_not_busy() - Wait for the DSI2H PRI to be not busy.
 * @dsi2h: The DSI2H device structure.
 *
 * This function polls the DW_DSI2H_CORE_STATUS register until the PRI_BUSY
 * status bit is clear, indicating that the PRI is not busy and ready for
 * transactions. If the PRI remains busy after timeout, it increments the
 * ST_PRI_BUSY statistic.
 *
 * Return: 0 if the PRI becomes not busy within the timeout, or the CORE_STATUS register content.
 */
static u32 dw_dsi2h_wait_pri_not_busy(struct dw_mipi_dsi2h *dsi2h)
{
	u32 val = 0;

	DPU_ATRACE_BEGIN("Polling PRI Busy");
	regmap_read_poll_timeout(dsi2h->regs, DW_DSI2H_CORE_STATUS, val, !(val & PRI_BUSY),
				 DW_DSI2H_PRI_BUSY_INTERVAL_US, DW_DSI2H_PRI_BUSY_TIMEOUT_US);
	DPU_ATRACE_END("Polling PRI Busy");

	if (unlikely(val & PRI_BUSY)) {
		if (dsi2h->stat_data)
			dsi2h->stat_data[ST_PRI_BUSY]++;
		return val;
	}

	return 0;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int dw_dsi2h_pri_trigger_request(struct dw_mipi_dsi2h *dsi2h, u8 request)
{
	u32 core_status;

	if (request != PHY_TX_TRIGGER_0 && request != PHY_TX_TRIGGER_1 &&
	    request != PHY_TX_TRIGGER_2 && request != PHY_TX_TRIGGER_3)
		return -EINVAL;

	/* Step 1 - PRI busy ? */
	core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
	if (unlikely(core_status)) {
		dev_err(dsi2h->dev, "PRI Busy! status = %x\n", core_status);
		return -EBUSY;
	}

	/* Step 2 - Select Trigger Request */
	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_PRI_TX_CMD, request);

	/* Step 3 - Request was completed ? */
	core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
	if (unlikely(core_status)) {
		dev_err(dsi2h->dev,
			"PRI didn't leave busy state after sending request! status = %x\n",
			core_status);
		return -EAGAIN;
	}

	return 0;
}

static int dw_dsi2h_pri_calibration(struct dw_mipi_dsi2h *dsi2h,
				    enum pri_cal_request type, u32 time)
{
	u32 core_status;

	if (time > 0x3FFFF)
		return -EINVAL;

	/* Step 1 - PRI busy ? */
	core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
	if (unlikely(core_status)) {
		dev_err(dsi2h->dev, "PRI Busy! status = %x\n", core_status);
		return -EBUSY;
	}

	/* Step 2 - Specify Calibration Duration */
	dw_dsi2h_write_field(dsi2h->field_phy_cal_time, time);

	/* Step 3 - Select Calibration Request */
	if (type == PHY_DESKEW_CAL)
		dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_PRI_TX_CMD, PHY_DESKEWCAL);
	else
		dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_PRI_TX_CMD, PHY_ALTERNATECAL);

	/* Step 4 - Request was completed ? */
	core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
	if (unlikely(core_status)) {
		dev_err(dsi2h->dev,
			"PRI didn't leave busy state after sending request! status = %x\n",
			core_status);
		return -EAGAIN;
	}

	return 0;
}
#endif /* CONFIG_DEBUG_FS */

static __maybe_unused int dw_dsi2h_pri_ulps_entry_request(struct dw_mipi_dsi2h *dsi2h,
							  u8 clk, u8 mode)
{
	u32 val = 0;
	u32 core_status;

	if (clk != 0 && clk != 1)
		return -EINVAL;

	if (mode != 0 && mode != 1)
		return -EINVAL;

	DPU_ATRACE_BEGIN("%s +", __func__);
	/* Step 1 - PRI busy ? */
	core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
	if (unlikely(core_status)) {
		dev_err(dsi2h->dev, "PRI Busy! status = %x\n", core_status);
		DPU_ATRACE_END("%s +", __func__);
		return -EBUSY;
	}

	/* Step 2 - Specify ULPS Entry Mode */
	dw_dsi2h_write_field(dsi2h->field_phy_ulps_clk_lane, clk);
	dw_dsi2h_write_field(dsi2h->field_phy_ulps_data_lanes, mode);

	/* Step 3 - Select ULPS Entry Request */
	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_PRI_TX_CMD, PHY_ULPS_ENTRY);

	/* Step 4 - Request was completed ? */
	core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
	if (unlikely(core_status)) {
		dev_err(dsi2h->dev,
			"PRI didn't leave busy state after sending request! status = %x\n",
			core_status);
		DPU_ATRACE_END("%s +", __func__);
		return -EAGAIN;
	}

	/* Read the PHY_STATUS register to check the ULPS status of the different lanes */
	val = dw_dsi2h_read_reg(dsi2h->regs, DW_DSI2H_PHY_STATUS);
	val = (val >> 17) & 0x0f;
	DPU_ATRACE_END("%s +", __func__);

	return val;
}

static __maybe_unused int dw_dsi2h_pri_ulps_exit_request(struct dw_mipi_dsi2h *dsi2h, u16 wakeup)
{
	u32 val = 0;
	u32 core_status;
	/* To the minimum 1ms specified by the spec */

	DPU_ATRACE_BEGIN("%s +", __func__);
	/* Step 1 - PRI busy ? */
	core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
	if (unlikely(core_status)) {
		dev_err(dsi2h->dev, "PRI Busy! status = %x\n", core_status);
		DPU_ATRACE_END("%s +", __func__);
		return -EBUSY;
	}

	/* Step 2 - Specify ULPS Exit Mode */
	dw_dsi2h_write_field(dsi2h->field_phy_wakeup_time, wakeup);

	/* Step 3 - Select ULPS Exit Request */
	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_PRI_TX_CMD, PHY_ULPS_EXIT);

	/* Step 4 - Request was completed ? */
	core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
	if (unlikely(core_status)) {
		dev_err(dsi2h->dev,
			"PRI didn't leave busy state after sending request! status = %x\n",
			core_status);
		DPU_ATRACE_END("%s +", __func__);
		return -EAGAIN;
	}

	/* Read the PHY_STATUS register to check the ULPS status of the different lanes */
	val = dw_dsi2h_read_reg(dsi2h->regs, DW_DSI2H_PHY_STATUS);
	val = (val >> 17) & 0x0f;
	DPU_ATRACE_END("%s +", __func__);

	return val;
}

static int dw_mipi_dsi2h_enter_ulps(struct dw_mipi_dsi2h *dsi2h)
{
	int ret;
	u32 core_status;

	lockdep_assert_held(&dsi2h->dsi2h_lock);

	DPU_ATRACE_BEGIN("%s +", __func__);
	core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
	if (unlikely(core_status)) {
		dev_err(dsi2h->dev, " %s: PRI Busy! status = %x\n", __func__, core_status);
		DPU_ATRACE_END("%s +", __func__);
		return -EBUSY;
	}

	/* DW flow is set PHY to ULPS first, then dsi host start ULPS entry */
	ret = phy_set_mode_ext(dsi2h->phy, PHY_MODE_MIPI_DPHY, DW_MIPI_CDPHY_OP_OVR_ULPS_ENTER);
	if (unlikely(ret)) {
		dev_err(dsi2h->dev, "%s: PHY ULPS state enter fail, ret = %d\n", __func__, ret);
		DPU_ATRACE_END("%s +", __func__);
		if (dsi2h->stat_data)
			dsi2h->stat_data[ST_ULPS_ENTER_ERR]++;
		return ret;
	}

	ret = dw_dsi2h_pri_ulps_entry_request(dsi2h, 1, 1);
	/* function dw_dsi2h_pri_ulps_entry_request return CSR value if success */
	if (unlikely(ret < 0)) {
		dev_err(dsi2h->dev, "%s dsi2h ULPS enter fail, ret = %d\n", __func__, ret);
		DPU_ATRACE_END("%s +", __func__);
		if (dsi2h->stat_data)
			dsi2h->stat_data[ST_ULPS_ENTER_ERR]++;
		return ret;
	}

	ret = phy_set_mode_ext(dsi2h->phy, PHY_MODE_MIPI_DPHY, DW_MIPI_CDPHY_OP_PLL_DISABLE);
	if (unlikely(ret)) {
		dev_err(dsi2h->dev, "%s: PHY disable fail, ret = %d", __func__, ret);
		DPU_ATRACE_END("%s +", __func__);
		if (dsi2h->stat_data)
			dsi2h->stat_data[ST_ULPS_ENTER_ERR]++;
		return ret;
	}
	DPU_ATRACE_END("%s +", __func__);

	dsi2h->state = DSI2H_STATE_ULPS;

	return ret;
}

static int dw_mipi_dsi2h_exit_ulps(struct dw_mipi_dsi2h *dsi2h)
{
	int ret;
	u32 core_status;

	lockdep_assert_held(&dsi2h->dsi2h_lock);

	DPU_ATRACE_BEGIN("%s +", __func__);
	core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
	if (unlikely(core_status)) {
		dev_err(dsi2h->dev, "%s: PRI Busy! status = %x\n", __func__, core_status);
		DPU_ATRACE_END("%s +", __func__);
		return -EBUSY;
	}

	ret = phy_set_mode_ext(dsi2h->phy, PHY_MODE_MIPI_DPHY, DW_MIPI_CDPHY_OP_PLL_ENABLE);
	if (unlikely(ret)) {
		dev_err(dsi2h->dev, "%s: PLL enable fail, ret = %d\n", __func__, ret);
		DPU_ATRACE_END("%s +", __func__);
		if (dsi2h->stat_data)
			dsi2h->stat_data[ST_ULPS_EXIT_ERR]++;
		return ret;
	}

	ret = dw_dsi2h_pri_ulps_exit_request(dsi2h, 0x8F7);
	/* function dw_dsi2h_pri_ulps_exit_request return CSR value if success */
	if (unlikely(ret < 0)) {
		dev_err(dsi2h->dev, "%s dsi2h ULPS exit fail, ret = %d\n", __func__, ret);
		DPU_ATRACE_END("%s +", __func__);
		if (dsi2h->stat_data)
			dsi2h->stat_data[ST_ULPS_EXIT_ERR]++;
		return ret;
	}

	ret = phy_set_mode_ext(dsi2h->phy, PHY_MODE_MIPI_DPHY, DW_MIPI_CDPHY_OP_OVR_ULPS_EXIT);
	if (unlikely(ret)) {
		dev_err(dsi2h->dev, "%s: PHY ULPS state exit fail, ret = %d\n",	__func__, ret);
		DPU_ATRACE_END("%s +", __func__);
		if (dsi2h->stat_data)
			dsi2h->stat_data[ST_ULPS_EXIT_ERR]++;
		return ret;
	}
	DPU_ATRACE_END("%s +", __func__);

	/* wait for lanes to stabilize */
	DPU_ATRACE_BEGIN("T_lane_stable");
	usleep_range(100, 110);
	DPU_ATRACE_END("T_lane_stable");

	dsi2h->state = DSI2H_STATE_HS_EN;

	return ret;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int dw_dsi2h_pri_receive_triggers(struct dw_mipi_dsi2h *dsi2h)
{
	u32 val = 0;

	DPU_ATRACE_BEGIN("Polling PRI Receive");
	regmap_read_poll_timeout(dsi2h->regs, DW_DSI2H_CORE_STATUS, val, (val & PRI_RX_DATA_AVAIL),
				 DW_DSI2H_PRI_RECEIVE_INTERVAL_US, DW_DSI2H_PRI_RECEIVE_TIMEOUT_US);
	DPU_ATRACE_END("Polling PRI Receive");

	if (!(val & PRI_RX_DATA_AVAIL)) {
		dev_err(dsi2h->dev, "PRI RX Data Available didn't assert!\n");
		return -EAGAIN;
	}

	val = dw_dsi2h_read_reg(dsi2h->regs, DW_DSI2H_PRI_RX_CMD);

	/* Return the value of the trigger bits, handle it on reception */
	return val & 0xF;
}

static int dw_dsi2h_pri_bta_request(struct dw_mipi_dsi2h *dsi2h)
{
	u32 core_status;

	/* Step 1 - PRI busy ? */
	core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
	if (unlikely(core_status)) {
		dev_err(dsi2h->dev, "PRI Busy! status = %x\n", core_status);
		return -EBUSY;
	}

	/* Step 2 - Select BTA Request */
	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_PRI_TX_CMD, PHY_BTA);

	/* Step 3 - Request was completed ? */
	core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
	if (unlikely(core_status)) {
		dev_err(dsi2h->dev,
			"PRI didn't leave busy state after sending request! status = %x\n",
			core_status);
		return -EAGAIN;
	}

	/* After performing a BTA, the peripheral may respond with an Acknowledge trigger. */
	return dw_dsi2h_pri_receive_triggers(dsi2h);
}
#endif /* CONFIG_DEBUG_FS */

/* CRI functions */

/**
 * dw_dsi2h_wait_cri_not_busy() - Wait for the DSI2H CRI to be not busy.
 * @dsi2h: The DSI2H device structure.
 *
 * This function polls the DW_DSI2H_CORE_STATUS register until the CRI_BUSY
 * status bit is clear, indicating that the CRI is not busy and ready for
 * transactions. If the CRI remains busy after timeout, it increments the
 * ST_CRI_BUSY statistic.
 *
 * Return: 0 if the CRI becomes not busy within the timeout, or the CORE_STATUS register content.
 */
static u32 dw_dsi2h_wait_cri_not_busy(struct dw_mipi_dsi2h *dsi2h)
{
	u32 val = 0;

	DPU_ATRACE_BEGIN("Polling CRI Busy");
	regmap_read_poll_timeout(dsi2h->regs, DW_DSI2H_CORE_STATUS, val, !(val & CRI_BUSY),
				 DW_DSI2H_CRI_BUSY_INTERVAL_US, DW_DSI2H_CRI_BUSY_TIMEOUT_US);
	DPU_ATRACE_END("Polling CRI Busy");

	if (unlikely(val & CRI_BUSY)) {
		if (dsi2h->stat_data)
			dsi2h->stat_data[ST_CRI_BUSY]++;
		return val;
	}

	return 0;
}

/**
 * dw_dsi2h_cri_rd_data_avail() - Check if CRI read data is available.
 * @dsi2h: The DSI2H device structure.
 *
 * This function polls the DW_DSI2H_CORE_STATUS register, specifically
 * checking the CRI_RD_DATA_AVAIL bit, until data is available or a timeout occurs.
 *
 * Return: The value of the CRI_RD_DATA_AVAIL bit from the status register if
 *         successful, or a non-zero error code if polling times out.
 */
static int dw_dsi2h_cri_rd_data_avail(struct dw_mipi_dsi2h *dsi2h)
{
	int ret;
	u32 val = 0;

	DPU_ATRACE_BEGIN("Polling CRI read data");
	ret = regmap_read_poll_timeout(dsi2h->regs, DW_DSI2H_CORE_STATUS, val,
				       (val & CRI_RD_DATA_AVAIL),
				       DW_DSI2H_CRI_DATA_AVAIL_INTERVAL_US,
				       DW_DSI2H_CRI_DATA_AVAIL_TIMEOUT_US);
	if (unlikely(ret))
		dev_err(dsi2h->dev, "Polling CRI read data failed %d, status = %x\n", ret, val);
	DPU_ATRACE_END("Polling CRI read data");

	return val & CRI_RD_DATA_AVAIL;
}

static int dw_mipi_dsi2h_cri_read(struct dw_mipi_dsi2h *dsi2h,
				  const struct mipi_dsi_msg *msg)
{
	int rcv_len = 0;
	int buf_len = msg->rx_len;
	int pkg_remain_len;
	u32 header;
	__le32 word;
	u8 data_type, *buf_pu8;

	if (dw_dsi2h_cri_rd_data_avail(dsi2h)) {
		u8 wc_msb, wc_lsb;
		/* Read CRI RX Header */
		header = dw_dsi2h_read_reg(dsi2h->regs, DW_DSI2H_CRI_RX_HDR);
		data_type = header & RX_HDR_DATA_TYPE;
		/* In short packages format WC_LSB and WC_MSB are Data 0 and Data 1 */
		wc_msb = ((header & RX_HDR_WC_MSB) >> 16) & 0xFF;
		wc_lsb = ((header & RX_HDR_WC_LSB) >> 8) & 0xFF;
		dev_dbg(dsi2h->dev, "CRI Read Header: 0x%02X 0x%02X 0x%02X", data_type, wc_lsb,
			wc_msb);

		switch (data_type) {
		case MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT:
			dev_err(dsi2h->dev, "%s rx ACK error report\n", __func__);
			if (dsi2h->stat_data)
				dsi2h->stat_data[ST_DCS_READ_ACK_ERR]++;
			break;

		case MIPI_DSI_RX_END_OF_TRANSMISSION:
			dev_dbg(dsi2h->dev, "%s rx end of transmission\n", __func__);
			break;

		case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE:
		case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_2BYTE:
			if (buf_len > 1) {
				((u8 *)msg->rx_buf)[1] = wc_msb;
				rcv_len++;
			}
			fallthrough;

		case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_1BYTE:
		case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE:
			if (buf_len > 0) {
				((u8 *)msg->rx_buf)[0] = wc_lsb;
				rcv_len++;
			}
			break;

		case MIPI_DSI_RX_GENERIC_LONG_READ_RESPONSE:
		case MIPI_DSI_RX_DCS_LONG_READ_RESPONSE:
			pkg_remain_len = (wc_msb << 8) | wc_lsb;
			dev_dbg(dsi2h->dev, "%s: long read response with payload %d bytes\n",
				__func__, pkg_remain_len);
			/* Read long packet payload */
			buf_pu8 = &((u8 *)msg->rx_buf)[0];
			while (pkg_remain_len > 0 && dw_dsi2h_cri_rd_data_avail(dsi2h)) {
				word = dw_dsi2h_read_reg(dsi2h->regs, DW_DSI2H_CRI_RX_PLD);
				pkg_remain_len -= 4;
				word = le32_to_cpu(word);

				if (rcv_len < buf_len) {
					int write_len = buf_len - rcv_len;
					if (write_len > 4)
						write_len = 4;

					memcpy(buf_pu8, &word, write_len);
					rcv_len += write_len;
					buf_pu8 += write_len;
				} else {
					dev_warn(dsi2h->dev, "Received extra data: %x\n", word);
					if (dsi2h->stat_data)
						dsi2h->stat_data[ST_DCS_READ_EXTRA_DATA]++;
				}
			}
			if (unlikely(pkg_remain_len > 0)) {
				dev_warn(dsi2h->dev, "%s: %d bytes not arrived\n", __func__,
					 pkg_remain_len);
				if (dsi2h->stat_data)
					dsi2h->stat_data[ST_DCS_READ_DATA_NOT_ARRIVED]++;
			}
			break;
		}
	}

	dev_dbg(dsi2h->dev, "%s: received %d bytes\n", __func__, rcv_len);
	return rcv_len;
}

static int dw_mipi_dsi2h_host_attach(struct mipi_dsi_host *host,
				     struct mipi_dsi_device *device)
{
	struct dw_mipi_dsi2h *dsi2h = host_to_dsi2h(host);
	const struct dw_mipi_dsi2h_plat_data *pdata = dsi2h->plat_data;
	int ret;

	if (device->lanes > dsi2h->max_data_lanes) {
		dev_err(dsi2h->dev, "number of data lanes(%u) is too many\n",
			device->lanes);
		return -EINVAL;
	}

	dsi2h->dsi_device = device;
	drm_bridge_add(&dsi2h->bridge);

	if (pdata->host_ops && pdata->host_ops->attach) {
		ret = pdata->host_ops->attach(pdata->priv_data, device);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int dw_mipi_dsi2h_host_detach(struct mipi_dsi_host *host,
				     struct mipi_dsi_device *device)
{
	return 0;
}

static int dw_mipi_dsi2h_host_packet_stack_enable(struct dw_mipi_dsi2h *dsi2h)
{
	u32 enable_packet_stack_flags = CRI_HOLD;

	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_CRI_TX_CTRL, enable_packet_stack_flags);
	dsi2h->packet_stack_mode = true;

	return 0;
}

static int dw_mipi_dsi2h_host_packet_stack_flush(struct dw_mipi_dsi2h *dsi2h)
{
	u32 flush_packet_stack_flags = 0;
	u32 core_status;

	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_CRI_TX_CTRL, flush_packet_stack_flags);
	dsi2h->packet_stack_mode = false;
	dsi2h->mipi_fifo_pld_used = 0;
	dsi2h->mipi_fifo_hdr_used = 0;

	core_status = dw_dsi2h_wait_cri_not_busy(dsi2h);
	if (unlikely(core_status)) {
		dev_err(dsi2h->dev, "CRI Busy! status = %x\n", core_status);
		return -EBUSY;
	}

	return 0;
}

static ssize_t dw_mipi_dsi2h_host_write_msg(struct dw_mipi_dsi2h *dsi2h,
					    const struct mipi_dsi_msg *msg)
{
	int ret, len, pld_data_bytes = sizeof(u32);
	struct mipi_dsi_packet packet;
	const u8 *tx_buf;
	__le32 word;

	/*Verify the size and the payload*/
	if (!msg->tx_buf && msg->tx_len != 0) {
		dev_err(dsi2h->dev, "No message to send\n");
		return -1;
	}

	/* TODO(b/416631795): command batch might not work for mipi_sync */
	if (msg->tx_len == 0)
		return 0;

	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret) {
		dev_err(dsi2h->dev, "failed to create packet: %d\n", ret);
		return ret;
	}

	if (!dsi2h->enabled) {
		dev_WARN(dsi2h->dev, "Unable to transfer cmd while disabled, state(%d)\n",
			 dsi2h->state);
		return -EPERM;
	}

	if (msg->rx_len) {
		u8 header[4] = {
			MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE,
			msg->rx_len & 0xff,
			(msg->rx_len >> 8) & 0xff,
		};
		memcpy(&word, header, sizeof(header));
		if (msg->flags & MIPI_DSI_MSG_USE_LPM)
			word |= CMD_TX_MODE;
		dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_CRI_TX_HDR, word);

		if (dsi2h->packet_stack_mode)
			dsi2h->mipi_fifo_hdr_used++;
	}

	tx_buf = packet.payload;
	len = packet.payload_length;
	/* Step 1 - Write CRI TX Payload */
	while (len) {
		if (len < pld_data_bytes) {
			word = 0;
			memcpy(&word, tx_buf, len);
			dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_CRI_TX_PLD, le32_to_cpu(word));

			if (dsi2h->packet_stack_mode)
				dsi2h->mipi_fifo_pld_used++;

			len = 0;
		} else {
			memcpy(&word, tx_buf, pld_data_bytes);
			dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_CRI_TX_PLD, le32_to_cpu(word));

			if (dsi2h->packet_stack_mode)
				dsi2h->mipi_fifo_pld_used++;

			tx_buf += pld_data_bytes;
			len -= pld_data_bytes;
		}
	}

	/* Step 2 - Write CRI TX Header */
	memcpy(&word, &packet.header, sizeof(packet.header));
	/* Configures the transmission mode of the packet: 0 - High-speed, 1 - Low-power */
	if (msg->flags & MIPI_DSI_MSG_USE_LPM)
		word |= CMD_TX_MODE;
	/* Configures the packet as a read request command */
	if (msg->rx_len)
		word |= CMD_HDR_RD;
	/* Configures the packet as a long packet */
	if (mipi_dsi_packet_format_is_long(msg->type))
		word |= CMD_HDR_LONG;

	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_CRI_TX_HDR, word);
	if (dsi2h->packet_stack_mode)
		dsi2h->mipi_fifo_hdr_used++;

	if (msg->rx_len)
		ret = dw_mipi_dsi2h_cri_read(dsi2h, msg);
	else
		ret = msg->tx_len;

	return ret;
}

static void dw_dsi2h_delay_ulps_handler(struct kthread_work *work)
{
	struct dw_mipi_dsi2h *dsi2h = container_of(work,
		struct dw_mipi_dsi2h, ulps_dwork.work);
	int ret;

	ret = pm_runtime_get_if_in_use(dsi2h->dev);
	if (ret == 0) {	/* dsi2h device not in active state */
		return;
	} else if (unlikely(ret < 0)) {
		dev_err(dsi2h->dev, "%s: failed to runtime resume get, ret = %d\n", __func__, ret);
		return;
	}

	mutex_lock(&dsi2h->dsi2h_lock);
	if (dsi2h->state != DSI2H_STATE_PENDING_ULPS)
		goto exit;

	ret = dw_mipi_dsi2h_enter_ulps(dsi2h);
	if (unlikely(ret))
		dev_err(dsi2h->dev, "%s: failed to enter ulps\n", __func__);

exit:
	mutex_unlock(&dsi2h->dsi2h_lock);

	pm_runtime_mark_last_busy(dsi2h->dev);
	ret = pm_runtime_put_autosuspend(dsi2h->dev);
	if (ret < 0)
		dev_WARN(dsi2h->dev, "%s: failed to runtime put autosuspend, ret = %d\n", __func__,
			 ret);
}

static int _dw_mipi_dsi2h_host_transfer(struct dw_mipi_dsi2h *dsi2h, const struct mipi_dsi_msg *msg)
{
	ssize_t num_bytes_transferred = 0;
	int ret;

	if (!dsi2h->packet_stack_mode) {
		u32 core_status;

		core_status = dw_dsi2h_wait_cri_not_busy(dsi2h);
		if (unlikely(core_status)) {
			dev_err(dsi2h->dev, "CRI Busy! status = %x, %d\n", core_status, __LINE__);
			return -EBUSY;
		}
	}

	if (!dsi2h->packet_stack_mode && (msg->flags & GS_DSI_MSG_QUEUE))
		dw_mipi_dsi2h_host_packet_stack_enable(dsi2h);

	if (dsi2h->packet_stack_mode && msg->rx_len > 0) {
		dev_warn(dsi2h->dev, "Received a read request in packet stack mode\n");
		ret = dw_mipi_dsi2h_host_packet_stack_flush(dsi2h);
		if (ret)
			return ret;
	}

	num_bytes_transferred = dw_mipi_dsi2h_host_write_msg(dsi2h, msg);
	if (num_bytes_transferred < 0) {
		dw_mipi_dsi2h_host_packet_stack_flush(dsi2h);
		return num_bytes_transferred;
	}

	if (dsi2h->packet_stack_mode) {
		bool is_last = false;

		is_last = !(msg->flags & GS_DSI_MSG_QUEUE);
		is_last |= (dsi2h->mipi_fifo_hdr_used > MIPI_FIFO_HDR_SIZE_ALMOST_FULL);
		is_last |= (dsi2h->mipi_fifo_pld_used > MIPI_FIFO_PLD_SIZE_ALMOST_FULL);

		if (is_last) {
			ret = dw_mipi_dsi2h_host_packet_stack_flush(dsi2h);
			if (ret)
				return ret;
		}
	}

	return num_bytes_transferred;
}

static ssize_t dw_mipi_dsi2h_host_transfer(struct mipi_dsi_host *host,
					   const struct mipi_dsi_msg *msg)
{
	struct dw_mipi_dsi2h *dsi2h = host_to_dsi2h(host);
	int ret;
	unsigned int dsi2h_pre_state;

	if (msg->rx_len != 0 && msg->flags != 0) {
		dev_err(dsi2h->dev, "No flags allowed with read msg [0x%x]\n", msg->flags);
		return -EINVAL;
	}

	if (!dsi2h->enabled) {
		dev_err(dsi2h->dev, "Unable to transfer cmd. DSI is not enabled\n");
		return -EPERM;
	}

	ret = pm_runtime_resume_and_get(dsi2h->dev);
	if (ret) {
		dev_err(dsi2h->dev, "Unable to transfer cmd. Runtime resume failed (%d)\n", ret);
		return ret;
	}

	mutex_lock(&dsi2h->dsi2h_lock);
	dsi2h_pre_state = dsi2h->state;
	if (dsi2h_pre_state == DSI2H_STATE_ULPS) {
		dw_mipi_dsi2h_exit_ulps(dsi2h);
	} else if (dsi2h_pre_state == DSI2H_STATE_SUSPEND) {
		dev_info(dsi2h->dev, "DSI suspended during transfer, enabling...\n");
		ret = dw_mipi_dsi2h_enable(dsi2h);
		if (ret < 0) {
			dev_err(dsi2h->dev, "%s Failed to enable DSI from suspend (%d)\n",
				__func__, ret);
			goto EXIT;
		}
	}

	ret = _dw_mipi_dsi2h_host_transfer(dsi2h, msg);
	if (ret < 0)
		dev_err(dsi2h->dev, "DSI message transfer failed: %d\n", ret);

	if (msg->rx_len > 0 && dsi2h->suppress_dsi_errors) {
		unsigned long irq_flags;

		spin_lock_irqsave(&dsi2h->spinlock_dsi, irq_flags);
		if (dsi2h->suppress_dsi_errors) {
			dev_dbg(dsi2h->dev,
				"Terminated DSI error suppression window after first read\n");
			dsi2h->suppress_dsi_errors = false;
		}
		spin_unlock_irqrestore(&dsi2h->spinlock_dsi, irq_flags);
	}

	if (dsi2h_pre_state == DSI2H_STATE_ULPS || dsi2h_pre_state == DSI2H_STATE_PENDING_ULPS ||
	    dsi2h_pre_state == DSI2H_STATE_SUSPEND) {
		dsi2h->state = DSI2H_STATE_PENDING_ULPS;
		kthread_mod_delayed_work(&dsi2h->dsi2h_worker, &dsi2h->ulps_dwork,
			msecs_to_jiffies(DSI2H_ULPS_DWORK_MS));
	}

EXIT:
	pm_runtime_mark_last_busy(dsi2h->dev);
	pm_runtime_put_autosuspend(dsi2h->dev);
	mutex_unlock(&dsi2h->dsi2h_lock);

	return ret;
}

static const struct mipi_dsi_host_ops dw_mipi_dsi2h_host_ops = {
	.attach = dw_mipi_dsi2h_host_attach,
	.detach = dw_mipi_dsi2h_host_detach,
	.transfer = dw_mipi_dsi2h_host_transfer,
};

void dw_dsi2h_update(struct dw_mipi_dsi2h *dsi2h)
{
	u32 prev_scramb = dw_dsi2h_read_field(dsi2h->field_scrambling_en);
	struct mipi_dsi_msg msg = {0};

	dw_dsi2h_write_field(dsi2h->field_hs_transferen_en, dsi2h->high_speed);
	dw_dsi2h_write_field(dsi2h->field_bta_en, dsi2h->bta);
	dw_dsi2h_write_field(dsi2h->field_eotp_tx_en, dsi2h->eotp);
	dw_dsi2h_write_field(dsi2h->field_auto_tear_bta_disable, dsi2h->tear_effect);
	dw_dsi2h_write_field(dsi2h->field_scrambling_en, dsi2h->scrambling);
	dw_dsi2h_write_field(dsi2h->field_hib_type, dsi2h->hib_type);

	if (prev_scramb != dsi2h->scrambling) {
		msg.type = MIPI_DSI_SCRAMBLING_MODE_COMMAND;
		msg.tx_len = 1;
		msg.tx_buf = (u8 *)&dsi2h->scrambling;
		dw_mipi_dsi2h_host_transfer(&dsi2h->dsi_host, &msg);
	}
}

static void
dw_mipi_dsi2h_bridge_mode_set(struct drm_bridge *bridge,
			      const struct drm_display_mode *mode,
			      const struct drm_display_mode *adjusted_mode)
{
	struct dw_mipi_dsi2h *dsi2h = bridge_to_dsi(bridge);
	struct drm_bridge_state *bridge_state = drm_priv_to_bridge_state(bridge->base.state);
	struct dsi2h_bridge_state *dsi2h_state = to_dsi2h_bridge_state(bridge_state);

	memcpy(&dsi2h->cfg, &dsi2h_state->cfg, sizeof(dsi2h->cfg));
	memcpy(&dsi2h->mode, adjusted_mode, sizeof(dsi2h->mode));

	drm_mode_debug_printmodeline(&dsi2h->mode);
}

static u32 div_to_fixed_point(u32 numerator, u32 denominator,
			      u32 fixed_point_res, u32 mask)
{
	return mask & (((u64)numerator * (1 << fixed_point_res)) / denominator);
}

static void dw_dsi2h_main_config(struct dw_mipi_dsi2h *dsi2h)
{
	/* Step 1 - Timeouts Configuration */
	/* Value zero turns the timeouts off */
	dw_dsi2h_write_field(dsi2h->field_to_hstx_value, 0x0);	/* disable HSTX TO */
	dw_dsi2h_write_field(dsi2h->field_to_hstxrdy_value, 0xFFFF);
	dw_dsi2h_write_field(dsi2h->field_to_lprx_value, 0xFFFF);
	dw_dsi2h_write_field(dsi2h->field_to_lptxrdy_value, 0xFFFF);
	dw_dsi2h_write_field(dsi2h->field_to_lptxtrig_value, 0xFFFF);
	dw_dsi2h_write_field(dsi2h->field_to_lptxulps_value, 0xFFFF);
	dw_dsi2h_write_field(dsi2h->field_to_bta_value, 0xFFFF);

	/* Step 2 - Manual/Auto Mode Configuration*/
	dw_dsi2h_write_field(dsi2h->field_manual_mode_en, dsi2h->auto_calc_off);
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int dw_dsi2h_start_tear_effect(struct dw_mipi_dsi2h *dsi2h, u32 sw_type, u32 set_tear_on,
				      u32 arg)
{
	struct mipi_dsi_msg msg;
	u8 payload_tx[64] = { 0 };
	u8 payload_rx[64] = { 0 };

	if (sw_type) {
		msg.channel = 0;
		msg.tx_buf = &payload_tx;
		msg.rx_buf = &payload_rx;
		msg.rx_len = 0;
		msg.flags = 0;

		if (set_tear_on) {
			/* Set tear on DCS command*/
			msg.type = 0x15;
			payload_tx[0] = 0x35;
			payload_tx[1] = arg & 0xFF;
			msg.tx_len = 2;
		} else {
			/* Set tear scanline DCScommand*/
			msg.type = 0x39;
			payload_tx[0] = 0x44;
			payload_tx[1] = arg & 0xFF;
			payload_tx[2] = (arg >> 8) & 0xFF;
			msg.tx_len = 3;
		}

		/* Send command */
		dw_mipi_dsi2h_host_transfer(&dsi2h->dsi_host, &msg);
	} else {
		if (set_tear_on) {
			dw_dsi2h_write_field(dsi2h->field_te_type_hw,
					     0); /* Set TE type for set_tear_on */

			dw_dsi2h_write_field(dsi2h->field_set_tear_on_args_hw, arg);
		} else {
			dw_dsi2h_write_field(dsi2h->field_te_type_hw,
					     1); /* Set TE type for set_tear_scanline */

			dw_dsi2h_write_field(dsi2h->field_set_tear_scanline_args_hw, arg);
		}
	}
	return 0;
}

static int dw_dsi2h_finish_tear_effect(struct dw_mipi_dsi2h *dsi2h)
{
	u32 reg;
	u32 core_status;

	/* Check if auto bta is enabled, if not, do it manually*/
	if (dw_dsi2h_read_field(dsi2h->field_auto_tear_bta_disable)) {
		/* Automatic tear effect BTA disabled, lets trigger it manually */
		core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
		if (unlikely(core_status)) {
			dev_err(dsi2h->dev, "PRI Busy! status = %x\n", core_status);
			return -EBUSY;
		}

		dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_PRI_TX_CMD, PHY_BTA);

		core_status = dw_dsi2h_wait_pri_not_busy(dsi2h);
		if (unlikely(core_status)) {
			dev_err(dsi2h->dev,
				"PRI didn't leave busy state after sending request! status = %x\n",
				core_status);
			return -EAGAIN;
		}
	}

	/* Check if acknowledge was received*/
	reg = dw_dsi2h_pri_receive_triggers(dsi2h);

	if (reg & PHY_RX_TRIGGER_2) {
		dev_info(dsi2h->dev,
			 "PHY RX Trigger 2 received, an acknowledge event has been received");
	}

	if (reg & PHY_RX_TRIGGER_1) {
		dev_info(dsi2h->dev,
			 "PHY RX Trigger 1 received, a tearing effect event has been received");
	}

	return 0;
}
#endif /* CONFIG_DEBUG_FS */

static void dw_dsi2h_phy_config(struct dw_mipi_dsi2h *dsi2h)
{
	u32 lptx_clk_div;

	/* Step 1 - PHY Type Selection */
	if (dsi2h->is_cphy)
		dw_dsi2h_write_field(dsi2h->field_phy_type, 1);
	else
		dw_dsi2h_write_field(dsi2h->field_phy_type, 0);

	/* Step 2 - Number of lanes Configuration */
	dw_dsi2h_write_field(dsi2h->field_phy_lanes, dsi2h->cfg.lanes - 1);

	/* Step 3 - PPI width Configuration */
	dw_dsi2h_write_field(dsi2h->field_ppi_width, dsi2h->ppi_width);

	/* Step 4 - High Speed Transfer Enable Configuration */
	dw_dsi2h_write_field(dsi2h->field_hs_transferen_en, dsi2h->high_speed);

	/* Step 5 - Configure the Clock Mode */
	dw_dsi2h_write_field(dsi2h->field_clk_type, dsi2h->clk_type);

	/* Step 6 - Configuration the PHY Clock Low-Power */
	/* lptx_clk = sys_clk / (lptx_clk_div * 2) */
	lptx_clk_div = dsi2h->sys_clk / dsi2h->lptx_clk / 2;
	lptx_clk_div = clamp(lptx_clk_div, DW_DSI2H_PHY_CLK_CFG_LPTX_CLK_DIV_MIN,
			     DW_DSI2H_PHY_CLK_CFG_LPTX_CLK_DIV_MAX);
	pr_debug("%s config lptx_clk_div to %d\n", __func__, lptx_clk_div);
	dw_dsi2h_write_field(dsi2h->field_phy_lptx_clk_div, lptx_clk_div);

	if (dsi2h->auto_calc_off) {
		u32 res = 0;

		/* Step 7 - PHY/IPI Clock Ratio Configuration */
		res = div_to_fixed_point(dsi2h->phy_hstx_clk, dsi2h->ipi_clk,
					 16, 0x3FFFFF);
		dw_dsi2h_write_field(dsi2h->field_phy_ipi_ratio, res);

		/* Step 8 - PHY/SYS Clock Ratio Configuration */
		res = div_to_fixed_point(dsi2h->phy_hstx_clk, dsi2h->sys_clk,
					 16, 0x3ffff);
		dw_dsi2h_write_field(dsi2h->field_phy_sys_ratio, res);

		/* Step 9 - PHY Transition Timings Configuration */
		dw_dsi2h_write_field(dsi2h->field_phy_hs2lp_time, dsi2h->hs2lp_time);
		dw_dsi2h_write_field(dsi2h->field_phy_lp2hs_time, dsi2h->lp2hs_time);

		/* Step 10 - PHY Maximum Read Time Configuration */
		/* Should be computed according panel specification */
		/* TODO */
		/* cycles of phy_hstx_clk */
		dw_dsi2h_write_field(dsi2h->field_phy_max_rd_time, 8 * dsi2h->phy_hstx_clk / 1000);

		/* Step 11 - PHY ESC Command Time Configuration */
		/* TODO */
		/* Get the way to calculate this */
		dw_dsi2h_write_field(dsi2h->field_phy_esc_cmd_time, 8645076);

		/* Step 12 - PHY Byte Transmission Time Configuration */
		dw_dsi2h_write_field(dsi2h->field_phy_esc_byte_time, 4715496);
	}
}

static void dw_dsi_config(struct dw_mipi_dsi2h *dsi2h)
{
	u32 sys_clk, div, cycles;

	/* Step 1 - DSI2 General Configuration */
	dw_dsi2h_write_field(dsi2h->field_bta_en, dsi2h->bta);
	dw_dsi2h_write_field(dsi2h->field_eotp_tx_en, dsi2h->eotp);

	/* Step 2 - DSI2 Virtual Channel Configuration */
	dw_dsi2h_write_field(dsi2h->field_tx_vcid, dsi2h->cfg.channel);

	/* Step 3 - DSI2 Scrambling Configuration */
	dw_dsi2h_write_field(dsi2h->field_scrambling_seed, 0x0);
	dw_dsi2h_write_field(dsi2h->field_scrambling_en, dsi2h->scrambling);

	/* Step 4 - DSI2 Video Transmission Configuration */
	dw_dsi2h_write_field(dsi2h->field_vid_mode_type, dsi2h->vid_mode_type);
	dw_dsi2h_write_field(dsi2h->field_blk_vfp_hs_en, dsi2h->vfp_hs_en);
	dw_dsi2h_write_field(dsi2h->field_blk_vbp_hs_en, dsi2h->vbp_hs_en);
	dw_dsi2h_write_field(dsi2h->field_blk_vsa_hs_en, dsi2h->vsa_hs_en);
	dw_dsi2h_write_field(dsi2h->field_blk_hfp_hs_en, dsi2h->hfp_hs_en);
	dw_dsi2h_write_field(dsi2h->field_blk_hbp_hs_en, dsi2h->hbp_hs_en);
	dw_dsi2h_write_field(dsi2h->field_blk_hsa_hs_en, dsi2h->hsa_hs_en);
	dw_dsi2h_write_field(dsi2h->field_lpdt_display_cmd_en, 0x0);

	/* Step 5 - Maximum Return Packet Size Configuration */
	dw_dsi2h_write_field(dsi2h->field_max_rt_pkt_sz, 1000); // 500

	/* Step 6 - Automatic Tear Effect Configuration */
	dw_dsi2h_write_field(dsi2h->field_auto_tear_bta_disable, dsi2h->tear_effect);
	if (dsi2h->hs_link_enable)
		dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_DSI_DS_TX_CFG, 1);

	/* Step 7 - IPI Hibernate Configuration */
	dw_dsi2h_write_field(dsi2h->field_hib_type, dsi2h->hib_type);
	if (dsi2h->ulps_wakeup_time) {
		sys_clk = dsi2h->sys_clk;
		div = 2 * dw_dsi2h_read_field(dsi2h->field_phy_lptx_clk_div);
		cycles = (div == 0) ? 0 : (sys_clk * dsi2h->ulps_wakeup_time) / div;
	} else {
		cycles = 0;
	}
	dw_dsi2h_write_field(dsi2h->field_hib_ulps_wakeup_time, cycles);
}

/* get the Data Unit value for a given value  */
static u32 dw_dsi2h_get_du_value(u32 ipi_clk, u32 phy_hstx_clk, u32 value)
{
	u32 res;

	res = (value >> 2) * ((phy_hstx_clk * (1ULL << 16)) / ipi_clk);

	return res;
}

static void dw_ipi_config(struct dw_mipi_dsi2h *dsi2h)
{
	u32 res;
	struct videomode vm;

	drm_display_mode_to_videomode(&dsi2h->mode, &vm);

	/* Step 1 - Pixel Packet Size Configuration */
	if (dsi2h->mode_ctrl == DATA_STREAM_MODE)
		dw_dsi2h_write_field(dsi2h->field_max_pix_pkt, vm.hactive);
	else
		dw_dsi2h_write_field(dsi2h->field_max_pix_pkt, 0);

	if (!dsi2h->auto_calc_off)
		return;

	/* Step 2 - Color Coding Configuration */
	dw_dsi2h_write_field(dsi2h->field_ipi_format, dsi2h->cfg.color_format.format);
	dw_dsi2h_write_field(dsi2h->field_ipi_depth, dsi2h->cfg.color_format.depth);
	/* Data Stream does not need this configuration */
	if (dsi2h->cfg.mode_flags & MIPI_DSI_MODE_VIDEO) {
		/* Step 3 - Horizontal Sync Active Configuration */
		res = dw_dsi2h_get_du_value(dsi2h->ipi_clk, dsi2h->phy_hstx_clk,
					    vm.hsync_len);
		dw_dsi2h_write_field(dsi2h->field_vid_hsa_time, res);

		/* Step 4 - Horizontal Back Porch Configuration */
		res = dw_dsi2h_get_du_value(dsi2h->ipi_clk, dsi2h->phy_hstx_clk,
					    vm.hback_porch);
		dw_dsi2h_write_field(dsi2h->field_vid_hbp_time, res);

		/* Step 5 - Horizontal Active Time Configuration */
		res = dw_dsi2h_get_du_value(dsi2h->ipi_clk, dsi2h->phy_hstx_clk,
					    vm.hactive);
		dw_dsi2h_write_field(dsi2h->field_vid_hact_time, res);

		/* Step 6 - Horizontal Line Time Configuration */
		res = dw_dsi2h_get_du_value(dsi2h->ipi_clk, dsi2h->phy_hstx_clk,
					    dsi2h->mode.htotal);
		dw_dsi2h_write_field(dsi2h->field_vid_hline_time, res);

		/* Step 7 - Vertical Sync Active Configuration */
		dw_dsi2h_write_field(dsi2h->field_vid_vsa_lines, vm.vsync_len);

		/* Step 8 - Vertical Back Porch Configuration */
		dw_dsi2h_write_field(dsi2h->field_vid_vbp_lines, vm.vback_porch);

		/* Step 9 - Vertical Active Configuration */
		dw_dsi2h_write_field(dsi2h->field_vid_vact_lines, vm.vactive);

		/* Step 10 - Vertical Front Porch Configuration */
		dw_dsi2h_write_field(dsi2h->field_vid_vfp_lines, vm.vfront_porch);
	}
}

static void dw_irq_status(struct dw_mipi_dsi2h *dsi2h)
{
	struct device *dev = dsi2h->dev;
	u32 val_main = 0;
	u32 val_spec = 0;

	/* Step 1 - Read INT_ST_MAIN */
	val_main = dw_dsi2h_read_reg(dsi2h->regs, DW_DSI2H_INT_ST_MAIN);

	/* Step 2 - Read INT_ST_<group> */
	/* int_st_cri */
	if (val_main & BIT(5)) {
		val_spec = dw_dsi2h_read_reg(dsi2h->regs, DW_DSI2H_INT_ST_CRI);
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_cri_cmd_time, 0, NO_RSTN,
			      "CRI request ignored due to timing error (Video Mode only)\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_cri_dtype, 1, NO_RSTN,
			      "CRI received packet header invalid data type\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_cri_vchannel, 2, NO_RSTN,
			      "CRI received packet header invalid virtual channel\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_cri_rx_length, 3, HARD_RSTN,
			      "CRI received packet and invalid length\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_cri_ecc, 4, NO_RSTN,
			      "CRI received packet header with single ECC error\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_cri_ecc_fatal, 5, NO_RSTN,
			      "CRI received packet header with multiple ECC errors\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_cri_crc, 6, NO_RSTN,
			      "CRI received long packet payload with CRC error\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_cmd_rd_pld_fifo_over, 16, NO_RSTN,
			      "DSI2-HOST reports this error when write operation is performed but Command Read Payload FIFO is full\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_cmd_rd_pld_fifo_under, 17, NO_RSTN,
			      "DSI2-HOST reports this error when read operation is performed but Command Read Payload FIFO is empty\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_cmd_wr_pld_fifo_over, 18, NO_RSTN,
			      "DSI2-HOST reports this error when write operation is performed but Command Write Payload FIFO is full\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_cmd_wr_pld_fifo_under, 19, NO_RSTN,
			      "DSI2-HOST reports this error when read operation is performed but Command Write Payload FIFO is empty\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_cmd_wr_hdr_fifo_over, 20, NO_RSTN,
			      "DSI2-HOST reports this error when write operation is performed but Command Write Header FIFO is full.\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_cmd_wr_hdr_fifo_under, 21, NO_RSTN,
			      "DSI2-HOST reports this error when read operation is performed but Command Write Header FIFO is empty.\n");
	}

	/* int_st_pri */
	if (val_main & BIT(4)) {
		val_spec = dw_dsi2h_read_reg(dsi2h->regs, DW_DSI2H_INT_ST_PRI);
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_pri_tx_time, 0, NO_RSTN,
			      "PRI request ignored due to timing error (Video Mode only)\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_pri_tx_cmd, 1, NO_RSTN,
			      "PRI request can not be attended and will be discarded\n");
	}

	/* int_st_ipi */
	if (val_main & BIT(3)) {
		val_spec = dw_dsi2h_read_reg(dsi2h->regs, DW_DSI2H_INT_ST_IPI);
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_display_cmd_time, 0, NO_RSTN,
			      "Display command request ignored by the controller due to timing\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ipi_dtype, 1,  NO_RSTN,
			      "No DSI-2 data type is a direct match for the choice made by the pair ipi_format and ipi_depth\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_vid_bandwidth, 2, SYS_RSTN | HARD_RSTN,
			      "Video packet size exceeds remaining bandwidth of total HLINETIME\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ipi_cmd, 3, NO_RSTN,
			      "Display command can not be attended and will be discarded\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_display_cmd_ovfl, 8, NO_RSTN,
			      "Display command request ignored by the controller due to internal buffer overflow\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_ipi_event_fifo_over, 16, NO_RSTN,
			      "DSI2-HOST reports this error when write operation is performed but IPI event FIFO is full\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_ipi_event_fifo_under, 17, NO_RSTN,
			      "DSI2-HOST reports this error when read operation is performed but IPI event FIFO is empty.\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_ipi_pixel_fifo_over, 18, HARD_RSTN,
			      "DSI2-HOST reports this error when write operation is performed but IPI Pixel FIFO is full.\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_ipi_pixel_fifo_under, 19, NO_RSTN,
			      "DSI2-HOST reports this error when read operation is performed but IPI Pixel FIFO is empty.\n");
	}

	/* int_st_ack */
	if (val_main & BIT(2)) {
		val_spec = dw_dsi2h_read_reg(dsi2h->regs, DW_DSI2H_INT_ST_ACK);
		/* TODO(b/443148644): follow up after root cause analysis */
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_0, 0, NO_RSTN,
			      "RC Retrieves the SoT error bit from the Acknowledge error report\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_1, 1, NO_RSTN,
			      "RC Retrieves the SoT Sync error bit from the Acknowledge error report\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_2, 2, HARD_RSTN,
			      "RC Retrieves the EoT Sync error bit from the Acknowledge error report\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_3, 3, HARD_RSTN,
			      "RC Retrieves the Escape Mode Entry Command error bit from the Acknowledge error report\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_4, 4, HARD_RSTN,
			      "RC Retrieves the Low-Power Transmit Sync error bit from the Acknowledge error report\n");
		/* TODO(b/437202062): follow up after root cause analysis */
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_5, 5, NO_RSTN,
			      "RC Retrieves the Peripheral Timeout error bit from the Acknowledge error report\n");
		/* TODO(b/437202062): follow up after root cause analysis */
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_6, 6, NO_RSTN,
			      "RC Retrieves the False Control error bit from the Acknowledge error report\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_7, 7, PHY_RSTN | HARD_RSTN,
			      "RC Retrieves the Contention Detected error bit from the Acknowledge error report\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_8, 8, NO_RSTN,
			      "RC Retrieves the header ECC/SSDC/Checksum single-bit (detected and corrected) error bit from the Acknowledge error report\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_9, 9, NO_RSTN,
			      "RC Retrieves the header ECC/SSDC/Checksum multi-bit (detected, not corrected) error bit from the Acknowledge error report\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_10, 10, NO_RSTN,
			      "RC Retrieves the Payload Checksum error bit (long packet only) from the Acknowledge error report\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_11, 11, NO_RSTN,
			      "RC Retrieves the DSI Data Type not recognized error bit from the Acknowledge error report\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_12, 12, NO_RSTN,
			      "RC Retrieves the DSI VC ID Invalid error bit from the Acknowledge error report\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_13, 13, NO_RSTN,
			      "RC Retrieves the Invalid Transmission Length error bit from the Acknowledge error report\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_14, 14, NO_RSTN,
			      "RC Retrieves the Reserved (specific to device) error bit from the Acknowledge error report\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_ack_rpt_15, 15, NO_RSTN,
			      "RC Retrieves the DSI Protocol Violation error bit from the Acknowledge error report\n");
	}

	/* int_st_to */
	if (val_main & BIT(1)) {
		val_spec = dw_dsi2h_read_reg(dsi2h->regs, DW_DSI2H_INT_ST_TO);
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_to_hstx, 0, NO_RSTN,
			      "RC Indicates that a high-speed TX timeout has occurred\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_to_hstxrdy, 1, SYS_RSTN | HARD_RSTN,
			      "RC Indicates that a high-speed TX RDY timeout has occurred\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_to_lprx, 2, SYS_RSTN,
			      "RC Indicates that a low-power RX timeout has occurred\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_to_lptxrdy, 3, SYS_RSTN | HARD_RSTN,
			      "RC Indicates that a low-power TX DATA timeout has occurred\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_to_lptxtrig, 4, SYS_RSTN,
			      "RC Indicates that a low-power TX TRIGGER timeout has occurred\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_to_lptxulps, 5, SYS_RSTN,
			      "RC Indicates that a low-power TX ULPS timeout has occurred\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_err_to_bta, 6, SYS_RSTN | HARD_RSTN,
			      "RC Indicates that a bus turnaround direction timeout has occurred\n");
	}

	/* int_st_phy */
	if (val_main & BIT(0)) {
		val_spec = dw_dsi2h_read_reg(dsi2h->regs, DW_DSI2H_INT_ST_PHY);
		INT_BIT_CHECK(dsi2h, err, val_spec, int_phy_l0_erresc, 0, PHY_RSTN | HARD_RSTN,
			      "RC Escape Entry Error\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_phy_l0_errsyncesc, 1, PHY_RSTN | HARD_RSTN,
			      "RC Low-Power Data Transmission Synchronization Error\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_phy_l0_errcontrol, 2, PHY_RSTN | HARD_RSTN,
			      "RC Control Error\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_phy_l0_errcontentionlp0, 3,
			      PHY_RSTN | HARD_RSTN, "RC LP0 Contention Error\n");
		/* TODO(b/447004565): relax LP1 Contention due to MIPI OVR solution */
		INT_BIT_CHECK(dsi2h, info, val_spec, int_phy_l0_errcontentionlp1, 4, NO_RSTN,
			      "RC LP1 Contention occurred\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_txhs_fifo_over, 16, NO_RSTN,
			      "RC DSI2-HOST reports this error when write operation is performed but PHY High-Speed data FIFO is full\n");
		INT_BIT_CHECK(dsi2h, err, val_spec, int_txhs_fifo_under, 17, NO_RSTN,
			      "RC DSI2-HOST reports this error when read operation is performed but PHY High-Speed data FIFO is empty\n");
	}
}

/**
 * dw_dsi2h_suppress_errors_workaround() - Suppress DSI errors after handoff.
 * @dsi2h: DSI host structure.
 *
 * Clears errors detected immediately following bootloader handoff;
 * specifically, the first instance of Peripheral Timeout error and
 * DSI Protocol Violation errors that occur during the first dcs read.
 *
 * Reference: b/501228404
 */
static void dw_dsi2h_suppress_errors_workaround(struct dw_mipi_dsi2h *dsi2h)
{
	if (unlikely(dsi2h->suppress_dsi_errors)) {
		if (!(dsi2h->int_cntrs.cntr_int_err_ack_rpt_5 ||
		      dsi2h->int_cntrs.cntr_int_err_ack_rpt_15))
			return;

		dev_warn(dsi2h->dev,
			 "Suppressing errs (Peripheral Timeout:%llu DSI Protocol Violation:%llu)\n",
			 dsi2h->int_cntrs.cntr_int_err_ack_rpt_5,
			 dsi2h->int_cntrs.cntr_int_err_ack_rpt_15);
		dsi2h->int_cntrs.cntr_int_err_ack_rpt_5 = 0;
		dsi2h->int_cntrs.cntr_int_err_ack_rpt_15 = 0;

		dsi2h->suppress_dsi_errors = false;
	}
}
static void update_int_cntrs(struct dw_mipi_dsi2h *dsi2h, struct drm_connector_state *conn_state)
{
	unsigned long flags = 0;
	struct gs_drm_connector_state *gs_conn_state;

	if (!dsi2h || !conn_state)
		return;

	if (!dsi2h->enabled || (dsi2h->state != DSI2H_STATE_HS_EN))
		return;

	if (!is_gs_drm_connector(conn_state->connector))
		return;

	spin_lock_irqsave(&dsi2h->spinlock_dsi, flags);
	dw_irq_status(dsi2h);
	spin_unlock_irqrestore(&dsi2h->spinlock_dsi, flags);

	if (dsi2h->rstn)
		gs_fault_event_emit(dsi2h->dev, GS_FAULT_EVENT_TYPE_DSI_ERROR, dsi2h->rstn);

	gs_conn_state = to_gs_connector_state(conn_state);
	bitmap_write(gs_conn_state->dsi_errors, dsi2h->rstn, 0, GS_DSI_ERR_MAX);
	dsi2h->rstn = 0;
}

static irqreturn_t dw_irq_callback(int irq, void *data)
{
	struct dw_mipi_dsi2h *dsi2h = (struct dw_mipi_dsi2h *)data;
	unsigned long flags = 0;

	spin_lock_irqsave(&dsi2h->spinlock_dsi, flags);
	dw_irq_status(dsi2h);
	dw_dsi2h_suppress_errors_workaround(dsi2h);
	spin_unlock_irqrestore(&dsi2h->spinlock_dsi, flags);

	return IRQ_HANDLED;
}

static int dw_int_mask_config(struct dw_mipi_dsi2h *dsi2h)
{
	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_INT_MASK_PHY, 0x3001f);
	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_INT_MASK_TO, 0);
	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_INT_MASK_ACK, 0xffff);
	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_INT_MASK_IPI, 0xf010f);
	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_INT_MASK_PRI, 0x3);
	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_INT_MASK_CRI, 0x3f007f);

	return 0;
}

static int dw_opt_mode_select(struct dw_mipi_dsi2h *dsi2h,
			      enum dsi2h_operation_mode mode)
{
	u32 val = U32_MAX;

	/* Step 1 - Mode Request */
	dw_dsi2h_write_reg(dsi2h->regs, DW_DSI2H_MODE_CTRL, mode);

	/* Step 2 - Controller in requested mode? */
	regmap_read_poll_timeout(dsi2h->regs, DW_DSI2H_MODE_STATUS, val, (val == mode),
				 DW_DSI2H_REQUESTED_MODE_INTERVAL_US,
				 DW_DSI2H_REQUESTED_MODE_TIMEOUT_US);
	if (val != mode)
		return -EAGAIN;

	/*
	 * TODO(b/288132949): for AUTO_CALC, checking with SNPS to see if we could wait IDLE
	 * at the first regmap_read_poll_timeout call and detect the error if fails to set
	 * AUTO_CACL.

	 * If mode is AUTO_CALC, wait until status transits to idle mode
	 * Some code here to get a reasonable timeout for regmap_read_poll_timeout for
	 * emulator and sillicon. Simplify it after bringup.
	 */
	if (mode == AUTO_CALC_MODE) {
		u32 val = U32_MAX;
		ktime_t start;
		ktime_t end;
		s64 time_auto_calc_us;

		DPU_ATRACE_BEGIN("mode select auto calculate");
		start = ktime_get();
		regmap_read_poll_timeout(dsi2h->regs, DW_DSI2H_MODE_STATUS, val, (val == IDLE_MODE),
					 DW_DSI2H_AUTO_CALC_INTERVAL_US,
					 DW_DSI2H_AUTO_CALC_TIMEOUT_US /* 500ms covers emulator */);
		end = ktime_get();
		DPU_ATRACE_END("mode select auto calculate");
		time_auto_calc_us = ktime_us_delta(end, start);

		if (val != IDLE_MODE)
			pr_err("%s auto calc not quit after long wait\n", __func__);
		else
			pr_debug("%s auto calc done successfully in %lld us\n", __func__,
				time_auto_calc_us);
	}

	return 0;
}

/* Conversion table [mipi_dsi_pixel_format => DSI2h] */
static struct dsi2h_color_format dsi2h_formats[] = {
	{ F_RGB, D_8_BIT },		/* MIPI_DSI_FMT_RGB888 */
	// TODO: check the rgb666/_packed mapping
	{ F_RGB_L, D_6_BIT },		/* MIPI_DSI_FMT_RGB666 */
	{ F_RGB, D_6_BIT },		/* MIPI_DSI_FMT_RGB666_PACKED */
	{ F_RGB, D_565_BIT },		/* MIPI_DSI_FMT_RGB565 */
};

static enum dsi2h_ipi_depth dw_dsi2h_get_depth_from_dev_dsc(const struct mipi_dsi_device *dsi_dev)
{
	enum dsi2h_ipi_depth depth = D_8_BIT;

	if (!dsi_dev->dsc) {
		dev_WARN(&dsi_dev->dev, "device given without valid DSC, using default depth\n");
		return depth;
	}

	switch (dsi_dev->dsc->bits_per_component) {
	case 8:
		depth = D_8_BIT;
		break;
	case 10:
		depth = D_10_BIT;
		break;
	case 12:
		depth = D_12_BIT;
		break;
	default:
		dev_WARN(&dsi_dev->dev, "Unexpected DSC BPC: %u, setting default\n",
			dsi_dev->dsc->bits_per_component);
		break;
	}

	return depth;
}

static void update_gs_hs_clk(struct dw_mipi_dsi2h *dsi2h, struct drm_connector_state *conn_state)
{
	struct gs_drm_connector_state *gs_conn_state;

	if (!conn_state || !is_gs_drm_connector(conn_state->connector))
		return;

	if (dsi2h->trace_pid)
		DPU_ATRACE_INT_PID("dsi2h_datarate", dsi2h->datarate, dsi2h->trace_pid);

	gs_conn_state = to_gs_connector_state(conn_state);
	gs_conn_state->dsi_hs_clk_mbps = dsi2h->datarate;
	if (dsi2h->dynamic_hs_clk_en) {
		gs_conn_state->pending_dsi_hs_clk_mbps = dsi2h->pending_datarate;
		gs_conn_state->dsi_hs_clk_changed = dsi2h->datarate_changed;
	}
}

static int _dw_mipi_dsi2h_enable(struct dw_mipi_dsi2h *dsi2h)
{
	struct device *dev = dsi2h->dev;
	union phy_configure_opts *phy_opts = &dsi2h->cfg.phy_opts;
	int ret;

	dw_dsi2h_import_device(dsi2h, dsi2h->dsi_device);

	dev_dbg(dsi2h->dev, "%s: state=%d, mode flags=%#lx, mode ctrl=%d\n", __func__, dsi2h->state,
		dsi2h->cfg.mode_flags, dsi2h->mode_ctrl);

	if (!(dsi2h->cfg.mode_flags & MIPI_DSI_MODE_VIDEO)) {
		dsi2h->mode_ctrl = DATA_STREAM_MODE;
		dsi2h->auto_calc_off = true;
	} else {
		dsi2h->mode_ctrl = VIDEO_MODE;
	}

	if (!dsi2h->plat_data->in_emulation) {
		dsi2h->phy_hstx_clk = dsi2h->datarate * 1000;
		if (!dsi2h->is_cphy) {
			/*
			 * DPHY clock frequency depends on data bus width and datarate
			 * 8bits data bus : datarate/8
			 * 16bits data bus: datarate/16
			 * 32bits data bus: datarate/32
			 */
			if (dsi2h->ppi_width == DSI2H_DPHY_32_BITS)
				dsi2h->phy_hstx_clk /= 32;
			else if (dsi2h->ppi_width == DSI2H_DPHY_16_BITS)
				dsi2h->phy_hstx_clk /= 16;
			else
				dsi2h->phy_hstx_clk /= 8;
		} else {
			/*
			 * CPHY
			 * 32bits data bus: datarate/14
			 * 16bits data bus: datarate/7
			 */
			if (dsi2h->ppi_width == 2)
				dsi2h->phy_hstx_clk /= 14;
			else
				dsi2h->phy_hstx_clk /= 7;
		}
	}
	/* Step 1 - Reset APB Interface - Done by HW */

	/* Step 2 - MAIN Configuration */
	dw_dsi2h_main_config(dsi2h);

	/* Step 3 - PHY Configuration */
	dw_dsi2h_phy_config(dsi2h);

	/* Step 4 - DSI Configuration */
	dw_dsi_config(dsi2h);

	/* Step 5 - IPI Configuration */
	dw_ipi_config(dsi2h);

	/* Step 6 - Interrupt Masks Configuration */
	dw_int_mask_config(dsi2h);

	/* no runtime phy mode change */

	phy_set_speed(dsi2h->phy, dsi2h->datarate);

	/* Step 7 - PHY Initialization */
	ret = phy_init(dsi2h->phy);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to init DSI phy: %d\n", ret);
		return ret;
	}

	ret = phy_configure(dsi2h->phy, phy_opts);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to configure DSI phy: %d\n", ret);
		return ret;
	}

	/* Step 8 - Wake-Up Core */
	dw_dsi2h_write_field(dsi2h->field_pwr_up, 0x1);

	ret = phy_power_on(dsi2h->phy);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi2h->dev,
			      "Failed to power on DPHY (%d)\n", ret);
		return ret;
	}

	if (!dsi2h->auto_calc_off) {
		/* Step 9 - Auto-Calculation Mode */
		ret = dw_opt_mode_select(dsi2h, AUTO_CALC_MODE);
		if (ret)
			return ret;
	}

	/* Step 10 - Operating Mode Selection */
	/* TODO */
	/* When doing interop we need to go first to Command mode */
	/* For panel and device init */
	return dw_opt_mode_select(dsi2h, dsi2h->mode_ctrl);
}

static int dw_mipi_dsi2h_enable(struct dw_mipi_dsi2h *dsi2h)
{
	int ret;

	lockdep_assert_held(&dsi2h->dsi2h_lock);

	if (dsi2h->state == DSI2H_STATE_HS_EN) {
		dev_dbg(dsi2h->dev, "%s: already in hs state\n", __func__);
		return 0;
	}

	ret = _dw_mipi_dsi2h_enable(dsi2h);
	dsi2h->state = DSI2H_STATE_HS_EN;

	return ret;
}

int dw_mipi_dsi2h_disable(struct dw_mipi_dsi2h *dsi2h)
{
	int ret;

	lockdep_assert_held(&dsi2h->dsi2h_lock);

	if (dsi2h->state == DSI2H_STATE_SUSPEND) {
		dev_dbg(dsi2h->dev, "%s: already in suspend state\n", __func__);
		return 0;
	}
	dev_dbg(dsi2h->dev, "%s: state=%d\n", __func__, dsi2h->state);

	if (dsi2h->pending_datarate && !dsi2h->dynamic_hs_clk_en) {
		dev_dbg(dsi2h->dev, "assign pending datarate %u for off\n",
			dsi2h->pending_datarate);
		dsi2h->datarate = dsi2h->pending_datarate;
		dsi2h->pending_datarate = 0;
		phy_set_speed(dsi2h->phy, dsi2h->datarate);
	}

	/* wait for the core to be idle */
	ret = dw_dsi2h_wait_core_idle(dsi2h);
	if (ret)
		dev_err(dsi2h->dev, "DSI core not idle, status = %x\n", ret);

	phy_power_off(dsi2h->phy);
	phy_exit(dsi2h->phy);
	dsi2h->state = DSI2H_STATE_SUSPEND;
	dsi2h->rstn = 0;

	return ret;
}

int dw_mipi_dsi2h_resume(void *handle)
{
	struct dw_mipi_dsi2h *dsi2h = handle;
	int ret = 0;
	bool pending_ulps;

	mutex_lock(&dsi2h->dsi2h_lock);
	dev_dbg(dsi2h->dev, "%s: enabled=%d state=%d\n", __func__, dsi2h->enabled, dsi2h->state);
	if ((dsi2h->pm_ver == 0) && dsi2h->enabled) {
		pending_ulps = (dsi2h->state == DSI2H_STATE_PENDING_ULPS);

		ret = dw_mipi_dsi2h_enable(dsi2h);
		if (unlikely(ret))
			dev_err(dsi2h->dev, "%s: failed to enable DSI\n", __func__);

		if (pending_ulps) {
			ret = dw_mipi_dsi2h_enter_ulps(dsi2h);
			if (unlikely(ret))
				dev_err(dsi2h->dev, "%s: failed to enter ulps\n", __func__);
		}
	}
	mutex_unlock(&dsi2h->dsi2h_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(dw_mipi_dsi2h_resume);

int dw_mipi_dsi2h_suspend(void *handle)
{
	struct dw_mipi_dsi2h *dsi2h = handle;
	int ret = 0;

	/* cancel and confirm all delay ulps works are done */
	kthread_cancel_delayed_work_sync(&dsi2h->ulps_dwork);

	mutex_lock(&dsi2h->dsi2h_lock);
	dev_dbg(dsi2h->dev, "%s: enabled=%d state=%d\n", __func__, dsi2h->enabled, dsi2h->state);

	/* update pm_runtime logic */
	if (pm_runtime_suspend_version == 1) {
		/* MIPI PHY enable override */
		phy_set_mode_ext(dsi2h->phy, PHY_MODE_MIPI_DPHY, DW_MIPI_CDPHY_OP_OVR_LANE_ENABLE);
		dsi2h->pm_ver = 1;
	} else {
		/* MIPI PHY disable override */
		phy_set_mode_ext(dsi2h->phy, PHY_MODE_MIPI_DPHY, DW_MIPI_CDPHY_OP_OVR_LANE_DISABLE);
		dsi2h->pm_ver = 0;
	}

	/* handle case that delay ulps worker does not switch DSI to ulps state. */
	if (unlikely(dsi2h->state == DSI2H_STATE_PENDING_ULPS))
		dw_mipi_dsi2h_enter_ulps(dsi2h);

	if (dsi2h->state == DSI2H_STATE_HS_EN) {
		ret = dw_mipi_dsi2h_disable(dsi2h);
	} else if (dsi2h->state == DSI2H_STATE_ULPS) {
		/* override MIPI lane output to LP00 before exit ulps */
		if (dsi2h->pm_ver == 1)
			phy_set_mode_ext(dsi2h->phy, PHY_MODE_MIPI_DPHY, DW_MIPI_CDPHY_OP_OVR_LANE_LP00);
		/* exit ULPS first, then power off. */
		ret = dw_mipi_dsi2h_exit_ulps(dsi2h);
		if (unlikely(ret < 0))
			dev_err(dsi2h->dev, "%s: failed to exit ulps\n", __func__);

		ret = dw_mipi_dsi2h_disable(dsi2h);
		if (unlikely(ret < 0))
			dev_err(dsi2h->dev, "%s: failed to disable DSI\n", __func__);
		/* set MIPI PHY in ULPS state after override enabled */
		if (dsi2h->pm_ver == 1) /* set MIPI PHY in ULPS state after override enabled */
			phy_set_mode_ext(dsi2h->phy, PHY_MODE_MIPI_DPHY,
				 DW_MIPI_CDPHY_OP_OVR_PHY_ULPS_STATE);
		else
			dsi2h->state = DSI2H_STATE_PENDING_ULPS;
	}
	mutex_unlock(&dsi2h->dsi2h_lock);

	/* return non-zero value will abort runtime_suspend() flow. */
	return 0;
}
EXPORT_SYMBOL_GPL(dw_mipi_dsi2h_suspend);

static void dw_dsi2h_import_device_config(struct dw_mipi_dsi2h_config *cfg,
			    struct mipi_dsi_device *dsi_dev)
{
	if (dsi_dev->dsc == NULL) {
		cfg->color_format = dsi2h_formats[dsi_dev->format];
	} else {
		cfg->color_format.format = F_COMPRESSED;
		cfg->color_format.depth = dw_dsi2h_get_depth_from_dev_dsc(dsi_dev);
	}

	cfg->lanes = dsi_dev->lanes;
	cfg->channel = dsi_dev->channel;
	cfg->mode_flags = dsi_dev->mode_flags;
}

void dw_dsi2h_import_device(struct dw_mipi_dsi2h *dsi2h,
			    struct mipi_dsi_device *dsi_dev)
{
	dw_dsi2h_import_device_config(&dsi2h->cfg, dsi_dev);
}

static bool is_active_modeset(struct drm_bridge *bridge, struct drm_bridge_state *bridge_state)
{
	struct dw_mipi_dsi2h *dsi2h = bridge_to_dsi(bridge);
	struct drm_atomic_state *state = bridge_state->base.state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	bool is_active_modeset = false;

	if (!bridge->encoder)
		return false;

	crtc = drm_atomic_get_new_crtc_for_encoder(state, bridge->encoder);
	if (!crtc)
		return false;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	if (!new_crtc_state)
		return false;

	is_active_modeset = (new_crtc_state->mode_changed && new_crtc_state->active &&
			     !new_crtc_state->active_changed);

	dev_dbg(dsi2h->dev, "active modeset:%d (mc:%d a:%d ac:%d)\n", is_active_modeset,
		new_crtc_state->mode_changed, new_crtc_state->active,
		new_crtc_state->active_changed);

	return is_active_modeset;
}

static bool is_seamless_modeset(struct drm_bridge *bridge,
				struct drm_bridge_state *bridge_state)
{
	struct dw_mipi_dsi2h *dsi2h = bridge_to_dsi(bridge);
	struct dsi2h_bridge_state *dsi2h_state = to_dsi2h_bridge_state(bridge_state);
	struct drm_atomic_state *state = bridge_state->base.state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;

	if (!bridge->encoder)
		return false;

	crtc = drm_atomic_get_new_crtc_for_encoder(state, bridge->encoder);
	if (!crtc)
		return false;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	if (!new_crtc_state)
		return false;

	if (!new_crtc_state->active || new_crtc_state->active_changed ||
	    new_crtc_state->connectors_changed)
		return false;

	if (memcmp(&dsi2h_state->cfg, &dsi2h->cfg, sizeof(dsi2h->cfg))) {
		dev_dbg(dsi2h->dev, "%s: unsupported: change in dsi config\n", __func__);
		return false;
	}

	if (new_crtc_state->mode_changed) {
		struct drm_crtc_state *old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);
		struct drm_display_mode *old_mode = &old_crtc_state->mode;
		struct drm_display_mode *new_mode = &new_crtc_state->mode;

		if ((old_mode->vdisplay != new_mode->vdisplay) ||
		    (old_mode->hdisplay != new_mode->hdisplay)) {
			dev_dbg(dsi2h->dev, "%s: unsupported: modes do not match\n", __func__);
			return false;
		}
	}

	return true;
}

static struct drm_bridge_state *dw_mipi_dsi2h_atomic_duplicate_state(struct drm_bridge *bridge)
{
	struct dsi2h_bridge_state *copy;
	struct drm_bridge_state *bridge_state;
	struct dsi2h_bridge_state *old_dsi2h_state;

	if (!bridge->base.state)
		return NULL;
	bridge_state = drm_priv_to_bridge_state(bridge->base.state);
	old_dsi2h_state = to_dsi2h_bridge_state(bridge_state);

	copy = kmemdup(old_dsi2h_state, sizeof(*copy), GFP_KERNEL);
	if (!copy)
		return NULL;

	__drm_atomic_helper_bridge_duplicate_state(bridge, &copy->base);
	copy->is_seamless_modeset = false;
	copy->is_active_modeset = false;

	return &copy->base;
}
static void dw_mipi_dsi2h_atomic_destroy_state(struct drm_bridge *bridge,
					       struct drm_bridge_state *state)
{
	struct dsi2h_bridge_state *dsi2h_state = to_dsi2h_bridge_state(state);

	kfree(dsi2h_state);
}
static struct drm_bridge_state *dw_mipi_dsi2h_atomic_reset(struct drm_bridge *bridge)

{
	struct dsi2h_bridge_state *dsi2h_state;

	if (bridge->base.state) {
		struct drm_bridge_state *bridge_state =
			drm_priv_to_bridge_state(bridge->base.state);

		dw_mipi_dsi2h_atomic_destroy_state(bridge, bridge_state);
	}
	dsi2h_state = kzalloc(sizeof(*dsi2h_state), GFP_KERNEL);
	if (!dsi2h_state)
		return ERR_PTR(-ENOMEM);

	__drm_atomic_helper_bridge_reset(bridge, &dsi2h_state->base);

	return &dsi2h_state->base;
}

static int dw_mipi_dsi2h_atomic_check(struct drm_bridge *bridge,
				      struct drm_bridge_state *bridge_state,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct dw_mipi_dsi2h *dsi2h = bridge_to_dsi(bridge);
	struct dsi2h_bridge_state *dsi2h_state = to_dsi2h_bridge_state(bridge_state);
	struct mipi_dsi_device *dsi_dev = dsi2h->dsi_device;
	unsigned long pixel_clock;
	int bpp;
	int ret;

	mutex_lock(&dsi2h->dsi2h_lock);
	update_int_cntrs(dsi2h, conn_state);
	mutex_unlock(&dsi2h->dsi2h_lock);

	if (!drm_atomic_crtc_needs_modeset(crtc_state))
		return 0;

	if (dsi_dev->lanes < 1 || dsi_dev->lanes > 4) {
		DRM_DEV_ERROR(dsi2h->dev, "Need DSI lanes: %d\n", dsi_dev->lanes);
		return -EINVAL;
	}
	if (dsi_dev->format >= ARRAY_SIZE(dsi2h_formats)) {
		DRM_DEV_ERROR(dsi2h->dev, "unsupported format: %d", dsi_dev->format);
		return -EINVAL;
	}

	bpp = mipi_dsi_pixel_format_to_bpp(dsi_dev->format);
	if (bpp < 0)
		return bpp;

	/* TODO(b/353743775): data rate should ideally come from panel */
	pixel_clock = mult_frac(dsi2h->datarate, 1000 * dsi_dev->lanes, bpp);
	ret = phy_mipi_dphy_get_default_config(pixel_clock, bpp, dsi_dev->lanes,
					       &dsi2h_state->cfg.phy_opts.mipi_dphy);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi2h->dev, "%s failed to get default config\n", __func__);
		return ret;
	}

	dw_dsi2h_import_device_config(&dsi2h_state->cfg, dsi_dev);
	dsi2h_state->is_seamless_modeset = is_seamless_modeset(bridge, bridge_state);
	dsi2h_state->is_active_modeset = is_active_modeset(bridge, bridge_state);

	mutex_lock(&dsi2h->dsi2h_lock);
	update_gs_hs_clk(dsi2h, conn_state);
	mutex_unlock(&dsi2h->dsi2h_lock);

	return 0;
}

static void dw_mipi_dsi2h_bridge_atomic_enable(struct drm_bridge *bridge,
					       struct drm_bridge_state *old_bridge_state)
{
	int ret;
	struct dw_mipi_dsi2h *dsi2h = bridge_to_dsi(bridge);
	struct drm_bridge_state *bridge_state = drm_priv_to_bridge_state(bridge->base.state);
	struct dsi2h_bridge_state *dsi2h_state = to_dsi2h_bridge_state(bridge_state);
	struct drm_atomic_state *state = old_bridge_state->base.state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	struct drm_connector *connector;
	bool needs_enable = true;

	if (dsi2h_state->is_seamless_modeset)
		return;

	crtc = drm_atomic_get_new_crtc_for_encoder(state, bridge->encoder);
	if (!crtc) {
		dev_WARN(dsi2h->dev, "no crtc during dsi2h enable\n");
		return;
	}

	old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);
	if (old_crtc_state && old_crtc_state->enable && old_crtc_state->self_refresh_active) {
		dev_dbg(dsi2h->dev, "enable after being in psr state\n");
		needs_enable = false;
	} else {
		dev_dbg(dsi2h->dev, "power ON\n");
	}

	if (dsi2h_state->is_active_modeset) {
		dev_dbg(dsi2h->dev, "active modeset, skip DSI power on\n");
	} else {
		ret = pm_runtime_resume_and_get(dsi2h->dev);
		if (unlikely(ret < 0)) {
			dev_err(dsi2h->dev,
				"failed to runtime enable during atomic enable, ret = %d\n", ret);
			return;
		}
	}

	mutex_lock(&dsi2h->dsi2h_lock);
	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	if (connector) {
		struct drm_connector_state *conn_state =
				drm_atomic_get_new_connector_state(state, connector);

		if (is_gs_drm_connector(connector)) {
			struct gs_drm_connector *gs_connector;

			gs_connector = to_gs_connector(connector);
			if (gs_connector) {
				gs_connector->funcs->get_mipi_allowed_datarates(
					gs_connector, &dsi2h->allowed_hs_clks);
			}
		}

		dev_dbg(dsi2h->dev, "dsi2h datarate %u\n", dsi2h->datarate);

		update_gs_hs_clk(dsi2h, conn_state);
		if ((!dsi2h->pending_datarate || dsi2h->pending_datarate == dsi2h->datarate) &&
		    dsi2h->dynamic_hs_clk_en)
			dsi2h->datarate_changed = false;
	}

	/* double enable DSI would cause unexpected exception */
	if (needs_enable && !dsi2h->enabled) {
		dw_mipi_dsi2h_enable(dsi2h);
		dsi2h->enabled = true;
	/* following cases are for exit ULPS state transition */
	} else {
		switch (dsi2h->state) {
		case DSI2H_STATE_ULPS:
			ret = dw_mipi_dsi2h_exit_ulps(dsi2h);
			if (ret < 0)
				dev_err(dsi2h->dev, "exit ulps fail\n");
			break;
		case DSI2H_STATE_PENDING_ULPS:
			/* DSI exit ULPS already in PENDING_ULPS state */
			dsi2h->state = DSI2H_STATE_HS_EN;
			break;
		case DSI2H_STATE_SUSPEND:
			/* enable DSI from system suspend state */
			dw_mipi_dsi2h_enable(dsi2h);
			dsi2h->enabled = true;
			break;
		case DSI2H_STATE_HS_EN:
			/* Already in HS mode, do nothing */
			break;
		}
	}

	if (dsi2h->psr_rpm_on) {
		/* remove extra vote taken to keep runtime pm on during psr */
		ret = pm_runtime_put(dsi2h->dev);
		if (unlikely(ret < 0))
			dev_err(dsi2h->dev, "failed to runtime disable out of psr, ret = %d\n",
				ret);

		dsi2h->psr_rpm_on = false;
	}

	if (unlikely(dsi2h->state != DSI2H_STATE_HS_EN))
		dev_WARN(dsi2h->dev, "incorrect state=%d after enable\n", dsi2h->state);
	mutex_unlock(&dsi2h->dsi2h_lock);
}

/**
 * dw_mipi_dsi2h_bridge_atomic_post_disable() - Disable the DSI bridge.
 * @bridge: The DRM bridge structure.
 * @old_bridge_state: The previous state of the bridge.
 *
 * This function is called after the display pipeline has been disabled. It
 * handles the transition to a low-power state. If the new state is Panel
 * Self-Refresh (PSR), it may enter Ultra-Low Power State (ULPS). Otherwise,
 * it fully disables the DSI host controller. It also manages runtime power
 * management, ensuring the device is powered down when not in use.
 *
 * This function is part of the DRM atomic modesetting API for bridges.
 */
static void dw_mipi_dsi2h_bridge_atomic_post_disable(struct drm_bridge *bridge,
						     struct drm_bridge_state *old_bridge_state)
{
	int ret;
	struct dw_mipi_dsi2h *dsi2h = bridge_to_dsi(bridge);
	struct drm_bridge_state *bridge_state = drm_priv_to_bridge_state(bridge->base.state);
	struct dsi2h_bridge_state *dsi2h_state = to_dsi2h_bridge_state(bridge_state);
	struct drm_atomic_state *state = old_bridge_state->base.state;
	struct drm_crtc *crtc;
	bool is_psr = false;
	bool should_rpm_put = true;

	if (dsi2h_state->is_seamless_modeset)
		return;

	crtc = drm_atomic_get_old_crtc_for_encoder(state, bridge->encoder);
	if (crtc) {
		struct drm_crtc_state *old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);
		struct drm_crtc_state *new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
		bool was_in_psr = old_crtc_state && old_crtc_state->self_refresh_active;

		if (was_in_psr && new_crtc_state &&
		    drm_atomic_crtc_effectively_active(new_crtc_state)) {
			dev_dbg(dsi2h->dev, "no change in active mode, skip atomic disable\n");
			return;
		}

		if (new_crtc_state && new_crtc_state->self_refresh_active) {
			dev_dbg(dsi2h->dev, "transition to psr state\n");
			is_psr = true;

			mutex_lock(&dsi2h->dsi2h_lock);
			if (dsi2h->pending_datarate && dsi2h->dynamic_hs_clk_en) {
				dev_dbg(dsi2h->dev, "assign pending datarate %u for psr\n",
					dsi2h->pending_datarate);
				dsi2h->datarate = dsi2h->pending_datarate;
				dsi2h->pending_datarate = 0;
				phy_set_speed(dsi2h->phy, dsi2h->datarate);
			}
			mutex_unlock(&dsi2h->dsi2h_lock);
		} else {
			dev_dbg(dsi2h->dev, "power OFF\n");
		}
	}

	/* cancel and confirm all delay ulps works are done */
	kthread_cancel_delayed_work_sync(&dsi2h->ulps_dwork);

	mutex_lock(&dsi2h->dsi2h_lock);
	if (!is_psr) {
		if (unlikely(!dsi2h->enabled))
			dev_WARN(dsi2h->dev, "already disabled. state=%d\n", dsi2h->state);

		if (dsi2h->state == DSI2H_STATE_PENDING_ULPS)
			dsi2h->state = DSI2H_STATE_HS_EN;
		/* exit ULPS state before power off, avoid unexpected HW abnormal */
		else if (dsi2h->state == DSI2H_STATE_ULPS)
			dw_mipi_dsi2h_exit_ulps(dsi2h);

		dev_dbg(dsi2h->dev, "%s: state=%d\n", __func__, dsi2h->state);
		if (!pm_runtime_enabled(dsi2h->dev))
			dw_mipi_dsi2h_disable(dsi2h);
		dsi2h->enabled = false;
	} else if (!dsi2h->disable_psr_ulps) {
		ret = dw_mipi_dsi2h_enter_ulps(dsi2h);
		if (unlikely(ret < 0))
			dev_err(dsi2h->dev, "enter ulps fail\n");

		/* DW DSI power can't disable during ULPS state */
		should_rpm_put = false;
	}
	dsi2h->psr_rpm_on = !should_rpm_put;
	mutex_unlock(&dsi2h->dsi2h_lock);

	if (should_rpm_put) {
		if (dsi2h_state->is_active_modeset) {
			dev_dbg(dsi2h->dev, "active modeset, skip DSI power off\n");
		} else {
			ret = pm_runtime_put_sync(dsi2h->dev);
			if (unlikely(ret < 0))
				dev_err(dsi2h->dev,
					"failed to runtime disable during atomic disable, ret = %d\n",
					ret);
		}
	}
}

static int dw_mipi_dsi2h_bridge_attach(struct drm_bridge *bridge,
				       enum drm_bridge_attach_flags flags)
{
	struct dw_mipi_dsi2h *dsi2h = bridge_to_dsi(bridge);

	if (!bridge->encoder) {
		pr_err("Parent encoder object not found\n");
		return -ENODEV;
	}

	if (!dsi2h->dsi_device) {
		return -EPROBE_DEFER;
	}

	dsi2h->panel_bridge =
		devm_drm_of_get_bridge(dsi2h->dev, dsi2h->dev->of_node, 1 /*port*/, 0 /*endpoint*/);
	if (IS_ERR(dsi2h->panel_bridge)) {
		dev_err(dsi2h->dev, "failed to get bridge\n");
		return PTR_ERR(dsi2h->panel_bridge);
	}

	/* Attach the panel-bridge to the dsi bridge */
	return drm_bridge_attach(bridge->encoder, dsi2h->panel_bridge, bridge,
				 flags);
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
/*
 * Returns errno on fail cases
 * Return 0 on success
 */
static __maybe_unused int debugfs_dsi_read(void *data, unsigned int *val, unsigned int offset)
{
	struct dw_mipi_dsi2h *dsi2h = data;

	if (!dsi2h)
		return -ENODEV;

	regmap_read(dsi2h->regs, offset, val);

	return 0;
}

/*
 * Returns errno on fail cases
 * Return 0 on success
 */
static __maybe_unused int debugfs_dsi_write(void *data, unsigned int val, unsigned int offset)
{
	struct dw_mipi_dsi2h *dsi2h = data;
	int ret;

	if (!dsi2h)
		return -ENODEV;

	ret = regmap_write(dsi2h->regs, offset, val);
	if (ret)
		return ret;

	return 0;
}

static struct mipi_dsi_host *dw_mipi_dsi2h_return_host(struct dw_mipi_dsi2h *dsi2h)
{
	return &dsi2h->dsi_host;
}

static int dw_dsi2h_pri_request(struct dw_mipi_dsi2h *dsi2h, u32 type, u64 val1, u64 val2)
{
	int ret = -1;

	switch (type) {
	case DW_PRI_TRIGGER_REQUEST:
		ret = dw_dsi2h_pri_trigger_request(dsi2h, val1);
		break;
	case DW_PRI_DESKEW_CAL:
		ret = dw_dsi2h_pri_calibration(dsi2h, PHY_DESKEW_CAL, val1);
		break;
	case DW_PRI_ALTERNATE_CAL:
		ret = dw_dsi2h_pri_calibration(dsi2h, PHY_ALTERNATE_CAL, val1);
		break;
	case DW_PRI_BTA_REQUEST:
		ret = dw_dsi2h_pri_bta_request(dsi2h);
		break;
	case DW_PRI_TRIGGERS_READ:
		ret = dw_dsi2h_pri_receive_triggers(dsi2h);
		break;
	}

	return ret;
}

static struct dw_mipi_dsi2h_debug_ops dw_mipi_dsi2h_debug_ops = {
	.ctrl_read_dbg = dw_ctrl_read_dbgfs,
	.display_export = dw_dsi2h_export_display,
	.device_export = dw_dsi2h_export_device,
	.phy_opts_export = dw_dsi2h_export_phy_opts,
	.device_import = dw_dsi2h_import_device,
	.dsi_reconfigure = dw_dsi2h_reconfigure,
	.dsi_return_host = dw_mipi_dsi2h_return_host,
	.dsi_write_command = dw_mipi_dsi2h_host_transfer,
	.dsi_pri_request = dw_dsi2h_pri_request,
	.dsi2_start_tear_effect = dw_dsi2h_start_tear_effect,
	.dsi2_finish_tear_effect = dw_dsi2h_finish_tear_effect,
};

struct dw_mipi_dsi2h_debug_ops *dw_mipi_dsi2h_get_debug_ops(void)
{
	return &dw_mipi_dsi2h_debug_ops;
}
EXPORT_SYMBOL_GPL(dw_mipi_dsi2h_get_debug_ops);

static int reg_dump_show(struct seq_file *s, void *data)
{
	int ret, ret_dump;
	struct dw_mipi_dsi2h *dw_dsi2h = s->private;
	struct drm_printer p = drm_seq_file_printer(s);
	char desc[5];

	ret = pm_runtime_get_if_in_use(dw_dsi2h->dev);
	if (ret < 0) {
		dev_err(dw_dsi2h->dev, "Failed to power ON, ret %d\n", ret);
		return ret;
	}

	if (!ret) {
		dev_dbg(dw_dsi2h->dev, "Device is powered OFF\n");
		return ret;
	}

	scnprintf(desc, sizeof(desc), "DSI%u", dw_dsi2h->plat_data->mux_id);
	ret_dump = gs_reg_dump_with_skips(desc, dw_dsi2h->base, dw_dsi2h->reg_dump_offset,
					  dw_dsi2h->reg_dump_size, &p, dw_dsi2h->reg_table);

	ret = pm_runtime_put_sync(dw_dsi2h->dev);
	if (ret < 0) {
		dev_err(dw_dsi2h->dev, "Failed to power OFF, ret %d\n", ret);
		return ret;
	}

	return ret_dump;
}

DEFINE_SHOW_ATTRIBUTE(reg_dump);

static ssize_t dsi_errors_store(struct file *file, const char __user *user_buf, size_t count,
				loff_t *ppos)
{
	struct dw_mipi_dsi2h *dw_dsi2h = file->private_data;
	unsigned long flags = 0;
	u64 errors;
	int ret;

	ret = kstrtou64_from_user(user_buf, count, 0, &errors);
	if (ret)
		return ret;

	spin_lock_irqsave(&dw_dsi2h->spinlock_dsi, flags);
	dw_dsi2h->rstn = errors;
	spin_unlock_irqrestore(&dw_dsi2h->spinlock_dsi, flags);

	return count;
}

static const struct file_operations dsi_errors_fops = {
	.open = simple_open,
	.write = dsi_errors_store,
};

static ssize_t coredump_trigger_store(struct file *file, const char __user *user_buf, size_t count,
				      loff_t *ppos)
{
	struct dw_mipi_dsi2h *dsi2h = file->private_data;
	bool trigger;
	int ret;

	ret = kstrtobool_from_user(user_buf, count, &trigger);
	if (ret)
		return ret;

	if (trigger && dsi2h->coredump_funcs && dsi2h->coredump_funcs->trigger_coredump) {
		dsi2h->coredump_funcs->trigger_coredump(dsi2h->bridge.dev, dsi2h->vdev,
							"Manual core dump");
	}

	return count;
}

static const struct file_operations coredump_trigger_fops = {
	.open = simple_open,
	.write = coredump_trigger_store,
};

static int dw_mipi_dsi2h_debugfs_init(struct dw_mipi_dsi2h *dsi2h)
{
	dsi2h->reg_dump_offset = 0;
	dsi2h->reg_dump_size = dsi2h->reg_size;

	dsi2h->debugfs = debugfs_create_dir(dev_name(dsi2h->dev), NULL);
	if (IS_ERR(dsi2h->debugfs))
		return PTR_ERR(dsi2h->debugfs);

	dw_create_debugfs_hwv_files(dsi2h);

	debugfs_create_file("reg_dump", 0444, dsi2h->debugfs, dsi2h, &reg_dump_fops);
	debugfs_create_x32("reg_dump_offset", 0644, dsi2h->debugfs, &dsi2h->reg_dump_offset);
	debugfs_create_x32("reg_dump_size", 0644, dsi2h->debugfs, &dsi2h->reg_dump_size);
	debugfs_create_bool("force_set_hs_clk", 0644, dsi2h->debugfs, &dsi2h->force_set_datarate);
	debugfs_create_bool("disable_psr_ulps", 0644, dsi2h->debugfs, &dsi2h->disable_psr_ulps);
	debugfs_create_file("dsi_errors", 0644, dsi2h->debugfs, dsi2h, &dsi_errors_fops);
	debugfs_create_file("coredump_trigger", 0200, dsi2h->debugfs, dsi2h,
			    &coredump_trigger_fops);

	return 0;
}

static void dw_mipi_dsi2h_debugfs_remove(struct dw_mipi_dsi2h *dsi2h)
{
	debugfs_remove_recursive(dsi2h->debugfs);
}

#else

static int dw_mipi_dsi2h_debugfs_init(struct dw_mipi_dsi2h *dsi2h)
{
	return 0;
}
static void dw_mipi_dsi2h_debugfs_remove(struct dw_mipi_dsi2h *dsi2h)
{
}

#endif /* CONFIG_DEBUG_FS */

static const struct drm_bridge_funcs dw_mipi_dsi2h_bridge_funcs = {
	.attach = dw_mipi_dsi2h_bridge_attach,
	.mode_set = dw_mipi_dsi2h_bridge_mode_set,
	.atomic_enable = dw_mipi_dsi2h_bridge_atomic_enable,
	.atomic_post_disable = dw_mipi_dsi2h_bridge_atomic_post_disable,
	.atomic_check = dw_mipi_dsi2h_atomic_check,
	.atomic_duplicate_state = dw_mipi_dsi2h_atomic_duplicate_state,
	.atomic_destroy_state = dw_mipi_dsi2h_atomic_destroy_state,
	.atomic_reset = dw_mipi_dsi2h_atomic_reset,
};

static int dw_mipi_dsi2h_regmap_fields_init(struct dw_mipi_dsi2h *dsi2h)
{
	const struct dw_mipi_dsi2h_variant *variant;

	switch (dsi2h->hw_version & 0xFFF) {
	case VERSION_01_00:
	case VERSION_01_10:
		variant = &dw_mipi_dsi2h_lca01_layout;
		break;
	default:
		pr_err("Unrecognized DSI host controller HW revision\n");
		return -ENODEV;
	}

	INIT_FIELD(core_id);
	INIT_FIELD(ver_number);
	INIT_FIELD(type_num);
	INIT_FIELD(pkg_num);
	INIT_FIELD(type_enum);
	INIT_FIELD(pwr_up);
	INIT_FIELD(mode_ctrl);
	INIT_FIELD(mode_status);
	INIT_FIELD(core_busy);
	INIT_FIELD(core_fifos_not_empty);
	INIT_FIELD(ipi_busy);
	INIT_FIELD(ipi_fifos_not_empty);
	INIT_FIELD(cri_busy);
	INIT_FIELD(cri_wr_fifos_not_empty);
	INIT_FIELD(cri_rd_data_avail);
	INIT_FIELD(pri_busy);
	INIT_FIELD(pri_tx_fifos_not_empty);
	INIT_FIELD(pri_rx_data_avail);
	INIT_FIELD(manual_mode_en);
	INIT_FIELD(fsm_selector);
	INIT_FIELD(current_state);
	INIT_FIELD(stuck);
	INIT_FIELD(previous_state);
	INIT_FIELD(current_state_cnt);
	INIT_FIELD(fsm_manual_init);
	INIT_FIELD(fifo_selector);
	INIT_FIELD(empty);
	INIT_FIELD(almost_empty);
	INIT_FIELD(half_full);
	INIT_FIELD(almost_full);
	INIT_FIELD(full);
	INIT_FIELD(current_word_count);
	INIT_FIELD(fifo_manual_init);
	INIT_FIELD(to_hstx_value);
	INIT_FIELD(to_hstxrdy_value);
	INIT_FIELD(to_lprx_value);
	INIT_FIELD(to_lptxrdy_value);
	INIT_FIELD(to_lptxtrig_value);
	INIT_FIELD(to_lptxulps_value);
	INIT_FIELD(to_bta_value);
	INIT_FIELD(phy_type);
	INIT_FIELD(phy_lanes);
	INIT_FIELD(ppi_width);
	INIT_FIELD(hs_transferen_en);
	INIT_FIELD(clk_type);
	INIT_FIELD(phy_lptx_clk_div);
	INIT_FIELD(phy_direction);
	INIT_FIELD(phy_clk_stopstate);
	INIT_FIELD(phy_l0_stopstate);
	INIT_FIELD(phy_l1_stopstate);
	INIT_FIELD(phy_l2_stopstate);
	INIT_FIELD(phy_l3_stopstate);
	INIT_FIELD(phy_clk_ulpsactivenot);
	INIT_FIELD(phy_l0_ulpsactivenot);
	INIT_FIELD(phy_l1_ulpsactivenot);
	INIT_FIELD(phy_l2_ulpsactivenot);
	INIT_FIELD(phy_l3_ulpsactivenot);
	INIT_FIELD(phy_lp2hs_time);
	INIT_FIELD(phy_lp2hs_time_auto);
	INIT_FIELD(phy_hs2lp_time);
	INIT_FIELD(phy_hs2lp_time_auto);
	INIT_FIELD(phy_max_rd_time);
	INIT_FIELD(phy_max_rd_time_auto);
	INIT_FIELD(phy_esc_cmd_time);
	INIT_FIELD(phy_esc_cmd_time_auto);
	INIT_FIELD(phy_esc_byte_time);
	INIT_FIELD(phy_esc_byte_time_auto);
	INIT_FIELD(phy_ipi_ratio);
	INIT_FIELD(phy_ipi_ratio_auto);
	INIT_FIELD(phy_sys_ratio);
	INIT_FIELD(phy_sys_ratio_auto);
	INIT_FIELD(phy_tx_trigger_0);
	INIT_FIELD(phy_tx_trigger_1);
	INIT_FIELD(phy_tx_trigger_2);
	INIT_FIELD(phy_tx_trigger_3);
	INIT_FIELD(phy_deskewcal);
	INIT_FIELD(phy_alternatecal);
	INIT_FIELD(phy_ulps_entry);
	INIT_FIELD(phy_ulps_exit);
	INIT_FIELD(phy_bta);
	INIT_FIELD(phy_cal_time);
	INIT_FIELD(phy_ulps_data_lanes);
	INIT_FIELD(phy_ulps_clk_lane);
	INIT_FIELD(phy_wakeup_time);
	INIT_FIELD(eotp_tx_en);
	INIT_FIELD(bta_en);
	INIT_FIELD(tx_vcid);
	INIT_FIELD(scrambling_en);
	INIT_FIELD(scrambling_seed);
	INIT_FIELD(vid_mode_type);
	INIT_FIELD(blk_hsa_hs_en);
	INIT_FIELD(blk_hbp_hs_en);
	INIT_FIELD(blk_hfp_hs_en);
	INIT_FIELD(blk_vsa_hs_en);
	INIT_FIELD(blk_vbp_hs_en);
	INIT_FIELD(blk_vfp_hs_en);
	INIT_FIELD(lpdt_display_cmd_en);
	INIT_FIELD(max_rt_pkt_sz);
	INIT_FIELD(auto_tear_bta_disable);
	INIT_FIELD(te_type_hw);
	INIT_FIELD(set_tear_on_args_hw);
	INIT_FIELD(set_tear_scanline_args_hw);
	INIT_FIELD(ipi_format);
	INIT_FIELD(ipi_depth);
	INIT_FIELD(vid_hsa_time);
	INIT_FIELD(vid_hsa_time_auto);
	INIT_FIELD(vid_hbp_time);
	INIT_FIELD(vid_hbp_time_auto);
	INIT_FIELD(vid_hact_time);
	INIT_FIELD(vid_hact_time_auto);
	INIT_FIELD(vid_hline_time);
	INIT_FIELD(vid_hline_time_auto);
	INIT_FIELD(vid_vsa_lines);
	INIT_FIELD(vid_vsa_lines_auto);
	INIT_FIELD(vid_vbp_lines);
	INIT_FIELD(vid_vbp_lines_auto);
	INIT_FIELD(vid_vact_lines);
	INIT_FIELD(vid_vact_lines_auto);
	INIT_FIELD(vid_vfp_lines);
	INIT_FIELD(vid_vfp_lines_auto);
	INIT_FIELD(max_pix_pkt);
	INIT_FIELD(hib_type);
	INIT_FIELD(hib_ulps_wakeup_time);
	INIT_FIELD(mask_phy_l0_erresc);
	INIT_FIELD(mask_phy_l0_errsyncesc);
	INIT_FIELD(mask_phy_l0_errcontrol);
	INIT_FIELD(mask_phy_l0_errcontentionlp0);
	INIT_FIELD(mask_phy_l0_errcontentionlp1);
	INIT_FIELD(mask_txhs_fifo_over);
	INIT_FIELD(mask_txhs_fifo_under);
	INIT_FIELD(force_phy_l0_erresc);
	INIT_FIELD(force_phy_l0_errsyncesc);
	INIT_FIELD(force_phy_l0_errcontrol);
	INIT_FIELD(force_phy_l0_errcontentionlp0);
	INIT_FIELD(force_phy_l0_errcontentionlp1);
	INIT_FIELD(force_txhs_fifo_over);
	INIT_FIELD(force_txhs_fifo_under);
	INIT_FIELD(mask_err_to_hstx);
	INIT_FIELD(mask_err_to_hstxrdy);
	INIT_FIELD(mask_err_to_lprx);
	INIT_FIELD(mask_err_to_lptxrdy);
	INIT_FIELD(mask_err_to_lptxtrig);
	INIT_FIELD(mask_err_to_lptxulps);
	INIT_FIELD(mask_err_to_bta);
	INIT_FIELD(force_err_to_hstx);
	INIT_FIELD(force_err_to_hstxrdy);
	INIT_FIELD(force_err_to_lprx);
	INIT_FIELD(force_err_to_lptxrdy);
	INIT_FIELD(force_err_to_lptxtrig);
	INIT_FIELD(force_err_to_lptxulps);
	INIT_FIELD(force_err_to_bta);
	INIT_FIELD(mask_err_ack_rpt_0);
	INIT_FIELD(mask_err_ack_rpt_1);
	INIT_FIELD(mask_err_ack_rpt_2);
	INIT_FIELD(mask_err_ack_rpt_3);
	INIT_FIELD(mask_err_ack_rpt_4);
	INIT_FIELD(mask_err_ack_rpt_5);
	INIT_FIELD(mask_err_ack_rpt_6);
	INIT_FIELD(mask_err_ack_rpt_7);
	INIT_FIELD(mask_err_ack_rpt_8);
	INIT_FIELD(mask_err_ack_rpt_9);
	INIT_FIELD(mask_err_ack_rpt_10);
	INIT_FIELD(mask_err_ack_rpt_11);
	INIT_FIELD(mask_err_ack_rpt_12);
	INIT_FIELD(mask_err_ack_rpt_13);
	INIT_FIELD(mask_err_ack_rpt_14);
	INIT_FIELD(mask_err_ack_rpt_15);
	INIT_FIELD(force_err_ack_rpt_0);
	INIT_FIELD(force_err_ack_rpt_1);
	INIT_FIELD(force_err_ack_rpt_2);
	INIT_FIELD(force_err_ack_rpt_3);
	INIT_FIELD(force_err_ack_rpt_4);
	INIT_FIELD(force_err_ack_rpt_5);
	INIT_FIELD(force_err_ack_rpt_6);
	INIT_FIELD(force_err_ack_rpt_7);
	INIT_FIELD(force_err_ack_rpt_8);
	INIT_FIELD(force_err_ack_rpt_9);
	INIT_FIELD(force_err_ack_rpt_10);
	INIT_FIELD(force_err_ack_rpt_11);
	INIT_FIELD(force_err_ack_rpt_12);
	INIT_FIELD(force_err_ack_rpt_13);
	INIT_FIELD(force_err_ack_rpt_14);
	INIT_FIELD(force_err_ack_rpt_15);
	INIT_FIELD(mask_err_display_cmd_time);
	INIT_FIELD(mask_err_ipi_dtype);
	INIT_FIELD(mask_err_vid_bandwidth);
	INIT_FIELD(mask_err_ipi_cmd);
	INIT_FIELD(mask_err_display_cmd_ovfl);
	INIT_FIELD(mask_ipi_event_fifo_over);
	INIT_FIELD(mask_ipi_event_fifo_under);
	INIT_FIELD(mask_ipi_pixel_fifo_over);
	INIT_FIELD(mask_ipi_pixel_fifo_under);
	INIT_FIELD(force_err_display_cmd_time);
	INIT_FIELD(force_err_ipi_dtype);
	INIT_FIELD(force_err_vid_bandwidth);
	INIT_FIELD(force_err_ipi_cmd);
	INIT_FIELD(force_err_display_cmd_ovfl);
	INIT_FIELD(force_ipi_event_fifo_over);
	INIT_FIELD(force_ipi_event_fifo_under);
	INIT_FIELD(force_ipi_pixel_fifo_over);
	INIT_FIELD(force_ipi_pixel_fifo_under);
	INIT_FIELD(mask_err_pri_tx_time);
	INIT_FIELD(mask_err_pri_tx_cmd);
	INIT_FIELD(force_err_pri_tx_time);
	INIT_FIELD(force_err_pri_tx_cmd);
	INIT_FIELD(mask_err_cri_cmd_time);
	INIT_FIELD(mask_err_cri_dtype);
	INIT_FIELD(mask_err_cri_vchannel);
	INIT_FIELD(mask_err_cri_rx_length);
	INIT_FIELD(mask_err_cri_ecc);
	INIT_FIELD(mask_err_cri_ecc_fatal);
	INIT_FIELD(mask_err_cri_crc);
	INIT_FIELD(mask_cmd_rd_pld_fifo_over);
	INIT_FIELD(mask_cmd_rd_pld_fifo_under);
	INIT_FIELD(mask_cmd_wr_pld_fifo_over);
	INIT_FIELD(mask_cmd_wr_pld_fifo_under);
	INIT_FIELD(mask_cmd_wr_hdr_fifo_over);
	INIT_FIELD(mask_cmd_wr_hdr_fifo_under);
	INIT_FIELD(force_err_cri_cmd_time);
	INIT_FIELD(force_err_cri_dtype);
	INIT_FIELD(force_err_cri_vchannel);
	INIT_FIELD(force_err_cri_rx_length);
	INIT_FIELD(force_err_cri_ecc);
	INIT_FIELD(force_err_cri_ecc_fatal);
	INIT_FIELD(force_err_cri_crc);
	INIT_FIELD(force_cmd_rd_pld_fifo_over);
	INIT_FIELD(force_cmd_rd_pld_fifo_under);
	INIT_FIELD(force_cmd_wr_pld_fifo_over);
	INIT_FIELD(force_cmd_wr_pld_fifo_under);
	INIT_FIELD(force_cmd_wr_hdr_fifo_over);
	INIT_FIELD(force_cmd_wr_hdr_fifo_under);

	return 0;
}

/* hs_clock actually stores the datarate with unit Mbps */
static ssize_t hs_clock_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dw_mipi_dsi2h *dsi2h = dev_get_drvdata(dev);

	if (!dsi2h)
		return -EINVAL;

	return sysfs_emit(buf, "%d\n", dsi2h->datarate);
}

static ssize_t hs_clock_store(struct device *dev, struct device_attribute *attr, const char *buf,
			      size_t len)
{
	int rc;
	unsigned int datarate;
	struct dw_mipi_dsi2h *dsi2h = dev_get_drvdata(dev);

	if (!dsi2h)
		return -EINVAL;

	rc = kstrtouint(buf, 0, &datarate);
	if (rc < 0)
		return rc;

	if (!dsi2h->force_set_datarate) {
		bool datarate_is_allowed = false;

		for (int i = 0; i < sizeof(dsi2h->allowed_hs_clks.clks); i++) {
			if (dsi2h->allowed_hs_clks.clks[i] == 0)
				break;

			if (datarate == dsi2h->allowed_hs_clks.clks[i]) {
				datarate_is_allowed = true;
				break;
			}
		}

		if (!datarate_is_allowed) {
			dev_warn(dsi2h->dev, "datarate=%u not in allowed_datarates\n", datarate);
			return -EINVAL;
		}
	}

	mutex_lock(&dsi2h->dsi2h_lock);

	dev_info(dsi2h->dev,
		 "dsi2h_state=%d, target_datarate=%u, current_datarate=%u, pending_datarate=%u, dynamic_en=%d\n",
		 dsi2h->state, datarate, dsi2h->datarate, dsi2h->pending_datarate,
		 dsi2h->dynamic_hs_clk_en);

	if (datarate == dsi2h->datarate) {
		dev_dbg(dsi2h->dev, "set the same datarate %u\n", datarate);
		dsi2h->pending_datarate = 0;
	} else if (!dsi2h->dynamic_hs_clk_en && dsi2h->state != DSI2H_STATE_SUSPEND) {
		dev_dbg(dsi2h->dev, "dynamic_hs_clk_en=0, set pending_datarate %u\n", datarate);
		dsi2h->pending_datarate = datarate;
	} else if (dsi2h->state != DSI2H_STATE_HS_EN && dsi2h->state != DSI2H_STATE_SUSPEND) {
		dev_dbg(dsi2h->dev, "not in HS state, set pending_datarate %u\n", datarate);
		dsi2h->pending_datarate = datarate;
		dsi2h->datarate_changed = true;
	} else {
		dsi2h->pending_datarate = 0;
		dsi2h->datarate = datarate;
		if (dsi2h->dynamic_hs_clk_en && dsi2h->state != DSI2H_STATE_SUSPEND) {
			dev_dbg(dsi2h->dev,
				"datarate %u will be applied at next exit ULPS\n", datarate);
			dsi2h->datarate_changed = true;
			phy_set_speed(dsi2h->phy, dsi2h->datarate);
		} else {
			dev_dbg(dsi2h->dev,
				"datarate %u will be applied at next dsi2h_enable\n", datarate);
		}
	}

	mutex_unlock(&dsi2h->dsi2h_lock);

	return len;
}

DEVICE_ATTR_RW(hs_clock);

static struct device_type drm_sysfs_device_dw_dsi = {
	.name = "drm_dw_dsi",
};

static struct attribute *dw_dsi_dev_attrs[] = {
	&dev_attr_hs_clock.attr,
	NULL
};

static const struct attribute_group dw_dsi_dev_group = {
	.attrs = dw_dsi_dev_attrs,
};

static const struct attribute_group *dw_dsi_dev_groups[] = {
	&dw_dsi_dev_group,
	NULL
};

static void dw_mipi_dsi2h_sysfs_free(struct device *dev)
{
	kfree(dev);
}

static int dw_mipi_dsi2h_create_vdev(struct dw_mipi_dsi2h *dsi2h)
{
	int ret;
	struct device *vdev;
	struct device *dev = dsi2h->dev;

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return -ENOMEM;

	device_initialize(vdev);
	vdev->class = dev->class;
	vdev->type = &drm_sysfs_device_dw_dsi;
	vdev->parent = dev;
	vdev->groups = dw_dsi_dev_groups;
	vdev->release = dw_mipi_dsi2h_sysfs_free;
	dev_set_drvdata(vdev, dsi2h);

	ret = dev_set_name(vdev, "dw-dsi");
	if (ret)
		goto err_free;

	ret = device_add(vdev);
	if (ret)
		goto err_free;

	dsi2h->vdev = vdev;

	return 0;

err_free:
	put_device(vdev);
	return ret;
}

static void dw_mipi_dsi2h_destroy_vdev(struct dw_mipi_dsi2h *dsi2h)
{
	device_unregister(dsi2h->vdev);
	dsi2h->vdev = NULL;
}

static struct dw_mipi_dsi2h *__dw_mipi_dsi2h_probe(struct platform_device *pdev,
						   const struct dw_mipi_dsi2h_plat_data *pdata)
{
	struct device *dev = &pdev->dev;
	struct dw_mipi_dsi2h *dw_dsi2h;
	struct phy *phy;
	struct resource *mem = NULL;
	int ret = 0;

	if (!pdata)
		return ERR_PTR(-EINVAL);

	phy = devm_phy_get(dev, pdata->phy_name);
	if (IS_ERR(phy)) {
		dev_warn(dev, "phy %s not ready, err %ld\n", pdata->phy_name, PTR_ERR(phy));
		return ERR_CAST(phy);
	}

	dw_dsi2h = devm_kzalloc(dev, sizeof(*dw_dsi2h), GFP_KERNEL);
	if (!dw_dsi2h) {
		dev_err(dev, "Failed to allocate memory\n");
		return ERR_PTR(-ENOMEM);
	}

	/* Get MEM resources */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(dev, "Failed to get memory resources.\n");
		return  ERR_PTR(-ENXIO);
	}

	dw_dsi2h->reg_size = resource_size(mem);
	dw_dsi2h->phys_addr = mem->start;
	dw_dsi2h->base = devm_ioremap_resource(dev, mem);
	if (IS_ERR(dw_dsi2h->base)) {
		dev_err(dev, "Failed to map io resources: %ld\n", PTR_ERR(dw_dsi2h->base));
		return ERR_CAST(dw_dsi2h->base);
	}

	dw_dsi2h->reg_table = &dw_dsi2h_dump_reg_table;

	dw_dsi2h->regs = devm_regmap_init_mmio(dev, dw_dsi2h->base, &dw_mipi_dsi2h_regmap_cfg);
	if (IS_ERR(dw_dsi2h->regs)) {
		dev_err(dev, "Failed to init regmap: %ld\n", PTR_ERR(dw_dsi2h->regs));
		return ERR_CAST(dw_dsi2h->regs);
	}

	/* TODO(longling) allow reconfiguring these items from pdata */
	dw_dsi2h->plat_data = pdata;
	dw_dsi2h->phy_hstx_clk = pdata->phy_clk;
	dw_dsi2h->dev = dev;

	dw_dsi2h->sys_clk = pdata->sys_clk;
	dw_dsi2h->lptx_clk = pdata->lptx_clk;
	dw_dsi2h->max_data_lanes = pdata->max_data_lanes;

	dw_dsi2h->is_cphy = pdata->is_cphy;
	dw_dsi2h->clk_type = pdata->clk_type;
	dw_dsi2h->auto_calc_off = pdata->auto_calc_off;
	dw_dsi2h->hs_link_enable = pdata->hs_link_enable;
	dw_dsi2h->ppi_width = pdata->ppi_width;
	dw_dsi2h->high_speed = pdata->high_speed;
	dw_dsi2h->bta = pdata->bta;
	dw_dsi2h->eotp = pdata->eotp;
	dw_dsi2h->tear_effect = pdata->tear_effect;
	dw_dsi2h->scrambling = pdata->scrambling;
	dw_dsi2h->hib_type = pdata->hib_type;
	dw_dsi2h->vid_mode_type = pdata->vid_mode_type;
	dw_dsi2h->vfp_hs_en = pdata->vfp_hs_en;
	dw_dsi2h->vbp_hs_en = pdata->vbp_hs_en;
	dw_dsi2h->vsa_hs_en = pdata->vsa_hs_en;
	dw_dsi2h->hfp_hs_en = pdata->hfp_hs_en;
	dw_dsi2h->hbp_hs_en = pdata->hbp_hs_en;
	dw_dsi2h->hsa_hs_en = pdata->hsa_hs_en;
	dw_dsi2h->datarate = pdata->datarate;
	dw_dsi2h->lp2hs_time = pdata->lp2hs_time;
	dw_dsi2h->hs2lp_time = pdata->hs2lp_time;
	dw_dsi2h->ulps_wakeup_time = pdata->ulps_wakeup_time;
	dw_dsi2h->ipi_clk = pdata->ipi_clk;
	dw_dsi2h->packet_stack_mode = false;
	dw_dsi2h->mipi_fifo_hdr_used = 0;
	dw_dsi2h->mipi_fifo_pld_used = 0;
	dw_dsi2h->dynamic_hs_clk_en = pdata->dynamic_hs_clk_en;
	dw_dsi2h->suppress_dsi_errors = true;

	spin_lock_init(&dw_dsi2h->spinlock_dsi);
	mutex_init(&dw_dsi2h->dsi2h_lock);

	kthread_init_worker(&dw_dsi2h->dsi2h_worker);
	dw_dsi2h->thread = kthread_run(kthread_worker_fn, &dw_dsi2h->dsi2h_worker,
			"dw_dsi2h_kthread");
	kthread_init_delayed_work(&dw_dsi2h->ulps_dwork, dw_dsi2h_delay_ulps_handler);

	/* Get Phy */
	/* If no CD-PHY available try to get D-PHY */
	dw_dsi2h->phy = phy;

	/* Test if PHY is working properly */
	ret = phy_init(dw_dsi2h->phy);
	if(ret) {
		dev_err(dev, "Failed to initialize phy, ret %d\n", ret);
		goto end;
	}

	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, DSI2H_AUTOSUPEND_DELAY_MS);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		dev_err(dev, "runtime pm enabled but failed to add disable action: %d\n", ret);

	regmap_read(dw_dsi2h->regs, DW_DSI2H_CORE_ID, &dw_dsi2h->hw_core_id);
	regmap_read(dw_dsi2h->regs, DW_DSI2H_VERSION, &dw_dsi2h->hw_version);
	dev_info(dev, "DSI2H CORE_ID = 0x%.8x\n", dw_dsi2h->hw_core_id);
	dev_info(dev, "DSI2H Version = 0x%.8x\n", dw_dsi2h->hw_version);
	if (!dw_dsi2h->hw_version) {
		dev_err(dev, "Failed to read DSI controller version\n");
		ret = -ENODEV;
		goto disable_phy;
	}

	ret = dw_mipi_dsi2h_regmap_fields_init(dw_dsi2h);
	if (ret) {
		dev_err(dev, "Failed to init register layout map: %d\n", ret);
		goto disable_phy;
	}

	dw_dsi2h->irq = pdata->irq;
	ret = devm_request_irq(dev, dw_dsi2h->irq, dw_irq_callback, IRQF_SHARED, "dw-mipi-dsi2h",
			dw_dsi2h);
	if (ret) {
		dev_err(dev, "request_irq failure (%d)\n", ret);
		goto disable_phy;
	}

	dw_dsi2h->dsi_host.ops = &dw_mipi_dsi2h_host_ops;
	dw_dsi2h->dsi_host.dev = dev;

	ret = mipi_dsi_host_register(&dw_dsi2h->dsi_host);
	if (ret) {
		dev_err(dev, "Failed to register MIPI host: %d\n", ret);
		goto disable_phy;
	}

	/* Add this to the platform_data so we can pass it to the device */
	if (pdata->get_resources)
		pdata->get_resources(pdata->priv_data, &dw_dsi2h->bridge, &dw_dsi2h->dsi_host);

	dw_dsi2h->bridge.driver_private = dw_dsi2h;
	dw_dsi2h->bridge.funcs = &dw_mipi_dsi2h_bridge_funcs;

	ret = dw_mipi_dsi2h_create_vdev(dw_dsi2h);
	if (ret) {
		dev_err(dev, "Failed to create MIPI vdev: %d\n", ret);
		goto disable_phy;
	}

	ret = dw_mipi_dsi2h_debugfs_init(dw_dsi2h);
	if (ret) {
		dev_err(dev, "Failed to create debugFS: %d\n", ret);
		goto destroy_vdev;
	}

	dw_dsi2h->stat_data =
		devm_kcalloc(dev, (STATS_EVENT_COUNT), sizeof(u32), GFP_KERNEL);
	if (unlikely(!dw_dsi2h->stat_data)) {
		dev_err(dev, "Failed to allocate dsi2h stat_data\n");
		ret = -ENOMEM;
		goto end;
	}

	ret = dw_mipi_dsi2h_sysfs_init(dw_dsi2h);
	if (unlikely(ret)) {
		dev_err(dev, "Failed to create sysfs: %d\n", ret);
		goto end;
	}

	dw_dsi2h->trace_pid = current->tgid;

	return dw_dsi2h;

destroy_vdev:
	dw_mipi_dsi2h_destroy_vdev(dw_dsi2h);
disable_phy:
	phy_exit(dw_dsi2h->phy);
end:
	return ERR_PTR(ret);
}

void *dw_mipi_dsi2h_probe(struct platform_device *pdev,
			  const struct dw_mipi_dsi2h_plat_data *plat_data)
{
	return __dw_mipi_dsi2h_probe(pdev, plat_data);
}
EXPORT_SYMBOL_GPL(dw_mipi_dsi2h_probe);

int dw_mipi_dsi2h_remove(void *handle)
{
	struct dw_mipi_dsi2h *dsi2h = handle;

	dw_mipi_dsi2h_debugfs_remove(dsi2h);
	dw_mipi_dsi2h_sysfs_remove(dsi2h);
	dw_mipi_dsi2h_destroy_vdev(dsi2h);
	kfree(dsi2h);

	return 0;
}
EXPORT_SYMBOL_GPL(dw_mipi_dsi2h_remove);

int dw_mipi_dsi2h_bind(void *handle, struct drm_encoder *encoder, struct sscd_funcs *coredump_funcs)
{
	struct dw_mipi_dsi2h *dsi2h = handle;

	if (coredump_funcs) {
		coredump_funcs->sscd_payload_prepare = dsi_sscd_payload_prepare;
		coredump_funcs->sscd_payload_destroy = dsi_sscd_payload_destroy;
		dsi2h->coredump_funcs = coredump_funcs;
	}

	return drm_bridge_attach(encoder, &dsi2h->bridge, NULL, 0);
}
EXPORT_SYMBOL_GPL(dw_mipi_dsi2h_bind);

void dw_mipi_dsi2h_unbind(void *handle)
{
}
EXPORT_SYMBOL_GPL(dw_mipi_dsi2h_unbind);

bool is_dw_mipi_dsi2h_bridge(struct drm_bridge *bridge)
{
	return bridge && bridge->funcs == &dw_mipi_dsi2h_bridge_funcs;
}
EXPORT_SYMBOL_GPL(is_dw_mipi_dsi2h_bridge);

struct device *dw_mipi_dsi2h_get_device(struct drm_bridge *bridge)
{
	struct dw_mipi_dsi2h *dsi2h = bridge_to_dsi(bridge);

	return dsi2h->vdev;
}
EXPORT_SYMBOL_GPL(dw_mipi_dsi2h_get_device);

MODULE_DESCRIPTION("DW MIPI DSI2 Host Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("dw-mipi-dsi2h");
MODULE_AUTHOR("Marcelo Borges <marcelob@synopsys.com>");
MODULE_AUTHOR("Pedro Correia <correia@synopsys.com>");
MODULE_AUTHOR("Nuno Cardoso <cardoso@synopsys.com>");
MODULE_AUTHOR("Gustavo Pimentel <gustavo.pimentel@synopsys.com>");
