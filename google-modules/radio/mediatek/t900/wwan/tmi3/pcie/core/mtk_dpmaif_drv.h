/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_DPMAIF_DRV_H__
#define __MTK_DPMAIF_DRV_H__

#include <linux/bitops.h>

#include "mtk_dpmaif_ring.h"
#include "mtk_pcie_trace.h"

#define DPMAIF_RXQ_CNT_MAX 3
#define DPMAIF_TXQ_CNT_MAX 5
#define DPMAIF_IRQ_CNT_MAX 5
#define DPMAIF_BAT_NUM_MAX 2
#define DPMAIF_INTR_COALESCE_EN_TIME BIT(0)
#define DPMAIF_INTR_COALESCE_EN_PKT  BIT(1)

enum dpmaif_queue_attr {
	DPMAIFQ_ATTR_NONE = 0,
	DPMAIFQ_ATTR_LOW_LATENCY = BIT(0),
	DPMAIFQ_ATTR_PIT_CACHED = BIT(1),
};

enum dpmaif_napi_sta {
	NAPI_DONE,
	NAPI_RESCH,
};

enum dpmaif_drv_dir {
	DPMAIF_TX,
	DPMAIF_RX,
};

struct dpmaif_drv_pkt_info {
	unsigned char prio;
	__u32 skb_hash;
};

enum mtk_data_hw_feature_type {
	DATA_HW_F_LRO = BIT(0),
	DATA_HW_F_INDR_TBL = BIT(1),
	DATA_HW_F_INTR_COALESCE = BIT(2),
	DATA_HW_F_FRAG = BIT(3),
	DATA_HW_F_RXCSUM = BIT(4),
	DATA_HW_F_TXCSUM = BIT(5),
	DATA_HW_F_HASH = BIT(6),
	DATA_HW_F_HPC = BIT(7),
	DATA_HW_F_HPC_STATS = BIT(8),
	DATA_HW_F_AGG = BIT(9),
	DATA_HW_F_TRAS_ALIGN = BIT(10),
};

enum dpmaif_drv_cmd {
	DATA_HW_INTR_COALESCE_SET,
	DATA_HW_HASH_GET,
	DATA_HW_HASH_SET,
	DATA_HW_HASH_KEY_SIZE_GET,
	DATA_HW_INDIR_GET,
	DATA_HW_INDIR_SET,
	DATA_HW_INDIR_SIZE_GET,
	DATA_HW_LRO_SET,
	DATA_HW_RXCSUM_SET,
	DATA_HW_TXCSUM_SET,
	DATA_HW_FEATURES_GET,
	DATA_HW_CFG_GET,
	DATA_HW_TXQ_GET,
};

struct dpmaif_drv_intr {
	enum dpmaif_drv_dir dir;
	unsigned int q_mask;
	unsigned int mode;
	unsigned int pkt_threshold;
	unsigned int time_threshold;
};

enum mtk_drv_err {
	DATA_ERR_STOP_MAX = 10,
	DATA_HW_REG_TIMEOUT,
	DATA_HW_REG_CHK_FAIL,
	DATA_FLOW_CHK_ERR,
	DATA_DMA_MAP_ERR,
	DATA_DL_ONCE_MORE,
	DATA_PIT_SEQ_CHK_FAIL,
	DATA_LOW_MEM_TYPE_MAX,
	DATA_LOW_MEM_DRB,
	DATA_LOW_MEM_BAT,
	DATA_LOW_MEM_PIT,
	DATA_LOW_MEM_SKB,
	DATA_HW_UNK_PKT,
};

enum {
	DPMAIF_CLEAR_INTR,
	DPMAIF_UNMASK_INTR,
};

enum dpmaif_drv_dlq_id {
	DPMAIF_DLQ0 = 0,
	DPMAIF_DLQ1,
	DPMAIF_DLQ2,
	DPMAIF_DLQ_MAX
};

enum dpmaif_drv_bat_id {
	DPMAIF_BAT0 = 0,
	DPMAIF_BAT1,
};

enum dpmaif_drv_ring_type {
	DPMAIF_PIT,
	DPMAIF_BAT,
	DPMAIF_FRAG,
	DPMAIF_DRB,
};

enum dpmaif_drv_ring_idx {
	DPMAIF_PIT_WIDX,
	DPMAIF_PIT_RIDX,
	DPMAIF_BAT_WIDX,
	DPMAIF_BAT_RIDX,
	DPMAIF_FRAG_WIDX,
	DPMAIF_FRAG_RIDX,
	DPMAIF_DRB_WIDX,
	DPMAIF_DRB_RIDX,
};

struct dpmaif_drv_regs {
	unsigned long ao_base;
	unsigned long pd_base;
	unsigned long pd2_base;
	unsigned long ao_ul_ch0_sta;
};

struct dpmaif_tx_srv_cfg {
	unsigned char vq_cnt;
	const unsigned char *vqs;
	int nice;
};

