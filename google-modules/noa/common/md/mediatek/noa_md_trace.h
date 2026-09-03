// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 *
 * This file implements the Modem RPC interface for the NOA Mediatek Modem Driver.
 */
#ifndef __NOA_MD_TRACE_H__
#define __NOA_MD_TRACE_H__

#include <linux/atomic.h>
#include <linux/if_link.h>  // For rtnl_link_stats64

enum noa_md_log_ctrl {
	NOA_MD_LOG_DISABLE = 0,
	NOA_MD_LOG_ENABLE,
};

enum noa_md_data_log_ctrl {
	NOA_MD_DATA_LOG_DISABLE = 0,
	NOA_MD_DATA_LOG_ENABLE,
	NOA_MD_DATA_LOG_ENABLE_LIMIT,
};

enum noa_md_log_type {
	NOA_MD_LOG_TYPE_OVERALL = 0,
	NOA_MD_LOG_TYPE_DATA,
	NOA_MD_LOG_TYPE_DEBUG,
};

struct noa_md_trace_log {
	char name[64];
	struct kobject kobj;
	spinlock_t lock;
	char *buffer;
	size_t buffer_size;
	char msg_output_str[PAGE_SIZE];
	atomic64_t trace_log_count;
	atomic64_t trace_data_log_count;
	atomic_t output_active;
	loff_t write_pos;
	loff_t read_pos;
	wait_queue_head_t wq;
	enum noa_md_data_log_ctrl data_log_ctrl;
	enum noa_md_log_ctrl debug_log_ctrl;
};

struct noa_md_trace {
	spinlock_t lock;
	struct noa_md_dev *md_dev;
	struct proc_dir_entry *log_dir;
	struct kobject trace_kobj;
	unsigned long tx_count;
	unsigned long tx_bytes;
	unsigned long tx_dropped_count;
	unsigned long tx_dropped_bytes;
	unsigned long tx_refill_count;
	unsigned long tx_refill_bytes;
	unsigned long rx_count;
	unsigned long rx_bytes;
	unsigned long rx_refill_count;
	unsigned long rx_refill_bytes;
	unsigned long rx_dropped_count;
	unsigned long rx_dropped_bytes;
};

enum noa_md_log_level {
	NOA_MD_LOG_LEVEL_ERROR = 0,
	NOA_MD_LOG_LEVEL_WARNING,
	NOA_MD_LOG_LEVEL_INFO,
	NOA_MD_LOG_LEVEL_DATA,
	NOA_MD_LOG_LEVEL_DEBUG,
	NOA_MD_LOG_LEVEL_MAX,
};

void noa_trace_tx_inc(struct rtnl_link_stats64 *stats, unsigned long bytes);
void noa_trace_tx_drop_inc(struct rtnl_link_stats64 *stats, unsigned long bytes);
void noa_trace_tx_refill_inc(unsigned long bytes);
void noa_trace_rx_inc(struct rtnl_link_stats64 *stats, unsigned long bytes);
void noa_trace_rx_drop_inc(struct rtnl_link_stats64 *stats, unsigned long bytes);
void noa_trace_rx_refill_inc(unsigned long bytes);
void noa_md_add_trace(
	void *dev_data, enum noa_md_log_type log_type,
	enum noa_md_log_level log_level, bool limit, const char *fmt, ...);
struct noa_md_trace_log* noa_md_trace_log_init(char *name, size_t size);
void noa_md_trace_log_destroy(struct noa_md_trace_log *tlog);
struct noa_md_trace* noa_md_trace_init(void *dev_data);
void noa_md_trace_exit(void);

#define NOA_MD_TRACE_DO_FALLBACK_LOG(_base, _limit, _msg) \
	do { \
		if (_limit) \
			pr_##_base##_ratelimited("%s", (_msg)); \
		else \
			pr_##_base("%s", (_msg)); \
	} while (0)

