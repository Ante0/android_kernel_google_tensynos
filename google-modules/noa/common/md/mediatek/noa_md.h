/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD Driver
 *
 * Copyright 2024 Google LLC.
 */
#ifndef __NOA_MD_H__
#define __NOA_MD_H__

#include <linux/cdev.h>
#include <linux/ip.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <net/ipv6.h>

/* NOA related header */
#include "common/ring.h"
#include "common/core.h"
#include "common/md/mediatek/noa_md_mtk_common.h"
#include "common/md/mediatek/noa_md_shmem_layout.h"
#include "common/modem_ring_id.h"
#include "sim/nep/nep.h"
#include "sim/nep/port.h"
#include "sim/nep/ring_manager.h"

/* NOA modem related header */
#include "noa_md_dma_mapper.h"
#include "noa_md_dpa.h"
#include "noa_md_trace.h"
#include "noa_md_tx_data.h"
#include "noa_md_rx_data.h"
#include "noa_md_shmem_sync.h"
#include "noa_md_cldma.h"
#include "t900/noa_md_mtk_priv.h"

/* NOA modem debugfs related header */
#include "debugfs/noa_md_debug_ctl.h"

/* DPA related header */
#include "soc/google/google_dpa.h"
#include "soc/google/google_dpa_doorbell.h"
#include "soc/google/google_dpa_ctrl.h"
#include "soc/google/google_dpa_service_modem_cmd.h"  /* For struct noa_md_fw_init */

#ifdef MD_MODULE_VERSION
#define NOA_MD_MODULE_VERSION __stringify(MD_MODULE_VERSION)
#else
#define NOA_MD_MODULE_VERSION "0.1.0-alpha"
#endif
#define NOA_MD_DEVICE_NAME "noa_md"
#define PCIE_DMA_MASK 64
#define NOA_PIT_CNT_UPDATE_THRESHOLD 1024
#define NOA_MD_FW_FIFO_SIZE 880
#define NOA_MD_FW_TX_POOL_TKID_OFFSET ((u16)(0x8000U))
#define NOA_MD_MAX_TX_PKT_SIZE 2048

struct dpmaif_irq_data {
	enum dpmaif_drv_intr_type intr_type;
	unsigned int q_mask;
};

struct noa_md_fw_ring {
	/* Ring buffer management */
	struct noa_ring_wrapper ring;
	/* DMA mapping information */
	void *desc_base;
	dma_addr_t dma_addr;
	/* Ring metadata */
	uint32_t num_desc;
	uint32_t virtual_write_idx;
	/* Concurrency control */
	spinlock_t lock;
};

struct noa_tx_queue {
	atomic_t to_submit_cnt;
	unsigned char id;
	struct dpmaif_pd_drb *drb_base;
	u16 *tkid_queue;
	unsigned int drb_cnt;
	unsigned short drb_wr_idx;
	unsigned short drb_rd_idx;
	unsigned short drb_temp_rd_idx;
	unsigned short drb_rel_rd_idx;
	unsigned int burst_submit_cnt;
	unsigned int db_delay_ms;
	unsigned int exit_tcp_ss_counter;
	unsigned int send_drb_cnt;
	atomic_t drb_stats;
	bool drb_poll_enable;
	bool drb_poll_mode;
	struct delayed_work tx_done_work;
	struct delayed_work doorbell_work;
};

struct noa_md_fw_tx {
	struct noa_md_fw_ring tx_ring;
	uint32_t txq_cnt;
	struct noa_tx_queue *txqs;                /* TX queue array for NOA */
	struct noa_dpmaif_tx_queue *dpmaif_txqs;  /* TX queue array for dpmaif */
	/* TODO: Add VPN queues array structure */
	unsigned long ints_reg_addr;              /* Hardware register addresses */
	struct tasklet_struct md_tx_task;         /* Tasklet for handling TX completion */
	struct tasklet_struct apc2ncp_task;       /* Tasklet for handling apc2ncp drb isr */
	uint32_t isr_drb_index;
	uint32_t isr_queue_mask;
	uint32_t remain_queue_mask;
	struct mutex read_desc_lock;
	const struct modem_fw_ring_ops *ring_ops;
};

struct noa_rx_queue {
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
	struct dpmaif_rx_record rx_record;
	unsigned int intr_coalesce_frame;
	/* Records the latest BID polled by this DLQ pit ring */
	unsigned int pit_bid;
	unsigned char bat_ring_id;
	unsigned int pit_seq_max;
	struct dpmaif_rx_info *rx_info;
	unsigned int attr;
	struct wakeup_source *ws;
	struct tasklet_struct ncp_md_rx_done_task;
	uint64_t pit_dpa_base;
	uint64_t noa_pit_dpa_base;
};

