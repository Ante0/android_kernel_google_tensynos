/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Router Advertisement timers header file
 *
 * Copyright 2025 Google LLC.
 *
 * Author: KH Shi <kenghua@google.com>
 */
#ifndef RA_TIMERS_H
#define RA_TIMERS_H

#ifdef linux
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/types.h>
#else
#include "linux_port/timer.h"
#include "linux_port/types.h"
#include "pw_chrono/system_clock.h"
#endif

#include "ra_packet.h"

#define MAX_RA_PACKETS		4
#define RA_REFRESH_INTERVAL_MS  (8900*1000) // Slightly smaller than the max router lifetime (9000)

typedef enum ra_timers_result {
	RA_TIMERS_RESULT_SUCCESS,
	RA_TIMERS_RESULT_NEW_ENTRY,
	RA_TIMERS_RESULT_REPLACED_BY_SOLICITED,
	RA_TIMERS_RESULT_TABLE_FULL,
	RA_TIMERS_RESULT_ALREADY_INITIALIZED,
	RA_TIMERS_RESULT_INVALID_ARGUMENT,
	RA_TIMERS_RESULT_FAIL,
	RA_TIMERS_RESULT_MAX
} ra_timers_result_t;

typedef enum ra_timer_type {
	RA_TIMER_TYPE_ROUTER_LIFETIME,
	RA_TIMER_TYPE_PIO_PREFERRED_LIFETIME,
	RA_TIMER_TYPE_PIO_VALID_LIFETIME,
	RA_TIMER_TYPE_RIO_ROUTE_LIFETIME,
	RA_TIMER_TYPE_RDNSS_LIFETIME,
	RA_TIMER_TYPE_MAX
} ra_timer_type_t;

typedef struct ra_timers {
	uint64_t expire_times_ms[RA_TIMER_TYPE_MAX];
	uint64_t min_expire_time_ms;
	uint64_t refresh_time_ms;
	int is_active;
	ra_packet_t ra_packet;
} __attribute__((packed, aligned(4))) ra_timers_t;

typedef struct ras_timers {
	int sta_ifindex;
	int num_active_timers;
	ra_timers_t ra_timers[MAX_RA_PACKETS];
} __attribute__((packed, aligned(4))) ras_timers_t;

int ra_timers_init(ras_timers_t *base_addr, void (*lifetime_expired_cb)(const ra_timers_t*),
		   void (*refresh_timer_cb)(const ra_timers_t*));
void ra_timers_deinit(void);
void ra_timers_clear(void);

int ra_timers_add_or_update(ra_packet_t *ra_packet);
void ra_timers_remove(const ra_packet_t *ra_packet);
ra_timers_t *ra_timers_get(const ra_packet_t *ra_packet);

// For test only
ras_timers_t *ras_timers_get(void);

#endif // RA_TIMERS_H
