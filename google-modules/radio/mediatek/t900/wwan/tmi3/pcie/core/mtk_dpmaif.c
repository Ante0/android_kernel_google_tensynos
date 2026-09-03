// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#define pr_fmt(fmt) "DATA_TRANS: " fmt

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/freezer.h>
#include <linux/hrtimer.h>
#include <linux/ip.h>
#include <linux/kthread.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/sched/clock.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>
#include <linux/timer.h>
#include <linux/version.h>
#include <net/ip6_checksum.h>
#include <net/ipv6.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
#include <net/page_pool.h>
#else
#include <net/page_pool/helpers.h>
#endif
#include <net/pkt_sched.h>

#include "mtk_data_plane.h"
#include "mtk_debug.h"
#include "mtk_debugfs.h"
#include "mtk_dev.h"
#include "mtk_dpmaif.h"
#include "mtk_dpmaif_drv.h"
#include "mtk_dpmaif_drv_interface.h"
#include "mtk_dpmaif_ring.h"
#include "mtk_except.h"
#include "mtk_frc.h"
#include "mtk_fsm.h"
#include "mtk_pcie_memlog.h"
#include "mtk_pcie_trace.h"
#include "mtk_pm.h"
#include "mtk_statistics.h"
#include "mtk_wwan.h"

#ifdef CONFIG_UT_PCIE_DPMAIF
#include "ut_dpmaif_fake.h"
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_AFFINITY) || \
	IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_FAST_DMA_SYNC) || \
	IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_LRO_SIZE_LIMIT)
#include <linux/debugfs.h>
#include "perf/dpmaif-google.h"
#endif /* CONFIG_GOOGLE_MODEM_DATA_AFFINITY */

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_SOC_BW_QOS)
#include "perf/soc-qos-ext.h"
#endif /* CONFIG_GOOGLE_MODEM_SOC_BW_QOS */

#define TAG "DATA_TRANS"

#define DPMAIF_TPUT_MONITOR_PERIOD_MS   512
#define DPMAIF_REFILL_FIFO_THRESHOLD    1024
#define MTK_DATA_WS_NAME_LEN		32
#define DPMAIF_PIT_CNT_UPDATE_THRESHOLD 60
#define DPMAIF_SKB_TX_WEIGHT		32
#define DPMAIF_REL_BAT_WEIGHT		128
#define DPMAIF_STATS_PERIOD_S		2

/* Interrupt coalesce default value */
#define DPMAIF_DFLT_INTR_RX_COA_FRAMES 0
#define DPMAIF_DFLT_INTR_TX_COA_FRAMES 0
#define DPMAIF_DFLT_INTR_RX_COA_USECS 0
#define DPMAIF_DFLT_INTR_TX_COA_USECS 0

#define DPMAIF_DUMP_DRB_CNT 25
#define DPMAIF_DUMP_PIT_CNT 25
#define DPMAIF_DUMP_BAT_CNT 25

#define DPMAIF_RING_TPUT_CALC(CNT, MS_SHIFT)	((CNT) >> (MS_SHIFT))
#define DPMAIF_MS_TO_NS(MS)	((MS) * 1000000)

/* This length should be equal to ETH_GSTRING_LEN */
#define DATA_TRANS_STRING_LEN 32

#ifdef CONFIG_DATA_TEST_MODE
/* Test mode */
/* MD Tput setting Mask. */
#define DPMAIF_MD_TPUT_MODE_SET_MASK BIT(31)
#define DPMAIF_MD_TPUT_CTL_SET_MASK BIT(30)
#define DPMAIF_MD_TPUT_PKT_SET_MASK BIT(29)
#define DPMAIF_MD_TPUT_TIME_SET_MASK BIT(28)

/*  MD TPUT mode [bit12-15]. */
#define DPMAIF_MD_TPUT_MODE_OFFSET 12
#define DPMAIF_MD_TPUT_MODE_MASK 0x0F
#define DPMAIF_MD_INVALID_MODE 0x0F

/* Start/stop MD DL [bit23]. */
#define DPMAIF_MD_DL_CTL_OFFSET 23
#define DPMAIF_MD_DL_CTL_MASK 0x01

/* pkt_count/ms mask [bit16-22]. */
#define DPMAIF_MD_PKT_OFFSET 16
#define DPMAIF_MD_PKT_MASK 0x7F

/* MD DL test time [bit24-26]. */
#define DPMAIF_MD_DL_TIME_OFFSET 24
#define DPMAIF_MD_DL_TIME_MASK 0x7

struct dpmaif_test_mode_cfg {
	unsigned char md_tput_mode;
	unsigned char md_start_dl_tput;
	unsigned int md_pkt_number_per_ms;
	unsigned char md_dl_tput_test_time; /* 1 -> 10s */
};
#endif

struct dpmaif_tput_stats {
	void *dcb;
	int dl_mode;
	int ul_mode;
	u8 auto_exit_cnt;
	u8 speed_show_cnt;
	bool high_speed;
	/* speed[0 ~ DPMAIF_RXQ_CNT_MAX -1] -> rxq speed
	 * speed[DPMAIF_RXQ_CNT_MAX -1 ~ DPMAIF_TXQ_CNT_MAX -1] -> txq speed
	 */
	u64 speed[DPMAIF_RXQ_CNT_MAX + DPMAIF_TXQ_CNT_MAX];
	u64 dl_speed;
	u64 ul_speed;
	u64 rx_pre_bytes[DPMAIF_RXQ_CNT_MAX];
	u64 rx_cur_bytes[DPMAIF_RXQ_CNT_MAX];
	u64 tx_pre_bytes[DPMAIF_TXQ_CNT_MAX];
	u64 tx_cur_bytes[DPMAIF_TXQ_CNT_MAX];
	struct workqueue_struct *monitor_wq;
	struct delayed_work monitor_work;
	bool work_first_enter;
	u64 pre_clock;
	atomic_t start_monitor;
	bool monitor_enabled;
};

enum dpmaif_state {
	DPMAIF_STATE_MIN,
	DPMAIF_STATE_PWROFF,
	DPMAIF_STATE_PWRON,
	DPMAIF_STATE_MAX
};

struct dpmaif_vq {
	unsigned char srv_id;
	unsigned char q_id;
	u32 max_len; /* align network tx qdisc 1000 */
	struct sk_buff_head list;
};

struct dpmaif_tx_srv {
	struct mtk_dpmaif_ctlb *dcb;
	unsigned char id;
	int nice;
	wait_queue_head_t wait;
	struct task_struct *srv;

	unsigned long txq_drb_lack_sta;
	unsigned char cur_vq_id;
	unsigned char vq_cnt;
	struct dpmaif_vq *vq[DPMAIF_TXQ_CNT_MAX];
};

struct dpmaif_drb_skb {
	struct sk_buff *skb;
	dma_addr_t data_dma_addr;
	unsigned short data_len;
	unsigned short drb_idx:13;
	unsigned short is_msg:1;
	unsigned short is_frag:1;
	unsigned short is_last:1;
};

struct dpmaif_txq {
	struct mtk_dpmaif_ctlb *dcb;
	unsigned char id;
	atomic_t budget;
	atomic_t to_submit_cnt;
	struct dpmaif_pd_drb *drb_base;
	dma_addr_t drb_dma_addr;
	unsigned int drb_cnt;
	unsigned short drb_wr_idx;
	unsigned short drb_rd_idx;
	unsigned short drb_rel_rd_idx;
	unsigned long long dma_map_errs;
	struct dpmaif_drb_skb *sw_drb_base;

	/* txq doorbell configuration */
	unsigned int db_delay_ns;
	unsigned int burst_submit_cnt;
	unsigned int exit_tcp_ss_counter;

	bool drb_poll_enable;
	atomic_t drb_stats;
	struct delayed_work tx_done_work;
	unsigned int intr_coalesce_frame;
	/* lock for multiple interfaces direction call */
	spinlock_t lock;
	unsigned int attr;
};

struct dpmaif_rx_record {
	bool msg_pit_recv;
	struct sk_buff *cur_skb;
	struct sk_buff_head rx_list;
	unsigned int lro_pkt_cnt;
	unsigned int hd_offset;
	unsigned short ip_protocol;
};

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
struct mtk_fifo_t {
	unsigned int size;
	unsigned int mask;

	unsigned int wr ____cacheline_aligned;
	unsigned int rd ____cacheline_aligned;
	void **buf;
};
#endif

struct dpmaif_task_ctlb {
	wait_queue_head_t wait;
	/* completion for dpmaif thread paused */
	struct completion paused_comp;
	unsigned long state;
	int pause_ref;
	bool need_wp;
};

struct dpmaif_rxq {
	struct mtk_dpmaif_ctlb *dcb;
	unsigned char id;
	bool started;
	struct dpmaif_pd_pit *pit_base;
	dma_addr_t pit_dma_addr;
	unsigned int pit_cnt;
	unsigned short pit_wr_idx;
	unsigned short pit_rd_idx;
	unsigned short pit_rel_rd_idx;
	unsigned char pit_seq_expect;
	bool pit_poll_enable;
	atomic_t pit_rel_cnt;
	atomic_t pit_stats;
	bool pit_cnt_err_intr_set;
	unsigned int pit_burst_rel_cnt;
	unsigned int pit_seq_fail_cnt;
	struct napi_struct napi;
	struct dpmaif_rx_record rx_record;
	unsigned int intr_coalesce_frame;
	/* Record the latest BID polled by this DLQ pit ring. */
	unsigned int pit_bid;

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	struct work_struct steer_work;
	struct workqueue_struct *steer_wq;
	int cpu_id;
	struct dpmaif_vq steer_vq;
	struct mtk_fifo_t fifo;
#endif
	unsigned char bat_ring_id;
	unsigned int pit_seq_max;
	struct dpmaif_rx_info *rx_info;
	unsigned int attr;
	struct wakeup_source *ws;
} ____cacheline_aligned;

struct skb_mapped_t {
	struct sk_buff *skb;
	dma_addr_t data_dma_addr;
	unsigned int data_len;
};

struct page_mapped_t {
	struct page *page;
	dma_addr_t data_dma_addr;
	unsigned int offset;
	unsigned int data_len;
};

union dpmaif_bat_record {
	struct skb_mapped_t normal;
	struct page_mapped_t frag;
};

struct dpmaif_bat_ring {
	enum dpmaif_bat_type type;
	unsigned char id;
	struct dpmaif_bat *bat_base;
	dma_addr_t bat_dma_addr;
	unsigned int bat_cnt;
	unsigned short bat_wr_idx;
	unsigned short bat_rd_idx;
	/* current max relaod bat cnt */
	unsigned short max_reload_cnt;
	atomic_t to_reload_cnt;
	/* reloaded bat cnt, not doorbelled */
	atomic_t reload_cnt;
	atomic_t bat_stats;
	unsigned int doorbell_th;
	union dpmaif_bat_record *sw_record_base;
	unsigned int buf_size;
	unsigned long *mask_tbl;
	bool bat_cnt_err_intr_set;
	bool dynamic_reload;
	struct page_pool *pp;
	unsigned int pp_frag_size;
	unsigned int pp_head_offset;
	int (*alloc)(struct mtk_dpmaif_ctlb *dcb,
		     struct dpmaif_bat_ring *bat_ring,
				 unsigned short bat_idx);
};

struct dpmaif_bat_info {
	struct dpmaif_task_ctlb task_ctlb;
	struct mtk_dpmaif_ctlb *dcb;
	unsigned int max_mtu;
	bool frag_bat_enabled;

	struct dpmaif_bat_ring normal_bat_ring;
	struct dpmaif_bat_ring frag_bat_ring;

	struct task_struct *reload_task;
};

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
struct dpmaif_skb_fifo_ctlb {
	void *dcb;
	struct mtk_fifo_t fifo;
	struct workqueue_struct *wq;
	struct work_struct work;
	int buf_size;
};

struct mtk_data_cpu_affinity_cfg {
	u64 speed;
	u8 napi_thrd_aff[MTK_DATA_NAPI_NR_MAX];
	u8 steer_wq_aff[MTK_DATA_NAPI_NR_MAX];
	u8 reload_thrd_aff[DPMAIF_BAT_NUM_MAX];
	u8 doorbell_thrd_aff;
};
#endif

struct dpmaif_irq_param {
	unsigned char idx;
	struct mtk_dpmaif_ctlb *dcb;
	enum mtk_irq_src dpmaif_irq_src;
	int dev_irq_id;
	int dev_virq_id;
#ifdef CONFIG_MTK_DATA_FEATURE_TEST
	int cpu_mask;
#endif
};

/* mhccif event data parse */
#define MHCCIF_MSG_TYPE        GENMASK(31, 29)
#define MHCCIF_MSG_QMASK       GENMASK(25, 20)
#define MHCCIF_MSG_PRD         GENMASK(19, 12)
#define MHCCIF_MSG_RST         BIT(8)
#define MHCCIF_MSG_FRC         GENMASK(31, 0)

enum dpmaif_tras_msg_type {
	DPMAIF_TRAS_ULQ_CFG	= 0,
	DPMAIF_TRAS_ULQ_RST,
};

static const char dpmaif_tx_stats[][DATA_TRANS_STRING_LEN] = {
	"tx_byte", "tx_sw_pkt", "tx_hw_pkt", "tx_sw_full", "tx_hw_full",
	"tx_done_last_time", "tx_done_last_cnt",
	"irq_evt.ul_done", "irq_evt.ul_drb_empty",
};

static const char dpmaif_rx_stats[][DATA_TRANS_STRING_LEN] = {
	"rx_byte", "rx_pkt", "rx_lro_tcp_pkt",
	"rx_lro_udp_pkt", "rx_errors",
	"rx_dropped", "rx_fifo_dropped",
	"rx_hw_ind_dropped", "rx_done_last_time",
	"rx_done_last_cnt", "irq_evt.dl_done",
	"irq_evt.pit_len_err",
};

static const char dpmaif_irq_stats[][DATA_TRANS_STRING_LEN] = {
	"irq_total_cnt", "irq_last_time",
};

static const char dpmaif_bat_stats[][DATA_TRANS_STRING_LEN] = {
	"dl_bat_cnt_len_err", "dl_frag_cnt_len_err",
};

struct dpmaif_tx_evt {
	unsigned long long ul_done;
	unsigned long long ul_drb_empty;
} __packed;

struct dpmaif_rx_evt {
	unsigned long long dl_done;
	unsigned long long pit_len_err;
} __packed;

struct dpmaif_bat_evt {
	unsigned long long dl_bat_cnt_len_err;
	unsigned long long dl_frag_cnt_len_err;
} __packed;

struct dpmaif_tx_traffic {
	/* txq traffic */
	unsigned long long tx_byte[DPMAIF_TXQ_CNT_MAX];
	unsigned long long tx_sw_pkt[DPMAIF_TXQ_CNT_MAX];
	unsigned long long tx_hw_pkt[DPMAIF_TXQ_CNT_MAX];
	unsigned long long tx_sw_full[DPMAIF_TXQ_CNT_MAX];
	unsigned long long tx_hw_full[DPMAIF_TXQ_CNT_MAX];
	unsigned long long tx_done_last_time[DPMAIF_TXQ_CNT_MAX];
	unsigned int tx_done_last_cnt[DPMAIF_TXQ_CNT_MAX];

	/* tx event traffic */
	struct dpmaif_tx_evt irq_tx_evt[DPMAIF_TXQ_CNT_MAX];
} __packed;

struct dpmaif_rx_traffic {
	/* rxq traffic */
	unsigned long long rx_byte[DPMAIF_RXQ_CNT_MAX];
	unsigned long long rx_pkt[DPMAIF_RXQ_CNT_MAX];
	unsigned long long rx_lro_tcp_pkt[DPMAIF_RXQ_CNT_MAX];
	unsigned long long rx_lro_udp_pkt[DPMAIF_RXQ_CNT_MAX];
	unsigned long long rx_errors[DPMAIF_RXQ_CNT_MAX];
	unsigned long long rx_dropped[DPMAIF_RXQ_CNT_MAX];
	unsigned long long rx_fifo_dropped[DPMAIF_RXQ_CNT_MAX];
	unsigned long long rx_hw_ind_dropped[DPMAIF_RXQ_CNT_MAX];
	unsigned long long rx_done_last_time[DPMAIF_RXQ_CNT_MAX];
	unsigned int rx_done_last_cnt[DPMAIF_RXQ_CNT_MAX];

	/* rx event traffic */
	struct dpmaif_rx_evt irq_rx_evt[DPMAIF_RXQ_CNT_MAX];
	struct dpmaif_bat_evt irq_bat_evt[DPMAIF_BAT_NUM_MAX];
} __packed;

struct dpmaif_irq_traffic {
	unsigned long long irq_total_cnt[DPMAIF_IRQ_CNT_MAX];
	unsigned long long irq_last_time[DPMAIF_IRQ_CNT_MAX];
} __packed;

struct dpmaif_traffic_stats {
	struct dpmaif_tx_traffic dpmaif_tx;
	struct dpmaif_rx_traffic dpmaif_rx;
	struct dpmaif_irq_traffic dpmaif_irq;
} __packed;

#ifdef CONFIG_MTK_MEMLOG_EVENT_SUPPORT

struct event_stats_data_tx {
	struct memlog_event_msg event_msg;
	struct dpmaif_tx_traffic data_tx;
	struct dpmaif_irq_traffic data_irq;
} __packed;

#define MTK_DBG_DATA_TX_STATS(mdev, data) \
do { \
	struct dpmaif_traffic_stats *__stats = data; \
	struct event_stats_data_tx *event_tx_stats; \
	struct mtk_md_dev *__mdev = mdev; \
	event_tx_stats = mtk_memlog_req_address(__mdev, MTK_MEMLOG_RG_STATS, \
							 sizeof(struct event_stats_data_tx)); \
	if (!event_tx_stats) \
		break; \
	memcpy(&event_tx_stats->data_tx, &__stats->dpmaif_tx, \
							 sizeof(struct dpmaif_tx_traffic)); \
	memcpy(&event_tx_stats->data_irq, &__stats->dpmaif_irq, \
							 sizeof(struct dpmaif_irq_traffic)); \
	mtk_memlog_event_msg_init(&event_tx_stats->event_msg, STATS_DATA_TX); \
	mtk_memlog_req_done(__mdev, MTK_MEMLOG_RG_STATS); \
} while (0)

#define MTK_DBG_DATA_TX_STATS_WITH_BUF(data, __buf, __size) \
do { \
	struct event_stats_data_tx *event_tx_stats = (struct event_stats_data_tx *)__buf; \
	struct dpmaif_traffic_stats *__stats = data; \
	mtk_memlog_add_info(&event_tx_stats->event_msg); \
	mtk_memlog_event_msg_init(&event_tx_stats->event_msg, STATS_DATA_TX); \
	memcpy(&event_tx_stats->data_tx, &__stats->dpmaif_tx, \
							 sizeof(struct dpmaif_tx_traffic)); \
	memcpy(&event_tx_stats->data_irq, &__stats->dpmaif_irq, \
							 sizeof(struct dpmaif_irq_traffic)); \
	__size = sizeof(struct event_stats_data_tx); \
} while (0)

struct event_stats_data_rx {
	struct memlog_event_msg event_msg;
	struct dpmaif_rx_traffic data_rx;
	struct dpmaif_irq_traffic data_irq;
} __packed;

#define MTK_DBG_DATA_RX_STATS(mdev, data) \
do { \
	struct dpmaif_traffic_stats *__stats = data; \
	struct event_stats_data_rx *event_rx_stats; \
	struct mtk_md_dev *__mdev = mdev; \
	event_rx_stats = mtk_memlog_req_address(__mdev, MTK_MEMLOG_RG_STATS, \
							 sizeof(struct event_stats_data_rx)); \
	if (!event_rx_stats) \
		break; \
	memcpy(&event_rx_stats->data_rx, &__stats->dpmaif_rx, \
							 sizeof(struct dpmaif_rx_traffic)); \
	memcpy(&event_rx_stats->data_irq, &__stats->dpmaif_irq, \
							 sizeof(struct dpmaif_irq_traffic)); \
	mtk_memlog_event_msg_init(&event_rx_stats->event_msg, STATS_DATA_RX); \
	mtk_memlog_req_done(__mdev, MTK_MEMLOG_RG_STATS); \
} while (0)

#define MTK_DBG_DATA_RX_STATS_WITH_BUF(data, __buf, __size) \
do { \
	struct event_stats_data_rx *event_rx_stats = (struct event_stats_data_rx *)__buf; \
	struct dpmaif_traffic_stats *__stats = data; \
	mtk_memlog_add_info(&event_rx_stats->event_msg); \
	mtk_memlog_event_msg_init(&event_rx_stats->event_msg, STATS_DATA_RX); \
	memcpy(&event_rx_stats->data_rx, &__stats->dpmaif_rx, \
							 sizeof(struct dpmaif_rx_traffic)); \
	memcpy(&event_rx_stats->data_irq, &__stats->dpmaif_irq, \
							 sizeof(struct dpmaif_irq_traffic)); \
	__size = sizeof(struct event_stats_data_rx); \
} while (0)

#else

#define MTK_DBG_DATA_TX_STATS(mdev, data) \
do { \
	struct dpmaif_traffic_stats *__stats = data; \
	struct mtk_md_dev *__mdev = mdev; \
	u8 __i, __j; \
	for (__i = 0; __i < DPMAIF_TXQ_CNT_MAX; __i++) { \
		__j = 0; \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"txq%u: %s=%llu, %s=%llu, %s=%llu\n", \
			__i, dpmaif_tx_stats[__j], __stats->dpmaif_tx.tx_byte[__i], \
			dpmaif_tx_stats[__j + 1], __stats->dpmaif_tx.tx_sw_pkt[__i], \
			dpmaif_tx_stats[__j + 2], __stats->dpmaif_tx.tx_hw_pkt[__i]); \
		__j += 3; \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"txq%u: %s=%llu, %s=%llu\n", \
			__i, dpmaif_tx_stats[__j], __stats->dpmaif_tx.tx_sw_full[__i], \
			dpmaif_tx_stats[__j + 1], __stats->dpmaif_tx.tx_hw_full[__i]); \
		__j += 2; \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"txq%u: %s=%llu, %s=%u, %s=%llu\n", \
			__i, dpmaif_tx_stats[__j], __stats->dpmaif_tx.tx_done_last_time[__i], \
			dpmaif_tx_stats[__j + 1], __stats->dpmaif_tx.tx_done_last_cnt[__i], \
			dpmaif_tx_stats[__j + 2], __stats->dpmaif_tx.irq_tx_evt[__i].ul_done); \
		__j += 3; \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"txq%u: %s=%llu\n", \
			__i, dpmaif_tx_stats[__j], \
			__stats->dpmaif_tx.irq_tx_evt[__i].ul_drb_empty); \
	} \
	for (__i = 0; __i < DPMAIF_IRQ_CNT_MAX; __i++) { \
		__j = 0; \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"irq%u: %s=%llu, %s=%llu\n", \
			__i, dpmaif_irq_stats[__j], __stats->dpmaif_irq.irq_total_cnt[__i], \
			dpmaif_irq_stats[__j + 1], __stats->dpmaif_irq.irq_last_time[__i]); \
	} \
} while (0)

#define MAX_BUF_CNT 256

#define MTK_DBG_DATA_TX_STATS_WITH_BUF(data, buf, size) \
do { \
	struct dpmaif_traffic_stats *__stats = data; \
	ssize_t __size = 0; \
	char *__buf = buf; \
	u8 __i, __j; \
	for (__i = 0; __i < DPMAIF_TXQ_CNT_MAX; __i++) { \
		__j = 0; \
		__size += snprintf(__buf + __size, MAX_BUF_CNT, \
			"txq%u: %s=%llu, %s=%llu, %s=%llu\n", \
			__i, dpmaif_tx_stats[__j], __stats->dpmaif_tx.tx_byte[__i], \
			dpmaif_tx_stats[__j + 1], __stats->dpmaif_tx.tx_sw_pkt[__i], \
			dpmaif_tx_stats[__j + 2], __stats->dpmaif_tx.tx_hw_pkt[__i]); \
		__j += 3; \
		__size += snprintf(__buf + __size, MAX_BUF_CNT, \
			"txq%u: %s=%llu, %s=%llu\n", \
			__i, dpmaif_tx_stats[__j], __stats->dpmaif_tx.tx_sw_full[__i], \
			dpmaif_tx_stats[__j + 1], __stats->dpmaif_tx.tx_hw_full[__i]); \
		__j += 2; \
		__size += snprintf(__buf + __size, MAX_BUF_CNT, \
			"txq%u: %s=%llu, %s=%u, %s=%llu\n", \
			__i, dpmaif_tx_stats[__j], __stats->dpmaif_tx.tx_done_last_time[__i], \
			dpmaif_tx_stats[__j + 1], __stats->dpmaif_tx.tx_done_last_cnt[__i], \
			dpmaif_tx_stats[__j + 2], __stats->dpmaif_tx.irq_tx_evt[__i].ul_done); \
		__j += 3; \
		__size += snprintf(__buf + __size, MAX_BUF_CNT, \
			"txq%u: %s=%llu\n", \
			__i, dpmaif_tx_stats[__j], \
			__stats->dpmaif_tx.irq_tx_evt[__i].ul_drb_empty); \
	} \
	for (__i = 0; __i < DPMAIF_IRQ_CNT_MAX; __i++) { \
		__j = 0; \
		__size += snprintf(__buf + __size, MAX_BUF_CNT, \
			"irq%u: %s=%llu, %s=%llu\n", \
			__i, dpmaif_irq_stats[__j], __stats->dpmaif_irq.irq_total_cnt[__i], \
			dpmaif_irq_stats[__j + 1], __stats->dpmaif_irq.irq_last_time[__i]); \
	} \
	size = __size; \
} while (0)

#define MTK_DBG_DATA_RX_STATS(mdev, data) \
do { \
	struct dpmaif_traffic_stats *__stats = data; \
	struct mtk_md_dev *__mdev = mdev; \
	u8 __i, __j; \
	for (__i = 0; __i < DPMAIF_RXQ_CNT_MAX; __i++) { \
		__j = 0; \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"rxq%u: %s=%llu, %s=%llu, %s=%llu\n", \
			__i, dpmaif_rx_stats[__j], __stats->dpmaif_rx.rx_byte[__i], \
			dpmaif_rx_stats[__j + 1], __stats->dpmaif_rx.rx_pkt[__i], \
			dpmaif_rx_stats[__j + 2], __stats->dpmaif_rx.rx_lro_tcp_pkt[__i]); \
		__j += 3; \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"rxq%u: %s=%llu, %s=%llu, %s=%llu\n", \
			__i, dpmaif_rx_stats[__j], __stats->dpmaif_rx.rx_lro_udp_pkt[__i], \
			dpmaif_rx_stats[__j + 1], __stats->dpmaif_rx.rx_errors[__i], \
			dpmaif_rx_stats[__j + 2], __stats->dpmaif_rx.rx_dropped[__i]); \
		__j += 3; \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"rxq%u: %s=%llu, %s=%llu, %s=%llu\n", \
			__i, dpmaif_rx_stats[__j], __stats->dpmaif_rx.rx_fifo_dropped[__i], \
			dpmaif_rx_stats[__j + 1], __stats->dpmaif_rx.rx_hw_ind_dropped[__i], \
			dpmaif_rx_stats[__j + 2], __stats->dpmaif_rx.rx_done_last_time[__i]); \
		__j += 3; \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"rxq%u: %s=%u, %s=%llu, %s=%llu\n", \
			__i, dpmaif_rx_stats[__j], __stats->dpmaif_rx.rx_done_last_cnt[__i], \
			dpmaif_rx_stats[__j + 1], __stats->dpmaif_rx.irq_rx_evt[__i].dl_done, \
			dpmaif_rx_stats[__j + 2], \
			__stats->dpmaif_rx.irq_rx_evt[__i].pit_len_err); \
	} \
	for (__i = 0; __i < DPMAIF_BAT_NUM_MAX; __i++) { \
		__j = 0; \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"bat%u: %s=%llu, %s=%llu\n", \
			__i, dpmaif_bat_stats[__j], \
			__stats->dpmaif_rx.irq_bat_evt[__i].dl_bat_cnt_len_err, \
			dpmaif_bat_stats[__j + 1], \
			__stats->dpmaif_rx.irq_bat_evt[__i].dl_frag_cnt_len_err); \
	} \
	for (__i = 0; __i < DPMAIF_IRQ_CNT_MAX; __i++) { \
		__j = 0; \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"irq%u: %s=%llu, %s=%llu\n", \
			__i, dpmaif_irq_stats[__j], __stats->dpmaif_irq.irq_total_cnt[__i], \
			dpmaif_irq_stats[__j + 1], __stats->dpmaif_irq.irq_last_time[__i]); \
	} \
} while (0)

#define MTK_DBG_DATA_RX_STATS_WITH_BUF(data, buf, size) \
do { \
	struct dpmaif_traffic_stats *__stats = data; \
	ssize_t __size = 0; \
	char *__buf = buf; \
	u8 __i, __j; \
	for (__i = 0; __i < DPMAIF_RXQ_CNT_MAX; __i++) { \
		__j = 0; \
		__size += snprintf(__buf + __size, MAX_BUF_CNT, \
			"rxq%u: %s=%llu, %s=%llu, %s=%llu\n", \
			__i, dpmaif_rx_stats[__j], __stats->dpmaif_rx.rx_byte[__i], \
			dpmaif_rx_stats[__j + 1], __stats->dpmaif_rx.rx_pkt[__i], \
			dpmaif_rx_stats[__j + 2], __stats->dpmaif_rx.rx_lro_tcp_pkt[__i]); \
		__j += 3; \
		__size += snprintf(__buf + __size, MAX_BUF_CNT, \
			"rxq%u: %s=%llu, %s=%llu, %s=%llu\n", \
			__i, dpmaif_rx_stats[__j], __stats->dpmaif_rx.rx_lro_udp_pkt[__i], \
			dpmaif_rx_stats[__j + 1], __stats->dpmaif_rx.rx_errors[__i], \
			dpmaif_rx_stats[__j + 2], __stats->dpmaif_rx.rx_dropped[__i]); \
		__size += snprintf(__buf + __size, MAX_BUF_CNT, \
			"rxq%u: %s=%llu, %s=%llu, %s=%llu\n", \
			__i, dpmaif_rx_stats[__j], __stats->dpmaif_rx.rx_fifo_dropped[__i], \
			dpmaif_rx_stats[__j + 1], __stats->dpmaif_rx.rx_hw_ind_dropped[__i], \
			dpmaif_rx_stats[__j + 2], __stats->dpmaif_rx.rx_done_last_time[__i]); \
		__j += 3; \
		__size += snprintf(__buf + __size, MAX_BUF_CNT, \
			"rxq%u: %s=%u, %s=%llu, %s=%llu\n", \
			__i, dpmaif_rx_stats[__j], __stats->dpmaif_rx.rx_done_last_cnt[__i], \
			dpmaif_rx_stats[__j + 1], __stats->dpmaif_rx.irq_rx_evt[__i].dl_done, \
			dpmaif_rx_stats[__j + 2], \
			__stats->dpmaif_rx.irq_rx_evt[__i].pit_len_err); \
	} \
	for (__i = 0; __i < DPMAIF_BAT_NUM_MAX; __i++) { \
		__j = 0; \
		__size += snprintf(__buf + __size, MAX_BUF_CNT, \
			"bat%u: %s=%llu, %s=%llu\n", \
			__i, dpmaif_bat_stats[__j], \
			__stats->dpmaif_rx.irq_bat_evt[__i].dl_bat_cnt_len_err, \
			dpmaif_bat_stats[__j + 1], \
			__stats->dpmaif_rx.irq_bat_evt[__i].dl_frag_cnt_len_err); \
	} \
	for (__i = 0; __i < DPMAIF_IRQ_CNT_MAX; __i++) { \
		__j = 0; \
		__size += snprintf(__buf + __size, MAX_BUF_CNT, \
			"irq%u: %s=%llu, %s=%llu\n", \
			__i, dpmaif_irq_stats[__j], __stats->dpmaif_irq.irq_total_cnt[__i], \
			dpmaif_irq_stats[__j + 1], __stats->dpmaif_irq.irq_last_time[__i]); \
	} \
	size = __size; \
} while (0)

#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
#define SKB_HEAD_ALIGN(X) (SKB_DATA_ALIGN(X) + \
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
#endif

enum dpmaif_dump_flag {
	DPMAIF_DUMP_TX_PKT = 0,
	DPMAIF_DUMP_RX_PKT,
	DPMAIF_DUMP_DRB,
	DPMAIF_DUMP_PIT
};

enum dpmaif_event_stats {
	DPMAIF_TX_SW_FULL = 0,
	DPMAIF_TX_HW_FULL,
	DPMAIF_RX_DROPPED,
	DPMAIF_RX_FIFO_DROPPED,
	DPMAIF_RX_HW_IND_DROPPED,
	DPMAIF_PIT_CNT_LEN_ERR,
	DPMAIF_BAT_CNT_LEN_ERR,
	DPMAIF_FRAG_CNT_LEN_ERR
};

struct dpmaif_doorbell_ctlb {
	struct dpmaif_task_ctlb task_ctlb;
	struct mtk_dpmaif_ctlb *dcb;
	unsigned long long db_base_ts;
	struct hrtimer db_timer;
	/* lock for timer modify */
	spinlock_t db_timer_lock;

	struct task_struct *db_task;
	bool data_no_intf;
};

struct mtk_dpmaif_ctlb {
	struct mtk_data_blk *data_blk;
	struct dpmaif_drv_info *drv_info;
	struct mtk_pm_entity pm_entity;
	struct napi_struct *napi[DPMAIF_RXQ_CNT_MAX];

	enum dpmaif_state dpmaif_state;
	bool dpmaif_pm_ready;
	bool dpmaif_user_ready;
	bool trans_enabled;
	bool suspend_late_called;
	/* lock for enable/disable routine */
	struct mutex trans_ctl_lock;

	struct dpmaif_tx_srv *tx_srvs;
	struct dpmaif_vq *tx_vqs;

	struct workqueue_struct *tx_done_wq;
	struct timer_list ring_rel_ctrl_timer;
	struct dpmaif_doorbell_ctlb db_ctlb;
	struct dpmaif_txq *txqs;
	struct dpmaif_rxq *rxqs;
	struct dpmaif_bat_info *bat_infos;
	bool dpmaif_rx_legacy;
	bool irq_enabled;
	struct dpmaif_irq_param *irq_params;

	struct dpmaif_traffic_stats traffic_stats;
	u8 stats_tx_id;
	u8 stats_rx_id;
	struct mtk_data_intr_coalesce intr_coalesce;
	unsigned long dump_flag;
	atomic_t dump_once;
	unsigned long err_event;

#ifdef CONFIG_DEBUG_FS
	struct dentry *dpmaif_dir;
#endif
#ifdef CONFIG_DATA_TEST_MODE
	struct dpmaif_test_mode_cfg test_mode_cfg;
#endif
	struct dpmaif_tput_stats tput_stats;
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	struct mtk_data_cpu_affinity_cfg  *aff_cfg;
	struct dpmaif_skb_fifo_ctlb skb_fifo_ctlb;
	struct work_struct qos_work;
	u32 online_cpus;
#endif
	bool dpmaif_sw_reset;
	/* lock for dpmaif thread paused/resume */
	struct mutex task_paused_lock;
	unsigned char rxq_cnt;
	unsigned char txq_cnt;
	u32 features;
};

enum dpmaif_task_state {
	DATA_TASK_PAUSE = 0,
	DATA_TASK_BOOKING,
};

#define DCB_TO_DEV(dcb) ((dcb)->data_blk->mdev->dev)
#define DCB_TO_MDEV(dcb) ((dcb)->data_blk->mdev)
#define DCB_TO_DEV_STR(dcb) ((dcb)->data_blk->mdev->dev_str)
#define DPMAIF_GET_HW_VER(dcb) ((dcb)->data_blk->mdev->hw_ver)
#define DPMAIF_GET_DRB_CNT(__skb) (skb_shinfo(__skb)->nr_frags + 1 + 1)
#define DPMAIF_SKB_CB_TO_MAP_INFO(__skb) ((struct skb_mapped_t *)&((__skb)->cb[0]))

#define DPMAIF_JUMBO_SIZE 9000
#define DPMAIF_DFLT_MTU 3000
#define DPMAIF_DL_BUF_MIN_SIZE 128
#define DPMAIF_NORMAL_BUF_SIZE_IN_JUMBO (128 * 12) /* 1536 */
#define DPMAIF_FRAG_BUF_SIZE_IN_JUMBO (128 * 15) /* 1920 */

#define DPMAIF_PAGE_ORDER 0
#define DPMAIF_PAGE_SIZE (PAGE_SIZE << DPMAIF_PAGE_ORDER)
#define DPMAIF_HALF_PAGE_SIZE (DPMAIF_PAGE_SIZE >> 1)
#define DPMAIF_PAGE_POOL_SIZE_FACTOR 2

static unsigned int traffic_stats_shift = 7;

/* the PIT speed threshold for enable polling register at rx done interrupt bottom-half */
static unsigned int rx_poll_th = 400;
#define MAX_RX_POLL_TH	1024
/* the DRB speed threshold for enable polling reister at tx done interrupt bottom-half */
static unsigned int tx_poll_th = 200;
#define MAX_TX_POLL_TH	512

static unsigned int max_pit_burst_cnt = 1024;
#define MIN_BAT_BURST_CNT	64

/* Driver stops to delay doorbell when detects TCP slow start,
 * and restarts the doorbell delay after the specified doorbell count expires.
 */
static unsigned int doorbell_reset_count = 500;

static unsigned int recycle_rx_ring_th = 128;

