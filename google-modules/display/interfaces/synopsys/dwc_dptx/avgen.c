// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include <linux/hdmi.h>

#include "dptx.h"
#include "clock_mng.h"
#include "video_bridge.h"
#include "regmaps/ctrl_fields.h"

static u8 dptx_bit_field(const u16 data, u8 shift, u8 width)
{
	return ((data >> shift) & ((((u16)1) << width) - 1));
}

static u16 dptx_concat_bits(u8 bhi, u8 ohi, u8 nhi, u8 blo, u8 olo, u8 nlo)
{
	return (dptx_bit_field(bhi, ohi, nhi) << nlo) |
		dptx_bit_field(blo, olo, nlo);
}

static u16 dptx_byte_to_word(const u8 hi, const u8 lo)
{
	return dptx_concat_bits(hi, 0, 8, lo, 0, 8);
}

static u32 dptx_byte_to_dword(u8 b3, u8 b2, u8 b1, u8 b0)
{
	u32 retval = 0;

	retval |= b0 << (0 * 8);
	retval |= b1 << (1 * 8);
	retval |= b2 << (2 * 8);
	retval |= b3 << (3 * 8);
	return retval;
}

int dptx_dtd_parse(struct dptx *dptx, struct dtd *mdtd, u8 data[18])
{
	mdtd->pixel_repetition_input = 0;

	mdtd->pixel_clock = dptx_byte_to_word(data[1], data[0]);
	if (mdtd->pixel_clock < 0x01)
		return -EINVAL;

	mdtd->h_active = dptx_concat_bits(data[4], 4, 4, data[2], 0, 8);
	mdtd->h_blanking = dptx_concat_bits(data[4], 0, 4, data[3], 0, 8);
	mdtd->h_sync_offset = dptx_concat_bits(data[11], 6, 2, data[8], 0, 8);
	mdtd->h_sync_pulse_width = dptx_concat_bits(data[11], 4, 2, data[9], 0, 8);
	mdtd->h_image_size = dptx_concat_bits(data[14], 4, 4, data[12], 0, 8);
	mdtd->v_active = dptx_concat_bits(data[7], 4, 4, data[5], 0, 8);
	mdtd->v_blanking = dptx_concat_bits(data[7], 0, 4, data[6], 0, 8);
	mdtd->v_sync_offset = dptx_concat_bits(data[11], 2, 2, data[10], 4, 4);
	mdtd->v_sync_pulse_width = dptx_concat_bits(data[11], 0, 2, data[10], 0, 4);
	mdtd->v_image_size = dptx_concat_bits(data[14], 0, 4, data[13], 0, 8);
	if (dptx_bit_field(data[17], 4, 1) != 1)
		return -EINVAL;
	if (dptx_bit_field(data[17], 3, 1) != 1)
		return -EINVAL;

	mdtd->interlaced = dptx_bit_field(data[17], 7, 1) == 1;
	mdtd->v_sync_polarity = dptx_bit_field(data[17], 2, 1) == 0;
	mdtd->h_sync_polarity = dptx_bit_field(data[17], 1, 1) == 0;
	if (mdtd->interlaced == 1)
		mdtd->v_active /= 2;
	mdtd->pixel_clock *= 10;
	dptx_dbg(dptx, "DTD pixel_clock: %d interlaced: %d\n",
		 mdtd->pixel_clock, mdtd->interlaced);
	dptx_dbg(dptx, "h_active: %d h_blanking: %d h_sync_offset: %d\n",
		 mdtd->h_active, mdtd->h_blanking, mdtd->h_sync_offset);
	dptx_dbg(dptx, "h_sync_pulse_width: %d h_image_size: %d h_sync_polarity: %d\n",
		 mdtd->h_sync_pulse_width, mdtd->h_image_size,
		 mdtd->h_sync_polarity);
	dptx_dbg(dptx, "v_active: %d v_blanking: %d v_sync_offset: %d\n",
		 mdtd->v_active, mdtd->v_blanking, mdtd->v_sync_offset);
	dptx_dbg(dptx, "v_sync_pulse_width: %d v_image_size: %d v_sync_polarity: %d\n",
		 mdtd->v_sync_pulse_width, mdtd->v_image_size,
		 mdtd->v_sync_polarity);

	return 0;
}

void dptx_audio_sdp_en(struct dptx *dptx, int enabled)
{
	struct ctrl_regfields *ctrl_fields;
	u32 value = enabled ? 1 : 0;

	ctrl_fields = dptx->ctrl_fields;
	dptx_write_regfield(dptx, ctrl_fields->field_en_audio_stream_sdp_vertical_ctrl, value);
	dptx_write_regfield(dptx, ctrl_fields->field_en_audio_stream_sdp_horizontal_ctrl, value);
}

void dptx_audio_timestamp_sdp_en(struct dptx *dptx, int enabled)
{
	struct ctrl_regfields *ctrl_fields;
	u32 value = enabled ? 1 : 0;

	ctrl_fields = dptx->ctrl_fields;
	dptx_write_regfield(dptx, ctrl_fields->field_en_audio_timestamp_sdp_vertical_ctrl, value);
	dptx_write_regfield(dptx, ctrl_fields->field_en_audio_timestamp_sdp_horizontal_ctrl, value);
}

void dptx_audio_infoframe_sdp_en(struct dptx *dptx, int enabled)
{
	struct sdp_full_data audio_infoframe;
	u32 byte;

	memset(&audio_infoframe, 0, sizeof(audio_infoframe));
	audio_infoframe.en = !!enabled;
	audio_infoframe.payload[0] = AUDIO_INFOFRAME_HEADER;

	/* DP audio stream is always: PCM, 2 channels, 48 kHz, 16-bit */

	/* data byte 1: PCM type + 2 channels */
	byte = (HDMI_AUDIO_CODING_TYPE_PCM << 4) | 0x1;
	audio_infoframe.payload[1] = (byte << 24);

	/* data byte 2: 48 kHz + 16-bit */
	byte = (HDMI_AUDIO_SAMPLE_FREQUENCY_48000 << 2) | HDMI_AUDIO_SAMPLE_SIZE_16;
	audio_infoframe.payload[1] |= (byte << 16);

	dptx_fill_sdp(dptx, &audio_infoframe);
}

void dptx_spd_sdp_en(struct dptx *dptx, int enabled)
{
	struct sdp_full_data spd;

	memset(&spd, 0, sizeof(spd));
	spd.en = !!enabled;
	spd.payload[0] = SPD_SDP_HEADER;

	/* Data byte 1: CTA header byte 1: infoframe version number */
	spd.payload[1] = 1 << 24;

	/* Data byte 2: CTA header byte 2: length of infoframe */
	spd.payload[1] |= 25 << 16;

	/* Data bytes 3-10: payload bytes 1-8: vendor name */
	spd.payload[1] |= 0x0000476F;  // Go
	spd.payload[2] = 0x6F676C65;   // ogle

	/* Data bytes 11-26: payload bytes 9-24: product description */
	spd.payload[3] = 0x00005069;  // Pi
	spd.payload[4] = 0x78656C00;  // xel

	dptx_fill_sdp(dptx, &spd);
}

void dptx_vsc_sdp_en(struct dptx *dptx, int enabled)
{
	struct sdp_full_data vsc;
	u8 pixelformat = DP_PIXELFORMAT_RGB;
	u8 colorimetry = DP_COLORIMETRY_DEFAULT;
	u8 bpc = dptx->link.bpc;
	u8 dynamic_range = DP_DYNAMIC_RANGE_VESA;
	u8 content_type = DP_CONTENT_TYPE_NOT_DEFINED;
	u32 byte;

	memset(&vsc, 0, sizeof(vsc));
	vsc.en = !!enabled;
	vsc.payload[0] = VSC_SDP_HEADER;

	byte = (pixelformat & 0xf) << 4;
	byte |= colorimetry & 0xf;
	vsc.payload[5] = byte << 24;

	switch (bpc) {
	case 6:
		byte = 0x0;
		break;
	case 8:
		byte = 0x1;
		break;
	case 10:
	default:
		byte = 0x2;
		break;
	}

	if (dynamic_range == DP_DYNAMIC_RANGE_CTA)
		byte |= 0x80;

	vsc.payload[5] |= byte << 16;

	byte = content_type & 0x7;
	vsc.payload[5] |= byte << 8;

	dptx_fill_sdp(dptx, &vsc);
}

static void dptx_disable_sdp(struct dptx *dptx, struct sdp_full_data *data)
{
	int i;
	u32 reg;
	int reg_num;
	u32 header;
	int sdp_offset;

	header = cpu_to_be32(data->payload[0]);

	for (i = 0; i < DPTX_SDP_NUM; i++) {
		if (dptx->sdp_list[i].payload[0] == header) {
			reg = dptx_read_reg(dptx, dptx->regs[DPTX], SDP_VERTICAL_CTRL);
			reg &= ~(1 << (2 + i));
			dptx_write_reg(dptx, dptx->regs[DPTX], SDP_VERTICAL_CTRL, reg);

			reg = dptx_read_reg(dptx, dptx->regs[DPTX], SDP_HORIZONTAL_CTRL);
			reg &= ~(1 << (2 + i));
			dptx_write_reg(dptx, dptx->regs[DPTX], SDP_HORIZONTAL_CTRL, reg);

			sdp_offset = i * DPTX_SDP_SIZE;
			reg_num = 0;
			while (reg_num < DPTX_SDP_LEN) {
				dptx_write_reg(dptx, dptx->regs[DPTX],
					       SDP_REGISTER_BANK_0 + sdp_offset + reg_num * 4, 0);
				reg_num++;
			}

			dptx->sdp_list[i].payload[0] = 0;
		}
	}
}

static void dptx_enable_sdp(struct dptx *dptx, struct sdp_full_data *data)
{
	int i;
	u32 reg;
	int reg_num;
	u32 header;
	int sdp_offset;

	header = cpu_to_be32(data->payload[0]);

	for (i = 0; i < DPTX_SDP_NUM; i++) {
		if (dptx->sdp_list[i].payload[0] == 0) {
			dptx->sdp_list[i].payload[0] = header;
			sdp_offset = i * DPTX_SDP_SIZE;
			reg_num = 0;
			while (reg_num < DPTX_SDP_LEN) {
				dptx_write_reg(dptx, dptx->regs[DPTX],
					       SDP_REGISTER_BANK_0 + sdp_offset + reg_num * 4,
					       cpu_to_be32(data->payload[reg_num]));
				reg_num++;
			}
			switch (data->blanking) {
			case 0:
				reg = dptx_read_reg(dptx, dptx->regs[DPTX], SDP_VERTICAL_CTRL);
				reg |= (1 << (2 + i));
				dptx_write_reg(dptx, dptx->regs[DPTX], SDP_VERTICAL_CTRL, reg);
				break;
			case 1:
				reg = dptx_read_reg(dptx, dptx->regs[DPTX], SDP_HORIZONTAL_CTRL);
				reg |= (1 << (2 + i));
				dptx_write_reg(dptx, dptx->regs[DPTX], SDP_HORIZONTAL_CTRL, reg);
				break;
			case 2:
				reg = dptx_read_reg(dptx, dptx->regs[DPTX], SDP_VERTICAL_CTRL);
				reg |= (1 << (2 + i));
				dptx_write_reg(dptx, dptx->regs[DPTX], SDP_VERTICAL_CTRL, reg);
				reg = dptx_read_reg(dptx, dptx->regs[DPTX], SDP_HORIZONTAL_CTRL);
				reg |= (1 << (2 + i));
				dptx_write_reg(dptx, dptx->regs[DPTX], SDP_HORIZONTAL_CTRL, reg);
				break;
			}
			break;
		}
	}
}

void dptx_fill_sdp(struct dptx *dptx, struct sdp_full_data *data)
{
	if (data->en == 1)
		dptx_enable_sdp(dptx, data);
	else
		dptx_disable_sdp(dptx, data);
}

void dptx_vsd_ycbcr420_send(struct dptx *dptx, u8 enable)
{
	struct sdp_full_data vsc_data;
	int i;

	struct video_params *vparams;

	vparams = &dptx->vparams;

	vsc_data.en = enable;
	for (i = 0 ; i < 9 ; i++) {
		if (i == 0)
			vsc_data.payload[i] = 0x00070513;
		else if (i == 5)
			switch (vparams->bpc) {
			case COLOR_DEPTH_8:
				vsc_data.payload[i] = 0x30010000;
				break;
			case COLOR_DEPTH_10:
				vsc_data.payload[i] = 0x30020000;
				break;
			case COLOR_DEPTH_12:
				vsc_data.payload[i] = 0x30030000;
				break;
			case COLOR_DEPTH_16:
				vsc_data.payload[i] = 0x30040000;
				break;
			}
		else
			vsc_data.payload[i] = 0x0;
	}
	vsc_data.blanking = 0;
	vsc_data.cont = 1;

	dptx_fill_sdp(dptx, &vsc_data);
}

