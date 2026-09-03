// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "dptx.h"
#include "dptx_utils.h"
#include "regmaps/ctrl_fields.h"

/*
 * Core Access Layer
 *
 * Provides low-level register access to the DPTX core.
 */

/**
 * dptx_intr_en() - Enables interrupts
 * @dptx: The dptx struct
 * @bits: The interrupts to enable
 *
 * This function enables (unmasks) all interrupts in the INTERRUPT
 * register specified by @bits.
 */
static void dptx_intr_en(struct dptx *dptx, u32 bits)
{
	u32 ien;

	ien = dptx_read_reg(dptx, dptx->regs[DPTX], GENERAL_INTERRUPT_ENABLE);
	ien |= bits;
	dptx_write_reg(dptx, dptx->regs[DPTX], GENERAL_INTERRUPT_ENABLE, ien);
}

/**
 * dptx_intr_dis() - Disables interrupts
 * @dptx: The dptx struct
 * @bits: The interrupts to disable
 *
 * This function disables (masks) all interrupts in the INTERRUPT
 * register specified by @bits.
 */
static void dptx_intr_dis(struct dptx *dptx, u32 bits)
{
	u32 ien;

	ien = dptx_read_reg(dptx, dptx->regs[DPTX], GENERAL_INTERRUPT_ENABLE);
	ien &= ~bits;
	dptx_write_reg(dptx, dptx->regs[DPTX], GENERAL_INTERRUPT_ENABLE, ien);
}

/**
 * dptx_global_intr_en() - Enables top-level interrupts
 * @dptx: The dptx struct
 *
 * Enables (unmasks) all top-level interrupts.
 */
void dptx_global_intr_en(struct dptx *dptx)
{
	dptx_intr_en(dptx, DPTX_IEN_ALL_INTR &
		     ~(DPTX_ISTS_AUX_REPLY | DPTX_ISTS_AUX_CMD_INVALID));
}

/**
 * dptx_global_intr_dis() - Disables top-level interrupts
 * @dptx: The dptx struct
 *
 * Disables (masks) all top-level interrupts.
 */
void dptx_global_intr_dis(struct dptx *dptx)
{
	dptx_intr_dis(dptx, DPTX_IEN_ALL_INTR);
}

/**
 * dptx_soft_reset() - Performs a core soft reset
 * @dptx: The dptx struct
 * @bits: The components to reset
 *
 * Resets specified parts of the core by writing @bits into the core
 * soft reset control register and clearing them 10-20 microseconds
 * later.
 */
void dptx_soft_reset(struct dptx *dptx, u32 bits)
{
	u32 rst;

	bits &= (DPTX_SRST_CTRL_ALL);

	/* Set reset bits */
	rst = dptx_read_reg(dptx, dptx->regs[DPTX], SOFT_RESET_CTRL);
	rst |= bits;
	dptx_write_reg(dptx, dptx->regs[DPTX], SOFT_RESET_CTRL, rst);

	usleep_range(10, 20);

	/* Clear reset bits */
	rst = dptx_read_reg(dptx, dptx->regs[DPTX], SOFT_RESET_CTRL);
	rst &= ~bits;
	dptx_write_reg(dptx, dptx->regs[DPTX], SOFT_RESET_CTRL, rst);
}

/**
 * dptx_soft_reset_all() - Reset all core modules
 * @dptx: The dptx struct
 */
void dptx_soft_reset_all(struct dptx *dptx)
{
	dptx_soft_reset(dptx, DPTX_SRST_CTRL_ALL);
}

void dptx_phy_soft_reset(struct dptx *dptx)
{
	dptx_soft_reset(dptx, DPTX_SRST_CTRL_PHY);
}

/**
 * dptx_core_init_phy() - Initializes the DP TX PHY module
 * @dptx: The dptx struct
 *
 * Initializes the PHY layer of the core. This needs to be called
 * whenever the PHY layer is reset.
 */
void dptx_core_init_phy(struct dptx *dptx)
{
	/* DO NOT touch phy_width field */
#if 0
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	dptx_write_regfield(dptx, ctrl_fields->field_phy_width, 1);
#endif
}

/**
 * dptx_sink_enabled_ssc() - Returns true, if sink is enabled ssc
 * @dptx: The dptx struct
 *
 */
