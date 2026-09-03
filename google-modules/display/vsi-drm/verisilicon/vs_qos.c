// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <drm/drm_atomic.h>

#include <interconnect/google_icc_helper.h>

#include <linux/minmax.h>
#include <linux/pm_qos.h>
#include <linux/units.h>
#include <perf/core/google_pm_qos.h>

#include <trace/dpu_trace.h>

#include "vs_crtc.h"
#include "vs_dc.h"
#include "vs_dc_info.h"
#include "vs_qos.h"
#include "vs_trace.h"

#define HRT_VC 3

static bool update_qos_config(u32 *req, u32 *cur, u32 *pending)
{
	bool qos_updated = false;

	if (*req == *cur) {
		*pending = 0;
	} else if (*req > *cur) {
		*pending = 0;
		qos_updated = true;
	} else if (*pending) {
		if (*req >= *pending) {
			*pending = 0;
			qos_updated = true;
		} else {
			/* apply and update pending qos resource */
			u32 new_pending = *req;
			*req = *pending;
			*pending = new_pending;
			qos_updated = true;
		}
	} else {
		*pending = *req;
	}

	return qos_updated;
}

static void vs_qos_override_min_rd_bw(struct drm_crtc *crtc, struct vs_crtc_state *vs_crtc_state)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	struct device *dev = vs_crtc->dev;
	struct vs_dc *dc = dev_get_drvdata(dev);
	u32 min_rd_avg_bw_mbps = dc->min_qos_config.rd_avg_bw_mbps;
	u32 min_rd_peak_bw_mbps = dc->min_qos_config.rd_peak_bw_mbps;
	u32 min_rd_rt_bw_mbps = dc->min_qos_config.rd_rt_bw_mbps;

	if (min_rd_avg_bw_mbps > vs_crtc_state->qos_config.rd_avg_bw_mbps) {
		vs_crtc_state->qos_config.rd_avg_bw_mbps = min_rd_avg_bw_mbps;
		set_bit(VS_QOS_OVERRIDE_RD_AVG_BW, vs_crtc_state->qos_override);
		dev_dbg(dev, "override rd_avg to %u Mbps from min qos\n", min_rd_avg_bw_mbps);
	}

	if (min_rd_peak_bw_mbps > vs_crtc_state->qos_config.rd_peak_bw_mbps) {
		vs_crtc_state->qos_config.rd_peak_bw_mbps = min_rd_peak_bw_mbps;
		set_bit(VS_QOS_OVERRIDE_RD_PEAK_BW, vs_crtc_state->qos_override);
		dev_dbg(dev, "override rd_peak to %u Mbps from min qos\n", min_rd_peak_bw_mbps);
	}

	if (min_rd_rt_bw_mbps > vs_crtc_state->qos_config.rd_rt_bw_mbps) {
		vs_crtc_state->qos_config.rd_rt_bw_mbps = min_rd_rt_bw_mbps;
		set_bit(VS_QOS_OVERRIDE_RD_RT_BW, vs_crtc_state->qos_override);
		dev_dbg(dev, "override rd_rt to %u Mbps from min qos\n", min_rd_rt_bw_mbps);
	}
}

static void vs_qos_override_min_wr_bw(struct drm_crtc *crtc, struct vs_crtc_state *vs_crtc_state)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	struct device *dev = vs_crtc->dev;
	struct vs_dc *dc = dev_get_drvdata(dev);
	u32 min_wr_avg_bw_mbps = dc->min_qos_config.wr_avg_bw_mbps;
	u32 min_wr_peak_bw_mbps = dc->min_qos_config.wr_peak_bw_mbps;
	u32 min_wr_rt_bw_mbps = dc->min_qos_config.wr_rt_bw_mbps;

	if (min_wr_avg_bw_mbps > vs_crtc_state->qos_config.wr_avg_bw_mbps) {
		vs_crtc_state->qos_config.wr_avg_bw_mbps = min_wr_avg_bw_mbps;
		set_bit(VS_QOS_OVERRIDE_WR_AVG_BW, vs_crtc_state->qos_override);
		dev_dbg(dev, "override wr_avg to %u Mbps from min qos\n", min_wr_avg_bw_mbps);
	}

	if (min_wr_peak_bw_mbps > vs_crtc_state->qos_config.wr_peak_bw_mbps) {
		vs_crtc_state->qos_config.wr_peak_bw_mbps = min_wr_peak_bw_mbps;
		set_bit(VS_QOS_OVERRIDE_WR_PEAK_BW, vs_crtc_state->qos_override);
		dev_dbg(dev, "override wr_peak to %u Mbps from min qos\n", min_wr_peak_bw_mbps);
	}

	if (min_wr_rt_bw_mbps > vs_crtc_state->qos_config.wr_rt_bw_mbps) {
		vs_crtc_state->qos_config.wr_rt_bw_mbps = min_wr_rt_bw_mbps;
		set_bit(VS_QOS_OVERRIDE_WR_RT_BW, vs_crtc_state->qos_override);
		dev_dbg(dev, "override wr_rt to %u Mbps from min qos\n", min_wr_rt_bw_mbps);
	}
}

