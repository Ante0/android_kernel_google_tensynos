// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "dptx.h"
#include "dptx_utils.h"
#include "intr.h"
#include "regmaps/ctrl_fields.h"

static int dptx_link_read_status(struct dptx *dptx)
{
	return dptx_read_bytes_from_dpcd(dptx, DP_LANE0_1_STATUS,
					 dptx->link.status,
					 DP_LINK_STATUS_SIZE);
}

static int dptx_link_check_cr_done(struct dptx *dptx, bool *out_done, u32 delay)
{
	int retval;

	if (WARN_ON(!out_done))
		return -EINVAL;

	*out_done = false;

	fsleep(delay);

	retval = dptx_link_read_status(dptx);
	if (retval)
		return retval;

	*out_done = drm_dp_clock_recovery_ok(dptx->link.status,
					     dptx->link.lanes);

	if (dptx->link_test_mode && dptx->link_test_force_cr) {
		/* force DP link training clock recovery to pass */
		dptx_dbg_link(dptx, "%s: LINK STATUS: %02x %02x %02x\n", __func__,
			      dptx->link.status[0], dptx->link.status[1], dptx->link.status[2]);
		*out_done = true;
	}

	dptx_dbg_link(dptx, "%s: CR_DONE = %d\n", __func__, *out_done);

	return 0;
}

static int dptx_link_check_ch_eq_done(struct dptx *dptx,
				      bool *out_cr_done,
				      bool *out_ch_eq_done, u32 delay)
{
	int retval;
	bool done;

	if (WARN_ON(!out_cr_done || !out_ch_eq_done))
		return -EINVAL;

	retval = dptx_link_check_cr_done(dptx, &done, delay);
	if (retval)
		return retval;

	*out_cr_done = false;
	*out_ch_eq_done = false;

	if (!done)
		return 0;

	*out_cr_done = true;
	*out_ch_eq_done = drm_dp_channel_eq_ok(dptx->link.status,
					       dptx->link.lanes);

	if (dptx->link_test_mode && dptx->link_test_force_cheq) {
		/* force DP link training channel equalization to pass */
		dptx_dbg_link(dptx, "%s: LINK STATUS: %02x %02x %02x\n", __func__,
			      dptx->link.status[0], dptx->link.status[1], dptx->link.status[2]);
		*out_ch_eq_done = true;
	}

	dptx_dbg_link(dptx, "%s: CH_EQ_DONE = %d\n", __func__, *out_ch_eq_done);

	return 0;
}

static void dptx_link_set_preemp_vswing(struct dptx *dptx)
{
	unsigned int i;
	u8 retval;

	/* Need to reassert override before applying tuning parameters */
	google_dpphy_eq_tune_ovrd_enable(dptx->dp_phy,
		DPTX_PIN_TO_NUM_LANES(dptx->hw_config.pin_type), true);

	for (i = 0; i < dptx->link.lanes; i++) {
		u8 pe;
		u8 vs;

		pe = dptx->link.preemp_level[i];
		vs = dptx->link.vswing_level[i];

		dptx_dbg_link(dptx, "set PHY lane %u to VS=%u PE=%u\n", i, vs, pe);
		/*
		 * PHY_TX_EQ registers do not have signals tied to the ComboPHY and are effectively
		 * no-op so the usb phy driver callback is needed. Programming the registers
		 * anyways if debug status is needed.
		 */
		dptx_phy_set_pre_emphasis(dptx, i, pe);
		dptx_phy_set_vswing(dptx, i, vs);
		google_dpphy_pipe_tx_eq_set(dptx->dp_phy, i, vs, pe);
		google_dpphy_eq_tune_ovrd_apply(dptx->dp_phy, i, dptx->link.rate, vs, pe);
		google_dpphy_eq_tune_asic_read(dptx->dp_phy, i, dptx->hw_config.orient_type);
	}
}

