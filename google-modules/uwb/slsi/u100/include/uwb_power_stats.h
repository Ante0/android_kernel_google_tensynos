/* SPDX-License-Identifier: GPL-2.0-ONLY */

/**
 * @copyright Copyright (c) 2025 Samsung Electronics Co., Ltd
 *
 */

#ifndef __UWB_POWER_STATS__
#define __UWB_POWER_STATS__

#include <linux/types.h>

#define U100_POWER_STATE_MAX 3

enum u100_power_state {
	U100_SLEEP = 0,
	U100_ACTIVE = 1,
	U100_OFF = 2
};

struct u100_power_stats_data {
	u64 count;
	u64 duration;
	u64 last_entry;
	u64 last_exit;
};

struct u100_power_stats {
	struct gpio_desc *gpio_u100_power_stats;
	int irq_u100_power_stats;
	enum u100_power_state current_power_state;
	struct u100_power_stats_data power_stats_data[U100_POWER_STATE_MAX];
	struct mutex power_stats_mutex;
};

int u100_power_stats_init(struct u100_ctx *u100_ctx);
void u100_power_stats_deinit(struct u100_ctx *u100_ctx);
void u100_power_stats_on_switch(struct u100_ctx *u100_ctx);

#endif /* __UWB_POWER_STATS__ */
