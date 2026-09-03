// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS Fence
 *
 * Copyright (c) 2022 Google, LLC
 */

#include <linux/dev_printk.h>
#include <linux/errno.h>
#include <linux/container_of.h>
#include <linux/err.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/dma-fence.h>
#include <linux/sync_file.h>
#include <linux/types.h>

#include "lwis_commands.h"
#include "lwis_fence.h"
#include "lwis_transaction.h"
#include "lwis_allocator.h"

bool lwis_fence_debug;
module_param(lwis_fence_debug, bool, 0644);

/*
 *  lwis_fence_release: Closing an instance of a LWIS fence
 */
static int lwis_fence_file_release(struct inode *node, struct file *fp)
{
	struct lwis_fence *lwis_fence = fp->private_data;

	dma_fence_put(&lwis_fence->dma_fence);
	return 0;
}

/*
 *  lwis_fence_write_status: Signal fence with the error code from user
 */
static ssize_t lwis_fence_write_status(struct file *fp, const char __user *user_buffer, size_t len,
				       loff_t *offset)
{
	int ret = 0;
	int errno;
	struct lwis_fence *lwis_fence = fp->private_data;

	if (!lwis_fence)
		return -EFAULT;

	if (len != sizeof(errno)) {
		dev_err(lwis_fence->lwis_top_dev->dev,
			"Signal lwis_fence %p with incorrect buffer length\n", lwis_fence);
		return -EINVAL;
	}

	if (copy_from_user(&errno, (void __user *)user_buffer, len)) {
		dev_err(lwis_fence->lwis_top_dev->dev,
			"Failed to copy all the status from user space\n");
		return -EFAULT;
	}

	if (errno > 0 || errno < -MAX_ERRNO) {
		dev_err(lwis_fence->lwis_top_dev->dev,
			"Wrong errno=%d value. lwis_fence must be signaled with errno values.",
			errno);
		return -EINVAL;
	}

	ret = lwis_dma_fence_signal_with_status(&lwis_fence->dma_fence, errno);
	if (ret)
		return ret;

	return len;
}

int lwis_dma_fence_signal_with_status(struct dma_fence *fence, int errno)
{
	if (errno != 0)
		dma_fence_set_error(fence, errno);
	return dma_fence_signal(fence);
}

static const char *lwis_fence_get_driver_name(struct dma_fence *fence)
{
	return "lwis";
}

static const char *lwis_fence_get_timeline_name(struct dma_fence *fence)
{
	return "unbound";
}

static void lwis_dma_fence_release(struct dma_fence *fence)
{
	struct lwis_fence *lwis_fence = container_of(fence, struct lwis_fence, dma_fence);

	lwis_debug_dev_info(lwis_fence->lwis_top_dev->dev, "Releasing lwis_fence %p", lwis_fence);

	kfree(lwis_fence);
}

static struct dma_fence_ops lwis_fence_dma_fence_ops = {
	.use_64bit_seqno = true,
	.get_driver_name = lwis_fence_get_driver_name,
	.get_timeline_name = lwis_fence_get_timeline_name,
	.release = lwis_dma_fence_release,
};

static atomic64_t dma_fence_sequence = ATOMIC64_INIT(0);

static struct lwis_fence *fence_create(struct lwis_device *lwis_dev)
{
	struct lwis_fence *new_fence;

	/* Allocate a new instance of lwis_fence struct */
	new_fence = kzalloc(sizeof(struct lwis_fence), GFP_KERNEL);
	if (!new_fence)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&new_fence->lock);
	new_fence->lwis_top_dev = lwis_dev->top_dev;

	/* Init DMA fence */
	dma_fence_init(&new_fence->dma_fence, &lwis_fence_dma_fence_ops, &new_fence->lock,
		       dma_fence_context_alloc(1), atomic64_inc_return(&dma_fence_sequence));

	return new_fence;
}

static const struct file_operations fence_file_ops = {
	.owner = THIS_MODULE,
	.release = lwis_fence_file_release,
	.write = lwis_fence_write_status,
};