static int dptx_link_training_lanes_set(struct dptx *dptx)
{
	int retval;
	unsigned int i;
	u8 bytes[4] = { 0xff, 0xff, 0xff, 0xff };

	for (i = 0; i < dptx->link.lanes; i++) {
		u8 byte = 0;

		byte |= ((dptx->link.vswing_level[i] <<
			  DP_TRAIN_VOLTAGE_SWING_SHIFT) &
			 DP_TRAIN_VOLTAGE_SWING_MASK);

		if (dptx->link.vswing_level[i] == DPTX_MAX_VS_LEVEL)
			byte |= DP_TRAIN_MAX_SWING_REACHED;

		byte |= ((dptx->link.preemp_level[i] <<
			  DP_TRAIN_PRE_EMPHASIS_SHIFT) &
			 DP_TRAIN_PRE_EMPHASIS_MASK);

		if (dptx->link.preemp_level[i] == DPTX_MAX_PE_LEVEL)
			byte |= DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

		bytes[i] = byte;
	}

	retval = dptx_write_bytes_to_dpcd(dptx, DP_TRAINING_LANE0_SET, bytes,
					  dptx->link.lanes);
	if (retval)
		return retval;

	return 0;
}

static int dptx_link_adjust_drive_settings(struct dptx *dptx, bool *out_changed,
					   bool *out_max_reached)
{
	unsigned int i;
	bool changed = false;
	bool max_reached = false;

	/* Save the drive settings */
	for (i = 0; i < dptx->link.lanes; i++) {
		u8 vs = drm_dp_get_adjust_request_voltage(dptx->link.status, i);
		u8 pe = drm_dp_get_adjust_request_pre_emphasis(dptx->link.status, i);

		vs >>= DP_TRAIN_VOLTAGE_SWING_SHIFT;
		pe >>= DP_TRAIN_PRE_EMPHASIS_SHIFT;

		if (vs > DPTX_MAX_VS_LEVEL) {
			dptx_dbg_link(dptx, "%s: VS=%u > DPTX_MAX_VS_LEVEL\n", __func__, vs);
			vs = DPTX_MAX_VS_LEVEL;
		}
		if (pe > DPTX_MAX_PE_LEVEL) {
			dptx_dbg_link(dptx, "%s: PE=%u > DPTX_MAX_PE_LEVEL\n", __func__, pe);
			pe = DPTX_MAX_PE_LEVEL;
		}
		if ((vs + pe) > DPTX_MAX_VS_PE_SUM) {
			dptx_dbg_link(dptx, "%s: VS+PE=%u > DPTX_MAX_VS_PE_SUM\n", __func__, vs+pe);
			vs = DPTX_MAX_VS_PE_SUM - pe;
		}

		if (vs == DPTX_MAX_VS_LEVEL || pe == DPTX_MAX_PE_LEVEL)
			max_reached = true;
		if (dptx->link.vswing_level[i] != vs)
			changed = true;
		if (dptx->link.preemp_level[i] != pe)
			changed = true;

		dptx->link.vswing_level[i] = vs;
		dptx->link.preemp_level[i] = pe;
		dptx_dbg_link(dptx, "adjust lane %u to VS=%u PE=%u\n", i, vs, pe);
	}

	if (out_changed)
		*out_changed = changed;

	if (out_max_reached)
		*out_max_reached = max_reached;

	return 0;
}

static int dptx_link_apply_drive_settings(struct dptx *dptx)
{
	dptx_link_set_preemp_vswing(dptx);
	return dptx_link_training_lanes_set(dptx);
}

static unsigned long dp_init_pe = 0;
module_param(dp_init_pe, ulong, 0664);
MODULE_PARM_DESC(dp_init_pe, "DP link initial pre-emphasis value: 0-3");

static unsigned long dp_init_vs = 0;
module_param(dp_init_vs, ulong, 0664);
MODULE_PARM_DESC(dp_init_vs, "DP link initial voltage swing value: 0-3");