static bool rx_legacy_mode;

static void mtk_dpmaif_cmd_string_cnt_get(struct mtk_dpmaif_ctlb *dcb, void *data);

static unsigned int mtk_dpmaif_describe_stats(struct mtk_dpmaif_ctlb *dcb, u8 *strings);

static void mtk_dpmaif_read_stats(struct mtk_dpmaif_ctlb *dcb, u64 *data);

static int mtk_dpmaif_rx_napi_poll(struct napi_struct *napi, int budget);

static int mtk_dpmaif_select_txq(struct mtk_data_blk *data_blk,
				 struct sk_buff *skb, enum mtk_data_pkt_prio pkt_prio);

static int mtk_dpmaif_send(struct mtk_data_blk *data_blk, enum mtk_data_type type,
			   struct sk_buff *skb);

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
#define DPMAIF_CPU_LOADING_MODE 2
static struct mtk_data_cpu_affinity_cfg   data_cpu_loading_cfg[][DPMAIF_CPU_LOADING_MODE] = {
	/*  bat speed, napi_task, steer_wq, reload_task, doorbell_task */
	{
		{0LL, {0, 1, 0}, {0, 1, 0}, {0x03, 0x03}, 0x03},
		{500LL, {0, 1, 0}, {0, 1, 1}, {0x01, 0x02}, 0x01},
	},
	{
		{0LL, {0, 1, 0}, {2, 3, 2}, {0x0F, 0x0F}, 0x0F},
		{500LL, {0, 1, 0}, {2, 3, 2}, {0x01, 0x04}, 0x02},
	},
	{
		{0LL, {1, 2, 0}, {4, 5, 6}, {0x0F, 0x0F}, 0x0F},
		{500LL, {1, 2, 3}, {4, 5, 6}, {0x80, 0xFF}, 0x49},
	},
};

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_AFFINITY)
static struct mtk_data_cpu_affinity_cfg google_affinity_config[DPMAIF_CPU_LOADING_MODE];
#endif

#define FIFO_WRITABLE(fifo) ({\
	typeof(fifo) _fifo = (fifo);\
	(((_fifo)->size + (_fifo)->rd - (_fifo)->wr - 1) & (_fifo)->mask);\
})
#define FIFO_READABLE(fifo) ({\
	typeof(fifo) _fifo = (fifo);\
	(((_fifo)->size + (_fifo)->wr - (_fifo)->rd) & (_fifo)->mask);\
})

static int mtk_dpmaif_fifo_init(struct mtk_fifo_t *fifo, unsigned int size)
{
	fifo->size = roundup_pow_of_two(size);
	fifo->mask = fifo->size - 1;

	fifo->buf = kcalloc(fifo->size, sizeof(*fifo->buf), GFP_ATOMIC);
	if (!fifo->buf)
		return -ENOMEM;

	return 0;
}

static void mtk_dpmaif_fifo_write(struct mtk_fifo_t *fifo, void *skb)
{
	/* make sure read correct idx */
	smp_rmb();

	fifo->buf[fifo->wr] = skb;

	/* wait write buf done */
	smp_wmb();

	fifo->wr = (fifo->wr + 1) & fifo->mask;
}

static void *mtk_dpmaif_fifo_read(struct mtk_fifo_t *fifo)
{
	void *skb;

	/* make sure read correct idx */
	smp_rmb();

	skb = fifo->buf[fifo->rd];

	/* wait write skb done */
	smp_wmb();
	fifo->rd = (fifo->rd + 1) & fifo->mask;

	return skb;
}

static void mtk_dpmaif_fifo_exit(struct mtk_fifo_t *fifo)
{
	kfree(fifo->buf);
}

static void mtk_dpmaif_dl_enqueue(struct dpmaif_rxq *rxq, struct sk_buff *skb)
{
	struct mtk_fifo_t *fifo = &rxq->fifo;
	unsigned char q_id = rxq->id;

	if (likely(FIFO_WRITABLE(fifo))) {
		mtk_dpmaif_fifo_write(fifo, skb);
	} else {
		rxq->dcb->traffic_stats.dpmaif_rx.rx_fifo_dropped[q_id]++;
		MTK_INFO_RATELIMITED(DCB_TO_MDEV(rxq->dcb), "rxq%u fifo full", q_id);
		trace_mtk_data_event_stats(DPMAIF_RX_FIFO_DROPPED, q_id,
					   rxq->dcb->traffic_stats.dpmaif_rx.rx_fifo_dropped[q_id]);
		dev_kfree_skb_any(skb);
	}
}

static void *mtk_dpmaif_dl_dequeue(struct dpmaif_rxq *rxq)
{
	return mtk_dpmaif_fifo_read(&rxq->fifo);
}

static void mtk_dpmaif_fifo_skb_free(struct mtk_fifo_t *fifo)
{
	unsigned int len = FIFO_READABLE(fifo);
	unsigned int i;

	for (i = 0; i < len; i++)
		dev_kfree_skb_any(mtk_dpmaif_fifo_read(fifo));
}

static void mtk_dpmaif_set_task_affinity(struct mtk_dpmaif_ctlb *dcb,
					 u32 cpu_mask, struct task_struct *task)
{
	char name[TASK_COMM_LEN];
	cpumask_var_t mask;
	int i, ret;

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL)) {
		MTK_WARN(DCB_TO_MDEV(dcb), "Failed to alloc cpumask var\n");
		return;
	}

	for (i = 0; i < dcb->online_cpus; i++) {
		if (cpu_mask & BIT(i)) {
			if (likely(cpu_online(i)))
				cpumask_set_cpu(i, mask);
			else
				MTK_INFO(DCB_TO_MDEV(dcb), "offline cpu=%d\n", i);
		}
	}

	ret = set_cpus_allowed_ptr(task, mask);
	get_task_comm(name, task);
	MTK_INFO(DCB_TO_MDEV(dcb), "%s: cpu=0x%x, ret=%d\n", name, cpu_mask, ret);
	free_cpumask_var(mask);
}

static void mtk_dpmaif_dl_skb_fifo_refill(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_skb_fifo_ctlb *skb_fifo_ctlb = &dcb->skb_fifo_ctlb;
	unsigned int len = FIFO_WRITABLE(&skb_fifo_ctlb->fifo);
	struct skb_mapped_t *skb_info;
	struct sk_buff *skb;
	int i;

	for (i = 0; i < len; i++) {
		skb = __dev_alloc_skb(skb_fifo_ctlb->buf_size, GFP_ATOMIC);
		if (unlikely(!skb)) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to alloc skb\n");
			return;
		}
		skb_info = DPMAIF_SKB_CB_TO_MAP_INFO(skb);
		skb_info->data_len = skb_fifo_ctlb->buf_size;
		skb_info->data_dma_addr = dma_map_single(DCB_TO_DEV(dcb),
							 skb->data,
							 skb_info->data_len,
							 DMA_FROM_DEVICE);
		if (dma_mapping_error(DCB_TO_DEV(dcb), skb_info->data_dma_addr)) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to map dma!\n");
			dev_kfree_skb_any(skb);
			return;
		}

		mtk_dpmaif_fifo_write(&skb_fifo_ctlb->fifo, skb);
	}
}

static void mtk_dpmaif_alloc_skb_work(struct work_struct *work)
{
	struct dpmaif_skb_fifo_ctlb *skb_fifo_ctlb;
	struct mtk_dpmaif_ctlb *dcb;

	skb_fifo_ctlb = container_of(work, struct dpmaif_skb_fifo_ctlb, work);
	dcb = skb_fifo_ctlb->dcb;
	if (unlikely(!dcb->trans_enabled))
		return;

	mtk_dpmaif_dl_skb_fifo_refill(dcb);
}

static int mtk_dpmaif_skb_fifo_init(struct mtk_dpmaif_ctlb *dcb,
				    int buf_size, int fifo_size)
{
	struct dpmaif_skb_fifo_ctlb *skb_fifo_ctlb = &dcb->skb_fifo_ctlb;
	int ret;

	ret = mtk_dpmaif_fifo_init(&dcb->skb_fifo_ctlb.fifo, fifo_size);
	if (ret < 0) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to init skb fifo\n");
		return ret;
	}

	skb_fifo_ctlb->buf_size = buf_size;
	INIT_WORK(&skb_fifo_ctlb->work, mtk_dpmaif_alloc_skb_work);
	skb_fifo_ctlb->wq = alloc_workqueue("dpmaif_alloc_skb_wq_%s",
					    WQ_HIGHPRI | WQ_UNBOUND | WQ_MEM_RECLAIM,
						  0, DCB_TO_DEV_STR(dcb));
	if (!skb_fifo_ctlb->wq) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to create skb workqueue\n");
		ret = -ENOMEM;
		goto fifo_exit;
	}

	return 0;
fifo_exit:
	mtk_dpmaif_fifo_exit(&skb_fifo_ctlb->fifo);
	return ret;
}

static void mtk_dpmaif_skb_fifo_exit(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_skb_fifo_ctlb *skb_fifo_ctlb = &dcb->skb_fifo_ctlb;
	struct sk_buff *skb;

	destroy_workqueue(skb_fifo_ctlb->wq);

	while (FIFO_READABLE(&skb_fifo_ctlb->fifo)) {
		skb = mtk_dpmaif_fifo_read(&skb_fifo_ctlb->fifo);
		dma_unmap_single(DCB_TO_DEV(dcb),
				 DPMAIF_SKB_CB_TO_MAP_INFO(skb)->data_dma_addr,
				 DPMAIF_SKB_CB_TO_MAP_INFO(skb)->data_len,
				 DMA_FROM_DEVICE);

		dev_kfree_skb_any(skb);
	}

	mtk_dpmaif_fifo_exit(&skb_fifo_ctlb->fifo);
}

static void mtk_dpmaif_change_task_aff(struct work_struct *work)
{
	struct dpmaif_tput_stats *tput_stats;
	struct mtk_dpmaif_ctlb *dcb;
	int bat_ring_num, i;

	dcb = container_of(work, struct mtk_dpmaif_ctlb, qos_work);
	bat_ring_num = dcb->drv_info->cfg->rx_cfg.bat_ring_num;
	tput_stats = &dcb->tput_stats;

	for (i = 0; i < bat_ring_num; i++) {
		mtk_dpmaif_set_task_affinity(dcb,
					     dcb->aff_cfg[tput_stats->dl_mode].reload_thrd_aff[i],
					     dcb->bat_infos[i].reload_task);
	}

	mtk_dpmaif_set_task_affinity(dcb,
				     dcb->aff_cfg[tput_stats->dl_mode].doorbell_thrd_aff,
				     dcb->db_ctlb.db_task);
}

static void mtk_dpmaif_qos_init(struct mtk_dpmaif_ctlb *dcb)
{
	dcb->online_cpus = num_online_cpus();
#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_AFFINITY)
	int ret, i;
	bool google_init_success = true;

	for (i = 0; i < DPMAIF_CPU_LOADING_MODE; i++) {
		ret = dpmaif_google_fill_affinity(i,
						  dcb->online_cpus,
						  &google_affinity_config[i].speed,
						  google_affinity_config[i].napi_thrd_aff,
						  MTK_DATA_NAPI_NR_MAX,
						  google_affinity_config[i].steer_wq_aff,
						  MTK_DATA_NAPI_NR_MAX,
						  google_affinity_config[i].reload_thrd_aff,
						  DPMAIF_BAT_NUM_MAX,
						  &google_affinity_config[i].doorbell_thrd_aff
		);

		if (ret) {
			MTK_WARN(DCB_TO_MDEV(dcb), "Google affinity filling failed, ret=%d\n", ret);
			google_init_success = false;
			break;
		}
	}

	if (google_init_success) {
		dcb->aff_cfg = google_affinity_config;
		goto aff_done;
	}
#endif /* CONFIG_GOOGLE_MODEM_DATA_AFFINITY */
	MTK_INFO(DCB_TO_MDEV(dcb), "dcb->online_cpus = %u\n", dcb->online_cpus);
	if (dcb->online_cpus <= 1) {
		MTK_INFO(DCB_TO_MDEV(dcb), "Only one CPU online, no need to set CPU affinity\n");
		dcb->aff_cfg = NULL;
	} else if (dcb->online_cpus < 4) {
		dcb->aff_cfg = data_cpu_loading_cfg[0];
	} else if (dcb->online_cpus < 8) {
		dcb->aff_cfg = data_cpu_loading_cfg[1];
	} else {
		dcb->aff_cfg = data_cpu_loading_cfg[2];
	}
#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_AFFINITY)
aff_done:
#endif /* CONFIG_GOOGLE_MODEM_DATA_AFFINITY */
	if (dcb->aff_cfg)
		INIT_WORK(&dcb->qos_work, mtk_dpmaif_change_task_aff);
}
#endif

static int mtk_dpmaif_ring_calc_tput(atomic_t *stats, unsigned int time_shift)
{
	int tmp_stats = atomic_read(stats);

	atomic_sub(tmp_stats, stats);

	return DPMAIF_RING_TPUT_CALC(tmp_stats, time_shift);
}

/* mtk_dpmaif_bat_reload_ctrl - calculate bat reload cnt and doorbell threshold based on @speed
 * @bat_ring: dpmaif_bat_ring
 * @speed: bat consume speed (cnt/ms)
 *
 * speed	:    bat reload count	:    doorbell threshold
 * [256, -)	:    bat_cnt - 1	:    MIN_BAT_BURST_CNT
 * [128, 256)	:    [21520, 21647]	:    [128, 192]
 * [0, 128)	:    [11264, 11518]	:    [385, 512]
 */
static void mtk_dpmaif_bat_reload_ctrl(struct dpmaif_bat_ring *bat_ring, unsigned int speed)
{
	unsigned short old_reload_cnt = bat_ring->max_reload_cnt;
	unsigned short new_reload_cnt;
	int diff;

	if (speed < 128) {
		new_reload_cnt = 11264 + (speed << 1);
		bat_ring->doorbell_th = 512 - speed;
		goto out;
	}

	if (speed < 256) {
		new_reload_cnt = 21392 + speed;
		bat_ring->doorbell_th = 256 - (speed >> 1);
		goto out;
	}

	new_reload_cnt = bat_ring->bat_cnt - 1;
	bat_ring->doorbell_th = MIN_BAT_BURST_CNT;

out:
	if (new_reload_cnt >= bat_ring->bat_cnt)
		new_reload_cnt = bat_ring->bat_cnt - 1;

	if (old_reload_cnt == new_reload_cnt)
		return;

	/* When decreasing reload count, decrease by 128 each time */
	diff = new_reload_cnt - old_reload_cnt;
	if (diff < 0) {
		if (diff < -128)
			diff = -128;

		bat_ring->max_reload_cnt = old_reload_cnt + diff;
	} else {
		bat_ring->max_reload_cnt = new_reload_cnt;
	}

	/* increase/decrease reload count to avoid consume much memory */
	atomic_add(diff, &bat_ring->to_reload_cnt);
}

/* mtk_dpmaif_pit_rel_ctrl - calculate pit release cnt based on @pit_speed
 * @rxq: receive queue
 * @pit_speed: pit cnt perf ms
 *
 * pit_speed correspondence table:
 * [1088, - )      :    64
 * [500, 1088)     :   (64, min(137, max_pit_burst_cnt)]
 * [128, 500)      :   (140, min(512, max_pit_burst_cnt)]
 * [32, 128)       :   (512, min(2048, max_pit_burst_cnt)]
 */
static void mtk_dpmaif_pit_rel_ctrl(struct dpmaif_rxq *rxq, unsigned int pit_speed)
{
	const unsigned int max_update_cnt = max_pit_burst_cnt;
	const unsigned int min_update_cnt = 64;
	unsigned int tmp_pit_burst_rel_cnt;
	bool tmp_pit_poll_enable;

	if (pit_speed > rx_poll_th)
		tmp_pit_poll_enable = true;
	else
		tmp_pit_poll_enable = false;

	if (pit_speed < 128) {
		tmp_pit_burst_rel_cnt = 2560 - (pit_speed << 4);
		goto out;
	} else if (pit_speed < 500) {
		tmp_pit_burst_rel_cnt = 640 - pit_speed;
		goto out;
	} else if (pit_speed < 1088) {
		tmp_pit_burst_rel_cnt = 200 - (pit_speed >> 3);
		goto out;
	}

	/* for scenario of pit_speed >= 1088 */
	tmp_pit_burst_rel_cnt = min_update_cnt;

out:
	if (tmp_pit_burst_rel_cnt > max_update_cnt)
		tmp_pit_burst_rel_cnt = max_update_cnt;

	rxq->pit_burst_rel_cnt = tmp_pit_burst_rel_cnt;
	rxq->pit_poll_enable = tmp_pit_poll_enable;
	trace_mtk_data_pit_burst_cnt(rxq->id, rxq->pit_burst_rel_cnt);
}

static void mtk_dpmaif_drb_rel_ctrl(struct dpmaif_txq *txq, unsigned int drb_speed)
{
	if (drb_speed > tx_poll_th)
		txq->drb_poll_enable = true;
	else
		txq->drb_poll_enable = false;
}

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
static void mtk_dpmaif_tput_task_qos(struct mtk_dpmaif_ctlb *dcb, u64 bat_speed)
{
	struct dpmaif_tput_stats *tput_stats = &dcb->tput_stats;
	int i, mode = 0;

	/* dl mode check */
	for (i = DPMAIF_CPU_LOADING_MODE - 1; i > 0; i--) {
		if (bat_speed >= dcb->aff_cfg[i].speed) {
			mode = i;
			break;
		}
	}

	if (mode == tput_stats->dl_mode)
		return;

	/* tput increase */
	if (mode > tput_stats->dl_mode) {
		tput_stats->high_speed = true;
		tput_stats->dl_mode = mode;
		/* wait write done */
		smp_wmb();
		schedule_work(&dcb->qos_work);
	}

	/* tput decrease */
	if (bat_speed > dcb->aff_cfg[tput_stats->dl_mode].speed - 84LL)
		return;

	tput_stats->high_speed = false;
	tput_stats->dl_mode = mode;
	/* wait write done */
	smp_wmb();
	schedule_work(&dcb->qos_work);
}
#endif

static void mtk_dpmaif_ring_rel_ctrl_timer_func(struct timer_list *t)
{
	struct mtk_dpmaif_ctlb *dcb = from_timer(dcb, t, ring_rel_ctrl_timer);
	struct dpmaif_bat_ring *bat_ring;
	struct dpmaif_rxq *rxq;
	struct dpmaif_txq *txq;
	unsigned int tmp_tput;
	int i;
#if IS_ENABLED(CONFIG_GOOGLE_MODEM_SOC_BW_QOS)
	unsigned int total_tx_tput = 0;
#endif

	for (i = 0; i < dcb->drv_info->cfg->rx_cfg.bat_ring_num; i++) {
		bat_ring = &dcb->bat_infos[i].normal_bat_ring;
		tmp_tput = mtk_dpmaif_ring_calc_tput(&bat_ring->bat_stats, traffic_stats_shift);
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
		if (i == DPMAIF_BAT0 && dcb->aff_cfg)
			mtk_dpmaif_tput_task_qos(dcb, tmp_tput);
#endif
		mtk_dpmaif_bat_reload_ctrl(bat_ring, tmp_tput);

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_SOC_BW_QOS)
		/* DPMAIF_BAT0 is for general data packet */
		if (i == DPMAIF_BAT0)
			soc_qos_report_normal_bat_tput(DCB_TO_MDEV(dcb)->google, tmp_tput);
#endif /* CONFIG_GOOGLE_MODEM_SOC_BW_QOS */
		if (!dcb->bat_infos[i].frag_bat_enabled)
			continue;

		bat_ring = &dcb->bat_infos[i].frag_bat_ring;
		if (!bat_ring->dynamic_reload)
			continue;

		tmp_tput = mtk_dpmaif_ring_calc_tput(&bat_ring->bat_stats, traffic_stats_shift);
		mtk_dpmaif_bat_reload_ctrl(bat_ring, tmp_tput);

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_SOC_BW_QOS)
		if (i == DPMAIF_BAT0)
			soc_qos_report_frag_bat_tput(DCB_TO_MDEV(dcb)->google, tmp_tput);
#endif /* CONFIG_GOOGLE_MODEM_SOC_BW_QOS */
	}

	for (i = 0; i < dcb->rxq_cnt; i++) {
		rxq = &dcb->rxqs[i];
		tmp_tput = mtk_dpmaif_ring_calc_tput(&rxq->pit_stats, traffic_stats_shift);
		mtk_dpmaif_pit_rel_ctrl(rxq, tmp_tput);
	}

	for (i = 0; i < dcb->txq_cnt; i++) {
		txq = &dcb->txqs[i];
		tmp_tput = mtk_dpmaif_ring_calc_tput(&txq->drb_stats, traffic_stats_shift);
		mtk_dpmaif_drb_rel_ctrl(txq, tmp_tput);

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_SOC_BW_QOS)
		total_tx_tput += tmp_tput;
#endif /* CONFIG_GOOGLE_MODEM_SOC_BW_QOS */
	}

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_SOC_BW_QOS)
	soc_qos_report_tx_tput(DCB_TO_MDEV(dcb)->google, total_tx_tput);
	soc_qos_update_ddr_vote(DCB_TO_MDEV(dcb)->google, traffic_stats_shift);
#endif /* CONFIG_GOOGLE_MODEM_SOC_BW_QOS */

	mod_timer(&dcb->ring_rel_ctrl_timer,
		  jiffies + msecs_to_jiffies(BIT(traffic_stats_shift)));
}

static void mtk_dpmaif_dl_stats_update(struct dpmaif_rxq *rxq, unsigned int data_len)
{
	struct dpmaif_tput_stats *tput_stats = &rxq->dcb->tput_stats;

	rxq->dcb->traffic_stats.dpmaif_rx.rx_byte[rxq->id] += data_len;
	if (tput_stats->monitor_enabled && atomic_cmpxchg(&tput_stats->start_monitor, 0, 1) == 0) {
		queue_delayed_work(tput_stats->monitor_wq,
				   &tput_stats->monitor_work,
				   msecs_to_jiffies(0));
	}
}

static void mtk_dpmaif_ul_stats_update(struct dpmaif_txq *txq, unsigned int data_len)
{
	struct dpmaif_tput_stats *tput_stats = &txq->dcb->tput_stats;

	txq->dcb->traffic_stats.dpmaif_tx.tx_byte[txq->id] += data_len;
	if (tput_stats->monitor_enabled && atomic_cmpxchg(&tput_stats->start_monitor, 0, 1) == 0) {
		queue_delayed_work(tput_stats->monitor_wq,
				   &tput_stats->monitor_work,
				   msecs_to_jiffies(0));
	}
}

static u64 mtk_dpmaif_speed_calc(u64 pre_bytes, u64 cur_bytes, u64 start, u64 end)
{
	u64 delta_bytes;

	if (cur_bytes >= pre_bytes)
		delta_bytes = cur_bytes - pre_bytes;
	else
		delta_bytes = (~0ULL) - cur_bytes + pre_bytes;

	if (!delta_bytes)
		return 0;

	/* max support: 180Gbps */
	return ((delta_bytes * 800000000LL) / ((end - start) / 10ULL));
}

static void mtk_dpmaif_tput_calc(struct mtk_dpmaif_ctlb *dcb, u64 start, u64 end)
{
	struct dpmaif_tput_stats *tput_stats = &dcb->tput_stats;
	unsigned char rxq_cnt = dcb->rxq_cnt;
	int i;

	for (i = 0; i < rxq_cnt; i++) {
		tput_stats->speed[i] = mtk_dpmaif_speed_calc(tput_stats->rx_pre_bytes[i],
							     tput_stats->rx_cur_bytes[i],
							     start, end);
		tput_stats->dl_speed += tput_stats->speed[i];
	}

	for (i = 0; i < dcb->txq_cnt; i++) {
		tput_stats->speed[rxq_cnt + i] = mtk_dpmaif_speed_calc(tput_stats->tx_pre_bytes[i],
								       tput_stats->tx_cur_bytes[i],
								       start, end);
		tput_stats->ul_speed += tput_stats->speed[rxq_cnt + i];
	}
}

static bool mtk_dpmaif_idle(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_tput_stats *tput_stats = &dcb->tput_stats;
	int i;

	for (i = 0; i < dcb->rxq_cnt + dcb->txq_cnt; i++)
		if (tput_stats->speed[i])
			return false;

	return true;
}

static void mtk_dpmaif_traffic_monitor_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct dpmaif_traffic_stats *traffic_stats;
	struct dpmaif_tput_stats *tput_stats;
	struct mtk_dpmaif_ctlb *dcb;
	u64 current_clock;
	int i;

	tput_stats = container_of(dwork, struct dpmaif_tput_stats, monitor_work);
	dcb = tput_stats->dcb;
	traffic_stats = &dcb->traffic_stats;

	if (unlikely(!dcb->trans_enabled))
		goto out;

	if (!tput_stats->work_first_enter) {
		tput_stats->work_first_enter = true;
		for (i = 0; i < dcb->rxq_cnt; i++)
			tput_stats->rx_pre_bytes[i] = traffic_stats->dpmaif_rx.rx_byte[i];

		for (i = 0; i < dcb->txq_cnt; i++)
			tput_stats->tx_pre_bytes[i] = traffic_stats->dpmaif_tx.tx_byte[i];

		tput_stats->pre_clock = sched_clock();
		queue_delayed_work(tput_stats->monitor_wq, dwork,
				   msecs_to_jiffies(DPMAIF_TPUT_MONITOR_PERIOD_MS));
		return;
	}

	tput_stats->dl_speed = 0;
	tput_stats->ul_speed = 0;

	for (i = 0; i < dcb->rxq_cnt; i++)
		tput_stats->rx_cur_bytes[i] = traffic_stats->dpmaif_rx.rx_byte[i];

	for (i = 0; i < dcb->txq_cnt; i++)
		tput_stats->tx_cur_bytes[i] = traffic_stats->dpmaif_tx.tx_byte[i];

	current_clock = sched_clock();
	mtk_dpmaif_tput_calc(dcb, tput_stats->pre_clock, current_clock);

	if (++tput_stats->speed_show_cnt >= 2) {
		tput_stats->speed_show_cnt = 0;
		MTK_INFO(DCB_TO_MDEV(dcb), "DL-TPUT = %llu, UL-TPUT = %llu\n",
			 tput_stats->dl_speed, tput_stats->ul_speed);
	}

	for (i = 0; i < dcb->rxq_cnt; i++)
		tput_stats->rx_pre_bytes[i] = tput_stats->rx_cur_bytes[i];

	for (i = 0; i < dcb->txq_cnt; i++)
		tput_stats->tx_pre_bytes[i] = tput_stats->tx_cur_bytes[i];

	tput_stats->pre_clock = current_clock;

	if (mtk_dpmaif_idle(dcb))
		tput_stats->auto_exit_cnt++;
	else
		tput_stats->auto_exit_cnt = 0;

	if (tput_stats->auto_exit_cnt < 4) {
		queue_delayed_work(tput_stats->monitor_wq, dwork,
				   msecs_to_jiffies(DPMAIF_TPUT_MONITOR_PERIOD_MS));
		return;
	}

out:
	tput_stats->auto_exit_cnt = 0;
	tput_stats->speed_show_cnt = 0;
	tput_stats->high_speed = false;
	tput_stats->work_first_enter = false;
	atomic_set(&tput_stats->start_monitor, 0);
}

static void dpmaif_dump_bat_mask(struct mtk_dpmaif_ctlb *dcb,
				 struct dpmaif_bat_ring *bat_ring, unsigned int cnt)
{
	unsigned int dump_bit_cnt, pre_cnt;
	unsigned long *mask_tbl, *p;
	unsigned short bat_idx;

	/* dump bat mask count by bit */
	dump_bit_cnt = cnt << 3;

	/* make sure dump size not exceed bat count and aligned to byte */
	if (dump_bit_cnt > (bat_ring->bat_cnt / 2)) {
		dump_bit_cnt = bat_ring->bat_cnt / 2;
		dump_bit_cnt = ALIGN_DOWN(dump_bit_cnt, 8);
		cnt = dump_bit_cnt >> 3;
	}

	mask_tbl = bat_ring->mask_tbl;
	bat_idx = ALIGN_DOWN(bat_ring->bat_wr_idx, BITS_PER_LONG);

	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
		"dump bat_mask from: (%u)\n", bat_idx);

	p = &mask_tbl[bat_idx / BITS_PER_LONG];

	if ((bat_idx + dump_bit_cnt) > bat_ring->bat_cnt) {
		pre_cnt = (bat_ring->bat_cnt - bat_idx) / 8;
		MTK_HEX_DUMP(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			     NULL, p, pre_cnt);
		MTK_HEX_DUMP(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			     NULL, mask_tbl, cnt - pre_cnt);
	} else {
		MTK_HEX_DUMP(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			     NULL, p, cnt);
	}
}

static bool dpmaif_hw_is_accessible(struct mtk_dpmaif_ctlb *dcb)
{
	if (unlikely(test_bit(DATA_LINK_ERR, &dcb->err_event)))
		return false;

	if (likely(mtk_pci_mmio_check(DCB_TO_MDEV(dcb))))
		return true;

	set_bit(DATA_LINK_ERR, &dcb->err_event);
	MTK_INFO(DCB_TO_MDEV(dcb), "Failed to access hw\n");
	mtk_exception_report_evt(DCB_TO_MDEV(dcb), EXCEPTION_LINK_ERR);

	return false;
}

static void dpmaif_dump_txq_drb_info(struct dpmaif_txq *txq, unsigned int cnt)
{
	unsigned int drb_dump_ridx = txq->drb_rd_idx;
	struct mtk_dpmaif_ctlb *dcb = txq->dcb;
	struct dpmaif_drv_info *drv_info;
	unsigned int drb_dump_widx;
	unsigned int *drb_info;
	unsigned int drb_idx;
	unsigned int j;

	if (dpmaif_hw_is_accessible(dcb)) {
		drv_info = dcb->drv_info;
		drb_dump_widx = drv_info->drv_ops->get_ring_idx(drv_info, DPMAIF_DRB_WIDX,
			txq->id);
		drb_dump_ridx = drv_info->drv_ops->get_ring_idx(drv_info, DPMAIF_DRB_RIDX,
			txq->id);
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"hw_drb: w=%u,r=%u\n", drb_dump_widx, drb_dump_ridx);
	}

	if (cnt > (txq->drb_cnt / 2))
		cnt = txq->drb_cnt / 2;

	drb_idx = (txq->drb_cnt + drb_dump_ridx - cnt) % txq->drb_cnt;
	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
		"txq(%u),dump drb info,start_idx=%u,rd_idx=%u\n",
		txq->id, drb_idx, drb_dump_ridx);

	if (txq->drb_base) {
		for (j = 0; j < cnt * 2; j++) {
			drb_info = (unsigned int *)(txq->drb_base + drb_idx);
			MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
				"drb(%u): 0x%08x, 0x%08x, 0x%08x,0x%08x\n",
				drb_idx, drb_info[0], drb_info[1], drb_info[2], drb_info[3]);
			drb_idx = mtk_dpmaif_ring_buf_get_next_idx(txq->drb_cnt, drb_idx);
		}
	}
}

static void dpmaif_dump_drb(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_txq *txq,
			    unsigned int dump_cnt)
{
	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
		"== dump txq%d=0x%llx ==\n", txq->id, (u64)txq);

	/* dump DRB info */
	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
		"drb base=0x%llx(%d*%d), sw record base=0x%llx(%d*%d)\n",
		      (u64)txq->drb_base, (int)sizeof(struct dpmaif_pd_drb), txq->drb_cnt,
		      (u64)txq->sw_drb_base, (int)sizeof(struct dpmaif_drb_skb), txq->drb_cnt);
	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
		"sw_drb: w=%u,r=%u,rel=%u,to_submit=%d\n",
		      txq->drb_wr_idx, txq->drb_rd_idx, txq->drb_rel_rd_idx,
		      atomic_read(&txq->to_submit_cnt));

	dpmaif_dump_txq_drb_info(txq, dump_cnt);

	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF,
		MTK_MEMLOG_RG_DATA_DUMP, "== dump txq%d end ==\n", txq->id);
}

static void dpmaif_dump_txq_info(struct mtk_dpmaif_ctlb *dcb, unsigned int q_mask)
{
	int i;

	for (i = 0; i < dcb->drv_info->cfg->tx_cfg.txq_cnt; i++) {
		if (!(BIT(i) & q_mask))
			continue;

		dpmaif_dump_drb(dcb, &dcb->txqs[i], DPMAIF_DUMP_DRB_CNT);
	}
}

static void dpmaif_dump_rxq_pit_info(struct dpmaif_rxq *rxq, unsigned int cnt)
{
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	unsigned int *pit_info;
	unsigned int pit_idx;
	unsigned int j;

	if (cnt > (rxq->pit_cnt / 2))
		cnt = rxq->pit_cnt / 2;

	pit_idx = (rxq->pit_cnt + rxq->pit_rd_idx - cnt) % rxq->pit_cnt;
	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
		"rxq(%u), dump pit info,start_idx=%u,rd_idx=%u\n",
		      rxq->id, pit_idx, rxq->pit_rd_idx);

	if (rxq->pit_base) {
		for (j = 0; j < cnt * 2; j++) {
			pit_info = (unsigned int *)(rxq->pit_base + pit_idx);
			MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
				"pit(%u): 0x%08x, 0x%08x, 0x%08x,0x%08x\n",
				pit_idx, pit_info[0], pit_info[1], pit_info[2], pit_info[3]);
			pit_idx = mtk_dpmaif_ring_buf_get_next_idx(rxq->pit_cnt, pit_idx);
		}
	}
}

static void dpmaif_dump_normal_bat_skb_info(struct dpmaif_rxq *rxq, unsigned int cnt)
{
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	union dpmaif_bat_record *bat_record;
	struct dpmaif_bat_ring *bat_ring;
	struct skb_mapped_t *skb_info;
	unsigned int bidx;
	int j;

	bat_ring = &dcb->bat_infos[rxq->bat_ring_id].normal_bat_ring;
	if (cnt > (bat_ring->bat_cnt / 2))
		cnt = bat_ring->bat_cnt / 2;

	bidx = (bat_ring->bat_cnt + rxq->pit_bid - cnt) % bat_ring->bat_cnt;
	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
		"rxq(%u),dump normal bat skb info,start_idx=%u,pit_bid=%u\n",
		rxq->id, bidx, rxq->pit_bid);
	for (j = 0; j < cnt * 2; j++) {
		bat_record = bat_ring->sw_record_base + bidx;
		skb_info = &bat_record->normal;
		if (skb_info->skb)
			MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
				"rxq(%u),bid=%u,skb=%llx,len=%u,dma=0x%llx\n",
					  rxq->id, bidx, (u64)skb_info->skb,
					  skb_info->data_len, (u64)skb_info->data_dma_addr);
		else
			MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
				"skb is NULL,bid=%u\n", bidx);
		bidx = mtk_dpmaif_ring_buf_get_next_idx(bat_ring->bat_cnt, bidx);
	}
}

static void dpmaif_dump_rxq_info(struct mtk_dpmaif_ctlb *dcb,
				 unsigned int q_mask)
{
	struct dpmaif_drv_info *drv_info = dcb->drv_info;
	struct dpmaif_rxq *rxq;
	int i;

	for (i = 0; i < dcb->rxq_cnt; i++) {
		if (!(BIT(i) & q_mask))
			continue;

		rxq = &dcb->rxqs[i];
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"== dump rxq%d=0x%llx ==\n", i, (u64)rxq);

		/* PIT information dump. */
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"pit base=0x%llx(%d*%d), rel_cnt=%d\n",
			      (u64)rxq->pit_base, (int)sizeof(struct dpmaif_pd_pit),
			      rxq->pit_cnt, atomic_read(&rxq->pit_rel_cnt));

		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"pit: w=%u,r=%u,rel=%u\n",
			      rxq->pit_wr_idx, rxq->pit_rd_idx, rxq->pit_rel_rd_idx);

		if (dpmaif_hw_is_accessible(dcb))
			MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
				"hw_pit: w=%d,r=%d\n",
				      drv_info->drv_ops->get_ring_idx(drv_info,
								  DPMAIF_PIT_WIDX, i),
				      drv_info->drv_ops->get_ring_idx(drv_info,
								  DPMAIF_PIT_RIDX, i));

		dpmaif_dump_rxq_pit_info(rxq, DPMAIF_DUMP_PIT_CNT);
		dpmaif_dump_normal_bat_skb_info(rxq, DPMAIF_DUMP_BAT_CNT);
	}
}

#define MASK_TBL_SIZE 32