static void vs_qos_override_min_core_clk(struct drm_crtc *crtc, struct vs_crtc_state *vs_crtc_state)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	struct device *dev = vs_crtc->dev;
	struct vs_dc *dc = dev_get_drvdata(dev);
	u32 min_core_clk = dc->min_qos_config.core_clk;
	u32 scaling_min_freq = dc->core_devfreq->scaling_min_freq;

	if (min_core_clk > vs_crtc_state->qos_config.core_clk) {
		vs_crtc_state->qos_config.core_clk = min_core_clk;
		set_bit(VS_QOS_OVERRIDE_CORE_CLK, vs_crtc_state->qos_override);
		dev_dbg(dev, "override core_clk to %u Mbps from min clk\n", min_core_clk);
	}

	if (scaling_min_freq > vs_crtc_state->qos_config.core_clk) {
		vs_crtc_state->qos_config.core_clk = scaling_min_freq;
		set_bit(VS_QOS_OVERRIDE_CORE_CLK, vs_crtc_state->qos_override);
		dev_dbg(dev, "override core_clk to %u Mbps from scaling min\n", scaling_min_freq);
	}
}

static void vs_qos_override_min_qos(struct drm_crtc *crtc, struct drm_crtc_state *new_crtc_state)
{
	struct vs_crtc_state *vs_crtc_state = to_vs_crtc_state(new_crtc_state);
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);

	vs_qos_override_min_rd_bw(crtc, vs_crtc_state);
	vs_qos_override_min_wr_bw(crtc, vs_crtc_state);
	if (dev_pm_qos_request_active(&vs_crtc->core_devfreq_req))
		vs_qos_override_min_core_clk(crtc, vs_crtc_state);
}

static void vs_qos_set_fe(struct device *dev, struct drm_crtc *crtc)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	struct vs_dc *dc = dev_get_drvdata(vs_crtc->dev);
	const struct vs_dc_info *dc_info = dc->hw.info;
	struct dc_hw *hw = &dc->hw;
	int i;
	u32 rt_bw = 0;
	u32 new_pending;
	bool need_update = false;

	for (i = 0; i < DC_DISPLAY_NUM; i++) {
		if (!dc->crtc[i])
			continue;
		rt_bw += dc->crtc[i]->qos_config.rd_rt_bw_mbps;
		rt_bw += dc->crtc[i]->qos_config.wr_rt_bw_mbps;
	}

	if (dc_info->fe_axqos_threshold_mbps && rt_bw <= dc_info->fe_axqos_threshold_mbps)
		dc->fe_qos_config.req = dc_info->fe_axqos_high;
	else
		dc->fe_qos_config.req = dc_info->fe_axqos_low;

	if (dc->fe_qos_config.req == dc->fe_qos_config.cur) {
		dc->fe_qos_config.is_pending = false;
		dc->fe_qos_config.pending = 0;
	} else if (dc->fe_qos_config.req > dc->fe_qos_config.cur) {
		need_update = true;
		dc->fe_qos_config.is_pending = false;
		dc->fe_qos_config.pending = 0;
	} else if (dc->fe_qos_config.is_pending) {
		need_update = true;
		if (dc->fe_qos_config.req >= dc->fe_qos_config.pending) {
			dc->fe_qos_config.is_pending = false;
			dc->fe_qos_config.pending = 0;
		} else {
			new_pending = dc->fe_qos_config.req;
			dc->fe_qos_config.req = dc->fe_qos_config.pending;
			dc->fe_qos_config.pending = new_pending;
		}
	} else {
		dc->fe_qos_config.pending = dc->fe_qos_config.req;
		dc->fe_qos_config.is_pending = true;
	}

	if (need_update) {
		dev_dbg(dev, "the rt bw is %#x, set fe qos to %#x\n", rt_bw, dc->fe_qos_config.req);
		dc_hw_set_fe_qos(hw, dc->fe_qos_config.req);
		dc->fe_qos_config.cur = dc->fe_qos_config.req;
		DPU_ATRACE_INT_PID("set_fe_qos", dc->fe_qos_config.req, vs_crtc->trace_pid);
	}
}

