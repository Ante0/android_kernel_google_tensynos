// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "api.h"
#include "audio_bridge.h"
#include "regmaps/ctrl_fields.h"

/* TODO Fix to use any stream */
#define DEFAULT_STREAM 0

/**
 * dptx_get_audio_inf_type() - Get audio interface type
 * @dptx: The dptx struct
 *
 * Returns audio interface type
 * 0 - I2S
 * 1 - SPDIF
 */
u8 dptx_get_audio_inf_type(struct dptx *dptx)
{
	struct audio_params *aparams;

	aparams = &dptx->aparams;
	return aparams->inf_type;
}

/**
 * dptx_get_audio_data_width() - Get audio input data width
 * @dptx: The dptx struct
 *
 * Returns audio input data width
 */
u8 dptx_get_audio_data_width(struct dptx *dptx)
{
	struct audio_params *aparams;

	aparams = &dptx->aparams;
	return aparams->data_width;
}

/**
 * dptx_get_audio_num_ch() - Get audio channel numbers
 * @dptx: The dptx struct
 *
 * Returns audio channel numbers
 */
u8 dptx_get_audio_num_ch(struct dptx *dptx)
{
	struct audio_params *aparams;

	aparams = &dptx->aparams;
	return aparams->num_channels;
}

u8 dptx_enable_audio_generator(struct dptx *dptx)
{
	int stream, nr_streams = 0;

	nr_streams = dptx->streams;

	for (stream = 0; stream < nr_streams; stream++) {
		dptx_dbg(dptx, "Enabling Audio Generator for Stream %d\n", stream);
		audio_generator_config(dptx, stream);
	}

	return 0;
}

u8 dptx_disable_audio_generator(struct dptx *dptx)
{
	audio_generator_disable(dptx);

	return 0;
}

/**
 * dptx_get_video_format() - Get current video format
 * @dptx: The dptx struct
 *
 * Returns video format.
 * 0 - CEA
 * 1 - CVT
 * 2 - DMT
 */
u8 dptx_get_video_format(struct dptx *dptx)
{
	struct video_params *vparams;

	vparams = &dptx->vparams;
	return vparams->video_format;
}

/**
 * dptx_set_video_format() - Set video format
 * @dptx: The dptx struct
 * @video_format: video format
 * Possible options: 0 - CEA, 1 - CVT, 2 - DMT
 *
 * Returns 0 on success otherwise negative errno.
 */
int dptx_set_video_format(struct dptx *dptx, u8 video_format)
{
	struct video_params *vparams;

	if (video_format > DMT) {
		dptx_dbg(dptx, "%s: Invalid video format value %d\n",
			 __func__, video_format);
		return -EINVAL;
	}

	vparams = &dptx->vparams;
	vparams->video_format = video_format;
	dptx_dbg(dptx, "%s: Change video format to %d\n",
		 __func__, video_format);
	return 0;
}

/**
 * dptx_get_link_lane_count() - Get link lane count
 * Possible options: 1, 2 or 4
 * @dptx: The dptx struct
 *
 * Returns the number of lanes on success otherwise negative errno.
 */
u8 dptx_get_link_lane_count(struct dptx *dptx)
{
	u32 hpdsts;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	hpdsts = dptx_read_regfield(dptx, ctrl_fields->field_hpd_status);
	if (!hpdsts) {
		dptx_dbg(dptx, "%s: Not connected\n", __func__);
		return  -ENODEV;
	}
	return dptx->link.lanes;
}

/**
 * dptx_get_link_rate() - Get link rate
 * Possible options: 0 - RBR, 1 - HBR, 2 - HBR2, 3 - HBR3
 * @dptx: The dptx struct
 *
 * Returns link rate on success otherwise negative errno.
 */
u8 dptx_get_link_rate(struct dptx *dptx)
{
	u32 hpdsts;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	hpdsts = dptx_read_regfield(dptx, ctrl_fields->field_hpd_status);
	if (!hpdsts) {
		dptx_dbg(dptx, "%s: Not connected\n", __func__);
		return -ENODEV;
	}
	return dptx->link.rate;
}

/**
 * dptx_get_video_dynamic_range() - Get video dynamic range
 * @dptx: The dptx struct
 *
 * Returns video dynamic range
 * 1 - CEA, 2 - VESA
 */
