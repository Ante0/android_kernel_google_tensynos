// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "regmaps/clock_mng.h"

/* Variant Reg Fields Init macros */
#define INIT_CLKMNG_FIELD(f) INIT_CLKMNG_FIELD_CFG(field_##f, cfg_##f)
#define INIT_CLKMNG_FIELD_CFG(f, conf)	({\
	dptx->clkmng_fields->f = devm_regmap_field_alloc(dptx->dev, dptx->regs[CLKMGR],\
			variant->conf);\
	if (IS_ERR(dptx->clkmng_fields->f))\
		dev_warn(dptx->dev, "Ignoring regmap field"#f "\n");\
	})

int clkmng_regmap_fields_init(struct dptx *dptx)
{
	const struct clkmng_regfield_variant *variant = &clkmng_regfield_cfg;

	INIT_CLKMNG_FIELD(video_clock_state);
	INIT_CLKMNG_FIELD(video_sen);
	INIT_CLKMNG_FIELD(video_srdy);
	INIT_CLKMNG_FIELD(video_locked);
	INIT_CLKMNG_FIELD(audio_clock_state);
	INIT_CLKMNG_FIELD(audio_sen);
	INIT_CLKMNG_FIELD(audio_srdy);
	INIT_CLKMNG_FIELD(audio_locked);

	return 0;
}
