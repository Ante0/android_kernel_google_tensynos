// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include <linux/device.h>
#include <linux/sysfs.h>
#include <drm/drm_bridge.h>
#include <drm/drm_print.h>

#include "dw-mipi-dsi2h.h"

#define DSI2H_U64_SHOW(name)                                                                 \
	static ssize_t cntr_##name##_show(struct device *dev, struct device_attribute *attr, \
					  char *buf)                                         \
	{                                                                                    \
		struct dw_mipi_dsi2h *dsi2h = dev_get_drvdata(dev);                          \
		return sysfs_emit(buf, "%llu\n", dsi2h->int_cntrs.cntr_##name);              \
	}

DSI2H_U64_SHOW(int_phy_l0_erresc)
DSI2H_U64_SHOW(int_phy_l0_errsyncesc)
DSI2H_U64_SHOW(int_phy_l0_errcontrol)
DSI2H_U64_SHOW(int_phy_l0_errcontentionlp0)
DSI2H_U64_SHOW(int_phy_l0_errcontentionlp1)
DSI2H_U64_SHOW(int_txhs_fifo_over)
DSI2H_U64_SHOW(int_txhs_fifo_under)
DSI2H_U64_SHOW(int_err_to_hstx)
DSI2H_U64_SHOW(int_err_to_hstxrdy)
DSI2H_U64_SHOW(int_err_to_lprx)
DSI2H_U64_SHOW(int_err_to_lptxrdy)
DSI2H_U64_SHOW(int_err_to_lptxtrig)
DSI2H_U64_SHOW(int_err_to_lptxulps)
DSI2H_U64_SHOW(int_err_to_bta)
DSI2H_U64_SHOW(int_err_ack_rpt_0)
DSI2H_U64_SHOW(int_err_ack_rpt_1)
DSI2H_U64_SHOW(int_err_ack_rpt_2)
DSI2H_U64_SHOW(int_err_ack_rpt_3)
DSI2H_U64_SHOW(int_err_ack_rpt_4)
DSI2H_U64_SHOW(int_err_ack_rpt_5)
DSI2H_U64_SHOW(int_err_ack_rpt_6)
DSI2H_U64_SHOW(int_err_ack_rpt_7)
DSI2H_U64_SHOW(int_err_ack_rpt_8)
DSI2H_U64_SHOW(int_err_ack_rpt_9)
DSI2H_U64_SHOW(int_err_ack_rpt_10)
DSI2H_U64_SHOW(int_err_ack_rpt_11)
DSI2H_U64_SHOW(int_err_ack_rpt_12)
DSI2H_U64_SHOW(int_err_ack_rpt_13)
DSI2H_U64_SHOW(int_err_ack_rpt_14)
DSI2H_U64_SHOW(int_err_ack_rpt_15)
DSI2H_U64_SHOW(int_err_display_cmd_time)
DSI2H_U64_SHOW(int_err_ipi_dtype)
DSI2H_U64_SHOW(int_err_vid_bandwidth)
DSI2H_U64_SHOW(int_err_ipi_cmd)
DSI2H_U64_SHOW(int_err_display_cmd_ovfl)
DSI2H_U64_SHOW(int_ipi_event_fifo_over)
DSI2H_U64_SHOW(int_ipi_event_fifo_under)
DSI2H_U64_SHOW(int_ipi_pixel_fifo_over)
DSI2H_U64_SHOW(int_ipi_pixel_fifo_under)
DSI2H_U64_SHOW(int_err_pri_tx_time)
DSI2H_U64_SHOW(int_err_pri_tx_cmd)
DSI2H_U64_SHOW(int_err_cri_cmd_time)
DSI2H_U64_SHOW(int_err_cri_dtype)
DSI2H_U64_SHOW(int_err_cri_vchannel)
DSI2H_U64_SHOW(int_err_cri_rx_length)
DSI2H_U64_SHOW(int_err_cri_ecc)
DSI2H_U64_SHOW(int_err_cri_ecc_fatal)
DSI2H_U64_SHOW(int_err_cri_crc)
DSI2H_U64_SHOW(int_cmd_rd_pld_fifo_over)
DSI2H_U64_SHOW(int_cmd_rd_pld_fifo_under)
DSI2H_U64_SHOW(int_cmd_wr_pld_fifo_over)
DSI2H_U64_SHOW(int_cmd_wr_pld_fifo_under)
DSI2H_U64_SHOW(int_cmd_wr_hdr_fifo_over)
DSI2H_U64_SHOW(int_cmd_wr_hdr_fifo_under)

#define DSI2H_STAT_SHOW(name, enum_val)                                                      \
	static ssize_t stat_##name##_show(struct device *dev, struct device_attribute *attr, \
					  char *buf)                                         \
	{                                                                                    \
		struct dw_mipi_dsi2h *dsi2h = dev_get_drvdata(dev);                          \
		return sysfs_emit(buf, "%u\n", dsi2h->stat_data[enum_val]);                  \
	}

