// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA MD VPN TX Debugfs Implementation
 *
 * Copyright 2025 Google LLC.
 */
#include <linux/atomic.h>    // For atomic operation
#include <linux/delay.h>     // For msleep()
#include <linux/freezer.h>   // For try_to_freeze()
#include <linux/kthread.h>   // For kthread operation
#include <linux/list.h>      // For list operation
#include <linux/sched.h>     // For current task_struct
#include <linux/slab.h>      // For kzalloc(), kfree()
#include <linux/skbuff.h>    // For sk_buff operation
#include <linux/spinlock.h>  // For spinlock operation
#include <linux/wait.h>      // For wait_queue_head_t operation

#include "../noa_md.h"
#include "noa_md_debug_skb_pool.h"

#define SKB_POOL_LOW_WATERMARK_PERCENT 10
#define SKB_POOL_HIGH_WATERMARK_PERCENT 90  // Producer aims to fill up to max_capacity

// Forward declaration of static functions
static int skb_pool_producer_thread_func(void *data);
static struct sk_buff *create_dummy_skb(struct skb_pool_manager *pool_mgr);


/**
 * create_dummy_skb() - Create a dummy SKB for the pool.
 * @pool_mgr: Pointer to the SKB pool manager.
 *
 * Creates an SKB of a configured size, fills it with a pattern,
 * and sets up basic control block info.
 *
 * Return:
 * * Pointer to the created sk_buff: On success.
 * * %NULL: On allocation failure.
 */
static struct sk_buff *create_dummy_skb(struct skb_pool_manager *pool_mgr)
{
	struct sk_buff *skb;
	union mtk_data_pkt_info *pkt_info;
	unsigned int size = pool_mgr->skb_data_size;
	unsigned int queue_index = pool_mgr->queue_index_for_dummy_skb;

	// Reserve headroom and tailroom, plus space for skb->cb
	// The exact headroom/tailroom requirements might depend on the ultimate consumer.
	// Using NET_IP_ALIGN and NET_SKB_PAD is a common practice.
	skb = alloc_skb(size + NET_IP_ALIGN + NET_SKB_PAD + sizeof(*pkt_info), GFP_KERNEL);
	if (!skb) {
		NOA_MD_ERROR("failed to allocate dummy skb");
		atomic64_inc(&pool_mgr->stats.create_failed_count);
		return NULL;
	}

	// Align IP header
	skb_reserve(skb, NET_IP_ALIGN);

	// Fill in some dummy data
	skb_put(skb, size);
	memset(skb->data, 0xAA, size); // Fill with a pattern

	// Set skb->cb content, as noa_md_vpn_tx_enqueue will use this
	pkt_info = DATA_SKB_CB(skb);
	memset(pkt_info, 0, sizeof(*pkt_info));
	pkt_info->tx.intf_id = 0; // Default interface ID for dummy packet
	pkt_info->tx.network_type = 0;  // Default network type
	pkt_info->tx.q_id = queue_index;
	pkt_info->tx.cnt = 2; // Assume not scatter-gather

	skb->dev = NULL; // No specific device association at creation by the pool

	atomic64_add(skb->truesize, &pool_mgr->stats.create_bytes);
	atomic64_inc(&pool_mgr->stats.create_count);

	return skb;
}

/**
 * skb_pool_producer_thread_func() - Producer thread to create and add SKBs.
 * @data: Pointer to the skb_pool_manager structure.
 *
 * This thread waits if the pool is full and produces SKBs
 * up to the high watermark or max capacity.
 *
 * Return:
 * * %0: On normal exit.
 */
static int skb_pool_producer_thread_func(void *data)
{
	struct skb_pool_manager *pool_mgr = (struct skb_pool_manager *)data;
	struct sk_buff *new_skb;
	unsigned long flags;

	NOA_MD_INFO("producer thread started (PID: %d)", current->pid);

	while (!kthread_should_stop() && !pool_mgr->stop_producer) {
		// Wait if the pool is full
		wait_event_interruptible(pool_mgr->producer_wait_q,
			atomic_read(&pool_mgr->current_count) < pool_mgr->max_capacity ||
			pool_mgr->stop_producer);

		if (pool_mgr->stop_producer || kthread_should_stop()) {
			break;
		}

		// Produce SKBs until high watermark or max capacity is reached
		while (atomic_read(&pool_mgr->current_count) < pool_mgr->max_capacity &&
			   !pool_mgr->stop_producer && !kthread_should_stop()) {

			new_skb = create_dummy_skb(pool_mgr);
			if (!new_skb) {
				// Failed to create SKB, maybe sleep and retry?
				NOA_MD_ERROR(
					"producer failed to create SKB, retrying shortly");
				msleep(1); // Avoid busy-looping on allocation failure
				continue;
			}

			spin_lock_irqsave(&pool_mgr->lock, flags);
			list_add_tail(&new_skb->list, &pool_mgr->skb_list);
			atomic_inc(&pool_mgr->current_count);
			spin_unlock_irqrestore(&pool_mgr->lock, flags);

			// Wake up waiting consumers
			wake_up_interruptible(&pool_mgr->consumer_wait_q);
		}
		try_to_freeze(); // Check for freezing conditions
	}

	NOA_MD_INFO("producer thread stopping (PID: %d)", current->pid);
	return 0;
}