bool dptx_sink_enabled_ssc(struct dptx *dptx)
{
	u8 byte = 0;

	dptx_read_dpcd(dptx, DP_MAX_DOWNSPREAD, &byte);

	return !!(byte & DP_MAX_DOWNSPREAD_0_5);
}

/**
 * dptx_core_program_ssc() - Move phy to P3 state and programs SSC
 * @dptx: The dptx struct
 * @enable: enable/disable SSC
 *
 * Enables SSC should be called during hot plug.
 *
 */
int dptx_core_program_ssc(struct dptx *dptx, bool enable)
{
	u8  retval;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	/* Enable 4 lanes, before programming SSC */
	dptx_phy_set_lanes(dptx, 4);

	// Move PHY to P3 to program SSC
	dptx_write_regfield(dptx, ctrl_fields->field_phy_powerdown, 3);

	retval = dptx_phy_wait_busy(dptx, DPTX_MAX_LINK_LANES);
	if (retval) {
		dptx_err(dptx, "Timed out waiting for PHY BUSY\n");
		return retval;
	}

	if (enable)
		dptx_write_regfield(dptx, ctrl_fields->field_ssc_dis, 0);
	else
		dptx_write_regfield(dptx, ctrl_fields->field_ssc_dis, 1);

	retval = dptx_phy_wait_busy(dptx, DPTX_MAX_LINK_LANES);
	if (retval) {
		dptx_err(dptx, "Timed out waiting for PHY BUSY\n");
		return retval;
	}

	return 0;
}

/**
 * dptx_check_dptx_id() - Check value of DPTX_ID register
 * @dptx: The dptx struct
 *
 * Returns True if DPTX core correctly identifyed.
 */
bool dptx_check_dptx_id(struct dptx *dptx)
{
	u32 dptx_id;

	dptx_id = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_ID);
	if (dptx_id != ((DPTX_ID_DEVICE_ID << DPTX_ID_DEVICE_ID_SHIFT) |
			DPTX_ID_VENDOR_ID))
		return false;

	return true;
}

/**
 * dptx_enable_ssc() - Enables SSC based on automation request,
 *		      if DPTX controller enables ssc
 * @dptx: The dptx struct
 *
 */
void dptx_enable_ssc(struct dptx *dptx)
{
	bool sink_ssc = dptx_sink_enabled_ssc(dptx);

	if (sink_ssc)
		dev_dbg(dptx->dev, "%s: SSC enable on the sink side\n", __func__);
	else
		dev_dbg(dptx->dev, "%s: SSC disabled on the sink side\n", __func__);

	dptx_core_program_ssc(dptx, dptx->ssc_en && sink_ssc);
}

void dptx_init_hwparams(struct dptx *dptx)
{
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	/* Num MST streams */
	dptx->streams = dptx_read_regfield(dptx, ctrl_fields->field_num_streams);

	/* Combo PHY */
	dptx->hwparams.gen2phy = dptx_read_regfield(dptx, ctrl_fields->field_gen2_phy);

	/* DSC */
	dptx->hwparams.dsc = dptx_read_regfield(dptx, ctrl_fields->field_dsc_en);

	/* Multi pixel mode */
	switch (dptx_read_regfield(dptx, ctrl_fields->field_mp_mode)) {
	default:
	case DPTX_CONFIG1_MP_MODE_SINGLE:
		dptx->hwparams.multipixel = DPTX_MP_SINGLE_PIXEL;
		break;
	case DPTX_CONFIG1_MP_MODE_DUAL:
		dptx->hwparams.multipixel = DPTX_MP_DUAL_PIXEL;
		break;
	case DPTX_CONFIG1_MP_MODE_QUAD:
		dptx->hwparams.multipixel = DPTX_MP_QUAD_PIXEL;
		break;
	}
}

/**
 * dptx_core_init() - Initializes the DP TX core
 * @dptx: The dptx struct
 *
 * Initialize the DP TX core and put it in a known state.
 */