static void dpmaif_dump_bat_info(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_drv_info *drv_info = dcb->drv_info;
	struct dpmaif_bat_ring *bat_ring;
	int bat_ring_num;
	int k;

	bat_ring_num = drv_info->cfg->rx_cfg.bat_ring_num;

	for (k = 0; k < bat_ring_num; k++) {
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"== dump BAT%d information ==\n", k);

		/* Normal BAT information dump. */
		bat_ring = &dcb->bat_infos[k].normal_bat_ring;
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"== dump normal bat base=0x%llx(%d*%u) ==\n",
			      (u64)bat_ring->bat_base, (int)sizeof(struct dpmaif_bat),
			      bat_ring->bat_cnt);
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"normal bat: w=%u,r=%u\n", bat_ring->bat_wr_idx, bat_ring->bat_rd_idx);
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"normal bat: max_reload_cnt=%u,to_reload_cnt=%d,db_th=%u,reload_cnt=%d\n",
				  bat_ring->max_reload_cnt, atomic_read(&bat_ring->to_reload_cnt),
			      bat_ring->doorbell_th, atomic_read(&bat_ring->reload_cnt));
		if (dpmaif_hw_is_accessible(dcb))
			MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
				"hw_bat: w=%d,r=%d\n",
				      drv_info->drv_ops->get_ring_idx(drv_info,
								  DPMAIF_BAT_WIDX, k),
				      drv_info->drv_ops->get_ring_idx(drv_info,
								  DPMAIF_BAT_RIDX, k));

		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"normal bat sw record base =0x%llx(%d*%u)\n",
			      (u64)bat_ring->sw_record_base, (int)sizeof(union dpmaif_bat_record),
			      bat_ring->bat_cnt);

		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"== dump normal bat mask tbl status ==\n");

		dpmaif_dump_bat_mask(dcb, bat_ring, DPMAIF_DUMP_BAT_CNT);

		if (!dcb->bat_infos[k].frag_bat_enabled)
			continue;

		/* Frag BAT information dump. */
		bat_ring = &dcb->bat_infos[k].frag_bat_ring;
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"== frag bat base=0x%llx(%d*%u) ==\n",
			      (u64)bat_ring->bat_base, (int)sizeof(struct dpmaif_bat),
			      bat_ring->bat_cnt);
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"frag bat: w=%u,r=%u\n", bat_ring->bat_wr_idx, bat_ring->bat_rd_idx);
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"frag bat: max_reload_cnt=%u,to_reload_cnt=%d,db_th=%u,reload_cnt=%d\n",
				  bat_ring->max_reload_cnt, atomic_read(&bat_ring->to_reload_cnt),
			      bat_ring->doorbell_th, atomic_read(&bat_ring->reload_cnt));
		if (dpmaif_hw_is_accessible(dcb))
			MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
				"hw_farg_bat: w=%d,r=%d\n",
				      drv_info->drv_ops->get_ring_idx(drv_info,
								  DPMAIF_FRAG_WIDX, k),
				      drv_info->drv_ops->get_ring_idx(drv_info,
								  DPMAIF_FRAG_RIDX, k));

		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"frag bat sw record base: 0x%llx(%d*%u)\n",
			      (u64)bat_ring->sw_record_base, (int)sizeof(union dpmaif_bat_record),
			      bat_ring->bat_cnt);

		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"== dump frag bat mask tbl status ==\n");

		dpmaif_dump_bat_mask(dcb, bat_ring, DPMAIF_DUMP_BAT_CNT);
	}
}

static void dpmaif_dump_stats(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned int i, n_stats;
	u8 *strings;
	u64 *stats;

	mtk_dpmaif_cmd_string_cnt_get(dcb, &n_stats);
	strings = devm_kzalloc(DCB_TO_DEV(dcb),
			       n_stats * DATA_TRANS_STRING_LEN, GFP_ATOMIC);

	if (!strings) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate stats strings\n");
		return;
	}
	mtk_dpmaif_describe_stats(dcb, strings);

	stats = devm_kzalloc(DCB_TO_DEV(dcb),
			     sizeof(u64) * n_stats, GFP_ATOMIC);
	if (!stats) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate stats\n");
		goto free_strings;
	}
	mtk_dpmaif_read_stats(dcb, stats);

	for (i = 0; i < n_stats; i++) {
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			"%s:%llu\n", &strings[i * DATA_TRANS_STRING_LEN], stats[i]);
	}

	devm_kfree(DCB_TO_DEV(dcb), stats);
free_strings:
	devm_kfree(DCB_TO_DEV(dcb), strings);
}

static void mtk_dpmaif_dump(struct mtk_md_dev *mdev)
{
	struct mtk_dpmaif_ctlb *dcb = ((struct mtk_data_blk *)(mdev->data_blk))->dcb;

	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
		"===start dump mtk dpmaif(cb=0x%llx) information===\n", (u64)dcb);

	dpmaif_dump_stats(dcb);

	/* Dump dpmaif drv information. */
	if (dpmaif_hw_is_accessible(dcb))
		dcb->drv_info->drv_ops->dump(dcb->drv_info);

	/* Dump dpmaif tx/rx information. */
	dpmaif_dump_txq_info(dcb, 0xffffffff);
	dpmaif_dump_rxq_info(dcb, 0xffffffff);
	dpmaif_dump_bat_info(dcb);
	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
		"===end dump mtk dpmaif information===\n");
}

static inline void dpmaif_dump_once(struct mtk_dpmaif_ctlb *dcb)
{
	if (atomic_cmpxchg(&dcb->dump_once, 0, 1) == 0)
		mtk_dpmaif_dump(DCB_TO_MDEV(dcb));
}

static inline void mtk_dpmaif_trigger_dev_exception(struct mtk_dpmaif_ctlb *dcb)
{
	mtk_pci_send_ext_evt(DCB_TO_MDEV(dcb), EXT_EVT_H2D_RESERVED_FOR_DPMAIF);
}

static void mtk_dpmaif_common_err_handle(struct mtk_dpmaif_ctlb *dcb, bool is_hw)
{
	MTK_ERR(DCB_TO_MDEV(dcb), "Enter common error handle: %ps, is_hw=%d\n",
		__builtin_return_address(0), is_hw);

	if (is_hw) {
		if (!dcb->data_blk->exception_dup_stop && dpmaif_hw_is_accessible(dcb)) {
			if (!test_and_set_bit(DATA_HW_CHECK_ERR, &dcb->err_event)) {
				dpmaif_dump_once(dcb);
				mtk_dpmaif_trigger_dev_exception(dcb);
			}
		}
	} else {
		if (!test_and_set_bit(DATA_SW_CHECK_ERR, &dcb->err_event)) {
			dpmaif_dump_once(dcb);
			WARN_ON_ONCE(true);
		}
	}
}

static void mtk_dpmaif_disable_irq(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned char irq_cnt = dcb->drv_info->cfg->intr_cfg.irq_cnt;
	struct dpmaif_irq_param *irq_param;
	int i;

	if (!dcb->irq_enabled)
		return;

	dcb->irq_enabled = false;
	for (i = 0; i < irq_cnt; i++) {
		irq_param = &dcb->irq_params[i];
		if (mtk_pci_mask_irq(DCB_TO_MDEV(dcb), irq_param->dev_irq_id) != 0)
			MTK_ERR(DCB_TO_MDEV(dcb),
				"Failed to mask dev irq%d\n", irq_param->dev_irq_id);
		synchronize_irq(irq_param->dev_virq_id);
	}
}

static void mtk_dpmaif_enable_irq(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned char irq_cnt = dcb->drv_info->cfg->intr_cfg.irq_cnt;
	struct dpmaif_irq_param *irq_param;
	int i;

	if (dcb->irq_enabled)
		return;

	dcb->irq_enabled = true;
	for (i = 0; i < irq_cnt; i++) {
		irq_param = &dcb->irq_params[i];
		if (mtk_pci_unmask_irq(DCB_TO_MDEV(dcb), irq_param->dev_irq_id) != 0)
			MTK_ERR(DCB_TO_MDEV(dcb),
				"Failed to unmask dev irq%d\n", irq_param->dev_irq_id);
	}
}

static void mtk_dpmaif_clear_irq(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned char irq_cnt = dcb->drv_info->cfg->intr_cfg.irq_cnt;
	struct dpmaif_irq_param *irq_param;
	int i;

	for (i = 0; i < irq_cnt; i++) {
		irq_param = &dcb->irq_params[i];
		if (mtk_pci_clear_irq(DCB_TO_MDEV(dcb), irq_param->dev_irq_id) != 0)
			MTK_ERR(DCB_TO_MDEV(dcb),
				"Failed to clear dev irq%d\n", irq_param->dev_irq_id);
	}
}

static void mtk_dpmaif_task_wakeup(struct dpmaif_task_ctlb *task_ctlb)
{
	task_ctlb->need_wp = true;
	wake_up(&task_ctlb->wait);
}

static void mtk_dpmaif_task_pause(struct dpmaif_task_ctlb *task_ctlb,
				  struct mtk_dpmaif_ctlb *dcb)
{
	bool need_wait = false;

	mutex_lock(&dcb->task_paused_lock);

	if (!test_and_set_bit(DATA_TASK_PAUSE, &task_ctlb->state)) {
		reinit_completion(&task_ctlb->paused_comp);
		mtk_dpmaif_task_wakeup(task_ctlb);
		need_wait = true;
	}
	task_ctlb->pause_ref++;
	mutex_unlock(&dcb->task_paused_lock);
	/* Wait for the task to acknowledge the pause outside task_paused_lock.
	 * Holding the lock while waiting would deadlock against the doorbell
	 * thread: it may call rpm_get_sync() which runs the resume
	 * callback in the same context, and the resume path (trans_enable ->
	 * mtk_dpmaif_task_resume) needs to acquire task_paused_lock.
	 * The pause/resume bookkeeping (DATA_TASK_PAUSE, pause_ref) is already
	 * updated under the lock above, so it is safe to wait.
	 */
	if (need_wait)
		wait_for_completion(&task_ctlb->paused_comp);

	MTK_INFO(DCB_TO_MDEV(dcb), "Pause thread by %ps! ref = %d, need_wait = %d\n",
		 __builtin_return_address(0), task_ctlb->pause_ref, need_wait);
}

static void mtk_dpmaif_task_resume(struct dpmaif_task_ctlb *task_ctlb,
				   struct mtk_dpmaif_ctlb *dcb, bool need_wp)
{
	mutex_lock(&dcb->task_paused_lock);
	task_ctlb->pause_ref--;

	if (unlikely(task_ctlb->pause_ref < 0))
		MTK_WARN(DCB_TO_MDEV(dcb),
			 "Note: pause_ref=%d is negative, called by %ps.\n",
			 task_ctlb->pause_ref, __builtin_return_address(0));

	if (!task_ctlb->pause_ref) {
		clear_bit(DATA_TASK_PAUSE, &task_ctlb->state);
		if (need_wp)
			mtk_dpmaif_task_wakeup(task_ctlb);
	}

	mutex_unlock(&dcb->task_paused_lock);

	MTK_INFO(DCB_TO_MDEV(dcb), "Start thread by %ps! ref = %d, need_wp=%d\n",
		 __builtin_return_address(0), task_ctlb->pause_ref, need_wp);
}

static enum hrtimer_restart mtk_dpmaif_doorbell_timer_func(struct hrtimer *t)
{
	struct dpmaif_doorbell_ctlb *db_ctlb;

	db_ctlb = container_of(t, struct dpmaif_doorbell_ctlb, db_timer);

	mtk_dpmaif_task_wakeup(&db_ctlb->task_ctlb);

	return HRTIMER_NORESTART;
}

static void mtk_dpmaif_book_doorbell_work(struct mtk_dpmaif_ctlb *dcb,
					  unsigned long long delay_ns)
{
	struct dpmaif_doorbell_ctlb *db_ctlb = &dcb->db_ctlb;
	unsigned long long now = 0;

	if (!delay_ns) {
		mtk_dpmaif_task_wakeup(&db_ctlb->task_ctlb);
		goto out;
	}

	now = ktime_get_ns();

	spin_lock(&db_ctlb->db_timer_lock);

	if (!hrtimer_active(&db_ctlb->db_timer)) {
		delay_ns -= (now - db_ctlb->db_base_ts) % delay_ns;
		hrtimer_start(&db_ctlb->db_timer, ns_to_ktime(delay_ns), HRTIMER_MODE_REL);
		goto release_lock;
	}

	if (ktime_after(hrtimer_get_expires(&db_ctlb->db_timer), ns_to_ktime(now + delay_ns))) {
		hrtimer_start(&db_ctlb->db_timer, ns_to_ktime(delay_ns), HRTIMER_MODE_REL);
		goto release_lock;
	}

	spin_unlock(&db_ctlb->db_timer_lock);

	return;

release_lock:
	spin_unlock(&db_ctlb->db_timer_lock);

out:
	trace_mtk_tras_data_tx(db_ctlb->db_base_ts, now, delay_ns);
}

static void mtk_dpmaif_book_tx_doorbell(struct mtk_dpmaif_ctlb *dcb,
					struct dpmaif_txq *txq)
{
	if (atomic_read(&txq->to_submit_cnt) >= txq->burst_submit_cnt || txq->exit_tcp_ss_counter)
		mtk_dpmaif_book_doorbell_work(dcb, 0);
	else
		mtk_dpmaif_book_doorbell_work(dcb, txq->db_delay_ns);

	if (txq->exit_tcp_ss_counter)
		txq->exit_tcp_ss_counter--;
}

static int mtk_dpmaif_alloc_rx_page(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_bat_ring *bat_ring,
				    unsigned short bat_idx)
{
	union dpmaif_bat_record *cur_bat_record;
	struct page_mapped_t *page_info;
	struct dpmaif_bat *cur_bat;
	void *data;

	cur_bat_record = bat_ring->sw_record_base + bat_idx;
	page_info = &cur_bat_record->frag;

	/* Pairs with smp_wmb() in mtk_dpmaif_get_rx_frag() */
	smp_rmb();

	/* For re-init flow, we don't release
	 * the rx buffer on FSM_STATE_OFF state.
	 * because we will pin rx buffers to BAT entries
	 * again on FSM_STATE_BOOTUP state,
	 * and also in bat preload case.
	 */
	if (page_info->page)
		return 0;

	data = netdev_alloc_frag(bat_ring->buf_size);
	if (unlikely(!data))
		return -ENOMEM;

	page_info->page = virt_to_head_page(data);
	page_info->offset = data - page_address(page_info->page);
	page_info->data_len = bat_ring->buf_size;
	page_info->data_dma_addr = dma_map_page(DCB_TO_DEV(dcb), page_info->page,
						page_info->offset, page_info->data_len,
						DMA_FROM_DEVICE);

	if (dma_mapping_error(DCB_TO_DEV(dcb), page_info->data_dma_addr)) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to map dma!\n");
		put_page(page_info->page);
		page_info->page = NULL;
		return -ENOMEM;
	}

	cur_bat = bat_ring->bat_base + bat_idx;
	cur_bat->buf_addr_high = cpu_to_le32(upper_32_bits(page_info->data_dma_addr));
	cur_bat->buf_addr_low = cpu_to_le32(lower_32_bits(page_info->data_dma_addr));

	return 0;
}

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
static int mtk_dpmaif_alloc_skb_with_fifo(struct mtk_dpmaif_ctlb *dcb,
					  struct dpmaif_bat_ring *bat_ring,
					  unsigned short bat_idx)
{
	struct dpmaif_skb_fifo_ctlb *skb_fifo_ctlb = &dcb->skb_fifo_ctlb;
	union dpmaif_bat_record *cur_bat_record;
	struct skb_mapped_t *skb_info;
	struct dpmaif_bat *cur_bat;

	cur_bat_record = bat_ring->sw_record_base + bat_idx;
	skb_info = &cur_bat_record->normal;

	/* Pairs with smp_wmb() in mtk_dpmaif_get_rx_pkt() */
	smp_rmb();

	/* For re-init flow, we don't release
	 * the rx buffer on FSM_STATE_OFF state.
	 * because we will pin rx buffers to BAT entries
	 * again on FSM_STATE_BOOTUP state,
	 * and also in bat preload case.
	 */
	if (skb_info->skb)
		return 0;

	if (FIFO_READABLE(&skb_fifo_ctlb->fifo)) {
		skb_info->skb = mtk_dpmaif_fifo_read(&skb_fifo_ctlb->fifo);
		skb_info->data_len = DPMAIF_SKB_CB_TO_MAP_INFO(skb_info->skb)->data_len;
		skb_info->data_dma_addr = DPMAIF_SKB_CB_TO_MAP_INFO(skb_info->skb)->data_dma_addr;
	} else {
		skb_info->skb = __dev_alloc_skb(bat_ring->buf_size, GFP_ATOMIC);
		if (unlikely(!skb_info->skb))
			return -ENOMEM;

		skb_info->data_len = bat_ring->buf_size;
		skb_info->data_dma_addr = dma_map_single(DCB_TO_DEV(dcb), skb_info->skb->data,
							 skb_info->data_len, DMA_FROM_DEVICE);
		if (dma_mapping_error(DCB_TO_DEV(dcb), skb_info->data_dma_addr)) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to map dma!\n");
			dev_kfree_skb_any(skb_info->skb);
			skb_info->skb = NULL;
			return -ENOMEM;
		}
	}

	cur_bat = bat_ring->bat_base + bat_idx;
	cur_bat->buf_addr_high = cpu_to_le32(upper_32_bits(skb_info->data_dma_addr));
	cur_bat->buf_addr_low = cpu_to_le32(lower_32_bits(skb_info->data_dma_addr));

	return 0;
}
#endif

static int mtk_dpmaif_alloc_skb(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_bat_ring *bat_ring,
				unsigned short bat_idx)
{
	union dpmaif_bat_record *cur_bat_record;
	struct skb_mapped_t *skb_info;
	struct dpmaif_bat *cur_bat;

	cur_bat_record = bat_ring->sw_record_base + bat_idx;
	skb_info = &cur_bat_record->normal;

	/* Pairs with smp_wmb() in mtk_dpmaif_get_rx_pkt() */
	smp_rmb();

	/* For re-init flow, we don't release
	 * the rx buffer on FSM_STATE_OFF state.
	 * because we will pin rx buffers to BAT entries
	 * again on FSM_STATE_BOOTUP state,
	 * and also in bat preload case.
	 */
	if (skb_info->skb)
		return 0;

	skb_info->skb = __dev_alloc_skb(bat_ring->buf_size, GFP_ATOMIC);
	if (unlikely(!skb_info->skb))
		return -ENOMEM;

	skb_info->data_len = bat_ring->buf_size;
	skb_info->data_dma_addr = dma_map_single(DCB_TO_DEV(dcb), skb_info->skb->data,
						 skb_info->data_len, DMA_FROM_DEVICE);
	if (dma_mapping_error(DCB_TO_DEV(dcb), skb_info->data_dma_addr)) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to map dma!\n");
		dev_kfree_skb_any(skb_info->skb);
		skb_info->skb = NULL;
		return -ENOMEM;
	}

	cur_bat = bat_ring->bat_base + bat_idx;
	cur_bat->buf_addr_high = cpu_to_le32(upper_32_bits(skb_info->data_dma_addr));
	cur_bat->buf_addr_low = cpu_to_le32(lower_32_bits(skb_info->data_dma_addr));

	return 0;
}

static int mtk_dpmaif_build_skb_with_pp_page(struct mtk_dpmaif_ctlb *dcb,
					     struct dpmaif_bat_ring *bat_ring,
					     unsigned short bat_idx)
{
	union dpmaif_bat_record *cur_bat_record;
	struct skb_mapped_t *skb_info;
	struct dpmaif_bat *cur_bat;
	struct page *page;
	u8 *data;

	cur_bat_record = bat_ring->sw_record_base + bat_idx;
	skb_info = &cur_bat_record->normal;

	/* Pairs with smp_wmb() in mtk_dpmaif_get_rx_pkt() */
	smp_rmb();

	/* For re-init flow, we don't release
	 * the rx buffer on FSM_STATE_OFF state.
	 * because we will pin rx buffers to BAT entries
	 * again on FSM_STATE_BOOTUP state,
	 * and also in bat preload case.
	 */
	if (skb_info->skb)
		return 0;

	page = page_pool_alloc_pages(bat_ring->pp, GFP_ATOMIC);
	if (unlikely(!page))
		return -ENOMEM;

	data = page_address(page);
	skb_info->skb = build_skb(data, bat_ring->pp_frag_size);
	if (unlikely(!skb_info->skb)) {
		page_pool_put_full_page(bat_ring->pp, page, false);
		return -ENOMEM;
	}

	skb_info->data_len = bat_ring->buf_size;
	skb_info->data_dma_addr = page_pool_get_dma_addr(page) + bat_ring->pp_head_offset;

	skb_reserve(skb_info->skb, bat_ring->pp_head_offset);
	skb_mark_for_recycle(skb_info->skb);

	cur_bat = bat_ring->bat_base + bat_idx;
	cur_bat->buf_addr_high = cpu_to_le32(upper_32_bits(skb_info->data_dma_addr));
	cur_bat->buf_addr_low = cpu_to_le32(lower_32_bits(skb_info->data_dma_addr));

	return 0;
}

static int mtk_dpmaif_build_skb_with_pp_frag(struct mtk_dpmaif_ctlb *dcb,
					     struct dpmaif_bat_ring *bat_ring,
					     unsigned short bat_idx)
{
	union dpmaif_bat_record *cur_bat_record;
	struct skb_mapped_t *skb_info;
	struct dpmaif_bat *cur_bat;
	unsigned int offset;
	struct page *page;
	u8 *data;

	cur_bat_record = bat_ring->sw_record_base + bat_idx;
	skb_info = &cur_bat_record->normal;

	/* Pairs with smp_wmb() in mtk_dpmaif_get_rx_pkt() */
	smp_rmb();

	/* For re-init flow, we don't release
	 * the rx buffer on FSM_STATE_OFF state.
	 * because we will pin rx buffers to BAT entries
	 * again on FSM_STATE_BOOTUP state,
	 * and also in bat preload case.
	 */
	if (skb_info->skb)
		return 0;

	page = page_pool_alloc_frag(bat_ring->pp, &offset, bat_ring->pp_frag_size, GFP_ATOMIC);
	if (unlikely(!page))
		return -ENOMEM;

	data = page_address(page) + offset;
	skb_info->skb = build_skb(data, bat_ring->pp_frag_size);
	if (unlikely(!skb_info->skb)) {
		page_pool_put_page(bat_ring->pp, page, -1, false);
		return -ENOMEM;
	}

	skb_info->data_len = bat_ring->buf_size;
	skb_info->data_dma_addr = page_pool_get_dma_addr(page) +
				bat_ring->pp_head_offset + offset;

	skb_reserve(skb_info->skb, bat_ring->pp_head_offset);
	skb_mark_for_recycle(skb_info->skb);

	cur_bat = bat_ring->bat_base + bat_idx;
	cur_bat->buf_addr_high = cpu_to_le32(upper_32_bits(skb_info->data_dma_addr));
	cur_bat->buf_addr_low = cpu_to_le32(lower_32_bits(skb_info->data_dma_addr));

	return 0;
}

static int mtk_dpmaif_pp_alloc_frag(struct mtk_dpmaif_ctlb *dcb,
				    struct dpmaif_bat_ring *bat_ring,
				    unsigned short bat_idx)
{
	union dpmaif_bat_record *cur_bat_record;
	struct page_mapped_t *page_info;
	struct dpmaif_bat *cur_bat;
	struct page *page;

	cur_bat_record = bat_ring->sw_record_base + bat_idx;
	page_info = &cur_bat_record->frag;

	/* Pairs with smp_wmb() in mtk_dpmaif_get_rx_frag() */
	smp_rmb();

	/* For re-init flow, we don't release
	 * the rx buffer on FSM_STATE_OFF state.
	 * because we will pin rx buffers to BAT entries
	 * again on FSM_STATE_BOOTUP state,
	 * and also in bat preload case.
	 */
	if (page_info->page)
		return 0;

	page = page_pool_alloc_frag(bat_ring->pp, &page_info->offset,
				    bat_ring->pp_frag_size, GFP_ATOMIC);
	if (unlikely(!page))
		return -ENOMEM;

	page_info->page = page;
	page_info->data_len = bat_ring->buf_size;
	page_info->data_dma_addr = page_pool_get_dma_addr(page) + page_info->offset;

	cur_bat = bat_ring->bat_base + bat_idx;
	cur_bat->buf_addr_high = cpu_to_le32(upper_32_bits(page_info->data_dma_addr));
	cur_bat->buf_addr_low = cpu_to_le32(lower_32_bits(page_info->data_dma_addr));

	return 0;
}

/* mtk_dpmaif_reload_rx_buff() - allocate BAT buffer and pin rx buffers to BAT entries
 * @dcb: pointer to mtk_dpmaif_ctlb
 * @bat_ring: pointer to dpmaif_bat_ring
 * @to_reload_cnt: the buffer count need to reload
 *
 * Return:
 *  * 0		- Buffer successfully allocated and pinned to BAT entries.
 *  * -EINVAL	- Can't pin rx buffer to BAT entries because of mask_tbl checking fail.
 *  * -ENOMEM	- Allocate memory fail or DMA mapping error.
 */
static int mtk_dpmaif_reload_rx_buff(struct mtk_dpmaif_ctlb *dcb,
				     struct dpmaif_bat_ring *bat_ring, unsigned int to_reload_cnt)
{
	unsigned short cur_bat_idx;
	int ret = -ENOMEM;
	unsigned int i;

	/* Pin rx buffers to BAT entries */
	cur_bat_idx = bat_ring->bat_wr_idx;

	for (i = 0; i < to_reload_cnt; i++) {
		if (test_bit(cur_bat_idx, bat_ring->mask_tbl)) {
			clear_bit(cur_bat_idx, bat_ring->mask_tbl);
		} else {
			ret = -EINVAL;
			break;
		}

		ret = bat_ring->alloc(dcb, bat_ring, cur_bat_idx);
		if (unlikely(ret)) {
			MTK_WARN(DCB_TO_MDEV(dcb),
				 "Failed to alloc rx buff, bat%u(%d) bid=%u\n",
				 bat_ring->id, bat_ring->type, cur_bat_idx);
			set_bit(cur_bat_idx, bat_ring->mask_tbl);
			break;
		}

		cur_bat_idx = mtk_dpmaif_ring_buf_get_next_idx(bat_ring->bat_cnt, cur_bat_idx);
	}

	if (unlikely(!i))
		return ret;

	trace_mtk_tput_data_bat_alloc(bat_ring->id, bat_ring->type, i);

	/* Make sure all bat write operation is done, before triggering doorbell. */
	dma_wmb();
	atomic_add(i, &bat_ring->reload_cnt);
	atomic_sub(i, &bat_ring->to_reload_cnt);
	bat_ring->bat_wr_idx = cur_bat_idx;

	return ret;
}

static void mtk_dpmaif_preload_rx_buf(struct mtk_dpmaif_ctlb *dcb,
				      struct dpmaif_bat_ring *bat_ring, unsigned int preload_cnt)
{
	int i, idx, ret;

	idx = bat_ring->bat_wr_idx;
	for (i = 0; i < preload_cnt; i++) {
		/* the next set bit means this buff consumed */
		idx = find_next_bit(bat_ring->mask_tbl, bat_ring->bat_cnt, idx + 1);
		if (idx >= bat_ring->bat_cnt)
			goto find_from_begin;

		ret = bat_ring->alloc(dcb, bat_ring, idx);
		if (ret) {
			MTK_WARN(DCB_TO_MDEV(dcb), "Failed to preload rx buff, bat%u(%d) bid=%u\n",
				 bat_ring->id, bat_ring->type, idx);
			break;
		}
	}

	goto out;

find_from_begin:
	idx = -1;
	for (; i < preload_cnt; i++) {
		idx = find_next_bit(bat_ring->mask_tbl, bat_ring->bat_wr_idx, idx + 1);
		if (idx >= bat_ring->bat_wr_idx)
			break;

		ret = bat_ring->alloc(dcb, bat_ring, idx);
		if (ret) {
			MTK_WARN(DCB_TO_MDEV(dcb), "Failed to preload rx buff, bat%u(%d) bid=%u\n",
				 bat_ring->id, bat_ring->type, idx);
			break;
		}
	}

out:
	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_MISC,
		"bat%u(%d) preload=%u/%u\n", bat_ring->id, bat_ring->type, i, preload_cnt);
	trace_mtk_tput_data_preload_bat(bat_ring->id, bat_ring->type, i, preload_cnt,
					bat_ring->bat_wr_idx, bat_ring->bat_rd_idx);
}

static int mtk_dpmaif_reload_bat(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_bat_ring *bat_ring)
{
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	struct dpmaif_skb_fifo_ctlb *skb_fifo_ctlb = &dcb->skb_fifo_ctlb;
	struct dpmaif_tput_stats *tput_stats = &dcb->tput_stats;
	int readable_cnt = 0;
#endif
	int pre_reload_cnt;
	int to_reload_cnt;
	int ret = 0;

	/* In the scenario of tput decrease, function mtk_dpmaif_bat_reload_ctrl
	 * decrease bat reload count to avoid waste of memory, and to_reload_cnt may be <= 0,
	 * bat reload work will be executed after the surplus bat consumption;
	 * In the scenario of bat reload work pending in the work queue,
	 * the first work reload all of the to_reload_cnt bat,
	 * and the second work reload count may be = 0.
	 */
	to_reload_cnt = atomic_read(&bat_ring->to_reload_cnt);
	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_MISC,
		"bat%u(%d),w=%d,r=%d,to_rel_cnt=%d\n",
		bat_ring->id, bat_ring->type, bat_ring->bat_wr_idx,
		bat_ring->bat_rd_idx, to_reload_cnt);

	trace_mtk_tput_data_bat(bat_ring->id, bat_ring->type, to_reload_cnt,
				bat_ring->max_reload_cnt, bat_ring->bat_wr_idx,
				bat_ring->bat_rd_idx);

	if (unlikely(to_reload_cnt <= 0))
		return 0;

	while (to_reload_cnt > 0) {
		pre_reload_cnt = to_reload_cnt > DPMAIF_REL_BAT_WEIGHT ?
				 DPMAIF_REL_BAT_WEIGHT : to_reload_cnt;

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
		if (dcb->dpmaif_rx_legacy) {
			readable_cnt = FIFO_READABLE(&skb_fifo_ctlb->fifo);
			if (tput_stats->high_speed &&
			    readable_cnt - pre_reload_cnt < DPMAIF_REFILL_FIFO_THRESHOLD)
				queue_work(skb_fifo_ctlb->wq, &skb_fifo_ctlb->work);
		}
#endif

		ret = mtk_dpmaif_reload_rx_buff(dcb, bat_ring, pre_reload_cnt);
		if (unlikely(ret))
			break;
		if (atomic_read(&bat_ring->reload_cnt) >= bat_ring->doorbell_th)
			mtk_dpmaif_book_doorbell_work(dcb, 0);
		to_reload_cnt -= pre_reload_cnt;
	}

	/* Those scenarios need doorbell directly:
	 * 1. reload buffer count meet expectations (ret != 0);
	 * 2. received bat_cnt_len_err interrupt;
	 */
	if ((ret || bat_ring->bat_cnt_err_intr_set) && atomic_read(&bat_ring->reload_cnt))
		mtk_dpmaif_book_doorbell_work(dcb, 0);

	/* Prefetch rx buff to accelerate next reload work */
	if (ret == -EINVAL) {
		mtk_dpmaif_preload_rx_buf(dcb, bat_ring, to_reload_cnt);
		ret = 0;
	}

	return ret;
}

static int mtk_dpmaif_bat_reload_thread(void *arg)
{
	struct dpmaif_bat_info *bat_info = arg;
	struct dpmaif_task_ctlb *task_ctlb;
	int ret;

	task_ctlb = &bat_info->task_ctlb;
#ifndef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	set_user_nice(current, -20);
#endif
	task_ctlb->pause_ref++;
	set_bit(DATA_TASK_PAUSE, &task_ctlb->state);
	while (1) {
		ret = wait_event_interruptible(task_ctlb->wait,
					       task_ctlb->need_wp ||
					       kthread_should_stop());
		task_ctlb->need_wp = false;
		if (unlikely(ret == -ERESTARTSYS))
			continue;

		if (unlikely(kthread_should_stop()))
			break;

		if (unlikely(test_bit(DATA_TASK_PAUSE, &task_ctlb->state))) {
			complete(&task_ctlb->paused_comp);
			continue;
		}

		mtk_dpmaif_reload_bat(bat_info->dcb, &bat_info->normal_bat_ring);

		if (bat_info->frag_bat_enabled)
			mtk_dpmaif_reload_bat(bat_info->dcb, &bat_info->frag_bat_ring);

		cond_resched();
	}

	return ret;
}

static int mtk_dpmaif_bat_pp_init(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_bat_ring *ring)
{
	struct page_pool_params pp_params = {
		.order = DPMAIF_PAGE_ORDER,
		.pool_size = ring->bat_cnt / (DPMAIF_PAGE_SIZE / ring->pp_frag_size) /
			     DPMAIF_PAGE_POOL_SIZE_FACTOR,
		.nid = dev_to_node(DCB_TO_DEV(dcb)),
		.dev = DCB_TO_DEV(dcb),
		.dma_dir = DMA_FROM_DEVICE,
		.max_len = DPMAIF_PAGE_SIZE,
		.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
	};

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 7, 0)
	pp_params.flags |= PP_FLAG_PAGE_FRAG;
#endif

	ring->pp = page_pool_create(&pp_params);
	if (IS_ERR(ring->pp))
		return -ENOMEM;

	return 0;
}

static int mtk_dpmaif_normal_bat_init(struct mtk_dpmaif_ctlb *dcb,
				      struct dpmaif_bat_ring *bat_ring,
				      int bat_ring_id)
{
	int ret;

	bat_ring->bat_cnt = dcb->drv_info->cfg->rx_cfg.bats[bat_ring_id].bat_cnt;
	bat_ring->max_reload_cnt = dcb->drv_info->cfg->rx_cfg.bats[bat_ring_id].reload_cnt;
	bat_ring->dynamic_reload = true;

	if (!dcb->dpmaif_rx_legacy) {
		bat_ring->pp_head_offset = ALIGN(NET_SKB_PAD,
						 dcb->drv_info->cfg->rx_cfg.pkt_alignment);
		bat_ring->pp_frag_size = ALIGN(SKB_HEAD_ALIGN(bat_ring->buf_size +
					       bat_ring->pp_head_offset),
					       dcb->drv_info->cfg->rx_cfg.pkt_alignment);
		ret = mtk_dpmaif_bat_pp_init(dcb, bat_ring);
		if (ret)
			return ret;
		MTK_INFO(DCB_TO_MDEV(dcb),
			 "bat%u(%d),pp_size:%u,max_len:%u,head_offset:%u,frag_size:%u\n",
			 bat_ring->id, NORMAL_BAT, bat_ring->pp->p.pool_size,
			 bat_ring->pp->p.max_len,
			 bat_ring->pp_head_offset, bat_ring->pp_frag_size);
		if (bat_ring->pp_frag_size > DPMAIF_HALF_PAGE_SIZE)
			bat_ring->alloc = mtk_dpmaif_build_skb_with_pp_page;
		else
			bat_ring->alloc = mtk_dpmaif_build_skb_with_pp_frag;
	} else {
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
		if (bat_ring_id == DPMAIF_BAT0)
			bat_ring->alloc = mtk_dpmaif_alloc_skb_with_fifo;
		else
			bat_ring->alloc = mtk_dpmaif_alloc_skb;
#else
		bat_ring->alloc = mtk_dpmaif_alloc_skb;
#endif
	}

	return 0;
}

static int mtk_dpmaif_frag_bat_init(struct mtk_dpmaif_ctlb *dcb,
				    struct dpmaif_bat_ring *bat_ring,
				    int bat_ring_id)
{
	int ret;

	bat_ring->bat_cnt = dcb->drv_info->cfg->rx_cfg.frags[bat_ring_id].bat_cnt;
	bat_ring->max_reload_cnt = dcb->drv_info->cfg->rx_cfg.frags[bat_ring_id].reload_cnt;
	bat_ring->dynamic_reload = false;

	if (!dcb->dpmaif_rx_legacy) {
		bat_ring->pp_frag_size = bat_ring->buf_size;
		ret = mtk_dpmaif_bat_pp_init(dcb, bat_ring);
		if (ret)
			return ret;
		MTK_INFO(DCB_TO_MDEV(dcb),
			 "bat%u(%d),pp_size:%u,max_len:%u,head_offset:%u,frag_size:%u\n",
			 bat_ring->id, FRAG_BAT, bat_ring->pp->p.pool_size,
			 bat_ring->pp->p.max_len,
			 bat_ring->pp_head_offset, bat_ring->pp_frag_size);
		bat_ring->alloc = mtk_dpmaif_pp_alloc_frag;
	} else {
		bat_ring->alloc = mtk_dpmaif_alloc_rx_page;
	}

	return 0;
}

static int mtk_dpmaif_bat_ring_init(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_bat_ring *bat_ring,
				    enum dpmaif_bat_type type, int bat_ring_id)
{
	int ret;

	bat_ring->id = bat_ring_id;
	bat_ring->type = type;

	if (type == NORMAL_BAT)
		ret = mtk_dpmaif_normal_bat_init(dcb, bat_ring, bat_ring_id);
	else
		ret = mtk_dpmaif_frag_bat_init(dcb, bat_ring, bat_ring_id);
	if (ret) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to initialize bat%u(%d)\n",
			bat_ring->id, bat_ring->type);
		return ret;
	}

	bat_ring->bat_cnt_err_intr_set = false;
	bat_ring->doorbell_th = MIN_BAT_BURST_CNT;
	atomic_set(&bat_ring->to_reload_cnt, bat_ring->max_reload_cnt);

	/* Allocate BAT memory for HW and SW. */
	bat_ring->bat_base = dma_alloc_coherent(DCB_TO_DEV(dcb), (bat_ring->bat_cnt +
						dcb->drv_info->cfg->rx_cfg.bat_wrap_cnt) *
						sizeof(*bat_ring->bat_base),
						&bat_ring->bat_dma_addr, GFP_KERNEL);
	if (!bat_ring->bat_base) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate bat%u(%d) buffer\n",
			bat_ring->id, bat_ring->type);
		return -ENOMEM;
	}

	/* Allocate buffer for SW to record skb information */
	bat_ring->sw_record_base = kvcalloc(bat_ring->bat_cnt,
					    sizeof(*bat_ring->sw_record_base), GFP_KERNEL);
	if (!bat_ring->sw_record_base) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate bat%u(%d) sw_record buffer\n",
			bat_ring->id, bat_ring->type);
		ret = -ENOMEM;
		goto free_bat_buf;
	}

	/* Allocate buffer for SW to recycle BAT. */
	bat_ring->mask_tbl = bitmap_alloc(bat_ring->bat_cnt, GFP_KERNEL);
	if (!bat_ring->mask_tbl) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate bat%u(%d) mask table\n",
			bat_ring->id, bat_ring->type);
		ret = -ENOMEM;
		goto free_sw_record_base;
	}

	bitmap_fill(bat_ring->mask_tbl, bat_ring->bat_cnt);

	MTK_INFO(DCB_TO_MDEV(dcb),
		 "bat%u(%d) base=0x%llx, size=%u, addr=0x%llx,cnt=%u, swbase=0x%llx, tbl=0x%llx\n",
		bat_ring->id, bat_ring->type, (u64)bat_ring->bat_base,
		bat_ring->buf_size, bat_ring->bat_dma_addr,
		bat_ring->bat_cnt, (u64)bat_ring->sw_record_base, (u64)bat_ring->mask_tbl);

	return 0;