int dptx_set_link_configs(struct dptx *dptx, u8 rate, u8 lanes)
{
	int sink_max_rate;
	u8 sink_max_lanes;
	u8 pin_cfg_max_lanes;

	if (WARN(rate > DPTX_PHYIF_CTRL_RATE_HBR3,
		 "Invalid rate %d\n", rate))
		rate = DPTX_PHYIF_CTRL_RATE_RBR;

	if (WARN(!lanes || lanes == 3 || lanes > 4,
		 "Invalid lanes %d\n", lanes))
		lanes = 1;

	if (dp_init_pe > DPTX_MAX_PE_LEVEL)
		dp_init_pe = 0;

	if (dp_init_vs > DPTX_MAX_VS_LEVEL)
		dp_init_vs = 0;

	if ((dp_init_pe + dp_init_vs) > DPTX_MAX_VS_PE_SUM)
		dp_init_vs = DPTX_MAX_VS_PE_SUM - dp_init_pe;

	/* Initialize link parameters */
	memset(dptx->link.preemp_level, (u8)dp_init_pe, sizeof(u8) * 4);
	memset(dptx->link.vswing_level, (u8)dp_init_vs, sizeof(u8) * 4);
	memset(dptx->link.status, 0, DP_LINK_STATUS_SIZE);

	sink_max_lanes = drm_dp_max_lane_count(dptx->rx_caps);
	if (sink_max_lanes != 4 && sink_max_lanes != 2 && sink_max_lanes != 1) {
		dptx_warn(dptx, "sink reports invalid max lanes, proceed with 4\n");
		sink_max_lanes = 4;
	}

	pin_cfg_max_lanes = DPTX_PIN_TO_NUM_LANES(dptx->typec_pin_assignment);
	if (sink_max_lanes > pin_cfg_max_lanes) {
		dptx_warn(dptx, "mismatch: sink max lanes > pin config max lanes\n");
		sink_max_lanes = pin_cfg_max_lanes;
	}

	if (lanes > sink_max_lanes)
		lanes = sink_max_lanes;

	sink_max_rate = dptx_bw_to_phy_rate(dptx->rx_caps[DP_MAX_LINK_RATE]);
	if (sink_max_rate < 0) {
		dptx_warn(dptx, "sink reports invalid max rate, proceed with HBR2\n");
		sink_max_rate = DPTX_PHYIF_CTRL_RATE_HBR2;
	}

	if (rate > sink_max_rate)
		rate = sink_max_rate;

	dptx->link.lanes = lanes;
	dptx->link.rate = rate;
	dptx->link.ef = dptx->ef_en && drm_dp_enhanced_frame_cap(dptx->rx_caps);
	dptx->link.ssc = dptx->ssc_en &&
			 !!(dptx->rx_caps[DP_MAX_DOWNSPREAD] & DP_MAX_DOWNSPREAD_0_5);
	dptx->link.fec = dptx->fec_en && !!(dptx->fec_caps & DP_FEC_CAPABLE);
	dptx->link.dsc = dptx->dsc_en && !!(dptx->dsc_caps & DP_DSC_DECOMPRESSION_IS_SUPPORTED);
	dptx->link.trained = false;

	dptx_dbg_link(dptx, "%s: rate=%d lanes=%d EF=%d SSC=%d FEC=%d DSC=%d\n",
		      __func__, dptx->link.rate, dptx->link.lanes, dptx->link.ef,
		      dptx->link.ssc, dptx->link.fec, dptx->link.dsc);

	return 0;
}

static int dptx_link_training_pattern_set(struct dptx *dptx, u8 pattern)
{
	int retval;

	retval = dptx_write_dpcd(dptx, DP_TRAINING_PATTERN_SET, pattern);
	if (retval)
		return retval;

	return 0;
}

static int dptx_link_wait_cr_and_adjust(struct dptx *dptx, bool ch_eq)
{
	int i;
	int retval;
	bool changed = false;
	bool max_reached = false;
	bool done = false;
	u32 same_vs_pe = 0;
	u32 cr_delay = 100;  /* cr_delay: 100 usec */

	/* First check for clock recovery status */
	retval = dptx_link_check_cr_done(dptx, &done, cr_delay);
	if (retval)
		return retval;

	if (done)
		return 0;

	/*
	 * Adjust and check status max 9 more times
	 * Unchanged VS/PE request is checked max 4 more times
	 */
	for (i = 0; i < 9; i++) {
		retval = dptx_link_adjust_drive_settings(dptx, &changed, &max_reached);
		if (retval)
			return retval;

		if (changed)
			same_vs_pe = 0;
		else if (++same_vs_pe == 5)
			return -EPROTO;

		retval = dptx_link_apply_drive_settings(dptx);
		if (retval)
			return retval;

		retval = dptx_link_check_cr_done(dptx, &done, cr_delay);
		if (retval)
			return retval;

		if (done)
			return 0;

		if (max_reached)
			return -EPROTO;
	}

	return -EPROTO;
}

