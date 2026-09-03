// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA MD VPN TX Data Path
 *
 * Copyright 2025 Google LLC.
 */

#include <linux/bug.h>          // For likely(), unlikely()
#include <linux/dma-mapping.h>  // For dma_map_single(), dma_unmap_single()
#include <linux/freezer.h>      // For try_to_freeze()
#include <linux/sched.h>        // For schedule(), cond_resched(), task_struct
#include <linux/workqueue.h>    // For workqueue management
#include <linux/wwan.h>         // For wwan_netdev_drvpriv
#include <linux/slab.h>         // For kmalloc(), kfree(), kcalloc()
#include <net/sch_generic.h>    // For dev_kfree_skb_any()

#include "noa_md.h"
#include "noa_md_tx_data.h"
#include "noa_md_vpn_tx_data.h"
#include "noa_md_trace.h"
#if IS_ENABLED(CONFIG_NOA_VPN_OFFLOAD_SUPPORT)
#include "common/core.h"             // For noa_tx_ipsec_metadata
#include "tether/noa_vpn_manager.h"  // For noa_vpn_manager_handle_ipsec_offload_tx_skb()
#endif


// Static function declarations
static int noa_md_vpn_tx_submit_to_nep(
	struct noa_vpn_tx_queue *vpn_q, struct noa_vpn_tx_skb_entry* entry);
static void noa_md_vpn_tx_send_work_handler(struct work_struct *work);
static void noa_md_vpn_tx_release_work_handler(struct work_struct *work);
static void noa_md_vpn_tx_batch_release_work_func(struct work_struct *work);
static void noa_md_vpn_tx_purge_queue(struct noa_vpn_tx_queue *vpn_q);
static int noa_md_vpn_tx_single_queue_setup(
	struct noa_vpn_tx_queue *vpn_q, unsigned int index,
	unsigned int queue_size, struct device *dev, struct noa_md_tx *parent_tx);
static void noa_md_vpn_tx_single_queue_release(struct noa_vpn_tx_queue *vpn_q);


/**
 * noa_md_vpn_tx_submit_to_nep() - Submit VPN packet to NEP.
 * @vpn_q: Pointer to the VPN queue.
 * @entry: Pointer to the SKB entry with DMA info and metadata.
 *
 * Return:
 * * %0: On success.
 * * Negative error code: On failure.
 */
static int noa_md_vpn_tx_submit_to_nep(
	struct noa_vpn_tx_queue *vpn_q, struct noa_vpn_tx_skb_entry* entry)
{
	// TODO: Implement actual submission logic to NEP.
	return 0;
}

/**
 * noa_md_vpn_tx_send_work_handler() - Send work handler for VPN TX.
 * @work: Pointer to the work_struct.
 *
 * Wakes up the send thread for the VPN queue.
 */
static void noa_md_vpn_tx_send_work_handler(struct work_struct *work) {
	struct noa_vpn_tx_queue *vpn_q =
		container_of(work, struct noa_vpn_tx_queue, send_work);
	wake_up_interruptible(&vpn_q->send_wq);
}

static void noa_md_vpn_tx_release_work_handler(struct work_struct *work) {
	struct noa_vpn_tx_queue *vpn_q =
		container_of(work, struct noa_vpn_tx_queue, release_work);
	wake_up_interruptible(&vpn_q->release_wq);
}

/**
 * noa_md_vpn_tx_enqueue() - Enqueues an SKB into a specific VPN TX queue.
 * @tx:          Pointer to the main NOA MD TX structure.
 * @skb:         Pointer to the sk_buff to enqueue. Ownership is transferred.
 * @queue_index: Index of the target VPN queue.
 *
 * Performs DMA mapping and wakes up the send thread.
 *
 * Return:
 * * %0: On success.
 * * %-EINVAL:  If @skb is NULL, or @queue_index is invalid, or SKB data length is zero.
 * * %-EIO:     If DMA mapping fails.
 * * %-ENOBUFS: If the VPN TX queue is full.
 * * (Note: On failure, the skb is freed by this function.)
 */
