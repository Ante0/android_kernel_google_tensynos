/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * DOC: qdf_wakelock_debug.h
 * This file abstracts wakelock debug infra APIs.
 */

#ifndef _QDF_WAKELOCK_DEBUG_H
#define _QDF_WAKELOCK_DEBUG_H

#include "qdf_lock.h"
#include "qdf_timer.h"

#define WAKE_LOCK_TIMEOUT_MS 60000
extern qdf_timer_t debug_wake_lock_timer;

#ifdef QDF_WAKE_LOCK_DEBUG
/**
 * qdf_track_wakelock() - add acquired wakelock tracking to debug list
 *
 * @qdf_lock: The wake lock to track
 *
 * Return: None
 */
void qdf_track_wakelock(qdf_wake_lock_t *qdf_lock);

/**
 * qdf_untrack_wakelock() - remove released wakelock from debug list
 *
 * @qdf_lock: The wake lock to untrack
 *
 * Return: None
 */
void qdf_untrack_wakelock(qdf_wake_lock_t *qdf_lock);

/**
 * qdf_dbg_wake_lock_tracker_init() - initialize wakelock debug infra
 *
 * Return: None
 */
void qdf_dbg_wake_lock_tracker_init(void);

/**
 * qdf_dbg_wake_lock_tracker_deinit() - deinitialize wakelock debug infra
 *
 * Return: None
 */
void qdf_dbg_wake_lock_tracker_deinit(void);
#else
static inline void qdf_track_wakelock(qdf_wake_lock_t *qdf_lock) {}
static inline void qdf_untrack_wakelock(qdf_wake_lock_t *qdf_lock) {}
static inline void qdf_dbg_wake_lock_tracker_init(void) {}
static inline void qdf_dbg_wake_lock_tracker_deinit(void) {}
#endif  /* QDF_WAKE_LOCK_DEBUG */
#endif /* _QDF_WAKELOCK_DEBUG_H */