u8 dptx_get_video_dynamic_range(struct dptx *dptx)
{
	struct video_params *vparams;

	vparams = &dptx->vparams;
	return vparams->dynamic_range;
}

/**
 * dptx_set_video_dynamic_range() - Set video dynamic range
 * @dptx: The dptx struct
 * @dynamic_range: video dynamic range
 * Possible options: 1 - CEA, 2 - VESA
 *
 * Returns 0 on success otherwise negative errno.
 */
int dptx_set_video_dynamic_range(struct dptx *dptx, u8 dynamic_range)
{
	struct video_params *vparams;

	if (dynamic_range > VESA) {
		dptx_dbg(dptx, "%s: Invalid dynamic range value %d\n",
			 __func__, dynamic_range);
		return -EINVAL;
	}

	vparams = &dptx->vparams;
	vparams->dynamic_range = dynamic_range;

	dptx_disable_default_video_stream(dptx, DEFAULT_STREAM);
	dptx_video_set_sink_col(dptx, DEFAULT_STREAM);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_SWRST_N(DEFAULT_STREAM), 0);
	dptx_enable_default_video_stream(dptx, DEFAULT_STREAM);
	dptx_dbg(dptx, "%s: Change video dynamic range to %d\n",
		 __func__, dynamic_range);

	return 0;
}

/**
 * dptx_get_video_colorimetry() - Get video colorimetry
 * @dptx: The dptx struct
 *
 * Returns video colorimetry
 * 1 - ITU-R BT.601, 2 - ITU-R BT.709
 */
u8 dptx_get_video_colorimetry(struct dptx *dptx)
{
	struct video_params *vparams;

	vparams = &dptx->vparams;
	return vparams->colorimetry;
}

/**
 * dptx_set_video_colorimetry() - Set video colorimetry
 * @dptx: The dptx struct
 * @video_col: Video colorimetry
 * Possible options: 1 - ITU-R BT.601, 2 - ITU-R BT.709
 *
 * Returns 0 on success otherwise negative errno.
 */
int dptx_set_video_colorimetry(struct dptx *dptx, u8 video_col)
{
	struct video_params *vparams;

	if (video_col > ITU709) {
		dptx_dbg(dptx, "%s: Invalid video colorimetry value %d\n",
			 __func__, video_col);
		return -EINVAL;
	}

	vparams = &dptx->vparams;
	vparams->colorimetry = video_col;

	dptx_disable_default_video_stream(dptx, DEFAULT_STREAM);
	dptx_video_set_sink_col(dptx, DEFAULT_STREAM);
	dptx_video_pattern_change(dptx, DEFAULT_STREAM);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_SWRST_N(DEFAULT_STREAM), 0);
	dptx_enable_default_video_stream(dptx, DEFAULT_STREAM);
	dptx_dbg(dptx, "%s: Change video colorimetry to %d\n",
		 __func__, video_col);

	return 0;
}

/**
 * dptx_get_pixel_enc() - Get pixel encoding
 * @dptx: The dptx struct
 *
 * Returns pixel encoding
 * RGB - 0, YCbCR420 - 1, YCbCR422 - 2, YCbCR444 - 3, YOnly - 4, RAW - 5
 */
u8 dptx_get_pixel_enc(struct dptx *dptx)
{
	struct video_params *vparams;

	vparams = &dptx->vparams;

	return vparams->pix_enc;
}

/**
 * dptx_set_pixel_enc() - Set pixel encoding
 * @dptx: The dptx struct
 * @pix_enc: Video pixel encoding
 * Possible options: RGB - 0, YCbCR420 - 1, YCbCR422 - 2
 *		     YCbCR444 - 3, YOnly - 4, RAW -5
 *
 * Returns 0 on success otherwise negative errno.
 */
