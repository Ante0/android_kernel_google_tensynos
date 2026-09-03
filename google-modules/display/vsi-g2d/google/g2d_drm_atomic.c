// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>

#include <uapi/linux/sched/types.h>

#include "g2d_drm_atomic.h"
#include "g2d_crtc.h"
#include "g2d_sc.h"

#define CRTC_KTHREAD_SCHED_PRIORITY 20

static void g2d_commit_tail(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;
	const struct drm_mode_config_helper_funcs *funcs;

	funcs = dev->mode_config.helper_private;

	drm_atomic_helper_wait_for_fences(dev, old_state, false);

	drm_atomic_helper_wait_for_dependencies(old_state);

	if (funcs && funcs->atomic_commit_tail)
		funcs->atomic_commit_tail(old_state);
	else
		drm_atomic_helper_commit_tail(old_state);

	drm_atomic_helper_commit_cleanup_done(old_state);

	drm_atomic_state_put(old_state);
}

static void g2d_wait_for_flip_done(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	int ret = sc_check_frame_done(crtc);

	if (ret < 0)
		sc_reset(crtc);
}

static void g2d_drm_atomic_helper_wait_for_flip_done(struct drm_device *dev,
						     struct drm_atomic_state *old_state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *new_crtc_state;
	int i;

	for_each_new_crtc_in_state(old_state, crtc, new_crtc_state, i) {
		if (!new_crtc_state->active)
			continue;

		g2d_wait_for_flip_done(crtc, old_state);
	}
}

void g2d_drm_atomic_helper_commit_tail_rpm(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;

	drm_atomic_helper_commit_modeset_disables(dev, old_state);

	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	drm_atomic_helper_commit_planes(dev, old_state, DRM_PLANE_COMMIT_ACTIVE_ONLY);

	drm_atomic_helper_fake_vblank(old_state);

	g2d_drm_atomic_helper_wait_for_flip_done(dev, old_state);

	drm_atomic_helper_commit_hw_done(old_state);

	drm_atomic_helper_wait_for_vblanks(dev, old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);
}

static void g2d_commit_kthread_work(struct kthread_work *work)
{
	struct g2d_crtc_state *g2d_crtc_state =
		container_of(work, struct g2d_crtc_state, commit_work);
	struct drm_atomic_state *state = g2d_crtc_state->base.state;

	g2d_commit_tail(state);
}

static void g2d_commit_work_struct(struct work_struct *work)
{
	struct drm_atomic_state *state = container_of(work, struct drm_atomic_state, commit_work);

	g2d_commit_tail(state);
}

static void g2d_commit_queue_work(struct drm_atomic_state *state)
{
	int i;
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct g2d_crtc *g2d_crtc;
	struct g2d_crtc_state *g2d_crtc_state;
	struct sched_param param = { .sched_priority = CRTC_KTHREAD_SCHED_PRIORITY };

	for_each_old_crtc_in_state(state, crtc, crtc_state, i) {
		g2d_crtc = to_g2d_crtc(crtc);
		g2d_crtc_state = to_g2d_crtc_state(crtc_state);

		if (IS_ERR(g2d_crtc->commit_worker)) {
			dev_dbg(g2d_crtc->dev,
				"failed to create FIFO thread to handle g2d commit work");
			continue;
		}

		/* Only queue the commit work on the first CRTC present in the atomic state */
		sched_setscheduler_nocheck(g2d_crtc->commit_worker->task, SCHED_FIFO, &param);
		kthread_init_work(&g2d_crtc_state->commit_work, g2d_commit_kthread_work);
		kthread_queue_work(g2d_crtc->commit_worker, &g2d_crtc_state->commit_work);
		return;
	}

	/* Fallback to system_highpri_wq, when there are no CRTC in the atomic state */
	INIT_WORK(&state->commit_work, g2d_commit_work_struct);
	queue_work(system_highpri_wq, &state->commit_work);
}

int g2d_drm_atomic_commit(struct drm_device *dev, struct drm_atomic_state *state, bool nonblock)
{
	int ret;

	if (state->async_update) {
		ret = drm_atomic_helper_prepare_planes(dev, state);
		if (ret)
			return ret;

		drm_atomic_helper_async_commit(dev, state);
		drm_atomic_helper_unprepare_planes(dev, state);

		return 0;
	}

	ret = drm_atomic_helper_setup_commit(state, nonblock);
	if (ret)
		return ret;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret)
		return ret;

	if (!nonblock) {
		ret = drm_atomic_helper_wait_for_fences(dev, state, true);
		if (ret)
			goto err;
	}

	/*
	 * This is the point of no return - everything below never fails except
	 * when the hw goes bonghits. Which means we can commit the new state on
	 * the software side now.
	 */

	ret = drm_atomic_helper_swap_state(state, true);
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
		g2d_commit_queue_work(state);
	else
		g2d_commit_tail(state);

	return 0;

err:
	drm_atomic_helper_unprepare_planes(dev, state);
	return ret;
}
