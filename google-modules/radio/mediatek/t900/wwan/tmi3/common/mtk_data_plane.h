/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_DATA_PLANE_H__
#define __MTK_DATA_PLANE_H__

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/skbuff.h>

#define DATA_DEFAULT_AFF_MODE 0
#define DATA_NETWORK_TYPE_MAX 7
#define SKB_TO_CMD(skb) ((struct mtk_data_cmd *)(skb)->data)
#define CMD_TO_DATA(cmd) (*(void **)(cmd)->data)
#define SKB_TO_CMD_DATA(skb) (*(void **)SKB_TO_CMD(skb)->data)

#define IPV4_VERSION 0x40
#define IPV6_VERSION 0x60
#define INVALID_PACKET_ID 0xFFFFFFFF

enum mtk_data_err_event {
	DATA_LINK_ERR = 0,
	DATA_HW_CHECK_ERR = 1,
	DATA_SW_CHECK_ERR = 2,
};

enum mtk_data_feature {
	DATA_F_GRO_HW = BIT(0),
	DATA_F_RXFH = BIT(1),
	DATA_F_INTR_COALESCE = BIT(2),
	DATA_F_RXCSUM = BIT(3),
	DATA_F_TXCSUM = BIT(4),
	DATA_F_MULTI_NETDEV = BIT(16),
	DATA_F_ETH_PDN = BIT(17),
};

/* DL queue number cannot be greater than this value */
#define MTK_DATA_NAPI_NR_MAX 3

struct mtk_data_trans_info {
	u32 cap;
	unsigned char rxq_cnt;
	unsigned char txq_cnt;
	unsigned int max_mtu;
	struct napi_struct **napis;
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
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

enum mtk_pkt_type {
	ETH_802_3,
	ETH_802_1Q,
	PURE_IP,
};

enum mtk_data_cmd_type {
	DATA_CMD_INTR_COALESCE_GET,
	DATA_CMD_INDIR_SIZE_GET,
	DATA_CMD_HKEY_SIZE_GET,
	DATA_CMD_RXQ_NUM_GET,
	DATA_CMD_CHANNELS_GET,
	DATA_CMD_STRING_CNT_GET,
	DATA_CMD_STRING_GET,
	DATA_CMD_TRANS_DUMP,
	DATA_CMD_TXCSUM_SET,
	DATA_CMD_MTU_SET,
	/* enum after DATA_CMD_HW_START will access HW */
	DATA_CMD_HW_START,
	DATA_CMD_TRANS_CTL,
	DATA_CMD_INTR_COALESCE_SET,
	DATA_CMD_RXFH_GET,
	DATA_CMD_RXFH_SET,
	DATA_CMD_GRO_HW_SET,
	DATA_CMD_RXCSUM_SET
};

struct mtk_data_intr_coalesce {
	unsigned int rx_coalesce_usecs;
	unsigned int tx_coalesce_usecs;
	unsigned int rx_coalesced_frames;
	unsigned int tx_coalesced_frames;
};

struct mtk_data_rxfh {
	unsigned int *indir;
	u8 *key;
};

struct mtk_data_channels {
	unsigned int max_rx;
	unsigned int rx_count;
};

struct mtk_data_trans_ctl {
	bool enable;
};

struct mtk_data_cmd {
	enum mtk_data_cmd_type cmd;
	unsigned int len;
	char data[];
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

#define DATA_SKB_CB(__skb) ((union mtk_data_pkt_info *)&((__skb)->cb[0]))

enum mtk_data_evt {
	DATA_EVT_MIN,
	DATA_EVT_TX_START,
	DATA_EVT_TX_STOP,
#ifndef CONFIG_DATA_GRO_WITHOUT_NAPI_POLL
	DATA_EVT_RX_START,
	DATA_EVT_RX_STOP,
#endif
#if defined(CONFIG_DATA_CPU_LOADING_OPTIMIZE) || defined(CONFIG_DATA_GRO_WITHOUT_NAPI_POLL)
	DATA_EVT_RX_FLUSH,
#endif
	DATA_EVT_REG_DEV,
	DATA_EVT_UNREG_DEV,
	DATA_EVT_DUMP,
	DATA_EVT_MAX,
};

enum mtk_data_pkt_type {
	PKT_UNKNOWN,
	PKT_EMPTY_ACK,
	PKT_ICMP,
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
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	void (*flush_rxq)(struct mtk_data_blk *data_blk);
#endif
};

int mtk_data_init(struct mtk_md_dev *mdev, struct mtk_data_hif_ops *ops);
int mtk_data_exit(struct mtk_md_dev *mdev);
u32 mtk_data_get_pkt_id(struct sk_buff *skb);

#endif /* __MTK_DATA_PLANE_H__ */