struct noa_rx_tkid_free_pool {
	unsigned short rx_tkid;
	struct dpmaif_bat bat;
	u64 noa_data_addr;
};

struct noa_rx_tkid_info {
	unsigned short *rx_tkid;
	spinlock_t rx_tkid_lock;
	struct noa_rx_tkid_free_pool *free_pool;
	unsigned short rx_tkid_free_fore;
	unsigned short rx_tkid_free_rear;
};

struct noa_rx_data_addr {
	u64 noa_va;
};

struct noa_bat_ring {
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
	struct noa_rx_tkid_info rx_tkid_info;
	struct noa_rx_data_addr *noa_data_addr;
	struct noa_rx_data_addr *noa_data_addr_apc;
};

struct noa_bat_info {
	unsigned int max_mtu;
	bool frag_bat_enabled;
	struct noa_bat_ring normal_bat_ring;
	struct noa_bat_ring frag_bat_ring;
};

struct noa_md_fw_rx {
	struct noa_md_fw_ring *rx_ring;
	uint32_t bat_ring_num;
	struct noa_bat_info *bat_infos;
	uint32_t rxq_cnt;
	struct noa_rx_queue *rxqs;
	struct noa_rx_queue *dpmaif_rxqs;
	unsigned long doorbell_reg_addr;
	const struct modem_fw_ring_ops *ring_ops;
	struct tasklet_struct rx_reload_task;
	struct tasklet_struct rx_tkid_free_poll_task;
	uint32_t isr_refill_index;
};

struct noa_tx_buffer_pool{
	void *va_base;
	dma_addr_t pa_base;
	noa_ring_producer ring;
	dma_addr_t dma_base;
	void *dpa_base;
};

struct noa_md_fw {
	/* Device and driver information */
	struct device *dev;
	struct dpmaif_drv_info *drv_info;
	struct dpmaif_drv_info *noa_drv_info;
	/* TX and RX data paths */
	struct noa_md_fw_tx *tx;
	struct noa_md_fw_rx *rx;
	struct noa_tx_buffer_pool tx_buffer_pool;
	struct timer_list ring_rel_ctrl_timer;
};

struct modem_fw_ring_ops {
	int (*begin_processing)(struct noa_ring_wrapper *ring);
	int (*complete_processing)(struct noa_ring_wrapper *ring);
	int (*read)(struct noa_ring_wrapper *ring, void *data, size_t len);
	int (*write)(struct noa_ring_wrapper *ring, void *data, size_t len);
	int (*tail_inc)(struct noa_ring_wrapper *ring);
	bool (*is_empty)(struct noa_ring_wrapper *ring);
	int (*init)(struct noa_md_fw *md_fw);
	void (*exit)(struct noa_md_fw *md_fw, int ring_type);
	int (*activate)(struct noa_ring_wrapper *ring, int ring_type);
	const char *(*get_name)(struct noa_ring_wrapper *ring);
};

struct noa_md_feature_ctrl {
	bool enabled;
	bool tx_enabled;
	bool rx_enabled;
	bool unified_desc_enabled;
	bool tx_tcp_slow_start_enabled;
};

enum noa_dpmaif_ring_state {
	NOA_DPMAIF_RING_STATE_UNAVAILABLE,
	NOA_DPMAIF_RING_STATE_READY,
	NOA_DPMAIF_RING_STATE_DONE,
	NOA_DPMAIF_RING_STATE_MAX,
};

/* Describes the layout of shared memory between AP and NCP. */
struct noa_md_shmem_handle {
	void *va_base;
	dma_addr_t dma_base;
	u64 pa_base;
};

struct noa_md_dev {
	/* Device and driver management */
	dev_t dev_num;
	struct class *cls;
	struct device *dev;
	struct mtk_md_dev *mdev;
	struct cldma_dev *cldma_dev;
	struct mtk_dpmaif_ctlb *dpmaif_dcb;
	struct mtk_dpmaif_ctlb *noa_dcb;
	/* Current dcb */
	struct mtk_dpmaif_ctlb *dcb;

	struct mtk_data_blk *data_blk;

	/* Netdev information */
	spinlock_t netdev_update_lock;
	unsigned short netdev_cnt;
	bool wwan_notifier_ready;
	struct net_device *netdevs[MTK_NETDEV_MAX];
	struct mtk_wwan_instance *wwan_inst[MTK_NETDEV_MAX];

	/* TX and RX data paths */
	struct noa_md_tx tx;
	struct noa_md_rx rx;
	struct noa_md_cldma cldma;

