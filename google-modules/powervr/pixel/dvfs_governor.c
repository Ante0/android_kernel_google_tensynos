// SPDX-License-Identifier: GPL-2.0

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/devfreq.h>
#include <linux/math64.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include <governor.h>

#include "dvfs_governor.h"

/* Default constants for DevFreq-Precise-Ondemand (DFPO) */
#define DFPO_UPTHRESHOLD	(90)
#define DFPO_DOWNDIFFERENCTIAL	(5)
#define MSEC_TO_KTIME(x) (ns_to_ktime(((u64)(x)) * 1000000U))

/*
 * DOC: Timer state machine
 *
 * Possible state transitions
 * ON        -> ON | OFF | STOPPED
 * STOPPED   -> ON | OFF
 * OFF       -> ON
 *
 *
 * ┌─e─┐┌────────────f───────────┐
 * │   v│                        v
 * └───ON ──a──> STOPPED ──b──> OFF
 *     ^^            │           │
 *     │└──────c─────┘           │
 *     │                         │
 *     └─────────────d───────────┘
 *
 * Transition effects:
 * a. None
 * b. Timer expires without restart
 * c. Timer is not stopped, timer period is unaffected
 * d. Timer must be restarted
 * e. Callback is executed and the timer is restarted
 * f. Timer is cancelled, or the callback is waited on if currently executing. This is called during
 *    tear-down and should not be subject to a race from an OFF->ON transition
 */
enum dvfs_timer_state { TIMER_OFF, TIMER_STOPPED, TIMER_ON };

/**
 * struct governor_data - Precise on-demand governor state.
 */
struct governor_data {
	/** @timer: htimer for periodic dvfs evaluation */
	struct hrtimer timer;
	/** @timer_state: See timer state machine doc */
	atomic_t timer_state;
	/** @work: dvfs work item queued from @timer */
	struct work_struct work;
	/** @devfreq: handle to devfreq instance using the governor */
	struct devfreq *devfreq;
	/** @lock: lock used to synchronize state transitions with a polling interval change */
	struct mutex lock;
	/** @last_interval: timestamp of the most recent work_func  */
	ktime_t last_interval;
};

static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
	struct governor_data *data;
	unsigned int expiry;

	if (WARN_ON(!timer))
		return HRTIMER_NORESTART;

	data = container_of(timer, struct governor_data, timer);

	/* Transition (b) to fully off if timer was stopped, don't restart the timer in this case */
	if (atomic_cmpxchg(&data->timer_state, TIMER_STOPPED, TIMER_OFF) != TIMER_ON)
		return HRTIMER_NORESTART;

	/* Queue up the opp change */
	queue_work(system_highpri_wq, &data->work);

	/* Released by precise_ondemand_update_interval */
	expiry = smp_load_acquire(&data->devfreq->profile->polling_ms);

	/* Set the new expiration time and restart (transition e) */
	hrtimer_forward_now(timer, MSEC_TO_KTIME(expiry));
	return HRTIMER_RESTART;
};

static void work_func(struct work_struct *work)
{
	struct governor_data *data = container_of(work, struct governor_data, work);
	struct devfreq *df = data->devfreq;
	const ktime_t now = ktime_get();

	mutex_lock(&df->lock);
	data->last_interval = ktime_to_ms(now);
	update_devfreq(df);
	mutex_unlock(&df->lock);
}