int noa_md_vpn_tx_enqueue(
	struct noa_md_tx *tx, struct sk_buff *skb, unsigned int queue_index) {
	unsigned long flags;
	struct noa_vpn_tx_queue *vpn_q;
	struct noa_vpn_tx_skb_entry *skb_entry;
	union mtk_data_pkt_info *src_pkt_info;
	dma_addr_t dma_addr;
	size_t map_len;
	unsigned int current_write_idx;

	// Basic validation
	if (unlikely(!skb)) {
		NOA_MD_TX_ERROR(
			"Invalid skb (id:%u, vpn_queues:0x%p, skb:0x%p)",
			queue_index, tx->vpn_queues, skb);
		return -EINVAL;
	}

	vpn_q = &tx->vpn_queues[queue_index];

	if (unlikely(!vpn_q || !vpn_q->skb_entries || vpn_q->size == 0)) {
		NOA_MD_TX_ERROR(
			"Invalid vpn_q (id:%u, vpn_q:0x%p, skb_entries:0x%p, size:%u, skb:0x%p)",
			queue_index, vpn_q, vpn_q ? vpn_q->skb_entries : NULL,
			vpn_q ? vpn_q->size : 0, skb);
		noa_md_tx_drop_inc(skb);
		dev_kfree_skb_any(skb);
		return -EIO;
	}

	// DMA Mapping
	map_len = skb_headlen(skb);  // Map only the linear part for simplicity
	if (unlikely(map_len == 0)) {
		NOA_MD_TX_ERROR("skb_headlen is 0 (id:%u, map_len:0, dropping skb:0x%p)",
			queue_index, skb);
		noa_md_tx_drop_inc(skb);
		dev_kfree_skb_any(skb);
		return -EINVAL;
	}

	dma_addr = dma_map_single(vpn_q->dev, skb->data, map_len, DMA_TO_DEVICE);
	if (dma_mapping_error(vpn_q->dev, dma_addr)) {
		NOA_MD_TX_ERROR("dma_mapping_error (id:%u, skb:0x%p, dma_addr:0x%llx), error:%d",
			queue_index, skb, dma_addr, dma_mapping_error(vpn_q->dev, dma_addr));
		noa_md_tx_drop_inc(skb);
		dev_kfree_skb_any(skb);
		return -EIO;
	}

	// Get packet info from SKB control block
	src_pkt_info = DATA_SKB_CB(skb);

	spin_lock_irqsave(&vpn_q->lock, flags);

	// Check if queue is full ( leave one empty slot for index distinction )
	if (unlikely(vpn_txq_write_avail(vpn_q) == 0)) {
		unsigned int w_idx = vpn_q->write_idx;
		unsigned int r_idx = vpn_q->release_idx;
		spin_unlock_irqrestore(&vpn_q->lock, flags);
		NOA_MD_TX_ERROR_LIMIT(
			"queue full (id:%u, skb:0x%p, w:%u, r:%u, size:%u)",
			queue_index, skb, w_idx, r_idx, vpn_q->size);
		dma_unmap_single(vpn_q->dev, dma_addr, map_len, DMA_TO_DEVICE);
		noa_md_tx_drop_inc(skb);
		dev_kfree_skb_any(skb);
		return -ENOBUFS;
	}

	current_write_idx = vpn_q->write_idx;

	// Store skb entry info
	skb_entry = &vpn_q->skb_entries[current_write_idx];
	skb_entry->skb = skb;
	skb_entry->dma_addr = dma_addr;
	skb_entry->mapped_len = map_len;
	// Copy packet metadata
	skb_entry->pkt_info = src_pkt_info->tx;

	// Update xfrm_interface_id
#if IS_ENABLED(CONFIG_NOA_VPN_OFFLOAD_SUPPORT)
	noa_vpn_manager_handle_ipsec_offload_tx_skb(skb, &skb_entry->metadata);
#endif

	// Write memory barrier ensures entry data is visible before index update
	dma_wmb();

	// Update write index
	vpn_q->write_idx = vpn_txq_next_idx(vpn_q, current_write_idx);
	spin_unlock_irqrestore(&vpn_q->lock, flags);

	// Wake up the sender thread
	schedule_work(&vpn_q->send_work);

	return 0;
}

/**
 * noa_vpn_tx_dequeue_to_batch_work() - Dequeue packets for batch release work.
 * @vpn_q:          Pointer to the VPN queue.
 * @prepare_count:  Number of packets to prepare for release.
 * @direct_dequeue: If true, dequeue and free directly; otherwise, queue for batch work.
 *
 * Dequeues packets from the release side of the queue and either frees them
 * directly or prepares them for a separate batch release worker thread.
 *
 * Return:
 * * Number of packets successfully prepared/dequeued.
 * * %-ENOMEM: If memory allocation fails.
 */
