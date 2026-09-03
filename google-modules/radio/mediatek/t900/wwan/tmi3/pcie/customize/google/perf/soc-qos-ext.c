// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/of.h>
#include "radio-google.h"
#include "soc-qos-ext.h"
#include "radio-utils.h"
#include "interconnect/google_icc_helper.h"
#include "feature-control.h"

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_SOC_BW_QOS)
/*
 * SoC QoS / DDR Bandwidth Voting Algorithm
 *
 * Algorithm Overview:
 * 1. Path-level Decision: RX and TX paths independently update their
 *    target bandwidth based on local thresholds and state inheritance.
 * 2. Global Aggregation: The combined target is max(RX_target, TX_target).
 * 3. Trend-based Execution:
 *    - Scale Up: Immediate update if combined target > current vote.
 *    - Scale Down: Delayed update if combined target < current vote.
 *    - Hold: Reset countdown if combined target == current vote.
 *
 * Formula:
 * - Mbps to Bytes/ms: (Mbps * 10^6) / 8 / 10^3 = Mbps * 125
 * - Current Bytes/ms: (TPUT_count * BUFFER_SIZE)
 */
#define SKB_BAT_SIZE (1536)
#define FRAG_BAT_SIZE (1920)
#define MBPS_TO_BYTES_PER_MS (125)

struct soc_qos_ext {
	struct radio_google *goog;
	struct google_icc_path *icc_path;

	/* State Machine Variables */
	u32 curr_vote_bw;
	u32 curr_rx_vote_bw;
	u32 curr_tx_vote_bw;
	int low_vote_countdown;

	/* Temporary storage for current cycle tput */
	unsigned int bat0_normal_tput;
	unsigned int bat0_frag_tput;
	unsigned int tx_tput;

	/* Configurable parameters (Mbps / MB/s) */
	u32 target_delay_ms;
	u32 rx_threshold_high_mbps;
	u32 rx_threshold_low_mbps;
	u32 rx_vote_bw_mb_high;
	u32 rx_vote_bw_mb_low;
	u32 tx_threshold_high_mbps;
	u32 tx_threshold_low_mbps;
	u32 tx_vote_bw_mb_high;
	u32 tx_vote_bw_mb_low;
	u32 vote_vc;

	/* Debugfs entry */
	struct dentry *dbg_dir;
};

static int soc_qos_parse_dt(struct soc_qos_ext *qos, const struct device_node *np)
{
	int ret = 0;

	qos->target_delay_ms = 0;
	qos->rx_threshold_high_mbps = 0;
	qos->rx_threshold_low_mbps = 0;
	qos->rx_vote_bw_mb_high = 0;
	qos->rx_vote_bw_mb_low = 0;
	qos->tx_threshold_high_mbps = 0;
	qos->tx_threshold_low_mbps = 0;
	qos->tx_vote_bw_mb_high = 0;
	qos->tx_vote_bw_mb_low = 0;
	qos->vote_vc = 0;

	if (!np)
		return -ENODEV;

	ret |= of_property_read_u32(np, "soc-qos-target-delay", &qos->target_delay_ms);
	ret |= of_property_read_u32(np, "soc-qos-rx-threshold-high", &qos->rx_threshold_high_mbps);
	ret |= of_property_read_u32(np, "soc-qos-rx-threshold-low", &qos->rx_threshold_low_mbps);
	ret |= of_property_read_u32(np, "soc-qos-rx-vote-high", &qos->rx_vote_bw_mb_high);
	ret |= of_property_read_u32(np, "soc-qos-rx-vote-low", &qos->rx_vote_bw_mb_low);
	ret |= of_property_read_u32(np, "soc-qos-tx-threshold-high", &qos->tx_threshold_high_mbps);
	ret |= of_property_read_u32(np, "soc-qos-tx-threshold-low", &qos->tx_threshold_low_mbps);
	ret |= of_property_read_u32(np, "soc-qos-tx-vote-high", &qos->tx_vote_bw_mb_high);
	ret |= of_property_read_u32(np, "soc-qos-tx-vote-low", &qos->tx_vote_bw_mb_low);
	ret |= of_property_read_u32(np, "soc-qos-vote-vc", &qos->vote_vc);

	return ret;
}

