// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of the direct IIF.
 *
 * The direct IIF can be used if there is no underlying sync-unit (e.g., SSU) and IP firmwares are
 * communicating with each other directly to signal fences. Therefore, to support direct fences, we
 * also need support of the firmware side.
 *
 * Copyright (C) 2025 Google LLC
 */

#include <linux/cleanup.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

#include <iif/iif-fence-table.h>
#include <iif/iif-fence.h>
#include <iif/iif-manager.h>
#include <iif/iif.h>

struct iif_direct_fence {
	int id;
	int timeline;
	int error;
	/*
	 * Holds the exact error code set by the kernel driver when signaling the fence.
	 * This is separate from @error to allow `iif_direct_fence_sync_status_locked()`
	 * to detect the error transition (from 0 to error) and trigger waiter callbacks,
	 * and to preserve precise error codes (e.g. -EDEADLK) across lossy table conversions.
	 */
	int kernel_error;
	int num_sync_points;
	struct iif_fence *iif;
	struct iif_manager *mgr;
	struct iif_fence_params params;
	struct list_head poll_cb_list;
	bool disable_poll_cb;
	bool poll_cb_pended;
	bool error_set_by_kernel;
	bool delegate_to_ap;
	spinlock_t fence_lock;
};

static bool iif_direct_fence_is_circular(struct iif_direct_fence *fence)
{
	return fence->params.fence_type == IIF_FENCE_TYPE_REUSABLE &&
	       fence->params.flags & IIF_FLAGS_CIRCULAR_REUSABLE;
}

static void iif_direct_fence_set_error(struct iif_direct_fence *fence, int error)
{
	if (fence->error && error != fence->error)
		iif_warn(fence->iif, "fence error has been overwritten: %d -> %d\n", fence->error,
			 error);

	fence->error = error;
}

static inline void iif_direct_fence_set_timeline(struct iif_direct_fence *fence, int timeline)
{
	fence->timeline = timeline;
}

static bool iif_direct_fence_table_is_errored(struct iif_direct_fence *fence)
{
	return iif_fence_table_get_flag(&fence->mgr->fence_table, fence->id) &
	       BIT(IIF_SIGNAL_TABLE_FLAG_ERROR_BIT);
}

static int iif_direct_fence_table_get_error(struct iif_direct_fence *fence)
{
	return iif_fence_table_get_error(&fence->mgr->fence_table, fence->id);
}

static void iif_direct_fence_table_set_error(struct iif_direct_fence *fence, int error)
{
	iif_fence_table_set_error(&fence->mgr->fence_table, fence->id, error);
}

static unsigned int iif_direct_fence_table_get_remaining_signals(struct iif_direct_fence *fence)
{
	return iif_fence_table_get_remaining_signals(&fence->mgr->fence_table, fence->id);
}

/* Sets the remaining signals of the fence to the signal fence table. */
static void iif_direct_fence_table_set_remaining_signals(struct iif_direct_fence *fence,
							 int remaining_signals)
{
	if (fence->delegate_to_ap)
		iif_fence_table_set_remaining_signals(&fence->mgr->fence_table, fence->id,
						      remaining_signals);
}

/* Increases the timeline value of the fence in the fence table. */
static void iif_direct_fence_table_inc_timeline(struct iif_direct_fence *fence)
{
	if (fence->delegate_to_ap)
		iif_fence_table_inc_timeline(&fence->mgr->fence_table, fence->id);
}

/* Returns the timeline value of the fence from the fence table. */
static unsigned int iif_direct_fence_table_get_timeline(struct iif_direct_fence *fence)
{
	if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		return fence->params.remaining_signalers -
		       iif_direct_fence_table_get_remaining_signals(fence);

	return iif_fence_table_get_timeline(&fence->mgr->fence_table, fence->id);
}

/* Adds a new sync point to the fence table. */
static void iif_direct_fence_table_set_sync_point_locked(struct iif_direct_fence *fence,
							 unsigned int timeline, unsigned int count)
{
	lockdep_assert_held(&fence->fence_lock);

	iif_fence_table_set_sync_point(&fence->mgr->fence_table, fence->id, fence->num_sync_points,
				       timeline, count);
	fence->num_sync_points++;
}

