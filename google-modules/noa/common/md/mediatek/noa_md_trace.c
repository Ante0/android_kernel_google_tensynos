// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 *
 * This file implements the Sysfs interface for the NOA Mediatek Modem Driver.
 */
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/time.h>

#include "noa_md.h"
#include "noa_md_utility.h"
#include "noa_md_trace.h"
#include "noa_md_tx_data.h"
#include "noa_md_rx_data.h"

#define NOA_MD_DATA_LOG_LIMIT_INTERVAL 1000
#define NOA_MD_LOG_ENTRY_MAX_LEN 512
#define TIMESTAMP_LEN 21

/**
 * Default setting for data path logging.
 * This can be changed at runtime by writing to the sysfs node.
 * The <log_name> is the name provided during initialization,
 * e.g., "noa_md_msg" or "ncp_msg".
 *
 * Example:
 * # echo "enable" > /sys/devices/virtual/noa_md/trace/<log_name>/data_log_ctrl
 */
#define DEFAULT_DATA_LOG_CTRL NOA_MD_DATA_LOG_DISABLE

/**
 * Default setting for debug message logging.
 * This can be changed at runtime by writing to the sysfs node.
 * Example:
 * # echo "disable" > /sys/devices/virtual/noa_md/trace/<log_name>/debug_log_ctrl
 */
#ifdef DEBUG
#define DEFAULT_DEBUG_LOG_CTRL NOA_MD_LOG_ENABLE
#else
#define DEFAULT_DEBUG_LOG_CTRL NOA_MD_LOG_DISABLE
#endif

#define NOA_MD_TLOG_DEFAULT_SIZE (4096 * 1)

static struct noa_md_trace *md_trace;
static int noa_md_log_level = NOA_MD_LOG_LEVEL_INFO;

static const char *noa_md_log_level_str[] = {
	"ERROR",
	"WARNING",
	"INFO",
	"DEBUG",
};

// TX increment
void noa_trace_tx_inc(struct rtnl_link_stats64 *stats, unsigned long bytes)
{
	struct noa_md_trace *trace = md_dev.trace;

	// Update netdev stats
	if (stats) {
		stats->tx_packets++;
		stats->tx_bytes += bytes;
	}

	// Check if the trace buffer is initialized
	if (!trace) {
		return;
	}

	spin_lock(&trace->lock);
	trace->tx_count++;
	trace->tx_bytes += bytes;
	spin_unlock(&trace->lock);
}

// TX drop increment
void noa_trace_tx_drop_inc(struct rtnl_link_stats64 *stats,
	unsigned long bytes)
{
	struct noa_md_trace *trace = md_dev.trace;

	// Update netdev stats
	if (stats) {
		stats->tx_dropped++;
		// TODO: Do we need to add dropped bytes? Need review MTK logic
		// wwan_inst->stats.tx_bytes += bytes;
	}

	// Check if the trace buffer is initialized
	if (!trace) {
		return;
	}

	spin_lock(&trace->lock);
	trace->tx_dropped_count++;
	trace->tx_dropped_bytes += bytes;
	spin_unlock(&trace->lock);
}

// TX refill increment
void noa_trace_tx_refill_inc(unsigned long bytes)
{
	struct noa_md_trace *trace = md_dev.trace;

	// Check if the trace buffer is initialized
	if (!trace) {
		return;
	}

	spin_lock(&trace->lock);
	trace->tx_refill_count++;
	trace->tx_refill_bytes += bytes;
	spin_unlock(&trace->lock);
}

// RX increment
void noa_trace_rx_inc(struct rtnl_link_stats64 *stats, unsigned long bytes)
{
	struct noa_md_trace *trace = md_dev.trace;

	// Update netdev stats
	if (stats) {
		stats->rx_packets++;
		stats->rx_bytes += bytes;
	}

	// Check if the trace buffer is initialized
	if (!trace) {
		return;
	}

	spin_lock(&trace->lock);
	trace->rx_count++;
	trace->rx_bytes += bytes;
	spin_unlock(&trace->lock);
}

// rX refill increment
void noa_trace_rx_refill_inc(unsigned long bytes)
{
	struct noa_md_trace *trace = md_dev.trace;

	// Check if the trace buffer is initialized
	if (!trace) {
		return;
	}

	spin_lock(&trace->lock);
	trace->rx_refill_count++;
	trace->rx_refill_bytes += bytes;
	spin_unlock(&trace->lock);
}

// TX drop increment
void noa_trace_rx_drop_inc(
		struct rtnl_link_stats64 *stats, unsigned long bytes)
{
	struct noa_md_trace *trace = md_dev.trace;

	// Update netdev stats
	if (stats) {
		stats->tx_dropped++;
		// TODO: Do we need to add dropped bytes? Need review MTK logic
		// wwan_inst->stats.rx_bytes += bytes;
	}

	// Check if the trace buffer is initialized
	if (!trace) {
		return;
	}

	spin_lock(&trace->lock);
	trace->rx_dropped_count++;
	trace->rx_dropped_bytes += bytes;
	spin_unlock(&trace->lock);
}