DSI2H_STAT_SHOW(cri_busy, ST_CRI_BUSY)
DSI2H_STAT_SHOW(pri_busy, ST_PRI_BUSY)
DSI2H_STAT_SHOW(ulps_exit_err, ST_ULPS_EXIT_ERR)
DSI2H_STAT_SHOW(ulps_enter_err, ST_ULPS_ENTER_ERR)
DSI2H_STAT_SHOW(dcs_read_ack_err, ST_DCS_READ_ACK_ERR)
DSI2H_STAT_SHOW(dcs_read_extra_data, ST_DCS_READ_EXTRA_DATA)
DSI2H_STAT_SHOW(dcs_read_data_not_arrived, ST_DCS_READ_DATA_NOT_ARRIVED)

#define DSI2H_U64_ATTR(name) DEVICE_ATTR_RO(cntr_##name)
#define DSI2H_STAT_ATTR(name) DEVICE_ATTR_RO(stat_##name)

static DSI2H_U64_ATTR(int_phy_l0_erresc);
static DSI2H_U64_ATTR(int_phy_l0_errsyncesc);
static DSI2H_U64_ATTR(int_phy_l0_errcontrol);
static DSI2H_U64_ATTR(int_phy_l0_errcontentionlp0);
static DSI2H_U64_ATTR(int_phy_l0_errcontentionlp1);
static DSI2H_U64_ATTR(int_txhs_fifo_over);
static DSI2H_U64_ATTR(int_txhs_fifo_under);
static DSI2H_U64_ATTR(int_err_to_hstx);
static DSI2H_U64_ATTR(int_err_to_hstxrdy);
static DSI2H_U64_ATTR(int_err_to_lprx);
static DSI2H_U64_ATTR(int_err_to_lptxrdy);
static DSI2H_U64_ATTR(int_err_to_lptxtrig);
static DSI2H_U64_ATTR(int_err_to_lptxulps);
static DSI2H_U64_ATTR(int_err_to_bta);
static DSI2H_U64_ATTR(int_err_ack_rpt_0);
static DSI2H_U64_ATTR(int_err_ack_rpt_1);
static DSI2H_U64_ATTR(int_err_ack_rpt_2);
static DSI2H_U64_ATTR(int_err_ack_rpt_3);
static DSI2H_U64_ATTR(int_err_ack_rpt_4);
static DSI2H_U64_ATTR(int_err_ack_rpt_5);
static DSI2H_U64_ATTR(int_err_ack_rpt_6);
static DSI2H_U64_ATTR(int_err_ack_rpt_7);
static DSI2H_U64_ATTR(int_err_ack_rpt_8);
static DSI2H_U64_ATTR(int_err_ack_rpt_9);
static DSI2H_U64_ATTR(int_err_ack_rpt_10);
static DSI2H_U64_ATTR(int_err_ack_rpt_11);
static DSI2H_U64_ATTR(int_err_ack_rpt_12);
static DSI2H_U64_ATTR(int_err_ack_rpt_13);
static DSI2H_U64_ATTR(int_err_ack_rpt_14);
static DSI2H_U64_ATTR(int_err_ack_rpt_15);
static DSI2H_U64_ATTR(int_err_display_cmd_time);
static DSI2H_U64_ATTR(int_err_ipi_dtype);
static DSI2H_U64_ATTR(int_err_vid_bandwidth);
static DSI2H_U64_ATTR(int_err_ipi_cmd);
static DSI2H_U64_ATTR(int_err_display_cmd_ovfl);
static DSI2H_U64_ATTR(int_ipi_event_fifo_over);
static DSI2H_U64_ATTR(int_ipi_event_fifo_under);
static DSI2H_U64_ATTR(int_ipi_pixel_fifo_over);
static DSI2H_U64_ATTR(int_ipi_pixel_fifo_under);
static DSI2H_U64_ATTR(int_err_pri_tx_time);
static DSI2H_U64_ATTR(int_err_pri_tx_cmd);
static DSI2H_U64_ATTR(int_err_cri_cmd_time);
static DSI2H_U64_ATTR(int_err_cri_dtype);
static DSI2H_U64_ATTR(int_err_cri_vchannel);
static DSI2H_U64_ATTR(int_err_cri_rx_length);
static DSI2H_U64_ATTR(int_err_cri_ecc);
static DSI2H_U64_ATTR(int_err_cri_ecc_fatal);
static DSI2H_U64_ATTR(int_err_cri_crc);
static DSI2H_U64_ATTR(int_cmd_rd_pld_fifo_over);
static DSI2H_U64_ATTR(int_cmd_rd_pld_fifo_under);
static DSI2H_U64_ATTR(int_cmd_wr_pld_fifo_over);
static DSI2H_U64_ATTR(int_cmd_wr_pld_fifo_under);
static DSI2H_U64_ATTR(int_cmd_wr_hdr_fifo_over);
static DSI2H_U64_ATTR(int_cmd_wr_hdr_fifo_under);

