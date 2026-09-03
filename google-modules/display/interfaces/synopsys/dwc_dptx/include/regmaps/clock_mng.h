/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "../clock_mng.h"

int clkmng_regmap_fields_init(struct dptx *dptx);

struct clkmng_regfield_variant {
	struct reg_field cfg_video_clock_state;
	struct reg_field cfg_video_sen;
	struct reg_field cfg_video_srdy;
	struct reg_field cfg_video_locked;
	struct reg_field cfg_audio_clock_state;
	struct reg_field cfg_audio_sen;
	struct reg_field cfg_audio_srdy;
	struct reg_field cfg_audio_locked;
};

static const struct clkmng_regfield_variant clkmng_regfield_cfg = {
	.cfg_video_clock_state = REG_FIELD(MMCM_VIDEO_OFFSET + MMCM_SADDR, 0, 7),
	.cfg_video_sen = REG_FIELD(MMCM_VIDEO_OFFSET + MMCM_SEN, 0, 0),
	.cfg_video_srdy = REG_FIELD(MMCM_VIDEO_OFFSET + MMCM_SRDY, 0, 0),
	.cfg_video_locked = REG_FIELD(MMCM_VIDEO_OFFSET + MMCM_LOCKED, 0, 0),
	.cfg_audio_clock_state = REG_FIELD(MMCM_AUDIO_OFFSET + MMCM_SADDR, 0, 7),
	.cfg_audio_sen = REG_FIELD(MMCM_AUDIO_OFFSET + MMCM_SEN, 0, 0),
	.cfg_audio_srdy = REG_FIELD(MMCM_AUDIO_OFFSET + MMCM_SRDY, 0, 0),
	.cfg_audio_locked = REG_FIELD(MMCM_AUDIO_OFFSET + MMCM_LOCKED, 0, 0),
};

struct clkmng_regfields {
	struct regmap_field *field_video_clock_state;
	struct regmap_field *field_video_sen;
	struct regmap_field *field_video_srdy;
	struct regmap_field *field_video_locked;
	struct regmap_field *field_audio_clock_state;
	struct regmap_field *field_audio_sen;
	struct regmap_field *field_audio_srdy;
	struct regmap_field *field_audio_locked;
};

static const struct regmap_config dwc_dptx_clkmng_regmap_cfg = {
	.name = "dwc_dptx_clk_mng",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};