static ssize_t noa_md_info_show(
	struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
	ssize_t count = 0;

	count += scnprintf(buf + count, PAGE_SIZE - count,
		"NOA MD Version: %s\n", NOA_MD_MODULE_VERSION);
	// Features
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"\nFeatures:\n");
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  enabled: %d\n", md_dev.feature_ctrl.enabled);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  tx_enabled: %d\n", md_dev.feature_ctrl.tx_enabled);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  rx_enabled: %d\n", md_dev.feature_ctrl.rx_enabled);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  unified_desc_enabled: %d\n",
		md_dev.feature_ctrl.unified_desc_enabled);
	// Ring Information
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"\nRing Information:\n");
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  struct noa_modem_ext_rxd_config size: %lu\n",
		sizeof(struct noa_modem_ext_rxd));
	// NOA MD Information
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"\nNOA MD Information:\n");

	return count;
}

static ssize_t noa_md_stats_show(
	struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
	ssize_t count = 0;
	unsigned long tx_count, tx_bytes;
	unsigned long tx_dropped_count, tx_dropped_bytes;
	unsigned long tx_refill_count, tx_refill_bytes;
	u32 skb_tx_queue_head[NOA_MD_NUM_TX_QUEUES];
	u32 skb_tx_queue_tail[NOA_MD_NUM_TX_QUEUES];

	unsigned long rx_count, rx_bytes;
	unsigned long rx_refill_count = 0L, rx_refill_bytes = 0L;

	if (!md_trace) {
		return 0;
	}

	spin_lock(&md_trace->lock);
	tx_count = md_trace->tx_count;
	tx_bytes = md_trace->tx_bytes;
	tx_dropped_count = md_trace->tx_dropped_count;
	tx_dropped_bytes = md_trace->tx_dropped_bytes;
	tx_refill_count = md_trace->tx_refill_count;
	tx_refill_bytes = md_trace->tx_refill_bytes;

	rx_count = md_trace->rx_count;
	rx_bytes = md_trace->rx_bytes;
	spin_unlock(&md_trace->lock);

	for (int i = 0; i < NOA_MD_NUM_TX_QUEUES; i++) {
		struct noa_md_tx_queue *txq = &md_dev.tx.tx_queues[i];
		spin_lock(&txq->txq_lock);
		skb_tx_queue_head[i] = txq->skb_tx_queue_head;
		skb_tx_queue_tail[i] = txq->skb_tx_queue_tail;
		spin_unlock(&txq->txq_lock);
	}

	count += scnprintf(buf + count, PAGE_SIZE - count,
		"NOA MD Version: %s\n\n", NOA_MD_MODULE_VERSION);
	// TX static
	count += scnprintf(buf + count, PAGE_SIZE - count, "TX static:\n");
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  Packets: %lu\n", tx_count);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  Bytes: %lu\n", tx_bytes);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  Dropped packets: %lu\n", tx_dropped_count);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  Dropped bytes: %lu\n", tx_dropped_bytes);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  Refill count: %lu\n", tx_refill_count);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  Refill bytes: %lu\n\n", tx_refill_bytes);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  TX queue size: %d\n", NOA_MD_TX_RING_SIZE);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  TX queue cnt: %d\n", NOA_MD_NUM_TX_QUEUES);
	for (int i = 0; i < NOA_MD_NUM_TX_QUEUES; i++) {
		count += scnprintf(buf + count, PAGE_SIZE - count,
			"  TX queue id: %d\n", i);
		count += scnprintf(buf + count, PAGE_SIZE - count,
			"    TX queue head: %d\n", skb_tx_queue_head[i]);
		count += scnprintf(buf + count, PAGE_SIZE - count,
			"    TX queue tail: %d\n", skb_tx_queue_tail[i]);
	}
	// RX static
	count += scnprintf(buf + count, PAGE_SIZE - count, "\nRX static:\n");
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  Packets: %lu\n", rx_count);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  Bytes: %lu\n\n", rx_bytes);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  Refill count: %lu\n", rx_refill_count);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  Refill bytes: %lu\n\n", rx_refill_bytes);

	return count;
}

static ssize_t noa_md_netdevs_show(
	struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
	ssize_t count = 0;

	for (int i = 0; i < md_dev.netdev_cnt; i++) {
		struct net_device *netdev = md_dev.netdevs[i];
		NOA_MD_INFO("netdevs[%d]=[0x%lx]", i, netdev);
		count += scnprintf(buf + count, PAGE_SIZE - count,
			"netdev %s is_up: %s\n", netdev->name,
			(netdev->flags & IFF_UP) ? "yes" : "no");
		if (netdev->flags & IFF_UP) {
			noa_md_get_ndev_info(netdev, (buf + count), (PAGE_SIZE - count));
		}
	}

	return count;
}

static ssize_t noa_md_trace_stats_show(
	struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
	ssize_t count = 0;

	count += scnprintf(buf + count, PAGE_SIZE - count,
		"NOA MD Version: %s\n\n", NOA_MD_MODULE_VERSION);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"Trace buffer info(MD):\n");
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  read_pos: %lld\n", md_dev.md_tlog->read_pos);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  write_pos: %lld\n", md_dev.md_tlog->write_pos);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  buffer_size: %zu\n", md_dev.md_tlog->buffer_size);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  trace_log_count: %llu\n",
		atomic64_read(&md_dev.md_tlog->trace_log_count));
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  trace_data_log_count: %llu\n",
		atomic64_read(&md_dev.md_tlog->trace_data_log_count));
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  output_active: %d\n\n", atomic_read(&md_dev.md_tlog->output_active));
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"Trace buffer info(NCP):\n");
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  read_pos: %lld\n", md_dev.ncp_tlog->read_pos);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  write_pos: %lld\n", md_dev.ncp_tlog->write_pos);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  buffer_size: %zu\n", md_dev.ncp_tlog->buffer_size);
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  trace_log_count: %llu\n",
		atomic64_read(&md_dev.ncp_tlog->trace_log_count));
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  trace_data_log_count: %llu\n",
		atomic64_read(&md_dev.ncp_tlog->trace_data_log_count));
	count += scnprintf(buf + count, PAGE_SIZE - count,
		"  output_active: %d\n\n", atomic_read(&md_dev.ncp_tlog->output_active));

	return count;
}