static DSI2H_STAT_ATTR(cri_busy);
static DSI2H_STAT_ATTR(pri_busy);
static DSI2H_STAT_ATTR(ulps_exit_err);
static DSI2H_STAT_ATTR(ulps_enter_err);
static DSI2H_STAT_ATTR(dcs_read_ack_err);
static DSI2H_STAT_ATTR(dcs_read_extra_data);
static DSI2H_STAT_ATTR(dcs_read_data_not_arrived);

static struct attribute *dw_mipi_dsi2h_error_irq_attrs[] = {
	&dev_attr_cntr_int_phy_l0_erresc.attr,
	&dev_attr_cntr_int_phy_l0_errsyncesc.attr,
	&dev_attr_cntr_int_phy_l0_errcontrol.attr,
	&dev_attr_cntr_int_phy_l0_errcontentionlp0.attr,
	&dev_attr_cntr_int_phy_l0_errcontentionlp1.attr,
	&dev_attr_cntr_int_txhs_fifo_over.attr,
	&dev_attr_cntr_int_txhs_fifo_under.attr,
	&dev_attr_cntr_int_err_to_hstx.attr,
	&dev_attr_cntr_int_err_to_hstxrdy.attr,
	&dev_attr_cntr_int_err_to_lprx.attr,
	&dev_attr_cntr_int_err_to_lptxrdy.attr,
	&dev_attr_cntr_int_err_to_lptxtrig.attr,
	&dev_attr_cntr_int_err_to_lptxulps.attr,
	&dev_attr_cntr_int_err_to_bta.attr,
	&dev_attr_cntr_int_err_ack_rpt_0.attr,
	&dev_attr_cntr_int_err_ack_rpt_1.attr,
	&dev_attr_cntr_int_err_ack_rpt_2.attr,
	&dev_attr_cntr_int_err_ack_rpt_3.attr,
	&dev_attr_cntr_int_err_ack_rpt_4.attr,
	&dev_attr_cntr_int_err_ack_rpt_5.attr,
	&dev_attr_cntr_int_err_ack_rpt_6.attr,
	&dev_attr_cntr_int_err_ack_rpt_7.attr,
	&dev_attr_cntr_int_err_ack_rpt_8.attr,
	&dev_attr_cntr_int_err_ack_rpt_9.attr,
	&dev_attr_cntr_int_err_ack_rpt_10.attr,
	&dev_attr_cntr_int_err_ack_rpt_11.attr,
	&dev_attr_cntr_int_err_ack_rpt_12.attr,
	&dev_attr_cntr_int_err_ack_rpt_13.attr,
	&dev_attr_cntr_int_err_ack_rpt_14.attr,
	&dev_attr_cntr_int_err_ack_rpt_15.attr,
	&dev_attr_cntr_int_err_display_cmd_time.attr,
	&dev_attr_cntr_int_err_ipi_dtype.attr,
	&dev_attr_cntr_int_err_vid_bandwidth.attr,
	&dev_attr_cntr_int_err_ipi_cmd.attr,
	&dev_attr_cntr_int_err_display_cmd_ovfl.attr,
	&dev_attr_cntr_int_ipi_event_fifo_over.attr,
	&dev_attr_cntr_int_ipi_event_fifo_under.attr,
	&dev_attr_cntr_int_ipi_pixel_fifo_over.attr,
	&dev_attr_cntr_int_ipi_pixel_fifo_under.attr,
	&dev_attr_cntr_int_err_pri_tx_time.attr,
	&dev_attr_cntr_int_err_pri_tx_cmd.attr,
	&dev_attr_cntr_int_err_cri_cmd_time.attr,
	&dev_attr_cntr_int_err_cri_dtype.attr,
	&dev_attr_cntr_int_err_cri_vchannel.attr,
	&dev_attr_cntr_int_err_cri_rx_length.attr,
	&dev_attr_cntr_int_err_cri_ecc.attr,
	&dev_attr_cntr_int_err_cri_ecc_fatal.attr,
	&dev_attr_cntr_int_err_cri_crc.attr,
	&dev_attr_cntr_int_cmd_rd_pld_fifo_over.attr,
	&dev_attr_cntr_int_cmd_rd_pld_fifo_under.attr,
	&dev_attr_cntr_int_cmd_wr_pld_fifo_over.attr,
	&dev_attr_cntr_int_cmd_wr_pld_fifo_under.attr,
	&dev_attr_cntr_int_cmd_wr_hdr_fifo_over.attr,
	&dev_attr_cntr_int_cmd_wr_hdr_fifo_under.attr,
	NULL,
};

