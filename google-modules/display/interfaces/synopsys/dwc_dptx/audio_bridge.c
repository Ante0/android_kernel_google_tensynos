// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "audio_bridge.h"

static void _ag_get_sample_rate(u32 freq_hz, uint32_t *sf, uint32_t *tb)
{
	if (!sf || !tb)
		return;

	if (freq_is_equal(freq_hz, 1536000)) {
		*sf = 17;
		*tb = 0xBB80000;
	} else if (freq_is_equal(freq_hz, 1411200)) {
		*sf = 16;
		*tb = 0xAC44000;
	} else if (freq_is_equal(freq_hz, 1024000)) {
		*sf = 15;
		*tb = 0x7D00000;
	} else if (freq_is_equal(freq_hz, 768000)) {
		*sf = 14;
		*tb = 0x5DC0000;
	} else if (freq_is_equal(freq_hz, 705600)) {
		*sf = 13;
		*tb = 0x5622000;
	} else if (freq_is_equal(freq_hz, 512000)) {
		*sf = 12;
		*tb = 0x3E80000;
	} else if (freq_is_equal(freq_hz, 384000)) {
		*sf = 11;
		*tb = 0x2EE0000;
	} else if (freq_is_equal(freq_hz, 352800)) {
		*sf = 10;
		*tb = 0x2B11000;
	} else if (freq_is_equal(freq_hz, 256000)) {
		*sf = 9;
		*tb = 0x1F40000;
	} else if (freq_is_equal(freq_hz, 192000)) {
		*sf = 8;
		*tb = 0x1770000;
	} else if (freq_is_equal(freq_hz, 176400)) {
		*sf = 7;
		*tb = 0x1588800;
	} else if (freq_is_equal(freq_hz, 128000)) {
		*sf = 6;
		*tb = 0xFA0000;
	} else if (freq_is_equal(freq_hz, 96000)) {
		*sf = 5;
		*tb = 0xBB8000;
	} else if (freq_is_equal(freq_hz, 88200)) {
		*sf = 4;
		*tb = 0xAC4400;
	} else if (freq_is_equal(freq_hz, 64000)) {
		*sf = 3;
		*tb = 0x7D0000;
	} else if (freq_is_equal(freq_hz, 48000)) {
		*sf = 2;
		*tb = 0x5DC000;
	} else if (freq_is_equal(freq_hz, 44100)) {
		*sf = 1;
		*tb = 0x562200;
	} else {
		*sf = 0;
		*tb = 0x3E8000;
	}
}

void audio_generator_config(struct dptx *dptx, int stream)
{
	u32 ag_conf0 = 0, ag_conf1, ap_conf0 = 0, timerbase = 0;
	struct audio_params *audio;

	audio = &dptx->aparams;

	if (audio->iec_samp_freq == 0) {
		dptx_err(dptx, "%s: Audio Sampling Frequency not defined", __func__);
		return;
	}

	if (audio->inf_type == I2S) {
		ap_conf0 = set(ap_conf0, AP_CONFIG0_I2S_EN_MASK, 1);
	} else if (audio->inf_type == SPDIF) {
		ap_conf0 = set(ap_conf0, AP_CONFIG0_SPDIF_EN_MASK, 1);
	} else {
		dptx_err(dptx, "%s: Audio Interface Type not supported [%d]",
			 __func__, audio->inf_type);
		return;
	}

	// More than 2 channels => layout 1 else layout 0
	if (audio->num_channels > 1)
		ag_conf0 = set(ag_conf0, AG_CONFIG0_LAYOUT_MASK, 1);

	_ag_get_sample_rate(audio->iec_samp_freq, &ag_conf1, &timerbase);

	// Configure
	ag_write(dptx, AG_TIMER_BASE + STREAM_OFFSET(stream), timerbase);
	ag_write(dptx, AP_CONFIG0 + STREAM_OFFSET(stream), ap_conf0);
}

void audio_generator_disable(struct dptx *dptx)
{
	uint32_t value;

	value = ag_read(dptx, EXT_AG_CONFIG0);
	value = set(value, AG_CONFIG0_ENABLE_MASK, 0);
	ag_write(dptx, EXT_AG_CONFIG0, value);
}
