/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2024 Google LLC */
#ifndef _GOOGLE_IP_IDLE_NOTIFIER_H
#define _GOOGLE_IP_IDLE_NOTIFIER_H

enum ip_idle_state {
	STATE_IDLE,
	STATE_BUSY,
};

#if IS_ENABLED(CONFIG_GOOGLE_IP_IDLE_NOTIFIER)

/**
 * @index: index of idle-ip
 * @idle: idle status, (idle == 0)idle or (idle == 1)busy
 */
int google_update_ip_idle_status(int index, enum ip_idle_state idle);

#else

static inline int google_update_ip_idle_status(int index, enum ip_idle_state idle)
{
	return 0;
}

#endif /* CONFIG_GOOGLE_IP_IDLE_NOTIFIER */

#endif /* _GOOGLE_IP_IDLE_NOTIFIER_H */
