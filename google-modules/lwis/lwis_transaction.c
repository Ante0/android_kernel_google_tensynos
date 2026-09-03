// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS Transaction Processor
 *
 * Copyright (c) 2019 Google, LLC
 */

#include "lwis_transaction.h"

#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/dma-fence.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/preempt.h>
#include <linux/slab.h>
#include <linux/sync_file.h>
#include <linux/overflow.h>

#include "lwis_allocator.h"
#include "lwis_bus_manager.h"
#include "lwis_commands.h"
#include "lwis_device.h"
#include "lwis_event.h"
#include "lwis_fence.h"
#include "lwis_io_buffer.h"
#include "lwis_io_entry.h"
#include "lwis_util.h"

#define CREATE_TRACE_POINTS
#include "lwis_trace.h"

/* Triggered event to actual execution of transaction threshold */
#define TRIGGRED_EVENT_EXECUTION_THRESHOLD_MS 5

/* Allow 20us range in usleep */
#define USLEEP_RANGE_DELTA 20

bool lwis_transaction_debug;
module_param(lwis_transaction_debug, bool, 0644);

/*
 * Helper for for_each_lwis_io() to avoid assignments in conditions.
 */
static inline bool lwis_io_iter_state_init(struct lwis_transaction *lwis_transaction,
					   struct lwis_device *default_lwis_dev,
					   struct lwis_io_iter_state *io_iter_state, int i,
					   bool is_batch)
{
	struct lwis_client *lwis_client;

	io_iter_state->device_entry_index = i;
	io_iter_state->is_batch = is_batch;

	if (is_batch) {
		/* Batch Case: Extract current IO entries from array */
		lwis_client =
			(struct lwis_client *)lwis_transaction->device_io_fps[i]->private_data;
		io_iter_state->lwis_dev = lwis_client->lwis_dev;
		io_iter_state->io_entries = lwis_transaction->info.device_io_entries[i].io_entries;
		io_iter_state->num_io_entries =
			lwis_transaction->info.device_io_entries[i].num_io_entries;
	} else {
		/* Single Case: Use defaults */
		io_iter_state->lwis_dev = default_lwis_dev;
		io_iter_state->io_entries = lwis_transaction->info.io_entries;
		io_iter_state->num_io_entries = lwis_transaction->info.num_io_entries;
	}

	return true;
}

/*
 * Iterates the transaction's io_entries or device_io_entries and provides
 * current io_entry array, count, current index and the lwis_device to run the
 * entries on. This allows us to consolidate the logic when handling the
 * different io_entry types.
 * @trans:          The transaction pointer (source of device_io_fps)
 * @def_dev:        The fallback device pointer for the 'else' (single) case
 * @io_iter_state:  [OUTPUT] io_iter_state for the current iteration
 */
#define for_each_lwis_io(trans, def_dev, io_iter_state)                                            \
	int _i = 0;                                                                                \
	bool _is_batch = ((trans)->info.num_device_io_entries > 0);                                \
	int _max = (_is_batch ? (trans)->info.num_device_io_entries : 1);                          \
	for (_i = 0; _i < _max &&                                                                  \
		     lwis_io_iter_state_init(trans, def_dev, &(io_iter_state), _i, _is_batch);     \
	     ++_i)

static struct lwis_transaction_event_list *event_list_find(struct lwis_client *client,
							   int64_t event_id)
{
	struct lwis_transaction_event_list *list;

	hash_for_each_possible(client->transaction_list, list, node, event_id) {
		if (list->event_id == event_id)
			return list;
	}
	return NULL;
}

static struct lwis_transaction_event_list *event_list_create(struct lwis_client *client,
							     int64_t event_id)
{
	struct lwis_transaction_event_list *event_list = lwis_allocator_allocate(
		client->lwis_dev, sizeof(struct lwis_transaction_event_list), GFP_ATOMIC);
	if (!event_list)
		return NULL;

	event_list->event_id = event_id;
	INIT_LIST_HEAD(&event_list->list);
	hash_add(client->transaction_list, &event_list->node, event_id);
	return event_list;
}

static struct lwis_transaction_event_list *event_list_find_or_create(struct lwis_client *client,
								     int64_t event_id)
{
	struct lwis_transaction_event_list *list = event_list_find(client, event_id);

	return (list == NULL) ? event_list_create(client, event_id) : list;
}

static void add_pending_transaction(struct lwis_client *client,
				    struct lwis_transaction *transaction)
{
	hash_add(client->pending_transactions, &transaction->pending_map_node,
		 transaction->info.id);
	lwis_debug_dev_info(client->lwis_dev->dev,
			    "lwis_fence add transaction id %llu to lwis_client pending map",
			    transaction->info.id);
}

static struct lwis_transaction *pending_transaction_peek(struct lwis_client *client,
							 int64_t transaction_id)
{
	struct hlist_node *tmp;
	struct lwis_transaction *transaction;

	hash_for_each_possible_safe(client->pending_transactions, transaction, tmp,
				    pending_map_node, transaction_id) {
		if (transaction->info.id == transaction_id)
			return transaction;
	}
	return NULL;
}

static void save_transaction_to_history(struct lwis_client *client,
					struct lwis_transaction_info *trans_info,
					int64_t process_timestamp, int64_t process_duration_ns)
{
	client->debug_info.transaction_hist[client->debug_info.cur_transaction_hist_idx].info =
		*trans_info;
	client->debug_info.transaction_hist[client->debug_info.cur_transaction_hist_idx]
		.process_timestamp = process_timestamp;
	client->debug_info.transaction_hist[client->debug_info.cur_transaction_hist_idx]
		.process_duration_ns = process_duration_ns;
	client->debug_info.cur_transaction_hist_idx++;
	if (client->debug_info.cur_transaction_hist_idx >= TRANSACTION_DEBUG_HISTORY_SIZE)
		client->debug_info.cur_transaction_hist_idx = 0;
}

void lwis_free_transaction_io_entries(struct lwis_device *lwis_dev, struct lwis_io_entry *entries,
				      int num_io_entries)
{
	int i;

	for (i = 0; i < num_io_entries; ++i) {
		if (entries[i].type == LWIS_IO_ENTRY_WRITE_BATCH ||
		    entries[i].type == LWIS_IO_ENTRY_WRITE_BATCH_V2) {
			lwis_allocator_free(lwis_dev, entries[i].rw_batch.buf);
			entries[i].rw_batch.buf = NULL;
		} else if (entries[i].type == LWIS_IO_ENTRY_WRITE_TO_BUFFER)
			lwis_io_buffer_unmap(&entries[i]);
	}
	lwis_allocator_free(lwis_dev, entries);
}

