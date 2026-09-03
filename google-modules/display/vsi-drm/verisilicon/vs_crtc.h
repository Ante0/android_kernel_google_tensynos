/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_CRTC_H__
#define __VS_CRTC_H__

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/vs_drm.h>

#include <linux/kthread.h>
#include <linux/pm_qos.h>
#include <linux/types.h>

#include "vs_dc_drm_property.h"
#include "vs_dc_hw.h"
#include "vs_dc_info.h"
#include "vs_dc_property.h"
#include "vs_gem.h"
#include "vs_gem_pool.h"
#include "vs_module_params.h"
#include "vs_qos.h"
#include "vs_recovery.h"

/**
 * enum drm_vs_power_state - list of CRTC power state
 * @VS_POWER_STATE_OFF:    indicates CRTC power state OFF
 * @VS_POWER_STATE_ON:     indicates CRTC power state ON
 * @VS_POWER_STATE_PSR:    indicates CRTC power state is in panel self-refresh state
 *
 * Note that when the CRTC is OFF, is doesn't necessarly mean that the hardware is OFF, since
 * other resources (vblank, CRTC, CONN) can keep a reference count to the DPU power domains.
 *
 * The power state variable is an overall power state representation derived from
 * enable, active, self_refresh_active, power_off_mode variables.
 *
 * ENABLE | ACTIVE | SELF_REFRESH_ACTIVE | POWER_OFF_MODE | POWER_STATE
 * 0        X        X                     X                OFF
 * 1        0        0                     0                OFF
 * 1        1        0                     X                ON
 * 1        0        1                     1                PSR
 */
enum drm_vs_power_state {
	VS_POWER_STATE_OFF,
	VS_POWER_STATE_ON,
	VS_POWER_STATE_PSR,
	VS_POWER_STATE_COUNT,
};

static const char *power_state_names[VS_POWER_STATE_COUNT] = {
	"POWER_STATE_OFF",
	"POWER_STATE_ON",
	"POWER_STATE_PSR",
};

enum vs_freq_boost_state {
	VS_FREQ_BOOST_INIT,
	VS_FREQ_BOOST_BOOSTING,
	VS_FREQ_BOOST_RESTORE,
	VS_FREQ_BOOST_PENDING,
};

struct vs_crtc;

struct vs_crtc_funcs {
	void (*enable)(struct device *dev, struct drm_crtc *crtc,
		       struct drm_atomic_state *atomic_state);
	void (*disable)(struct device *dev, struct drm_crtc *crtc,
			struct drm_crtc_state *old_state);
#if IS_ENABLED(CONFIG_DEBUG_FS)
	int (*show_pattern_config)(struct seq_file *s);
	void (*set_pattern)(struct drm_crtc *crtc, const char __user *ubuf, size_t len);
	void (*set_qos)(struct device *dev, struct drm_crtc *crtc, const char __user *ubuf,
			size_t len);
	int (*show_qos)(struct seq_file *s);
#endif /* CONFIG_DEBUG_FS */
	void (*config)(struct device *dev, struct drm_crtc *crtc);
	void (*enable_vblank)(struct vs_crtc *crtc, bool enable);
	u32 (*get_vblank_count)(struct vs_crtc *crtc);
	void (*commit)(struct device *dev, struct drm_crtc *crtc,
		       struct drm_atomic_state *state);
	int (*check)(struct device *dev, struct drm_crtc *crtc, struct drm_crtc_state *state);
	int (*get_crtc_scanout_position)(struct device *dev, struct drm_crtc *crtc, u32 *position);
};

struct vs_drm_blob_state {
	struct drm_property_blob *blob;
	bool changed;
};

/**
 * struct vs_recovery_request_data: data associated with a recovery request
 *                                  stored on the vs_crtc_state
 */
struct vs_recovery_request_data {
	/** @needs_recovery: true if recovery trigger requested for this commit */
	bool needs_recovery;
	/**
	 * @recovery_srcs: bitmap of enum coredump_source values indicating the
	 *                 cause of this recovery request
	 */
	u32 recovery_srcs;
	/** @panel_errors: error bitmap from panel to report; see enum gs_panel_err */
	u64 panel_errors;
	/** @dsi_errors: error bitmap from dsi host to report; see enum gs_dsi_err */
	u64 dsi_errors;
	/** @pmic_errors: error bitmap from panel PMIC to report; see enum gs_pmic_err */
	u64 pmic_errors;
};

struct vs_crtc_state {
	struct drm_crtc_state base;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dc_hw_pattern pattern; /* for pattern debugfs */
	struct dc_hw_disp_qos qos; /* for CRTC qos debugfs */
#endif

	u32 sync_mode;
	u32 output_fmt;
	u8 output_id;
	u32 output_mode;
	u32 bld_size;
	bool bld_size_changed;
	u8 encoder_type;
	u8 bpp;
	u32 te_usec;
	ktime_t expected_present_time;

	struct drm_framebuffer *blur_mask;
	struct drm_framebuffer *brightness_mask;

