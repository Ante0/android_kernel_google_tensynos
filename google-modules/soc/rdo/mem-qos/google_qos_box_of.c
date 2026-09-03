// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/minmax.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/types.h>

#include "google_qos_box_of.h"

struct qos_box_dt_attr {
	const char *propname;
	u32 offset;
	u32 bitmask;
	u32 bitshift;
};

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

static int of_qos_box_read_u32(struct qos_box_dev *qos_box_dev,
			struct device_node *np, const char *propname, u32 *data)
{
	struct device *dev = qos_box_dev->dev;
	int ret;

	if (!of_property_present(np, propname)) {
		dev_err(dev, "%s property not existed\n", propname);
		return -EINVAL;
	}

	ret = of_property_read_u32(np, propname, data);
	if (ret) {
		dev_err(dev, "Read %s property failed, ret = %d\n", propname, ret);
		return -EINVAL;
	}

	return 0;
}

static int of_qos_box_read_vc_map_cfg(struct qos_box_dev *qos_box_dev, struct device_node *np)
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

static int of_qos_box_read_cycles_per_slot(struct qos_box_dev *qos_box_dev, struct device_node *np)
{
	const char *propname = "google,cycles_per_slot";
	struct qcfg *config = &qos_box_dev->config;
	u32 val;

	if (of_qos_box_read_u32(qos_box_dev, np, propname, &val))
		return -EINVAL;

	config->bw_mon_cfg.cycles_per_slot = val;

	return 0;
}

static int of_qos_box_read_slots_per_window(struct qos_box_dev *qos_box_dev, struct device_node *np)
{
	const char *propname = "google,slots_per_window";
	struct qcfg *config = &qos_box_dev->config;
	u32 val;

	if (of_qos_box_read_u32(qos_box_dev, np, propname, &val))
		return -EINVAL;

	config->bw_mon_cfg.slots_per_window = val;

	return 0;
}

static int of_qos_box_read_slots_per_urgmod(struct qos_box_dev *qos_box_dev, struct device_node *np)
{
	const char *propname = "google,slots_per_urgmod";
	struct qcfg *config = &qos_box_dev->config;
	u32 val;

	if (of_qos_box_read_u32(qos_box_dev, np, propname, &val))
		return -EINVAL;

	config->bw_mon_cfg.slots_per_urgmod = val;

	return 0;
}

static int of_qos_box_read_rd_mon_vc_filter(struct qos_box_dev *qos_box_dev, struct device_node *np)
{
	const char *propname = "google,rd_mon_vc_filter";
	struct qcfg *config = &qos_box_dev->config;
	struct device *dev = qos_box_dev->dev;
	u32 val;
	u32 mask = 0;

	if (of_qos_box_read_u32(qos_box_dev, np, propname, &val))
		return -EINVAL;

	mask = GENMASK(qos_box_dev->desc.num_hw_vc - 1, 0);
	if (val > mask) {
		dev_err(dev, "%s: val (%x) > max possible val (%x)\n", propname, val, mask);
		return -EINVAL;
	}

	config->vc_filter_cfg.rd_mon_vc_filter = val;

	return 0;
}

static int of_qos_box_read_wr_mon_vc_filter(struct qos_box_dev *qos_box_dev, struct device_node *np)
{
	const char *propname = "google,wr_mon_vc_filter";
	struct qcfg *config = &qos_box_dev->config;
	struct device *dev = qos_box_dev->dev;
	u32 val;
	u32 mask = 0;

	if (of_qos_box_read_u32(qos_box_dev, np, propname, &val))
		return -EINVAL;

	mask = GENMASK(qos_box_dev->desc.num_hw_vc - 1, 0);
	if (val > mask) {
		dev_err(dev, "%s: val (%x) > max possible val (%x)\n", propname, val, mask);
		return -EINVAL;
	}

	config->vc_filter_cfg.wr_mon_vc_filter = val;

	return 0;
}