static int dptx_link_cr(struct dptx *dptx)
{
	int retval, count;
	u8 byte;
	struct ctrl_regfields *ctrl_fields;
	u32 phyif_reg;
	u32 rst;

	ctrl_fields = dptx->ctrl_fields;

	dptx_phy_enable_xmit(dptx, dptx->link.lanes, false);

	/* Set PHY lanes */
	dptx_phy_set_lanes(dptx, dptx->link.lanes);

	/* Move PHY to DPTX_PHY_POWER_DOWN (P3) */
	//dptx_phy_set_lanes_powerdown_state(dptx, DPTX_PHY_INTER_P2_POWER);
	dptx_write_regfield(dptx, ctrl_fields->field_phy_powerdown, DPTX_PHY_POWER_DOWN);
	byte = dptx_read_regfield(dptx, ctrl_fields->field_phy_powerdown);
	dptx_dbg_link(dptx, "PHY POWERDOWN STATE: %u\n", byte);
	dptx_dbg_link(dptx, "Lanes to wait PHY BUSY: %u\n", dptx->link.lanes);
	retval = dptx_phy_wait_busy(dptx, dptx->link.lanes);
	if (retval) {
		dptx_err(dptx, "Timed out waiting for PHY BUSY\n");
		return retval;
	}

	/* Disable DP PHY pipe clock */
	retval = google_dpphy_set_maxpclk(dptx->dp_phy, 2, 2);
	if (retval) {
		dptx_err(dptx, "Failed to set pipe clock\n");
		return retval;
	}
	google_dpphy_set_pipe_pclk_on(dptx->dp_phy, 0);
	udelay(100);

	/* Assert DP PHY reset */
	rst = dptx_read_reg(dptx, dptx->regs[DPTX], SOFT_RESET_CTRL);
	rst |= DPTX_SRST_CTRL_PHY;
	dptx_write_reg(dptx, dptx->regs[DPTX], SOFT_RESET_CTRL, rst);

	/* Set PHY rate + lanes + SSC */
	dptx_dbg_link(dptx, "Reset PHY to rate %d lanes %d SSC %d\n",
		      dptx->link.rate, dptx->link.lanes, dptx->link.ssc);
	phyif_reg = dptx_read_reg(dptx, dptx->regs[DPTX], PHYIF_CTRL);
	phyif_reg &= ~DPTX_PHYIF_CTRL_RATE_LANES_SSC_MASK;
	phyif_reg |= DPTX_PHYIF_CTRL_RATE_VAL(dptx->link.rate);
	phyif_reg |= DPTX_PHYIF_CTRL_LANES_VAL(dptx->link.lanes);
	phyif_reg |= DPTX_PHYIF_CTRL_SSC_VAL(!dptx->link.ssc);
	dptx_write_reg(dptx, dptx->regs[DPTX], PHYIF_CTRL, phyif_reg);
	udelay(100);

	/* De-assert DP PHY reset */
	rst = dptx_read_reg(dptx, dptx->regs[DPTX], SOFT_RESET_CTRL);
	rst &= ~DPTX_SRST_CTRL_PHY;
	dptx_write_reg(dptx, dptx->regs[DPTX], SOFT_RESET_CTRL, rst);
	udelay(100);

	/* Enable DP PHY pipe clock */
	google_dpphy_set_pipe_pclk_on(dptx->dp_phy, 1);
	udelay(100);
	retval = google_dpphy_set_maxpclk(dptx->dp_phy, 3, 3);
	if (retval) {
		dptx_err(dptx, "Failed to set pipe clock\n");
		return retval;
	}

	retval = dptx_phy_wait_busy(dptx, dptx->link.lanes);
	if (retval) {
		dptx_err(dptx, "Timed out waiting for PHY BUSY\n");
		return retval;
	}

	/* Force no transmitted pattern */
	dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_NONE);

	/* Lanes PowerDown State */
	//dptx_phy_set_lanes_powerdown_state(dptx, DPTX_PHY_POWER_ON);
	dptx_write_regfield(dptx, ctrl_fields->field_phy_powerdown, DPTX_PHY_POWER_ON);
	byte = dptx_read_regfield(dptx, ctrl_fields->field_phy_powerdown);
	dptx_dbg_link(dptx, "PHY POWERDOWN STATE: %u\n", byte);
	retval = dptx_phy_wait_busy(dptx, dptx->link.lanes);
	if (retval) {
		dptx_err(dptx, "Timed out waiting for PHY BUSY\n");
		return retval;
	}

	/* Set PHY_TX_EQ */
	dptx_link_set_preemp_vswing(dptx);

	phyif_reg = ctrl_read(dptx, PHYIF_CTRL);
	dptx_dbg(dptx, "Before Pattern 1: PHYIF REG: 0x%X\n", phyif_reg);

	/* Set TPS1 pattern to transmitte */
	dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_1);

	/* Enable Transmitter */
	dptx_phy_enable_xmit(dptx, dptx->link.lanes, true);

	phyif_reg = ctrl_read(dptx, PHYIF_CTRL);
	dptx_dbg(dptx, "After Pattern 1: PHYIF REG: 0x%X\n", phyif_reg);

	/* Define Stream Mode */
	dptx_write_regfield(dptx, ctrl_fields->field_enable_mst_mode, dptx->mst);
	dptx_dbg(dptx, "Stream Mode: %u\n", dptx->mst);