static void vs_qos_calculate_sum_rd_bw(struct vs_dc *dc, u32 *avg_bw, u32 *peak_bw,
					u32 *rt_bw, u8 id)
{
	int i;

	for (i = 0; i < DC_DISPLAY_NUM; i++) {
		if (!dc->crtc[i] || i == id)
			continue;
		*avg_bw += dc->crtc[i]->qos_config.rd_avg_bw_mbps;
		*peak_bw += dc->crtc[i]->qos_config.rd_peak_bw_mbps;
		*rt_bw += dc->crtc[i]->qos_config.rd_rt_bw_mbps;
	}
}

static void vs_qos_calculate_sum_wr_bw(struct vs_dc *dc, u32 *avg_bw, u32 *peak_bw,
					u32 *rt_bw, u8 id)
{
	int i;

	for (i = 0; i < DC_DISPLAY_NUM; i++) {
		if (!dc->crtc[i] || i == id)
			continue;
		*avg_bw += dc->crtc[i]->qos_config.wr_avg_bw_mbps;
		*peak_bw += dc->crtc[i]->qos_config.wr_peak_bw_mbps;
		*rt_bw += dc->crtc[i]->qos_config.wr_rt_bw_mbps;
	}
}

static bool vs_qos_set_rd_bw(struct device *dev, struct drm_crtc *crtc)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	struct vs_dc *dc = dev_get_drvdata(vs_crtc->dev);
	struct vs_crtc_state *vs_crtc_state = to_vs_crtc_state(crtc->state);
	u32 rd_avg_bw_mbps = vs_crtc_state->qos_config.rd_avg_bw_mbps;
	u32 rd_peak_bw_mbps = vs_crtc_state->qos_config.rd_peak_bw_mbps;
	u32 rd_rt_bw_mbps = vs_crtc_state->qos_config.rd_rt_bw_mbps;
	u32 req_rd_avg_bw_mbps, req_rd_peak_bw_mbps, req_rd_rt_bw_mbps;
	bool rd_bw_updated = false;
	int ret;

	/* update read bandwidth requirement */
	rd_bw_updated = update_qos_config(&rd_avg_bw_mbps,
					  &vs_crtc->qos_config.rd_avg_bw_mbps,
					  &vs_crtc->pending_qos_config.rd_avg_bw_mbps);
	rd_bw_updated |= update_qos_config(&rd_peak_bw_mbps,
					   &vs_crtc->qos_config.rd_peak_bw_mbps,
					   &vs_crtc->pending_qos_config.rd_peak_bw_mbps);
	rd_bw_updated |= update_qos_config(&rd_rt_bw_mbps,
					   &vs_crtc->qos_config.rd_rt_bw_mbps,
					   &vs_crtc->pending_qos_config.rd_rt_bw_mbps);

	if (rd_bw_updated) {
		req_rd_avg_bw_mbps = rd_avg_bw_mbps;
		req_rd_peak_bw_mbps = rd_peak_bw_mbps;
		req_rd_rt_bw_mbps = rd_rt_bw_mbps;
		vs_qos_calculate_sum_rd_bw(dc, &req_rd_avg_bw_mbps, &req_rd_peak_bw_mbps,
					   &req_rd_rt_bw_mbps, vs_crtc->id);

		trace_disp_qos_set_rd_bw(vs_crtc->id, req_rd_avg_bw_mbps, req_rd_peak_bw_mbps,
					 req_rd_rt_bw_mbps);
		dev_dbg(dev, "set rd_avg_bw: %u Mbps rd_peak_bw: %u Mbps rd_rt_bw: %u Mbps\n",
			req_rd_avg_bw_mbps, req_rd_peak_bw_mbps, req_rd_rt_bw_mbps);

		DPU_ATRACE_BEGIN("dpu_qos_update_rd_bw");
		ret = google_icc_set_read_bw_gmc(dc->path, req_rd_avg_bw_mbps, req_rd_peak_bw_mbps,
						 req_rd_rt_bw_mbps, HRT_VC);
		DPU_ATRACE_END("dpu_qos_update_rd_bw");
		if (ret) {
			rd_bw_updated = false;
			dev_err(dev, "failed to set read bandwidth, %d\n", ret);
			goto end;
		}
		vs_crtc->qos_config.rd_avg_bw_mbps = rd_avg_bw_mbps;
		vs_crtc->qos_config.rd_peak_bw_mbps = rd_peak_bw_mbps;
		vs_crtc->qos_config.rd_rt_bw_mbps = rd_rt_bw_mbps;
		DPU_ATRACE_INT_PID("set_rd_avg_bw_mbps", req_rd_avg_bw_mbps, vs_crtc->trace_pid);
		DPU_ATRACE_INT_PID("set_rd_peak_bw_mbps", req_rd_peak_bw_mbps, vs_crtc->trace_pid);
		DPU_ATRACE_INT_PID("set_rd_rt_bw_mbps", req_rd_rt_bw_mbps, vs_crtc->trace_pid);
	}

