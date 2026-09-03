// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "rst_mng.h"
#include "regmaps/rst_mng.h"

void rst_avp(struct dptx *dptx)
{
	struct rstmng_regfields *rstmng_fields;

	rstmng_fields = dptx->rstmng_fields;

	dptx_write_regfield(dptx, rstmng_fields->field_global_rst, 0);
	dptx_write_regfield(dptx, rstmng_fields->field_global_rst, 1);
}

void rst_dptx_ctrl(struct dptx *dptx)
{
	struct rstmng_regfields *rstmng_fields;

	rstmng_fields = dptx->rstmng_fields;

	dptx_write_regfield(dptx, rstmng_fields->field_ctrl_rst, 0);
	dptx_write_regfield(dptx, rstmng_fields->field_ctrl_rst, 1);
}

void rst_phy(struct dptx *dptx)
{
	struct rstmng_regfields *rstmng_fields;

	rstmng_fields = dptx->rstmng_fields;

	dptx_write_regfield(dptx, rstmng_fields->field_phy_if_rst, 0);
	dptx_write_regfield(dptx, rstmng_fields->field_phy_if_rst, 1);
}

void rst_ag(struct dptx *dptx)
{
	struct rstmng_regfields *rstmng_fields;

	rstmng_fields = dptx->rstmng_fields;

	dptx_write_regfield(dptx, rstmng_fields->field_audiogen_aud_rst, 0);
	dptx_write_regfield(dptx, rstmng_fields->field_audiogen_ref_rst, 0);
	dptx_write_regfield(dptx, rstmng_fields->field_audiobridge_rst, 0);

	dptx_write_regfield(dptx, rstmng_fields->field_audiogen_aud_rst, 1);
	dptx_write_regfield(dptx, rstmng_fields->field_audiogen_ref_rst, 1);
	dptx_write_regfield(dptx, rstmng_fields->field_audiobridge_rst, 1);
}

void rst_vg(struct dptx *dptx)
{
	struct rstmng_regfields *rstmng_fields;

	rstmng_fields = dptx->rstmng_fields;

	dptx_write_regfield(dptx, rstmng_fields->field_videobridge_rst, 0);
	dptx_write_regfield(dptx, rstmng_fields->field_videobridge_rst, 1);
}

void rst_clkmng(struct dptx *dptx)
{
	struct rstmng_regfields *rstmng_fields;

	rstmng_fields = dptx->rstmng_fields;

	dptx_write_regfield(dptx, rstmng_fields->field_vidclk_gen_rst, 0);
	dptx_write_regfield(dptx, rstmng_fields->field_audclk_gen_rst, 0);
	dptx_write_regfield(dptx, rstmng_fields->field_vidclk_gen_rst, 1);
	dptx_write_regfield(dptx, rstmng_fields->field_audclk_gen_rst, 1);
}

void rst_jtag_master(struct dptx *dptx)
{
	struct rstmng_regfields *rstmng_fields;

	rstmng_fields = dptx->rstmng_fields;

	dptx_write_regfield(dptx, rstmng_fields->field_jtaglab_rst, 0);
	dptx_write_regfield(dptx, rstmng_fields->field_jtaglab_rst, 1);
}


void rst_clean_all(struct dptx *dptx)
{
	rstmng_write(dptx, RM_SYSTEM_RST_N, RM_SYSTEM_RST_N_CLEAN_MASK);
	rstmng_write(dptx, RM_MMCM_RST_N, RM_MMCM_RST_N_CLEAN_MASK);
	rstmng_write(dptx, RM_HDCP22EXT_RST_N, RM_HDCP22EXT_RST_N_CLEAN_MASK);
	rstmng_write(dptx, RM_JTAGCTRL_RST_N, RM_JTAGCTRL_RST_N_CLEAN_MASK);
}
