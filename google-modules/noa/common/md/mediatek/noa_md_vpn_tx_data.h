/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD VPN TX Driver
 *
 * Copyright 2025 Google LLC.
 */
#ifndef __NOA_MD_VPN_TX_DATA_H__
#define __NOA_MD_VPN_TX_DATA_H__

#include <linux/device.h>    // For struct device
#include <linux/kthread.h>   // For struct task_struct (kernel threads)
#include <linux/skbuff.h>    // For struct sk_buff
#include <linux/spinlock.h>  // For spinlock_t
#include <linux/wait.h>      // For wait_queue_head_t

#include "noa_md_trace.h"
#include "noa_md_tx_data.h"
#include "t900/noa_md_mtk_priv.h"

#if IS_ENABLED(CONFIG_NOA_VPN_OFFLOAD_SUPPORT)
#include "common/core.h"  // For noa_tx_ipsec_metadata
#endif

#define NOA_MD_NUM_VPN_TX_QUEUES 1
#define NOA_MD_VPN_QUEUE_SIZE_POWER 15  // 2^15: 32768
// Size for each VPN TX queue (MUST be power of 2)
#define NOA_MD_VPN_TX_QUEUE_SIZE (1<<NOA_MD_VPN_QUEUE_SIZE_POWER)
#define NOA_MD_VPN_TX_BATCH_RELEASE_THRESHOLD_CNT 64

/**
 * struct noa_vpn_tx_skb_entry - Stores an SKB and related info for VPN TX queue.
 * @skb:        Network packet buffer.
 * @metadata:   Tx ipsec metadata. Used if CONFIG_NOA_VPN_OFFLOAD_SUPPORT is enabled.
 * @dma_addr:   DMA address for hardware access.
 * @mapped_len: Length of the DMA mapped data.
 * @pkt_info:   Copied transmit metadata (from SKB CB).
 */
struct noa_vpn_tx_skb_entry {
	struct sk_buff *skb;
	struct noa_tx_ipsec_metadata metadata;
	dma_addr_t dma_addr;
	size_t mapped_len;
	struct mtk_tx_pkt_info pkt_info;
};

/**
 * struct noa_vpn_skb_release_info - Info for a single SKB to be released.
 * @skb:        The sk_buff to release.
 * @dma_addr:   DMA address of the mapped data.
 * @mapped_len: Length of the mapped data.
 */
struct noa_vpn_skb_release_info {
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	size_t mapped_len;
};

/**
 * struct noa_vpn_batch_release_work_item - Work item for batch releasing SKBs.
 * @work:  Work structure for scheduling.
 * @vpn_q: Pointer to the VPN queue this work item belongs to.
 * @count: Number of SKBs in this batch.
 * @items: Array of SKB release information.
  */
struct noa_vpn_batch_release_work_item {
	struct work_struct work;
	struct noa_vpn_tx_queue *vpn_q;
	unsigned int count;
	struct noa_vpn_skb_release_info *items;
};

/**
 * struct noa_vpn_tx_queue - Represents a single VPN transmit queue.
 * @id:               Queue identifier (index).
 * @skb_entries:      Array storing SKB entries. Points to &struct noa_vpn_tx_skb_entry.
 * @size:             Queue size (capacity, must be power of 2).
 * @mask:             Mask for index calculation (size - 1).
 * @write_idx:        Index where the next SKB will be enqueued. Volatile.
 * @send_idx:         Index of the next SKB to be sent to NEP. Volatile.
 * @release_idx:      Index of the next SKB to be released. Volatile.
 * @send_wq:          Wait queue for the send thread.
 * @release_wq:       Wait queue for the release thread.
 * @pkts_to_release:  Atomic counter for completed packets pending release.
 * @parent_tx:        Pointer back to the main &struct noa_md_tx.
 * @dev:              Device pointer for DMA operations (&struct device).
 * @lock:             Spinlock protecting queue indices and skb_entries access.
 * @send_thread:      Kernel thread for sending packets (&struct task_struct).
 * @release_thread:   Kernel thread for releasing completed packets (&struct task_struct).
 * @send_work:        Work structure for send thread scheduling.
 * @release_work:     Work structure for release thread scheduling.
 * @batch_release_wq: Work queue for batch releasing SKBs.
 * @pending_batch_release_work_count: Atomic counter for pending batch release work items.
 * @total_batch_release_count: Atomic counter for total SKBs released in batches.
 * @batch_release_threshold_cnt: Max SKBs processed by each batch release work.
 *
 * Handles enqueuing, sending, and releasing SKBs via dedicated threads.
 */
