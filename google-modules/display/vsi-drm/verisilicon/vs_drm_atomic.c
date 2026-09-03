// SPDX-License-Identifier: MIT
/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include "vs_drm_atomic.h"
#include "vs_crtc.h"
#include "vs_drm_state_record.h"
#include "vs_dc.h"
#include "vs_writeback.h"

#include <linux/bitmap.h>
#include <linux/dma-fence.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/timekeeping.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_bridge.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_print.h>
#include <drm/drm_self_refresh_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_writeback.h>

#include <gs_drm/gs_drm_connector.h>
#include <trace/dpu_trace.h>

#include <uapi/linux/sched/types.h>

#define CRTC_KTHREAD_SCHED_PRIORITY 20

#define VS_DRM_WAIT_FENCE_TIMEOUT_MS 250

/**
 * bitmap_to_u64() - for putting a bitmap into u64 with size checking
 *
 * This is a wrapper of `bitmap_to_arr64` for when the destination is a single
 * 64-bit integer, which conducts compiletime checking of the number of bits.
 *
 * @buf: address of u64 destination
 * @bitmap: bitmap being moved to u64
 * @nbits: number of bits being moved in the bitmap
 */
#define bitmap_to_u64(buf, bitmap, nbits)                  \
	do {                                               \
		BUILD_BUG_ON((nbits) > 64);                \
		bitmap_to_arr64((buf), (bitmap), (nbits)); \
	} while (0)

/*
  * These functions are similar to the upstream 6.6.1 version, with the following changes:
  * - Removed support asynchronous commit, as it's not used.
  * - Removed automatic self-refresh updates, to avoid bootloader hand-off issues.
  * - Uses a kthread work queue to reduce scheduling latency.
  * - Increase kthread priority and use SCHED_FIFO to reduce scheduling latency.
  * - Add support for gs_connector pre/post commit functions.
  * - Uses a 250 ms fence timeout instead of a infinite one.
  * - Split modeset disable sequence to power off ENCODERS before CRTCS
  * - Added vs_drm_atomic_print_old_state to match drm_atomic_print_new_state,
  *     along with function dependencies, to print the old components of a state
  */

static void dump_fence_timeout_info(struct drm_printer *p, struct drm_plane *plane,
				    struct drm_plane_state *plane_state, struct dma_fence *fence)
{
	struct timespec64 ts64;

	if (!p || !plane || !plane_state || !fence)
		return;

	spin_lock_irq(fence->lock);

	drm_printf(p, "fence: %s-%s %llu-%llu status:%s\n",
		   fence->ops ? fence->ops->get_driver_name(fence) : "none",
		   fence->ops ? fence->ops->get_timeline_name(fence) : "none", fence->context,
		   fence->seqno, dma_fence_get_status_locked(fence) < 0 ? "error" : "active");

	if (test_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags)) {
		ts64 = ktime_to_timespec64(fence->timestamp);
		drm_printf(p, "fence: timestamp:%lld.%09ld\n", (s64)ts64.tv_sec, ts64.tv_nsec);
	}

	if (fence->error)
		drm_printf(p, "fence: err=%d\n", fence->error);

	spin_unlock_irq(fence->lock);

	drm_printf(p, "plane-%d fb allocated by = %s\n", plane->base.id, plane_state->fb->comm);
	drm_printf(p, "plane-%d src-pos=" DRM_RECT_FP_FMT "\n", plane->base.id,
		   DRM_RECT_FP_ARG(&plane_state->src));
	drm_printf(p, "plane-%d crtc-pos=" DRM_RECT_FMT "\n", plane->base.id,
		   DRM_RECT_ARG(&plane_state->dst));
	drm_printf(p, "plane-%d rotation=%x zpos=%x alpha=%x blend_mode=%x\n", plane->base.id,
		   plane_state->rotation, plane_state->normalized_zpos, plane_state->alpha,
		   plane_state->pixel_blend_mode);
}

static int vs_drm_atomic_helper_wait_for_fences(struct drm_device *dev,
						struct drm_atomic_state *state, bool pre_swap)
{
	int i, ret, err = 0;
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state;
	struct dma_fence *fence;
	struct drm_printer p = drm_info_printer(dev->dev);
	long timeout = msecs_to_jiffies(VS_DRM_WAIT_FENCE_TIMEOUT_MS);

	for_each_new_plane_in_state(state, plane, new_plane_state, i) {
		fence = new_plane_state->fence;
		if (!fence)
			continue;

		WARN_ON(!new_plane_state->fb);

		DPU_ATRACE_BEGIN("dma_fence_wait_timeout");
		ret = dma_fence_wait_timeout(fence, pre_swap, timeout);
		if (ret == 0) {
			drm_err(dev, "timeout waiting for fence, name:%s idx:%d\n",
				plane->name ?: "NA", plane->index);

			dump_fence_timeout_info(&p, plane, new_plane_state, fence);
			err = -ETIMEDOUT;
		} else if (ret < 0) {
			drm_err(dev, "error waiting for fence, name:%s idx:%d ret:%d\n",
				plane->name ?: "NA", plane->index, ret);

			dump_fence_timeout_info(&p, plane, new_plane_state, fence);
			err = ret;
		}

		dma_fence_put(new_plane_state->fence);
		new_plane_state->fence = NULL;
		DPU_ATRACE_END("dma_fence_wait_timeout");
	}

	return err;
}

static void vs_drm_atomic_mark_commit_timestamp(struct drm_atomic_state *state, bool is_begin_ts)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	int i;
	ktime_t ts = ktime_get_real();

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		struct vs_crtc_state *vs_crtc_state = to_vs_crtc_state(new_crtc_state);

		if (is_begin_ts)
			vs_crtc_state->commit_begin_ts = ts;
		else
			vs_crtc_state->commit_done_ts = ts;
	}
}

static void vs_drm_commit_tail(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;
	const struct drm_mode_config_helper_funcs *funcs;

	DPU_ATRACE_BEGIN(__func__);
	funcs = dev->mode_config.helper_private;

	/* Record the state being committed */
	vs_drm_record_state(old_state);

	DPU_ATRACE_BEGIN("wait_for_fences");
	vs_drm_atomic_helper_wait_for_fences(dev, old_state, false);
	DPU_ATRACE_END("wait_for_fences");

	DPU_ATRACE_BEGIN("wait_for_dependencies");
	drm_atomic_helper_wait_for_dependencies(old_state);
	DPU_ATRACE_END("wait_for_dependencies");

	if (funcs && funcs->atomic_commit_tail)
		funcs->atomic_commit_tail(old_state);
	else
		drm_atomic_helper_commit_tail(old_state);

	DPU_ATRACE_BEGIN("cleanup_done");
	drm_atomic_helper_commit_cleanup_done(old_state);
	DPU_ATRACE_END("cleanup_done");

	vs_drm_atomic_mark_commit_timestamp(old_state, false);

	drm_atomic_state_put(old_state);
	DPU_ATRACE_END(__func__);
}

static void vs_drm_commit_work_struct(struct work_struct *work)
{
	struct drm_atomic_state *state = container_of(work, struct drm_atomic_state, commit_work);

	vs_drm_commit_tail(state);
}

