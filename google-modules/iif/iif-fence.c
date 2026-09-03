// SPDX-License-Identifier: GPL-2.0-only
/*
 * The inter-IP fence.
 *
 * The actual functionality (waiting and signaling) won't be done by the kernel driver. The main
 * role of it is creating fences with assigning fence IDs, initializing the fence table and managing
 * the life cycle of them.
 *
 * Copyright (C) 2023-2024 Google LLC
 */

#include <linux/atomic.h>
#include <linux/container_of.h>
#include <linux/export.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/lockdep.h>
#include <linux/lockdep_types.h>
#include <linux/sched.h>
#include <linux/sort.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <iif/iif-fence-table.h>
#include <iif/iif-fence.h>
#include <iif/iif-manager.h>
#include <iif/iif-shared.h>
#include <iif/iif-sync-file.h>
#include <iif/iif.h>

#define IIF_FENCE_WAIT_CB_SIGNALED (0)

/*
 * A callback instance which will be created when `iif_fence_wait_timeout()` is called and
 * registered to a fence as a poll callback.
 */
struct iif_fence_wait_cb {
	struct iif_fence_poll_cb base;
	struct task_struct *task;
	struct iif_fence_status *status;
	unsigned long flags;
};

/* Returns true if @fence is a direct fence. */
static bool iif_fence_is_direct(struct iif_fence *fence)
{
	return fence->params.flags & IIF_FLAGS_DIRECT;
}

static inline int iif_fence_ops_fence_create(struct iif_fence *fence,
					     const struct iif_fence_params *params)
{
	return fence->fence_ops->fence_create(fence, params, fence->driver_data);
}

static inline void iif_fence_ops_fence_retire(struct iif_fence *fence)
{
	fence->fence_ops->fence_retire(fence, fence->driver_data);
}

static inline void iif_fence_ops_fence_release(struct iif_fence *fence)
{
	fence->fence_ops->fence_release(fence, fence->driver_data);
}

static inline int iif_fence_ops_fence_add_sync_point(struct iif_fence *fence, u64 timeline,
						     u64 count)
{
	if (fence->fence_ops->fence_add_sync_point)
		return fence->fence_ops->fence_add_sync_point(fence, timeline, count,
							      fence->driver_data);
	return 0;
}

static inline int iif_fence_ops_fence_signal(struct iif_fence *fence, int status)
{
	return fence->fence_ops->fence_signal(fence, status, fence->driver_data);
}

static inline int iif_fence_ops_add_poll_cb(struct iif_fence *fence, struct iif_fence_poll_cb *cb,
					    iif_fence_poll_cb_t func)
{
	cb->func = func;

	return fence->fence_ops->add_poll_cb(fence, cb, fence->driver_data);
}

static inline bool iif_fence_ops_remove_poll_cb(struct iif_fence *fence,
						struct iif_fence_poll_cb *cb)
{
	return fence->fence_ops->remove_poll_cb(fence, cb, fence->driver_data);
}

static inline int iif_fence_ops_disable_poll_cb(struct iif_fence *fence, bool disable)
{
	if (fence->fence_ops->disable_poll_cb) {
		fence->fence_ops->disable_poll_cb(fence, disable, fence->driver_data);
		return 0;
	}

	return -EOPNOTSUPP;
}

static inline int iif_fence_ops_delegate_to_ap(struct iif_fence *fence)
{
	if (fence->fence_ops->delegate_to_ap)
		return fence->fence_ops->delegate_to_ap(fence, fence->driver_data);

	return -EOPNOTSUPP;
}

static inline void iif_fence_ops_fence_unblocked(struct iif_fence *fence)
{
	if (fence->fence_ops->fence_unblocked)
		fence->fence_ops->fence_unblocked(fence, fence->driver_data);
}

/* A compare function to sort fences by their ID. */
static int compare_iif_fence_by_id(const void *lhs, const void *rhs)
{
	const struct iif_fence *lfence = *(const struct iif_fence **)lhs;
	const struct iif_fence *rfence = *(const struct iif_fence **)rhs;

	if (lfence->id < rfence->id)
		return -1;
	if (lfence->id > rfence->id)
		return 1;
	return 0;
}

/*
 * Sorts fences by their ID.
 *
 * If developers are going to hold locks of multiple fences at the same time, they should sort them
 * using this function to prevent a potential deadlock.
 *
 * Returns 0 if there are no repeating fences. Otherwise, returns -EDEADLK.
 */
static inline int iif_fences_sort_by_id(struct iif_fence **fences, int size)
{
	int i;

	sort(fences, size, sizeof(*fences), &compare_iif_fence_by_id, NULL);

	for (i = 1; i < size; i++) {
		if (fences[i - 1]->id == fences[i]->id) {
			iif_err(fences[i], "Duplicated fences in the fence array\n");
			return -EDEADLK;
		}
	}

	return 0;
}

/*
 * Holds the rwlocks which protect the number of signalers of each fence in @fences without saving
 * the IRQ state.
 *
 * To prevent a deadlock, the caller should sort @fences using the `iif_fences_sort_by_id` function
 * first.
 *
 * The caller must use the `iif_fences_write_unlock` function to release the locks.
 */
static void iif_fences_write_lock(struct iif_fence **fences, int num_fences, unsigned long *flags)
{
	int i;

	if (!fences || !num_fences)
		return;

	for (i = 0; i < num_fences; i++)
		write_lock_irqsave(&fences[i]->fence_lock, flags[i]);
}

/*
 * Releases the rwlocks held by the `iif_fences_write_lock` function without restoring the IRQ
 * state.
 */
static void iif_fences_write_unlock(struct iif_fence **fences, int num_fences, unsigned long *flags)
{
	int i;

	if (!fences || !num_fences)
		return;

	for (i = num_fences - 1; i >= 0; i--)
		write_unlock_irqrestore(&fences[i]->fence_lock, flags[i]);
}

/*
 * Returns the number of remaining signalers to be submitted. Returns 0 if all signalers are
 * submitted.
 */
static int iif_fence_unsubmitted_signalers_locked(struct iif_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	return fence->params.remaining_signalers - fence->submitted_signalers;
}

/*
 * Returns the number of outstanding signalers which have submitted signaler commands, but haven't
 * signaled @fence yet.
 */
static int iif_fence_outstanding_signalers_locked(struct iif_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	return fence->submitted_signalers - fence->signaled_signalers;
}

/* Checks whether all signalers have signaled @fence or not. */
static bool iif_fence_all_signalers_signaled_locked(struct iif_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	return fence->signaled_signalers == fence->params.remaining_signalers;
}

/* Checks whether @fence is already retired or not. */
static inline bool iif_fence_has_retired_locked(struct iif_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	return fence->state == IIF_FENCE_STATE_RETIRED;
}