struct noa_vpn_tx_queue {
	unsigned int id;
	struct noa_vpn_tx_skb_entry *skb_entries;
	unsigned int size;
	unsigned int mask;
	volatile unsigned int write_idx;
	volatile unsigned int send_idx;
	volatile unsigned int release_idx;
	wait_queue_head_t send_wq;
	wait_queue_head_t release_wq;
	atomic_t pkts_to_release;
	struct noa_md_tx *parent_tx;
	struct device *dev;
	spinlock_t lock;
	struct task_struct *send_thread;
	struct task_struct *release_thread;
	struct work_struct send_work;
	struct work_struct release_work;
	struct workqueue_struct *batch_release_wq;
	atomic_t pending_batch_release_work_count;
	atomic_t total_batch_release_count;
	unsigned int batch_release_threshold_cnt;
};

/**
 * struct noa_vpn_thread_data - Data passed to VPN send/release kernel threads.
 * @vpn_q: Pointer to the associated &struct noa_vpn_tx_queue.
 */
struct noa_vpn_thread_data {
	struct noa_vpn_tx_queue *vpn_q;
};

/**
 * vpn_txq_write_avail() - Calculate available write space in the VPN TX queue.
 * @vpn_q: Pointer to the &struct noa_vpn_tx_queue.
 *
 * Return:
 * * Number of available slots for writing.
 */
static inline unsigned int vpn_txq_write_avail(struct noa_vpn_tx_queue *vpn_q)
{
	// Includes the release_idx slot, subtract 1 for the reserved empty slot
	return (vpn_q->release_idx - vpn_q->write_idx - 1 + vpn_q->size) &
		vpn_q->mask;
}

/**
 * vpn_txq_next_idx() - Calculate the next index in the circular buffer.
 * @vpn_q:       Pointer to the &struct noa_vpn_tx_queue.
 * @current_idx: Current index.
 *
 * Return:
 * * Next index.
 */
static inline unsigned int vpn_txq_next_idx(
	struct noa_vpn_tx_queue *vpn_q, unsigned int current_idx)
{
	return (current_idx + 1) & vpn_q->mask;
}

/**
 * vpn_txq_send_ready() - Calculate packets ready to be sent.
 * @vpn_q: Pointer to the &struct noa_vpn_tx_queue.
 *
 * Return:
 * * Number of packets ready to send.
 */
static inline unsigned int vpn_txq_send_ready(struct noa_vpn_tx_queue *vpn_q)
{
	return (vpn_q->write_idx - vpn_q->send_idx + vpn_q->size) & vpn_q->mask;
}

/**
 * vpn_txq_release_pending() - Calculate packets sent but pending release.
 * @vpn_q: Pointer to the &struct noa_vpn_tx_queue.
 *
 * Return:
 * * Number of packets pending release.
 */
static inline unsigned int vpn_txq_release_pending(
	struct noa_vpn_tx_queue *vpn_q)
{
	return (vpn_q->send_idx - vpn_q->release_idx + vpn_q->size) & vpn_q->mask;
}

// VPN queue operation functions
int noa_md_vpn_tx_enqueue(
	struct noa_md_tx *tx, struct sk_buff *skb, unsigned int queue_index);
int noa_md_vpn_tx_release_request(struct noa_md_tx *tx,
	unsigned int queue_index, unsigned int count);
int noa_md_vpn_tx_send_thread_func(void *data);
int noa_md_vpn_tx_release_thread_func(void *data);

// VPN setup/release functions
int noa_md_vpn_tx_queues_setup(struct noa_md_tx *tx, unsigned int num_queues,
	unsigned int per_queue_size);
void noa_md_vpn_tx_queues_release(struct noa_md_tx *tx);

#endif /* __NOA_MD_VPN_TX_DATA_H__ */