static int governor_data_init(struct devfreq *df)
{
	struct governor_data *data = kzalloc(sizeof(struct governor_data), GFP_KERNEL);

	if (!data)
		return -ENOMEM;

	atomic_set(&data->timer_state, TIMER_OFF);
	INIT_WORK(&data->work, work_func);
	hrtimer_init(&data->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	mutex_init(&data->lock);
	data->timer.function = timer_callback;
	data->devfreq = df;

	df->governor_data = data;

	return 0;
}

static void governor_data_term(struct devfreq *df)
{
	struct governor_data *data = df->governor_data;

	atomic_set(&data->timer_state, TIMER_OFF);
	hrtimer_cancel(&data->timer);
	cancel_work_sync(&data->work);
	kfree(data);
}

static int precise_ondemand_func(struct devfreq *df, unsigned long *freq)
{
	int err;
	struct devfreq_dev_status *stat;
	unsigned long long a, b;
	unsigned int dfpo_upthreshold = DFPO_UPTHRESHOLD;
	unsigned int dfpo_downdifferential = DFPO_DOWNDIFFERENCTIAL;
	struct devfreq_simple_ondemand_data *data = df->data;

	err = devfreq_update_stats(df);
	if (err)
		return err;

	stat = &df->last_status;

	if (data) {
		if (data->upthreshold)
			dfpo_upthreshold = data->upthreshold;
		if (data->downdifferential)
			dfpo_downdifferential = data->downdifferential;
	}
	if (dfpo_upthreshold > 100 ||
	    dfpo_upthreshold < dfpo_downdifferential)
		return -EINVAL;

	/* Assume MAX if it is going to be divided by zero */
	if (stat->total_time == 0) {
		*freq = DEVFREQ_MAX_FREQ;
		return 0;
	}

	/* Prevent overflow */
	if (stat->busy_time >= (1 << 24) || stat->total_time >= (1 << 24)) {
		stat->busy_time >>= 7;
		stat->total_time >>= 7;
	}

	/* Set MAX if it's busy enough */
	if (stat->busy_time * 100 >
	    stat->total_time * dfpo_upthreshold) {
		*freq = DEVFREQ_MAX_FREQ;
		return 0;
	}

	/* Set MAX if we do not know the initial frequency */
	if (stat->current_frequency == 0) {
		*freq = DEVFREQ_MAX_FREQ;
		return 0;
	}

	/* Keep the current frequency */
	if (stat->busy_time * 100 >
	    stat->total_time * (dfpo_upthreshold - dfpo_downdifferential)) {
		*freq = stat->current_frequency;
		return 0;
	}

	/* Set the desired frequency based on the load */
	a = stat->busy_time;
	a *= stat->current_frequency;
	b = div_u64(a, stat->total_time);
	b *= 100;
	b = div_u64(b, (dfpo_upthreshold - dfpo_downdifferential / 2));
	*freq = (unsigned long) b;

	return 0;
}

static int precise_ondemand_resume(struct devfreq *df)
{
	struct governor_data *data = df->governor_data;

	mutex_lock(&data->lock);

	/* Transition to ON, from a stopped state (transition c) */
	if (atomic_xchg(&data->timer_state, TIMER_ON) == TIMER_OFF) {
		/* Find out when the next interval should be */
		s64 expiry = 0;
		const s64 now = ktime_to_ms(ktime_get());
		const s64 delta = now - data->last_interval;

		if (delta < data->devfreq->profile->polling_ms)
			expiry = data->devfreq->profile->polling_ms - delta;

		/* Start the timer only if it's been fully stopped (transition d), and
		 * schedule an update (expiry).
		 */
		hrtimer_start(&data->timer,
			      ms_to_ktime(expiry),
			      HRTIMER_MODE_REL);
	}

	mutex_unlock(&data->lock);

	return 0;
}

static int precise_ondemand_suspend(struct devfreq *df)
{
	struct governor_data *data = df->governor_data;

	mutex_lock(&data->lock);

	/* Timer is Stopped if its currently on (transition a) */
	if (atomic_cmpxchg(&data->timer_state, TIMER_ON, TIMER_STOPPED) == TIMER_ON) {
		/* Try canceling the active timer and run transition b to OFF.
		 *
		 * If the timer callback is concurrently executing, expect the callback to
		 * run transition b.
		 */
		if (hrtimer_try_to_cancel(&data->timer) >= 0)
			atomic_set(&data->timer_state, TIMER_OFF);
	}

	mutex_unlock(&data->lock);

	return 0;
}

static int precise_ondemand_start(struct devfreq *df)
{
	int err = 0;

	err = governor_data_init(df);
	if (err)
		goto exit_init;

	err = precise_ondemand_resume(df);
	if (err)
		goto exit_resume;

	return 0;

exit_resume:
	governor_data_term(df);
exit_init:
	return err;
}

static int precise_ondemand_stop(struct devfreq *df)
{
	precise_ondemand_suspend(df);
	governor_data_term(df);
	return 0;
}

static int precise_ondemand_update_interval(struct devfreq *df, unsigned int delay)
{
	struct governor_data *data = df->governor_data;
	ktime_t delta, expiry;

	mutex_lock(&data->lock);

	if (atomic_read(&data->timer_state) != TIMER_ON) {
		/* The timer is not running, just update the polling interval. */
		smp_store_release(&df->profile->polling_ms, delay);
		goto out_unlock;
	}

	/* The order here matters, first calculate the new expiry based on the
	 * current expiry time. We haven't yet updated the polling_ms var, meaning
	 * the current expiry time is still guaranteed to be based on the old
	 * polling interval, which makes our adjustment calculation accurate. We
	 * then update the polling interval so that a racing timer expiry will
	 * pick up the new value, and restart itself with the correct interval.
	 * Subsequently we cancel the timer and restart it with the adjusted expiry,
	 * but thanks to this ordering the result will still use the correct expiry
	 * timer.
	 *
	 * Consider the reverse case where we set the polling interval first. It's
	 * possible that a racing timer could restart itself with the new interval,
	 * which we then read here using hrtimer_get_expires, and subtract the new
	 * interval from it! This is guaranteed to result in an adjusted expiry of 0
	 * and immediately schedule the timer again which is not desired.
	 */

	delta = MSEC_TO_KTIME((s64)df->profile->polling_ms - delay);
	expiry = hrtimer_get_expires(&data->timer) - delta;
	/* Use release semantics to enforce ordering - acquired by timer callback */
	smp_store_release(&df->profile->polling_ms, delay);

	/* Cancel or block until the timer is inactive. From this point it cannot be
	 * restarted as we hold data->lock.
	 * This is a no op if the timer is already inactive.
	 */
	hrtimer_cancel(&data->timer);

	hrtimer_start(&data->timer, expiry, HRTIMER_MODE_ABS);

out_unlock:
	mutex_unlock(&data->lock);

	return 0;
}

static int precise_ondemand_handler(struct devfreq *df, unsigned int event, void *data)
{
	switch (event) {
	case DEVFREQ_GOV_START:
		return precise_ondemand_start(df);
	case DEVFREQ_GOV_STOP:
		return precise_ondemand_stop(df);
	case DEVFREQ_GOV_UPDATE_INTERVAL:
		return precise_ondemand_update_interval(df, *(unsigned int *)data);
	case DEVFREQ_GOV_SUSPEND:
		return precise_ondemand_suspend(df);
	case DEVFREQ_GOV_RESUME:
		return precise_ondemand_resume(df);
	default:
		return 0;
	}
}

static struct devfreq_governor precise_ondemand = {
	.name = "precise_ondemand",
	.attrs = DEVFREQ_GOV_ATTR_POLLING_INTERVAL | DEVFREQ_GOV_ATTR_TIMER,
	.get_target_freq = precise_ondemand_func,
	.event_handler = precise_ondemand_handler,
};

int init_dvfs_gov(struct pixel_gpu_device *pixel_dev)
{
	return devfreq_add_governor(&precise_ondemand);
}

void deinit_dvfs_gov(struct pixel_gpu_device *pixel_dev)
{
	int const ret = devfreq_remove_governor(&precise_ondemand);

	if (ret)
		dev_err(pixel_dev->dev, "%s: failed remove governor %d", __func__, ret);
}