void dptx_en_audio_channel(struct dptx *dptx, int ch_num, int enable)
{
	u32 reg = 0;
	u32 data_en = 0;

	reg = dptx_read_reg(dptx, dptx->regs[DPTX], AUD_CONFIG1);
	reg &= ~DPTX_AUD_CONFIG1_DATA_EN_IN_MASK;

	if (enable) {
		switch (ch_num) {
		case 1:
			data_en = DPTX_EN_AUDIO_CH_1;
			break;
		case 2:
			data_en = DPTX_EN_AUDIO_CH_2;
			break;
		case 3:
			data_en = DPTX_EN_AUDIO_CH_3;
			break;
		case 4:
			data_en = DPTX_EN_AUDIO_CH_4;
			break;
		case 5:
			data_en = DPTX_EN_AUDIO_CH_5;
			break;
		case 6:
			data_en = DPTX_EN_AUDIO_CH_6;
			break;
		case 7:
			data_en = DPTX_EN_AUDIO_CH_7;
			break;
		case 8:
			data_en = DPTX_EN_AUDIO_CH_8;
			break;
		}
		reg |= data_en << DPTX_AUD_CONFIG1_DATA_EN_IN_SHIFT;
	} else {
		switch (ch_num) {
		case 1:
			data_en = ~DPTX_EN_AUDIO_CH_1;
			break;
		case 2:
			data_en = ~DPTX_EN_AUDIO_CH_2;
			break;
		case 3:
			data_en = ~DPTX_EN_AUDIO_CH_3;
			break;
		case 4:
			data_en = ~DPTX_EN_AUDIO_CH_4;
			break;
		case 5:
			data_en = ~DPTX_EN_AUDIO_CH_5;
			break;
		case 6:
			data_en = ~DPTX_EN_AUDIO_CH_6;
			break;
		case 7:
			data_en = ~DPTX_EN_AUDIO_CH_7;
			break;
		case 8:
			data_en = ~DPTX_EN_AUDIO_CH_8;
			break;
		}
		reg &= data_en << DPTX_AUD_CONFIG1_DATA_EN_IN_SHIFT;
	}
	dptx_write_reg(dptx, dptx->regs[DPTX], AUD_CONFIG1, reg);
}

void dptx_video_reset(struct dptx *dptx, int enable, int stream)
{
	u32 reg;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	reg = dptx_read_regfield(dptx, ctrl_fields->field_video_reset);

	if (enable)
		reg |= BIT(stream);
	else
		reg &= ~BIT(stream);

	dptx_write_regfield(dptx, ctrl_fields->field_video_reset, reg);
}

void dptx_audio_mute(struct dptx *dptx)
{
	struct audio_params *aparams;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	aparams = &dptx->aparams;
	if (aparams->mute == 1)
		dptx_write_regfield(dptx, ctrl_fields->field_audio_mute, 1);
	else
		dptx_write_regfield(dptx, ctrl_fields->field_audio_mute, 0);
}

void dptx_audio_config(struct dptx *dptx)
{
	dptx_audio_core_config(dptx);
	dptx_audio_sdp_en(dptx, 1);
	dptx_audio_timestamp_sdp_en(dptx, 1);
	dptx_audio_infoframe_sdp_en(dptx, 1);
}

void dptx_audio_deconfig(struct dptx *dptx)
{
	dptx_audio_sdp_en(dptx, 0);
	dptx_audio_timestamp_sdp_en(dptx, 0);
	dptx_audio_infoframe_sdp_en(dptx, 0);
}

void dptx_audio_core_config(struct dptx *dptx)
{
	struct audio_params *aparams;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;
	aparams = &dptx->aparams;

	dptx_audio_inf_type_change(dptx);
	dptx_audio_num_ch_change(dptx);
	dptx_audio_data_width_change(dptx);
	dptx_write_regfield(dptx, ctrl_fields->field_audio_timestamp_version_num, aparams->ats_ver);
	dptx_en_audio_channel(dptx, aparams->num_channels, 1);
}

void dptx_audio_inf_type_change(struct dptx *dptx)
{
	struct audio_params *aparams;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;
	aparams = &dptx->aparams;
	dptx_write_regfield(dptx, ctrl_fields->field_audio_inf_select, aparams->inf_type);
}

void dptx_audio_num_ch_change(struct dptx *dptx)
{
	u32 num_ch_map;
	struct audio_params *aparams;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;
	aparams = &dptx->aparams;

	if (aparams->num_channels == 1)
		num_ch_map = 0;
	else if (aparams->num_channels == 2)
		num_ch_map = 1;
	else
		num_ch_map = aparams->num_channels - 1;

	dptx_write_regfield(dptx, ctrl_fields->field_num_channels, num_ch_map);
}

void dptx_audio_data_width_change(struct dptx *dptx)
{
	struct audio_params *aparams;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;
	aparams = &dptx->aparams;
	dptx_write_regfield(dptx, ctrl_fields->field_audio_data_width, aparams->data_width);
}

void dptx_audio_samp_freq_config(struct dptx *dptx)
{
	struct audio_params *aparams;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;
	aparams = &dptx->aparams;
	dptx_write_regfield(dptx, ctrl_fields->field_iec_samp_freq, aparams->iec_samp_freq);
	dptx_write_regfield(dptx, ctrl_fields->field_iec_origsampfreq, aparams->iec_orig_samp_freq);
}

void dptx_audio_gen_config(struct dptx *dptx)
{
	struct audio_params *aparams;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;
	aparams = &dptx->aparams;

	/* AG_CONFIG1 */
	dptx_write_regfield(dptx, ctrl_fields->field_audio_use_lut, aparams->use_lut);
	dptx_audio_data_width_change(dptx);

	/* AG_CONFIG3 */
	dptx_write_regfield(dptx, ctrl_fields->field_iec_channelnumcl0_3, aparams->iec_channel_numcl0);
	dptx_write_regfield(dptx, ctrl_fields->field_iec_channelnumcr0_3, aparams->iec_channel_numcr0);

	/* AG_CONFIG4 */
	dptx_audio_samp_freq_config(dptx);
	dptx_write_regfield(dptx, ctrl_fields->field_iec_word_length, aparams->iec_word_length);

	/* AG_CONFIG5 */
	dptx_write_regfield(dptx, ctrl_fields->field_iec_channelnumcl0_5, aparams->iec_channel_numcl0);
	dptx_write_regfield(dptx, ctrl_fields->field_iec_channelnumcr0_5, aparams->iec_channel_numcr0);
}

/*
 * Video Generation
 */

void dptx_video_timing_change(struct dptx *dptx, int stream)
{
	dptx_disable_default_video_stream(dptx, stream);
	dptx_video_core_config(dptx, stream);
	dptx_video_ts_change(dptx, stream);
	dptx_audio_mute(dptx);
	dptx_enable_default_video_stream(dptx, stream);
#if 0
	// TODO: make all video generator calls conditional on HW support
	dptx_video_gen_config(dptx, stream);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_SWRST_N(stream), 0);
#endif
}

int dptx_video_mode_change(struct dptx *dptx, u8 vmode, int stream)
{
	int retval;
	struct video_params *vparams;
	struct dtd mdtd;

	vparams = &dptx->vparams;
	dptx_dtd_fill_from_mode(&mdtd, &dptx->current_mode);

	retval = dptx_video_ts_calculate(dptx, dptx->link.lanes,
					 dptx->link.rate, vparams->bpc,
					 vparams->pix_enc, mdtd.pixel_clock);
	if (retval)
		return retval;
	vparams->mdtd = mdtd;
	vparams->mode = vmode;

	/* MMCM */
	dptx_video_reset(dptx, 1, stream);
	retval = 0; //TODO: configure_video_mmcm(dptx, mdtd.pixel_clock);
	if (retval) {
		dptx_video_reset(dptx, 0, stream);
		return retval;
	}
	dptx_video_reset(dptx, 0, stream);
	dptx_video_timing_change(dptx, stream);
	dptx_dbg(dptx, "%s: Change video mode to %d\n",
		 __func__, vmode);

	return retval;
}

int dptx_video_config(struct dptx *dptx, int stream)
{
	struct video_params *vparams;
	struct dtd *mdtd;

	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;

	if (!dptx_dtd_fill(mdtd, vparams->mode,
			   vparams->refresh_rate, vparams->video_format))
		return -EINVAL;

	dptx_video_core_config(dptx, stream);
	return 0;
}

void dptx_video_gen_config(struct dptx *dptx, int stream)
{
	u32 reg = 0;
	struct video_params *vparams;
	struct dtd *mdtd;
	u8 vmode;

	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;
	vmode = vparams->mode;

	reg |= DPTX_VG_CONFIG1_ODE_POL_EN;

	/* Configure video polarity   */
	if (mdtd->h_sync_polarity == 1)
		reg |= DPTX_VG_CONFIG1_OH_SYNC_POL_EN;

	if (mdtd->v_sync_polarity == 1)
		reg |= DPTX_VG_CONFIG1_OV_SYNC_POL_EN;

	/* Configure Interlaced or Prograssive video  */
	if (mdtd->interlaced == 1)
		reg |= DPTX_VG_CONFIG1_OIP_EN;

	/* Configure BLANK  */

	if (vparams->video_format == VCEA) {
		if (vmode == 5 || vmode == 6 || vmode == 7 ||
		    vmode == 10 || vmode == 11 || vmode == 20 ||
		    vmode == 21 || vmode == 22 || vmode == 39 ||
		    vmode == 25 || vmode == 26 || vmode == 40 ||
		    vmode == 44 || vmode == 45 || vmode == 46 ||
		    vmode == 50 || vmode == 51 || vmode == 54 ||
		    vmode == 55 || vmode == 58 || vmode == 59)
			reg |= DPTX_VG_CONFIG1_BLANK_IN_OSC_EN;
	}

	/* Single, dual, or quad pixel */
	reg &= ~DPTX_VG_CONFIG1_MULTI_PIXEL_MASK;
	reg |= dptx->multipixel << DPTX_VG_CONFIG1_MULTI_PIXEL_SHIFT;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_CONFIG1_N(stream), reg);

	dptx_video_ycc_mapping_change(dptx, stream);
	dptx_video_set_gen_bpc(dptx, stream);
	dptx_video_pattern_change(dptx, stream);

	/* Configure video_gen2 register */

	reg = 0;
	if (vparams->pix_enc == YCBCR420) {
		reg |= (mdtd->h_active / 2) << DPTX_VG_CONFIG2_H_ACTIVE_SHIFT;
		reg |= (mdtd->h_blanking / 2) << DPTX_VG_CONFIG2_H_BLANK_SHIFT;
	} else {
		reg |= mdtd->h_active << DPTX_VG_CONFIG2_H_ACTIVE_SHIFT;
		reg |= mdtd->h_blanking << DPTX_VG_CONFIG2_H_BLANK_SHIFT;
	}

	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_CONFIG2_N(stream), reg);

	/* Configure video_gen3 register */
	reg = 0;
	reg |= mdtd->h_sync_offset << DPTX_VIDEO_H_FRONT_PORCH;
	reg |= mdtd->h_sync_pulse_width << DPTX_VIDEO_H_SYNC_WIDTH;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_CONFIG3_N(stream), reg);

	/* Configure video_gen4 register */
	reg = 0;
	reg |= mdtd->v_active << DPTX_VIDEO_V_ACTIVE_SHIFT;
	reg |= mdtd->v_blanking << DPTX_VIDEO_V_BLANK_SHIFT;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_CONFIG4_N(stream), reg);

	/* Configure video_gen5 register */
	reg = 0;
	reg |= mdtd->v_sync_offset << DPTX_VIDEO_V_FRONT_PORCH;
	reg |= mdtd->v_sync_pulse_width << DPTX_VIDEO_V_SYNC_WIDTH;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_CONFIG5_N(stream), reg);
}

void dptx_video_ycc_mapping_change(struct dptx *dptx, int stream)
{
	u32 reg = 0;
	enum pixel_enc_type pix_enc;
	struct video_params *vparams;

	vparams = &dptx->vparams;
	pix_enc = vparams->pix_enc;
	reg = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VG_CONFIG1_N(stream));

	if (pix_enc == YCBCR420 || pix_enc  == YCBCR422) {
		reg |= DPTX_VG_CONFIG1_YCC_PATTERN_GEN_EN;
		if (pix_enc == YCBCR420) {
		//	reg |= DPTX_VG_CONFIG1_YCC_420_EN; //sahakyan
			reg &= ~DPTX_VG_CONFIG1_YCC_422_EN;
		} else if (pix_enc == YCBCR422) {
			reg |= DPTX_VG_CONFIG1_YCC_422_EN;
			reg &= ~DPTX_VG_CONFIG1_YCC_420_EN;
		}
	} else {
		reg &= ~DPTX_VG_CONFIG1_YCC_PATTERN_GEN_EN;
		reg &= ~DPTX_VG_CONFIG1_YCC_420_EN;
		reg &= ~DPTX_VG_CONFIG1_YCC_422_EN;
	}
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_CONFIG1_N(stream), reg);
}

static int dptx_calculate_hblank_interval(struct dptx *dptx)
{
	struct video_params *vparams;
	int pixel_clk;
	u16 h_blank;
	u32 link_clk;
	u8 rate;
	int hblank_interval;

	vparams = &dptx->vparams;
	pixel_clk = vparams->mdtd.pixel_clock;
	h_blank = vparams->mdtd.h_blanking;
	rate = dptx->link.rate;

	switch (rate) {
	case DPTX_PHYIF_CTRL_RATE_RBR:
		link_clk = 40500;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR:
		link_clk = 67500;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR2:
		link_clk = 135000;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR3:
		link_clk = 202500;
		break;
	default:
		WARN(1, "Invalid rate 0x%x\n", rate);
		return -EINVAL;
	}

	hblank_interval = h_blank * link_clk / pixel_clk;

	return hblank_interval;
}

