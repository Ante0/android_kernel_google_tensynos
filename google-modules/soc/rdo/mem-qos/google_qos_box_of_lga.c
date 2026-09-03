// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/minmax.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/types.h>

#include "google_qos_box_of.h"

static struct qos_box_dt_attr dt_attr[] = {
	/* QOS_OVRD_CFG */
	{ "google,arqos_ovrd_en", 0x0, 0x00000001, 0 },
	{ "google,arqos_ovrd_val", 0x0, 0x00000030, 4 },
	{ "google,awqos_ovrd_en", 0x0, 0x00000100, 8 },
	{ "google,awqos_ovrd_val", 0x0, 0x00003000, 12 },
	/* QOS_LATMOD_CFG */
	{ "google,arqos_latmod_en", 0x4, 0x00000001, 0 },
	{ "google,arqos_lat_step_th", 0x4, 0x0000FFF0, 4 },
	{ "google,awqos_latmod_en", 0x4, 0x00010000, 16 },
	{ "google,awqos_lat_step_th", 0x4, 0xFFF00000, 20 },
	/* QOS_BWMOD_CFG */
	{ "google,arqos_bwmod_en", 0x8, 0x00000001, 0 },
	{ "google,arqos_bw_step_th", 0x8, 0x0000FFF0, 4 },
	{ "google,awqos_bwmod_en", 0x8, 0x00010000, 16 },
	{ "google,awqos_bw_step_th", 0x8, 0xFFF00000, 20 },
	/* QOS_URGOVRD_CFG */
	{ "google,arqos_urgovrd_en", 0xC, 0x00000001, 0 },
	{ "google,awqos_urgovrd_en", 0xC, 0x00000010, 4 },
	/* URG_OVRD_CFG */
	{ "google,rurglvl_ovrd_en", 0x10, 0x00000001, 0 },
	{ "google,rurglvl_ovrd_val", 0x10, 0x00000030, 4 },
	{ "google,wurglvl_ovrd_en", 0x10, 0x00000100, 8 },
	{ "google,wurglvl_ovrd_val", 0x10, 0x00003000, 12 },
	/* URG_LATMOD_CFG */
	{ "google,rurglvl_latmod_en", 0x14, 0x00000001, 0 },
	{ "google,rurglvl_lat_step_th", 0x14, 0x0000FFF0, 4 },
	{ "google,wurglvl_latmod_en", 0x14, 0x00010000, 16 },
	{ "google,wurglvl_lat_step_th", 0x14, 0xFFF00000, 20 },
	/* URG_BWMOD_CFG */
	{ "google,rurglvl_bwmod_en", 0x18, 0x00000001, 0 },
	{ "google,rurglvl_bw_step_th", 0x18, 0x0000FFF0, 4 },
	{ "google,wurglvl_bwmod_en", 0x18, 0x00010000, 16 },
	{ "google,wurglvl_bw_step_th", 0x18, 0xFFF00000, 20 },
	/* MO_LIMIT_CFG */
	{ "google,rdmo_limit_en", 0x1c, 0x00000001, 0 },
	{ "google,wrmo_limit_en", 0x1c, 0x00000002, 1 },
	/* RDMO_LIMIT_CFG */
	{ "google,rdmo_limit_trtl0", 0x20, 0x000000FF, 0 },
	{ "google,rdmo_limit_trtl1", 0x20, 0x0000FF00, 8 },
	{ "google,rdmo_limit_trtl2", 0x20, 0x00FF0000, 16 },
	{ "google,rdmo_limit_trtl3", 0x20, 0xFF000000, 24 },
	/* WRMO_LIMIT_CFG */
	{ "google,wrmo_limit_trtl0", 0x24, 0x000000FF, 0 },
	{ "google,wrmo_limit_trtl1", 0x24, 0x0000FF00, 8 },
	{ "google,wrmo_limit_trtl2", 0x24, 0x00FF0000, 16 },
	{ "google,wrmo_limit_trtl3", 0x24, 0xFF000000, 24 },
	/* BW_LIMIT_CFG */
	{ "google,rdbw_limit_en", 0x28, 0x00000001, 0 },
	{ "google,wrbw_limit_en", 0x28, 0x00000010, 4 },
	/* RDBW_LIMIT_CTRL[0] */
	{ "google,rdbw_slot_limit_trtl_0", 0x2c, 0x0000FFFF, 0 },
	{ "google,rdbw_window_limit_trtl_0", 0x2c, 0xFFFF0000, 16 },
	/* RDBW_LIMIT_CTRL[1] */
	{ "google,rdbw_slot_limit_trtl_1", 0x30, 0x0000FFFF, 0 },
	{ "google,rdbw_window_limit_trtl_1", 0x30, 0xFFFF0000, 16 },
	/* RDBW_LIMIT_CTRL[2] */
	{ "google,rdbw_slot_limit_trtl_2", 0x34, 0x0000FFFF, 0 },
	{ "google,rdbw_window_limit_trtl_2", 0x34, 0xFFFF0000, 16 },
	/* RDBW_LIMIT_CTRL[3] */
	{ "google,rdbw_slot_limit_trtl_3", 0x38, 0x0000FFFF, 0 },
	{ "google,rdbw_window_limit_trtl_3", 0x38, 0xFFFF0000, 16 },
	/* WRBW_LIMIT_CTRL[0] */
	{ "google,wrbw_slot_limit_trtl_0", 0x3c, 0x0000FFFF, 0 },
	{ "google,wrbw_window_limit_trtl_0", 0x3c, 0xFFFF0000, 16 },
	/* WRBW_LIMIT_CTRL[1] */
	{ "google,wrbw_slot_limit_trtl_1", 0x40, 0x0000FFFF, 0 },
	{ "google,wrbw_window_limit_trtl_1", 0x40, 0xFFFF0000, 16 },
	/* WRBW_LIMIT_CTRL[2] */
	{ "google,wrbw_slot_limit_trtl_2", 0x44, 0x0000FFFF, 0 },
	{ "google,wrbw_window_limit_trtl_2", 0x44, 0xFFFF0000, 16 },
	/* WRBW_LIMIT_CTRL[3] */
	{ "google,wrbw_slot_limit_trtl_3", 0x48, 0x0000FFFF, 0 },
	{ "google,wrbw_window_limit_trtl_3", 0x48, 0xFFFF0000, 16 },
	/* RGLTR_RD_CFG */
	{ "google,arqos_rgltr_en", 0x4c, 0x00000001, 0 },
	{ "google,arqos_rgltr_val", 0x4c, 0x00000030, 4 },
	{ "google,rgltr_rdbw_gap_en", 0x4c, 0x00000100, 8 },
	{ "google,rgltr_rdreq_gap", 0x4c, 0x000FF000, 12 },
	/* RGLTR_WR_CFG */
	{ "google,awqos_rgltr_en", 0x50, 0x00000001, 0 },
	{ "google,awqos_rgltr_val", 0x50, 0x00000030, 4 },
	{ "google,rgltr_wrbw_gap_en", 0x50, 0x00000100, 8 },
	{ "google,rgltr_wrreq_gap", 0x50, 0x000FF000, 12 },
	/* RGLTR_BW_CTRL[0] */
	{ "google,rgltr_rdbw_th_trtl_0", 0x54, 0x0000FFFF, 0 },
	{ "google,rgltr_wrbw_th_trtl_0", 0x54, 0xFFFF0000, 16 },
	/* RGLTR_BW_CTRL[1] */
	{ "google,rgltr_rdbw_th_trtl_1", 0x58, 0x0000FFFF, 0 },
	{ "google,rgltr_wrbw_th_trtl_1", 0x58, 0xFFFF0000, 16 },
	/* RGLTR_BW_CTRL[2] */
	{ "google,rgltr_rdbw_th_trtl_2", 0x5c, 0x0000FFFF, 0 },
	{ "google,rgltr_wrbw_th_trtl_2", 0x5c, 0xFFFF0000, 16 },
	/* RGLTR_BW_CTRL[3] */
	{ "google,rgltr_rdbw_th_trtl_3", 0x60, 0x0000FFFF, 0 },
	{ "google,rgltr_wrbw_th_trtl_3", 0x60, 0xFFFF0000, 16 },
};

