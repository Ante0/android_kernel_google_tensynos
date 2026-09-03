// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPCA Operations Queue management.
 *
 * Copyright (C) 2022 Google LLC.
 */

#include "gpca_op_queue.h"

#include <linux/mutex.h>

#include "gpca_internal.h"

static struct gpca_op_context *
gpca_op_queue_pop_front(struct gpca_dev *gpca_dev, bool *queue_empty_after_pop)
{
	struct gpca_op_context *first_node = NULL;

	mutex_lock(&gpca_dev->gpca_op_queue_mutex);
	if (list_empty(&gpca_dev->gpca_op_queue_head))
		goto exit;

	first_node = list_first_entry(&gpca_dev->gpca_op_queue_head,
				      struct gpca_op_context, op_list);
	list_del(&first_node->op_list);
	*queue_empty_after_pop = list_empty(&gpca_dev->gpca_op_queue_head);
exit:
	mutex_unlock(&gpca_dev->gpca_op_queue_mutex);

	return first_node;
}

static void gpca_op_queue_process(struct work_struct *work)
{
	struct gpca_op_context *head_op = NULL;
	int ret = 0;
	struct gpca_dev *gpca_dev = NULL;
	bool queue_empty_after_pop = false;
	void (*cb)(int ret, void *cb_ctx) = NULL;
	void *cb_ctx = NULL;

	if (!work) {
		pr_err("NULL work struct in %s", __func__);
		return;
	}
	gpca_dev = container_of(work, struct gpca_dev, gpca_op_process);
	head_op = gpca_op_queue_pop_front(gpca_dev, &queue_empty_after_pop);
	if (!head_op)
		return;

	cb = head_op->cb;
	cb_ctx = head_op->cb_ctx;
	/* head_op is freed by the op_handler, must not be used post that */
	ret = head_op->op_handler(head_op);

	if (!queue_empty_after_pop)
		queue_work(gpca_dev->gpca_op_wq, &gpca_dev->gpca_op_process);

	if (cb)
		cb(ret, cb_ctx);
	else
		dev_dbg(gpca_dev->dev,
			"No callback function provided for GPCA operation. Return value = %d",
			ret);
}

int gpca_op_queue_init(struct gpca_dev *gpca_dev)
{
	if (!gpca_dev)
		return -EINVAL;

	/**
	 * Ordered worqueue has a max_active=1
	 * which ensures only 1 workitem is active system-wide
	 */
	gpca_dev->gpca_op_wq = alloc_ordered_workqueue("gpca_op_wq", 0);
	if (!gpca_dev->gpca_op_wq)
		return -ENOMEM;
	INIT_WORK(&gpca_dev->gpca_op_process, gpca_op_queue_process);
	INIT_LIST_HEAD(&gpca_dev->gpca_op_queue_head);
	mutex_init(&gpca_dev->gpca_op_queue_mutex);

	return 0;
}

void gpca_op_queue_deinit(struct gpca_dev *gpca_dev)
{
	if (!gpca_dev)
		return;
	destroy_workqueue(gpca_dev->gpca_op_wq);
	gpca_dev->gpca_op_wq = NULL;
}

int gpca_op_queue_push_back(struct gpca_dev *gpca_dev,
			    struct gpca_op_context *op_ctx)
{
	bool first_node = true;

	if (!gpca_dev || !op_ctx)
		return -EINVAL;

	mutex_lock(&gpca_dev->gpca_op_queue_mutex);
	first_node = list_empty(&gpca_dev->gpca_op_queue_head);
	list_add_tail(&op_ctx->op_list, &gpca_dev->gpca_op_queue_head);
	mutex_unlock(&gpca_dev->gpca_op_queue_mutex);
	if (first_node)
		queue_work(gpca_dev->gpca_op_wq, &gpca_dev->gpca_op_process);

	return 0;
}