void dptx_video_core_config(struct dptx *dptx, int stream)
{
	u32 reg = 0;
	u8 vmode;

	struct video_params *vparams;
	struct dtd *mdtd;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;
	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;
	vmode = vparams->mode;

	dptx_video_set_core_bpc(dptx, stream);

	/* Single, dual, or quad pixel */
	reg = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_CTRL_N(stream));
	reg &= ~DPTX_VSAMPLE_CTRL_MULTI_PIXEL_MASK;
	reg |= dptx->multipixel << DPTX_VSAMPLE_CTRL_MULTI_PIXEL_SHIFT;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_CTRL_N(stream), reg);

	/* Configure DPTX_VSAMPLE_POLARITY_CTRL register */
	reg = 0;

	if (mdtd->h_sync_polarity == 1)
		reg |= DPTX_POL_CTRL_H_SYNC_POL_EN;
	if (mdtd->v_sync_polarity == 1)
		reg |= DPTX_POL_CTRL_V_SYNC_POL_EN;

	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_POLARITY_CTRL_N(stream), reg);

	reg = 0;

	/* Configure video_config1 register */
	if (vparams->video_format == VCEA) {
		if (vmode == 5 || vmode == 6 || vmode == 7 ||
		    vmode == 10 || vmode == 11 || vmode == 20 ||
		    vmode == 21 || vmode == 22 || vmode == 39 ||
		    vmode == 25 || vmode == 26 || vmode == 40 ||
		    vmode == 44 || vmode == 45 || vmode == 46 ||
		    vmode == 50 || vmode == 51 || vmode == 54 ||
		    vmode == 55 || vmode == 58 || vmode  == 59)
			reg |= DPTX_VIDEO_CONFIG1_IN_OSC_EN;
	}

	if (mdtd->interlaced == 1)
		reg |= DPTX_VIDEO_CONFIG1_O_IP_EN;

	reg |= mdtd->h_active << DPTX_VIDEO_H_ACTIVE_SHIFT;
	reg |= mdtd->h_blanking << DPTX_VIDEO_H_BLANK_SHIFT;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_CONFIG1_N(stream), reg);

	/* Configure video_config2 register */
	reg = 0;
	reg |= mdtd->v_active << DPTX_VIDEO_V_ACTIVE_SHIFT;
	reg |= mdtd->v_blanking << DPTX_VIDEO_V_BLANK_SHIFT;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_CONFIG2_N(stream), reg);

	/* Configure video_config3 register */
	reg = 0;
	reg |= mdtd->h_sync_offset << DPTX_VIDEO_H_FRONT_PORCH;
	reg |= mdtd->h_sync_pulse_width << DPTX_VIDEO_H_SYNC_WIDTH;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_CONFIG3_N(stream), reg);

	/* Configure video_config4 register */
	reg = 0;
	reg |= mdtd->v_sync_offset << DPTX_VIDEO_V_FRONT_PORCH;
	reg |= mdtd->v_sync_pulse_width << DPTX_VIDEO_V_SYNC_WIDTH;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_CONFIG4_N(stream), reg);

	/* Configure video_config5 register */
	dptx_video_ts_change(dptx, stream);

	/* Configure video_msa1 register */
	reg = 0;
	reg |= (mdtd->h_blanking - mdtd->h_sync_offset)
		<< DPTX_VIDEO_MSA1_H_START_SHIFT;
	reg |= (mdtd->v_blanking - mdtd->v_sync_offset)
		<< DPTX_VIDEO_MSA1_V_START_SHIFT;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA1_N(stream), reg);

	dptx_video_set_sink_bpc(dptx, stream);

	reg = dptx_calculate_hblank_interval(dptx);
	dptx_write_regfield(dptx, ctrl_fields->field_hblank_interval, reg);
}

int dptx_get_vc_payload_size(struct dptx *dptx)
{
	struct video_params *vparams;
	int vc_payload_size, ts_int, ts_frac;
	int peak_stream_bandwidth;
	int link_bandwidth;
	int link_rate, bpp;

	vparams = &dptx->vparams;

	switch (dptx->link.rate) {
	case DPTX_PHYIF_CTRL_RATE_RBR:
		link_rate = 162;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR:
		link_rate = 270;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR2:
		link_rate = 540;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR3:
		link_rate = 810;
		break;
	default:
		link_rate = 162;
	}

	switch (vparams->bpc) {
	case COLOR_DEPTH_6:
		bpp = 18;
		break;
	case COLOR_DEPTH_8:
		if (vparams->pix_enc == YCBCR420)
			bpp  = 12;
		else if (vparams->pix_enc == YCBCR422)
			bpp = 16;
		else if (vparams->pix_enc == YONLY)
			bpp = 8;
		else
			bpp = 24;
		break;
	case COLOR_DEPTH_10:
		if (vparams->pix_enc == YCBCR420)
			bpp = 15;
		else if (vparams->pix_enc == YCBCR422)
			bpp = 20;
		else if (vparams->pix_enc  == YONLY)
			bpp = 10;
		else
			bpp = 30;
		break;

	case COLOR_DEPTH_12:
		if (vparams->pix_enc == YCBCR420)
			bpp = 18;
		else if (vparams->pix_enc == YCBCR422)
			bpp = 24;
		else if (vparams->pix_enc == YONLY)
			bpp = 12;
		else
			bpp = 36;
		break;

	case COLOR_DEPTH_16:
		if (vparams->pix_enc == YCBCR420)
			bpp = 24;
		else if (vparams->pix_enc == YCBCR422)
			bpp = 32;
		else if (vparams->pix_enc == YONLY)
			bpp = 16;
		else
			bpp = 48;
		break;
	default:
		bpp = 18;
		break;
	}

	peak_stream_bandwidth =  (vparams->mdtd.pixel_clock * bpp) / (8 * 1000);
	link_bandwidth = link_rate * dptx->link.lanes;
	vc_payload_size = DIV_ROUND_UP_ULL(64 * peak_stream_bandwidth, link_bandwidth);
	ts_int = DIV_ROUND_DOWN_ULL(64 * peak_stream_bandwidth, link_bandwidth);
	ts_frac = ((64 * 100 * peak_stream_bandwidth / link_bandwidth) - ts_int * 100);
	vparams->aver_bytes_per_tu = ts_int;
	vparams->aver_bytes_per_tu_frac = ts_frac;

	return vc_payload_size;
}

int dptx_video_ts_calculate(struct dptx *dptx, int lane_num, int rate,
			    int bpc, int encoding, int pixel_clock)
{
	struct video_params *vparams;
	struct dtd *mdtd;
	u32 link_rate;
	u32 link_clk;
	u32 color_dep;
	u32 ts;
	u32 tu;
	u32 tu_frac;
	u32 T1;
	u32 T2;
	u32 T3;

	if (lane_num <= 0)
		return -EINVAL;

	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;

	switch (rate) {
	case DPTX_PHYIF_CTRL_RATE_RBR:
		link_rate = 162;
		link_clk = 40500;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR:
		link_rate = 270;
		link_clk = 67500;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR2:
		link_rate = 540;
		link_clk = 135000;
		break;
	case DPTX_PHYIF_CTRL_RATE_HBR3:
		link_rate = 810;
		link_clk = 202500;
		break;
	default:    //sahakyan
		link_rate = 162;
		link_clk = 40500;
	}

	switch (bpc) {
	case COLOR_DEPTH_6:
		color_dep = 18;
		break;
	case COLOR_DEPTH_8:
		if (encoding == YCBCR420)
			color_dep  = 12;
		else if (encoding == YCBCR422)
			color_dep = 16;
		else if (encoding == YONLY)
			color_dep = 8;
		else
			color_dep = 24;
		break;
	case COLOR_DEPTH_10:
		if (encoding == YCBCR420)
			color_dep = 15;
		else if (encoding == YCBCR422)
			color_dep = 20;
		else if (encoding  == YONLY)
			color_dep = 10;
		else
			color_dep = 30;
		break;

	case COLOR_DEPTH_12:
		if (encoding == YCBCR420)
			color_dep = 18;
		else if (encoding == YCBCR422)
			color_dep = 24;
		else if (encoding == YONLY)
			color_dep = 12;
		else
			color_dep = 36;
		break;

	case COLOR_DEPTH_16:
		if (encoding == YCBCR420)
			color_dep = 24;
		else if (encoding == YCBCR422)
			color_dep = 32;
		else if (encoding == YONLY)
			color_dep = 16;
		else
			color_dep = 48;
		break;
	default:
		color_dep = 18;
		break;
	}

	ts = (8 * color_dep * pixel_clock) / (lane_num * link_rate);

	tu  = ts / 1000;
	if (tu >= 65) {
		dptx_dbg(dptx, "%s: tu(%d) > 65",
			 __func__, tu);
		return -EINVAL;
	}

	tu_frac = ts / 100 - tu * 10;

	// Calculate init_threshold for non-DSC mode
	if (dptx->multipixel == DPTX_MP_SINGLE_PIXEL) {
		// Single-Pixel Mode
		if (tu < 6)
			vparams->init_threshold = 32;
		else if (mdtd->h_blanking <= 40 && encoding == YCBCR420)
			vparams->init_threshold = 3;
		else if (mdtd->h_blanking <= 80  && encoding != YCBCR420)
			vparams->init_threshold = 12;
		else
			vparams->init_threshold = 16;
	} else {
		// Multi-Pixel Mode
		switch (bpc) {
		case COLOR_DEPTH_6:
			T1 = (4 * 1000 * lane_num) / 9;
			break;
		case COLOR_DEPTH_8:
			if (encoding == YCBCR422)
				T1 = (1000 / 2) * lane_num;
			else if (encoding == YONLY)
				T1 = lane_num * 1000;
			else
				if (dptx->multipixel == DPTX_MP_DUAL_PIXEL)
					T1 = (1000 * lane_num) / 3;
				else
					T1 = (3000 * lane_num) / 16;
			break;
		case COLOR_DEPTH_10:
			if (encoding == YCBCR422)
				T1 = (2000 / 5) * lane_num;
			else if (encoding == YONLY)
				T1 = (4000 / 5) * lane_num;
			else
				T1 = (4000 * lane_num) / 15;
			break;
		case COLOR_DEPTH_12:
			if (encoding == YCBCR422)	//Nothing happens here
				if (dptx->multipixel == DPTX_MP_DUAL_PIXEL)
					T1 = (1000 / 6) * lane_num;
				else
					T1 = (1000 / 3) * lane_num;
			else if (encoding == YONLY)
				T1 = (2000 / 3) * lane_num;
			else
				T1 = (2000 / 9) * lane_num;
			break;
		case COLOR_DEPTH_16:
			if (encoding == YONLY)
				T1 = (1000 / 2) * lane_num;
			if ((encoding != YONLY) && (encoding != YCBCR422) && (dptx->multipixel == DPTX_MP_DUAL_PIXEL))
				T1 = (1000 / 6) * lane_num;
			else
				T1 = (1000 / 4) * lane_num;
			break;
		default:
			dptx_dbg(dptx, "Invalid param BPC = %d\n", bpc);
			return -EINVAL;
		}

		if (encoding == YCBCR420)
			pixel_clock = pixel_clock / 2;

		T2 = (link_clk * 1000 /  pixel_clock);

		T3 = (tu_frac > 0) ? (tu + 1) : tu;
		if (dptx->link.fec)
			T3 += (lane_num == 1) ? 13 : 7;

		dptx_dbg_link(dptx, "T1 = %d, T2 = %d, T3 = %d\n", T1, T2, T3);

		vparams->init_threshold = T1 * T2 * T3 / (1000 * 1000);

		if (vparams->init_threshold <= 16 || tu < 10)
			vparams->init_threshold = 40;
	}

	dptx_dbg_link(dptx, "tu = %d, tu_frac = %d, init_threshold = %d\n",
		      tu, tu_frac, vparams->init_threshold);

	vparams->aver_bytes_per_tu = tu;
	vparams->aver_bytes_per_tu_frac = tu_frac;

	if (dptx->mst) {
		u32 tu_mst;
		u32 tu_frac_mst;

		int numerator;
		int denominator;
		s64 fixp;

		dptx_dbg(dptx, "MST: pixel_clock=%d\n", mdtd->pixel_clock);
		numerator = 25175 * 3 * 10;
		dptx_dbg(dptx, "MST: numerator=%d\n", numerator);
		denominator = (link_rate) * lane_num * 100 * 1000 / 10;
		dptx_dbg(dptx, "MST: denominator=%d\n", denominator);
		fixp = drm_fixp_from_fraction(numerator * 64, denominator);
		tu_mst = drm_fixp2int(fixp);

		fixp &= DRM_FIXED_DECIMAL_MASK;
		fixp *= 64;
		tu_frac_mst = drm_fixp2int(fixp);

		dptx_dbg(dptx, "MST: tu = %d, tu_frac = %d\n", tu_mst, tu_frac_mst);
		vparams->aver_bytes_per_tu = tu_mst;
		vparams->aver_bytes_per_tu_frac = tu_frac_mst;

		/* TODO this is a duplicate calculation from above */
		if (tu_mst < 6) {
			vparams->init_threshold = 32;
		} else if ((encoding == RGB || encoding == YCBCR444) &&
			 mdtd->h_blanking <= 80) {
			if (dptx->multipixel == DPTX_MP_QUAD_PIXEL)
				vparams->init_threshold = 4;
			else
				vparams->init_threshold = 12;
		} else {
			vparams->init_threshold = 15;
		}
	}

	return 0;
}