/**
 * skb_pool_extract_skb() - Extract an SKB from the pool.
 * @pool_mgr: Pointer to the skb_pool_manager structure.
 *
 * Waits for an SKB if the pool is empty (with a timeout).
 * Wakes the producer if the pool level drops below the low watermark.
 *
 * Return:
 * * Pointer to an sk_buff: On success.
 * * %NULL: If the pool is empty after timeout or if stopping.
 */
struct sk_buff *skb_pool_extract_skb(struct skb_pool_manager *pool_mgr)
{
	struct sk_buff *skb = NULL;
	struct list_head *entry;
	unsigned long flags;

	if (unlikely(!pool_mgr)) {
		NOA_MD_ERROR("extract called with NULL pool manager");
		return NULL;
	}

	// Wait if the pool is empty and the producer is still running
	if (wait_event_interruptible_timeout(
		pool_mgr->consumer_wait_q,
		atomic_read(&pool_mgr->current_count) > 0 || pool_mgr->stop_producer,
		msecs_to_jiffies(1)) == 0) { // Timeout after 1ms
		if (atomic_read(&pool_mgr->current_count) == 0 && !pool_mgr->stop_producer) {
			 NOA_MD_INFO("consumer timed out waiting for SKB, pool empty");
			 return NULL;
		}
	}

	if (pool_mgr->stop_producer && atomic_read(&pool_mgr->current_count) == 0) {
		NOA_MD_INFO("producer stopped and pool empty");
		return NULL;
	}

	spin_lock_irqsave(&pool_mgr->lock, flags);
	if (!list_empty(&pool_mgr->skb_list)) {
		entry = pool_mgr->skb_list.next;
		skb = list_entry(entry, struct sk_buff, list);
		list_del(entry);
		atomic_dec(&pool_mgr->current_count);

		atomic64_inc(&pool_mgr->stats.used_count);
		atomic64_add(skb->truesize, &pool_mgr->stats.used_bytes);
	} else {
		// Should not happen if wait_event condition was met, unless race or stop_producer
		if (!pool_mgr->stop_producer) {
			 NOA_MD_ERROR(
				"pool empty after wake-up, count %d.",
				atomic_read(&pool_mgr->current_count));
		}
	}
	spin_unlock_irqrestore(&pool_mgr->lock, flags);

	// Wake producer if count drops to low watermark.
	if (skb && pool_mgr->max_capacity > 0) {  // Avoid division by zero
		if ((atomic_read(&pool_mgr->current_count) * 100 / pool_mgr->max_capacity) <=
			SKB_POOL_LOW_WATERMARK_PERCENT) {
			wake_up_interruptible(&pool_mgr->producer_wait_q);
		}
	}

	return skb;
}

/**
 * skb_pool_init() - Initialize the SKB pool.
 * @pool_mgr_ptr:              Pointer to the skb_pool_manager pointer.
 * @skb_data_size:             Data size for each SKB.
 * @initial_fill_target:       Target number of SKBs to fill initially.
 * @max_pool_capacity:         Maximum number of SKBs in the pool.
 * @queue_index_for_dummy_skb: Queue index for dummy SKB creation.
 *
 * Sets up the pool manager, initializes locks, wait queues,
 * and starts the producer thread.
 *
 * Return:
 * * %0: On success.
 * * %-EINVAL: If arguments are invalid.
 * * %-ENOMEM: If memory allocation fails.
 * * Other negative error codes from kthread_run.
 */
