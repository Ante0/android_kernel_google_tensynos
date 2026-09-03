// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "pixelmd: " fmt

#include "pixelmd_events.h"
#include "pixelmd_client.h"
#include "pixelmd_api.h"

#include <linux/atomic.h>
#include <linux/cleanup.h>
#include <linux/kfifo.h>
#include <linux/ktime.h>
#include <linux/lockdep.h>
#include <linux/printk.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#define _PAYLOAD_SIZE_FROM_EVENT(event_code)                                   \
	((unsigned long)(((event_code) >> _PIXELMD_EVENT_PAYLOAD_SIZE_SHIFT) & \
			 _PIXELMD_EVENT_PAYLOAD_SIZE_MASK))

#define _EVENT_ID_FROM_EVENT(event_code) \
	((unsigned long)(((event_code) >> _PIXELMD_EVENT_ID_SHIFT) & _PIXELMD_EVENT_ID_MASK))

static bool __must_check init_header(struct pixelmd_event_header *header, unsigned long event_code,
				     size_t payload_size)
{
	unsigned long expected_payload_size = _PAYLOAD_SIZE_FROM_EVENT(event_code);

	if (payload_size != expected_payload_size) {
		pr_err("Not writing event %lu with incorrect payload size %zu (expected %lu)\n",
		       _EVENT_ID_FROM_EVENT(event_code), payload_size, expected_payload_size);
		return false;
	}

	memset(header, 0, sizeof(*header));
	header->timestamp_ns = ktime_get_ns();
	header->event_code = event_code;

	return true;
}

/*
 * Writes the event (header+payload) to the client. Does nothing if the client
 * doesn't have the source enabled. Drops event if there is not enough space in
 * the client's fifo.
 *
 * NOTE: client->lock must be held by the caller!
 */
static void write_to_client_locked(struct pixelmd_client *client, enum pixelmd_source source,
				   const struct pixelmd_event_header *header, const void *payload,
				   size_t payload_size)
{
	lockdep_assert_held(&client->event_write_lock);

	if (!test_bit(source, client->enabled_sources))
		return;

	size_t total_size = sizeof(*header) + payload_size;

	if (kfifo_avail(&client->event_fifo) < total_size) {
		// TODO: accumulate and send EVENTS_DROPPED event instead.
		pr_warn_ratelimited("No space in the queue - dropping event %lu\n",
				    _EVENT_ID_FROM_EVENT(header->event_code));
	} else {
		kfifo_in(&client->event_fifo, header, sizeof(*header));
		if (payload_size)
			kfifo_in(&client->event_fifo, payload, payload_size);
		wake_up_interruptible(&client->event_wq);
	}
}

void pixelmd_client_write_event(struct pixelmd_client *client, enum pixelmd_source source,
				unsigned long event_code, const void *payload, size_t payload_size)
{
	struct pixelmd_event_header header;

	if (!init_header(&header, event_code, payload_size))
		return;

	/*
	 * Since we can be called from an atomic context (e.g. via a vendor hook), we
	 * need to ensure that interrupts are always disabled to avoid self-deadlock.
	 */
	scoped_guard(spinlock_irqsave, &client->event_write_lock) {
		write_to_client_locked(client, source, &header, payload, payload_size);
	}
}

void pixelmd_write_event(enum pixelmd_source source, unsigned long event_code, const void *payload,
			 size_t payload_size)
{
	struct pixelmd_event_header header;

	// Don't do anything if there are no clients that have the source enabled.
	if (atomic_read(&pixelmd_clients.source_enable_counts[source]) == 0)
		return;

	if (!init_header(&header, event_code, payload_size))
		return;

	/*
	 * Since we can be called from an atomic context (e.g. via a vendor hook), we
	 * need to ensure that interrupts are always disabled to avoid self-deadlock.
	 */
	scoped_guard(spinlock_irqsave, &pixelmd_clients.lock) {
		struct pixelmd_client *client;

		list_for_each_entry(client, &pixelmd_clients.list, node) {
			scoped_guard(spinlock_irqsave, &client->event_write_lock) {
				write_to_client_locked(client, source, &header, payload,
						       payload_size);
			}
		}
	}
}
