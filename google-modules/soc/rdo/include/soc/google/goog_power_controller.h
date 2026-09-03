/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2025 Google LLC */

#ifndef __GOOG_POWER_CONTROLLER_H
#define __GOOG_POWER_CONTROLLER_H

#include <linux/notifier.h>

enum power_state {
	PD_STATE_OFF,	/* PM domain is off */
	PD_STATE_ON,	/* PM domain is on */
	PD_STATE_COUNT
};

int get_pd_state_by_name(const char *name, enum power_state *state);
int register_pd_notifier_by_name(const char *name, struct notifier_block *nb);

#endif /* __GOOG_POWER_CONTROLLER_H */
