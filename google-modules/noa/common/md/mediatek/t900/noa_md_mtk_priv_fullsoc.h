/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NOA Mediatek MD Private Full SoC header file
 *
 * Copyright (c) 2024 Google Inc.
 *
 */
#ifndef __NOA_MD_MTK_PRIV_FULLSOC_H__
#define __NOA_MD_MTK_PRIV_FULLSOC_H__

#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/workqueue.h>

// From mtk_drv.h
#define MTK_DEV_STR_LEN 16

enum mtk_user_id {
	MTK_USER_MIN,
	MTK_USER_CTRL,
	MTK_USER_DATA,
	MTK_USER_PM,
	MTK_USER_EXCEPT,
	MTK_USER_DEVLINK,
	MTK_USER_HW,
	MTK_USER_FRC,
#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
	MTK_USER_METRICS,
#endif /* CONFIG_METRICS_COLLECTION_FRAMEWORK */
#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_SUPPORT) || \
	IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
	MTK_USER_NOA,
#endif
	MTK_USER_MAX
};

// From mtk_trans_ctrl.h
#define HW_QUE_NUM 8
enum mtk_hif_id {
	CLDMA0,
	CLDMA1,
	CLDMA4,
	NR_CLDMA
};

// From mtk_data_plane.h
#define DATA_DEFAULT_AFF_MODE 0
#define IPV4_VERSION 0x40
#define IPV6_VERSION 0x60

#define MTK_DATA_NAPI_NR_MAX 3
#define MTK_DATA_BAT_NR_MAX 2

struct mtk_data_trans_info {
	u32 cap;
	unsigned char rxq_cnt;
	unsigned char txq_cnt;
	unsigned int max_mtu;
	struct napi_struct **napis;
#if IS_ENABLED(CONFIG_DATA_CPU_LOADING_OPTIMIZE)
	u8 *napi_thrd_aff;
#endif
	unsigned int rxq_attr[MTK_DATA_NAPI_NR_MAX];
};

struct mtk_data_blk {
	struct mtk_md_dev *mdev;
	struct mtk_wwan_ctlb *wcb;
	void *dcb;
	struct mtk_data_hif_ops *hif_ops;
	struct mtk_data_trans_info trans_info;
	bool exception_dup_stop;
};

enum mtk_data_type {
	DATA_PKT,
	DATA_CMD,
};

struct mtk_data_intr_coalesce {
	unsigned int rx_coalesce_usecs;
	unsigned int tx_coalesce_usecs;
	unsigned int rx_coalesced_frames;
	unsigned int tx_coalesced_frames;
};

struct mtk_tx_pkt_info {
	unsigned char intf_id;
	unsigned char cnt;
	unsigned char network_type;
	bool in_tcp_slow_start;
	unsigned char q_id;
	u32 id;
};

struct mtk_rx_pkt_info {
	unsigned char ch_id;
	unsigned char q_id;
	u32 id;
};

union mtk_data_pkt_info {
	struct mtk_tx_pkt_info tx;
	struct mtk_rx_pkt_info rx;
};

enum mtk_data_pkt_prio {
	PKT_PRIO_0 = 0, /* lowest priority, Normal pkt. keep value is 0(dscp tbl used) */
	PKT_PRIO_1 = 1, /* lower priority, Echo pkt */
	PKT_PRIO_2 = 2, /* higher priority, ACK pkt */
	PKT_PRIO_3 = 3, /* highest priority, IMS pkt */
	PKT_PRIO_MAX
};

enum mtk_data_queue_attr {
	DATAQ_ATTR_LOW_LATENCY = BIT(0),
};

struct mtk_data_hif_ops {
	int (*init)(struct mtk_md_dev *mdev);
	int (*exit)(struct mtk_md_dev *mdev);
	int (*stop)(struct mtk_md_dev *mdev);
	void (*clear)(struct mtk_md_dev *mdev);
	int (*start)(struct mtk_md_dev *mdev);
	void (*dump)(struct mtk_md_dev *mdev);
	int (*poll)(struct napi_struct *napi, int budget);
	int (*select_txq)(struct mtk_data_blk *data_blk, struct sk_buff *skb,
			  enum mtk_data_pkt_prio pkt_prio);
	int (*send)(struct mtk_data_blk *data_blk, enum mtk_data_type type,
		    struct sk_buff *skb);
	void (*link_exception)(struct mtk_md_dev *mdev);
#if IS_ENABLED(CONFIG_DATA_CPU_LOADING_OPTIMIZE)
	void (*flush_rxq)(struct mtk_data_blk *data_blk);
#endif
};

#define DATA_SKB_CB(__skb) ((union mtk_data_pkt_info *)&((__skb)->cb[0]))

enum mtk_data_evt {
	DATA_EVT_MIN,
	DATA_EVT_TX_START,
	DATA_EVT_TX_STOP,
	DATA_EVT_RX_START,
	DATA_EVT_RX_STOP,
#if IS_ENABLED(CONFIG_DATA_CPU_LOADING_OPTIMIZE)
	DATA_EVT_RX_FLUSH,
#endif
	DATA_EVT_REG_DEV,
	DATA_EVT_UNREG_DEV,
	DATA_EVT_DUMP,
	DATA_EVT_MAX,
};