void dptx_video_ts_change(struct dptx *dptx, int stream)
{
	u32 reg;
	struct video_params *vparams;

	vparams = &dptx->vparams;

	reg = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_CONFIG5_N(stream));
	reg = reg & (~DPTX_VIDEO_CONFIG5_TU_MASK);
	reg = reg | (vparams->aver_bytes_per_tu <<
			DPTX_VIDEO_CONFIG5_TU_SHIFT);
	if (dptx->mst) {
		reg = reg & (~DPTX_VIDEO_CONFIG5_TU_FRAC_MASK_MST);
		reg = reg | (vparams->aver_bytes_per_tu_frac <<
			     DPTX_VIDEO_CONFIG5_TU_FRAC_SHIFT_MST);
	} else {
		reg = reg & (~DPTX_VIDEO_CONFIG5_TU_FRAC_MASK_SST);
		reg = reg | (vparams->aver_bytes_per_tu_frac <<
			     DPTX_VIDEO_CONFIG5_TU_FRAC_SHIFT_SST);
	}
	reg = reg & (~DPTX_VIDEO_CONFIG5_INIT_THRESHOLD_MASK);
	reg = reg | (vparams->init_threshold <<
		      DPTX_VIDEO_CONFIG5_INIT_THRESHOLD_SHIFT);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_CONFIG5_N(stream), reg);
}

void dptx_video_bpc_change(struct dptx *dptx, int stream)
{
	dptx_video_set_core_bpc(dptx, stream);
	dptx_video_set_sink_bpc(dptx, stream);
	dptx_video_set_gen_bpc(dptx, stream);
}

void dptx_video_set_core_bpc(struct dptx *dptx, int stream)
{
	u32 reg;
	u8 bpc_mapping = 0, bpc = 0;
	enum pixel_enc_type pix_enc;
	struct video_params *vparams;

	vparams = &dptx->vparams;
	bpc = vparams->bpc;
	pix_enc = vparams->pix_enc;

	reg = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_CTRL_N(stream));
	reg &= ~DPTX_VSAMPLE_CTRL_VMAP_BPC_MASK;

	switch (pix_enc) {
	case RGB:
		if (bpc == COLOR_DEPTH_6)
			bpc_mapping = 0;
		else if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YCBCR444:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 5;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 6;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 7;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 8;
		break;
	case YCBCR422:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 9;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 10;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 11;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 12;
		break;
	case YCBCR420:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 13;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 14;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 15;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 16;
		break;
	case YONLY:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 17;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 18;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 19;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 20;
		break;
	case RAW:
		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 23;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 24;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 25;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 27;
		break;
	}

	reg |= (bpc_mapping << DPTX_VSAMPLE_CTRL_VMAP_BPC_SHIFT);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_CTRL_N(stream), reg);
}

void dptx_video_set_gen_bpc(struct dptx *dptx, int stream)
{
	u32 reg;
	u8 bpc_mapping = 0, bpc = 0;
	struct video_params *vparams;

	vparams = &dptx->vparams;
	bpc = vparams->bpc;
	reg = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VG_CONFIG1_N(stream));
	reg &= ~DPTX_VG_CONFIG1_BPC_MASK;

	switch (bpc) {
	case COLOR_DEPTH_6:
		bpc_mapping = 0;
		break;
	case COLOR_DEPTH_8:
		bpc_mapping = 1;
		break;
	case COLOR_DEPTH_10:
		bpc_mapping = 2;
		break;
	case COLOR_DEPTH_12:
		bpc_mapping = 3;
		break;
	case COLOR_DEPTH_16:
		bpc_mapping = 4;
		break;
	}

	reg |= (bpc_mapping << DPTX_VG_CONFIG1_BPC_SHIFT);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_CONFIG1_N(stream), reg);
}

void dptx_video_set_sink_col(struct dptx *dptx, int stream)
{
	u32 reg_msa2;
	u8 col_mapping;
	u8 colorimetry;
	u8 dynamic_range;
	struct video_params *vparams;
	enum pixel_enc_type pix_enc;

	vparams = &dptx->vparams;
	pix_enc = vparams->pix_enc;
	colorimetry = vparams->colorimetry;
	dynamic_range = vparams->dynamic_range;

	reg_msa2 = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA2_N(stream));
	reg_msa2 &= ~DPTX_VIDEO_VMSA2_COL_MASK;

	col_mapping = 0;

	/* According to Table 2-94 of DisplayPort spec 1.3 */
	switch (pix_enc) {
	case RGB:
		if (dynamic_range == CEA)
			col_mapping = 4;
		else if (dynamic_range == VESA)
			col_mapping = 0;
		break;
	case YCBCR422:
		if (colorimetry == ITU601)
			col_mapping = 5;
		else if (colorimetry == ITU709)
			col_mapping = 13;
		break;
	case YCBCR444:
		if (colorimetry == ITU601)
			col_mapping = 6;
		else if (colorimetry == ITU709)
			col_mapping = 14;
		break;
	case RAW:
		col_mapping = 1;
		break;
	case YCBCR420:
	case YONLY:
		break;
	}

	reg_msa2 |= (col_mapping << DPTX_VIDEO_VMSA2_COL_SHIFT);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA2_N(stream), reg_msa2);
}

void dptx_video_set_sink_bpc(struct dptx *dptx, int stream)
{
	u32 reg_msa2, reg_msa3;
	u8 bpc_mapping = 0, bpc = 0;
	struct video_params *vparams;
	enum pixel_enc_type pix_enc;

	vparams = &dptx->vparams;
	pix_enc = vparams->pix_enc;
	bpc = vparams->bpc;

	reg_msa2 = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA2_N(stream));
	reg_msa3 = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA3_N(stream));

	reg_msa2 &= ~DPTX_VIDEO_VMSA2_BPC_MASK;
	reg_msa3 &= ~DPTX_VIDEO_VMSA3_PIX_ENC_MASK;

	switch (pix_enc) {
	case RGB:

		if (bpc == COLOR_DEPTH_6)
			bpc_mapping = 0;
		else if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YCBCR444:

		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YCBCR422:

		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YCBCR420:
		reg_msa3 |= 1 << DPTX_VIDEO_VMSA3_PIX_ENC_YCBCR420_SHIFT;
		break;
	case YONLY:

		/* According to Table 2-94 of DisplayPort spec 1.3 */
		reg_msa3 |= 1 << DPTX_VIDEO_VMSA3_PIX_ENC_SHIFT;

		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case RAW:

		 /* According to Table 2-94 of DisplayPort spec 1.3 */
		reg_msa3 |= (1 << DPTX_VIDEO_VMSA3_PIX_ENC_SHIFT);

		if (bpc == COLOR_DEPTH_6)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 3;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 4;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 5;
		else if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 7;
		break;
	}

	reg_msa2 |= (bpc_mapping << DPTX_VIDEO_VMSA2_BPC_SHIFT);

	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA2_N(stream), reg_msa2);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA3_N(stream), reg_msa3);

	dptx_video_set_sink_col(dptx, stream);
}

void dptx_video_set_timing_info(struct dptx *dptx, int stream)
{
	u32 reg = 0;
	u8 vmode;

	struct video_params *vparams;
	struct dtd *mdtd;

	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;
	vmode = vparams->mode;

	dptx_video_set_core_bpc(dptx, stream);

	/* Single, dual, or quad pixel */
	reg = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_CTRL_N(stream));
	reg &= ~DPTX_VSAMPLE_CTRL_MULTI_PIXEL_MASK;
	reg |= dptx->multipixel << DPTX_VSAMPLE_CTRL_MULTI_PIXEL_SHIFT;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_CTRL_N(stream), reg);

	/* Configure DPTX_VSAMPLE_POLARITY_CTRL register */
	reg = 0;

	if (mdtd->h_sync_polarity == 1)
		reg |= DPTX_POL_CTRL_H_SYNC_POL_EN;
	if (mdtd->v_sync_polarity == 1)
		reg |= DPTX_POL_CTRL_V_SYNC_POL_EN;

	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_POLARITY_CTRL_N(stream), reg);

	reg = 0;

	/* Configure video_config1 register */
	if (vparams->video_format == VCEA) {
		if (vmode == 5 || vmode == 6 || vmode == 7 ||
		    vmode == 10 || vmode == 11 || vmode == 20 ||
		    vmode == 21 || vmode == 22 || vmode == 39 ||
		    vmode == 25 || vmode == 26 || vmode == 40 ||
		    vmode == 44 || vmode == 45 || vmode == 46 ||
		    vmode == 50 || vmode == 51 || vmode == 54 ||
		    vmode == 55 || vmode == 58 || vmode  == 59)
			reg |= DPTX_VIDEO_CONFIG1_IN_OSC_EN;
	}

	if (mdtd->interlaced == 1)
		reg |= DPTX_VIDEO_CONFIG1_O_IP_EN;

	reg |= mdtd->h_active << DPTX_VIDEO_H_ACTIVE_SHIFT;
	reg |= mdtd->h_blanking << DPTX_VIDEO_H_BLANK_SHIFT;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_CONFIG1_N(stream), reg);

	/* Configure video_config2 register */
	reg = 0;
	reg |= mdtd->v_active << DPTX_VIDEO_V_ACTIVE_SHIFT;
	reg |= mdtd->v_blanking << DPTX_VIDEO_V_BLANK_SHIFT;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_CONFIG2_N(stream), reg);

	/* Configure video_config3 register */
	reg = 0;
	reg |= mdtd->h_sync_offset << DPTX_VIDEO_H_FRONT_PORCH;
	reg |= mdtd->h_sync_pulse_width << DPTX_VIDEO_H_SYNC_WIDTH;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_CONFIG3_N(stream), reg);

	/* Configure video_config4 register */
	reg = 0;
	reg |= mdtd->v_sync_offset << DPTX_VIDEO_V_FRONT_PORCH;
	reg |= mdtd->v_sync_pulse_width << DPTX_VIDEO_V_SYNC_WIDTH;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_CONFIG4_N(stream), reg);

	/* Configure video_config5 register */
	dptx_video_ts_change(dptx, stream);
}

void dptx_video_set_MSA(struct dptx *dptx, int stream)
{
	u32 reg;
	u32 reg_msa2, reg_msa3;
	u8 bpc_mapping = 0, bpc = 0;
	u8 col_mapping;
	u8 colorimetry;
	u8 dynamic_range;
	struct video_params *vparams;
	struct dtd *mdtd;
	enum pixel_enc_type pix_enc;

	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;
	vparams = &dptx->vparams;
	pix_enc = vparams->pix_enc;
	bpc = vparams->bpc;
	colorimetry = vparams->colorimetry;
	dynamic_range = vparams->dynamic_range;


	/* Configure video_msa1 register */
	reg = 0;
	reg |= (mdtd->h_blanking - mdtd->h_sync_offset)
		<< DPTX_VIDEO_MSA1_H_START_SHIFT;
	reg |= (mdtd->v_blanking - mdtd->v_sync_offset)
		<< DPTX_VIDEO_MSA1_V_START_SHIFT;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA1_N(stream), reg);

	reg_msa2 = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA2_N(stream));
	reg_msa3 = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA3_N(stream));

	reg_msa2 &= ~DPTX_VIDEO_VMSA2_BPC_MASK;
	reg_msa3 &= ~DPTX_VIDEO_VMSA3_PIX_ENC_MASK;

	switch (pix_enc) {
	case RGB:

		if (bpc == COLOR_DEPTH_6)
			bpc_mapping = 0;
		else if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YCBCR444:

		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YCBCR422:

		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case YCBCR420:
		reg_msa3 |= 1 << DPTX_VIDEO_VMSA3_PIX_ENC_YCBCR420_SHIFT;
		break;
	case YONLY:

		/* According to Table 2-94 of DisplayPort spec 1.3 */
		reg_msa3 |= 1 << DPTX_VIDEO_VMSA3_PIX_ENC_SHIFT;

		if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 2;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 3;
		if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 4;
		break;
	case RAW:

		 /* According to Table 2-94 of DisplayPort spec 1.3 */
		reg_msa3 |= (1 << DPTX_VIDEO_VMSA3_PIX_ENC_SHIFT);

		if (bpc == COLOR_DEPTH_6)
			bpc_mapping = 1;
		else if (bpc == COLOR_DEPTH_8)
			bpc_mapping = 3;
		else if (bpc == COLOR_DEPTH_10)
			bpc_mapping = 4;
		else if (bpc == COLOR_DEPTH_12)
			bpc_mapping = 5;
		else if (bpc == COLOR_DEPTH_16)
			bpc_mapping = 7;
		break;
	}

	reg_msa2 |= (bpc_mapping << DPTX_VIDEO_VMSA2_BPC_SHIFT);

	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA2_N(stream), reg_msa2);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA3_N(stream), reg_msa3);

	reg_msa2 = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA2_N(stream));
	reg_msa2 &= ~DPTX_VIDEO_VMSA2_COL_MASK;

	col_mapping = 0;

	/* According to Table 2-94 of DisplayPort spec 1.3 */
	switch (pix_enc) {
	case RGB:
		if (dynamic_range == CEA)
			col_mapping = 4;
		else if (dynamic_range == VESA)
			col_mapping = 0;
		break;
	case YCBCR422:
		if (colorimetry == ITU601)
			col_mapping = 5;
		else if (colorimetry == ITU709)
			col_mapping = 13;
		break;
	case YCBCR444:
		if (colorimetry == ITU601)
			col_mapping = 6;
		else if (colorimetry == ITU709)
			col_mapping = 14;
		break;
	case RAW:
		col_mapping = 1;
		break;
	case YCBCR420:
	case YONLY:
		break;
	}

	reg_msa2 |= (col_mapping << DPTX_VIDEO_VMSA2_COL_SHIFT);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VIDEO_MSA2_N(stream), reg_msa2);
}

