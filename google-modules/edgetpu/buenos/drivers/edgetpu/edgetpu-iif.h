/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Support Inter-IP Fences.
 *
 * Copyright (C) 2025 Google LLC
 */

#ifndef __EDGETPU_IIF_H__
#define __EDGETPU_IIF_H__

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <gcip/gcip-fence-array.h>
#include <gcip/gcip-mailbox.h>
#include <gcip/gcip-memory.h>
#include <iif/iif-fence.h>
#include <iif/iif-manager.h>

#include "edgetpu-internal.h"
#include "edgetpu-mailbox.h"

struct edgetpu_iif {
	struct edgetpu_dev *etdev;

	struct iif_manager *iif_mgr;
	struct device *iif_dev;

	/* Interface for managing sending/receiving messages via the mailbox queues. */
	struct gcip_mailbox *mbx_protocol;
	/* Interface for accessing the mailbox hardware and the values in their data registers. */
	struct edgetpu_mailbox *mbx_hardware;
	struct gcip_memory cmd_queue_mem;
	struct mutex cmd_queue_lock;
	struct gcip_memory resp_queue_mem;
	spinlock_t resp_queue_lock;
	unsigned long resp_queue_lock_flags;

	/*
	 * Fields used to ensure pending signal commands are flushed when firmware resets.
	 *
	 * When edgetpu_iif_send_unblock_notification() is called it must check if @is_flushing
	 * has been set, and exit immediately if so. Otherwise, it increments @pending_signals.
	 * If @is_flushing becomes set while a thread is waiting for @cmd_queue_lock or space in
	 * the command queue, it should return an error immediately rather than enqueueing its
	 * command.
	 *
	 * When firmware resets, @is_flushing will be set and the reset code will wait until
	 * @pending_signals is 0 before proceeding.
	 *
	 * @flush_lock protects both @is_flushing and @pending_signals.
	 */
	bool is_flushing;
	unsigned int pending_signals;
	spinlock_t flush_lock;

	/* The work sending IIF unblock notification to the firmware. */
	struct delayed_work unblocked_work;
	/* The list of unblocked IIF. */
	struct list_head unblocked_list;
	/* Protects @unblocked_list. */
	spinlock_t unblocked_lock;
};

/* Wrapper of `iif_fence_poll_cb` to have private per-callback data. */
struct edgetpu_iif_poll_cb {
	struct iif_fence_poll_cb cb;
	struct edgetpu_iif *etiif;
};

/*
 * Initializes an IIF object.
 *
 * Initializes the IIF mailbox (if supported), obtain references to the system @iif_mgr and
 * @iif_dev, and start @unblocked_work.
 */
int edgetpu_iif_init(struct edgetpu_dev *etdev, struct edgetpu_iif *etiif);

/*
 * Releases resources allocated by @etiif.
 *
 * Since @unblocked_work calls `edgetpu_pm_get()` and this function will stop @unblocked_work, this
 * function cannot be called in a thread holding the power management lock.
 *
 * The IIF mailbox can be reset or flushed while holding the power management lock by calling
 * `edgetpu_iif_reinit_mailbox()` or `edgetpu_iif_release_mailbox()` respectively.
 */
void edgetpu_iif_release(struct edgetpu_iif *etiif);

/*
 * Initialize the IIF mailbox if supported.
 *
 * If the platform does not support an IIF mailbox, this function returns success immediately.
 */
int edgetpu_iif_init_mailbox(struct edgetpu_dev *etdev, struct edgetpu_iif *etiif);

/*
 * Releases resources used by the IIF mailbox.
 *
 * This function is meant to flush any pending IIF mailbox commands and de-allocate the mailbox's
 * queues and other resources. The rest of @etiif's state will be unaffected.
 */
void edgetpu_iif_release_mailbox(struct edgetpu_iif *etiif);

/*
 * Re-initializes the initialized IIF object.
 *
 * This function is used when the TPU device is reset, it re-programs CSRs related to the IIF
 * mailbox.
 */
void edgetpu_iif_reinit_mailbox(struct edgetpu_iif *etiif);

/*
 * Notifies the firmware of the unblock of the @fence_id inter-IP fence.
 *
 * Note that this function will be called when the fence has been unblocked and the IIF driver calls
 * the unblocked callback.
 *
 * This function can be called in any context. It's caller's responsibility to retry when it returns
 * an -EBUSY error.
 *
 * Returns 0 on success, or a negative errno on error. Specifically, it returns -EBUSY if the IIF
 * command queue is full.
 */
int edgetpu_iif_send_unblock_notification(struct edgetpu_iif *etiif, int fence_id);

/**
 * edgetpu_iif_submit_waiter_and_signaler() - Submits waiter and signaler to in-fences and
 *                                            out-fences of @ikv_resp.
 * @etiif: TPU IIF support context.
 * @in_fence_array: Array of in-fences.
 * @out_fence_array: Array of out-fences.
 * @poll_cb_array: Array of poll callbacks for the in-fences. Its size must be equal to the size of
 *                 @in_fence_array.
 *
 * A waiter will be submitted to IIFs in @in_fence_array and a signaler will be submitted to IIFs in
 * @out_fence_array. Also, poll callbacks will be registered to IIFs in @in_fence_array.
 *
 * Context: Normal.
 * Return: 0 on success. Otherwise, a negative errno.
 */
int edgetpu_iif_submit_waiter_and_signaler(struct edgetpu_iif *etiif,
					   struct gcip_fence_array *in_fence_array,
					   struct gcip_fence_array *out_fence_array,
					   struct edgetpu_iif_poll_cb *poll_cb_array);

/**
 * edgetpu_iif_waiter_and_signaler_completed_async() - Notifies completion of waiter and signaler
 *                                                     to in-fences and out-fences of @ikv_resp.
 *
 * @etiif: TPU IIF support context.
 * @in_fence_array: Array of in-fences.
 * @out_fence_array: Array of out-fences.
 * @poll_cb_array: Array of poll callbacks for the in-fences. Its size must be equal to the size of
 *                 @in_fence_array.
 *
 * Notifies IIFs in @in_fence_array that a waiter has completed and IIFs in @out_fence_array that a
 * signaler has completed. Also, the poll callbacks will be removed from IIFs in @in_fence_array.
 *
 * Context: Any.
 */
void edgetpu_iif_waiter_and_signaler_completed_async(struct edgetpu_iif *etiif,
						     struct gcip_fence_array *in_fence_array,
						     struct gcip_fence_array *out_fence_array,
						     struct edgetpu_iif_poll_cb *poll_cb_array);

#endif /* __EDGETPU_IIF_H__*/