end:
	return rd_bw_updated;
}

static bool vs_qos_set_wr_bw(struct device *dev, struct drm_crtc *crtc)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	struct vs_dc *dc = dev_get_drvdata(vs_crtc->dev);
	struct vs_crtc_state *vs_crtc_state = to_vs_crtc_state(crtc->state);
	u32 wr_avg_bw_mbps = vs_crtc_state->qos_config.wr_avg_bw_mbps;
	u32 wr_peak_bw_mbps = vs_crtc_state->qos_config.wr_peak_bw_mbps;
	u32 wr_rt_bw_mbps = vs_crtc_state->qos_config.wr_rt_bw_mbps;
	u32 req_wr_avg_bw_mbps, req_wr_peak_bw_mbps, req_wr_rt_bw_mbps;
	bool wr_bw_updated = false;
	int ret;

	/* update write bandwidth requirement */
	wr_bw_updated = update_qos_config(&wr_avg_bw_mbps,
					  &vs_crtc->qos_config.wr_avg_bw_mbps,
					  &vs_crtc->pending_qos_config.wr_avg_bw_mbps);
	wr_bw_updated |= update_qos_config(&wr_peak_bw_mbps,
					   &vs_crtc->qos_config.wr_peak_bw_mbps,
					   &vs_crtc->pending_qos_config.wr_peak_bw_mbps);
	wr_bw_updated |= update_qos_config(&wr_rt_bw_mbps,
					   &vs_crtc->qos_config.wr_rt_bw_mbps,
					   &vs_crtc->pending_qos_config.wr_rt_bw_mbps);

	if (wr_bw_updated) {
		req_wr_avg_bw_mbps = wr_avg_bw_mbps;
		req_wr_peak_bw_mbps = wr_peak_bw_mbps;
		req_wr_rt_bw_mbps = wr_rt_bw_mbps;
		vs_qos_calculate_sum_wr_bw(dc, &req_wr_avg_bw_mbps, &req_wr_peak_bw_mbps,
					   &req_wr_rt_bw_mbps, vs_crtc->id);

		trace_disp_qos_set_wr_bw(vs_crtc->id, req_wr_avg_bw_mbps, req_wr_peak_bw_mbps,
					 req_wr_rt_bw_mbps);
		dev_dbg(dev, "set wr_avg_bw: %u Mbps wr_peak_bw: %u Mbps wr_rt_bw: %u Mbps\n",
			req_wr_avg_bw_mbps, req_wr_peak_bw_mbps, req_wr_rt_bw_mbps);

		DPU_ATRACE_BEGIN("dpu_qos_update_wr_bw");
		ret = google_icc_set_write_bw_gmc(dc->path, req_wr_avg_bw_mbps, req_wr_peak_bw_mbps,
						  req_wr_rt_bw_mbps, HRT_VC);
		DPU_ATRACE_END("dpu_qos_update_wr_bw");
		if (ret) {
			wr_bw_updated = false;
			dev_err(dev, "failed to set write bandwidth, %d\n", ret);
			goto end;
		}
		vs_crtc->qos_config.wr_avg_bw_mbps = wr_avg_bw_mbps;
		vs_crtc->qos_config.wr_peak_bw_mbps = wr_peak_bw_mbps;
		vs_crtc->qos_config.wr_rt_bw_mbps = wr_rt_bw_mbps;
		DPU_ATRACE_INT_PID("set_wr_avg_bw_mbps", req_wr_avg_bw_mbps, vs_crtc->trace_pid);
		DPU_ATRACE_INT_PID("set_wr_peak_bw_mbps", req_wr_peak_bw_mbps, vs_crtc->trace_pid);
		DPU_ATRACE_INT_PID("set_wr_rt_bw_mbps", req_wr_rt_bw_mbps, vs_crtc->trace_pid);
	}

end:
	return wr_bw_updated;
}