static void soc_qos_debugfs_init(struct soc_qos_ext *qos, struct radio_google *goog)
{
	struct dentry *parent_dir = get_radio_debugfs_root();

	if (IS_ERR_OR_NULL(parent_dir)) {
		LOG_ERR("Radio debugfs root not available");
		return;
	}

	qos->dbg_dir = debugfs_create_dir("soc_qos", parent_dir);
	if (IS_ERR_OR_NULL(qos->dbg_dir)) {
		LOG_ERR("Fail to create soc_qos debugfs dir");
		return;
	}

	debugfs_create_u32("target_delay_ms", 0644, qos->dbg_dir, &qos->target_delay_ms);
	debugfs_create_u32("rx_threshold_high_mbps", 0644, qos->dbg_dir,
			   &qos->rx_threshold_high_mbps);
	debugfs_create_u32("rx_threshold_low_mbps", 0644, qos->dbg_dir,
			   &qos->rx_threshold_low_mbps);
	debugfs_create_u32("rx_vote_bw_mb_high", 0644, qos->dbg_dir, &qos->rx_vote_bw_mb_high);
	debugfs_create_u32("rx_vote_bw_mb_low", 0644, qos->dbg_dir, &qos->rx_vote_bw_mb_low);
	debugfs_create_u32("tx_threshold_high_mbps", 0644, qos->dbg_dir,
			   &qos->tx_threshold_high_mbps);
	debugfs_create_u32("tx_threshold_low_mbps", 0644, qos->dbg_dir,
			   &qos->tx_threshold_low_mbps);
	debugfs_create_u32("tx_vote_bw_mb_high", 0644, qos->dbg_dir, &qos->tx_vote_bw_mb_high);
	debugfs_create_u32("tx_vote_bw_mb_low", 0644, qos->dbg_dir, &qos->tx_vote_bw_mb_low);
	debugfs_create_u32("vote_vc", 0644, qos->dbg_dir, &qos->vote_vc);
}

static void perform_ddr_vote(struct soc_qos_ext *qos, u32 vote_bw,
			     unsigned int rx_bytes, unsigned int tx_bytes)
{
	int ret;
	unsigned int vc = qos->vote_vc;
	unsigned int rx_mbps = (rx_bytes * 8) / 1000;
	unsigned int tx_mbps = (tx_bytes * 8) / 1000;

	ret = google_icc_set_write_bw_gmc(qos->icc_path, vote_bw, vote_bw, 0, vc);
	if (ret) {
		LOG_ERR("google_icc_set_write_bw_gmc() failed, ret=%d\n", ret);
		return;
	}

	ret = google_icc_update_constraint_async(qos->icc_path);
	if (ret) {
		LOG_ERR("google_icc_update_constraint_async() failed, ret=%d\n", ret);
		return;
	}

	qos->curr_vote_bw = vote_bw;
	LOG_INFO("Modem DDR Vote: %u MB/s at VC%u (RX:%u, TX:%u Mbps)\n",
		 vote_bw, vc, rx_mbps, tx_mbps);
}

