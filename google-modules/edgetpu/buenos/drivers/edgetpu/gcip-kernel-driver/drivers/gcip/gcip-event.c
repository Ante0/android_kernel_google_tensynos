// SPDX-License-Identifier: GPL-2.0-only
/*
 * Definitions of GCIP event interfaces.
 *
 * The GCIP event manager is a standalone module which allows users to use a single module to manage
 * eventfd contexts for registering, signaling, and life cycle handling.
 *
 * Copyright (C) 2026 Google LLC
 */

#include <linux/container_of.h>
#include <linux/err.h>
#include <linux/eventfd.h>
#include <linux/kref.h>
#include <linux/lockdep.h>
#include <linux/rwlock.h>
#include <linux/slab.h>

#include <gcip/gcip-event.h>

struct gcip_event_mgr *gcip_event_mgr_create(size_t count)
{
	struct gcip_event_mgr *mgr;

	mgr = kzalloc(struct_size(mgr, events, count), GFP_KERNEL);
	if (!mgr)
		return ERR_PTR(-ENOMEM);

	mgr->count = count;
	kref_init(&mgr->ref);
	rwlock_init(&mgr->events_lock);

	return mgr;
}

/**
 * gcip_event_mgr_set_locked() - Sets an event context and owner under lock.
 * @mgr: The event manager.
 * @event_id: Index of the event.
 * @ctx: The eventfd context to set, or NULL to clear.
 * @owner: The owner of the event, or NULL to clear.
 *
 * If the slot already has a context, it will be released.
 *
 * Context: Must be called with @mgr->events_lock held for writing.
 */
static void gcip_event_mgr_set_locked(struct gcip_event_mgr *mgr, size_t event_id,
				      struct eventfd_ctx *ctx, void *owner)
{
	lockdep_assert_held_write(&mgr->events_lock);

	if (mgr->events[event_id].ctx)
		eventfd_ctx_put(mgr->events[event_id].ctx);

	mgr->events[event_id].ctx = ctx;
	mgr->events[event_id].owner = owner;
}

void gcip_event_mgr_destroy(struct gcip_event_mgr *mgr)
{
	unsigned long flags;
	size_t i;

	write_lock_irqsave(&mgr->events_lock, flags);
	for (i = 0; i < mgr->count; i++)
		gcip_event_mgr_set_locked(mgr, i, NULL, NULL);
	write_unlock_irqrestore(&mgr->events_lock, flags);

	gcip_event_mgr_put(mgr);
}

struct gcip_event_mgr *gcip_event_mgr_get(struct gcip_event_mgr *mgr)
{
	kref_get(&mgr->ref);

	return mgr;
}

/**
 * gcip_event_mgr_release() - The callback function to call when the reference count reaches 0.
 * @ref: Pointer to the kref structure.
 */
static void gcip_event_mgr_release(struct kref *ref)
{
	struct gcip_event_mgr *mgr = container_of(ref, struct gcip_event_mgr, ref);

	kfree(mgr);
}

void gcip_event_mgr_put(struct gcip_event_mgr *mgr)
{
	kref_put(&mgr->ref, gcip_event_mgr_release);
}

int gcip_event_mgr_set(struct gcip_event_mgr *mgr, size_t event_id, int eventfd, void *owner)
{
	struct eventfd_ctx *ctx;
	unsigned long flags;

	if (event_id >= mgr->count)
		return -EINVAL;

	ctx = eventfd_ctx_fdget(eventfd);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	write_lock_irqsave(&mgr->events_lock, flags);
	gcip_event_mgr_set_locked(mgr, event_id, ctx, owner);
	write_unlock_irqrestore(&mgr->events_lock, flags);

	return 0;
}

void gcip_event_mgr_unset(struct gcip_event_mgr *mgr, size_t event_id)
{
	unsigned long flags;

	if (event_id >= mgr->count)
		return;

	write_lock_irqsave(&mgr->events_lock, flags);
	gcip_event_mgr_set_locked(mgr, event_id, NULL, NULL);
	write_unlock_irqrestore(&mgr->events_lock, flags);
}

void gcip_event_mgr_unset_by_owner(struct gcip_event_mgr *mgr, void *owner)
{
	unsigned long flags;
	size_t i;

	if (!owner)
		return;

	write_lock_irqsave(&mgr->events_lock, flags);
	for (i = 0; i < mgr->count; i++) {
		if (mgr->events[i].owner == owner)
			gcip_event_mgr_set_locked(mgr, i, NULL, NULL);
	}
	write_unlock_irqrestore(&mgr->events_lock, flags);
}

bool gcip_event_mgr_signal(struct gcip_event_mgr *mgr, size_t event_id)
{
	unsigned long flags;
	bool signaled = false;

	if (event_id >= mgr->count)
		return false;

	read_lock_irqsave(&mgr->events_lock, flags);
	if (mgr->events[event_id].ctx) {
		eventfd_signal(mgr->events[event_id].ctx);
		signaled = true;
	}
	read_unlock_irqrestore(&mgr->events_lock, flags);

	return signaled;
}