struct lwis_fence_fds lwis_fence_create(struct lwis_device *lwis_dev)
{
	struct lwis_fence *new_fence;
	struct sync_file *sync_file = NULL;
	int fd, signal_fd;
	int ret;

	new_fence = fence_create(lwis_dev);
	if (IS_ERR(new_fence)) {
		return (struct lwis_fence_fds){
			.error = PTR_ERR(new_fence),
			.fd = -1,
			.signal_fd = -1,
		};
	}

	/* Open DMA fence fd for the new fence */
	ret = get_unused_fd_flags(O_CLOEXEC);
	if (ret < 0)
		goto error;

	fd = ret;
	sync_file = sync_file_create(&new_fence->dma_fence);
	if (sync_file == NULL) {
		ret = -ENOMEM;
		goto error_put_fd;
	}

	/* Open LWIS fd for the new fence */
	ret = anon_inode_getfd("lwis_fence_file", &fence_file_ops, new_fence, O_RDWR | O_CLOEXEC);
	if (ret < 0)
		goto error_put_fd;
	signal_fd = ret;

	/* We install the sync_file only after we know there won't be errors. */
	fd_install(fd, sync_file->file);

	lwis_debug_dev_info(lwis_dev->dev, "new lwis_fence=%p created fd=%d signal_fd=%d",
			    new_fence, fd, signal_fd);
	return (struct lwis_fence_fds){
		.error = 0,
		.fd = fd,
		.signal_fd = signal_fd,
	};

error_put_fd:
	put_unused_fd(fd);
	if (sync_file)
		fput(sync_file->file);
error:
	dma_fence_put(&new_fence->dma_fence);
	dev_err(lwis_dev->dev, "Failed to create a new file instance for lwis_fence\n");
	return (struct lwis_fence_fds){
		.error = ret,
		.fd = -1,
		.signal_fd = -1,
	};
}

static int trigger_event_add_transaction(struct lwis_client *client,
					 struct lwis_transaction *transaction,
					 struct lwis_transaction_trigger_event *event)
{
	struct lwis_device *lwis_dev = client->lwis_dev;
	struct lwis_device_event_state *event_state;
	struct lwis_transaction_info *info = &transaction->info;
	int32_t operator_type = info->trigger_condition.operator_type;
	size_t all_signaled = info->trigger_condition.num_nodes;
	int precondition_fence_status = LWIS_FENCE_STATUS_NOT_SIGNALED;

	/* Check if the event has been encountered and if the event counters match. */
	unsigned long dev_flags;
	uint64_t current_event_counter = 0;
	bool event_found = false;

	spin_lock_irqsave(&lwis_dev->lock, dev_flags);
	event_state = lwis_device_event_state_find_locked(lwis_dev, event->id);
	if (event_state != NULL) {
		current_event_counter = event_state->event_counter;
		event_found = true;
	}
	spin_unlock_irqrestore(&lwis_dev->lock, dev_flags);

	if (event_found && transaction->info.is_level_triggered &&
	    EXPLICIT_EVENT_COUNTER(event->counter) && event->counter == current_event_counter) {
		/* The event is currently level triggered, first we need to check if there is a
		 * precondition fence associated with the event.
		 */
		if (event->precondition_fence_fd >= 0) {
			struct dma_fence *fence = sync_file_get_fence(event->precondition_fence_fd);

			if (IS_ERR_OR_NULL(fence)) {
				dev_err(client->lwis_dev->dev, "Unable to get fence with fd=%d\n",
					event->precondition_fence_fd);
				return -EBADF;
			}
			precondition_fence_status =
				(dma_fence_is_signaled(fence) && !fence->error);
			dma_fence_put(fence);
		}
		/* If the event is not triggered by a precondition fence, or the precondition fence
		 * is already signaled, queue the transaction immediately.
		 */
		if (event->precondition_fence_fd < 0 ||
		    precondition_fence_status == LWIS_FENCE_STATUS_SUCCESSFULLY_SIGNALED) {
			/* The event trigger has been satisfied, so we can increase the signal
			 * count.
			 */
			transaction->signaled_count++;
			transaction->queue_immediately =
				operator_type != LWIS_TRIGGER_NODE_OPERATOR_AND ||
				transaction->signaled_count == all_signaled;
			return 0;
		}
	}