int dptx_core_init(struct dptx *dptx)
{
	char str[15];
	u32 version;
	u32 hpd_ien;

	/* Reset the core */
	dptx_soft_reset_all(dptx);

	/* Enable MST */
	dptx_write_reg(dptx, dptx->regs[DPTX], CCTL, DPTX_CCTL_ENH_FRAME_EN
		    | (dptx->mst ? DPTX_CCTL_ENABLE_MST_MODE : 0));

	/* Check the core version */
	memset(str, 0, sizeof(str));
	version = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VERSION_NUMBER);
	str[0] = (version >> 24) & 0xff;
	str[1] = '.';
	str[2] = (version >> 16) & 0xff;
	str[3] = (version >> 8) & 0xff;
	str[4] = version & 0xff;

	version = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VERSION_TYPE);
	str[5] = '-';
	str[6] = (version >> 24) & 0xff;
	str[7] = (version >> 16) & 0xff;
	str[8] = (version >> 8) & 0xff;
	str[9] = version & 0xff;

	dptx_dbg(dptx, "Core version: %s\n", str);
	dptx->version = version;
	dptx_core_init_phy(dptx);

	/* Enable all HPD interrupts */
	hpd_ien = dptx_read_reg(dptx, dptx->regs[DPTX], HPD_INTERRUPT_ENABLE);
	hpd_ien |= (DPTX_HPD_IEN_IRQ_EN |
		    DPTX_HPD_IEN_HOT_PLUG_EN |
		    DPTX_HPD_IEN_HOT_UNPLUG_EN);
	dptx_write_reg(dptx, dptx->regs[DPTX], HPD_INTERRUPT_ENABLE, hpd_ien);

	/* Enable all top-level interrupts */
	dptx_global_intr_en(dptx);

	return 0;
}

/**
 * dptx_core_deinit() - Deinitialize the core
 * @dptx: The dptx struct
 *
 * Disable the core in preparation for module shutdown.
 */
int dptx_core_deinit(struct dptx *dptx)
{
	dptx_global_intr_dis(dptx);
	dptx_soft_reset_all(dptx);
	return 0;
}

/*
 * PHYIF core access functions
 */

unsigned int dptx_phy_get_lanes(struct dptx *dptx)
{
	u32 val;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;
	val = dptx_read_regfield(dptx, ctrl_fields->field_phy_lanes);

	return (1 << val);
}

void dptx_phy_set_lanes(struct dptx *dptx, unsigned int lanes)
{
	u32 val;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	dptx_dbg(dptx, "%s: lanes=%d\n", __func__, lanes);

	switch (lanes) {
	case 1:
		val = 0;
		break;
	case 2:
		val = 1;
		break;
	case 4:
		val = 2;
		break;
	default:
		WARN(1, "Invalid number of lanes %d\n", lanes);
		return;
	}

	dptx_write_regfield(dptx, ctrl_fields->field_phy_lanes, val);
}

void dptx_phy_set_lanes_powerdown_state(struct dptx *dptx, u8 state)
{
	u8 i, nr_lanes;
	u32 phy_pwrdown_reg;

	nr_lanes = dptx->link.lanes;
	phy_pwrdown_reg = ctrl_read(dptx, PHYIF_PWRDOWN_CTRL);
	dptx_dbg(dptx, "Lanes Powerdown State: 0x%08X", phy_pwrdown_reg);

	// Set state only for the lanes being used
	for (i = 0; i < nr_lanes; i++)
		phy_pwrdown_reg = set(phy_pwrdown_reg, DPTX_POWER_DOWN_CTRL_LANE_MASK(i), state);

	// Make sure other lanes are shutdown
	for (i = nr_lanes; i < DPTX_MAX_LINK_LANES; i++)
		phy_pwrdown_reg = set(phy_pwrdown_reg, DPTX_POWER_DOWN_CTRL_LANE_MASK(i), DPTX_PHY_POWER_DOWN);

	dptx_dbg(dptx, "Set Lanes Powerdown State: 0x%08X", phy_pwrdown_reg);
	ctrl_write(dptx, PHYIF_PWRDOWN_CTRL, phy_pwrdown_reg);
	phy_pwrdown_reg = ctrl_read(dptx, PHYIF_PWRDOWN_CTRL);
	dptx_dbg(dptx, "Checking Lanes Powerdown State: 0x%08X", phy_pwrdown_reg);
}