free_sw_record_base:
	kvfree(bat_ring->sw_record_base);

free_bat_buf:
	dma_free_coherent(DCB_TO_DEV(dcb), (bat_ring->bat_cnt +
			  dcb->drv_info->cfg->rx_cfg.bat_wrap_cnt) *
			  sizeof(*bat_ring->bat_base),
			  bat_ring->bat_base, bat_ring->bat_dma_addr);

	return ret;
}

static void mtk_dpmaif_flush_bat_reload(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned int bat_ring_num = dcb->drv_info->cfg->rx_cfg.bat_ring_num;
	struct dpmaif_bat_ring *normal_bat_ring;
	struct dpmaif_bat_ring *frag_bat_ring;
	int i;

	for (i = 0; i < bat_ring_num; i++) {
		/* Wait bat/frag reload process done. */
		mtk_dpmaif_task_pause(&dcb->bat_infos[i].task_ctlb, dcb);

		normal_bat_ring = &dcb->bat_infos[i].normal_bat_ring;
		frag_bat_ring = &dcb->bat_infos[i].frag_bat_ring;

		MTK_INFO(DCB_TO_MDEV(dcb),
			 "bat%d,normal:r=%u,w=%u; frag:r=%u,w=%u\n",
			 i, normal_bat_ring->bat_rd_idx, normal_bat_ring->bat_wr_idx,
			 frag_bat_ring->bat_rd_idx, frag_bat_ring->bat_wr_idx);
	}
}

static void mtk_dpmaif_bat_ring_exit(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_bat_ring *bat_ring,
				     enum dpmaif_bat_type type)
{
	union dpmaif_bat_record *bat_record;
	struct page *page;
	int i;

	bitmap_free(bat_ring->mask_tbl);

	if (type == NORMAL_BAT) {
		for (i = 0; i < bat_ring->bat_cnt; i++) {
			bat_record = bat_ring->sw_record_base + i;
			if (!bat_record->normal.skb)
				continue;

			if (dcb->dpmaif_rx_legacy)
				dma_unmap_single(DCB_TO_DEV(dcb),
						 bat_record->normal.data_dma_addr,
						 bat_record->normal.data_len,
						 DMA_FROM_DEVICE);
			dev_kfree_skb_any(bat_record->normal.skb);
		}
	} else {
		for (i = 0; i < bat_ring->bat_cnt; i++) {
			bat_record = bat_ring->sw_record_base + i;
			page = bat_record->frag.page;
			if (!page)
				continue;

			if (dcb->dpmaif_rx_legacy) {
				dma_unmap_page(DCB_TO_DEV(dcb),
					       bat_record->frag.data_dma_addr,
					       bat_record->frag.data_len,
					       DMA_FROM_DEVICE);
				put_page(page);
			} else {
				page_pool_put_page(bat_ring->pp, page, -1, false);
			}
		}
	}

	kvfree(bat_ring->sw_record_base);

	dma_free_coherent(DCB_TO_DEV(dcb), (bat_ring->bat_cnt +
			  dcb->drv_info->cfg->rx_cfg.bat_wrap_cnt) *
			  sizeof(*bat_ring->bat_base),
			  bat_ring->bat_base, bat_ring->bat_dma_addr);

	if (!dcb->dpmaif_rx_legacy)
		page_pool_destroy(bat_ring->pp);
}

static void mtk_dpmaif_bat_ring_reset(struct dpmaif_bat_ring *bat_ring)
{
	bat_ring->bat_cnt_err_intr_set = false;
	bat_ring->bat_wr_idx = 0;
	bat_ring->bat_rd_idx = 0;
	atomic_set(&bat_ring->reload_cnt, 0);
	atomic_set(&bat_ring->bat_stats, 0);
	atomic_set(&bat_ring->to_reload_cnt, bat_ring->max_reload_cnt);

	bitmap_fill(bat_ring->mask_tbl, bat_ring->bat_cnt);
}

static void mtk_dpmaif_bat_res_reset(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned int bat_ring_num = dcb->drv_info->cfg->rx_cfg.bat_ring_num;
	struct dpmaif_bat_ring *bat_ring;
	int i;

	for (i = 0; i < bat_ring_num; i++) {
		bat_ring = &dcb->bat_infos[i].normal_bat_ring;
		bat_ring->max_reload_cnt = dcb->drv_info->cfg->rx_cfg.bats[i].reload_cnt;
		bat_ring->dynamic_reload = true;
		mtk_dpmaif_bat_ring_reset(bat_ring);

		if (!dcb->bat_infos[i].frag_bat_enabled)
			continue;

		bat_ring = &dcb->bat_infos[i].frag_bat_ring;
		bat_ring->max_reload_cnt = dcb->drv_info->cfg->rx_cfg.frags[i].reload_cnt;
		bat_ring->dynamic_reload = false;
		mtk_dpmaif_bat_ring_reset(bat_ring);
	}
}

static void mtk_dpmaif_set_bat_buf_size(struct mtk_dpmaif_ctlb *dcb)
{
	int bat_ring_num = dcb->drv_info->cfg->rx_cfg.bat_ring_num;
	struct dpmaif_bat_info *bat_infos = dcb->bat_infos;
	unsigned int buf_size;
	int i;

	for (i = 0; i < bat_ring_num; i++) {
		bat_infos[i].max_mtu = dcb->drv_info->cfg->rx_cfg.mtu;

		/* Set max mtu, DPMAIF_JUMBO_SIZE. */
		if (bat_infos[i].max_mtu > DPMAIF_JUMBO_SIZE)
			bat_infos[i].max_mtu = DPMAIF_JUMBO_SIZE;

		/* Normal and frag BAT buffer size setting. */
		buf_size = bat_infos[i].max_mtu + dcb->drv_info->cfg->rx_cfg.pkt_alignment +
			dcb->drv_info->cfg->rx_cfg.normal_bat_rsv_length;

		if (buf_size <= DPMAIF_NORMAL_BUF_SIZE_IN_JUMBO ||
		    !(dcb->drv_info->cfg->cap & DATA_HW_F_FRAG)) {
			bat_infos[i].frag_bat_enabled = false;
			bat_infos[i].normal_bat_ring.buf_size = ALIGN(buf_size,
								      DPMAIF_DL_BUF_MIN_SIZE);
			bat_infos[i].frag_bat_ring.buf_size = 0;
		} else {
			bat_infos[i].frag_bat_enabled = true;
			bat_infos[i].normal_bat_ring.buf_size = DPMAIF_NORMAL_BUF_SIZE_IN_JUMBO;
			bat_infos[i].frag_bat_ring.buf_size = DPMAIF_FRAG_BUF_SIZE_IN_JUMBO;
		}

		MTK_INFO(DCB_TO_MDEV(dcb),
			 "dpmaif bat%d: mtu=%u, frag_enable=%d, normal_buf_size=%u, frag_buf_size=%u\n",
			i, bat_infos[i].max_mtu, bat_infos[i].frag_bat_enabled,
			bat_infos[i].normal_bat_ring.buf_size,
			bat_infos[i].frag_bat_ring.buf_size);
	}
	/* At present, a NIC only corresponds to one mtu,
	 * here take the value of bat_infos[0].max_mtu.
	 */
	dcb->drv_info->cfg->rx_cfg.mtu = dcb->bat_infos[0].max_mtu;
}

static void mtk_dpmaif_task_info_clear(struct dpmaif_task_ctlb *task_ctlb)
{
	task_ctlb->pause_ref = 0;
	task_ctlb->state = 0;
}

static void mtk_dpmaif_reload_task_exit(struct dpmaif_bat_info *bat_info)
{
	if (bat_info->reload_task) {
		kthread_stop(bat_info->reload_task);
		bat_info->reload_task = NULL;
	}
	mtk_dpmaif_task_info_clear(&bat_info->task_ctlb);
}

static void mtk_dpmaif_bat_info_exit(struct mtk_dpmaif_ctlb *dcb, int bat_ring_id)
{
	struct dpmaif_bat_info *bat_infos = dcb->bat_infos;

	mtk_dpmaif_reload_task_exit(&bat_infos[bat_ring_id]);
	mtk_dpmaif_bat_ring_exit(dcb, &bat_infos[bat_ring_id].normal_bat_ring, NORMAL_BAT);
	if (bat_infos[bat_ring_id].frag_bat_enabled)
		mtk_dpmaif_bat_ring_exit(dcb, &bat_infos[bat_ring_id].frag_bat_ring, FRAG_BAT);
}

static void mtk_dpmaif_bat_res_exit(struct mtk_dpmaif_ctlb *dcb)
{
	int bat_ring_num = dcb->drv_info->cfg->rx_cfg.bat_ring_num;
	int i;

	for (i = 0; i < bat_ring_num; i++)
		mtk_dpmaif_bat_info_exit(dcb, i);

	devm_kfree(DCB_TO_DEV(dcb), dcb->bat_infos);
}

static int mtk_dpmaif_bat_info_init(struct mtk_dpmaif_ctlb *dcb,
				    struct dpmaif_bat_info *bat_info, int bat_ring_id)
{
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	struct mtk_data_cpu_affinity_cfg *aff_cfg = &dcb->aff_cfg[DATA_DEFAULT_AFF_MODE];
#endif
	int ret;

	bat_info->dcb = dcb;
	ret = mtk_dpmaif_bat_ring_init(dcb, &bat_info->normal_bat_ring, NORMAL_BAT, bat_ring_id);
	if (ret < 0) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to initialize normal bat ring\n");
		return ret;
	}

	if (bat_info->frag_bat_enabled) {
		ret = mtk_dpmaif_bat_ring_init(dcb, &bat_info->frag_bat_ring, FRAG_BAT,
					       bat_ring_id);
		if (ret < 0) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to initialize frag bat ring\n");
			goto normal_bat_exit;
		}
	}

	init_waitqueue_head(&bat_info->task_ctlb.wait);
	init_completion(&bat_info->task_ctlb.paused_comp);
	bat_info->reload_task = kthread_run(mtk_dpmaif_bat_reload_thread, bat_info,
					    "dpmaif_reload_%d", bat_ring_id);
	if (IS_ERR(bat_info->reload_task)) {
		MTK_ERR(DCB_TO_MDEV(dcb),
			"Failed to create dpmaif bat reload thread, Errcode=%ld\n",
			PTR_ERR(bat_info->reload_task));
		bat_info->reload_task = NULL;
		ret = -ENOMEM;
		goto frag_bat_exit;
	}

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	sched_set_fifo(bat_info->reload_task);
	if (dcb->aff_cfg)
		mtk_dpmaif_set_task_affinity(dcb,
					     aff_cfg->reload_thrd_aff[bat_ring_id],
					     bat_info->reload_task);
#endif

	return 0;

frag_bat_exit:
	if (bat_info->frag_bat_enabled)
		mtk_dpmaif_bat_ring_exit(dcb, &bat_info->frag_bat_ring, FRAG_BAT);

normal_bat_exit:
	mtk_dpmaif_bat_ring_exit(dcb, &bat_info->normal_bat_ring, NORMAL_BAT);

	return ret;
}

static int mtk_dpmaif_bat_res_init(struct mtk_dpmaif_ctlb *dcb)
{
	int bat_ring_num = dcb->drv_info->cfg->rx_cfg.bat_ring_num;
	int i, j;
	int ret;

	dcb->bat_infos = devm_kcalloc(DCB_TO_DEV(dcb), bat_ring_num, sizeof(*dcb->bat_infos),
				      GFP_KERNEL);
	if (!dcb->bat_infos) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate dcb->bat_infos\n");
		return -ENOMEM;
	}

	/* Check and set normal and frag bat buffer size. */
	mtk_dpmaif_set_bat_buf_size(dcb);

	for (i = 0; i < bat_ring_num; i++) {
		ret = mtk_dpmaif_bat_info_init(dcb, &dcb->bat_infos[i], i);
		if (ret < 0) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to initialize bat_info%d\n", i);
			goto bat_info_exit;
		}
	}

	return 0;

bat_info_exit:
	for (j = i - 1; j >= 0; j--)
		mtk_dpmaif_bat_info_exit(dcb, j);

	devm_kfree(DCB_TO_DEV(dcb), dcb->bat_infos);

	return ret;
}

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
static void mtk_dpmaif_rx_steer(struct work_struct *work)
{
	struct dpmaif_rxq *rxq = container_of(work, struct dpmaif_rxq, steer_work);
	unsigned long steer_tmo = jiffies + msecs_to_jiffies(2000);
	unsigned long flush_tmo = jiffies + msecs_to_jiffies(2);
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	struct mtk_fifo_t *fifo;
	struct sk_buff *skb;
	int cnt = 0;

	fifo = &rxq->fifo;
	while (FIFO_READABLE(fifo) > 0) {
		skb = mtk_dpmaif_dl_dequeue(rxq);
		mtk_wwan_recv(dcb->data_blk, skb);

		if ((++cnt & (NAPI_POLL_WEIGHT - 1)) == 0) {
			if (time_after_eq(jiffies, flush_tmo)) {
				mtk_wwan_notify(dcb->data_blk, DATA_EVT_RX_FLUSH, rxq->id);
				flush_tmo = jiffies + msecs_to_jiffies(2);
				cnt = 0;
				cond_resched();
			}
			if (unlikely(time_after_eq(jiffies, steer_tmo))) {
				mtk_wwan_notify(dcb->data_blk, DATA_EVT_RX_FLUSH, rxq->id);
				queue_work_on(rxq->cpu_id, rxq->steer_wq, &rxq->steer_work);
				return;
			}
		}
	}

	mtk_wwan_notify(dcb->data_blk, DATA_EVT_RX_FLUSH, rxq->id);
}

static int mtk_dpmaif_get_work_cpu(struct dpmaif_rxq *rxq)
{
	if (likely(rxq->dcb->aff_cfg))
		return rxq->dcb->aff_cfg[DATA_DEFAULT_AFF_MODE].steer_wq_aff[rxq->id];

	return cpumask_first(cpu_online_mask);
}

static int mtk_dpmaif_rxq_steer_work_init(struct dpmaif_rxq *rxq)
{
	char wq_name[48];
	int ret;

	ret = mtk_dpmaif_fifo_init(&rxq->fifo,
				   rxq->dcb->drv_info->cfg->rx_cfg.rxqs[rxq->id].qsize);
	if (ret < 0) {
		MTK_ERR(DCB_TO_MDEV(rxq->dcb), "Failed to init rxq%u pool\n", rxq->id);
		return ret;
	}

	snprintf(wq_name, sizeof(wq_name), "mtk_rx_push_%s", DCB_TO_MDEV(rxq->dcb)->dev_str);
	rxq->steer_wq = alloc_workqueue(wq_name, WQ_CPU_INTENSIVE | WQ_MEM_RECLAIM | WQ_HIGHPRI, 0);
	if (!rxq->steer_wq) {
		mtk_dpmaif_fifo_exit(&rxq->fifo);
		return -ENOMEM;
	}

	INIT_WORK(&rxq->steer_work, mtk_dpmaif_rx_steer);
	rxq->cpu_id = mtk_dpmaif_get_work_cpu(rxq);

	return 0;
}

static void mtk_dpmaif_rxq_steer_work_exit(struct dpmaif_rxq *rxq)
{
	flush_workqueue(rxq->steer_wq);
	destroy_workqueue(rxq->steer_wq);

	/* Drop all packet in rx virtual queues. */
	mtk_dpmaif_fifo_skb_free(&rxq->fifo);
	mtk_dpmaif_fifo_exit(&rxq->fifo);
}
#endif

static int mtk_dpmaif_rxq_init(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_rxq *rxq)
{
	char ws_name[MTK_DATA_WS_NAME_LEN];
	int ret;

	rxq->pit_seq_max = dcb->drv_info->cfg->rx_cfg.rxqs[rxq->id].pit_seq_max;
	rxq->bat_ring_id = dcb->drv_info->cfg->rx_cfg.rxqs[rxq->id].bat_ring_id;
	rxq->pit_cnt = dcb->drv_info->cfg->rx_cfg.rxqs[rxq->id].pit_cnt;
	rxq->attr = dcb->drv_info->cfg->rx_cfg.rxqs[rxq->id].attr;
	rxq->pit_burst_rel_cnt = DPMAIF_PIT_CNT_UPDATE_THRESHOLD;
	rxq->intr_coalesce_frame = dcb->intr_coalesce.rx_coalesced_frames;

	snprintf(ws_name, sizeof(ws_name), "dpmaif_rxq%d_ws", rxq->id);

	rxq->ws = wakeup_source_register(NULL, ws_name);
	if (!rxq->ws) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to register rxq%d wakeup source\n", rxq->id);
		return -ENOMEM;
	}

	if (rxq->attr & DPMAIFQ_ATTR_PIT_CACHED) {
		rxq->pit_base = dma_alloc_noncoherent(DCB_TO_DEV(dcb),
						      rxq->pit_cnt * sizeof(*rxq->pit_base),
						      &rxq->pit_dma_addr,
						      DMA_FROM_DEVICE,
						      GFP_KERNEL);
		if (!rxq->pit_base) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate rxq%u pit resource\n",
				rxq->id);
			ret = -ENOMEM;
			goto unregister_ws;
		}
	} else {
		rxq->pit_base = dma_alloc_coherent(DCB_TO_DEV(dcb),
						   rxq->pit_cnt * sizeof(*rxq->pit_base),
						   &rxq->pit_dma_addr, GFP_KERNEL);
		if (!rxq->pit_base) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate rxq%u pit resource\n",
				rxq->id);
			ret = -ENOMEM;
			goto unregister_ws;
		}
	}

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	if (!(rxq->attr & DPMAIFQ_ATTR_LOW_LATENCY)) {
		ret = mtk_dpmaif_rxq_steer_work_init(rxq);
		if (ret < 0) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to init rxq%u steer work\n", rxq->id);
			goto free_pit;
		}
	}
#endif

	__skb_queue_head_init(&rxq->rx_record.rx_list);

	MTK_INFO(DCB_TO_MDEV(dcb), "rxq%d: pit_base=0x%llx, pit_dma_addr=0x%llx, pit_cnt=%u\n",
		 rxq->id, (u64)rxq->pit_base, rxq->pit_dma_addr, rxq->pit_cnt);

	return 0;
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
free_pit:
	if (rxq->attr & DPMAIFQ_ATTR_PIT_CACHED) {
		dma_free_noncoherent(DCB_TO_DEV(dcb),
				     rxq->pit_cnt * sizeof(*rxq->pit_base),
				     rxq->pit_base, rxq->pit_dma_addr, DMA_FROM_DEVICE);
	} else {
		dma_free_coherent(DCB_TO_DEV(dcb), rxq->pit_cnt * sizeof(*rxq->pit_base),
				  rxq->pit_base, rxq->pit_dma_addr);
	}
#endif
unregister_ws:
	wakeup_source_unregister(rxq->ws);

	return ret;
}

static void mtk_dpmaif_rxq_exit(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_rxq *rxq)
{
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	if (!(rxq->attr & DPMAIFQ_ATTR_LOW_LATENCY))
		mtk_dpmaif_rxq_steer_work_exit(rxq);
#endif

	if (rxq->attr & DPMAIFQ_ATTR_PIT_CACHED) {
		dma_free_noncoherent(DCB_TO_DEV(dcb),
				     rxq->pit_cnt * sizeof(*rxq->pit_base),
				     rxq->pit_base, rxq->pit_dma_addr, DMA_FROM_DEVICE);
	} else {
		dma_free_coherent(DCB_TO_DEV(dcb), rxq->pit_cnt * sizeof(*rxq->pit_base),
				  rxq->pit_base, rxq->pit_dma_addr);
	}
	wakeup_source_unregister(rxq->ws);
}

static int mtk_dpmaif_sw_stop_rxq(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_rxq *rxq)
{
	/* Rxq done process will check this flag, if rxq->started is false, process will stop. */
	rxq->started = false;
	MTK_INFO(DCB_TO_MDEV(dcb), "rxq%u pit:r=%u,w=%u,rel=%u\n",
		 rxq->id, rxq->pit_rd_idx, rxq->pit_wr_idx,
		 rxq->pit_rel_rd_idx);

	/* Make sure rxq->started value update done. */
	smp_mb();

	/* Wait rxq process done. */
	napi_synchronize(&rxq->napi);

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	if (!(rxq->attr & DPMAIFQ_ATTR_LOW_LATENCY))
		flush_workqueue(rxq->steer_wq);
#endif
	return 0;
}

static void mtk_dpmaif_sw_stop_rx(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_rxq *rxq;
	int i;

	/* Stop all rx process. */
	for (i = 0; i < dcb->rxq_cnt; i++) {
		rxq = &dcb->rxqs[i];
		mtk_dpmaif_sw_stop_rxq(dcb, rxq);
	}

	/* Stop PIT polling NAPI. */
	mtk_wwan_notify(dcb->data_blk, DATA_EVT_RX_STOP, 0xff);
}

static void mtk_dpmaif_sw_start_rx(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_rxq *rxq;
	int i;

	/* Start PIT polling NAPI. */
	mtk_wwan_notify(dcb->data_blk, DATA_EVT_RX_START, 0xff);

	for (i = 0; i < dcb->rxq_cnt; i++) {
		rxq = &dcb->rxqs[i];
		rxq->started = true;
	}
}

static void mtk_dpmaif_sw_reset_rxq(struct dpmaif_rxq *rxq)
{
	memset(rxq->pit_base, 0x00, (rxq->pit_cnt * sizeof(*rxq->pit_base)));
	memset(&rxq->rx_record, 0x00, sizeof(rxq->rx_record));
	__skb_queue_head_init(&rxq->rx_record.rx_list);

	rxq->started = false;
	rxq->pit_wr_idx = 0;
	rxq->pit_rd_idx = 0;
	rxq->pit_rel_rd_idx = 0;
	rxq->pit_seq_expect = 0;
	atomic_set(&rxq->pit_rel_cnt, 0);
	rxq->pit_cnt_err_intr_set = false;
	rxq->pit_seq_fail_cnt = 0;
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	if (!(rxq->attr & DPMAIFQ_ATTR_LOW_LATENCY) && rxq->dcb->dpmaif_rx_legacy)
		mtk_dpmaif_fifo_skb_free(&rxq->fifo);
#endif
}

static void mtk_dpmaif_rx_res_reset(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_rxq *rxq;
	int i;

	for (i = 0; i < dcb->rxq_cnt; i++) {
		rxq = &dcb->rxqs[i];
		mtk_dpmaif_sw_reset_rxq(rxq);
	}
}

static int mtk_dpmaif_rx_res_init(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_rxq *rxq;
	int i, j;
	int ret;

	dcb->rxqs = devm_kcalloc(DCB_TO_DEV(dcb), dcb->rxq_cnt, sizeof(*rxq), GFP_KERNEL);
	if (!dcb->rxqs) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate rxqs\n");
		return -ENOMEM;
	}

	for (i = 0; i < dcb->rxq_cnt; i++) {
		rxq = &dcb->rxqs[i];
		rxq->id = i;
		rxq->dcb = dcb;
		ret = mtk_dpmaif_rxq_init(dcb, rxq);
		if (ret < 0) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to init rxq%u resource\n", rxq->id);
			goto exit_rxq;
		}
	}

	return 0;

exit_rxq:
	for (j = i - 1; j >= 0; j--)
		mtk_dpmaif_rxq_exit(dcb, &dcb->rxqs[j]);

	devm_kfree(DCB_TO_DEV(dcb), dcb->rxqs);

	return ret;
}

static void mtk_dpmaif_rx_res_exit(struct mtk_dpmaif_ctlb *dcb)
{
	int i;

	for (i = 0; i < dcb->rxq_cnt; i++)
		mtk_dpmaif_rxq_exit(dcb, &dcb->rxqs[i]);

	devm_kfree(DCB_TO_DEV(dcb), dcb->rxqs);
}

static bool mtk_dpmaif_has_doorbell(struct mtk_dpmaif_ctlb *dcb)
{
	int bat_ring_num = dcb->drv_info->cfg->rx_cfg.bat_ring_num;
	int txq_cnt = dcb->drv_info->cfg->tx_cfg.txq_cnt;
	struct dpmaif_bat_ring *bat_ring;
	int i;

	/* Tx drb doorbell */
	for (i = 0; i < txq_cnt; i++) {
		if (atomic_read(&dcb->txqs[i].to_submit_cnt) > 0)
			return true;
	}

	/* Rx pit doorbell */
	for (i = 0; i < dcb->rxq_cnt; i++) {
		if (atomic_read(&dcb->rxqs[i].pit_rel_cnt) > 0)
			return true;
	}

	/* Rx bat/frag doorbell */
	for (i = 0; i < bat_ring_num; i++) {
		bat_ring = &dcb->bat_infos[i].normal_bat_ring;
		if (atomic_read(&bat_ring->reload_cnt) > 0)
			return true;

		if (!dcb->bat_infos[i].frag_bat_enabled)
			continue;

		bat_ring = &dcb->bat_infos[i].frag_bat_ring;
		if (atomic_read(&bat_ring->reload_cnt) > 0)
			return true;
	}

	return false;
}

static int mtk_dpmaif_tx_doorbell(struct mtk_dpmaif_ctlb *dcb)
{
	int txq_cnt = dcb->drv_info->cfg->tx_cfg.txq_cnt;
	int doorbell_cnt, i, ret = 0;

	/* First of all, send Tx packets. */
	for (i = 0; i < txq_cnt; i++) {
		doorbell_cnt = atomic_read(&dcb->txqs[i].to_submit_cnt);
		if (doorbell_cnt > 0) {
#ifdef CONFIG_DEBUG_FS
			if (test_bit(DPMAIF_DUMP_DRB, &dcb->dump_flag))
				dpmaif_dump_drb(dcb, &dcb->txqs[i], 6);
#endif
			ret = mtk_dpmaif_drv_send_doorbell(dcb->drv_info,
							   DPMAIF_DRB, i, doorbell_cnt);
			if (unlikely(ret < 0)) {
				MTK_ERR(DCB_TO_MDEV(dcb), "Failed to send txq%d doorbell\n", i);
				mtk_dpmaif_common_err_handle(dcb, true);
				return ret;
			}
			atomic_sub(doorbell_cnt, &dcb->txqs[i].to_submit_cnt);
			MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_TX,
				"txq%u doorbell drb cnt=%u\n",
					dcb->txqs[i].id, doorbell_cnt);
			trace_mtk_tput_data_tx(i, "4", INVALID_PACKET_ID);
		}
	}

	return ret;
}

static int mtk_dpmaif_rx_doorbell(struct mtk_dpmaif_ctlb *dcb)
{
	int bat_ring_num = dcb->drv_info->cfg->rx_cfg.bat_ring_num;
	struct dpmaif_bat_ring *bat_ring;
	int doorbell_cnt, i, ret = 0;
	int tmp_bat_wr_idx;

	/* recycle PIT */
	for (i = 0; i < dcb->rxq_cnt; i++) {
		doorbell_cnt = atomic_read(&dcb->rxqs[i].pit_rel_cnt);
		if (doorbell_cnt <= 0)
			continue;

		ret = mtk_dpmaif_drv_send_doorbell(dcb->drv_info, DPMAIF_PIT, i, doorbell_cnt);
		if (unlikely(ret < 0)) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to send pit%d doorbell\n", i);
			mtk_dpmaif_common_err_handle(dcb, true);
			return ret;
		}
		atomic_sub(doorbell_cnt, &dcb->rxqs[i].pit_rel_cnt);

		if (dcb->rxqs[i].pit_cnt_err_intr_set) {
			dcb->rxqs[i].pit_cnt_err_intr_set = false;
			mtk_dpmaif_drv_intr_complete(dcb->drv_info, DPMAIF_INTR_DL_PITCNT_LEN_ERR,
						     i, 0);
		}

		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_MISC,
			"rxq%u, doorbell pit cnt=%u\n", dcb->rxqs[i].id, doorbell_cnt);
	}

	for (i = 0; i < bat_ring_num; i++) {
		/* recycle BAT */
		bat_ring = &dcb->bat_infos[i].normal_bat_ring;
		tmp_bat_wr_idx = bat_ring->bat_wr_idx;
		doorbell_cnt = atomic_read(&bat_ring->reload_cnt);
		if (doorbell_cnt > 0) {
			ret = mtk_dpmaif_drv_send_doorbell(dcb->drv_info,
							   DPMAIF_BAT, i, doorbell_cnt);
			if (unlikely(ret < 0)) {
				MTK_ERR(DCB_TO_MDEV(dcb), "Failed to send bat doorbell\n");
				mtk_dpmaif_common_err_handle(dcb, true);
				return ret;
			}
			atomic_sub(doorbell_cnt, &bat_ring->reload_cnt);

			if (bat_ring->bat_cnt_err_intr_set) {
				bat_ring->bat_cnt_err_intr_set = false;
				mtk_dpmaif_drv_intr_complete(dcb->drv_info,
							     DPMAIF_INTR_DL_BATCNT_LEN_ERR, i, 0);
			}

			ret = dcb->drv_info->drv_ops->get_ring_idx(dcb->drv_info,
								   DPMAIF_BAT_RIDX, i);
			if (unlikely(ret < 0)) {
				MTK_ERR(DCB_TO_MDEV(dcb), "Failed to update bat_rd_idx\n");
				mtk_dpmaif_common_err_handle(dcb, true);
				return ret;
			}
			bat_ring->bat_rd_idx = ret;

			MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_MISC,
				"bat%u(%d),w=%u,r=%u,db_cnt=%u\n",
				bat_ring->id, bat_ring->type, bat_ring->bat_wr_idx,
				bat_ring->bat_rd_idx, doorbell_cnt);
			trace_mtk_tput_data_bat_db(bat_ring->id, bat_ring->type,
						   tmp_bat_wr_idx,
						   bat_ring->bat_rd_idx,
						   doorbell_cnt);
		}
		/* recycle FRAG BAT */
		if (!dcb->bat_infos[i].frag_bat_enabled)
			continue;

		bat_ring = &dcb->bat_infos[i].frag_bat_ring;
		tmp_bat_wr_idx = bat_ring->bat_wr_idx;
		doorbell_cnt = atomic_read(&bat_ring->reload_cnt);
		if (doorbell_cnt <= 0)
			continue;

		ret = mtk_dpmaif_drv_send_doorbell(dcb->drv_info, DPMAIF_FRAG, i, doorbell_cnt);
		if (unlikely(ret < 0)) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to send frag_bat doorbell\n");
			mtk_dpmaif_common_err_handle(dcb, true);
			return ret;
		}
		atomic_sub(doorbell_cnt, &bat_ring->reload_cnt);

		if (bat_ring->bat_cnt_err_intr_set) {
			bat_ring->bat_cnt_err_intr_set = false;
			mtk_dpmaif_drv_intr_complete(dcb->drv_info, DPMAIF_INTR_DL_FRGCNT_LEN_ERR,
						     i, 0);
		}

		ret = dcb->drv_info->drv_ops->get_ring_idx(dcb->drv_info, DPMAIF_FRAG_RIDX, i);
		if (unlikely(ret < 0)) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to update frag_bat_rd_idx\n");
			mtk_dpmaif_common_err_handle(dcb, true);
			return ret;
		}
		bat_ring->bat_rd_idx = ret;

		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_MISC,
			"bat%u(%d),w=%u,r=%u,db_cnt=%u\n",
			bat_ring->id, bat_ring->type, bat_ring->bat_wr_idx,
			bat_ring->bat_rd_idx, doorbell_cnt);

		trace_mtk_tput_data_bat_db(bat_ring->id, bat_ring->type, tmp_bat_wr_idx,
					   bat_ring->bat_rd_idx, doorbell_cnt);
	}

	return 0;
}

/**
 * mtk_dpmaif_doorbell_thread() - tx/rx doorbell thread
 * @arg: dpmaif contrl block
 *
 * Merge all register WRITE operations,
 * including adding DRB count, PIT count and BAT count.
 *
 * Return: return value is 0 on success, a negative error code on failure.
 */
static int mtk_dpmaif_doorbell_thread(void *arg)
{
	struct dpmaif_task_ctlb *task_ctlb;
	struct mtk_dpmaif_ctlb *dcb = arg;
	int ret = 0;

	task_ctlb = &dcb->db_ctlb.task_ctlb;

	while (1) {
		wait_event_interruptible(task_ctlb->wait,
					 task_ctlb->need_wp || kthread_should_stop());
		task_ctlb->need_wp = false;
		if (unlikely(ret == -ERESTARTSYS))
			continue;

		if (kthread_should_stop())
			break;

		if (unlikely(test_bit(DATA_TASK_PAUSE, &task_ctlb->state))) {
			complete(&task_ctlb->paused_comp);
			continue;
		}

		mtk_pm_runtime_get(DCB_TO_MDEV(dcb), MTK_USER_DATA, true);

		if (unlikely(!dcb->trans_enabled)) {
			mtk_pm_runtime_put(DCB_TO_MDEV(dcb), MTK_USER_DATA, true);
			continue;
		}

		mtk_pm_ds_lock(DCB_TO_MDEV(dcb), MTK_USER_DATA);
		ret = mtk_pm_ds_wait_complete(DCB_TO_MDEV(dcb), MTK_USER_DATA);
		if (unlikely(ret < 0)) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to wait ds_lock\n");
			mtk_dpmaif_common_err_handle(dcb, true);
			goto out;
		}

		ret = mtk_dpmaif_tx_doorbell(dcb);
		if (unlikely(ret < 0))
			goto out;

		ret = mtk_dpmaif_rx_doorbell(dcb);

out:
		mtk_pm_ds_unlock(DCB_TO_MDEV(dcb), MTK_USER_DATA);
		mtk_pm_runtime_put(DCB_TO_MDEV(dcb), MTK_USER_DATA, true);

		cond_resched();
	}

	return ret;
}

static unsigned int mtk_dpmaif_poll_tx_drb(struct dpmaif_txq *txq)
{
	unsigned short old_sw_rd_idx, new_hw_rd_idx;
	struct mtk_dpmaif_ctlb *dcb = txq->dcb;
	unsigned int drb_cnt;
	int ret;

	old_sw_rd_idx = txq->drb_rd_idx;
	ret = dcb->drv_info->drv_ops->get_ring_idx(dcb->drv_info, DPMAIF_DRB_RIDX, txq->id);
	if (unlikely(ret < 0)) {
		MTK_ERR(DCB_TO_MDEV(dcb),
			"Failed to read txq%u drb_rd_idx, ret=%d\n", txq->id, ret);
		mtk_dpmaif_common_err_handle(dcb, true);
		return 0;
	}

	new_hw_rd_idx = ret;

	if (old_sw_rd_idx <= new_hw_rd_idx)
		drb_cnt = new_hw_rd_idx - old_sw_rd_idx;
	else
		drb_cnt = txq->drb_cnt - old_sw_rd_idx + new_hw_rd_idx;

	txq->drb_rd_idx = new_hw_rd_idx;

	return drb_cnt;
}

static int mtk_dpmaif_tx_rel_internal(struct dpmaif_txq *txq,
				      unsigned int rel_cnt, unsigned int *real_rel_cnt)
{
	struct dpmaif_pd_drb *cur_drb = NULL, *drb_base = txq->drb_base;
	struct mtk_dpmaif_ctlb *dcb = txq->dcb;
	struct dpmaif_drb_skb *cur_drb_skb;
	struct dpmaif_tx_srv *tx_srv;
	struct sk_buff *skb_free;
	unsigned short cur_idx;
	unsigned char srv_id;
	unsigned int i;

	cur_idx = txq->drb_rel_rd_idx;
	for (i = 0; i < rel_cnt; i++) {
		cur_drb = drb_base + cur_idx;
		cur_drb_skb = txq->sw_drb_base + cur_idx;
		if (cur_drb_skb->is_msg == PD_DRB) {
			dma_unmap_single(DCB_TO_DEV(dcb), cur_drb_skb->data_dma_addr,
					 cur_drb_skb->data_len, DMA_TO_DEVICE);

			/* The last one drb entry of one tx packet, so, skb will be released. */
			if (cur_drb_skb->is_last) {
				skb_free = cur_drb_skb->skb;
				if (likely(skb_free)) {
					trace_mtk_tput_data_tx(txq->id, "5",
							       DATA_SKB_CB(skb_free)->tx.id);
					dev_consume_skb_any(skb_free);
				} else {
					MTK_ERR(DCB_TO_MDEV(dcb),
						"Failed to free skb,txq%u pkt%u,w=%u,r=%u,rel=%u,cnt=%u\n",
						txq->id, cur_idx, txq->drb_wr_idx,
						txq->drb_rd_idx, txq->drb_rel_rd_idx, rel_cnt);
					mtk_dpmaif_common_err_handle(dcb, false);
				}

				dcb->traffic_stats.dpmaif_tx.tx_hw_pkt[txq->id]++;
			}
		}

		cur_drb_skb->skb = NULL;
		cur_idx = mtk_dpmaif_ring_buf_get_next_idx(txq->drb_cnt, cur_idx);
		txq->drb_rel_rd_idx = cur_idx;
		atomic_inc(&txq->budget);
	}

	*real_rel_cnt = i;

	if (likely(cur_drb)) {
		if (unlikely(!cur_drb_skb->is_last)) {
			MTK_WARN(DCB_TO_MDEV(dcb), "txq%u done, last one c_bit != 0\n", txq->id);
			mtk_dpmaif_common_err_handle(dcb, true);
		}
	}

	if (atomic_read(&txq->budget) > txq->drb_cnt >> 3) {
		if (!(txq->attr & DPMAIFQ_ATTR_LOW_LATENCY)) {
			srv_id = dcb->tx_vqs[txq->id].srv_id;
			tx_srv = &dcb->tx_srvs[srv_id];
			clear_bit(txq->id, &tx_srv->txq_drb_lack_sta);
			wake_up(&tx_srv->wait);
		}

		mtk_wwan_notify(dcb->data_blk, DATA_EVT_TX_START, (u64)1 << txq->id);
	}

	return 0;
}

