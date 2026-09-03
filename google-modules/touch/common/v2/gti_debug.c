// SPDX-License-Identifier: GPL
/*
 * GTI Debug for Pixel.
 *
 * Copyright 2025 Google LLC.
 */

#include "gti_internal.h"

void gti_debug_healthcheck_push(struct goog_touch_interface *gti)
{
	/*
	 * Use kfifo as circular buffer by skipping one element
	 * when fifo is full.
	 */
	if (kfifo_is_full(&gti->debug_fifo_healthcheck))
		kfifo_skip(&gti->debug_fifo_healthcheck);
	kfifo_in(&gti->debug_fifo_healthcheck, &gti->debug_healthcheck, 1);
}

void gti_debug_healthcheck_update(struct goog_touch_interface *gti, bool from_top_half)
{
	if (from_top_half) {
		gti->debug_healthcheck.irq_time = ktime_get_real();
		gti->debug_healthcheck.irq_index = gti->irq_index;
	} else {
		gti->debug_healthcheck.input_index = gti->input_index;
		gti->debug_healthcheck.slot_bit_active = gti->slot_bit_active;
		gti_debug_healthcheck_push(gti);
	}
}

void gti_debug_healthcheck_dump(struct goog_touch_interface *gti)
{
	s16 i, count;
	s64 delta;
	s64 sec_delta;
	u32 ms_delta;
	ktime_t current_time = ktime_get_real();
	struct gti_debug_healthcheck *last_fifo = gti->debug_healthcheck_history;

	/*
	 * Use peek to keep data without pop-out to support different timing
	 * print-out by each caller.
	 */
	count = kfifo_out_peek(&gti->debug_fifo_healthcheck, last_fifo,
			       GTI_DEBUG_HEALTHCHECK_KFIFO_LEN);

	i = max_t(s16, 0, count - GTI_DEBUG_HEALTHCHECK_LOGS_LEN);
	for (; i < count; i++) {
		sec_delta = -1;
		ms_delta = 0;
		/*
		 * Calculate the delta time between irq triggered and current time.
		 */
		delta = ktime_ms_delta(current_time, last_fifo[i].irq_time);
		if (delta > 0)
			sec_delta = div_u64_rem(delta, MSEC_PER_SEC, &ms_delta);
		GOOG_INFO(gti, "dump-int: #%llu(%lld.%u): C#%llu(0x%lx).\n", last_fifo[i].irq_index,
			  sec_delta, ms_delta, last_fifo[i].input_index,
			  last_fifo[i].slot_bit_active);
	}
}

void gti_debug_input_push(struct goog_touch_interface *gti, int slot)
{
	struct gti_debug_input fifo;

	if (slot < 0 || slot >= MAX_SLOTS) {
		GOOG_ERR(gti, "Invalid slot: %d\n", slot);
		return;
	}

	/*
	 * Use kfifo as circular buffer by skipping one element
	 * when fifo is full.
	 */
	if (kfifo_is_full(&gti->debug_fifo_input))
		kfifo_skip(&gti->debug_fifo_input);

	memcpy(&fifo, &gti->debug_input[slot], sizeof(struct gti_debug_input));
	kfifo_in(&gti->debug_fifo_input, &fifo, 1);
}

void gti_debug_input_update(struct goog_touch_interface *gti)
{
	int slot;
	u64 irq_index = gti->irq_index;
	ktime_t time = ktime_get_real();

	for_each_set_bit(slot, &gti->slot_bit_changed, MAX_SLOTS) {
		if (test_bit(slot, &gti->slot_bit_active)) {
			gti->debug_input[slot].pressed.time = time;
			gti->debug_input[slot].pressed.irq_index = irq_index;
			memcpy(&gti->debug_input[slot].pressed.coord, &gti->offload.coords[slot],
			       sizeof(struct TouchOffloadCoord));
		} else {
			gti->debug_input[slot].released.time = time;
			gti->debug_input[slot].released.irq_index = irq_index;
			memcpy(&gti->debug_input[slot].released.coord, &gti->offload.coords[slot],
			       sizeof(struct TouchOffloadCoord));
			gti_debug_input_push(gti, slot);
		}
	}
	gti->slot_bit_changed = 0;
}