static ssize_t noa_md_log_level_show(
		struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE,
			"Current log level: %d\n"
			"Available log levels (and their corresponding values):\n"
			"  ERROR:   0\n"
			"  WARNING: 1\n"
			"  INFO:    2\n"
			"  DEBUG:   3\n",
			noa_md_log_level);
}

static ssize_t noa_md_log_level_store(
		struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	int level;
	int ret;

	ret = kstrtoint(buf, 10, &level);
	if (ret < 0) {
		NOA_MD_ERROR("Invalid log level input: %s", buf);
		return ret;
	}

	if (level < NOA_MD_LOG_LEVEL_ERROR || level >= NOA_MD_LOG_LEVEL_MAX) {
		NOA_MD_ERROR("Invalid log level: %d", level);
		return -EINVAL;
	}

	noa_md_log_level = level;

	NOA_MD_INFO("Log level set to: %s",
		 noa_md_log_level_str[noa_md_log_level]);

	return count;
}

static ssize_t noa_md_noa_enabled_show(
	struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct noa_md_trace *trace = container_of(kobj, struct noa_md_trace,
		trace_kobj);
	return scnprintf(buf, PAGE_SIZE, "%d\n",
		trace->md_dev->feature_ctrl.enabled);
}

static ssize_t noa_md_noa_enabled_store(
	struct kobject *kobj, struct kobj_attribute *attr,
	const char *buf, size_t count)
{
	struct noa_md_trace *trace = container_of(kobj, struct noa_md_trace,
		trace_kobj);
	struct noa_md_dev *dev = trace->md_dev;
	int enabled;
	int ret;

	ret = kstrtoint(buf, 10, &enabled);
	if (ret < 0) {
		NOA_MD_ERROR("Invalid noa_enabled input: %s", buf);
		return ret;
	}

	if (enabled != 0 && enabled != 1) {
		NOA_MD_ERROR("Invalid enabled value: %d", enabled);
		return -EINVAL;
	}

	dev->feature_ctrl.enabled = enabled;
	NOA_MD_INFO("enabled set to: %d", dev->feature_ctrl.enabled);

	return count;
}

static ssize_t noa_md_unified_desc_enabled_show(
	struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct noa_md_trace *trace = container_of(kobj, struct noa_md_trace,
		trace_kobj);
	return scnprintf(buf, PAGE_SIZE, "%d\n",
		trace->md_dev->feature_ctrl.unified_desc_enabled);
}

static ssize_t noa_md_unified_desc_enabled_store(
	struct kobject *kobj, struct kobj_attribute *attr,
	const char *buf, size_t count)
{
	struct noa_md_trace *trace = container_of(kobj, struct noa_md_trace,
		trace_kobj);
	struct noa_md_dev *dev = trace->md_dev;
	int unified_desc_enabled;
	int ret;

	ret = kstrtoint(buf, 10, &unified_desc_enabled);
	if (ret < 0) {
		NOA_MD_ERROR("Invalid unified_desc_enabled input: %s", buf);
		return ret;
	}

	if (unified_desc_enabled != 0 && unified_desc_enabled != 1) {
		NOA_MD_ERROR("Invalid unified_desc_enabled value: %d",
			unified_desc_enabled);
		return -EINVAL;
	}

	dev->feature_ctrl.unified_desc_enabled = unified_desc_enabled;
	NOA_MD_INFO("unified_desc_enabled set to: %d",
		dev->feature_ctrl.unified_desc_enabled);

	return count;
}

static struct kobj_attribute noa_md_trace_stats_attr =
	__ATTR_RO(noa_md_trace_stats); // Define stats sysfs attr
static struct kobj_attribute noa_md_info_attr =
	__ATTR_RO(noa_md_info); // Define information sysfs attr
static struct kobj_attribute noa_md_stats_attr =
	__ATTR_RO(noa_md_stats); // Define stats sysfs attr
static struct kobj_attribute noa_md_netdevs_attr =
	__ATTR_RO(noa_md_netdevs); // Define netdev sysfs attr
static struct kobj_attribute noa_md_log_level_attr =
	__ATTR(log_level, 0664, noa_md_log_level_show, noa_md_log_level_store);
static struct kobj_attribute noa_md_noa_enabled_attr =
	__ATTR(noa_enabled, 0664, noa_md_noa_enabled_show,
		noa_md_noa_enabled_store);
static struct kobj_attribute noa_md_unified_desc_enabled_attr =
	__ATTR(unified_desc_enabled, 0664, noa_md_unified_desc_enabled_show,
		noa_md_unified_desc_enabled_store);

static ssize_t noa_md_trace_dpath_state_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	struct noa_md_dpath_ctrl *ctrl = md_dev.dpath_ctrl;
	ssize_t len;

	if (!ctrl)
		return sysfs_emit(buf, "Controller not initialized\n");

	mutex_lock(&ctrl->lock);
	len = sysfs_emit(buf, "%s\n",
		noa_md_dpath_ctrl_state_to_str(ctrl->current_state));
	mutex_unlock(&ctrl->lock);

	return len;
}

