/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AOSS header for SSR notifier block
 *
 * Copyright (c) 2025 Google LLC
 */

#ifndef __AOSS_SSR_NOTIFIER_H_
#define __AOSS_SSR_NOTIFIER_H_

#include <linux/notifier.h>

enum aoss_ssr_notifier_event_t {
	AOSS_SSR_AMBSS_DOWN = 0,
	AOSS_SSR_PG_DOWN,
	AOSS_SSR_AMBSS_UP,
	AOSS_SSR_PG_UP,
	AOSS_SSR_ONLINE,
	AOSS_SSR_EARLY_PREPARE,

	AOSS_SSR_NOTIFICATION_EVENT_TOT,
};

#if IS_ENABLED(CONFIG_AOSS_SSR_NOTIFIER)
int aoss_ssr_add_notifier(struct notifier_block *nb);
int aoss_ssr_remove_notifier(struct notifier_block *nb);
void aoss_ssr_notify(enum aoss_ssr_notifier_event_t event);
#else
static inline int aoss_ssr_add_notifier(struct notifier_block *nb) { return 0; }
static inline int aoss_ssr_remove_notifier(struct notifier_block *nb) { return 0; }
static inline void aoss_ssr_notify(enum aoss_ssr_notifier_event_t event) {}
#endif

#endif /* __AOSS_SSR_NOTIFIER_H_ */
