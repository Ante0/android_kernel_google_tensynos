// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020-2021 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare MIPI DSI 2 host - Debugfs Interface
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

#include <video/mipi_display.h>

#include <drm/bridge/dw_mipi_dsi2h.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_print.h>

#include "dw-mipi-dsi2h.h"

#if IS_ENABLED(CONFIG_DEBUG_FS)

static int dw_mipi_debugfs_u32_get(void *data, u64 *val)
{
	u32 tmp = 0;
	struct regmap_field *reg_field = (struct regmap_field *)data;

	regmap_field_read(reg_field, &tmp);
	*val = (u64)tmp;

	return 0;
}

static int dw_mipi_debugfs_u32_set(void *data, u64 val)
{
	struct regmap_field *reg_field = (struct regmap_field *)data;

	regmap_field_write(reg_field, val);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_x32, dw_mipi_debugfs_u32_get, dw_mipi_debugfs_u32_set, "%llu\n");

#define REGISTER(reg)									    \
	do {										    \
		if (!debugfs_create_file_unsafe(#reg, 0444, dsi2h->debugfs,		    \
						dsi2h->field_##reg, &fops_x32)) {	    \
			dev_warn(dsi2h->dev, "failed to create entry for " #reg "\n");	    \
		}									    \
	} while (0)

void dw_create_debugfs_hwv_files(struct dw_mipi_dsi2h *dsi2h)
{
	REGISTER(core_id);
	REGISTER(ver_number);
	REGISTER(type_num);
	REGISTER(pkg_num);
	REGISTER(type_enum);
	REGISTER(pwr_up);
	//REGISTER(int_st_phy);
	//REGISTER(int_st_to);
	//REGISTER(int_st_ack);
	//REGISTER(int_st_ipi);
	//REGISTER(int_st_pri);
	//REGISTER(int_st_cri);
	REGISTER(mode_ctrl);
	REGISTER(mode_status);
	REGISTER(core_busy);
	REGISTER(core_fifos_not_empty);
	REGISTER(ipi_busy);
	REGISTER(ipi_fifos_not_empty);
	REGISTER(cri_busy);
	REGISTER(cri_wr_fifos_not_empty);
	REGISTER(cri_rd_data_avail);
	REGISTER(pri_busy);
	REGISTER(pri_tx_fifos_not_empty);
	REGISTER(pri_rx_data_avail);
	REGISTER(manual_mode_en);
	REGISTER(fsm_selector);
	REGISTER(current_state);
	REGISTER(stuck);
	REGISTER(previous_state);
	REGISTER(current_state_cnt);
	REGISTER(fsm_manual_init);
	REGISTER(fifo_selector);
	REGISTER(empty);
	REGISTER(almost_empty);
	REGISTER(half_full);
	REGISTER(almost_full);
	REGISTER(full);
	REGISTER(current_word_count);
	REGISTER(fifo_manual_init);
	REGISTER(to_hstx_value);
	REGISTER(to_hstxrdy_value);
	REGISTER(to_lprx_value);
	REGISTER(to_lptxrdy_value);
	REGISTER(to_lptxtrig_value);
	REGISTER(to_lptxulps_value);
	REGISTER(to_bta_value);
	REGISTER(phy_type);
	REGISTER(phy_lanes);
	REGISTER(ppi_width);
	REGISTER(hs_transferen_en);
	REGISTER(clk_type);
	REGISTER(phy_lptx_clk_div);
	REGISTER(phy_direction);
	REGISTER(phy_clk_stopstate);
	REGISTER(phy_l0_stopstate);
	REGISTER(phy_l1_stopstate);
	REGISTER(phy_l2_stopstate);
	REGISTER(phy_l3_stopstate);
	REGISTER(phy_clk_ulpsactivenot);
	REGISTER(phy_l0_ulpsactivenot);
	REGISTER(phy_l1_ulpsactivenot);
	REGISTER(phy_l2_ulpsactivenot);
	REGISTER(phy_l3_ulpsactivenot);
	REGISTER(phy_lp2hs_time);
	REGISTER(phy_lp2hs_time_auto);
	REGISTER(phy_hs2lp_time);
	REGISTER(phy_hs2lp_time_auto);
	REGISTER(phy_max_rd_time);
	REGISTER(phy_max_rd_time_auto);
	REGISTER(phy_esc_cmd_time);
	REGISTER(phy_esc_cmd_time_auto);
	REGISTER(phy_esc_byte_time);
	REGISTER(phy_esc_byte_time_auto);
	REGISTER(phy_ipi_ratio);
	REGISTER(phy_ipi_ratio_auto);
	REGISTER(phy_sys_ratio);
	REGISTER(phy_sys_ratio_auto);
	REGISTER(phy_tx_trigger_0);
	REGISTER(phy_tx_trigger_1);
	REGISTER(phy_tx_trigger_2);
	REGISTER(phy_tx_trigger_3);
	REGISTER(phy_deskewcal);
	REGISTER(phy_alternatecal);
	REGISTER(phy_ulps_entry);
	REGISTER(phy_ulps_exit);
	REGISTER(phy_bta);
	//REGISTER(phy_rx_trigger_0);
	//REGISTER(phy_rx_trigger_1);
	//REGISTER(phy_rx_trigger_2);
	//REGISTER(phy_rx_trigger_3);
	REGISTER(phy_cal_time);
	REGISTER(phy_ulps_data_lanes);
	REGISTER(phy_ulps_clk_lane);
	REGISTER(phy_wakeup_time);
	REGISTER(eotp_tx_en);
	REGISTER(bta_en);
	REGISTER(tx_vcid);
	REGISTER(scrambling_en);
	REGISTER(scrambling_seed);
	REGISTER(vid_mode_type);
	REGISTER(blk_hsa_hs_en);
	REGISTER(blk_hbp_hs_en);
	REGISTER(blk_hfp_hs_en);
	REGISTER(blk_vsa_hs_en);
	REGISTER(blk_vbp_hs_en);
	REGISTER(blk_vfp_hs_en);
	REGISTER(lpdt_display_cmd_en);
	REGISTER(max_rt_pkt_sz);
	REGISTER(auto_tear_bta_disable);
	REGISTER(te_type_hw);
	REGISTER(set_tear_on_args_hw);
	REGISTER(set_tear_scanline_args_hw);
	//REGISTER(data_type_tx); /* data_type - 2 */
	//REGISTER(virtual_channel_tx); /* virtual_channel - 2 */
	//REGISTER(wc_lsb_tx); /* wc_lsb - 2 */
	//REGISTER(wc_msb_tx); /* wc_msb - 2 */
	//REGISTER(cmd_tx_mode);
	//REGISTER(cmd_hdr_rd);
	//REGISTER(cmd_hdr_long);
	//REGISTER(byte_0_tx); /* byte_0 - 2 */
	//REGISTER(byte_1_tx); /* byte_1 - 2 */
	//REGISTER(byte_2_tx); /* byte_2 - 2 */
	//REGISTER(byte_3_tx); /* byte_3 - 2 */
	//REGISTER(data_type_rx); /* data_type - 2 */
	//REGISTER(virtual_channel_rx); /* virtual_channel - 2 */
	//REGISTER(wc_lsb_rx); /* wc_lsb - 2 */
	//REGISTER(wc_msb_rx); /* wc_msb - 2 */
	//REGISTER(byte_0_rx); /* byte_0 - 2 */
	//REGISTER(byte_1_rx); /* byte_1 - 2 */
	//REGISTER(byte_2_rx); /* byte_2 - 2 */
	//REGISTER(byte_3_rx); /* byte_3 - 2 */
	REGISTER(ipi_format);
	REGISTER(ipi_depth);
	REGISTER(vid_hsa_time);
	REGISTER(vid_hsa_time_auto);
	REGISTER(vid_hbp_time);
	REGISTER(vid_hbp_time_auto);
	REGISTER(vid_hact_time);
	REGISTER(vid_hact_time_auto);
	REGISTER(vid_hline_time);
	REGISTER(vid_hline_time_auto);
	REGISTER(vid_vsa_lines);
	REGISTER(vid_vsa_lines_auto);
	REGISTER(vid_vbp_lines);
	REGISTER(vid_vbp_lines_auto);
	REGISTER(vid_vact_lines);
	REGISTER(vid_vact_lines_auto);
	REGISTER(vid_vfp_lines);
	REGISTER(vid_vfp_lines_auto);
	REGISTER(max_pix_pkt);
	REGISTER(hib_type);
	REGISTER(hib_ulps_wakeup_time);
	//REGISTER(phy_l0_erresc);
	//REGISTER(phy_l0_errsyncesc);
	//REGISTER(phy_l0_errcontrol);
	//REGISTER(phy_l0_errcontentionlp0);
	//REGISTER(phy_l0_errcontentionlp1);
	//REGISTER(txhs_fifo_over);
	//REGISTER(txhs_fifo_under);
	REGISTER(mask_phy_l0_erresc);
	REGISTER(mask_phy_l0_errsyncesc);
	REGISTER(mask_phy_l0_errcontrol);
	REGISTER(mask_phy_l0_errcontentionlp0);
	REGISTER(mask_phy_l0_errcontentionlp1);
	REGISTER(mask_txhs_fifo_over);
	REGISTER(mask_txhs_fifo_under);
	REGISTER(force_phy_l0_erresc);
	REGISTER(force_phy_l0_errsyncesc);
	REGISTER(force_phy_l0_errcontrol);
	REGISTER(force_phy_l0_errcontentionlp0);
	REGISTER(force_phy_l0_errcontentionlp1);
	REGISTER(force_txhs_fifo_over);
	REGISTER(force_txhs_fifo_under);
	//REGISTER(err_to_hstx);
	//REGISTER(err_to_hstxrdy);
	//REGISTER(err_to_lprx);
	//REGISTER(err_to_lptxrdy);
	//REGISTER(err_to_lptxtrig);
	//REGISTER(err_to_lptxulps);
	//REGISTER(err_to_bta);
	REGISTER(mask_err_to_hstx);
	REGISTER(mask_err_to_hstxrdy);
	REGISTER(mask_err_to_lprx);
	REGISTER(mask_err_to_lptxrdy);
	REGISTER(mask_err_to_lptxtrig);
	REGISTER(mask_err_to_lptxulps);
	REGISTER(mask_err_to_bta);
	REGISTER(force_err_to_hstx);
	REGISTER(force_err_to_hstxrdy);
	REGISTER(force_err_to_lprx);
	REGISTER(force_err_to_lptxrdy);
	REGISTER(force_err_to_lptxtrig);
	REGISTER(force_err_to_lptxulps);
	REGISTER(force_err_to_bta);
	//REGISTER(err_ack_rpt_0);
	//REGISTER(err_ack_rpt_1);
	//REGISTER(err_ack_rpt_2);
	//REGISTER(err_ack_rpt_3);
	//REGISTER(err_ack_rpt_4);
	//REGISTER(err_ack_rpt_5);
	//REGISTER(err_ack_rpt_6);
	//REGISTER(err_ack_rpt_7);
	//REGISTER(err_ack_rpt_8);
	//REGISTER(err_ack_rpt_9);
	//REGISTER(err_ack_rpt_10);
	//REGISTER(err_ack_rpt_11);
	//REGISTER(err_ack_rpt_12);
	//REGISTER(err_ack_rpt_13);
	//REGISTER(err_ack_rpt_14);
	//REGISTER(err_ack_rpt_15);
	REGISTER(mask_err_ack_rpt_0);
	REGISTER(mask_err_ack_rpt_1);
	REGISTER(mask_err_ack_rpt_2);
	REGISTER(mask_err_ack_rpt_3);
	REGISTER(mask_err_ack_rpt_4);
	REGISTER(mask_err_ack_rpt_5);
	REGISTER(mask_err_ack_rpt_6);
	REGISTER(mask_err_ack_rpt_7);
	REGISTER(mask_err_ack_rpt_8);
	REGISTER(mask_err_ack_rpt_9);
	REGISTER(mask_err_ack_rpt_10);
	REGISTER(mask_err_ack_rpt_11);
	REGISTER(mask_err_ack_rpt_12);
	REGISTER(mask_err_ack_rpt_13);
	REGISTER(mask_err_ack_rpt_14);
	REGISTER(mask_err_ack_rpt_15);
	REGISTER(force_err_ack_rpt_0);
	REGISTER(force_err_ack_rpt_1);
	REGISTER(force_err_ack_rpt_2);
	REGISTER(force_err_ack_rpt_3);
	REGISTER(force_err_ack_rpt_4);
	REGISTER(force_err_ack_rpt_5);
	REGISTER(force_err_ack_rpt_6);
	REGISTER(force_err_ack_rpt_7);
	REGISTER(force_err_ack_rpt_8);
	REGISTER(force_err_ack_rpt_9);
	REGISTER(force_err_ack_rpt_10);
	REGISTER(force_err_ack_rpt_11);
	REGISTER(force_err_ack_rpt_12);
	REGISTER(force_err_ack_rpt_13);
	REGISTER(force_err_ack_rpt_14);
	REGISTER(force_err_ack_rpt_15);
	//REGISTER(err_display_cmd_time);
	//REGISTER(err_ipi_dtype);
	//REGISTER(err_vid_bandwidth);
	//REGISTER(err_ipi_cmd);
	//REGISTER(err_display_cmd_ovfl);
	//REGISTER(ipi_event_fifo_over);
	//REGISTER(ipi_event_fifo_under);
	//REGISTER(ipi_pixel_fifo_over);
	//REGISTER(ipi_pixel_fifo_under);
	REGISTER(mask_err_display_cmd_time);
	REGISTER(mask_err_ipi_dtype);
	REGISTER(mask_err_vid_bandwidth);
	REGISTER(mask_err_ipi_cmd);
	REGISTER(mask_err_display_cmd_ovfl);
	REGISTER(mask_ipi_event_fifo_over);
	REGISTER(mask_ipi_event_fifo_under);
	REGISTER(mask_ipi_pixel_fifo_over);
	REGISTER(mask_ipi_pixel_fifo_under);
	REGISTER(force_err_display_cmd_time);
	REGISTER(force_err_ipi_dtype);
	REGISTER(force_err_vid_bandwidth);
	REGISTER(force_err_ipi_cmd);
	REGISTER(force_err_display_cmd_ovfl);
	REGISTER(force_ipi_event_fifo_over);
	REGISTER(force_ipi_event_fifo_under);
	REGISTER(force_ipi_pixel_fifo_over);
	REGISTER(force_ipi_pixel_fifo_under);
	//REGISTER(err_pri_tx_time);
	//REGISTER(err_pri_tx_cmd);
	REGISTER(mask_err_pri_tx_time);
	REGISTER(mask_err_pri_tx_cmd);
	REGISTER(force_err_pri_tx_time);
	REGISTER(force_err_pri_tx_cmd);
	//REGISTER(err_cri_cmd_time);
	//REGISTER(err_cri_dtype);
	//REGISTER(err_cri_vchannel);
	//REGISTER(err_cri_rx_length);
	//REGISTER(err_cri_ecc);
	//REGISTER(err_cri_ecc_fatal);
	//REGISTER(err_cri_crc);
	//REGISTER(cmd_rd_pld_fifo_over);
	//REGISTER(cmd_rd_pld_fifo_under);
	//REGISTER(cmd_wr_pld_fifo_over);
	//REGISTER(cmd_wr_pld_fifo_under);
	//REGISTER(cmd_wr_hdr_fifo_over);
	//REGISTER(cmd_wr_hdr_fifo_under);
	REGISTER(mask_err_cri_cmd_time);
	REGISTER(mask_err_cri_dtype);
	REGISTER(mask_err_cri_vchannel);
	REGISTER(mask_err_cri_rx_length);
	REGISTER(mask_err_cri_ecc);
	REGISTER(mask_err_cri_ecc_fatal);
	REGISTER(mask_err_cri_crc);
	REGISTER(mask_cmd_rd_pld_fifo_over);
	REGISTER(mask_cmd_rd_pld_fifo_under);
	REGISTER(mask_cmd_wr_pld_fifo_over);
	REGISTER(mask_cmd_wr_pld_fifo_under);
	REGISTER(mask_cmd_wr_hdr_fifo_over);
	REGISTER(mask_cmd_wr_hdr_fifo_under);
	REGISTER(force_err_cri_cmd_time);
	REGISTER(force_err_cri_dtype);
	REGISTER(force_err_cri_vchannel);
	REGISTER(force_err_cri_rx_length);
	REGISTER(force_err_cri_ecc);
	REGISTER(force_err_cri_ecc_fatal);
	REGISTER(force_err_cri_crc);
	REGISTER(force_cmd_rd_pld_fifo_over);
	REGISTER(force_cmd_rd_pld_fifo_under);
	REGISTER(force_cmd_wr_pld_fifo_over);
	REGISTER(force_cmd_wr_pld_fifo_under);
	REGISTER(force_cmd_wr_hdr_fifo_over);
	REGISTER(force_cmd_wr_hdr_fifo_under);
}

u32 dw_ctrl_read_dbgfs(struct dw_mipi_dsi2h *dsi2h, int offset)
{
	//return dsi2h_read(dsi2h, offset);
	return 0;
}

struct drm_display_mode *dw_dsi2h_export_display(struct dw_mipi_dsi2h *dsi2h)
{
	return &dsi2h->mode;
}

struct mipi_dsi_device *dw_dsi2h_export_device(struct dw_mipi_dsi2h *dsi2h)
{
	return dsi2h->dsi_device;
}

union phy_configure_opts *dw_dsi2h_export_phy_opts(struct dw_mipi_dsi2h *dsi2h)
{
	return &dsi2h->cfg.phy_opts;
}

int dw_dsi2h_reconfigure(struct dw_mipi_dsi2h *dsi2h)
{
	if (dsi2h->reconf) {
		dsi2h->bridge.funcs->disable(&dsi2h->bridge);

		dsi2h->bridge.funcs->mode_set(&dsi2h->bridge, &dsi2h->mode,
					      &dsi2h->mode);

		dsi2h->bridge.funcs->enable(&dsi2h->bridge);
		dsi2h->reconf = 0;
	} else {
		dw_dsi2h_update(dsi2h);
	}

	return 0;
}

#endif

MODULE_DESCRIPTION("Synopsys DW MIPI DSI Host 2 Controller driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marcelo Borges <marcelob@synopsys.com>");
MODULE_AUTHOR("Pedro Correia <correia@synopsys.com>");
MODULE_AUTHOR("Nuno Cardoso <cardoso@synopsys.com>");
MODULE_AUTHOR("Gustavo Pimentel <gustavo.pimentel@synopsys.com>");
