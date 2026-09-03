/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "../audio_bridge.h"

int ag_regmap_fields_init(struct dptx *dptx);

struct ag_regfield_variant {
	//Stream 0
	struct reg_field cfg_ab0_src_sel;
	struct reg_field cfg_agen0_en;
	struct reg_field cfg_agen0_layout;
	struct reg_field cfg_agen0_sample_flat;
	struct reg_field cfg_agen0_speaker_aloc;
	struct reg_field cfg_agen0_freq_fs;
	//Stream 1
	struct reg_field cfg_ab1_src_sel;
	struct reg_field cfg_agen1_en;
	struct reg_field cfg_agen1_layout;
	struct reg_field cfg_agen1_sample_flat;
	struct reg_field cfg_agen1_speaker_aloc;
	struct reg_field cfg_agen1_freq_fs;
	//Stream 2
	struct reg_field cfg_ab2_src_sel;
	struct reg_field cfg_agen2_en;
	struct reg_field cfg_agen2_layout;
	struct reg_field cfg_agen2_sample_flat;
	struct reg_field cfg_agen2_speaker_aloc;
	struct reg_field cfg_agen2_freq_fs;
	//Stream 3
	struct reg_field cfg_ab3_src_sel;
	struct reg_field cfg_agen3_en;
	struct reg_field cfg_agen3_layout;
	struct reg_field cfg_agen3_sample_flat;
	struct reg_field cfg_agen3_speaker_aloc;
	struct reg_field cfg_agen3_freq_fs;
};

static const struct ag_regfield_variant ag_regfield_cfg = {
	.cfg_ab0_src_sel = REG_FIELD(STREAM_OFFSET(0) + AB_SRC_SEL, 0, 1),
	.cfg_agen0_en = REG_FIELD(STREAM_OFFSET(0) + EXT_AG_CONFIG0, 0, 0),
	.cfg_agen0_layout = REG_FIELD(STREAM_OFFSET(0) + EXT_AG_CONFIG0, 4, 4),
	.cfg_agen0_sample_flat = REG_FIELD(STREAM_OFFSET(0) + EXT_AG_CONFIG0, 5, 5),
	.cfg_agen0_speaker_aloc = REG_FIELD(STREAM_OFFSET(0) + EXT_AG_CONFIG0, 8, 15),
	.cfg_agen0_freq_fs = REG_FIELD(STREAM_OFFSET(0) + EXT_AG_CONFIG1, 0, 4),
	.cfg_ab1_src_sel = REG_FIELD(STREAM_OFFSET(1) + AB_SRC_SEL, 0, 1),
	.cfg_agen1_en = REG_FIELD(STREAM_OFFSET(1) + EXT_AG_CONFIG0, 0, 0),
	.cfg_agen1_layout = REG_FIELD(STREAM_OFFSET(1) + EXT_AG_CONFIG0, 4, 4),
	.cfg_agen1_sample_flat = REG_FIELD(STREAM_OFFSET(1) + EXT_AG_CONFIG0, 5, 5),
	.cfg_agen1_speaker_aloc = REG_FIELD(STREAM_OFFSET(1) + EXT_AG_CONFIG0, 8, 15),
	.cfg_agen1_freq_fs = REG_FIELD(STREAM_OFFSET(1) + EXT_AG_CONFIG1, 0, 4),
	.cfg_ab2_src_sel = REG_FIELD(STREAM_OFFSET(2) + AB_SRC_SEL, 0, 1),
	.cfg_agen2_en = REG_FIELD(STREAM_OFFSET(2) + EXT_AG_CONFIG0, 0, 0),
	.cfg_agen2_layout = REG_FIELD(STREAM_OFFSET(2) + EXT_AG_CONFIG0, 4, 4),
	.cfg_agen2_sample_flat = REG_FIELD(STREAM_OFFSET(2) + EXT_AG_CONFIG0, 5, 5),
	.cfg_agen2_speaker_aloc = REG_FIELD(STREAM_OFFSET(2) + EXT_AG_CONFIG0, 8, 15),
	.cfg_agen2_freq_fs = REG_FIELD(STREAM_OFFSET(2) + EXT_AG_CONFIG1, 0, 4),
	.cfg_ab3_src_sel = REG_FIELD(STREAM_OFFSET(3) + AB_SRC_SEL, 0, 1),
	.cfg_agen3_en = REG_FIELD(STREAM_OFFSET(3) + EXT_AG_CONFIG0, 0, 0),
	.cfg_agen3_layout = REG_FIELD(STREAM_OFFSET(3) + EXT_AG_CONFIG0, 4, 4),
	.cfg_agen3_sample_flat = REG_FIELD(STREAM_OFFSET(3) + EXT_AG_CONFIG0, 5, 5),
	.cfg_agen3_speaker_aloc = REG_FIELD(STREAM_OFFSET(3) + EXT_AG_CONFIG0, 8, 15),
	.cfg_agen3_freq_fs = REG_FIELD(STREAM_OFFSET(3) + EXT_AG_CONFIG1, 0, 4),
};

struct ag_regfields {
	struct regmap_field *field_ab0_src_sel;
	struct regmap_field *field_agen0_en;
	struct regmap_field *field_agen0_layout;
	struct regmap_field *field_agen0_sample_flat;
	struct regmap_field *field_agen0_speaker_aloc;
	struct regmap_field *field_agen0_freq_fs;
	struct regmap_field *field_ab1_src_sel;
	struct regmap_field *field_agen1_en;
	struct regmap_field *field_agen1_layout;
	struct regmap_field *field_agen1_sample_flat;
	struct regmap_field *field_agen1_speaker_aloc;
	struct regmap_field *field_agen1_freq_fs;
	struct regmap_field *field_ab2_src_sel;
	struct regmap_field *field_agen2_en;
	struct regmap_field *field_agen2_layout;
	struct regmap_field *field_agen2_sample_flat;
	struct regmap_field *field_agen2_speaker_aloc;
	struct regmap_field *field_agen2_freq_fs;
	struct regmap_field *field_ab3_src_sel;
	struct regmap_field *field_agen3_en;
	struct regmap_field *field_agen3_layout;
	struct regmap_field *field_agen3_sample_flat;
	struct regmap_field *field_agen3_speaker_aloc;
	struct regmap_field *field_agen3_freq_fs;
};

static const struct regmap_config dwc_dptx_ag_regmap_cfg = {
	.name = "dwc_dptx_audio_gen",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};
