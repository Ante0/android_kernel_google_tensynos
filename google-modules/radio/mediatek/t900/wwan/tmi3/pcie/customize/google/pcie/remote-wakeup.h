/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef __REMOTE_WAKEUP_H__
#define __REMOTE_WAKEUP_H__

#include "../common/radio-google.h"

struct remote_wakeup {
	struct radio_google *goog;
	struct gpio_desc *pewake_gpio;
	int pewake_irq;
	bool ready;
	bool enabled;
};

int remote_wakeup_init(struct radio_google *goog);

void remote_wakeup_exit(struct radio_google *goog);

int remote_wakeup_enable(struct radio_google *goog);

int remote_wakeup_disable(struct radio_google *goog);

#endif /* __REMOTE_WAKEUP_H__ */