static void __lwis_transaction_free(struct lwis_client *client, struct lwis_transaction **lwis_tx)
{
	struct lwis_device *lwis_dev = client->lwis_dev;
	struct lwis_fence_pending_signal *pending_fence, *fence_tmp;
	struct lwis_pending_transaction_id *pend_id, *pend_id_tmp;
	struct lwis_transaction *transaction;
	struct lwis_io_iter_state io_iter_state;
	struct lwis_io_bundle *bundle;
	bool free_resources = false;
	struct file **device_io_fps = NULL;
	struct lwis_device_io_entries *device_io_entries = NULL;
	size_t num_device_io_entries = 0;

	if (!lwis_tx || !*lwis_tx)
		return;

	transaction = *lwis_tx;
	bundle = transaction->bundle;

	if (transaction->is_weak_transaction) {
		*lwis_tx = NULL;
		if (transaction->precondition_fence)
			dma_fence_put(transaction->precondition_fence);
		lwis_allocator_free(lwis_dev, transaction);
		return;
	}

	/* Dequeue from process queue to prevent UAF/Double-Free in client flush path */
	list_del_init(&transaction->process_queue_node);

	if (!list_empty(&transaction->completion_fence_list)) {
		list_for_each_entry_safe(pending_fence, fence_tmp,
					 &transaction->completion_fence_list, node) {
			list_del(&pending_fence->node);
			dma_fence_put(pending_fence->fence);
			lwis_allocator_free(lwis_dev, pending_fence);
		}
	}

	list_for_each_entry_safe(pend_id, pend_id_tmp, &transaction->trigger_fences, node) {
		bool safe_to_free = false;

		list_del(&pend_id->node);
		if (pend_id->triggered) {
			/* Callback already finished or currently running. Safe to free. */
			safe_to_free = true;
		} else {
			bool removed;

			spin_unlock(&client->transaction_lock);
			removed = dma_fence_remove_callback(pend_id->fence, &pend_id->fence_cb);
			spin_lock(&client->transaction_lock);
			if (removed || pend_id->triggered) {
				/* Callback removed or already finished. Safe to free. */
				safe_to_free = true;
			} else {
				/* Callback is running. Delegate freeing to callback. */
				pend_id->free_on_trigger = true;
			}
		}

		if (safe_to_free) {
			dma_fence_put(pend_id->fence);
			lwis_client_put(pend_id->owner);
			lwis_allocator_free(lwis_dev, pend_id);
		}
	}

	/* Free up IO entry related memory */
	if (bundle) {
		if (atomic_dec_and_test(&bundle->refcount)) {
			free_resources = true;
			device_io_fps = bundle->device_io_fps;
			device_io_entries = bundle->device_io_entries;
			num_device_io_entries = bundle->num_device_io_entries;
		}
	} else {
		free_resources = true;
		device_io_fps = transaction->device_io_fps;
		device_io_entries = transaction->info.device_io_entries;
		num_device_io_entries = transaction->info.num_device_io_entries;
	}

	if (free_resources) {
		for_each_lwis_io(transaction, lwis_dev, io_iter_state) {
			lwis_free_transaction_io_entries(io_iter_state.lwis_dev,
							 io_iter_state.io_entries,
							 io_iter_state.num_io_entries);
			if (io_iter_state.is_batch)
				fput(device_io_fps[io_iter_state.device_entry_index]);
		}
		if (num_device_io_entries > 0) {
			lwis_allocator_free(lwis_dev, device_io_entries);
			kfree(device_io_fps);
		}
		if (bundle)
			lwis_allocator_free(lwis_dev, bundle);
	}

	transaction->bundle = NULL;
	transaction->device_io_fps = NULL;
	transaction->info.io_entries = NULL;
	transaction->info.device_io_entries = NULL;

	transaction->starting_read_buf = NULL;

	lwis_allocator_free(lwis_dev, transaction->resp);
	*lwis_tx = NULL;
	INIT_HLIST_NODE(&transaction->pending_map_node);
	lwis_allocator_free(lwis_dev, transaction);
}

void lwis_transaction_free(struct lwis_client *client, struct lwis_transaction **lwis_tx)
{
	unsigned long flags;

	if (!client || !lwis_tx || !*lwis_tx)
		return;

	spin_lock_irqsave(&client->transaction_lock, flags);
	__lwis_transaction_free(client, lwis_tx);
	spin_unlock_irqrestore(&client->transaction_lock, flags);
}

static void enable_debug_trace(struct lwis_client *client, struct lwis_transaction *transaction,
			       bool trace_begin)
{
	if (!lwis_transaction_debug)
		return;

	if (strlen(transaction->info.transaction_name) > 0) {
		char trace_name[LWIS_MAX_NAME_STRING_LEN];

		scnprintf(trace_name, LWIS_MAX_NAME_STRING_LEN, "processing:%s",
			  transaction->info.transaction_name);
		if (trace_begin)
			LWIS_ATRACE_BEGIN(client->lwis_dev, trace_name);
		else
			LWIS_ATRACE_END(client->lwis_dev, trace_name);
	}
}

static int process_transaction_io_entries(struct lwis_device *lwis_dev,
					  struct lwis_io_entry *io_entries, int start_idx,
					  int end_idx, bool skip_err, bool run_in_irq_context,
					  int64_t process_timestamp, int *io_entry_index,
					  int64_t *process_duration_ns, uint8_t **read_buf)
{
	int ret = 0;
	const int reg_value_bytewidth = lwis_dev->native_read_value_bitwidth / 8;

	/*
	 * Use write memory barrier at the beginning of I/O entries if the access
	 * protocol allows it.
	 */
	if (lwis_dev->vops.register_io_barrier != NULL) {
		lwis_dev->vops.register_io_barrier(lwis_dev,
						   /*use_read_barrier=*/false,
						   /*use_write_barrier=*/true);
	}

	/*
	 * Some devices like IOREG devices can process the transaction in IRQ context.
	 * In these cases, do not schedule the bus/thread manager for further
	 * optimization.
	 */
	if (!run_in_irq_context)
		lwis_bus_manager_lock_bus(lwis_dev);

	/* Prepare the io_results */
	lwis_io_entry_prepare_results(lwis_dev, io_entries, start_idx, end_idx, *read_buf,
				      /*is_periodic=*/false);

	ret = lwis_io_entries_process(lwis_dev, io_entries, start_idx, end_idx, skip_err);
	if (ret)
		goto post_process;

	for (*io_entry_index = start_idx; *io_entry_index < end_idx; (*io_entry_index)++) {
		struct lwis_io_entry *entry = &io_entries[*io_entry_index];
		*read_buf = lwis_io_entry_result(*read_buf, entry, reg_value_bytewidth,
						 /*is_periodic=*/false);
	}

post_process:
	if (!run_in_irq_context)
		lwis_bus_manager_unlock_bus(lwis_dev);

	if (lwis_transaction_debug)
		*process_duration_ns = ktime_to_ns(lwis_get_time() - process_timestamp);

	/*
	 * Use read memory barrier at the end of I/O entries if the access protocol
	 * allows it.
	 */
	if (lwis_dev->vops.register_io_barrier != NULL) {
		lwis_dev->vops.register_io_barrier(lwis_dev, /*use_read_barrier=*/true,
						   /*use_write_barrier=*/false);
	}
	return ret;
}

static void cancel_transaction(struct lwis_client *client, struct lwis_transaction **lwis_tx,
			       int error_code, struct list_head *pending_events,
			       struct list_head *pending_fences)
{
	struct lwis_device *lwis_dev = client->lwis_dev;
	struct lwis_transaction *transaction = *lwis_tx;
	struct lwis_transaction_info *info = &transaction->info;
	struct lwis_transaction_response_header resp;

	resp.id = info->id;
	resp.error_code = error_code;
	resp.num_entries = 0;
	resp.results_size_bytes = 0;
	resp.completion_index = -1;

	if (transaction->is_weak_transaction) {
		__lwis_transaction_free(client, lwis_tx);
		return;
	}

	if (pending_events) {
		/* Check to make sure an error event has been specified. */
		if (info->emit_error_event_id != LWIS_EVENT_ID_NONE) {
			lwis_pending_event_push(lwis_dev, pending_events, info->emit_error_event_id,
						&resp, sizeof(resp));
		}
	}
	if (pending_fences)
		lwis_pending_fences_move_all(lwis_dev, transaction, pending_fences, error_code);

	__lwis_transaction_free(client, lwis_tx);
}

