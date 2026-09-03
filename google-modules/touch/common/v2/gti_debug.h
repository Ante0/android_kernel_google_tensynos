/* SPDX-License-Identifier: GPL */
/*
 * GTI Debug for Pixel.
 *
 * Copyright 2025 Google LLC.
 */

#ifndef __GTI_DEBUG_H__
#define __GTI_DEBUG_H__

#include "uapi/input/touch_offload.h"

/* Forward declaration */
struct goog_touch_interface;

/*
 * GTI_DEBUG_HEALTHCHECK_KFIFO_LEN
 * Define the array length of struct gti_debug_healthcheck to track recent
 * touch interrupts information for debug.
 */
#define GTI_DEBUG_HEALTHCHECK_KFIFO_LEN 32 /* must be power of 2. */
#define GTI_DEBUG_HEALTHCHECK_LOGS_LEN 4
/*
 * GTI_DEBUG_INPUT_KFIFO_LEN
 * Define the array length of struct gti_debug_input to track recent
 * touch input report for debug.
 */
#define GTI_DEBUG_INPUT_KFIFO_LEN 16 /* must be power of 2. */
#define GTI_DEBUG_INPUT_LOGS_LEN 4

/*
 * GTI_DEBUG_OFFLOAD_TOGGLE_KFIFO_LEN
 * Define the array length of struct gti_debug_offload_toggle to track
 * offload toggling for debug.
 */
#define GTI_DEBUG_OFFLOAD_TOGGLE_KFIFO_LEN 16 /* must be power of 2. */
#define GTI_DEBUG_OFFLOAD_TOGGLE_LOGS_LEN 1

struct gti_debug_coord {
	ktime_t time;
	u64 irq_index;
	struct TouchOffloadCoord coord;
};

struct gti_debug_healthcheck {
	ktime_t irq_time;
	u64 irq_index;
	u64 input_index;
	unsigned long slot_bit_active;
};

struct gti_debug_input {
	int slot;
	struct gti_debug_coord pressed;
	struct gti_debug_coord released;
};

struct gti_debug_offload_toggle {
	bool running;
	u64 frame_index;
	u64 irq_index;
	u64 input_index;
	ktime_t time;
};

void gti_debug_healthcheck_push(struct goog_touch_interface *gti);
void gti_debug_healthcheck_update(struct goog_touch_interface *gti, bool from_top_half);
void gti_debug_healthcheck_dump(struct goog_touch_interface *gti);
void gti_debug_input_push(struct goog_touch_interface *gti, int slot);
void gti_debug_input_update(struct goog_touch_interface *gti);
void gti_debug_input_dump(struct goog_touch_interface *gti);
void gti_debug_offload_toggle_push(struct goog_touch_interface *gti);
void gti_debug_offload_toggle_dump(struct goog_touch_interface *gti);

#endif /* __GTI_DEBUG_H__ */
