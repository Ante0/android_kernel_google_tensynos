// SPDX-License-Identifier: GPL-2.0-only
/*
 * Router Advertisement timers implementation
 *
 * Copyright 2025 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifdef linux
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/types.h>
#include "ra_timers.h"
#else
#include "linux_port/jiffies.h"
#include <linux_port/timer.h>
#include "linux_port/types.h"
#include "net/ra_timers.h"
#endif

#ifndef ULLONG_MAX
#define ULLONG_MAX ((uint64_t)0xFFFFFFFFFFFFFFFFULL)
#endif

#define ONE_HOUR_MS	(1000 * 60 * 60)

static ras_timers_t *ras_timers = NULL;
static void (*lifetime_expired_callback)(const ra_timers_t *ra_timers) = NULL;
static void (*refresh_expired_callback)(const ra_timers_t *ra_timers) = NULL;
static struct timer_list ra_timer;
static struct timer_list rs_timer;
static struct timer_list refresh_timer;

static inline bool is_initialized(void) {
	return ras_timers != NULL;
}

static inline uint64_t __jiffies(void) {
#ifdef linux
	return jiffies;
#else
	return jiffies();
#endif
}

static uint64_t get_current_time_ms(void) {
#ifdef linux
	return ktime_to_ms(ktime_get_boottime());
#else
	return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			pw::chrono::SystemClock::now().time_since_epoch()).count());
#endif
}

static uint64_t get_min_expire_time_ms(void) {
	int i;
	uint64_t min_expire_time_ms = ULLONG_MAX;
	ra_timers_t *ra_timers;

	if (!is_initialized()) {
		return min_expire_time_ms;
	}

	for (i = 0; i < MAX_RA_PACKETS; ++i) {
		ra_timers = &ras_timers->ra_timers[i];
		if (!ra_timers->is_active ||
		    ra_timers->min_expire_time_ms == 0) {
			continue;
		}
		if (min_expire_time_ms > ra_timers->min_expire_time_ms) {
			min_expire_time_ms = ra_timers->min_expire_time_ms;
		}
	}
	return min_expire_time_ms;
}

static void check_and_expire_ra_timers(uint64_t current_time_ms) {
	int i;
	int j;
	ra_timers_t *ra_timers;
	uint64_t new_min_expire_time_ms;

	if (!is_initialized()) {
		return;
	}

	for (i = 0; i < MAX_RA_PACKETS; ++i) {
		ra_timers = &ras_timers->ra_timers[i];
		if (!ra_timers->is_active ||
		    current_time_ms < ra_timers->min_expire_time_ms) {
			continue;
		}
		// Some of the timers should have expired
		if (lifetime_expired_callback != NULL) {
			lifetime_expired_callback(ra_timers);
		}
		// Reset expire timers after callback
		new_min_expire_time_ms = ULLONG_MAX;
		for (j = 0; j < RA_TIMER_TYPE_MAX; ++j) {
			if (ra_timers->expire_times_ms[j] == 0) {
				continue;
			}
			if (ra_timers->expire_times_ms[j] <= current_time_ms) {
				ra_timers->expire_times_ms[j] = 0;
			} else if (ra_timers->expire_times_ms[j] < new_min_expire_time_ms) {
				new_min_expire_time_ms = ra_timers->expire_times_ms[j];
			}
		}
		ra_timers->min_expire_time_ms = new_min_expire_time_ms;
	}
}

static void ra_timer_reschedule(uint64_t current_time_ms) {
	uint64_t min_expire_time_ms = get_min_expire_time_ms();
	uint64_t one_hour_later_ms = current_time_ms + ONE_HOUR_MS;
	if (min_expire_time_ms < current_time_ms) {
		min_expire_time_ms = current_time_ms;
	}
	if (min_expire_time_ms > one_hour_later_ms) {
		min_expire_time_ms = one_hour_later_ms;
	}
	mod_timer(&ra_timer, __jiffies() + msecs_to_jiffies(min_expire_time_ms - current_time_ms));
	if (min_expire_time_ms - current_time_ms > 1000) {
		mod_timer(&rs_timer,
			  __jiffies() + msecs_to_jiffies(min_expire_time_ms - current_time_ms - 1000));
	} else {
		del_timer(&rs_timer);
	}
}

static void check_and_expire_refresh_timers(uint64_t current_time_ms) {
	int i;
	ra_timers_t *ra_timers;

	if (!is_initialized()) {
		return;
	}

	for (i = 0; i < MAX_RA_PACKETS; ++i) {
		ra_timers = &ras_timers->ra_timers[i];
		if (!ra_timers->is_active ||
			current_time_ms < ra_timers->refresh_time_ms) {
			continue;
		}
		if (refresh_expired_callback != NULL) {
			refresh_expired_callback(ra_timers);
		}
		// Reset refresh timers after callback
		ra_timers->refresh_time_ms = current_time_ms + RA_REFRESH_INTERVAL_MS;
	}
}

static void refresh_timer_reschedule(uint64_t current_time_ms) {
	int i;
	ra_timers_t *ra_timers;
	uint64_t min_refresh_time_ms = ULLONG_MAX;

	for (i = 0; i < MAX_RA_PACKETS; ++i) {
		ra_timers = &ras_timers->ra_timers[i];
		if (!ra_timers->is_active) {
			continue;
		}
		if (ra_timers->refresh_time_ms < min_refresh_time_ms) {
			min_refresh_time_ms = ra_timers->refresh_time_ms;
		}
	}
	if (min_refresh_time_ms != ULLONG_MAX && min_refresh_time_ms > current_time_ms) {
		mod_timer(&refresh_timer,
			  __jiffies() + msecs_to_jiffies(min_refresh_time_ms - current_time_ms));
	} else {
		del_timer(&refresh_timer);
	}
}

static void on_ra_timer_expire(struct timer_list * timer __attribute__((__unused__))) {
	uint64_t current_time_ms = get_current_time_ms();

	check_and_expire_ra_timers(current_time_ms);
	ra_timer_reschedule(current_time_ms);
}

static void on_rs_timer_expire(struct timer_list *timer __attribute__((__unused__))) {
	send_rs_packet(ras_timers->sta_ifindex);
}

static void on_refresh_timer_expire(struct timer_list *timer __attribute__((__unused__))) {
	uint64_t current_time_ms = get_current_time_ms();

	check_and_expire_refresh_timers(current_time_ms);
	refresh_timer_reschedule(current_time_ms);
}

static void reset_expire_times(ra_timers_t *ra_timers, ra_packet_t *ra_packet,
			       uint64_t current_time_ms) {
	int i;
	uint64_t min_expire_time_ms = ULLONG_MAX;

	memset(ra_timers->expire_times_ms, 0, sizeof(ra_timers->expire_times_ms));
	if (ra_packet->router_lifetime != 0) {
		ra_timers->expire_times_ms[RA_TIMER_TYPE_ROUTER_LIFETIME] =
				current_time_ms + 1000 * ra_packet->router_lifetime;
	}
	if (ra_packet->min_pio_valid_lifetime != 0) {
		ra_timers->expire_times_ms[RA_TIMER_TYPE_PIO_VALID_LIFETIME] =
				current_time_ms + 1000 * ra_packet->min_pio_valid_lifetime;
	}
	if (ra_packet->min_rio_route_lifetime != 0) {
		ra_timers->expire_times_ms[RA_TIMER_TYPE_RIO_ROUTE_LIFETIME] =
				current_time_ms + 1000 * ra_packet->min_rio_route_lifetime;
	}
	if (ra_packet->min_rdnss_lifetime != 0) {
		ra_timers->expire_times_ms[RA_TIMER_TYPE_RDNSS_LIFETIME] =
				current_time_ms + 1000 * ra_packet->min_rdnss_lifetime;
	}
	for (i = 0; i < RA_TIMER_TYPE_MAX; ++i) {
		if (ra_timers->expire_times_ms[i] == 0) {
			continue;
		}
		if (ra_timers->expire_times_ms[i] < min_expire_time_ms) {
			min_expire_time_ms = ra_timers->expire_times_ms[i];
		}
	}
	ra_timers->min_expire_time_ms = min_expire_time_ms;
}

int ra_timers_init(ras_timers_t *base_addr, void (*lifetime_expired_cb)(const ra_timers_t*),
		   void (*refresh_timer_cb)(const ra_timers_t*)) {
	int i;

	if (base_addr == NULL || lifetime_expired_cb == NULL || refresh_timer_cb == NULL) {
		return RA_TIMERS_RESULT_INVALID_ARGUMENT;
	}
	if (ras_timers != NULL) {
		return RA_TIMERS_RESULT_ALREADY_INITIALIZED;
	}
	ras_timers = base_addr;
	for (i = 0; i < MAX_RA_PACKETS; ++i) {
		ras_timers->ra_timers[i].is_active = 0;
		ras_timers->ra_timers[i].min_expire_time_ms = 0;
	}
	ras_timers->sta_ifindex = 0;
	ras_timers->num_active_timers = 0;
	lifetime_expired_callback = lifetime_expired_cb;
	refresh_expired_callback = refresh_timer_cb;
	timer_setup(&ra_timer, on_ra_timer_expire, 0);
	timer_setup(&rs_timer, on_rs_timer_expire, 0);
	timer_setup(&refresh_timer, on_refresh_timer_expire, 0);

	return RA_TIMERS_RESULT_SUCCESS;
}

void ra_timers_deinit(void) {
	ras_timers = NULL;
	lifetime_expired_callback = NULL;
	refresh_expired_callback = NULL;
	del_timer(&ra_timer);
	del_timer(&rs_timer);
	del_timer(&refresh_timer);
}

void ra_timers_clear(void) {
	int i;

	if (!is_initialized()) {
		return;
	}
	ras_timers->num_active_timers = 0;
	for (i = 0; i < MAX_RA_PACKETS; ++i) {
		ras_timers->ra_timers[i].is_active = 0;
		ras_timers->ra_timers[i].min_expire_time_ms = 0;
	}
	del_timer(&ra_timer);
	del_timer(&rs_timer);
	del_timer(&refresh_timer);
}

int ra_timers_add_or_update(ra_packet_t *ra_packet) {
	int i;
	ra_timers_t *ra_timers = NULL;
	uint64_t current_time_ms = get_current_time_ms();
	int is_replaced = 0;

	if (!is_initialized()) {
		return RA_TIMERS_RESULT_FAIL;
	}

	ra_timers = ra_timers_get(ra_packet);
	if (ra_timers != NULL) {
		if ((ra_timers->ra_packet.flags & RA_PACKET_FLAG_SOLICITED) == 0 &&
		    (ra_packet->flags & RA_PACKET_FLAG_SOLICITED) != 0) {
			memcpy(&ra_timers->ra_packet, ra_packet, sizeof(ra_packet_t));
			is_replaced = 1;
		}
		reset_expire_times(ra_timers, ra_packet, current_time_ms);
		ra_timer_reschedule(current_time_ms);
		return is_replaced ? RA_TIMERS_RESULT_REPLACED_BY_SOLICITED
				: RA_TIMERS_RESULT_SUCCESS;
	}

	// Check if the maximum is exceeded
	if (ras_timers->num_active_timers >= MAX_RA_PACKETS) {
		return RA_TIMERS_RESULT_TABLE_FULL;
	}
	// Iterate through the timers to find a free timers slot
	for (i = 0; i < MAX_RA_PACKETS; ++i) {
		if (!ras_timers->ra_timers[i].is_active) {
			ra_timers = &ras_timers->ra_timers[i];
			break;
		}
	}
	// No free slot, impossible if we already check num_active_timers
	if (ra_timers == NULL) {
		return RA_TIMERS_RESULT_FAIL;
	}

	ra_timers->is_active = 1;
	ras_timers->num_active_timers++;
	memcpy(&ra_timers->ra_packet, ra_packet, sizeof(ra_packet_t));
	reset_expire_times(ra_timers, ra_packet, get_current_time_ms());
	ra_timer_reschedule(current_time_ms);

	return RA_TIMERS_RESULT_NEW_ENTRY;
}

void ra_timers_remove(const ra_packet_t *ra_packet) {
	ra_timers_t *ra_timers;

	if (!is_initialized()) {
		return;
	}
	ra_timers = ra_timers_get(ra_packet);
	if (ra_timers == NULL) {
		return;
	}
	ra_timers->is_active = 0;
	ras_timers->num_active_timers--;
	ra_timer_reschedule(get_current_time_ms());
}

ra_timers_t *ra_timers_get(const ra_packet_t *ra_packet) {
	int i;
	for (i = 0; i < MAX_RA_PACKETS; ++i) {
		if (!ras_timers->ra_timers[i].is_active) {
			continue;
		}
		if (is_same_ra(&ras_timers->ra_timers[i].ra_packet, ra_packet)) {
			return &ras_timers->ra_timers[i];
		}
	}
	return NULL;
}

// For test only
ras_timers_t *ras_timers_get(void) {
	return ras_timers;
}