/* Gets the sync point timeline and count at @sync_point_index from the fence table. */
static void iif_direct_fence_table_get_sync_point(struct iif_direct_fence *fence,
						  int sync_point_index, u8 *timeline, u8 *count)
{
	iif_fence_table_get_sync_point(&fence->mgr->fence_table, fence->id, sync_point_index,
				       timeline, count);
}

/* Updates the status of a direct single-shot fence and returns true if unblocked. */
static bool iif_direct_fence_update_single_shot_status_locked(struct iif_direct_fence *fence,
							      struct iif_fence_status *status)
{
	lockdep_assert_held(&fence->fence_lock);

	status->signaled = fence->timeline >= fence->params.remaining_signalers;
	status->error = status->signaled ? fence->error : 0;

	return status->signaled;
}

/* Returns true if there is any sync point reached by the signaler at @status->timeline. */
static bool iif_direct_fence_is_sync_point_ready_locked(struct iif_direct_fence *fence,
							struct iif_fence_status *status)
{
	u8 timeline, count;
	int i;

	lockdep_assert_held(&fence->fence_lock);

	/* If the fence is circular, the fence must be unblocked at every single signal. */
	if (iif_direct_fence_is_circular(fence) && status->timeline)
		return true;

	for (i = 0; i < fence->num_sync_points; i++) {
		iif_direct_fence_table_get_sync_point(fence, i, &timeline, &count);
		if ((count == IIF_WAIT_TABLE_SYNC_POINT_COUNT_ALL ||
		     status->timeline < timeline + count) &&
		    status->timeline >= timeline)
			return true;
	}

	return false;
}

/*
 * Proceeds the fence timeline of @status (waiter's perspective) by 1 if the timeline of @fence
 * (signaler's perspective) is ahead of it.
 *
 * Returns true if @status->timeline has been advanced.
 */
static bool iif_direct_fence_proceed_timeline(struct iif_direct_fence *fence,
					      struct iif_fence_status *status)
{
	/*
	 * If the fence was never signaled, or if the waiter is already caught up with the
	 * signaler, we can't proceed the timeline. Additionally, if the fence is not circular and
	 * the waiter is ahead of the signaler (likely a bug though), we can't proceed the timeline
	 * as well.
	 */
	if (fence->timeline == 0 || status->timeline == fence->timeline ||
	    (!iif_direct_fence_is_circular(fence) && status->timeline > fence->timeline))
		return false;

	/*
	 * If the fence is circular, the timeline wraps around at IIF_SIGNAL_TABLE_MAX_TIMELINE.
	 * For non-circular fences, we also use the same modulo operation since timeline cannot
	 * exceed IIF_SIGNAL_TABLE_MAX_TIMELINE here.
	 */
	status->timeline = (status->timeline % IIF_SIGNAL_TABLE_MAX_TIMELINE) + 1;

	return true;
}

/*
 * Updates @status to advance @status->timeline and returns true if the fence is unblocked at the
 * updated timeline.
 *
 * - @status->timeline: It will be advanced to the earliest sync point reached by the signaler or
 *                      the signaler's timeline.
 * - @status->error: It will be set to the fence error if @status->timeline has reached the
 *                   signaler's timeline and the fence is errored out.
 *
 * Return: true if @status->timeline has reached a sync point, or reached the signaler's timeline
 * and the fence is errored out. Otherwise, false.
 */
static bool iif_direct_fence_update_reusable_status_locked(struct iif_direct_fence *fence,
							   struct iif_fence_status *status)
{
	bool unblocked = false;

	/*
	 * Advances the timeline viewpoint of the waiter (@status->timeline) until it reaches the
	 * signaler's timeline (@fence->timeline). If it reaches any sync-point in the middle, which
	 * should be the earliest reachable sync-point from the original @status->timeline, break
	 * the loop and unblock the fence.
	 */
	while (!unblocked && iif_direct_fence_proceed_timeline(fence, status))
		unblocked = iif_direct_fence_is_sync_point_ready_locked(fence, status);