static const struct attribute_group dw_mipi_dsi2h_error_irq_group = {
	.name = "error_irq",
	.attrs = dw_mipi_dsi2h_error_irq_attrs,
};

static struct attribute *dw_mipi_dsi2h_error_event_attrs[] = {
	&dev_attr_stat_cri_busy.attr,
	&dev_attr_stat_pri_busy.attr,
	&dev_attr_stat_ulps_exit_err.attr,
	&dev_attr_stat_ulps_enter_err.attr,
	&dev_attr_stat_dcs_read_ack_err.attr,
	&dev_attr_stat_dcs_read_extra_data.attr,
	&dev_attr_stat_dcs_read_data_not_arrived.attr,
	NULL,
};

static const struct attribute_group dw_mipi_dsi2h_error_event_group = {
	.name = "error_event",
	.attrs = dw_mipi_dsi2h_error_event_attrs,
};

static const struct attribute_group *dw_mipi_dsi2h_error_groups[] = {
	&dw_mipi_dsi2h_error_irq_group,
	&dw_mipi_dsi2h_error_event_group,
	NULL,
};

int dw_mipi_dsi2h_sysfs_init(struct dw_mipi_dsi2h *dsi2h)
{
	int ret;

	ret = sysfs_create_groups(&dsi2h->vdev->kobj, dw_mipi_dsi2h_error_groups);

	return ret;
}

void dw_mipi_dsi2h_sysfs_remove(struct dw_mipi_dsi2h *dsi2h)
{
	sysfs_remove_groups(&dsi2h->vdev->kobj, dw_mipi_dsi2h_error_groups);
}

MODULE_AUTHOR("Tai-Hua Tseng <taihua@google.com>");
MODULE_DESCRIPTION("Implementation of DSI Module sysfs Nodes.");
MODULE_LICENSE("Dual MIT/GPL");