static int noa_vpn_tx_dequeue_to_batch_work(
	struct noa_vpn_tx_queue *vpn_q,
	unsigned int prepare_count,
	bool direct_dequeue)
{
	unsigned long flags;
	unsigned int pending_count;
	unsigned int max_process_count;
	unsigned int current_processing_idx;
	struct noa_vpn_batch_release_work_item *work_item = NULL;
	unsigned int prepared_count = 0;
	int ret = 0;

	work_item = kmalloc(sizeof(*work_item), GFP_ATOMIC);
	if (!work_item) {
		NOA_MD_TX_ERROR(
			"Failed to allocate work_item (id:%u)",
			vpn_q->id);
		ret = -ENOMEM; // Return error code
		goto error;
	}
	work_item->vpn_q = vpn_q;
	work_item->count = 0;
	work_item->items = NULL;

	spin_lock_irqsave(&vpn_q->lock, flags);

	pending_count = vpn_txq_release_pending(vpn_q);
	// Calculate the maximum count that can be processed
	max_process_count = min(prepare_count, pending_count);

	if (max_process_count == 0) {
		spin_unlock_irqrestore(&vpn_q->lock, flags);
		NOA_MD_TX_ERROR_LIMIT(
			"Dequeue requested but none pending "
			"(id:%u, s:%u, w:%u, r:%u, pending:%u, prepare:%u, "
			"batch_release_threshold_cnt:%u, max_process:%u)",
			vpn_q->id,
			vpn_q->send_idx, vpn_q->write_idx, vpn_q->release_idx,
			pending_count, prepare_count,
			vpn_q->batch_release_threshold_cnt, max_process_count);
		goto error;
	}

	if (!direct_dequeue) {
		// Calculate the maximum count that not reach the threshold
		max_process_count = min(
			max_process_count, vpn_q->batch_release_threshold_cnt);
		if (max_process_count == 0) {
			spin_unlock_irqrestore(&vpn_q->lock, flags);
			NOA_MD_TX_INFO_LIMIT(
				"Dequeue requested but not reach the threshold"
				"(id:%u, s:%u, w:%u, r:%u, pending:%u, prepare:%u, "
				"release_threshold:%u, max_process:%u)",
				vpn_q->id,
				vpn_q->send_idx, vpn_q->write_idx, vpn_q->release_idx,
				pending_count, prepare_count,
				vpn_q->batch_release_threshold_cnt, max_process_count);
			goto error;
		}
	}

	work_item->items = kzalloc(
		sizeof(struct noa_vpn_skb_release_info) * max_process_count,
		GFP_ATOMIC);
	if (!work_item->items) {
		spin_unlock_irqrestore(&vpn_q->lock, flags);
		NOA_MD_TX_ERROR(
			"Failed to allocate items for batch release work items (id:%u, count:%u)",
			vpn_q->id, max_process_count);
		ret = -ENOMEM;  // Return error code
		goto error;
	}

	current_processing_idx = vpn_q->release_idx;
	while (prepared_count < max_process_count) {
		struct noa_vpn_tx_skb_entry *entry = &vpn_q->skb_entries[current_processing_idx];

		if (unlikely(!entry->skb)) {
			NOA_MD_TX_ERROR(
				"NULL SKB during batch preparation "
				"(id:%u, s:%u, w:%u, r:%u, current_processing:%u, "
				"pending:%u, prepare:%u, max_process:%u)",
				vpn_q->id,
				vpn_q->send_idx, vpn_q->write_idx, vpn_q->release_idx,
				current_processing_idx, pending_count,
				prepared_count, max_process_count);
			break;
		}

		work_item->items[prepared_count].skb = entry->skb;
		work_item->items[prepared_count].dma_addr = entry->dma_addr;
		work_item->items[prepared_count].mapped_len = entry->mapped_len;

		entry->skb = NULL;
		entry->dma_addr = 0;
		entry->mapped_len = 0;
		// Clear the entry and prepare skb for freeing
		memset(&entry->pkt_info, 0, sizeof(entry->pkt_info));

		prepared_count++;
		current_processing_idx = vpn_txq_next_idx(vpn_q, current_processing_idx);
	}

	vpn_q->release_idx = current_processing_idx;
	work_item->count = prepared_count;

	spin_unlock_irqrestore(&vpn_q->lock, flags);

	if (work_item->count > 0) {
		if (direct_dequeue) {
			goto direct_dequeue_work;
		}

		INIT_WORK(&work_item->work, noa_md_vpn_tx_batch_release_work_func);
		atomic_inc(&vpn_q->pending_batch_release_work_count);
		ret = queue_work(vpn_q->batch_release_wq, &work_item->work);
		if (!ret) {
			NOA_MD_TX_ERROR(
				"queue_work failed for batch_release_wq (id:%u, ret:%d)",
				vpn_q->id, ret);
			atomic_dec(&vpn_q->pending_batch_release_work_count);
			goto direct_dequeue_work;
		}
	}
	return prepared_count;

direct_dequeue_work:
	for (unsigned int i = 0; i < work_item->count; ++i) {
		if (work_item->items[i].dma_addr && vpn_q->dev) {
			dma_unmap_single(
				vpn_q->dev, work_item->items[i].dma_addr,
				work_item->items[i].mapped_len, DMA_TO_DEVICE);
		}
		if (work_item->items[i].skb) {
			dev_kfree_skb_any(work_item->items[i].skb);
		}
	}
	ret = prepared_count;

error:
	if (work_item->items) {
		kfree(work_item->items);
		work_item->items = NULL;
	}
	if (work_item) {
		kfree(work_item);
		work_item = NULL;
	}
	return ret;
}

/**
 * noa_md_vpn_tx_purge_queue() - Purge all SKBs from a VPN queue.
 * @vpn_q: Pointer to the VPN queue to be purged.
 *
 * Unmaps DMA, frees SKBs, and resets queue indices and counters.
 * Typically called during queue release or shutdown.
 */