int soc_qos_init(struct radio_google *goog)
{
	struct soc_qos_ext *soc_qos_ext;
	struct device *dev;
	struct device_node *root, *child_np;
	int ret;

	if (!goog || !goog->mdev || !goog->mdev->dev)
		return -ENODEV;

	dev = goog->mdev->dev;

	if (!get_soc_qos_enable_status())
		LOG_INFO("SoC QoS default disabled in dts\n");

	/* Allocate memory managed by the device */
	soc_qos_ext = devm_kzalloc(dev, sizeof(*soc_qos_ext), GFP_KERNEL);
	if (!soc_qos_ext)
		return -ENOMEM;

	soc_qos_ext->icc_path = google_devm_of_icc_get(goog->mdev->dev, "sswrp-pcie-0");

	if (IS_ERR(soc_qos_ext->icc_path)) {
		ret = PTR_ERR(soc_qos_ext->icc_path);
		LOG_ERR("devm_of_icc_get failed: %d\n", ret);
		return ret;
	}

	if (!soc_qos_ext->icc_path) {
		LOG_ERR("devm_of_icc_get returned NULL\n");
		return -EINVAL;
	}

	root = of_find_node_by_path("/");
	if (!root) {
		LOG_ERR("root dts node not found\n");
		return -EINVAL;
	}

	child_np = of_get_child_by_name(root, "radio-google-data");
	if (!child_np) {
		LOG_ERR("radio-google-data dts node not found\n");
		of_node_put(root);
		return -EINVAL;
	}

	ret = soc_qos_parse_dt(soc_qos_ext, child_np);
	if (ret) {
		LOG_ERR("Failed to parse SoC QoS properties from DT, ret=%d\n", ret);
		of_node_put(child_np);
		of_node_put(root);
		return ret;
	}

	of_node_put(child_np);
	of_node_put(root);

	if (soc_qos_ext->rx_vote_bw_mb_high == 0 && soc_qos_ext->tx_vote_bw_mb_high == 0)
		LOG_WARN("SoC QoS enabled but NOT configured\n");

	/* Initialize state machine votes with low bandwidth values */
	soc_qos_ext->curr_rx_vote_bw = soc_qos_ext->rx_vote_bw_mb_low;
	soc_qos_ext->curr_tx_vote_bw = soc_qos_ext->tx_vote_bw_mb_low;

	goog->soc_qos_ext = soc_qos_ext;
	soc_qos_ext->goog = goog;

	soc_qos_debugfs_init(soc_qos_ext, goog);

	LOG_INFO("SoC QoS init: RX Th:%u/%u Mbps, Vote:%u/%u MB/s\n",
		 soc_qos_ext->rx_threshold_high_mbps,
		 soc_qos_ext->rx_threshold_low_mbps,
		 soc_qos_ext->rx_vote_bw_mb_high,
		 soc_qos_ext->rx_vote_bw_mb_low);
	LOG_INFO("SoC QoS init: TX Th:%u/%u Mbps, Vote:%u/%u MB/s (VC%u), Dly:%u ms\n",
		 soc_qos_ext->tx_threshold_high_mbps,
		 soc_qos_ext->tx_threshold_low_mbps,
		 soc_qos_ext->tx_vote_bw_mb_high,
		 soc_qos_ext->tx_vote_bw_mb_low,
		 soc_qos_ext->vote_vc,
		 soc_qos_ext->target_delay_ms);

	return 0;
}

void soc_qos_exit(struct radio_google *goog)
{
	if (goog && goog->soc_qos_ext && goog->soc_qos_ext->dbg_dir)
		debugfs_remove_recursive(goog->soc_qos_ext->dbg_dir);
}

void soc_qos_report_normal_bat_tput(struct radio_google *goog, unsigned int tput)
{
	if (unlikely(!goog || !goog->soc_qos_ext))
		return;

	if (!get_soc_qos_enable_status())
		return;

	goog->soc_qos_ext->bat0_normal_tput = tput;
}
EXPORT_SYMBOL_GPL(soc_qos_report_normal_bat_tput);

void soc_qos_report_frag_bat_tput(struct radio_google *goog, unsigned int tput)
{
	if (unlikely(!goog || !goog->soc_qos_ext))
		return;

	if (!get_soc_qos_enable_status())
		return;

	goog->soc_qos_ext->bat0_frag_tput = tput;
}
EXPORT_SYMBOL_GPL(soc_qos_report_frag_bat_tput);