	struct vs_drm_blob_state prior_gamma;
	struct vs_drm_blob_state roi0_gamma;
	struct vs_drm_blob_state roi1_gamma;
	struct vs_drm_blob_state hist_chan[VS_HIST_CHAN_IDX_COUNT];
	bool hist_rgb_enable;
	struct vs_qos_config qos_config;
	struct vs_qos_config requested_qos_config;
	DECLARE_BITMAP(qos_override, VS_QOS_OVERRIDE_COUNT);

	enum drm_vs_power_state power_state;
	enum drm_vs_power_off_mode power_off_mode;
	bool power_off_mode_changed;

	enum drm_vs_data_extend_mode data_ext_mode;

	bool sync_enable;
	bool out_dp;

	bool seamless_mode_change;

	/** @commit_begin_ts: utc time at which commit began */
	ktime_t commit_begin_ts;
	/** @commit_done_ts: utc time at which commit done occurred */
	ktime_t commit_done_ts;

	bool skip_update;
	bool force_skip_update;
	bool planes_updated;
	bool wb_connectors_updated;
	bool need_boost_freq;

	/**
	 * @swapped_plane_mask: A bitmask of planes that have been swapped to this CRTC.
	 */
	u32 swapped_plane_mask;

	/**
	 * @secure_plane_mask: A bitmask of planes using a secure buffer
	 */
	u32 secure_plane_mask;

	bool recovery_disabled;
	/** @recovery_info: information about any request for recovery trigger */
	struct vs_recovery_request_data recovery_info;
	bool needs_hard_reset;

	struct vs_drm_property_state drm_states[VS_DC_MAX_PROPERTY_NUM];

	struct kthread_work commit_work;
};

/**
 * struct frame_irq_err_counters - counts frame irq errors on crtc
 *
 * Counts both errors related to illegal states (ex. overflow bit set without
 * interrupt being set), as well as cases where logical operation of a
 * responsive irq handling system would not lead to the indicated state (such as
 * where the irq handling has been delayed for a significant gap in time)
 */
struct frame_irq_err_counters {
	/**
	 * @illegal_irq_state: irq status/overflow combination should not exist
	 *                     as indicated (ex. overflow without status, two
	 *                     frame_done's without frame_start)
	 */
	u32 illegal_irq_state;
	/**
	 * @frame_done_overflow: two frame_done's with only one frame_start
	 */
	u32 frame_done_overflow;
	/**
	 * @frame_start_overflow_cmd: two frame_start's with only one frame_done
	 *			      in command mode, where one frame starting
	 *			      should be dependent on the previous
	 *			      frame's ending being handled
	 */
	u32 frame_start_overflow_cmd;
	/**
	 * @full_frame_overflow: status and overflow both set for frame_start
	 *			 and frame_done, implying interrupt gap for more
	 *			 than two full frame lengths
	 */
	u32 full_frame_overflow;
	/**
	 * @full_frame_length_irq_gap: tracks all cases where implied irq gap
	 *			       extends past the length of a frame
	 *			       transfer
	 */
	u32 full_frame_length_irq_gap;
};

struct vs_recovery_stat {
	u32 success;
	u32 failure;
	u32 max_retry;
};

struct vs_crtc {
	struct drm_crtc base;
	u8 id;
	struct device *dev;
	struct device *sysfs_dev;

	/* track dc device for centralized power on/off */
	struct device *dc_dev;

	struct wakeup_source *ws;
	struct drm_pending_vblank_event *event;
	unsigned int max_bpc;
	unsigned int color_formats; /* supported color format */

	spinlock_t slock_ltm_hist;
	dma_addr_t ltm_hist_dma_addr_cur;
	dma_addr_t ltm_hist_dma_addr_nxt;

	/* Timestamp at start of vblank irq - unaffected by lock delays. */
	ktime_t t_vblank;
	bool vblank_en;

	struct drm_property *sync_mode_prop;
	struct drm_property *panel_sync_prop;
	struct drm_property *bld_size_prop;
	struct drm_property *data_ext_prop;
	struct drm_property *prior_gamma_prop;
	struct drm_property *roi0_gamma_prop;
	struct drm_property *roi1_gamma_prop;
	struct drm_property *blur_mask_prop;
	struct drm_property *brightness_mask_prop;
	struct drm_property *hw_caps_prop;
	struct drm_property *hist_chan_prop[VS_HIST_CHAN_IDX_COUNT];
	struct drm_property *hist_rgb_prop;
	struct drm_property *core_clk;
	struct drm_property *rd_avg_bw_mbps;
	struct drm_property *rd_peak_bw_mbps;
	struct drm_property *rd_rt_bw_mbps;
	struct drm_property *wr_avg_bw_mbps;
	struct drm_property *wr_peak_bw_mbps;
	struct drm_property *wr_rt_bw_mbps;
	struct drm_property *expected_present_time;
	struct drm_property *power_off_mode;
	struct drm_property *recovery_disabled;

	struct vs_drm_property_group properties;