static void vs_drm_commit_kthread_work(struct kthread_work *work)
{
	struct vs_crtc_state *vs_crtc_state = container_of(work, struct vs_crtc_state, commit_work);
	struct drm_atomic_state *state = vs_crtc_state->base.state;

	vs_drm_commit_tail(state);
}

static void vs_drm_commit_queue_work(struct drm_atomic_state *state)
{
	int i;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct vs_crtc *vs_crtc;
	struct vs_crtc_state *vs_crtc_state;
	struct sched_param param = { .sched_priority = CRTC_KTHREAD_SCHED_PRIORITY };

	for_each_old_crtc_in_state(state, crtc, crtc_state, i) {
		vs_crtc = container_of(crtc, struct vs_crtc, base);
		vs_crtc_state = container_of(crtc_state, struct vs_crtc_state, base);

		/* Only queue the commit work on the first CRTC present in the atomic state */
		sched_setscheduler_nocheck(vs_crtc->commit_worker->task, SCHED_FIFO, &param);
		kthread_init_work(&vs_crtc_state->commit_work, vs_drm_commit_kthread_work);
		kthread_queue_work(vs_crtc->commit_worker, &vs_crtc_state->commit_work);
		return;
	}

	/* Fallback to system_highpri_wq, when there are no CRTC in the atomic state */
	INIT_WORK(&state->commit_work, vs_drm_commit_work_struct);
	queue_work(system_highpri_wq, &state->commit_work);
}

static int _vs_validate_secure_wb(const struct drm_device *dev, struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_state;
	int i;
	bool pending_secure = false;
	bool pending_wb = false;

	for_each_new_crtc_in_state(state, crtc, new_state, i) {
		if (vs_crtc_state_has_secure(new_state))
			pending_secure = true;
		if (vs_crtc_state_is_active_and_wb(new_state, dev))
			pending_wb = true;
	}

	if (pending_secure && vs_writeback_is_active(dev)) {
		drm_dbg_atomic(dev, "Rejecting commit: enabling secure display while wb active\n");
		return -EBUSY;
	}

	if (pending_wb && vs_crtc_secure_hardware_active(dev)) {
		drm_dbg_atomic(dev, "Rejecting commit: enabling wb while secure is active\n");
		return -EBUSY;
	}

	return 0;
}

static int vs_drm_atomic_commit_internal(struct drm_device *dev, struct drm_atomic_state *state,
					 bool nonblock)
{
	int ret;

	vs_drm_atomic_mark_commit_timestamp(state, true);

	ret = _vs_validate_secure_wb(dev, state);
	if (ret)
		return ret;

	DPU_ATRACE_BEGIN("setup_commit");
	ret = drm_atomic_helper_setup_commit(state, nonblock);
	DPU_ATRACE_END("setup_commit");
	if (ret)
		return ret;

	DPU_ATRACE_BEGIN("prepare_planes");
	ret = drm_atomic_helper_prepare_planes(dev, state);
	DPU_ATRACE_END("prepare_planes");
	if (ret)
		return ret;

	if (!nonblock) {
		DPU_ATRACE_BEGIN("wait_for_fences");
		ret = vs_drm_atomic_helper_wait_for_fences(dev, state, true);
		DPU_ATRACE_END("wait_for_fences");
		if (ret)
			goto err;
	}

	/*
	 * This is the point of no return - everything below never fails except
	 * when the hw goes bonghits. Which means we can commit the new state on
	 * the software side now.
	 */
	DPU_ATRACE_BEGIN("swap_state");
	ret = drm_atomic_helper_swap_state(state, true);
	DPU_ATRACE_END("swap_state");
	if (ret)
		goto err;

	/*
	 * Everything below can be run asynchronously without the need to grab
	 * any modeset locks at all under one condition: It must be guaranteed
	 * that the asynchronous work has either been cancelled (if the driver
	 * supports it, which at least requires that the framebuffers get
	 * cleaned up with drm_atomic_helper_cleanup_planes()) or completed
	 * before the new state gets committed on the software side with
	 * drm_atomic_helper_swap_state().
	 *
	 * This scheme allows new atomic state updates to be prepared and
	 * checked in parallel to the asynchronous completion of the previous
	 * update. Which is important since compositors need to figure out the
	 * composition of the next frame right after having submitted the
	 * current layout.
	 *
	 * NOTE: Commit work has multiple phases, first hardware commit, then
	 * cleanup. We want them to overlap, hence need system_unbound_wq to
	 * make sure work items don't artificially stall on each another.
	 */

	drm_atomic_state_get(state);
	if (nonblock)
		vs_drm_commit_queue_work(state);
	else
		vs_drm_commit_tail(state);

	return 0;

err:
	DPU_ATRACE_BEGIN("unprepare_planes");
	drm_atomic_helper_unprepare_planes(dev, state);
	DPU_ATRACE_END("unprepare_planes");

	return ret;
}

int vs_drm_atomic_commit(struct drm_device *dev, struct drm_atomic_state *state, bool nonblock)
{
	int rc;

	DPU_ATRACE_BEGIN(__func__);
	rc = vs_drm_atomic_commit_internal(dev, state, nonblock);
	DPU_ATRACE_END(__func__);

	return rc;
}

static bool plane_crtc_active(const struct drm_plane_state *state)
{
	return state->crtc && state->crtc->state->active;
}

static void vs_drm_update_frame_transfer_status(struct drm_connector_state *conn_state)
{
	if (is_gs_drm_connector_state(conn_state)) {
		struct gs_drm_connector_state *gs_connector_state =
			to_gs_connector_state(conn_state);
		struct vs_crtc *vs_crtc = to_vs_crtc(conn_state->crtc);

		if (vs_crtc->frame_transfer_pending)
			gs_connector_state->frame_start_ts = vs_crtc->t_vblank;
		else
			gs_connector_state->frame_start_ts = 0;
	}
}

static inline void vs_drm_update_conn_recovery_status(struct drm_connector_state *conn_state)
{
	struct gs_drm_connector_state *gs_connector_state = to_gs_connector_state(conn_state);
	struct vs_crtc *vs_crtc = to_vs_crtc(conn_state->crtc);

	gs_connector_state->is_recovering = atomic_read(&vs_crtc->recovery.recovering);
}