int skb_pool_init(struct skb_pool_manager **pool_mgr_ptr,
				  unsigned int skb_data_size,
				  unsigned int initial_fill_target,
				  unsigned int max_pool_capacity,
				  unsigned int queue_index_for_dummy_skb)
{
	struct skb_pool_manager *pool_mgr;

	NOA_MD_INFO(
		"initializing pool (skb_size:%u, initial_target:%u, max_capacity:%u, q_idx:%u)",
		skb_data_size, initial_fill_target, max_pool_capacity, queue_index_for_dummy_skb);

	if (!pool_mgr_ptr || skb_data_size == 0 || max_pool_capacity == 0) {
		NOA_MD_ERROR("invalid arguments for init");
		return -EINVAL;
	}
	if (initial_fill_target > max_pool_capacity) {
		NOA_MD_INFO("initial fill target (%u) exceeds max capacity (%u), adjusting",
					initial_fill_target, max_pool_capacity);
		initial_fill_target = max_pool_capacity;
	}

	pool_mgr = kzalloc(sizeof(struct skb_pool_manager), GFP_KERNEL);
	if (!pool_mgr) {
		NOA_MD_ERROR("failed to allocate memory for pool manager");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&pool_mgr->skb_list);
	spin_lock_init(&pool_mgr->lock);
	init_waitqueue_head(&pool_mgr->consumer_wait_q);
	init_waitqueue_head(&pool_mgr->producer_wait_q);
	atomic_set(&pool_mgr->current_count, 0);

	pool_mgr->skb_data_size = skb_data_size;
	pool_mgr->queue_index_for_dummy_skb = queue_index_for_dummy_skb;
	pool_mgr->max_capacity = max_pool_capacity;
	pool_mgr->stop_producer = false;
	// Statistics are already zeroed by kzalloc

	pool_mgr->producer_thread = kthread_run(
		skb_pool_producer_thread_func, pool_mgr, "skb_pool_prod");
	if (IS_ERR(pool_mgr->producer_thread)) {
		NOA_MD_ERROR(
			"failed to create producer thread: %ld",
			PTR_ERR(pool_mgr->producer_thread));
		kfree(pool_mgr);
		*pool_mgr_ptr = NULL;
		return PTR_ERR(pool_mgr->producer_thread);
	}

	// Producer thread handles filling. Wake it up to start.
	wake_up_interruptible(&pool_mgr->producer_wait_q);

	*pool_mgr_ptr = pool_mgr;
	NOA_MD_INFO("initialization complete");
	return 0;
}

/**
 * skb_pool_release() - Release the SKB pool and all its resources.
 * @pool_mgr: Pointer to the skb_pool_manager to release.
 *
 * Stops the producer thread, frees all SKBs in the pool,
 * and frees the pool manager itself. Logs pool statistics.
 */
void skb_pool_release(struct skb_pool_manager *pool_mgr)
{
	struct sk_buff *skb;
	struct list_head *curr, *next;
	unsigned long flags;
	unsigned long long initial_released_count, initial_released_bytes;

	NOA_MD_INFO("releasing pool");
	if (!pool_mgr) {
		NOA_MD_ERROR("release called with NULL pool manager");
		return;
	}

	initial_released_count = atomic64_read(&pool_mgr->stats.release_count);
	initial_released_bytes = atomic64_read(&pool_mgr->stats.release_bytes);

	pool_mgr->stop_producer = true;

	if (pool_mgr->producer_thread) {
		NOA_MD_INFO("signaling producer thread to stop");
		// Wake it if it's sleeping because pool is full
		wake_up_interruptible(&pool_mgr->producer_wait_q);
		// Also wake consumers that might be stuck
		wake_up_interruptible(&pool_mgr->consumer_wait_q);

		// Wait for producer thread to exit
		if (!IS_ERR(pool_mgr->producer_thread) && pool_mgr->producer_thread != current) {
			kthread_stop(pool_mgr->producer_thread);
			NOA_MD_INFO("producer thread stopped");
		} else if (IS_ERR(pool_mgr->producer_thread)) {
			NOA_MD_ERROR(
				"producer thread was in error state: %ld",
				PTR_ERR(pool_mgr->producer_thread));
		}
		pool_mgr->producer_thread = NULL;
	}

	NOA_MD_INFO(
		"freeing remaining SKBs. Current count: %d",
		atomic_read(&pool_mgr->current_count));
	spin_lock_irqsave(&pool_mgr->lock, flags);
	list_for_each_safe(curr, next, &pool_mgr->skb_list) {
		skb = list_entry(curr, struct sk_buff, list);
		list_del(curr);  // Remove from our list
		atomic_dec(&pool_mgr->current_count);  // Decrement our count

		atomic64_inc(&pool_mgr->stats.release_count);
		atomic64_add(skb->truesize, &pool_mgr->stats.release_bytes);
		kfree_skb(skb);
	}
	spin_unlock_irqrestore(&pool_mgr->lock, flags);

	NOA_MD_INFO(
		"release stats: created(%llu, %lluB), used(%llu, %lluB), "
		"released(%llu, %lluB), create failed(%llu)",
		atomic64_read(&pool_mgr->stats.create_count),
		atomic64_read(&pool_mgr->stats.create_bytes),
		atomic64_read(&pool_mgr->stats.used_count),
		atomic64_read(&pool_mgr->stats.used_bytes),
		atomic64_read(&pool_mgr->stats.release_count) - initial_released_count,
		atomic64_read(&pool_mgr->stats.release_bytes) - initial_released_bytes,
		atomic64_read(&pool_mgr->stats.create_failed_count));

	kfree(pool_mgr);
	NOA_MD_INFO("release complete");
}