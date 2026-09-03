// SPDX-License-Identifier: GPL-2.0

#include <drm/drm_atomic.h>
#include <drm/drm_drv.h>
#include <drm/drm_modeset_lock.h>
#include <drm/drm_print.h>

#include <trace/dpu_trace.h>

#include "vs_crtc.h"
#include "vs_dc_post.h"
#include "vs_recovery.h"
#include "vs_trace.h"

#define VS_RECOVERY_TRY_MAX 3

enum vs_recovery_stat_type {
	VS_RECOVERY_STAT_SUCCESS = 0,
	VS_RECOVERY_STAT_FAILURE,
	VS_RECOVERY_STAT_MAX_RETRY,
};

/**
 * vs_update_recovery_stats() - update stats for all active resources
 * @vs_crtc: crtc instance
 * @srcs: recovery source bitmap captured before suspend
 * @type: stat type to increment
 */
static void vs_update_recovery_stats(struct vs_crtc *vs_crtc, u32 srcs,
				     enum vs_recovery_stat_type type)
{
	int i;

	for (i = 0; i < SSCD_SRC_MAX; i++) {
		if (srcs & BIT(i)) {
			struct vs_recovery_stat *stat = &vs_crtc->recovery_stats[i];

			switch (type) {
			case VS_RECOVERY_STAT_SUCCESS:
				stat->success++;
				break;
			case VS_RECOVERY_STAT_FAILURE:
				stat->failure++;
				break;
			case VS_RECOVERY_STAT_MAX_RETRY:
				stat->max_retry++;
				break;
			default:
				dev_err(vs_crtc->dev, "Invalid recovery stat type: %d\n", type);
			}
		}
	}
}

static void vs_recovery_handler(struct work_struct *work)
{
	struct vs_recovery *recovery = container_of(work, struct vs_recovery, work);
	struct vs_crtc *vs_crtc = container_of(recovery, struct vs_crtc, recovery);
	struct device *dev = vs_crtc->dev;
	struct drm_modeset_acquire_ctx ctx;
	struct drm_atomic_state *suspend_state;
	u32 recovery_srcs = atomic_xchg(&recovery->pending_recovery_srcs, 0);
	int ret = 0;

	DPU_ATRACE_BEGIN("crtc[%s] vs recovery handler", vs_crtc->base.name);
	dev_info(dev, "crtc[%s] vs recovery handler entered\n", vs_crtc->base.name);

	drm_modeset_acquire_init(&ctx, 0);

	suspend_state = vs_crtc_suspend(vs_crtc, &ctx);
	if (!IS_ERR_OR_NULL(suspend_state)) {
		ret = vs_crtc_resume(suspend_state, &ctx);
		drm_atomic_state_put(suspend_state);
	} else {
		dev_err(dev, "crtc[%s] failed to suspend state during recovery (%ld)\n",
			vs_crtc->base.name, PTR_ERR(suspend_state));
		ret = -EINVAL;
	}

	if (ret == 0) {
		dev_info(dev, "crtc[%s] recovery is successfully finished(%d)\n",
			 vs_crtc->base.name, recovery->count);
		vs_update_recovery_stats(vs_crtc, recovery_srcs, VS_RECOVERY_STAT_SUCCESS);
	} else {
		dev_err(dev, "crtc[%s] Failed to recover display (%d)\n", vs_crtc->base.name, ret);
		vs_update_recovery_stats(vs_crtc, recovery_srcs, VS_RECOVERY_STAT_FAILURE);
	}

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	atomic_dec(&recovery->recovering);
	DPU_ATRACE_END("crtc[%s] vs recovery handler", vs_crtc->base.name);
}

void vs_recovery_register(struct vs_crtc *vs_crtc)
{
	struct vs_recovery *recovery = &vs_crtc->recovery;

	INIT_WORK(&recovery->work, vs_recovery_handler);
	recovery->count = 0;
	atomic_set(&recovery->recovering, 0);
	atomic_set(&recovery->pending_recovery_srcs, 0);

	dev_dbg(vs_crtc->dev, "crtc[%s] recovery is supported\n", vs_crtc->base.name);
}

void vs_crtc_trigger_recovery(struct vs_crtc *vs_crtc, u32 srcs)
{
	struct vs_recovery *recovery = &vs_crtc->recovery;
	struct device *dev = vs_crtc->dev;
	struct vs_dc *dc = dev_get_drvdata(dev);
	u8 display_id = to_vs_display_id(dc, &vs_crtc->base);

	atomic_or(srcs, &recovery->pending_recovery_srcs);

	/* only recover if we don't have another active recovery attempt going */
	if (!atomic_add_unless(&recovery->recovering, 1, 1)) {
		dev_dbg(vs_crtc->dev, "crtc[%s] ignoring recovery attempt while one in progress\n",
			vs_crtc->base.name);
		return;
	}

	recovery->count++;
	trace_disp_trigger_recovery(display_id, vs_crtc);
	if (recovery->count >= VS_RECOVERY_TRY_MAX) {
		u32 recovery_srcs = atomic_xchg(&recovery->pending_recovery_srcs, 0);

		dev_err(vs_crtc->dev, "crtc[%s] maximum consecutive recovery try reached (%d)\n",
			vs_crtc->base.name, VS_RECOVERY_TRY_MAX);
		vs_update_recovery_stats(vs_crtc, recovery_srcs, VS_RECOVERY_STAT_MAX_RETRY);
		atomic_dec(&recovery->recovering);
		return;
	}

	queue_work(system_highpri_wq, &recovery->work);
}

void vs_execute_recovery_or_coredump_if_needed(struct vs_crtc_state *vs_crtc_state,
					       u64 panel_errors,
					       u64 dsi_errors,
					       u64 pmic_errors,
					       u32 srcs)
{
	struct drm_crtc *crtc = vs_crtc_state->base.crtc;
	bool coredump_executed = false;
	int i;

	vs_crtc_state->recovery_info.panel_errors = panel_errors;
	vs_crtc_state->recovery_info.dsi_errors = dsi_errors;
	vs_crtc_state->recovery_info.pmic_errors = pmic_errors;

	for (i = 0; i < SSCD_SRC_MAX; i++) {
		if (!(srcs & BIT(i)))
			continue;

		/*
		 * Marks the recovery as needing to happen; does not execute until the
		 * commit proper occurs.
		 */
		if (vs_crtc_state_is_recovery_source_enabled(vs_crtc_state, i)) {
			vs_crtc_state->recovery_info.needs_recovery = true;
			vs_crtc_state->recovery_info.recovery_srcs |= BIT(i);
		}

		/*
		 * Executes the coredump immediately. If multiple sources in the check would
		 * coredump, we instead only do the first one, using the
		 * coredump_executed pointer to keep track of whether we dumped this commit.
		 */
		if (!coredump_executed && coredump_source_enabled(i)) {
			struct vs_crtc_state *old_vs_crtc_state = to_vs_crtc_state(crtc->state);

			coredump_executed = true;
			vs_crtc_trigger_panel_dsi_coredump(to_vs_crtc(crtc), old_vs_crtc_state,
							   panel_errors, dsi_errors, i);
		}
	}
}