	/*
	 * If the timeline viewpoint of waiter reaches the signaler's and the fence is errored out,
	 * the fence error must be propagated to the waiter regardless of sync points.
	 */
	if (status->timeline == fence->timeline && fence->error) {
		status->error = fence->error;
		unblocked = true;
	} else {
		status->error = 0;
	}

	return unblocked;
}

/* Returns true if the fence is unblocked. @status will be updated accordingly. */
static bool iif_direct_fence_is_unblocked_locked(struct iif_direct_fence *fence,
						 struct iif_fence_status *status)
{
	lockdep_assert_held(&fence->fence_lock);

	if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		return iif_direct_fence_update_single_shot_status_locked(fence, status);
	else if (fence->params.fence_type == IIF_FENCE_TYPE_REUSABLE)
		return iif_direct_fence_update_reusable_status_locked(fence, status);

	iif_warn(fence->iif, "Unsupported fence type while checking fence unblocked: %d",
		 fence->params.fence_type);

	return false;
}

/* Handles the error passed while signaling the kernel fence object. */
static void iif_direct_fence_handle_signal_error(struct iif_direct_fence *fence, int error)
{
	int table_error = iif_direct_fence_table_get_error(fence);
	bool errored = iif_direct_fence_table_is_errored(fence);

	/*
	 * For the backward compatibility of IP firmwares which don't set the fence error code to
	 * the table (i.e., only mark the errored flag), if the errored flag was set, but the error
	 * code is empty in the table, let the kernel driver set it on behalf of the signaler IP
	 * firmware regardless of @fence->delegate_to_ap.
	 */
	if (fence->delegate_to_ap || (errored && !table_error))
		fence->error_set_by_kernel = true;

	/*
	 * If @error is 0, we don't need to handle the error.
	 * If @fence->error_set_by_kernel is false, the firmware is supposed to set error to the
	 * fence table, and the error of kernel fence object will be synced with the fence table
	 * when `iif_direct_fence_sync_status_locked()` is called.
	 */
	if (!error || !fence->error_set_by_kernel)
		return;

	iif_direct_fence_table_set_error(fence, error);
	fence->kernel_error = error;
}

/* Invokes poll callbacks registered to a direct single-shot fence. */
static void
iif_direct_fence_invoke_poll_callbacks_single_shot_locked(struct iif_direct_fence *fence)
{
	struct iif_fence_poll_cb *cur, *tmp;

	lockdep_assert_held(&fence->fence_lock);

	/*
	 * For single-shot fence, notify waiters only if all signalers have signaled the fence
	 * regardless of the fence error.
	 */
	if (fence->timeline < fence->params.remaining_signalers)
		return;

	list_for_each_entry_safe(cur, tmp, &fence->poll_cb_list, node) {
		/*
		 * Don't need to check the return value as the if statement above guarantees the
		 * fence unblock.
		 */
		iif_direct_fence_update_single_shot_status_locked(fence, &cur->status);
		list_del_init(&cur->node);
		cur->func(fence->iif, cur);
	}
}

/* Invokes poll callbacks registered to a direct reusable fence. */
static void iif_direct_fence_invoke_poll_callbacks_reusable_locked(struct iif_direct_fence *fence)
{
	struct iif_fence_poll_cb *cur, *tmp;
	bool unblocked = false;

	lockdep_assert_held(&fence->fence_lock);

	list_for_each_entry_safe(cur, tmp, &fence->poll_cb_list, node) {
		unblocked = iif_direct_fence_update_reusable_status_locked(fence, &cur->status);
		if (unblocked) {
			/*
			 * If the fence is errored out, the fence doesn't need to be signaled
			 * anymore.
			 */
			if (cur->status.error)
				list_del_init(&cur->node);
			cur->func(fence->iif, cur);
		}
	}
}

static void iif_direct_fence_invoke_poll_callbacks_locked(struct iif_direct_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	if (unlikely(fence->disable_poll_cb)) {
		fence->poll_cb_pended = true;
		return;
	}

	if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		iif_direct_fence_invoke_poll_callbacks_single_shot_locked(fence);
	else if (fence->params.fence_type == IIF_FENCE_TYPE_REUSABLE)
		iif_direct_fence_invoke_poll_callbacks_reusable_locked(fence);

	fence->poll_cb_pended = false;
}

