// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "regmaps/video_bridge.h"

/* Variant Reg Fields Init macros */
#define INIT_VG_FIELD(f) INIT_VG_FIELD_CFG(field_##f, cfg_##f)
#define INIT_VG_FIELD_CFG(f, conf)	({\
	dptx->vg_fields->f = devm_regmap_field_alloc(dptx->dev, dptx->regs[VG],\
			variant->conf);\
	if (IS_ERR(dptx->vg_fields->f))\
		dev_warn(dptx->dev, "Ignoring regmap field"#f "\n");\
	})

int vg_regmap_fields_init(struct dptx *dptx)
{
	const struct vg_regfield_variant *variant = &vg_regfield_cfg;

	INIT_VG_FIELD(vpg0_en);
	INIT_VG_FIELD(vpg0_colorimetry);
	INIT_VG_FIELD(vpg0_colordepth);
	INIT_VG_FIELD(vpg0_patt_mode);
	INIT_VG_FIELD(vpg0_hactive);
	INIT_VG_FIELD(vpg0_hblank);
	INIT_VG_FIELD(vpg0_vactive);
	INIT_VG_FIELD(vpg0_vblank);

	return 0;
}
