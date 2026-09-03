#ifndef __NOA_MD_DEBUG_SKB_POOL_H__
#define __NOA_MD_DEBUG_SKB_POOL_H__

#include <linux/list.h>      // For struct list_head
#include <linux/spinlock.h>  // For spinlock_t
#include <linux/atomic.h>    // For atomic_t, atomic64_t
#include <linux/wait.h>      // For wait_queue_head_t
#include <linux/sched.h>     // For struct task_struct

/**
 * struct skb_pool_manager_stats - Statistics for the SKB pool.
 * @create_count:        Total SKBs created by the pool.
 * @create_bytes:        Total bytes of SKBs created.
 * @used_count:          Total SKBs extracted from the pool.
 * @used_bytes:          Total bytes of SKBs extracted.
 * @release_count:       Total SKBs released (freed by the pool during cleanup).
 * @release_bytes:       Total bytes of SKBs released.
 * @create_failed_count: Count of failed SKB creations.
 */
struct skb_pool_manager_stats {
    atomic64_t create_count;
    atomic64_t create_bytes;
    atomic64_t used_count;
    atomic64_t used_bytes;
    atomic64_t release_count;
    atomic64_t release_bytes;
    atomic64_t create_failed_count;
};

/**
 * struct skb_pool_manager - Manages a pool of pre-allocated SKBs.
 * @skb_list:                  List of available SKBs.
 * @skb_data_size:             Configured data size for each SKB in the pool.
 * @queue_index_for_dummy_skb: Queue index used for dummy SKB creation.
 * @lock:                      Lock to protect skb_list and count modification.
 * @producer_thread:           Kernel thread for producing SKBs.
 * @consumer_wait_q:           Wait queue for consumers when the pool is empty.
 * @producer_wait_q:           Wait queue for the producer when the pool is full.
 * @current_count:             Current number of SKBs in the pool.
 * @max_capacity:              Maximum number of SKBs the pool can hold.
 * @stop_producer:             Flag to signal the producer thread to stop.
 * @stats:                     Statistics for the SKB pool.
 */
struct skb_pool_manager {
    struct list_head skb_list;
    unsigned int skb_data_size;
    unsigned int queue_index_for_dummy_skb;
    spinlock_t lock;
    struct task_struct *producer_thread;
    wait_queue_head_t consumer_wait_q;
    wait_queue_head_t producer_wait_q;
    atomic_t current_count;
    unsigned int max_capacity;
    bool stop_producer;
    struct skb_pool_manager_stats stats;
};

// Function declarations
int skb_pool_init(struct skb_pool_manager **pool_mgr,
	unsigned int skb_data_size,
	unsigned int initial_fill_target, // How many SKBs to try to create initially
	unsigned int max_pool_capacity,
	unsigned int queue_index_for_dummy_skb);
void skb_pool_release(struct skb_pool_manager *pool_mgr);
struct sk_buff *skb_pool_extract_skb(struct skb_pool_manager *pool_mgr);

#endif /* __NOA_MD_DEBUG_SKB_POOL_H__ */