/*
 * Synchronizes the fence status with the fence table. If it has been actually updated, invoke the
 * poll callback to notify the update to the IIF driver users.
 */
static void iif_direct_fence_sync_status_locked(struct iif_direct_fence *fence)
{
	int table_timeline, table_error;
	bool table_errored;

	lockdep_assert_held(&fence->fence_lock);

	table_timeline = iif_direct_fence_table_get_timeline(fence);
	table_errored = iif_direct_fence_table_is_errored(fence);
	table_error = iif_direct_fence_table_get_error(fence);

	/* If @fence->error_set_by_kernel is true, use the kernel level fence error directly. */
	if (fence->error_set_by_kernel)
		table_error = fence->kernel_error;

	/*
	 * @fence->error has a value means that the errored flag was set and the fence was already
	 * errored out before. Reverting the errored fence to normal state is an invalid action. It
	 * is likely a bug of the signaler IP.
	 */
	if (unlikely(!table_errored && fence->error)) {
		iif_warn(fence->iif, "fence was already errored out, cannot revert it\n");
		table_errored = true;
	}

	/*
	 * Setting the fence error without the setting errored flag to the table is an invalid
	 * action. The error code might be a dirty value unintendedly set by the signaler IP.
	 */
	if (unlikely(!table_errored && table_error)) {
		iif_warn(fence->iif, "fence was not errored out, ignore the error\n");
		table_error = 0;
	}

	/*
	 * If the fence is errored out without any reason, treat it as canceled. If there is another
	 * thread which is going to signal @fence with the exact error code, @fence->error will be
	 * updated at that moment.
	 */
	if (unlikely(table_errored && !table_error))
		table_error = -ECANCELED;

	/* The fence timeline must be always increasing for non-circular reusable fences. */
	if (!iif_direct_fence_is_circular(fence) && unlikely(table_timeline < fence->timeline)) {
		iif_warn(fence->iif, "fence timeline shouldn't be decreased\n");
		table_timeline = fence->timeline;
	}

	/* If the fence status is not updated, do nothing. */
	if (fence->timeline == table_timeline && fence->error == table_error)
		return;

	/*
	 * Update the kernel fence object's error and timeline to match the synchronized values.
	 * If the error was set by the kernel, @table_error was overridden with the kernel error
	 * code.
	 */
	iif_direct_fence_set_error(fence, table_error);
	iif_direct_fence_set_timeline(fence, table_timeline);

	iif_direct_fence_invoke_poll_callbacks_locked(fence);
}

/*
 * Signals a single-shot fence with the specified error.
 *
 * If @retire is true, it means that this function is called for forcefully erroring out the fence
 * if the fence hasn't been fully signaled.
 */
