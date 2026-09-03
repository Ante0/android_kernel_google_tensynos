// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "regmaps/audio_bridge.h"

/* Variant Reg Fields Init macros */
#define INIT_AG_FIELD(f) INIT_AG_FIELD_CFG(field_##f, cfg_##f)
#define INIT_AG_FIELD_CFG(f, conf)	({\
	dptx->ag_fields->f = devm_regmap_field_alloc(dptx->dev, dptx->regs[AG],\
			variant->conf);\
	if (IS_ERR(dptx->ag_fields->f))\
		dev_warn(dptx->dev, "Ignoring regmap field"#f "\n");\
	})

int ag_regmap_fields_init(struct dptx *dptx)
{
	const struct ag_regfield_variant *variant = &ag_regfield_cfg;

	INIT_AG_FIELD(ab0_src_sel);
	INIT_AG_FIELD(agen0_en);
	INIT_AG_FIELD(agen0_layout);
	INIT_AG_FIELD(agen0_sample_flat);
	INIT_AG_FIELD(agen0_speaker_aloc);
	INIT_AG_FIELD(agen0_freq_fs);
	INIT_AG_FIELD(ab1_src_sel);
	INIT_AG_FIELD(agen1_en);
	INIT_AG_FIELD(agen1_layout);
	INIT_AG_FIELD(agen1_sample_flat);
	INIT_AG_FIELD(agen1_speaker_aloc);
	INIT_AG_FIELD(agen1_freq_fs);
	INIT_AG_FIELD(ab2_src_sel);
	INIT_AG_FIELD(agen2_en);
	INIT_AG_FIELD(agen2_layout);
	INIT_AG_FIELD(agen2_sample_flat);
	INIT_AG_FIELD(agen2_speaker_aloc);
	INIT_AG_FIELD(agen2_freq_fs);
	INIT_AG_FIELD(ab3_src_sel);
	INIT_AG_FIELD(agen3_en);
	INIT_AG_FIELD(agen3_layout);
	INIT_AG_FIELD(agen3_sample_flat);
	INIT_AG_FIELD(agen3_speaker_aloc);
	INIT_AG_FIELD(agen3_freq_fs);

	return 0;
}
