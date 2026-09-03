/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NOA MD MTK T900 Private header file for DPMAIF
 *
 * This file synchronizes with MTK T900 mtk_dpmaif.c for
 * the definition and structures.
 *
 * Copyright (c) 2025 Google Inc.
 *
 */
#ifndef __NOA_WWAN_MTK_PRIV_DPMAIF_H__
#define __NOA_WWAN_MTK_PRIV_DPMAIF_H__

#include <linux/netdevice.h>
#include <linux/sched/clock.h>

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
#include "noa_md_mtk_priv_fullsoc.h"
#else
#include "mtk_dpmaif_drv.h"
#include "mtk_pci.h"
#include "mtk_pm.h"
#endif

#include "noa_md_mtk_priv_dpmaif_wwan.h"

#define DCB_TO_DEV(dcb) ((dcb)->data_blk->mdev->dev)
#define DCB_TO_MDEV(dcb) ((dcb)->data_blk->mdev)

#define DPMAIF_PIT_SEQ_CHECK_FAIL_CNT 2500

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

#if IS_ENABLED(CONFIG_DATA_CPU_LOADING_OPTIMIZE)
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

#if IS_ENABLED(CONFIG_DATA_CPU_LOADING_OPTIMIZE)
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

#if IS_ENABLED(CONFIG_DATA_CPU_LOADING_OPTIMIZE)
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

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *dpmaif_dir;
#endif
#ifdef CONFIG_DATA_TEST_MODE
	struct dpmaif_test_mode_cfg test_mode_cfg;
#endif
	struct dpmaif_tput_stats tput_stats;
#if IS_ENABLED(CONFIG_DATA_CPU_LOADING_OPTIMIZE)
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

#endif // __NOA_WWAN_MTK_PRIV_DPMAIF_H__