void dptx_phy_shutdown_unused_lanes(struct dptx *dptx)
{
	u8 i, nr_lanes;
	u32 phy_pwrdown_reg;

	nr_lanes = dptx->link.lanes;
	//phy_pwrdown_reg = ctrl_read(dptx, PHYIF_PWRDOWN_CTRL);
	phy_pwrdown_reg = dptx_read_reg(dptx, dptx->regs[DPTX], PHYIF_PWRDOWN_CTRL);

	for (i = nr_lanes; i < DPTX_MAX_LINK_LANES; i++)
		set(phy_pwrdown_reg, DPTX_POWER_DOWN_CTRL_LANE_MASK(i), DPTX_PHY_POWER_DOWN);

	dptx_dbg(dptx, "Shutdown Unused Lanes: 0x%08X", phy_pwrdown_reg);
	//ctrl_write(dptx, PHYIF_PWRDOWN_CTRL, phy_pwrdown_reg);
	dptx_write_reg(dptx, dptx->regs[DPTX], PHYIF_PWRDOWN_CTRL, phy_pwrdown_reg);
}


void dptx_phy_set_rate(struct dptx *dptx, unsigned int rate)
{
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;
	dptx_dbg(dptx, "%s: rate=%d\n", __func__, rate);

#ifdef DPTX_COMBO_PHY
	switch (rate) {
	case DPTX_PHYIF_CTRL_RATE_RBR:
	case DPTX_PHYIF_CTRL_RATE_HBR:
		/* Set 20-bit PHY width */
		dptx_write_regfield(dptx, ctrl_fields->field_phy_width, 0);
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR2:
	case DPTX_PHYIF_CTRL_RATE_HBR3:
		/* Set 40-bit PHY width */
		dptx_write_regfield(dptx, ctrl_fields->field_phy_width, 1);
		break;
	default:
		WARN(1, "Invalid PHY rate %d\n", rate);
		break;
	}
#endif

	dptx_write_regfield(dptx, ctrl_fields->field_phyrate, rate);
}

unsigned int dwc_phy_get_rate(struct dptx *dptx)
{
	u32 rate;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	rate = dptx_read_regfield(dptx, ctrl_fields->field_phyrate);

	return rate;
}

int dptx_phy_wait_busy(struct dptx *dptx, unsigned int lanes)
{
	unsigned int count;
	u32 phyifctrl;
	u32 mask = 0;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	switch (lanes) {
	case 4:
		mask |= DPTX_PHYIF_CTRL_BUSY(3);
		mask |= DPTX_PHYIF_CTRL_BUSY(2);
		fallthrough;
	case 2:
		mask |= DPTX_PHYIF_CTRL_BUSY(1);
		fallthrough;
	case 1:
		mask |= DPTX_PHYIF_CTRL_BUSY(0);
		break;
	case 0:
		/*
		 * DP branch device unplug
		 * No sinks were attached, no DP link was negotiated
		 */
		break;
	default:
		WARN(1, "Invalid number of lanes %d\n", lanes);
		break;
	}

	count = 0;

	while (1) {
		phyifctrl = dptx_read_reg(dptx, dptx->regs[DPTX], PHYIF_CTRL);
		dptx_info(dptx, "Wait busy: lanes %u, phyif_ctrl %X, mask %X ... value: %X\n",
				lanes, phyifctrl, mask, (phyifctrl & mask));

		if (!(phyifctrl & mask))
			break;

		count++;
		if (count > 20) { // was 50
			dptx_warn(dptx, "%s: PHY BUSY timed out\n", __func__);
			return -EBUSY;
		}

		msleep(20);
	}

	return 0;
}

void dptx_phy_set_pre_emphasis(struct dptx *dptx,
			       unsigned int lane,
			       unsigned int level)
{
	u32 phytxeq;

	dptx_dbg(dptx, "%s: lane=%d, level=0x%x\n", __func__, lane, level);

	if (WARN(lane > 3, "Invalid lane %d", lane))
		return;

	if (WARN(level > 3, "Invalid pre-emphasis level %d, using 3", level))
		level = 3;

	phytxeq = dptx_read_reg(dptx, dptx->regs[DPTX], PHY_TX_EQ);
	phytxeq &= ~DPTX_PHY_TX_EQ_PREEMP_MASK(lane);
	phytxeq |= (level << DPTX_PHY_TX_EQ_PREEMP_SHIFT(lane)) &
		DPTX_PHY_TX_EQ_PREEMP_MASK(lane);

	dptx_write_reg(dptx, dptx->regs[DPTX], PHY_TX_EQ, phytxeq);
}