static int iif_direct_fence_signal_single_shot_locked(struct iif_direct_fence *fence, int error,
						      bool retire)
{
	int table_remaining_signals;
	bool table_errored;

	lockdep_assert_held(&fence->fence_lock);

	/*
	 * The meaning of this function called when the signaler is an IP is that the IP has
	 * become faulty and the IIF driver takes care of updating the fence table. However, since
	 * the timing of the IP crash is nondeterministic, a race condition that the IP already
	 * unblocked the fence right before the crash, but the IP driver is going to signal the
	 * fence with an error because of the IP crash can happen. Therefore if the fence is already
	 * unblocked without error, we should ignore the signal error sent from the IP driver side.
	 *
	 * Note that if this case happens, some waiter IPs might be already notified of the fence
	 * unblock from the signaler IP before it crashes, but the IIF driver will notify waiter IP
	 * drivers and they may notify their IP of the unblock of the same fences again. That says
	 * waiter IPs can receive the fence unblock notification for the same fence for two times by
	 * the race condition, but we expect that they will ignore the second one.
	 *
	 * When the signaler is AP, that race condition won't happen since the fence table should be
	 * always managed by the IIF driver only and theoretically this logic won't have any effect.
	 */
	table_remaining_signals = iif_direct_fence_table_get_remaining_signals(fence);
	table_errored = iif_direct_fence_table_is_errored(fence);

	/* If the fence is already fully signaled, we don't need to do anything on retire. */
	if (retire && !table_remaining_signals)
		return 0;

	if (!table_remaining_signals && !table_errored && error) {
		error = 0;
	} else if (!table_remaining_signals && table_errored && !error) {
		/*
		 * Theoretically, this case wouldn't happen since @fence->delegate_to_ap was set
		 * means that the signaler IP has been crashed and the IP driver will signal the
		 * fence with an error. Handle it just in case and we can consider that the signaler
		 * command has been canceled.
		 */
		error = -ECANCELED;
	}

	if (fence->delegate_to_ap && table_remaining_signals)
		table_remaining_signals--;

	/* If the fence is retiring, set remaining signals to 0 forcefully. */
	if (retire)
		table_remaining_signals = 0;

	/*
	 * Sets the error and remaining signals to the fence table. Note that these functions will
	 * be NO-OP if @fence->delegate_to_ap is false except setting @error to @fence->error.
	 *
	 * We should set the error before signaling the fence. Otherwise, if @fence->delegate_to_ap
	 * is true so that the IIF driver is updating the fence table and if it signals the fence
	 * first, waiter IPs may misundestand that the fence has been unblocked without an error.
	 */
	iif_direct_fence_handle_signal_error(fence, error);
	iif_direct_fence_table_set_remaining_signals(fence, table_remaining_signals);

	/* Synchronizes the fence status with the fence table. */
	iif_direct_fence_sync_status_locked(fence);

	return 0;
}

/*
 * Signals a reusable fence with the specified error.
 *
 * If @retire is true, it means that this function is called for forcefully erroring out the fence
 * if the fence hasn't been errored (timed out) yet.
 */
static int iif_direct_fence_signal_reusable_locked(struct iif_direct_fence *fence, int error,
						   bool retire)
{
	int table_timeline;
	bool table_errored;

	lockdep_assert_held(&fence->fence_lock);

	if (!fence->delegate_to_ap)
		return -EOPNOTSUPP;

	table_timeline = iif_direct_fence_table_get_timeline(fence);
	table_errored = iif_direct_fence_table_is_errored(fence);

	/* If the fence is already errored, handle error setting or return an error. */
	if (table_errored) {
		/*
		 * If this call is for retiring/destroying the fence, and it is already errored out,
		 * it is already in a terminal error state and there is no need to forcefully error
		 * it out again (e.g. with -EDEADLK). This is a no-op, so return success.
		 */
		if (retire)
			return 0;

		/*
		 * Re-signaling a reusable fence that has already errored out is invalid
		 * and returns -EBUSY to avoid overwriting the original error.
		 */
		return -EBUSY;
	}

	/*
	 * For non-circular fences, if @fence->delegate_to_ap is true, see if the fence will be
	 * timedout by this signal. Otherwise, see if the fence was already timedout.
	 */
	if (!iif_direct_fence_is_circular(fence) &&
	    (table_timeline >= IIF_SIGNAL_TABLE_MAX_TIMELINE ||
	     table_timeline + (fence->delegate_to_ap ? 1 : 0) >= fence->params.timeout))
		error = -ETIMEDOUT;

	/*
	 * Updates the error and timeline of the fence table. We should set the error before
	 * increasing the timeline value. Otherwise, if the IIF driver signals the fence first,
	 * waiter IPs may misunderstand that the fence has been unblocked without an error.
	 */
	iif_direct_fence_handle_signal_error(fence, error);

	/* Increases the fence timeline by 1. */
	iif_direct_fence_table_inc_timeline(fence);

	/* Synchronizes the fence status with the fence table. */
	iif_direct_fence_sync_status_locked(fence);

	return 0;
}

static void iif_direct_fence_init_single_shot(struct iif_direct_fence *fence)
{
	struct iif_manager *mgr = fence->mgr;

	iif_fence_table_init_single_shot_fence_entry(&mgr->fence_table, fence->id,
						     fence->params.remaining_signalers,
						     fence->params.waiters);
}