int dptx_set_pixel_enc(struct dptx *dptx, u8 pix_enc)
{
	u32 hpdsts;
	int retval;
	struct video_params *vparams;
	struct dtd *mdtd;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	hpdsts = dptx_read_regfield(dptx, ctrl_fields->field_hpd_status);
	if (!hpdsts) {
		dptx_dbg(dptx, "%s: Not connected\n", __func__);
		return -ENODEV;
	}
	if (pix_enc > RAW) {
		dptx_dbg(dptx, "%s: Invalid pixel encoding value %d\n",
			 __func__, pix_enc);
		return -EINVAL;
	}
	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;
	retval = dptx_video_ts_calculate(dptx, dptx->link.lanes,
					 dptx->link.rate, vparams->bpc,
					 pix_enc, mdtd->pixel_clock);
	if (retval)
		return retval;

	vparams->pix_enc = pix_enc;

	dptx_disable_default_video_stream(dptx, DEFAULT_STREAM);
	dptx_video_bpc_change(dptx, DEFAULT_STREAM);
	dptx_video_ycc_mapping_change(dptx, DEFAULT_STREAM);
	dptx_video_ts_change(dptx, DEFAULT_STREAM);
	dptx_video_pattern_change(dptx, DEFAULT_STREAM);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_SWRST_N(DEFAULT_STREAM), 0);
	dptx_enable_default_video_stream(dptx, DEFAULT_STREAM);

	if (pix_enc == YCBCR420) {
		dptx_vsd_ycbcr420_send(dptx, 1);
		dptx->ycbcr420 = 1;
	} else {
		dptx_vsd_ycbcr420_send(dptx, 0);
		dptx->ycbcr420 = 0;
	}

	dptx_dbg(dptx, "%s: Change pixel encoding to %d\n",
		 __func__, pix_enc);

	return retval;
}

/**
 * dptx_get_video_mode() - Get current video mode
 * @dptx: The dptx struct
 *
 * Returns video mode
 */
u8 dptx_get_video_mode(struct dptx *dptx)
{
	struct video_params *vparams;

	vparams = &dptx->vparams;
	return vparams->mode;
}

/**
 * dptx_set_video_mode() - Set current video mode
 * @dptx: The dptx struct
 * @vmode: Video mode number
 *
 * Returns 0 on success otherwise negative errno.
 */
int dptx_set_video_mode(struct dptx *dptx, u8 vmode)
{
	u32 hpdsts;
	int retval;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	hpdsts = dptx_read_regfield(dptx, ctrl_fields->field_hpd_status);
	if (!hpdsts) {
		dptx_dbg(dptx, "%s: Not connected\n", __func__);
		return -ENODEV;
	}
	retval = dptx_video_mode_change(dptx, vmode, DEFAULT_STREAM);
	if (retval)
		return retval;

	return retval;
}

/**
 * dptx_get_bpc() - Get bits per component
 * @dptx: The dptx struct
 *
 * Returns the bit per componenet value
 */
u8 dptx_get_bpc(struct dptx *dptx)
{
	struct video_params *vparams;

	vparams = &dptx->vparams;
	return vparams->bpc;
}

/**
 * dptx_set_bpc() - Set bits per component
 * @dptx: The dptx struct
 * @bpc: Bits per component value
 * Possible options: 6, 8, 10, 12, 16
 *
 * Returns 0 on success otherwise negative errno.
 */
int dptx_set_bpc(struct dptx *dptx, u8 bpc)
{
	u32 hpdsts;
	int retval;
	struct video_params *vparams;
	struct dtd *mdtd;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	hpdsts = dptx_read_regfield(dptx, ctrl_fields->field_hpd_status);
	if (!hpdsts) {
		dptx_dbg(dptx, "%s: Not connected\n", __func__);
		return -ENODEV;
	}

	if (bpc != COLOR_DEPTH_6  && bpc != COLOR_DEPTH_8 &&
	    bpc != COLOR_DEPTH_10 && bpc != COLOR_DEPTH_12 &&
	    bpc != COLOR_DEPTH_16) {
		dptx_dbg(dptx, "%s: Invalid bits per component value %d\n",
			 __func__, bpc);
		return -EINVAL;
	}

	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;
	retval = dptx_video_ts_calculate(dptx, dptx->link.lanes,
					 dptx->link.rate, bpc,
					 vparams->pix_enc, mdtd->pixel_clock);
	if (retval)
		return retval;

	vparams->bpc = bpc;
	dptx_disable_default_video_stream(dptx, DEFAULT_STREAM);
	dptx_video_bpc_change(dptx, DEFAULT_STREAM);
	dptx_video_ts_change(dptx, DEFAULT_STREAM);
	dptx_video_pattern_change(dptx, DEFAULT_STREAM);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VG_SWRST_N(DEFAULT_STREAM), 0);
	dptx_enable_default_video_stream(dptx, DEFAULT_STREAM);

	dptx_dbg(dptx, "%s: Change bits per component to %d\n",
		 __func__, bpc);

	return retval;
}