static int process_transaction(struct lwis_client *client, struct lwis_transaction **lwis_tx,
			       struct list_head *pending_events, struct list_head *pending_fences,
			       bool skip_err, bool check_transaction_limit, bool run_in_irq_context,
			       bool is_cleanup)
{
	int device_io_entry_index = 0, io_entry_index = 0;
	int ret = 0;
	struct lwis_io_entry *entry = NULL;
	struct lwis_device *lwis_dev = client->lwis_dev;
	struct lwis_transaction *transaction = *lwis_tx;
	struct lwis_transaction_info *info = &transaction->info;
	struct lwis_transaction_response_header *resp = transaction->resp;
	size_t resp_size;
	uint8_t *read_buf;
	int64_t process_duration_ns = -1;
	int64_t process_timestamp = -1;
	int64_t triggered_duration_ns = -1;
	int64_t delayed_execution_remaining_us = -1;
	int64_t output_event = LWIS_EVENT_ID_NONE;
	unsigned long flags;
	struct lwis_io_iter_state io_iter_state;

	const int total_entries = transaction->info.num_device_io_entries > 0 ?
					  transaction->remaining_entries_to_process :
					  info->num_io_entries;
	int max_limit = lwis_dev->transaction_process_limit;
	int remaining_entries = transaction->remaining_entries_to_process;
	int current_run_entries;
	int start_idx;
	int end_idx;

	/*
	 * Check flush_state. If a flush started while this transaction was
	 * being queued, we should not proceed. The caller (Queue worker or
	 * Event handler) has already incremented running_tx_count for us.
	 */
	if (!is_cleanup && READ_ONCE(client->flush_state) == FLUSHING) {
		if (atomic_dec_and_test(&client->running_tx_count))
			wake_up(&client->running_tx_wq);
		spin_lock_irqsave(&client->transaction_lock, flags);
		cancel_transaction(client, lwis_tx, -ECANCELED, NULL, pending_fences);
		spin_unlock_irqrestore(&client->transaction_lock, flags);
		return -ECANCELED;
	}

	enable_debug_trace(client, transaction, true);

	/*
	 * Process all the transactions at once if:
	 * 1. the processing device has no limitation on the number of entries
	 * to process per transaction.
	 * 2. the transaction is running in event context.
	 * 3. the transaction is being called as a part of cleanup.
	 * Note: For #2 and #3, this transaction will not be queued for further
	 * processing by any worker thread later and therefore all entries need
	 * to be processed in the same run.
	 */
	if ((lwis_dev->transaction_process_limit <= 0) ||
	    (transaction->info.run_in_event_context || skip_err || !check_transaction_limit) ||
	    (transaction->info.num_device_io_entries > 0)) {
		max_limit = total_entries;
	}

	current_run_entries = (remaining_entries > max_limit) ? max_limit : remaining_entries;
	start_idx = total_entries - remaining_entries;
	end_idx = start_idx + current_run_entries;
	remaining_entries = remaining_entries - current_run_entries;
	delayed_execution_remaining_us =
		(transaction->delayed_execution_timestamp - ktime_to_ns(lwis_get_time())) / 1000;

	/*
	 * If there is still time remaining before this transaction can be executed, sleep for the
	 * remaining duration.
	 */
	if (delayed_execution_remaining_us > 0)
		usleep_range(delayed_execution_remaining_us,
			     delayed_execution_remaining_us + USLEEP_RANGE_DELTA);

	if (lwis_transaction_debug)
		process_timestamp = ktime_to_ns(lwis_get_time());

	if (transaction->info.trigger_event_id != LWIS_EVENT_ID_NONE &&
	    transaction->triggered_event_timestamp != 0) {
		triggered_duration_ns =
			ktime_to_ns(lwis_get_time()) - transaction->triggered_event_timestamp;
		if (ktime_to_ms(triggered_duration_ns) >= TRIGGRED_EVENT_EXECUTION_THRESHOLD_MS)
			dev_warn(client->lwis_dev->dev,
				 "Triggered event id %#llx transaction id %llu, %lldms",
				 transaction->info.trigger_event_id, transaction->info.id,
				 ktime_to_ms(triggered_duration_ns));
	}

	resp_size = sizeof(struct lwis_transaction_response_header) + resp->results_size_bytes;
	read_buf = (uint8_t *)resp + sizeof(struct lwis_transaction_response_header);

	resp->completion_index = -1;

	/*
	 * If the starting read buffer pointer is not null then
	 * use this cached location to correctly
	 * set the read buffer for the current transaction processing run.
	 */
	if (transaction->starting_read_buf)
		read_buf = transaction->starting_read_buf;

	/*
	 * Perform the register IO depending on whether this transaction contains
	 * cross-device IO entries or regular IO entries.
	 */
	for_each_lwis_io(transaction, lwis_dev, io_iter_state) {
		int err_code = READ_ONCE(transaction->resp->error_code);

		/*
		 * Check if the client is flushing or if the transaction was cancelled
		 * asynchronously before executing register I/O.
		 */
		if (READ_ONCE(client->flush_state) == FLUSHING || err_code) {
			ret = err_code ? err_code : -ECANCELED;
			break;
		}
		/*
		 * If this is a multi-device IO entry, we need to check if the child devices
		 * are enabled. If not, we should check if it is OK for the devices to be
		 * disabled in this transaction.
		 */
		if (io_iter_state.is_batch &&
		    (!io_iter_state.lwis_dev->enabled ||
		     io_iter_state.lwis_dev->suspend_count == io_iter_state.lwis_dev->enabled)) {
			if (transaction->info.skip_device_io_entries_if_powered_off) {
				dev_info(
					lwis_dev->dev,
					"Device (%s) is disabled, skipping multi-device IO entry as per transaction setting.",
					io_iter_state.lwis_dev->name);
				continue;
			} else {
				dev_err(lwis_dev->dev,
					"Device (%s) is disabled, but transaction does not allow skipping powered off devices in multi-device IO.",
					io_iter_state.lwis_dev->name);
				ret = -ENODEV;
				break;
			}
		}
		ret = process_transaction_io_entries(
			io_iter_state.lwis_dev, io_iter_state.io_entries,
			io_iter_state.is_batch ? 0 : start_idx,
			io_iter_state.is_batch ?
				io_iter_state.num_io_entries :
				end_idx, /* device_io_entries don't support partial transactions */
			skip_err, run_in_irq_context, process_timestamp, &io_entry_index,
			&process_duration_ns, &read_buf);

		if (ret)
			break;
	}

	if (ret) {
		resp->error_code = ret;
		resp->completion_index = 0;
	} else if (end_idx > start_idx) {
		resp->completion_index = end_idx - 1;
		entry = &info->io_entries[resp->completion_index];
	}

	if (remaining_entries > 0 && ret == 0) {
		/*
		 * If there are remaining entries to be processed in this transaction,
		 * don't delete this transaction and update the current remaining
		 * count of entries in the transaction. Stop processing further
		 * until there are no more remaining entries to be processed
		 * in the transaction.
		 */
		spin_lock_irqsave(&client->transaction_lock, flags);
		transaction->starting_read_buf = read_buf;
		transaction->remaining_entries_to_process = remaining_entries;
		spin_unlock_irqrestore(&client->transaction_lock, flags);
		enable_debug_trace(client, transaction, false);
		/*
		 * DO NOT decrement running_tx_count here!
		 * Leave it incremented so the flush thread waits for us to
		 * finish process_broken_transaction().
		 */
		return ret;
	}

	if (pending_events) {
		output_event =
			resp->error_code ? info->emit_error_event_id : info->emit_success_event_id;
		/* Check to make sure an output event has been specified. */
		if (output_event != LWIS_EVENT_ID_NONE) {
			lwis_pending_event_push(lwis_dev, pending_events, output_event,
						(void *)resp, resp_size);
		}
	} else {
		/* No pending events indicates it's cleanup io_entries. */
		if (entry && resp->error_code) {
			if (transaction->info.num_device_io_entries > 0) {
				dev_err(lwis_dev->dev,
					"Clean-up fails with error code %d, transaction %llu, device_io_entries[%d][%d], entry_type %d",
					resp->error_code, transaction->info.id,
					device_io_entry_index, resp->completion_index, entry->type);
			} else {
				dev_err(lwis_dev->dev,
					"Clean-up fails with error code %d, transaction %llu, io_entries[%d], entry_type %d",
					resp->error_code, transaction->info.id,
					resp->completion_index, entry->type);
			}
		}
	}

	spin_lock_irqsave(&client->transaction_lock, flags);
	transaction->remaining_entries_to_process = remaining_entries;

	if (pending_fences) {
		lwis_pending_fences_move_all(lwis_dev, transaction, pending_fences,
					     resp->error_code);
	}
	save_transaction_to_history(client, info, process_timestamp, process_duration_ns);
	enable_debug_trace(client, transaction, false);

	__lwis_transaction_free(client, lwis_tx);
	spin_unlock_irqrestore(&client->transaction_lock, flags);
	if (!is_cleanup && atomic_dec_and_test(&client->running_tx_count))
		wake_up(&client->running_tx_wq);
	return ret;
}

