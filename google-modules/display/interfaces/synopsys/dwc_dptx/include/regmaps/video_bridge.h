/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "../video_bridge.h"

int vg_regmap_fields_init(struct dptx *dptx);

struct vg_regfield_variant {
	struct reg_field cfg_vpg0_en;
	struct reg_field cfg_vpg0_colorimetry;
	struct reg_field cfg_vpg0_colordepth;
	struct reg_field cfg_vpg0_patt_mode;
	struct reg_field cfg_vpg0_hactive;
	struct reg_field cfg_vpg0_hblank;
	struct reg_field cfg_vpg0_vactive;
	struct reg_field cfg_vpg0_vblank;
};

static const struct vg_regfield_variant vg_regfield_cfg = {
	.cfg_vpg0_en = REG_FIELD(STREAM_OFFSET(0) + VPG_CONF0, 0, 0),
	.cfg_vpg0_colorimetry = REG_FIELD(STREAM_OFFSET(0) + VPG_CONF0, 12, 14),
	.cfg_vpg0_colordepth = REG_FIELD(STREAM_OFFSET(0) + VPG_CONF0, 20, 23),
	.cfg_vpg0_patt_mode = REG_FIELD(STREAM_OFFSET(0) + VPG_CONF1, 16, 17),
	.cfg_vpg0_hactive = REG_FIELD(STREAM_OFFSET(0) + VPG_HAHB_CONFIG, 0, 13),
	.cfg_vpg0_hblank = REG_FIELD(STREAM_OFFSET(0) + VPG_HAHB_CONFIG, 16, 28),
	.cfg_vpg0_vactive = REG_FIELD(STREAM_OFFSET(0) + VPG_VAVB_CONFIG, 0, 12),
	.cfg_vpg0_vblank = REG_FIELD(STREAM_OFFSET(0) + VPG_VAVB_CONFIG, 16, 25),
};

struct vg_regfields {
	struct regmap_field *field_vpg0_en;
	struct regmap_field *field_vpg0_colorimetry;
	struct regmap_field *field_vpg0_colordepth;
	struct regmap_field *field_vpg0_patt_mode;
	struct regmap_field *field_vpg0_hactive;
	struct regmap_field *field_vpg0_hblank;
	struct regmap_field *field_vpg0_vactive;
	struct regmap_field *field_vpg0_vblank;
};

static const struct regmap_config dwc_dptx_vg_regmap_cfg = {
	.name = "dwc_dptx_video_gen",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};