/**
 * dptx_get_pattern() - Get video pattern value
 * @dptx: The dptx struct
 *
 * Returns video pattern value
 */
u8 dptx_get_pattern(struct dptx *dptx)
{
	struct video_params *vparams;

	vparams = &dptx->vparams;

	return vparams->pattern_mode;
}

/**
 * dptx_set_pattern() - Change video pattern
 * @dptx: The dptx struct
 * @pattern: Video pattern value
 * Possible options: 0 - Tile, 1 - Ramp, 2 - Chess, 3 - ColRamp
 *
 * Returns 0 on success otherwise negative errno.
 */
int dptx_set_pattern(struct dptx *dptx, u8 pattern)
{
	if (pattern > COLRAMP) {
		dptx_dbg(dptx, "%s: Invalid video pattern value %d\n",
			 __func__, pattern);
		return -EINVAL;
	}
	dptx_video_pattern_set(dptx, pattern, DEFAULT_STREAM);
	return 0;
}

/**
 * dptx_link_retrain() - Retrain link
 * @dptx: The dptx struct
 * @rate: Link rate - Possible options: 0 - RBR, 1 - HBR, 2 - HBR2, 3 - HBR3
 * @lanes: Link lanes count - Possible options 1,2 or 4
 *
 * Returns 0 on success otherwise negative errno.
 */
int dptx_link_retrain(struct dptx *dptx, u8 rate, u8 lanes)
{
	u32 hpdsts;
	struct video_params *vparams;
	struct dtd *mdtd;
	int retval = 0;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	hpdsts = dptx_read_regfield(dptx, ctrl_fields->field_hpd_status);
	if (!hpdsts) {
		dptx_dbg(dptx, "%s: Not connected\n", __func__);
		return  -ENODEV;
	}

	if (lanes != 1 && lanes != 2 && lanes != 4) {
		dptx_dbg(dptx, "%s: Invalid number of lanes %d\n",
			 __func__, lanes);
		return  -EINVAL;
	}

	if (rate > DPTX_PHYIF_CTRL_RATE_HBR3) {
		dptx_dbg(dptx, "%s: Invalid number of lane rate %d\n",
			 __func__, rate);
		return  -EINVAL;
	}
	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;
	retval = dptx_video_ts_calculate(dptx, lanes, rate, vparams->bpc,
					 vparams->pix_enc, mdtd->pixel_clock);
	if (retval)
		return retval;

	retval = dptx_set_link_configs(dptx, rate, lanes);
	retval = dptx_link_training(dptx);

	if (retval)
		return retval;

	dptx_video_ts_change(dptx, DEFAULT_STREAM);
	dptx_dbg(dptx, "%s: Retraining link rate=%d lanes=%d\n",
		 __func__, rate, lanes);

	return retval;
}

/**
 * dptx_get_edid() - Get EDID raw data
 * @dptx: The dptx struct
 * @buf: The buffer to copy the EDID into
 * @buflen: The length of the buffer
 *
 * This function copies the EDID into @buf and returns the amount of
 * byte written or a negative error code. It will not copy any more
 * than buflen bytes.
 */
int dptx_get_edid(struct dptx *dptx, u8 *buf, size_t buflen)
{
	u32 hpdsts;
	int retval = 0;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	hpdsts = dptx_read_regfield(dptx, ctrl_fields->field_hpd_status);
	if (!hpdsts) {
		dptx_dbg(dptx, "%s: Not connected\n", __func__);
		retval = -ENODEV;
		goto fail;
	}

	memcpy(buf, dptx->edid, buflen);
fail:
	return retval;
}

/**
 * dptx_get_edid_size() - Get the size of the EDID
 * @dptx: The dptx struct
 *
 * Returns the size in bytes of the EDID data or -EINVAL if there is
 * no EDID data.
 */
