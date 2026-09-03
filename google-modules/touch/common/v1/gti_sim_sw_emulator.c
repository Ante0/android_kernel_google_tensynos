// SPDX-License-Identifier: GPL-2.0
/*
 * GTI Simulation for Pixel.
 *
 * Copyright 2025 Google LLC.
 */

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/version.h>

#include "uapi/input/touch_offload.h"
#include "gti_sim.h"

#undef pr_fmt
#define pr_fmt(fmt) "touch_sim: " fmt

enum software_emulator_state {
	SW_EMULATOR_NORMAL,
	SW_EMULATOR_ERROR,
};

struct software_emulator {
	struct emulator base;
	ktime_t first_frame_timestamp;
	ktime_t previous_frame_timestamp;
	ktime_t start_timestamp;
	struct completion timer_completion;
	struct hrtimer sleep_timer;
	enum software_emulator_state state;
	struct task_struct *thread;
};

#define to_sw_emulator(emulator) \
	container_of(emulator, struct software_emulator, base)

static enum hrtimer_restart
software_emulator_timer_callback(struct hrtimer *timer)
{
	struct software_emulator *sw_emul =
		container_of(timer, struct software_emulator, sleep_timer);

	complete(&sw_emul->timer_completion);
	return HRTIMER_NORESTART;
}

static int
software_emulator_interruptible_usleep(struct software_emulator *sw_emul,
				       ktime_t ktime)
{
	reinit_completion(&sw_emul->timer_completion);

	hrtimer_start(&sw_emul->sleep_timer, ktime, HRTIMER_MODE_REL);

	if (wait_for_completion_interruptible(&sw_emul->timer_completion)) {
		// Interrupted by a signal
		hrtimer_cancel(&sw_emul->sleep_timer);
		return -ERESTARTSYS;
	}

	return 0;
}

static int software_emulator_process_frames(struct software_emulator *sw_emul)
{
	struct touch_sim *sim = sw_emul->base.parent;
	struct TouchOffloadFrameHeader *header = sim->temp_frame_header;
	int ret = 0;

	pr_debug("frame_size: %u, index: %llu, timestamp: %llu",
		 header->frame_size, header->index, header->timestamp);

	ktime_t frame_time = ns_to_ktime(header->timestamp);

	// first frame
	if (ktime_to_ns(sw_emul->first_frame_timestamp) == 0) {
		sw_emul->first_frame_timestamp = frame_time;
		sw_emul->start_timestamp = ktime_get();
	}

	ktime_t target_time = ktime_add(
		sw_emul->start_timestamp,
		ktime_sub(frame_time, sw_emul->first_frame_timestamp));

	if (ktime_to_ns(sw_emul->previous_frame_timestamp) >=
	    ktime_to_ns(target_time)) {
		pr_err("Error: timestamp with wrong order , index: %llu, timestamp: %llu",
		       header->index, header->timestamp);
		return -EIO;
	}
	sw_emul->previous_frame_timestamp = target_time;

	ktime_t now = ktime_get();
	ktime_t sleep_time = ktime_sub(target_time, now);

	if (ktime_to_ns(sleep_time) > 0) {
		//pr_debug("sleep %lld ns", ktime_to_ns(sleep_time));
		ret = software_emulator_interruptible_usleep(sw_emul,
							     sleep_time);
		if (ret) {
			pr_info("Close interrupt event during frame sleep %llu",
				ktime_to_ns(sleep_time));
			return ret;
		}
	}

	ret = touch_sim_on_frame_processed(sim, target_time);
	if (ret)
		pr_warn("touch_sim_on_frame_processed error, ret %d", ret);

	return ret;
}

static int software_emulator_thread_func(void *sw_emul_self)
{
	struct software_emulator *sw_emul = sw_emul_self;
	struct touch_sim *sim = sw_emul->base.parent;

	int ret;

	while (!kthread_should_stop()) {
		if (sw_emul->state == SW_EMULATOR_ERROR) {
			touch_sim_clean_fifo(sim);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			continue;
		}

		ret = touch_sim_peak_and_load_frame(sim);
		if (ret == -EBUSY) {
			software_emulator_interruptible_usleep(
				sw_emul, ms_to_ktime(100));
			continue;
		} else if (ret) {
			pr_warn("touch_sim_peak_and_load_frame error, ret %u",
				ret);
			sw_emul->state = SW_EMULATOR_ERROR;
			continue;
		}

		ret = software_emulator_process_frames(sw_emul);
		if (ret) {
			sw_emul->state = SW_EMULATOR_ERROR;
			continue;
		}
	}

	return 0;
}

static int software_emulator_start(struct emulator *emulator)
{
	struct software_emulator *sw_emul = to_sw_emulator(emulator);

	sw_emul->first_frame_timestamp = ktime_set(0, 0);
	sw_emul->previous_frame_timestamp = ktime_set(0, 0);
	sw_emul->start_timestamp = ktime_set(0, 0);

#if KERNEL_VERSION(6, 13, 0) <= LINUX_VERSION_CODE
	hrtimer_setup(&sw_emul->sleep_timer, software_emulator_timer_callback, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL);
#else
	hrtimer_init(&sw_emul->sleep_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sw_emul->sleep_timer.function = &software_emulator_timer_callback;
#endif

	init_completion(&sw_emul->timer_completion);

	sw_emul->state = SW_EMULATOR_NORMAL;
	sw_emul->thread = kthread_run(software_emulator_thread_func, sw_emul,
				      "touch_sim_thread");
	if (IS_ERR_OR_NULL(sw_emul->thread)) {
		pr_err("Failed to create kthread\n");
		return PTR_ERR(sw_emul->thread);
	}

	sched_set_fifo(sw_emul->thread);

	pr_info("software_emulator start");
	return 0;
};

static void software_emulator_stop(struct emulator *emulator)
{
	struct software_emulator *sw_emul = to_sw_emulator(emulator);

	hrtimer_cancel(&sw_emul->sleep_timer);
	complete(&sw_emul->timer_completion);
	if (!IS_ERR_OR_NULL(sw_emul->thread)) {
		kthread_stop(sw_emul->thread);
		sw_emul->thread = NULL;
	}
	pr_info("software_emulator stop");
};

static bool software_emulator_is_ready(struct emulator *emulator)
{
	struct software_emulator *sw_emul = to_sw_emulator(emulator);

	return !IS_ERR_OR_NULL(sw_emul->thread) &&
	       sw_emul->state == SW_EMULATOR_NORMAL;
};

static bool software_emulator_has_error(struct emulator *emulator)
{
	struct software_emulator *sw_emul = to_sw_emulator(emulator);

	return sw_emul->state == SW_EMULATOR_ERROR;
}

static const struct emulator_ops software_emulator_ops = {
	.start = software_emulator_start,
	.stop = software_emulator_stop,
	.is_ready = software_emulator_is_ready,
	.has_error = software_emulator_has_error,
};

struct software_emulator sw_emulator = {
	.base = { .ops = &software_emulator_ops },
};