void gti_debug_input_dump(struct goog_touch_interface *gti)
{
	int slot;
	s16 i, count;
	s64 delta;
	s64 sec_delta_down;
	u32 ms_delta_down;
	s64 sec_delta_duration;
	u32 ms_delta_duration;
	s32 px_delta_x, px_delta_y;
	ktime_t current_time = ktime_get_real();
	struct gti_debug_input *last_fifo = gti->debug_input_history;

	/*
	 * Use peek to keep data without pop-out to support different timing
	 * print-out by each caller.
	 */
	count = kfifo_out_peek(&gti->debug_fifo_input, last_fifo, GTI_DEBUG_INPUT_KFIFO_LEN);

	i = max_t(s16, 0, count - GTI_DEBUG_INPUT_LOGS_LEN);
	for (; i < count; i++) {
		if (last_fifo[i].slot < 0 || last_fifo[i].slot >= MAX_SLOTS) {
			GOOG_INFO(gti, "dump: #%d: invalid slot #!\n", last_fifo[i].slot);
			continue;
		}
		sec_delta_down = -1;
		ms_delta_down = 0;
		/*
		 * Calculate the delta time of finger down from current time.
		 */
		delta = ktime_ms_delta(current_time, last_fifo[i].pressed.time);
		if (delta > 0)
			sec_delta_down = div_u64_rem(delta, MSEC_PER_SEC, &ms_delta_down);

		/*
		 * Calculate the delta time of finger duration from finger up to down.
		 */
		sec_delta_duration = -1;
		ms_delta_duration = 0;
		px_delta_x = 0;
		px_delta_y = 0;
		if (ktime_compare(last_fifo[i].released.time, last_fifo[i].pressed.time) > 0) {
			delta = ktime_ms_delta(last_fifo[i].released.time,
					       last_fifo[i].pressed.time);
			if (delta > 0) {
				sec_delta_duration =
					div_u64_rem(delta, MSEC_PER_SEC, &ms_delta_duration);
				px_delta_x = last_fifo[i].released.coord.x -
					     last_fifo[i].pressed.coord.x;
				px_delta_y = last_fifo[i].released.coord.y -
					     last_fifo[i].pressed.coord.y;
			}
		}

		GOOG_INFO(gti, "dump: #%d: %lld.%u(%lld.%u) D(%d, %d) I(%llu, %llu).\n",
			  last_fifo[i].slot, sec_delta_down, ms_delta_down, sec_delta_duration,
			  ms_delta_duration, px_delta_x, px_delta_y, last_fifo[i].pressed.irq_index,
			  last_fifo[i].released.irq_index);
		GOOG_DBG(gti, "dump-dbg: #%d: P(%u, %u) -> R(%u, %u).\n\n", last_fifo[i].slot,
			 last_fifo[i].pressed.coord.x, last_fifo[i].pressed.coord.y,
			 last_fifo[i].released.coord.x, last_fifo[i].released.coord.y);
	}
	/* Extra check for unexpected case. */
	for_each_set_bit(slot, &gti->slot_bit_active, MAX_SLOTS) {
		GOOG_INFO(gti, "slot #%d(%u, %u, %u) is active!\n", slot,
			  gti->offload.coords[slot].x, gti->offload.coords[slot].y,
			  gti->offload.coords[slot].pressure);
	}
}

void gti_debug_offload_toggle_push(struct goog_touch_interface *gti)
{
	struct gti_debug_offload_toggle fifo;

	fifo.running = gti->offload.offload_running;
	fifo.time = ktime_get_real();
	fifo.frame_index = gti->frame_index;
	fifo.input_index = gti->input_index;
	fifo.irq_index = gti->irq_index;

	/*
	 * Use kfifo as circular buffer by skipping one element
	 * when fifo is full.
	 */
	if (kfifo_is_full(&gti->debug_fifo_offload_toggle))
		kfifo_skip(&gti->debug_fifo_offload_toggle);

	kfifo_in(&gti->debug_fifo_offload_toggle, &fifo, 1);
}

void gti_debug_offload_toggle_dump(struct goog_touch_interface *gti)
{
	s16 i, count;
	s64 delta;
	s64 sec_delta;
	u32 ms_delta;
	ktime_t current_time = ktime_get_real();
	struct gti_debug_offload_toggle *last_fifo = gti->debug_offload_toggle_history;

	/*
	 * Use peek to keep data without pop-out to support different timing
	 * print-out by each caller.
	 */
	count = kfifo_out_peek(&gti->debug_fifo_offload_toggle, last_fifo,
			       GTI_DEBUG_OFFLOAD_TOGGLE_KFIFO_LEN);

	i = max_t(s16, 0, count - GTI_DEBUG_OFFLOAD_TOGGLE_LOGS_LEN);
	for (; i < count; i++) {
		sec_delta = -1;
		ms_delta = 0;
		/*
		 * Calculate the delta time between offload toggling and current time.
		 */
		delta = ktime_ms_delta(current_time, last_fifo[i].time);
		if (delta > 0)
			sec_delta = div_u64_rem(delta, MSEC_PER_SEC, &ms_delta);
		GOOG_INFO(gti, "dump-offload: %s(%lld.%u): IDX#%llu C#%llu I#%llu.\n",
			  (last_fifo[i].running) ? "O" : "X", sec_delta, ms_delta,
			  last_fifo[i].frame_index, last_fifo[i].input_index,
			  last_fifo[i].irq_index);
	}
}
