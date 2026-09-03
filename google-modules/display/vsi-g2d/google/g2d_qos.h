/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#ifndef _G2D_QOS_H_
#define _G2D_QOS_H_

struct g2d_qos_config {
	u32 core_clk;
	u32 rd_avg_bw_mbps;
	u32 rd_peak_bw_mbps;
	u32 wr_avg_bw_mbps;
	u32 wr_peak_bw_mbps;
};

struct g2d_device;
struct g2d_crtc;

void g2d_qos_init(struct device *dev, struct g2d_device *g2d_device);
int g2d_qos_remove_devfreq_request(struct drm_crtc *crtc);
void g2d_qos_create_crtc_qos_properties(struct g2d_device *g2d_device, struct g2d_crtc *g2d_crtc);
void g2d_qos_set_qos_config(struct g2d_device *g2d_device, struct drm_crtc *crtc);
void g2d_qos_clear_qos_configs(struct g2d_device *g2d_device, struct drm_crtc *crtc);
void g2d_qos_restore_qos_configs(struct g2d_device *g2d_device, struct drm_crtc *crtc);

#endif /* _G2D_QOS_H_ */