// Set Sink DPCD registers --------------------------------------

	retval = dptx_phy_rate_to_bw(dptx->link.rate);
	if (retval < 0)
		return retval;

	byte = retval;
	retval = dptx_write_dpcd(dptx, DP_LINK_BW_SET, byte);
	if (retval)
		return retval;

	byte = (dptx->link.ef ? DP_ENHANCED_FRAME_CAP : 0) | dptx->link.lanes;
	retval = dptx_write_dpcd(dptx, DP_LANE_COUNT_SET, byte);
	if (retval)
		return retval;

	byte = dptx->link.ssc ? DP_SPREAD_AMP_0_5 : 0;
	retval = dptx_write_dpcd(dptx, DP_DOWNSPREAD_CTRL, byte);
	if (retval)
		return retval;

	byte = 0x01;
	retval = dptx_write_dpcd(dptx, DP_MAIN_LINK_CHANNEL_CODING_SET, byte);
	if (retval)
		return retval;

	/* Set TRAINING_PATTERN_SET register */
	retval = dptx_write_dpcd(dptx, DP_TRAINING_PATTERN_SET,
				 DP_TRAINING_PATTERN_1 | DP_LINK_SCRAMBLING_DISABLE);
	if (retval)
		return retval;

	/* Set TRAINING_LANEx_SET registers */
	retval = dptx_link_training_lanes_set(dptx);
	if (retval)
		return retval;

	retval = dptx_link_wait_cr_and_adjust(dptx, false);

	return retval;
}

static int dptx_link_ch_eq(struct dptx *dptx)
{
	int retval;
	bool cr_done;
	bool ch_eq_done;
	unsigned int pattern;
	unsigned int i;
	u8 dp_pattern;
	struct ctrl_regfields *ctrl_fields;
	u8 byte;
	u32 ch_eq_delay;

	ctrl_fields = dptx->ctrl_fields;

	if (drm_dp_tps4_supported(dptx->rx_caps)) {
		pattern = DPTX_PHYIF_CTRL_TPS_4;
		dp_pattern = DP_TRAINING_PATTERN_4;
	} else if (drm_dp_tps3_supported(dptx->rx_caps)) {
		pattern = DPTX_PHYIF_CTRL_TPS_3;
		dp_pattern = DP_TRAINING_PATTERN_3;
	} else {
		pattern = DPTX_PHYIF_CTRL_TPS_2;
		dp_pattern = DP_TRAINING_PATTERN_2;
	}

	/* ch_eq_delay: 400/4000/8000/12000/16000 usec */
	retval = dptx_read_dpcd(dptx, DP_TRAINING_AUX_RD_INTERVAL, &byte);
	if (retval)
		return retval;

	ch_eq_delay = min_t(u32, (byte & 0x7f), 4);
	ch_eq_delay *= 4000;
	if (ch_eq_delay == 0)
		ch_eq_delay = 400;

	dptx_phy_set_pattern(dptx, pattern);

	/* Set TRAINING_PATTERN_SET register */
	if (dp_pattern != DP_TRAINING_PATTERN_4) {
		retval = dptx_link_training_pattern_set(dptx,
							dp_pattern | DP_LINK_SCRAMBLING_DISABLE);
	} else {
		retval = dptx_link_training_pattern_set(dptx, dp_pattern);
		dptx_dbg_link(dptx, "%s:  Enabling scrambling for TPS4\n", __func__);
		dptx_write_regfield(dptx, ctrl_fields->field_scramble_dis, 0);
	}

	if (retval)
		return retval;

	/* Set TRAINING_LANEx_SET registers */
	retval = dptx_link_training_lanes_set(dptx);
	if (retval)
		return retval;

	/* First check for channel equalization status */
	retval = dptx_link_check_ch_eq_done(dptx, &cr_done, &ch_eq_done, ch_eq_delay);
	if (retval)
		return retval;

	dptx->cr_fail = !cr_done;
	if (!cr_done)
		return -EPROTO;

	if (ch_eq_done)
		return 0;

	/* Adjust and check status max 5 more times */
	for (i = 0; i < 5; i++) {
		retval = dptx_link_adjust_drive_settings(dptx, NULL, NULL);
		if (retval)
			return retval;

		retval = dptx_link_apply_drive_settings(dptx);
		if (retval)
			return retval;

		retval = dptx_link_check_ch_eq_done(dptx, &cr_done, &ch_eq_done, ch_eq_delay);
		if (retval)
			return retval;

		dptx->cr_fail = !cr_done;
		if (!cr_done)
			return -EPROTO;

		if (ch_eq_done)
			return 0;
	}

	return -EPROTO;
}