static int mtk_dpmaif_tx_rel(struct dpmaif_txq *txq)
{
	struct mtk_dpmaif_ctlb *dcb = txq->dcb;
	unsigned int real_rel_cnt = 0;
	int ret = 0, rel_cnt;

	rel_cnt = mtk_dpmaif_ring_buf_releasable(txq->drb_cnt, txq->drb_rel_rd_idx,
						 txq->drb_rd_idx);

	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_TX,
		"txq%u drb: w=%u,r=%u,rel=%u, rel_cnt=%u\n",
		txq->id, txq->drb_wr_idx, txq->drb_rd_idx, txq->drb_rel_rd_idx, rel_cnt);

	if (likely(rel_cnt > 0)) {
		/* Release tx data buffer. */
		ret = mtk_dpmaif_tx_rel_internal(txq, rel_cnt, &real_rel_cnt);
		dcb->traffic_stats.dpmaif_tx.tx_done_last_cnt[txq->id] = real_rel_cnt;
		mtk_stats_chk_and_proc(DCB_TO_MDEV(dcb), BIT(dcb->stats_tx_id));
		trace_mtk_tput_data_drb_rel(txq->id, real_rel_cnt, atomic_read(&txq->budget),
					    txq->drb_wr_idx, txq->drb_rd_idx, txq->drb_rel_rd_idx);
	}

	return ret;
}

static void mtk_dpmaif_tx_done(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct mtk_dpmaif_ctlb *dcb;
	struct dpmaif_txq *txq;

	txq = container_of(dwork, struct dpmaif_txq, tx_done_work);
	dcb = txq->dcb;

	dcb->traffic_stats.dpmaif_tx.tx_done_last_time[txq->id] = local_clock();

	mtk_pm_runtime_get(DCB_TO_MDEV(dcb), MTK_USER_DATA, false);

	/* Recycle drb and release hardware tx done buffer around drb. */
	mtk_dpmaif_tx_rel(txq);

	/* try best to recycle drb */
	if (txq->drb_poll_enable && mtk_dpmaif_poll_tx_drb(txq) > 0) {
		mtk_dpmaif_drv_intr_complete(dcb->drv_info, DPMAIF_INTR_UL_DONE,
					     txq->id, DPMAIF_CLEAR_INTR);
		queue_delayed_work(dcb->tx_done_wq, &txq->tx_done_work, msecs_to_jiffies(0));
	} else {
		mtk_dpmaif_drv_intr_complete(dcb->drv_info, DPMAIF_INTR_UL_DONE,
					     txq->id, DPMAIF_UNMASK_INTR);
	}

	mtk_pm_runtime_put(DCB_TO_MDEV(dcb), MTK_USER_DATA, false);
}

static int mtk_dpmaif_txq_init(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_txq *txq)
{
	struct dpmaif_txq_cfg *txq_cfg = &dcb->drv_info->cfg->tx_cfg.txqs[txq->id];
	int ret;

	spin_lock_init(&txq->lock);
	txq->drb_cnt = txq_cfg->drb_cnt;
	txq->db_delay_ns = DPMAIF_MS_TO_NS(txq_cfg->doorbell_delay);
	txq->burst_submit_cnt = txq_cfg->burst_pkts;
	txq->intr_coalesce_frame = dcb->intr_coalesce.tx_coalesced_frames;
	txq->attr = txq_cfg->attr;
	atomic_set(&txq->budget, txq->drb_cnt);

	/* Allocate DRB memory for HW and SW. */
	txq->drb_base = dma_alloc_coherent(DCB_TO_DEV(dcb), txq->drb_cnt * sizeof(*txq->drb_base),
					   &txq->drb_dma_addr, GFP_KERNEL);
	if (!txq->drb_base) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate txq%u drb resource\n", txq->id);
		return -ENOMEM;
	}

	/* Allocate buffer for SW to record the skb information. */
	txq->sw_drb_base = devm_kcalloc(DCB_TO_DEV(dcb), txq->drb_cnt, sizeof(*txq->sw_drb_base),
					GFP_KERNEL);
	if (!txq->sw_drb_base) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Faile to allocate txq%u sw_drb buffer\n", txq->id);
		ret = -ENOMEM;
		goto free_drb;
	}

	MTK_INFO(DCB_TO_MDEV(dcb),
		 "txq%u: drb_base=0x%llx, drb_dma_addr=0x%llx, drb_cnt=%u, skb_ptr=0x%llx\n",
		txq->id, (u64)txq->drb_base, txq->drb_dma_addr, txq->drb_cnt,
		(u64)txq->sw_drb_base);

	/* It belongs to dcb->tx_done_wq. */
	INIT_DELAYED_WORK(&txq->tx_done_work, mtk_dpmaif_tx_done);

	return 0;

free_drb:
	dma_free_coherent(DCB_TO_DEV(dcb), txq->drb_cnt * sizeof(*txq->drb_base),
			  txq->drb_base, txq->drb_dma_addr);

	return ret;
}

static void mtk_dpmaif_txq_exit(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_txq *txq)
{
	struct dpmaif_drb_skb *drb_skb;
	int i;

	dma_free_coherent(DCB_TO_DEV(dcb), txq->drb_cnt * sizeof(*txq->drb_base),
			  txq->drb_base, txq->drb_dma_addr);

	for (i = 0; i < txq->drb_cnt; i++) {
		drb_skb = txq->sw_drb_base + i;
		if (!drb_skb->skb)
			continue;

		/* Verify msg drb or payload drb, and only payload drb need to unmap dma. */
		if (drb_skb->data_dma_addr)
			dma_unmap_single(DCB_TO_DEV(dcb),
					 drb_skb->data_dma_addr,
					 drb_skb->data_len, DMA_TO_DEVICE);
		if (drb_skb->is_last)
			dev_kfree_skb_any(drb_skb->skb);
	}

	devm_kfree(DCB_TO_DEV(dcb), txq->sw_drb_base);
}

static int mtk_dpmaif_sw_wait_txq_stop(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_txq *txq)
{
	/* Wait tx done work done. */
	flush_delayed_work(&txq->tx_done_work);

	MTK_INFO(DCB_TO_MDEV(dcb),
		 "txq%u, drb:r=%u,w=%u,rel=%u; to_submit_cnt=%d; budget=%d\n",
		 txq->id, txq->drb_rd_idx, txq->drb_wr_idx, txq->drb_rel_rd_idx,
		 atomic_read(&txq->to_submit_cnt), atomic_read(&txq->budget));

	return 0;
}

static void mtk_dpmaif_sw_wait_tx_stop(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned char txq_cnt = dcb->drv_info->cfg->tx_cfg.txq_cnt;
	int i;

	/* Wait all tx handle complete */
	for (i = 0; i < txq_cnt; i++)
		mtk_dpmaif_sw_wait_txq_stop(dcb, &dcb->txqs[i]);
}

static void mtk_dpmaif_sw_reset_txq(struct dpmaif_txq *txq)
{
	struct dpmaif_drb_skb *drb_skb;
	int i;

	/* Drop all tx buffer around drb. */
	for (i = 0; i < txq->drb_cnt; i++) {
		drb_skb = txq->sw_drb_base + i;
		if (!drb_skb->skb)
			continue;

		if (drb_skb->data_dma_addr)
			dma_unmap_single(DCB_TO_DEV(txq->dcb), drb_skb->data_dma_addr,
					 drb_skb->data_len, DMA_TO_DEVICE);
		if (drb_skb->is_last) {
			dev_kfree_skb_any(drb_skb->skb);
			drb_skb->skb = NULL;
		}
	}

	/* Reset all txq resource. */
	memset(txq->drb_base, 0x00, (txq->drb_cnt * sizeof(*txq->drb_base)));
	memset(txq->sw_drb_base, 0x00, (txq->drb_cnt * sizeof(*txq->sw_drb_base)));

	atomic_set(&txq->budget, txq->drb_cnt);
	atomic_set(&txq->to_submit_cnt, 0);
	txq->drb_rd_idx = 0;
	txq->drb_wr_idx = 0;
	txq->drb_rel_rd_idx = 0;
}

static void mtk_dpmaif_tx_res_reset(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned char txq_cnt = dcb->drv_info->cfg->tx_cfg.txq_cnt;
	struct dpmaif_txq *txq;
	int i;

	for (i = 0; i < txq_cnt; i++) {
		txq = &dcb->txqs[i];
		mtk_dpmaif_sw_reset_txq(txq);
	}
}

static int mtk_dpmaif_tx_res_init(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_txq *txq;
	int i, j;
	int ret;

	dcb->txqs = devm_kcalloc(DCB_TO_DEV(dcb), dcb->txq_cnt, sizeof(*txq), GFP_KERNEL);
	if (!dcb->txqs) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate txqs\n");
		return -ENOMEM;
	}

	for (i = 0; i < dcb->txq_cnt; i++) {
		txq = &dcb->txqs[i];
		txq->id = i;
		txq->dcb = dcb;
		ret = mtk_dpmaif_txq_init(dcb, txq);
		if (ret < 0) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to init txq%d resource\n", txq->id);
			goto exit_txq;
		}
	}

	dcb->tx_done_wq = alloc_workqueue("dpmaif_tx_done_wq_%s",
					  WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_HIGHPRI,
					  dcb->txq_cnt, DCB_TO_DEV_STR(dcb));
	if (!dcb->tx_done_wq) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate tx done workqueue\n");
		ret = -ENOMEM;
		goto exit_txq;
	}

	return 0;

exit_txq:
	for (j = i - 1; j >= 0; j--)
		mtk_dpmaif_txq_exit(dcb, &dcb->txqs[j]);

	devm_kfree(DCB_TO_DEV(dcb), dcb->txqs);

	return ret;
}

static void mtk_dpmaif_tx_res_exit(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_txq *txq;
	int i;

	for (i = 0; i < dcb->txq_cnt; i++) {
		txq = &dcb->txqs[i];
		flush_delayed_work(&txq->tx_done_work);
	}

	if (dcb->tx_done_wq) {
		flush_workqueue(dcb->tx_done_wq);
		destroy_workqueue(dcb->tx_done_wq);
	}

	for (i = 0; i < dcb->txq_cnt; i++)
		mtk_dpmaif_txq_exit(dcb, &dcb->txqs[i]);

	devm_kfree(DCB_TO_DEV(dcb), dcb->txqs);
}

static int mtk_dpmaif_doorbell_task_start(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_doorbell_ctlb *db_ctlb = &dcb->db_ctlb;

	db_ctlb->db_task = kthread_run(mtk_dpmaif_doorbell_thread, dcb, "dpmaif_doorbell_task_%s",
				       DCB_TO_DEV_STR(dcb));
	if (IS_ERR(db_ctlb->db_task)) {
		MTK_ERR(DCB_TO_MDEV(dcb),
			"Failed to create dpmaif doorbell thread, Errcode=%ld\n",
			PTR_ERR(db_ctlb->db_task));
		db_ctlb->db_task = NULL;
		return -EFAULT;
	}

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	if (likely(dcb->aff_cfg))
		mtk_dpmaif_set_task_affinity(dcb,
					     dcb->aff_cfg[DATA_DEFAULT_AFF_MODE].doorbell_thrd_aff,
					     dcb->db_ctlb.db_task);

	sched_set_fifo(dcb->db_ctlb.db_task);
#endif

	return 0;
}

static void mtk_dpmaif_doorbell_task_stop(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_doorbell_ctlb *db_ctlb = &dcb->db_ctlb;

	if (db_ctlb->db_task) {
		kthread_stop(db_ctlb->db_task);
		db_ctlb->db_task = NULL;
	}
	mtk_dpmaif_task_info_clear(&db_ctlb->task_ctlb);
}

static int mtk_dpmaif_doorbell_task_init(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_doorbell_ctlb *db_ctlb = &dcb->db_ctlb;

	db_ctlb->dcb = dcb;
	init_waitqueue_head(&db_ctlb->task_ctlb.wait);
	init_completion(&db_ctlb->task_ctlb.paused_comp);

	hrtimer_init(&db_ctlb->db_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	db_ctlb->db_timer.function = mtk_dpmaif_doorbell_timer_func;

	spin_lock_init(&db_ctlb->db_timer_lock);

	return 0;
}

static void mtk_dpmaif_doorbell_task_exit(struct mtk_dpmaif_ctlb *dcb)
{
	hrtimer_cancel(&dcb->db_ctlb.db_timer);
	mtk_dpmaif_doorbell_task_stop(dcb);
}

static int mtk_dpmaif_sw_res_init(struct mtk_dpmaif_ctlb *dcb)
{
	int ret;

	ret = mtk_dpmaif_bat_res_init(dcb);
	if (ret < 0)
		return ret;

	ret = mtk_dpmaif_rx_res_init(dcb);
	if (ret < 0)
		goto bat_res_exit;

	ret = mtk_dpmaif_tx_res_init(dcb);
	if (ret < 0)
		goto rx_res_exit;

	ret = mtk_dpmaif_doorbell_task_init(dcb);
	if (ret < 0)
		goto tx_res_exit;

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	if (dcb->dpmaif_rx_legacy) {
		ret = mtk_dpmaif_skb_fifo_init(dcb,
					       dcb->bat_infos[0].normal_bat_ring.buf_size, 8192);
		if (ret < 0)
			goto tx_res_exit;
	}
#endif

	return 0;

tx_res_exit:
	mtk_dpmaif_tx_res_exit(dcb);

rx_res_exit:
	mtk_dpmaif_rx_res_exit(dcb);

bat_res_exit:
	mtk_dpmaif_bat_res_exit(dcb);

	return ret;
}

static void mtk_dpmaif_sw_res_exit(struct mtk_dpmaif_ctlb *dcb)
{
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	if (dcb->dpmaif_rx_legacy)
		mtk_dpmaif_skb_fifo_exit(dcb);
#endif
	mtk_dpmaif_doorbell_task_exit(dcb);
	mtk_dpmaif_tx_res_exit(dcb);
	mtk_dpmaif_rx_res_exit(dcb);
	mtk_dpmaif_bat_res_exit(dcb);
}

static bool mtk_dpmaif_all_vqs_empty_or_busy(struct dpmaif_tx_srv *tx_srv)
{
	bool is_empty_or_busy = true;
	struct dpmaif_vq *vq;
	int i;

	for (i = 0; i < tx_srv->vq_cnt; i++) {
		vq = tx_srv->vq[i];
		if (!skb_queue_empty(&vq->list) && !test_bit(vq->q_id, &tx_srv->txq_drb_lack_sta)) {
			is_empty_or_busy = false;
			break;
		}
	}

	return is_empty_or_busy;
}

static void mtk_dpmaif_record_drb_skb(struct mtk_dpmaif_ctlb *dcb, unsigned char q_id,
				      unsigned short cur_idx, struct sk_buff *skb,
				      unsigned short is_msg, unsigned short is_frag,
				      unsigned short is_last, dma_addr_t data_dma_addr,
				      unsigned int data_len)
{
	struct dpmaif_drb_skb *drb_skb = dcb->txqs[q_id].sw_drb_base + cur_idx;

	drb_skb->skb = skb;
	drb_skb->data_dma_addr = data_dma_addr;
	drb_skb->data_len = data_len;
	drb_skb->drb_idx = cur_idx;
	drb_skb->is_msg = is_msg;
	drb_skb->is_frag = is_frag;
	drb_skb->is_last = is_last;
}

static void mtk_dpmaif_skb_dump(struct mtk_dpmaif_ctlb *dcb, struct sk_buff *skb,
				enum mtk_memlog_region_id reg_id, int protocol)
{
	struct net_device *dev = skb->dev;
	struct sock *sk = skb->sk;

	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, reg_id,
		"csum_level=%u, csum=0x%x, ip_summed=%u, cc_sw=%u csum_valid=%u\n",
		skb->csum_level, skb->csum, skb->ip_summed, skb->csum_complete_sw,
		skb->csum_valid);
	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, reg_id,
		"hash=0x%x, sw_hash=%u, l4_hash=%u\n",
		skb->hash, skb->sw_hash, skb->l4_hash);

	if (sk)
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, reg_id,
			"sk_family=%hu, type=%u, proto=%u\n",
			sk->sk_family, sk->sk_type, sk->sk_protocol);
	else
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, reg_id,
			"proto=%u\n", protocol);

	if (dev)
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, reg_id,
			"dev_name=%s, features=%pNF\n", dev->name, &dev->features);
}

static int mtk_dpmaif_tx_fill_drb(struct mtk_dpmaif_ctlb *dcb,
				  unsigned char q_id, struct sk_buff *skb)
{
	unsigned short cur_idx, cur_backup_idx, is_frag, is_last;
	unsigned int send_drb_cnt, wt_cnt, payload_cnt;
	struct dpmaif_txq *txq = &dcb->txqs[q_id];
	struct dpmaif_drb_skb *cur_drb_skb;
	struct dpmaif_msg_drb *msg_drb;
	struct dpmaif_tx_info tx_info;
	struct dpmaif_pd_drb *pd_drb;
	struct skb_shared_info *info;
	dma_addr_t data_dma_addr;
	unsigned int data_len;
	skb_frag_t *frag;
	void *data_addr;
	u32 features;
	int i, ret;

#ifdef CONFIG_DEBUG_FS
	if (test_bit(DPMAIF_DUMP_TX_PKT, &dcb->dump_flag))
		mtk_dpmaif_skb_dump(dcb, skb, MTK_MEMLOG_RG_DATA_TX, IPPROTO_MAX);
#endif

	mtk_dpmaif_ul_stats_update(txq, skb->len);

	info = skb_shinfo(skb);
	if (unlikely(info->frag_list))
		MTK_WARN(DCB_TO_MDEV(dcb), "txq%d not support skb frag_list\n", q_id);

	send_drb_cnt = DATA_SKB_CB(skb)->tx.cnt;
	payload_cnt = send_drb_cnt - 1;
	cur_idx = txq->drb_wr_idx;
	cur_backup_idx = cur_idx;

	tx_info.msg_pkt_len = skb->len;
	tx_info.msg_channel_id = DATA_SKB_CB(skb)->tx.intf_id;

#ifdef CONFIG_DATA_TEST_MODE
	if (dcb->test_mode_cfg.md_tput_mode != DPMAIF_MD_INVALID_MODE) {
		tx_info.msg_count_l = 0;

		/* md mode */
		tx_info.msg_count_l |= (dcb->test_mode_cfg.md_tput_mode <<
									DPMAIF_MD_TPUT_MODE_OFFSET);

		/* Start/stop dl */
		tx_info.msg_count_l |= (dcb->test_mode_cfg.md_start_dl_tput <<
									DPMAIF_MD_DL_CTL_OFFSET);

		/* Packets count per ms */
		tx_info.msg_count_l |= (dcb->test_mode_cfg.md_pkt_number_per_ms <<
									DPMAIF_MD_PKT_OFFSET);
	}

	tx_info.msg_network_type = dcb->test_mode_cfg.md_dl_tput_test_time;
#else
	tx_info.msg_count_l = 0;
	tx_info.msg_network_type = DATA_SKB_CB(skb)->tx.network_type;
#endif

	dcb->drv_info->drv_ops->feature_cmd(dcb->drv_info, DATA_HW_FEATURES_GET, &features);
	if (likely(features & DATA_HW_F_TXCSUM))
		tx_info.msg_txcsum = 1;
	else
		tx_info.msg_txcsum = 0;

	/* Update tx drb, a msg drb first, then payload drb. */
	/* Update and record payload drb information. */
	msg_drb = (struct dpmaif_msg_drb *)dcb->txqs[txq->id].drb_base + cur_idx;
	dcb->drv_info->drv_ops->fill_tx_info(msg_drb, &tx_info, MSG_DRB);
	mtk_dpmaif_record_drb_skb(dcb, txq->id, cur_idx, skb, 1, 0, 0, 0, 0);
	MTK_DBG_DATA_TX_PKT_INFO(DCB_TO_MDEV(dcb), txq->id,
				 payload_cnt, cur_idx,
				 DATA_SKB_CB(skb)->tx.id,
				 *(unsigned int *)msg_drb,
				 *((unsigned int *)msg_drb + 1));

	/* Payload drb: skb->data + frags[]. */
	cur_idx = mtk_dpmaif_ring_buf_get_next_idx(txq->drb_cnt, cur_idx);
	for (wt_cnt = 0; wt_cnt < payload_cnt; wt_cnt++) {
		/* Get data_addr and data_len. */
		if (wt_cnt == 0) {
			data_len = skb_headlen(skb);
			data_addr = skb->data;
			is_frag = 0;
		} else {
			frag = info->frags + wt_cnt - 1;
			data_len = skb_frag_size(frag);
			data_addr = skb_frag_address(frag);
			is_frag = 1;
		}

		if (wt_cnt == payload_cnt - 1)
			is_last = 1;
		else
			is_last = 0;

		data_dma_addr = dma_map_single(DCB_TO_DEV(dcb),
					       data_addr, data_len, DMA_TO_DEVICE);
		ret = dma_mapping_error(DCB_TO_DEV(dcb), data_dma_addr);
		if (unlikely(ret)) {
			MTK_WARN(DCB_TO_MDEV(dcb), "dma mapping fail\n");
			txq->dma_map_errs++;
			ret = -DATA_DMA_MAP_ERR;
			goto unmap_dma;
		}
		tx_info.pd_is_last = is_last;
		tx_info.pd_data_len = data_len;
		tx_info.pd_data_dma_addr = data_dma_addr;

		/* Update and record payload drb information. */
		pd_drb = dcb->txqs[txq->id].drb_base + cur_idx;
		dcb->drv_info->drv_ops->fill_tx_info(pd_drb, &tx_info, PD_DRB);
		mtk_dpmaif_record_drb_skb(dcb, txq->id, cur_idx, skb, 0, is_frag, is_last,
					  data_dma_addr, data_len);

		cur_idx = mtk_dpmaif_ring_buf_get_next_idx(txq->drb_cnt, cur_idx);
	}

	/* Make sure all drb write operation is done, before triggering doorbell */
	dma_wmb();
	txq->drb_wr_idx = cur_idx;

	return 0;

unmap_dma:
	mtk_dpmaif_record_drb_skb(dcb, txq->id, cur_backup_idx, NULL, 0, 0, 0, 0, 0);
	cur_backup_idx = mtk_dpmaif_ring_buf_get_next_idx(txq->drb_cnt, cur_backup_idx);
	for (i = 0; i < wt_cnt; i++) {
		cur_drb_skb = txq->sw_drb_base + cur_backup_idx;

		dma_unmap_single(DCB_TO_DEV(dcb),
				 cur_drb_skb->data_dma_addr, cur_drb_skb->data_len,
				 DMA_TO_DEVICE);

		cur_backup_idx = mtk_dpmaif_ring_buf_get_next_idx(txq->drb_cnt, cur_backup_idx);
		mtk_dpmaif_record_drb_skb(dcb, txq->id, cur_backup_idx, NULL, 0, 0, 0, 0, 0);
	}

	return ret;
}

static int mtk_dpmaif_tx_update_ring_direct(struct mtk_dpmaif_ctlb *dcb,
					    struct sk_buff *skb, int q_id)
{
	unsigned char skb_drb_cnt = DATA_SKB_CB(skb)->tx.cnt;
	struct dpmaif_txq *txq = &dcb->txqs[q_id];
	u32 id = DATA_SKB_CB(skb)->tx.id;
	int drb_available_cnt;
	int ret;

	spin_lock_bh(&txq->lock);
	drb_available_cnt = mtk_dpmaif_ring_buf_writable(txq->drb_cnt,
							 txq->drb_rel_rd_idx, txq->drb_wr_idx);
	if (unlikely(drb_available_cnt < skb_drb_cnt)) {
		/* Notify to data port layer, data port should carry off the net device tx queue. */
		mtk_wwan_notify(dcb->data_blk, DATA_EVT_TX_STOP, (u64)1 << q_id);
		txq->dcb->traffic_stats.dpmaif_tx.tx_hw_full[q_id]++;
		MTK_WARN_RATELIMITED(DCB_TO_MDEV(dcb), "txq%u drb lack\n", q_id);
		trace_mtk_data_event_stats(DPMAIF_TX_HW_FULL, q_id,
					   txq->dcb->traffic_stats.dpmaif_tx.tx_hw_full[q_id]);
		ret = -EBUSY;
		goto out;
	}

	ret = mtk_dpmaif_tx_fill_drb(dcb, q_id, skb);
	if (unlikely(ret < 0)) {
		ret = -EBUSY;
		goto out;
	}

	dcb->traffic_stats.dpmaif_tx.tx_sw_pkt[q_id]++;

	atomic_sub(skb_drb_cnt, &txq->budget);
	atomic_add(skb_drb_cnt, &txq->drb_stats);
	atomic_add(skb_drb_cnt, &txq->to_submit_cnt);

out:
	spin_unlock_bh(&txq->lock);
	trace_mtk_tput_data_drb_fill(-1, q_id, 0, 0, drb_available_cnt, id);

	return ret;
}

static void mtk_dpmaif_tx_update_ring(struct mtk_dpmaif_ctlb *dcb, struct dpmaif_tx_srv *tx_srv,
				      struct dpmaif_vq *vq)
{
	struct dpmaif_txq *txq = &dcb->txqs[vq->q_id];
	unsigned long long tx_hw_full_cnt;
	bool in_tcp_slow_start = false;
	unsigned char q_id = vq->q_id;
	unsigned char skb_drb_cnt;
	int i, drb_available_cnt;
	struct sk_buff *skb;
	u32 id;

	drb_available_cnt = mtk_dpmaif_ring_buf_writable(txq->drb_cnt,
							 txq->drb_rel_rd_idx, txq->drb_wr_idx);

	for (i = 0; i < DPMAIF_SKB_TX_WEIGHT; i++) {
		skb = skb_dequeue(&vq->list);
		if (!skb)
			break;
		id = DATA_SKB_CB(skb)->tx.id;
		trace_mtk_tput_data_tx(txq->id, "3", id);

		skb_drb_cnt = DATA_SKB_CB(skb)->tx.cnt;
		if (drb_available_cnt < skb_drb_cnt) {
			skb_queue_head(&vq->list, skb);
			set_bit(q_id, &tx_srv->txq_drb_lack_sta);
			tx_hw_full_cnt = ++txq->dcb->traffic_stats.dpmaif_tx.tx_hw_full[q_id];
			MTK_WARN_RATELIMITED(DCB_TO_MDEV(dcb),
					     "txq%u drb lack,ava_cnt=%d,skb_drb_cnt=%d,rel_rd=%hu,wr=%hu\n",
				 q_id, drb_available_cnt, skb_drb_cnt, txq->drb_rel_rd_idx,
				 txq->drb_wr_idx);
			trace_mtk_data_event_stats(DPMAIF_TX_HW_FULL, q_id, tx_hw_full_cnt);
			break;
		}

		if (mtk_dpmaif_tx_fill_drb(dcb, q_id, skb) < 0) {
			skb_queue_head(&vq->list, skb);
			break;
		}

		/* if any packet in tcp slow start, the doorbell delay timer will be set to 0 */
		if (DATA_SKB_CB(skb)->tx.in_tcp_slow_start)
			in_tcp_slow_start = true;

		drb_available_cnt -= skb_drb_cnt;
		dcb->traffic_stats.dpmaif_tx.tx_sw_pkt[q_id]++;
		trace_mtk_tput_data_drb_fill(tx_srv->id, q_id, i, skb_queue_len(&vq->list),
					     drb_available_cnt, id);

		atomic_sub(skb_drb_cnt, &txq->budget);
		atomic_add(skb_drb_cnt, &txq->drb_stats);
		atomic_add(skb_drb_cnt, &txq->to_submit_cnt);
	}

	if (in_tcp_slow_start)
		txq->exit_tcp_ss_counter = doorbell_reset_count;
}

static struct dpmaif_vq *mtk_dpmaif_srv_select_vq(struct dpmaif_tx_srv *tx_srv)
{
	struct dpmaif_vq *vq;
	int i;

	/* Round robin select tx vqs. */
	for (i = 0; i < tx_srv->vq_cnt; i++) {
		tx_srv->cur_vq_id = tx_srv->cur_vq_id % tx_srv->vq_cnt;
		vq = tx_srv->vq[tx_srv->cur_vq_id];
		tx_srv->cur_vq_id++;
		if (!skb_queue_empty(&vq->list) && !test_bit(vq->q_id, &tx_srv->txq_drb_lack_sta))
			return vq;
	}

	return NULL;
}

static void mtk_dpmaif_tx(struct dpmaif_tx_srv *tx_srv)
{
	struct mtk_dpmaif_ctlb *dcb = tx_srv->dcb;
	struct dpmaif_vq *vq;

	while (!kthread_should_stop() && (dcb->dpmaif_state == DPMAIF_STATE_PWRON)) {
		if (likely(!dcb->err_event)) {
			vq = mtk_dpmaif_srv_select_vq(tx_srv);
			if (!vq)
				break;

			mtk_dpmaif_tx_update_ring(dcb, tx_srv, vq);
			mtk_dpmaif_book_tx_doorbell(dcb, &dcb->txqs[vq->q_id]);
		}

		cond_resched();
	}
}

static int mtk_dpmaif_tx_thread(void *arg)
{
	struct dpmaif_tx_srv *tx_srv = arg;
	struct mtk_dpmaif_ctlb *dcb;
	int ret;

	dcb = tx_srv->dcb;
	set_user_nice(current, tx_srv->nice);
	while (!kthread_should_stop()) {
		ret = wait_event_interruptible(tx_srv->wait,
					       (!mtk_dpmaif_all_vqs_empty_or_busy(tx_srv) &&
					       (dcb->dpmaif_state == DPMAIF_STATE_PWRON)) ||
					       kthread_should_stop());

		if (ret == -ERESTARTSYS)
			continue;

		/* Send packets of all tx virtual queues belong to the tx service. */
		mtk_dpmaif_tx(tx_srv);
	}

	return 0;
}

static int mtk_dpmaif_tx_srvs_start(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned char srvs_cnt = dcb->drv_info->cfg->tx_srvs_cfg.tx_srv_cnt;
	struct dpmaif_tx_srv *tx_srv;
	int i, j, ret;

	for (i = 0; i < srvs_cnt; i++) {
		tx_srv = &dcb->tx_srvs[i];
		tx_srv->cur_vq_id = 0;
		tx_srv->txq_drb_lack_sta = 0;
		if (tx_srv->srv) {
			MTK_WARN(DCB_TO_MDEV(dcb), "The tx_srv:%d existed", i);
			continue;
		}
		tx_srv->srv = kthread_run(mtk_dpmaif_tx_thread,
					  tx_srv, "dpmaif_tx_srv%u_%s",
					  tx_srv->id, DCB_TO_DEV_STR(dcb));
		if (IS_ERR(tx_srv->srv)) {
			MTK_ERR(DCB_TO_MDEV(dcb),
				"Failed to alloc dpmaif tx_srv%u\n", tx_srv->id);
			ret = PTR_ERR(tx_srv->srv);
			tx_srv->srv = NULL;
			goto free_tx_srvs;
		}
	}

	return 0;

free_tx_srvs:
	for (j = i - 1; j >= 0; j--) {
		tx_srv = &dcb->tx_srvs[j];
		kthread_stop(tx_srv->srv);
		tx_srv->srv = NULL;
	}

	return ret;
}

static void mtk_dpmaif_tx_srvs_stop(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned char srvs_cnt = dcb->drv_info->cfg->tx_srvs_cfg.tx_srv_cnt;
	struct dpmaif_tx_srv *tx_srv;
	int i;

	for (i = 0; i < srvs_cnt; i++) {
		tx_srv = &dcb->tx_srvs[i];
		if (tx_srv->srv) {
			kthread_stop(tx_srv->srv);
			tx_srv->srv = NULL;
		}
	}
}

static int mtk_dpmaif_tx_srvs_init(struct mtk_dpmaif_ctlb *dcb)
{
	const struct dpmaif_tx_srvs_cfg *tx_srvs_cfg = &dcb->drv_info->cfg->tx_srvs_cfg;
	struct dpmaif_tx_srv *tx_srv;
	struct dpmaif_vq *tx_vq;
	int i, j, vq_id;
	int ret;

	/* Initialize all data packet tx vitrual queue. */
	dcb->tx_vqs = devm_kcalloc(DCB_TO_DEV(dcb), tx_srvs_cfg->tx_vq_cnt, sizeof(*dcb->tx_vqs),
				   GFP_KERNEL);
	if (!dcb->tx_vqs) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to alloc tx_vqs\n");
		return -ENOMEM;
	}

	for (i = 0; i < tx_srvs_cfg->tx_vq_cnt; i++) {
		tx_vq = &dcb->tx_vqs[i];
		tx_vq->q_id = i;
		tx_vq->max_len = DEFAULT_TX_QUEUE_LEN;
		skb_queue_head_init(&tx_vq->list);
	}

	/* Initialize all data packet tx services. */
	dcb->tx_srvs = devm_kcalloc(DCB_TO_DEV(dcb), tx_srvs_cfg->tx_srv_cnt, sizeof(*dcb->tx_srvs),
				    GFP_KERNEL);
	if (!dcb->tx_srvs) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to alloc tx_srvs\n");
		ret = -ENOMEM;
		goto free_tx_vqs;
	}

	for (i = 0; i < tx_srvs_cfg->tx_srv_cnt; i++) {
		tx_srv = &dcb->tx_srvs[i];
		tx_srv->dcb = dcb;
		tx_srv->id = i;
		tx_srv->nice = tx_srvs_cfg->tx_srvs[i].nice;
		tx_srv->cur_vq_id = 0;
		tx_srv->txq_drb_lack_sta = 0;
		init_waitqueue_head(&tx_srv->wait);

		/* Set virtual queues and tx service mapping. */
		tx_srv->vq_cnt = tx_srvs_cfg->tx_srvs[i].vq_cnt;
		for (j = 0; j < tx_srv->vq_cnt; j++) {
			vq_id = tx_srvs_cfg->tx_srvs[i].vqs[j];
			tx_srv->vq[j] = &dcb->tx_vqs[vq_id];
			tx_srv->vq[j]->srv_id = tx_srv->id;
		}
	}

	return 0;

free_tx_vqs:
	devm_kfree(DCB_TO_DEV(dcb), dcb->tx_vqs);

	return ret;
}

static void mtk_dpmaif_tx_vqs_reset(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned char vqs_cnt = dcb->drv_info->cfg->tx_srvs_cfg.tx_vq_cnt;
	struct dpmaif_vq *tx_vq;
	int i;

	/* Drop all packet in tx virtual queues. */
	for (i = 0; i < vqs_cnt; i++) {
		tx_vq = &dcb->tx_vqs[i];
		if (tx_vq)
			skb_queue_purge(&tx_vq->list);
	}
}

static void mtk_dpmaif_tx_srvs_exit(struct mtk_dpmaif_ctlb *dcb)
{
	mtk_dpmaif_tx_srvs_stop(dcb);
	devm_kfree(DCB_TO_DEV(dcb), dcb->tx_srvs);
	mtk_dpmaif_tx_vqs_reset(dcb);
	devm_kfree(DCB_TO_DEV(dcb), dcb->tx_vqs);
}

static void mtk_dpmaif_trans_enable(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_drv_info *drv_info = dcb->drv_info;
	int i;

	for (i = 0; i < drv_info->cfg->rx_cfg.bat_ring_num; i++)
		mtk_dpmaif_task_resume(&dcb->bat_infos[i].task_ctlb, dcb, true);

	if (dcb->db_ctlb.data_no_intf) {
		mtk_dpmaif_task_resume(&dcb->db_ctlb.task_ctlb, dcb, mtk_dpmaif_has_doorbell(dcb));
		dcb->db_ctlb.data_no_intf = false;
	}

	mod_timer(&dcb->ring_rel_ctrl_timer,
		  jiffies + msecs_to_jiffies(BIT(traffic_stats_shift)));

	mtk_dpmaif_sw_start_rx(dcb);
	mtk_dpmaif_enable_irq(dcb);

	if (dpmaif_hw_is_accessible(dcb)) {
		if (drv_info->drv_ops->start_queue(drv_info, DPMAIF_RX) < 0) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to start dpmaif hw rx\n");
			mtk_dpmaif_common_err_handle(dcb, true);
			return;
		}

		if (drv_info->drv_ops->start_queue(drv_info, DPMAIF_TX) < 0) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to start dpmaif hw tx\n");
			mtk_dpmaif_common_err_handle(dcb, true);
			return;
		}
	}
}

