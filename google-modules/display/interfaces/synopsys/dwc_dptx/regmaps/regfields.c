// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "dptx.h"
//#include "clock_mng.h"
#include "regmaps/regfields.h"
#include "regmaps/ctrl_fields.h"
#include "regmaps/phyif_fields.h"
#include "regmaps/audio_bridge.h"
#include "regmaps/clock_mng.h"
#include "regmaps/rst_mng.h"
#include "regmaps/video_bridge.h"

int init_regfields(struct dptx *dptx)
{
	int retval;

	//DPTX Controller Regfields
	dptx->ctrl_fields = devm_kzalloc(dptx->dev, sizeof(*dptx->ctrl_fields), GFP_KERNEL);
	if (!dptx->ctrl_fields)
		return -ENOMEM;

	dptx->regs[DPTX] = devm_regmap_init_mmio(dptx->dev, dptx->base[DPTX], &dwc_dptx_ctrl_regmap_cfg);
	if (IS_ERR(dptx->regs[DPTX])) {
		dev_err(dptx->dev, "Failed to create Controller Regmap\n");
		return PTR_ERR(dptx->regs[DPTX]);
	}
	retval = ctrl_regmap_fields_init(dptx);
	if (retval) {
		dev_err(dptx->dev, "Failed to init register layout map\n");
		return retval;
	}

	//PHYIF Registers
	dptx->regs[PHYIF] = devm_regmap_init_mmio(dptx->dev, dptx->base[PHYIF], &dwc_dptx_phyif_regmap_cfg);
	if (IS_ERR(dptx->regs[PHYIF])) {
		dev_err(dptx->dev, "Failed to create PHYIF Regmap\n");
		return PTR_ERR(dptx->regs[PHYIF]);
	}

	//Clock Manager Regfields
	dptx->clkmng_fields = devm_kzalloc(dptx->dev, sizeof(*dptx->clkmng_fields), GFP_KERNEL);
	if (!dptx->clkmng_fields)
		return -ENOMEM;

	dptx->regs[CLKMGR] = devm_regmap_init_mmio(dptx->dev, dptx->base[CLKMGR], &dwc_dptx_clkmng_regmap_cfg);
	if (IS_ERR(dptx->regs[CLKMGR])) {
		dev_err(dptx->dev, "Failed to create Clock Manager Regmap\n");
		return PTR_ERR(dptx->regs[CLKMGR]);
	}
	retval = clkmng_regmap_fields_init(dptx);
	if (retval) {
		dev_err(dptx->dev, "Failed to init register layout map\n");
		return retval;
	}

	//Reset Manager Regfields
	dptx->rstmng_fields = devm_kzalloc(dptx->dev, sizeof(*dptx->rstmng_fields), GFP_KERNEL);
	if (!dptx->rstmng_fields)
		return -ENOMEM;

	dptx->regs[RSTMGR] = devm_regmap_init_mmio(dptx->dev, dptx->base[RSTMGR], &dwc_dptx_rstmng_regmap_cfg);
	if (IS_ERR(dptx->regs[RSTMGR])) {
		dev_err(dptx->dev, "Failed to create Reset Manager Regmap\n");
		return PTR_ERR(dptx->regs[RSTMGR]);
	}
	retval = rstmng_regmap_fields_init(dptx);
	if (retval) {
		dev_err(dptx->dev, "Failed to init register layout map\n");
		return retval;
	}

	//Audio Generator Regfields
	dptx->ag_fields = devm_kzalloc(dptx->dev, sizeof(*dptx->ag_fields), GFP_KERNEL);
	if (!dptx->ag_fields)
		return -ENOMEM;

	dptx->regs[AG] = devm_regmap_init_mmio(dptx->dev, dptx->base[AG], &dwc_dptx_ag_regmap_cfg);
	if (IS_ERR(dptx->regs[AG])) {
		dev_err(dptx->dev, "Failed to create Audio Generator Regmap\n");
		return PTR_ERR(dptx->regs[AG]);
	}
	retval = ag_regmap_fields_init(dptx);
	if (retval) {
		dev_err(dptx->dev, "Failed to init register layout map\n");
		return retval;
	}

	//Video Generator Regfields
	dptx->vg_fields = devm_kzalloc(dptx->dev, sizeof(*dptx->vg_fields), GFP_KERNEL);
	if (!dptx->vg_fields)
		return -ENOMEM;

	dptx->regs[VG] = devm_regmap_init_mmio(dptx->dev, dptx->base[VG], &dwc_dptx_vg_regmap_cfg);
	if (IS_ERR(dptx->regs[VG])) {
		dev_err(dptx->dev, "Failed to create Video Generator Regmap\n");
		return PTR_ERR(dptx->regs[VG]);
	}
	retval = vg_regmap_fields_init(dptx);
	if (retval) {
		dev_err(dptx->dev, "Failed to init register layout map\n");
		return retval;
	}

	return 0;
}