#define NOA_MD_TRACE(module, module_name, log_type, log_level, limit, \
	trans_type, fmt, ...) \
	do { \
		struct noa_md_trace_log *tlog = md_dev.module##_tlog; \
		if (tlog) { \
			noa_md_add_trace((void*)tlog, log_type, log_level, \
				limit, "[%c]["#module_name"]%s %s[%d]:" fmt, \
				((log_level) == NOA_MD_LOG_LEVEL_ERROR ? 'E' : \
				 (log_level) == NOA_MD_LOG_LEVEL_WARNING ? 'W' : \
				 (log_level) == NOA_MD_LOG_LEVEL_INFO ? 'I' : \
				 (log_level) == NOA_MD_LOG_LEVEL_DATA ? 'D' : 'd'), \
				trans_type, __func__, \
				__LINE__, ##__VA_ARGS__); \
		} \
	} while (0)

#define _NOA_MD_TRACE_DISABLE_EXEC( \
	print_func, print_func_limited, log_char, \
	module_name, limit, trans_type, fmt, ...) \
	do { \
		if (limit) { \
			print_func_limited( \
				"[%c]["#module_name"]%s %s[%d]:" fmt, \
				log_char, trans_type, __func__, \
				__LINE__, ##__VA_ARGS__); \
		} else { \
			print_func( \
				"[%c]["#module_name"]%s %s[%d]:" fmt, \
				log_char, trans_type, __func__, \
				__LINE__, ##__VA_ARGS__); \
		} \
	} while (0)

#define _NOA_LOG_NOA_MD_LOG_LEVEL_ERROR(...) \
	_NOA_MD_TRACE_DISABLE_EXEC(pr_err, pr_err_ratelimited, 'E', __VA_ARGS__)

#define _NOA_LOG_NOA_MD_LOG_LEVEL_WARNING(...) \
	_NOA_MD_TRACE_DISABLE_EXEC(pr_warn, pr_warn_ratelimited, 'W', __VA_ARGS__)

#define _NOA_LOG_NOA_MD_LOG_LEVEL_INFO(...) \
	_NOA_MD_TRACE_DISABLE_EXEC(pr_info, pr_info_ratelimited, 'I', __VA_ARGS__)

#define _NOA_LOG_NOA_MD_LOG_LEVEL_DATA(...) \
	_NOA_MD_TRACE_DISABLE_EXEC(pr_debug, pr_debug_ratelimited, 'D', __VA_ARGS__)

#define _NOA_LOG_NOA_MD_LOG_LEVEL_DEBUG(...) \
	_NOA_MD_TRACE_DISABLE_EXEC(pr_debug, pr_debug_ratelimited, 'd', __VA_ARGS__)

#define NOA_MD_TRACE_DISABLE(module_name, log_level, limit, trans_type, fmt, ...) \
	_NOA_LOG_##log_level(module_name, limit, trans_type, fmt, ##__VA_ARGS__)

#define NOA_MD_DEBUG(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_DEBUG, \
		NOA_MD_LOG_LEVEL_DEBUG, false, "", fmt, ##__VA_ARGS__)
#define NOA_MD_INFO(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_INFO, false, "", fmt, ##__VA_ARGS__)
#define NOA_MD_ERROR(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_ERROR, false, "", fmt, ##__VA_ARGS__)
#define NOA_MD_DATA(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_DATA, \
		NOA_MD_LOG_LEVEL_DATA, false, "", fmt, ##__VA_ARGS__)
#define NOA_MD_DATA_LIMIT(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_DATA, \
		NOA_MD_LOG_LEVEL_DATA, true, "", fmt, ##__VA_ARGS__)
#define NOA_MD_WPR_INFO(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD_WPR, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_INFO, false, "[WPR]", fmt, ##__VA_ARGS__)
#define NOA_MD_WPR_ERROR(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD_WPR, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_ERROR, false, "[WPR]", fmt, ##__VA_ARGS__)
#define NOA_MD_WPR_DEBUG(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD_WPR, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_DEBUG, false, "[WPR]", fmt, ##__VA_ARGS__)
#define NOA_MD_WPR_DATA(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD_WPR, NOA_MD_LOG_TYPE_DATA, \
		NOA_MD_LOG_LEVEL_DATA, false, "[WPR]", fmt, ##__VA_ARGS__)
#define NOA_MD_WPR_DATA_LIMIT(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD_WPR, NOA_MD_LOG_TYPE_DATA, \
		NOA_MD_LOG_LEVEL_DATA, true, "[WPR]", fmt, ##__VA_ARGS__)
#define NOA_MD_TX_INFO(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_INFO, false, "[TX]", fmt, ##__VA_ARGS__)
#define NOA_MD_TX_ERROR(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_ERROR, false, "[TX]", fmt, ##__VA_ARGS__)
#define NOA_MD_TX_DATA(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_DATA, \
		NOA_MD_LOG_LEVEL_DATA, false, "[TX]", fmt, ##__VA_ARGS__)
#define NOA_MD_RX_INFO(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_INFO, false, "[RX]", fmt, ##__VA_ARGS__)
#define NOA_MD_RX_ERROR(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_ERROR, false, "[RX]", fmt, ##__VA_ARGS__)
#define NOA_MD_RX_DATA(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_DATA, \
		NOA_MD_LOG_LEVEL_DATA, false, "[RX]", fmt, ##__VA_ARGS__)
#define NOA_MD_CPATH_INFO(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_INFO, false, "[CPATH]", fmt, ##__VA_ARGS__)
#define NOA_MD_CPATH_ERROR(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_ERROR, false, "[CPATH]", fmt, ##__VA_ARGS__)
#define NOA_MD_CPATH_DATA(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_DATA, \
		NOA_MD_LOG_LEVEL_DATA, false, "[CPATH]", fmt, ##__VA_ARGS__)

#define NOA_MD_DEBUG_LIMIT(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_DEBUG, \
		NOA_MD_LOG_LEVEL_DEBUG, true, "", fmt, ##__VA_ARGS__)
#define NOA_MD_INFO_LIMIT(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_INFO, true, "", fmt, ##__VA_ARGS__)
#define NOA_MD_ERROR_LIMIT(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_ERROR, true, "", fmt, ##__VA_ARGS__)
#define NOA_MD_WRAPPER_INFO_LIMIT(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD_WPR, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_INFO, true, "", fmt, ##__VA_ARGS__)
#define NOA_MD_WRAPPER_ERROR_LIMIT(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD_WPR, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_ERROR, true, "", fmt, ##__VA_ARGS__)
#define NOA_MD_TX_INFO_LIMIT(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_INFO, true, "[TX]", fmt, ##__VA_ARGS__)
#define NOA_MD_TX_ERROR_LIMIT(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_ERROR, true, "[TX]", fmt, ##__VA_ARGS__)
#define NOA_MD_RX_INFO_LIMIT(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_INFO, true, "[RX]", fmt, ##__VA_ARGS__)
#define NOA_MD_RX_ERROR_LIMIT(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_ERROR, true, "[RX]", fmt, ##__VA_ARGS__)
#define NOA_MD_CPATH_INFO_LIMIT(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_INFO, true, "[CPATH]", fmt, ##__VA_ARGS__)
#define NOA_MD_CPATH_ERROR_LIMIT(fmt, ...) \
	NOA_MD_TRACE(md, NOA_MD, NOA_MD_LOG_TYPE_OVERALL, \
		NOA_MD_LOG_LEVEL_ERROR, true, "[CPATH]", fmt, ##__VA_ARGS__)

#define NCP_MD_INFO(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_INFO, false, "", fmt, ##__VA_ARGS__)
#define NCP_MD_DEBUG(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_DEBUG, false, "", fmt, ##__VA_ARGS__)
#define NCP_MD_ERROR(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_ERROR, false, "", fmt, ##__VA_ARGS__)
#define NCP_MD_TX_INFO(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_INFO, false, "[TX]", fmt, ##__VA_ARGS__)
#define NCP_MD_TX_DEBUG(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_DEBUG, false, "[TX]", fmt, ##__VA_ARGS__)
#define NCP_MD_TX_ERROR(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_ERROR, false, "[TX]", fmt, ##__VA_ARGS__)
#define NCP_MD_RX_INFO(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_INFO, false, "[RX]", fmt, ##__VA_ARGS__)
#define NCP_MD_RX_DEBUG(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_DEBUG, false, "[RX]", fmt, ##__VA_ARGS__)
#define NCP_MD_RX_ERROR(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_ERROR, false, "[RX]", fmt, ##__VA_ARGS__)
#define APC2NCP_DEBUG(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_DEBUG, false, "", fmt, ##__VA_ARGS__)
#define NCP_IRQ_DEBUG(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_DEBUG, false, "", fmt, ##__VA_ARGS__)

#define NCP_MD_INFO_LIMIT(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_INFO, true, "", fmt, ##__VA_ARGS__)
#define NCP_MD_DATA_LIMIT(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_DATA, true, "", fmt, ##__VA_ARGS__)
#define NCP_MD_ERROR_LIMIT(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_ERROR, true, "", fmt, ##__VA_ARGS__)
#define NCP_MD_TX_INFO_LIMIT(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_INFO, true, "[TX]", fmt, ##__VA_ARGS__)
#define NCP_MD_TX_ERROR_LIMIT(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_ERROR, true, "[TX]", fmt, ##__VA_ARGS__)
#define NCP_MD_RX_INFO_LIMIT(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_INFO, true, "[RX]", fmt, ##__VA_ARGS__)
#define NCP_MD_RX_ERROR_LIMIT(fmt, ...) \
	NOA_MD_TRACE_DISABLE(NCP_MD, NOA_MD_LOG_LEVEL_ERROR, true, "[RX]", fmt, ##__VA_ARGS__)

#define NOA_CHECK_ACT_GOTO 0
#define NOA_CHECK_ACT_RTN 1
#define NOA_CHECK_ACT_RTN_ERR_PTR 2
#define NOA_CHECK_ACT_RTN_ERR_CODE 3
#define NOA_CHECK_ACT_RTN_ERR_VAL 4

#define CHECK_TRUE_OR_RETURN(condition) \
	if (unlikely(condition)) { \
		NOA_MD_ERROR(#condition); \
		return; \
	}

#define CHECK_TRUE_OR_RETURN_ERR(condition, return_val) \
	if (unlikely(condition)) { \
		NOA_MD_ERROR(#condition); \
		return return_val; \
	}

#define CHECK_TRUE_OR_GOTO_ERR(condition, goto_val) \
if (unlikely(condition)) { \
	NOA_MD_ERROR(#condition); \
	goto goto_val; \
}

#define CHECK_PTR_OR_RETURN(object) \
	if (unlikely(IS_ERR_OR_NULL(object))) { \
		NOA_MD_ERROR(#object " is null"); \
		return; \
	}

#define CHECK_PTR_OR_RETURN_ERR(object, return_val) \
	if (unlikely(IS_ERR_OR_NULL(object))) { \
		NOA_MD_ERROR(#object " is null"); \
		return return_val; \
	}

#define CHECK_PTR_OR_RETURN_LIMIT(object) \
	if (unlikely(IS_ERR_OR_NULL(object))) { \
		NOA_MD_ERROR_LIMIT(#object " is null"); \
		return; \
	}

#define CHECK_PTR_OR_GOTO_ERR(object, goto_val) \
	if (unlikely(IS_ERR_OR_NULL(object))) { \
		NOA_MD_ERROR(#object " is null"); \
		goto goto_val; \
	}

#endif // __NOA_MD_TRACE_H__