static int vs_qos_set_bw(struct device *dev, struct drm_crtc *crtc)
{
	int ret = 0;
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	struct vs_dc *dc = dev_get_drvdata(vs_crtc->dev);
	const struct vs_dc_info *dc_info = dc->hw.info;
	bool rd_bw_updated, wr_bw_updated;

	if (!dc->path) {
		ret = -EINVAL;
		goto end;
	}

	mutex_lock(&dc->dc_qos_lock);
	rd_bw_updated = vs_qos_set_rd_bw(dev, crtc);
	wr_bw_updated = vs_qos_set_wr_bw(dev, crtc);
	if (dc_info->fe_axqos_threshold_mbps)
		vs_qos_set_fe(dev, crtc);
	mutex_unlock(&dc->dc_qos_lock);

	if (rd_bw_updated || wr_bw_updated) {
		dev_dbg(dev, "update bw constraints\n");
		DPU_ATRACE_BEGIN("dpu_qos_update_bw_sync");
		ret = google_icc_update_constraint(dc->path);
		DPU_ATRACE_END("dpu_qos_update_bw_sync");
		if (ret)
			dev_err(dev, "failed to update bandwidth, %d\n", ret);
	}

end:
	return ret;
}

static int vs_qos_set_core_clk(struct device *dev, struct drm_crtc *crtc)
{
	struct vs_dc *dc = dev_get_drvdata(dev);
	int ret = 0;
	bool core_clk_updated = false;
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	struct vs_crtc_state *vs_crtc_state = to_vs_crtc_state(crtc->state);
	u32 core_clk = vs_crtc_state->qos_config.core_clk;
	u32 core_clk_khz;

	core_clk_updated = update_qos_config(&core_clk,
					     &vs_crtc->qos_config.core_clk,
					     &vs_crtc->pending_qos_config.core_clk);
	if (core_clk_updated && dc->core_devfreq != NULL) {
		core_clk_khz = core_clk / HZ_PER_KHZ;
		trace_disp_qos_set_core_clk(vs_crtc->id, core_clk_khz);
		dev_dbg(dev, "set core_clk to %u kHz\n", core_clk_khz);
		DPU_ATRACE_BEGIN("dpu_qos_update_core_clock");
		ret = dev_pm_qos_update_request(&vs_crtc->core_devfreq_req, core_clk_khz);
		DPU_ATRACE_END("dpu_qos_update_core_clock");

		if (ret < 0)
			dev_err(dev, "failed to update core_clk to %u kHz, %d\n",
				core_clk_khz, ret);
		else
			vs_crtc->qos_config.core_clk = core_clk;
	}

	return ret;
}

static u32 vs_qos_calc_fe_clk(const struct vs_dc_info *dc_info, u16 hdisplay, u16 vdisplay, u32 fps,
			      u32 crtc_h)
{
	u64 fe_clk;

	fe_clk = (u64)hdisplay * vdisplay * fps / dc_info->ppc;
	fe_clk = mult_frac(fe_clk,
			   (100 + dc_info->vblank_margin_pct + dc_info->h_bubble_pct) * vdisplay,
			   100 * crtc_h);
	return fe_clk;
}

static u32 vs_qos_calc_be_clk(const struct vs_dc_info *dc_info, u16 hdisplay, u16 vdisplay, u32 fps)
{
	u64 be_clk;

	be_clk = (u64)hdisplay * vdisplay * fps / dc_info->ppc;
	be_clk = mult_frac(be_clk, 100 + dc_info->vblank_margin_pct + dc_info->h_bubble_pct, 100);
	return be_clk;
}

static u32 vs_qos_calc_axi_clk(const struct vs_dc_info *dc_info, u32 peak_bw_mbps)
{
	u32 axi_clk_mhz = mult_frac(peak_bw_mbps, 100,
				    dc_info->axi_bus_bit_width * dc_info->axi_bus_util_pct / 8);

	return axi_clk_mhz * HZ_PER_MHZ;
}

static u32 vs_qos_calc_full_size_avgbw(u16 hdisplay, u16 vdisplay, u32 fps)
{
	return DIV_ROUND_UP((u64)hdisplay * vdisplay * 4 * fps, MEGA);
}

static u32 vs_qos_calc_full_size_rtbw(const struct vs_dc_info *dc_info, u32 avg_bw_mbps)
{
	return mult_frac(avg_bw_mbps, 100 + dc_info->vblank_margin_pct, 100);
}

static void vs_qos_calc_default_qos(struct drm_crtc *crtc, struct drm_crtc_state *new_crtc_state,
				    struct vs_qos_config *qos_config, bool is_wb)
{
	struct drm_plane *plane;
	const struct drm_plane_state *plane_state;
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	struct device *dev = vs_crtc->dev;
	struct vs_dc *dc = dev_get_drvdata(dev);
	const struct vs_dc_info *dc_info = dc->hw.info;
	u32 hdisplay, vdisplay, fps, plane_num;
	u32 avg_bw_mbps, rt_bw_mbps, be_clk, axi_clk, core_clk, fe_clk, max_fe_clk = 0;