/* Prints a warning log when @fence is retiring when there are remaining outstand waiters. */
static void iif_fence_retire_print_outstanding_waiters_warning(struct iif_fence *fence)
{
	char waiters[64] = { 0 };
	int i = 0, written = 0, tmp;
	enum iif_ip_type waiter;

	for_each_waiting_ip(&fence->mgr->fence_table, fence->id, waiter, tmp) {
		written += scnprintf(waiters + written, sizeof(waiters) - written, "%.*s%d", i, " ",
				     waiter);
		i++;
	}

	iif_warn(
		fence,
		"Fence is retiring when outstanding waiters > 0, it's likely a bug of the waiter IP driver, waiter_ips=[%s]\n",
		waiters);
}

/* Returns the fence ID to the ID pool. */
static void iif_fence_retire_locked(struct iif_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	if (iif_fence_has_retired_locked(fence))
		return;

	/*
	 * If the waiter IP driver calls `iif_fence_put()` before it waits on waiter commands and
	 * calls `iif_fence_waiter_completed()` somehow, the case that @fence retires while it is
	 * destroying can happen.
	 *
	 * In this case, there is a potential bug that the IP accesses @fence which is already
	 * retired. The waiter IP driver should ensure that calling `iif_fence_waiter_completed()`
	 * first and then `iif_fence_put()` in any case to guarantee that water IPs are not
	 * referring @fence anymore.
	 */
	if (fence->outstanding_waiters)
		iif_fence_retire_print_outstanding_waiters_warning(fence);

	/* Removes the fence from the ID to fence object hash table. */
	iif_manager_remove_fence_from_hlist(fence->mgr, fence);

	/* Asks the sync-unit to retire the fence. */
	iif_fence_ops_fence_retire(fence);

	/*
	 * The sync-unit driver is expected to internally unregister @fence->sync_unit_poll_cb on
	 * retire, but ensures that the callback is removed just in case.
	 */
	iif_fence_ops_remove_poll_cb(fence, &fence->sync_unit_poll_cb);

	fence->state = IIF_FENCE_STATE_RETIRED;
}

/*
 * If there are no more outstanding waiters and no file binding to this fence, we can assume that
 * there will be no more signalers/waiters. Therefore, we can retire the fence ID earlier to not
 * block allocating an another fence.
 */
static void iif_fence_retire_if_possible_locked(struct iif_fence *fence)
{
	lockdep_assert_held(&fence->fence_lock);

	if (!(fence->params.flags & IIF_FLAGS_RETIRE_ON_RELEASE) && !fence->outstanding_waiters &&
	    !iif_fence_outstanding_signalers_locked(fence) && !atomic_read(&fence->num_sync_file))
		iif_fence_retire_locked(fence);
}

/*
 * Increases the number of signaled signalers of @fence.
 *
 * If @complete is true, it will forcefully set the number of signaled signalers to the total.
 * This must be used only when @fence is going to be released before it is signaled and let the
 * fence can retire.
 *
 * The function returns the number of remaining signalers which will signal the fence. If the
 * function returns a positive number, it means that there are (or will be) more signalers which
 * can signal the fence.
 */
static int iif_fence_inc_signaled_signalers_locked(struct iif_fence *fence, bool complete)
{
	int ret;

	lockdep_assert_held(&fence->fence_lock);

	if (iif_fence_all_signalers_signaled_locked(fence))
		return 0;

	if (!complete)
		fence->signaled_signalers++;
	else
		fence->signaled_signalers = fence->params.remaining_signalers;

	ret = fence->params.remaining_signalers - fence->signaled_signalers;

	/*
	 * Normally @fence won't be retired here and it will be retired when there are no more
	 * outstanding waiters and all file descriptors linked to @fence are closed. However, if
	 * somehow all runtime and waiter IPs are crashed at the same time (or even the signaler IP
	 * is also crashed), the fence can be retired at this moment.
	 */
	iif_fence_retire_if_possible_locked(fence);

	return ret;
}

/*
 * Submits a signaler to @fence.
 *
 * If @complete is true, it will make @fence have finished the signaler submission. This must be
 * used only when @fence is going to be released before the signaler submission is being finished
 * and let the IP driver side notice that there was some problem by triggering registered callbacks.
 */
static void iif_fence_submit_signaler_locked(struct iif_fence *fence, bool complete)
{
	struct iif_fence_all_signaler_submitted_cb *cur, *tmp;

	lockdep_assert_held(&fence->fence_lock);

	if (!complete)
		fence->submitted_signalers++;
	else
		fence->submitted_signalers = fence->params.remaining_signalers;

	/* Notifies the waiters if all signalers have been submitted. */
	if (iif_fence_unsubmitted_signalers_locked(fence))
		return;

	list_for_each_entry_safe(cur, tmp, &fence->all_signaler_submitted_cb_list, node) {
		list_del_init(&cur->node);
		cur->func(fence, cur);
	}
}

/* Submits @waiter_ip as a waiter to @fence. */
static void iif_fence_submit_waiter_locked(struct iif_fence *fence, enum iif_ip_type waiter_ip)
{
	struct iif_fence_params params;

	lockdep_assert_held(&fence->fence_lock);

	fence->outstanding_waiters++;
	fence->outstanding_waiters_per_ip[waiter_ip]++;

	/* If the waiter is already set, we don't need to update params. */
	if (fence->params.waiters & BIT(waiter_ip))
		return;

	/* Fills @params out with new waiters. */
	params = fence->params;
	params.waiters |= BIT(waiter_ip);

	/* TODO(b/398979516): Propagates updates waiters to the underlying sync-unit. */
	iif_fence_table_set_waiting_ip(&fence->mgr->fence_table, fence->id, waiter_ip);

	/* Updates @params of the kernel fence object. */
	fence->params = params;
}

/* Decreases the number of outstanding waiters of @waiter_ip. */
static void iif_fence_remove_waiter_locked(struct iif_fence *fence, enum iif_ip_type waiter_ip)
{
	lockdep_assert_held(&fence->fence_lock);

	WARN_ON(waiter_ip >= IIF_IP_NUM);
	WARN_ON(!fence->outstanding_waiters || !fence->outstanding_waiters_per_ip[waiter_ip]);

	fence->outstanding_waiters--;
	fence->outstanding_waiters_per_ip[waiter_ip]--;
	iif_fence_retire_if_possible_locked(fence);
}

/* Decreases the number of outstanding waiters of @waiter_ip. */
static void iif_fence_remove_waiter(struct iif_fence *fence, enum iif_ip_type waiter_ip)
{
	unsigned long flags;

	write_lock_irqsave(&fence->fence_lock, flags);
	iif_fence_remove_waiter_locked(fence, waiter_ip);
	write_unlock_irqrestore(&fence->fence_lock, flags);
}

/*
 * Checks whether a signaler can be submitted to @fences.
 * If all signalers are already submitted, submitting signalers is not allowed anymore.
 *
 * Returns 0 on success. Otherwise, a negative errno.
 */