int dptx_get_edid_size(struct dptx *dptx)
{
	int blocks;
	u32 hpdsts;
	int retval = 0;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	hpdsts = dptx_read_regfield(dptx, ctrl_fields->field_hpd_status);
	if (!hpdsts) {
		dptx_dbg(dptx, "%s: Not connected\n", __func__);
		retval = -ENODEV;
		goto fail;
	}

	blocks = (dptx->edid[126] + 1) * 128;
	retval = blocks;
fail:
	return retval;
}

/**
 * dptx_sdp_disable() - Disable sending SDP
 * @dptx: The dptx struct
 * @buf: The buffer that contains SDP header and payload data
 *
 * Returns 0 on success or error code
 */
int dptx_sdp_disable(struct dptx *dptx, u32 *buf)
{
	struct sdp_full_data sdp;

	sdp.en = 0;
	memcpy(sdp.payload, buf, 9);
	sdp.blanking = 0;
	sdp.cont = 0;
	dptx_fill_sdp(dptx, &sdp);
	return 0;
}

/**
 * dptx_sdp_enable() - Enable sending SDP
 * @dptx: The dptx struct
 * @buf: The buffer that contains SDP header and payload data
 * @blanking: Specifys blanking period to send SDP
 * @cont: Send SDP continuously or only once
 *
 * Returns 0 on success or error code
 */
int dptx_sdp_enable(struct dptx *dptx, u32 *buf, u8 blanking, u8 cont)
{
	struct sdp_full_data sdp;

	sdp.en = 1;
	memcpy(sdp.payload, buf, 9);
	sdp.blanking = blanking;
	sdp.cont = cont;
	dptx_fill_sdp(dptx, &sdp);
	return 0;
}

/**
 * dptx_aux_transfer() - Send AUX transfers
 * @dptx: The dptx struct
 * @aux_msg: AUX message
 *
 * Returns result of AUX transfer on success otherwise negative errno.
 */
int dptx_aux_transfer(struct dptx *dptx, struct drm_dp_aux_msg *aux_msg)
{
	unsigned int addr;
	u8 req;
	void *buf;
	int len;
	int result;
	u32 hpdsts;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	hpdsts = dptx_read_regfield(dptx, ctrl_fields->field_hpd_status);
	if (!hpdsts) {
		dptx_dbg(dptx, "%s: Not connected\n", __func__);
		return -ENODEV;
	}

	result = 0;
	addr = aux_msg->address;
	req = aux_msg->request;
	buf = aux_msg->buffer;
	len = aux_msg->size;

	switch (req) {
	case DP_AUX_NATIVE_WRITE:
		result = dptx_aux_rw_bytes(dptx, false, false, addr, buf, len);
		break;
	case DP_AUX_NATIVE_READ:
		result = dptx_aux_rw_bytes(dptx, true, false, addr, buf, len);
		break;
	case DP_AUX_I2C_WRITE:
		result = dptx_write_bytes_to_i2c(dptx, addr, buf, len);
		break;
	case DP_AUX_I2C_READ:
		result = dptx_read_bytes_from_i2c(dptx, addr, buf, len);
		break;
	}

	return result;
}

/**
 * dptx_update_stream_mode() - Switch between SST and MST
 * @dptx: The dptx struct
 *
 * Switch between SST and MST on success otherwise negative errno.
 */
int dptx_update_stream_mode(struct dptx *dptx)
{
	int retval = 0;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	//Disable video
	dptx_write_regfield(dptx, ctrl_fields->field_video_stream_enable, 0);

	//Disable audio
	dptx_write_regfield(dptx, ctrl_fields->field_en_audio_stream_sdp_vertical_ctrl, 0x0);
	dptx_write_regfield(dptx, ctrl_fields->field_en_audio_stream_sdp_horizontal_ctrl, 0x0);
	dptx_write_regfield(dptx, ctrl_fields->field_en_audio_timestamp_sdp_vertical_ctrl, 0x0);
	dptx_write_regfield(dptx, ctrl_fields->field_en_audio_timestamp_sdp_horizontal_ctrl, 0x0);

	//Init Link Training
	retval = dptx_link_training(dptx);

	return retval;
}

/**
 * dptx_add_stream() - Add stream to transmition
 * @dptx: The dptx struct
 *
 * Add a new stream to the transmitter. Maximum number of streams allowed: 4
 */