struct dpmaif_tx_srvs_cfg {
	unsigned char tx_vq_cnt;
	unsigned char tx_srv_cnt;
	const struct dpmaif_tx_srv_cfg *tx_srvs;
};

struct dpmaif_txq_cfg {
	dma_addr_t drb_base;
	const unsigned int drb_cnt;
	unsigned int doorbell_delay;
	unsigned int burst_pkts;
	const unsigned int attr;
	unsigned int tx_coalesce_usecs; /* units: 512us */
	unsigned int tx_coalesced_frames;
};

struct dpmaif_tx_cfg {
	bool txq_all_enable;
	const unsigned char txq_cnt;
	struct dpmaif_txq_cfg *txqs;
};

struct dpmaif_rxq_cfg {
	dma_addr_t pit_base;
	const unsigned int pit_cnt;
	const unsigned int pit_seq_max;
	const unsigned char bat_ring_id;
	const unsigned char frag_ring_id;
	const unsigned int attr;
	const unsigned int qsize;
};

struct dpmaif_bat_cfg {
	dma_addr_t bat_base;
	const unsigned int bat_cnt;
	unsigned int buf_size;
	unsigned int reload_cnt;
	/* actual buffer count allocated during init */
	unsigned int real_reload_cnt;
};

enum dpmaif_dl_mode {
	/* one pit, one Legacy DL interrupt */
	DPMAIF_DL_M0,
	/* two pit, two DL interrupt  */
	DPMAIF_DL_M1,
};

struct dpmaif_rx_cfg {
	bool rxq_all_enable;
	unsigned int mtu;
	const unsigned int normal_bat_rsv_length;
	const unsigned int pkt_alignment;
	const unsigned char bat_ring_num;
	const unsigned char mode;
	struct dpmaif_bat_cfg *bats;
	const unsigned char frag_ring_num;
	struct dpmaif_bat_cfg *frags;
	const unsigned char rxq_cnt;
	const unsigned char indir_rxq_cnt;
	struct dpmaif_rxq_cfg *rxqs;
	/* Prefetch count required for DPMAIF Read */
	const unsigned char bat_wrap_cnt;
};

struct dpmaif_drv_info;
struct dpmaif_drv_intr_info;

struct dpmaif_irq_cfg {
	const unsigned int id;
#ifdef CONFIG_MTK_DATA_FEATURE_TEST
	int cpu_mask;
#endif
	int (*handle)(struct dpmaif_drv_info *drv_info, struct dpmaif_drv_intr_info *intr_info);
};

struct dpmaif_intr_cfg {
	u32 ul3_l2intrs_enable;
	u32 ul3_l2intrs_disable;
	u32 ul_l2intrs_enable;
	u32 ul_l2intrs_disable;
	u32 dl_l2intrs_enable;
	u32 dl_l2intrs_disable;
	u32 dl2_l2intrs_enable;
	u32 dl2_l2intrs_disable;
	u32 udl_ip_busy_disable;
	u32 hpc_disable;
	unsigned char irq_cnt;
	const struct dpmaif_irq_cfg *irqs;
};

struct dpmaif_dump_regs {
	char *name;
	u32 base_addr;
	u32 length;
};

struct dpmaif_dump_cfg {
	unsigned int cnt;
	const struct dpmaif_dump_regs *dump_regs;
};

struct dpmaif_drv_cfg {
	const u32 cap;
	const struct dpmaif_tx_srvs_cfg tx_srvs_cfg;
	struct dpmaif_tx_cfg tx_cfg;
	struct dpmaif_rx_cfg rx_cfg;
	const struct dpmaif_intr_cfg intr_cfg;
	const struct dpmaif_dump_cfg dump_cfg;
};

struct dpmaif_priv_ops {
	void (*set_pcie_domain)(struct dpmaif_drv_info *drv_info);
	u32 (*get_ul_intr_mask)(struct dpmaif_drv_info *drv_info);
	int (*dynamic_sram_init)(struct dpmaif_drv_info *drv_info);
	int (*ul_intr_init)(struct dpmaif_drv_info *drv_info);
	int (*mask_ulq_intr)(struct dpmaif_drv_info *drv_info, u32 q_num);
	void (*unmask_ulq_intr)(struct dpmaif_drv_info *drv_info, u32 q_num);
	void (*mask_ul_intr)(struct dpmaif_drv_info *drv_info, u32 mask);
	void (*set_hpc_cntl)(struct dpmaif_drv_info *drv_info);
	void (*set_dlq_timeout)(struct dpmaif_drv_info *drv_info);
	void (*clr_dlq_timeout)(struct dpmaif_drv_info *drv_info);
};

struct dpmaif_drv_info {
	struct mtk_md_dev *mdev;
	struct dpmaif_drv_ops *drv_ops;
	struct dpmaif_priv_ops *priv_ops;
	const struct dpmaif_drv_regs *regs;
	struct dpmaif_drv_cfg *cfg;
	u32 features;
};

