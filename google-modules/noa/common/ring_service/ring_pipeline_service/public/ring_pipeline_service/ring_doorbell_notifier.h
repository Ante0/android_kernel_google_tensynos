/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header of ring doorbell notifier component.
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#ifndef NOA_RING_PIPELINE_SERVICE_RING_DOORBELL_NOTIFIER_H
#define NOA_RING_PIPELINE_SERVICE_RING_DOORBELL_NOTIFIER_H

#ifdef linux
#include <linux/types.h>

#include "nep.h"
#include "port.h"
#else /* linux */
#include <cstdint>

#include "notifier/notifier_mailbox.h"
#endif /* linux */

#define MAX_NUM_OF_TASK_PER_RING_SERVICE_ISR (8U)
#define RING_SERVICE_DOORBELL_INTERRUPT_BIT (1U)

typedef struct {
#ifdef linux
	struct noa_port *port;
#else /* linux */
	::noa::module::notifier::NotifierMailbox notifier;
#endif /* linux */
} RingServiceDoorbellContext;

/**
 * @brief Triggers a doorbell notification.
 *
 * This function sends a notification to signal that new data or a task is ready.
 *
 * @param[in] notifier A pointer to the RingServiceDoorbellContext containing
 * information for triggering the doorbell.
 * @return This function does not return a value.
 */
static inline void RingServiceDoorbellTrigger(RingServiceDoorbellContext *notifier)
{
	if (!notifier) {
		return;
	}
#ifdef linux
	notifier->port->ints |= RING_SERVICE_DOORBELL_INTERRUPT_BIT;
	noa_sim_trig_tx();
#else /* linux */
	notifier->notifier.Notify();
#endif /* linux */
}

/**
 * @brief Initializes all ring service doorbell notifiers.
 *
 * This function iterates through all doorbell IDs to initialize their
 * corresponding RingServiceDoorbellContext. For each valid ID, it sets up the
 * notification mechanism.
 *
 * @return 0 on success, or a negative error code on failure.
 */
int32_t RingServiceDoorbellNotifierInitAll(void);

/**
 * @brief Retrieves a specific ring service doorbell context.
 *
 * This function returns a pointer to the RingServiceDoorbellContext
 * associated with the given doorbell_id. This context is used to trigger
 * notifications for that specific doorbell.
 *
 * @param[in] doorbell_id The identifier of the doorbell whose context is to be retrieved.
 * @return A pointer to the RingServiceDoorbellContext if the doorbell_id is valid, otherwise NULL.
 */
RingServiceDoorbellContext *RingServiceDoorbellNotifierGet(uint8_t doorbell_id);

#endif /* NOA_RING_PIPELINE_SERVICE_RING_DOORBELL_NOTIFIER_H */
