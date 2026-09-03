/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include "qdf_timer.h"
#include "qdf_list.h"
#include "qdf_lock.h"
#include "qdf_wakelock_debug.h"

#ifdef QDF_WAKE_LOCK_DEBUG

struct qdf_wakelock_node {
	struct wakeup_source *ws;
	qdf_list_node_t node;
};

static qdf_list_t qdf_wakelock_list;
static qdf_spinlock_t qdf_wakelock_lock;
qdf_timer_t debug_wake_lock_timer;

static void qdf_debug_wakelock_timer_fn(void *data)
{
	struct qdf_wakelock_node *node;
	qdf_list_node_t *iter, *next;
	QDF_STATUS status;
	unsigned long flags;

	qdf_spin_lock_bh(&qdf_wakelock_lock);
	status = qdf_list_peek_front(&qdf_wakelock_list, &iter);
	while (QDF_IS_STATUS_SUCCESS(status)) {
		node = qdf_container_of(iter,
					struct qdf_wakelock_node, node);
		if (node && node->ws) {
			spin_lock_irqsave(&node->ws->lock, flags);
			if (node->ws->active) {
				QDF_TRACE(QDF_MODULE_ID_QDF,
					  QDF_TRACE_LEVEL_ERROR,
					  "Wake lock leak detected: %s",
					  node->ws->name);
				QDF_BUG(0);
			}
			spin_unlock_irqrestore(&node->ws->lock, flags);
		}
		status = qdf_list_peek_next(&qdf_wakelock_list, iter, &next);
		iter = next;
		next = NULL;
	}
	qdf_spin_unlock_bh(&qdf_wakelock_lock);
}

void qdf_track_wakelock(qdf_wake_lock_t *qdf_lock)
{
	struct qdf_wakelock_node *node;

	// Skip undefined timeout wake locks
	if (!qdf_lock || qdf_lock->wake_lifetime == QDF_WAKE_TIME_UNDEFINED)
		return;

	node = qdf_mem_malloc(sizeof(*node));
	if (!node)
		return;

	node->ws = &qdf_lock->lock;

	qdf_spin_lock_bh(&qdf_wakelock_lock);
	qdf_list_insert_front(&qdf_wakelock_list, &node->node);
	qdf_spin_unlock_bh(&qdf_wakelock_lock);

	// Restart the timer on each acquire
	qdf_timer_mod(&debug_wake_lock_timer, WAKE_LOCK_TIMEOUT_MS);
}

void qdf_untrack_wakelock(qdf_wake_lock_t *qdf_lock)
{
	QDF_STATUS status;
	struct qdf_wakelock_node *node;
	qdf_list_node_t *iter, *next;

	if (!qdf_lock)
		return;

	qdf_spin_lock_bh(&qdf_wakelock_lock);
	status = qdf_list_peek_front(&qdf_wakelock_list, &iter);

	while (QDF_IS_STATUS_SUCCESS(status)) {
		node = qdf_container_of(iter,
					struct qdf_wakelock_node,
					node);

		if (node && node->ws == &qdf_lock->lock) {
			qdf_list_remove_node(&qdf_wakelock_list,
					     &node->node);
			qdf_mem_free(node);
			break;
		}
		status = qdf_list_peek_next(&qdf_wakelock_list,
					    iter, &next);
		iter = next;
		next = NULL;
	}

	qdf_spin_unlock_bh(&qdf_wakelock_lock);
}

void qdf_dbg_wake_lock_tracker_init(void)
{
	qdf_spinlock_create(&qdf_wakelock_lock);
	qdf_list_create(&qdf_wakelock_list, 0);
	qdf_timer_init(NULL, &debug_wake_lock_timer,
		       qdf_debug_wakelock_timer_fn, NULL,
		       QDF_TIMER_TYPE_WAKE_APPS);
}

static void qdf_dbg_wake_lock_track_list_clean(void)
{
	struct qdf_wakelock_node *node;
	qdf_list_node_t *iter;

	qdf_spin_lock_bh(&qdf_wakelock_lock);
	while (qdf_list_remove_front(&qdf_wakelock_list, &iter) ==
	       QDF_STATUS_SUCCESS) {
		node = qdf_container_of(iter,
					struct qdf_wakelock_node,
					node);
		qdf_mem_free(node);
	}
	qdf_spin_unlock_bh(&qdf_wakelock_lock);
}

void qdf_dbg_wake_lock_tracker_deinit(void)
{
	qdf_timer_stop(&debug_wake_lock_timer);
	qdf_timer_free(&debug_wake_lock_timer);
	qdf_dbg_wake_lock_track_list_clean();
	qdf_spinlock_destroy(&qdf_wakelock_lock);
	qdf_list_destroy(&qdf_wakelock_list);
}
#endif /* QDF_WAKE_LOCK_DEBUG */