	const struct vs_crtc_funcs *funcs;
	atomic_t frames_pending;
	bool frame_transfer_pending;
	bool ddic_cmd_mode_start_scan;
	bool ltm_hist_query_pending;
	atomic_t frame_start_timeout, frame_done_timeout;
	atomic_t frame_start_missing, frame_done_missing;
	/* frame-based underrun counter */
	atomic_t frame_underrun_count;
	/* scanline-based underrun counter */
	atomic_t line_underrun_count;
	struct frame_irq_err_counters frame_irq_err_counters;
	bool needs_hw_reset;
	atomic_t hw_reset_count;
	wait_queue_head_t framedone_waitq;
	u32 framedone_timeout;

	struct kthread_worker *commit_worker;
	struct kthread_worker *vblank_worker;
	struct kthread_work vblank_enable_work;
	spinlock_t vblank_enable_lock;
	bool vblank_enable_in_progress;
	pid_t trace_pid;

	/** @recovery: struct containing info for handling ESD recovery */
	struct vs_recovery recovery;
	struct vs_recovery_stat recovery_stats[SSCD_SRC_MAX];

	atomic_t te_count;
	atomic_t unexpected_te_count;
	atomic_t frame_done_count;

	/**
	 * @first_frame_trigger: whether sof sent out on this crtc since
	 *			 latest device resume. Guards against
	 *			 invalid frame_start interrupt.
	 */
	bool first_frame_trigger;

	struct dev_pm_qos_request core_devfreq_req;
	struct dev_pm_qos_request boost_fab_devfreq_req;
	struct dev_pm_qos_request boost_core_devfreq_req;
	struct vs_qos_config qos_config;
	struct vs_qos_config pending_qos_config;
	bool underrun_enabled;

	/* histogram io-buffer allocation */
	struct vs_gem_pool hist_chan_gem_pool[VS_HIST_CHAN_IDX_COUNT];
	struct vs_gem_pool hist_rgb_gem_pool;

	enum drm_vs_power_off_mode power_off_mode_override;

	struct wait_queue_head fboost_wait_q;
	u32 fboost_state;
	struct kthread_work fboost_work;
	struct kthread_worker *fboost_worker;

	unsigned long last_notify_dpu_underrun_time;
};

int vs_crtc_check_power_state(struct drm_atomic_state *state, struct drm_crtc *crtc,
			      struct drm_crtc_state *crtc_state);

bool vs_display_get_crtc_scanoutpos(struct drm_device *dev, unsigned int crtc_id,
				    bool in_vblank_irq, int *vpos, int *hpos, ktime_t *stime,
				    ktime_t *etime, const struct drm_display_mode *mode);

void vs_crtc_destroy(struct drm_crtc *crtc);

struct vs_crtc *vs_crtc_create(const struct dc_hw_display *display, struct drm_device *drm_dev,
			       struct vs_dc *dc, const struct vs_dc_info *info, u8 index);
void vs_crtc_handle_frm_start(struct drm_crtc *crtc);
int vs_crtc_get_ltm_hist(struct drm_file *file_priv, struct vs_crtc *crtc, struct dc_hw *hw,
			 struct drm_vs_ltm_histogram_data *out);

void vs_crtc_wait_for_flip_done(struct drm_crtc *crtc, struct drm_atomic_state *state);
void vs_crtc_store_ltm_hist_dma_addr(struct device *dev, u8 hw_id, dma_addr_t addr);
dma_addr_t vs_crtc_get_ltm_hist_dma_addr(struct device *dev, u8 hw_id);

struct drm_atomic_state *vs_crtc_suspend(struct vs_crtc *vs_crtc,
					 struct drm_modeset_acquire_ctx *ctx);
int vs_crtc_resume(struct drm_atomic_state *suspend_state, struct drm_modeset_acquire_ctx *ctx);
void vs_crtc_record_frame_timeout(struct vs_crtc *vs_crtc, int old_te_count,
				  struct drm_crtc_state *old_crtc_state,
				  unsigned long wait_time_jiffies, bool during_disable);
void vs_crtc_trigger_panel_dsi_coredump(struct vs_crtc *vs_crtc,
					struct vs_crtc_state *old_vs_crtc_state, u64 panel_errors,
					u64 dsi_errors, enum coredump_source source);
bool vs_crtc_state_is_recovery_source_enabled(const struct vs_crtc_state *vs_crtc_state,
					      enum coredump_source source);
bool vs_crtc_state_has_secure(const struct drm_crtc_state *crtc_state);
u32 vs_crtc_state_active_planes_enabling_secure_mask(const struct drm_crtc_state *old_crtc_state,
						     const struct drm_crtc_state *new_crtc_state);
bool vs_crtc_state_is_active_and_wb(const struct drm_crtc_state *crtc_state,
				    const struct drm_device *drm_dev);
bool vs_crtc_commit_enables_secure_or_wb(const struct drm_crtc_state *crtc_state,
					 const struct drm_crtc_state *old_state,
					 const struct drm_device *dev);
bool vs_crtc_secure_hardware_active(const struct drm_device *dev);

#define to_vs_crtc(crtc) container_of(crtc, struct vs_crtc, base)

#define to_vs_crtc_state(state) container_of(state, struct vs_crtc_state, base)

#endif /* __VS_CRTC_H__ */
