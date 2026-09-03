// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2026 Google LLC
 */

#include <linux/regmap.h>
#include "dw-mipi-dsi2h-regmap-limits.h"

#define regmap_range_single(addr) regmap_reg_range(addr, addr)
#define regmap_range_sized(addr, size) regmap_reg_range((addr), (addr) + (size) - 1)

static const struct regmap_range dw_dsi2h_dump_reg_allowed[] = {
	regmap_range_sized(0x0, 0x64),   /* CORE_ID to TO_BTA_CFG */
	regmap_range_sized(0x100, 0x4c), /* PHY_MODE_CFG to PHY_LANES_CFG */
	regmap_range_sized(0x1c0, 0x10), /* PRI_TX_CMD to PRI_ULPS_CTRL */
	regmap_range_sized(0x200, 0x1c), /* DSI_GENERAL_CFG to DSI_DS_TX_CFG */
	regmap_range_sized(0x2c0, 0x14), /* CRI_TX_HDR to CRI_TX_CTRL */
	regmap_range_sized(0x300, 0x4c), /* IPI_COLOR_MAN_CFG to IPI_HIBERNATE_CFG */
	regmap_range_single(0x400),      /* INT_ST_MAIN */
	regmap_range_sized(0x420, 0x18), /* INT_ST_PHY to INT_ST_CRI */
	regmap_range_sized(0x460, 0x18), /* INT_MASK_PHY to INT_MASK_CRI */
};

static const struct regmap_range dw_dsi2h_dump_reg_disallowed[] = {
	regmap_range_single(0x8),        /* Unused */
	regmap_range_single(0x14),       /* Unused */
	regmap_range_single(0x18),       /* MODE_CTRL */
	regmap_range_single(0x30),       /* OBS_FSM_CTRL */
	regmap_range_single(0x3c),       /* OBS_FIFO_CTRL */
	regmap_range_single(0x40),       /* Unused */
	regmap_range_single(0x44),       /* Unused */
	regmap_range_sized(0x64, 0x9c),  /* Unused */
	regmap_range_single(0x110),      /* PHY_LP2HS_AUTO */
	regmap_range_sized(0x14c, 0x74), /* Unused */
	regmap_range_single(0x1c0),      /* PRI_TX_CMD */
	regmap_range_sized(0x1d0, 0x30), /* Unused */
	regmap_range_sized(0x21c, 0xa4), /* Unused */
	regmap_range_single(0x2c0),      /* CRI_TX_HDR */
	regmap_range_single(0x2c4),      /* CRI_TX_PLD */
	regmap_range_sized(0x2d4, 0x2c), /* Unused */
	regmap_range_sized(0x34c, 0xb4), /* Unused */
	regmap_range_sized(0x404, 0x1c), /* Unused */
	regmap_range_single(0x4a0),      /* INT_FORCE_PHY */
	regmap_range_single(0x4a4),      /* INT_FORCE_TO */
	regmap_range_single(0x4a8),      /* INT_FORCE_ACK */
	regmap_range_single(0x4ac),      /* INT_FORCE_IPI */
	regmap_range_single(0x4b0),      /* INT_FORCE_PRI */
	regmap_range_single(0x4b4),      /* INT_FORCE_CRI */
	regmap_range_sized(0x550, 0x2c), /* Unused */
};

const struct regmap_access_table dw_dsi2h_dump_reg_table = {
	.yes_ranges = dw_dsi2h_dump_reg_allowed,
	.n_yes_ranges = ARRAY_SIZE(dw_dsi2h_dump_reg_allowed),
	.no_ranges = dw_dsi2h_dump_reg_disallowed,
	.n_no_ranges = ARRAY_SIZE(dw_dsi2h_dump_reg_disallowed),
};