static int iif_fences_are_signaler_submittable_locked(struct iif_fence **fences, int num_fences)
{
	int i;

	for (i = 0; i < num_fences; i++) {
		lockdep_assert_held(&fences[i]->fence_lock);

		if (!iif_fence_unsubmitted_signalers_locked(fences[i]) ||
		    iif_fence_has_retired_locked(fences[i]))
			return -EPERM;
	}

	return 0;
}

/* Submits a signaler to @fences. */
static void iif_fences_submit_signaler_locked(struct iif_fence **fences, int num_fences)
{
	int i;

	for (i = 0; i < num_fences; i++) {
		lockdep_assert_held(&fences[i]->fence_lock);
		iif_fence_submit_signaler_locked(fences[i], false);
	}
}

/*
 * Checks whether a waiter can be submitted to @fences.
 *
 * Returns 0 on success. Otherwise, a negative errno.
 */
static int iif_fences_are_waiter_submittable_locked(struct iif_fence **fences, int num_fences)
{
	int i;

	for (i = 0; i < num_fences; i++) {
		lockdep_assert_held(&fences[i]->fence_lock);

		if (iif_fence_has_retired_locked(fences[i]))
			return -EPERM;
	}

	return 0;
}

/* Submits a waiter to @fences. */
static void iif_fences_submit_waiter_locked(struct iif_fence **fences, int num_fences,
					    enum iif_ip_type waiter_ip)
{
	int i;

	for (i = 0; i < num_fences; i++) {
		lockdep_assert_held(&fences[i]->fence_lock);
		iif_fence_submit_waiter_locked(fences[i], waiter_ip);
	}
}

/*
 * The poll callback which will be registered to sync-unit fences.
 *
 * The sync-unit driver must guarantee that the callback won't be called concurrently for the same
 * fence object.
 */
static void iif_fence_poll_cb_func(struct iif_fence *fence, struct iif_fence_poll_cb *cb)
{
	/* Here must be the only place which updates the @fence status. */
	if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		fence->signaled = cb->status.signaled;
	else
		fence->timeline = cb->status.timeline;

	fence->signal_error = cb->status.error;

	/*
	 * Don't need to check the return value because the purpose of `signaled_work` is to invoke
	 * `fence_unblocked()` operators and the IP drivers don't need to get the notification for
	 * every single signal. It is enough to read the latest fence status at the last
	 * `signaler_work` invocation.
	 */
	schedule_work(&fence->signaled_work);
}

/*
 * Signals @fence with @error.
 *
 * If @force is true, it will signal the fence even though there is no outstanding signaler. It is
 * supposed to be used only when @fence is destroying.
 */
static int iif_fence_signal_with_status_locked(struct iif_fence *fence, int error, bool force)
{
	lockdep_assert_held(&fence->fence_lock);

	if (iif_fence_all_signalers_signaled_locked(fence)) {
		iif_err(fence, "The fence is already signaled by all signalers\n");
		return -EBUSY;
	}

	if (!force && !iif_fence_outstanding_signalers_locked(fence)) {
		iif_err(fence, "There is no outstanding signalers\n");
		return -EPERM;
	}

	if (error > 0 || error < -MAX_ERRNO) {
		iif_err(fence, "Invalid fence error: %d\n", error);
		return -EINVAL;
	}

	/*
	 * If @fence->propagate is not set, ignore it.
	 *
	 * For the backward compatibility, if the fence is a direct fence, we should ask the
	 * iif-direct to set @error as there is no sync-unit even if @fence->propagate is false.
	 */
	if (!fence->propagate && !iif_fence_is_direct(fence))
		return 0;

	return iif_fence_ops_fence_signal(fence, error);
}

/*
 * Increases the number of signaled signalers of @fence and schedules @waited_work if all signalers
 * have signaled the fence.
 *
 * Returns the number of remaining signalers which can signal the fence.
 */
static int
iif_fence_do_waited_work_if_no_outstanding_signalers(struct iif_fence *fence,
						     void (*waited_work)(struct iif_fence *))
{
	unsigned long flags;
	int outstanding_signalers = 0;
	int ret;

	write_lock_irqsave(&fence->fence_lock, flags);

	if (iif_fence_all_signalers_signaled_locked(fence)) {
		ret = -EBUSY;
		goto out;
	}

	if (!iif_fence_outstanding_signalers_locked(fence)) {
		ret = -EPERM;
		goto out;
	}

	/*
	 * Increases the number of signaled signalers and retires the fence is possible.
	 * The returned value should be the number of remaining signalers.
	 */
	ret = iif_fence_inc_signaled_signalers_locked(fence, false);
	outstanding_signalers = iif_fence_outstanding_signalers_locked(fence);
out:
	write_unlock_irqrestore(&fence->fence_lock, flags);

	/* If there are no outstanding signalers, execute @waited_work. */
	if (ret >= 0 && !outstanding_signalers)
		waited_work(fence);

	return ret;
}

/* The fence signaling function which supports the backward compatiblilty with a direct fence. */
static int iif_fence_signal_and_update(struct iif_fence *fence, int error,
				       void (*waited_work)(struct iif_fence *))
{
	unsigned long flags;
	int ret;

	write_lock_irqsave(&fence->fence_lock, flags);
	ret = iif_fence_signal_with_status_locked(fence, error, false);
	write_unlock_irqrestore(&fence->fence_lock, flags);

	/*
	 * This is for the backward compatibility for a direct fence. For non-direct fences, the
	 * fence status is supposed to be updated when the underlying sync unit notifies the IIF
	 * driver of the fence signal.
	 */
	if (!ret && iif_fence_is_direct(fence)) {
		/*
		 * If there are no outstanding signalers, execute @waited_work to release block
		 * wakelocks which were pended to be released because of outstanding signalers when
		 * `waited_work()` was called.
		 *
		 * If the fence is reusable, `iif_fence_signaler_completed()` will be called by the
		 * IP driver when a signaler command completes which will execute @waited_work.
		 */
		if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
			ret = iif_fence_do_waited_work_if_no_outstanding_signalers(fence,
										   waited_work);
	}

	return ret;
}

/*
 * Acquires the block wakelock of @ip.
 *
 * This function can be called in the normal context only.
 */
static inline int iif_fence_acquire_block_wakelock(struct iif_fence *fence, enum iif_ip_type ip)
{
	return iif_manager_acquire_block_wakelock(fence->mgr, ip);
}

/*
 * Releases the block wakelock of @ip for @locks times.
 *
 * This function can be called in the normal context only.
 */
static inline void iif_fence_release_block_wakelock(struct iif_fence *fence, enum iif_ip_type ip,
						    int locks)
{
	while (locks > 0 && locks--)
		iif_manager_release_block_wakelock(fence->mgr, ip);
}

/* Releases all block wakelock which hasn't been released yet. */
static void iif_fence_release_all_block_wakelock(struct iif_fence *fence)
{
	int i;
	uint16_t locks[IIF_IP_RESERVED] = { 0 };
	unsigned long flags;

	write_lock_irqsave(&fence->fence_lock, flags);

	for (i = 0; i < IIF_IP_RESERVED; i++) {
		locks[i] = fence->outstanding_block_wakelock[i];
		fence->outstanding_block_wakelock[i] = 0;
	}

	write_unlock_irqrestore(&fence->fence_lock, flags);

	for (i = 0; i < IIF_IP_RESERVED; i++) {
		while (locks[i]) {
			iif_manager_release_block_wakelock(fence->mgr, i);
			locks[i]--;
		}
	}
}