	/* Modem firmware information */
	struct noa_md_fw *md_fw;

	/* Trace and logging */
	struct noa_md_trace *trace;
	struct noa_md_trace_log *md_tlog;
	struct noa_md_trace_log *ncp_tlog;

	/* Feature control */
	struct noa_md_feature_ctrl feature_ctrl;

	/* DPA resources */
	struct noa_md_dpa *dpa_res;

	/* SMMU mapping for shared resources like control structures */
	struct noa_md_dma_mapper mapper;

	/* Data path switching controller */
	struct noa_md_dpath_ctrl *dpath_ctrl;

	/* Shared memory synchronization handle */
	struct noa_md_shmem_sync_handle *shmem_sync;

	/* NOA FW initialization members */
	enum noa_dpmaif_ring_state dpmaif_ring_state;
	enum dpa_state dpa_state;
	struct mutex fw_init_state_lock;
	struct noa_md_dpa_state_client *dpa_state_client;
	struct noa_md_fw_init fw_info;

	struct noa_tx_buffer_pool tx_buffer_pool;

	atomic_t dpa_napi_enabled;

	struct noa_md_shmem_handle shmem_handle;

	/* Debug control flags */
	struct noa_md_debug_control debug_ctl;
};

extern struct noa_md_dev md_dev;

/* DPMAIF */
void noa_md_dpmaif_start(void *data);
void noa_md_cldma_init(void *data);
void noa_md_cldma_exit(void *data);
void noa_md_cldma_dev_init(void *data);
void noa_md_cldma_dev_exit(void *data);
void noa_md_cldma_open(void *data);
void noa_md_cldma_close(void *data);
void noa_md_dpmaif_stop(void *data);
void noa_md_dpmaif_sw_init(void *data);
void noa_md_dpmaif_sw_reset(void *data);
void noa_md_dpmaif_sw_exit(void *data);
void noa_md_dpmaif_sw_init_script(void *data);

/* WWAN */
void noa_md_wwan_init(void *data);
void noa_md_wwan_setup(void *data);
void noa_md_wwan_open(void *data);
void noa_md_wwan_stop(void *data);
void noa_md_wwan_exit(void *data);
void noa_md_wwan_data_event(void *data);
void noa_md_netdev_update(void *data);

/* PM */
int noa_md_pm_suspend(void *data);
int noa_md_pm_suspend_late(void *data);
int noa_md_pm_resume_early(void *data);
int noa_md_pm_resume(void *data);

/* WWAN data transmit */
int noa_md_data_irq_handle(void *data);

/* Status sync */
int noa_md_dpmaif_stats_sync(void *data);

/* PCIE probe done */
void noa_md_pcie_probe_done(void *data);

/* Initialize and release */
int noa_md_init(void);
void noa_md_exit(void);
void noa_md_ring_service_tx_activate(bool activate);
void noa_md_ring_service_rx_activate(bool activate);
int noa_md_rx_ring_setup_activate(struct noa_md_dev *p_md_dev);
int noa_md_tx_ring_setup_activate(struct noa_md_dev *p_md_dev);
/**
 * noa_md_rx_bat_rings_init_from_dpmaif() - Init NCP RX BAT rings from DPMAIF
 * state.
 * @p_md_dev: Pointer to the main NOA modem device structure.
 *
 * Populates NCP software view of RX BAT rings based on the initial
 * state of the hardware-facing DPMAIF rings. This is a one-time setup
 * to synchronize buffer ownership before the data path is active.
 *
 * Context: Can be called during driver initialization.
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_md_rx_bat_rings_init_from_dpmaif(struct noa_md_dev* p_md_dev);
/**
 * noa_md_rx_tkid_to_bat_index() - Translates a global RX token ID to a
 * local BAT index.
 * @bat_id:        The ID of the Buffer Address Table (0 or 1).
 * @bat_type:      The type of BAT (NORMAL_BAT or FRAG_BAT).
 * @rx_tkid:       The global RX token ID to translate.
 *
 * This function decodes a global rx_tkid into a zero-based local index
 * for a specific BAT by subtracting the BAT's known token ID base address.
 * Error-checking branches are marked as unlikely to optimize the hot path.
 *
 * Context:         Can be called from any context, including atomic.
 * Cannot sleep.
 * Return:          The local BAT index on success, or NOA_MD_RX_INVALID_BAT_INDEX
 * on failure (e.g., invalid parameters or out-of-range tkid).
 */
unsigned short noa_md_rx_tkid_to_bat_index(int bat_id, int bat_type,
	unsigned short rx_tkid);

#endif /* __NOA_MD_H__ */