	return lwis_trigger_event_add_weak_transaction(client, info->id, event->id,
						       event->precondition_fence_fd);
}

static void fence_signal_transaction_cb(struct dma_fence *dma_fence, struct dma_fence_cb *cb)
{
	struct lwis_pending_transaction_id *pending_transaction =
		container_of(cb, struct lwis_pending_transaction_id, fence_cb);
	struct lwis_client *client = lwis_client_get(pending_transaction->owner);
	unsigned long flags;
	bool free_me = false;
	int64_t tx_id;

	spin_lock_irqsave(&client->transaction_lock, flags);
	pending_transaction->triggered = true;
	tx_id = pending_transaction->id;
	if (pending_transaction->free_on_trigger)
		free_me = true;
	spin_unlock_irqrestore(&client->transaction_lock, flags);

	if (free_me) {
		dma_fence_put(pending_transaction->fence);
		lwis_allocator_free(client->lwis_dev, pending_transaction);
		lwis_client_put(client);
	} else {
		lwis_transaction_fence_trigger(client, dma_fence, tx_id);
	}

	lwis_client_put(client);
}

static int trigger_fence_add_transaction(int fence_fd, struct lwis_client *client,
					 struct lwis_transaction *transaction)
{
	struct dma_fence *fence;
	struct lwis_pending_transaction_id *pending_transaction_id;
	int ret = 0;

	pending_transaction_id = lwis_allocator_allocate(
		client->lwis_dev, sizeof(struct lwis_pending_transaction_id), GFP_ATOMIC);
	if (!pending_transaction_id)
		return -ENOMEM;

	fence = sync_file_get_fence(fence_fd);
	if (IS_ERR_OR_NULL(fence)) {
		lwis_allocator_free(client->lwis_dev, pending_transaction_id);
		dev_err(client->lwis_dev->dev, "Unable to get fence with error fd=%d\n", fence_fd);
		return -EBADF;
	}

	pending_transaction_id->id = transaction->info.id;
	pending_transaction_id->fence = fence;
	pending_transaction_id->owner = lwis_client_get(client);
	pending_transaction_id->triggered = false;
	pending_transaction_id->free_on_trigger = false;

	ret = dma_fence_add_callback(fence, &pending_transaction_id->fence_cb,
				     fence_signal_transaction_cb);
	if (ret == -ENOENT) {
		/* If we are here, the fence was already signaled. */
		int status = fence->error ?
				   fence->error : LWIS_FENCE_STATUS_SUCCESSFULLY_SIGNALED;

		lwis_debug_dev_info(
			client->lwis_dev->dev,
			"lwis_fence=%p fd=%d not added to transaction id %llu, fence already signaled with error code %d\n",
			fence, fence_fd, transaction->info.id, status);

		if (!transaction->info.is_level_triggered) {
			/* If level triggering is disabled, return an error. */
			lwis_client_put(pending_transaction_id->owner);
			lwis_allocator_free(client->lwis_dev, pending_transaction_id);
			dma_fence_put(fence);
			return -EINVAL;
		}

		/* Add it to the list of trigger fences so the transaction put it once it's done
		 * with it. Mark it as triggered since the fence signal event has already occurred.
		 */
		pending_transaction_id->triggered = true;
		list_add(&pending_transaction_id->node, &transaction->trigger_fences);
		/* If the transaction's trigger_condition evaluates to true, queue the
		 * transaction to be executed immediately.
		 */
		if (lwis_fence_triggered_condition_ready(transaction, status)) {
			if (status != LWIS_FENCE_STATUS_SUCCESSFULLY_SIGNALED)
				transaction->resp->error_code = -ECANCELED;
			transaction->queue_immediately = true;
		}
	} else if (ret == 0) {
		list_add(&pending_transaction_id->node, &transaction->trigger_fences);
		lwis_debug_dev_info(
			client->lwis_dev->dev,
			"lwis_fence transaction id %llu added to its trigger fence=%p fd %d ",
			transaction->info.id, fence, fence_fd);
	} else {
		/* Unexpected error from dma_fence_add_callback */
		dev_err(client->lwis_dev->dev, "Error %d from dma_fence_add_callback\n", ret);
		lwis_client_put(pending_transaction_id->owner);
		lwis_allocator_free(client->lwis_dev, pending_transaction_id);
		dma_fence_put(fence);
		return ret;
	}