static void noa_md_vpn_tx_purge_queue(struct noa_vpn_tx_queue *vpn_q)
{
	unsigned long flags;
	unsigned int current_idx;
	unsigned int end_idx;
	int cleaned_count = 0;
	// Local list to hold skbs to be freed outside the lock
	struct sk_buff_head free_list;

	// Basic validation
	if (unlikely(!vpn_q || !vpn_q->skb_entries || vpn_q->size == 0)) {
		NOA_MD_TX_ERROR("Invalid queue "
			"(q:0x%p, entries:0x%p, size:%u)",
			vpn_q, vpn_q ? vpn_q->skb_entries : NULL, vpn_q ? vpn_q->size : 0);
		return;
	}

	NOA_MD_TX_INFO("enter, id:%u", vpn_q->id);
	skb_queue_head_init(&free_list);

	spin_lock_irqsave(&vpn_q->lock, flags);

	current_idx = vpn_q->release_idx;
	end_idx = vpn_q->write_idx;

	while (current_idx != end_idx) {
		struct noa_vpn_tx_skb_entry *entry = &vpn_q->skb_entries[current_idx];
		struct sk_buff *skb = entry->skb;

		if (likely(skb)) {  // Process non-empty entries
			if (likely(entry->dma_addr)) {
				dma_unmap_single(
					vpn_q->dev, entry->dma_addr,
					entry->mapped_len, DMA_TO_DEVICE);
				entry->dma_addr = 0;
				entry->mapped_len = 0;
			}
			memset(&entry->pkt_info, 0, sizeof(entry->pkt_info));

			entry->skb = NULL;
			skb_queue_tail(&free_list, skb);
			cleaned_count++;
		} else {
			NOA_MD_TX_ERROR("Invalid skb (id:%u, r:%u, w:%u, s:%u)",
				vpn_q->id, current_idx, vpn_q->release_idx,
				vpn_q->write_idx, vpn_q->send_idx);
		}

		current_idx = vpn_txq_next_idx(vpn_q, current_idx);
	}

	// Reset all indices to the beginning
	vpn_q->write_idx = 0;
	vpn_q->send_idx = 0;
	vpn_q->release_idx = 0;
	atomic_set(&vpn_q->pkts_to_release, 0);

	spin_unlock_irqrestore(&vpn_q->lock, flags);

	if (!skb_queue_empty(&free_list)) {
		skb_queue_purge(&free_list);
	}

	NOA_MD_TX_INFO(
		"exit (id:%u, cleaned_count:%d)", vpn_q->id, cleaned_count);
}

/**
 * noa_md_vpn_tx_release_request() - Signal completion of transmitted packets.
 * @tx:          Pointer to the main NOA MD TX structure.
 * @queue_index: Index of the VPN queue where packets completed.
 * @count:       Number of packets completed by NEP for this queue.
 *
 * Increments the pending release counter and wakes the release thread.
 *
 * Return:
 * * %0: On success or if count is 0.
 * * %-EINVAL: If @tx or VPN queues within @tx are not initialized.
 */
int noa_md_vpn_tx_release_request(
	struct noa_md_tx *tx, unsigned int queue_index, unsigned int count)
{
	struct noa_vpn_tx_queue *vpn_q;

	if (unlikely(count == 0)) {
		NOA_MD_TX_ERROR("count is 0 (id:%u)",
			queue_index);
		return 0;
	}

	vpn_q = &tx->vpn_queues[queue_index];
	// Increment pending release count
	atomic_add(count, &vpn_q->pkts_to_release);
	// Wake the release thread
	schedule_work(&vpn_q->release_work);

	return 0;
}

/**
 * noa_md_vpn_tx_batch_release_work_func() - Work function for batch SKB release.
 * @work: Pointer to the work_struct.
 *
 * This function is executed by a worker thread to free a batch of SKBs.
 * It unmaps DMA and frees the SKB memory.
 */
static void noa_md_vpn_tx_batch_release_work_func(struct work_struct *work)
{
	struct noa_vpn_batch_release_work_item *batch_item =
		container_of(work, struct noa_vpn_batch_release_work_item, work);
	struct noa_vpn_tx_queue *vpn_q = batch_item->vpn_q;
	unsigned int total_batch_release_count;
	unsigned int pending_batch_release_work_count;

	for (int i = 0; i < batch_item->count; i++) {
		struct noa_vpn_skb_release_info *item_info = &batch_item->items[i];
		if (likely(item_info->skb)) {
			if (item_info->dma_addr) {
				dma_unmap_single(
					vpn_q->dev, item_info->dma_addr,
					item_info->mapped_len, DMA_TO_DEVICE);
			}
			dev_kfree_skb_any(item_info->skb);
		} else {
			NOA_MD_TX_ERROR("Invalid skb (id:%u, i:%d, skb:0x%p, count:%u)",
				vpn_q->id, i, item_info->skb, batch_item->count);
		}
	}

	total_batch_release_count =
		atomic_add_return(batch_item->count, &vpn_q->total_batch_release_count);

	pending_batch_release_work_count =
		atomic_dec_return(&vpn_q->pending_batch_release_work_count);
	NOA_MD_TX_INFO_LIMIT(
		"Done (id:%u, count:%u, total_batch_release_count:%u, "
		"pending_batch_release_work_count:%d)",
		vpn_q->id, batch_item->count,
		total_batch_release_count, pending_batch_release_work_count);
	kfree(batch_item->items);
	batch_item->items = NULL;
	kfree(batch_item);
}