static int of_qos_box_read_policy_single(struct qos_box_dev *qos_box_dev,
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

static int of_qos_box_read_scenario_single(struct qos_box_dev *qos_box_dev,
					   struct qos_box_policy *policy, u32 *scenario_idx,
					   struct device_node *np)
{
	const char *propname = "google,scenario-idx";
	struct device *dev = qos_box_dev->dev;
	int ret = 0;

	ret = of_property_read_u32(np, propname, scenario_idx);
	if (ret) {
		dev_err(dev, "Read %s property failed, ret = %d\n", propname, ret);
		return -EINVAL;
	}

	if (*scenario_idx >= NUM_MEM_QOS_SCENARIO) {
		dev_err(dev, "Invalid %s property value: %u\n", propname, *scenario_idx);
		return -EINVAL;
	}

	if (qos_box_dev->scenario_arr[*scenario_idx]) {
		dev_err(dev, "Redefined %s property value: %u\n", propname, *scenario_idx);
		return -EINVAL;
	}

	return of_qos_box_read_policy_single(qos_box_dev, policy, np);
}

static int of_qos_box_read_scenarios(struct qos_box_dev *qos_box_dev, struct device_node *np)
{
	struct device *dev = qos_box_dev->dev;
	struct device_node *child_np;
	struct qos_box_policy *policy;
	u32 scenario_idx;
	int ret;

	for_each_child_of_node(np, child_np) {
		policy = devm_kzalloc(dev, sizeof(*policy), GFP_KERNEL);

		scenario_idx = 0;
		ret = of_qos_box_read_scenario_single(qos_box_dev, policy, &scenario_idx, child_np);
		if (ret)
			return ret;

		qos_box_dev->scenario_arr[scenario_idx] = policy;
	}

	if (!qos_box_dev->scenario_arr[MEM_QOS_SCENARIO_DEFAULT]) {
		dev_err(dev, "Default scenario is not defined in DT\n");
		return -EINVAL;
	}

	return 0;
}

static int of_qos_box_read_null_policy(struct qos_box_dev *qos_box_dev, struct device_node *np)
{
	struct qos_box_policy *policy = &qos_box_dev->null_policy.policy;

	return of_qos_box_read_policy_single(qos_box_dev, policy, np);
}

int of_qos_box_read_qcfg(struct qos_box_dev *qos_box_dev, struct device_node *np)
{
	if (!qos_box_dev || !np)
		return -EINVAL;

	if (of_qos_box_read_vc_map_cfg(qos_box_dev, np))
		return -EINVAL;

	if (of_qos_box_read_cycles_per_slot(qos_box_dev, np))
		return -EINVAL;

	if (of_qos_box_read_slots_per_window(qos_box_dev, np))
		return -EINVAL;

	if (of_qos_box_read_slots_per_urgmod(qos_box_dev, np))
		return -EINVAL;

	if (of_qos_box_read_rd_mon_vc_filter(qos_box_dev, np))
		return -EINVAL;

	if (of_qos_box_read_wr_mon_vc_filter(qos_box_dev, np))
		return -EINVAL;

	if (of_qos_box_read_scenarios(qos_box_dev, np))
		return -EINVAL;

	/*
	 * TODO(b/247494035): sanity check for qos_box settings
	 *    - Either latency modulation or BW modulation can be enabled but not both
	 *    - Latency/BW modulation should not be used along with AxQoS regulator override
	 *    - One of the limiting mechanisms to be used among MO limiting, BW limiting
	 *      and Regulator
	 */

	return 0;
}

int of_qos_box_read_common_property(struct qos_box_dev *qos_box_dev, struct device_node *np)
{
	struct device_node *np_common_prop, *np_null_policy, *np_por_policy;
	const char *propname_load_en_delay_ns = "load_en_delay_ns";
	const char *propname_null_policy = "null_policy";
	const char *propname_por_policy = "por_policy";
	struct device *dev;
	struct qos_box_config_delay *load_en_delay;
	u32 val;
	int ret = 0;

	if (!qos_box_dev || !np)
		return -EINVAL;

	dev = qos_box_dev->dev;

	np_common_prop = of_parse_phandle(np, "google,qos-box-commom-prop", 0);
	if (!np_common_prop) {
		dev_err(dev, "Read google,qos-box-commom-prop failed.\n");
		return -EINVAL;
	}

	/* read load_en_delay_ns */
	load_en_delay = &qos_box_dev->load_en_delay;
	load_en_delay->enable = of_property_present(np_common_prop, propname_load_en_delay_ns);

	if (load_en_delay->enable) {
		if (of_property_read_u32(np_common_prop,
					 propname_load_en_delay_ns,
					 &val) < 0) {
			dev_err(dev, "Read `%s` failed.\n", propname_load_en_delay_ns);
			return -EINVAL;
		}

		load_en_delay->delay_ns = (u64)val;
		load_en_delay->last_ts = 0;
	}

	/* read null_policy */
	np_null_policy = of_parse_phandle(np_common_prop, propname_null_policy, 0);
	if (!np_null_policy) {
		qos_box_dev->null_policy.enable = false;
		ret = 0;
	} else {
		qos_box_dev->null_policy.enable = true;
		qos_box_dev->null_policy.idx = NUM_QOS_POLICY - 1;

		ret = of_qos_box_read_null_policy(qos_box_dev, np_null_policy);
	}

	/* read por_policy */
	np_por_policy = of_parse_phandle(np_common_prop, propname_por_policy, 0);
	if (!np_por_policy) {
		dev_err(dev, "Read `%s` failed.\n", propname_por_policy);
		return -EINVAL;
	}
	ret = of_qos_box_read_policy_single(qos_box_dev, &qos_box_dev->por_policy, np_por_policy);

	dev_dbg(dev, "read_common_property:\n");
	dev_dbg(dev, "  null_policy:\n");
	dev_dbg(dev, "    enable = %u\n", qos_box_dev->null_policy.enable);
	dev_dbg(dev, "    idx = %u\n", qos_box_dev->null_policy.idx);

	return ret;
}
