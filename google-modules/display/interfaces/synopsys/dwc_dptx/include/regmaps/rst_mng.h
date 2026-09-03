/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "../rst_mng.h"

int rstmng_regmap_fields_init(struct dptx *dptx);

struct rstmng_regfield_variant {
	struct reg_field cfg_global_rst;
	struct reg_field cfg_ctrl_rst;
	struct reg_field cfg_phy_if_rst;
	struct reg_field cfg_videobridge_rst;
	struct reg_field cfg_audiobridge_rst;
	struct reg_field cfg_audiogen_aud_rst;
	struct reg_field cfg_audiogen_ref_rst;
	struct reg_field cfg_i2s_ctrl_rst;
	struct reg_field cfg_vidclk_gen_rst;
	struct reg_field cfg_audclk_gen_rst;
	struct reg_field cfg_hdcp2Xext_rst;
	struct reg_field cfg_jtaglab_rst;
};

static const struct rstmng_regfield_variant rstmng_regfield_cfg = {
	.cfg_global_rst = REG_FIELD(RM_SYSTEM_RST_N, 0, 0),
	.cfg_ctrl_rst = REG_FIELD(RM_SYSTEM_RST_N, 1, 1),
	.cfg_phy_if_rst = REG_FIELD(RM_SYSTEM_RST_N, 2, 2),
	.cfg_videobridge_rst = REG_FIELD(RM_SYSTEM_RST_N, 4, 4),
	.cfg_audiobridge_rst = REG_FIELD(RM_SYSTEM_RST_N, 8, 8),
	.cfg_audiogen_aud_rst = REG_FIELD(RM_SYSTEM_RST_N, 9, 9),
	.cfg_audiogen_ref_rst = REG_FIELD(RM_SYSTEM_RST_N, 10, 10),
	.cfg_i2s_ctrl_rst = REG_FIELD(RM_SYSTEM_RST_N, 11, 11),
	.cfg_vidclk_gen_rst = REG_FIELD(RM_MMCM_RST_N, 0, 0),
	.cfg_audclk_gen_rst = REG_FIELD(RM_MMCM_RST_N, 1, 1),
	.cfg_hdcp2Xext_rst = REG_FIELD(RM_HDCP22EXT_RST_N, 0, 0),
	.cfg_jtaglab_rst = REG_FIELD(RM_JTAGCTRL_RST_N, 0, 0),
};

struct rstmng_regfields {
	struct regmap_field *field_global_rst;
	struct regmap_field *field_ctrl_rst;
	struct regmap_field *field_phy_if_rst;
	struct regmap_field *field_videobridge_rst;
	struct regmap_field *field_audiobridge_rst;
	struct regmap_field *field_audiogen_aud_rst;
	struct regmap_field *field_audiogen_ref_rst;
	struct regmap_field *field_i2s_ctrl_rst;
	struct regmap_field *field_vidclk_gen_rst;
	struct regmap_field *field_audclk_gen_rst;
	struct regmap_field *field_hdcp2Xext_rst;
	struct regmap_field *field_jtaglab_rst;
};

static const struct regmap_config dwc_dptx_rstmng_regmap_cfg = {
	.name = "dwc_dptx_rst_mng",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};