static ssize_t noa_md_trace_dpath_clients_status_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	struct noa_md_dpath_ctrl *ctrl = md_dev.dpath_ctrl;
	struct noa_dpath_client *client;
	ssize_t len = 0;

	if (!ctrl)
		return sysfs_emit(buf, "Controller not initialized\n");

	len += sysfs_emit_at(buf, len, "CLIENTS STATUS:\n");
	mutex_lock(&ctrl->lock);
	list_for_each_entry(client, &ctrl->client_list, node) {
		len += sysfs_emit_at(buf, len, "- client: %d, status: %d\n",
				   client->type, atomic_read(&client->status));
	}
	mutex_unlock(&ctrl->lock);

	return len;
}

static ssize_t noa_md_trace_dpath_stats_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	struct noa_md_dpath_ctrl *ctrl = md_dev.dpath_ctrl;
	struct noa_dpath_stats stats;
	s64 avg_latency_ns = 0;
	ssize_t len = 0;

	if (!ctrl)
		return sysfs_emit(buf, "Controller not initialized\n");

	mutex_lock(&ctrl->lock);
	stats = ctrl->stats;
	mutex_unlock(&ctrl->lock);

	if (stats.successful_switches > 0)
		avg_latency_ns = div64_s64(stats.total_latency_ns,
			stats.successful_switches);

	len += sysfs_emit_at(buf, len, "SWITCH STATS:\n");
	len += sysfs_emit_at(buf, len, "  successful: %llu\n",
		stats.successful_switches);
	len += sysfs_emit_at(buf, len, "  failed: %llu\n", stats.failed_switches);
	len += sysfs_emit_at(buf, len, "  rollbacks: %llu\n", stats.rollbacks);
	len += sysfs_emit_at(buf, len, "LATENCY (ns):\n");
	len += sysfs_emit_at(buf, len, "  last_ns: %lld\n", stats.last_latency_ns);
	len += sysfs_emit_at(buf, len, "  avg_ns: %lld\n", avg_latency_ns);
	len += sysfs_emit_at(buf, len, "  max_ns: %lld\n", stats.max_latency_ns);
	len += sysfs_emit_at(buf, len, "  min_ns: %lld\n", stats.min_latency_ns);

	return len;
}

static ssize_t noa_md_trace_dpath_switch_state_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
    /* TODO: b/434646935 - Implement based on final shared memory structure */
    return sysfs_emit(buf, "Not implemented yet.\n");
}

static struct kobj_attribute noa_md_trace_dpath_state_attr =
	__ATTR_RO(noa_md_trace_dpath_state);
static struct kobj_attribute noa_md_trace_dpath_clients_status_attr =
	__ATTR_RO(noa_md_trace_dpath_clients_status);
static struct kobj_attribute noa_md_trace_dpath_stats_attr =
	__ATTR_RO(noa_md_trace_dpath_stats);
static struct kobj_attribute noa_md_trace_dpath_switch_state_attr =
	__ATTR_RO(noa_md_trace_dpath_switch_state);

/**
 * noa_md_trace_do_fallback_log() - Fallback to kernel's printk for logging.
 * @log_level: The log level, used to select the appropriate pr_* function.
 * @limit:     If true, use the rate-limited version of the printk function.
 * @message:   The pre-formatted message string to print.
 *
 * This helper function acts as a fallback mechanism to the standard kernel
 * log buffer. It checks the global log level and prints the message using
 * the corresponding pr_* family function.
 */
static inline void noa_md_trace_do_fallback_log(
	enum noa_md_log_level log_level,
	bool limit,
	const char *message)
{
	if (log_level > noa_md_log_level) {
		return;
	}

	switch (log_level) {
	case NOA_MD_LOG_LEVEL_ERROR:
		NOA_MD_TRACE_DO_FALLBACK_LOG(err, limit, message);
		break;
	case NOA_MD_LOG_LEVEL_WARNING:
		NOA_MD_TRACE_DO_FALLBACK_LOG(warn, limit, message);
		break;
	case NOA_MD_LOG_LEVEL_INFO:
	case NOA_MD_LOG_LEVEL_DATA:
		NOA_MD_TRACE_DO_FALLBACK_LOG(info, limit, message);
		break;
	case NOA_MD_LOG_LEVEL_DEBUG:
		NOA_MD_TRACE_DO_FALLBACK_LOG(debug, limit, message);
		break;
	default:
		break;
	}
}

/**
 * noa_md_trace_should_log() - Determines if a log should be written to the
 * custom trace buffer.
 * @tlog:      Pointer to the trace log structure.
 * @log_type:  The type of the log message (e.g., DATA, DEBUG).
 * @log_level: The level of the log message (e.g., INFO, ERROR).
 *
 * This helper encapsulates the decision logic for whether a message is
 * written to the custom in-memory circular buffer. It respects the global
 * log level, as well as specific controls for data and debug logs,
 * such as rate-limiting.
 */
static inline bool noa_md_trace_should_log(
	struct noa_md_trace_log *tlog,
	enum noa_md_log_type log_type,
	enum noa_md_log_level log_level)
{
	if (log_level > noa_md_log_level) {
		return false;
	}