static void vs_drm_atomic_commit_planes(struct drm_device *dev, struct drm_atomic_state *old_state,
					uint32_t flags)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *old_plane_state, *new_plane_state;
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state, *new_conn_state;
	int i;
	bool active_only = flags & DRM_PLANE_COMMIT_ACTIVE_ONLY;
	bool no_disable = flags & DRM_PLANE_COMMIT_NO_DISABLE_AFTER_MODESET;

	for_each_oldnew_crtc_in_state(old_state, crtc, old_crtc_state, new_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		funcs = crtc->helper_private;

		if (!funcs || !funcs->atomic_begin)
			continue;

		if (active_only && !new_crtc_state->active)
			continue;

		DPU_ATRACE_BEGIN("atomic_begin");
		funcs->atomic_begin(crtc, old_state);
		DPU_ATRACE_END("atomic_begin");
	}

	for_each_oldnew_plane_in_state(old_state, plane, old_plane_state, new_plane_state, i) {
		const struct drm_plane_helper_funcs *funcs;
		bool disabling;

		funcs = plane->helper_private;

		if (!funcs)
			continue;

		disabling = drm_atomic_plane_disabling(old_plane_state, new_plane_state);

		if (active_only) {
			/*
			 * Skip planes related to inactive CRTCs. If the plane
			 * is enabled use the state of the current CRTC. If the
			 * plane is being disabled use the state of the old
			 * CRTC to avoid skipping planes being disabled on an
			 * active CRTC.
			 */
			if (!disabling && !plane_crtc_active(new_plane_state))
				continue;
			if (disabling && !plane_crtc_active(old_plane_state))
				continue;
		}

		/*
		 * Special-case disabling the plane if drivers support it.
		 */
		if (disabling && funcs->atomic_disable) {
			struct drm_crtc_state *crtc_state;

			crtc_state = old_plane_state->crtc->state;

			if (drm_atomic_crtc_needs_modeset(crtc_state) && no_disable)
				continue;

			DPU_ATRACE_BEGIN("atomic_disable");
			funcs->atomic_disable(plane, old_state);
			DPU_ATRACE_END("atomic_disable");

		} else if (new_plane_state->crtc || disabling) {
			DPU_ATRACE_BEGIN("atomic_update");
			funcs->atomic_update(plane, old_state);
			DPU_ATRACE_END("atomic_update");
		}
	}

	/* gs_connector pre-commit function */
	for_each_oldnew_connector_in_state(old_state, connector, old_conn_state, new_conn_state,
					   i) {
		struct gs_drm_connector *gs_connector;
		const struct gs_drm_connector_helper_funcs *funcs;

		if (!new_conn_state->crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(old_state, new_conn_state->crtc);
		if (!new_crtc_state->active)
			continue;

		if (connector->connector_type != DRM_MODE_CONNECTOR_DSI)
			continue;

		if (!is_gs_drm_connector(connector))
			continue;

		gs_connector = to_gs_connector(connector);
		funcs = gs_connector->helper_private;
		if (!funcs || !funcs->atomic_pre_commit)
			continue;

		drm_dbg_atomic(dev, "gs_connector [CONN:%d:%s] atomic_pre_commit\n",
			       connector->base.id, connector->name);
		DPU_ATRACE_BEGIN("gs_conn_atomic_pre_commit");
		funcs->atomic_pre_commit(gs_connector, to_gs_connector_state(old_conn_state),
					 to_gs_connector_state(new_conn_state));
		DPU_ATRACE_END("gs_conn_atomic_pre_commit");
	}

	for_each_oldnew_crtc_in_state(old_state, crtc, old_crtc_state, new_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		funcs = crtc->helper_private;

		if (!funcs || !funcs->atomic_flush)
			continue;

		if (active_only && !new_crtc_state->active)
			continue;

		DPU_ATRACE_BEGIN("atomic_flush");
		funcs->atomic_flush(crtc, old_state);
		DPU_ATRACE_END("atomic_flush");
	}

	/* gs_connector post-commit function */
	for_each_oldnew_connector_in_state(old_state, connector, old_conn_state, new_conn_state,
					   i) {
		struct gs_drm_connector *gs_connector;
		const struct gs_drm_connector_helper_funcs *funcs;

		if (!new_conn_state->crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(old_state, new_conn_state->crtc);
		/* allow connector atomic_commit for crtc in self_refresh_active state */
		if (!new_crtc_state->active && !new_crtc_state->self_refresh_active)
			continue;

		if (connector->connector_type != DRM_MODE_CONNECTOR_DSI)
			continue;

		if (!is_gs_drm_connector(connector))
			continue;

		gs_connector = to_gs_connector(connector);
		funcs = gs_connector->helper_private;
		if (!funcs || !funcs->atomic_commit)
			continue;

		vs_drm_update_frame_transfer_status(new_conn_state);

		vs_drm_update_conn_recovery_status(new_conn_state);

		drm_dbg_atomic(dev, "gs_connector [CONN:%d:%s] atomic_commit\n", connector->base.id,
			       connector->name);
		DPU_ATRACE_BEGIN("gs_conn_atomic_flush");
		funcs->atomic_commit(gs_connector, to_gs_connector_state(old_conn_state),
				     to_gs_connector_state(new_conn_state));
		DPU_ATRACE_END("gs_conn_atomic_flush");
	}
}

static int vs_drm_atomic_add_affected_gs_connectors(struct drm_device *dev,
						    struct drm_atomic_state *state)
{
	int i;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;
	struct drm_connector_list_iter conn_iter;
	const struct gs_drm_connector *gs_connector;
	u32 connector_mask = 0;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		if (!new_crtc_state->active)
			continue;

		connector_mask |= new_crtc_state->connector_mask;
	}

	if (!connector_mask)
		return 0;

	drm_connector_list_iter_begin(state->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		gs_connector = to_gs_connector(connector);

		if (!(connector_mask & drm_connector_mask(connector)))
			continue;

		if (connector->connector_type != DRM_MODE_CONNECTOR_DSI)
			continue;

		if (!is_gs_drm_connector(connector))
			continue;

		if (!gs_connector->needs_commit)
			continue;

		drm_dbg_atomic(dev, "adding gs_connector [CONN:%d:%s] to %p\n", connector->base.id,
			       connector->name, state);

		conn_state = drm_atomic_get_connector_state(state, connector);
		if (IS_ERR(conn_state)) {
			drm_err(dev, "invalid gs_connector:%s state\n", connector->name);
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}

static int vs_drm_atomic_check_power_state(struct drm_device *dev, struct drm_atomic_state *state)
{
	int i, ret;
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		ret = vs_crtc_check_power_state(state, crtc, new_crtc_state);
		if (ret) {
			drm_dbg_atomic(crtc->dev,
				       "[CRTC:%d:%s] atomic check power state failed %d\n",
				       crtc->base.id, crtc->name, ret);
			return ret;
		}
	}

	return 0;
}

static int vs_drm_atomic_check_updated_planes(struct drm_device *dev,
					      struct drm_atomic_state *state)
{
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	int i;

	for_each_new_plane_in_state(state, plane, plane_state, i) {
		struct drm_crtc_state *crtc_state;
		struct vs_crtc_state *vs_crtc_state;

		if (plane_state->crtc) {
			crtc_state = drm_atomic_get_new_crtc_state(state, plane_state->crtc);

			if (WARN_ON(!crtc_state))
				return -EINVAL;

			vs_crtc_state = to_vs_crtc_state(crtc_state);
			vs_crtc_state->planes_updated = true;
		}
	}

	return 0;
}

static void _check_display_errors(struct vs_crtc_state *vs_crtc_state,
				  struct gs_drm_connector_state *gs_conn_state)
{
	struct drm_crtc *crtc = vs_crtc_state->base.crtc;
	struct device *dev = crtc->dev->dev;
	u32 recovery_srcs = 0;

	if (gs_drm_connector_check_ddic_errors(gs_conn_state)) {
		dev_err_ratelimited(dev, "DDIC errors detected on %s: %*pbl\n",
			crtc->name, GS_PANEL_ERR_MAX, gs_conn_state->panel_errors);
		recovery_srcs |= BIT(SSCD_SRC_DDIC_ERR);
	}

	if (gs_drm_connector_check_gram_errors(gs_conn_state)) {
		dev_err_ratelimited(dev, "GRAM collision detected on %s\n", crtc->name);
		recovery_srcs |= BIT(SSCD_SRC_GRAM_COLLISION);
	}

	if (gs_drm_connector_check_dsi_errors(gs_conn_state)) {
		dev_err_ratelimited(dev, "DSI errors detected on %s: %*pbl\n",
			crtc->name, GS_DSI_ERR_MAX, gs_conn_state->dsi_errors);
		recovery_srcs |= BIT(SSCD_SRC_DSI_ERR);
	}

	if (gs_drm_connector_check_pmic_errors(gs_conn_state)) {
		dev_err_ratelimited(dev, "PMIC errors detected on %s: %*pbl\n",
			crtc->name, GS_PMIC_ERR_MAX, gs_conn_state->pmic_errors);
		recovery_srcs |= BIT(SSCD_SRC_PMIC_ERR);
	}

	if (recovery_srcs) {
		u64 panel_errors, dsi_errors, pmic_errors;

		bitmap_to_u64(&panel_errors, gs_conn_state->panel_errors, GS_PANEL_ERR_MAX);
		bitmap_to_u64(&dsi_errors, gs_conn_state->dsi_errors, GS_DSI_ERR_MAX);
		bitmap_to_u64(&pmic_errors, gs_conn_state->pmic_errors, GS_PMIC_ERR_MAX);

		vs_execute_recovery_or_coredump_if_needed(vs_crtc_state,
							  panel_errors,
							  dsi_errors,
							  pmic_errors,
							  recovery_srcs);
	}
}

static void vs_drm_atomic_check_display_errors(struct drm_device *dev,
						struct drm_atomic_state *state)
{
	struct drm_connector *connector;
	struct drm_connector_state *new_conn_state;
	int i;

	for_each_new_connector_in_state(state, connector, new_conn_state, i) {
		struct drm_crtc_state *new_crtc_state;
		struct gs_drm_connector_state *gs_conn_state;
		struct vs_crtc_state *vs_crtc_state;

		if (!new_conn_state->crtc)
			continue;

		new_crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);
		if (!new_crtc_state->active)
			continue;

		if (connector->connector_type != DRM_MODE_CONNECTOR_DSI)
			continue;

		if (!is_gs_drm_connector(connector))
			continue;

		gs_conn_state = to_gs_connector_state(new_conn_state);
		vs_crtc_state = to_vs_crtc_state(new_crtc_state);

		_check_display_errors(vs_crtc_state, gs_conn_state);

		clear_bit(GS_DSI_ERR_HARD_RSTN, gs_conn_state->dsi_errors);
		bitmap_clear(gs_conn_state->panel_errors, 0, GS_PANEL_ERR_MAX);
		bitmap_clear(gs_conn_state->pmic_errors, 0, GS_PMIC_ERR_MAX);
	}
}