static bool process_broken_transaction(struct lwis_client *client, struct list_head *pending_events,
				       struct list_head *pending_fences,
				       struct lwis_transaction *transaction)
{
	unsigned long flush_flags;
	struct device *dev = client->lwis_dev->dev;

	/*
	 * Continue the loop if the transaction is complete and deleted or
	 * if the transaction exists but all the entries are processed
	 */
	if (transaction && transaction->remaining_entries_to_process > 0) {
		/*
		 * If the transaction exists and there are entries
		 * remaning to be processed, that would indicate the
		 * transaction processing limit has reached for this
		 * device and we stop processing its queue further.
		 */
		if (lwis_transaction_debug) {
			dev_info(
				dev,
				"Transaction processing limit reached, remaining entries to process %d\n",
				transaction->remaining_entries_to_process);
		}

		/*
		 * Queue the remaining transaction again on the transaction
		 * worker/bus manager worker to be processed again later if
		 * the client is not flushing.
		 * If the client is flushing, cancel the remaining transaction
		 * and delete from the process queue node.
		 */
		spin_lock_irqsave(&client->flush_lock, flush_flags);
		if (client->flush_state == NOT_FLUSHING) {
			if (lwis_transaction_debug)
				dev_info(dev, "Client: NOT_FLUSHING, schedule remaining work");

			list_add(&transaction->process_queue_node,
				 &client->transaction_process_queue);
			lwis_queue_device_worker(client);
		} else {
			if (lwis_transaction_debug)
				dev_info(dev,
					 "Client: FLUSHING, abort remaining transaction ourselves");

			cancel_transaction(client, &transaction, transaction->resp->error_code,
					   pending_events, pending_fences);
		}
		spin_unlock_irqrestore(&client->flush_lock, flush_flags);
		return true;
	}
	return false;
}