	return 0;
}

bool lwis_triggered_by_condition(struct lwis_transaction *transaction)
{
	return (transaction->info.trigger_condition.num_nodes > 0);
}

bool lwis_event_triggered_condition_ready(struct lwis_device *lwis_dev,
					  struct lwis_transaction *transaction,
					  struct lwis_transaction *weak_transaction,
					  int64_t event_id, int64_t event_counter)
{
	int32_t operator_type;
	size_t all_signaled;
	struct lwis_transaction_info *info = &transaction->info;
	int i;
	bool is_node_signaled = false;

	operator_type = info->trigger_condition.operator_type;
	all_signaled = info->trigger_condition.num_nodes;

	/*
	 * Three scenarios to consider a node signaled:
	 * 1) Event ID and event counter match,
	 * 2) Event ID match, event counter not specified but precondition fence signaled, or,
	 * 3) Event ID match, event counter and precondition fence not specified.
	 */
	for (i = 0; i < info->trigger_condition.num_nodes; i++) {
		is_node_signaled = false;
		if (info->trigger_condition.trigger_nodes[i].type != LWIS_TRIGGER_EVENT ||
		    info->trigger_condition.trigger_nodes[i].event.id != event_id) {
			continue;
		}

		if (info->trigger_condition.trigger_nodes[i].event.counter == event_counter ||
		    (info->trigger_condition.trigger_nodes[i].event.counter ==
			     LWIS_EVENT_COUNTER_ON_NEXT_OCCURRENCE &&
		     weak_transaction->precondition_fence == NULL)) {
			is_node_signaled = true;
		} else if (info->trigger_condition.trigger_nodes[i].event.counter ==
			   LWIS_EVENT_COUNTER_ON_NEXT_OCCURRENCE) {
			struct dma_fence *fence = weak_transaction->precondition_fence;

			is_node_signaled = (weak_transaction->precondition_fence != NULL &&
					    dma_fence_is_signaled(fence) &&
					    !fence->error);
			lwis_debug_info(
				"TransactionId %lld: event 0x%llx (%lld), precondition fence %d %s signaled",
				info->id, event_id, event_counter,
				info->trigger_condition.trigger_nodes[i].event.precondition_fence_fd,
				is_node_signaled ? "" : "NOT");
		}

		if (is_node_signaled) {
			transaction->signaled_count++;
			list_del(&weak_transaction->event_list_node);
			if (weak_transaction->precondition_fence)
				dma_fence_put(weak_transaction->precondition_fence);
			lwis_allocator_free(lwis_dev, weak_transaction);
			/* The break here assumes that this event ID only appears once in the trigger
			 * expression. Might need to revisit this.
			 */
			break;
		}
	}

	if (i >= info->trigger_condition.num_nodes) {
		/* No event counter is matched */
		return false;
	}

	switch (operator_type) {
	case LWIS_TRIGGER_NODE_OPERATOR_AND:
		return transaction->signaled_count == all_signaled;
	case LWIS_TRIGGER_NODE_OPERATOR_OR:
	case LWIS_TRIGGER_NODE_OPERATOR_NONE:
		return true;
	}

	return false;
}