static void mtk_dpmaif_trans_disable(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_drv_info *drv_info = dcb->drv_info;
	bool io_err = false;

	if (dcb->db_ctlb.data_no_intf)
		mtk_dpmaif_task_pause(&dcb->db_ctlb.task_ctlb, dcb);

	/* Stop dpmaif hw tx and rx. */
	if (dpmaif_hw_is_accessible(dcb)) {
		if (drv_info->drv_ops->stop_queue(drv_info, DPMAIF_TX) < 0) {
			io_err = true;
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to stop dpmaif hw tx\n");
		}

		if (drv_info->drv_ops->stop_queue(drv_info, DPMAIF_RX) < 0) {
			io_err = true;
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to stop dpmaif hw rx\n");
		}

		if (io_err)
			mtk_dpmaif_common_err_handle(dcb, true);
	}

	/* Disable all dpmaif L1 interrupt. */
	mtk_dpmaif_disable_irq(dcb);

	/* Wait tx done work complete */
	mtk_dpmaif_sw_wait_tx_stop(dcb);

	/* Stop and wait rx handle done  */
	mtk_dpmaif_sw_stop_rx(dcb);

	/* Wait bat reload task done */
	mtk_dpmaif_flush_bat_reload(dcb);

	del_timer_sync(&dcb->ring_rel_ctrl_timer);

	hrtimer_cancel(&dcb->db_ctlb.db_timer);
}

static void mtk_dpmaif_trans_ctl(struct mtk_dpmaif_ctlb *dcb, bool enable)
{
	MTK_INFO(DCB_TO_MDEV(dcb),
		 "%ps: pm_ready=%d, dpmaif_state=%d, port_ready=%d, trans_enabled=%d, enable=%d\n",
		__builtin_return_address(0), dcb->dpmaif_pm_ready,
		dcb->dpmaif_state, dcb->dpmaif_user_ready, dcb->trans_enabled, enable);

	if (enable) {
		if (!dcb->trans_enabled) {
			if (dcb->dpmaif_pm_ready &&
			    dcb->dpmaif_state == DPMAIF_STATE_PWRON &&
			    dcb->dpmaif_user_ready) {
				mtk_dpmaif_trans_enable(dcb);
				dcb->trans_enabled = true;
			}
		}
	} else {
		if (dcb->trans_enabled) {
			if (!dcb->dpmaif_pm_ready ||
			    !(dcb->dpmaif_state == DPMAIF_STATE_PWRON) ||
			    !dcb->dpmaif_user_ready) {
				dcb->trans_enabled = false;
				mtk_dpmaif_trans_disable(dcb);
			}
		}
	}
}

static void mtk_dpmaif_cmd_trans_ctl(struct mtk_dpmaif_ctlb *dcb, void *data)
{
	struct mtk_data_trans_ctl *trans_ctl = data;

	dcb->dpmaif_user_ready = trans_ctl->enable;

	/* Try best to drop all tx vq packets when disable trans */
	if (!trans_ctl->enable)
		mtk_dpmaif_tx_vqs_reset(dcb);

	mutex_lock(&dcb->trans_ctl_lock);
	if (dcb->trans_enabled)
		dcb->db_ctlb.data_no_intf = true;
	mtk_dpmaif_trans_ctl(dcb, trans_ctl->enable);
	mutex_unlock(&dcb->trans_ctl_lock);
}

static void mtk_dpmaif_cmd_intr_coalesce_write(struct mtk_dpmaif_ctlb *dcb,
					       unsigned int qid, enum dpmaif_drv_dir dir)
{
	struct dpmaif_drv_intr drv_intr;

	if (dir == DPMAIF_TX) {
		drv_intr.pkt_threshold = dcb->txqs[qid].intr_coalesce_frame;
		drv_intr.time_threshold = dcb->intr_coalesce.tx_coalesce_usecs;
	} else {
		drv_intr.pkt_threshold = dcb->rxqs[qid].intr_coalesce_frame;
		drv_intr.time_threshold = dcb->intr_coalesce.rx_coalesce_usecs;
	}

	drv_intr.dir = dir;
	drv_intr.q_mask = BIT(qid);

	drv_intr.mode = 0;
	if (drv_intr.pkt_threshold)
		drv_intr.mode |= DPMAIF_INTR_COALESCE_EN_PKT;
	if (drv_intr.time_threshold)
		drv_intr.mode |= DPMAIF_INTR_COALESCE_EN_TIME;
	MTK_DBG(DCB_TO_MDEV(dcb),
		MTK_DBG_DPMF, MTK_MEMLOG_RG_COMMON, "mode=%u, ptk_th=%u, time_th=%u\n",
		drv_intr.mode, drv_intr.pkt_threshold, drv_intr.time_threshold);
	dcb->drv_info->drv_ops->feature_cmd(dcb->drv_info, DATA_HW_INTR_COALESCE_SET, &drv_intr);
}

static int mtk_dpmaif_cmd_intr_coalesce_set(struct mtk_dpmaif_ctlb *dcb, void *data)
{
	struct mtk_data_intr_coalesce *dpmaif_intr_cfg = &dcb->intr_coalesce;
	struct mtk_data_intr_coalesce *user_intr_cfg = data;
	int i;

	memcpy(dpmaif_intr_cfg, data, sizeof(*dpmaif_intr_cfg));

	for (i = 0; i < dcb->rxq_cnt; i++) {
		dcb->rxqs[i].intr_coalesce_frame = user_intr_cfg->rx_coalesced_frames;
		mtk_dpmaif_cmd_intr_coalesce_write(dcb, i, DPMAIF_RX);
	}

	for (i = 0; i < dcb->txq_cnt; i++) {
		dcb->txqs[i].intr_coalesce_frame = user_intr_cfg->tx_coalesced_frames;
		mtk_dpmaif_cmd_intr_coalesce_write(dcb, i, DPMAIF_TX);
	}

	return 0;
}

static int mtk_dpmaif_cmd_intr_coalesce_get(struct mtk_dpmaif_ctlb *dcb, void *data)
{
	struct mtk_data_intr_coalesce *dpmaif_intr_cfg = &dcb->intr_coalesce;

	memcpy(data, dpmaif_intr_cfg, sizeof(*dpmaif_intr_cfg));

	return 0;
}

static int mtk_dpmaif_cmd_rxfh_set(struct mtk_dpmaif_ctlb *dcb, void *data)
{
	struct dpmaif_drv_info *drv_info = dcb->drv_info;
	struct mtk_data_rxfh *indir_rxfh = data;
	int ret;

	if (indir_rxfh->key) {
		ret = drv_info->drv_ops->feature_cmd(drv_info, DATA_HW_HASH_SET, indir_rxfh->key);
		if (ret < 0) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to set hash key\n");
			return ret;
		}
	}

	if (indir_rxfh->indir) {
		ret = drv_info->drv_ops->feature_cmd(drv_info, DATA_HW_INDIR_SET,
						     indir_rxfh->indir);
		if (ret < 0) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to set indirection table\n");
			return ret;
		}
	}

	return 0;
}

static int mtk_dpmaif_cmd_rxfh_get(struct mtk_dpmaif_ctlb *dcb, void *data)
{
	struct dpmaif_drv_info *drv_info = dcb->drv_info;
	struct mtk_data_rxfh *indir_rxfh = data;
	int ret;

	if (indir_rxfh->key) {
		ret = drv_info->drv_ops->feature_cmd(drv_info, DATA_HW_HASH_GET, indir_rxfh->key);
		if (ret < 0) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to get hash key\n");
			return ret;
		}
	}

	if (indir_rxfh->indir) {
		ret = drv_info->drv_ops->feature_cmd(drv_info, DATA_HW_INDIR_GET,
						     indir_rxfh->indir);
		if (ret < 0) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to get indirection table\n");
			return ret;
		}
	}

	return 0;
}

static inline void mtk_dpmaif_cmd_rxq_num_get(struct mtk_dpmaif_ctlb *dcb, void *data)
{
	*(unsigned int *)data = dcb->drv_info->cfg->rx_cfg.indir_rxq_cnt;
}

static inline void mtk_dpmaif_cmd_channels_get(struct mtk_dpmaif_ctlb *dcb, void *data)
{
	struct mtk_data_channels *channels = (struct mtk_data_channels *)data;

	channels->max_rx = dcb->rxq_cnt;
	channels->rx_count = dcb->drv_info->cfg->rx_cfg.indir_rxq_cnt;
}

#define DATA_TX_STATS_LEN	ARRAY_SIZE(dpmaif_tx_stats)
#define DATA_RX_STATS_LEN	ARRAY_SIZE(dpmaif_rx_stats)
#define DATA_IRQ_STATS_LEN	ARRAY_SIZE(dpmaif_irq_stats)
#define DATA_BAT_STATS_LEN	ARRAY_SIZE(dpmaif_bat_stats)

static unsigned int mtk_dpmaif_describe_stats(struct mtk_dpmaif_ctlb *dcb, u8 *strings)
{
	unsigned int i, j, n_stats = 0;

	for (i = 0; i < dcb->txq_cnt; i++) {
		n_stats += DATA_TX_STATS_LEN;
		if (strings) {
			for (j = 0; j < DATA_TX_STATS_LEN; j++) {
				snprintf(strings, DATA_TRANS_STRING_LEN,
					 "txq%u.%s", i, dpmaif_tx_stats[j]);
				strings += DATA_TRANS_STRING_LEN;
			}
		}
	}

	for (i = 0; i < dcb->rxq_cnt; i++) {
		n_stats += DATA_RX_STATS_LEN;
		if (strings) {
			for (j = 0; j < DATA_RX_STATS_LEN; j++) {
				snprintf(strings, DATA_TRANS_STRING_LEN,
					 "rxq%u.%s", i, dpmaif_rx_stats[j]);
				strings += DATA_TRANS_STRING_LEN;
			}
		}
	}

	for (i = 0; i < dcb->drv_info->cfg->intr_cfg.irq_cnt; i++) {
		n_stats += DATA_IRQ_STATS_LEN;
		if (strings) {
			for (j = 0; j < DATA_IRQ_STATS_LEN; j++) {
				snprintf(strings, DATA_TRANS_STRING_LEN,
					 "irq%u.%s", i, dpmaif_irq_stats[j]);
				strings += DATA_TRANS_STRING_LEN;
			}
		}
	}

	for (i = 0; i < dcb->drv_info->cfg->rx_cfg.bat_ring_num; i++) {
		n_stats += DATA_BAT_STATS_LEN;
		if (strings) {
			for (j = 0; j < DATA_BAT_STATS_LEN; j++) {
				snprintf(strings, DATA_TRANS_STRING_LEN,
					 "bat%u.%s", i, dpmaif_bat_stats[j]);
				strings += DATA_TRANS_STRING_LEN;
			}
		}
	}

	return n_stats;
}

static void mtk_dpmaif_read_stats(struct mtk_dpmaif_ctlb *dcb, u64 *data)
{
	unsigned int i, j = 0;

	for (i = 0; i < dcb->txq_cnt; i++) {
		data[j++] = dcb->traffic_stats.dpmaif_tx.tx_byte[i];
		data[j++] = dcb->traffic_stats.dpmaif_tx.tx_sw_pkt[i];
		data[j++] = dcb->traffic_stats.dpmaif_tx.tx_hw_pkt[i];
		data[j++] = dcb->traffic_stats.dpmaif_tx.tx_sw_full[i];
		data[j++] = dcb->traffic_stats.dpmaif_tx.tx_hw_full[i];
		data[j++] = dcb->traffic_stats.dpmaif_tx.tx_done_last_time[i];
		data[j++] = dcb->traffic_stats.dpmaif_tx.tx_done_last_cnt[i];
		data[j++] = dcb->traffic_stats.dpmaif_tx.irq_tx_evt[i].ul_done;
		data[j++] = dcb->traffic_stats.dpmaif_tx.irq_tx_evt[i].ul_drb_empty;
	}

	for (i = 0; i < dcb->rxq_cnt; i++) {
		data[j++] = dcb->traffic_stats.dpmaif_rx.rx_byte[i];
		data[j++] = dcb->traffic_stats.dpmaif_rx.rx_pkt[i];
		data[j++] = dcb->traffic_stats.dpmaif_rx.rx_lro_tcp_pkt[i];
		data[j++] = dcb->traffic_stats.dpmaif_rx.rx_lro_udp_pkt[i];
		data[j++] = dcb->traffic_stats.dpmaif_rx.rx_errors[i];
		data[j++] = dcb->traffic_stats.dpmaif_rx.rx_dropped[i];
		data[j++] = dcb->traffic_stats.dpmaif_rx.rx_fifo_dropped[i];
		data[j++] = dcb->traffic_stats.dpmaif_rx.rx_hw_ind_dropped[i];
		data[j++] = dcb->traffic_stats.dpmaif_rx.rx_done_last_time[i];
		data[j++] = dcb->traffic_stats.dpmaif_rx.rx_done_last_cnt[i];
		data[j++] = dcb->traffic_stats.dpmaif_rx.irq_rx_evt[i].dl_done;
		data[j++] = dcb->traffic_stats.dpmaif_rx.irq_rx_evt[i].pit_len_err;
	}

	for (i = 0; i < dcb->drv_info->cfg->intr_cfg.irq_cnt; i++) {
		data[j++] = dcb->traffic_stats.dpmaif_irq.irq_total_cnt[i];
		data[j++] = dcb->traffic_stats.dpmaif_irq.irq_last_time[i];
	}

	for (i = 0; i < dcb->drv_info->cfg->rx_cfg.bat_ring_num; i++) {
		data[j++] = dcb->traffic_stats.dpmaif_rx.irq_bat_evt[i].dl_bat_cnt_len_err;
		data[j++] = dcb->traffic_stats.dpmaif_rx.irq_bat_evt[i].dl_frag_cnt_len_err;
	}
}

static void mtk_dpmaif_cmd_string_cnt_get(struct mtk_dpmaif_ctlb *dcb, void *data)
{
	*(unsigned int *)data = mtk_dpmaif_describe_stats(dcb, NULL);
}

static struct dpmaif_drv_ops *mtk_dpmaif_get_drv_ops(u32 hw_ver)
{
	struct dpmaif_drv_ops_desc *p_drv_ops;
	unsigned char i;

	for (i = 0; (p_drv_ops = &dpmaif_drv_ops_tbl[i]) && p_drv_ops && p_drv_ops->drv_ops; i++)
		if (p_drv_ops->hw_ver == hw_ver)
			return p_drv_ops->drv_ops;

	return NULL;
}

static int mtk_dpmaif_enable_frag_dynamic_reload(struct mtk_dpmaif_ctlb *dcb, int mtu)
{
	int bat_ring_num = dcb->drv_info->cfg->rx_cfg.bat_ring_num;
	struct dpmaif_bat_info *bat_infos = dcb->bat_infos;
	int bat_reload_cnt, to_reload_cnt, i;
	struct dpmaif_bat_ring *bat_ring;

	for (i = 0; i < bat_ring_num; i++) {
		if (mtu < bat_infos[i].normal_bat_ring.buf_size)
			continue;

		if (unlikely(!bat_infos[i].frag_bat_enabled)) {
			MTK_WARN(DCB_TO_MDEV(dcb), "Unsupported MTU size, mtu=%d\n", mtu);
			return -EINVAL;
		}

		bat_ring = &bat_infos[i].frag_bat_ring;
		if (bat_ring->dynamic_reload)
			continue;

		/* align with the default reload count of normal BAT */
		bat_reload_cnt = dcb->drv_info->cfg->rx_cfg.bats[i].reload_cnt;
		if (bat_reload_cnt >= bat_ring->bat_cnt)
			bat_reload_cnt = bat_ring->bat_cnt - 1;
		to_reload_cnt = bat_reload_cnt - bat_ring->max_reload_cnt;
		bat_ring->max_reload_cnt = bat_reload_cnt;

		atomic_add(to_reload_cnt, &bat_ring->to_reload_cnt);
		mtk_dpmaif_task_wakeup(&bat_infos[i].task_ctlb);
		bat_ring->dynamic_reload = true;

		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_MEMLOG_RG_COMMON,
			"dpmaif frag_bat%d: dynamic_reload=%d, mtu=%d\n",
			i, bat_ring->dynamic_reload, mtu);
	}

	return 0;
}

static int mtk_dpmaif_drv_res_init(struct mtk_dpmaif_ctlb *dcb)
{
	dcb->drv_info = devm_kzalloc(DCB_TO_DEV(dcb), sizeof(*dcb->drv_info), GFP_KERNEL);
	if (!dcb->drv_info) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate dpmaif_drv info\n");
		return -ENOMEM;
	}

	dcb->drv_info->mdev = DCB_TO_MDEV(dcb);
	dcb->drv_info->drv_ops = mtk_dpmaif_get_drv_ops(DPMAIF_GET_HW_VER(dcb));
	if (!dcb->drv_info->drv_ops) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Unsupported mdev, hw_ver=0x%x\n",
			DPMAIF_GET_HW_VER(dcb));
		devm_kfree(DCB_TO_DEV(dcb), dcb->drv_info);
		return -EFAULT;
	}

	dcb->drv_info->drv_ops->feature_cmd(dcb->drv_info, DATA_HW_CFG_GET, NULL);
	dcb->rxq_cnt = dcb->drv_info->cfg->rx_cfg.rxq_cnt;
	dcb->txq_cnt = dcb->drv_info->cfg->tx_cfg.txq_cnt;
	dcb->drv_info->drv_ops->feature_cmd(dcb->drv_info, DATA_HW_FEATURES_GET, &dcb->features);

	return 0;
}

static void mtk_dpmaif_drv_res_exit(struct mtk_dpmaif_ctlb *dcb)
{
	devm_kfree(DCB_TO_DEV(dcb), dcb->drv_info);
}

static void mtk_dpmaif_irq_tx_done(struct mtk_dpmaif_ctlb *dcb, unsigned int q_mask)
{
	unsigned int ulq_done;
	int drb_rd_idx;
	int i;

	/* All txq done share one interrupt, and then,
	 * one interrupt will check all ulq done status and schedule corresponding bottom half.
	 */
	for (i = 0; i < dcb->drv_info->cfg->tx_cfg.txq_cnt; i++) {
		ulq_done = q_mask & BIT(i);
		if (!ulq_done)
			continue;

		drb_rd_idx = dcb->drv_info->drv_ops->get_ring_idx(dcb->drv_info,
								  DPMAIF_DRB_RIDX, i);
		if (unlikely(drb_rd_idx < 0)) {
			MTK_ERR(DCB_TO_MDEV(dcb),
				"Failed to read txq%u drb_rd_idx, ret=%d\n", i, drb_rd_idx);
			mtk_dpmaif_common_err_handle(dcb, true);
			break;
		}

		dcb->txqs[i].drb_rd_idx = drb_rd_idx;
		queue_delayed_work(dcb->tx_done_wq,
				   &dcb->txqs[i].tx_done_work,
				   msecs_to_jiffies(0));

		dcb->traffic_stats.dpmaif_tx.irq_tx_evt[i].ul_done++;
	}
}

static void mtk_dpmaif_irq_rx_done(struct mtk_dpmaif_ctlb *dcb, unsigned int q_id)
{
	struct dpmaif_rxq *rxq;
	int pit_widx;

	/* RSS: one dlq done belongs to one interrupt, and then,
	 * one interrupt will only check one dlq done status and schedule bottom half.
	 */

	dcb->traffic_stats.dpmaif_rx.rx_done_last_time[q_id] = local_clock();
	rxq = &dcb->rxqs[q_id];
	__pm_stay_awake(rxq->ws);

	trace_mtk_tput_data_rx(rxq->id, "1");

	pit_widx = dcb->drv_info->drv_ops->get_ring_idx(dcb->drv_info, DPMAIF_PIT_WIDX, q_id);
	if (unlikely(pit_widx < 0)) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to read rxq%u hw pit_wr_idx, ret=%d\n",
			q_id, pit_widx);
		mtk_dpmaif_common_err_handle(dcb, true);
		return;
	}

	rxq->pit_wr_idx = pit_widx;
	dcb->traffic_stats.dpmaif_rx.rx_done_last_cnt[q_id] = 0;
	napi_schedule(&rxq->napi);
	dcb->traffic_stats.dpmaif_rx.irq_rx_evt[q_id].dl_done++;
}

static void mtk_dpmaif_irq_pit_len_err(struct mtk_dpmaif_ctlb *dcb, unsigned int q_id)
{
	dcb->traffic_stats.dpmaif_rx.irq_rx_evt[q_id].pit_len_err++;
	dcb->rxqs[q_id].pit_cnt_err_intr_set = true;
	trace_mtk_data_event_stats(DPMAIF_PIT_CNT_LEN_ERR, q_id,
				   dcb->traffic_stats.dpmaif_rx.irq_rx_evt[q_id].pit_len_err);
}

static void mtk_dpmaif_tras_ulq_cfg(struct mtk_dpmaif_ctlb *dcb, unsigned int cfg_data,
				    unsigned int frc)
{
	unsigned char txq_cnt = dcb->drv_info->cfg->tx_cfg.txq_cnt;
	unsigned long long base_ts, cur_ts;
	unsigned char q_mask, rst, period;
	struct dpmaif_txq *txq;
	int i;

	q_mask = FIELD_GET(MHCCIF_MSG_QMASK, cfg_data);
	period = FIELD_GET(MHCCIF_MSG_PRD, cfg_data);
	rst = FIELD_GET(MHCCIF_MSG_RST, cfg_data);

	if (rst && !mtk_frc_get_host_ts_by_dev_us(DCB_TO_MDEV(dcb), frc, &base_ts)) {
		cur_ts = ktime_get_ns();
		if (base_ts > cur_ts) {
			if (period)
				base_ts = cur_ts - (period - (base_ts - cur_ts) % period);
			else
				base_ts = 0;
		}

		dcb->db_ctlb.db_base_ts = base_ts;
	}

	for (i = 0; i < txq_cnt; i++) {
		if (!(q_mask & BIT(i)))
			continue;

		txq = &dcb->txqs[i];
		txq->db_delay_ns = DPMAIF_MS_TO_NS(period);
	}
}

static void mtk_dpmaif_tras_ulq_rst(struct mtk_dpmaif_ctlb *dcb, unsigned int cfg_data)
{
	struct dpmaif_tx_cfg *tx_cfg = &dcb->drv_info->cfg->tx_cfg;
	struct dpmaif_txq *txq;
	unsigned char q_mask;
	int i;

	q_mask = FIELD_GET(MHCCIF_MSG_QMASK, cfg_data);
	for (i = 0; i < tx_cfg->txq_cnt; i++) {
		if (!(q_mask & BIT(i)))
			continue;

		txq = &dcb->txqs[i];
		txq->db_delay_ns = DPMAIF_MS_TO_NS(tx_cfg->txqs[txq->id].doorbell_delay);
	}
}

static void mtk_dpmaif_irq_tras_sync(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned char msg_type;
	unsigned int cfg_data;
	unsigned int frc_data;

	cfg_data = mtk_pci_get_tras_cfg(DCB_TO_MDEV(dcb));
	frc_data = mtk_pci_get_tras_frc(DCB_TO_MDEV(dcb));

	msg_type = FIELD_GET(MHCCIF_MSG_TYPE, cfg_data);
	switch (msg_type) {
	case DPMAIF_TRAS_ULQ_CFG:
		mtk_dpmaif_tras_ulq_cfg(dcb, cfg_data, frc_data);
		break;

	case DPMAIF_TRAS_ULQ_RST:
		mtk_dpmaif_tras_ulq_rst(dcb, cfg_data);
		break;

	default:
		MTK_WARN(DCB_TO_MDEV(dcb), "Invalid parameter, unknown tras config type\n");
		break;
	}

	trace_mtk_tras_irq_data(cfg_data, frc_data);
}

static int mtk_dpmaif_irq_handle(int irq_id, void *data)
{
	struct dpmaif_drv_intr_info intr_info;
	struct dpmaif_traffic_stats *stats;
	struct dpmaif_irq_param *irq_param;
	struct dpmaif_bat_ring *bat_ring;
	unsigned long long len_err_cnt;
	struct mtk_dpmaif_ctlb *dcb;
	int bat_id;
	int ret;
	int i;

	irq_param = data;
	dcb = irq_param->dcb;
	stats = &dcb->traffic_stats;

	stats->dpmaif_irq.irq_last_time[irq_param->idx] = local_clock();
	stats->dpmaif_irq.irq_total_cnt[irq_param->idx]++;

	if (unlikely(dcb->dpmaif_state != DPMAIF_STATE_PWRON)) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Invalid parameter, unexpected dpmaif irq 0x%x\n",
			irq_param->idx);
		goto out;
	}

	memset(&intr_info, 0x00, sizeof(struct dpmaif_drv_intr_info));
	ret = mtk_dpmaif_drv_intr_handle(dcb->drv_info, &intr_info, irq_param->idx);
	if (ret < 0) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to get dpmaif drv irq info\n");
		goto clean_drv_irq_info;
	}

	for (i = 0; i < intr_info.intr_cnt; i++) {
		switch (intr_info.intr_types[i]) {
		case DPMAIF_INTR_UL_DONE:
			MTK_DBG_DATA_IRQ_HANDLER_INFO(DCB_TO_MDEV(dcb), irq_param->idx,
						      DPMAIF_INTR_UL_DONE,
						      intr_info.intr_queues[i]);
			mtk_dpmaif_irq_tx_done(dcb, intr_info.intr_queues[i]);
			break;
		case DPMAIF_INTR_DL_BATCNT_LEN_ERR:
			bat_id = intr_info.intr_queues[i];
			len_err_cnt = ++stats->dpmaif_rx.irq_bat_evt[bat_id].dl_bat_cnt_len_err;
			bat_ring = &dcb->bat_infos[bat_id].normal_bat_ring;
			bat_ring->bat_cnt_err_intr_set = true;
			mtk_dpmaif_task_wakeup(&dcb->bat_infos[bat_id].task_ctlb);
			MTK_DBG_DATA_IRQ_HANDLER_INFO(DCB_TO_MDEV(dcb), irq_param->idx,
						      DPMAIF_INTR_DL_BATCNT_LEN_ERR,
						      bat_id);
			trace_mtk_data_event_stats(DPMAIF_BAT_CNT_LEN_ERR, bat_id, len_err_cnt);
			break;
		case DPMAIF_INTR_DL_FRGCNT_LEN_ERR:
			bat_id = intr_info.intr_queues[i];
			len_err_cnt = ++stats->dpmaif_rx.irq_bat_evt[bat_id].dl_frag_cnt_len_err;
			bat_ring = &dcb->bat_infos[bat_id].frag_bat_ring;
			bat_ring->bat_cnt_err_intr_set = true;
			if (dcb->bat_infos[bat_id].frag_bat_enabled)
				mtk_dpmaif_task_wakeup(&dcb->bat_infos[bat_id].task_ctlb);

			MTK_DBG_DATA_IRQ_HANDLER_INFO(DCB_TO_MDEV(dcb), irq_param->idx,
						      DPMAIF_INTR_DL_FRGCNT_LEN_ERR,
						      bat_id);
			trace_mtk_data_event_stats(DPMAIF_FRAG_CNT_LEN_ERR, bat_id, len_err_cnt);
			break;
		case DPMAIF_INTR_DL_PITCNT_LEN_ERR:
			MTK_DBG_DATA_IRQ_HANDLER_INFO(DCB_TO_MDEV(dcb), irq_param->idx,
						      DPMAIF_INTR_DL_PITCNT_LEN_ERR,
						      intr_info.intr_queues[i]);
			mtk_dpmaif_irq_pit_len_err(dcb, intr_info.intr_queues[i]);
			mtk_dpmaif_book_doorbell_work(dcb, 0);
			dpmaif_dump_once(dcb);
			break;
		case DPMAIF_INTR_DL_DONE:
			MTK_DBG_DATA_IRQ_HANDLER_INFO(DCB_TO_MDEV(dcb), irq_param->idx,
						      DPMAIF_INTR_DL_DONE,
						      intr_info.intr_queues[i]);
			mtk_dpmaif_irq_rx_done(dcb, intr_info.intr_queues[i]);
			break;
		case DPMAIF_INTR_TRAS_SYNC:
			mtk_dpmaif_irq_tras_sync(dcb);
			break;
		default:
			break;
		}
	}

clean_drv_irq_info:
	mtk_pci_clear_irq(DCB_TO_MDEV(dcb), irq_param->dev_irq_id);
	mtk_pci_unmask_irq(DCB_TO_MDEV(dcb), irq_param->dev_irq_id);
out:
	return IRQ_HANDLED;
}

#ifdef CONFIG_MTK_DATA_FEATURE_TEST
static void mtk_dpmaif_set_irq_affinity(struct mtk_dpmaif_ctlb *dcb,
					struct dpmaif_irq_param *irq_param)
{
	int cpu_mask = irq_param->cpu_mask;
	int online_cpus = 0;
	cpumask_var_t mask;
	int i, ret;

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	online_cpus = dcb->online_cpus;
#endif

	if (cpu_mask == 0 || online_cpus == 0)
		return;

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL)) {
		MTK_WARN(DCB_TO_MDEV(dcb), "Failed to alloc cpumask var\n");
		return;
	}

	for (i = 0; i < online_cpus; i++) {
		if (cpu_mask & BIT(i))
			cpumask_set_cpu(i, mask);
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0)
	ret = irq_set_affinity_and_hint(irq_param->dev_virq_id, mask);
#else
	ret = irq_set_affinity_hint(irq_param->dev_virq_id, mask);
#endif

	MTK_INFO(DCB_TO_MDEV(dcb), "dpmaif_irq_src%d affinity, cpu=0x%x, ret=%d\n",
		 irq_param->dpmaif_irq_src, cpu_mask, ret);
	free_cpumask_var(mask);
}
#endif

static int mtk_dpmaif_irq_init(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned char irq_cnt = dcb->drv_info->cfg->intr_cfg.irq_cnt;
	struct dpmaif_irq_param *irq_param;
	enum mtk_irq_src irq_src;
	int i, j;
	int ret;

	dcb->irq_params = devm_kcalloc(DCB_TO_DEV(dcb), irq_cnt, sizeof(*irq_param), GFP_KERNEL);
	if (!dcb->irq_params) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate dpmaif interrupt parameters\n");
		return -ENOMEM;
	}

	for (i = 0; i < irq_cnt; i++) {
		irq_param = &dcb->irq_params[i];
		irq_param->idx = i;
		irq_param->dcb = dcb;
		irq_src = dcb->drv_info->cfg->intr_cfg.irqs[i].id;
#ifdef CONFIG_MTK_DATA_FEATURE_TEST
		irq_param->cpu_mask = dcb->drv_info->cfg->intr_cfg.irqs[i].cpu_mask;
#endif
		irq_param->dpmaif_irq_src = irq_src;
		irq_param->dev_irq_id = mtk_pci_get_irq_id(DCB_TO_MDEV(dcb), irq_src);
		if (irq_param->dev_irq_id < 0) {
			MTK_ERR(DCB_TO_MDEV(dcb),
				"Failed to allocate irq id, irq_src=%d\n", irq_src);
			ret = -EINVAL;
			goto unregister_irq;
		}

		ret = mtk_pci_register_irq(DCB_TO_MDEV(dcb), irq_param->dev_irq_id,
					   mtk_dpmaif_irq_handle, irq_param);
		if (ret < 0) {
			MTK_ERR(DCB_TO_MDEV(dcb),
				"Failed to register irq, irq_src=%d\n", irq_src);
			goto unregister_irq;
		}
		irq_param->dev_virq_id = mtk_pci_get_virq_id(DCB_TO_MDEV(dcb),
							     irq_param->dev_irq_id);
#ifdef CONFIG_MTK_DATA_FEATURE_TEST
		mtk_dpmaif_set_irq_affinity(dcb, irq_param);
#endif
	}

	/* HW layer default mask dpmaif interrupt. */
	dcb->irq_enabled = false;

	return 0;

unregister_irq:
	for (j = i - 1; j >= 0; j--) {
		irq_param = &dcb->irq_params[j];
		mtk_pci_unregister_irq(DCB_TO_MDEV(dcb), irq_param->dev_irq_id);
	}

	devm_kfree(DCB_TO_DEV(dcb), dcb->irq_params);

	return ret;
}

static int mtk_dpmaif_irq_exit(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned char irq_cnt = dcb->drv_info->cfg->intr_cfg.irq_cnt;
	struct dpmaif_irq_param *irq_param;
	int i;

	for (i = 0; i < irq_cnt; i++) {
		irq_param = &dcb->irq_params[i];
		mtk_pci_unregister_irq(DCB_TO_MDEV(dcb), irq_param->dev_irq_id);
	}

	devm_kfree(DCB_TO_DEV(dcb), dcb->irq_params);

	return 0;
}

static int mtk_dpmaif_port_cfg(struct mtk_dpmaif_ctlb *dcb)
{
	struct mtk_data_trans_info *trans_info = &dcb->data_blk->trans_info;
	struct dpmaif_drv_cfg *cfg = dcb->drv_info->cfg;
	struct dpmaif_rxq *rxq;
	int i;

	memset(trans_info, 0x00, sizeof(struct mtk_data_trans_info));
	if (cfg->cap & DATA_HW_F_LRO)
		trans_info->cap |= DATA_F_GRO_HW;
	if (cfg->cap & DATA_HW_F_INDR_TBL)
		trans_info->cap |= DATA_F_RXFH;
	if (cfg->cap & DATA_HW_F_INTR_COALESCE)
		trans_info->cap |= DATA_F_INTR_COALESCE;
	if (cfg->cap & DATA_HW_F_RXCSUM)
		trans_info->cap |= DATA_F_RXCSUM;
	if (cfg->cap & DATA_HW_F_TXCSUM)
		trans_info->cap |= DATA_F_TXCSUM;

	trans_info->txq_cnt = cfg->tx_cfg.txq_cnt;
	trans_info->rxq_cnt = cfg->rx_cfg.rxq_cnt;
	trans_info->max_mtu = cfg->rx_cfg.mtu;

	for (i = 0; i < trans_info->rxq_cnt; i++) {
		rxq = &dcb->rxqs[i];
		dcb->napi[i] = &rxq->napi;
		trans_info->rxq_attr[i] = dcb->rxqs[i].attr;
	}
	trans_info->napis = dcb->napi;

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	if (dcb->aff_cfg)
		trans_info->napi_thrd_aff = dcb->aff_cfg[DATA_DEFAULT_AFF_MODE].napi_thrd_aff;
#endif

	return 0;
}

static int mtk_dpmaif_hw_init(struct mtk_dpmaif_ctlb *dcb)
{
	unsigned int bat_ring_num = dcb->drv_info->cfg->rx_cfg.bat_ring_num;
	struct dpmaif_drv_cfg *cfg = dcb->drv_info->cfg;
	struct dpmaif_bat_ring *bat_ring;
	unsigned int bat_reload_cnt;
	struct dpmaif_rxq *rxq;
	struct dpmaif_txq *txq;
	int ret, i;

	for (i = 0; i < bat_ring_num; i++) {
		bat_ring = &dcb->bat_infos[i].normal_bat_ring;
		bat_reload_cnt = atomic_read(&bat_ring->to_reload_cnt);
		mtk_dpmaif_reload_rx_buff(dcb, bat_ring, bat_reload_cnt);
		if (!atomic_read(&bat_ring->reload_cnt)) {
			MTK_ERR(DCB_TO_MDEV(dcb), "Failed to reload normal bat%d buf\n", i);
			return -ENOMEM;
		}

		cfg->rx_cfg.bats[i].bat_base = bat_ring->bat_dma_addr;
		cfg->rx_cfg.bats[i].buf_size = bat_ring->buf_size;
		cfg->rx_cfg.bats[i].real_reload_cnt = atomic_read(&bat_ring->reload_cnt);

		if (dcb->bat_infos[i].frag_bat_enabled) {
			dcb->drv_info->features |= DATA_HW_F_FRAG;
			bat_ring = &dcb->bat_infos[i].frag_bat_ring;
			bat_reload_cnt = atomic_read(&bat_ring->to_reload_cnt);
			mtk_dpmaif_reload_rx_buff(dcb, bat_ring, bat_reload_cnt);
			if (!atomic_read(&bat_ring->reload_cnt)) {
				MTK_ERR(DCB_TO_MDEV(dcb), "Failed to reload frag bat%d buf\n", i);
				return -ENOMEM;
			}

			cfg->rx_cfg.frags[i].bat_base = bat_ring->bat_dma_addr;
			cfg->rx_cfg.frags[i].buf_size = bat_ring->buf_size;
			cfg->rx_cfg.frags[i].real_reload_cnt = atomic_read(&bat_ring->reload_cnt);
		} else {
			dcb->drv_info->features &= ~DATA_HW_F_FRAG;
		}
	}

	for (i = 0; i < cfg->rx_cfg.rxq_cnt; i++) {
		rxq = &dcb->rxqs[i];
		cfg->rx_cfg.rxqs[i].pit_base = rxq->pit_dma_addr;
	}

	for (i = 0; i < cfg->tx_cfg.txq_cnt; i++) {
		txq = &dcb->txqs[i];
		cfg->tx_cfg.txqs[i].drb_base = txq->drb_dma_addr;
	}

	ret = dcb->drv_info->drv_ops->init(dcb->drv_info, NULL);
	if (ret < 0)
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to initialize dpmaif hw\n");

	for (i = 0; i < bat_ring_num; i++) {
		bat_ring = &dcb->bat_infos[i].normal_bat_ring;
		atomic_sub(cfg->rx_cfg.bats[i].real_reload_cnt, &bat_ring->reload_cnt);

		if (dcb->bat_infos[i].frag_bat_enabled) {
			bat_ring = &dcb->bat_infos[i].frag_bat_ring;
			atomic_sub(cfg->rx_cfg.frags[i].real_reload_cnt, &bat_ring->reload_cnt);
		}
	}
	return ret;
}

