/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "dptx.h"
#include "dptx_utils.h"

/*****************************************************************************
 *                                                                           *
 *                   Reset Manager Configuration Registers                   *
 *                                                                           *
 *****************************************************************************/

#define RM_SYSTEM_RST_N				0x00000000
#define RM_MMCM_RST_N                           0x00000004
#define RM_HDCP22EXT_RST_N                      0x00000008
#define RM_JTAGCTRL_RST_N                       0x0000000C

#define RM_SYSTEM_RST_N_CLEAN_MASK		0x00000F1F
#define RM_MMCM_RST_N_CLEAN_MASK		0x00000003
#define RM_HDCP22EXT_RST_N_CLEAN_MASK		0x00000001
#define RM_JTAGCTRL_RST_N_CLEAN_MASK		0x00000001


void rst_avp(struct dptx *dptx);
void rst_dptx_ctrl(struct dptx *dptx);
void rst_phy(struct dptx *dptx);
void rst_ag(struct dptx *dptx);
void rst_vg(struct dptx *dptx);
void rst_clkmng(struct dptx *dptx);
void rst_clean_all(struct dptx *dptx);
void rst_jtag_master(struct dptx *dptx);