	switch (log_type) {
	case NOA_MD_LOG_TYPE_DATA:
		if (tlog->data_log_ctrl == NOA_MD_DATA_LOG_DISABLE)
			return false;
		if (tlog->data_log_ctrl == NOA_MD_DATA_LOG_ENABLE_LIMIT) {
			if ((atomic64_fetch_add(1, &tlog->trace_data_log_count) %
				NOA_MD_DATA_LOG_LIMIT_INTERVAL) != 0) {
				return false;
			}
		}
		break;
	case NOA_MD_LOG_TYPE_DEBUG:
		if (tlog->debug_log_ctrl == NOA_MD_LOG_DISABLE)
			return false;
		break;
	case NOA_MD_LOG_TYPE_OVERALL:
	default:
		break;
	}

	return true;
}

/**
 * noa_md_trace_write_to_buffer() - Writes a prepared message into the circular buffer.
 * @tlog:    Pointer to the trace log structure.
 * @message: The final, formatted message to write.
 * @length:  The length of the message.
 *
 * This function handles the spinlock, copies the message into the circular
 * buffer, and correctly manages the write position, including wrapping around
 * the end of the buffer.
 */
static inline void noa_md_trace_write_to_buffer(
	struct noa_md_trace_log *tlog,
	const char *message,
	int length)
{
	unsigned long flags;

	spin_lock_irqsave(&tlog->lock, flags);

	int write_pos = tlog->write_pos;
	char *write_start = tlog->buffer + write_pos;
	int free_len = tlog->buffer_size - write_pos;

	/* Handle wraparound for the circular buffer */
	if (length > free_len) {
		int first_part_len = free_len;
		int second_part_len = length - first_part_len;

		memcpy(write_start, message, first_part_len);
		memcpy(tlog->buffer, message + first_part_len, second_part_len);
		tlog->write_pos = second_part_len;
	} else {
		memcpy(write_start, message, length);
		tlog->write_pos = (write_pos + length) % tlog->buffer_size;
	}

	atomic64_inc(&tlog->trace_log_count);

	spin_unlock_irqrestore(&tlog->lock, flags);
}

void noa_md_add_trace(
	void *dev_data,
	enum noa_md_log_type log_type,
	enum noa_md_log_level log_level,
	bool limit,
	const char *fmt, ...)
{
	struct noa_md_trace_log *tlog = (struct noa_md_trace_log *)dev_data;
	char log_buf[NOA_MD_LOG_ENTRY_MAX_LEN]; /* Single buffer for all operations */
	const char *message_only;
	int timestamp_len, message_len, final_len;
	struct timespec64 tv;
	va_list args;

	/* Write the timestamp into the beginning of the buffer. */
	ktime_get_real_ts64(&tv);
	timestamp_len = scnprintf(log_buf, TIMESTAMP_LEN, "[%lld.%06ld] ",
		(long long)tv.tv_sec, tv.tv_nsec / 1000);
	message_only = log_buf + timestamp_len;

	/* Format the raw message directly after the timestamp. */
	va_start(args, fmt);
	message_len = vsnprintf(log_buf + timestamp_len,
		sizeof(log_buf) - timestamp_len, fmt, args);
	va_end(args);

	final_len = timestamp_len + message_len;

	/* Append a newline and handle potential truncation. */
	if (final_len < sizeof(log_buf)) {
		log_buf[final_len] = '\n';
		final_len++;
	} else {
		log_buf[sizeof(log_buf) - 2] = '\n';
		log_buf[sizeof(log_buf) - 1] = '\0';
		final_len = sizeof(log_buf);
		pr_warn_ratelimited("noa_md_add_trace: Message truncated.\n");
	}

	if (!tlog || !tlog->buffer) {
		/* Pass pointer starting after the timestamp for a cleaner log. */
		noa_md_trace_do_fallback_log(log_level, limit, message_only);
		return;
	}

	if (!noa_md_trace_should_log(tlog, log_type, log_level)) {
		return;
	}

	/* Pass pointer starting after the timestamp for a cleaner log. */
	noa_md_trace_do_fallback_log(log_level, limit, message_only);

	if (final_len >= tlog->buffer_size) {
		pr_warn_ratelimited(
			"noa_md_add_trace: Message too long, message_buf=[%s]\n", log_buf);
		return;
	}

	/* Pass the pointer to the START of the buffer to include the timestamp. */
	noa_md_trace_write_to_buffer(tlog, log_buf, final_len);

	wake_up_interruptible(&tlog->wq);
}
EXPORT_SYMBOL(noa_md_add_trace);

static ssize_t noa_md_msg_read(
		struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct noa_md_trace_log *tlog =
		(struct noa_md_trace_log *)file->private_data;
	ssize_t bytes_read = 0;
	size_t bytes_to_read;
	loff_t read_pos;
	loff_t write_pos;
	int wait_result;

	if (!tlog || atomic_read(&tlog->output_active) == 0) {
		pr_info("noa_md_msg_read: stop output");
		return 0;
	}

	wait_result = wait_event_interruptible(
			tlog->wq, (tlog->read_pos != tlog->write_pos));
	if (wait_result == -ERESTARTSYS) {
		return -EINTR;
	}
	spin_lock(&tlog->lock);
	memset(tlog->msg_output_str, 0, PAGE_SIZE);

	read_pos = tlog->read_pos;
	write_pos = tlog->write_pos;

	while (bytes_read < PAGE_SIZE && read_pos != write_pos) {
		if (read_pos > write_pos) {
			bytes_to_read = tlog->buffer_size - read_pos;
		} else {
			bytes_to_read = write_pos - read_pos;
		}

		bytes_to_read = min(bytes_to_read, PAGE_SIZE - bytes_read);

		memcpy(
			tlog->msg_output_str + bytes_read,
			tlog->buffer + read_pos,
			bytes_to_read);
		bytes_read += bytes_to_read;
		read_pos = (read_pos + bytes_to_read) % tlog->buffer_size;
	}

	tlog->read_pos = read_pos;

	spin_unlock(&tlog->lock);

	if (copy_to_user(buf, tlog->msg_output_str, bytes_read)) {
		atomic_set(&tlog->output_active, 0);
		pr_info("noa_md_msg_read: copy_to_user fail");
		return -EFAULT;
	}

	return bytes_read;
}

