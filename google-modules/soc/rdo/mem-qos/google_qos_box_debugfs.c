// SPDX-License-Identifier: GPL-2.0-only
/*
 * qos debugfs support
 */

#include <linux/debugfs.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include "google_qos_box_debugfs.h"
#include "google_qos_box_reg.h"

static int control_set(void *data, u64 val)
{
	struct qos_box_dbg *dbg = (struct qos_box_dbg *)data;
	struct qos_box_dev *qos_box_dev = dbg->qos_box_dev;
	u8 prev_ctl;
	unsigned long flags;
	int ret = 0;

	ret = qos_box_rpm_get(qos_box_dev);
	if (ret)
		goto out;

	spin_lock_irqsave(&qos_box_dev->lock, flags);

	prev_ctl = dbg->ctl;
	dbg->ctl = (u8)(val >= 1);

	if (prev_ctl && !dbg->ctl) {
		if (dbg->restore_when_release_ctl)
			qos_box_setting_restore(dbg->qos_box_dev);
	}

	spin_unlock_irqrestore(&qos_box_dev->lock, flags);

	qos_box_rpm_put(qos_box_dev);

out:
	return ret;
}

static int control_get(void *data, u64 *val)
{
	struct qos_box_dbg *dbg = (struct qos_box_dbg *)data;
	struct qos_box_dev *qos_box_dev = dbg->qos_box_dev;
	unsigned long flags;

	spin_lock_irqsave(&qos_box_dev->lock, flags);

	*val = (u64)dbg->ctl;

	spin_unlock_irqrestore(&qos_box_dev->lock, flags);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(control, control_get, control_set, "%llu");

static int power_vote_set(void *data, u64 val)
{
	struct qos_box_dbg *dbg = (struct qos_box_dbg *)data;
	struct qos_box_dev *qos_box_dev = dbg->qos_box_dev;
	u8 prev_power_vote;
	int ret = 0;

	mutex_lock(&dbg->mutex);

	prev_power_vote = dbg->power_vote;
	dbg->power_vote = (u8)(val >= 1);

	if (prev_power_vote && !dbg->power_vote) {
		qos_box_rpm_put(qos_box_dev);
	} else if (!prev_power_vote && dbg->power_vote) {
		ret = qos_box_rpm_get(qos_box_dev);
	}

	mutex_unlock(&dbg->mutex);

	return ret;
}

static int power_vote_get(void *data, u64 *val)
{
	struct qos_box_dbg *dbg = (struct qos_box_dbg *)data;

	mutex_lock(&dbg->mutex);

	*val = (u64)dbg->power_vote;

	mutex_unlock(&dbg->mutex);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(power_vote, power_vote_get, power_vote_set, "%llu");

static int restore_release_control_set(void *data, u64 val)
{
	struct qos_box_dbg *dbg = (struct qos_box_dbg *)data;
	struct qos_box_dev *qos_box_dev = dbg->qos_box_dev;
	unsigned long flags;

	spin_lock_irqsave(&qos_box_dev->lock, flags);

	dbg->restore_when_release_ctl = (u8)(val >= 1);

	spin_unlock_irqrestore(&qos_box_dev->lock, flags);

	return 0;
}

static int restore_release_control_get(void *data, u64 *val)
{
	struct qos_box_dbg *dbg = (struct qos_box_dbg *)data;
	struct qos_box_dev *qos_box_dev = dbg->qos_box_dev;
	unsigned long flags;

	spin_lock_irqsave(&qos_box_dev->lock, flags);

	*val = (u64)dbg->restore_when_release_ctl;

	spin_unlock_irqrestore(&qos_box_dev->lock, flags);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(restore_release_control, restore_release_control_get,
			 restore_release_control_set, "%llu");

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(vc_map_cfg, VC_MAP_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(vc0_map, VC_MAP_CFG, 0x0000000F, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(vc1_map, VC_MAP_CFG, 0x000000F0, 4);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(vc2_map, VC_MAP_CFG, 0x00000F00, 8);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(vc3_map, VC_MAP_CFG, 0x0000F000, 12);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(vc4_map, VC_MAP_CFG, 0x000F0000, 16);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(vc5_map, VC_MAP_CFG, 0x00F00000, 20);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(vc6_map, VC_MAP_CFG, 0x0F000000, 24);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(qos_policy_cfg, QOS_POLICY_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(load_en, QOS_POLICY_CFG, 0x00000001, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(index_sel, QOS_POLICY_CFG, 0x00000030, 4);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(bw_mon_cfg, BW_MON_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(cycles_per_slot, BW_MON_CFG, 0x000003FF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(slots_per_window, BW_MON_CFG, 0x000F0000, 16);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(slots_per_urgmod, BW_MON_CFG, 0x0F000000, 24);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(vc_filter_cfg, VC_FILTER_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(rd_mon_vc_fltr, VC_FILTER_CFG, 0x0000001F, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(rd_mon_vc_fltr_lga, VC_FILTER_CFG, 0x0000007F, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(wr_mon_vc_fltr, VC_FILTER_CFG, 0x001F0000, 16);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE(wr_mon_vc_fltr_lga, VC_FILTER_CFG, 0x007F0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(qos_box_status, QOS_BOX_STATUS, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(policy_index, QOS_BOX_STATUS, 0x00000003, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(rd_vc_active, QOS_BOX_STATUS, 0x000001F0, 4);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(wr_vc_active, QOS_BOX_STATUS, 0x0001F000, 12);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(rd_urglvl_ingress, QOS_BOX_STATUS, 0x03000000, 24);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(wr_urglvl_ingress, QOS_BOX_STATUS, 0x0C000000, 26);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(rd_trtlvl, QOS_BOX_STATUS, 0x30000000, 28);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(wr_trtlvl, QOS_BOX_STATUS, 0xC0000000, 30);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(qos_box_urg_status, QOS_BOX_URG_STATUS, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(rd_urglvl_egress, QOS_BOX_URG_STATUS, 0x000003FF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(wr_urglvl_egress, QOS_BOX_URG_STATUS, 0x03FF0000, 16);

/* RDO */
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_rd, BW_MON_STATUS_RD, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_rd, BW_MON_STATUS_RD, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_rd, BW_MON_STATUS_RD, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_wr, BW_MON_STATUS_WR, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_wr, BW_MON_STATUS_WR, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_wr, BW_MON_STATUS_WR, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(lat_mon_status_rd, LAT_MON_STATUS_RD, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(slot_latency_rd, LAT_MON_STATUS_RD, 0x00FFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(max_outstanding_rd, LAT_MON_STATUS_RD, 0xFF000000, 24);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(lat_mon_status_wr, LAT_MON_STATUS_WR, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(slot_latency_wr, LAT_MON_STATUS_WR, 0x00FFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(max_outstanding_wr, LAT_MON_STATUS_WR, 0xFF000000, 24);

/* LGA */
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_rd_0, BW_MON_STATUS_RD_LGA + 0x0, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_rd_0, BW_MON_STATUS_RD_LGA + 0x0, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_rd_0, BW_MON_STATUS_RD_LGA + 0x0, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_rd_1, BW_MON_STATUS_RD_LGA + 0x4, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_rd_1, BW_MON_STATUS_RD_LGA + 0x4, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_rd_1, BW_MON_STATUS_RD_LGA + 0x4, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_rd_2, BW_MON_STATUS_RD_LGA + 0x8, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_rd_2, BW_MON_STATUS_RD_LGA + 0x8, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_rd_2, BW_MON_STATUS_RD_LGA + 0x8, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_rd_3, BW_MON_STATUS_RD_LGA + 0xc, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_rd_3, BW_MON_STATUS_RD_LGA + 0xc, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_rd_3, BW_MON_STATUS_RD_LGA + 0xc, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_rd_4, BW_MON_STATUS_RD_LGA + 0x10, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_rd_4, BW_MON_STATUS_RD_LGA + 0x10, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_rd_4, BW_MON_STATUS_RD_LGA + 0x10, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_rd_5, BW_MON_STATUS_RD_LGA + 0x14, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_rd_5, BW_MON_STATUS_RD_LGA + 0x14, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_rd_5, BW_MON_STATUS_RD_LGA + 0x14, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_rd_6, BW_MON_STATUS_RD_LGA + 0x18, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_rd_6, BW_MON_STATUS_RD_LGA + 0x18, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_rd_6, BW_MON_STATUS_RD_LGA + 0x18, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_wr_0, BW_MON_STATUS_WR_LGA + 0x0, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_wr_0, BW_MON_STATUS_WR_LGA + 0x0, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_wr_0, BW_MON_STATUS_WR_LGA + 0x0, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_wr_1, BW_MON_STATUS_WR_LGA + 0x4, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_wr_1, BW_MON_STATUS_WR_LGA + 0x4, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_wr_1, BW_MON_STATUS_WR_LGA + 0x4, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_wr_2, BW_MON_STATUS_WR_LGA + 0x8, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_wr_2, BW_MON_STATUS_WR_LGA + 0x8, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_wr_2, BW_MON_STATUS_WR_LGA + 0x8, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_wr_3, BW_MON_STATUS_WR_LGA + 0xc, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_wr_3, BW_MON_STATUS_WR_LGA + 0xc, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_wr_3, BW_MON_STATUS_WR_LGA + 0xc, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_wr_4, BW_MON_STATUS_WR_LGA + 0x10, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_wr_4, BW_MON_STATUS_WR_LGA + 0x10, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_wr_4, BW_MON_STATUS_WR_LGA + 0x10, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_wr_5, BW_MON_STATUS_WR_LGA + 0x14, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_wr_5, BW_MON_STATUS_WR_LGA + 0x14, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_wr_5, BW_MON_STATUS_WR_LGA + 0x14, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bw_mon_status_wr_6, BW_MON_STATUS_WR_LGA + 0x18, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_slot_wr_6, BW_MON_STATUS_WR_LGA + 0x18, 0x0000FFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(bytes_window_wr_6, BW_MON_STATUS_WR_LGA + 0x18, 0xFFFF0000, 16);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(lat_mon_status_rd_lga, LAT_MON_STATUS_RD_LGA, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(slot_latency_rd_lga, LAT_MON_STATUS_RD_LGA, 0x00FFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(max_outstanding_rd_lga, LAT_MON_STATUS_RD_LGA, 0xFF000000, 24);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(lat_mon_status_wr_lga, LAT_MON_STATUS_WR_LGA, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(slot_latency_wr_lga, LAT_MON_STATUS_WR_LGA, 0x00FFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(max_outstanding_wr_lga, LAT_MON_STATUS_WR_LGA, 0xFF000000, 24);

DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(qbox_arch_cg_dis_lga, QBOX_ARCH_CG_DIS_LGA, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_DEBUGFS_ATTRIBUTE_RO(qbox_qactive_lga, QBOX_QACTIVE_LGA, 0xFFFFFFFF, 0);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(qos_ovrd_cfg, QOS_OVRD_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(arqos_ovrd_en, QOS_OVRD_CFG, 0x00000001, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(arqos_ovrd_val, QOS_OVRD_CFG, 0x00000030, 4);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(awqos_ovrd_en, QOS_OVRD_CFG, 0x00000100, 8);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(awqos_ovrd_val, QOS_OVRD_CFG, 0x00003000, 12);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(qos_latmod_cfg, QOS_LATMOD_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(arqos_latmod_en, QOS_LATMOD_CFG, 0x00000001, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(arqos_lat_step_th, QOS_LATMOD_CFG, 0x0000FFF0, 4);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(awqos_latmod_en, QOS_LATMOD_CFG, 0x00010000, 16);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(awqos_latmod_step_th, QOS_LATMOD_CFG, 0xFFF00000, 20);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(qos_bwmod_cfg, QOS_BWMOD_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(arqos_bwmod_en, QOS_BWMOD_CFG, 0x00000001, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(arqos_bw_step_th, QOS_BWMOD_CFG, 0x0000FFF0, 4);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(awqos_bwmod_en, QOS_BWMOD_CFG, 0x00010000, 16);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(awqos_bw_step_th, QOS_BWMOD_CFG, 0xFFF00000, 20);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(qos_urgovrd_cfg, QOS_URGOVRD_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(arqos_urgovrd_en, QOS_URGOVRD_CFG, 0x00000001, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(awqos_urgovrd_en, QOS_URGOVRD_CFG, 0x00000010, 4);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(urg_ovrd_cfg, URG_OVRD_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rurglvl_ovrd_en, URG_OVRD_CFG, 0x00000001, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rurglvl_ovrd_val, URG_OVRD_CFG, 0x00000030, 4);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wurglvl_ovrd_en, URG_OVRD_CFG, 0x00000100, 8);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wurglvl_ovrd_val, URG_OVRD_CFG, 0x00003000, 12);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(urg_latmod_cfg, URG_LATMOD_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rurglvl_latmod_en, URG_LATMOD_CFG, 0x00000001, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rurglvl_lat_step_th, URG_LATMOD_CFG, 0x0000FFF0, 4);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wurglvl_latmod_en, URG_LATMOD_CFG, 0x00010000, 16);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wurglvl_lat_step_th, URG_LATMOD_CFG, 0xFFF00000, 20);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(urg_bwmod_cfg, URG_BWMOD_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rurglvl_bwmod_en, URG_BWMOD_CFG, 0x00000001, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rurglvl_step_th, URG_BWMOD_CFG, 0x0000FFF0, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wurglvl_bwmod_en, URG_BWMOD_CFG, 0x00010000, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wurglvl_step_th, URG_BWMOD_CFG, 0xFFF00000, 0);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(mo_limit_cfg, MO_LIMIT_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdmo_limit_en, MO_LIMIT_CFG, 0x00000001, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrmo_limit_en, MO_LIMIT_CFG, 0x00000002, 1);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdmo_limit_cfg, RDMO_LIMIT_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdmo_limit_trtl0, RDMO_LIMIT_CFG, 0x000000FF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdmo_limit_trtl1, RDMO_LIMIT_CFG, 0x0000FF00, 8);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdmo_limit_trtl2, RDMO_LIMIT_CFG, 0x00FF0000, 16);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdmo_limit_trtl3, RDMO_LIMIT_CFG, 0xFF000000, 24);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrmo_limit_cfg, WRMO_LIMIT_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrmo_limit_trtl0, WRMO_LIMIT_CFG, 0x000000FF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrmo_limit_trtl1, WRMO_LIMIT_CFG, 0x0000FF00, 8);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrmo_limit_trtl2, WRMO_LIMIT_CFG, 0x00FF0000, 16);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrmo_limit_trtl3, WRMO_LIMIT_CFG, 0xFF000000, 24);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(bw_limit_cfg, BW_LIMIT_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdbw_limit_en, BW_LIMIT_CFG, 0x00000001, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrbw_limit_en, BW_LIMIT_CFG, 0x00000010, 4);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdbw_limit_ctrl_0, RDBW_LIMIT_CTRL_0, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdbw_slot_limit_trtl_0,
					RDBW_LIMIT_CTRL_0, 0x0000FFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdbw_window_limit_trtl_0,
					RDBW_LIMIT_CTRL_0, 0xFFFF0000, 16);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdbw_limit_ctrl_1, RDBW_LIMIT_CTRL_1, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdbw_slot_limit_trtl_1,
					RDBW_LIMIT_CTRL_1, 0x0000FFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdbw_window_limit_trtl_1,
					RDBW_LIMIT_CTRL_1, 0xFFFF0000, 16);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdbw_limit_ctrl_2, RDBW_LIMIT_CTRL_2, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdbw_slot_limit_trtl_2,
					RDBW_LIMIT_CTRL_2, 0x0000FFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdbw_window_limit_trtl_2,
					RDBW_LIMIT_CTRL_2, 0xFFFF0000, 16);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdbw_limit_ctrl_3, RDBW_LIMIT_CTRL_3, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdbw_slot_limit_trtl_3,
					RDBW_LIMIT_CTRL_3, 0x0000FFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rdbw_window_limit_trtl_3,
					RDBW_LIMIT_CTRL_3, 0xFFFF0000, 16);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrbw_limit_ctrl_0, WRBW_LIMIT_CTRL_0, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrbw_slot_limit_trtl_0,
					WRBW_LIMIT_CTRL_0, 0x0000FFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrbw_window_limit_trtl_0,
					WRBW_LIMIT_CTRL_0, 0xFFFF0000, 16);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrbw_limit_ctrl_1, WRBW_LIMIT_CTRL_1, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrbw_slot_limit_trtl_1,
					WRBW_LIMIT_CTRL_1, 0x0000FFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrbw_window_limit_trtl_1,
					WRBW_LIMIT_CTRL_1, 0xFFFF0000, 16);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrbw_limit_ctrl_2, WRBW_LIMIT_CTRL_2, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrbw_slot_limit_trtl_2,
					WRBW_LIMIT_CTRL_2, 0x0000FFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrbw_window_limit_trtl_2,
					WRBW_LIMIT_CTRL_2, 0xFFFF0000, 16);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrbw_limit_ctrl_3, WRBW_LIMIT_CTRL_3, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrbw_slot_limit_trtl_3,
					WRBW_LIMIT_CTRL_3, 0x0000FFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(wrbw_window_limit_trtl_3,
					WRBW_LIMIT_CTRL_3, 0xFFFF0000, 16);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_rd_cfg, RGLTR_RD_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(arqos_rgltr_en, RGLTR_RD_CFG, 0x00000001, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(arqos_rgltr_val, RGLTR_RD_CFG, 0x00000030, 4);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_rdbw_gap_en, RGLTR_RD_CFG, 0x00000100, 8);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_rdreq_gap, RGLTR_RD_CFG, 0x000FF000, 12);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_wr_cfg, RGLTR_WR_CFG, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(awqos_rgltr_en, RGLTR_WR_CFG, 0x00000001, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(awqos_rgltr_val, RGLTR_WR_CFG, 0x00000030, 4);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_wrbw_gap_en, RGLTR_WR_CFG, 0x00000100, 8);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_wrreq_gap, RGLTR_WR_CFG, 0x000FF000, 12);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_bw_ctrl_0, RGLTR_BW_CTRL_0, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_rdbw_th_trtl_0, RGLTR_BW_CTRL_0, 0x0000FFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_wrbw_th_trtl_0, RGLTR_BW_CTRL_0, 0xFFFF0000, 16);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_bw_ctrl_1, RGLTR_BW_CTRL_1, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_rdbw_th_trtl_1, RGLTR_BW_CTRL_1, 0x0000FFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_wrbw_th_trtl_1, RGLTR_BW_CTRL_1, 0xFFFF0000, 16);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_bw_ctrl_2, RGLTR_BW_CTRL_2, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_rdbw_th_trtl_2, RGLTR_BW_CTRL_2, 0x0000FFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_wrbw_th_trtl_2, RGLTR_BW_CTRL_2, 0xFFFF0000, 16);

DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_bw_ctrl_3, RGLTR_BW_CTRL_3, 0xFFFFFFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_rdbw_th_trtl_3, RGLTR_BW_CTRL_3, 0x0000FFFF, 0);
DEFINE_QOS_BOX_POLICY_DEBUGFS_ATTRIBUTE(rgltr_wrbw_th_trtl_3, RGLTR_BW_CTRL_3, 0xFFFF0000, 16);

static int qos_box_init_debugfs_policy(struct qos_box_dev *qos_box_dev,
				       u32 idx, struct dentry *base_dir)
{
	struct qos_box_dbg *dbg;
	struct qos_box_dbg_policy *dbg_policy;
	struct dentry *dir;
	char dir_name[10];

	if (idx >= NUM_QOS_POLICY)
		return 0;

	dbg = qos_box_dev->dbg;
	dbg_policy = &dbg->dbg_policy[idx];

	snprintf(dir_name, strlen("policy") + 2, "policy%u", idx);
	dir = debugfs_create_dir(dir_name, base_dir);

	dbg_policy->dir = dir;
	dbg_policy->base = QOS_POLICY_BLOCK(idx);
	dbg_policy->instance = dbg;

	debugfs_create_file("qos_ovrd_cfg", 0600, dir, dbg_policy, &qos_ovrd_cfg);
	debugfs_create_file("arqos_ovrd_en", 0600, dir, dbg_policy, &arqos_ovrd_en);
	debugfs_create_file("arqos_ovrd_val", 0600, dir, dbg_policy, &arqos_ovrd_val);
	debugfs_create_file("awqos_ovrd_en", 0600, dir, dbg_policy, &awqos_ovrd_en);
	debugfs_create_file("awqos_ovrd_val", 0600, dir, dbg_policy, &awqos_ovrd_val);

	debugfs_create_file("qos_latmod_cfg", 0600, dir, dbg_policy, &qos_latmod_cfg);
	debugfs_create_file("arqos_latmod_en", 0600, dir, dbg_policy, &arqos_latmod_en);
	debugfs_create_file("arqos_lat_step_th", 0600, dir, dbg_policy, &arqos_lat_step_th);
	debugfs_create_file("awqos_latmod_en", 0600, dir, dbg_policy, &awqos_latmod_en);
	debugfs_create_file("awqos_latmod_step_th", 0600, dir, dbg_policy, &awqos_latmod_step_th);

	debugfs_create_file("qos_bwmod_cfg", 0600, dir, dbg_policy, &qos_bwmod_cfg);
	debugfs_create_file("arqos_bwmod_en", 0600, dir, dbg_policy, &arqos_bwmod_en);
	debugfs_create_file("arqos_bw_step_th", 0600, dir, dbg_policy, &arqos_bw_step_th);
	debugfs_create_file("awqos_bwmod_en", 0600, dir, dbg_policy, &awqos_bwmod_en);
	debugfs_create_file("awqos_bw_step_th", 0600, dir, dbg_policy, &awqos_bw_step_th);

	debugfs_create_file("qos_urgovrd_cfg", 0600, dir, dbg_policy, &qos_urgovrd_cfg);
	debugfs_create_file("arqos_urgovrd_en", 0600, dir, dbg_policy, &arqos_urgovrd_en);
	debugfs_create_file("awqos_urgovrd_en", 0600, dir, dbg_policy, &awqos_urgovrd_en);

	debugfs_create_file("urg_ovrd_cfg", 0600, dir, dbg_policy, &urg_ovrd_cfg);
	debugfs_create_file("rurglvl_ovrd_en", 0600, dir, dbg_policy, &rurglvl_ovrd_en);
	debugfs_create_file("rurglvl_ovrd_val", 0600, dir, dbg_policy, &rurglvl_ovrd_val);
	debugfs_create_file("wurglvl_ovrd_en", 0600, dir, dbg_policy, &wurglvl_ovrd_en);
	debugfs_create_file("wurglvl_ovrd_val", 0600, dir, dbg_policy, &wurglvl_ovrd_val);

	debugfs_create_file("urg_latmod_cfg", 0600, dir, dbg_policy, &urg_latmod_cfg);
	debugfs_create_file("rurglvl_latmod_en", 0600, dir, dbg_policy, &rurglvl_latmod_en);
	debugfs_create_file("rurglvl_lat_step_th", 0600, dir, dbg_policy, &rurglvl_lat_step_th);
	debugfs_create_file("wurglvl_latmod_en", 0600, dir, dbg_policy, &wurglvl_latmod_en);
	debugfs_create_file("wurglvl_lat_step_th", 0600, dir, dbg_policy, &wurglvl_lat_step_th);

	debugfs_create_file("urg_bwmod_cfg", 0600, dir, dbg_policy, &urg_bwmod_cfg);
	debugfs_create_file("rurglvl_bwmod_en", 0600, dir, dbg_policy, &rurglvl_bwmod_en);
	debugfs_create_file("rurglvl_step_th", 0600, dir, dbg_policy, &rurglvl_step_th);
	debugfs_create_file("wurglvl_bwmod_en", 0600, dir, dbg_policy, &wurglvl_bwmod_en);
	debugfs_create_file("wurglvl_step_th", 0600, dir, dbg_policy, &wurglvl_step_th);

	debugfs_create_file("mo_limit_cfg", 0600, dir, dbg_policy, &mo_limit_cfg);
	debugfs_create_file("rdmo_limit_en", 0600, dir, dbg_policy, &rdmo_limit_en);
	debugfs_create_file("wrmo_limit_en", 0600, dir, dbg_policy, &wrmo_limit_en);

	debugfs_create_file("rdmo_limit_cfg", 0600, dir, dbg_policy, &rdmo_limit_cfg);
	debugfs_create_file("rdmo_limit_trtl0", 0600, dir, dbg_policy, &rdmo_limit_trtl0);
	debugfs_create_file("rdmo_limit_trtl1", 0600, dir, dbg_policy, &rdmo_limit_trtl1);
	debugfs_create_file("rdmo_limit_trtl2", 0600, dir, dbg_policy, &rdmo_limit_trtl2);
	debugfs_create_file("rdmo_limit_trtl3", 0600, dir, dbg_policy, &rdmo_limit_trtl3);

	debugfs_create_file("wrmo_limit_cfg", 0600, dir, dbg_policy, &wrmo_limit_cfg);
	debugfs_create_file("wrmo_limit_trtl0", 0600, dir, dbg_policy, &wrmo_limit_trtl0);
	debugfs_create_file("wrmo_limit_trtl1", 0600, dir, dbg_policy, &wrmo_limit_trtl1);
	debugfs_create_file("wrmo_limit_trtl2", 0600, dir, dbg_policy, &wrmo_limit_trtl2);
	debugfs_create_file("wrmo_limit_trtl3", 0600, dir, dbg_policy, &wrmo_limit_trtl3);

	debugfs_create_file("bw_limit_cfg", 0600, dir, dbg_policy, &bw_limit_cfg);
	debugfs_create_file("rdbw_limit_en", 0600, dir, dbg_policy, &rdbw_limit_en);
	debugfs_create_file("wrbw_limit_en", 0600, dir, dbg_policy, &wrbw_limit_en);

	debugfs_create_file("rdbw_limit_ctrl_0", 0600, dir, dbg_policy, &rdbw_limit_ctrl_0);
	debugfs_create_file("rdbw_slot_limit_trtl_0", 0600,
			    dir, dbg_policy, &rdbw_slot_limit_trtl_0);
	debugfs_create_file("rdbw_window_limit_trtl_0", 0600,
			    dir, dbg_policy, &rdbw_window_limit_trtl_0);

	debugfs_create_file("rdbw_limit_ctrl_1", 0600, dir, dbg_policy, &rdbw_limit_ctrl_1);
	debugfs_create_file("rdbw_slot_limit_trtl_1", 0600,
			    dir, dbg_policy, &rdbw_slot_limit_trtl_1);
	debugfs_create_file("rdbw_window_limit_trtl_1", 0600,
			    dir, dbg_policy, &rdbw_window_limit_trtl_1);

	debugfs_create_file("rdbw_limit_ctrl_2", 0600, dir, dbg_policy, &rdbw_limit_ctrl_2);
	debugfs_create_file("rdbw_slot_limit_trtl_2", 0600,
			    dir, dbg_policy, &rdbw_slot_limit_trtl_2);
	debugfs_create_file("rdbw_window_limit_trtl_2", 0600,
			    dir, dbg_policy, &rdbw_window_limit_trtl_2);

	debugfs_create_file("rdbw_limit_ctrl_3", 0600, dir, dbg_policy, &rdbw_limit_ctrl_3);
	debugfs_create_file("rdbw_slot_limit_trtl_3", 0600,
			    dir, dbg_policy, &rdbw_slot_limit_trtl_3);
	debugfs_create_file("rdbw_window_limit_trtl_3", 0600,
			    dir, dbg_policy, &rdbw_window_limit_trtl_3);

	debugfs_create_file("wrbw_limit_ctrl_0", 0600, dir, dbg_policy, &wrbw_limit_ctrl_0);
	debugfs_create_file("wrbw_slot_limit_trtl_0", 0600,
			    dir, dbg_policy, &wrbw_slot_limit_trtl_0);
	debugfs_create_file("wrbw_window_limit_trtl_0", 0600,
			    dir, dbg_policy, &wrbw_window_limit_trtl_0);

	debugfs_create_file("wrbw_limit_ctrl_1", 0600, dir, dbg_policy, &wrbw_limit_ctrl_1);
	debugfs_create_file("wrbw_slot_limit_trtl_1", 0600,
			    dir, dbg_policy, &wrbw_slot_limit_trtl_1);
	debugfs_create_file("wrbw_window_limit_trtl_1", 0600,
			    dir, dbg_policy, &wrbw_window_limit_trtl_1);

	debugfs_create_file("wrbw_limit_ctrl_2", 0600, dir, dbg_policy, &wrbw_limit_ctrl_2);
	debugfs_create_file("wrbw_slot_limit_trtl_2", 0600,
			    dir, dbg_policy, &wrbw_slot_limit_trtl_2);
	debugfs_create_file("wrbw_window_limit_trtl_2", 0600,
			    dir, dbg_policy, &wrbw_window_limit_trtl_2);

	debugfs_create_file("wrbw_limit_ctrl_3", 0600, dir, dbg_policy, &wrbw_limit_ctrl_3);
	debugfs_create_file("wrbw_slot_limit_trtl_3", 0600,
			    dir, dbg_policy, &wrbw_slot_limit_trtl_3);
	debugfs_create_file("wrbw_window_limit_trtl_3", 0600,
			    dir, dbg_policy, &wrbw_window_limit_trtl_3);

	debugfs_create_file("rgltr_rd_cfg", 0600, dir, dbg_policy, &rgltr_rd_cfg);
	debugfs_create_file("arqos_rgltr_en", 0600, dir, dbg_policy, &arqos_rgltr_en);
	debugfs_create_file("arqos_rgltr_val", 0600, dir, dbg_policy, &arqos_rgltr_val);
	debugfs_create_file("rgltr_rdbw_gap_en", 0600, dir, dbg_policy, &rgltr_rdbw_gap_en);
	debugfs_create_file("rgltr_rdreq_gap", 0600, dir, dbg_policy, &rgltr_rdreq_gap);

	debugfs_create_file("rgltr_wr_cfg", 0600, dir, dbg_policy, &rgltr_wr_cfg);
	debugfs_create_file("awqos_rgltr_en", 0600, dir, dbg_policy, &awqos_rgltr_en);
	debugfs_create_file("awqos_rgltr_val", 0600, dir, dbg_policy, &awqos_rgltr_val);
	debugfs_create_file("rgltr_wrbw_gap_en", 0600, dir, dbg_policy, &rgltr_wrbw_gap_en);
	debugfs_create_file("rgltr_wrreq_gap", 0600, dir, dbg_policy, &rgltr_wrreq_gap);

	debugfs_create_file("rgltr_bw_ctrl_0", 0600, dir, dbg_policy, &rgltr_bw_ctrl_0);
	debugfs_create_file("rgltr_rdbw_th_trtl_0", 0600, dir, dbg_policy, &rgltr_rdbw_th_trtl_0);
	debugfs_create_file("rgltr_wrbw_th_trtl_0", 0600, dir, dbg_policy, &rgltr_wrbw_th_trtl_0);

	debugfs_create_file("rgltr_bw_ctrl_1", 0600, dir, dbg_policy, &rgltr_bw_ctrl_1);
	debugfs_create_file("rgltr_rdbw_th_trtl_1", 0600, dir, dbg_policy, &rgltr_rdbw_th_trtl_1);
	debugfs_create_file("rgltr_wrbw_th_trtl_1", 0600, dir, dbg_policy, &rgltr_wrbw_th_trtl_1);

	debugfs_create_file("rgltr_bw_ctrl_2", 0600, dir, dbg_policy, &rgltr_bw_ctrl_2);
	debugfs_create_file("rgltr_rdbw_th_trtl_2", 0600, dir, dbg_policy, &rgltr_rdbw_th_trtl_2);
	debugfs_create_file("rgltr_wrbw_th_trtl_2", 0600, dir, dbg_policy, &rgltr_wrbw_th_trtl_2);

	debugfs_create_file("rgltr_bw_ctrl_3", 0600, dir, dbg_policy, &rgltr_bw_ctrl_3);
	debugfs_create_file("rgltr_rdbw_th_trtl_3", 0600, dir, dbg_policy, &rgltr_rdbw_th_trtl_3);
	debugfs_create_file("rgltr_wrbw_th_trtl_3", 0600, dir, dbg_policy, &rgltr_wrbw_th_trtl_3);

	return 0;
}

int qos_box_init_debugfs(struct qos_box_dev *qos_box_dev, struct dentry *root_dir)
{
	struct qos_box_dbg *dbg;
	struct dentry *dir;
	u32 idx;

	if (!qos_box_dev || !qos_box_dev->dbg || !root_dir)
		return 0;

	dir = debugfs_create_dir(qos_box_dev->name, root_dir);

	dbg = qos_box_dev->dbg;
	dbg->dir = dir;
	dbg->root_dir = root_dir;
	dbg->qos_box_dev = qos_box_dev;
	dbg->restore_when_release_ctl = 1;

	mutex_init(&dbg->mutex);

	debugfs_create_file("control", 0600, dir, dbg, &control);
	debugfs_create_file("power_vote", 0600, dir, dbg, &power_vote);
	debugfs_create_file("restore_release_control", 0600, dir, dbg, &restore_release_control);

	debugfs_create_file("vc_map_cfg", 0600, dir, dbg, &vc_map_cfg);
	debugfs_create_file("vc0_map", 0600, dir, dbg, &vc0_map);
	debugfs_create_file("vc1_map", 0600, dir, dbg, &vc1_map);
	debugfs_create_file("vc2_map", 0600, dir, dbg, &vc2_map);
	debugfs_create_file("vc3_map", 0600, dir, dbg, &vc3_map);
	debugfs_create_file("vc4_map", 0600, dir, dbg, &vc4_map);
	if (qos_box_dev->desc.version == QOS_BOX_VERSION_LGA) {
		debugfs_create_file("vc5_map", 0600, dir, dbg, &vc5_map);
		debugfs_create_file("vc6_map", 0600, dir, dbg, &vc6_map);
	}

	debugfs_create_file("qos_policy_cfg", 0600, dir, dbg, &qos_policy_cfg);
	debugfs_create_file("load_en", 0600, dir, dbg, &load_en);
	debugfs_create_file("index_sel", 0600, dir, dbg, &index_sel);

	debugfs_create_file("bw_mon_cfg", 0600, dir, dbg, &bw_mon_cfg);
	debugfs_create_file("cycles_per_slot", 0600, dir, dbg, &cycles_per_slot);
	debugfs_create_file("slots_per_window", 0600, dir, dbg, &slots_per_window);
	debugfs_create_file("slots_per_urgmod", 0600, dir, dbg, &slots_per_urgmod);

	debugfs_create_file("vc_filter_cfg", 0600, dir, dbg, &vc_filter_cfg);

	if (qos_box_dev->desc.version == QOS_BOX_VERSION_RDO) {
		debugfs_create_file("rd_mon_vc_fltr", 0600, dir, dbg, &rd_mon_vc_fltr);
		debugfs_create_file("wr_mon_vc_fltr", 0600, dir, dbg, &wr_mon_vc_fltr);
	} else if (qos_box_dev->desc.version == QOS_BOX_VERSION_LGA) {
		debugfs_create_file("rd_mon_vc_fltr", 0600, dir, dbg, &rd_mon_vc_fltr_lga);
		debugfs_create_file("wr_mon_vc_fltr", 0600, dir, dbg, &wr_mon_vc_fltr_lga);
	}

	for (idx = 0; idx < NUM_QOS_POLICY; idx++)
		qos_box_init_debugfs_policy(qos_box_dev, idx, dir);

	debugfs_create_file("qos_box_status", 0400, dir, dbg, &qos_box_status);
	debugfs_create_file("policy_index", 0400, dir, dbg, &policy_index);
	debugfs_create_file("rd_vc_active", 0400, dir, dbg, &rd_vc_active);
	debugfs_create_file("wr_vc_active", 0400, dir, dbg, &wr_vc_active);
	debugfs_create_file("rd_urglvl_ingress", 0400, dir, dbg, &rd_urglvl_ingress);
	debugfs_create_file("wr_urglvl_ingress", 0400, dir, dbg, &wr_urglvl_ingress);
	debugfs_create_file("rd_trtlvl", 0400, dir, dbg, &rd_trtlvl);
	debugfs_create_file("wr_trtlvl", 0400, dir, dbg, &wr_trtlvl);

	debugfs_create_file("qos_box_urg_status", 0400, dir, dbg, &qos_box_urg_status);
	debugfs_create_file("rd_urglvl_egress", 0400, dir, dbg, &rd_urglvl_egress);
	debugfs_create_file("wr_urglvl_egress", 0400, dir, dbg, &wr_urglvl_egress);

	if (qos_box_dev->desc.version == QOS_BOX_VERSION_RDO) {
		debugfs_create_file("bw_mon_status_rd", 0400, dir, dbg, &bw_mon_status_rd);
		debugfs_create_file("bytes_slot_rd", 0400, dir, dbg, &bytes_slot_rd);
		debugfs_create_file("bytes_window_rd", 0400, dir, dbg, &bytes_window_rd);

		debugfs_create_file("bw_mon_status_wr", 0400, dir, dbg, &bw_mon_status_wr);
		debugfs_create_file("bytes_slot_wr", 0400, dir, dbg, &bytes_slot_wr);
		debugfs_create_file("bytes_window_wr", 0400, dir, dbg, &bytes_window_wr);

		debugfs_create_file("lat_mon_status_rd", 0400, dir, dbg, &lat_mon_status_rd);
		debugfs_create_file("slot_latency_rd", 0400, dir, dbg, &slot_latency_rd);
		debugfs_create_file("max_outstanding_rd", 0400, dir, dbg, &max_outstanding_rd);

		debugfs_create_file("lat_mon_status_wr", 0400, dir, dbg, &lat_mon_status_wr);
		debugfs_create_file("slot_latency_wr", 0400, dir, dbg, &slot_latency_wr);
		debugfs_create_file("max_outstanding_wr", 0400, dir, dbg, &max_outstanding_wr);
	} else if (qos_box_dev->desc.version == QOS_BOX_VERSION_LGA) {
		debugfs_create_file("bw_mon_status_rd_0", 0400, dir, dbg, &bw_mon_status_rd_0);
		debugfs_create_file("bytes_slot_rd_0", 0400, dir, dbg, &bytes_slot_rd_0);
		debugfs_create_file("bytes_window_rd_0", 0400, dir, dbg, &bytes_window_rd_0);

		debugfs_create_file("bw_mon_status_rd_1", 0400, dir, dbg, &bw_mon_status_rd_1);
		debugfs_create_file("bytes_slot_rd_1", 0400, dir, dbg, &bytes_slot_rd_1);
		debugfs_create_file("bytes_window_rd_1", 0400, dir, dbg, &bytes_window_rd_1);

		debugfs_create_file("bw_mon_status_rd_2", 0400, dir, dbg, &bw_mon_status_rd_2);
		debugfs_create_file("bytes_slot_rd_2", 0400, dir, dbg, &bytes_slot_rd_2);
		debugfs_create_file("bytes_window_rd_2", 0400, dir, dbg, &bytes_window_rd_2);

		debugfs_create_file("bw_mon_status_rd_3", 0400, dir, dbg, &bw_mon_status_rd_3);
		debugfs_create_file("bytes_slot_rd_3", 0400, dir, dbg, &bytes_slot_rd_3);
		debugfs_create_file("bytes_window_rd_3", 0400, dir, dbg, &bytes_window_rd_3);

		debugfs_create_file("bw_mon_status_rd_4", 0400, dir, dbg, &bw_mon_status_rd_4);
		debugfs_create_file("bytes_slot_rd_4", 0400, dir, dbg, &bytes_slot_rd_4);
		debugfs_create_file("bytes_window_rd_4", 0400, dir, dbg, &bytes_window_rd_4);

		debugfs_create_file("bw_mon_status_rd_5", 0400, dir, dbg, &bw_mon_status_rd_5);
		debugfs_create_file("bytes_slot_rd_5", 0400, dir, dbg, &bytes_slot_rd_5);
		debugfs_create_file("bytes_window_rd_5", 0400, dir, dbg, &bytes_window_rd_5);

		debugfs_create_file("bw_mon_status_rd_6", 0400, dir, dbg, &bw_mon_status_rd_6);
		debugfs_create_file("bytes_slot_rd_6", 0400, dir, dbg, &bytes_slot_rd_6);
		debugfs_create_file("bytes_window_rd_6", 0400, dir, dbg, &bytes_window_rd_6);

		debugfs_create_file("bw_mon_status_wr_0", 0400, dir, dbg, &bw_mon_status_wr_0);
		debugfs_create_file("bytes_slot_wr_0", 0400, dir, dbg, &bytes_slot_wr_0);
		debugfs_create_file("bytes_window_wr_0", 0400, dir, dbg, &bytes_window_wr_0);

		debugfs_create_file("bw_mon_status_wr_1", 0400, dir, dbg, &bw_mon_status_wr_1);
		debugfs_create_file("bytes_slot_wr_1", 0400, dir, dbg, &bytes_slot_wr_1);
		debugfs_create_file("bytes_window_wr_1", 0400, dir, dbg, &bytes_window_wr_1);

		debugfs_create_file("bw_mon_status_wr_2", 0400, dir, dbg, &bw_mon_status_wr_2);
		debugfs_create_file("bytes_slot_wr_2", 0400, dir, dbg, &bytes_slot_wr_2);
		debugfs_create_file("bytes_window_wr_2", 0400, dir, dbg, &bytes_window_wr_2);

		debugfs_create_file("bw_mon_status_wr_3", 0400, dir, dbg, &bw_mon_status_wr_3);
		debugfs_create_file("bytes_slot_wr_3", 0400, dir, dbg, &bytes_slot_wr_3);
		debugfs_create_file("bytes_window_wr_3", 0400, dir, dbg, &bytes_window_wr_3);

		debugfs_create_file("bw_mon_status_wr_4", 0400, dir, dbg, &bw_mon_status_wr_4);
		debugfs_create_file("bytes_slot_wr_4", 0400, dir, dbg, &bytes_slot_wr_4);
		debugfs_create_file("bytes_window_wr_4", 0400, dir, dbg, &bytes_window_wr_4);

		debugfs_create_file("bw_mon_status_wr_5", 0400, dir, dbg, &bw_mon_status_wr_5);
		debugfs_create_file("bytes_slot_wr_5", 0400, dir, dbg, &bytes_slot_wr_5);
		debugfs_create_file("bytes_window_wr_5", 0400, dir, dbg, &bytes_window_wr_5);

		debugfs_create_file("bw_mon_status_wr_6", 0400, dir, dbg, &bw_mon_status_wr_6);
		debugfs_create_file("bytes_slot_wr_6", 0400, dir, dbg, &bytes_slot_wr_6);
		debugfs_create_file("bytes_window_wr_6", 0400, dir, dbg, &bytes_window_wr_6);

		debugfs_create_file("lat_mon_status_rd", 0400, dir, dbg, &lat_mon_status_rd_lga);
		debugfs_create_file("slot_latency_rd", 0400, dir, dbg, &slot_latency_rd_lga);
		debugfs_create_file("max_outstanding_rd", 0400, dir, dbg, &max_outstanding_rd_lga);

		debugfs_create_file("lat_mon_status_wr", 0400, dir, dbg, &lat_mon_status_wr_lga);
		debugfs_create_file("slot_latency_wr", 0400, dir, dbg, &slot_latency_wr_lga);
		debugfs_create_file("max_outstanding_wr", 0400, dir, dbg, &max_outstanding_wr_lga);
	}

	if (qos_box_dev->desc.version == QOS_BOX_VERSION_LGA) {
		debugfs_create_file("qbox_arch_cg_dis", 0400, dir, dbg, &qbox_arch_cg_dis_lga);
		debugfs_create_file("qbox_qactive", 0400, dir, dbg, &qbox_qactive_lga);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qos_box_init_debugfs);

void qos_box_remove_debugfs(struct qos_box_dev *qos_box_dev)
{
	if (!qos_box_dev || !qos_box_dev->dbg)
		return;

	debugfs_remove_recursive(qos_box_dev->dbg->dir);
}