int of_qos_box_read_policy_single(struct qos_box_dev *qos_box_dev,
				  struct qos_box_policy *policy, struct device_node *np)
{
	struct qos_box_dt_attr *attr;
	u32 idx;
	u32 val;
	int ret;

	for (idx = 0; idx < ARRAY_SIZE(dt_attr); idx++) {
		attr = &dt_attr[idx];
		ret = of_qos_box_read_u32(qos_box_dev, np, attr->propname, &val);
		if (ret)
			return ret;

		policy->val[attr->offset >> 2] |= ((val << attr->bitshift) & attr->bitmask);
	}

	return 0;
}

int of_qos_box_read_vc_map_cfg(struct qos_box_dev *qos_box_dev, struct device_node *np)
{
	const char *propname = "google,vc_map_cfg";
	struct device *dev = qos_box_dev->dev;
	struct qcfg *config;
	int ret;

	/*
	 * google,vc_map_cfg is optional,
	 * qos_box driver only write VC_MAP_CFG value during probe when the property exists
	 */
	if (!of_property_present(np, propname)) {
		qos_box_dev->have_vc_map_cfg_init_val = false;
		return 0;
	}

	qos_box_dev->have_vc_map_cfg_init_val = true;

	config = &qos_box_dev->config;

	ret = of_property_read_u32(np, propname, &config->vc_map_cfg.val);
	if (ret < 0) {
		dev_err(dev, "Read %s property failed, ret = %d\n", propname, ret);
		return -EINVAL;
	}

	return 0;
}