static unsigned int noa_md_msg_poll(struct file *file, poll_table *wait)
{
	struct noa_md_trace_log *tlog =
		(struct noa_md_trace_log *)file->private_data;
	if (!tlog) {
		pr_err("noa_md_msg_poll: tlog null");
		return 0;
	}
	pr_info("noa_md_msg_poll: poll_wait+");
	poll_wait(file, &tlog->wq, wait);
	pr_info("noa_md_msg_poll: poll_wait-");
	if (atomic_read(&tlog->output_active) == 0) {
		pr_info("noa_md_msg_poll: return 1");
		return 0;
	}
	return POLLIN | POLLRDNORM;
}

static int noa_md_msg_open(struct inode *inode, struct file *file)
{
	struct noa_md_trace_log *tlog = (struct noa_md_trace_log *)pde_data(inode);
	if (!tlog) {
		pr_err("noa_md_msg_open: tlog null");
		return 0;
	}
	file->private_data = tlog;
	pr_info("noa_md_msg_open");
	// Place here the initialization operations that need to be performed
	// when opening /proc/noa_md_msg
	atomic_set(&tlog->output_active, 1);
	return 0;
}

static int noa_md_msg_release(struct inode *inode, struct file *file)
{
	struct noa_md_trace_log *tlog =
		(struct noa_md_trace_log *)file->private_data;
	if (!tlog) {
		pr_err("noa_md_msg_release: tlog null");
		return 0;
	}
	pr_info("noa_md_msg_release");
	// Place here the cleanup operations that need to be performed
	// when closing /proc/noa_md_msg
	atomic_set(&tlog->output_active, 0);
	wake_up_interruptible(&tlog->wq);
	return 0;
}

static const struct proc_ops noa_md_msg_fops = {
	.proc_open = noa_md_msg_open,
	.proc_read = noa_md_msg_read,
	.proc_lseek = seq_lseek,
	.proc_release = noa_md_msg_release,
	.proc_poll = noa_md_msg_poll,
};

static const char * const noa_md_log_ctrl_str[] = {
	[NOA_MD_LOG_DISABLE] = "disable",
	[NOA_MD_LOG_ENABLE] = "enable",
};

static const char * const noa_md_data_log_ctrl_str[] = {
	[NOA_MD_DATA_LOG_DISABLE] = "disable",
	[NOA_MD_DATA_LOG_ENABLE] = "enable",
	[NOA_MD_DATA_LOG_ENABLE_LIMIT] = "enable_limit",
};

static ssize_t debug_log_ctrl_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	struct noa_md_trace_log *tlog =
		container_of(kobj, struct noa_md_trace_log, kobj);
	int len = 0;
	int i;

	len += sysfs_emit_at(buf, len, "%s\n",
			  noa_md_log_ctrl_str[tlog->debug_log_ctrl]);
	len += sysfs_emit_at(buf, len, "Available values:\n");
	for (i = 0; i < ARRAY_SIZE(noa_md_log_ctrl_str); i++) {
		len += sysfs_emit_at(buf, len, "  %s\n", noa_md_log_ctrl_str[i]);
	}
	return len;
}


static ssize_t debug_log_ctrl_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct noa_md_trace_log *tlog =
		container_of(kobj, struct noa_md_trace_log, kobj);
	int i;

	for (i = 0; i < ARRAY_SIZE(noa_md_log_ctrl_str); i++) {
		if (sysfs_streq(buf, noa_md_log_ctrl_str[i])) {
			tlog->debug_log_ctrl = i;
			return count;
		}
	}
	return -EINVAL;
}

static struct kobj_attribute noa_md_debug_log_ctrl_attr =
	__ATTR(debug_log_ctrl, 0644, debug_log_ctrl_show,
		debug_log_ctrl_store);

static ssize_t data_log_ctrl_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	struct noa_md_trace_log *tlog =
		container_of(kobj, struct noa_md_trace_log, kobj);
	int len = 0;
	int i;

	len += sysfs_emit_at(buf, len, "%s\n",
			  noa_md_data_log_ctrl_str[tlog->data_log_ctrl]);
	len += sysfs_emit_at(buf, len, "Available values:\n");
	for (i = 0; i < ARRAY_SIZE(noa_md_data_log_ctrl_str); i++) {
		len += sysfs_emit_at(buf, len, "  %s\n",
			noa_md_data_log_ctrl_str[i]);
	}
	return len;
}

static ssize_t data_log_ctrl_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct noa_md_trace_log *tlog =
		container_of(kobj, struct noa_md_trace_log, kobj);
	int i;

	for (i = 0; i < ARRAY_SIZE(noa_md_data_log_ctrl_str); i++) {
		if (sysfs_streq(buf, noa_md_data_log_ctrl_str[i])) {
			tlog->data_log_ctrl = i;
			return count;
		}
	}
	return -EINVAL;
}