static bool _drm_commit_enables_secure_or_wb(struct drm_atomic_state *state)
{
	const struct drm_device *dev = state->dev;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	int i;

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		struct drm_crtc_state *old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);

		if (vs_crtc_commit_enables_secure_or_wb(crtc_state, old_crtc_state, dev))
			return true;
	}

	return false;
}

/**
 * vs_drm_validate_secure_wb() - Validate global secure + WB status.
 * @state: The global atomic state.
 *
 * This function checks for concurrent secure display and writeback configurations
 * across all CRTCs on the device. Unmodified CRTCs are locked and peeked.
 *
 * Return: 0 on success,
 *         -EINVAL if concurrent secure and writeback is detected,
 *         -EDEADLK if a modeset lock contention occurs (requiring backoff/retry),
 *         or other negative error codes on failure.
 */
static int vs_drm_validate_secure_wb(struct drm_atomic_state *state)
{
	const struct drm_device *dev = state->dev;
	struct drm_crtc *crtc;
	u32 secure_crtc_mask = 0;
	u32 wb_crtc_mask = 0;

	if (!_drm_commit_enables_secure_or_wb(state))
		return 0;

	drm_for_each_crtc(crtc, dev) {
		struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
		int ret;

		if (!crtc_state) {
			ret = drm_modeset_lock(&crtc->mutex, state->acquire_ctx);
			if (ret)
				return ret;
			crtc_state = crtc->state;
		}

		if (crtc_state) {
			if (vs_crtc_state_has_secure(crtc_state))
				secure_crtc_mask |= drm_crtc_mask(crtc);
			if (vs_crtc_state_is_active_and_wb(crtc_state, dev))
				wb_crtc_mask |= drm_crtc_mask(crtc);
		}
	}

	if (secure_crtc_mask && wb_crtc_mask) {
		drm_dbg_atomic(dev, "Rejecting config: concurrent secure (%#x) + wb (%#x)\n",
			       secure_crtc_mask, wb_crtc_mask);
		return -EINVAL;
	}

	return 0;
}

int vs_drm_atomic_check(struct drm_device *dev, struct drm_atomic_state *state)
{
	int ret;

	DPU_ATRACE_BEGIN(__func__);

	ret = vs_drm_atomic_add_affected_gs_connectors(dev, state);
	if (ret) {
		drm_err(dev, "add_affected_gs_connectors failed %d", ret);
		goto end;
	}

	ret = vs_drm_atomic_check_updated_planes(dev, state);
	if (ret) {
		drm_err(dev, "check_updated_planes failed %d", ret);
		goto end;
	}

	DPU_ATRACE_BEGIN("check_power_state");
	ret = vs_drm_atomic_check_power_state(dev, state);
	DPU_ATRACE_END("check_power_state");
	if (ret) {
		drm_err(dev, "check_power_state failed %d", ret);
		goto end;
	}

	DPU_ATRACE_BEGIN("check_modeset");
	ret = drm_atomic_helper_check_modeset(dev, state);
	DPU_ATRACE_END("check_modeset");
	if (ret) {
		drm_err(dev, "check_modeset failed %d", ret);
		goto end;
	}

	if (dev->mode_config.normalize_zpos) {
		ret = drm_atomic_normalize_zpos(dev, state);
		if (ret) {
			drm_err(dev, "normalize_zpos failed %d", ret);
			goto end;
		}
	}

	vs_drm_atomic_check_display_errors(dev, state);

	DPU_ATRACE_BEGIN("check_planes");
	ret = drm_atomic_helper_check_planes(dev, state);
	DPU_ATRACE_END("check_planes");
	if (ret) {
		drm_err(dev, "check_planes failed %d", ret);
		goto end;
	}

	ret = vs_drm_validate_secure_wb(state);
	if (ret)
		goto end;

	if (state->legacy_cursor_update)
		state->async_update = !drm_atomic_helper_async_check(dev, state);

end:
	DPU_ATRACE_END(__func__);
	return ret;
}

