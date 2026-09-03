/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __VS_QOS_H__
#define __VS_QOS_H__

enum vs_qos_override {
	VS_QOS_OVERRIDE_CORE_CLK,
	VS_QOS_OVERRIDE_RD_AVG_BW,
	VS_QOS_OVERRIDE_RD_PEAK_BW,
	VS_QOS_OVERRIDE_RD_RT_BW,
	VS_QOS_OVERRIDE_WR_AVG_BW,
	VS_QOS_OVERRIDE_WR_PEAK_BW,
	VS_QOS_OVERRIDE_WR_RT_BW,
	VS_QOS_OVERRIDE_COUNT,
};

struct vs_qos_config {
	u32 core_clk;
	u32 rd_avg_bw_mbps;
	u32 rd_peak_bw_mbps;
	u32 rd_rt_bw_mbps;
	u32 wr_avg_bw_mbps;
	u32 wr_peak_bw_mbps;
	u32 wr_rt_bw_mbps;
};

struct vs_fe_qos_config {
	u32 req;
	u32 cur;
	u32 pending;
	bool is_pending;
};

int vs_qos_check_qos_config(struct drm_crtc *crtc, struct drm_crtc_state *crtc_state);
void vs_qos_set_qos_config(struct device *dev, struct drm_crtc *crtc);
void vs_qos_clear_qos_configs(struct vs_dc *dc);

void vs_qos_set_freq_boost(struct drm_crtc *crtc);
void vs_qos_clear_freq_boost(struct drm_crtc *crtc);
int vs_qos_add_devfreq_request(struct vs_dc *dc, struct drm_crtc *crtc,
			       enum dev_pm_qos_req_type type, s32 value);
int vs_qos_remove_devfreq_request(struct drm_crtc *crtc);

int vs_qos_add_boost_fab_devfreq_request(struct vs_dc *dc, struct drm_crtc *crtc,
					 enum dev_pm_qos_req_type type, s32 value);
int vs_qos_remove_boost_fab_devfreq_request(struct drm_crtc *crtc);
int vs_qos_add_boost_core_devfreq_request(struct vs_dc *dc, struct drm_crtc *crtc,
					  enum dev_pm_qos_req_type type, s32 value);
int vs_qos_remove_boost_core_devfreq_request(struct drm_crtc *crtc);

#endif /* __VS_QOS_H__ */