/**
 * noa_md_vpn_tx_send_thread_func() - Kernel thread to send packets from VPN queue to NEP.
 * @data: Pointer to noa_vpn_thread_data containing queue info.
 *
 * Return:
 * * %0: On normal exit.
 */
int noa_md_vpn_tx_send_thread_func(void *data) {
	struct noa_vpn_thread_data *thread_data =
		(struct noa_vpn_thread_data *)data;
	struct noa_vpn_tx_queue *vpn_q = thread_data->vpn_q;
	unsigned int queue_index = vpn_q->id;
	unsigned int local_send_idx;
	unsigned int local_write_idx;
	unsigned int local_release_idx;
	int ret;

	NOA_MD_TX_INFO("enter (vpn_q: 0x%p, id:%u)", vpn_q, vpn_q->id);

	while (!kthread_should_stop()) {
		ret = wait_event_interruptible(vpn_q->send_wq,
			(vpn_q->send_idx != vpn_q->write_idx) || kthread_should_stop());

		if (unlikely(kthread_should_stop())) {
			NOA_MD_TX_ERROR(
				"kthread_should_stop (id:%u)", queue_index);
			break;
		}
		if (unlikely(ret == -ERESTARTSYS)) {
			NOA_MD_TX_ERROR(
				"signal received (id:%u)", queue_index);
			break;
		}

		// Process available packets
		// Read volatile index once before loop
		local_send_idx = vpn_q->send_idx;
		local_write_idx = vpn_q->write_idx;
		local_release_idx = vpn_q->release_idx;

		while (local_send_idx != local_write_idx) {
			struct noa_vpn_tx_skb_entry *entry;

			if (unlikely(kthread_should_stop())) {
				break;
			}

			entry = &vpn_q->skb_entries[local_send_idx];
			if (unlikely(!entry->skb)) {
				NOA_MD_TX_ERROR_LIMIT(
					"Invalid skb (id:%u, skb: 0x%p, s:%u, w:%u, r:%u)",
					queue_index, entry->skb, local_send_idx,
					local_write_idx, local_release_idx);
				// Skip this enrty
				local_send_idx = vpn_txq_next_idx(vpn_q, local_send_idx);
				spin_lock_bh(&vpn_q->lock);
				vpn_q->send_idx = local_send_idx;
				spin_unlock_bh(&vpn_q->lock);
				break;
			}

			ret = noa_md_vpn_tx_submit_to_nep(vpn_q, entry);
			if (ret == 0) {
				noa_md_tx_inc(entry->skb);
				// Submission successful, advance local index
				local_send_idx = vpn_txq_next_idx(vpn_q, local_send_idx);

				// Update the shared send_idx under lock
				spin_lock_bh(&vpn_q->lock);
				vpn_q->send_idx = local_send_idx;
				spin_unlock_bh(&vpn_q->lock);
			} else {
				NOA_MD_TX_ERROR(
					"submit to nep fail (id:%u, ret:%d, s:%u, w:%u)",
					queue_index, ret, local_send_idx, local_write_idx);
				// TODO: Handle failure (Retry or drop?)
			}
			// Check if write_idx has changed during processing
			local_write_idx = vpn_q->write_idx;
			cond_resched();
		}
		try_to_freeze();
	}

	kfree(thread_data);
	NOA_MD_TX_INFO("exit (id:%u)", queue_index);
	return 0;
}

/**
 * noa_md_vpn_tx_release_thread_func() - Kernel thread to release completed VPN packets.
 * @data: Pointer to noa_vpn_thread_data containing queue info.
 *
 * Return:
 * * %0: On normal exit.
 */