static void drm_atomic_helper_commit_writebacks(struct drm_device *dev,
						struct drm_atomic_state *old_state)
{
	struct drm_connector *connector;
	struct drm_connector_state *new_conn_state;
	int i;

	for_each_new_connector_in_state(old_state, connector, new_conn_state, i) {
		const struct drm_connector_helper_funcs *funcs;

		funcs = connector->helper_private;
		if (!funcs->atomic_commit)
			continue;

		if (new_conn_state->writeback_job && new_conn_state->writeback_job->fb) {
			WARN_ON(connector->connector_type != DRM_MODE_CONNECTOR_WRITEBACK);
			funcs->atomic_commit(connector, old_state);
		}
	}
}

static bool is_connector_changed(struct drm_connector_state *old_conn_state,
				 struct drm_connector_state *new_conn_state)
{
	return old_conn_state->crtc != new_conn_state->crtc ||
	    old_conn_state->best_encoder != new_conn_state->best_encoder;
}

static bool connector_needs_modeset(struct drm_atomic_state *old_state,
				    struct drm_connector *connector,
				    struct drm_crtc_state *crtc_state)
{
	if (crtc_state->active_changed || crtc_state->mode_changed)
		return true;

	if (crtc_state->connectors_changed) {
		struct drm_connector_state *old_conn_state, *new_conn_state;

		new_conn_state = drm_atomic_get_new_connector_state(old_state, connector);
		old_conn_state = drm_atomic_get_old_connector_state(old_state, connector);

		if (is_connector_changed(old_conn_state, new_conn_state))
			return true;
	}

	return false;
}

static bool crtc_needs_modeset(struct drm_crtc_state *new_crtc_state,
			       struct drm_crtc_state *old_crtc_state)
{
	const struct drm_crtc *crtc = new_crtc_state->crtc;
	struct vs_crtc_state *vs_crtc_state = to_vs_crtc_state(new_crtc_state);

	if (new_crtc_state->active_changed)
		return true;

	if (new_crtc_state->connectors_changed) {
		struct drm_atomic_state *old_state = old_crtc_state->state;
		struct drm_connector *conn;
		struct drm_connector_state *old_conn_state, *new_conn_state;
		int i;

		for_each_oldnew_connector_in_state(old_state, conn, old_conn_state, new_conn_state,
						   i) {
			if (conn->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
				continue;
			if (is_connector_changed(old_conn_state, new_conn_state))
				return true;
		}
		drm_dbg_atomic(crtc->dev, "no physical connectors changed [CRTC:%d:%s]\n",
			       crtc->base.id, crtc->name);
	}

	if (new_crtc_state->mode_changed) {
		if (!vs_crtc_state->seamless_mode_change)
			return true;

		drm_dbg_atomic(crtc->dev, "seamless mode change for [CRTC:%d:%s]\n",
				crtc->base.id, crtc->name);
	}

	if (vs_crtc_state->power_off_mode_changed) {
		drm_dbg_atomic(crtc->dev, "power off mode change for [CRTC:%d:%s]\n", crtc->base.id,
			       crtc->name);
		return true;
	}

	return false;
}

static bool crtc_needs_disable(struct drm_crtc_state *old_state, struct drm_crtc_state *new_state)
{
	/*
	 * No new_state means the CRTC is off, so the only criteria is whether
	 * it's currently active or in self refresh mode.
	 */
	if (!new_state)
		return drm_atomic_crtc_effectively_active(old_state);

	/*
	 * We need to disable bridge(s) and CRTC if we're transitioning out of
	 * self-refresh and changing CRTCs at the same time, because the
	 * bridge tracks self-refresh status via CRTC state.
	 */
	if (old_state->self_refresh_active && old_state->crtc != new_state->crtc)
		return true;

	/*
	 * We also need to run through the crtc_funcs->disable() function if
	 * the CRTC is currently on, if it's transitioning to self refresh
	 * mode, or if it's in self refresh mode and needs to be fully
	 * disabled.
	 */
	return old_state->active || (old_state->self_refresh_active && !new_state->active) ||
	       new_state->self_refresh_active;
}

static void disable_outputs(struct drm_device *dev, struct drm_atomic_state *old_state)
{
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state, *new_conn_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	int i;

	for_each_oldnew_connector_in_state(old_state, connector, old_conn_state, new_conn_state,
					   i) {
		struct drm_encoder *encoder;
		struct drm_bridge *bridge;

		/*
		 * Shut down everything that's in the changeset and currently
		 * still on. So need to check the old, saved state.
		 */
		if (!old_conn_state->crtc)
			continue;

		old_crtc_state = drm_atomic_get_old_crtc_state(old_state, old_conn_state->crtc);

		if (new_conn_state->crtc)
			new_crtc_state =
				drm_atomic_get_new_crtc_state(old_state, new_conn_state->crtc);
		else
			new_crtc_state = NULL;

		if (!crtc_needs_disable(old_crtc_state, new_crtc_state) ||
		    !connector_needs_modeset(old_state, connector, old_conn_state->crtc->state))
			continue;

		encoder = old_conn_state->best_encoder;

		/* We shouldn't get this far if we didn't previously have
		 * an encoder.. but WARN_ON() rather than explode.
		 */
		if (WARN_ON(!encoder))
			continue;

		drm_dbg_atomic(dev, "disabling bridge chain [ENCODER:%d:%s]\n", encoder->base.id,
			       encoder->name);

		/*
		 * Each encoder has at most one connector (since we always steal
		 * it away), so we won't call disable hooks twice.
		 */
		bridge = drm_bridge_chain_get_first_bridge(encoder);
		drm_atomic_bridge_chain_disable(bridge, old_state);
	}

	for_each_oldnew_crtc_in_state(old_state, crtc, old_crtc_state, new_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;
		int ret;
		bool drm_crtc_need_modeset, need_disable, need_modeset;

		drm_crtc_need_modeset = drm_atomic_crtc_needs_modeset(new_crtc_state);
		need_disable = crtc_needs_disable(old_crtc_state, new_crtc_state);
		need_modeset = crtc_needs_modeset(new_crtc_state, old_crtc_state);
		drm_dbg_atomic(dev,
			       "[CRTC:%d:%s] drm_need_modeset:%d need_disable:%d need_modeset:%d\n",
			       crtc->base.id, crtc->name, drm_crtc_need_modeset, need_disable,
			       need_modeset);

		if (!need_disable || !drm_crtc_need_modeset || !need_modeset) {
			drm_dbg_atomic(dev, "[CRTC:%d:%s] skip disable\n", crtc->base.id,
				       crtc->name);
			continue;
		}

		funcs = crtc->helper_private;

		drm_dbg_atomic(dev, "disabling [CRTC:%d:%s]\n", crtc->base.id, crtc->name);

		/* Right function depends upon target state. */
		if (new_crtc_state->enable && funcs->prepare)
			funcs->prepare(crtc);
		else if (funcs->atomic_disable)
			funcs->atomic_disable(crtc, old_state);
		else if (funcs->disable)
			funcs->disable(crtc);
		else if (funcs->dpms)
			funcs->dpms(crtc, DRM_MODE_DPMS_OFF);

		if (!drm_dev_has_vblank(dev))
			continue;

		ret = drm_crtc_vblank_get(crtc);
		/*
		 * Self-refresh is not a true "disable"; ensure vblank remains
		 * enabled.
		 */
		if (new_crtc_state->self_refresh_active)
			WARN_ONCE(ret != 0, "driver disabled vblank in self-refresh\n");
		else
			WARN_ONCE(ret != -EINVAL, "driver forgot to call drm_crtc_vblank_off()\n");
		if (ret == 0)
			drm_crtc_vblank_put(crtc);
	}

	for_each_oldnew_connector_in_state(old_state, connector, old_conn_state, new_conn_state,
					   i) {
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_encoder *encoder;
		struct drm_bridge *bridge;

		/*
		 * Shut down everything that's in the changeset and currently
		 * still on. So need to check the old, saved state.
		 */
		if (!old_conn_state->crtc)
			continue;

		old_crtc_state = drm_atomic_get_old_crtc_state(old_state, old_conn_state->crtc);

		if (new_conn_state->crtc)
			new_crtc_state =
				drm_atomic_get_new_crtc_state(old_state, new_conn_state->crtc);
		else
			new_crtc_state = NULL;

		if (!crtc_needs_disable(old_crtc_state, new_crtc_state) ||
		    !connector_needs_modeset(old_state, connector, old_conn_state->crtc->state))
			continue;

		encoder = old_conn_state->best_encoder;

		/* We shouldn't get this far if we didn't previously have
		 * an encoder.. but WARN_ON() rather than explode.
		 */
		if (WARN_ON(!encoder))
			continue;

		funcs = encoder->helper_private;

		drm_dbg_atomic(dev, "disabling [ENCODER:%d:%s]\n", encoder->base.id, encoder->name);

		/*
		 * Each encoder has at most one connector (since we always steal
		 * it away), so we won't call disable hooks twice.
		 */
		bridge = drm_bridge_chain_get_first_bridge(encoder);

		/* Right function depends upon target state. */
		if (funcs) {
			if (funcs->atomic_disable)
				funcs->atomic_disable(encoder, old_state);
			else if (new_conn_state->crtc && funcs->prepare)
				funcs->prepare(encoder);
			else if (funcs->disable)
				funcs->disable(encoder);
			else if (funcs->dpms)
				funcs->dpms(encoder, DRM_MODE_DPMS_OFF);
		}

		drm_atomic_bridge_chain_post_disable(bridge, old_state);
	}
}

static void crtc_set_mode(struct drm_device *dev, struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *new_conn_state;
	int i;

	for_each_new_crtc_in_state(old_state, crtc, new_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		if (!new_crtc_state->mode_changed)
			continue;

		funcs = crtc->helper_private;

		if (new_crtc_state->enable && funcs->mode_set_nofb) {
			drm_dbg_atomic(dev, "modeset on [CRTC:%d:%s]\n", crtc->base.id, crtc->name);

			funcs->mode_set_nofb(crtc);
		}
	}

	for_each_new_connector_in_state(old_state, connector, new_conn_state, i) {
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_encoder *encoder;
		struct drm_display_mode *mode, *adjusted_mode;
		struct drm_bridge *bridge;

		if (!new_conn_state->best_encoder)
			continue;

		encoder = new_conn_state->best_encoder;
		funcs = encoder->helper_private;
		new_crtc_state = new_conn_state->crtc->state;
		mode = &new_crtc_state->mode;
		adjusted_mode = &new_crtc_state->adjusted_mode;

		if (!new_crtc_state->mode_changed)
			continue;

		drm_dbg_atomic(dev, "modeset on [ENCODER:%d:%s]\n", encoder->base.id,
			       encoder->name);

		/*
		 * Each encoder has at most one connector (since we always steal
		 * it away), so we won't call mode_set hooks twice.
		 */
		if (funcs && funcs->atomic_mode_set)
			funcs->atomic_mode_set(encoder, new_crtc_state, new_conn_state);
		else if (funcs && funcs->mode_set)
			funcs->mode_set(encoder, mode, adjusted_mode);

		bridge = drm_bridge_chain_get_first_bridge(encoder);
		drm_bridge_chain_mode_set(bridge, mode, adjusted_mode);
	}
}

static void vs_drm_atomic_helper_commit_modeset_disables(struct drm_device *dev,
							 struct drm_atomic_state *old_state)
{
	disable_outputs(dev, old_state);