static void dptx_set_ram_ctr(struct dptx *dptx, int stream)
{
	u32 reg;

	reg = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VG_RAM_ADDR_N(stream));
	reg &= ~DPTX_VG_RAM_ADDR_START_MASK;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_RAM_ADDR_N(stream), reg);

	reg = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VG_WRT_RAM_CTR_N(stream));
	reg |= DPTX_VG_RAM_CTR_START_MASK;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_WRT_RAM_CTR_N(stream), reg);

	reg = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VG_WRT_RAM_CTR_N(stream));
	reg &= ~DPTX_VG_RAM_CTR_START_MASK;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_WRT_RAM_CTR_N(stream), reg);
}

static void dptx_set_ram_data(struct dptx *dptx, u32 value, int count, int stream)
{
	int i;

	for (i = 0; i < count; i++)
		dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_WRT_RAM_DATA_N(stream), value);
}

static void dptx_set_ramp_pattern(struct dptx *dptx, int column_num, int stream)
{
	int i, step, range, shift1, shift2, value;
	u8 bpc, mult;
	struct video_params *vparams;

	vparams = &dptx->vparams;
	bpc = vparams->bpc;

	dptx_set_ram_ctr(dptx, stream);

	switch (dptx->multipixel) {
	default:
	case DPTX_MP_SINGLE_PIXEL:
		dptx_set_ram_data(dptx, 0, 6, stream);
		break;
	case DPTX_MP_DUAL_PIXEL:
		dptx_set_ram_data(dptx, 0, 12, stream);
		break;
	case DPTX_MP_QUAD_PIXEL:
		dptx_set_ram_data(dptx, 0, 24, stream);
		break;
	}

	range = 0;
	shift1 = 0;
	shift2 = 0;
	step = 0;
	value = 0;
	mult = 1;

	if (bpc == COLOR_DEPTH_6 || bpc == COLOR_DEPTH_8) {
		if (!dptx->link.dsc) {
			if (bpc == COLOR_DEPTH_6)
				mult = 4;
		}

		/* Program RED color */
		for (i = 0; i < column_num; i++) {
			dptx_set_ram_data(dptx, i * mult, 1, stream);
			dptx_set_ram_data(dptx, 0, 5, stream);
		}
		/* Program GREEN color */
		for (i = 0; i < column_num; i++) {
			dptx_set_ram_data(dptx, 0, 2, stream);
			dptx_set_ram_data(dptx, i * mult, 1, stream);
			dptx_set_ram_data(dptx, 0, 3, stream);
		}
		/* Program BLUE color */
		for (i = 0; i < column_num; i++) {
			dptx_set_ram_data(dptx, 0, 4, stream);
			dptx_set_ram_data(dptx, i * mult, 1, stream);
			dptx_set_ram_data(dptx, 0, 1, stream);
		}
		/* Program WHITE color */
		for (i = 0; i < column_num; i++) {
			dptx_set_ram_data(dptx, i * mult, 1, stream);
			dptx_set_ram_data(dptx, 0, 1, stream);
			dptx_set_ram_data(dptx, i * mult, 1, stream);
			dptx_set_ram_data(dptx, 0, 1, stream);
			dptx_set_ram_data(dptx, i * mult, 1, stream);
			dptx_set_ram_data(dptx, 0, 1, stream);
		}
	} else if (bpc == COLOR_DEPTH_10 || bpc == COLOR_DEPTH_12 ||
		   bpc == COLOR_DEPTH_16) {
		if (bpc == COLOR_DEPTH_10) {
			range = 384;
			shift1 = 2;
			shift2 = 6;
			step = 4;
		} else if (bpc == COLOR_DEPTH_12) {
			range = 1920;
			shift1 = 1;
			shift2 = 7;
			step = 16;
		} else if (bpc == COLOR_DEPTH_16) {
			range = 32640;
			shift1 = 0;
			shift2 = 8;
			step = 256;
		}

		for (i = 0; i < column_num; i++) {
			value = range + i;
			dptx_set_ram_data(dptx, (value >> shift1), 1, stream);
			dptx_set_ram_data(dptx, (value << shift2), 1, stream);
			dptx_set_ram_data(dptx, 0, 4, stream);
		}
		for (i = 0; i < column_num * step; i = i + step) {
			dptx_set_ram_data(dptx, (i >> shift1), 1, stream);
			dptx_set_ram_data(dptx, (i << shift2), 1, stream);
			dptx_set_ram_data(dptx, 0, 4, stream);
		}
		for (i = 0; i < column_num; i++) {
			value = range + i;
			dptx_set_ram_data(dptx, 0, 2, stream);
			dptx_set_ram_data(dptx, (value >> shift1), 1, stream);
			dptx_set_ram_data(dptx, (value << shift2), 1, stream);
			dptx_set_ram_data(dptx, 0, 2, stream);
		}
		for (i = 0; i < column_num * step; i = i + step) {
			dptx_set_ram_data(dptx, 0, 2, stream);
			dptx_set_ram_data(dptx, (i >> shift1), 1, stream);
			dptx_set_ram_data(dptx, (i << shift2), 1, stream);
			dptx_set_ram_data(dptx, 0, 2, stream);
		}
		for (i = 0; i < column_num; i++) {
			value = range + i;
			dptx_set_ram_data(dptx, 0, 4, stream);
			dptx_set_ram_data(dptx, (value >> shift1), 1, stream);
			dptx_set_ram_data(dptx, (value << shift2), 1, stream);
		}
		for (i = 0; i < column_num * step; i = i + step) {
			dptx_set_ram_data(dptx, 0, 4, stream);
			dptx_set_ram_data(dptx, (i >> shift1), 1, stream);
			dptx_set_ram_data(dptx, (i << shift2), 1, stream);
		}
		for (i = 0; i < column_num; i++) {
			value = range + i;
			dptx_set_ram_data(dptx, (value >> shift1), 1, stream);
			dptx_set_ram_data(dptx, (value << shift2), 1, stream);
			dptx_set_ram_data(dptx, (value >> shift1), 1, stream);
			dptx_set_ram_data(dptx, (value << shift2), 1, stream);
			dptx_set_ram_data(dptx, (value >> shift1), 1, stream);
			dptx_set_ram_data(dptx, (value << shift2), 1, stream);
		}
		for (i = 0; i < column_num * step; i = i + step) {
			dptx_set_ram_data(dptx, (i >> shift1), 1, stream);
			dptx_set_ram_data(dptx, (i << shift2), 1, stream);
			dptx_set_ram_data(dptx, (i >> shift1), 1, stream);
			dptx_set_ram_data(dptx, (i << shift2), 1, stream);
			dptx_set_ram_data(dptx, (i >> shift1), 1, stream);
			dptx_set_ram_data(dptx, (i << shift2), 1, stream);
		}
	}
}

static void dptx_set_col_ramp_pattern(struct dptx *dptx, int stream)
{
	int i, j;
	u8 ram_pix_cnt;
	static int white[3] = {235, 235, 235};
	static int yellow[3] = {235, 235, 16};
	static int cyan[3] = {16, 235, 235};
	static int green[3] = {16, 235, 16};
	static int magenta[3] = {235, 16, 235};
	static int red[3] = {235, 16, 16};
	static int blue[3] = {16, 16, 235};
	static int black[3] = {16, 16, 16};
	static int *colors_row1[8] = {
		white, yellow, cyan, green,
		magenta, red, blue, black
	};
	static int *colors_row2[8] = {
		blue, red, magenta, green,
		cyan, yellow, white, black
	};

	dptx_set_ram_ctr(dptx, stream);

	switch (dptx->multipixel) {
	default:
	case DPTX_MP_SINGLE_PIXEL:
		dptx_set_ram_data(dptx, 0, 6, stream);
		ram_pix_cnt = 1;
		break;
	case DPTX_MP_DUAL_PIXEL:
		dptx_set_ram_data(dptx, 0, 12, stream);
		ram_pix_cnt = 2;
		break;
	case DPTX_MP_QUAD_PIXEL:
		dptx_set_ram_data(dptx, 0, 24, stream);
		ram_pix_cnt = 4;
		break;
	}

	for (i = 0; i < 8; i++) {
		for (j = 0; j < ram_pix_cnt; j++) {
			dptx_set_ram_data(dptx, colors_row1[i][0], 1, stream);
			dptx_set_ram_data(dptx, 0, 1, stream);
			dptx_set_ram_data(dptx, colors_row1[i][1], 1, stream);
			dptx_set_ram_data(dptx, 0, 1, stream);
			dptx_set_ram_data(dptx, colors_row1[i][2], 1, stream);
			dptx_set_ram_data(dptx, 0, 1, stream);
		}
	}
	for (i = 0; i < 8; i++) {
		for (j = 0; j < ram_pix_cnt; j++) {
			dptx_set_ram_data(dptx, colors_row2[i][0], 1, stream);
			dptx_set_ram_data(dptx, 0, 1, stream);
			dptx_set_ram_data(dptx, colors_row2[i][1], 1, stream);
			dptx_set_ram_data(dptx, 0, 1, stream);
			dptx_set_ram_data(dptx, colors_row2[i][2], 1, stream);
			dptx_set_ram_data(dptx, 0, 1, stream);
		}
	}
}

static void dptx_set_col_ycbcr_ramp_pattern(struct dptx *dptx, int stream)
{
	struct video_params *vparams;
	u8 colorimetry, bpc, shift1, shift2;
	int i, j, mult;
	u8 ram_pix_cnt;

	static int white[3] = {235, 128, 128};
	static int yellow[3] = {210, 16, 146};
	static int cyan[3] = {170, 166, 16};
	static int green[3] = {145, 54, 34};
	static int magenta[3] = {106, 202, 222};
	static int red[3] = {81, 90, 240};
	static int blue[3] = {41, 240, 110};
	static int black[3] = {16, 128, 128};
	static int *colors_row1[8] = {
		white, yellow, cyan, green,
		magenta, red, blue, black
	};

	dptx_set_ram_ctr(dptx, stream);

	switch (dptx->multipixel) {
	default:
	case DPTX_MP_SINGLE_PIXEL:
		dptx_set_ram_data(dptx, 0, 6, stream);
		ram_pix_cnt = 1;
		break;
	case DPTX_MP_DUAL_PIXEL:
		dptx_set_ram_data(dptx, 0, 12, stream);
		ram_pix_cnt = 2;
		break;
	case DPTX_MP_QUAD_PIXEL:
		dptx_set_ram_data(dptx, 0, 24, stream);
		ram_pix_cnt = 4;
		break;
	}

	vparams = &dptx->vparams;
	colorimetry = vparams->colorimetry;
	bpc = vparams->bpc;

	if (colorimetry == ITU601) {
		if (bpc == COLOR_DEPTH_10) {
			white[0] = 940;
			white[1] = 512;
			white[2] = 512;
			yellow[0] = 840;
			yellow[1] = 64;
			yellow[2] = 585;
			cyan[0] = 678;
			cyan[1] = 663;
			cyan[2] = 64;
			green[0] = 578;
			green[1] = 215;
			green[2] = 137;
			magenta[0] = 426;
			magenta[1] = 809;
			magenta[2] = 887;
			red[0] = 326;
			red[1] = 361;
			red[2] = 960;
			blue[0] = 164;
			blue[1] = 960;
			blue[2] = 439;
			black[0] = 64;
			black[1] = 512;
			black[2] = 512;
		} else {
			white[0] = 235;
			white[1] = 128;
			white[2] = 128;
			yellow[0] = 210;
			yellow[1] = 16;
			yellow[2] = 146;
			cyan[0] = 170;
			cyan[1] = 166;
			cyan[2] = 16;
			green[0] = 145;
			green[1] = 54;
			green[2] = 34;
			magenta[0] = 106;
			magenta[1] = 202;
			magenta[2] = 222;
			red[0] = 81;
			red[1] = 90;
			red[2] = 240;
			blue[0] = 41;
			blue[1] = 240;
			blue[2] = 110;
			black[0] = 16;
			black[1] = 128;
			black[2] = 128;
		}
	} else {
		if (bpc == COLOR_DEPTH_10) {
			white[0] = 940;
			white[1] = 512;
			white[2] = 512;
			yellow[0] = 877;
			yellow[1] = 64;
			yellow[2] = 553;
			cyan[0] = 753;
			cyan[1] = 614;
			cyan[2] = 64;
			green[0] = 690;
			green[1] = 167;
			green[2] = 106;
			magenta[0] = 314;
			magenta[1] = 857;
			magenta[2] = 918;
			red[0] = 251;
			red[1] = 410;
			red[2] = 960;
			blue[0] = 127;
			blue[1] = 960;
			blue[2] = 471;
			black[0] = 64;
			black[1] = 512;
			black[2] = 512;
		} else {
			white[0] = 235;
			white[1] = 128;
			white[2] = 128;
			yellow[0] = 219;
			yellow[1] = 16;
			yellow[2] = 138;
			cyan[0] = 188;
			cyan[1] = 154;
			cyan[2] = 16;
			green[0] = 173;
			green[1] = 42;
			green[2] =  26;
			magenta[0] = 78;
			magenta[1] = 214;
			magenta[2] = 230;
			red[0] = 63;
			red[1] = 102;
			red[2] = 240;
			blue[0] = 32;
			blue[1] = 240;
			blue[2] = 118;
			black[0] = 16;
			black[1] = 128;
			black[2] = 128;
		}
	}

	if (bpc == COLOR_DEPTH_10) {
		shift1 = 2;
		shift2 = 6;
		mult = 1;
	} else if (bpc == COLOR_DEPTH_12) {
		shift1 = 4;
		shift2 = 4;
		mult = 16;
	} else if (bpc == COLOR_DEPTH_16) {
		shift1 = 8;
		shift2 = 0;
		mult = 16 * 16;
	} else {
		shift1 = 0;
		shift2 = 0;
		mult = 1;
	}

	colors_row1[0] = white;
	colors_row1[1] = yellow;
	colors_row1[2] = cyan;
	colors_row1[3] = green;
	colors_row1[4] = magenta;
	colors_row1[5] = red;
	colors_row1[6] = blue;
	colors_row1[7] = black;

	for (i = 0; i < 8; i++) {
		for (j = 0; j < ram_pix_cnt; j++) { // for quad/dual pixel the same pixel should be programmed 4/2 times
			dptx_set_ram_data(dptx, (colors_row1[i][0] * mult) >> shift1, 1, stream);
			dptx_set_ram_data(dptx, (colors_row1[i][0] * mult) << shift2, 1, stream);
			dptx_set_ram_data(dptx, (colors_row1[i][1] * mult) >> shift1, 1, stream);
			dptx_set_ram_data(dptx, (colors_row1[i][1] * mult) << shift2, 1, stream);
			dptx_set_ram_data(dptx, (colors_row1[i][2] * mult) >> shift1, 1, stream);
			dptx_set_ram_data(dptx, (colors_row1[i][2] * mult) << shift2, 1, stream);
		}
	}

	colors_row1[0] = blue;
	colors_row1[1] = red;
	colors_row1[2] = magenta;
	colors_row1[4] = cyan;
	colors_row1[5] = yellow;
	colors_row1[6] = white;
	colors_row1[7] = black;

	for (i = 0; i < 8; i++) {
		for (j = 0; j < ram_pix_cnt; j++) { // for quad/dual pixel the same pixel should be programmed 4/2 times
			dptx_set_ram_data(dptx, (colors_row1[i][0] * mult) >> shift1, 1, stream);
			dptx_set_ram_data(dptx, (colors_row1[i][0] * mult) << shift2, 1, stream);
			dptx_set_ram_data(dptx, (colors_row1[i][1] * mult) >> shift1, 1, stream);
			dptx_set_ram_data(dptx, (colors_row1[i][1] * mult) << shift2, 1, stream);
			dptx_set_ram_data(dptx, (colors_row1[i][2] * mult) >> shift1, 1, stream);
			dptx_set_ram_data(dptx, (colors_row1[i][2] * mult) << shift2, 1, stream);
		}
	}
}