	plane_num = hweight32(new_crtc_state->plane_mask);

	/* TODO(b/362456139): according drm_display_mode to set fps */
	fps = 120;
	hdisplay = new_crtc_state->mode.hdisplay;
	vdisplay = new_crtc_state->mode.vdisplay;
	drm_atomic_crtc_state_for_each_plane_state(plane, plane_state, new_crtc_state) {
		struct vs_plane_state *vs_plane_state = to_vs_plane_state(plane_state);

		fe_clk = vs_qos_calc_fe_clk(dc_info, hdisplay, vdisplay, fps, plane_state->crtc_h);
		if (fe_clk > max_fe_clk)
			max_fe_clk = fe_clk;

		dev_dbg(dev, "%s: [%d] max_fe_clk %u fe_clk %u fps %d vdisplay %d, crtc_h %d\n",
			__func__, plane_num, max_fe_clk, fe_clk, fps, vdisplay,
			plane_state->crtc_h);

		if (vs_plane_state->fb_ext)
			plane_num++;
	}

	be_clk = vs_qos_calc_be_clk(dc_info, hdisplay, vdisplay, fps);
	avg_bw_mbps = vs_qos_calc_full_size_avgbw(hdisplay, vdisplay, fps);
	rt_bw_mbps = vs_qos_calc_full_size_rtbw(dc_info, avg_bw_mbps);

	qos_config->rd_avg_bw_mbps = plane_num * avg_bw_mbps;
	qos_config->rd_peak_bw_mbps = mult_frac(
		plane_num, rt_bw_mbps * (100 + dc_info->default_rd_peak_bw_margin_pct), 100);
	qos_config->rd_rt_bw_mbps = mult_frac(
		plane_num, rt_bw_mbps * (100 + dc_info->default_rd_rt_bw_margin_pct), 100);
	if (qos_config->rd_rt_bw_mbps < dc_info->default_rd_rt_bw_min_mbps)
		qos_config->rd_rt_bw_mbps = dc_info->default_rd_rt_bw_min_mbps;
	if (is_wb) {
		qos_config->wr_avg_bw_mbps = avg_bw_mbps;
		qos_config->wr_rt_bw_mbps = rt_bw_mbps;
		qos_config->wr_peak_bw_mbps = rt_bw_mbps;
		axi_clk = vs_qos_calc_axi_clk(dc_info, rt_bw_mbps);
	} else {
		qos_config->wr_avg_bw_mbps = 0;
		qos_config->wr_rt_bw_mbps = 0;
		qos_config->wr_peak_bw_mbps = 0;
		axi_clk = vs_qos_calc_axi_clk(dc_info, vs_crtc->qos_config.rd_peak_bw_mbps);
	}

	core_clk = max3(max_fe_clk, be_clk, axi_clk);
	if (core_clk > qos_config->core_clk)
		qos_config->core_clk = core_clk;

	dev_dbg(dev, "%s: core clk %u Hz, plane num %u, is_wb %d, fps %d\n", __func__,
		qos_config->core_clk, plane_num, is_wb, fps);
	dev_dbg(dev, "%s: rd_avg: %u Mbps rd_peak: %u Mbps rd_rt: %u Mbps\n", __func__,
		qos_config->rd_avg_bw_mbps, qos_config->rd_peak_bw_mbps, qos_config->rd_rt_bw_mbps);
	dev_dbg(dev, "%s: wr_avg: %u Mbps wr_peak: %u Mbps wr_rt: %u Mbps\n", __func__,
		qos_config->wr_avg_bw_mbps, qos_config->wr_peak_bw_mbps, qos_config->wr_rt_bw_mbps);
}

static void vs_qos_override_default_qos(struct drm_crtc *crtc,
					struct drm_crtc_state *new_crtc_state)
{
	struct drm_atomic_state *state = new_crtc_state->state;
	struct drm_connector *conn;
	const struct drm_connector_state *conn_state;
	struct vs_crtc_state *vs_crtc_state = to_vs_crtc_state(new_crtc_state);
	bool is_wb = false;
	int i;

