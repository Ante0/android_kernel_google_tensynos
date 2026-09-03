// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "regmaps/rst_mng.h"

/* Variant Reg Fields Init macros */
#define INIT_RSTMNG_FIELD(f) INIT_RSTMNG_FIELD_CFG(field_##f, cfg_##f)
#define INIT_RSTMNG_FIELD_CFG(f, conf)	({\
	dptx->rstmng_fields->f = devm_regmap_field_alloc(dptx->dev, dptx->regs[RSTMGR],\
			variant->conf);\
	if (IS_ERR(dptx->rstmng_fields->f))\
		dev_warn(dptx->dev, "Ignoring regmap field"#f "\n");\
	})

int rstmng_regmap_fields_init(struct dptx *dptx)
{
	const struct rstmng_regfield_variant *variant = &rstmng_regfield_cfg;

	INIT_RSTMNG_FIELD(global_rst);
	INIT_RSTMNG_FIELD(ctrl_rst);
	INIT_RSTMNG_FIELD(phy_if_rst);
	INIT_RSTMNG_FIELD(videobridge_rst);
	INIT_RSTMNG_FIELD(audiobridge_rst);
	INIT_RSTMNG_FIELD(audiogen_aud_rst);
	INIT_RSTMNG_FIELD(audiogen_ref_rst);
	INIT_RSTMNG_FIELD(i2s_ctrl_rst);
	INIT_RSTMNG_FIELD(vidclk_gen_rst);
	INIT_RSTMNG_FIELD(audclk_gen_rst);
	INIT_RSTMNG_FIELD(hdcp2Xext_rst);
	INIT_RSTMNG_FIELD(jtaglab_rst);

	return 0;
}