static struct kobj_attribute noa_md_data_log_ctrl_attr =
	__ATTR(data_log_ctrl, 0644, data_log_ctrl_show,
		data_log_ctrl_store);

static void noa_md_tlog_release(struct kobject *kobj)
{
	// The container of tlog is freed in noa_md_trace_log_destroy
}

static struct kobj_type noa_md_tlog_ktype = {
	.release = noa_md_tlog_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

/**
 * noa_md_trace_sysfs_create() - Create sysfs entries for a trace log.
 * @tlog: The trace log instance to create sysfs files for.
 *
 * This function creates a directory under /sys/devices/virtual/noa_md/trace/
 * named after the tlog, and populates it with control files like
 * 'data_log_ctrl' and 'debug_log_ctrl'.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int noa_md_trace_sysfs_create(struct noa_md_trace_log *tlog)
{
	int ret;

	if (!md_trace) {
		NOA_MD_ERROR("Cannot create sysfs files, main trace kobj is not ready");
		return -EINVAL;
	}

	/* Create a directory for this tlog under the main trace kobject */
	tlog->kobj.ktype = &noa_md_tlog_ktype;
	ret = kobject_init_and_add(&tlog->kobj, &noa_md_tlog_ktype,
		&md_trace->trace_kobj, tlog->name);
	if (ret) {
		kobject_put(&tlog->kobj);
		NOA_MD_ERROR("Failed to create tlog kobject for %s", tlog->name);
		return ret;
	}

	ret = sysfs_create_file(&tlog->kobj,
				&noa_md_debug_log_ctrl_attr.attr);
	if (ret)
		NOA_MD_ERROR("Failed to create debug_log_ctrl for %s", tlog->name);

	ret = sysfs_create_file(&tlog->kobj,
				&noa_md_data_log_ctrl_attr.attr);
	if (ret)
		NOA_MD_ERROR("Failed to create data_log_ctrl for %s", tlog->name);

	return 0;
}

/**
 * noa_md_trace_procfs_create() - Create a procfs entry for a trace log.
 * @tlog: The trace log instance.
 *
 * This function creates a file in /proc/noa/ named after the tlog,
 * which can be read to get the log messages.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int noa_md_trace_procfs_create(struct noa_md_trace_log *tlog)
{
	struct proc_dir_entry *log_dir = NULL;
	struct proc_dir_entry *entry;

	if (md_trace)
		log_dir = md_trace->log_dir;

	entry = proc_create_data(tlog->name, 0, log_dir, &noa_md_msg_fops, tlog);

	if (!entry) {
		NOA_MD_ERROR("Failed to create proc entry for %s", tlog->name);
		return -ENOMEM;
	}

	return 0;
}

static void noa_md_trace_sysfs_remove(struct noa_md_trace_log *tlog)
{
	kobject_put(&tlog->kobj);
}

static void noa_md_trace_procfs_remove(struct noa_md_trace_log *tlog)
{
	if (md_trace && md_trace->log_dir)
		remove_proc_entry(tlog->name, md_trace->log_dir);
}

/**
 * noa_md_trace_log_alloc() - Allocate and initialize a trace log instance.
 * @name: The name for the log, used in procfs and sysfs.
 * @size: The requested buffer size. If 0, a default size is used.
 *
 * This function allocates a noa_md_trace_log structure and its internal
 * buffer. It initializes the spinlock, wait queue, and default control values.
 * It does NOT create any procfs or sysfs interfaces.
 *
 * Return: A pointer to the allocated noa_md_trace_log structure,
 * or NULL on failure.
 */
static struct noa_md_trace_log* noa_md_trace_log_alloc(
	const char *name, size_t size)
{
	struct noa_md_trace_log *tlog;

	/* Alloc noa_md_trace struct */
	tlog = kzalloc(sizeof(*tlog), GFP_KERNEL);
	if (!tlog) {
		NOA_MD_ERROR("Failed to allocate memory for trace log");
		return NULL;
	}

	strscpy(tlog->name, name, sizeof(tlog->name));
	spin_lock_init(&tlog->lock);
	init_waitqueue_head(&tlog->wq);

	tlog->write_pos = 0;  /* Initialize write position */
	tlog->read_pos = 0;   /* Initialize read position */
	atomic64_set(&tlog->trace_log_count, 0);
	atomic64_set(&tlog->trace_data_log_count, 0);

	/* Set buffer size (adjustable) */
	tlog->buffer_size = (size > 0) ? size : NOA_MD_TLOG_DEFAULT_SIZE;
	tlog->buffer = kzalloc(tlog->buffer_size, GFP_KERNEL);
	if (!tlog->buffer) {
		NOA_MD_ERROR("Failed to allocate memory for trace buffer");
		kfree(tlog);
		return NULL;
	}

	/* Set default log control values */
	tlog->data_log_ctrl = DEFAULT_DATA_LOG_CTRL;
	tlog->debug_log_ctrl = DEFAULT_DEBUG_LOG_CTRL;

	NOA_MD_INFO("Allocated tlog '%s'", tlog->name);

	return tlog;
}

void noa_md_trace_log_destroy(struct noa_md_trace_log *tlog)
{
	if (!tlog) {
		return;
	}

	noa_md_trace_sysfs_remove(tlog);
	noa_md_trace_procfs_remove(tlog);

	atomic_set(&tlog->output_active, 0);
	wake_up_interruptible_all(&tlog->wq);

	if (tlog->buffer) {
		kfree(tlog->buffer);
		tlog->buffer = NULL;
	}

	kfree(tlog);
}
EXPORT_SYMBOL(noa_md_trace_log_destroy);

