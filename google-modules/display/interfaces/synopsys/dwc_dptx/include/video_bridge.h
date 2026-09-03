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
 *                       VB/VPG Configuration Registers                      *
 *                                                                           *
 *****************************************************************************/

#define VB_SRC_SEL			0x0000
#define VPG_CONF0			0x0010
#define VPG_CONF0_ENABLE_MASK		0x00000001
#define VPG_CONF0_DE_POL_MASK		0x00000010
#define VPG_CONF0_HS_POL_MASK		0x00000020
#define VPG_CONF0_VS_POL_MASK		0x00000040
#define VPG_CONF0_INTERLACED_MASK	0x00000100
#define VPG_CONF0_VBLANK_OSC_MASK	0x00000200
#define VPG_CONF0_COLORIMETRY_MASK	0x00007000
#define VPG_CONF0_PREPETITION_MASK	0x000F0000
#define VPG_CONF0_COLORDEPTH_MASK	0x00F00000
#define VPG_CONF0_COLOR_RANGE_MASK	0x01000000
#define VPG_CONF1			0x0014
#define VPG_CONF1_3D_ENABLE_MASK	0x00000001
#define VPG_CONF1_3D_FRAMESEQ_MASK	0x00000002
#define VPG_CONF1_3D_STRUCT_MASK	0x000000F0
#define VPG_CONF1_PATT_MODE_MASK	0x00030000
#define VPG_HAHB_CONFIG			0x0018
#define VPG_HAHB_HACTIVE_MASK		0x00003FFF
#define VPG_HAHB_HBLANK_MASK		0x1FFF0000
#define VPG_HDHW_CONFIG			0x001C
#define VPG_HDHW_HFRONT_MASK		0x00001FFF
#define VPG_HDHW_HWIDTH_MASK		0x03FF0000
#define VPG_VAVB_CONFIG			0x0020
#define VPG_VAVB_VACTIVE_MASK		0x00001FFF
#define VPG_VAVB_VBLANK_MASK		0x03FF0000
#define VPG_VDVW_CONFIG			0x0024
#define VPG_VDVW_VFRONT_MASK		0x000001FF
#define VPG_VDVW_VWIDTH_MASK		0x007F0000
#define VPG_CB_LENGTH_CONFIG		0x0028
#define VPG_CB_WIDTH_MASK		0x000003FF
#define VPG_CB_HEIGHT_MASK		0x01FF0000
#define VPG_CB_COLORA_L			0x002C
#define VPG_CB_COLORA_H			0x0030
#define VPG_CB_COLORB_L			0x0034
#define VPG_CB_COLORB_H			0x0038

#define STREAM_OFFSET(stream) (0x100 * stream)

void video_chess_board_config(struct dptx *dptx, int stream);
int video_generator_config(struct dptx *dptx, int stream);