bool lwis_fence_triggered_condition_ready(struct lwis_transaction *transaction, int fence_status)
{
	int32_t operator_type;
	size_t all_signaled;

	operator_type = transaction->info.trigger_condition.operator_type;
	all_signaled = transaction->info.trigger_condition.num_nodes;

	transaction->signaled_count++;
	if ((operator_type == LWIS_TRIGGER_NODE_OPERATOR_AND ||
	     operator_type == LWIS_TRIGGER_NODE_OPERATOR_OR) &&
	    transaction->signaled_count == all_signaled) {
		return true;
	} else if (operator_type == LWIS_TRIGGER_NODE_OPERATOR_AND &&
		   fence_status != LWIS_FENCE_STATUS_SUCCESSFULLY_SIGNALED) {
		/*
		 * This condition is ready to cancel transaction as long as there is
		 * an error condition from fence with operator type "AND".
		 * No matter whether all condition nodes are signaled.
		 */
		return true;
	} else if (operator_type == LWIS_TRIGGER_NODE_OPERATOR_OR &&
		   fence_status == LWIS_FENCE_STATUS_SUCCESSFULLY_SIGNALED) {
		return true;
	} else if (operator_type == LWIS_TRIGGER_NODE_OPERATOR_NONE) {
		return true;
	}

	return false;
}

int lwis_parse_trigger_condition(struct lwis_client *client, struct lwis_transaction *transaction)
{
	struct lwis_transaction_info *info;
	struct lwis_device *lwis_dev;
	int i, ret;

	if (!transaction || !client)
		return -EINVAL;

	info = &transaction->info;
	lwis_dev = client->lwis_dev;

	if (info->trigger_condition.num_nodes > LWIS_TRIGGER_NODES_MAX_NUM) {
		dev_err(lwis_dev->dev,
			"Trigger condition contains %llu node, more than the limit of %d\n",
			(unsigned long long)info->trigger_condition.num_nodes,
			LWIS_TRIGGER_NODES_MAX_NUM);
		return -EINVAL;
	}

	for (i = 0; i < info->trigger_condition.num_nodes; i++) {
		if (info->trigger_condition.trigger_nodes[i].type == LWIS_TRIGGER_EVENT) {
			ret = trigger_event_add_transaction(
				client, transaction,
				&info->trigger_condition.trigger_nodes[i].event);
		} else {
			ret = trigger_fence_add_transaction(
				info->trigger_condition.trigger_nodes[i].fence_fd, client,
				transaction);
		}
		if (ret)
			return ret;
	}

	return 0;
}

int lwis_initialize_transaction_fences(struct lwis_client *client,
				       struct lwis_transaction *transaction)
{
	struct lwis_transaction_info *info = &transaction->info;
	struct lwis_device *lwis_dev = client->lwis_dev;
	int i;

	/* If triggered by trigger_condition */
	if (lwis_triggered_by_condition(transaction)) {
		/* Initialize all placeholder fences in the trigger_condition */
		for (i = 0; i < info->trigger_condition.num_nodes; i++) {
			struct lwis_fence_fds fence_fds;

			if (info->trigger_condition.trigger_nodes[i].type !=
			    LWIS_TRIGGER_FENCE_PLACEHOLDER) {
				continue;
			}

			fence_fds = lwis_fence_create(lwis_dev);
			if (fence_fds.error != 0)
				return fence_fds.error;
			info->trigger_condition.trigger_nodes[i].fence_fd = fence_fds.fd;
			info->trigger_condition.trigger_nodes[i].fence_signal_fd =
				fence_fds.signal_fd;
		}
	}

	/* Initialize completion fence if one is requested */
	if (info->create_completion_fence_fd == LWIS_CREATE_COMPLETION_FENCE) {
		struct lwis_fence_fds fence_fds = lwis_fence_create(lwis_dev);
		if (fence_fds.error != 0)
			return fence_fds.error;
		info->create_completion_fence_fd = fence_fds.fd;
		info->create_completion_fence_signal_fd = fence_fds.signal_fd;
	}

	return 0;
}

static struct lwis_fence_pending_signal *fence_pending_signal_create(struct lwis_device *lwis_dev,
								     struct dma_fence *fence)
{
	struct lwis_fence_pending_signal *pending_fence_signal = lwis_allocator_allocate(
		lwis_dev, sizeof(struct lwis_fence_pending_signal), GFP_ATOMIC);
	if (!pending_fence_signal)
		return NULL;
	pending_fence_signal->fence = fence;
	pending_fence_signal->pending_status = LWIS_FENCE_STATUS_NOT_SIGNALED;
	return pending_fence_signal;
}