/**
 * noa_md_trace_log_init() - Initialize a trace log instance.
 * @name: The name for the log, used in procfs and sysfs.
 * @size: The requested buffer size. Use 0 for default.
 *
 * This function allocates and initializes a circular buffer for logging,
 * and creates corresponding entries in /proc/noa/ and
 * /sys/devices/virtual/noa_md/trace/ for user-space interaction.
 *
 * To enable logging, you can write to the control files, for example:
 * echo "enable" > /sys/devices/virtual/noa_md/trace/noa_md_msg/debug_log_ctrl
 *
 * To read the log, you can use cat:
 * cat /proc/noa/noa_md_msg
 *
 * Return: A valid pointer on success, or an error pointer on failure.
 */
struct noa_md_trace_log* noa_md_trace_log_init(char *name, size_t size)
{
	struct noa_md_trace_log *tlog;
	int ret;

	if (!name) {
		NOA_MD_ERROR("Name is null");
		return ERR_PTR(-EINVAL);
	}

	/* 1. Allocate and initialize the core structure */
	tlog = noa_md_trace_log_alloc(name, size);
	if (!tlog) {
		return ERR_PTR(-ENOMEM);
	}

	/* 2. Create procfs interface. Cleanup on failure. */
	ret = noa_md_trace_procfs_create(tlog);
	if (ret)
		goto err_free_tlog;

	/* 3. Create sysfs interface. Cleanup on failure. */
	ret = noa_md_trace_sysfs_create(tlog);
	if (ret)
		goto err_free_tlog;

	NOA_MD_INFO("Init done");
	return tlog;
err_free_tlog:
	noa_md_trace_log_destroy(tlog);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(noa_md_trace_log_init);

/* Release function for the main trace kobject */
static void noa_md_trace_release(struct kobject *kobj)
{
	struct noa_md_trace *trace = container_of(kobj, struct noa_md_trace,
		trace_kobj);
	kfree(trace);
}

static struct kobj_type kobj_type_trace = {
	.release	= noa_md_trace_release,
	.sysfs_ops	= &kobj_sysfs_ops,
};

struct noa_md_trace* noa_md_trace_init(void *dev_data)
{
	struct noa_md_trace *trace;
	struct noa_md_dev* dev = (struct noa_md_dev*)dev_data;
	int ret = 0;

	// Alloc noa_md_trace struct
	trace = kzalloc(sizeof(struct noa_md_trace), GFP_KERNEL);
	if (!trace) {
		NOA_MD_ERROR("Failed to allocate memory for trace buffer.");
		return NULL;
	}

	spin_lock_init(&trace->lock);
	trace->md_dev = dev;

	/* Initialize and add the kobject embedded in our trace struct */
	ret = kobject_init_and_add(&trace->trace_kobj, &kobj_type_trace,
		dev->dev->kobj.parent, "trace");
	if (ret) {
		NOA_MD_ERROR("Failed to create trace kobject.");
		kobject_put(&trace->trace_kobj);
		kfree(trace);
		return NULL;
	}

	// Create noa_md_trace & noa_md_stats
	ret |= sysfs_create_file(&trace->trace_kobj,
		&noa_md_trace_stats_attr.attr);
	ret |= sysfs_create_file(&trace->trace_kobj,
		&noa_md_info_attr.attr);
	ret |= sysfs_create_file(&trace->trace_kobj,
		&noa_md_stats_attr.attr);
	ret |= sysfs_create_file(&trace->trace_kobj,
		&noa_md_netdevs_attr.attr);
	ret |= sysfs_create_file(&trace->trace_kobj,
		&noa_md_log_level_attr.attr);
	ret |= sysfs_create_file(&trace->trace_kobj,
		&noa_md_noa_enabled_attr.attr);
	ret |= sysfs_create_file(&trace->trace_kobj,
		&noa_md_unified_desc_enabled_attr.attr);

	ret |= sysfs_create_file(&trace->trace_kobj, &noa_md_trace_dpath_state_attr.attr);
	ret |= sysfs_create_file(&trace->trace_kobj, &noa_md_trace_dpath_clients_status_attr.attr);
	ret |= sysfs_create_file(&trace->trace_kobj, &noa_md_trace_dpath_stats_attr.attr);
	ret |= sysfs_create_file(&trace->trace_kobj, &noa_md_trace_dpath_switch_state_attr.attr);

	if (ret) {
		NOA_MD_ERROR("Failed to create trace file in sysfs.\n");
		kobject_put(&trace->trace_kobj);
		kfree(trace);
		return NULL;
	}

	if (!trace->log_dir) {
		trace->log_dir = proc_mkdir("noa", NULL);
	}

	md_trace = trace;

	NOA_MD_INFO("exit");
	return trace;
}

void noa_md_trace_exit(void)
{
	NOA_MD_INFO("enter");
	if (!md_trace) {
		NOA_MD_INFO("exit, md_trace is null");
		return;
	}

	// Remove kobject
	kobject_put(&md_trace->trace_kobj);

	if (md_trace->log_dir) {
		remove_proc_entry("noa", NULL);
	}

	kfree(md_trace);
	md_trace = NULL;

	NOA_MD_INFO("exit");
}
