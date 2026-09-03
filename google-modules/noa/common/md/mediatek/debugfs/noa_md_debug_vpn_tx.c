// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA MD VPN TX Debugfs Implementation
 *
 * Copyright 2025 Google LLC.
 */
#include <linux/debugfs.h>      // For debugfs operation
#include <linux/seq_file.h>     // For seq_file operation
#include <linux/uaccess.h>      // For copy_from_user
#include <linux/sched.h>        // For task_struct, current, wake_up_process
#include <linux/kthread.h>      // For kthread operation
#include <linux/delay.h>        // For msleep, udelay
#include <linux/jiffies.h>      // For jiffies operation
#include <linux/timekeeping.h>  // For ktime_get, ktime_sub, ktime_to_ns, NSEC_PER_SEC
#include <linux/string.h>       // For strcmp, strim, strsep, memset, scnprintf
#include <linux/slab.h>         // For kzalloc, kfree, kcalloc
#include <linux/completion.h>   // For struct completion operation
#include <linux/freezer.h>      // For try_to_freeze
#include <linux/skbuff.h>       // For sk_buff operation

#include "../noa_md.h"
#include "../noa_md_tx_data.h"
#include "../noa_md_vpn_tx_data.h"
#include "noa_md_debug_vpn_tx.h"
#include "noa_md_debug_skb_pool.h"


#define DEBUG_FS_ROOT_NAME "/sys/kernel/debug"
#define VPN_TEST_NUM_QUEUES 1
#define VPN_TEST_QUEUE_SIZE_POWER 12  // 2^10: 1024, 2^12: 4096, 2^15: 32768, 2^16: 1048576
#define VPN_TEST_QUEUE_SIZE (1<<VPN_TEST_QUEUE_SIZE_POWER)
#define VPN_TEST_RELEASE_COUNT_LIMIT (VPN_TEST_QUEUE_SIZE>>1)

#define VPN_TX_TEST_STATUS_BUF_SIZE 8192
static char vpn_tx_test_status_buf[VPN_TX_TEST_STATUS_BUF_SIZE];
static size_t vpn_tx_test_status_len;

#define CLEAR_STATUS() \
	do { \
		vpn_tx_test_status_len = 0; \
		vpn_tx_test_status_buf[0] = '\0'; \
	} while (0)