	drm_atomic_helper_update_legacy_modeset_state(dev, old_state);
	drm_atomic_helper_calc_timestamping_constants(old_state);
}

static void vs_drm_atomic_helper_commit_modeset_enables(struct drm_device *dev,
							struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	struct drm_crtc_state *new_crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *new_conn_state;
	int i;

	for_each_oldnew_crtc_in_state(old_state, crtc, old_crtc_state, new_crtc_state, i) {
		const struct drm_crtc_helper_funcs *funcs;

		/* Need to filter out CRTCs where only planes change. */
		if (!drm_atomic_crtc_needs_modeset(new_crtc_state))
			continue;

		if (!new_crtc_state->active)
			continue;

		if (!crtc_needs_modeset(new_crtc_state, old_crtc_state)) {
			drm_dbg_atomic(dev, "seamless change [CRTC:%d:%s], skip enable\n",
				       crtc->base.id, crtc->name);
			continue;
		}

		funcs = crtc->helper_private;

		if (new_crtc_state->enable) {
			drm_dbg_atomic(dev, "enabling [CRTC:%d:%s]\n", crtc->base.id, crtc->name);
			if (funcs->atomic_enable)
				funcs->atomic_enable(crtc, old_state);
			else if (funcs->commit)
				funcs->commit(crtc);
		}
	}

	for_each_new_connector_in_state(old_state, connector, new_conn_state, i) {
		const struct drm_encoder_helper_funcs *funcs;
		struct drm_encoder *encoder;
		struct drm_bridge *bridge;

		if (!new_conn_state->best_encoder)
			continue;

		if (!new_conn_state->crtc->state->active ||
		    !connector_needs_modeset(old_state, connector, new_conn_state->crtc->state))
			continue;

		encoder = new_conn_state->best_encoder;
		funcs = encoder->helper_private;

		drm_dbg_atomic(dev, "enabling [ENCODER:%d:%s]\n", encoder->base.id, encoder->name);

		/*
		 * Each encoder has at most one connector (since we always steal
		 * it away), so we won't call enable hooks twice.
		 */
		bridge = drm_bridge_chain_get_first_bridge(encoder);
		drm_atomic_bridge_chain_pre_enable(bridge, old_state);

		if (funcs) {
			if (funcs->atomic_enable)
				funcs->atomic_enable(encoder, old_state);
			else if (funcs->enable)
				funcs->enable(encoder);
			else if (funcs->commit)
				funcs->commit(encoder);
		}

		drm_atomic_bridge_chain_enable(bridge, old_state);
	}

	drm_atomic_helper_commit_writebacks(dev, old_state);
}

static void vs_drm_atomic_helper_wait_for_flip_done(struct drm_device *dev,
						    struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	int i;

	for_each_new_crtc_in_state(old_state, crtc, new_crtc_state, i) {
		if (!new_crtc_state->active)
			continue;

		vs_crtc_wait_for_flip_done(crtc, old_state);
	}
}

void vs_drm_atomic_commit_tail(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;

	DPU_ATRACE_BEGIN("modeset_disables");
	vs_drm_atomic_helper_commit_modeset_disables(dev, old_state);
	DPU_ATRACE_END("modeset_disables");

	DPU_ATRACE_BEGIN("crtc_set_mode");
	crtc_set_mode(dev, old_state);
	DPU_ATRACE_END("crtc_set_mode");

	DPU_ATRACE_BEGIN("modeset_enables");
	vs_drm_atomic_helper_commit_modeset_enables(dev, old_state);
	DPU_ATRACE_END("modeset_enables");

	DPU_ATRACE_BEGIN("commit_planes");
	vs_drm_atomic_commit_planes(dev, old_state, DRM_PLANE_COMMIT_ACTIVE_ONLY);
	DPU_ATRACE_END("commit_planes");

	DPU_ATRACE_BEGIN("fake_vblank");
	drm_atomic_helper_fake_vblank(old_state);
	DPU_ATRACE_END("fake_vblank");

	DPU_ATRACE_BEGIN("wait_for_flip_done");
	vs_drm_atomic_helper_wait_for_flip_done(dev, old_state);
	DPU_ATRACE_END("wait_for_flip_done");

	DPU_ATRACE_BEGIN("commit_hw_done");
	drm_atomic_helper_commit_hw_done(old_state);
	DPU_ATRACE_END("commit_hw_done");

	DPU_ATRACE_BEGIN("cleanup_planes");
	drm_atomic_helper_cleanup_planes(dev, old_state);
	DPU_ATRACE_END("cleanup_planes");
}

int vs_drm_atomic_disable_all(struct drm_device *dev, struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	u32 crtc_mask = 0;
	int ret = 0;

	state = drm_atomic_state_alloc(dev);
	if (!state) {
		drm_err(dev, "%s: unable to allocate state", __func__);
		return -ENOMEM;
	}

	DPU_ATRACE_BEGIN(__func__);
	state->acquire_ctx = ctx;

	drm_for_each_crtc(crtc, dev) {
		if (!crtc->state->active)
			continue;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			drm_err(dev, "%s: unable to retrieve %s state: %pe\n", __func__, crtc->name,
				crtc_state);
			ret = PTR_ERR(crtc_state);
			goto free;
		}

		crtc_state->active = false;
		crtc_mask |= drm_crtc_mask(crtc);
	}