/*
 * Releases the block wakelock of @ip for multiple fences.
 *
 * This function can be called in the normal context only.
 */
static void iif_fences_release_block_wakelock(struct iif_fence **fences, int num_fences,
					      enum iif_ip_type ip)
{
	int i;

	for (i = num_fences - 1; i >= 0; i--)
		iif_fence_release_block_wakelock(fences[i], ip, 1);
}

/*
 * Acquires the block wakelock of @ip for multiple fences.
 *
 * This function can be called in the normal context only.
 */
static int iif_fences_acquire_block_wakelock(struct iif_fence **fences, int num_fences,
					     enum iif_ip_type ip)
{
	int i, ret = 0;

	for (i = 0; i < num_fences; i++) {
		ret = iif_fence_acquire_block_wakelock(fences[i], ip);
		if (ret)
			break;
	}

	if (ret)
		iif_fences_release_block_wakelock(fences, i, ip);

	return ret;
}

/*
 * Releases the block wakelock of waiters of multiple fences. @wakelock_held should describe which
 * IP wakelock was held for which fence.
 *
 * This function can be called in the normal context only.
 */
static void iif_fences_release_block_wakelock_of_waiters(struct iif_fence **fences, int num_fences,
							 const u16 *wakelock_held)
{
	enum iif_ip_type ip;
	int i, tmp;

	for (i = num_fences - 1; i >= 0; i--) {
		for_each_ip(wakelock_held[i], ip, tmp)
			iif_fence_release_block_wakelock(fences[i], ip, 1);
	}
}

/*
 * Acquires the block wakelock of waiters of multiple fences. Which IP wakelock was held for which
 * fence will be passed to @wakelock_held.
 *
 * This function can be called in the normal context only.
 */
static int iif_fences_acquire_block_wakelock_of_waiters(struct iif_fence **fences, int num_fences,
							u16 *wakelock_held)
{
	enum iif_ip_type ip;
	int i, tmp, ret = 0;

	for (i = 0; i < num_fences && !ret; i++) {
		for_each_ip(fences[i]->params.waiters, ip, tmp) {
			ret = iif_fence_acquire_block_wakelock(fences[i], ip);
			if (ret)
				break;
			wakelock_held[i] |= BIT(ip);
		}
	}

	if (ret)
		iif_fences_release_block_wakelock_of_waiters(fences, i, wakelock_held);

	return ret;
}

/* Cleans up @fence which was initialized by the `iif_fence_init` function. */
static void iif_fence_do_destroy(struct iif_fence *fence)
{
	unsigned long flags;

	/* Checks whether there is remaining all_signaler_submitted and poll callbacks. */
	write_lock_irqsave(&fence->fence_lock, flags);

	if (!list_empty(&fence->all_signaler_submitted_cb_list) &&
	    fence->submitted_signalers < fence->params.remaining_signalers) {
		fence->all_signaler_submitted_error = -EDEADLK;
		iif_fence_submit_signaler_locked(fence, true);
	}

	if (!iif_fence_all_signalers_signaled_locked(fence)) {
		/*
		 * This case can happen when:
		 * - The signaler runtime just didn't submit enough signaler commands or it
		 *   becomes unavailable to submit commands in the middle (e.g., IP crashes).
		 * - The signaler IP kernel driver didn't call `iif_fence_signaler_completed()`
		 *   before calling `iif_fence_put()` somehow.
		 */
		iif_warn(
			fence,
			"Fence is destroying before signaled, likely a bug of the signaler, signaler_ip=%d\n",
			fence->params.signaler_ip);

		iif_fence_inc_signaled_signalers_locked(fence, true);
	}

	/*
	 * It is supposed to be retired when the file is closed and there are no more outstanding
	 * waiters. However, let's ensure that the fence is retired before releasing it.
	 */
	iif_fence_retire_locked(fence);

	write_unlock_irqrestore(&fence->fence_lock, flags);

	/*
	 * If the IP driver puts @fence asynchronously, the works might be not finished. We should
	 * wait for them.
	 *
	 * As the sync-unit driver can signal the fence when the fence has retired above, we should
	 * flush the works after that.
	 */
	flush_work(&fence->signaled_work);
	flush_work(&fence->waited_work);

	/*
	 * If @fence is not signaled normally or IP drivers haven't called
	 * `iif_fence_waiter_completed()` with some reasons, there would be block wakelocks which
	 * haven't released yet. We should release all of them.
	 */
	iif_fence_release_all_block_wakelock(fence);

	/* Release sync-unit fence object. */
	iif_fence_ops_fence_release(fence);

#if IS_ENABLED(CONFIG_DEBUG_SPINLOCK)
	lockdep_unregister_key(&fence->fence_lock_key);
#endif /* IS_ENABLED(CONFIG_DEBUG_SPINLOCK) */

	iif_manager_unset_fence_ops(fence->mgr, fence);
	iif_manager_put(fence->mgr);

	if (fence->ops && fence->ops->on_release)
		fence->ops->on_release(fence);
}

/* Will be called once the refcount of @fence becomes 0 and destroy it. */
static void iif_fence_destroy(struct kref *kref)
{
	struct iif_fence *fence = container_of(kref, struct iif_fence, kref);

	iif_fence_do_destroy(fence);
}

/* Will be called once the refcount of @fence becomes 0 and destroy it asynchronously. */
static void iif_fence_destroy_async(struct kref *kref)
{
	struct iif_fence *fence = container_of(kref, struct iif_fence, kref);

	/* No need to check return value since this destroy should only happen once per fence. */
	schedule_work(&fence->put_work);
}

/* A worker function which will be called when @fence is signaled. */
static void iif_fence_signaled_work_func(struct work_struct *work)
{
	struct iif_fence *fence = container_of(work, struct iif_fence, signaled_work);

	iif_manager_broadcast_fence_unblocked(fence->mgr, fence);
}