int noa_md_vpn_tx_release_thread_func(void *data)
{
	struct noa_vpn_thread_data *thread_data =
		(struct noa_vpn_thread_data *)data;
	struct noa_vpn_tx_queue *vpn_q = thread_data->vpn_q;
	unsigned int queue_index = vpn_q->id;
	int final_pending;
	int ret;

	NOA_MD_TX_INFO("enter (vpn_q:0x%p, id:%u, thread_data:0x%p)",
		vpn_q, vpn_q->id, thread_data);

	while (!kthread_should_stop()) {
		ret = wait_event_interruptible(vpn_q->release_wq,
			atomic_read(&vpn_q->pkts_to_release) > 0 || kthread_should_stop());
		if (unlikely(kthread_should_stop())) {
			NOA_MD_TX_ERROR(
				" kthread_should_stop (id:%u)", queue_index);
			break;
		}
		if (unlikely(ret == -ERESTARTSYS)) {
			NOA_MD_TX_ERROR(
				"signal received (id:%u)", queue_index);
			break;
		}

		while (atomic_read(&vpn_q->pkts_to_release) > 0 &&
			!kthread_should_stop()) {
			int pkts_to_release = atomic_read(&vpn_q->pkts_to_release);
			int prepared_count;

			prepared_count =
				noa_vpn_tx_dequeue_to_batch_work(vpn_q, pkts_to_release, false);

			if (likely(prepared_count > 0)) {
				int remaining_pkts =
					atomic_sub_return(prepared_count, &vpn_q->pkts_to_release);
				if (unlikely(remaining_pkts < 0)) {
					atomic_set(&vpn_q->pkts_to_release, 0);
					NOA_MD_TX_ERROR(
						"Resetting pkts_to_release to 0 "
						"(id:%u, remaining:%d, prepared:%d)",
						queue_index, remaining_pkts, prepared_count);
					break;
				}
			} else {
				NOA_MD_TX_ERROR_LIMIT(
					"noa_vpn_tx_dequeue_to_batch_work failed "
					"(id:%u, prepared:%u, pkts_to_release: %u)",
					queue_index, prepared_count,
					atomic_read(&vpn_q->pkts_to_release));
				break;
			}
		}
		try_to_freeze();
	}

	// Thread Exit Cleanup
	// Attempt to release any remaining packets signaled before stop request
	final_pending = atomic_read(&vpn_q->pkts_to_release);
	if (final_pending > 0) {
		NOA_MD_TX_INFO("id:%u, final_pending:%d",
			queue_index, final_pending);
		noa_vpn_tx_dequeue_to_batch_work(vpn_q, final_pending, true);
		atomic_set(&vpn_q->pkts_to_release, 0);  // Ensure counter is 0 on exit
	}

	kfree(thread_data);
	NOA_MD_TX_INFO("exit, id:%d", queue_index);
	return 0;
}

/**
 * noa_md_vpn_tx_single_queue_setup() - Setup a single VPN TX queue.
 * @vpn_q:      Pointer to the VPN queue structure to initialize.
 * @index:      Numerical index of this queue.
 * @queue_size: Desired size of the queue (must be a power of 2).
 * @dev:        Pointer to the device structure for DMA operations.
 * @parent_tx:  Pointer to the parent noa_md_tx structure.
 *
 * Initializes threads and data structures for the queue.
 *
 * Return:
 * * %0: On success.
 * * %-EINVAL: If arguments are invalid or queue_size is not power of 2.
 * * %-ENOMEM: If memory allocation fails.
 * * Other negative error codes from kthread_run.
 */
