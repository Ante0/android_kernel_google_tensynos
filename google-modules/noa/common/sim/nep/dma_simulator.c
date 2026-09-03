// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/io.h>

#include "dma_simulator.h"
#include "common/core.h"
#include "common/noa_hw_ring.h"

struct dma_transaction {
	bool need_to_copy;
	struct list_head list;
	struct dma_request req;
};

LIST_HEAD(g_reqs);
static DEFINE_SPINLOCK(g_reqs_lock);
DECLARE_WAIT_QUEUE_HEAD(g_dma_waitqueue);

static int thread_function(void *data)
{
	while (!kthread_should_stop()) {
		unsigned long flags;
		struct dma_transaction *trans = NULL;

		if (list_empty(&g_reqs)) {
			wait_event_interruptible(g_dma_waitqueue,
						 kthread_should_stop() || !list_empty(&g_reqs));

			if (kthread_should_stop()) {
				break;
			}
		}

		spin_lock_irqsave(&g_reqs_lock, flags);
		if (!list_empty(&g_reqs)) {
			trans = list_first_entry(&g_reqs, struct dma_transaction, list);
			list_del(&trans->list);
		}
		spin_unlock_irqrestore(&g_reqs_lock, flags);

		if (trans) {
			if (trans->need_to_copy) {
				memcpy((void *)trans->req.destination_address,
				       (void *)trans->req.source_address, trans->req.size);
			}
			trans->req.callback(trans->req.context);
			kfree(trans);
		}
		schedule();
	}

	return 0;
}

static struct task_struct *g_dma_thread;

int start_dma_simulator(void)
{
	int ret;
	g_dma_thread = kthread_run(thread_function, NULL, "noa_dma_simulator");
	if (IS_ERR(g_dma_thread)) {
		printk(KERN_ERR "Error creating kernel thread\n");
		ret = PTR_ERR(g_dma_thread);
		g_dma_thread = NULL;
		return ret;
	}

	printk(KERN_INFO "noa_dma_simulator thread created successfully\n");
	return 0;
}

void stop_dma_simulator(void)
{
	if (!g_dma_thread) {
		return;
	}

	kthread_stop(g_dma_thread);
	g_dma_thread = NULL;
}

int queue_dma_request(const struct dma_request request)
{
	unsigned long flags;
	struct dma_transaction *transaction = kmalloc(sizeof(struct dma_transaction), GFP_KERNEL);
	if (!transaction) {
		return -ENOMEM;
	}
	transaction->need_to_copy = true;
	transaction->req = request;
	spin_lock_irqsave(&g_reqs_lock, flags);
	list_add_tail(&transaction->list, &g_reqs);
	spin_unlock_irqrestore(&g_reqs_lock, flags);

	wake_up_interruptible(&g_dma_waitqueue);
	return 0;
}

int queue_dma_cache_desc_request(const dma_cache_desc_request *req)
{
	int i = 0;
	unsigned long flags;
	struct dma_transaction *transaction = kmalloc(sizeof(struct dma_transaction), GFP_KERNEL);
	if (!transaction) {
		WARN_ON(1);
		return -ENOMEM;
	}

	// As this is merely a simulator, a direct memory copy is performed, and the callback
	// function is deferred for later execution.
	for (i = 0; i < req->num; i++) {
		memcpy((void *)req->unit[i].dst, (void *)req->unit[i].src,
		       req->unit[i].desc_num * req->desc_len);
	}
	transaction->need_to_copy = false;
	transaction->req.context = req->context;
	transaction->req.callback = req->callback;
	spin_lock_irqsave(&g_reqs_lock, flags);
	list_add_tail(&transaction->list, &g_reqs);
	spin_unlock_irqrestore(&g_reqs_lock, flags);

	wake_up_interruptible(&g_dma_waitqueue);
	return 0;
}

int queue_dma_cache_buffer_pool_request(const dma_cache_buffer_pool_request *req)
{
	unsigned long flags;
	struct dma_transaction *transaction = kmalloc(sizeof(struct dma_transaction), GFP_KERNEL);
	if (!transaction) {
		WARN_ON(1);
		return -ENOMEM;
	}

	// As this is merely a simulator, a direct memory copy is performed, and the callback
	// function is deferred for later execution.
	memcpy((void *)req->dst, (void *)req->src,
	       DMA_CACHE_BUFFER_POOL_DESC_NUM * sizeof(noa_buffer_pool_desc));
	transaction->need_to_copy = false;
	transaction->req.context = req->context;
	transaction->req.callback = req->callback;
	spin_lock_irqsave(&g_reqs_lock, flags);
	list_add_tail(&transaction->list, &g_reqs);
	spin_unlock_irqrestore(&g_reqs_lock, flags);

	wake_up_interruptible(&g_dma_waitqueue);
	return 0;
}