static void iif_fence_waited_work_func(struct work_struct *work)
{
	struct iif_fence *fence = container_of(work, struct iif_fence, waited_work);
	uint16_t locks[IIF_IP_RESERVED] = { 0 };
	unsigned long flags;
	int i;

	write_lock_irqsave(&fence->fence_lock, flags);

	/*
	 * Note that if there are outstanding signalers, releasing the block wakelock will be pended
	 * there are no outstanding signalers or the fence destroys.
	 *
	 * For single-shot fences, this case can happen when the signaler IPx is not responding in
	 * time and the waiter IPy processes its command as timeout. For reusable fences, it can
	 * always happen as signalers will keep signaling the fence and waiters will leave in the
	 * middle once the fence timeline reaches the value they are waiting for.
	 *
	 * This pending logic is required because if IPy releases its block wakelock and IPy signals
	 * the fence, IPx may try to notify IPy whose block is already powered down and it may cause
	 * an unexpected bug if IPy spec doesn't allow that.
	 */
	if (iif_fence_outstanding_signalers_locked(fence)) {
		write_unlock_irqrestore(&fence->fence_lock, flags);
		return;
	}

	for (i = 0; i < IIF_IP_RESERVED; i++) {
		if (fence->outstanding_block_wakelock[i] > fence->outstanding_waiters_per_ip[i]) {
			locks[i] = fence->outstanding_block_wakelock[i] -
				   fence->outstanding_waiters_per_ip[i];
			fence->outstanding_block_wakelock[i] = fence->outstanding_waiters_per_ip[i];
		}
	}

	write_unlock_irqrestore(&fence->fence_lock, flags);

	for (i = 0; i < IIF_IP_RESERVED; i++)
		iif_fence_release_block_wakelock(fence, i, locks[i]);
}

static inline void iif_fence_waited_work(struct iif_fence *fence)
{
	iif_fence_waited_work_func(&fence->waited_work);
}

static inline void iif_fence_waited_work_async(struct iif_fence *fence)
{
	/*
	 * Don't need to check the return as it is not required to execute
	 * `iif_fence_waited_work_func()` per `waiter_completed()` call. The function only refers to
	 * the last number of outstanding block wakelocks and waiters.
	 */
	schedule_work(&fence->waited_work);
}

static void iif_fence_put_work_func(struct work_struct *work)
{
	struct iif_fence *fence = container_of(work, struct iif_fence, put_work);

	iif_fence_do_destroy(fence);
}

static void iif_fence_wait_poll(struct iif_fence *fence, struct iif_fence_poll_cb *poll_cb)
{
	struct iif_fence_wait_cb *wait_cb = container_of(poll_cb, struct iif_fence_wait_cb, base);

	/*
	 * For reusable fences, this poll callback can be invoked multiple times if the signaler
	 * fires signals faster than the waiter thread wakes up and returns from
	 * iif_fence_wait_timeout_with_status(). The waiter only needs the status of the first
	 * unblock event that matches the timeline target passed to the wait function.
	 * This ensures that subsequent signals do not overwrite the initial unblock status
	 * before the waiter has finished waking up.
	 *
	 * This logic is also compatible with single-shot fences, where the callback is
	 * invoked exactly once when the fence is unblocked.
	 */
	if (!test_and_set_bit(IIF_FENCE_WAIT_CB_SIGNALED, &wait_cb->flags))
		*wait_cb->status = poll_cb->status;
	wake_up_process(wait_cb->task);
}

int iif_fence_init(struct iif_manager *mgr, struct iif_fence *fence,
		   const struct iif_fence_ops *ops, enum iif_ip_type signaler_ip,
		   uint16_t total_signalers)
{
	struct iif_fence_params params;

	params.signaler_type = IIF_FENCE_SIGNALER_TYPE_IP;
	params.fence_type = IIF_FENCE_TYPE_SINGLE_SHOT;
	params.signaler_ip = signaler_ip;
	params.remaining_signalers = total_signalers;
	params.waiters = 0;
	params.timeout = 0;
	params.flags = IIF_FLAGS_DIRECT;

	return iif_fence_init_with_params(mgr, fence, ops, &params);
}
EXPORT_SYMBOL_GPL(iif_fence_init);

int iif_fence_init_with_params(struct iif_manager *mgr, struct iif_fence *fence,
			       const struct iif_fence_ops *ops,
			       const struct iif_fence_params *params)
{
	int id, ret;

	if (params->signaler_ip >= IIF_IP_NUM)
		return -EINVAL;

	ret = iif_manager_set_fence_ops(mgr, fence, params);
	if (ret)
		return ret;

	id = iif_fence_ops_fence_create(fence, params);
	if (id < 0) {
		iif_manager_unset_fence_ops(mgr, fence);
		return id;
	}

	/* Ensure that the callback is initialized. */
	memset(&fence->sync_unit_poll_cb.status, 0, sizeof(fence->sync_unit_poll_cb.status));
	INIT_LIST_HEAD(&fence->sync_unit_poll_cb.node);

	ret = iif_fence_ops_add_poll_cb(fence, &fence->sync_unit_poll_cb, iif_fence_poll_cb_func);
	if (ret < 0) {
		iif_fence_ops_fence_retire(fence);
		iif_manager_unset_fence_ops(mgr, fence);
		return ret;
	}

	fence->id = id;
	fence->params = *params;
	fence->mgr = iif_manager_get(mgr);
	fence->signaler_ip = params->signaler_ip;
	fence->submitted_signalers = 0;
	fence->signaled_signalers = 0;
	fence->outstanding_waiters = 0;
	fence->signal_error = 0;
	fence->all_signaler_submitted_error = 0;
	fence->timeline = 0;
	fence->signaled = false;
	fence->ops = ops;
	fence->state = IIF_FENCE_STATE_INITIALIZED;
	fence->propagate = params->signaler_ip == IIF_IP_AP;
	kref_init(&fence->kref);
#if IS_ENABLED(CONFIG_DEBUG_SPINLOCK)
	lockdep_register_key(&fence->fence_lock_key);
	__rwlock_init(&fence->fence_lock, "&fence->fence_lock", &fence->fence_lock_key);
#else
	rwlock_init(&fence->fence_lock);
#endif /* IS_ENABLED(CONFIG_DEBUG_SPINLOCK) */
	INIT_LIST_HEAD(&fence->all_signaler_submitted_cb_list);
	atomic_set(&fence->num_sync_file, 0);
	INIT_WORK(&fence->signaled_work, &iif_fence_signaled_work_func);
	INIT_WORK(&fence->waited_work, &iif_fence_waited_work_func);
	INIT_WORK(&fence->put_work, &iif_fence_put_work_func);
	memset(fence->outstanding_waiters_per_ip, 0, sizeof(fence->outstanding_waiters_per_ip));
	memset(fence->outstanding_block_wakelock, 0, sizeof(fence->outstanding_block_wakelock));

	/* Adds the fence to the ID to fence object hash table. */
	iif_manager_add_fence_to_hlist(fence->mgr, fence);

	return 0;
}
EXPORT_SYMBOL_GPL(iif_fence_init_with_params);

void iif_fence_set_priv_fence_data(struct iif_fence *iif, void *fence_data)
{
	iif->fence_data = fence_data;
}
EXPORT_SYMBOL_GPL(iif_fence_set_priv_fence_data);

void *iif_fence_get_priv_fence_data(struct iif_fence *iif)
{
	return iif->fence_data;
}
EXPORT_SYMBOL_GPL(iif_fence_get_priv_fence_data);