static int dptx_link_reduce_rate(struct dptx *dptx)
{
	unsigned int rate = dptx->link.rate;

	switch (rate) {
	case DPTX_PHYIF_CTRL_RATE_RBR:
		return -EPROTO;
	case DPTX_PHYIF_CTRL_RATE_HBR:
		rate = DPTX_PHYIF_CTRL_RATE_RBR;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR2:
		rate = DPTX_PHYIF_CTRL_RATE_HBR;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR3:
		rate = DPTX_PHYIF_CTRL_RATE_HBR2;
		break;
	}

	dptx_dbg_link(dptx, "%s: Reducing rate from %d to %d\n",
		__func__, dptx->link.rate, rate);
	dptx->link.rate = rate;
	return 0;
}

static int dptx_link_reduce_lanes(struct dptx *dptx)
{
	unsigned int lanes;

	switch (dptx->link.lanes) {
	case 4:
		lanes = 2;
		break;
	case 2:
		lanes = 1;
		break;
	case 1:
	default:
		return -EPROTO;
	}

	dptx_dbg_link(dptx, "%s: Reducing lanes from %d to %d\n",
		 __func__, dptx->link.lanes, lanes);
	dptx->link.lanes = lanes;
	return 0;
}

int dptx_link_training(struct dptx *dptx)
{
	int retval, i;
	u8 byte;
	u32 hpd_sts;
	u32 phyif_reg;
	struct ctrl_regfields *ctrl_fields;
	int num_lanes = DPTX_PIN_TO_NUM_LANES(dptx->hw_config.pin_type);

	ctrl_fields = dptx->ctrl_fields;
	i = 0;

	dptx_set_link_configs(dptx, dptx->link.rate, dptx->link.lanes);

	dptx_dbg_link(dptx, "--- START LINK TRAINING ---\n");

	/* Check LTTPR (Link Training Tunable PHY Repeater) presence */
	if (dptx->rx_caps[0] >= DP_DPCD_REV_14) {
		byte = 0;
		dptx_read_dpcd(dptx, DP_PHY_REPEATER_CNT, &byte);
		dptx_dbg_link(dptx, "LTTPR count = 0x%02x\n", byte);
	}

	phyif_reg = ctrl_read(dptx, PHYIF_CTRL);
	dptx_dbg(dptx, "PHYIF REG: 0x%X\n", phyif_reg);

	/*phy_pwrdown_reg = dptx_read_reg(dptx, dptx->regs[DPTX], PHYIF_PWRDOWN_CTRL);
	phy_pwrdown_reg = set(phy_pwrdown_reg, 0x10000, 0x1);	// Enable Per Lane Powerdown Control
	dptx_write_reg(dptx, dptx->regs[DPTX], PHYIF_PWRDOWN_CTRL, phy_pwrdown_reg);*/

again:
	i++;
	dptx_dbg_link(dptx, "... ITERATION %d ...\n", i);
	dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_NONE);
	dptx_link_training_pattern_set(dptx, DP_TRAINING_PATTERN_DISABLE);

	dptx_dbg_link(dptx, "... Clock Recovery ...\n");
	retval = dptx_link_cr(dptx);
	if (retval) {
		if (retval == -EPROTO) {
			if (dptx_link_reduce_rate(dptx)) {
				if (dptx_link_reduce_lanes(dptx))
					goto fail;
				dptx_dbg_link(dptx, ">>> Reduce Lanes\n");
				dptx->link.rate = dptx->max_rate;
			} else {
				dptx_dbg_link(dptx, ">>> Reduce Rate\n");
			}

			dptx_set_link_configs(dptx, dptx->link.rate, dptx->link.lanes);
			goto again;
		} else {
			goto fail;
		}
	}

	dptx_dbg_link(dptx, "... Channel Equalization ...\n");
	retval = dptx_link_ch_eq(dptx);
	if (retval) {
		if (retval == -EPROTO) {
			if (!dptx->cr_fail) {
				if (dptx_link_reduce_lanes(dptx)) {
					if (dptx_link_reduce_rate(dptx))
						goto fail;
					dptx_dbg_link(dptx, ">>> Reduce Rate\n");
					dptx->link.lanes = dptx->max_lanes;
				} else {
					dptx_dbg_link(dptx, ">>> Reduce Lanes\n");
				}
			} else {
				if (dptx_link_reduce_rate(dptx)) {
					if (dptx_link_reduce_lanes(dptx))
						goto fail;
					dptx_dbg_link(dptx, ">>> Reduce Lanes\n");
					dptx->link.rate = dptx->max_rate;
				} else {
					dptx_dbg_link(dptx, ">>> Reduce Rate\n");
				}
			}

			dptx_set_link_configs(dptx, dptx->link.rate, dptx->link.lanes);
			goto again;
		} else {
			goto fail;
		}
	}

	/* Mute audio to set VB-ID AudioMute flag for DP idle pattern */
	dptx_write_regfield(dptx, ctrl_fields->field_audio_mute, 1);

	dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_NONE);

	retval = dptx_link_training_pattern_set(dptx, DP_TRAINING_PATTERN_DISABLE);
	if (retval)
		goto fail;

	dptx_phy_enable_xmit(dptx, dptx->link.lanes, true);
	dptx->link.trained = true;

	dptx_dbg_link(dptx, "--- LINK TRAINING DONE: rate=%d lanes=%d ---\n",
		 dptx->link.rate, dptx->link.lanes);

	return 0;