int queue_dma_fetch_header_request(const dma_fetch_header_request *req)
{
	int i = 0;
	unsigned long flags;
	struct dma_transaction *transaction = kmalloc(sizeof(struct dma_transaction), GFP_KERNEL);
	if (!transaction) {
		WARN_ON(1);
		return -ENOMEM;
	}

	// As this is merely a simulator, a direct memory copy is performed, and the callback
	// function is deferred for later execution.
	for (i = 0; i < req->num; i++) {
		memcpy((void *)req->unit[i].dst, (void *)req->unit[i].src, NEP_HEADER_FETCH_SIZE);
	}
	transaction->need_to_copy = false;
	transaction->req.context = req->context;
	transaction->req.callback = req->callback;
	spin_lock_irqsave(&g_reqs_lock, flags);
	list_add_tail(&transaction->list, &g_reqs);
	spin_unlock_irqrestore(&g_reqs_lock, flags);

	wake_up_interruptible(&g_dma_waitqueue);
	return 0;
}

int queue_dma_route_data_request(const dma_route_data_request *req)
{
	int i = 0;
	unsigned long flags;
	struct dma_transaction *transaction = kmalloc(sizeof(struct dma_transaction), GFP_KERNEL);
	if (!transaction) {
		WARN_ON(1);
		return -ENOMEM;
	}
	for (i = 0; i < req->num; i++) {
		memcpy((void *)req->unit[i].dst, (void *)req->unit[i].src, req->desc_len);
	}
	sys_io_wr32((uint32_t *)req->src_ring_addr, req->src_ring_tail);
	sys_io_wr32((uint32_t *)req->dst_ring_addr, req->dst_ring_head);

	// As this is merely a simulator, a direct memory copy is performed, and the callback
	// function is deferred for later execution.

	transaction->need_to_copy = false;
	transaction->req.context = req->context;
	transaction->req.callback = req->callback;
	spin_lock_irqsave(&g_reqs_lock, flags);
	list_add_tail(&transaction->list, &g_reqs);
	spin_unlock_irqrestore(&g_reqs_lock, flags);

	wake_up_interruptible(&g_dma_waitqueue);
	return 0;
}

int queue_dma_drop_data_request(const dma_drop_data_request *req)
{
	unsigned long flags;
	struct dma_transaction *transaction = kmalloc(sizeof(struct dma_transaction), GFP_KERNEL);
	if (!transaction) {
		WARN_ON(1);
		return -ENOMEM;
	}

	// As this is merely a simulator, a direct memory copy is performed, and the callback
	// function is deferred for later execution.
	sys_io_wr32((uint32_t *)req->src_ring_addr, req->src_ring_tail);

	transaction->need_to_copy = false;
	transaction->req.context = req->context;
	transaction->req.callback = req->callback;
	spin_lock_irqsave(&g_reqs_lock, flags);
	list_add_tail(&transaction->list, &g_reqs);
	spin_unlock_irqrestore(&g_reqs_lock, flags);

	wake_up_interruptible(&g_dma_waitqueue);
	return 0;
}

int queue_dma_route_forward_pkt_request(const dma_route_forward_pkt_request *req)
{
	int i = 0;
	unsigned long flags;
	struct dma_transaction *transaction = kmalloc(sizeof(struct dma_transaction), GFP_KERNEL);
	if (!transaction) {
		WARN_ON(1);
		return -ENOMEM;
	}

	// As this is merely a simulator, a direct memory copy is performed, and the callback
	// function is deferred for later execution.
	for (i = 0; i < req->num; i++) {
		memcpy((void *)req->forward_unit[i].pkt_dst,
		       (void *)req->forward_unit[i].header_src, req->forward_unit[i].header_len);
		memcpy((void *)(req->forward_unit[i].pkt_dst + req->forward_unit[i].header_len),
		       (void *)req->forward_unit[i].payload_src, req->forward_unit[i].payload_len);
		smp_mb();
		memcpy((void *)req->forward_unit[i].forward_dst,
		       (void *)req->forward_unit[i].forward_desc, req->forward_desc_len);
		smp_mb();
		memcpy((void *)req->forward_unit[i].feedback_dst,
		       (void *)req->forward_unit[i].feedback_desc, NOA_DESC_BASIC_BYTE);
	}
	sys_io_wr32((uint32_t *)req->src_ring_addr, req->src_ring_tail);
	sys_io_wr32((uint32_t *)req->dst_ring_addr, req->dst_ring_head);
	sys_io_wr32((uint32_t *)req->feedback_ring_addr, req->feedback_ring_head);
	sys_io_wr32((uint32_t *)req->buffer_ring_addr, req->buffer_ring_tail);

	transaction->need_to_copy = false;
	transaction->req.context = req->context;
	transaction->req.callback = req->callback;
	spin_lock_irqsave(&g_reqs_lock, flags);
	list_add_tail(&transaction->list, &g_reqs);
	spin_unlock_irqrestore(&g_reqs_lock, flags);

	wake_up_interruptible(&g_dma_waitqueue);
	return 0;
}