/*
 *  add_completion_fence: Adds a single completion fence to the transaction
 */
static int add_completion_fence(struct lwis_client *client, struct lwis_transaction *transaction,
				int fence_fd)
{
	struct dma_fence *fence;
	struct lwis_fence_pending_signal *fence_pending_signal;

	fence = sync_file_get_fence(fence_fd);
	if (IS_ERR_OR_NULL(fence)) {
		dev_err(client->lwis_dev->dev, "Unable to get fence with fd=%d\n", fence_fd);
		return -EBADF;
	}

	fence_pending_signal = fence_pending_signal_create(client->lwis_dev, fence);
	if (fence_pending_signal == NULL) {
		dma_fence_put(fence);
		return -ENOMEM;
	}
	list_add(&fence_pending_signal->node, &transaction->completion_fence_list);
	lwis_debug_dev_info(client->lwis_dev->dev,
			    "lwis_fence transaction id %llu add completion fence=%p fd=%d ",
			    transaction->info.id, fence, fence_fd);
	return 0;
}

int lwis_add_completion_fences_to_transaction(struct lwis_client *client,
					      struct lwis_transaction *transaction)
{
	int ret = 0;
	int i;
	int fence_fd;
	struct lwis_device *lwis_dev = client->lwis_dev;
	struct lwis_transaction_info *info = &transaction->info;

	/* If a completion fence is requested but not initialized, we cannot continue. */
	if (info->create_completion_fence_fd == LWIS_CREATE_COMPLETION_FENCE) {
		dev_err(lwis_dev->dev,
			"Cannot add uninitialized completion fence to transaction\n");
		return -EPERM;
	}
	/* Otherwise, add the created completion fence to the transaction's list. */
	if (info->create_completion_fence_fd >= 0) {
		ret = add_completion_fence(client, transaction, info->create_completion_fence_fd);
		if (ret)
			return ret;
	}
	/* Add each external completion fence to the transaction's completion fence list. */
	for (i = 0; i < info->num_completion_fences; ++i) {
		fence_fd = info->completion_fence_fds[i];
		if (fence_fd < 0) {
			dev_err(lwis_dev->dev, "Invalid external completion fence fd %d\n",
				fence_fd);
			return -EINVAL;
		}
		ret = add_completion_fence(client, transaction, fence_fd);
		if (ret)
			return ret;
	}

	return 0;
}

void lwis_fences_pending_signal_emit(struct lwis_device *lwis_device,
				     struct list_head *pending_fences)
{
	int ret;
	struct lwis_fence_pending_signal *pending_fence, *pending_fence_tmp;

	list_for_each_entry_safe(pending_fence, pending_fence_tmp, pending_fences, node) {
		ret = lwis_dma_fence_signal_with_status(pending_fence->fence,
							pending_fence->pending_status);
		if (ret) {
			dev_err(lwis_device->dev, "Failed signaling fence %p\n",
				pending_fence->fence);
		}
		list_del(&pending_fence->node);
		dma_fence_put(pending_fence->fence);
		lwis_allocator_free(lwis_device, pending_fence);
	}
}

void lwis_pending_fences_move_all(struct lwis_device *lwis_device,
				  struct lwis_transaction *transaction,
				  struct list_head *pending_fences, int error_code)
{
	struct lwis_fence_pending_signal *pending_fence, *temp;

	if (!transaction) {
		dev_err(lwis_device->dev, "%s called with NULL transaction\n", __func__);
		return;
	}

	/* For each fence in transaction's signal list, move to pending_fences for signaling */
	list_for_each_entry_safe(pending_fence, temp, &transaction->completion_fence_list, node) {
		pending_fence->pending_status = error_code;
		list_move_tail(&pending_fence->node, pending_fences);
	}
}