fail:
	/* Reset TX EQ message bus */
	for (int i = 0; i < num_lanes; i++)
		google_dpphy_pipe_tx_eq_set(dptx->dp_phy, i, 0, 0);
	google_dpphy_eq_tune_ovrd_enable(dptx->dp_phy, num_lanes, false);

	hpd_sts = dptx_read_regfield(dptx, ctrl_fields->field_hpd_status);
	if (hpd_sts) {
		dptx_phy_set_pattern(dptx, DPTX_PHYIF_CTRL_TPS_NONE);
		dptx_link_training_pattern_set(dptx, DP_TRAINING_PATTERN_DISABLE);
		dptx_err(dptx, "--- LINK TRAINING FAILED: %d ---\n", retval);
		dptx->stats.link_negotiation_failures++;
	} else {
		dptx_err(dptx, "--- LINK TRAINING FAILED: sink disconnected %d ---\n", retval);
	}

	return retval;
}

int dptx_fast_link_training(struct dptx *dptx)
{
	int nr_lanes;
	int link_rate;
	int count;
	struct ctrl_regfields *ctrl_fields;

	dptx_dbg_link(dptx, "--- STARTING FAST LINK TRAINING ---\n");

	ctrl_fields = dptx->ctrl_fields;
	nr_lanes = dptx->max_lanes;
	link_rate = dptx->max_rate;
	dptx_write_regfield(dptx, ctrl_fields->field_phy_powerdown, 0);
	dptx_write_regfield(dptx, ctrl_fields->field_phyrate, link_rate);

	switch (nr_lanes) {
	case (1):
		dptx_write_regfield(dptx, ctrl_fields->field_phy_lanes, 0);
		break;
	case (2):
		dptx_write_regfield(dptx, ctrl_fields->field_phy_lanes, 2);
		break;
	case (4):
		dptx_write_regfield(dptx, ctrl_fields->field_phy_lanes, 4);
		break;
	default:
		dptx_write_regfield(dptx, ctrl_fields->field_phy_lanes, 0);
	}

	count = 0;
	dptx_dbg_link(dptx, "PHY BUSY...WAITING...\n");
	while (dptx_read_regfield(dptx, ctrl_fields->field_phy_busy)) {
		count++;
		if (count > 1000)
			return -EBUSY;
		mdelay(20);
	}

	dptx_link_set_preemp_vswing(dptx);
	dptx_phy_set_pattern(dptx, 1);
	dptx_phy_enable_xmit(dptx, nr_lanes, true);

	udelay(500);

	switch (link_rate) {
	case (DPTX_PHYIF_CTRL_RATE_HBR):
		dptx_phy_set_pattern(dptx, 2);
		break;
	case (DPTX_PHYIF_CTRL_RATE_HBR2):
		dptx_phy_set_pattern(dptx, 3);
		break;
	case (DPTX_PHYIF_CTRL_RATE_HBR3):
		dptx_phy_set_pattern(dptx, 4);
		break;
	default:
		dptx_phy_set_pattern(dptx, 2);
		break;
	}

	udelay(500);

	dptx_phy_set_pattern(dptx, 0);

	return 0;
}