static int noa_md_vpn_tx_single_queue_setup(
	struct noa_vpn_tx_queue *vpn_q, unsigned int index,
	unsigned int queue_size, struct device *dev, struct noa_md_tx *parent_tx)
{
	char thread_name[32];
	char wq_name[32];
	struct noa_vpn_thread_data *thread_data_send = NULL;
	struct noa_vpn_thread_data *thread_data_release = NULL;
	int ret = 0;

	NOA_MD_TX_INFO("enter (vpn_q:0x%p, id:%d, size:%d)",
		vpn_q, index, queue_size);

	if (!vpn_q || !dev || queue_size == 0 || !parent_tx) {
		NOA_MD_TX_ERROR("Invalid arguments (id:%d, dev:0x%p, parent_tx:0x%p)",
			index, dev, parent_tx);
		return -EINVAL;
	}

	if (!is_power_of_2(queue_size)) {
		NOA_MD_TX_ERROR("must be a power of 2 (id:%d)", index);
		return -EINVAL;
	}

	spin_lock_init(&vpn_q->lock);
	vpn_q->write_idx = 0;
	vpn_q->send_idx = 0;
	vpn_q->release_idx = 0;
	atomic_set(&vpn_q->pkts_to_release, 0);
	vpn_q->size = queue_size;
	vpn_q->mask = queue_size - 1;
	vpn_q->parent_tx = parent_tx;
	vpn_q->dev = dev;
	vpn_q->id = index;
	vpn_q->batch_release_threshold_cnt = NOA_MD_VPN_TX_BATCH_RELEASE_THRESHOLD_CNT;

	vpn_q->skb_entries = kcalloc(
		queue_size, sizeof(struct noa_vpn_tx_skb_entry), GFP_KERNEL);
	if (unlikely(!vpn_q->skb_entries)) {
		NOA_MD_TX_ERROR(
			"Failed to allocate skb_entries (id:%d, size:%u, alloc size:%u)",
			index, queue_size, queue_size*sizeof(struct noa_vpn_tx_skb_entry));
		return -ENOMEM;
	}

	init_waitqueue_head(&vpn_q->send_wq);
	init_waitqueue_head(&vpn_q->release_wq);

	// Allocate thread data
	thread_data_send = kmalloc(sizeof(*thread_data_send), GFP_KERNEL);
	if (unlikely(!thread_data_send)) {
		NOA_MD_TX_ERROR(
			"Failed to allocate thread_data_send (id:%d)", index);
		ret = -ENOMEM;
		goto cleanup_entries;
	}
	thread_data_send->vpn_q = vpn_q;  // Pass the queue struct itself

	thread_data_release = kmalloc(sizeof(*thread_data_release), GFP_KERNEL);
	if (!thread_data_release) {
		NOA_MD_TX_ERROR(
			"Failed to allocate thread_data_release (id:%d)", index);
		ret = -ENOMEM;
		goto cleanup_entries;
	}
	thread_data_release->vpn_q = vpn_q;  // Pass the queue struct itself

	// Create kernel threads
	snprintf(thread_name, sizeof(thread_name), "noa_vpn_send_%d", index);
	vpn_q->send_thread = kthread_run(
		noa_md_vpn_tx_send_thread_func, thread_data_send, thread_name);
	if (IS_ERR(vpn_q->send_thread)) {
		ret = PTR_ERR(vpn_q->send_thread);
		NOA_MD_TX_ERROR("Failed to create send_thread (id:%d, ret:%d)", index, ret);
		vpn_q->send_thread = NULL;
		goto cleanup_entries;
	}

	snprintf(thread_name, sizeof(thread_name), "noa_vpn_release_%d", index);
	vpn_q->release_thread = kthread_run(
		noa_md_vpn_tx_release_thread_func, thread_data_release, thread_name);
	if (IS_ERR(vpn_q->release_thread)) {
		ret = PTR_ERR(vpn_q->release_thread);
		NOA_MD_TX_ERROR("Failed to create release_thread (id:%d, ret:%d)", index, ret);
		vpn_q->release_thread = NULL;
		goto cleanup_entries;
	}

	// Create work
	INIT_WORK(&vpn_q->send_work, noa_md_vpn_tx_send_work_handler);
	INIT_WORK(&vpn_q->release_work, noa_md_vpn_tx_release_work_handler);

	// Create batch release work
	atomic_set(&vpn_q->pending_batch_release_work_count, 0);
	atomic_set(&vpn_q->total_batch_release_count, 0);
	snprintf(wq_name, sizeof(wq_name), "noa_vpn_batch_rel_%u", index);

	vpn_q->batch_release_wq = alloc_workqueue(wq_name, WQ_UNBOUND | WQ_MEM_RECLAIM, 1);

	if (!vpn_q->batch_release_wq) {
		NOA_MD_TX_ERROR("Failed to create batch_release_wq(id:%u)", index);

		cancel_work_sync(&vpn_q->send_work);
		cancel_work_sync(&vpn_q->release_work);

		ret = -ENOMEM;
		goto cleanup_entries;
	}

	NOA_MD_TX_INFO("exit (q:%d, ret:%d)", index, ret);
	return ret;

cleanup_entries:

	//TODO: Check to use noa_md_vpn_tx_single_queue_release
	if (vpn_q->send_thread && !PTR_ERR(vpn_q->send_thread)) {
		kthread_stop(vpn_q->send_thread);
		vpn_q->send_thread = NULL;
	}
	if (vpn_q->release_thread && !PTR_ERR(vpn_q->release_thread)) {
		kthread_stop(vpn_q->release_thread);
		vpn_q->release_thread = NULL;
	}
	if (thread_data_send) {
		kfree(thread_data_send);
		thread_data_send = NULL;
	}
	if (thread_data_release) {
		kfree(thread_data_release);
		thread_data_release = NULL;
	}
	if (vpn_q->skb_entries) {
		kfree(vpn_q->skb_entries);
		vpn_q->skb_entries = NULL;
	}
	NOA_MD_TX_INFO("exit (q:%d, ret:%d)", index, ret);
	return ret;
}

/**
 * @brief Releases resources for a single VPN TX queue.
 * Stops associated kernel threads, purges any remaining SKBs, and frees allocated memory.
 * @param vpn_q Pointer to the VPN queue structure to release.
 */