static int mtk_dpmaif_start(struct mtk_md_dev *mdev)
{
	struct mtk_dpmaif_ctlb *dcb = ((struct mtk_data_blk *)(mdev->data_blk))->dcb;
	int ret;

	if (dcb->dpmaif_state == DPMAIF_STATE_PWRON) {
		MTK_WARN(DCB_TO_MDEV(dcb), "Invalid parameters, dpmaif_state in PWRON\n");
		ret = -EINVAL;
		goto out;
	}

	/* Initialize dpmaif hw. */
	ret = mtk_dpmaif_hw_init(dcb);
	if (ret < 0) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to initialize dpmaif hw\n");
		goto out;
	}

	ret = mtk_dpmaif_doorbell_task_start(dcb);
	if (ret < 0) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to run doorbell task, ret=%d\n", ret);
		goto out;
	}

	/* Initialize and run all tx services. */
	ret = mtk_dpmaif_tx_srvs_start(dcb);
	if (ret) {
		MTK_WARN(DCB_TO_MDEV(dcb), "Failed to start all tx srvs\n");
		goto stop_doorbell_task;
	}

	dcb->dpmaif_state = DPMAIF_STATE_PWRON;
	dcb->dpmaif_sw_reset = false;
	mtk_dpmaif_disable_irq(dcb);
	mtk_dpmaif_clear_irq(dcb);

	MTK_INFO(DCB_TO_MDEV(dcb), "dpmaif start done\n");

	return 0;
stop_doorbell_task:
	mtk_dpmaif_doorbell_task_stop(dcb);
out:
	return ret;
}

static void mtk_dpmaif_sw_reset(struct mtk_dpmaif_ctlb *dcb)
{
	if (!dcb->dpmaif_sw_reset) {
		dcb->dpmaif_sw_reset = true;
		mtk_dpmaif_tx_res_reset(dcb);
		mtk_dpmaif_rx_res_reset(dcb);
		mtk_dpmaif_bat_res_reset(dcb);
		mtk_dpmaif_tx_vqs_reset(dcb);
		memset(&dcb->traffic_stats, 0x00, sizeof(struct dpmaif_traffic_stats));
		dcb->dpmaif_pm_ready = true;
		dcb->dpmaif_user_ready = false;
		dcb->trans_enabled = false;
		dcb->suspend_late_called = false;
		dcb->dump_flag = 0;
		atomic_set(&dcb->dump_once, 0);
		dcb->err_event = 0;
	}
}

static int mtk_dpmaif_stop(struct mtk_md_dev *mdev)
{
	struct mtk_dpmaif_ctlb *dcb = ((struct mtk_data_blk *)(mdev->data_blk))->dcb;

	if (dcb->dpmaif_state == DPMAIF_STATE_PWROFF)
		goto out;

	/* The flow of trans control as follow depends on dpmaif state,
	 * so change state firstly.
	 */
	dcb->dpmaif_state = DPMAIF_STATE_PWROFF;

	/* Stop all tx service. */
	mtk_dpmaif_tx_srvs_stop(dcb);

	mutex_lock(&dcb->trans_ctl_lock);

	/* Stop dpmaif tx/rx handle. */
	mtk_dpmaif_trans_ctl(dcb, false);

	/* Stop doorbell thread. */
	mtk_dpmaif_doorbell_task_exit(dcb);

	/* Clear data_no_intf state. */
	dcb->db_ctlb.data_no_intf = false;

	mutex_unlock(&dcb->trans_ctl_lock);

out:
	return 0;
}

static void mtk_dpmaif_clear(struct mtk_md_dev *mdev)
{
	struct mtk_dpmaif_ctlb *dcb = ((struct mtk_data_blk *)(mdev->data_blk))->dcb;

	/* clear data structure */
	mtk_dpmaif_sw_reset(dcb);
}

/**
 * mtk_dpmaif_prepare() - perform the additional part that
 * system suspend exceeds RPM suspend.
 * @mdev: pointer to mtk_md_dev
 * @param: pointer to dcb
 * @is_smart_suspend: true means is smart suspend
 *
 * This function called by pm when entering smart suspend. Since
 * already in RPM suspend state, entering smart suspend only needs
 * to perform the additional part that system suspend exceeds RPM
 * suspend. Do not access HW register in this function because PCIe
 * link is not ready at this time.
 *
 * Return:
 * 0:	 success.
 */
static int mtk_dpmaif_prepare(struct mtk_md_dev *mdev, void *param, bool is_smart_suspend)
{
	struct mtk_dpmaif_ctlb *dcb = param;

	if (is_smart_suspend)
		mtk_dpmaif_task_pause(&dcb->db_ctlb.task_ctlb, dcb);

	MTK_INFO(DCB_TO_MDEV(dcb), "dpmaif prepare done");

	return 0;
}

/**
 * mtk_dpmaif_complete() - perform the additional part that
 * system resume exceeds RPM resume.
 * @mdev: pointer to mtk_md_dev
 * @param: pointer to dcb
 * @is_smart_suspend: true means is smart suspend
 *
 * This function called by pm when exiting smart suspend. Since
 * keeping RPM suspend is need after exiting smart suspend, only
 * needs to perform the additional part that system resume exceeds
 * RPM resume. Do not access HW register in this function because
 * PCIe link is not ready at this time.
 *
 * Return:
 * 0:	success.
 */
static int mtk_dpmaif_complete(struct mtk_md_dev *mdev, void *param, bool is_smart_suspend)
{
	struct mtk_dpmaif_ctlb *dcb = param;

	if (is_smart_suspend)
		mtk_dpmaif_task_resume(&dcb->db_ctlb.task_ctlb, dcb, mtk_dpmaif_has_doorbell(dcb));

	MTK_INFO(DCB_TO_MDEV(dcb), "dpmaif complete done");

	return 0;
}

static int mtk_dpmaif_suspend(struct mtk_md_dev *mdev, void *param, bool is_runtime)
{
	struct mtk_dpmaif_ctlb *dcb = param;

	dcb->dpmaif_pm_ready = false;

	if (!is_runtime)
		mtk_dpmaif_task_pause(&dcb->db_ctlb.task_ctlb, dcb);

	MTK_INFO(DCB_TO_MDEV(dcb), "dpmaif suspend done, is_runtime=%d", is_runtime);

	return 0;
}

static int mtk_dpmaif_suspend_late(struct mtk_md_dev *mdev, void *param, bool is_runtime)
{
	struct mtk_dpmaif_ctlb *dcb = param;

	mutex_lock(&dcb->trans_ctl_lock);
	mtk_dpmaif_trans_ctl(dcb, false);
	mutex_unlock(&dcb->trans_ctl_lock);
	dcb->suspend_late_called = true;
	MTK_INFO(DCB_TO_MDEV(dcb), "dpmaif suspend late done, is_runtime=%d\n", is_runtime);
	return 0;
}

static int mtk_dpmaif_resume(struct mtk_md_dev *mdev, void *param, bool is_runtime,
			     bool link_ready)
{
	bool dev_is_reset = mtk_pm_check_dev_reset(mdev);
	struct mtk_dpmaif_ctlb *dcb = param;

	if (!is_runtime)
		mtk_dpmaif_task_resume(&dcb->db_ctlb.task_ctlb, dcb, mtk_dpmaif_has_doorbell(dcb));

	/* If device resume after device power off, we don't need to enable trans.
	 * Since host driver will run re-init flow, we will get back to normal.
	 */
	if (!dev_is_reset) {
		dcb->dpmaif_pm_ready = true;
		if (dcb->suspend_late_called) {
			dcb->suspend_late_called = false;
			mutex_lock(&dcb->trans_ctl_lock);
			mtk_dpmaif_trans_ctl(dcb, true);
			mutex_unlock(&dcb->trans_ctl_lock);
		}
	}

	MTK_INFO(DCB_TO_MDEV(dcb),
		 "dpmaif resume done, dev_is_reset=%d, is_runtime=%d",
		 dev_is_reset, is_runtime);

	return 0;
}

static int mtk_dpmaif_pm_init(struct mtk_dpmaif_ctlb *dcb)
{
	struct mtk_pm_entity *pm_entity;
	int ret;

	pm_entity = &dcb->pm_entity;
	INIT_LIST_HEAD(&pm_entity->entry);
	pm_entity->user = MTK_USER_DATA;
	pm_entity->param = dcb;
	pm_entity->suspend = &mtk_dpmaif_suspend;
	pm_entity->suspend_late = &mtk_dpmaif_suspend_late;
	pm_entity->resume = &mtk_dpmaif_resume;
	pm_entity->prepare = &mtk_dpmaif_prepare;
	pm_entity->complete = &mtk_dpmaif_complete;

	ret = mtk_pm_entity_register(DCB_TO_MDEV(dcb), pm_entity);
	if (ret < 0)
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to register dpmaif pm_entity\n");

	return ret;
}

static int mtk_dpmaif_pm_exit(struct mtk_dpmaif_ctlb *dcb)
{
	int ret;

	ret = mtk_pm_entity_unregister(DCB_TO_MDEV(dcb), &dcb->pm_entity);
	if (ret < 0)
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to unregister dpmaif pm_entity\n");

	return ret;
}

#ifdef CONFIG_DEBUG_FS
#ifdef CONFIG_DATA_TEST_MODE
#define MTK_DEBUGFS_BUF_SIZE 256
static ssize_t dpmaif_test_mode_read(void *data, char *buf, ssize_t max_cnt)
{
	struct dpmaif_test_mode_cfg *test_mode_cfg;
	struct mtk_dpmaif_ctlb *dcb = data;

	test_mode_cfg = &dcb->test_mode_cfg;

	return snprintf(buf, max_cnt,
			"test configuration: mode=%u, dl_start=%u, pkts/ms=%u, test_time=%u\n",
		test_mode_cfg->md_tput_mode, test_mode_cfg->md_start_dl_tput,
		test_mode_cfg->md_pkt_number_per_ms, test_mode_cfg->md_dl_tput_test_time);
}

static ssize_t dpmaif_test_mode_write(void *data, const char *buf, ssize_t cnt)
{
	struct dpmaif_test_mode_cfg *test_mode_cfg;
	struct mtk_dpmaif_ctlb *dcb = data;
	unsigned int mode_cfg;
	int ret;

	test_mode_cfg = &dcb->test_mode_cfg;
	ret = kstrtou32(buf, 16, &mode_cfg);
	if (ret) {
		pr_notice("Invalid parameter, Please check input string format\n");
		return -EFAULT;
	}

	pr_notice("test mode: 0x%08x\n", mode_cfg);

	/* Tput mode */
	if (mode_cfg & DPMAIF_MD_TPUT_MODE_SET_MASK)
		test_mode_cfg->md_tput_mode = (mode_cfg >> DPMAIF_MD_TPUT_MODE_OFFSET) &
			DPMAIF_MD_TPUT_MODE_MASK;

	/* Start/stop dl test */
	if (mode_cfg & DPMAIF_MD_TPUT_CTL_SET_MASK)
		test_mode_cfg->md_start_dl_tput = (mode_cfg >> DPMAIF_MD_DL_CTL_OFFSET) &
			DPMAIF_MD_DL_CTL_MASK;

	/* pkt_count/ms */
	if (mode_cfg & DPMAIF_MD_TPUT_PKT_SET_MASK)
		test_mode_cfg->md_pkt_number_per_ms = (mode_cfg >> DPMAIF_MD_PKT_OFFSET) &
			DPMAIF_MD_PKT_MASK;

	/* DL test time */
	if (mode_cfg & DPMAIF_MD_TPUT_TIME_SET_MASK)
		test_mode_cfg->md_dl_tput_test_time = (mode_cfg >> DPMAIF_MD_DL_TIME_OFFSET) &
			DPMAIF_MD_DL_TIME_MASK;

	pr_notice("test configuration: mode=%u, dl_start=%u, pkts/ms=%u, test_time=%u\n",
		  test_mode_cfg->md_tput_mode, test_mode_cfg->md_start_dl_tput,
		test_mode_cfg->md_pkt_number_per_ms, test_mode_cfg->md_dl_tput_test_time);

	return cnt;
}

MTK_DBGFS(test_mode, &dpmaif_test_mode_read, &dpmaif_test_mode_write);
#endif

static ssize_t dpmaif_tput_monitor_read(void *data, char *buf, ssize_t max_cnt)
{
	struct mtk_dpmaif_ctlb *dcb = data;

	return snprintf(buf, max_cnt, "monitor_enabled: %d\n", dcb->tput_stats.monitor_enabled);
}

#define TPUT_CFG_MASK BIT(0)
static ssize_t dpmaif_tput_monitor_write(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_dpmaif_ctlb *dcb = data;
	unsigned int user_setting;
	int ret;

	ret = kstrtou32(buf, 16, &user_setting);
	if (ret) {
		pr_notice("Invalid parameter, Please check input string format\n");
		return -EFAULT;
	}

	dcb->tput_stats.monitor_enabled = !!(user_setting & TPUT_CFG_MASK);

	return cnt;
}

MTK_DBGFS(tput_monitor, &dpmaif_tput_monitor_read, &dpmaif_tput_monitor_write);

static ssize_t dpmaif_dump_flag_read(void *data, char *buf, ssize_t max_cnt)
{
	struct mtk_dpmaif_ctlb *dcb = data;

	return snprintf(buf, max_cnt, "dpmaif dump flag=0x%lx\n", dcb->dump_flag);
}

static ssize_t dpmaif_dump_flag_write(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_dpmaif_ctlb *dcb = data;
	unsigned int user_mask;
	int ret;

	ret = kstrtou32(buf, 16, &user_mask);
	if (ret) {
		pr_notice("Invalid parameter, Please check input string format\n");
		return -EFAULT;
	}

	dcb->dump_flag = user_mask;

	return cnt;
}

MTK_DBGFS(dump_flag, &dpmaif_dump_flag_read, &dpmaif_dump_flag_write);

static ssize_t dpmaif_dump_read(void *data, char *buf, ssize_t max_cnt)
{
	struct mtk_dpmaif_ctlb *dcb = data;

	if (!dcb)
		return 0;

	/* Dump WWAN information. */
	mtk_wwan_notify(dcb->data_blk, DATA_EVT_DUMP, 0);

	mtk_pm_runtime_get(DCB_TO_MDEV(dcb), MTK_USER_DATA, true);

	if (dcb->dpmaif_state == DPMAIF_STATE_PWRON)
		mtk_dpmaif_dump(dcb->data_blk->mdev);

	mtk_pm_runtime_put(DCB_TO_MDEV(dcb), MTK_USER_DATA, true);

	return 0;
}

MTK_DBGFS(dump, &dpmaif_dump_read, NULL);

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_AFFINITY)
static void refresh_google_affinity_values(struct mtk_dpmaif_ctlb *dcb)
{
	int i, ret;

	for (i = 0; i < DPMAIF_CPU_LOADING_MODE; i++) {
		ret = dpmaif_google_fill_affinity(i,
						  dcb->online_cpus,
						  &google_affinity_config[i].speed,
						  google_affinity_config[i].napi_thrd_aff,
						  MTK_DATA_NAPI_NR_MAX,
						  google_affinity_config[i].steer_wq_aff,
						  MTK_DATA_NAPI_NR_MAX,
						  google_affinity_config[i].reload_thrd_aff,
						  DPMAIF_BAT_NUM_MAX,
						  &google_affinity_config[i].doorbell_thrd_aff);

		if (ret)
			MTK_WARN(DCB_TO_MDEV(dcb),
				 "Refresh affinity for index %d, ret=%d\n", i, ret);
	}
}

static ssize_t refresh_google_affinity_write(void *data, const char *buf, ssize_t cnt)
{
	struct mtk_dpmaif_ctlb *dcb = data;

	refresh_google_affinity_values(dcb);

	return cnt;
}

MTK_DBGFS(refresh_google_affinity, NULL, &refresh_google_affinity_write);
#endif

static int dpmaif_debugfs_init(struct mtk_dpmaif_ctlb *dcb)
{
	struct dentry *parent = mtk_get_dev_dentry(DCB_TO_MDEV(dcb));

	if (!parent) {
		pr_notice("Failed to get host device debugfs parent dir\n");
		return -EFAULT;
	}

	/* Create dpmaif debugfs folder. */
	dcb->dpmaif_dir = mtk_dbgfs_create_dir(parent, "data_plane");
	if (!dcb->dpmaif_dir) {
		pr_notice("Failed to create dir\n");
		return -EFAULT;
	}

#ifdef CONFIG_DATA_TEST_MODE
	mtk_dbgfs_create_file(dcb->dpmaif_dir, &mtk_dbgfs_test_mode, dcb);
#endif
	mtk_dbgfs_create_file(dcb->dpmaif_dir, &mtk_dbgfs_tput_monitor, dcb);
	mtk_dbgfs_create_file(dcb->dpmaif_dir, &mtk_dbgfs_dump_flag, dcb);
	mtk_dbgfs_create_file(dcb->dpmaif_dir, &mtk_dbgfs_dump, dcb);
#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_AFFINITY)
	mtk_dbgfs_create_file(dcb->dpmaif_dir, &mtk_dbgfs_refresh_google_affinity, dcb);
#endif

	return 0;
}

static void dpmaif_debugfs_exit(struct mtk_dpmaif_ctlb *dcb)
{
	mtk_dbgfs_remove(dcb->dpmaif_dir);
}
#else
static inline int dpmaif_debugfs_init(struct mtk_dpmaif_ctlb *dcb)
{
	return 0;
}

static inline void dpmaif_debugfs_exit(struct mtk_dpmaif_ctlb *dcb)
{
}
#endif

static int mtk_dpmaif_tput_monitor_init(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_tput_stats *tput_stats = &dcb->tput_stats;

	INIT_DELAYED_WORK(&tput_stats->monitor_work, mtk_dpmaif_traffic_monitor_func);
	tput_stats->monitor_wq = alloc_workqueue("dpmaif_monitor_wq_%s",
						 WQ_HIGHPRI | WQ_UNBOUND | WQ_MEM_RECLAIM,
					      0, DCB_TO_DEV_STR(dcb));
	if (!tput_stats->monitor_wq) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to allocate monitor workqueue\n");
		return -ENOMEM;
	}

	return 0;
}

static void mtk_dpmaif_tput_monitor_exit(struct mtk_dpmaif_ctlb *dcb)
{
	struct dpmaif_tput_stats *tput_stats = &dcb->tput_stats;

	cancel_delayed_work_sync(&tput_stats->monitor_work);
	destroy_workqueue(tput_stats->monitor_wq);
}

static ssize_t mtk_dpmaif_tx_stats_cb(struct mtk_md_dev *mdev, void *data, char *buf)
{
	ssize_t size = 0;

	if (!buf)
		MTK_DBG_DATA_TX_STATS(mdev, data);
	else
		MTK_DBG_DATA_TX_STATS_WITH_BUF(data, buf, size);
	return size;
}

static ssize_t mtk_dpmaif_rx_stats_cb(struct mtk_md_dev *mdev, void *data, char *buf)
{
	ssize_t size = 0;

	if (!buf)
		MTK_DBG_DATA_RX_STATS(mdev, data);
	else
		MTK_DBG_DATA_RX_STATS_WITH_BUF(data, buf, size);
	return size;
}

static int mtk_dpmaif_stats_init(struct mtk_dpmaif_ctlb *dcb)
{
	struct mtk_md_dev *mdev = DCB_TO_MDEV(dcb);
	int ret;

	dcb->stats_tx_id = mdev->utility_cfg->stats_cfg->data_type_base;
	dcb->stats_rx_id = mdev->utility_cfg->stats_cfg->data_type_base + 1;
	ret = mtk_stats_register_cb(mdev, dcb->stats_tx_id,
				    DPMAIF_STATS_PERIOD_S, mtk_dpmaif_tx_stats_cb,
				    &dcb->traffic_stats);
	if (ret) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to register dpmaif stats\n");
		return ret;
	}
	ret = mtk_stats_register_cb(mdev, dcb->stats_rx_id,
				    DPMAIF_STATS_PERIOD_S, mtk_dpmaif_rx_stats_cb,
				    &dcb->traffic_stats);
	if (ret) {
		MTK_ERR(DCB_TO_MDEV(dcb), "Failed to register dpmaif stats\n");
		mtk_stats_unregister_cb(mdev, dcb->stats_tx_id);
	}
	return ret;
}

static void mtk_dpmaif_stats_exit(struct mtk_dpmaif_ctlb *dcb)
{
	mtk_stats_unregister_cb(DCB_TO_MDEV(dcb), dcb->stats_tx_id);
	mtk_stats_unregister_cb(DCB_TO_MDEV(dcb), dcb->stats_rx_id);
}

static int mtk_dpmaif_sw_init(struct mtk_md_dev *mdev)
{
	struct mtk_data_blk *data_blk = mdev->data_blk;
	struct mtk_dpmaif_ctlb *dcb;
	int ret;

	dcb = devm_kzalloc(data_blk->mdev->dev, sizeof(*dcb), GFP_KERNEL);
	if (!dcb) {
		MTK_ERR(data_blk->mdev, "Failed to allocate dpmaif_ctlb\n");
		return -ENOMEM;
	}

	data_blk->dcb = dcb;
	dcb->data_blk = data_blk;
	dcb->dpmaif_state = DPMAIF_STATE_PWROFF;
	dcb->dpmaif_pm_ready = true;
	dcb->dpmaif_user_ready = false;
	dcb->trans_enabled = false;
	dcb->suspend_late_called = false;
	dcb->dpmaif_sw_reset = false;
	dcb->dpmaif_rx_legacy = rx_legacy_mode;
	dcb->tput_stats.dcb = dcb;
	mutex_init(&dcb->trans_ctl_lock);
	mutex_init(&dcb->task_paused_lock);

#ifdef CONFIG_DATA_TEST_MODE
	memset(&dcb->test_mode_cfg, 0x00, sizeof(dcb->test_mode_cfg));
	dcb->test_mode_cfg.md_tput_mode = DPMAIF_MD_INVALID_MODE;
#endif

	timer_setup(&dcb->ring_rel_ctrl_timer, mtk_dpmaif_ring_rel_ctrl_timer_func, 0);

	ret = mtk_dpmaif_tput_monitor_init(dcb);
	if (ret < 0) {
		ret = -ENOMEM;
		goto free_dcb;
	}

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	if (dcb->dpmaif_rx_legacy)
		dcb->skb_fifo_ctlb.dcb = dcb;
	mtk_dpmaif_qos_init(dcb);
#endif

	/* interrupt coalesce init */
	dcb->intr_coalesce.rx_coalesced_frames = DPMAIF_DFLT_INTR_RX_COA_FRAMES;
	dcb->intr_coalesce.tx_coalesced_frames = DPMAIF_DFLT_INTR_TX_COA_FRAMES;
	dcb->intr_coalesce.rx_coalesce_usecs = DPMAIF_DFLT_INTR_RX_COA_USECS;
	dcb->intr_coalesce.tx_coalesce_usecs = DPMAIF_DFLT_INTR_TX_COA_USECS;

	dpmaif_debugfs_init(dcb);

	ret = mtk_dpmaif_drv_res_init(dcb);
	if (ret < 0)
		goto debugfs_exit;

	ret = mtk_dpmaif_sw_res_init(dcb);
	if (ret < 0)
		goto drv_res_exit;

	ret = mtk_dpmaif_tx_srvs_init(dcb);
	if (ret < 0)
		goto sw_res_exit;

	ret = mtk_dpmaif_port_cfg(dcb);
	if (ret < 0)
		goto tx_srvs_exit;

	ret = mtk_dpmaif_pm_init(dcb);
	if (ret < 0)
		goto tx_srvs_exit;

	ret = mtk_dpmaif_stats_init(dcb);
	if (ret < 0)
		goto pm_exit;

	ret = mtk_dpmaif_irq_init(dcb);
	if (ret < 0)
		goto stats_exit;

	MTK_INFO(data_blk->mdev, "dpmaif sw init done\n");

	return 0;

stats_exit:
	mtk_dpmaif_stats_exit(dcb);
pm_exit:
	mtk_dpmaif_pm_exit(dcb);
tx_srvs_exit:
	mtk_dpmaif_tx_srvs_exit(dcb);
sw_res_exit:
	mtk_dpmaif_sw_res_exit(dcb);
drv_res_exit:
	mtk_dpmaif_drv_res_exit(dcb);
debugfs_exit:
	dpmaif_debugfs_exit(dcb);
	mtk_dpmaif_tput_monitor_exit(dcb);

free_dcb:
	devm_kfree(DCB_TO_DEV(dcb), dcb);

	return ret;
}

static int mtk_dpmaif_sw_exit(struct mtk_md_dev *mdev)
{
	struct mtk_dpmaif_ctlb *dcb = ((struct mtk_data_blk *)(mdev->data_blk))->dcb;

	if (!dcb) {
		pr_err("Invalid parameter\n");
		return -EINVAL;
	}

	mtk_dpmaif_irq_exit(dcb);
	mtk_dpmaif_stats_exit(dcb);
	mtk_dpmaif_pm_exit(dcb);
	mtk_dpmaif_tx_srvs_exit(dcb);
	mtk_dpmaif_sw_res_exit(dcb);
	mtk_dpmaif_drv_res_exit(dcb);
	dpmaif_debugfs_exit(dcb);
	mtk_dpmaif_tput_monitor_exit(dcb);

	devm_kfree(DCB_TO_DEV(dcb), dcb);
	MTK_INFO(mdev, "dpmaif sw exit done\n");

	return 0;
}

static int mtk_dpmaif_poll_rx_pit(struct dpmaif_rxq *rxq)
{
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	unsigned int sw_rd_idx, hw_wr_idx;
	unsigned int pit_cnt;
	int ret;

	sw_rd_idx = rxq->pit_rd_idx;
	ret = dcb->drv_info->drv_ops->get_ring_idx(dcb->drv_info, DPMAIF_PIT_WIDX, rxq->id);
	if (unlikely(ret < 0)) {
		MTK_ERR(DCB_TO_MDEV(dcb),
			"Failed to read rxq%u hw pit_wr_idx, ret=%d\n", rxq->id, ret);
		mtk_dpmaif_common_err_handle(dcb, true);
		goto out;
	}

	hw_wr_idx = ret;
	pit_cnt = mtk_dpmaif_ring_buf_readable(rxq->pit_cnt, sw_rd_idx, hw_wr_idx);
	rxq->pit_wr_idx = hw_wr_idx;

	return pit_cnt;

out:
	return ret;
}

static int mtk_dpmaif_pit_bid_check(struct dpmaif_rxq *rxq, unsigned int cur_bid)
{
	union dpmaif_bat_record *cur_bat_record;
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	struct dpmaif_bat_ring *bat_ring;

	bat_ring = &rxq->dcb->bat_infos[rxq->bat_ring_id].normal_bat_ring;
	cur_bat_record = bat_ring->sw_record_base + cur_bid;

	if (unlikely(cur_bid >= bat_ring->bat_cnt || !cur_bat_record->normal.skb)) {
		MTK_ERR(DCB_TO_MDEV(dcb),
			"Invalid parameter rxq%u bat%u(%d), bid=%u, bat_cnt=%u\n",
			rxq->id, bat_ring->id, bat_ring->type, cur_bid, bat_ring->bat_cnt);

		return -DATA_FLOW_CHK_ERR;
	}

	rxq->pit_bid = cur_bid;

	return 0;
}

static int mtk_dpmaif_rx_set_data_to_skb(struct dpmaif_rxq *rxq, struct dpmaif_pd_pit *pit_info,
					 struct dpmaif_rx_record *rx_record)
{
	struct dpmaif_bat_ring *bat_ring = &rxq->dcb->bat_infos[rxq->bat_ring_id].normal_bat_ring;
	unsigned int data_len, hd_offset, data_offset;
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	unsigned long long data_dma_base_addr;
	union dpmaif_bat_record *bat_record;
	struct sk_buff *new_skb;

	bat_record = bat_ring->sw_record_base + rxq->rx_info->pit_pd_cur_bid;
	new_skb = bat_record->normal.skb;
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	prefetchw(new_skb);
#endif
	data_dma_base_addr = (unsigned long long)bat_record->normal.data_dma_addr;

	if (dcb->dpmaif_rx_legacy)
		dma_unmap_single(DCB_TO_DEV(dcb), bat_record->normal.data_dma_addr,
				 bat_record->normal.data_len, DMA_FROM_DEVICE);
	else
#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_FAST_DMA_SYNC)
		dpmaif_google_dma_sync_fast(DCB_TO_DEV(dcb),
					    bat_record->normal.data_dma_addr,
					    bat_record->normal.data_len,
					    DMA_FROM_DEVICE,
					    virt_to_page(new_skb->data),
					    offset_in_page(new_skb->data));
#else
		dma_sync_single_for_cpu(DCB_TO_DEV(dcb), bat_record->normal.data_dma_addr,
					bat_record->normal.data_len, DMA_FROM_DEVICE);
#endif /* CONFIG_GOOGLE_MODEM_DATA_FAST_DMA_SYNC */

	/* Calculate data address and data length. */
	data_offset = rxq->rx_info->pit_pd_dma_addr - data_dma_base_addr;
	data_len = rxq->rx_info->pit_pd_data_len;

	/* Only the header_offset of the first packet of lro skb is zero,
	 * and other packet's header_offset is not zero.
	 * The data_len is the packet len that has subtracted the packet header length.
	 */
	hd_offset = rxq->rx_info->pit_pd_hd_offset;

	/* Check the LRO packet must have same header offset. */
	if (rx_record->lro_pkt_cnt > 1) {
		if (unlikely(hd_offset != rx_record->hd_offset))
			goto err_rx_flow;
	} else {
		rx_record->hd_offset = hd_offset;
	}

	/* Check and rebuild skb. */
	new_skb->len = 0;
	if (unlikely(data_offset > bat_record->normal.data_len ||
		     data_offset + hd_offset + data_len > bat_record->normal.data_len))
		goto err_rx_flow;

	skb_put(new_skb, data_offset + hd_offset + data_len);
	skb_pull(new_skb, data_offset);

	__skb_queue_tail(&rx_record->rx_list, new_skb);
	rx_record->cur_skb = new_skb;
	rx_record->lro_pkt_cnt++;

	bat_record->normal.skb = NULL;

	return 0;

err_rx_flow:
	MTK_ERR(DCB_TO_MDEV(dcb),
		"Invalid packet(%u/%u):data_len=%u,hd_offset=%u,offset=0x%llx-0x%llx,skb(%llx,%llx,%u,%u)\n",
		rxq->pit_rd_idx, rxq->rx_info->pit_pd_cur_bid, data_len, hd_offset,
		rxq->rx_info->pit_pd_dma_addr, data_dma_base_addr, (u64)new_skb->head,
		(u64)new_skb->data, (unsigned int)new_skb->tail,
		(unsigned int)new_skb->end);

	dpmaif_dump_rxq_pit_info(rxq, 2);

	return -DATA_FLOW_CHK_ERR;
}

static void mtk_dpmaif_bat_ring_set_mask(struct mtk_dpmaif_ctlb *dcb, enum dpmaif_bat_type type,
					 unsigned int bat_idx, int bat_ring_id)
{
	struct dpmaif_bat_ring *bat_ring;

	if (type == NORMAL_BAT)
		bat_ring = &dcb->bat_infos[bat_ring_id].normal_bat_ring;
	else
		bat_ring = &dcb->bat_infos[bat_ring_id].frag_bat_ring;

	set_bit(bat_idx, bat_ring->mask_tbl);

	atomic_inc(&bat_ring->bat_stats);
	atomic_inc(&bat_ring->to_reload_cnt);
}

static void mtk_dpmaif_lro_add_skb(struct dpmaif_rxq *rxq, struct dpmaif_rx_record *rx_record,
				   struct sk_buff *parent)
{
	unsigned int parent_len = parent->len;
	struct sk_buff *last = NULL;
	struct sk_buff *cur_skb;
#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_LRO_SIZE_LIMIT)
	unsigned int lro_limit = dpmaif_google_get_lro_limit();
	unsigned int total_len;
#endif

	while (!skb_queue_empty(&rx_record->rx_list)) {
		cur_skb = __skb_dequeue(&rx_record->rx_list);
#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_LRO_SIZE_LIMIT)
		total_len = parent->len + cur_skb->len;

		if (cur_skb->len <= parent_len && total_len <= lro_limit) {
#else
		if (cur_skb->len <= parent_len) {
#endif
			skb_pull(cur_skb, rx_record->hd_offset);

			if (last) {
				last->next = cur_skb;
				skb_shinfo(parent)->gso_segs++;
			} else {
				skb_shinfo(parent)->frag_list = cur_skb;
				skb_shinfo(parent)->gso_size = parent->len - rx_record->hd_offset;
				skb_shinfo(parent)->gso_segs = 2;
			}

			/* Update the len, data_len, truesize of the lro skb. */
			parent->len += cur_skb->len;
			parent->data_len += cur_skb->len;
			parent->truesize += cur_skb->truesize;

			last = cur_skb;

			if (cur_skb->len < skb_shinfo(parent)->gso_size)
				break;
		} else {
			__skb_queue_head(&rx_record->rx_list, cur_skb);
			break;
		}
	}
}

static int mtk_dpmaif_get_rx_pkt(struct dpmaif_rxq *rxq, struct dpmaif_pd_pit *pit_info,
				 struct dpmaif_rx_record *rx_record)
{
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	int ret;

	/* Check the bid in pit information, don't exceed bat size. */
	ret = mtk_dpmaif_pit_bid_check(rxq, rxq->rx_info->pit_pd_cur_bid);
	if (unlikely(ret < 0))
		goto out;

	/* Receive data from bat and save to rx_record. */
	ret = mtk_dpmaif_rx_set_data_to_skb(rxq, pit_info, rx_record);
	if (unlikely(ret < 0))
		goto out;

	/* Make sure that setting skb_info->skb to NULL is completed
	 * before setting the corresponding bat mask.
	 */
	smp_wmb();

	/* Set bat mask that have been received. */
	mtk_dpmaif_bat_ring_set_mask(dcb, NORMAL_BAT,
				     rxq->rx_info->pit_pd_cur_bid, rxq->bat_ring_id);

	return 0;

out:
	return ret;
}

static int mtk_dpmaif_pit_bid_frag_check(struct dpmaif_rxq *rxq, unsigned int cur_bid)
{
	union dpmaif_bat_record *cur_bat_record;
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	struct dpmaif_bat_ring *bat_ring;

	bat_ring = &rxq->dcb->bat_infos[rxq->bat_ring_id].frag_bat_ring;
	cur_bat_record = bat_ring->sw_record_base + cur_bid;
	if (unlikely(cur_bid >= bat_ring->bat_cnt || !cur_bat_record->frag.page)) {
		MTK_ERR(DCB_TO_MDEV(dcb),
			"Invalid parameter rxq%u bat%u(%d), bid=%u, bat_cnt=%u\n",
			rxq->id, bat_ring->id, bat_ring->type, cur_bid, bat_ring->bat_cnt);
		return -DATA_FLOW_CHK_ERR;
	}

	return 0;
}

static int mtk_dpmaif_rx_set_frag_to_skb(struct dpmaif_rxq *rxq, struct dpmaif_pd_pit *pit_info,
					 struct dpmaif_rx_record *rx_record)
{
	struct dpmaif_bat_ring *bat_ring = &rxq->dcb->bat_infos[rxq->bat_ring_id].frag_bat_ring;
	unsigned int page_offset, data_len, data_offset;
	struct sk_buff *base_skb = rx_record->cur_skb;
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	unsigned long long data_dma_base_addr;
	union dpmaif_bat_record *bat_record;
	struct page_mapped_t *cur_frag;
	struct page *page;

	bat_record = bat_ring->sw_record_base + rxq->rx_info->pit_pd_cur_bid;
	cur_frag = &bat_record->frag;
	page = cur_frag->page;
	page_offset = cur_frag->offset;
	data_dma_base_addr = (unsigned long long)cur_frag->data_dma_addr;

	if (dcb->dpmaif_rx_legacy)
		dma_unmap_page(DCB_TO_DEV(dcb), cur_frag->data_dma_addr,
			       cur_frag->data_len, DMA_FROM_DEVICE);
	else
#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_FAST_DMA_SYNC)
		dpmaif_google_dma_sync_fast(DCB_TO_DEV(dcb),
					    cur_frag->data_dma_addr,
					    cur_frag->data_len,
					    DMA_FROM_DEVICE,
					    page,
					    page_offset);
#else
		dma_sync_single_for_cpu(DCB_TO_DEV(dcb), cur_frag->data_dma_addr,
					cur_frag->data_len, DMA_FROM_DEVICE);
#endif /* CONFIG_GOOGLE_MODEM_DATA_FAST_DMA_SYNC */

	/* Calculate data address and data length. */
	data_offset = rxq->rx_info->pit_pd_dma_addr - data_dma_base_addr;
	data_len = rxq->rx_info->pit_pd_data_len;
	if (unlikely(data_offset > cur_frag->data_len ||
		     (data_len + data_offset) > cur_frag->data_len ||
		     skb_shinfo(base_skb)->nr_frags >= MAX_SKB_FRAGS)) {
		MTK_ERR(DCB_TO_MDEV(dcb),
			"Invalid frag(%u/%u):data_len=%u, nr_frags=%u\n",
			rxq->pit_rd_idx, rxq->rx_info->pit_pd_cur_bid,
			data_len, skb_shinfo(base_skb)->nr_frags);

		dpmaif_dump_rxq_pit_info(rxq, 2);
		return -DATA_FLOW_CHK_ERR;
	}