static void iif_direct_fence_init_reusable(struct iif_direct_fence *fence)
{
	struct iif_manager *mgr = fence->mgr;
	u8 flag = 0;

	if (fence->params.timeout > IIF_SIGNAL_TABLE_MAX_TIMELINE)
		fence->params.timeout = IIF_SIGNAL_TABLE_MAX_TIMELINE;

	if (iif_direct_fence_is_circular(fence))
		flag |= BIT(IIF_SIGNAL_TABLE_FLAG_CIRCULAR_REUSABLE_BIT);

	iif_fence_table_init_reusable_fence_entry(
		&mgr->fence_table, fence->id, fence->params.timeout, fence->params.waiters, flag);
}

static const char *iif_direct_sync_unit_name(void *driver_data)
{
	return "iif_direct";
}

static int iif_direct_fence_create(struct iif_fence *iif, const struct iif_fence_params *params,
				   void *driver_data)
{
	struct iif_manager *mgr = driver_data;
	struct iif_direct_fence *fence;
	const unsigned int id_min = params->signaler_ip * IIF_NUM_FENCES_PER_IP;
	const unsigned int id_max = id_min + IIF_NUM_FENCES_PER_IP - 1;
	int id, ret;

	/* Direct fence only supports IP or AP signaled fences. */
	if (params->signaler_type != IIF_FENCE_SIGNALER_TYPE_IP)
		return -EOPNOTSUPP;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -ENOMEM;

	/* Allocates the fence ID. */
	id = ida_alloc_range(&mgr->idp, id_min, id_max, GFP_KERNEL);
	if (id < 0) {
		ret = id;
		goto err_free_fence;
	}

	fence->iif = iif;
	fence->mgr = mgr;
	fence->id = id;
	fence->params = *params;
	fence->delegate_to_ap = params->signaler_ip == IIF_IP_AP;
	INIT_LIST_HEAD(&fence->poll_cb_list);
	spin_lock_init(&fence->fence_lock);

	/* Initializes the entry of the fence table. */
	if (params->fence_type == IIF_FENCE_TYPE_SINGLE_SHOT) {
		iif_direct_fence_init_single_shot(fence);
	} else if (params->fence_type == IIF_FENCE_TYPE_REUSABLE) {
		iif_direct_fence_init_reusable(fence);
	} else {
		ret = -EOPNOTSUPP;
		goto err_free_id;
	}

	iif_fence_set_priv_fence_data(iif, fence);

	return fence->id;

err_free_id:
	ida_free(&fence->mgr->idp, fence->id);
err_free_fence:
	kfree(fence);

	return ret;
}

static void iif_direct_fence_retire(struct iif_fence *iif, void *driver_data)
{
	struct iif_direct_fence *fence = iif_fence_get_priv_fence_data(iif);

	scoped_guard(spinlock_irqsave, &fence->fence_lock) {
		fence->delegate_to_ap = true;

		if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
			iif_direct_fence_signal_single_shot_locked(fence, -EDEADLK,
								   /*retire=*/true);
		else if (fence->params.fence_type == IIF_FENCE_TYPE_REUSABLE)
			iif_direct_fence_signal_reusable_locked(fence, -EDEADLK, /*retire=*/true);
	}

	ida_free(&fence->mgr->idp, fence->id);
}

static void iif_direct_fence_release(struct iif_fence *iif, void *driver_data)
{
	struct iif_direct_fence *fence = iif_fence_get_priv_fence_data(iif);

	kfree(fence);
}

static int iif_direct_fence_add_sync_point(struct iif_fence *iif, u64 timeline, u64 count,
					   void *driver_data)
{
	struct iif_direct_fence *fence = iif_fence_get_priv_fence_data(iif);

	/* Only reusable fence supports adding sync points. */
	if (fence->params.fence_type != IIF_FENCE_TYPE_REUSABLE)
		return -EOPNOTSUPP;

	/* The timeline and signal count should be bigger than 0. */
	if (!timeline || !count)
		return -EINVAL;

	/* Check if the sync point timeline and count are within valid range. */
	if (count != IIF_FENCE_SYNC_POINT_COUNT_ALL &&
	    (count > IIF_SIGNAL_TABLE_MAX_TIMELINE ||
	     timeline > IIF_SIGNAL_TABLE_MAX_TIMELINE - count))
		return -EINVAL;

	if (count == IIF_FENCE_SYNC_POINT_COUNT_ALL)
		count = IIF_WAIT_TABLE_SYNC_POINT_COUNT_ALL;

	scoped_guard(spinlock_irqsave, &fence->fence_lock) {
		/* No more space to register new sync points. */
		if (fence->num_sync_points >= IIF_NUM_SYNC_POINTS)
			return -ENOSPC;

		iif_direct_fence_table_set_sync_point_locked(fence, timeline, count);

		/*
		 * If the current fence timeline already overlaps or has paased the new sync point
		 * window, but the last timeline invoked poll callbacks was too old, invoke
		 * callbacks here.
		 */
		iif_direct_fence_invoke_poll_callbacks_reusable_locked(fence);
	}

	return 0;
}