int iif_fence_set_flags(struct iif_fence *fence, unsigned long flags, bool clear)
{
	unsigned long irq_flags;
	int ret = 0;

	write_lock_irqsave(&fence->fence_lock, irq_flags);

	/* Changing the direct mode after the fence creation is not allowed. */
	if ((flags & IIF_FLAGS_DIRECT) &&
	    (clear == (bool)(fence->params.flags & IIF_FLAGS_DIRECT))) {
		ret = -EINVAL;
		goto out;
	}

	if (!clear)
		fence->params.flags |= flags;
	else
		fence->params.flags &= ~flags;

	if (flags & IIF_FLAGS_DISABLE_POLL)
		iif_fence_ops_disable_poll_cb(fence, !clear);
out:
	write_unlock_irqrestore(&fence->fence_lock, irq_flags);

	return ret;
}
EXPORT_SYMBOL_GPL(iif_fence_set_flags);

int iif_fence_install_fd(struct iif_fence *fence)
{
	struct iif_sync_file *sync_file;
	int fd;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

	sync_file = iif_sync_file_create(fence);
	if (IS_ERR(sync_file)) {
		put_unused_fd(fd);
		return PTR_ERR(sync_file);
	}

	fd_install(fd, sync_file->file);

	return fd;
}
EXPORT_SYMBOL_GPL(iif_fence_install_fd);

void iif_fence_on_sync_file_release(struct iif_fence *fence)
{
	unsigned long flags;

	write_lock_irqsave(&fence->fence_lock, flags);
	iif_fence_retire_if_possible_locked(fence);
	write_unlock_irqrestore(&fence->fence_lock, flags);
}

struct iif_fence *iif_fence_get(struct iif_fence *fence)
{
	if (fence)
		kref_get(&fence->kref);
	return fence;
}
EXPORT_SYMBOL_GPL(iif_fence_get);

struct iif_fence *iif_fence_fdget(int fd)
{
	struct iif_sync_file *sync_file;
	struct iif_fence *fence;

	sync_file = iif_sync_file_fdget(fd);
	if (IS_ERR(sync_file))
		return ERR_CAST(sync_file);

	fence = iif_fence_get(sync_file->fence);

	/*
	 * Since `iif_sync_file_fdget` opens the file and increases the file refcount, put here as
	 * we don't need to access the file anymore in this function.
	 */
	fput(sync_file->file);

	return fence;
}
EXPORT_SYMBOL_GPL(iif_fence_fdget);

void iif_fence_put(struct iif_fence *fence)
{
	if (fence)
		kref_put(&fence->kref, iif_fence_destroy);
}
EXPORT_SYMBOL_GPL(iif_fence_put);

void iif_fence_put_async(struct iif_fence *fence)
{
	if (fence)
		kref_put(&fence->kref, iif_fence_destroy_async);
}
EXPORT_SYMBOL_GPL(iif_fence_put_async);

int iif_fence_submit_signaler(struct iif_fence *fence)
{
	return iif_fence_submit_signaler_and_waiter(NULL, 0, &fence, 1, 0);
}
EXPORT_SYMBOL_GPL(iif_fence_submit_signaler);

int iif_fence_submit_waiter(struct iif_fence *fence, enum iif_ip_type ip)
{
	might_sleep();

	if (ip >= IIF_IP_NUM)
		return -EINVAL;

	return iif_fence_submit_signaler_and_waiter(&fence, 1, NULL, 0, ip);
}
EXPORT_SYMBOL_GPL(iif_fence_submit_waiter);

int iif_fence_add_sync_point(struct iif_fence *fence, u64 timeline, u64 count)
{
	if (fence->params.fence_type != IIF_FENCE_TYPE_REUSABLE)
		return -EOPNOTSUPP;

	if (fence->params.flags & IIF_FLAGS_CIRCULAR_REUSABLE)
		return -EINVAL;

	if (!timeline || !count)
		return -EINVAL;

	/* Check if the sync point timeline and count are within valid range. */
	if (count != IIF_FENCE_SYNC_POINT_COUNT_ALL && timeline > ULLONG_MAX - count)
		return -EINVAL;

	return iif_fence_ops_fence_add_sync_point(fence, timeline, count);
}
EXPORT_SYMBOL_GPL(iif_fence_add_sync_point);

int iif_fence_submit_signaler_and_waiter(struct iif_fence **in_fences, int num_in_fences,
					 struct iif_fence **out_fences, int num_out_fences,
					 enum iif_ip_type waiter_ip)
{
	struct iif_fence **fences;
	enum iif_ip_type ip;
	u16 *wakelock_held;
	unsigned long *flags;
	int num_fences = num_in_fences + num_out_fences;
	int i, tmp, ret;

	might_sleep();

	/* @waiter_ip must be tested only if there are @in_fences to submit a waiter. */
	if (num_in_fences && waiter_ip >= IIF_IP_NUM)
		return -EINVAL;

	fences = kcalloc(num_fences, sizeof(*fences), GFP_KERNEL);
	if (!fences)
		return -ENOMEM;

	flags = kcalloc(num_fences, sizeof(*flags), GFP_KERNEL);
	if (!flags) {
		ret = -ENOMEM;
		goto err_free_fences;
	}

	wakelock_held = kcalloc(num_out_fences, sizeof(*wakelock_held), GFP_KERNEL);
	if (!wakelock_held) {
		ret = -ENOMEM;
		goto err_free_flags;
	}

	memcpy(fences, in_fences, num_in_fences * sizeof(*in_fences));
	memcpy(fences + num_in_fences, out_fences, num_out_fences * sizeof(*out_fences));

	ret = iif_fences_sort_by_id(fences, num_fences);
	if (ret)
		goto err_free_wakelock_held;

	/* Holds the block wakelocks of @waiter_ip for @in_fences. */
	ret = iif_fences_acquire_block_wakelock(in_fences, num_in_fences, waiter_ip);
	if (ret)
		goto err_free_wakelock_held;

	/*
	 * Holds the block wakelocks of waiters of @out_fences.
	 *
	 * If there are waiters which were set before sending signaler commands, try to hold the
	 * block wakelock of waiters. As we don't increase the number of outstanding waiters here,
	 * the held block wakelocks will be considered as locks which were pended to be released.
	 * That means they won't be released until there are no more outstanding signalers and
	 * `iif_fence_waited_work_func()` is called which releases all pended block wakelocks.
	 *
	 * We don't need to hold @out_fences[]->fence_lock here because:
	 * - Holding block wakelock usually can be done in the normal context only.
	 * - If there is a new waiter which were not informed to the IIF driver before submitting
	 *   signalers, the waiter driver will call `submit_waiter()` before submitting its command
	 *   to its IP and the block wakelock will be held there. Also, the held block wakelock
	 *   won't be released until there are no more outstanding signalers.
	 */
	ret = iif_fences_acquire_block_wakelock_of_waiters(out_fences, num_out_fences,
							   wakelock_held);
	if (ret)
		goto err_release_block_wakelock;

	iif_fences_write_lock(fences, num_fences, flags);

