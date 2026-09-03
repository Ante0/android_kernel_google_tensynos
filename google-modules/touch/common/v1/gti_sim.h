/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GTI Simulation for Pixel.
 *
 * Copyright 2025 Google LLC.
 */

#ifndef _GOOG_TOUCH_INTERFACE_SIM_H_
#define _GOOG_TOUCH_INTERFACE_SIM_H_

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/kfifo.h>

#define DEFAULT_TEMP_PAGE_ORDER (1)

struct TouchOffloadFrameHeader;
struct emulator;
struct touch_sim;

struct emulator_ops {
	int (*start)(struct emulator *emulator);
	void (*stop)(struct emulator *emulator);
	bool (*is_ready)(struct emulator *emulator);
	bool (*has_error)(struct emulator *emulator);
};

struct emulator {
	const struct emulator_ops *ops;
	struct touch_sim *parent;
};

struct touch_sim {
	struct cdev cdev;
	struct device *dev;
	dev_t devt;
	atomic_t device_is_locked;
	struct kfifo fifo;
	wait_queue_head_t event_wait_queue;
	unsigned long temp_page;
	struct TouchOffloadFrameHeader *temp_frame_header;
	u8 *temp_frame;
	u32 frame_size;
	atomic_t reported_frame_count;
	s64 total_diff_ns;
	s64 max_diff_ns;
	s64 min_diff_ns;
	struct emulator *emulator;
	int (*pop_data_cb)(void *private_data, char *buf, size_t count,
			   ktime_t timestamp);
	void *private_data;
};

extern const struct file_operations touch_sim_fops;
extern struct software_emulator sw_emulator;

void touch_sim_stop(struct touch_sim *sim);
int touch_sim_on_frame_processed(struct touch_sim *sim, ktime_t timestamp);
int touch_sim_peak_and_load_frame(struct touch_sim *sim);
void touch_sim_clean_fifo(struct touch_sim *sim);

#endif // _GOOG_TOUCH_INTERFACE_SIM_H_