	if (crtc_mask) {
		drm_dbg_atomic(dev, "%s: disabling crtc_mask=%#x\n", __func__, crtc_mask);
		ret = drm_atomic_commit(state);
	}

free:
	drm_atomic_state_put(state);
	DPU_ATRACE_END(__func__);

	return ret ? : crtc_mask;
}

/* Print Functions */
/* These are designed to match their counterparts in drm_atomic.c */

static const char *const color_encoding_name[] = {
	[DRM_COLOR_YCBCR_BT601] = "ITU-R BT.601 YCbCr",
	[DRM_COLOR_YCBCR_BT709] = "ITU-R BT.709 YCbCr",
	[DRM_COLOR_YCBCR_BT2020] = "ITU-R BT.2020 YCbCr",
};

static const char *const color_range_name[] = {
	[DRM_COLOR_YCBCR_FULL_RANGE] = "YCbCr full range",
	[DRM_COLOR_YCBCR_LIMITED_RANGE] = "YCbCr limited range",
};

static const char *drm_get_color_encoding_name(enum drm_color_encoding encoding)
{
	if (WARN_ON(encoding >= ARRAY_SIZE(color_encoding_name)))
		return "unknown";

	return color_encoding_name[encoding];
}

static const char *drm_get_color_range_name(enum drm_color_range range)
{
	if (WARN_ON(range >= ARRAY_SIZE(color_range_name)))
		return "unknown";

	return color_range_name[range];
}

static const char *const colorspace_names[] = {
	/* For Default case, driver will set the colorspace */
	[DRM_MODE_COLORIMETRY_DEFAULT] = "Default",
	/* Standard Definition Colorimetry based on CEA 861 */
	[DRM_MODE_COLORIMETRY_SMPTE_170M_YCC] = "SMPTE_170M_YCC",
	[DRM_MODE_COLORIMETRY_BT709_YCC] = "BT709_YCC",
	/* Standard Definition Colorimetry based on IEC 61966-2-4 */
	[DRM_MODE_COLORIMETRY_XVYCC_601] = "XVYCC_601",
	/* High Definition Colorimetry based on IEC 61966-2-4 */
	[DRM_MODE_COLORIMETRY_XVYCC_709] = "XVYCC_709",
	/* Colorimetry based on IEC 61966-2-1/Amendment 1 */
	[DRM_MODE_COLORIMETRY_SYCC_601] = "SYCC_601",
	/* Colorimetry based on IEC 61966-2-5 [33] */
	[DRM_MODE_COLORIMETRY_OPYCC_601] = "opYCC_601",
	/* Colorimetry based on IEC 61966-2-5 */
	[DRM_MODE_COLORIMETRY_OPRGB] = "opRGB",
	/* Colorimetry based on ITU-R BT.2020 */
	[DRM_MODE_COLORIMETRY_BT2020_CYCC] = "BT2020_CYCC",
	/* Colorimetry based on ITU-R BT.2020 */
	[DRM_MODE_COLORIMETRY_BT2020_RGB] = "BT2020_RGB",
	/* Colorimetry based on ITU-R BT.2020 */
	[DRM_MODE_COLORIMETRY_BT2020_YCC] = "BT2020_YCC",
	/* Added as part of Additional Colorimetry Extension in 861.G */
	[DRM_MODE_COLORIMETRY_DCI_P3_RGB_D65] = "DCI-P3_RGB_D65",
	[DRM_MODE_COLORIMETRY_DCI_P3_RGB_THEATER] = "DCI-P3_RGB_Theater",
	[DRM_MODE_COLORIMETRY_RGB_WIDE_FIXED] = "RGB_WIDE_FIXED",
	/* Colorimetry based on scRGB (IEC 61966-2-2) */
	[DRM_MODE_COLORIMETRY_RGB_WIDE_FLOAT] = "RGB_WIDE_FLOAT",
	[DRM_MODE_COLORIMETRY_BT601_YCC] = "BT601_YCC",
};

const char *drm_get_colorspace_name(enum drm_colorspace colorspace)
{
	if (colorspace < ARRAY_SIZE(colorspace_names) && colorspace_names[colorspace])
		return colorspace_names[colorspace];
	else
		return "(null)";
}

static void drm_gem_print_info(struct drm_printer *p, unsigned int indent,
			       const struct drm_gem_object *obj)
{
	drm_printf_indent(p, indent, "name=%d\n", obj->name);
	drm_printf_indent(p, indent, "refcount=%u\n", kref_read(&obj->refcount));
	drm_printf_indent(p, indent, "start=%08lx\n", drm_vma_node_start(&obj->vma_node));
	drm_printf_indent(p, indent, "size=%zu\n", obj->size);
	drm_printf_indent(p, indent, "imported=%s\n", str_yes_no(obj->import_attach));

	if (obj->funcs->print_info)
		obj->funcs->print_info(p, indent, obj);
}

