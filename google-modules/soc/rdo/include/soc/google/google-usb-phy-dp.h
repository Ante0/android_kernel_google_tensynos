/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC
 * Google's SoC-specific Glue Driver for the DWC DPTX
 */

#include <linux/phy/phy.h>

#define DP_PIPE_LANE_0				0
#define DP_PIPE_LANE_1				1

#define DP_PIPE_LANEx_ENABLE			0
#define DP_PIPE_LANEx_DISABLE			1

/* HSIO_N_DP_CONFIG block */
#if IS_ENABLED(CONFIG_SOC_LGA)
#define DP_CONFIG_DP_ENCRYPTION_MODE_MASK		BIT(0)
#define DP_CONFIG_DP_ENCRYPTION_MODE_VAL(x)		(x << 0)
#define DP_CONFIG_DSC_ENABLE_0_MASK			BIT(1)
#define DP_CONFIG_DSC_ENABLE_0_VAL(x)			(x << 1)
#define DP_CONFIG_DSC_ENABLE_1_MASK			BIT(2)
#define DP_CONFIG_DSC_ENABLE_1_VAL(x)			(x << 2)
#define DP_CONFIG_REG_DP_HPD_MASK			BIT(4)
#define DP_CONFIG_REG_DP_HPD_VAL(x)			(x << 4)
#else
#define DP_CONFIG_REG_DP_HPD_MASK			BIT(0)
#define DP_CONFIG_REG_DP_HPD_VAL(x)			(x << 0)
#define DP_CONFIG_DP_ENCRYPTION_MODE_MASK		BIT(1)
#define DP_CONFIG_DP_ENCRYPTION_MODE_VAL(x)		(x << 1)
#endif

/* Type-C Specific */
#define DPTX_PIN_TO_NUM_LANES(_assign_)		((_assign_) == PIN_TYPE_D ? 2 : 4)

/*
 * DFP_D (USB Type-C)
 *
 * Pin Assignments A, B, F are not supported after Alt Mode Spec v1.0b
 */
enum pin_assignment {
	PIN_TYPE_A = 0,
	PIN_TYPE_B,
	PIN_TYPE_C,
	PIN_TYPE_D,
	PIN_TYPE_E,
	PIN_TYPE_F,
};

enum plug_orientation {
	PLUG_NONE = 0,		// TYPEC_ORIENTATION_NONE
	PLUG_NORMAL,		// TYPEC_ORIENTATION_NORMAL
	PLUG_FLIPPED,		// TYPEC_ORIENTATION_REVERSE
};

enum dp_pipe_rate {
	DP_PIPE_RATE_RBR = 0,
	DP_PIPE_RATE_HBR,
	DP_PIPE_RATE_HBR2,
	DP_PIPE_RATE_HBR3,
};

enum dp_tx_eq_level {
	DP_TX_EQ_LEVEL_0 = 0,
	DP_TX_EQ_LEVEL_1,
	DP_TX_EQ_LEVEL_2,
	DP_TX_EQ_LEVEL_3,
};

/*
 * google_dpphy_set_maxpclk - set pclk from phy to DP controller
 *
 * set maxpclkreq on DP PIPE lanes 1 and 0. Setting dp_pipe_lane0_maxpclkreq
 * won't have any functional impact in USB+DP mode or when DP isn't using
 * the lane, so we can set both lanes here.
 *
 */
extern int google_dpphy_set_maxpclk(struct phy *phy, u32 lane0, u32 lane1);

/*
 * google_dpphy_set_pipe_pclk_on - set pclk availability.
 *	0 - pclk is off
 *	1 - pclk is on
 */
extern int google_dpphy_set_pipe_pclk_on(struct phy *phy, bool on);

/*
 * google_dpphy_config_write - write to DP_CONFIG_REG in DP_TOP_CSR
 */
extern int google_dpphy_config_write(struct phy *phy, u32 mask, u32 val);

/*
 * google_dpphy_init_upcs_pipe_config - set upcs_pipe_config in PHY_power_config_reg1
 */
extern int google_dpphy_init_upcs_pipe_config(struct phy *phy);

/*
 * google_dpphy_aux_powerup - configure AUX ctrl and AUX powerup for DP Usage
 */
extern int google_dpphy_aux_powerup(struct phy *phy);

/*
 * google_dpphy_pipe_lane_disable_tx - disable allocated but unused PHY tx lanes
 *
 * max_lanes and used_lanes are used to prevent disabling lanes allocated to USB3.
 */
extern int google_dpphy_pipe_lane_disable_tx(struct phy *phy, int max_lanes, int used_lanes);

/*
 * google_dpphy_pipe_tx_eq_set - set DPTX transmitter equalization settings for an individual lane
 * vs values are expected to be of enum dp_tx_eq_level -> TxMargin
 * pe values are expected to be of enum dp_tx_eq_level -> TxDeemph
 * Any changes made need to be undone during the HPD unplug process
 *
 */
extern int google_dpphy_pipe_tx_eq_set(struct phy *phy, int lane, enum dp_tx_eq_level vs,
				       enum dp_tx_eq_level pe);

/*
 * google_dpphy_eq_tune_ovrd_enable - set tuning ovrd enable bit. The is required for
 * google_dpphy_eq_tune_ovrd_apply values to propagate to the phy properly.
 *	max_lanes is expected to be 2 or 4
 */
extern int google_dpphy_eq_tune_ovrd_enable(struct phy *phy, int max_lanes, bool enable);

/*
 * google_dpphy_eq_tune_ovrd_apply - apply tuning setting for a specific rate and vs/pe level.
 *	lane is expected to be [0,3]
 *	rate is expected to be of enum dp_pipe_rate
 *	vs is expected to be of enum dp_tx_eq_level
 *	pe is expected to be of enum dp_tx_eq_level
 *	vs + pe < 4 for a valid vs/pe level
 */
extern int google_dpphy_eq_tune_ovrd_apply(struct phy *phy, int lane, enum dp_pipe_rate rate,
					   enum dp_tx_eq_level vs, enum dp_tx_eq_level pe);

/*
 * google_dpphy_eq_tune_asic_read - read live tuning values on a given lane.
 *	lane is expected to be [0,3]
 *	orientation is expected to be PLUG_NORMAL or PLUG_FLIPPED
 */
extern int google_dpphy_eq_tune_asic_read(struct phy *phy, int lane,
					  enum plug_orientation orientation);