	/*
	 * Checks whether we can submit a waiter to @in_fences.
	 * If there are unsubmitted signalers, the caller should retry submitting waiters later.
	 */
	ret = iif_fences_are_waiter_submittable_locked(in_fences, num_in_fences);
	if (ret)
		goto err_unlock_fences;

	/*
	 * Checks whether we can submit a signaler to @out_fences.
	 * If all signalers are already submitted, submitting signalers is not allowed anymore.
	 */
	ret = iif_fences_are_signaler_submittable_locked(out_fences, num_out_fences);
	if (ret)
		goto err_unlock_fences;

	/* Submits a waiter to @in_fences. */
	iif_fences_submit_waiter_locked(in_fences, num_in_fences, waiter_ip);

	/* Submits a signaler to @out_fences. */
	iif_fences_submit_signaler_locked(out_fences, num_out_fences);

	/* Increases the number of outstanding block wakelock held for @in_fences above. */
	for (i = 0; i < num_in_fences; i++)
		in_fences[i]->outstanding_block_wakelock[waiter_ip]++;

	/* Increases the number of outstanding block wakelock held for @out_fences above. */
	for (i = 0; i < num_out_fences; i++) {
		for_each_ip(wakelock_held[i], ip, tmp)
			out_fences[i]->outstanding_block_wakelock[ip]++;
	}

err_unlock_fences:
	iif_fences_write_unlock(fences, num_fences, flags);

	if (ret)
		iif_fences_release_block_wakelock_of_waiters(out_fences, num_out_fences,
							     wakelock_held);
err_release_block_wakelock:
	if (ret)
		iif_fences_release_block_wakelock(in_fences, num_in_fences, waiter_ip);
err_free_wakelock_held:
	kfree(wakelock_held);
err_free_flags:
	kfree(flags);
err_free_fences:
	kfree(fences);

	return ret;
}
EXPORT_SYMBOL_GPL(iif_fence_submit_signaler_and_waiter);