int dptx_link_check_status(struct dptx *dptx, u8 *irq_vector)
{
	int retval;
	u8 byte;
	u8 sink_count;

	retval = dptx_read_dpcd(dptx, DP_SINK_COUNT, &byte);
	if (retval) {
		dptx_dbg_link(dptx, "%s: cannot read DP_SINK_COUNT\n", __func__);
		return retval;
	}

	sink_count = DP_GET_SINK_COUNT(byte);
	dptx_dbg_link(dptx, "%s: sink count = %d\n", __func__, sink_count);

	retval = dptx_read_dpcd(dptx, DP_DEVICE_SERVICE_IRQ_VECTOR, irq_vector);
	if (retval) {
		dptx_dbg_link(dptx, "%s: cannot read DP_IRQ_VECTOR\n", __func__);
		return retval;
	}

	if (*irq_vector != 0)
		dptx_write_dpcd(dptx, DP_DEVICE_SERVICE_IRQ_VECTOR, *irq_vector);

	dptx_dbg_link(dptx, "%s: irq vector = 0x%02x\n", __func__, *irq_vector);

	if (dptx->branch_dev) {
		/* Check the sink count */
		if (sink_count > dptx->dfp_count + 1) {
			dptx_err(dptx, "%s: invalid sink count, adjusting to 0\n", __func__);
			sink_count = 0;
		}

		if (dptx->sink_count == 0 && sink_count > 0) {
			/* sink plugged into DFP port */
			dptx->sink_count = sink_count;
			return handle_hotplug_core(dptx);
		}

		if (dptx->sink_count > 0 && sink_count == 0) {
			/* sink unplugged from DFP port */
			dptx->sink_count = sink_count;
			return handle_hotunplug_core(dptx, true);
		}
	}

	retval = dptx_link_read_status(dptx);
	if (retval) {
		dptx_dbg_link(dptx, "%s: cannot read link status\n", __func__);
		return retval;
	}

	byte = dptx->link.status[DP_LANE_ALIGN_STATUS_UPDATED -
				 DP_LANE0_1_STATUS];

	if (!(byte & DP_LINK_STATUS_UPDATED)) {
		dptx_dbg_link(dptx, "%s: no change in link status\n", __func__);
		return 0;
	}

	/* Check if need to retrain link */
	if (dptx->link.trained &&
	    (!drm_dp_channel_eq_ok(dptx->link.status, dptx->link.lanes) ||
	     !drm_dp_clock_recovery_ok(dptx->link.status, dptx->link.lanes))) {
		dptx_dbg_link(dptx, "%s: Retraining link\n", __func__);
		dptx->stats.link_unstable_failures++;
		handle_hotunplug_core(dptx, true);
		handle_hotplug_core(dptx);
	}

	return 0;
}