enum dpmaif_drv_intr_type {
	DPMAIF_INTR_MIN = 0,
	/* uplink part */
	DPMAIF_INTR_UL_DONE,
	DPMAIF_INTR_UL_DRB_EMPTY,
	DPMAIF_INTR_UL_MD_NOTREADY,
	DPMAIF_INTR_UL_MD_PWR_NOTREADY,
	DPMAIF_INTR_UL_LEN_ERR,

	/* downlink part */
	DPMAIF_INTR_DL_LEGACY_DONE,
	DPMAIF_INTR_DL_SKB_LEN_ERR,
	DPMAIF_INTR_DL_BATCNT_LEN_ERR,
	DPMAIF_INTR_DL_PKT_EMPTY_SET,
	DPMAIF_INTR_DL_FRG_EMPTY_SET,
	DPMAIF_INTR_DL_MTU_ERR,
	DPMAIF_INTR_DL_FRGCNT_LEN_ERR,

	DPMAIF_INTR_DL_PITCNT_LEN_ERR,
	DPMAIF_INTR_DL_HPC_ENT_TYPE_ERR,
	DPMAIF_INTR_DL_DONE,

	/* traffic sync */
	DPMAIF_INTR_TRAS_SYNC,
	DPMAIF_INTR_MAX
};

#define DPMAIF_INTR_COUNT ((DPMAIF_INTR_MAX) - (DPMAIF_INTR_MIN) - 1)

struct dpmaif_drv_intr_info {
	unsigned char intr_cnt;
	enum dpmaif_drv_intr_type intr_types[DPMAIF_INTR_COUNT];
	/* it's a queue mask or queue index */
	u32 intr_queues[DPMAIF_INTR_COUNT];
};

/* This structure defines the management hooks for dpmaif devices. */
struct dpmaif_drv_ops {
	/* Initialize dpmaif hardware. */
	int (*init)(struct dpmaif_drv_info *drv_info, void *data);
	/* Start dpmaif hardware transaction and unmask dpmaif interrupt. */
	int (*start_queue)(struct dpmaif_drv_info *drv_info, enum dpmaif_drv_dir dir);
	/* Stop dpmaif hardware transaction and mask dpmaif interrupt. */
	int (*stop_queue)(struct dpmaif_drv_info *drv_info, enum dpmaif_drv_dir dir);
	/* Check, mask and clear the dpmaif interrupts,
	 * and then, collect interrupt information for data plane transaction layer.
	 */
	int (*intr_handle)(struct dpmaif_drv_info *drv_info, void *data, u8 irq_id);
	/* Unmask or clear dpmaif interrupt. */
	int (*intr_complete)(struct dpmaif_drv_info *drv_info, enum dpmaif_drv_intr_type type,
			     u8 q_id, u64 data);
	int (*send_doorbell)(struct dpmaif_drv_info *drv_info, enum dpmaif_drv_ring_type type,
			     u8 q_id, u32 cnt);
	int (*get_ring_idx)(struct dpmaif_drv_info *drv_info, enum dpmaif_drv_ring_idx index,
			    u8 q_id);
	int (*feature_cmd)(struct dpmaif_drv_info *drv_info, enum dpmaif_drv_cmd cmd, void *data);
	void (*dump)(struct dpmaif_drv_info *drv_info);
	int (*get_rx_info)(void *pit, struct dpmaif_rx_info *rx_info, u32 pit_seq_expect, u8 q_id);
	void (*fill_tx_info)(void *drb, struct dpmaif_tx_info *tx_info, int type);
};

static inline int mtk_dpmaif_drv_intr_handle(struct dpmaif_drv_info *drv_info,
					     void *data, u8 irq_id)
{
	trace_mtk_irq_entry(irq_id);
	return drv_info->drv_ops->intr_handle(drv_info, data, irq_id);
}

static inline int mtk_dpmaif_drv_intr_complete(struct dpmaif_drv_info *drv_info,
					       enum dpmaif_drv_intr_type type, u8 q_id, u64 data)
{
	trace_mtk_irq_exit(type, q_id);
	return drv_info->drv_ops->intr_complete(drv_info, type, q_id, data);
}

static inline int mtk_dpmaif_drv_send_doorbell(struct dpmaif_drv_info *drv_info,
					       enum dpmaif_drv_ring_type type, u8 q_id, u32 cnt)
{
	trace_mtk_data_doorbell(type, q_id, cnt);
	return drv_info->drv_ops->send_doorbell(drv_info, type, q_id, cnt);
}

struct dpmaif_drv_ops_desc {
	u32 hw_ver;
	struct dpmaif_drv_ops *drv_ops;
};

#define drv_ops_name(NAME) dpmaif_drv_ops_##NAME

#define MTK_DATA_IRQ_MEMLOG_RG(irq_id)  ((MTK_MEMLOG_RG_DATA_IRQ) + (irq_id))
#endif
