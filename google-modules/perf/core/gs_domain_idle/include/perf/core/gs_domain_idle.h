/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google, Inc.
 *
 * Domain Idle driver for Performance notifiers
 * and governors.
 */

#ifndef _GS_DOMAIN_IDLE_H_
#define _GS_DOMAIN_IDLE_H_

#if IS_ENABLED(CONFIG_GS_DOMAIN_IDLE)

void register_set_cluster_enabled_cb(void (*func)(int, int));

#else

static inline void register_set_cluster_enabled_cb(void (*func)(int, int))
{
	return;
}

#endif

#endif // _GS_DOMAIN_IDLE_H_
