// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS Bus Scheduler
 *
 * Copyright 2023 Google LLC.
 */
#define pr_fmt(fmt) KBUILD_MODNAME "-bus-sched: " fmt

#include "lwis_bus_scheduler.h"
#include "lwis_bus_manager.h"

/*
 * lwis_process_request_queue_is_empty:
 * Checks if the process request queue is empty.
 */
bool lwis_process_request_queue_is_empty(struct lwis_process_queue *process_queue)
{
	return (!process_queue || (process_queue && (process_queue->number_of_nodes == 0)));
}

/*
 * lwis_process_request_queue_initialize:
 * Initializes the process request queue for a given Bus.
 */
void lwis_process_request_queue_initialize(struct lwis_process_queue *process_queue)
{
	process_queue->number_of_nodes = 0;
	INIT_LIST_HEAD(&process_queue->head);
}

/*
 * lwis_process_request_queue_destroy:
 * Frees all the requests in the queue.
 */
void lwis_process_request_queue_destroy(struct lwis_process_queue *process_queue)
{
	struct lwis_process_request *process_request, *process_request_tmp;

	if (!process_queue)
		return;

	if (lwis_process_request_queue_is_empty(process_queue))
		return;

	list_for_each_entry_safe(process_request, process_request_tmp, &process_queue->head,
				 request_node) {
		list_del(&process_request->request_node);
		process_request->requesting_client = NULL;
		kfree(process_request);
		process_queue->number_of_nodes--;
	}
}
