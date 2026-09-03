/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This module provides a framework for managing eventfd contexts within the GCIP ecosystem.
 *
 * It offers a layer above the standard Linux eventfd_* functions, providing benefits such as:
 * - Structured Event Management: Allows grouping multiple eventfds under a single manager object,
 *   which can be used to identify events during the cleanup stage.
 * - Reference Counting: The `gcip_event_mgr` uses a kref to manage its lifecycle, ensuring it's
 *   only destroyed when all references are released.
 * - Simplified Cleanup: The manager handles the eventfd context cleanup when it's destroyed or
 *   when individual events are unset.
 *
 * Copyright (C) 2026 Google LLC
 */

#ifndef __GCIP_EVENT_H__
#define __GCIP_EVENT_H__

#include <linux/eventfd.h>
#include <linux/kref.h>
#include <linux/rwlock.h>
#include <linux/types.h>

/**
 * struct gcip_event - The GCIP event object.
 * @ctx: The Eventfd context.
 * @owner: (Optional) The owner of the event, set to NULL if no owner is specified.
 *
 * When @owner is set, it allows grouping multiple events together and unsetting them all at once by
 * calling gcip_event_mgr_unset_by_owner() with the owner's address. The value of this pointer is
 * not interpreted by the gcip-event module; it's solely used for comparison.
 */
struct gcip_event {
	struct eventfd_ctx *ctx;
	void *owner;
};

/**
 * struct gcip_event_mgr - GCIP event manager.
 * @ref: Reference count.
 * @count: The dynamically allocated size of the events array.
 * @events_lock: Protects the events array.
 * @events: Array of registered events.
 */
struct gcip_event_mgr {
	struct kref ref;
	size_t count;
	rwlock_t events_lock;
	struct gcip_event events[];
};

/**
 * gcip_event_mgr_create() - Creates a new event manager.
 * @count: Number of eventfds to allocate dynamically.
 *
 * The caller of this function is responsible for cleaning up and putting the reference of the
 * manager by calling gcip_event_mgr_destroy().
 *
 * Return: The pointer to the manager object, or the pointer to a negative errno otherwise.
 */
struct gcip_event_mgr *gcip_event_mgr_create(size_t count);

/**
 * gcip_event_mgr_get() - Increments reference count of the event manager.
 * @mgr: The event manager to increment the reference count.
 *
 * Return: The same event manager.
 */
struct gcip_event_mgr *gcip_event_mgr_get(struct gcip_event_mgr *mgr);

/**
 * gcip_event_mgr_put() - Decrements reference count of the event manager.
 * @mgr: The event manager to decrement the reference count.
 */
void gcip_event_mgr_put(struct gcip_event_mgr *mgr);

/**
 * gcip_event_mgr_destroy() - Clears eventfd contexts and put the references of the manager.
 * @mgr: The event manager to destroy.
 */
void gcip_event_mgr_destroy(struct gcip_event_mgr *mgr);

/**
 * gcip_event_mgr_set() - Registers an eventfd descriptor.
 * @mgr: The event manager.
 * @event_id: Index of the event.
 * @eventfd: The eventfd descriptor.
 * @owner: An IP-designed pointer to specify the owner of the event.
 *
 * The @owner pointer is defined by the caller.
 * It will be used to identify the event when gcip_event_mgr_unset_by_owner() is called.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int gcip_event_mgr_set(struct gcip_event_mgr *mgr, size_t event_id, int eventfd, void *owner);

/**
 * gcip_event_mgr_unset() - Unregisters an eventfd descriptor.
 * @mgr: The event manager.
 * @event_id: Index of the event.
 */
void gcip_event_mgr_unset(struct gcip_event_mgr *mgr, size_t event_id);

/**
 * gcip_event_mgr_unset_by_owner() - Unsets all the eventfd contexts registered by the owner.
 * @mgr: The event manager.
 * @owner: Owner of the events to clean up.
 *
 * This function simplifies cleanup for the owner by removing the need to track the registration
 * status of individual event IDs and unset them one by one. This is particularly useful
 * when the owner's lifecycle is shorter than that of the event manager.
 */
void gcip_event_mgr_unset_by_owner(struct gcip_event_mgr *mgr, void *owner);

/**
 * gcip_event_mgr_signal() - Signals the eventfd registered at event_id.
 * @mgr: The event manager.
 * @event_id: Index of the event.
 *
 * If the @event_id is out of bounds or has no eventfd registered, this function will be NO-OP.
 *
 * Return: true if the event was signaled, false otherwise.
 */
bool gcip_event_mgr_signal(struct gcip_event_mgr *mgr, size_t event_id);

#endif /* __GCIP_EVENT_H__ */