	for_each_new_connector_in_state(state, conn, conn_state, i) {
		if (!conn_state->crtc)
			continue;
		if (conn_state->crtc != crtc)
			continue;
		if (conn->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			is_wb = true;
		break;
	}

	if (!is_wb && vs_crtc_state->qos_config.rd_avg_bw_mbps)
		return;
	if (is_wb && vs_crtc_state->qos_config.wr_avg_bw_mbps)
		return;

	vs_qos_calc_default_qos(crtc, new_crtc_state, &vs_crtc_state->qos_config, is_wb);
	bitmap_fill(vs_crtc_state->qos_override, VS_QOS_OVERRIDE_COUNT);
}

int vs_qos_check_qos_config(struct drm_crtc *crtc, struct drm_crtc_state *new_crtc_state)
{
	struct vs_crtc_state *vs_crtc_state = to_vs_crtc_state(new_crtc_state);

	memcpy(&vs_crtc_state->qos_config, &vs_crtc_state->requested_qos_config,
	       sizeof(vs_crtc_state->qos_config));

	bitmap_zero(vs_crtc_state->qos_override, VS_QOS_OVERRIDE_COUNT);
	vs_qos_override_default_qos(crtc, new_crtc_state);
	vs_qos_override_min_qos(crtc, new_crtc_state);

	return 0;
}

void vs_qos_set_qos_config(struct device *dev, struct drm_crtc *crtc)
{
	vs_qos_set_bw(dev, crtc);
	vs_qos_set_core_clk(dev, crtc);
}

void vs_qos_clear_qos_configs(struct vs_dc *dc)
{
	struct dc_hw *hw = &dc->hw;
	const struct vs_dc_info *dc_info = dc->hw.info;
	int i;

	if (!dc->path || !dc_info)
		return;

	DPU_ATRACE_BEGIN(__func__);

	google_icc_set_read_bw_gmc(dc->path, 0, 0, 0, HRT_VC);
	google_icc_set_write_bw_gmc(dc->path, 0, 0, 0, HRT_VC);
	google_icc_update_constraint(dc->path);

	for (i = 0; i < DC_DISPLAY_NUM; i++) {
		if (!dc->crtc[i])
			continue;

		if (dc->crtc[i]->qos_config.core_clk && dc->core_devfreq != NULL)
			if (dev_pm_qos_request_active(&dc->crtc[i]->core_devfreq_req))
				dev_pm_qos_update_request(&dc->crtc[i]->core_devfreq_req,
					dc->core_devfreq->scaling_min_freq / HZ_PER_KHZ);

		memset(&dc->crtc[i]->qos_config, 0,  sizeof(dc->crtc[i]->qos_config));
		DPU_ATRACE_INT_PID("set_rd_avg_bw_mbps", 0, dc->crtc[i]->trace_pid);
		DPU_ATRACE_INT_PID("set_rd_peak_bw_mbps", 0, dc->crtc[i]->trace_pid);
		DPU_ATRACE_INT_PID("set_rd_rt_bw_mbps", 0, dc->crtc[i]->trace_pid);
		DPU_ATRACE_INT_PID("set_wr_avg_bw_mbps", 0, dc->crtc[i]->trace_pid);
		DPU_ATRACE_INT_PID("set_wr_peak_bw_mbps", 0, dc->crtc[i]->trace_pid);
		DPU_ATRACE_INT_PID("set_wr_rt_bw_mbps", 0, dc->crtc[i]->trace_pid);
		DPU_ATRACE_INT_PID("set_fe_qos", 0, dc->crtc[i]->trace_pid);
	}

	memset(&dc->fe_qos_config, 0, sizeof(dc->fe_qos_config));
	if (dc_info->fe_axqos_threshold_mbps)
		dc_hw_set_fe_qos(hw, 0);

	DPU_ATRACE_END(__func__);
}

void vs_qos_set_freq_boost(struct drm_crtc *crtc)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	struct device *dev = vs_crtc->dev;
	struct vs_dc *dc = dev_get_drvdata(dev);

	trace_disp_qos_boost_fab_clk(vs_crtc->id, dc->boost_fab_freq, dc->boost_core_freq);
	DPU_ATRACE_BEGIN("%s to %u,%u", __func__, dc->boost_fab_freq, dc->boost_core_freq);
	if (dc->boost_fab_freq && dc->boost_fab_devfreq)
		dev_pm_qos_update_request(&vs_crtc->boost_fab_devfreq_req, dc->boost_fab_freq);
	if (dc->boost_core_freq && dc->boost_core_devfreq)
		dev_pm_qos_update_request(&vs_crtc->boost_core_devfreq_req, dc->boost_core_freq);
	DPU_ATRACE_END("%s to %u,%u", __func__, dc->boost_fab_freq, dc->boost_core_freq);
}