void soc_qos_report_tx_tput(struct radio_google *goog, unsigned int tput)
{
	if (unlikely(!goog || !goog->soc_qos_ext))
		return;

	if (!get_soc_qos_enable_status())
		return;

	goog->soc_qos_ext->tx_tput = tput;
}
EXPORT_SYMBOL_GPL(soc_qos_report_tx_tput);

void soc_qos_update_ddr_vote(struct radio_google *goog, unsigned int shift)
{
	unsigned int delay_in_ticks;
	unsigned int rx_bytes, tx_bytes;
	u32 total_vote_bw;
	struct soc_qos_ext *qos;

	if (unlikely(!goog || !goog->soc_qos_ext))
		return;

	qos = goog->soc_qos_ext;

	if (unlikely(!get_soc_qos_enable_status())) {
		if (qos->curr_vote_bw != 0)
			perform_ddr_vote(qos, 0, 0, 0);

		qos->low_vote_countdown = 0;
		qos->curr_rx_vote_bw = qos->rx_vote_bw_mb_low;
		qos->curr_tx_vote_bw = qos->tx_vote_bw_mb_low;
		goto out_cleanup_tput;
	}

	delay_in_ticks = (qos->target_delay_ms >> shift);
	/* Ensure we hold at least 1 tick. */
	if (delay_in_ticks == 0)
		delay_in_ticks = 1;

	/* Current cycle throughput in Bytes per ms */
	rx_bytes = (qos->bat0_normal_tput * SKB_BAT_SIZE) +
		   (qos->bat0_frag_tput * FRAG_BAT_SIZE);

	/* Calibration: TX DRB count is ~2x packet count due to Header+Data descriptors.
	 * Dividing by 2 normalizes DRB-speed to a more accurate Payload-speed.
	 */
	tx_bytes = (qos->tx_tput * SKB_BAT_SIZE) / 2;

	/* 1. Path-level Decisions (Independent Hysteresis via State Inheritance) */
	if (rx_bytes > (qos->rx_threshold_high_mbps * MBPS_TO_BYTES_PER_MS))
		qos->curr_rx_vote_bw = qos->rx_vote_bw_mb_high;
	else if (rx_bytes < (qos->rx_threshold_low_mbps * MBPS_TO_BYTES_PER_MS))
		qos->curr_rx_vote_bw = qos->rx_vote_bw_mb_low;

	if (tx_bytes > (qos->tx_threshold_high_mbps * MBPS_TO_BYTES_PER_MS))
		qos->curr_tx_vote_bw = qos->tx_vote_bw_mb_high;
	else if (tx_bytes < (qos->tx_threshold_low_mbps * MBPS_TO_BYTES_PER_MS))
		qos->curr_tx_vote_bw = qos->tx_vote_bw_mb_low;

	/* 2. Global Aggregation */
	total_vote_bw = max(qos->curr_rx_vote_bw, qos->curr_tx_vote_bw);

	/* 3. Trend-based Global Hysteresis State Machine */
	if (total_vote_bw > qos->curr_vote_bw) {
		/* Demand increased: Scale up immediately */
		perform_ddr_vote(qos, total_vote_bw, rx_bytes, tx_bytes);
		qos->low_vote_countdown = delay_in_ticks;
	} else if (total_vote_bw < qos->curr_vote_bw) {
		/* Demand decreased: Scale down after countdown */
		if (qos->low_vote_countdown > 0) {
			qos->low_vote_countdown--;
		} else {
			perform_ddr_vote(qos, total_vote_bw, rx_bytes, tx_bytes);
			qos->low_vote_countdown = delay_in_ticks;
		}
	} else {
		/* Demand constant: Reset countdown (Keep-alive) */
		qos->low_vote_countdown = delay_in_ticks;
	}

out_cleanup_tput:
	/* Clean up flags for the next cycle */
	qos->bat0_normal_tput = 0;
	qos->bat0_frag_tput = 0;
	qos->tx_tput = 0;
}
EXPORT_SYMBOL_GPL(soc_qos_update_ddr_vote);
#endif /* CONFIG_GOOGLE_MODEM_SOC_BW_QOS */