void dptx_video_pattern_change(struct dptx *dptx, int stream)
{
	struct video_params *vparams;
	u32 reg;
	u8 pattern, bpc, encoding;
	int column_num;

	vparams = &dptx->vparams;
	pattern = vparams->pattern_mode;
	bpc = vparams->bpc;
	encoding = vparams->pix_enc;

	column_num = 256;

	if (pattern == RAMP) {
		if (!dptx->link.dsc) {
			if (bpc == COLOR_DEPTH_6)
				column_num = 64;
		}
		dptx_set_ramp_pattern(dptx, column_num, stream);
	} else if (pattern == COLRAMP) {
		if (encoding == YCBCR422 || encoding == YCBCR444)
			dptx_set_col_ycbcr_ramp_pattern(dptx, stream);
		else
			dptx_set_col_ramp_pattern(dptx, stream);
	}

	reg = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VG_CONFIG1_N(stream));
	reg &= ~DPTX_VG_CONFIG1_PATTERN_MASK;
	reg |= (vparams->pattern_mode << DPTX_VG_CONFIG1_PATTERN_SHIFT);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_CONFIG1_N(stream), reg);
}

void dptx_video_pattern_set(struct dptx *dptx, enum pattern_mode pattern, int stream)
{
	struct video_params *vparams;

	vparams = &dptx->vparams;
	vparams->pattern_mode = pattern;

	switch (pattern) {
	case TILE:
	case RAMP:
	case COLRAMP:
	case CHESS_BOARD:
		video_chess_board_config(dptx, stream);
		break;
	}

	dptx_dbg(dptx, "%s: Change video pattern to %d\n",
		 __func__, pattern);
}

void dptx_disable_default_video_stream(struct dptx *dptx, int stream)
{
	u32 vsamplectrl;

	vsamplectrl = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_CTRL_N(stream));
	vsamplectrl &= ~DPTX_VSAMPLE_CTRL_STREAM_EN;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_CTRL_N(stream), vsamplectrl);
}

void dptx_enable_default_video_stream(struct dptx *dptx, int stream)
{
	u32 vsamplectrl;

	vsamplectrl = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_CTRL_N(stream));
	vsamplectrl |= DPTX_VSAMPLE_CTRL_STREAM_EN;
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_CTRL_N(stream), vsamplectrl);
}

/*
 * Audio/Video Parameters
 */

void dptx_audio_params_reset(struct audio_params *params)
{
	params->iec_channel_numcl0 = 8;
	params->iec_channel_numcr0 = 4;
	params->use_lut = 1;
	params->iec_samp_freq = 3;
	params->iec_word_length = 11;
	params->iec_orig_samp_freq = 12;
	params->data_width = 24;
	params->num_channels = 2;
	params->inf_type = 0;
	params->ats_ver = 17;
	params->mute = 0;
}

void dptx_video_params_reset(struct dptx *dptx)
{
	struct video_params *params = &dptx->vparams;

	params->pix_enc = RGB;
	params->mode = 1;

	if (dptx->mst || dptx->dsc_en) {
		/* TODO 6 bpc should be default - use 8 bpc for MST calculation */
		params->bpc = COLOR_DEPTH_8;
	} else {
		params->bpc = COLOR_DEPTH_6;
	}

	params->colorimetry = ITU601;
	params->dynamic_range = VESA;
	params->aver_bytes_per_tu = 30;
	params->aver_bytes_per_tu_frac = 0;
	params->init_threshold = 15;
	params->pattern_mode = RAMP;
	params->refresh_rate = 60000;
	params->video_format = VCEA;
}

/*
 * DTD
 */

void dwc_dptx_dtd_reset(struct dtd *mdtd)
{
	mdtd->pixel_repetition_input = 0;
	mdtd->pixel_clock  = 0;
	mdtd->h_active = 0;
	mdtd->h_blanking = 0;
	mdtd->h_sync_offset = 0;
	mdtd->h_sync_pulse_width = 0;
	mdtd->h_image_size = 0;
	mdtd->v_active = 0;
	mdtd->v_blanking = 0;
	mdtd->v_sync_offset = 0;
	mdtd->v_sync_pulse_width = 0;
	mdtd->v_image_size = 0;
	mdtd->interlaced = 0;
	mdtd->v_sync_polarity = 0;
	mdtd->h_sync_polarity = 0;
}

void dptx_dtd_fill_from_mode(struct dtd *mdtd, struct drm_display_mode *m)
{
	dwc_dptx_dtd_reset(mdtd);
	mdtd->h_active = m->hdisplay;
	mdtd->h_blanking = m->htotal - m->hdisplay;
	mdtd->h_sync_offset = m->hsync_start - m->hdisplay;
	mdtd->h_sync_pulse_width = m->hsync_end - m->hsync_start;
	mdtd->h_sync_polarity = !!(m->flags & DRM_MODE_FLAG_PHSYNC);
	mdtd->v_active = m->vdisplay;
	mdtd->v_blanking = m->vtotal - m->vdisplay;
	mdtd->v_sync_offset = m->vsync_start - m->vdisplay;
	mdtd->v_sync_pulse_width = m->vsync_end - m->vsync_start;
	mdtd->v_sync_polarity = !!(m->flags & DRM_MODE_FLAG_PVSYNC);
	mdtd->interlaced = !!(m->flags & DRM_MODE_FLAG_INTERLACE);
	mdtd->pixel_clock = m->clock;
}