#define ADD_STATUS(fmt, ...) \
	do { \
		if (vpn_tx_test_status_len < VPN_TX_TEST_STATUS_BUF_SIZE) { \
			vpn_tx_test_status_len += scnprintf( \
				vpn_tx_test_status_buf + vpn_tx_test_status_len, \
				VPN_TX_TEST_STATUS_BUF_SIZE - vpn_tx_test_status_len, \
				fmt "\n", ##__VA_ARGS__); \
		} \
	} while (0)

#define RECORD_TEST_INFO(fmt, ...) \
	do { \
		ADD_STATUS(fmt, ##__VA_ARGS__); \
		NOA_MD_INFO(fmt, ##__VA_ARGS__); \
	} while (0)

#define RECORD_TEST_ERROR(fmt, ...) \
	do { \
		ADD_STATUS(fmt, ##__VA_ARGS__); \
		NOA_MD_ERROR(fmt, ##__VA_ARGS__); \
	} while (0)

/**
 * struct stress_test_release_thread_info - Info for stress test release thread.
 * @main_stress_info: Pointer to main stress test data.
 * @stop_flag:        Flag to stop the thread.
 * @thread_done_completion: Completion to signal thread exit.
 */
struct stress_test_release_thread_info {
	struct stress_test_info *main_stress_info;
	bool stop_flag;
	struct completion thread_done_completion;
};

/**
 * struct vpn_tx_test_thread_data - Data for VPN TX test thread.
 * @skb_num_to_test:       Number of SKBs for the test.
 * @skb_pool:              SKB pool manager for dummy SKBs.
 * @test_started_completion: Completion to signal thread has started.
 */
struct vpn_tx_test_thread_data {
	unsigned int skb_num_to_test;
	struct completion test_started_completion;  // To signal thread has started
};


/**
 * vpn_tx_test_thread_func() - Kernel thread for VPN TX functional test.
 * @data: Pointer to struct vpn_tx_test_thread_data.
 *
 * This thread performs a functional test on the VPN TX path by:
 * 1. Setting up VPN TX queues.
 * 2. Creating dummy SKBs using an SKB pool.
 * 3. Enqueuing SKBs into the VPN TX queue.
 * 4. Requesting release of enqueued packets.
 * 5. Logging queue status and cleaning up resources.
 *
 * Return:
 * * %0: On success.
 * * %-EINVAL: If input @data is NULL or skb_num_to_test is zero.
 * * %-ENOMEM: If memory allocation fails.
 * * Other negative error codes on failure during queue setup or SKB operations.
 */
static int vpn_tx_test_thread_func(void *data)
{
	struct vpn_tx_test_thread_data *thread_data = data;
	struct noa_md_tx *test_tx = NULL;
	struct sk_buff **skbs = NULL;
	struct skb_pool_manager *skb_pool = NULL;
	int *enqueue_results = NULL;
	unsigned int release_count = 0;
	unsigned int skb_num_to_test = 0;
	const unsigned int vpn_q_index = 0;
	int ret = -ENOMEM;;

	const unsigned int skb_pool_skb_data_size = 1400;
	const unsigned int skb_pool_initial_fill = 512;
	unsigned int skb_pool_max_capacity = VPN_TEST_QUEUE_SIZE;

	NOA_MD_INFO("enter");

	CHECK_PTR_OR_RETURN_ERR(thread_data, -EINVAL);

	skb_num_to_test = thread_data->skb_num_to_test;
	// Signal that thread has started and copied data
	complete(&thread_data->test_started_completion);

	NOA_MD_INFO("Started for %u SKBs", skb_num_to_test);

	RECORD_TEST_INFO("Test start (Thread ID: %d)", current->pid);

	if (unlikely(skb_num_to_test == 0)) {
		RECORD_TEST_ERROR("skb_num_to_test cannot be zero.");
		ret = -EINVAL;
		goto free_arrays;
	}

	ret = skb_pool_init(
		&skb_pool, skb_pool_skb_data_size,
		skb_pool_initial_fill > VPN_TEST_QUEUE_SIZE ?
			VPN_TEST_QUEUE_SIZE : skb_pool_initial_fill,
		skb_pool_max_capacity, 0);
	CHECK_TRUE_OR_GOTO_ERR(ret, free_arrays);

	test_tx = kzalloc(sizeof(*test_tx), GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(test_tx, free_arrays);

	skbs = kcalloc(skb_num_to_test, sizeof(*skbs), GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(skbs, free_arrays);

	enqueue_results =
		kcalloc(skb_num_to_test, sizeof(*enqueue_results), GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(enqueue_results, free_arrays);
	// Set default value to -EINVAL
	memset(enqueue_results, -EINVAL,
		   skb_num_to_test * sizeof(*enqueue_results));

	RECORD_TEST_INFO("Starting VPN TX test (%u SKBs)", skb_num_to_test);

	RECORD_TEST_INFO("Setup queues");
	ret = noa_md_vpn_tx_queues_setup(
		test_tx, VPN_TEST_NUM_QUEUES, VPN_TEST_QUEUE_SIZE);
	RECORD_TEST_INFO("noa_md_vpn_tx_queues_setup=%d", ret);
	CHECK_TRUE_OR_GOTO_ERR(ret, free_arrays);

	udelay(100);

	RECORD_TEST_INFO("Create dummy SKBs");
	for (int i = 0; i < skb_num_to_test; i++) {
		skbs[i] = skb_pool_extract_skb(skb_pool);
		if (!skbs[i]) {
			RECORD_TEST_ERROR("Failed to create dummy skb %u", i);
			ret = -ENOMEM;
			goto cleanup_skbs_and_queues;
		}
	}
	RECORD_TEST_INFO("Created %u dummy SKBs",
		skb_num_to_test);

	RECORD_TEST_INFO("SKB size info:");
	RECORD_TEST_INFO("  Headroom: %d", skb_headroom(skbs[0]));
	RECORD_TEST_INFO("  Head size: %d", skb_headlen(skbs[0]));
	RECORD_TEST_INFO("  Nonlinear size: %d", skbs[0]->data_len);
	RECORD_TEST_INFO("  Allocated size: %lu",
		(unsigned long)skbs[0]->end - (unsigned long)skbs[0]->head);
	RECORD_TEST_INFO("  Tailroom: %d", skb_tailroom(skbs[0]));
	RECORD_TEST_INFO("  Data size: %d", skbs[0]->len);
	RECORD_TEST_INFO("  Total size: %u", skbs[0]->truesize);

	RECORD_TEST_INFO("Enqueuing SKBs");
	release_count = 0;
	for (int i = 0; i < skb_num_to_test; i++) {
		enqueue_results[i] =
			noa_md_vpn_tx_enqueue(test_tx, skbs[i], vpn_q_index);
		if (enqueue_results[i] != 0) {
			RECORD_TEST_ERROR(
				"vpn_tx_enqueue failed for skb %u: %d", i, enqueue_results[i]);
			break;
		} else {
			release_count++;
			skbs[i] = NULL;  // Mark as not needing free by the loop below
			if (release_count >= VPN_TEST_RELEASE_COUNT_LIMIT ) {
				RECORD_TEST_INFO("Requesting release for %u packets",
					release_count);
				ret = noa_md_vpn_tx_release_request(
					test_tx, vpn_q_index, release_count);
				if (ret) {
					RECORD_TEST_ERROR("vpn_tx_release_request failed: %d", ret);
				} else {
					release_count = 0;
				}
			}
		}
	}

	msleep(500);

	if (release_count > 0) {
		RECORD_TEST_INFO("Requesting release for %u packets", release_count);
		ret = noa_md_vpn_tx_release_request(
			test_tx, vpn_q_index, release_count);
		if (ret) {
			RECORD_TEST_ERROR("vpn_tx_release_request failed: %d", ret);
		}
	}

	RECORD_TEST_INFO("Waiting 500ms for release thread");

	msleep(500);

	if (test_tx && test_tx->vpn_queues) {
		struct noa_vpn_tx_queue *vpn_q = &test_tx->vpn_queues[vpn_q_index];
		unsigned long flags;

		RECORD_TEST_INFO("--- VPN Queue Status (after release request) ---");
		spin_lock_irqsave(&vpn_q->lock, flags);
		RECORD_TEST_INFO(
			"vpn_q[%u]: write_idx = %u", vpn_q_index, vpn_q->write_idx);
		RECORD_TEST_INFO(
			"vpn_q[%u]: send_idx = %u", vpn_q_index, vpn_q->send_idx);
		RECORD_TEST_INFO(
			"vpn_q[%u]: release_idx = %u", vpn_q_index, vpn_q->release_idx);
		RECORD_TEST_INFO(
			"vpn_q[%u]: size = %u", vpn_q_index, vpn_q->size);
		RECORD_TEST_INFO(
			"vpn_q[%u]: mask = %u", vpn_q_index, vpn_q->mask);
		RECORD_TEST_INFO(
			"vpn_q[%u]: pkts_to_release = %d",
			vpn_q_index, atomic_read(&vpn_q->pkts_to_release));
		RECORD_TEST_INFO(
			"vpn_q[%u]: write_avail = %u",
			vpn_q_index, vpn_txq_write_avail(vpn_q));
		RECORD_TEST_INFO(
			"vpn_q[%u]: send_ready = %u",
			vpn_q_index, vpn_txq_send_ready(vpn_q));
		RECORD_TEST_INFO(
			"vpn_q[%u]: release_pending = %u",
			vpn_q_index, vpn_txq_release_pending(vpn_q));

		if (vpn_q->skb_entries) {
			unsigned int unreleased_count = 0;
			for (int i = 0; i < vpn_q->size; i ++) {
				if (vpn_q->skb_entries[i].skb) {
					RECORD_TEST_ERROR("vpn_q[%u]: SKB %u (ptr: %p) found "
						"in skb_entries", vpn_q_index, i,
						vpn_q->skb_entries[i].skb);
					unreleased_count++;
				}
			}
			if (unreleased_count > 0) {
				RECORD_TEST_INFO(
					"vpn_q[%u]: Found %u SKBs potentially not released.",
					vpn_q_index, unreleased_count);
			} else {
				RECORD_TEST_INFO(
					"vpn_q[%u]: All SKBs from initial array appear released.",
					vpn_q_index);
			}
		}
		spin_unlock_irqrestore(&vpn_q->lock, flags);

		if (vpn_q->send_thread) {
			rcu_read_lock();
			RECORD_TEST_INFO(
				"vpn_q[%u]: send_thread state = %hhu (raw), pid = %d",
				vpn_q_index,
				task_state_to_char(vpn_q->send_thread),
				vpn_q->send_thread ? vpn_q->send_thread->pid : -1);
			rcu_read_unlock();
		} else {
			RECORD_TEST_INFO("vpn_q[%u]: send_thread is NULL", vpn_q_index);
		}

		if (vpn_q->release_thread) {
			rcu_read_lock();
			RECORD_TEST_INFO(
			"vpn_q[%u]: release_thread state = %hhu (raw), pid = %d",
			vpn_q_index,
			task_state_to_char(vpn_q->release_thread),
			vpn_q->release_thread ? vpn_q->release_thread->pid : -1);
			rcu_read_unlock();
		} else {
			RECORD_TEST_INFO("vpn_q[%u]: release_thread is NULL", vpn_q_index);
		}
		RECORD_TEST_INFO("--- End VPN Queue Status ---");
	} else {
		RECORD_TEST_ERROR("test_tx or test_tx->vpn_queues is NULL after test operations");
	}

cleanup_skbs_and_queues:
	RECORD_TEST_INFO("Releasing queues");
	noa_md_vpn_tx_queues_release(test_tx);
	RECORD_TEST_INFO("VPN TX queues released");

	if (skbs) {
		for (int i = 0; i < skb_num_to_test; i++) {
			if (skbs[i]) {
				RECORD_TEST_ERROR(
					"SKB %u (0x%p) was not enqueue",
					i, skbs[i]);
				kfree_skb(skbs[i]);
				skbs[i] = NULL;
			}
		}
	}

free_arrays:
	if (skbs) {
		kfree(skbs);
		skbs = NULL;
	}
	if (enqueue_results) {
		kfree(enqueue_results);
		enqueue_results = NULL;
	}

	if (test_tx) {
		kfree(test_tx);
		test_tx = NULL;
	}

	if (skb_pool) {
		skb_pool_release(skb_pool);
		skb_pool = NULL;
	}

	RECORD_TEST_INFO("Test finished (Thread). Result: %d", ret);
	kfree(thread_data);

	NOA_MD_INFO("VPN TX Test Thread: Exiting");
	return ret;
}

/**
 * vpn_tx_test_show() - Show VPN TX test interface status.
 * @m: Seq_file pointer.
 * @v: Unused.
 *
 * Displays usage instructions and the status of the last test run.
 *
 * Return:
 * * %0: Always.
 */
static int vpn_tx_test_show(struct seq_file *m, void *v)
{
	seq_puts(m, "VPN TX Test Interface:\n");
	seq_puts(m, "Usage: Write a number (e.g., '100') to this file ");
	seq_puts(m, "to run the test with that many SKBs.\n");
	seq_printf(m, "Example: echo \"50\" > %s/%s/%s\n\n",
		DEBUG_FS_ROOT_NAME, "noa_md", "vpn_tx/vpn_tx_test");
	seq_puts(m, "\n--- Last Test Run Status ---\n");
	if (vpn_tx_test_status_len > 0) {
		seq_printf(m, "%s", vpn_tx_test_status_buf);
	} else {
		seq_puts(m, "No test run status available. ");
		seq_puts(m, "Execute a write command to generate status.\n");
	}
	seq_puts(m, "--------------------------\n");
	return 0;
}

/**
 * vpn_tx_test_write() - Handle writes to the VPN TX test debugfs file.
 * @file:      File pointer.
 * @user_buf:  User buffer containing the number of SKBs.
 * @count:     Number of bytes in user_buf.
 * @ppos:      Position offset.
 *
 * Kicks off the VPN TX test thread with the specified number of SKBs.
 *
 * Return:
 * * Number of bytes written on success.
 * * %-EFAULT: If copy_from_user fails.
 * * %-EINVAL: If the input number is invalid or zero.
 * * %-ENOMEM: If memory allocation for thread data fails.
 * * Other negative error codes from kthread_run.
 */
static ssize_t vpn_tx_test_write(
	struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[32];
	int ret = 0;
	unsigned int skb_num_to_test = 0;
	struct vpn_tx_test_thread_data *thread_data = NULL;
	struct task_struct *test_thread = NULL;

	NOA_MD_INFO("enter");

	CLEAR_STATUS();
	RECORD_TEST_INFO("vpn_tx_test_write: Initiating test");

	if (copy_from_user(buf, user_buf, min_t(size_t, sizeof(buf) - 1, count))) {
		RECORD_TEST_ERROR("Failed to copy from user");
		return -EFAULT;
	}

	buf[min_t(size_t, sizeof(buf) - 1, count)] = '\0';
	strim(buf);  // Strip leading and trailing whitespace

	if (kstrtouint(buf, 10, &skb_num_to_test) != 0) {
		RECORD_TEST_ERROR("Invalid number format: %s", buf);
		return -EINVAL;
	}

	RECORD_TEST_INFO("Requested SKBs to test: %u", skb_num_to_test);
	if (skb_num_to_test == 0) {
		RECORD_TEST_ERROR("SKB number cannot be zero.");
		return -EINVAL;
	}

	thread_data = kzalloc(sizeof(*thread_data), GFP_KERNEL);
	if (!thread_data) {
		RECORD_TEST_ERROR("Failed to allocate memory for thread data.");
		return -ENOMEM;
	}

	thread_data->skb_num_to_test = skb_num_to_test;
	init_completion(&thread_data->test_started_completion);

	test_thread = kthread_run(vpn_tx_test_thread_func, thread_data, "vpn_tx_test");
	if (IS_ERR(test_thread)) {
		ret = PTR_ERR(test_thread);
		RECORD_TEST_ERROR("Failed to create vpn_tx_test thread: %d", ret);
		kfree(thread_data);
		return ret;
	}

	wait_for_completion_timeout(
		&thread_data->test_started_completion, msecs_to_jiffies(1000));

	RECORD_TEST_INFO("Test thread (PID: %d) created and started for %u SKBs.",
		test_thread->pid, skb_num_to_test);

	return count;
}

/**
 * vpn_tx_test_open() - Open method for the VPN TX test debugfs file.
 * @inode: Inode structure.
 * @file:  File structure.
 *
 * Return:
 * * %0 on success, from single_open.
 */
static int vpn_tx_test_open(struct inode *inode, struct file *file)
{
	return single_open(file, vpn_tx_test_show, inode->i_private);
}

static const struct file_operations vpn_tx_test_fops = {
	.owner = THIS_MODULE,
	.open = vpn_tx_test_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = vpn_tx_test_write,
};

/**
 * struct stress_test_info - Holds all info for a VPN TX stress test.
 * @thread:               Task struct for the main sending thread.
 * @tx:                   Pointer to the NOA MD TX structure.
 * @skb_pool:             SKB pool manager for dummy SKBs.
 * @queue_index:          VPN queue index to use for the test.
 * @stop_flag:            Flag to signal the test threads to stop.
 * @target_bytes_per_sec: Target send rate in bytes per second.
 * @total_bytes_sent:     Atomic counter for total bytes successfully sent.
 * @total_pkts_sent:      Atomic counter for total packets successfully sent.
 * @total_pkts_released:  Atomic counter for total packets confirmed released.
 * @total_pkts_create_failed: Atomic counter for SKB creation failures.
 * @total_pkts_failed:    Atomic counter for enqueue failures.
 * @start_time_jiffies:   Test start time in jiffies.
 * @duration_sec:         Requested duration of the test in seconds.
 * @test_done:            Completion to signal the main test thread has finished.
 */
struct stress_test_info {
	struct task_struct *thread;
	struct noa_md_tx *tx;
	struct skb_pool_manager *skb_pool;
	unsigned int queue_index;
	bool stop_flag;
	u64 target_bytes_per_sec;
	atomic64_t total_bytes_sent;
	atomic64_t total_pkts_sent;
	atomic64_t total_pkts_released;
	atomic64_t total_pkts_create_failed;
	atomic64_t total_pkts_failed;
	unsigned long start_time_jiffies;
	unsigned int duration_sec;
	struct completion test_done;
};

/**
 * vpn_tx_stress_release_thread_func() - Release thread for VPN TX stress test.
 * @data: Pointer to struct stress_test_release_thread_info.
 *
 * Periodically checks for packets ready to be released in the VPN queue
 * and requests their release.
 *
 * Return:
 * * %0: On normal thread exit.
 * * %-EINVAL: If essential pointers in @data are NULL.
 */
static int vpn_tx_stress_release_thread_func(void *data)
{
	struct stress_test_release_thread_info *release_info = data;
	struct stress_test_info *stress_info = release_info->main_stress_info;
	struct noa_vpn_tx_queue *vpn_q;
	int ret;
	unsigned int pkts_to_release_local;

	RECORD_TEST_INFO("VPN TX Stress Release Thread: Started (PID: %d)", current->pid);

	if (!stress_info || !stress_info->tx || !stress_info->tx->vpn_queues) {
		RECORD_TEST_ERROR(
			"VPN TX Stress Release Thread: Invalid stress_info or vpn_queues");
		complete(&release_info->thread_done_completion);
		return -EINVAL;
	}

	vpn_q = &stress_info->tx->vpn_queues[stress_info->queue_index];

	while (!kthread_should_stop() && !release_info->stop_flag) {
		udelay(10);

		if (kthread_should_stop() || release_info->stop_flag) {
			break;
		}

        spin_lock_irq(&vpn_q->lock);
        // Calculate how many can be released (send_idx - release_idx).
        pkts_to_release_local = vpn_txq_release_pending(vpn_q);
        spin_unlock_irq(&vpn_q->lock);

		if (pkts_to_release_local > 0) {
			// Call release request, riggers the existing release logic.
			ret = noa_md_vpn_tx_release_request(
				stress_info->tx, stress_info->queue_index, pkts_to_release_local);
			if (ret) {
				NOA_MD_TX_ERROR(
					"VPN TX Stress Release Thread: vpn_tx_release_request failed: %d",
					ret);
			} else {
				atomic64_add(pkts_to_release_local, &stress_info->total_pkts_released);
			}
		}
		try_to_freeze();
	}

	RECORD_TEST_INFO("VPN TX Stress Release Thread: Exiting");
	complete(&release_info->thread_done_completion);
	return 0;
}

/**
 * vpn_tx_stress_thread_func() - Main sending thread for VPN TX stress test.
 * @data: Pointer to struct stress_test_info.
 *
 * Sends SKBs at a target rate for a specified duration.
 *
 * Return:
 * * %0: On normal thread exit.
 */
static int vpn_tx_stress_thread_func(void *data)
{
	struct stress_test_info *info = data;
	struct noa_vpn_tx_queue *vpn_q;
	struct sk_buff *skb;
	ktime_t start_ktime, now_ktime;
	s64 elapsed_ns, total_duration_ns;
	u64 target_bytes_for_duration, bytes_this_interval;
	unsigned int skb_payload_size = 1400;
	unsigned int num_skbs_needed;
	int ret;
	unsigned long interval_end_jiffies;
	unsigned long loop_interval_hz = HZ / (100 * 5);  // Check 100 * 5 times every 1 second

	info->start_time_jiffies = jiffies;
	vpn_q = &info->tx->vpn_queues[info->queue_index];
	atomic64_set(&info->total_bytes_sent, 0);
	atomic64_set(&info->total_pkts_sent, 0);
	atomic64_set(&info->total_pkts_released, 0);
	atomic64_set(&info->total_pkts_create_failed, 0);
	atomic64_set(&info->total_pkts_failed, 0);
	start_ktime = ktime_get();
	total_duration_ns = (s64)info->duration_sec * NSEC_PER_SEC;

	RECORD_TEST_INFO("Thread started (Target: %llu Bps for %u s)",
		info->target_bytes_per_sec, info->duration_sec);

	while (!kthread_should_stop() && !info->stop_flag) {
		now_ktime = ktime_get();
		elapsed_ns = ktime_to_ns(ktime_sub(now_ktime, start_ktime));

		if (elapsed_ns >= total_duration_ns) {
			break;  // Testing time is up
		}

		// Calculate the target number of bytes that should have been sent
		target_bytes_for_duration =
			div_u64((u64)elapsed_ns * info->target_bytes_per_sec, NSEC_PER_SEC);

		bytes_this_interval = 0;
		if (target_bytes_for_duration > (u64)atomic64_read(&info->total_bytes_sent)) {
			bytes_this_interval =
				target_bytes_for_duration - (u64)atomic64_read(
					&info->total_bytes_sent);
		}

		// Calculate how many SKB are needed
		num_skbs_needed =
			div_u64(bytes_this_interval + skb_payload_size -1 , skb_payload_size);

		if (num_skbs_needed > vpn_q->size / 10 * 8)
			num_skbs_needed = vpn_q->size / 10 * 8;

		// release_count = 0;
		for (int i = 0; i < num_skbs_needed; i++) {
			if (kthread_should_stop() || info->stop_flag) break;

			skb = skb_pool_extract_skb(info->skb_pool);
			if (!skb) {
				atomic64_inc(&info->total_pkts_create_failed);
				if(info->stop_flag || kthread_should_stop()) break;
				udelay(10);
				continue;
			}

			ret = noa_md_vpn_tx_enqueue(info->tx, skb, info->queue_index);
			if (ret == 0) {
				atomic64_inc(&info->total_pkts_sent);
				atomic64_add(skb->len, &info->total_bytes_sent);
			} else {
				atomic64_inc(&info->total_pkts_failed);
				// skb is freed by noa_md_vpn_tx_enqueue on failure
			}
		}
		interval_end_jiffies = jiffies + loop_interval_hz;
		while (time_before(jiffies, interval_end_jiffies)) {
			if (kthread_should_stop() || info->stop_flag) break;
			// schedule();
			cond_resched();
			try_to_freeze();  // Include freezer check
		}

		// Check for stop_flag more frequently if loop_interval_hz is large
		if (kthread_should_stop() || info->stop_flag) break;

		try_to_freeze(); // Check again after the loop
	}

	RECORD_TEST_INFO("exit");
	complete(&info->test_done);
	return 0;
}

/**
 * vpn_tx_stress_test_show() - Show VPN TX stress test interface status.
 * @m: Seq_file pointer.
 * @v: Unused.
 *
 * Displays usage instructions and the status of the last stress test run.
 *
 * Return:
 * * %0: Always.
 */
static int vpn_tx_stress_test_show(struct seq_file *m, void *v)
{
	seq_puts(m, "VPN TX Stress Test:\n");
	seq_puts(m, "Write 'start [queue_size_power] [duration_sec] [rate_mbps]' to run.\n");
	seq_puts(m, "[queue_size_power] defines queue capacity as 2^power.\n");
	seq_puts(m, "Example: Start test with 2^12 queue size, 10s duration, 1Gbps rate:\n");
	seq_printf(m, "  echo \"start 12 10 1000\" > %s/%s/%s\n\n",
		DEBUG_FS_ROOT_NAME, "noa_md", "vpn_tx/vpn_tx_stress_test");

	seq_puts(m, "\n--- Last Test Run Status ---\n");
	if (vpn_tx_test_status_len > 0) {
		seq_printf(m, "%s", vpn_tx_test_status_buf);
	} else {
		seq_puts(m, "No test run status available. ");
		seq_puts(m, "Execute a write command to generate status.\n");
	}
	seq_puts(m, "--------------------------\n");
	return 0;
}

/**
 * vpn_tx_stress_test_write() - Handle writes to the VPN TX stress test debugfs file.
 * @file:      File pointer.
 * @user_buf:  User buffer with test parameters.
 * @count:     Number of bytes in user_buf.
 * @ppos:      Position offset.
 *
 * Parses parameters and starts the VPN TX stress test.
 *
 * Return:
 * * Number of bytes written on success.
 * * %-EFAULT: If copy_from_user fails.
 * * %-EINVAL: If command format or parameters are invalid.
 * * %-ENOMEM: If memory allocation fails.
 * * Other negative error codes from underlying function calls.
 */
static ssize_t vpn_tx_stress_test_write(
	struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[64];
	char *p = buf;  // Pointer for strsep
	char *token;
	struct noa_md_tx *test_tx;
	struct stress_test_info *stress_info = NULL;
	struct skb_pool_manager *skb_pool = NULL;

	unsigned int queue_size_power = VPN_TEST_QUEUE_SIZE_POWER;
	unsigned int queue_size = (1<<queue_size_power);
	unsigned int duration = 10;  // 10 secs
	unsigned int rate_mbps = 1000;  // 1000 Mbps (1Gbps)
	unsigned long long rate_bps;
	int ret = 0;
	ktime_t start_ktime, end_ktime;
	unsigned long long actual_duration_ns;
	unsigned long long actual_rate_bps;
	unsigned long long final_bytes_sent, final_pkts_sent;
	unsigned long long final_pkts_released, final_pkts_create_failed, final_pkts_failed;

	const unsigned int skb_pool_skb_data_size = 1400;
	const unsigned int skb_pool_initial_fill = 512;
	unsigned int skb_pool_max_capacity;

	struct stress_test_release_thread_info *release_thread_info = NULL;
	struct task_struct *release_kthread = NULL;

	NOA_MD_INFO("enter");

	CLEAR_STATUS();

	RECORD_TEST_INFO("vpn_tx_test_write: Initiating test");

	if (copy_from_user(buf, user_buf, min_t(size_t, sizeof(buf) - 1, count))) {
		RECORD_TEST_ERROR("Failed to copy from user");
		return -EFAULT;
	}

	buf[min_t(size_t, sizeof(buf) - 1, count)] = '\0';
	strim(buf);  // Strip leading and trailing whitespace

	// Correct usage of strsep
	token = strsep(&p, " ");
	if (!token || strcmp(token, "start") != 0) {
		RECORD_TEST_ERROR("Invalid command format. Use 'start [duration] [rate]'");
		return -EINVAL;
	}

	// Parsing queue_size_power
	token = strsep(&p, " ");
	if (token) {
		if (kstrtouint(token, 10, &queue_size_power) != 0) {
			RECORD_TEST_ERROR("Invalid queue_size_power value: %s", token);
			return -EINVAL;
		}
	}
	queue_size = (1 << queue_size_power);
	skb_pool_max_capacity = queue_size;

	// Parsing duration
	token = strsep(&p, " ");
	if (token) {
		if (kstrtouint(token, 10, &duration) != 0) {
			RECORD_TEST_ERROR("Invalid duration value: %s", token);
			return -EINVAL;
		}
	}

	// Parsing rate
	token = strsep(&p, " ");
	if (token) {
		if (kstrtouint(token, 10, &rate_mbps) != 0) {
			RECORD_TEST_ERROR("Invalid rate value: %s", token);
			return -EINVAL;
		}
	}

	if (duration == 0 || rate_mbps == 0) {
		RECORD_TEST_ERROR("Duration and Rate must be positive.");
		return -EINVAL;
	}

	rate_bps = (unsigned long long)rate_mbps * 1000 * 1000;

	RECORD_TEST_INFO("duration=%u, rate_mbps=%u, rate_bps=%llu",
		duration, rate_mbps, rate_bps);

	ret = skb_pool_init(
		&skb_pool, skb_pool_skb_data_size,
		skb_pool_initial_fill > queue_size ? queue_size : skb_pool_initial_fill,
		skb_pool_max_capacity, 0);
	if (ret) {
		RECORD_TEST_ERROR("Failed to initialize SKB pool: %d", ret);
		return ret;
	}

	test_tx = kzalloc(sizeof(*test_tx), GFP_KERNEL);
	if (!test_tx) {
		RECORD_TEST_ERROR("test_tx is NULL");
		skb_pool_release(skb_pool);
		skb_pool = NULL;
		return -ENOMEM;
	}

	stress_info = kzalloc(sizeof(*stress_info), GFP_KERNEL);
	if (!stress_info) {
		RECORD_TEST_ERROR("stress_info is NULL");
		kfree(test_tx);
		skb_pool_release(skb_pool);
		skb_pool = NULL;
		return -ENOMEM;
	}

	stress_info->tx = test_tx;
	stress_info->skb_pool = skb_pool;
	stress_info->queue_index = 0;  // Terst with first VPN queue
	stress_info->stop_flag = false;
	stress_info->target_bytes_per_sec = rate_bps / 8;
	stress_info->duration_sec = duration;
	init_completion(&stress_info->test_done);

	RECORD_TEST_INFO("Starting test (Duration: %u s, Rate: %u Mbps)",
		duration, rate_mbps);

	RECORD_TEST_INFO("VPN TX queues setup");
	ret = noa_md_vpn_tx_queues_setup(test_tx, VPN_TEST_NUM_QUEUES, queue_size);
	if (ret) {
		RECORD_TEST_ERROR("vpn_tx_queues_setup failed: %d", ret);
		kfree(stress_info);
		kfree(test_tx);
		skb_pool_release(skb_pool);
		skb_pool = NULL;
		return ret;
	}
	RECORD_TEST_INFO("VPN TX queues setup return %d", ret);
	msleep(100);

	// Initial Release Thread Info
	release_thread_info = kzalloc(sizeof(*release_thread_info), GFP_KERNEL);
	if (!release_thread_info) {
		RECORD_TEST_ERROR("Failed to allocate memory for release_thread_info.");
		noa_md_vpn_tx_queues_release(test_tx);
		kfree(stress_info);
		kfree(test_tx);
		skb_pool_release(skb_pool);
		return -ENOMEM;
	}
	release_thread_info->main_stress_info = stress_info;
	release_thread_info->stop_flag = false;
	init_completion(&release_thread_info->thread_done_completion);

	RECORD_TEST_INFO("Start stress thread");
	stress_info->thread =
		kthread_run(vpn_tx_stress_thread_func, stress_info, "noa_stress_tx_send");
	if (IS_ERR(stress_info->thread)) {
		ret = PTR_ERR(stress_info->thread);
		RECORD_TEST_ERROR("Stress: Failed to create stress thread, ret=%d", ret);
		noa_md_vpn_tx_queues_release(test_tx);
		stress_info->tx = NULL;
		kfree(stress_info);
		kfree(test_tx);
		skb_pool_release(skb_pool);
		skb_pool = NULL;
		return ret;
	}

	RECORD_TEST_INFO("Start stress release thread");
	release_kthread = kthread_run(
		vpn_tx_stress_release_thread_func, release_thread_info, "noa_stress_tx_release");
	if (IS_ERR(release_kthread)) {
		ret = PTR_ERR(release_kthread);
		RECORD_TEST_ERROR("Stress: Failed to create stress release thread, ret=%d", ret);
		// Update stop_flag, the stress thread will check this flag and exit the thread loop
		stress_info->stop_flag = true;
		if (stress_info->thread) {
			 wake_up_process(stress_info->thread);  // Make sure it checks the stop_flag
		}
		// Waiting main thread complete
		wait_for_completion_timeout(
			&stress_info->test_done, msecs_to_jiffies(duration * 1000 + 1000));

		noa_md_vpn_tx_queues_release(test_tx);
		kfree(release_thread_info);
		kfree(stress_info);
		kfree(test_tx);
		skb_pool_release(skb_pool);
		return ret;
	}

	RECORD_TEST_INFO("Waiting for test thread to finish");
	start_ktime = ktime_get();
	wait_for_completion(&stress_info->test_done);
	end_ktime = ktime_get();

	RECORD_TEST_INFO("Stopping the release thread");
	if (release_kthread) {
		release_thread_info->stop_flag = true;
		wake_up_process(release_kthread);  // Make sure it checks the stop_flag
		// Waiting 2 secs for release thread complete
		wait_for_completion_timeout(
			&release_thread_info->thread_done_completion, msecs_to_jiffies(2000));
		release_kthread = NULL;
	}
	kfree(release_thread_info);
	release_thread_info = NULL;

	RECORD_TEST_INFO("Stop thread");
	if (stress_info->thread) {
		// TODO: Check thread and Force stop it by calling kthread_stop
		// if it's still running
		stress_info->thread = NULL; // Mark as stopped
	}

	RECORD_TEST_INFO("Releasing queues");
	noa_md_vpn_tx_queues_release(test_tx);
	stress_info->tx = NULL;
	NOA_MD_INFO("Stress: VPN TX queues released.");

	RECORD_TEST_INFO("Releasing SKB pool");
	skb_pool_release(skb_pool);
	skb_pool = NULL;
	NOA_MD_INFO("SKB pool released");

	RECORD_TEST_INFO("Calculation results");
	final_bytes_sent = atomic64_read(&stress_info->total_bytes_sent);
	final_pkts_sent = atomic64_read(&stress_info->total_pkts_sent);
	// TODO: Check the release count from queue structure
	final_pkts_released = atomic64_read(&stress_info->total_pkts_released);
	final_pkts_create_failed = atomic64_read(&stress_info->total_pkts_create_failed);
	final_pkts_failed = atomic64_read(&stress_info->total_pkts_failed);

	actual_duration_ns = ktime_to_ns(ktime_sub(end_ktime, start_ktime));
	if (actual_duration_ns > 0) {
		actual_rate_bps =
			div64_u64(final_bytes_sent * 8 * NSEC_PER_SEC, actual_duration_ns);
	} else {
		actual_rate_bps = 0;
	}

	RECORD_TEST_INFO("--- NOA MD VPN TX Stress Test Results ---");
	RECORD_TEST_INFO("Duration: %lld ns (~%u s)", actual_duration_ns, duration);
	RECORD_TEST_INFO("Target Rate: %llu Bps (%u Mbps)",
		stress_info->target_bytes_per_sec, rate_mbps);
	RECORD_TEST_INFO("Total Packets Sent: %llu", final_pkts_sent);
	RECORD_TEST_INFO("Total Packets Released: %llu", final_pkts_released);
	RECORD_TEST_INFO("Total Bytes Sent: %llu", final_bytes_sent);
	RECORD_TEST_INFO("Create SKB Failures: %llu", final_pkts_create_failed);
	RECORD_TEST_INFO("Enqueue Failures: %llu", final_pkts_failed);
	RECORD_TEST_INFO("Actual Rate: ~%llu bps (~%llu Mbps)",
		actual_rate_bps, div_u64(actual_rate_bps, 1000*1000));
	RECORD_TEST_INFO("-----------------------------------------");

	if (stress_info) {
		kfree(stress_info);
		stress_info = NULL;
	}
	if (test_tx) {
		kfree(test_tx);
		test_tx = NULL;
	}
	return count;
}

/**
 * vpn_tx_stress_test_open() - Open method for VPN TX stress test debugfs file.
 * @inode: Inode structure.
 * @file:  File structure.
 *
 * Return:
 * * %0 on success, from single_open.
 */
static int vpn_tx_stress_test_open(struct inode *inode, struct file *file)
{
	return single_open(file, vpn_tx_stress_test_show, inode->i_private);
}

static const struct file_operations vpn_tx_stress_test_fops = {
	.owner = THIS_MODULE,
	.open = vpn_tx_stress_test_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = vpn_tx_stress_test_write,
};

/**
 * noa_md_debug_vpn_tx_init() - Initialize VPN TX debugfs entries.
 * @noa_root: Pointer to the root debugfs directory for NOA.
 * @p_md_dev: Pointer to the NOA modem device structure.
 *
 * Creates "vpn_tx" directory and specific test files under it.
 *
 * Return:
 * * %0: On success.
 * * %-EINVAL: If @noa_root or @p_md_dev is NULL.
 * * %-ENOMEM: If debugfs entry creation fails.
 */
int noa_md_debug_vpn_tx_init(struct dentry *noa_root, struct noa_md_dev *p_md_dev)
{
	struct dentry *vpn_tx_dir;
	struct dentry *vpn_tx_test_file;
	struct dentry *vpn_tx_stress_file;

	CHECK_PTR_OR_RETURN_ERR(noa_root, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(p_md_dev, -EINVAL);

	vpn_tx_dir = debugfs_create_dir("vpn_tx", noa_root);
	if (!vpn_tx_dir) {
		NOA_MD_ERROR("Debug: Failed to create vpn_tx directory");
		return -ENOMEM;
	}

	vpn_tx_test_file = debugfs_create_file(
		"vpn_tx_test", 0644, vpn_tx_dir, p_md_dev, &vpn_tx_test_fops);
	if (!vpn_tx_test_file) {
		NOA_MD_ERROR("Debug: Failed to create vpn_tx_test file");
		debugfs_remove_recursive(vpn_tx_dir);
		return -ENOMEM;
	}

	vpn_tx_stress_file = debugfs_create_file(
		"vpn_tx_stress_test", 0644, vpn_tx_dir, p_md_dev,
		&vpn_tx_stress_test_fops);
	if (!vpn_tx_stress_file) {
		NOA_MD_ERROR("Debug: Failed to create vpn_tx_stress_test file");
		debugfs_remove_recursive(vpn_tx_dir);
		return -ENOMEM;
	}

	return 0;
}

void noa_md_debug_vpn_tx_exit(void)
{
	/*
	 * No action needed here.
	 * The parent debugfs module will call debugfs_remove_recursive() on the
	 * root directory, which cleans up everything we created in our init
	 * function.
	 */
}