static void drm_framebuffer_print_info(struct drm_printer *p, unsigned int indent,
				       const struct drm_framebuffer *fb)
{
	unsigned int i;

	drm_printf_indent(p, indent, "allocated by = %s\n", fb->comm);
	drm_printf_indent(p, indent, "refcount=%u\n", drm_framebuffer_read_refcount(fb));
	drm_printf_indent(p, indent, "format=%p4cc\n", &fb->format->format);
	drm_printf_indent(p, indent, "modifier=0x%llx\n", fb->modifier);
	drm_printf_indent(p, indent, "size=%ux%u\n", fb->width, fb->height);
	drm_printf_indent(p, indent, "layers:\n");

	for (i = 0; i < fb->format->num_planes; i++) {
		drm_printf_indent(p, indent + 1, "size[%u]=%dx%d\n", i,
				  drm_format_info_plane_width(fb->format, fb->width, i),
				  drm_format_info_plane_height(fb->format, fb->height, i));
		drm_printf_indent(p, indent + 1, "pitch[%u]=%u\n", i, fb->pitches[i]);
		drm_printf_indent(p, indent + 1, "offset[%u]=%u\n", i, fb->offsets[i]);
		drm_printf_indent(p, indent + 1, "obj[%u]:%s\n", i, fb->obj[i] ? "" : "(null)");
		if (fb->obj[i])
			drm_gem_print_info(p, indent + 2, fb->obj[i]);
	}
}

static void drm_atomic_plane_print_state(struct drm_printer *p, const struct drm_plane_state *state)
{
	struct drm_plane *plane = state->plane;
	struct drm_rect src = drm_plane_state_src(state);
	struct drm_rect dest = drm_plane_state_dest(state);

	drm_printf(p, "plane[%u]: %s\n", plane->base.id, plane->name);
	drm_printf(p, "\tcrtc=%s\n", state->crtc ? state->crtc->name : "(null)");
	drm_printf(p, "\tfb=%u\n", state->fb ? state->fb->base.id : 0);
	if (state->fb)
		drm_framebuffer_print_info(p, 2, state->fb);
	drm_printf(p, "\tcrtc-pos=" DRM_RECT_FMT "\n", DRM_RECT_ARG(&dest));
	drm_printf(p, "\tsrc-pos=" DRM_RECT_FP_FMT "\n", DRM_RECT_FP_ARG(&src));
	drm_printf(p, "\trotation=%x\n", state->rotation);
	drm_printf(p, "\tnormalized-zpos=%x\n", state->normalized_zpos);
	drm_printf(p, "\tcolor-encoding=%s\n", drm_get_color_encoding_name(state->color_encoding));
	drm_printf(p, "\tcolor-range=%s\n", drm_get_color_range_name(state->color_range));

	if (plane->funcs->atomic_print_state)
		plane->funcs->atomic_print_state(p, state);
}

static void drm_atomic_crtc_print_state(struct drm_printer *p, const struct drm_crtc_state *state)
{
	struct drm_crtc *crtc = state->crtc;

	drm_printf(p, "crtc[%u]: %s\n", crtc->base.id, crtc->name);
	drm_printf(p, "\tenable=%d\n", state->enable);
	drm_printf(p, "\tactive=%d\n", state->active);
	drm_printf(p, "\tself_refresh_active=%d\n", state->self_refresh_active);
	drm_printf(p, "\tplanes_changed=%d\n", state->planes_changed);
	drm_printf(p, "\tmode_changed=%d\n", state->mode_changed);
	drm_printf(p, "\tactive_changed=%d\n", state->active_changed);
	drm_printf(p, "\tconnectors_changed=%d\n", state->connectors_changed);
	drm_printf(p, "\tcolor_mgmt_changed=%d\n", state->color_mgmt_changed);
	drm_printf(p, "\tplane_mask=%x\n", state->plane_mask);
	drm_printf(p, "\tconnector_mask=%x\n", state->connector_mask);
	drm_printf(p, "\tencoder_mask=%x\n", state->encoder_mask);
	drm_printf(p, "\tmode: " DRM_MODE_FMT "\n", DRM_MODE_ARG(&state->mode));

	if (crtc->funcs->atomic_print_state)
		crtc->funcs->atomic_print_state(p, state);
}

static void drm_atomic_connector_print_state(struct drm_printer *p,
					     const struct drm_connector_state *state)
{
	struct drm_connector *connector = state->connector;

	drm_printf(p, "connector[%u]: %s\n", connector->base.id, connector->name);
	drm_printf(p, "\tcrtc=%s\n", state->crtc ? state->crtc->name : "(null)");
	drm_printf(p, "\tself_refresh_aware=%d\n", state->self_refresh_aware);
	drm_printf(p, "\tmax_requested_bpc=%d\n", state->max_requested_bpc);
	drm_printf(p, "\tcolorspace=%s\n", drm_get_colorspace_name(state->colorspace));

	if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
		if (state->writeback_job && state->writeback_job->fb)
			drm_printf(p, "\tfb=%d\n", state->writeback_job->fb->base.id);

	if (connector->funcs->atomic_print_state)
		connector->funcs->atomic_print_state(p, state);
}

static void drm_atomic_private_obj_print_state(struct drm_printer *p,
					       const struct drm_private_state *state)
{
	struct drm_private_obj *obj = state->obj;

	if (obj->funcs->atomic_print_state)
		obj->funcs->atomic_print_state(p, state);
}

/**
 * vs_drm_atomic_print_old_state - prints old drm atomic state
 * @state: atomic configuration to check
 * @p: drm printer
 *
 * This functions prints the drm atomic state snapshot using the drm printer
 * which is passed to it. This snapshot can be used for debugging purposes.
 *
 * Note that this function looks into the "old" state objects and hence it
 * can not only be used after the call to drm_atomic_helper_commit_hw_done(),
 * but it may be used after calls to drm_atomic_helper_swap_state() to
 * effectively print the previously-new state of the associated atomic commit.
 *
 * It mirrors drm_atomic_print_new_state() directly.
 */
void vs_drm_atomic_print_old_state(const struct drm_atomic_state *state, struct drm_printer *p)
{
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;
	struct drm_connector_state *connector_state;
	struct drm_private_obj *obj;
	struct drm_private_state *obj_state;
	int i;

	if (!p) {
		drm_err(state->dev, "invalid drm printer\n");
		return;
	}

	for_each_old_plane_in_state(state, plane, plane_state, i)
		drm_atomic_plane_print_state(p, plane_state);

	for_each_old_crtc_in_state(state, crtc, crtc_state, i)
		drm_atomic_crtc_print_state(p, crtc_state);

	for_each_old_connector_in_state(state, connector, connector_state, i)
		drm_atomic_connector_print_state(p, connector_state);

	for_each_old_private_obj_in_state(state, obj, obj_state, i)
		drm_atomic_private_obj_print_state(p, obj_state);
}