void vs_qos_clear_freq_boost(struct drm_crtc *crtc)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	struct device *dev = vs_crtc->dev;
	struct vs_dc *dc = dev_get_drvdata(dev);

	trace_disp_qos_boost_fab_clk(vs_crtc->id, 0, 0);
	DPU_ATRACE_BEGIN(__func__);
	if (dc->boost_fab_freq && dc->boost_fab_devfreq)
		dev_pm_qos_update_request(&vs_crtc->boost_fab_devfreq_req,
					  dc->boost_fab_devfreq->scaling_min_freq / HZ_PER_KHZ);
	if (dc->boost_core_freq && dc->boost_core_devfreq)
		dev_pm_qos_update_request(&vs_crtc->boost_core_devfreq_req,
					  dc->boost_core_devfreq->scaling_min_freq / HZ_PER_KHZ);

	DPU_ATRACE_END(__func__);
}

int vs_qos_add_devfreq_request(struct vs_dc *dc, struct drm_crtc *crtc,
			       enum dev_pm_qos_req_type type, s32 value)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	int ret;

#if IS_ENABLED(CONFIG_GOOGLE_PM_QOS)
	ret = google_pm_qos_add_devfreq_request(dc->core_devfreq, &vs_crtc->core_devfreq_req,
						DEV_PM_QOS_MIN_FREQUENCY,
						PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
#else
	ret = dev_pm_qos_add_request(dc->core_devfreq->dev.parent, &vs_crtc->core_devfreq_req,
				     DEV_PM_QOS_MIN_FREQUENCY, PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
#endif

	return ret;
}

int vs_qos_remove_devfreq_request(struct drm_crtc *crtc)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
#if IS_ENABLED(CONFIG_GOOGLE_PM_QOS)
	struct device *dev = vs_crtc->dev;
	struct vs_dc *dc = dev_get_drvdata(dev);
	int ret;

	ret = google_pm_qos_remove_devfreq_request(dc->core_devfreq, &vs_crtc->core_devfreq_req);
#else
	ret = dev_pm_qos_remove_request(&vs_crtc->core_devfreq_req);
#endif

	return ret;
}

int vs_qos_add_boost_fab_devfreq_request(struct vs_dc *dc, struct drm_crtc *crtc,
					 enum dev_pm_qos_req_type type, s32 value)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	int ret;

#if IS_ENABLED(CONFIG_GOOGLE_PM_QOS)
	ret = google_pm_qos_add_devfreq_request(dc->boost_fab_devfreq,
						&vs_crtc->boost_fab_devfreq_req,
						DEV_PM_QOS_MIN_FREQUENCY,
						PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
#else
	ret = dev_pm_qos_add_request(dc->boost_fab_devfreq->dev.parent,
				     &vs_crtc->boost_fab_devfreq_req, DEV_PM_QOS_MIN_FREQUENCY,
				     PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
#endif

	return ret;
}

int vs_qos_remove_boost_fab_devfreq_request(struct drm_crtc *crtc)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
#if IS_ENABLED(CONFIG_GOOGLE_PM_QOS)
	struct device *dev = vs_crtc->dev;
	struct vs_dc *dc = dev_get_drvdata(dev);
	int ret;

	ret = google_pm_qos_remove_devfreq_request(dc->boost_fab_devfreq,
						   &vs_crtc->boost_fab_devfreq_req);
#else
	ret = dev_pm_qos_remove_request(&vs_crtc->boost_fab_devfreq_req);
#endif

	return ret;
}

int vs_qos_add_boost_core_devfreq_request(struct vs_dc *dc, struct drm_crtc *crtc,
					  enum dev_pm_qos_req_type type, s32 value)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	int ret;

#if IS_ENABLED(CONFIG_GOOGLE_PM_QOS)
	ret = google_pm_qos_add_devfreq_request(dc->boost_core_devfreq,
						&vs_crtc->boost_core_devfreq_req,
						DEV_PM_QOS_MIN_FREQUENCY,
						PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
#else
	ret = dev_pm_qos_add_request(dc->boost_core_devfreq->dev.parent,
				     &vs_crtc->boost_core_devfreq_req, DEV_PM_QOS_MIN_FREQUENCY,
				     PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
#endif

	return ret;
}

int vs_qos_remove_boost_core_devfreq_request(struct drm_crtc *crtc)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
#if IS_ENABLED(CONFIG_GOOGLE_PM_QOS)
	struct device *dev = vs_crtc->dev;
	struct vs_dc *dc = dev_get_drvdata(dev);
	int ret;

	ret = google_pm_qos_remove_devfreq_request(dc->boost_core_devfreq,
						   &vs_crtc->boost_core_devfreq_req);
#else
	ret = dev_pm_qos_remove_request(&vs_crtc->boost_core_devfreq_req);
#endif

	return ret;
}
