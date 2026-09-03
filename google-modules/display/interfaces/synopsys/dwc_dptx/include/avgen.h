/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#ifndef __DPTX_AVGEN_H__
#define __DPTX_AVGEN_H__

/*
 * DP 1.4 spec: audio infoframes use infoframe SDP v1.2 format
 * type = 0x84, length = 0x1B, version = 0x48 (0x48 >> 2 = 0x12)
 */
#define AUDIO_INFOFRAME_HEADER 0x00841B48
/*
 * DP 1.4 spec: SPD infoframes use infoframe SDP v1.3 format
 * type = 0x83, length = 0x1D, version = 0x4C (0x4C >> 2 = 0x13)
 */
#define SPD_SDP_HEADER         0x00831D4C
#define VSC_SDP_HEADER         0x00070513

struct dptx;

enum interfaceType {
	I2S = 0,
	SPDIF
};

struct audio_params {
	u8 iec_channel_numcl0;
	u8 iec_channel_numcr0;
	u8 use_lut;
	u8 iec_samp_freq;
	u8 iec_word_length;
	u8 iec_orig_samp_freq;
	u8 data_width;
	u8 num_channels;
	u8 inf_type;
	u8 mute;
	u8 ats_ver;
};

enum audio_sample_freq {
	SAMPLE_FREQ_32 = 0,
	SAMPLE_FREQ_44_1 = 1,
	SAMPLE_FREQ_48 = 2,
	SAMPLE_FREQ_88_2 = 3,
	SAMPLE_FREQ_96 = 4,
	SAMPLE_FREQ_176_4 = 5,
	SAMPLE_FREQ_192 = 6
};

struct audio_short_desc {
	u8 max_num_of_channels;
	enum audio_sample_freq max_sampling_freq;
	u8 max_bit_per_sample;
};

void dptx_audio_params_reset(struct audio_params *aparams);
void dptx_audio_config(struct dptx *dptx);
void dptx_audio_deconfig(struct dptx *dptx);
void dptx_audio_core_config(struct dptx *dptx);
void dptx_audio_gen_config(struct dptx *dptx);
void dptx_en_audio_channel(struct dptx *dptx, int ch_num, int enable);
void dptx_audio_mute(struct dptx *dptx);

void dptx_audio_inf_type_change(struct dptx *dptx);
void dptx_audio_num_ch_change(struct dptx *dptx);
void dptx_audio_data_width_change(struct dptx *dptx);

enum pixel_enc_type {
	RGB = 0,
	YCBCR420 = 1,
	YCBCR422 = 2,
	YCBCR444 = 3,
	YONLY = 4,
	RAW = 5
};

enum color_depth {
	COLOR_DEPTH_INVALID = 0,
	COLOR_DEPTH_6 = 6,
	COLOR_DEPTH_8 = 8,
	COLOR_DEPTH_10 = 10,
	COLOR_DEPTH_12 = 12,
	COLOR_DEPTH_16 = 16
};

enum pattern_mode {
	TILE = 0,
	RAMP = 1,
	CHESS_BOARD = 2,
	COLRAMP = 3
};

enum dynamic_range_type {
	CEA = 1,
	VESA = 2
};

enum colorimetry_type {
	ITU601 = 1,
	ITU709 = 2
};

enum video_format_type {
	VCEA = 0,
	CVT = 1,
	DMT = 2
};

struct dtd {
	u16 pixel_repetition_input;
	int pixel_clock;
	u8 interlaced; /* 1 for interlaced, 0 progressive */
	u16 h_active;
	u16 h_blanking;
	u16 h_image_size;
	u16 h_sync_offset;
	u16 h_sync_pulse_width;
	u8 h_sync_polarity;
	u16 v_active;
	u16 v_blanking;
	u16 v_image_size;
	u16 v_sync_offset;
	u16 v_sync_pulse_width;
	u8 v_sync_polarity;
};

void dptx_dtd_fill_from_mode(struct dtd *mdtd, struct drm_display_mode *m);
int dptx_dtd_fill(struct dtd *mdtd, u8 code, u32 refresh_rate,
		  u8 video_format);
void dwc_dptx_dtd_reset(struct dtd *mdtd);

struct video_params {
	u8 pix_enc;
	u8 pattern_mode;
	struct dtd mdtd;
	u8 mode;
	u8 bpc;
	u8 colorimetry;
	u8 dynamic_range;
	u8 aver_bytes_per_tu;
	u8 aver_bytes_per_tu_frac;
	u8 init_threshold;
	u32 refresh_rate;
	u8 video_format;
};

int dptx_dtd_parse(struct dptx *dptx, struct dtd *mdtd, u8 data[18]);
void dptx_video_params_reset(struct dptx *dptx);

void dptx_video_timing_change(struct dptx *dptx, int stream);
int dptx_video_mode_change(struct dptx *dptx, u8 vmode, int stream);
int dptx_video_config(struct dptx *dptx, int stream);
void dptx_video_core_config(struct dptx *dptx, int stream);
void dptx_video_gen_config(struct dptx *dptx, int stream);
void dptx_video_set_core_bpc(struct dptx *dptx, int stream);
void dptx_video_set_gen_bpc(struct dptx *dptx, int stream);
void dptx_video_set_sink_bpc(struct dptx *dptx, int stream);
void dptx_video_set_sink_col(struct dptx *dpx, int stream);
void dptx_video_set_timing_info(struct dptx *dptx, int stream);
void dptx_video_set_MSA(struct dptx *dptx, int stream);
void dptx_video_pattern_change(struct dptx *dptx, int stream);
void dptx_video_bpc_change(struct dptx *dptx, int stream);
void dptx_video_ycc_mapping_change(struct dptx *dptx, int stream);
void dptx_video_ts_change(struct dptx *dptx, int stream);
int dptx_video_ts_calculate(struct dptx *dptx, int lane_num, int rate,
			    int bpc, int encoding, int pixel_clock);
int dptx_get_vc_payload_size(struct dptx *dptx);
void dptx_video_pattern_set(struct dptx *dptx, enum pattern_mode pattern, int stream);
void dptx_enable_default_video_stream(struct dptx *dptx, int stream);
void dptx_disable_default_video_stream(struct dptx *dptx, int stream);
void dptx_video_reset(struct dptx *dptx, int enable, int stream);
void dptx_audio_samp_freq_config(struct dptx *dptx);
void dptx_audio_infoframe_sdp_en(struct dptx *dptx, int enabled);
void dptx_spd_sdp_en(struct dptx *dptx, int enabled);
void dptx_vsc_sdp_en(struct dptx *dptx, int enabled);
void dptx_vsd_ycbcr420_send(struct dptx *dptx, u8 enable);
#endif