int dptx_add_stream(struct dptx *dptx)
{
	int vc_payload_size;
	int stream;
	int tries;
	int i;
	u8 status;

	tries = 0;
	status = 0;

	stream = dptx->streams + 1;
	if (stream > DPTX_MAX_STREAM_NUMBER) {
		dptx_err(dptx, "ERROR: Max stream number achieved\n");
		return -EINVAL;
	}
	dptx->streams++;

	dptx_video_set_core_bpc(dptx, stream);
	dptx_video_set_timing_info(dptx, stream);
	dptx_video_set_MSA(dptx, stream);
	vc_payload_size =  dptx_get_vc_payload_size(dptx);

	dptx_err(dptx, "%s: --------------- Clearing DPCD VCPID table\n", __func__);
	dptx_dpcd_clear_vcpid_table(dptx);

	dptx_err(dptx, "%s: --------------- Clearing DPTX VCPID table\n", __func__);
	dptx_clear_vcpid_table(dptx);

	for (i = 0; i < dptx->streams; i++)
		dptx_set_vcpid_table_range(dptx, vc_payload_size * i + 1, vc_payload_size, i + 1);

	for (i = 0; i < dptx->streams; i++)
		dptx_dpcd_set_vcpid_table(dptx, vc_payload_size * i + 1, vc_payload_size, i + 1);

	dptx_print_vcpid_table(dptx);

	dptx_initiate_mst_act(dptx);

	//Wait for ACT_handled
	while (!(status & DP_PAYLOAD_ACT_HANDLED)) {
		dptx_read_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, &status);
		tries++;
		if (WARN(tries > 100, "Timeout waiting for ACT_HANDLED\n"))
			break;

		mdelay(20);
	}
	dptx_dbg(dptx, "%s: PAYLOAD ACT HANDLED\n", __func__);

	return 0;
}

/**
 * dptx_remove_stream() - Remove stream to transmition
 * @dptx: The dptx struct
 * @stream: The stream to remove
 * Add a new stream to the transmitter. Maximum number of streams allowed: 4
 */
int dptx_remove_stream(struct dptx *dptx, int stream)
{
	u8 payload_table[64] = {0};
	u32 reg;
	int i;
	int table_slot, start_slot, payload;
	int ret;

	// Update VC Payload ID Table (DPCD)
	ret = dptx_remove_stream_vcpid_table(dptx, stream);
	if (ret < 0)
		return -EINVAL;

	//Update VC Payload Allocation Table
	dptx_clear_vcpid_table(dptx);
	dptx_read_bytes_from_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, payload_table, 64);
	for (i = 0; i < DPTX_MAX_STREAM_NUMBER; i++) {
		start_slot = -1;
		payload = 0;
		for (table_slot = 1; table_slot < 64; table_slot++) {
			if (payload_table[table_slot] == i) {
				if (start_slot == -1)
					start_slot = table_slot;
				payload++;
			}
		}
		if (payload > 0)
			dptx_set_vcpid_table_range(dptx, start_slot, payload, i);
	}

	dptx_print_vcpid_table(dptx);

	// Trigger ACT Sequence
	dptx_initiate_mst_act(dptx);

	// Disable Video Stream
	reg = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_CTRL_N(stream - 1));
	reg &= ~BIT(5);
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_VSAMPLE_CTRL_N(stream - 1), reg);

	dptx->active_mst_vc_payload -= payload;
	dptx->streams--;

	return 0;
}

/**
 * dptx_enable_adaptive_sync() - Read Sink Adaptive-Sync Capabilities
 * @dptx: The dptx struct
 * @mode: The Adaptive Sync mode (AVT or FAVT) - for eDP only
 * Enable Adaptive Sync functionality
 */