static int iif_direct_fence_signal(struct iif_fence *iif, int error, void *driver_data)
{
	struct iif_direct_fence *fence = iif_fence_get_priv_fence_data(iif);

	scoped_guard(spinlock_irqsave, &fence->fence_lock) {
		if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
			return iif_direct_fence_signal_single_shot_locked(fence, error,
									  /*retire=*/false);
		else if (fence->params.fence_type == IIF_FENCE_TYPE_REUSABLE)
			return iif_direct_fence_signal_reusable_locked(fence, error,
								       /*retire=*/false);
	}

	return -EOPNOTSUPP;
}

static int iif_direct_add_poll_cb(struct iif_fence *iif, struct iif_fence_poll_cb *cb,
				  void *driver_data)
{
	struct iif_direct_fence *fence = iif_fence_get_priv_fence_data(iif);

	guard(spinlock_irqsave)(&fence->fence_lock);

	if (iif_direct_fence_is_unblocked_locked(fence, &cb->status)) {
		INIT_LIST_HEAD(&cb->node);
		return -EPERM;
	}

	list_add_tail(&cb->node, &fence->poll_cb_list);

	return 0;
}

static bool iif_direct_remove_poll_cb(struct iif_fence *iif, struct iif_fence_poll_cb *cb,
				      void *driver_data)
{
	struct iif_direct_fence *fence = iif_fence_get_priv_fence_data(iif);
	bool removed = false;

	guard(spinlock_irqsave)(&fence->fence_lock);

	if (!list_empty(&cb->node)) {
		list_del_init(&cb->node);
		removed = true;
	}

	return removed;
}

static void iif_direct_disable_poll_cb(struct iif_fence *iif, bool disable, void *driver_data)
{
	struct iif_direct_fence *fence = iif_fence_get_priv_fence_data(iif);

	guard(spinlock_irqsave)(&fence->fence_lock);

	if (fence->disable_poll_cb == disable)
		return;

	fence->disable_poll_cb = disable;

	if (!disable && fence->poll_cb_pended)
		iif_direct_fence_invoke_poll_callbacks_locked(fence);
}

static int iif_direct_fence_delegate_to_ap(struct iif_fence *iif, void *driver_data)
{
	struct iif_direct_fence *fence = iif_fence_get_priv_fence_data(iif);

	fence->delegate_to_ap = true;

	return 0;
}

static void iif_direct_fence_unblocked(struct iif_fence *iif, void *driver_data)
{
	struct iif_direct_fence *fence = iif_fence_get_priv_fence_data(iif);

	guard(spinlock_irqsave)(&fence->fence_lock);

	iif_direct_fence_sync_status_locked(fence);
}

const struct iif_manager_fence_ops iif_direct_fence_ops = {
	.sync_unit_name = iif_direct_sync_unit_name,
	.fence_create = iif_direct_fence_create,
	.fence_retire = iif_direct_fence_retire,
	.fence_release = iif_direct_fence_release,
	.fence_add_sync_point = iif_direct_fence_add_sync_point,
	.fence_signal = iif_direct_fence_signal,
	.add_poll_cb = iif_direct_add_poll_cb,
	.remove_poll_cb = iif_direct_remove_poll_cb,
	.disable_poll_cb = iif_direct_disable_poll_cb,
	.delegate_to_ap = iif_direct_fence_delegate_to_ap,
	.fence_unblocked = iif_direct_fence_unblocked,
};