	/* Add fragment data to cur_skb->frags[]. */
	skb_add_rx_frag(base_skb, skb_shinfo(base_skb)->nr_frags, page,
			page_offset + data_offset, data_len, cur_frag->data_len);

	cur_frag->page = NULL;

	return 0;
}

static int mtk_dpmaif_get_rx_frag(struct dpmaif_rxq *rxq, struct dpmaif_pd_pit *pit_info,
				  struct dpmaif_rx_record *rx_record)
{
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	int ret;

	/* Check the bid in pit information, don't exceed frag bat size. */
	ret = mtk_dpmaif_pit_bid_frag_check(rxq, rxq->rx_info->pit_pd_cur_bid);
	if (unlikely(ret < 0))
		goto out;

	/* Receive data from frag bat and save to currunt skb. */
	ret = mtk_dpmaif_rx_set_frag_to_skb(rxq, pit_info, rx_record);
	if (unlikely(ret < 0))
		goto out;

	/* Make sure that setting page_info->page to NULL is completed
	 * before setting the corresponding frag bat mask.
	 */
	smp_wmb();

	/* Set bat mask that have been received. */
	mtk_dpmaif_bat_ring_set_mask(dcb, FRAG_BAT,
				     rxq->rx_info->pit_pd_cur_bid, rxq->bat_ring_id);

out:
	return ret;
}

static int mtk_dpmaif_update_rx_skb_info(struct sk_buff *skb,
					 struct dpmaif_rxq *rxq, struct dpmaif_rx_record *rx_record)
{
	union mtk_data_pkt_info *pkt_info = DATA_SKB_CB(skb);
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	unsigned int gso_type = 0;
	unsigned int payload_len;
	unsigned short l4_proto;
	struct tcphdr *tcphdr;
	struct ipv6hdr *ip6h;
	struct iphdr *iph;
	int inner_offset;
	__be16 frag_off;
	u8 packet_type;
	__wsum csum;
	u8 nexthdr;
	u32 id;

	skb_reset_network_header(skb);
	skb_reset_mac_header(skb);

	skb_set_hash(skb, rxq->rx_info->pit_msg_hash, PKT_HASH_TYPE_L4);
	skb_record_rx_queue(skb, rxq->id);

	/* 1.Handle forwarding scenarios when LRO is enable.
	 * 2.Handle the scenario where UDP LRO is available
	 * but the application layer does not support packet
	 * gathering.
	 */

	if ((dcb->features & DATA_HW_F_HPC) && rxq->rx_info->pit_msg_checksum == CS_RESULT_PASS) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		if (rxq->rx_info->pit_msg_pro == DPMAIF_PIT_TCP) {
			if (rxq->rx_info->pit_msg_ip == DPMAIF_PIT_IPV4) {
				skb->protocol = htons(ETH_P_IP);
				gso_type = SKB_GSO_TCPV4;
			} else {
				skb->protocol = htons(ETH_P_IPV6);
				gso_type = SKB_GSO_TCPV6;
			}
			rx_record->ip_protocol = IPPROTO_TCP;
		} else if (rxq->rx_info->pit_msg_pro == DPMAIF_PIT_UDP) {
#ifdef CONFIG_MTK_DATA_FEATURE_TEST
			gso_type = SKB_GSO_UDP_L4;
#endif
			rx_record->ip_protocol = IPPROTO_UDP;
			if (rxq->rx_info->pit_msg_ip == DPMAIF_PIT_IPV4)
				skb->protocol = htons(ETH_P_IP);
			else
				skb->protocol = htons(ETH_P_IPV6);
		} else {
			MTK_ERR(DCB_TO_MDEV(dcb), "Invalid packet type, value=%u\n",
				rxq->rx_info->pit_msg_pro);
			goto out;
		}
	} else {
		packet_type = skb->data[0] & 0xF0;
		skb->ip_summed = (rxq->rx_info->pit_msg_checksum == CS_RESULT_PASS) ?
			CHECKSUM_UNNECESSARY : CHECKSUM_NONE;
		if (packet_type == IPV4_VERSION) {
			skb->protocol = htons(ETH_P_IP);
			l4_proto = ((struct iphdr *)skb->data)->protocol;
			rx_record->ip_protocol = l4_proto;
			if (l4_proto == IPPROTO_TCP)
				gso_type = SKB_GSO_TCPV4;
#ifdef CONFIG_MTK_DATA_FEATURE_TEST
			else if (l4_proto == IPPROTO_UDP)
				gso_type = SKB_GSO_UDP_L4;
#endif
			else
				goto out;

		} else if (packet_type == IPV6_VERSION) {
			skb->protocol = htons(ETH_P_IPV6);
			nexthdr = ((struct ipv6hdr *)skb->data)->nexthdr;
			/* Now skip over extension headers. */
			inner_offset = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr),
							&nexthdr, &frag_off);
			if (unlikely(inner_offset < 0))
				goto out;

			rx_record->ip_protocol = nexthdr;
			if (nexthdr == IPPROTO_TCP)
				gso_type = SKB_GSO_TCPV6;
#ifdef CONFIG_MTK_DATA_FEATURE_TEST
			else if (nexthdr == IPPROTO_UDP)
				gso_type = SKB_GSO_UDP_L4;
#endif
			else
				goto out;

		} else {
			MTK_ERR(DCB_TO_MDEV(dcb), "Invalid packet type, value=%hhu\n",
				packet_type);
			goto free_skb;
		}
	}

	if (rx_record->lro_pkt_cnt > 1) {
		if (skb_shinfo(skb)->gso_segs) {
			skb_shinfo(skb)->gso_type |= gso_type;

#ifdef CONFIG_MTK_DATA_FEATURE_TEST
			if (rx_record->ip_protocol == IPPROTO_UDP) {
				skb->csum_level = 1;
				dcb->traffic_stats.dpmaif_rx.rx_lro_udp_pkt[rxq->id]++;
			} else if (rx_record->ip_protocol == IPPROTO_TCP) {
				dcb->traffic_stats.dpmaif_rx.rx_lro_tcp_pkt[rxq->id]++;
			}
#else
			if (rx_record->ip_protocol == IPPROTO_UDP) {
				dcb->traffic_stats.dpmaif_rx.rx_lro_udp_pkt[rxq->id]++;
				WARN_ON_ONCE(true);
				goto free_skb;
			} else if (rx_record->ip_protocol == IPPROTO_TCP) {
				dcb->traffic_stats.dpmaif_rx.rx_lro_tcp_pkt[rxq->id]++;
			}
#endif
		}

		if (unlikely(skb_shinfo(skb)->gso_segs != rx_record->lro_pkt_cnt &&
			     rx_record->ip_protocol == IPPROTO_TCP)) {
			if (skb->protocol == htons(ETH_P_IP)) {
				iph = (struct iphdr *)skb->data;
				iph->tot_len = htons(skb->len);
				iph->check = 0;
				iph->check = ip_fast_csum((const void *)iph, iph->ihl);

				inner_offset = iph->ihl << 2;
				payload_len = skb->len - inner_offset;
				if (unlikely(inner_offset > skb->len ||
					     payload_len < sizeof(*tcphdr)))
					goto out;

				tcphdr = (struct tcphdr *)(skb->data + inner_offset);
				tcphdr->check = 0;
				csum = skb_checksum(skb, inner_offset, payload_len, 0);
				tcphdr->check = csum_tcpudp_magic(iph->saddr, iph->daddr,
								  payload_len, IPPROTO_TCP, csum);
			} else if (skb->protocol == htons(ETH_P_IPV6)) {
				ip6h = (struct ipv6hdr *)skb->data;
				ip6h->payload_len = htons(skb->len - sizeof(*ip6h));

				nexthdr = ((struct ipv6hdr *)skb->data)->nexthdr;
				inner_offset = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr),
								&nexthdr, &frag_off);
				if (unlikely(inner_offset < 0 || inner_offset > skb->len))
					goto out;

				payload_len = skb->len - inner_offset;
				if (unlikely(payload_len < sizeof(*tcphdr)))
					goto out;

				tcphdr = (struct tcphdr *)(skb->data + inner_offset);
				tcphdr->check = 0;
				csum = skb_checksum(skb, inner_offset, payload_len, 0);
				tcphdr->check = csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr,
								payload_len, IPPROTO_TCP, csum);
			}
		}
	}

out:
	id = mtk_data_get_pkt_id(skb);
	MTK_DBG_DATA_RX_PKT_INFO(DCB_TO_MDEV(dcb), rxq->id, skb_headlen(skb),
				 skb->data_len, rx_record->lro_pkt_cnt,
				 skb_shinfo(skb)->gso_segs, rx_record->ip_protocol, id);

	trace_mtk_tput_data_lro_cnt(rxq->id, rx_record->lro_pkt_cnt,
				    skb_shinfo(skb)->gso_segs, id);

	pkt_info->rx.ch_id = rxq->rx_info->pit_msg_chnl_id;
	pkt_info->rx.q_id = rxq->id;
	pkt_info->rx.id = id;

	return 0;

free_skb:
	dcb->traffic_stats.dpmaif_rx.rx_errors[rxq->id]++;
	dcb->traffic_stats.dpmaif_rx.rx_dropped[rxq->id]++;
	trace_mtk_data_event_stats(DPMAIF_RX_DROPPED, rxq->id,
				   dcb->traffic_stats.dpmaif_rx.rx_dropped[rxq->id]);
	dev_kfree_skb_any(skb);

	return -DATA_HW_UNK_PKT;
}

static int mtk_dpmaif_rx_skb(struct dpmaif_rxq *rxq, struct dpmaif_rx_record *rx_record)
{
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	struct sk_buff *new_skb;
	int ret;

	if (unlikely(rxq->rx_info->pit_msg_dp || rxq->rx_info->pit_msg_err)) {
		dcb->traffic_stats.dpmaif_rx.rx_hw_ind_dropped[rxq->id]++;
		__skb_queue_purge(&rx_record->rx_list);
		trace_mtk_data_event_stats(DPMAIF_RX_HW_IND_DROPPED, rxq->id,
					   dcb->traffic_stats.dpmaif_rx.rx_hw_ind_dropped[rxq->id]);
		return 0;
	}

	do {
		/* Logically, the rx_list should contain at least one skb. */
		new_skb = __skb_dequeue(&rx_record->rx_list);
		mtk_dpmaif_lro_add_skb(rxq, rx_record, new_skb);
		mtk_dpmaif_dl_stats_update(rxq, new_skb->len);
		ret = mtk_dpmaif_update_rx_skb_info(new_skb, rxq, rx_record);
		if (unlikely(ret < 0))
			continue;

#ifdef CONFIG_DEBUG_FS
		if (test_bit(DPMAIF_DUMP_RX_PKT, &dcb->dump_flag))
			mtk_dpmaif_skb_dump(dcb, new_skb, MTK_DATA_RX_MEMLOG_RG(rxq->id),
					    rx_record->ip_protocol);
#endif

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
		if (rxq->attr & DPMAIFQ_ATTR_LOW_LATENCY) {
			mtk_wwan_recv(dcb->data_blk, new_skb);
		} else {
			mtk_dpmaif_dl_enqueue(rxq, new_skb);
			queue_work_on(rxq->cpu_id, rxq->steer_wq, &rxq->steer_work);
		}
#else
		/* Send skb to data port. */
		mtk_wwan_recv(dcb->data_blk, new_skb);
#endif
		dcb->traffic_stats.dpmaif_rx.rx_pkt[rxq->id]++;
	} while (!skb_queue_empty(&rx_record->rx_list));

	return ret;
}

static void mtk_dpmaif_recycle_pit_internal(struct dpmaif_rxq *rxq, unsigned short pit_rel_cnt)
{
	unsigned short old_sw_rel_idx, new_sw_rel_idx, old_hw_wr_idx;
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;

	MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_DATA_RX_MEMLOG_RG(rxq->id),
		"pit(%u): w=%u,r=%u,rel=%u,rel_cnt=%u\n",
		 rxq->id, rxq->pit_wr_idx, rxq->pit_rd_idx, rxq->pit_rel_rd_idx, pit_rel_cnt);

	trace_mtk_tput_data_pit(rxq->id, rxq->pit_rel_rd_idx,
				rxq->pit_wr_idx, rxq->pit_rd_idx, pit_rel_cnt);

	old_sw_rel_idx = rxq->pit_rel_rd_idx;
	new_sw_rel_idx = old_sw_rel_idx + pit_rel_cnt;
	old_hw_wr_idx = rxq->pit_wr_idx;

	/* pit_rel_rd_idx should not exceed pit_wr_idx. */
	if (old_hw_wr_idx > old_sw_rel_idx) {
		if (new_sw_rel_idx > old_hw_wr_idx)
			MTK_WARN(DCB_TO_MDEV(dcb), "new_rel_idx=%hu > old_hw_wr_idx=%hu\n",
				 new_sw_rel_idx, old_hw_wr_idx);
	} else if (old_hw_wr_idx < old_sw_rel_idx) {
		if (new_sw_rel_idx >= rxq->pit_cnt) {
			new_sw_rel_idx = new_sw_rel_idx - rxq->pit_cnt;
			if (new_sw_rel_idx > old_hw_wr_idx)
				MTK_WARN(DCB_TO_MDEV(dcb), "new_rel_idx=%hu > old_wr_idx=%hu\n",
					 new_sw_rel_idx, old_hw_wr_idx);
		}
	}

	atomic_add(pit_rel_cnt, &rxq->pit_rel_cnt);
	rxq->pit_rel_rd_idx = new_sw_rel_idx;

	if (atomic_read(&rxq->pit_rel_cnt) >= rxq->pit_burst_rel_cnt)
		mtk_dpmaif_book_doorbell_work(dcb, 0);
}

static int mtk_dpmaif_recycle_rx_ring(struct dpmaif_rxq *rxq)
{
	unsigned int pit_rel_cnt;

	pit_rel_cnt = mtk_dpmaif_ring_buf_releasable(rxq->pit_cnt,
						     rxq->pit_rel_rd_idx,
						     rxq->pit_rd_idx);

	if (unlikely(pit_rel_cnt > rxq->pit_cnt)) {
		MTK_ERR(DCB_TO_MDEV(rxq->dcb),
			"Invalid rxq%u pit release count, %u>%u\n",
			rxq->id, pit_rel_cnt, rxq->pit_cnt);
		mtk_dpmaif_common_err_handle(rxq->dcb, false);
		return -DATA_FLOW_CHK_ERR;
	}

	mtk_dpmaif_recycle_pit_internal(rxq, pit_rel_cnt);

	mtk_dpmaif_task_wakeup(&rxq->dcb->bat_infos[rxq->bat_ring_id].task_ctlb);

	return 0;
}

static void rxq_pit_cache_memory_flush(struct dpmaif_rxq *rxq,
				       unsigned short cnt)
{
	unsigned int cur_pit = rxq->pit_rd_idx;
	dma_addr_t cache_start_addr;

	/* flush pit base memory cache for read pit data */
	cache_start_addr = rxq->pit_dma_addr + (sizeof(*rxq->pit_base) * cur_pit);

	if ((cur_pit + cnt) <= rxq->pit_cnt) {
		dma_sync_single_for_cpu(DCB_TO_DEV(rxq->dcb), cache_start_addr,
					sizeof(*rxq->pit_base) * cnt, DMA_FROM_DEVICE);

	} else {
		dma_sync_single_for_cpu(DCB_TO_DEV(rxq->dcb), cache_start_addr,
					sizeof(*rxq->pit_base) * (rxq->pit_cnt - cur_pit),
			DMA_FROM_DEVICE);

		dma_sync_single_for_cpu(DCB_TO_DEV(rxq->dcb), rxq->pit_dma_addr,
					sizeof(*rxq->pit_base) * (cur_pit + cnt - rxq->pit_cnt),
			DMA_FROM_DEVICE);
	}
}

#define DPMAIF_PIT_SEQ_CHECK_FAIL_CNT 2500

static int mtk_dpmaif_rx_data_collect_internal(struct dpmaif_rxq *rxq, int pit_cnt,
					       unsigned int *pkt_cnt)
{
	unsigned long time_limit = jiffies + msecs_to_jiffies(2);
	struct dpmaif_rx_record *rx_record = &rxq->rx_record;
	unsigned int recv_pkt_cnt = 0, pit_rd_cnt = 0;
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	struct dpmaif_pd_pit *pit_info;
	struct dpmaif_rx_info rx_info;
	unsigned int rx_cnt, cur_pit;
	int ret = 0;

	if (rxq->attr & DPMAIFQ_ATTR_PIT_CACHED)
		rxq_pit_cache_memory_flush(rxq, pit_cnt);

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	prefetch(rxq);
#endif
	cur_pit = rxq->pit_rd_idx;
	for (rx_cnt = 0; rx_cnt < pit_cnt; rx_cnt++) {
		if (!rx_record->msg_pit_recv && time_after_eq(jiffies, time_limit)) {
			ret = -DATA_DL_ONCE_MORE;
			break;
		}

		/* Pit sequence check. */
		pit_info = rxq->pit_base + cur_pit;
		ret = dcb->drv_info->drv_ops->get_rx_info(pit_info, &rx_info,
						rxq->pit_seq_expect, rxq->id);
		if (likely(!ret)) {
			rxq->pit_seq_expect++;
			if (rxq->pit_seq_expect >= rxq->pit_seq_max)
				rxq->pit_seq_expect = 0;

			rxq->pit_seq_fail_cnt = 0;
		} else {
			MTK_INFO_RATELIMITED(DCB_TO_MDEV(rxq->dcb),
					     "Failed to check rxq%u pit seq, cur_seq(%u) != exp_seq(%u)\n",
					rxq->id, rx_info.pit_pd_seq, rxq->pit_seq_expect);

			rxq->pit_seq_fail_cnt++;
			if (rxq->pit_seq_fail_cnt >= DPMAIF_PIT_SEQ_CHECK_FAIL_CNT) {
				rxq->pit_seq_fail_cnt = 0;
				return -DATA_FLOW_CHK_ERR;
			}
			break;
		}
		rxq->rx_info = &rx_info;

		/* Parse message pit. */
		if (rx_info.msg_pit) {
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
			prefetchw(rx_record);
#endif
			if (unlikely(rx_record->msg_pit_recv)) {
				if (rx_record->lro_pkt_cnt) {
					dcb->traffic_stats.dpmaif_rx.rx_errors[rxq->id]++;
					dcb->traffic_stats.dpmaif_rx.rx_dropped[rxq->id]++;
					__skb_queue_purge(&rx_record->rx_list);
				}
				memset(rx_record, 0x00, sizeof(*rx_record));
				__skb_queue_head_init(&rx_record->rx_list);
				MTK_ERR(DCB_TO_MDEV(dcb),
					"Invalid pit, rxq%u two continuous message pit\n", rxq->id);
				return -DATA_FLOW_CHK_ERR;
			}

			rx_record->msg_pit_recv = true;
			MTK_DBG_DATA_RX_MSG_INFO(DCB_TO_MDEV(rxq->dcb), rxq->id,
						 rx_info.pit_msg_chnl_id, rx_info.pit_msg_checksum,
						 rx_info.pit_msg_dp, rx_info.pit_msg_err);

		} else {
			/* Parse normal pit or frag pit. */
			if (!rx_info.normal_bat) {
				ret = mtk_dpmaif_get_rx_pkt(rxq, pit_info, rx_record);
			} else {
				/* Pit sequence: normal pit + frag pit. */
				if (likely(rx_record->cur_skb)) {
					ret = mtk_dpmaif_get_rx_frag(rxq, pit_info, rx_record);
				} else {
					/* Unexpected pit sequence: message pit + frag pit. */
					MTK_WARN(DCB_TO_MDEV(dcb),
						 "unexpected rxq%u frag pit, pit=%u,bid=%u; rx_cnt=%u, pit_cnt=%u\n",
						 rxq->id, cur_pit,
						 rx_info.pit_pd_cur_bid,
						 rx_cnt, pit_cnt);
					ret = -DATA_FLOW_CHK_ERR;
				}
			}

			if (unlikely(ret < 0)) {
				__skb_queue_purge(&rx_record->rx_list);

				dcb->traffic_stats.dpmaif_rx.rx_errors[rxq->id]++;
				MTK_ERR(DCB_TO_MDEV(dcb),
					"Invalid packet, error payload, rxq%u pit_idx=%d\n",
					 rxq->id, cur_pit);
				return ret;
			}

			/* Last one pit of a packet. */
			if (!rx_info.pit_continue) {
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
				prefetchw(rx_record);
#endif
				mtk_dpmaif_rx_skb(rxq, rx_record);
				memset(rx_record, 0x00, sizeof(*rx_record));
				__skb_queue_head_init(&rx_record->rx_list);
				recv_pkt_cnt++;
			}
		}

		cur_pit = mtk_dpmaif_ring_buf_get_next_idx(rxq->pit_cnt, cur_pit);
		rxq->pit_rd_idx = cur_pit;

		pit_rd_cnt++;
		/* Recycle pit/bat in batches (rx packet budget). */
		if (pit_rd_cnt == recycle_rx_ring_th) {
			atomic_add(recycle_rx_ring_th, &rxq->pit_stats);
			mtk_dpmaif_recycle_rx_ring(rxq);
			pit_rd_cnt = 0;
		}
	}

	if (pit_rd_cnt) {
		atomic_add(pit_rd_cnt, &rxq->pit_stats);
		mtk_dpmaif_recycle_rx_ring(rxq);
	}

	*pkt_cnt = recv_pkt_cnt;

	return ret;
}

static int mtk_dpmaif_rx_data_collect(struct dpmaif_rxq *rxq, unsigned int *pkt_cnt)
{
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	unsigned int pit_cnt;
	int ret = 0;

	pit_cnt = mtk_dpmaif_ring_buf_readable(rxq->pit_cnt, rxq->pit_rd_idx, rxq->pit_wr_idx);
	/* only enable rxq->pit_poll_enable will polling pit */
	if (rxq->pit_poll_enable && !pit_cnt) {
		ret = mtk_dpmaif_poll_rx_pit(rxq);
		if (unlikely(ret < 0))
			return ret;

		pit_cnt = ret;
	}

	trace_mtk_tput_data_napi_per_poll(rxq->id, pit_cnt);

	/* Collect rx packets. */
	if (likely(pit_cnt > 0)) {
		ret = mtk_dpmaif_rx_data_collect_internal(rxq, pit_cnt, pkt_cnt);
		if (ret <= -DATA_DL_ONCE_MORE) {
			ret = -DATA_DL_ONCE_MORE;
		} else if (ret <= -DATA_ERR_STOP_MAX) {
			ret = -DATA_ERR_STOP_MAX;
			mtk_dpmaif_common_err_handle(dcb, true);
		} else {
			ret = 0;
		}
	}

	return ret;
}

static int mtk_dpmaif_rx_data_collect_more(struct dpmaif_rxq *rxq, int *work_done)
{
	unsigned long time_limit = jiffies + msecs_to_jiffies(2);
	unsigned int total_pkt_cnt = 0, pkt_cnt;
	int ret = 0;

	do {
		if (time_after_eq(jiffies, time_limit)) {
			ret = -DATA_DL_ONCE_MORE;
			break;
		}

		pkt_cnt = 0;
		ret = mtk_dpmaif_rx_data_collect(rxq, &pkt_cnt);
		total_pkt_cnt += pkt_cnt;
		if (ret < 0)
			break;
	} while (pkt_cnt > 0 && rxq->started);

	*work_done = total_pkt_cnt;

	return ret;
}

static int mtk_dpmaif_rx_napi_poll(struct napi_struct *napi, int budget)
{
	struct dpmaif_rxq *rxq = container_of(napi, struct dpmaif_rxq, napi);
	struct dpmaif_traffic_stats *stats = &rxq->dcb->traffic_stats;
	struct mtk_dpmaif_ctlb *dcb = rxq->dcb;
	int work_done = 0;
	int ret;

	mtk_pm_runtime_get(DCB_TO_MDEV(dcb), MTK_USER_DATA, false);
	if (likely(rxq->started)) {
		if (rxq->pit_poll_enable)
			ret = mtk_dpmaif_rx_data_collect_more(rxq, &work_done);
		else
			ret = mtk_dpmaif_rx_data_collect(rxq, &work_done);

		stats->dpmaif_rx.rx_done_last_cnt[rxq->id] += work_done;
		mtk_stats_chk_and_proc(DCB_TO_MDEV(dcb), BIT(dcb->stats_rx_id));

		if (ret == -DATA_DL_ONCE_MORE) {
			napi_gro_flush(napi, false);
			work_done = budget;
			trace_mtk_tput_data_napi(rxq->id, NAPI_RESCH,
						 stats->dpmaif_rx.rx_done_last_cnt[rxq->id]);
		} else {
			if (unlikely(ret == -DATA_ERR_STOP_MAX))
				rxq->started = false;
			if (work_done > budget)
				work_done = budget - 1;
		}
	}

	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		__pm_wakeup_event(rxq->ws, jiffies_to_msecs(HZ));
		mtk_dpmaif_drv_intr_complete(dcb->drv_info, DPMAIF_INTR_DL_DONE, rxq->id, 0);
		trace_mtk_tput_data_napi(rxq->id, NAPI_DONE,
					 stats->dpmaif_rx.rx_done_last_cnt[rxq->id]);
		MTK_DBG(DCB_TO_MDEV(dcb), MTK_DBG_DPMF, MTK_DATA_RX_MEMLOG_RG(rxq->id),
			"pit(%u): w=%u,r=%u,rel=%u\n",
			rxq->id, rxq->pit_wr_idx, rxq->pit_rd_idx, rxq->pit_rel_rd_idx);
	}

	mtk_pm_runtime_put(DCB_TO_MDEV(dcb), MTK_USER_DATA, false);

	return work_done;
}

static int mtk_dpmaif_select_txq(struct mtk_data_blk *data_blk,
				 struct sk_buff *skb, enum mtk_data_pkt_prio pkt_prio)
{
	struct mtk_dpmaif_ctlb *dcb = data_blk->dcb;
	struct dpmaif_drv_pkt_info pkt_info = {0};

	pkt_info.prio = pkt_prio;
	pkt_info.skb_hash = skb_get_hash(skb);

	return dcb->drv_info->drv_ops->feature_cmd(dcb->drv_info,
		DATA_HW_TXQ_GET, (void *)&pkt_info);
}

static int mtk_dpmaif_send_pkt(struct mtk_dpmaif_ctlb *dcb, struct sk_buff *skb)
{
	union mtk_data_pkt_info *pkt_info = DATA_SKB_CB(skb);
	unsigned long long tx_sw_full_cnt;
	u32 id = DATA_SKB_CB(skb)->tx.id;
	struct dpmaif_txq *txq;
	struct dpmaif_vq *vq;
	unsigned char vq_id;
	int ret = 0;

	vq_id = pkt_info->tx.q_id;
	pkt_info->tx.cnt = DPMAIF_GET_DRB_CNT(skb);
	txq = &dcb->txqs[vq_id];

	if (txq->attr & DPMAIFQ_ATTR_LOW_LATENCY) {
		ret = mtk_dpmaif_tx_update_ring_direct(dcb, skb, vq_id);
		if (likely(!ret))
			mtk_dpmaif_book_doorbell_work(dcb, 0);
	} else {
		vq = &dcb->tx_vqs[vq_id];
		if (likely(skb_queue_len(&vq->list) < vq->max_len)) {
			skb_queue_tail(&vq->list, skb);
		} else {
			/* data port should carry off the net device tx queue. */
			mtk_wwan_notify(dcb->data_blk, DATA_EVT_TX_STOP, (u64)1 << vq_id);
			tx_sw_full_cnt = ++txq->dcb->traffic_stats.dpmaif_tx.tx_sw_full[vq_id];
			MTK_INFO_RATELIMITED(DCB_TO_MDEV(dcb), "wwan%u: pkt tx vq%u full\n",
					     pkt_info->tx.intf_id, vq_id);
			trace_mtk_data_event_stats(DPMAIF_TX_SW_FULL, vq_id, tx_sw_full_cnt);
			ret = -EBUSY;
		}

		wake_up(&dcb->tx_srvs[vq->srv_id].wait);
	}

	trace_mtk_tput_data_tx(txq->id, "2", id);
	return ret;
}

static int mtk_dpmaif_send_cmd_sw(struct mtk_dpmaif_ctlb *dcb, struct mtk_data_cmd *cmd_info)
{
	struct dpmaif_drv_info *drv_info = dcb->drv_info;
	int ret = 0;

	switch (cmd_info->cmd) {
	case DATA_CMD_INTR_COALESCE_GET:
		ret = mtk_dpmaif_cmd_intr_coalesce_get(dcb,
						       CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_INDIR_SIZE_GET:
		ret = drv_info->drv_ops->feature_cmd(drv_info,
						     DATA_HW_INDIR_SIZE_GET,
						     CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_HKEY_SIZE_GET:
		ret = drv_info->drv_ops->feature_cmd(drv_info,
						     DATA_HW_HASH_KEY_SIZE_GET,
						     CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_RXQ_NUM_GET:
		mtk_dpmaif_cmd_rxq_num_get(dcb, CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_CHANNELS_GET:
		mtk_dpmaif_cmd_channels_get(dcb, CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_STRING_CNT_GET:
		mtk_dpmaif_cmd_string_cnt_get(dcb, CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_STRING_GET:
		ret = mtk_dpmaif_describe_stats(dcb, (u8 *)CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_TRANS_DUMP:
		mtk_dpmaif_read_stats(dcb, (u64 *)CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_TXCSUM_SET:
		ret = drv_info->drv_ops->feature_cmd(drv_info, DATA_HW_TXCSUM_SET,
						     CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_MTU_SET:
		ret = mtk_dpmaif_enable_frag_dynamic_reload(dcb, *(int *)CMD_TO_DATA(cmd_info));
		break;
	default:
		MTK_WARN(DCB_TO_MDEV(dcb), "Unknown cmd type=%d\n", cmd_info->cmd);
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int mtk_dpmaif_send_cmd_hw(struct mtk_dpmaif_ctlb *dcb, struct mtk_data_cmd *cmd_info)
{
	struct dpmaif_drv_info *drv_info = dcb->drv_info;
	int ret;

	mtk_pm_runtime_get(DCB_TO_MDEV(dcb), MTK_USER_DATA, true);
	mtk_pm_ds_lock(DCB_TO_MDEV(dcb), MTK_USER_DATA);
	ret = mtk_pm_ds_wait_complete(DCB_TO_MDEV(dcb), MTK_USER_DATA);
	if (unlikely(ret < 0)) {
		MTK_WARN(DCB_TO_MDEV(dcb), "Failed to wait ds_lock\n");
		mtk_dpmaif_common_err_handle(dcb, true);
		goto out;
	}

	switch (cmd_info->cmd) {
	case DATA_CMD_TRANS_CTL:
		mtk_dpmaif_cmd_trans_ctl(dcb, CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_INTR_COALESCE_SET:
		ret = mtk_dpmaif_cmd_intr_coalesce_set(dcb, CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_RXFH_GET:
		ret = mtk_dpmaif_cmd_rxfh_get(dcb, CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_RXFH_SET:
		ret = mtk_dpmaif_cmd_rxfh_set(dcb, CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_GRO_HW_SET:
		ret = drv_info->drv_ops->feature_cmd(drv_info, DATA_HW_LRO_SET,
						     CMD_TO_DATA(cmd_info));
		break;
	case DATA_CMD_RXCSUM_SET:
		ret = drv_info->drv_ops->feature_cmd(drv_info, DATA_HW_RXCSUM_SET,
						     CMD_TO_DATA(cmd_info));
		break;
	default:
		MTK_WARN(DCB_TO_MDEV(dcb), "Unknown cmd type=%d\n", cmd_info->cmd);
		ret = -EOPNOTSUPP;
		break;
	}

out:
	mtk_pm_ds_unlock(DCB_TO_MDEV(dcb), MTK_USER_DATA);
	mtk_pm_runtime_put(DCB_TO_MDEV(dcb), MTK_USER_DATA, true);

	return ret;
}

static int mtk_dpmaif_send_cmd(struct mtk_dpmaif_ctlb *dcb, struct sk_buff *skb)
{
	struct mtk_data_cmd *cmd_info = SKB_TO_CMD(skb);
	int ret;

	if (dcb->dpmaif_state != DPMAIF_STATE_PWRON)
		return -EINVAL;

	if (cmd_info->cmd > DATA_CMD_HW_START)
		ret = mtk_dpmaif_send_cmd_hw(dcb, cmd_info);
	else
		ret = mtk_dpmaif_send_cmd_sw(dcb, cmd_info);

	return ret;
}

static int mtk_dpmaif_send(struct mtk_data_blk *data_blk, enum mtk_data_type type,
			   struct sk_buff *skb)
{
	struct mtk_dpmaif_ctlb *dcb;
	int ret;

	if (unlikely(!data_blk || !data_blk->dcb)) {
		pr_warn("Invalid parameter\n");
		return -EINVAL;
	}

	dcb = data_blk->dcb;

	if (unlikely(dcb->dpmaif_state == DPMAIF_STATE_PWROFF))
		return -EINVAL;

	if (likely(type == DATA_PKT))
		ret = mtk_dpmaif_send_pkt(dcb, skb);
	else
		ret = mtk_dpmaif_send_cmd(dcb, skb);

	return ret;
}

static void mtk_dpmaif_param_check(void)
{
	if (rx_poll_th > MAX_RX_POLL_TH)
		rx_poll_th = MAX_RX_POLL_TH;

	if (tx_poll_th > MAX_TX_POLL_TH)
		tx_poll_th = MAX_TX_POLL_TH;

	if (max_pit_burst_cnt > 2048)
		max_pit_burst_cnt = 2048;
	if (max_pit_burst_cnt < 64)
		max_pit_burst_cnt = 64;

	if (traffic_stats_shift > 9)
		traffic_stats_shift = 9;
	if (traffic_stats_shift < 3)
		traffic_stats_shift = 3;
}

static void mtk_dpmaif_link_exception(struct mtk_md_dev *mdev)
{
	struct mtk_dpmaif_ctlb *dcb = ((struct mtk_data_blk *)(mdev->data_blk))->dcb;

	set_bit(DATA_LINK_ERR, &dcb->err_event);
	MTK_INFO(DCB_TO_MDEV(dcb), "Link was exceptional\n");
}

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
static void mtk_dpmaif_flush_rx_wq(struct mtk_data_blk *data_blk)
{
	struct mtk_dpmaif_ctlb *dcb = data_blk->dcb;
	struct dpmaif_rxq *rxq;
	int i;

	for (i = 0; i < dcb->rxq_cnt; i++) {
		rxq = &dcb->rxqs[i];
		if (!(rxq->attr & DPMAIFQ_ATTR_LOW_LATENCY))
			flush_workqueue(rxq->steer_wq);
	}
}
#endif

static struct mtk_data_hif_ops pcie_data_ops = {
	.init = mtk_dpmaif_sw_init,
	.exit = mtk_dpmaif_sw_exit,
	.poll = mtk_dpmaif_rx_napi_poll,
	.select_txq = mtk_dpmaif_select_txq,
	.send = mtk_dpmaif_send,
	.stop = mtk_dpmaif_stop,
	.clear = mtk_dpmaif_clear,
	.start = mtk_dpmaif_start,
	.dump = mtk_dpmaif_dump,
	.link_exception = mtk_dpmaif_link_exception,
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	.flush_rxq = mtk_dpmaif_flush_rx_wq,
#endif
};

/**
 * mtk_pcie_data_init() - initialize data path
 * @mdev: pointer to mtk_md_dev
 *
 * Allocate and initialize all software resource of data transction layer and data port layer.
 *
 * Return: return value is 0 on success, a negative error code on failure.
 */
int mtk_pcie_data_init(struct mtk_md_dev *mdev)
{
	mtk_dpmaif_param_check();

	return mtk_data_init(mdev, &pcie_data_ops);
}

/**
 * mtk_pcie_data_exit() - deinitialize data path
 * @mdev: pointer to mtk_md_dev
 *
 * deinitialize and release all software resource of data transction layer and data port layer.
 *
 * Return: return value is 0 on success, a negative error code on failure.
 */
int mtk_pcie_data_exit(struct mtk_md_dev *mdev)
{
	return mtk_data_exit(mdev);
}

module_param(rx_poll_th, uint, 0444);
MODULE_PARM_DESC(rx_poll_th,
		 "This threshold(PIT speed) is used to enable polling register at rx done interrupt bottom-half, default 400(PITs/ms), max 1024(PITs/ms)");
module_param(tx_poll_th, uint, 0444);
MODULE_PARM_DESC(tx_poll_th,
		 "This threshold(DRB speed) is used to enable polling register at tx done interrupt bottom-half, default 200(DRBs/ms), max 512(DRBs/ms)");
module_param(max_pit_burst_cnt, uint, 0444);
MODULE_PARM_DESC(max_pit_burst_cnt,
		 "This max PIT burst release count, default 1024, max 2048");
module_param(traffic_stats_shift, uint, 0444);
MODULE_PARM_DESC(traffic_stats_shift,
		 "This is timer for traffic statistic, 1ms shift, default 5 (32ms), min 3 (8ms), max 9 (512ms)");
module_param(doorbell_reset_count, uint, 0644);
MODULE_PARM_DESC(doorbell_reset_count,
		 "The doorbell delay reset after specified doorbell count, used in TCP slow start stage, default 500");
module_param(recycle_rx_ring_th, uint, 0644);
MODULE_PARM_DESC(recycle_rx_ring_th,
		 "This threshold(recycle_rx_ring_th) is used to poll pit when recycle rx ring, default 128");
module_param(rx_legacy_mode, bool, 0444);
MODULE_PARM_DESC(rx_legacy_mode,
		 "Enable legacy mode for RX, disable page pool for RX, default is false");