int dptx_enable_adaptive_sync(struct dptx *dptx, u8 mode)
{
	u8 msa_timing_par_ignored;
	u8 adaptive_sync_sdp_supported;
	u8 dwnspread_ctrl;
	u8 sdp_transmission_timing;
	struct adaptive_sync_sdp_data sdp;

	//Check Sink features compatibility
	dptx_read_dpcd(dptx, DOWN_STREAM_PORT_COUNT, &msa_timing_par_ignored);
	msa_timing_par_ignored = (msa_timing_par_ignored & BIT(6)) >> 6;
	if (!msa_timing_par_ignored) {
		dptx_err(dptx, "MSA_TIMING_PAR_IGNORED not supported. Unable to use adaptive-sync.\n");
		return -EACCES;
	}
	dptx_read_dpcd(dptx, EXT_DOWN_STREAM_PORT_COUNT, &msa_timing_par_ignored);
	msa_timing_par_ignored = (msa_timing_par_ignored & BIT(6)) >> 6;
	if (!msa_timing_par_ignored) {
		dptx_err(dptx, "MSA_TIMING_PAR_IGNORED not supported. Unable to use adaptive-sync.\n");
		return -EACCES;
	}

	dptx_read_dpcd(dptx, DPRX_FEATURE_ENUMERATION_LIST_CONT_1, &adaptive_sync_sdp_supported);
	adaptive_sync_sdp_supported &= BIT(0);
	if (dptx->edp && !adaptive_sync_sdp_supported) {
		dptx_err(dptx, "Adaptive-sync SDP not supported. Unable to use adaptive-sync during eDP operation.\n");
		return -EACCES;
	}

	//Enable Sink features
	dptx_read_dpcd(dptx, DOWNSPREAD_CTRL, &dwnspread_ctrl);
	dwnspread_ctrl |= BIT(7);

	//If AS SDP is active, get SDP to the interface
	if (adaptive_sync_sdp_supported) {
		dwnspread_ctrl |= BIT(6);
		dptx_read_dpcd(dptx, ADAPTIVE_SYNC_SDP_TRANSMISSION_TIMING_CONFIG, &sdp_transmission_timing);
		sdp_transmission_timing |= BIT(7);
		dptx_write_dpcd(dptx, ADAPTIVE_SYNC_SDP_TRANSMISSION_TIMING_CONFIG, sdp_transmission_timing);
		dptx->adaptive_sync_sdp = true;
	}
	dptx_write_dpcd(dptx, DOWNSPREAD_CTRL, dwnspread_ctrl);

	fill_as_sdp_header(dptx, &sdp);
	fill_as_sdp(dptx, &sdp, mode);

	//Add AS SDP to register

	dptx->adaptive_sync = true;

	return 0;
}

/**
 * dptx_disable_adaptive_sync() - Disable Adaptive-Sync functionality
 * @dptx: The dptx struct
 * Disable Adaptive Sync functionality
 */
int dptx_disable_adaptive_sync(struct dptx *dptx)
{
	u8 dwnspread_ctrl;
	int retval;

	dptx_read_dpcd(dptx, DOWNSPREAD_CTRL, &dwnspread_ctrl);

	//Change input video to fixed Vfront mode

	//If AS SDP is active, clear it
	if (dptx->adaptive_sync_sdp)
		dwnspread_ctrl &= ~BIT(6);

	//Clear MSA Ignore
	dwnspread_ctrl &= ~BIT(7);
	retval = dptx_write_dpcd(dptx, DOWNSPREAD_CTRL, dwnspread_ctrl);
	if (retval)
		return -EINVAL;

	dptx->adaptive_sync = false;
	dptx_dbg(dptx, "Adaptive Sync STATE was: %d", dptx->adaptive_sync);

	return 0;
}

/**
 * dptx_check_adaptive_sync_status() - Check Adaptive Sync Status (Enabled or Disabled)
 * @dptx: The dptx struct
 * Disable Adaptive Sync functionality
 */
bool dptx_check_adaptive_sync_status(struct dptx *dptx)
{
	return dptx->adaptive_sync;
}

/**
 * dptx_set_audio_is_active() - Update audio is active or not
 * @dptx: The dptx struct
 * @active: audio active state
 * Update audio active state to dptx driver and signal audio
 * complete when active == false
 */
int dptx_set_audio_is_active(struct dptx *dptx, bool active)
{
	if (dptx == NULL)
		return -1;

	mutex_lock(&dptx->mutex);

	dptx->audio_enabled = active;

	if (!active)
		complete(&dptx->audio_disable_done);

	mutex_unlock(&dptx->mutex);
	return 0;
}

/**
 * dptx_get_device_handle() - Get device handle
 *
 * Returns pointer to device instance
 */
struct dptx *dptx_get_device_handle(void)
{
	return dptx_get_handle();
}

