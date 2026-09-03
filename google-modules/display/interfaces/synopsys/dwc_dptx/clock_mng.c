// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "clock_mng.h"
#include "clk_cfg.h"
#include "rst_mng.h"

/***********************
 * VIDEO CLOCK CONTROL *
 ***********************/

uint8_t video_mmcm_locked(struct dptx *dptx)
{
	uint32_t reg = MMCM_VIDEO_OFFSET + MMCM_LOCKED;

	if (clkmng_read(dptx, reg) & 0x1)
		return TRUE;

	return FALSE;
}

int get_video_mmcm_config_idx(struct dptx *dptx, u32 freq_khz)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(video_clock); i++) {
		if (freq_is_equal(video_clock[i], freq_khz)) {
			dptx_dbg(dptx, "%s: For %u kHz", __func__, freq_khz);
			return i;
		}
	}

	return -1;
}

int configure_video_mmcm(struct dptx *dptx, u32 freq_khz)
{
	int ret, idx;

	idx = get_video_mmcm_config_idx(dptx, freq_khz);
	if (idx < 0) {
		dptx_err(dptx, "%s: No configurations for %u kHz", __func__, freq_khz);
		return -EINVAL;
	}

	dptx_dbg(dptx, "Configuring video MMCM - %u kHz -> idx: %u", freq_khz, idx);
	ret = configure_mmcm(dptx, idx, VIDEO);
	if (ret)
		dptx_err(dptx, "%s: Failed to configure video MMCM", __func__);

	return ret;
}

/***********************
 * AUDIO CLOCK CONTROL *
 ***********************/

uint8_t audio_mmcm_locked(struct dptx *dptx)
{
	uint32_t reg = MMCM_AUDIO_OFFSET + MMCM_LOCKED;

	if (clkmng_read(dptx, reg) & 0x1)
		return TRUE;

	return FALSE;
}

int get_audio_mmcm_config_idx(struct dptx *dptx, u32 freq_hz)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(audio_clock); i++) {
		if (freq_is_equal(audio_clock[i], freq_hz)) {
			dptx_dbg(dptx, "%s: For %u Hz", __func__, freq_hz);
			return i;
		}
	}

	return -1;
}

int configure_audio_mmcm(struct dptx *dptx, u32 freq_hz)
{
	int ret, idx;

	idx = get_audio_mmcm_config_idx(dptx, freq_hz);
	if (idx < 0) {
		dptx_err(dptx, "%s: No configurations for %u Hz", __func__, freq_hz);
		return -EINVAL;
	}

	dptx_dbg(dptx, "Configuring audio MMCM - %u Hz -> idx: %u", freq_hz, idx);
	ret = configure_mmcm(dptx, idx, AUDIO);
	if (ret)
		dptx_err(dptx, "%s: Failed to configure audio MMCM", __func__);

	return ret;
}

int configure_mmcm(struct dptx *dptx, int idx, int clk_type)
{
	uint32_t block_offset;

	// Reset
	if (clk_type == VIDEO) {
		block_offset = MMCM_VIDEO_OFFSET;
	} else if (clk_type == AUDIO) {
		block_offset = MMCM_AUDIO_OFFSET;
	} else {
		dptx_err(dptx, "%s: clock type is not known", __func__);
		return -EINVAL;
	}

	// Configure
	clkmng_write(dptx, block_offset + MMCM_SADDR, idx);
	clkmng_write(dptx, block_offset + MMCM_SEN, 1);

	if (clk_type == VIDEO) {
		msleep(20);
		if (video_mmcm_locked(dptx) == FALSE) {
			dptx_err(dptx, "%s: Video MMCM is not locked", __func__);
			return -EINVAL;
		}
		dptx_dbg(dptx, "%s: Video MMCM is locked", __func__);
	} else {
		msleep(20);
		if (audio_mmcm_locked(dptx) == FALSE) {
			dptx_err(dptx, "%s: Audio MMCM is not locked", __func__);
			return -EINVAL;
		}
		dptx_dbg(dptx, "%s: Audio MMCM is locked", __func__);
	}

	return 0;
}