int dptx_dtd_fill(struct dtd *mdtd, u8 code, u32 refresh_rate,
		  u8 video_format)
{
	dwc_dptx_dtd_reset(mdtd);

	mdtd->h_image_size = 16;
	mdtd->v_image_size = 9;

	if (video_format == VCEA) {
		switch (code) {
		case 1: /* 640x480p @ 59.94/60Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 640;
			mdtd->v_active = 480;
			mdtd->h_blanking = 160;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 16;
			mdtd->v_sync_offset = 10;
			mdtd->h_sync_pulse_width = 96;
			mdtd->v_sync_pulse_width = 2;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 25175;
			break;
		case 2: /* 720x480p @ 59.94/60Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 3: /* 720x480p @ 59.94/60Hz 16:9 */
			mdtd->h_active = 720;
			mdtd->v_active = 480;
			mdtd->h_blanking = 138;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 16;
			mdtd->v_sync_offset = 9;
			mdtd->h_sync_pulse_width = 62;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 27000;
			break;
		case 69:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 4: /* 1280x720p @ 59.94/60Hz 16:9 */
			mdtd->h_active = 1280;
			mdtd->v_active = 720;
			mdtd->h_blanking = 370;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 110;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 74250;
			break;
		case 5: /* 1920x1080i @ 59.94/60Hz 16:9 */
			mdtd->h_active = 1920;
			mdtd->v_active = 540;
			mdtd->h_blanking = 280;
			mdtd->v_blanking = 22;
			mdtd->h_sync_offset = 88;
			mdtd->v_sync_offset = 2;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 1;
			mdtd->pixel_clock = 74250;
			break;
		case 6: /* 720(1440)x480i @ 59.94/60Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 7: /* 720(1440)x480i @ 59.94/60Hz 16:9 */
			mdtd->h_active = 1440;
			mdtd->v_active = 240;
			mdtd->h_blanking = 276;
			mdtd->v_blanking = 22;
			mdtd->h_sync_offset = 38;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 124;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 1;
			mdtd->pixel_clock = 27000;
			break;
		case 8: /* 720(1440)x240p @ 59.826/60.054/59.886/60.115Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 9: /* 720(1440)x240p @59.826/60.054/59.886/60.115Hz 16:9 */
			mdtd->h_active = 1440;
			mdtd->v_active = 240;
			mdtd->h_blanking = 276;
			mdtd->v_blanking = (refresh_rate == 59940) ? 22 : 23;
			mdtd->h_sync_offset = 38;
			mdtd->v_sync_offset = (refresh_rate == 59940) ? 4 : 5;
			mdtd->h_sync_pulse_width = 124;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 27000;
			break;
		case 10: /* 2880x480i @ 59.94/60Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 11: /* 2880x480i @ 59.94/60Hz 16:9 */
			mdtd->h_active = 2880;
			mdtd->v_active = 240;
			mdtd->h_blanking = 552;
			mdtd->v_blanking = 22;
			mdtd->h_sync_offset = 76;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 248;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 1;
			mdtd->pixel_clock = 54000;
			break;
		case 12: /* 2880x240p @ 59.826/60.054/59.886/60.115Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 13: /* 2880x240p @ 59.826/60.054/59.886/60.115Hz 16:9 */
			mdtd->h_active = 2880;
			mdtd->v_active = 240;
			mdtd->h_blanking = 552;
			mdtd->v_blanking = (refresh_rate == 60054) ? 22 : 23;
			mdtd->h_sync_offset = 76;
			mdtd->v_sync_offset = (refresh_rate == 60054) ? 4 : 5;
			mdtd->h_sync_pulse_width = 248;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 54000;
			break;
		case 14: /* 1440x480p @ 59.94/60Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 15: /* 1440x480p @ 59.94/60Hz 16:9 */
			mdtd->h_active = 1440;
			mdtd->v_active = 480;
			mdtd->h_blanking = 276;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 32;
			mdtd->v_sync_offset = 9;
			mdtd->h_sync_pulse_width = 124;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 54000;
			break;
		case 76:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 16: /* 1920x1080p @ 59.94/60Hz 16:9 */
			mdtd->h_active = 1920;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 280;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 88;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 148500;
			break;
		case 17: /* 720x576p @ 50Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 18: /* 720x576p @ 50Hz 16:9 */
			mdtd->h_active = 720;
			mdtd->v_active = 576;
			mdtd->h_blanking = 144;
			mdtd->v_blanking = 49;
			mdtd->h_sync_offset = 12;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 64;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 27000;
			break;
		case 68:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 19: /* 1280x720p @ 50Hz 16:9 */
			mdtd->h_active = 1280;
			mdtd->v_active = 720;
			mdtd->h_blanking = 700;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 440;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 74250;
			break;
		case 20: /* 1920x1080i @ 50Hz 16:9 */
			mdtd->h_active = 1920;
			mdtd->v_active = 540;
			mdtd->h_blanking = 720;
			mdtd->v_blanking = 22;
			mdtd->h_sync_offset = 528;
			mdtd->v_sync_offset = 2;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 1;
			mdtd->pixel_clock = 74250;
			break;
		case 21: /* 720(1440)x576i @ 50Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 22: /* 720(1440)x576i @ 50Hz 16:9 */
			mdtd->h_active = 1440;
			mdtd->v_active = 288;
			mdtd->h_blanking = 288;
			mdtd->v_blanking = 24;
			mdtd->h_sync_offset = 24;
			mdtd->v_sync_offset = 2;
			mdtd->h_sync_pulse_width = 126;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 1;
			mdtd->pixel_clock = 27000;
			break;
		case 23: /* 720(1440)x288p @ 50Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 24: /* 720(1440)x288p @ 50Hz 16:9 */
			mdtd->h_active = 1440;
			mdtd->v_active = 288;
			mdtd->h_blanking = 288;
			mdtd->v_blanking = (refresh_rate == 50080) ? 24
				: ((refresh_rate == 49920) ? 25 : 26);
			mdtd->h_sync_offset = 24;
			mdtd->v_sync_offset = (refresh_rate == 50080) ? 2
				: ((refresh_rate == 49920) ? 3 : 4);
			mdtd->h_sync_pulse_width = 126;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 27000;
			break;
		case 25: /* 2880x576i @ 50Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 26: /* 2880x576i @ 50Hz 16:9 */
			mdtd->h_active = 2880;
			mdtd->v_active = 288;
			mdtd->h_blanking = 576;
			mdtd->v_blanking = 24;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 2;
			mdtd->h_sync_pulse_width = 252;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 1;
			mdtd->pixel_clock = 54000;
			break;
		case 27: /* 2880x288p @ 50Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 28: /* 2880x288p @ 50Hz 16:9 */
			mdtd->h_active = 2880;
			mdtd->v_active = 288;
			mdtd->h_blanking = 576;
			mdtd->v_blanking = (refresh_rate == 50080) ? 24
				: ((refresh_rate == 49920) ? 25 : 26);
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = (refresh_rate == 50080) ? 2
				: ((refresh_rate == 49920) ? 3 : 4);
			mdtd->h_sync_pulse_width = 252;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 54000;
			break;
		case 29: /* 1440x576p @ 50Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 30: /* 1440x576p @ 50Hz 16:9 */
			mdtd->h_active = 1440;
			mdtd->v_active = 576;
			mdtd->h_blanking = 288;
			mdtd->v_blanking = 49;
			mdtd->h_sync_offset = 24;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 128;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 54000;
			break;
		case 75:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 31: /* 1920x1080p @ 50Hz 16:9 */
			mdtd->h_active = 1920;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 720;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 528;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 148500;
			break;
		case 72:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 32: /* 1920x1080p @ 23.976/24Hz 16:9 */
			mdtd->h_active = 1920;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 830;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 638;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 74250;
			break;
		case 73:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 33: /* 1920x1080p @ 25Hz 16:9 */
			mdtd->h_active = 1920;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 720;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 528;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 74250;
			break;
		case 74:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 34: /* 1920x1080p @ 29.97/30Hz 16:9 */
			mdtd->h_active = 1920;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 280;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 88;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 74250;
			break;
		case 35: /* 2880x480p @ 60Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 36: /* 2880x480p @ 60Hz 16:9 */
			mdtd->h_active = 2880;
			mdtd->v_active = 480;
			mdtd->h_blanking = 552;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 64;
			mdtd->v_sync_offset = 9;
			mdtd->h_sync_pulse_width = 248;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 108000;
			break;
		case 37: /* 2880x576p @ 50Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 38: /* 2880x576p @ 50Hz 16:9 */
			mdtd->h_active = 2880;
			mdtd->v_active = 576;
			mdtd->h_blanking = 576;
			mdtd->v_blanking = 49;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 256;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 108000;
			break;
		case 39: /* 1920x1080i (1250 total) @ 50Hz 16:9 */
			mdtd->h_active = 1920;
			mdtd->v_active = 540;
			mdtd->h_blanking = 384;
			mdtd->v_blanking = 85;
			mdtd->h_sync_offset = 32;
			mdtd->v_sync_offset = 23;
			mdtd->h_sync_pulse_width = 168;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 1;
			mdtd->pixel_clock = 72000;
			break;
		case 40: /* 1920x1080i @ 100Hz 16:9 */
			mdtd->h_active = 1920;
			mdtd->v_active = 540;
			mdtd->h_blanking = 720;
			mdtd->v_blanking = 22;
			mdtd->h_sync_offset = 528;
			mdtd->v_sync_offset = 2;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 1;
			mdtd->pixel_clock = 148500;
			break;
		case 70:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 41: /* 1280x720p @ 100Hz 16:9 */
			mdtd->h_active = 1280;
			mdtd->v_active = 720;
			mdtd->h_blanking = 700;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 440;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 148500;
			break;
		case 42: /* 720x576p @ 100Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 43: /* 720x576p @ 100Hz 16:9 */
			mdtd->h_active = 720;
			mdtd->v_active = 576;
			mdtd->h_blanking = 144;
			mdtd->v_blanking = 49;
			mdtd->h_sync_offset = 12;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 64;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 54000;
			break;
		case 44: /* 720(1440)x576i @ 100Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 45: /* 720(1440)x576i @ 100Hz 16:9 */
			mdtd->h_active = 1440;
			mdtd->v_active = 288;
			mdtd->h_blanking = 288;
			mdtd->v_blanking = 24;
			mdtd->h_sync_offset = 24;
			mdtd->v_sync_offset = 2;
			mdtd->h_sync_pulse_width = 126;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 1;
			mdtd->pixel_clock = 54000;
			break;
		case 46: /* 1920x1080i @ 119.88/120Hz 16:9 */
			mdtd->h_active = 1920;
			mdtd->v_active = 540;
			mdtd->h_blanking = 288;
			mdtd->v_blanking = 22;
			mdtd->h_sync_offset = 88;
			mdtd->v_sync_offset = 2;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 1;
			mdtd->pixel_clock = 148500;
			break;
		case 71:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 47: /* 1280x720p @ 119.88/120Hz 16:9 */
			mdtd->h_active = 1280;
			mdtd->v_active = 720;
			mdtd->h_blanking = 370;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 110;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 148500;
			break;
		case 48: /* 720x480p @ 119.88/120Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 49: /* 720x480p @ 119.88/120Hz 16:9 */
			mdtd->h_active = 720;
			mdtd->v_active = 480;
			mdtd->h_blanking = 138;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 16;
			mdtd->v_sync_offset = 9;
			mdtd->h_sync_pulse_width = 62;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 54000;
			break;
		case 50: /* 720(1440)x480i @ 119.88/120Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 51: /* 720(1440)x480i @ 119.88/120Hz 16:9 */
			mdtd->h_active = 1440;
			mdtd->v_active = 240;
			mdtd->h_blanking = 276;
			mdtd->v_blanking = 22;
			mdtd->h_sync_offset = 38;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 124;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 1;
			mdtd->pixel_clock = 54000;
			break;
		case 52: /* 720X576p @ 200Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 53: /* 720X576p @ 200Hz 16:9 */
			mdtd->h_active = 720;
			mdtd->v_active = 576;
			mdtd->h_blanking = 144;
			mdtd->v_blanking = 49;
			mdtd->h_sync_offset = 12;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 64;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 108000;
			break;
		case 54: /* 720(1440)x576i @ 200Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 55: /* 720(1440)x576i @ 200Hz 16:9 */
			mdtd->h_active = 1440;
			mdtd->v_active = 288;
			mdtd->h_blanking = 288;
			mdtd->v_blanking = 24;
			mdtd->h_sync_offset = 24;
			mdtd->v_sync_offset = 2;
			mdtd->h_sync_pulse_width = 126;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 1;
			mdtd->pixel_clock = 108000;
			break;
		case 56: /* 720x480p @ 239.76/240Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 57: /* 720x480p @ 239.76/240Hz 16:9 */
			mdtd->h_active = 720;
			mdtd->v_active = 480;
			mdtd->h_blanking = 138;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 16;
			mdtd->v_sync_offset = 9;
			mdtd->h_sync_pulse_width = 62;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 108000;
			break;
		case 58: /* 720(1440)x480i @ 239.76/240Hz 4:3 */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 59: /* 720(1440)x480i @ 239.76/240Hz 16:9 */
			mdtd->h_active = 1440;
			mdtd->v_active = 240;
			mdtd->h_blanking = 276;
			mdtd->v_blanking = 22;
			mdtd->h_sync_offset = 38;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 124;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 1;
			mdtd->pixel_clock = 108000;
			break;
		case 65:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 60: /* 1280x720p @ 23.97/24Hz 16:9 */
			mdtd->h_active = 1280;
			mdtd->v_active = 720;
			mdtd->h_blanking = 2020;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 1760;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 594000;
			break;
		case 66:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 61: /* 1280x720p @ 25Hz 16:9 */
			mdtd->h_active = 1280;
			mdtd->v_active = 720;
			mdtd->h_blanking = 2680;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 2420;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 74250;
			break;
		case 67:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 62: /* 1280x720p @ 29.97/30Hz  16:9 */
			mdtd->h_active = 1280;
			mdtd->v_active = 720;
			mdtd->h_blanking = 2020;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 1760;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 74250;
			break;
		case 78:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 63: /* 1920x1080p @ 119.88/120Hz 16:9 */
			mdtd->h_active = 1920;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 280;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 88;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 297000;
			break;
		case 77:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 64: /* 1920x1080p @ 100Hz 16:9 */
			mdtd->h_active = 1920;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 720;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 528;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 297000;
			break;
		case 79:
			mdtd->h_active = 1680;
			mdtd->v_active = 720;
			mdtd->h_blanking = 1620;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 1360;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 594000;
			break;
		case 80:
			mdtd->h_active = 1680;
			mdtd->v_active = 720;
			mdtd->h_blanking = 1488;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 1228;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 594000;
			break;
		case 81:
			mdtd->h_active = 1680;
			mdtd->v_active = 720;
			mdtd->h_blanking = 960;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 700;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 594000;
			break;
		case 82:
			mdtd->h_active = 1680;
			mdtd->v_active = 720;
			mdtd->h_blanking = 520;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 260;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 82500;
			break;
		case 83:
			mdtd->h_active = 1680;
			mdtd->v_active = 720;
			mdtd->h_blanking = 520;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 260;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 99000;
			break;
		case 84:
			mdtd->h_active = 1680;
			mdtd->v_active = 720;
			mdtd->h_blanking = 320;
			mdtd->v_blanking = 105;
			mdtd->h_sync_offset = 60;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 165000;
			break;
		case 85:
			mdtd->h_active = 1680;
			mdtd->v_active = 720;
			mdtd->h_blanking = 320;
			mdtd->v_blanking = 105;
			mdtd->h_sync_offset = 60;
			mdtd->v_sync_offset = 5;
			mdtd->h_sync_pulse_width = 40;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 198000;
			break;
		case 86:
			mdtd->h_active = 2560;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 1190;
			mdtd->v_blanking = 20;
			mdtd->h_sync_offset = 998;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 99000;
			break;
		case 87:
			mdtd->h_active = 2560;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 640;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 448;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 90000;
			break;
		case 88:
			mdtd->h_active = 2560;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 960;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 768;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 118800;
			break;
		case 89:
			mdtd->h_active = 2560;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 740;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 548;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 185625;
			break;
		case 90:
			mdtd->h_active = 2560;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 440;
			mdtd->v_blanking = 20;
			mdtd->h_sync_offset = 248;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 198000;
			break;
		case 91:
			mdtd->h_active = 2560;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 410;
			mdtd->v_blanking = 170;
			mdtd->h_sync_offset = 218;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 371250;
			break;
		case 92:
			mdtd->h_active = 2560;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 740;
			mdtd->v_blanking = 170;
			mdtd->h_sync_offset = 548;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 495000;
			break;
		case 101:
			mdtd->h_active = 4096;
			mdtd->v_active = 2160;
			mdtd->h_blanking = 1184;
			mdtd->v_blanking = 90;
			mdtd->h_sync_offset = 968;
			mdtd->v_sync_offset = 8;
			mdtd->h_sync_pulse_width = 88;
			mdtd->v_sync_pulse_width = 10;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 59400;
			break;
		case 100:
			mdtd->h_active = 4096;
			mdtd->v_active = 2160;
			mdtd->h_blanking = 304;
			mdtd->v_blanking = 90;
			mdtd->h_sync_offset = 88;
			mdtd->v_sync_offset = 8;
			mdtd->h_sync_pulse_width = 88;
			mdtd->v_sync_pulse_width = 10;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 297000;
			break;
		case 99:
			mdtd->h_active = 4096;
			mdtd->v_active = 2160;
			mdtd->h_blanking = 1184;
			mdtd->v_blanking = 90;
			mdtd->h_sync_offset = 968;
			mdtd->v_sync_offset = 8;
			mdtd->h_sync_pulse_width = 88;
			mdtd->v_sync_pulse_width = 10;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 297000;
			break;
		case 102:
			mdtd->h_active = 4096;
			mdtd->v_active = 2160;
			mdtd->h_blanking = 304;
			mdtd->v_blanking = 90;
			mdtd->h_sync_offset = 88;
			mdtd->v_sync_offset = 8;
			mdtd->h_sync_pulse_width = 88;
			mdtd->v_sync_pulse_width = 10;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 594000;
			break;
		case 103:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 93:		/* 4k x 2k, 30Hz */
			mdtd->h_active = 3840;
			mdtd->v_active = 2160;
			mdtd->h_blanking = 1660;
			mdtd->v_blanking = 90;
			mdtd->h_sync_offset = 1276;
			mdtd->v_sync_offset = 8;
			mdtd->h_sync_pulse_width = 88;
			mdtd->v_sync_pulse_width = 10;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 297000;
			break;
		case 104:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 94:
			mdtd->h_active = 3840;
			mdtd->v_active = 2160;
			mdtd->h_blanking = 1440;
			mdtd->v_blanking = 90;
			mdtd->h_sync_offset = 1056;
			mdtd->v_sync_offset = 8;
			mdtd->h_sync_pulse_width = 88;
			mdtd->v_sync_pulse_width = 10;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 297000;
			break;
		case 105:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 95:
			mdtd->h_active = 3840;
			mdtd->v_active = 2160;
			mdtd->h_blanking = 560;
			mdtd->v_blanking = 90;
			mdtd->h_sync_offset = 176;
			mdtd->v_sync_offset = 8;
			mdtd->h_sync_pulse_width = 88;
			mdtd->v_sync_pulse_width = 10;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 297000;
			break;
		case 106:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 96:
			mdtd->h_active = 3840;
			mdtd->v_active = 2160;
			mdtd->h_blanking = 1440;
			mdtd->v_blanking = 90;
			mdtd->h_sync_offset = 1056;
			mdtd->v_sync_offset = 8;
			mdtd->h_sync_pulse_width = 88;
			mdtd->v_sync_pulse_width = 10;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 594000;
			break;
		case 107:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			fallthrough;
		case 97:
			mdtd->h_active = 3840;
			mdtd->v_active = 2160;
			mdtd->h_blanking = 560;
			mdtd->v_blanking = 90;
			mdtd->h_sync_offset = 176;
			mdtd->v_sync_offset = 8;
			mdtd->h_sync_pulse_width = 88;
			mdtd->v_sync_pulse_width = 10;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 594000;
			break;
		case 98:
			mdtd->h_active = 4096;
			mdtd->v_active = 2160;
			mdtd->h_blanking = 1404;
			mdtd->v_blanking = 90;
			mdtd->h_sync_offset = 1020;
			mdtd->v_sync_offset = 8;
			mdtd->h_sync_pulse_width = 88;
			mdtd->v_sync_pulse_width = 10;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;
			mdtd->pixel_clock = 297000;
			break;
		default:
			return false;
		}
	} else if (video_format == CVT) {
		switch (code) {
		case 1:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 640;
			mdtd->v_active = 480;
			mdtd->h_blanking = 160;
			mdtd->v_blanking = 20;
			mdtd->h_sync_offset = 8;
			mdtd->v_sync_offset = 1;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 8;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 23750;
			break;
		case 2:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 800;
			mdtd->v_active = 600;
			mdtd->h_blanking = 224;
			mdtd->v_blanking = 24;
			mdtd->h_sync_offset = 31;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 81;
			mdtd->v_sync_pulse_width = 4;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 38250;
			break;
		case 3:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1024;
			mdtd->v_active = 768;
			mdtd->h_blanking = 304;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 104;
			mdtd->v_sync_pulse_width = 4;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 63500;
			break;
		case 4:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1280;
			mdtd->v_active = 960;
			mdtd->h_blanking = 416;
			mdtd->v_blanking = 36;
			mdtd->h_sync_offset = 80;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 128;
			mdtd->v_sync_pulse_width = 4;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 101250;
			break;
		case 5:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1400;
			mdtd->v_active = 1050;
			mdtd->h_blanking = 464;
			mdtd->v_blanking = 39;
			mdtd->h_sync_offset = 88;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 144;
			mdtd->v_sync_pulse_width = 4;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 121750;
			break;
		case 6:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1600;
			mdtd->v_active = 1200;
			mdtd->h_blanking = 560;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 112;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 68;
			mdtd->v_sync_pulse_width = 4;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 161000;
			break;
		case 12:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1280;
			mdtd->v_active = 1024;
			mdtd->h_blanking = 432;
			mdtd->v_blanking = 39;
			mdtd->h_sync_offset = 80;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 136;
			mdtd->v_sync_pulse_width = 7;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 109000;
			break;
		case 13:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1280;
			mdtd->v_active = 768;
			mdtd->h_blanking = 384;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 64;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 128;
			mdtd->v_sync_pulse_width = 7;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 79500;
			break;
		case 16:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1280;
			mdtd->v_active = 720;
			mdtd->h_blanking = 384;
			mdtd->v_blanking = 28;
			mdtd->h_sync_offset = 64;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 128;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 74500;
			break;
		case 17:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1360;
			mdtd->v_active = 768;
			mdtd->h_blanking = 416;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 72;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 136;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 84750;
			break;
		case 20:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1920;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 656;
			mdtd->v_blanking = 40;
			mdtd->h_sync_offset = 128;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 200;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 173000;
			break;
		case 22:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 2560;
			mdtd->v_active = 1440;
			mdtd->h_blanking = 928;
			mdtd->v_blanking = 53;
			mdtd->h_sync_offset = 192;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 272;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 312250;
			break;
		case 28:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1280;
			mdtd->v_active = 800;
			mdtd->h_blanking = 400;
			mdtd->v_blanking = 31;
			mdtd->h_sync_offset = 72;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 128;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 83500;
			break;
		case 34:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1920;
			mdtd->v_active = 1200;
			mdtd->h_blanking = 672;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 136;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 200;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 193250;
			break;
		case 38:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 3840;
			mdtd->v_active = 2400;
			mdtd->h_blanking = 80;
			mdtd->v_blanking = 69;
			mdtd->h_sync_offset = 320;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 424;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 580128;
			break;
		case 40:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1600;
			mdtd->v_active = 1200;
			mdtd->h_blanking = 160;
			mdtd->v_blanking = 35;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 4;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 124076;
			break;
		case 41:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 2048;
			mdtd->v_active = 1536;
			mdtd->h_blanking = 160;
			mdtd->v_blanking = 44;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 4;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 208000;
			break;
		default:
			return false;
		}
	} else if (video_format == DMT) {
		switch (code) {
		case 1: // HISilicon timing
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 3600;
			mdtd->v_active = 1800;
			mdtd->h_blanking = 120;
			mdtd->v_blanking = 128;
			mdtd->h_sync_offset = 20;
			mdtd->v_sync_offset = 2;
			mdtd->h_sync_pulse_width = 20;
			mdtd->v_sync_pulse_width = 2;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 645500;
			break;
		case 2:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 3840;
			mdtd->v_active = 2160;
			mdtd->h_blanking = 160;
			mdtd->v_blanking = 62;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 533000;
			break;
		case 4:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 640;
			mdtd->v_active = 480;
			mdtd->h_blanking = 144;
			mdtd->v_blanking = 29;
			mdtd->h_sync_offset = 8;
			mdtd->v_sync_offset = 2;
			mdtd->h_sync_pulse_width = 96;
			mdtd->v_sync_pulse_width = 2;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 25175;
			break;
		case 13:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 800;
			mdtd->v_active = 600;
			mdtd->h_blanking = 160;
			mdtd->v_blanking = 36;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 4;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 73250;
			break;
		case 14: /* 848x480p@60Hz */
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 848;
			mdtd->v_active = 480;
			mdtd->h_blanking = 240;
			mdtd->v_blanking = 37;
			mdtd->h_sync_offset = 16;
			mdtd->v_sync_offset = 6;
			mdtd->h_sync_pulse_width = 112;
			mdtd->v_sync_pulse_width = 8;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI)  */;
			mdtd->pixel_clock = 33750;
			break;
		case 22:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1280;
			mdtd->v_active = 768;
			mdtd->h_blanking = 160;
			mdtd->v_blanking = 22;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 7;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 68250;
			break;
		case 35:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1280;
			mdtd->v_active = 1024;
			mdtd->h_blanking = 408;
			mdtd->v_blanking = 42;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 1;
			mdtd->h_sync_pulse_width = 112;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 108000;
			break;
		case 39:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1360;
			mdtd->v_active = 768;
			mdtd->h_blanking = 432;
			mdtd->v_blanking = 27;
			mdtd->h_sync_offset = 64;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 112;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 85500;
			break;
		case 40:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1360;
			mdtd->v_active = 768;
			mdtd->h_blanking = 160;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 148250;
			break;
		case 81:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1366;
			mdtd->v_active = 768;
			mdtd->h_blanking = 426;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 70;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 142;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 85500;
			break;
		case 86:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1366;
			mdtd->v_active = 768;
			mdtd->h_blanking = 134;
			mdtd->v_blanking = 32;
			mdtd->h_sync_offset = 14;
			mdtd->v_sync_offset = 1;
			mdtd->h_sync_pulse_width = 56;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 72000;
			break;
		case 87:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 4096;
			mdtd->v_active = 2160;
			mdtd->h_blanking = 80;
			mdtd->v_blanking = 62;
			mdtd->h_sync_offset = 8;
			mdtd->v_sync_offset = 48;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 8;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 556744;
			break;
		case 88:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 4096;
			mdtd->v_active = 2160;
			mdtd->h_blanking = 80;
			mdtd->v_blanking = 62;
			mdtd->h_sync_offset = 8;
			mdtd->v_sync_offset = 48;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 8;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 556188;
			break;
		case 41:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1400;
			mdtd->v_active = 1050;
			mdtd->h_blanking = 160;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 4;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 101000;
			break;
		case 42:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1400;
			mdtd->v_active = 1050;
			mdtd->h_blanking = 464;
			mdtd->v_blanking = 39;
			mdtd->h_sync_offset = 88;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 144;
			mdtd->v_sync_pulse_width = 4;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 121750;
			break;
		case 46:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1440;
			mdtd->v_active = 900;
			mdtd->h_blanking = 160;
			mdtd->v_blanking = 26;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 88750;
			break;
		case 47:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1440;
			mdtd->v_active = 900;
			mdtd->h_blanking = 464;
			mdtd->v_blanking = 34;
			mdtd->h_sync_offset = 80;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 152;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 106500;
			break;
		case 51:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1600;
			mdtd->v_active = 1200;
			mdtd->h_blanking = 560;
			mdtd->v_blanking = 50;
			mdtd->h_sync_offset = 64;
			mdtd->v_sync_offset = 1;
			mdtd->h_sync_pulse_width = 192;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 162000;
			break;
		case 57:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1680;
			mdtd->v_active = 1050;
			mdtd->h_blanking = 160;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 119000;
			break;
		case 58:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1680;
			mdtd->v_active = 1050;
			mdtd->h_blanking = 560;
			mdtd->v_blanking = 39;
			mdtd->h_sync_offset = 104;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 176;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 146250;
			break;
		case 68:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1920;
			mdtd->v_active = 1200;
			mdtd->h_blanking = 160;
			mdtd->v_blanking = 35;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 154000;
			break;
		case 69:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1920;
			mdtd->v_active = 1200;
			mdtd->h_blanking = 672;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 136;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 200;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 193250;
			break;
		case 82:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1920;
			mdtd->v_active = 1080;
			mdtd->h_blanking = 280;
			mdtd->v_blanking = 45;
			mdtd->h_sync_offset = 88;
			mdtd->v_sync_offset = 4;
			mdtd->h_sync_pulse_width = 44;
			mdtd->v_sync_pulse_width = 5;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 148500;
			break;
		case 83:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1600;
			mdtd->v_active = 900;
			mdtd->h_blanking = 200;
			mdtd->v_blanking = 100;
			mdtd->h_sync_offset = 24;
			mdtd->v_sync_offset = 1;
			mdtd->h_sync_pulse_width = 80;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 108000;
			break;
		case 9:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 800;
			mdtd->v_active = 600;
			mdtd->h_blanking = 256;
			mdtd->v_blanking = 28;
			mdtd->h_sync_offset = 40;
			mdtd->v_sync_offset = 1;
			mdtd->h_sync_pulse_width = 128;
			mdtd->v_sync_pulse_width = 4;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 40000;
			break;
		case 16:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1024;
			mdtd->v_active = 768;
			mdtd->h_blanking = 320;
			mdtd->v_blanking = 38;
			mdtd->h_sync_offset = 24;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 136;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 65000;
			break;
		case 23:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1280;
			mdtd->v_active = 768;
			mdtd->h_blanking = 384;
			mdtd->v_blanking = 30;
			mdtd->h_sync_offset = 64;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 128;
			mdtd->v_sync_pulse_width = 7;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 79500;
			break;
		case 62:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active =  1792;
			mdtd->v_active = 1344;
			mdtd->h_blanking = 656;
			mdtd->v_blanking =  50;
			mdtd->h_sync_offset = 128;
			mdtd->v_sync_offset = 1;
			mdtd->h_sync_pulse_width = 200;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0; /* (progressive_nI) */
			mdtd->pixel_clock = 204750;
			break;
		case 32:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1280;
			mdtd->v_active = 960;
			mdtd->h_blanking = 520;
			mdtd->v_blanking = 40;
			mdtd->h_sync_offset = 96;
			mdtd->v_sync_offset = 1;
			mdtd->h_sync_pulse_width = 112;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;  /* (progressive_nI) */
			mdtd->pixel_clock = 108000;
			break;
		case 73:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1920;
			mdtd->v_active = 1440;
			mdtd->h_blanking = 680;
			mdtd->v_blanking = 60;
			mdtd->h_sync_offset = 128;
			mdtd->v_sync_offset = 1;
			mdtd->h_sync_pulse_width = 208;
			mdtd->v_sync_pulse_width = 3;
			mdtd->h_sync_polarity = 0;
			mdtd->v_sync_polarity = 1;
			mdtd->interlaced = 0;  /* (progressive_nI) */
			mdtd->pixel_clock = 234000;
			break;
		case 27:
			mdtd->h_image_size = 4;
			mdtd->v_image_size = 3;
			mdtd->h_active = 1280;
			mdtd->v_active = 800;
			mdtd->h_blanking = 160;
			mdtd->v_blanking = 23;
			mdtd->h_sync_offset = 48;
			mdtd->v_sync_offset = 3;
			mdtd->h_sync_pulse_width = 32;
			mdtd->v_sync_pulse_width = 6;
			mdtd->h_sync_polarity = 1;
			mdtd->v_sync_polarity = 0;
			mdtd->interlaced = 0;  /* (progressive_nI) */
			mdtd->pixel_clock = 71000;
			break;
		default:
			return false;
		}
	}

	return true;
}

/*
 * Adaptive-Sync
 */

