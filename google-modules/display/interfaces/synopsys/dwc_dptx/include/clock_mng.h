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
 *                   Clock Manager Configuration Registers                   *
 *                                                                           *
 *****************************************************************************/

#define CLK_SRC_SEL			0x00000000
#define CLK_SRC_SEL_OPMODE_MASK		0x00000001
#define CLK_SRC_SEL_PR2X_MASK		0x00000002

#define MMCM_VIDEO_OFFSET		0x00000010
#define MMCM_AUDIO_OFFSET		0x00000020
#define MMCM_SADDR			0x00000000
#define MMCM_SEN			0x00000004
#define MMCM_SRDY			0x00000008
#define MMCM_LOCKED			0x0000000C

enum target_device {
	VIDEO,
	AUDIO
};

uint8_t video_mmcm_locked(struct dptx *dptx);
int get_video_mmcm_config_idx(struct dptx *dptx, u32 freq_khz);
int configure_video_mmcm(struct dptx *dptx, u32 freq_khz);
uint8_t audio_mmcm_locked(struct dptx *dptx);
int get_audio_mmcm_config_idx(struct dptx *dptx, u32 freq_hz);
int configure_audio_mmcm(struct dptx *dptx, u32 freq_hz);
int configure_mmcm(struct dptx *dptx, int idx, int clk_type);