void lwis_process_transactions_in_queue(struct lwis_client *client,
					bool process_high_priority_transaction)
{
	unsigned long flags;
	struct list_head pending_events;
	struct list_head pending_fences;
	struct lwis_transaction *transaction, *transaction_tmp;
	bool check_transaction_limit = true;

	INIT_LIST_HEAD(&pending_events);
	INIT_LIST_HEAD(&pending_fences);

	spin_lock_irqsave(&client->transaction_lock, flags);
	list_for_each_entry_safe(transaction, transaction_tmp, &client->transaction_process_queue,
				 process_queue_node) {
		if (READ_ONCE(client->flush_state) == FLUSHING)
			break;

		if (!client->is_enabled && client->lwis_dev->type != DEVICE_TYPE_TOP) {
			if (lwis_transaction_debug) {
				dev_info(
					client->lwis_dev->dev,
					"Client is disabled. Please enable client to process transactions.");
			}
			spin_unlock_irqrestore(&client->transaction_lock, flags);
			return;
		}

		atomic_inc(&client->running_tx_count);
		if (READ_ONCE(client->flush_state) == FLUSHING) {
			if (atomic_dec_and_test(&client->running_tx_count))
				wake_up(&client->running_tx_wq);
			break;
		}
		/*
		 * For all high priority transactions:
		 * 1. Do not process normal priority transactions if
		 * process_high_priority_transaction is set.
		 * Instead, continue looping through the client's queue and
		 * find only high priority transactions to process.
		 * 2. For high priority transactions, do not apply any
		 * transaction limit and process entire transaction in one go.
		 */
		if (process_high_priority_transaction) {
			if (!transaction->info.is_high_priority_transaction) {
				if (atomic_dec_and_test(&client->running_tx_count))
					wake_up(&client->running_tx_wq);
				continue;
			} else {
				check_transaction_limit = false;
				if (lwis_transaction_debug) {
					dev_info(client->lwis_dev->dev,
						 "Start processing high priority transaction.");
				}
			}
		}

		list_del_init(&transaction->process_queue_node);

		if (transaction->resp->error_code) {
			if (atomic_dec_and_test(&client->running_tx_count))
				wake_up(&client->running_tx_wq);
			cancel_transaction(client, &transaction, transaction->resp->error_code,
					   &pending_events, &pending_fences);
		} else {
			spin_unlock_irqrestore(&client->transaction_lock, flags);
			process_transaction(client, &transaction, &pending_events, &pending_fences,
					    /*skip_err=*/false, check_transaction_limit,
					    /*run_in_irq_context=*/false, /*is_cleanup=*/false);

			lwis_pending_events_emit(client->lwis_dev, &pending_events);
			spin_lock_irqsave(&client->transaction_lock, flags);

			/*
			 * If LWIS is processing a broken transaction,
			 * then it needs to stop processing the client's transaction queue further
			 * until the broken transaction is completely processed.
			 */
			if (transaction) {
				process_broken_transaction(client, &pending_events, &pending_fences,
							   transaction);
				if (atomic_dec_and_test(&client->running_tx_count))
					wake_up(&client->running_tx_wq);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&client->transaction_lock, flags);
	lwis_pending_events_emit(client->lwis_dev, &pending_events);
	lwis_fences_pending_signal_emit(client->lwis_dev, &pending_fences);
}

int lwis_transaction_init(struct lwis_client *client)
{
	spin_lock_init(&client->transaction_lock);
	INIT_LIST_HEAD(&client->transaction_process_queue);
	client->transaction_counter = 0;
	hash_init(client->transaction_list);
	hash_init(client->pending_transactions);

	atomic_set(&client->running_tx_count, 0);
	init_waitqueue_head(&client->running_tx_wq);
	return 0;
}

int lwis_transaction_clear(struct lwis_client *client)
{
	int ret;

	ret = lwis_transaction_client_flush(client);
	if (ret) {
		dev_err(client->lwis_dev->dev,
			"Failed to wait for all in-process transactions to complete (%d)\n", ret);
		return ret;
	}
	return 0;
}

static void collect_all_transactions_in_queue_locked(struct lwis_client *client,
						     struct list_head *transaction_queue,
						     struct list_head *local_cancel_list)
{
	struct lwis_transaction *transaction, *transaction_tmp;

	if (!list_empty(transaction_queue)) {
		dev_warn(client->lwis_dev->dev, "Still transaction entries in process queue\n");
		list_for_each_entry_safe(transaction, transaction_tmp, transaction_queue,
					 process_queue_node) {
			list_del_init(&transaction->process_queue_node);

			/*
			 * Invariant Rigor: event_list_node MUST be empty for queued
			 * transactions!
			 */
			if (WARN_ON(!list_empty(&transaction->event_list_node))) {
				pr_err("LWIS BUG: Queued transaction %llu has non-empty/poisoned event_list_node!\n",
				       transaction->info.id);
				list_del_init(&transaction->event_list_node);
			}

			list_add_tail(&transaction->event_list_node, local_cancel_list);
		}
	}
}

int lwis_transaction_client_flush(struct lwis_client *client)
{
	unsigned long flags;
	struct lwis_transaction *transaction, *transaction_tmp;
	int i;
	struct hlist_node *tmp;
	struct lwis_transaction_event_list *it_evt_list;
	struct list_head local_cancel_list;
	struct list_head pending_fences;

	if (!client) {
		pr_err("Client pointer cannot be NULL while flushing transactions.\n");
		return -ENODEV;
	}

	INIT_LIST_HEAD(&local_cancel_list);
	INIT_LIST_HEAD(&pending_fences);

	spin_lock_irqsave(&client->flush_lock, flags);
	client->flush_state = FLUSHING;
	spin_unlock_irqrestore(&client->flush_lock, flags);

	lwis_flush_device_worker(client);

	/* Wait for all ongoing transactions to complete */
	wait_event(client->running_tx_wq, atomic_read(&client->running_tx_count) == 0);

	spin_lock_irqsave(&client->transaction_lock, flags);
	hash_for_each_safe(client->transaction_list, i, tmp, it_evt_list, node) {
		if ((it_evt_list->event_id & 0xFFFF0000FFFFFFFFll) ==
		    LWIS_EVENT_ID_CLIENT_CLEANUP) {
			continue;
		}
		list_for_each_entry_safe(transaction, transaction_tmp, &it_evt_list->list,
					 event_list_node) {
			list_del(&transaction->event_list_node);
			list_del_init(&transaction->process_queue_node);
			list_add_tail(&transaction->event_list_node, &local_cancel_list);
		}
		hash_del(&it_evt_list->node);
		lwis_allocator_free(client->lwis_dev, it_evt_list);
	}
	hash_for_each_safe(client->pending_transactions, i, tmp, transaction, pending_map_node) {
		hash_del(&transaction->pending_map_node);
		list_del_init(&transaction->process_queue_node);
		list_add_tail(&transaction->event_list_node, &local_cancel_list);
	}
	/*
	 * The transaction queue should be empty after canceling all transactions,
	 * but check anyway.
	 */
	collect_all_transactions_in_queue_locked(client, &client->transaction_process_queue,
						 &local_cancel_list);

	spin_unlock_irqrestore(&client->transaction_lock, flags);

	while (!list_empty(&local_cancel_list)) {
		struct lwis_transaction *tx;

		tx = list_first_entry(&local_cancel_list, struct lwis_transaction, event_list_node);
		list_del(&tx->event_list_node);

		spin_lock_irqsave(&client->transaction_lock, flags);
		cancel_transaction(client, &tx, -ECANCELED, NULL, &pending_fences);
		spin_unlock_irqrestore(&client->transaction_lock, flags);
	}

	lwis_fences_pending_signal_emit(client->lwis_dev, &pending_fences);

	spin_lock_irqsave(&client->flush_lock, flags);
	client->flush_state = NOT_FLUSHING;
	spin_unlock_irqrestore(&client->flush_lock, flags);

	return 0;
}

int lwis_transaction_client_cleanup(struct lwis_client *client)
{
	unsigned long flags;
	struct lwis_transaction *transaction, *transaction_tmp;
	struct lwis_transaction_event_list *it_evt_list;
	struct list_head local_cleanup_list;

	if (lwis_transaction_debug)
		dev_info(client->lwis_dev->dev, "transaction client cleanup begin\n");

	INIT_LIST_HEAD(&local_cleanup_list);

	spin_lock_irqsave(&client->transaction_lock, flags);
	it_evt_list = event_list_find(client, LWIS_EVENT_ID_CLIENT_CLEANUP |
						      (int64_t)client->lwis_dev->id
							      << LWIS_EVENT_ID_EVENT_CODE_LEN);
	if (it_evt_list == NULL) {
		spin_unlock_irqrestore(&client->transaction_lock, flags);
		if (lwis_transaction_debug)
			dev_info(client->lwis_dev->dev, "No cleanup event\n");
		return 0;
	}

	list_for_each_entry_safe(transaction, transaction_tmp, &it_evt_list->list,
				 event_list_node) {
		list_del(&transaction->event_list_node);
		list_del_init(&transaction->process_queue_node);
		list_add_tail(&transaction->event_list_node, &local_cleanup_list);
	}
	hash_del(&it_evt_list->node);
	lwis_allocator_free(client->lwis_dev, it_evt_list);

	spin_unlock_irqrestore(&client->transaction_lock, flags);

	while (!list_empty(&local_cleanup_list)) {
		struct lwis_transaction *tx;

		tx = list_first_entry(&local_cleanup_list, struct lwis_transaction,
				      event_list_node);
		list_del(&tx->event_list_node);

		if (!list_empty(&tx->completion_fence_list)) {
			dev_warn(
				client->lwis_dev->dev,
				"Cleanup transaction with id %llu has tailing fences; cleanup transactions should not have tailing fences",
				tx->info.id);
		}

		if (tx->resp->error_code || client->lwis_dev->enabled == 0) {
			if (lwis_transaction_debug) {
				dev_info(client->lwis_dev->dev,
					 "err = %d, enabled = %d cancel_transaction\n",
					 tx->resp->error_code, client->lwis_dev->enabled);
			}
			spin_lock_irqsave(&client->transaction_lock, flags);
			cancel_transaction(client, &tx, -ECANCELED, NULL, NULL);
			spin_unlock_irqrestore(&client->transaction_lock, flags);
		} else {
			if (lwis_transaction_debug)
				dev_info(client->lwis_dev->dev, "process_transaction\n");

			process_transaction(client, &tx,
					    /*pending_events=*/NULL,
					    /*pending_fences=*/NULL,
					    /*skip_err=*/true,
					    /*check_transaction_limit=*/false,
					    /*run_in_irq_context=*/false,
					    /*is_cleanup=*/true);
		}
	}
	return 0;
}

int lwis_trigger_event_add_weak_transaction(struct lwis_client *client, int64_t transaction_id,
					    int64_t event_id, int32_t precondition_fence_fd)
{
	struct lwis_transaction *weak_transaction;
	struct lwis_transaction_event_list *event_list;

	weak_transaction = lwis_allocator_zallocate(client->lwis_dev,
						    sizeof(struct lwis_transaction), GFP_ATOMIC);
	if (!weak_transaction)
		return -ENOMEM;

	weak_transaction->is_weak_transaction = true;
	weak_transaction->id = transaction_id;
	weak_transaction->precondition_fence = NULL;
	INIT_LIST_HEAD(&weak_transaction->process_queue_node);
	if (precondition_fence_fd >= 0) {
		struct dma_fence *fence = sync_file_get_fence(precondition_fence_fd);
		if (IS_ERR_OR_NULL(fence)) {
			dev_err(client->lwis_dev->dev, "Unable to get fence with fd=%d",
				precondition_fence_fd);
			lwis_allocator_free(client->lwis_dev, weak_transaction);
			return -EBADF;
		}
		weak_transaction->precondition_fence = fence;
	}

	event_list = event_list_find_or_create(client, event_id);
	if (!event_list) {
		dev_err(client->lwis_dev->dev, "Cannot create transaction event list\n");
		if (weak_transaction->precondition_fence)
			dma_fence_put(weak_transaction->precondition_fence);
		lwis_allocator_free(client->lwis_dev, weak_transaction);
		return -EINVAL;
	}
	list_add_tail(&weak_transaction->event_list_node, &event_list->list);
	lwis_debug_dev_info(
		client->lwis_dev->dev,
		"lwis_fence add weak transaction for event id-%lld triggered transaction id %llu",
		event_id, transaction_id);
	return 0;
}

static int check_transaction_param_locked(struct lwis_client *client,
					  struct lwis_transaction *transaction,
					  bool is_level_triggered)
{
	struct lwis_device_event_state *event_state;
	struct lwis_transaction_info *info = &transaction->info;
	struct lwis_device *lwis_dev = client->lwis_dev;

	if (!client) {
		pr_err("Client is NULL while checking transaction parameter.\n");
		return -ENODEV;
	}

	if (!transaction) {
		dev_err(lwis_dev->dev, "Transaction is NULL.\n");
		return -ENODEV;
	}

	info->id = client->transaction_counter++;

	info->current_trigger_event_counter = -1LL;

	/* Look for the trigger event state, if specified. */
	if (info->trigger_event_id != LWIS_EVENT_ID_NONE) {
		unsigned long dev_flags;
		uint64_t current_counter = 0;
		bool event_found = false;

		spin_lock_irqsave(&lwis_dev->lock, dev_flags);
		event_state = lwis_device_event_state_find_locked(lwis_dev, info->trigger_event_id);
		if (event_state != NULL) {
			current_counter = event_state->event_counter;
			event_found = true;
		}
		spin_unlock_irqrestore(&lwis_dev->lock, dev_flags);

		if (!event_found) {
			/* Event has not been encountered, setting event counter to zero. */
			info->current_trigger_event_counter = 0;
		} else {
			/* Event found, return current counter to userspace. */
			info->current_trigger_event_counter = current_counter;
		}
	} else if (!lwis_triggered_by_condition(transaction)) {
		/* Otherwise it's an immediate transaction. */
		transaction->queue_immediately = true;
	}

	/* Both trigger event ID and counter are defined. */
	if (info->trigger_event_id != LWIS_EVENT_ID_NONE &&
	    EXPLICIT_EVENT_COUNTER(info->trigger_event_counter)) {
		/* Check if event has happened already. */
		if (info->trigger_event_counter == info->current_trigger_event_counter) {
			if (is_level_triggered) {
				/* Convert this transaction into an immediate one. */
				transaction->queue_immediately = true;
			} else {
				return -ENOENT;
			}
		} else if (info->trigger_event_counter < info->current_trigger_event_counter) {
			return -ENOENT;
		}
	}

	/* Make sure either an output success/error event OR a completion fence is specified */
	if ((info->emit_success_event_id == LWIS_EVENT_ID_NONE ||
	     info->emit_error_event_id == LWIS_EVENT_ID_NONE) &&
	    info->create_completion_fence_fd == LWIS_NO_COMPLETION_FENCE &&
	    info->num_completion_fences == 0) {
		dev_err(lwis_dev->dev,
			"No transaction events or completion fence specified for transaction");
		return -EINVAL;
	}

	/* Make sure sw events exist in event table. */
	if (info->emit_success_event_id != LWIS_EVENT_ID_NONE) {
		if (IS_ERR_OR_NULL(lwis_device_event_state_find_or_create(
			    lwis_dev, info->emit_success_event_id)) ||
		    IS_ERR_OR_NULL(lwis_client_event_state_find_or_create(
			    client, info->emit_success_event_id))) {
			dev_err(lwis_dev->dev, "Cannot create success event for transaction");
			return -EINVAL;
		}
	}
	if (info->emit_error_event_id != LWIS_EVENT_ID_NONE) {
		if (IS_ERR_OR_NULL(lwis_device_event_state_find_or_create(
			    lwis_dev, info->emit_error_event_id)) ||
		    IS_ERR_OR_NULL(lwis_client_event_state_find_or_create(
			    client, info->emit_error_event_id))) {
			dev_err(lwis_dev->dev, "Cannot create error event for transaction");
			return -EINVAL;
		}
	}

	return 0;
}

static int prepare_transaction_fences_locked(struct lwis_client *client,
					     struct lwis_transaction *transaction)
{
	int ret = 0;

	if (lwis_triggered_by_condition(transaction)) {
		ret = lwis_parse_trigger_condition(client, transaction);
		if (ret)
			return ret;
	}

	/* If transaction contains completion fences, add them to the transaction. */
	ret = lwis_add_completion_fences_to_transaction(client, transaction);

	return ret;
}

static int prepare_io_results(struct lwis_client *client, struct lwis_io_entry *io_entries,
			      int num_io_entries, size_t *read_buf_size, int *num_read_results)
{
	int i;
	const int reg_value_bytewidth = client->lwis_dev->native_read_value_bitwidth / 8;

	for (i = 0; i < num_io_entries; ++i) {
		struct lwis_io_entry *entry = &io_entries[i];

		if (entry->type == LWIS_IO_ENTRY_READ || entry->type == LWIS_IO_ENTRY_READ_V2 ||
		    entry->type == LWIS_IO_ENTRY_READ_BATCH ||
		    entry->type == LWIS_IO_ENTRY_READ_BATCH_V2) {
			size_t size_in_bytes;

			if (entry->type == LWIS_IO_ENTRY_READ ||
			    entry->type == LWIS_IO_ENTRY_READ_V2)
				size_in_bytes = reg_value_bytewidth;
			else
				size_in_bytes = entry->rw_batch.size_in_bytes;

			if (check_add_overflow(*read_buf_size, size_in_bytes, read_buf_size))
				return -EOVERFLOW;
			(*num_read_results)++;
		}
	}
	return 0;
}

int lwis_transaction_prepare_response(struct lwis_client *client,
				      struct lwis_transaction *transaction)
{
	struct lwis_transaction_info *info = &transaction->info;
	size_t resp_size;
	size_t read_buf_size = 0;
	int read_entries = 0;
	struct lwis_io_iter_state io_iter_state;
	size_t result_size, temp_size;
	int ret;

	/* Transactions will support either device io_entries or regular io_entries,
	 * but we will never expect both in the same transaction. Here we will prepare
	 * the response for one of these io_entry types.
	 */
	for_each_lwis_io(transaction, client->lwis_dev, io_iter_state) {
		ret = prepare_io_results(client, io_iter_state.io_entries,
					 io_iter_state.num_io_entries, &read_buf_size,
					 &read_entries);
		if (ret)
			return ret;
	}

	if (check_mul_overflow(read_entries, sizeof(struct lwis_io_result), &result_size) ||
	    check_add_overflow(sizeof(struct lwis_transaction_response_header), result_size,
			       &temp_size) ||
	    check_add_overflow(temp_size, read_buf_size, &resp_size)) {
		return -EOVERFLOW;
	}

	transaction->resp = lwis_allocator_allocate(client->lwis_dev, resp_size, GFP_KERNEL);
	if (!transaction->resp)
		return -ENOMEM;

	transaction->resp->id = info->id;
	transaction->resp->error_code = 0;
	transaction->resp->completion_index = 0;
	transaction->resp->num_entries = read_entries;
	transaction->resp->results_size_bytes =
		read_entries * sizeof(struct lwis_io_result) + read_buf_size;
	return 0;
}

static int add_transaction_to_queue_locked(struct lwis_client *client,
					   struct lwis_transaction *transaction)
{
	int ret;
	struct lwis_transaction_info *info = &transaction->info;

	if (info->is_high_priority_transaction) {
		list_add(&transaction->process_queue_node, &client->transaction_process_queue);
		ret = lwis_bus_manager_add_high_priority_client(client);
		if (ret) {
			dev_err(client->lwis_dev->dev, "Failed to add high priority transaction");
			list_del_init(&transaction->process_queue_node);
			return ret;
		}
	} else {
		list_add_tail(&transaction->process_queue_node, &client->transaction_process_queue);
	}
	return 0;
}

/* Calling this function requires holding the client's transaction_lock. */
static int queue_transaction_locked(struct lwis_client *client,
				    struct lwis_transaction *transaction)
{
	struct lwis_transaction_event_list *event_list;
	struct lwis_transaction_info *info = &transaction->info;

	int ret;

	if (transaction->queue_immediately) {
		ret = add_transaction_to_queue_locked(client, transaction);
		if (ret) {
			lwis_allocator_free(client->lwis_dev, transaction->resp);
			transaction->resp = NULL;
			return ret;
		}
		lwis_queue_device_worker(client);
	} else if (lwis_triggered_by_condition(transaction)) {
		add_pending_transaction(client, transaction);
	} else {
		event_list = event_list_find_or_create(client, info->trigger_event_id);
		if (!event_list) {
			dev_err(client->lwis_dev->dev, "Cannot create transaction event list\n");
			lwis_allocator_free(client->lwis_dev, transaction->resp);
			transaction->resp = NULL;
			return -EINVAL;
		}
		list_add_tail(&transaction->event_list_node, &event_list->list);
	}
	info->submission_timestamp_ns = ktime_to_ns(lwis_get_time());

	/* Set the trigger delay if one has been specified */
	if (info->minimum_trigger_delay_ns > 0) {
		transaction->delayed_execution_timestamp =
			info->submission_timestamp_ns + info->minimum_trigger_delay_ns;
	}
	return 0;
}

int lwis_transaction_submit_locked(struct lwis_client *client, struct lwis_transaction *transaction)
{
	int ret;
	struct lwis_transaction_info *info = &transaction->info;

	ret = check_transaction_param_locked(client, transaction,
					     /*is_level_triggered=*/info->is_level_triggered);
	if (ret)
		return ret;

	transaction->resp->id = info->id;

	ret = prepare_transaction_fences_locked(client, transaction);
	if (ret)
		return ret;

	ret = queue_transaction_locked(client, transaction);
	return ret;
}

static struct lwis_transaction *
new_repeating_transaction_iteration(struct lwis_client *client,
				    struct lwis_transaction *transaction)
{
	struct lwis_transaction *new_instance;
	uint8_t *resp_buf;
	int remaining_entries = 0;
	struct lwis_io_iter_state io_iter_state;

	new_instance = lwis_allocator_zallocate(client->lwis_dev, sizeof(struct lwis_transaction),
						GFP_ATOMIC);
	if (!new_instance)
		return NULL;

	memcpy(&new_instance->info, &transaction->info, sizeof(transaction->info));
	new_instance->device_io_fps = transaction->device_io_fps;
	new_instance->bundle = transaction->bundle;
	if (new_instance->bundle)
		atomic_inc(&new_instance->bundle->refcount);

	resp_buf = lwis_allocator_allocate(client->lwis_dev,
					   sizeof(struct lwis_transaction_response_header) +
						   transaction->resp->results_size_bytes,
					   GFP_ATOMIC);
	if (!resp_buf) {
		if (new_instance->bundle)
			atomic_dec(&new_instance->bundle->refcount);
		lwis_allocator_free(client->lwis_dev, new_instance);
		return NULL;
	}
	memcpy(resp_buf, transaction->resp, sizeof(struct lwis_transaction_response_header));
	new_instance->resp = (struct lwis_transaction_response_header *)resp_buf;

	for_each_lwis_io(transaction, client->lwis_dev, io_iter_state) {
		remaining_entries += io_iter_state.num_io_entries;
	}

	new_instance->is_weak_transaction = transaction->is_weak_transaction;
	new_instance->remaining_entries_to_process = remaining_entries;
	new_instance->starting_read_buf = NULL;

	INIT_LIST_HEAD(&new_instance->event_list_node);
	INIT_LIST_HEAD(&new_instance->process_queue_node);
	INIT_LIST_HEAD(&new_instance->trigger_fences);
	INIT_LIST_HEAD(&new_instance->completion_fence_list);
	INIT_HLIST_NODE(&new_instance->pending_map_node);

	return new_instance;
}

static bool should_process_in_event_context(struct lwis_client *client,
					    struct lwis_transaction *transaction)
{
	struct lwis_io_iter_state io_iter_state;
	int i;
	struct lwis_transaction_info *info = &transaction->info;

	if (!transaction->info.run_in_event_context)
		return false;

	/* I2C/I3C reads/writes can sleep, must always be queued to worker */
	if (client->lwis_dev->type == DEVICE_TYPE_I2C || client->lwis_dev->type == DEVICE_TYPE_I3C)
		return false;

	if (in_hardirq()) {
		/* The trigger delay cannot be executed in IRQ context */
		if (info->minimum_trigger_delay_ns > 0)
			return false;

		/* IO entry poll/wait cannot be executed in IRQ context */
		for_each_lwis_io(transaction, client->lwis_dev, io_iter_state) {
			for (i = 0; i < io_iter_state.num_io_entries; ++i) {
				if (io_iter_state.io_entries[i].type == LWIS_IO_ENTRY_POLL ||
				    io_iter_state.io_entries[i].type == LWIS_IO_ENTRY_POLL_SHORT ||
				    io_iter_state.io_entries[i].type == LWIS_IO_ENTRY_WAIT)
					return false;
			}
		}
	}

	return true;
}

static inline bool check_and_handle_flush_state(struct lwis_client *client)
{
	atomic_inc(&client->running_tx_count);
	if (READ_ONCE(client->flush_state) == FLUSHING) {
		if (atomic_dec_and_test(&client->running_tx_count))
			wake_up(&client->running_tx_wq);
		return true;
	}
	return false;
}

static void free_weak_transaction(struct lwis_client *client, struct lwis_transaction **weak_tx)
{
	struct lwis_transaction *weak_transaction = *weak_tx;

	if (!weak_transaction)
		return;

	list_del_init(&weak_transaction->event_list_node);
	if (weak_transaction->precondition_fence)
		dma_fence_put(weak_transaction->precondition_fence);
	lwis_allocator_free(client->lwis_dev, weak_transaction);
	*weak_tx = NULL;
}

int lwis_transaction_event_trigger(struct lwis_client *client, int64_t event_id,
				   int64_t event_counter, int64_t event_timestamp,
				   struct list_head *pending_events)
{
	unsigned long flags;
	struct lwis_transaction_event_list *event_list;
	struct lwis_transaction *transaction = NULL;
	struct lwis_transaction *weak_transaction = NULL;
	struct lwis_transaction *new_instance, *transaction_tmp;
	int64_t trigger_counter = 0;
	struct list_head pending_fences;
	struct list_head local_process_list;
	int ret;

	INIT_LIST_HEAD(&pending_fences);
	INIT_LIST_HEAD(&local_process_list);

	/* Find event list that matches the trigger event ID. */
	spin_lock_irqsave(&client->transaction_lock, flags);
	if (READ_ONCE(client->is_suspended) || READ_ONCE(client->flush_state) == FLUSHING) {
		spin_unlock_irqrestore(&client->transaction_lock, flags);
		return 0;
	}

	if (event_id & LWIS_OVERFLOW_IRQ_EVENT_FLAG)
		event_id = event_id ^ LWIS_OVERFLOW_IRQ_EVENT_FLAG;

	event_list = event_list_find(client, event_id);
	if (event_list == NULL || list_empty(&event_list->list)) {
		spin_unlock_irqrestore(&client->transaction_lock, flags);
		return 0;
	}

	list_for_each_entry_safe(transaction, transaction_tmp, &event_list->list, event_list_node) {
		/* The trigger event of the transaction happens */
		transaction->triggered_event_timestamp = event_timestamp;
		if (transaction->is_weak_transaction) {
			weak_transaction = transaction;
			transaction = pending_transaction_peek(client, weak_transaction->id);
			if (transaction == NULL) {
				/* It means the transaction is already executed or is canceled. */
				free_weak_transaction(client, &weak_transaction);
				continue;
			}

			if (transaction->resp->error_code) {
				ret = add_transaction_to_queue_locked(client, transaction);
				if (ret) {
					hash_del(&transaction->pending_map_node);
					free_weak_transaction(client, &weak_transaction);
					cancel_transaction(client, &transaction,
							   transaction->resp->error_code,
							   pending_events, &pending_fences);
					spin_unlock_irqrestore(&client->transaction_lock, flags);
					return ret;
				}
				hash_del(&transaction->pending_map_node);
				free_weak_transaction(client, &weak_transaction);
				continue;
			}

			if (!lwis_event_triggered_condition_ready(client->lwis_dev, transaction,
								  weak_transaction, event_id,
								  event_counter))
				continue;

			lwis_debug_dev_info(
				client->lwis_dev->dev,
				"lwis_fence event id-%lld counter-%lld triggered transaction id %llu",
				event_id, event_counter, transaction->info.id);

			if (should_process_in_event_context(client, transaction)) {
				if (check_and_handle_flush_state(client))
					break;
				hash_del(&transaction->pending_map_node);
				list_del_init(&transaction->event_list_node);
				list_add_tail(&transaction->event_list_node, &local_process_list);
			} else {
				hash_del(&transaction->pending_map_node);
				list_del_init(&transaction->event_list_node);
				ret = add_transaction_to_queue_locked(client, transaction);
				if (ret) {
					cancel_transaction(client, &transaction, ret,
							   pending_events, &pending_fences);
					spin_unlock_irqrestore(&client->transaction_lock, flags);
					return ret;
				}
			}
			continue;
		}

		if (transaction->resp->error_code) {
			ret = add_transaction_to_queue_locked(client, transaction);
			if (ret) {
				list_del_init(&transaction->event_list_node);
				cancel_transaction(client, &transaction,
						   transaction->resp->error_code, pending_events,
						   &pending_fences);
				spin_unlock_irqrestore(&client->transaction_lock, flags);
				return ret;
			}

			list_del_init(&transaction->event_list_node);
			continue;
		}

		/*
		 * Compare current event with trigger event counter to make
		 * sure this transaction needs to be executed now.
		 */
		trigger_counter = transaction->info.trigger_event_counter;
		if (trigger_counter == LWIS_EVENT_COUNTER_ON_NEXT_OCCURRENCE ||
		    trigger_counter == event_counter) {
			if (should_process_in_event_context(client, transaction)) {
				if (check_and_handle_flush_state(client))
					break;
				list_del_init(&transaction->event_list_node);
				list_add_tail(&transaction->event_list_node, &local_process_list);
			} else {
				list_del_init(&transaction->event_list_node);
				ret = add_transaction_to_queue_locked(client, transaction);
				if (ret) {
					cancel_transaction(client, &transaction, ret,
							   pending_events, &pending_fences);
					spin_unlock_irqrestore(&client->transaction_lock, flags);
					return ret;
				}
			}
		} else if (trigger_counter == LWIS_EVENT_COUNTER_EVERY_TIME) {
			new_instance = new_repeating_transaction_iteration(client, transaction);
			if (!new_instance) {
				transaction->resp->error_code = -ENOMEM;
				ret = add_transaction_to_queue_locked(client, transaction);
				if (ret) {
					list_del_init(&transaction->event_list_node);
					cancel_transaction(client, &transaction,
							   transaction->resp->error_code,
							   pending_events, &pending_fences);
					spin_unlock_irqrestore(&client->transaction_lock, flags);
					return ret;
				}
				list_del_init(&transaction->event_list_node);
				continue;
			}
			if (READ_ONCE(client->flush_state) == FLUSHING) {
				__lwis_transaction_free(client, &new_instance);
				break;
			}
			if (should_process_in_event_context(client, new_instance)) {
				atomic_inc(&client->running_tx_count);
				list_add_tail(&new_instance->event_list_node, &local_process_list);
			} else {
				ret = add_transaction_to_queue_locked(client, new_instance);
				if (ret) {
					__lwis_transaction_free(client, &new_instance);
					spin_unlock_irqrestore(&client->transaction_lock, flags);
					return ret;
				}
			}
		}
	}

	spin_unlock_irqrestore(&client->transaction_lock, flags);

	/* Process transactions in event context without holding the lock */
	list_for_each_entry_safe(transaction, transaction_tmp, &local_process_list,
				 event_list_node) {
		list_del_init(&transaction->event_list_node);

		process_transaction(client, &transaction, pending_events, &pending_fences,
				    /*skip_err=*/false, /*check_transaction_limit=*/false,
				    /*run_in_irq_context=*/true, /*is_cleanup=*/false);
	}

	if (!list_empty(&client->transaction_process_queue))
		lwis_queue_device_worker(client);

	lwis_fences_pending_signal_emit(client->lwis_dev, &pending_fences);

	return 0;
}

void lwis_transaction_fence_trigger(struct lwis_client *client, struct dma_fence *fence,
				    int64_t transaction_id)
{
	unsigned long flags = 0;
	struct lwis_transaction *transaction;
	int ret;
	LIST_HEAD(pending_events);
	LIST_HEAD(pending_fences);

	spin_lock_irqsave(&client->transaction_lock, flags);
	transaction = pending_transaction_peek(client, transaction_id);
	if (transaction == NULL) {
		/* It means the transaction is already executed or is canceled. */
		lwis_debug_dev_info(
			client->lwis_dev->dev,
			"dma_fence %p did NOT triggered transaction id %llu, seems already triggered",
			fence, transaction_id);
	} else {
		int fence_status = dma_fence_get_status_locked(fence);

		if (lwis_fence_triggered_condition_ready(transaction, fence_status)) {
			hash_del(&transaction->pending_map_node);
			if (fence_status == LWIS_FENCE_STATUS_SUCCESSFULLY_SIGNALED) {
				ret = add_transaction_to_queue_locked(client, transaction);
				if (ret) {
					cancel_transaction(client, &transaction, ret,
							   &pending_events, &pending_fences);
				} else {
					lwis_debug_dev_info(
						client->lwis_dev->dev,
						"dma_fence %p triggered transaction id %llu", fence,
						transaction->info.id);
				}
			} else {
				cancel_transaction(client, &transaction, -ECANCELED,
						   &pending_events, &pending_fences);
			}
		}
	}
	spin_unlock_irqrestore(&client->transaction_lock, flags);

	if (!list_empty(&client->transaction_process_queue))
		lwis_queue_device_worker(client);

	lwis_pending_events_emit(client->lwis_dev, &pending_events);
	lwis_fences_pending_signal_emit(client->lwis_dev, &pending_fences);
}

/*
 * Calling this function requires holding the client's transaction_lock.
 */
static int cancel_waiting_transaction_locked(struct lwis_client *client, int64_t id)
{
	int i;
	struct hlist_node *tmp;
	struct lwis_transaction_event_list *it_evt_list;
	struct lwis_transaction *transaction, *transaction_tmp;

	/* Search transactions triggered by events. */
	hash_for_each_safe(client->transaction_list, i, tmp, it_evt_list, node) {
		list_for_each_entry_safe(transaction, transaction_tmp, &it_evt_list->list,
					 event_list_node) {
			if (transaction->is_weak_transaction)
				continue;
			if (transaction->info.id == id) {
				transaction->resp->error_code = -ECANCELED;
				return 0;
			}
		}
	}

	/* Search transactions triggered by trigger_condition. */
	hash_for_each_possible_safe(client->pending_transactions, transaction, tmp,
				    pending_map_node, id) {
		if (transaction->info.id == id) {
			for (i = 0; i < transaction->info.num_nested_transactions; ++i) {
				if (cancel_waiting_transaction_locked(
					    client, transaction->info.nested_transaction_ids[i])) {
					dev_err(client->lwis_dev->dev,
						"Nested transaction with id %llu not found",
						transaction->info.nested_transaction_ids[i]);
				}
			}
			transaction->resp->error_code = -ECANCELED;
			return 0;
		}
	}

	return -ENOENT;
}

int lwis_transaction_cancel(struct lwis_client *client, int64_t id)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&client->transaction_lock, flags);
	ret = cancel_waiting_transaction_locked(client, id);
	spin_unlock_irqrestore(&client->transaction_lock, flags);

	return ret;
}