void dptx_phy_set_vswing(struct dptx *dptx,
			 unsigned int lane,
			 unsigned int level)
{
	u32 phytxeq;

	dptx_dbg(dptx, "%s: lane=%d, level=0x%x\n", __func__, lane, level);

	if (WARN(lane > 3, "Invalid lane %d", lane))
		return;

	if (WARN(level > 3, "Invalid vswing level %d, using 3", level))
		level = 3;

	phytxeq = dptx_read_reg(dptx, dptx->regs[DPTX], PHY_TX_EQ);
	phytxeq &= ~DPTX_PHY_TX_EQ_VSWING_MASK(lane);
	phytxeq |= (level << DPTX_PHY_TX_EQ_VSWING_SHIFT(lane)) &
		DPTX_PHY_TX_EQ_VSWING_MASK(lane);

	dptx_write_reg(dptx, dptx->regs[DPTX], PHY_TX_EQ, phytxeq);
}

void dptx_phy_set_pattern(struct dptx *dptx,
			  unsigned int pattern)
{
	struct ctrl_regfields *ctrl_fields;
	u8 pattern_check;

	ctrl_fields = dptx->ctrl_fields;
	dptx_dbg(dptx, "%s: Setting PHY pattern=0x%x\n", __func__, pattern);

	dptx_write_regfield(dptx, ctrl_fields->field_tps_sel, pattern);
	pattern_check = dptx_read_regfield(dptx, ctrl_fields->field_tps_sel);
	dptx_dbg(dptx, "REG Pattern Defined: 0x%X", pattern);
}

void dptx_phy_enable_xmit(struct dptx *dptx, unsigned int lanes, bool enable)
{
	u32 phyifctrl;
	u32 mask = 0;
	/* PHY safety check, used lanes should never be greater than max */
	int max_lanes = DPTX_PIN_TO_NUM_LANES(dptx->hw_config.pin_type);
	int used_lanes = lanes > max_lanes ? max_lanes : lanes;

	dptx_dbg(dptx, "%s: lanes=%d, enable=%d\n", __func__, lanes, enable);

	/* Enable Transmitter on DPTX */
	phyifctrl = ctrl_read(dptx, PHYIF_CTRL);

	switch (lanes) {
	case 4:
		mask |= DPTX_PHYIF_CTRL_XMIT_EN(3);
		mask |= DPTX_PHYIF_CTRL_XMIT_EN(2);
		fallthrough;
	case 2:
		mask |= DPTX_PHYIF_CTRL_XMIT_EN(1);
		fallthrough;
	case 1:
		mask |= DPTX_PHYIF_CTRL_XMIT_EN(0);
		break;
	default:
		WARN(1, "Invalid number of lanes %d\n", lanes);
		break;
	}

	if (enable)
		phyifctrl |= mask;
	else
		phyifctrl &= ~mask;

	ctrl_write(dptx, PHYIF_CTRL, phyifctrl);

	/* Enable Transmitter on PHY */
	if (enable)
		google_dpphy_pipe_lane_disable_tx(dptx->dp_phy, max_lanes, used_lanes);
	else
		google_dpphy_pipe_lane_disable_tx(dptx->dp_phy, max_lanes, max_lanes);
}

int dptx_phy_rate_to_bw(unsigned int rate)
{
	switch (rate) {
	case DPTX_PHYIF_CTRL_RATE_RBR:
		return DP_LINK_BW_1_62;
	case DPTX_PHYIF_CTRL_RATE_HBR:
		return DP_LINK_BW_2_7;
	case DPTX_PHYIF_CTRL_RATE_HBR2:
		return DP_LINK_BW_5_4;
	case DPTX_PHYIF_CTRL_RATE_HBR3:
		return DP_LINK_BW_8_1;
	default:
		WARN(1, "Invalid rate 0x%x\n", rate);
		return -EINVAL;
	}
}

int dptx_bw_to_phy_rate(unsigned int bw)
{
	switch (bw) {
	case DP_LINK_BW_1_62:
		return DPTX_PHYIF_CTRL_RATE_RBR;
	case DP_LINK_BW_2_7:
		return DPTX_PHYIF_CTRL_RATE_HBR;
	case DP_LINK_BW_5_4:
		return DPTX_PHYIF_CTRL_RATE_HBR2;
	case DP_LINK_BW_8_1:
	case DP_LINK_BW_10:
	case DP_LINK_BW_13_5:
	case DP_LINK_BW_20:
		/* use HBR3 with UHBR sinks */
		return DPTX_PHYIF_CTRL_RATE_HBR3;
	default:
		WARN(1, "Invalid bw 0x%x\n", bw);
		return -EINVAL;
	}
}
