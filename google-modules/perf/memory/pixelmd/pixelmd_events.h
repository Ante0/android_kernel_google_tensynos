/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PIXELMD_EVENTS_
#define PIXELMD_EVENTS_

#include "pixelmd_api.h"
#include "pixelmd_client.h"

/*
 * Writes the event to all clients that have 'source' enabled.
 *
 * This function is safe to call from async contexts.
 */
void pixelmd_write_event(enum pixelmd_source source, unsigned long event_code, const void *payload,
			 size_t payload_size);

/*
 * Writes the event to the client. Does nothing if the client doesn't have 'source' enabled.
 *
 * This function is safe to call from async contexts.
 */
void pixelmd_client_write_event(struct pixelmd_client *client, enum pixelmd_source source,
				unsigned long event_code, const void *payload, size_t payload_size);

#endif /* PIXELMD_EVENTS_ */