static void noa_md_vpn_tx_single_queue_release(struct noa_vpn_tx_queue *vpn_q)
{
	unsigned int queue_index = vpn_q->id;
	NOA_MD_TX_INFO("enter (id:%d)", queue_index);

	// Stop threads
	if (vpn_q->send_thread && !PTR_ERR(vpn_q->send_thread)) {
		NOA_MD_TX_INFO("stopping send thread (id:%u)", queue_index);
		wake_up_interruptible(&vpn_q->send_wq);
		kthread_stop(vpn_q->send_thread);
		vpn_q->send_thread = NULL;
		NOA_MD_TX_INFO("send thread stopped(id:%u)", queue_index);
	}

	if (vpn_q->release_thread && !PTR_ERR(vpn_q->release_thread)) {
		NOA_MD_TX_INFO("stopping release thread (id:%u)", queue_index);
		wake_up_interruptible(&vpn_q->release_wq);
		kthread_stop(vpn_q->release_thread);
		vpn_q->release_thread = NULL;
		NOA_MD_TX_INFO("release thread stopped(id:%u)", queue_index);
	}

	cancel_work_sync(&vpn_q->send_work);
	cancel_work_sync(&vpn_q->release_work);

	if (vpn_q->batch_release_wq) {
		NOA_MD_TX_INFO(
			"draining and destroying batch_release_wq (id:%u)", queue_index);
		drain_workqueue(vpn_q->batch_release_wq);
		destroy_workqueue(vpn_q->batch_release_wq);
		vpn_q->batch_release_wq = NULL;
	}

	// Ensure pending_batch_release_work_count becomes 0 before proceeding.
	// drain_workqueue should ideally handle this, but add a wait loop as a safeguard.
	if (atomic_read(&vpn_q->pending_batch_release_work_count) != 0) {
		int retries = 200;  // Wait for up to 200 * 50ms = 10 seconds
		NOA_MD_TX_INFO(
			"waiting for batch release work to complete "
			"(id:%u, pending_work:%d, retries:%d)  ",
			queue_index, atomic_read(&vpn_q->pending_batch_release_work_count),
			retries);

		while (
			atomic_read(&vpn_q->pending_batch_release_work_count) != 0 && retries-- > 0) {
			msleep(50);  // Yield and sleep for 50 milliseconds
		}

		if (atomic_read(&vpn_q->pending_batch_release_work_count) != 0) {
			NOA_MD_TX_ERROR(
				"waiting for batch release work timeout "
				"(id:%u, pending_work:%d, retries:%d)",
				queue_index, atomic_read(&vpn_q->pending_batch_release_work_count),
				retries);
			// TODO: Error handling, it seems that data stall has occurred
		} else {
			NOA_MD_TX_INFO(
				"batch release work became 0 (id:%u)",
				queue_index);
		}
	}

	noa_md_vpn_tx_purge_queue(vpn_q);
	// Cleanup remaining skbs and DMA mappings
	if (vpn_q->skb_entries) {
		kfree(vpn_q->skb_entries);
		vpn_q->skb_entries = NULL;
	}
	vpn_q->size = 0; // Mark as released
	vpn_q->mask = 0;
	vpn_q->dev = NULL;

	NOA_MD_TX_INFO("exit (id:%d)", queue_index);
}

/**
 * noa_md_vpn_tx_queues_setup() - Setup multiple VPN TX queues.
 * @tx:             Pointer to the main NOA MD TX structure.
 * @num_queues:     Number of VPN queues to create.
 * @per_queue_size: Size for each VPN queue (must be power of 2).
 *
 * Return:
 * * %0: On success.
 * * %-EINVAL: If arguments are invalid.
 * * %-ENOMEM: If memory allocation fails.
 */
int noa_md_vpn_tx_queues_setup(
	struct noa_md_tx *tx, unsigned int num_queues, unsigned int per_queue_size)
{
	int i, ret = 0;
	NOA_MD_TX_INFO("enter");

	CHECK_PTR_OR_RETURN_ERR(tx, -EINVAL);
	CHECK_TRUE_OR_RETURN_ERR((num_queues == 0), -EINVAL);
	// Allow NOA_MD_NUM_VPN_TX_QUEUES to be 0 if VPN is disabled
	if (num_queues == 0) {
		NOA_MD_TX_INFO("number of VPN queues is 0");
		tx->vpn_queues = NULL;
		tx->num_vpn_queues = 0;
		return 0;
	}
	CHECK_TRUE_OR_RETURN_ERR((per_queue_size == 0), -EINVAL);

	tx->vpn_queues = kcalloc(
		num_queues, sizeof(struct noa_vpn_tx_queue), GFP_KERNEL);
	CHECK_PTR_OR_RETURN_ERR(tx->vpn_queues, -ENOMEM);

	tx->num_vpn_queues = num_queues;

	for (i = 0; i < num_queues; i++) {
		// Pass necessary info to setup each individual queue
		ret = noa_md_vpn_tx_single_queue_setup(
			&tx->vpn_queues[i], i, per_queue_size, md_dev.dev, tx);
		if (ret) {
			NOA_MD_TX_ERROR(
				"noa_md_vpn_tx_single_queue_setup failed (id:%d, ret:%d)",
				i, ret);
			// Cleanup already setup queues
			for (int j = i - 1; j >= 0; j--) {
				noa_md_vpn_tx_single_queue_release(&tx->vpn_queues[j]);
			}
			kfree(tx->vpn_queues);
			tx->vpn_queues = NULL;
			tx->num_vpn_queues = 0;
			return ret;
		}
	}

	NOA_MD_TX_INFO("exit");
	return 0;
}

/**
 * noa_md_vpn_tx_queues_release() - Release all VPN TX queues.
 * @tx: Pointer to the main NOA MD TX structure.
 *
 * Stops threads, cleans SKBs, and frees memory.
 */
void noa_md_vpn_tx_queues_release(struct noa_md_tx *tx) {
	int i;

	NOA_MD_TX_INFO("enter");
	CHECK_PTR_OR_RETURN(tx);
	CHECK_PTR_OR_RETURN(tx->vpn_queues);

	for (i = 0; i < tx->num_vpn_queues; i++) {
		noa_md_vpn_tx_single_queue_release(&tx->vpn_queues[i]);
	}

	kfree(tx->vpn_queues);
	tx->vpn_queues = NULL;
	tx->num_vpn_queues = 0;
	NOA_MD_TX_INFO("exit");
}