void iif_fence_signaler_completed(struct iif_fence *fence)
{
	/*
	 * For the backward compatibility, if the fence is a direct single-shot IIF, ignore this
	 * function call. Removing signaler will be done when `iif_fence_signal{_*}()` functions
	 * are called.
	 */
	if (iif_fence_is_direct(fence) && fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		return;

	iif_fence_do_waited_work_if_no_outstanding_signalers(fence, iif_fence_waited_work);
}
EXPORT_SYMBOL_GPL(iif_fence_signaler_completed);

void iif_fence_signaler_completed_async(struct iif_fence *fence)
{
	/*
	 * For the backward compatibility, if the fence is a direct single-shot IIF, ignore this
	 * function call. Removing signaler will be done when `iif_fence_signal{_*}()` functions
	 * are called.
	 */
	if (iif_fence_is_direct(fence) && fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		return;

	iif_fence_do_waited_work_if_no_outstanding_signalers(fence, iif_fence_waited_work_async);
}
EXPORT_SYMBOL_GPL(iif_fence_signaler_completed_async);

void iif_fence_waiter_completed(struct iif_fence *fence, enum iif_ip_type waiter_ip)
{
	unsigned long flags;
	bool release_block_wakelock = true;

	write_lock_irqsave(&fence->fence_lock, flags);

	iif_fence_remove_waiter_locked(fence, waiter_ip);

	if (!iif_fence_outstanding_signalers_locked(fence) &&
	    fence->outstanding_block_wakelock[waiter_ip])
		fence->outstanding_block_wakelock[waiter_ip]--;
	else
		release_block_wakelock = false;

	write_unlock_irqrestore(&fence->fence_lock, flags);

	if (release_block_wakelock)
		iif_fence_release_block_wakelock(fence, waiter_ip, 1);
}
EXPORT_SYMBOL_GPL(iif_fence_waiter_completed);

void iif_fence_waiter_completed_async(struct iif_fence *fence, enum iif_ip_type waiter_ip)
{
	iif_fence_remove_waiter(fence, waiter_ip);
	iif_fence_waited_work_async(fence);
}
EXPORT_SYMBOL_GPL(iif_fence_waiter_completed_async);

int iif_fence_signal(struct iif_fence *fence)
{
	return iif_fence_signal_with_status(fence, 0);
}
EXPORT_SYMBOL_GPL(iif_fence_signal);

int iif_fence_signal_async(struct iif_fence *fence)
{
	return iif_fence_signal_with_status_async(fence, 0);
}
EXPORT_SYMBOL_GPL(iif_fence_signal_async);

int iif_fence_signal_with_status(struct iif_fence *fence, int error)
{
	int ret;

	ret = iif_fence_signal_and_update(fence, error, iif_fence_waited_work);
	if (!ret)
		flush_work(&fence->signaled_work);

	return ret;
}
EXPORT_SYMBOL_GPL(iif_fence_signal_with_status);

int iif_fence_signal_with_status_async(struct iif_fence *fence, int error)
{
	return iif_fence_signal_and_update(fence, error, iif_fence_waited_work_async);
}
EXPORT_SYMBOL_GPL(iif_fence_signal_with_status_async);

int iif_fence_get_signal_status(struct iif_fence *fence)
{
	unsigned long flags;
	int status = 0;

	read_lock_irqsave(&fence->fence_lock, flags);

	if (fence->signaled)
		status = fence->signal_error ? fence->signal_error : 1;

	read_unlock_irqrestore(&fence->fence_lock, flags);

	return status;
}
EXPORT_SYMBOL_GPL(iif_fence_get_signal_status);

void iif_fence_get_status(struct iif_fence *fence, struct iif_fence_status *status)
{
	unsigned long flags;

	read_lock_irqsave(&fence->fence_lock, flags);

	if (fence->params.fence_type == IIF_FENCE_TYPE_SINGLE_SHOT)
		status->signaled = fence->signaled;
	else
		status->timeline = fence->timeline;
	status->error = fence->signal_error;

	read_unlock_irqrestore(&fence->fence_lock, flags);
}
EXPORT_SYMBOL_GPL(iif_fence_get_status);

void iif_fence_set_propagate_unblock(struct iif_fence *fence)
{
	iif_fence_delegate_to_ap(fence);
}
EXPORT_SYMBOL_GPL(iif_fence_set_propagate_unblock);

int iif_fence_delegate_to_ap(struct iif_fence *fence)
{
	int ret;

	ret = iif_fence_ops_delegate_to_ap(fence);
	if (ret)
		return ret;

	/*
	 * It is safe to not hold any locks because this function is expected to be called before
	 * signaling @fence and @fence->propagate will be accessed only when the fence has been
	 * unblocked and the poll callbacks are executed. The value won't be changed while the
	 * callbacks are being processed.
	 */
	fence->propagate = true;

	return 0;
}
EXPORT_SYMBOL_GPL(iif_fence_delegate_to_ap);

bool iif_fence_is_signaled(struct iif_fence *fence)
{
	/* TODO(b/379110867): Consider reusable fence. */
	return iif_fence_get_signal_status(fence);
}
EXPORT_SYMBOL_GPL(iif_fence_is_signaled);

signed long iif_fence_wait_timeout(struct iif_fence *fence, bool intr, signed long timeout_jiffies)
{
	struct iif_fence_status status = {
		.timeline = fence->timeline,
		.error = 0,
	};

	return iif_fence_wait_timeout_with_status(fence, intr, timeout_jiffies, &status);
}
EXPORT_SYMBOL_GPL(iif_fence_wait_timeout);

signed long iif_fence_wait_timeout_with_status(struct iif_fence *fence, bool intr,
					       signed long timeout_jiffies,
					       struct iif_fence_status *status)
{
	struct iif_fence_wait_cb wait_cb = {
		.base = {
			.node = LIST_HEAD_INIT(wait_cb.base.node),
			.status = *status,
		},
		.task = current,
		.status = status,
		.flags = 0,
	};
	signed long ret = timeout_jiffies ? timeout_jiffies : 1;

	/*
	 * If @fence is already unblocked or failed in registering the callback, exit the function
	 * directly.
	 */
	if (iif_fence_ops_add_poll_cb(fence, &wait_cb.base, iif_fence_wait_poll) < 0) {
		*status = wait_cb.base.status;
		return ret;
	}

	/* If the thread is already interrupted, return an error right away. */
	if (intr && signal_pending(current)) {
		ret = -ERESTARTSYS;
		goto out;
	}

	/* If the timeout is 0, but the fence is not signaled yet, return 0 which means timeout. */
	if (!timeout_jiffies) {
		ret = 0;
		goto out;
	}

	/* Wait until @fence to be unblocked. */
	while (!test_bit(IIF_FENCE_WAIT_CB_SIGNALED, &wait_cb.flags) && ret > 0) {
		/* Sets interruptible status of the current thread before sleep. */
		if (intr)
			__set_current_state(TASK_INTERRUPTIBLE);
		else
			__set_current_state(TASK_UNINTERRUPTIBLE);

		/* Sleeps until interrupt, timeout or `iif_fence_wait_poll()` is invoked. */
		ret = schedule_timeout(ret);

		/* If timeout hasn't elapsed yet, but the thread is interrupted, return an error. */
		if (ret > 0 && intr && signal_pending(current))
			ret = -ERESTARTSYS;
	}
out:
	iif_fence_ops_remove_poll_cb(fence, &wait_cb.base);

	return ret;
}
EXPORT_SYMBOL_GPL(iif_fence_wait_timeout_with_status);

void iif_fence_waited(struct iif_fence *fence, enum iif_ip_type ip)
{
	iif_fence_waiter_completed(fence, ip);
}
EXPORT_SYMBOL_GPL(iif_fence_waited);

void iif_fence_waited_async(struct iif_fence *fence, enum iif_ip_type ip)
{
	iif_fence_waiter_completed_async(fence, ip);
}
EXPORT_SYMBOL_GPL(iif_fence_waited_async);

int iif_fence_add_poll_callback(struct iif_fence *fence, struct iif_fence_poll_cb *poll_cb,
				iif_fence_poll_cb_t func)
{
	unsigned long flags;
	int ret;

	write_lock_irqsave(&fence->fence_lock, flags);

	if (iif_fence_has_retired_locked(fence)) {
		INIT_LIST_HEAD(&poll_cb->node);
		ret = -EPERM;
		goto out;
	}

	ret = iif_fence_ops_add_poll_cb(fence, poll_cb, func);
out:
	write_unlock_irqrestore(&fence->fence_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(iif_fence_add_poll_callback);

bool iif_fence_remove_poll_callback(struct iif_fence *fence, struct iif_fence_poll_cb *poll_cb)
{
	unsigned long flags;
	bool removed;

	write_lock_irqsave(&fence->fence_lock, flags);
	removed = iif_fence_ops_remove_poll_cb(fence, poll_cb);
	write_unlock_irqrestore(&fence->fence_lock, flags);

	return removed;
}
EXPORT_SYMBOL_GPL(iif_fence_remove_poll_callback);

int iif_fence_add_all_signaler_submitted_callback(struct iif_fence *fence,
						  struct iif_fence_all_signaler_submitted_cb *cb,
						  iif_fence_all_signaler_submitted_cb_t func)
{
	int ret = 0;
	unsigned long flags;

	write_lock_irqsave(&fence->fence_lock, flags);

	cb->remaining_signalers = iif_fence_unsubmitted_signalers_locked(fence);

	/* Already all signalers are submitted. */
	if (!cb->remaining_signalers) {
		ret = -EPERM;
		goto out;
	}

	cb->func = func;
	list_add_tail(&cb->node, &fence->all_signaler_submitted_cb_list);
out:
	write_unlock_irqrestore(&fence->fence_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(iif_fence_add_all_signaler_submitted_callback);

bool iif_fence_remove_all_signaler_submitted_callback(
	struct iif_fence *fence, struct iif_fence_all_signaler_submitted_cb *cb)
{
	bool removed = false;
	unsigned long flags;

	write_lock_irqsave(&fence->fence_lock, flags);

	if (!list_empty(&cb->node)) {
		list_del_init(&cb->node);
		removed = true;
	}

	write_unlock_irqrestore(&fence->fence_lock, flags);

	return removed;
}
EXPORT_SYMBOL_GPL(iif_fence_remove_all_signaler_submitted_callback);

int iif_fence_unsubmitted_signalers(struct iif_fence *fence)
{
	unsigned long flags;
	int unsubmitted;

	read_lock_irqsave(&fence->fence_lock, flags);
	unsubmitted = iif_fence_unsubmitted_signalers_locked(fence);
	read_unlock_irqrestore(&fence->fence_lock, flags);

	return unsubmitted;
}
EXPORT_SYMBOL_GPL(iif_fence_unsubmitted_signalers);

int iif_fence_submitted_signalers(struct iif_fence *fence)
{
	return fence->params.remaining_signalers - iif_fence_unsubmitted_signalers(fence);
}
EXPORT_SYMBOL_GPL(iif_fence_submitted_signalers);

int iif_fence_signaled_signalers(struct iif_fence *fence)
{
	unsigned long flags;
	int signaled;

	read_lock_irqsave(&fence->fence_lock, flags);
	signaled = fence->signaled_signalers;
	read_unlock_irqrestore(&fence->fence_lock, flags);

	return signaled;
}
EXPORT_SYMBOL_GPL(iif_fence_signaled_signalers);

int iif_fence_outstanding_waiters(struct iif_fence *fence)
{
	unsigned long flags;
	int outstanding;

	read_lock_irqsave(&fence->fence_lock, flags);
	outstanding = fence->outstanding_waiters;
	read_unlock_irqrestore(&fence->fence_lock, flags);

	return outstanding;
}
EXPORT_SYMBOL_GPL(iif_fence_outstanding_waiters);

void iif_fence_unblocked(struct iif_fence *fence)
{
	iif_fence_ops_fence_unblocked(fence);
}
EXPORT_SYMBOL_GPL(iif_fence_unblocked);
