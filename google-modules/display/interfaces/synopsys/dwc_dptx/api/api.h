/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#ifndef __DPTX_API_H__
#define __DPTX_API_H__

#include "../include/dptx.h"
#include "../include/edp.h"

u8 dptx_get_audio_data_width(struct dptx *dptx);
u8 dptx_get_audio_num_ch(struct dptx *dptx);
u8 dptx_get_audio_inf_type(struct dptx *dptx);
u8 dptx_enable_audio_generator(struct dptx *dptx);
u8 dptx_disable_audio_generator(struct dptx *dptx);

u8 dptx_get_video_format(struct dptx *dptx);
u8 dptx_get_link_lane_count(struct dptx *dptx);
u8 dptx_get_link_rate(struct dptx *dptx);
int dptx_aux_transfer(struct dptx *dptx, struct drm_dp_aux_msg *aux_msg);
u8 dptx_get_pixel_enc(struct dptx *dptx);
u8 dptx_get_video_mode(struct dptx *dptx);
u8 dptx_get_bpc(struct dptx *dptx);
u8 dptx_get_video_colorimetry(struct dptx *dptx);
u8 dptx_get_video_dynamic_range(struct dptx *dptx);
u8 dptx_get_pattern(struct dptx *dptx);

int dptx_set_pixel_enc(struct dptx *dptx, u8 pix_enc);
int dptx_set_bpc(struct dptx *dptx, u8 bpc);
int dptx_set_video_mode(struct dptx *dptx, u8 vmode);
int dptx_set_video_colorimetry(struct dptx *dptx, u8 video_col);
int dptx_set_video_dynamic_range(struct dptx *dptx, u8 dynamic_range);
int dptx_set_video_format(struct dptx *dptx, u8 video_format);
int dptx_set_pattern(struct dptx *dptx, u8 pattern);

int dptx_link_retrain(struct dptx *dptx, u8 rate, u8 lanes);
int dptx_get_edid(struct dptx *dptx, u8 *buf, size_t buflen);
int dptx_get_edid_size(struct dptx *dptx);

int dptx_sdp_disable(struct dptx *dptx, u32 *buf);
int dptx_sdp_enable(struct dptx *dptx, u32 *buf, u8 blanking, u8 cont);

int dptx_update_stream_mode(struct dptx *dptx);
int dptx_add_stream(struct dptx *dptx);
int dptx_remove_stream(struct dptx *dptx, int stream);

int dptx_enable_adaptive_sync(struct dptx *dptx, u8 mode);
int dptx_disable_adaptive_sync(struct dptx *dptx);
bool dptx_check_adaptive_sync_status(struct dptx *dptx);

int dptx_set_audio_is_active(struct dptx *dptx, bool active);

struct dptx *dptx_get_device_handle(void);
#endif /* __DPTX_API_H__  */