// From mtk_dpmaif_ring.h
/* buffer type */
enum dpmaif_bat_type {
	NORMAL_BAT = 0,
	FRAG_BAT = 1,
};

/* RX: buffer address table */
struct dpmaif_bat {
	__le32 buf_addr_low;
	__le32 buf_addr_high;
};

struct dpmaif_rx_info {
	u32 pit_pd_seq;
	u32 msg_pit;
	u32 pit_msg_chnl_id;
	u32 pit_msg_checksum;
	u32 pit_msg_err;
	u32 pit_msg_dp;
	u32 pit_msg_hash;
	u32 pit_msg_pro;
	u32 pit_msg_ip;
	u32 normal_bat;
	u32 pit_pd_cur_bid;
	u32 pit_pd_data_len;
	u32 pit_pd_hd_offset;
	u32 pit_continue;
	u64 pit_pd_dma_addr;
};

struct dpmaif_pd_pit {
	__le32 pd_header;
	__le32 addr_low;
	__le32 addr_high;
	__le32 pd_footer;
};

struct dpmaif_msg_pit {
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
	__le32 dword4;
};

struct dpmaif_tx_info {
	u32 msg_pkt_len;
	u16 msg_count_l;
	u16 msg_network_type;
	u8 msg_channel_id;
	u8 msg_txcsum;
	dma_addr_t pd_data_dma_addr;
	u32 pd_data_len;
	u8 pd_is_last;
#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_SUPPORT)
	u8 in_tcp_slow_start;
#endif
};

struct dpmaif_msg_drb {
	__le32 msg_header1;
	__le32 msg_header2;
	__le32 msg_rsv1;
	__le32 msg_rsv2;
};

/* drb->type */
enum dpmaif_drb_type {
	PD_DRB,
	MSG_DRB
};

static inline unsigned int mtk_dpmaif_ring_buf_get_next_idx(unsigned int buf_len,
							    unsigned int buf_idx)
{
	buf_idx++;

	return buf_idx < buf_len ? buf_idx : 0;
}

// From mtk_dpmaif_drv.h
#define DPMAIF_RXQ_CNT_MAX 3
#define DPMAIF_TXQ_CNT_MAX 5
#define DPMAIF_IRQ_CNT_MAX 5
#define DPMAIF_BAT_NUM_MAX 2

enum dpmaif_drv_dir {
	DPMAIF_TX,
	DPMAIF_RX,
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

enum dpmaif_drv_bat_id {
	DPMAIF_BAT0 = 0,
	DPMAIF_BAT1,
};

enum dpmaif_drv_ring_type {
	DPMAIF_PIT,
	DPMAIF_BAT,
	DPMAIF_FRAG,
	DPMAIF_DRB,
	NOA_FREE_POOL,
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

// From mtk_pci.h
enum mtk_irq_src {
	MTK_IRQ_SRC_INVALID = -1,
	MTK_IRQ_SRC_MIN,
	MTK_IRQ_SRC_MHCCIF,
	MTK_IRQ_SRC_SAP_RGU,
	MTK_IRQ_SRC_DPMAIF,
	MTK_IRQ_SRC_DPMAIF2,
	MTK_IRQ_SRC_CLDMA0,
	MTK_IRQ_SRC_CLDMA1,
	MTK_IRQ_SRC_CLDMA2,
	MTK_IRQ_SRC_CLDMA3,
	MTK_IRQ_SRC_PM_LOCK,
	MTK_IRQ_SRC_DPMAIF3,
	MTK_IRQ_SRC_ADO,
	MTK_IRQ_SRC_CLDMA4,
	MTK_IRQ_SRC_DPMAIF6,
	MTK_IRQ_SRC_TRAS_SYNC,
	MTK_IRQ_SRC_MAX
};

// From mtk_dev.h
struct mtk_md_dev {
	struct device *dev;
	const struct mtk_dev_ops *dev_ops;
	void *hw_priv;
	u32 hw_ver;
	char dev_str[MTK_DEV_STR_LEN];
	void *fsm;
	void *ctrl_blk;
	void *data_blk;
	void *devlink;
	struct dentry *dev_dentry; /* For debug */
	void *bm_ctrl;
	void *memlog;
	struct mtk_utility_cfg *utility_cfg;
};

// From mtk_pm.h
struct mtk_pm_entity {
	struct list_head entry;
	enum mtk_user_id user;
	unsigned long flag;
	void *param;

	int (*suspend)(struct mtk_md_dev *mdev, void *param, bool is_runtime);
	int (*suspend_late)(struct mtk_md_dev *mdev, void *param, bool is_runtime);
	int (*resume_early)(struct mtk_md_dev *mdev, void *param, bool is_runtime, bool link_ready);
	int (*resume)(struct mtk_md_dev *mdev, void *param, bool is_runtime, bool link_ready);
};
#endif
